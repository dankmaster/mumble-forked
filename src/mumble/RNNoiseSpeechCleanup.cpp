// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RNNoiseSpeechCleanup.h"

#ifdef USE_RNNOISE
extern "C" {
#	include "rnnoise.h"
}
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace {
	constexpr float RNNOISE_PCM_SCALE = 32768.0f;
	constexpr qint64 MAX_RNNOISE_MODEL_SIZE = 64 * 1024 * 1024;

	QString resolvePackagedLittleModelPath() {
		return QDir(QCoreApplication::applicationDirPath())
			.filePath(QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin"));
	}

	void logRnnoiseFallback(const QString &requestedModelId, const QString &requestedCustomModelPath,
							const QString &reason) {
		qWarning("RNNoiseSpeechCleanup: backend=RNNoise requestedModelId=%s requestedCustomModelPath=\"%s\" "
				 "activeModelId=rnnoise:embedded fallbackReason=\"%s\"",
				 qUtf8Printable(requestedModelId), qUtf8Printable(requestedCustomModelPath), qUtf8Printable(reason));
	}

	std::FILE *openRnnoiseModelFile(const QString &path, QString &absolutePath, QString &failureReason) {
		const QFileInfo modelInfo(path);
		if (!modelInfo.exists()) {
			failureReason = QStringLiteral("Model file does not exist: %1").arg(path);
			return nullptr;
		}
		if (!modelInfo.isFile()) {
			failureReason = QStringLiteral("Model path is not a regular file: %1").arg(path);
			return nullptr;
		}

		absolutePath = modelInfo.absoluteFilePath();
		const qint64 declaredSize = modelInfo.size();
		if (declaredSize <= 0) {
			failureReason = QStringLiteral("Model file is empty: %1").arg(absolutePath);
			return nullptr;
		}
		if (declaredSize > MAX_RNNOISE_MODEL_SIZE) {
			failureReason = QStringLiteral("Model file exceeds the 64 MiB safety limit: %1")
							.arg(absolutePath);
			return nullptr;
		}

#ifdef Q_OS_WIN
		std::FILE *modelFile = _wfopen(reinterpret_cast< const wchar_t * >(absolutePath.utf16()), L"rb");
#else
		const QByteArray encodedPath = QFile::encodeName(absolutePath);
		std::FILE *modelFile          = std::fopen(encodedPath.constData(), "rb");
#endif
		if (!modelFile) {
			failureReason = QStringLiteral("Cannot open model file %1: %2")
							.arg(absolutePath, QString::fromLocal8Bit(std::strerror(errno)));
			return nullptr;
		}

		if (std::fseek(modelFile, 0, SEEK_END) != 0) {
			failureReason = QStringLiteral("Cannot seek model file: %1").arg(absolutePath);
			std::fclose(modelFile);
			return nullptr;
		}
		const long openedSize = std::ftell(modelFile);
		if (openedSize <= 0 || openedSize > MAX_RNNOISE_MODEL_SIZE || std::fseek(modelFile, 0, SEEK_SET) != 0) {
			failureReason = QStringLiteral("Model file changed or is not readable: %1").arg(absolutePath);
			std::fclose(modelFile);
			return nullptr;
		}

		return modelFile;
	}
}

RNNoiseSpeechCleanup::RNNoiseSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection)
	: m_selection(Mumble::SpeechCleanup::normalizeSelection(selection)) {
#ifdef USE_RNNOISE
	reset();
#else
	(void) m_selection;
#endif
}

RNNoiseSpeechCleanup::~RNNoiseSpeechCleanup() {
#ifdef USE_RNNOISE
	if (m_state) {
		rnnoise_destroy(m_state);
	}
	if (m_model) {
		rnnoise_model_free(m_model);
	}
	if (m_modelFile) {
		std::fclose(m_modelFile);
	}
#endif
}

bool RNNoiseSpeechCleanup::isReady() const {
#ifdef USE_RNNOISE
	return m_state != nullptr;
#else
	return false;
#endif
}

QString RNNoiseSpeechCleanup::activeModelId() const {
	return m_activeModelId;
}

QString RNNoiseSpeechCleanup::activeModelPath() const {
	return m_activeModelPath;
}

bool RNNoiseSpeechCleanup::usedFallback() const {
	return m_usedFallback;
}

void RNNoiseSpeechCleanup::reset() {
#ifdef USE_RNNOISE
	m_inputBuffer.fill(0.0f);
	m_inputBufferSize = 0;
	m_outputBuffer.fill(0.0f);
	m_outputBufferPosition = FRAME_SIZE;
	m_dryDelayBuffer.fill(0.0f);
	m_dryDelayPosition = 0;

	if (m_state) {
		rnnoise_destroy(m_state);
		m_state = nullptr;
	}
	if (m_model) {
		rnnoise_model_free(m_model);
		m_model = nullptr;
	}
	if (m_modelFile) {
		std::fclose(m_modelFile);
		m_modelFile = nullptr;
	}

	const QString normalizedModelId = Mumble::SpeechCleanup::normalizedModelId(m_selection.backend, m_selection.modelId);
	const QString requestedCustomModelPath = m_selection.customModelPath.trimmed();
	QString modelPath;
	m_activeModelId   = QStringLiteral("rnnoise:embedded");
	m_activeModelPath.clear();
	m_usedFallback = false;

	if (normalizedModelId == QLatin1String("rnnoise:little")) {
		modelPath = resolvePackagedLittleModelPath();
	} else if (normalizedModelId == QLatin1String("rnnoise:custom")) {
		modelPath = requestedCustomModelPath;
		if (modelPath.isEmpty()) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Custom model path is empty"));
			modelPath.clear();
		}
	}

	if (!modelPath.isEmpty()) {
		QString failureReason;
		QString absoluteModelPath;
		m_modelFile = openRnnoiseModelFile(modelPath, absoluteModelPath, failureReason);
		if (!m_modelFile) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath, failureReason);
			modelPath.clear();
		} else {
			modelPath = absoluteModelPath;
			m_model   = rnnoise_model_from_file(m_modelFile);
			if (!m_model) {
				m_usedFallback = true;
				logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
								   QStringLiteral("Failed to load model from %1").arg(modelPath));
				std::fclose(m_modelFile);
				m_modelFile = nullptr;
				modelPath.clear();
			}
		}
	}

	m_state = rnnoise_create(m_model);
	if (!m_state) {
		if (m_model) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Model is corrupt or incompatible: %1").arg(modelPath));
			rnnoise_model_free(m_model);
			m_model = nullptr;
			std::fclose(m_modelFile);
			m_modelFile = nullptr;
			m_state = rnnoise_create(nullptr);
		}
	}

	if (!m_state) {
		m_activeModelId.clear();
		m_activeModelPath.clear();
		qWarning("RNNoiseSpeechCleanup: backend=RNNoise requestedModelId=%s requestedCustomModelPath=\"%s\" "
				 "activeModelId=none initializationFailed=true",
				 qUtf8Printable(normalizedModelId), qUtf8Printable(requestedCustomModelPath));
		return;
	}

	if (m_model) {
		m_activeModelId   = normalizedModelId;
		m_activeModelPath = modelPath;
	} else if (normalizedModelId != QLatin1String("rnnoise:embedded")) {
		m_usedFallback = true;
	}

	qInfo("RNNoiseSpeechCleanup: Initialized backend=RNNoise requestedModelId=%s activeModelId=%s "
		  "requestedCustomModelPath=\"%s\" activeModelPath=\"%s\" usedFallback=%s",
		  qUtf8Printable(normalizedModelId), qUtf8Printable(m_activeModelId),
		  qUtf8Printable(requestedCustomModelPath), qUtf8Printable(m_activeModelPath),
		  m_usedFallback ? "true" : "false");
#endif
}

unsigned int RNNoiseSpeechCleanup::latencySamples() const {
	return LATENCY_SAMPLES;
}

void RNNoiseSpeechCleanup::processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
#ifdef USE_RNNOISE
	if (!m_state || !samples || sampleCount == 0) {
		return;
	}

	mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
	const float dryFactor = 1.0f - mixFactor;
	for (unsigned int i = 0; i < sampleCount; ++i) {
		const float inputSample = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
		const float rawCleanedSample = m_outputBufferPosition < FRAME_SIZE
										 ? m_outputBuffer[m_outputBufferPosition++] / RNNOISE_PCM_SCALE
										 : 0.0f;
		const float cleanedSample = std::isfinite(rawCleanedSample) ? rawCleanedSample : 0.0f;
		const float delayedDry = m_dryDelayBuffer[m_dryDelayPosition];
		m_dryDelayBuffer[m_dryDelayPosition] = inputSample;
		m_dryDelayPosition = (m_dryDelayPosition + 1) % LATENCY_SAMPLES;

		m_inputBuffer[m_inputBufferSize++] = inputSample * RNNOISE_PCM_SCALE;
		if (m_inputBufferSize == FRAME_SIZE) {
			// The previous frame has been consumed exactly when the next frame is
			// ready, so a fixed frame buffer is sufficient and never overwritten.
			rnnoise_process_frame(m_state, m_outputBuffer.data(), m_inputBuffer.data());
			m_inputBufferSize = 0;
			m_outputBufferPosition = 0;
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
