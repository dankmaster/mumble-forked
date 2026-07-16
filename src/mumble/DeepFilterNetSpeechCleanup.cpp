// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetSpeechCleanup.h"

#include "DeepFilterNetRealtimeWorker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {
	struct DFState;
	// DeepFilterNet is materially less speech-preserving when a quiet microphone
	// reaches the model at roughly -50 dBFS or below (the downstream Mumble AGC
	// intentionally runs after input enhancement). Normalize only the model
	// domain, then undo the exact gain on the causally corresponding output. The
	// user's PCM level and the delayed dry path therefore remain unchanged.
	constexpr float modelDomainNominalGain = 8.0f; // +18.06 dB
	constexpr float modelDomainPeakLimit   = 0.95f;

	using DfCreateFn            = DFState *(*)(const char *path, float attenLim, const char *logLevel);
	using DfGetFrameLengthFn    = std::size_t (*)(DFState *state);
	using DfGetLatencyFn        = std::size_t (*)(DFState *state);
	using DfProcessFrameV2Fn    = int (*)(DFState *state, const float *input, float *output, float *localSnr);
	using DfSetAttenLimFn       = void (*)(DFState *state, float limitDb);
	using DfSetPostFilterBetaFn = void (*)(DFState *state, float beta);
	using DfFreeFn              = void (*)(DFState *state);

	struct DeepFilterNetProfile {
		QString modelId;
		float attenuationLimitDb = 48.0f;
		float postFilterBeta     = 0.0f;
		bool usesLowLatencyModel = false;
	};

	DeepFilterNetProfile profileForSelection(const Mumble::SpeechCleanup::Selection &selection) {
		const QString normalizedModelId =
			Mumble::SpeechCleanup::normalizedModelId(selection.backend, selection.modelId);

		if (normalizedModelId == QLatin1String("deepfilternet:gentle")) {
			return { normalizedModelId, 24.0f, 0.0f, false };
		}
		if (normalizedModelId == QLatin1String("deepfilternet:low-latency")) {
			return { normalizedModelId, 48.0f, 0.0f, true };
		}
		if (normalizedModelId == QLatin1String("deepfilternet:maximum")) {
			return { normalizedModelId, 100.0f, 0.0f, false };
		}
		if (normalizedModelId == QLatin1String("deepfilternet:maximum-postfilter")) {
			return { normalizedModelId, 100.0f, 0.05f, false };
		}

		return { normalizedModelId, 48.0f, 0.0f, false };
	}

	QStringList candidateLibraryPaths() {
		const QString appDir = QCoreApplication::applicationDirPath();
		return {
			QDir(appDir).filePath(QStringLiteral("deepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("libdeepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("deepfilternet/deepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("deepfilternet/libdeepfilter.dll"))
		};
	}

	QString resolveModelPath(const DeepFilterNetProfile &profile, QString &activeModelId, bool &usedFallback) {
		const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("deepfilternet"));
		const QString standardModelPath = QDir(baseDir).filePath(QStringLiteral("DeepFilterNet3_onnx.tar.gz"));
		const QString lowLatencyModelPath = QDir(baseDir).filePath(QStringLiteral("DeepFilterNet3_ll_onnx.tar.gz"));

		activeModelId.clear();
		usedFallback = false;

		if (profile.usesLowLatencyModel) {
			if (QFileInfo::exists(lowLatencyModelPath)) {
				activeModelId = profile.modelId;
				return lowLatencyModelPath;
			}
			if (QFileInfo::exists(standardModelPath)) {
				activeModelId = QStringLiteral("deepfilternet:default");
				usedFallback = true;
				return standardModelPath;
			}

			return {};
		}

		if (QFileInfo::exists(standardModelPath)) {
			activeModelId = profile.modelId;
			return standardModelPath;
		}

		return {};
	}
} // namespace

class DeepFilterNetSpeechCleanup::Implementation {
public:
	explicit Implementation(const Mumble::SpeechCleanup::Selection &selection)
		: m_profile(profileForSelection(selection)), m_modelDomainNormalization(selection.modelDomainNormalization) {
#ifdef USE_DEEPFILTERNET
		m_modelPath = resolveModelPath(m_profile, m_activeModelId, m_usedFallback);
		if (m_modelPath.isEmpty()) {
			qWarning("DeepFilterNetSpeechCleanup: no DeepFilterNet model archive found");
			return;
		}

		for (const QString &candidate : candidateLibraryPaths()) {
			m_library.setFileName(candidate);
			if (m_library.load()) {
				break;
			}
		}

		if (!m_library.isLoaded()) {
			qWarning("DeepFilterNetSpeechCleanup: failed to load deepfilter runtime library");
			return;
		}

		m_create            = reinterpret_cast< DfCreateFn >(m_library.resolve("df_create"));
		m_getFrameLength    = reinterpret_cast< DfGetFrameLengthFn >(m_library.resolve("df_get_frame_length"));
		m_getLatency        = reinterpret_cast< DfGetLatencyFn >(m_library.resolve("df_get_latency"));
		m_processFrameV2    = reinterpret_cast< DfProcessFrameV2Fn >(m_library.resolve("df_process_frame_v2"));
		m_setAttenLim       = reinterpret_cast< DfSetAttenLimFn >(m_library.resolve("df_set_atten_lim"));
		m_setPostFilterBeta = reinterpret_cast< DfSetPostFilterBetaFn >(m_library.resolve("df_set_post_filter_beta"));
		m_free              = reinterpret_cast< DfFreeFn >(m_library.resolve("df_free"));

		if (!m_create || !m_getFrameLength || !m_getLatency || !m_processFrameV2 || !m_setAttenLim
			|| !m_setPostFilterBeta || !m_free) {
			qWarning("DeepFilterNetSpeechCleanup: deepfilter runtime library is missing the fail-closed v2 C API");
			m_library.unload();
			return;
		}

		if (!initializeState()) {
			return;
		}

		m_ready = true;
		qInfo("DeepFilterNetSpeechCleanup: Initialized backend=DeepFilterNet requestedModelId=%s activeModelId=%s "
			  "modelPath=\"%s\" attenuationLimitDb=%.1f postFilterBeta=%.2f usedFallback=%s",
			  qUtf8Printable(m_profile.modelId), qUtf8Printable(m_activeModelId), qUtf8Printable(m_modelPath),
			  m_profile.attenuationLimitDb, m_profile.postFilterBeta, m_usedFallback ? "true" : "false");
#else
		(void) selection;
#endif
	}

	~Implementation() {
#ifdef USE_DEEPFILTERNET
		m_realtimeWorker.stop();
		releaseState();
		if (m_library.isLoaded()) {
			m_library.unload();
		}
#endif
	}

	bool isReady() const {
		return m_ready && (!m_realtimePrepared || m_realtimeWorker.healthy());
	}

	void reset() {
#ifdef USE_DEEPFILTERNET
		m_realtimeWorker.stop();
		m_realtimePrepared = false;
		if (!m_library.isLoaded()) {
			m_ready = false;
			return;
		}

		releaseState();
		m_ready = initializeState();
#endif
	}

	bool prepareRealtime() {
#ifdef USE_DEEPFILTERNET
		if (!m_ready || !m_state || !m_processFrameV2
			|| m_frameLength != DeepFilterNetRealtimeWorker::frameSamples) {
			m_ready = false;
			return false;
		}

		m_latencySamples = m_realtimeLatencySamples;
		m_dryDelay.assign(m_latencySamples, 0.0f);
		m_dryDelayPosition = 0;
		m_realtimeOutputFrame.fill(0.0f);
		m_realtimePrepared = m_realtimeWorker.start(this, &Implementation::processRealtimeFrame);
		if (!m_realtimePrepared) {
			m_ready = false;
		}
		return m_realtimePrepared;
#else
		return false;
#endif
	}

	bool prepareOfflineFrame() noexcept {
#ifdef USE_DEEPFILTERNET
		return m_ready && (!m_realtimePrepared || m_realtimeWorker.prepareOfflineFrame());
#else
		return false;
#endif
	}

	bool finishOfflineProcessing() noexcept {
#ifdef USE_DEEPFILTERNET
		return m_ready && (!m_realtimePrepared || m_realtimeWorker.finishOfflineProcessing());
#else
		return false;
#endif
	}

	unsigned int latencySamples() const {
		return m_latencySamples;
	}

	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
#ifdef USE_DEEPFILTERNET
		if (!m_ready || !samples || sampleCount == 0) {
			return;
		}
		if (m_realtimePrepared) {
			processRealtimeInPlace(samples, sampleCount, mixFactor);
			return;
		}

		mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
		const float dryFactor = 1.0f - mixFactor;
		for (unsigned int i = 0; i < sampleCount; ++i) {
			const float inputSample = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
			const float rawCleanedSample = m_outputFramePosition < m_frameLength
										   ? m_outputFrame[m_outputFramePosition++]
										   : 0.0f;
			const float cleanedSample = std::isfinite(rawCleanedSample) ? rawCleanedSample : 0.0f;
			const float delayedDry = m_dryDelay[m_dryDelayPosition];
			m_dryDelay[m_dryDelayPosition] = inputSample;
			m_dryDelayPosition = (m_dryDelayPosition + 1) % m_dryDelay.size();

			m_inputFrame[m_inputFrameSize++] = inputSample;
			if (m_inputFrameSize == m_frameLength) {
				float localSnr   = std::numeric_limits< float >::quiet_NaN();
				const int status = processModelFrame(m_inputFrame.data(), m_outputFrame.data(), &localSnr);
				m_inputFrameSize = 0;
				m_outputFramePosition = 0;
				if (status != DeepFilterNetRealtimeWorker::statusOk) {
					std::fill(m_outputFrame.begin(), m_outputFrame.end(), 0.0f);
					m_ready = false;
				}
			}

			const float blendedSample = cleanedSample * mixFactor + delayedDry * dryFactor;
			samples[i] = std::isfinite(blendedSample) ? std::clamp(blendedSample, -1.0f, 1.0f) : 0.0f;
			if (!m_ready) {
				std::fill(samples + i + 1, samples + sampleCount, 0.0f);
				return;
			}
		}
#else
		(void) samples;
		(void) sampleCount;
		(void) mixFactor;
#endif
	}

	QString activeModelId() const {
		return m_ready ? m_activeModelId : QString();
	}

	QString activeModelPath() const {
		return m_ready ? m_modelPath : QString();
	}

	bool usedFallback() const {
		return m_ready && m_usedFallback;
	}

	std::uint64_t workerProcessingFrames() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.processingFrames() : 0;
	}

	std::uint64_t lastWorkerProcessingNanoseconds() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.lastProcessingNanoseconds() : 0;
	}

	std::uint64_t workerTotalProcessingNanoseconds() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.totalProcessingNanoseconds() : 0;
	}

	std::uint64_t workerMaximumProcessingNanoseconds() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.maximumProcessingNanoseconds() : 0;
	}

	std::uint64_t workerProcessingP99Nanoseconds() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.processingP99Nanoseconds() : 0;
	}

	unsigned int workerPendingFrames() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.pendingFrames() : 0;
	}

	unsigned int workerSchedulingDelayFrames() const noexcept {
		return m_realtimePrepared ? DeepFilterNetRealtimeWorker::schedulingDelayFrames : 0;
	}

	unsigned int workerSchedulingSlackFrames() const noexcept {
		return m_realtimePrepared ? m_realtimeWorker.schedulingSlackFrames() : 0;
	}

private:
	static int processRealtimeFrame(void *context, const float *input, float *output) noexcept {
		auto *self = static_cast< Implementation * >(context);
		if (!self || !self->m_state || !self->m_processFrameV2) {
			return 1;
		}
		float localSnr = std::numeric_limits< float >::quiet_NaN();
		return self->processModelFrame(input, output, &localSnr);
	}

	int processModelFrame(const float *input, float *output, float *localSnr) noexcept {
		if (!input || !output || !m_state || !m_processFrameV2) {
			return DeepFilterNetRealtimeWorker::statusInvalidFrame;
		}
		if (!m_modelDomainNormalization) {
			return m_processFrameV2(m_state, input, output, localSnr);
		}
		if (m_modelInputFrame.size() != m_frameLength || m_modelOutputInverseGain.size() != m_frameLength) {
			return DeepFilterNetRealtimeWorker::statusInvalidFrame;
		}

		float peak = 0.0f;
		for (std::size_t index = 0; index < m_frameLength; ++index) {
			const float sample = std::isfinite(input[index]) ? std::clamp(input[index], -1.0f, 1.0f) : 0.0f;
			peak               = std::max(peak, std::abs(sample));
		}
		const float headroomGain = peak > 0.0f ? modelDomainPeakLimit / peak : modelDomainNominalGain;
		const float inputGain    = std::clamp(headroomGain, 1.0f, modelDomainNominalGain);

		for (std::size_t index = 0; index < m_frameLength; ++index) {
			const float sample = std::isfinite(input[index]) ? std::clamp(input[index], -1.0f, 1.0f) : 0.0f;
			m_modelInputFrame[index] = std::clamp(sample * inputGain, -modelDomainPeakLimit, modelDomainPeakLimit);

			float correspondingGain = inputGain;
			if (!m_modelGainDelay.empty()) {
				correspondingGain = m_modelGainDelay[m_modelGainDelayPosition];
				m_modelGainDelay[m_modelGainDelayPosition] = inputGain;
				m_modelGainDelayPosition = (m_modelGainDelayPosition + 1) % m_modelGainDelay.size();
			}
			m_modelOutputInverseGain[index] = 1.0f / std::max(correspondingGain, 1.0f);
		}

		const int status = m_processFrameV2(m_state, m_modelInputFrame.data(), output, localSnr);
		if (status != DeepFilterNetRealtimeWorker::statusOk) {
			return status;
		}
		for (std::size_t index = 0; index < m_frameLength; ++index) {
			output[index] *= m_modelOutputInverseGain[index];
		}
		return DeepFilterNetRealtimeWorker::statusOk;
	}

	void processRealtimeInPlace(float *samples, unsigned int sampleCount, float mixFactor) noexcept {
		for (unsigned int i = 0; i < sampleCount; ++i) {
			samples[i] = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
		}
		if (!m_realtimeWorker.processFrame(samples, m_realtimeOutputFrame.data(), sampleCount)) {
			std::fill(samples, samples + sampleCount, 0.0f);
			return;
		}

		mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
		const float dryFactor = 1.0f - mixFactor;
		for (unsigned int i = 0; i < sampleCount; ++i) {
			const float inputSample = samples[i];
			const float cleanedSample = std::isfinite(m_realtimeOutputFrame[i])
									  ? std::clamp(m_realtimeOutputFrame[i], -1.0f, 1.0f)
									  : 0.0f;
			const float delayedDry = m_dryDelay[m_dryDelayPosition];
			m_dryDelay[m_dryDelayPosition] = inputSample;
			m_dryDelayPosition = (m_dryDelayPosition + 1) % m_dryDelay.size();
			const float blendedSample = cleanedSample * mixFactor + delayedDry * dryFactor;
			samples[i] = std::isfinite(blendedSample) ? std::clamp(blendedSample, -1.0f, 1.0f) : 0.0f;
		}
	}

	bool initializeState() {
		if (!m_create || !m_getFrameLength || !m_getLatency || !m_processFrameV2 || !m_setAttenLim
			|| !m_setPostFilterBeta || !m_free || m_modelPath.isEmpty()) {
			return false;
		}

		const QByteArray modelPathBytes = QDir::toNativeSeparators(m_modelPath).toUtf8();
		m_state = m_create(modelPathBytes.constData(), m_profile.attenuationLimitDb, nullptr);
		if (!m_state) {
			return false;
		}

		m_frameLength = m_getFrameLength(m_state);
		const std::size_t algorithmicLatency = m_getLatency(m_state);
		const std::size_t maximumAdapterLatency =
			std::max< std::size_t >(m_frameLength, DeepFilterNetRealtimeWorker::schedulingDelaySamples);
		if (m_frameLength == 0 || m_frameLength > 4096 || algorithmicLatency > 48000
			|| algorithmicLatency > (std::numeric_limits< unsigned int >::max() - maximumAdapterLatency)) {
			releaseState();
			return false;
		}
		// The synchronous adapter emits a frame after collecting it. The worker
		// instead grants inference two callback periods and emits frame N at N+2.
		// For DFN3 (1440 algorithmic samples), that is 2400 samples / 50 ms.
		m_synchronousLatencySamples = static_cast< unsigned int >(algorithmicLatency + m_frameLength);
		m_realtimeLatencySamples = static_cast< unsigned int >(
			algorithmicLatency + DeepFilterNetRealtimeWorker::schedulingDelaySamples);
		m_latencySamples = m_synchronousLatencySamples;

		m_setAttenLim(m_state, m_profile.attenuationLimitDb);
		m_setPostFilterBeta(m_state, m_profile.postFilterBeta);
		m_inputFrame.assign(m_frameLength, 0.0f);
		m_outputFrame.assign(m_frameLength, 0.0f);
		m_modelInputFrame.assign(m_modelDomainNormalization ? m_frameLength : 0, 0.0f);
		m_modelOutputInverseGain.assign(m_modelDomainNormalization ? m_frameLength : 0, 1.0f);
		m_modelGainDelay.assign(m_modelDomainNormalization ? algorithmicLatency : 0, 1.0f);
		m_modelGainDelayPosition = 0;
		m_inputFrameSize = 0;
		m_outputFramePosition = m_frameLength;
		m_dryDelay.assign(m_latencySamples, 0.0f);
		m_dryDelayPosition = 0;
		m_realtimeOutputFrame.fill(0.0f);
		return true;
	}

	void releaseState() {
		if (m_state && m_free) {
			m_free(m_state);
		}
		m_state = nullptr;
		m_frameLength = 0;
		m_synchronousLatencySamples = 0;
		m_realtimeLatencySamples = 0;
		m_latencySamples = 0;
		m_modelInputFrame.clear();
		m_modelOutputInverseGain.clear();
		m_modelGainDelay.clear();
		m_modelGainDelayPosition = 0;
	}

	QLibrary m_library;
	DeepFilterNetProfile m_profile;
	const bool m_modelDomainNormalization = false;
	QString m_modelPath;
	QString m_activeModelId;
	DFState *m_state = nullptr;
	DfCreateFn m_create = nullptr;
	DfGetFrameLengthFn m_getFrameLength = nullptr;
	DfGetLatencyFn m_getLatency = nullptr;
	DfProcessFrameV2Fn m_processFrameV2 = nullptr;
	DfSetAttenLimFn m_setAttenLim = nullptr;
	DfSetPostFilterBetaFn m_setPostFilterBeta = nullptr;
	DfFreeFn m_free = nullptr;
	std::size_t m_frameLength = 0;
	std::vector< float > m_inputFrame;
	std::vector< float > m_outputFrame;
	std::vector< float > m_modelInputFrame;
	std::vector< float > m_modelOutputInverseGain;
	std::vector< float > m_modelGainDelay;
	std::size_t m_modelGainDelayPosition = 0;
	std::size_t m_inputFrameSize = 0;
	std::size_t m_outputFramePosition = 0;
	std::vector< float > m_dryDelay;
	std::size_t m_dryDelayPosition = 0;
	DeepFilterNetRealtimeWorker m_realtimeWorker;
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > m_realtimeOutputFrame = {};
	unsigned int m_synchronousLatencySamples = 0;
	unsigned int m_realtimeLatencySamples = 0;
	unsigned int m_latencySamples = 0;
	bool m_usedFallback = false;
	bool m_ready = false;
	bool m_realtimePrepared = false;
};

DeepFilterNetSpeechCleanup::DeepFilterNetSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection)
	: m_impl(std::make_unique< Implementation >(selection)) {
}

DeepFilterNetSpeechCleanup::~DeepFilterNetSpeechCleanup() = default;

bool DeepFilterNetSpeechCleanup::isReady() const {
	return m_impl && m_impl->isReady();
}

void DeepFilterNetSpeechCleanup::reset() {
	if (m_impl) {
		m_impl->reset();
	}
}

bool DeepFilterNetSpeechCleanup::prepareRealtime() {
	return m_impl && m_impl->prepareRealtime();
}

bool DeepFilterNetSpeechCleanup::prepareOfflineFrame() noexcept {
	return m_impl && m_impl->prepareOfflineFrame();
}

bool DeepFilterNetSpeechCleanup::finishOfflineProcessing() noexcept {
	return m_impl && m_impl->finishOfflineProcessing();
}

unsigned int DeepFilterNetSpeechCleanup::latencySamples() const {
	return m_impl ? m_impl->latencySamples() : 0;
}

void DeepFilterNetSpeechCleanup::processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
	if (m_impl) {
		m_impl->processInPlace(samples, sampleCount, mixFactor);
	}
}

QString DeepFilterNetSpeechCleanup::activeModelId() const {
	return m_impl ? m_impl->activeModelId() : QString();
}

QString DeepFilterNetSpeechCleanup::activeModelPath() const {
	return m_impl ? m_impl->activeModelPath() : QString();
}

bool DeepFilterNetSpeechCleanup::usedFallback() const {
	return m_impl ? m_impl->usedFallback() : false;
}

std::uint64_t DeepFilterNetSpeechCleanup::workerProcessingFrames() const noexcept {
	return m_impl ? m_impl->workerProcessingFrames() : 0;
}

std::uint64_t DeepFilterNetSpeechCleanup::lastWorkerProcessingNanoseconds() const noexcept {
	return m_impl ? m_impl->lastWorkerProcessingNanoseconds() : 0;
}

std::uint64_t DeepFilterNetSpeechCleanup::workerTotalProcessingNanoseconds() const noexcept {
	return m_impl ? m_impl->workerTotalProcessingNanoseconds() : 0;
}

std::uint64_t DeepFilterNetSpeechCleanup::workerMaximumProcessingNanoseconds() const noexcept {
	return m_impl ? m_impl->workerMaximumProcessingNanoseconds() : 0;
}

std::uint64_t DeepFilterNetSpeechCleanup::workerProcessingP99Nanoseconds() const noexcept {
	return m_impl ? m_impl->workerProcessingP99Nanoseconds() : 0;
}

unsigned int DeepFilterNetSpeechCleanup::workerPendingFrames() const noexcept {
	return m_impl ? m_impl->workerPendingFrames() : 0;
}

unsigned int DeepFilterNetSpeechCleanup::workerSchedulingDelayFrames() const noexcept {
	return m_impl ? m_impl->workerSchedulingDelayFrames() : 0;
}

unsigned int DeepFilterNetSpeechCleanup::workerSchedulingSlackFrames() const noexcept {
	return m_impl ? m_impl->workerSchedulingSlackFrames() : 0;
}
