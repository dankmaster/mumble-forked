// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetSpeechCleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {
	struct DFState;

	using DfCreateFn            = DFState *(*)(const char *path, float attenLim, const char *logLevel);
	using DfGetFrameLengthFn    = std::size_t (*)(DFState *state);
	using DfGetLatencyFn        = std::size_t (*)(DFState *state);
	using DfProcessFrameFn      = float (*)(DFState *state, float *input, float *output);
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
		: m_profile(profileForSelection(selection)) {
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
		m_processFrame      = reinterpret_cast< DfProcessFrameFn >(m_library.resolve("df_process_frame"));
		m_setAttenLim       = reinterpret_cast< DfSetAttenLimFn >(m_library.resolve("df_set_atten_lim"));
		m_setPostFilterBeta = reinterpret_cast< DfSetPostFilterBetaFn >(m_library.resolve("df_set_post_filter_beta"));
		m_free              = reinterpret_cast< DfFreeFn >(m_library.resolve("df_free"));

		if (!m_create || !m_getFrameLength || !m_getLatency || !m_processFrame || !m_setAttenLim
			|| !m_setPostFilterBeta || !m_free) {
			qWarning("DeepFilterNetSpeechCleanup: deepfilter runtime library is missing required C API symbols");
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
		releaseState();
		if (m_library.isLoaded()) {
			m_library.unload();
		}
#endif
	}

	bool isReady() const {
		return m_ready;
	}

	void reset() {
#ifdef USE_DEEPFILTERNET
		if (!m_library.isLoaded()) {
			m_ready = false;
			return;
		}

		releaseState();
		m_ready = initializeState();
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
				m_processFrame(m_state, m_inputFrame.data(), m_outputFrame.data());
				m_inputFrameSize = 0;
				m_outputFramePosition = 0;
			}

			const float blendedSample = cleanedSample * mixFactor + delayedDry * dryFactor;
			samples[i] = std::isfinite(blendedSample) ? std::clamp(blendedSample, -1.0f, 1.0f) : 0.0f;
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

private:
	bool initializeState() {
		if (!m_create || !m_getFrameLength || !m_getLatency || !m_processFrame || !m_setAttenLim
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
		if (m_frameLength == 0 || m_frameLength > 4096 || algorithmicLatency > 48000
			|| algorithmicLatency > (std::numeric_limits< unsigned int >::max() - m_frameLength)) {
			releaseState();
			return false;
		}
		m_latencySamples = static_cast< unsigned int >(algorithmicLatency + m_frameLength);

		m_setAttenLim(m_state, m_profile.attenuationLimitDb);
		m_setPostFilterBeta(m_state, m_profile.postFilterBeta);
		m_inputFrame.assign(m_frameLength, 0.0f);
		m_outputFrame.assign(m_frameLength, 0.0f);
		m_inputFrameSize = 0;
		m_outputFramePosition = m_frameLength;
		m_dryDelay.assign(m_latencySamples, 0.0f);
		m_dryDelayPosition = 0;
		return true;
	}

	void releaseState() {
		if (m_state && m_free) {
			m_free(m_state);
		}
		m_state = nullptr;
		m_frameLength = 0;
		m_latencySamples = 0;
	}

	QLibrary m_library;
	DeepFilterNetProfile m_profile;
	QString m_modelPath;
	QString m_activeModelId;
	DFState *m_state = nullptr;
	DfCreateFn m_create = nullptr;
	DfGetFrameLengthFn m_getFrameLength = nullptr;
	DfGetLatencyFn m_getLatency = nullptr;
	DfProcessFrameFn m_processFrame = nullptr;
	DfSetAttenLimFn m_setAttenLim = nullptr;
	DfSetPostFilterBetaFn m_setPostFilterBeta = nullptr;
	DfFreeFn m_free = nullptr;
	std::size_t m_frameLength = 0;
	std::vector< float > m_inputFrame;
	std::vector< float > m_outputFrame;
	std::size_t m_inputFrameSize = 0;
	std::size_t m_outputFramePosition = 0;
	std::vector< float > m_dryDelay;
	std::size_t m_dryDelayPosition = 0;
	unsigned int m_latencySamples = 0;
	bool m_usedFallback = false;
	bool m_ready = false;
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
