// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanupProcessor.h"

#include "DTLNSpeechCleanup.h"
#include "DeepFilterNetSpeechCleanup.h"
#include "RNNoiseSpeechCleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QThreadPool>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {
	bool hasDtlnModelPair(const QString &directoryPath) {
		return QFileInfo::exists(QDir(directoryPath).filePath(QStringLiteral("model_1.onnx")))
			   && QFileInfo::exists(QDir(directoryPath).filePath(QStringLiteral("model_2.onnx")));
	}

	QString resolveDtlnModelDirectory(const Mumble::SpeechCleanup::Selection &selection, QString *activeModelId = nullptr) {
		const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dtln"));
		const QString normalizedModelId =
			Mumble::SpeechCleanup::normalizedModelId(selection.backend, selection.modelId);

		QString requestedDirectory = baseDir;
		if (normalizedModelId == QLatin1String("dtln:norm500h")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("norm_500h"));
		} else if (normalizedModelId == QLatin1String("dtln:norm40h")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("norm_40h"));
		} else if (normalizedModelId == QLatin1String("dtln:baseline")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("baseline"));
		}

		if (hasDtlnModelPair(requestedDirectory)) {
			if (activeModelId) {
				*activeModelId = normalizedModelId;
			}
			return requestedDirectory;
		}

		const QString legacyBaselineDirectory = baseDir;
		const QString packagedBaselineDirectory = QDir(baseDir).filePath(QStringLiteral("baseline"));
		if (normalizedModelId != QLatin1String("dtln:baseline")) {
			qWarning("DTLNSpeechCleanup: Missing model variant \"%s\" under %s, falling back to baseline",
					 qUtf8Printable(normalizedModelId), qUtf8Printable(requestedDirectory));
		}

		if (hasDtlnModelPair(packagedBaselineDirectory)) {
			if (activeModelId) {
				*activeModelId = QStringLiteral("dtln:baseline");
			}
			return packagedBaselineDirectory;
		}
		if (hasDtlnModelPair(legacyBaselineDirectory)) {
			if (activeModelId) {
				*activeModelId = QStringLiteral("dtln:baseline");
			}
			return legacyBaselineDirectory;
		}

		if (activeModelId) {
			activeModelId->clear();
		}
		return {};
	}

	class DTLNSpeechCleanupProcessor final : public SpeechCleanupProcessor {
	public:
		explicit DTLNSpeechCleanupProcessor(const Mumble::SpeechCleanup::Selection &selection) {
#ifdef USE_DTLN
			const QString requestedModelId =
				Mumble::SpeechCleanup::normalizedModelId(selection.backend, selection.modelId);
			m_modelDirectory = resolveDtlnModelDirectory(selection, &m_activeModelId);
			m_usedFallback = !m_activeModelId.isEmpty() && m_activeModelId != requestedModelId;
			if (!m_modelDirectory.isEmpty()) {
				m_dtln = std::make_unique< DTLNSpeechCleanup >(m_modelDirectory);
				if (m_dtln && m_dtln->isReady()) {
					qInfo("DTLNSpeechCleanup: Initialized backend=DTLN requestedModelId=%s activeModelId=%s "
						  "modelDirectory=\"%s\"",
						  qUtf8Printable(requestedModelId), qUtf8Printable(m_activeModelId),
						  qUtf8Printable(m_modelDirectory));
				}
			}

			if (!m_dtln || !m_dtln->isReady()) {
				m_usedFallback = m_usedFallback || !m_modelDirectory.isEmpty() || requestedModelId != QLatin1String("dtln:baseline");
				qWarning("DTLNSpeechCleanup: Failed to initialize backend=DTLN requestedModelId=%s modelDirectory=\"%s\"",
						 qUtf8Printable(requestedModelId), qUtf8Printable(m_modelDirectory));
			}
#else
			(void) selection;
#endif
		}

		bool isReady() const override {
#ifdef USE_DTLN
			return m_dtln && m_dtln->isReady();
#else
			return false;
#endif
		}

		void reset() override {
#ifdef USE_DTLN
			if (m_dtln) {
				m_dtln->reset();
			}
#endif
		}

		unsigned int latencySamples() const override {
#ifdef USE_DTLN
			return m_dtln ? m_dtln->latencySamples() : 0;
#else
			return 0;
#endif
		}

		void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) override {
#ifdef USE_DTLN
			if (m_dtln) {
				m_dtln->processNormalizedMonoInPlace(samples, sampleCount, mixFactor);
			}
#else
			(void) samples;
			(void) sampleCount;
			(void) mixFactor;
#endif
		}

		QString activeModelId() const override {
			return m_activeModelId;
		}

		QString activeModelPath() const override {
			return m_modelDirectory;
		}

		bool usedFallback() const override {
			return m_usedFallback;
		}

	private:
#ifdef USE_DTLN
		std::unique_ptr< DTLNSpeechCleanup > m_dtln;
#endif
		QString m_activeModelId;
		QString m_modelDirectory;
		bool m_usedFallback = false;
	};
} // namespace

std::unique_ptr< SpeechCleanupProcessor > createSpeechCleanupProcessor(
	const Mumble::SpeechCleanup::Selection &requestedSelection) {
	const Mumble::SpeechCleanup::Selection selection = Mumble::SpeechCleanup::normalizeSelection(requestedSelection);

	switch (selection.backend) {
		case Settings::RNNoiseBackend:
			return std::make_unique< RNNoiseSpeechCleanup >(selection);
		case Settings::DTLNBackend:
			return std::make_unique< DTLNSpeechCleanupProcessor >(selection);
		case Settings::DeepFilterNetBackend:
			return std::make_unique< DeepFilterNetSpeechCleanup >(selection);
	}

	return {};
}

ReceiveSpeechCleanupProcessor::ReceiveSpeechCleanupProcessor(
	std::unique_ptr< SpeechCleanupProcessor > processor, unsigned int maximumBurstFrames)
	: m_processor(std::move(processor)), m_maximumBurstFrames(maximumBurstFrames) {
}

bool ReceiveSpeechCleanupProcessor::isReady() const { return m_prepared; }

bool ReceiveSpeechCleanupProcessor::prepareRealtime() {
	if (!m_processor || !m_processor->isReady()
		|| !m_processor->prepareRealtimeForReceive(m_maximumBurstFrames)) return false;
	m_latencySamples = m_processor->latencySamples();
	// Bound even a malformed backend's latency before allocating. This covers
	// the largest supported model latency plus the bounded receive worker ring.
	if (m_latencySamples > 48000 + 64 * frameSamples) return false;
	m_dryDelay.assign(static_cast< std::size_t >(m_latencySamples) * 2, 0.0f);
	m_monoDelay.assign(m_latencySamples, 0);
	m_delayPosition = 0;
	m_failed = false;
	m_prepared = true;
	return true;
}

void ReceiveSpeechCleanupProcessor::reset() {
	m_prepared = false;
	if (m_processor) m_processor->reset();
	m_dryDelay.clear();
	m_monoDelay.clear();
}

unsigned int ReceiveSpeechCleanupProcessor::latencySamples() const { return m_latencySamples; }

bool ReceiveSpeechCleanupProcessor::prepareOfflineFrame() noexcept {
	return m_prepared && (m_failed || m_processor->prepareOfflineFrame());
}

bool ReceiveSpeechCleanupProcessor::finishOfflineProcessing() noexcept {
	return m_prepared && (m_failed || m_processor->finishOfflineProcessing());
}

void ReceiveSpeechCleanupProcessor::processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
	if (!samples || sampleCount > frameSamples) return;
	std::array< float, frameSamples * 2 > stereo {};
	for (unsigned int i = 0; i < sampleCount; ++i) stereo[2 * i] = stereo[2 * i + 1] = samples[i];
	if (processStereoFrame(stereo.data(), sampleCount * 2, mixFactor))
		for (unsigned int i = 0; i < sampleCount; ++i) samples[i] = stereo[2 * i];
}

bool ReceiveSpeechCleanupProcessor::processStereoFrame(float *samples, unsigned int sampleCount, float mixFactor) {
	if (!m_prepared || !samples || sampleCount == 0 || sampleCount % 2 != 0
		|| sampleCount > frameSamples * 2) return false;
	const unsigned int frames = sampleCount / 2;
	bool mono = true;
	for (unsigned int i = 0; i < frames; ++i) {
		for (unsigned int channel = 0; channel < 2; ++channel) {
			float &sample = samples[2 * i + channel];
			sample = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
		}
		mono = mono && std::fabs(samples[2 * i] - samples[2 * i + 1]) <= 1.0e-4f;
		m_monoFrame[i] = (samples[2 * i] + samples[2 * i + 1]) * 0.5f;
	}
	std::fill(m_monoFrame.begin() + frames, m_monoFrame.end(), 0.0f);
	for (unsigned int i = 0; i < frames; ++i) {
		if (m_latencySamples == 0) {
			m_dryFrame[2 * i] = samples[2 * i];
			m_dryFrame[2 * i + 1] = samples[2 * i + 1];
			m_wetEligible[i] = mono;
			continue;
		}
		m_wetEligible[i] = m_monoDelay[m_delayPosition];
		m_monoDelay[m_delayPosition] = mono;
		for (unsigned int channel = 0; channel < 2; ++channel) {
			m_dryFrame[2 * i + channel] = m_dryDelay[2 * m_delayPosition + channel];
			m_dryDelay[2 * m_delayPosition + channel] = samples[2 * i + channel];
		}
		m_delayPosition = (m_delayPosition + 1) % m_latencySamples;
	}
	if (!m_failed) {
		try {
			if (!m_processor->isReady()) m_failed = true;
			else {
				m_processor->processInPlace(m_monoFrame.data(), frameSamples, mixFactor);
				m_failed = !m_processor->isReady()
					|| !std::all_of(m_monoFrame.cbegin(), m_monoFrame.cend(), [](float sample) {
						return std::isfinite(sample) && std::fabs(sample) <= 1.0f;
					});
			}
		} catch (...) { m_failed = true; }
	}
	for (unsigned int i = 0; i < frames; ++i) {
		const bool wet = !m_failed && m_wetEligible[i];
		samples[2 * i] = wet ? m_monoFrame[i] : m_dryFrame[2 * i];
		samples[2 * i + 1] = wet ? m_monoFrame[i] : m_dryFrame[2 * i + 1];
	}
	return true;
}

namespace {
	QThreadPool &speechCleanupRetirementPool() {
		// Process-lifetime: normal client shutdown must not join a backend while
		// holding the output-map lock or destroying a short-lived utterance.
		static QThreadPool *pool = [] {
			auto *created = new QThreadPool;
			created->setMaxThreadCount(1);
			created->setExpiryTimeout(-1);
			return created;
		}();
		return *pool;
	}
}

struct SpeechCleanupPreparation::Cancellation {
	std::atomic_bool cancelled { false };
	std::mutex mutex;
	std::condition_variable wake;
};

SpeechCleanupPreparation::SpeechCleanupPreparation(QThreadPool &pool, Factory factory,
	const std::chrono::steady_clock::time_point notBefore)
	: m_cancellation(std::make_shared< Cancellation >()) {
	// Construct the retirement service on the preparation caller, never during
	// audio adoption or a callback's fallback transition.
	(void) speechCleanupRetirementPool();
	m_future = QtConcurrent::run(&pool, [state = m_cancellation, factory = std::move(factory), notBefore]() {
		{
			std::unique_lock< std::mutex > lock(state->mutex);
			state->wake.wait_until(lock, notBefore, [&] { return state->cancelled.load(std::memory_order_acquire); });
		}
		if (state->cancelled.load(std::memory_order_acquire)) return std::unique_ptr< SpeechCleanupProcessor > {};
		try {
			auto processor = factory();
			if (state->cancelled.load(std::memory_order_acquire)) return std::unique_ptr< SpeechCleanupProcessor > {};
			if (!processor || !processor->prepareRealtime() || !processor->isReady()
				|| state->cancelled.load(std::memory_order_acquire)) return std::unique_ptr< SpeechCleanupProcessor > {};
			return processor;
		} catch (...) {
			return std::unique_ptr< SpeechCleanupProcessor > {};
		}
	});
}

SpeechCleanupPreparation::~SpeechCleanupPreparation() {
	cancel();
	// A completed, unadopted result can own a running realtime worker. Release
	// the last future reference on the reclaimer, not under AudioOutput's lock.
	(void) QtConcurrent::run(&speechCleanupRetirementPool(), [future = std::move(m_future)]() mutable {
		future.waitForFinished();
	});
}

void SpeechCleanupPreparation::cancel() {
	{
		std::lock_guard< std::mutex > lock(m_cancellation->mutex);
		m_cancellation->cancelled.store(true, std::memory_order_release);
	}
	m_cancellation->wake.notify_all();
	m_future.cancel();
}

bool SpeechCleanupPreparation::isFinished() const { return m_future.isFinished(); }

std::unique_ptr< SpeechCleanupProcessor > SpeechCleanupPreparation::takeResult() {
	if (!m_future.isFinished() || m_future.isCanceled() || m_future.resultCount() == 0) return {};
	return m_future.takeResult();
}

void retireSpeechCleanupProcessor(std::unique_ptr< SpeechCleanupProcessor > processor) {
	if (!processor) return;
	(void) QtConcurrent::run(&speechCleanupRetirementPool(), [processor = std::move(processor)]() mutable {
		processor.reset();
	});
}

bool noiseCancelUsesSpeechCleanup(const Settings::NoiseCancel mode) noexcept {
	return mode == Settings::NoiseCancelRNN || mode == Settings::NoiseCancelBoth;
}

void reconcileSpeechCleanupProcessor(
	const Settings::NoiseCancel mode, const Mumble::SpeechCleanup::Selection &requestedSelection,
	Mumble::SpeechCleanup::Selection &activeSelection,
	std::unique_ptr< SpeechCleanupProcessor > &processor) {
	const Mumble::SpeechCleanup::Selection normalizedSelection =
		Mumble::SpeechCleanup::normalizeSelection(requestedSelection);

	if (!noiseCancelUsesSpeechCleanup(mode)) {
		activeSelection = normalizedSelection;
		processor.reset();
		return;
	}

	if (!processor || normalizedSelection != activeSelection) {
		activeSelection = normalizedSelection;
		processor       = createSpeechCleanupProcessor(activeSelection);
	}
}
