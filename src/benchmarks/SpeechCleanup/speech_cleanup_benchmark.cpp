// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Audio.h"
#include "SpeechCleanup.h"
#include "SpeechCleanupProcessor.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#include <sndfile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
	constexpr unsigned int kFrameSize = SAMPLE_RATE / 100;
	constexpr unsigned int kAlignmentFrameSize = SAMPLE_RATE / 1000;
	constexpr float kEpsilon          = 1.0e-12f;

	enum class InputFormat {
		Auto,
		Wav,
		S16LE,
		F32LE
	};

	struct LoadedAudio {
		std::vector< float > samples;
		int sampleRate          = 0;
		int channels            = 0;
		QString resolvedFormat;
	};

	struct BenchmarkMetrics {
		double initializationMs = 0.0;
		double cpuMs            = 0.0;
		double audioMs          = 0.0;
		double processedAudioMs = 0.0;
		double realTimeFactor   = 0.0;
		float mixFactor         = 1.0f;
		std::size_t inputSampleCount = 0;
		std::size_t outputSampleCount = 0;
		std::size_t drainSampleCount = 0;
		std::size_t processingPaddingSampleCount = 0;
		std::size_t inputSaturatedSampleCount = 0;
		std::size_t inputOutOfRangeSampleCount = 0;
		std::size_t saturatedSampleCount = 0;
		std::size_t outOfRangeSampleCount = 0;
		std::size_t nonFiniteSampleCount = 0;
		// Backwards-compatible alias. Unlike the old post-clamp `> 1.0`
		// counter, this includes samples exactly on either digital rail.
		std::size_t clippingCount = 0;
		float peak              = 0.0f;
		float rms               = 0.0f;
		QString activeModelId;
		QString activeModelPath;
		bool usedFallback       = false;
		unsigned int reportedLatencySamples = 0;
		int alignmentLagSamples = 0;
		std::optional< double > siSdr;
		std::optional< double > segmentalSnr;
	};

	struct SignalMetrics {
		std::size_t saturatedSampleCount = 0;
		std::size_t outOfRangeSampleCount = 0;
		std::size_t nonFiniteSampleCount = 0;
		float peak = 0.0f;
		float rms  = 0.0f;
	};

	struct AlignedAudio {
		std::vector< float > reference;
		std::vector< float > estimate;
		int lagSamples = 0;
	};

	SignalMetrics measureSignal(const std::vector< float > &samples) {
		SignalMetrics metrics;
		double sumSquares = 0.0;
		std::size_t finiteSampleCount = 0;
		for (float sample : samples) {
			if (!std::isfinite(sample)) {
				++metrics.nonFiniteSampleCount;
				continue;
			}

			const float absolute = std::fabs(sample);
			if (absolute >= 1.0f) {
				++metrics.saturatedSampleCount;
			}
			if (absolute > 1.0f) {
				++metrics.outOfRangeSampleCount;
			}
			metrics.peak = std::max(metrics.peak, absolute);
			sumSquares += static_cast< double >(sample) * static_cast< double >(sample);
			++finiteSampleCount;
		}

		metrics.rms = finiteSampleCount == 0
			? 0.0f
			: static_cast< float >(std::sqrt(sumSquares / static_cast< double >(finiteSampleCount)));
		return metrics;
	}

	void requireFiniteSignal(const std::vector< float > &samples, const QString &description) {
		for (std::size_t index = 0; index < samples.size(); ++index) {
			if (!std::isfinite(samples[index])) {
				throw std::runtime_error(
					QStringLiteral("%1 contains a non-finite sample at index %2")
						.arg(description)
						.arg(static_cast< quint64 >(index))
						.toStdString());
			}
		}
	}

	std::size_t roundUpToFrameSize(std::size_t sampleCount) {
		if (sampleCount == 0) {
			return 0;
		}
		if (sampleCount > std::numeric_limits< std::size_t >::max() - (kFrameSize - 1)) {
			throw std::runtime_error("Audio sample count is too large to frame safely");
		}
		return ((sampleCount + kFrameSize - 1) / kFrameSize) * kFrameSize;
	}

	bool hasHelpArgument(int argc, char **argv) {
		for (int i = 1; i < argc; ++i) {
			const QString argument = QString::fromLocal8Bit(argv[i]);
			if (argument == QLatin1String("--help") || argument == QLatin1String("-h")
				|| argument == QLatin1String("-?") || argument == QLatin1String("/?")
				|| argument == QLatin1String("--help-all")) {
				return true;
			}
		}

		return false;
	}

	InputFormat parseInputFormat(const QString &value) {
		const QString normalized = value.trimmed().toLower();
		if (normalized.isEmpty() || normalized == QLatin1String("auto")) {
			return InputFormat::Auto;
		}
		if (normalized == QLatin1String("wav")) {
			return InputFormat::Wav;
		}
		if (normalized == QLatin1String("s16le")) {
			return InputFormat::S16LE;
		}
		if (normalized == QLatin1String("f32le")) {
			return InputFormat::F32LE;
		}

		throw std::runtime_error(QStringLiteral("Unsupported input format: %1").arg(value).toStdString());
	}

	InputFormat resolveInputFormat(const QString &path, InputFormat requestedFormat) {
		if (requestedFormat != InputFormat::Auto) {
			return requestedFormat;
		}

		if (QFileInfo(path).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
			return InputFormat::Wav;
		}

		throw std::runtime_error(QStringLiteral("Unable to infer input format for %1").arg(path).toStdString());
	}

	std::vector< float > mixDownToMono(const std::vector< float > &interleavedSamples, int channels) {
		if (channels <= 0 || interleavedSamples.empty()) {
			return {};
		}
		if (channels == 1) {
			return interleavedSamples;
		}

		const std::size_t frameCount = interleavedSamples.size() / static_cast< std::size_t >(channels);
		std::vector< float > mono(frameCount, 0.0f);
		for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			float mixed = 0.0f;
			for (int channelIndex = 0; channelIndex < channels; ++channelIndex) {
				mixed += interleavedSamples[frameIndex * static_cast< std::size_t >(channels) + channelIndex];
			}
			mono[frameIndex] = mixed / static_cast< float >(channels);
		}

		return mono;
	}

	LoadedAudio loadWav(const QString &path) {
		SF_INFO info = {};
		SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		if (!file) {
			throw std::runtime_error(QStringLiteral("Failed to open WAV file %1").arg(path).toStdString());
		}

		const std::size_t frameCount = static_cast< std::size_t >(info.frames);
		std::vector< float > interleavedSamples(frameCount * static_cast< std::size_t >(info.channels), 0.0f);
		const sf_count_t readFrames = sf_readf_float(file, interleavedSamples.data(), info.frames);
		sf_close(file);

		if (readFrames != info.frames) {
			throw std::runtime_error(QStringLiteral("Failed to read all samples from %1").arg(path).toStdString());
		}

		return { mixDownToMono(interleavedSamples, info.channels), info.samplerate, 1, QStringLiteral("wav") };
	}

	LoadedAudio loadRaw(const QString &path, InputFormat format, int sampleRate, int channels) {
		if (sampleRate <= 0) {
			throw std::runtime_error(QStringLiteral("A positive sample rate is required for raw input: %1")
										 .arg(path)
										 .toStdString());
		}
		if (channels <= 0) {
			throw std::runtime_error(
				QStringLiteral("A positive channel count is required for raw input: %1").arg(path).toStdString());
		}

		std::ifstream stream(path.toStdString(), std::ios::binary);
		if (!stream) {
			throw std::runtime_error(QStringLiteral("Failed to open raw input %1").arg(path).toStdString());
		}

		stream.seekg(0, std::ios::end);
		const std::streamoff fileSize = stream.tellg();
		stream.seekg(0, std::ios::beg);
		if (fileSize < 0) {
			throw std::runtime_error(QStringLiteral("Failed to determine raw input size for %1").arg(path).toStdString());
		}

		std::vector< float > interleavedSamples;
		if (format == InputFormat::S16LE) {
			if ((fileSize % static_cast< std::streamoff >(sizeof(std::int16_t))) != 0) {
				throw std::runtime_error(QStringLiteral("Raw s16le input has an invalid length: %1").arg(path).toStdString());
			}

			std::vector< std::int16_t > pcmSamples(static_cast< std::size_t >(fileSize / sizeof(std::int16_t)));
			stream.read(reinterpret_cast< char * >(pcmSamples.data()), fileSize);
			interleavedSamples.resize(pcmSamples.size());
			std::transform(pcmSamples.begin(), pcmSamples.end(), interleavedSamples.begin(),
						   [](std::int16_t sample) { return static_cast< float >(sample) / 32768.0f; });
		} else if (format == InputFormat::F32LE) {
			if ((fileSize % static_cast< std::streamoff >(sizeof(float))) != 0) {
				throw std::runtime_error(QStringLiteral("Raw f32le input has an invalid length: %1").arg(path).toStdString());
			}

			interleavedSamples.resize(static_cast< std::size_t >(fileSize / sizeof(float)));
			stream.read(reinterpret_cast< char * >(interleavedSamples.data()), fileSize);
		} else {
			throw std::runtime_error(QStringLiteral("Unsupported raw format for %1").arg(path).toStdString());
		}

		if (!stream) {
			throw std::runtime_error(QStringLiteral("Failed to read raw input %1").arg(path).toStdString());
		}
		if ((interleavedSamples.size() % static_cast< std::size_t >(channels)) != 0) {
			throw std::runtime_error(
				QStringLiteral("Raw input sample count is not divisible by channel count: %1").arg(path).toStdString());
		}

		QString resolvedFormat = (format == InputFormat::S16LE) ? QStringLiteral("s16le") : QStringLiteral("f32le");
		return { mixDownToMono(interleavedSamples, channels), sampleRate, 1, resolvedFormat };
	}

	LoadedAudio loadAudio(const QString &path, InputFormat requestedFormat, int sampleRate, int channels) {
		const InputFormat resolvedFormat = resolveInputFormat(path, requestedFormat);
		switch (resolvedFormat) {
			case InputFormat::Wav:
				return loadWav(path);
			case InputFormat::S16LE:
			case InputFormat::F32LE:
				return loadRaw(path, resolvedFormat, sampleRate, channels);
			case InputFormat::Auto:
				break;
		}

		throw std::runtime_error(QStringLiteral("Unsupported input format for %1").arg(path).toStdString());
	}

	void writeOutputWav(const QString &path, const std::vector< float > &samples, int sampleRate) {
		SF_INFO info = {};
		info.channels = 1;
		info.samplerate = sampleRate;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

		SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
		if (!file) {
			throw std::runtime_error(QStringLiteral("Failed to open output WAV %1").arg(path).toStdString());
		}

		const sf_count_t writtenFrames = sf_writef_float(file, samples.data(), static_cast< sf_count_t >(samples.size()));
		sf_close(file);
		if (writtenFrames != static_cast< sf_count_t >(samples.size())) {
			throw std::runtime_error(QStringLiteral("Failed to write output WAV %1").arg(path).toStdString());
		}
	}

	Settings::SpeechCleanupBackend parseBackend(const QString &value) {
		for (Settings::SpeechCleanupBackend backend : Mumble::SpeechCleanup::supportedBackends) {
			if (QString::fromLatin1(Mumble::SpeechCleanup::backendDisplayName(backend))
					.compare(value.trimmed(), Qt::CaseInsensitive)
				== 0) {
				return backend;
			}
		}

		throw std::runtime_error(QStringLiteral("Unsupported backend: %1").arg(value).toStdString());
	}

	double computeSiSdr(const std::vector< float > &reference, const std::vector< float > &estimate) {
		const std::size_t sampleCount = std::min(reference.size(), estimate.size());
		if (sampleCount == 0) {
			return 0.0;
		}

		double referenceEnergy = 0.0;
		double dotProduct      = 0.0;
		for (std::size_t index = 0; index < sampleCount; ++index) {
			referenceEnergy += static_cast< double >(reference[index]) * static_cast< double >(reference[index]);
			dotProduct += static_cast< double >(reference[index]) * static_cast< double >(estimate[index]);
		}

		if (referenceEnergy <= kEpsilon) {
			return 0.0;
		}

		const double scale = dotProduct / referenceEnergy;
		double targetEnergy = 0.0;
		double noiseEnergy  = 0.0;
		for (std::size_t index = 0; index < sampleCount; ++index) {
			const double targetSample = scale * static_cast< double >(reference[index]);
			const double noiseSample  = static_cast< double >(estimate[index]) - targetSample;
			targetEnergy += targetSample * targetSample;
			noiseEnergy += noiseSample * noiseSample;
		}

		return 10.0 * std::log10(std::max(targetEnergy, static_cast< double >(kEpsilon))
								 / std::max(noiseEnergy, static_cast< double >(kEpsilon)));
	}

	double computeSegmentalSnr(const std::vector< float > &reference, const std::vector< float > &estimate) {
		const std::size_t sampleCount = std::min(reference.size(), estimate.size());
		if (sampleCount == 0) {
			return 0.0;
		}

		double accumulatedSnr = 0.0;
		std::size_t segmentCount = 0;
		for (std::size_t offset = 0; offset < sampleCount; offset += kFrameSize) {
			const std::size_t segmentLength = std::min<std::size_t>(kFrameSize, sampleCount - offset);
			double signalEnergy = 0.0;
			double noiseEnergy  = 0.0;
			for (std::size_t index = 0; index < segmentLength; ++index) {
				const double referenceSample = static_cast< double >(reference[offset + index]);
				const double errorSample = static_cast< double >(reference[offset + index] - estimate[offset + index]);
				signalEnergy += referenceSample * referenceSample;
				noiseEnergy += errorSample * errorSample;
			}

			if (signalEnergy <= kEpsilon) {
				continue;
			}

			double segmentSnr = 10.0 * std::log10(signalEnergy / std::max(noiseEnergy, static_cast< double >(kEpsilon)));
			segmentSnr        = std::clamp(segmentSnr, -10.0, 35.0);
			accumulatedSnr += segmentSnr;
			++segmentCount;
		}

		return segmentCount > 0 ? accumulatedSnr / static_cast< double >(segmentCount) : 0.0;
	}

	std::vector< double > computeFrameEnergy(const std::vector< float > &samples) {
		std::vector< double > energies;
		energies.reserve((samples.size() + kAlignmentFrameSize - 1) / kAlignmentFrameSize);

		for (std::size_t offset = 0; offset < samples.size(); offset += kAlignmentFrameSize) {
			const std::size_t frameLength =
				std::min< std::size_t >(kAlignmentFrameSize, samples.size() - offset);
			double energy                 = 0.0;
			for (std::size_t index = 0; index < frameLength; ++index) {
				const double sample = samples[offset + index];
				energy += sample * sample;
			}
			energies.push_back(energy / static_cast< double >(frameLength));
		}

		return energies;
	}

	double normalizedCorrelation(const std::vector< double > &reference, const std::vector< double > &estimate,
								 int lagFrames) {
		const std::size_t referenceStart = lagFrames < 0 ? static_cast< std::size_t >(-lagFrames) : 0;
		const std::size_t estimateStart  = lagFrames > 0 ? static_cast< std::size_t >(lagFrames) : 0;
		const std::size_t count =
			std::min(reference.size() - std::min(referenceStart, reference.size()),
					 estimate.size() - std::min(estimateStart, estimate.size()));
		if (count == 0) {
			return 0.0;
		}

		double dotProduct      = 0.0;
		double referenceEnergy = 0.0;
		double estimateEnergy  = 0.0;
		for (std::size_t index = 0; index < count; ++index) {
			const double referenceSample = reference[referenceStart + index];
			const double estimateSample  = estimate[estimateStart + index];
			dotProduct += referenceSample * estimateSample;
			referenceEnergy += referenceSample * referenceSample;
			estimateEnergy += estimateSample * estimateSample;
		}

		return dotProduct / std::sqrt(std::max(referenceEnergy * estimateEnergy, static_cast< double >(kEpsilon)));
	}

	double normalizedSampleCorrelation(const std::vector< float > &reference, const std::vector< float > &estimate,
									 int lagSamples) {
		const std::size_t referenceStart = lagSamples < 0 ? static_cast< std::size_t >(-lagSamples) : 0;
		const std::size_t estimateStart  = lagSamples > 0 ? static_cast< std::size_t >(lagSamples) : 0;
		const std::size_t count =
			std::min(reference.size() - std::min(referenceStart, reference.size()),
					 estimate.size() - std::min(estimateStart, estimate.size()));
		if (count == 0) {
			return 0.0;
		}

		double dotProduct      = 0.0;
		double referenceEnergy = 0.0;
		double estimateEnergy  = 0.0;
		for (std::size_t index = 0; index < count; ++index) {
			const double referenceSample = reference[referenceStart + index];
			const double estimateSample  = estimate[estimateStart + index];
			dotProduct += referenceSample * estimateSample;
			referenceEnergy += referenceSample * referenceSample;
			estimateEnergy += estimateSample * estimateSample;
		}

		return dotProduct / std::sqrt(std::max(referenceEnergy * estimateEnergy, static_cast< double >(kEpsilon)));
	}

	AlignedAudio alignForReferenceMetrics(const std::vector< float > &reference, const std::vector< float > &estimate) {
		constexpr int maxAlignmentLagMs = 250;
		const int maxLagSamples = (SAMPLE_RATE * maxAlignmentLagMs) / 1000;
		const int maxLagFrames  = maxLagSamples / static_cast< int >(kAlignmentFrameSize);
		const std::vector< double > referenceEnergy = computeFrameEnergy(reference);
		const std::vector< double > estimateEnergy  = computeFrameEnergy(estimate);

		int bestLagFrames = 0;
		double bestScore  = -std::numeric_limits< double >::infinity();
		for (int lagFrames = -maxLagFrames; lagFrames <= maxLagFrames; ++lagFrames) {
			const double score = normalizedCorrelation(referenceEnergy, estimateEnergy, lagFrames);
			if (score > bestScore) {
				bestScore     = score;
				bestLagFrames = lagFrames;
			}
		}

		const int coarseLagSamples = bestLagFrames * static_cast< int >(kAlignmentFrameSize);
		int lagSamples             = coarseLagSamples;
		bestScore                  = -std::numeric_limits< double >::infinity();
		const int refinementRadius = static_cast< int >(kAlignmentFrameSize);
		for (int candidateLag = std::max(-maxLagSamples, coarseLagSamples - refinementRadius);
			 candidateLag <= std::min(maxLagSamples, coarseLagSamples + refinementRadius); ++candidateLag) {
			const double score = normalizedSampleCorrelation(reference, estimate, candidateLag);
			if (score > bestScore) {
				bestScore  = score;
				lagSamples = candidateLag;
			}
		}

		const std::size_t referenceStart = lagSamples < 0 ? static_cast< std::size_t >(-lagSamples) : 0;
		const std::size_t estimateStart  = lagSamples > 0 ? static_cast< std::size_t >(lagSamples) : 0;
		const std::size_t clampedReferenceStart = std::min(referenceStart, reference.size());
		const std::size_t clampedEstimateStart  = std::min(estimateStart, estimate.size());
		const std::size_t sampleCount =
			std::min(reference.size() - clampedReferenceStart, estimate.size() - clampedEstimateStart);

		AlignedAudio aligned;
		aligned.lagSamples = lagSamples;
		aligned.reference.assign(
			reference.begin() + static_cast< std::ptrdiff_t >(clampedReferenceStart),
			reference.begin() + static_cast< std::ptrdiff_t >(clampedReferenceStart + sampleCount));
		aligned.estimate.assign(estimate.begin() + static_cast< std::ptrdiff_t >(clampedEstimateStart),
								estimate.begin() + static_cast< std::ptrdiff_t >(clampedEstimateStart + sampleCount));
		return aligned;
	}

	BenchmarkMetrics processPreparedProcessor(SpeechCleanupProcessor &processor, std::vector< float > &samples,
										 float mixFactor) {
		if (!processor.isReady()) {
			throw std::runtime_error("Speech cleanup processor is not ready");
		}
		if (samples.empty()) {
			throw std::runtime_error("Input contains no audio samples");
		}
		requireFiniteSignal(samples, QStringLiteral("Input audio"));

		BenchmarkMetrics metrics;
		metrics.activeModelId   = processor.activeModelId();
		metrics.activeModelPath = processor.activeModelPath();
		metrics.usedFallback    = processor.usedFallback();
		metrics.reportedLatencySamples = processor.latencySamples();
		metrics.mixFactor       = std::clamp(mixFactor, 0.0f, 1.0f);
		metrics.inputSampleCount = samples.size();

		const SignalMetrics inputMetrics = measureSignal(samples);
		metrics.inputSaturatedSampleCount = inputMetrics.saturatedSampleCount;
		metrics.inputOutOfRangeSampleCount = inputMetrics.outOfRangeSampleCount;

		if (metrics.inputSampleCount
			> std::numeric_limits< std::size_t >::max() - metrics.reportedLatencySamples) {
			throw std::runtime_error("Input plus processor latency exceeds the supported sample count");
		}
		metrics.outputSampleCount = metrics.inputSampleCount + metrics.reportedLatencySamples;
		metrics.drainSampleCount  = metrics.reportedLatencySamples;
		const std::size_t framedSampleCount = roundUpToFrameSize(metrics.outputSampleCount);
		metrics.processingPaddingSampleCount = framedSampleCount - metrics.outputSampleCount;

		std::vector< float > framedInput(framedSampleCount, 0.0f);
		std::copy(samples.begin(), samples.end(), framedInput.begin());
		std::vector< float > processed(metrics.outputSampleCount, 0.0f);
		const auto startTime = std::chrono::steady_clock::now();

		std::vector< float > frameBuffer(kFrameSize, 0.0f);
		for (std::size_t offset = 0; offset < framedInput.size(); offset += kFrameSize) {
			std::copy_n(framedInput.data() + offset, frameBuffer.size(), frameBuffer.data());
			processor.processInPlace(frameBuffer.data(), static_cast< unsigned int >(frameBuffer.size()),
									 metrics.mixFactor);
			for (std::size_t frameIndex = 0; frameIndex < frameBuffer.size(); ++frameIndex) {
				if (!std::isfinite(frameBuffer[frameIndex])) {
					throw std::runtime_error(
						QStringLiteral("Speech cleanup output contains a non-finite sample at stream index %1")
							.arg(static_cast< quint64 >(offset + frameIndex))
							.toStdString());
				}
			}

			if (offset < processed.size()) {
				const std::size_t copyCount = std::min< std::size_t >(frameBuffer.size(), processed.size() - offset);
				std::copy_n(frameBuffer.data(), copyCount, processed.data() + offset);
			}
		}

		const auto endTime = std::chrono::steady_clock::now();
		metrics.cpuMs = std::chrono::duration< double, std::milli >(endTime - startTime).count();
		metrics.audioMs = static_cast< double >(metrics.inputSampleCount) * 1000.0 / SAMPLE_RATE;
		metrics.processedAudioMs = static_cast< double >(framedSampleCount) * 1000.0 / SAMPLE_RATE;
		metrics.realTimeFactor = (metrics.audioMs > 0.0) ? (metrics.cpuMs / metrics.audioMs) : 0.0;

		const SignalMetrics outputMetrics = measureSignal(processed);
		metrics.saturatedSampleCount = outputMetrics.saturatedSampleCount;
		metrics.outOfRangeSampleCount = outputMetrics.outOfRangeSampleCount;
		metrics.nonFiniteSampleCount = outputMetrics.nonFiniteSampleCount;
		metrics.clippingCount = outputMetrics.saturatedSampleCount;
		metrics.peak = outputMetrics.peak;
		metrics.rms  = outputMetrics.rms;
		samples = std::move(processed);
		return metrics;
	}

	BenchmarkMetrics processSamples(const Mumble::SpeechCleanup::Selection &selection, std::vector< float > &samples,
									float mixFactor) {
		if (!Mumble::SpeechCleanup::isBackendAvailable(selection.backend)) {
			throw std::runtime_error(
				QStringLiteral("Requested backend is unavailable: %1")
					.arg(Mumble::SpeechCleanup::backendDisplayName(selection.backend))
					.toStdString());
		}

		const auto initializationStart = std::chrono::steady_clock::now();
		std::unique_ptr< SpeechCleanupProcessor > processor = createSpeechCleanupProcessor(selection);
		const auto initializationEnd = std::chrono::steady_clock::now();
		if (!processor || !processor->isReady()) {
			throw std::runtime_error(QStringLiteral("Failed to initialize speech cleanup backend for model %1")
										 .arg(selection.modelId)
										 .toStdString());
		}

		BenchmarkMetrics metrics = processPreparedProcessor(*processor, samples, mixFactor);
		metrics.initializationMs =
			std::chrono::duration< double, std::milli >(initializationEnd - initializationStart).count();
		return metrics;
	}

	class FixedDelayTestProcessor final : public SpeechCleanupProcessor {
	public:
		explicit FixedDelayTestProcessor(unsigned int delay) : m_delay(delay, 0.0f) {}

		bool isReady() const override { return !m_delay.empty(); }
		void reset() override {
			std::fill(m_delay.begin(), m_delay.end(), 0.0f);
			m_position = 0;
		}
		unsigned int latencySamples() const override { return static_cast< unsigned int >(m_delay.size()); }
		void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) override {
			(void) mixFactor;
			for (unsigned int index = 0; index < sampleCount; ++index) {
				const float delayed = m_delay[m_position];
				m_delay[m_position] = samples[index];
				m_position = (m_position + 1) % m_delay.size();
				samples[index] = delayed;
			}
		}

	private:
		std::vector< float > m_delay;
		std::size_t m_position = 0;
	};

	class NonFiniteOutputTestProcessor final : public SpeechCleanupProcessor {
	public:
		bool isReady() const override { return true; }
		void reset() override {}
		unsigned int latencySamples() const override { return 0; }
		void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) override {
			(void) mixFactor;
			if (sampleCount > 0) {
				samples[0] = std::numeric_limits< float >::quiet_NaN();
			}
		}
	};

	void runSelfTests() {
		auto require = [](bool condition, const char *message) {
			if (!condition) {
				throw std::runtime_error(message);
			}
		};

		const SignalMetrics railMetrics = measureSignal({ 0.0f, 1.0f, -1.0f, 1.25f, -0.25f });
		require(railMetrics.saturatedSampleCount == 3, "Saturation self-test failed");
		require(railMetrics.outOfRangeSampleCount == 1, "Out-of-range self-test failed");

		const std::vector< float > nonFinite = { 0.0f, std::numeric_limits< float >::quiet_NaN(),
											 std::numeric_limits< float >::infinity() };
		require(measureSignal(nonFinite).nonFiniteSampleCount == 2, "Non-finite counter self-test failed");
		bool rejectedNonFinite = false;
		try {
			requireFiniteSignal(nonFinite, QStringLiteral("Self-test audio"));
		} catch (const std::runtime_error &) {
			rejectedNonFinite = true;
		}
		require(rejectedNonFinite, "Non-finite validation self-test failed");

		NonFiniteOutputTestProcessor nonFiniteOutputProcessor;
		std::vector< float > finiteInput(kFrameSize, 0.0f);
		bool rejectedNonFiniteOutput = false;
		try {
			(void) processPreparedProcessor(nonFiniteOutputProcessor, finiteInput, 1.0f);
		} catch (const std::runtime_error &) {
			rejectedNonFiniteOutput = true;
		}
		require(rejectedNonFiniteOutput, "Non-finite processor output self-test failed");

		constexpr unsigned int delay = 17;
		FixedDelayTestProcessor processor(delay);
		std::vector< float > input(kFrameSize + 1, 0.0f);
		for (std::size_t index = 0; index < input.size(); ++index) {
			input[index] = static_cast< float >(index + 1) / 1000.0f;
		}
		const std::vector< float > original = input;
		const BenchmarkMetrics metrics = processPreparedProcessor(processor, input, 1.0f);
		require(metrics.inputSampleCount == original.size(), "Drain self-test input length failed");
		require(metrics.outputSampleCount == original.size() + delay, "Drain self-test output length failed");
		require(metrics.drainSampleCount == delay, "Drain self-test latency count failed");
		for (unsigned int index = 0; index < delay; ++index) {
			require(input[index] == 0.0f, "Drain self-test leading delay failed");
		}
		for (std::size_t index = 0; index < original.size(); ++index) {
			require(input[index + delay] == original[index], "Drain self-test tail recovery failed");
		}
	}
} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("speech_cleanup_benchmark"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Offline speech cleanup benchmark runner"));
	parser.addHelpOption();

	const QCommandLineOption backendOption(QStringList() << QStringLiteral("backend"),
										   QStringLiteral("Speech cleanup backend name"), QStringLiteral("backend"));
	const QCommandLineOption modelIdOption(QStringList() << QStringLiteral("model-id"),
										   QStringLiteral("Speech cleanup model identifier"), QStringLiteral("modelId"));
	const QCommandLineOption customModelPathOption(QStringList() << QStringLiteral("custom-model-path"),
												   QStringLiteral("Optional custom model path"),
												   QStringLiteral("path"));
	const QCommandLineOption mixFactorOption(QStringList() << QStringLiteral("mix-factor"),
											 QStringLiteral("Dry/wet cleanup mix factor from 0.0 to 1.0"),
											 QStringLiteral("factor"), QStringLiteral("1.0"));
	const QCommandLineOption analysisOnlyOption(
		QStringList() << QStringLiteral("analysis-only"),
		QStringLiteral("Measure the input against --clean-reference without running a cleanup processor"));
	const QCommandLineOption selfTestOption(
		QStringList() << QStringLiteral("self-test"),
		QStringLiteral("Run deterministic benchmark metric and drain self-tests"));
	const QCommandLineOption inputOption(QStringList() << QStringLiteral("input"),
										 QStringLiteral("Input WAV or raw file"), QStringLiteral("path"));
	const QCommandLineOption inputFormatOption(QStringList() << QStringLiteral("input-format"),
											   QStringLiteral("Input format: auto, wav, s16le, f32le"),
											   QStringLiteral("format"), QStringLiteral("auto"));
	const QCommandLineOption inputSampleRateOption(QStringList() << QStringLiteral("sample-rate"),
												   QStringLiteral("Input sample rate for raw files"),
												   QStringLiteral("sampleRate"));
	const QCommandLineOption inputChannelsOption(QStringList() << QStringLiteral("channels"),
												 QStringLiteral("Input channel count for raw files"),
												 QStringLiteral("channels"), QStringLiteral("1"));
	const QCommandLineOption cleanReferenceOption(QStringList() << QStringLiteral("clean-reference"),
												  QStringLiteral("Optional clean-reference WAV or raw file"),
												  QStringLiteral("path"));
	const QCommandLineOption cleanFormatOption(QStringList() << QStringLiteral("clean-format"),
											   QStringLiteral("Clean-reference format: auto, wav, s16le, f32le"),
											   QStringLiteral("format"), QStringLiteral("auto"));
	const QCommandLineOption cleanSampleRateOption(QStringList() << QStringLiteral("clean-sample-rate"),
												   QStringLiteral("Clean-reference sample rate for raw files"),
												   QStringLiteral("sampleRate"));
	const QCommandLineOption cleanChannelsOption(QStringList() << QStringLiteral("clean-channels"),
												 QStringLiteral("Clean-reference channel count for raw files"),
												 QStringLiteral("channels"), QStringLiteral("1"));
	const QCommandLineOption outputOption(QStringList() << QStringLiteral("output"),
										  QStringLiteral("Output WAV path"), QStringLiteral("path"));
	const QCommandLineOption reportOption(QStringList() << QStringLiteral("report"),
										  QStringLiteral("Output JSON report path"), QStringLiteral("path"));

	parser.addOption(backendOption);
	parser.addOption(modelIdOption);
	parser.addOption(customModelPathOption);
	parser.addOption(mixFactorOption);
	parser.addOption(analysisOnlyOption);
	parser.addOption(selfTestOption);
	parser.addOption(inputOption);
	parser.addOption(inputFormatOption);
	parser.addOption(inputSampleRateOption);
	parser.addOption(inputChannelsOption);
	parser.addOption(cleanReferenceOption);
	parser.addOption(cleanFormatOption);
	parser.addOption(cleanSampleRateOption);
	parser.addOption(cleanChannelsOption);
	parser.addOption(outputOption);
	parser.addOption(reportOption);

	if (hasHelpArgument(argc, argv)) {
		QTextStream(stdout) << parser.helpText();
		return 0;
	}

	parser.process(app);

	try {
		if (parser.isSet(selfTestOption)) {
			runSelfTests();
			QTextStream(stdout) << "speech_cleanup_benchmark self-test passed\n";
			return 0;
		}

		const bool analysisOnly = parser.isSet(analysisOnlyOption);
		if (!parser.isSet(inputOption) || !parser.isSet(outputOption) || !parser.isSet(reportOption)
			|| (!analysisOnly && (!parser.isSet(backendOption) || !parser.isSet(modelIdOption)))) {
			throw std::runtime_error(
				"Required options: --input, --output, --report and, unless --analysis-only is used, --backend and --model-id");
		}
		if (analysisOnly && !parser.isSet(cleanReferenceOption)) {
			throw std::runtime_error("--analysis-only requires --clean-reference");
		}

		Mumble::SpeechCleanup::Selection selection;
		if (!analysisOnly) {
			selection = Mumble::SpeechCleanup::normalizeSelection({
				parseBackend(parser.value(backendOption)),
				parser.value(modelIdOption),
				parser.value(customModelPathOption),
			});
		}
		bool mixFactorValid = false;
		const float parsedMixFactor = parser.value(mixFactorOption).toFloat(&mixFactorValid);
		if (!mixFactorValid || !std::isfinite(parsedMixFactor)) {
			throw std::runtime_error("--mix-factor must be a finite number");
		}
		const float mixFactor = std::clamp(parsedMixFactor, 0.0f, 1.0f);

		const LoadedAudio input = loadAudio(parser.value(inputOption), parseInputFormat(parser.value(inputFormatOption)),
											parser.value(inputSampleRateOption).toInt(),
											parser.value(inputChannelsOption).toInt());
		if (input.sampleRate != SAMPLE_RATE) {
			throw std::runtime_error(QStringLiteral("Expected %1 Hz input, got %2 Hz")
										 .arg(SAMPLE_RATE)
										 .arg(input.sampleRate)
										 .toStdString());
		}

		std::vector< float > processed = input.samples;
		BenchmarkMetrics metrics;
		if (analysisOnly) {
			if (processed.empty()) {
				throw std::runtime_error("Input contains no audio samples");
			}
			requireFiniteSignal(processed, QStringLiteral("Input audio"));
			const SignalMetrics signalMetrics = measureSignal(processed);
			metrics.inputSampleCount = processed.size();
			metrics.outputSampleCount = processed.size();
			metrics.inputSaturatedSampleCount = signalMetrics.saturatedSampleCount;
			metrics.inputOutOfRangeSampleCount = signalMetrics.outOfRangeSampleCount;
			metrics.saturatedSampleCount = signalMetrics.saturatedSampleCount;
			metrics.outOfRangeSampleCount = signalMetrics.outOfRangeSampleCount;
			metrics.nonFiniteSampleCount = signalMetrics.nonFiniteSampleCount;
			metrics.clippingCount = signalMetrics.saturatedSampleCount;
			metrics.peak = signalMetrics.peak;
			metrics.rms  = signalMetrics.rms;
			metrics.audioMs = static_cast< double >(processed.size()) * 1000.0 / SAMPLE_RATE;
			metrics.processedAudioMs = metrics.audioMs;
		} else {
			metrics = processSamples(selection, processed, mixFactor);
		}

		std::optional< LoadedAudio > cleanReference;
		if (parser.isSet(cleanReferenceOption)) {
			cleanReference = loadAudio(parser.value(cleanReferenceOption), parseInputFormat(parser.value(cleanFormatOption)),
									  parser.value(cleanSampleRateOption).toInt(),
									  parser.value(cleanChannelsOption).toInt());
			if (cleanReference->sampleRate != SAMPLE_RATE) {
				throw std::runtime_error(QStringLiteral("Expected %1 Hz clean reference, got %2 Hz")
											 .arg(SAMPLE_RATE)
											 .arg(cleanReference->sampleRate)
											 .toStdString());
			}
			if (cleanReference->samples.empty()) {
				throw std::runtime_error("Clean reference contains no audio samples");
			}
			requireFiniteSignal(cleanReference->samples, QStringLiteral("Clean reference"));

			const AlignedAudio aligned = alignForReferenceMetrics(cleanReference->samples, processed);
			metrics.alignmentLagSamples = aligned.lagSamples;
			metrics.siSdr               = computeSiSdr(aligned.reference, aligned.estimate);
			metrics.segmentalSnr        = computeSegmentalSnr(aligned.reference, aligned.estimate);
		}

		writeOutputWav(parser.value(outputOption), processed, input.sampleRate);

		nlohmann::json report = {
			{ "analysis_only", analysisOnly },
			{ "backend", analysisOnly ? "AnalysisOnly" : Mumble::SpeechCleanup::backendDisplayName(selection.backend) },
			{ "model_id", analysisOnly ? std::string() : selection.modelId.toStdString() },
			{ "custom_model_path", analysisOnly ? std::string() : selection.customModelPath.toStdString() },
			{ "active_model_id", metrics.activeModelId.toStdString() },
			{ "active_model_path", metrics.activeModelPath.toStdString() },
			{ "used_fallback", metrics.usedFallback },
			{ "reported_latency_samples", metrics.reportedLatencySamples },
			{ "reported_latency_ms",
			  static_cast< double >(metrics.reportedLatencySamples) * 1000.0 / SAMPLE_RATE },
			{ "mix_factor", metrics.mixFactor },
			{ "input_path", parser.value(inputOption).toStdString() },
			{ "input_format", input.resolvedFormat.toStdString() },
			{ "output_path", parser.value(outputOption).toStdString() },
			{ "report_path", parser.value(reportOption).toStdString() },
			{ "sample_rate", input.sampleRate },
			{ "sample_count", processed.size() },
			{ "input_sample_count", metrics.inputSampleCount },
			{ "output_sample_count", metrics.outputSampleCount },
			{ "drain_sample_count", metrics.drainSampleCount },
			{ "processing_padding_sample_count", metrics.processingPaddingSampleCount },
			{ "initialization_ms", metrics.initializationMs },
			{ "cold_start_ms", metrics.initializationMs },
			{ "cpu_ms", metrics.cpuMs },
			{ "total_cpu_ms", metrics.initializationMs + metrics.cpuMs },
			{ "audio_ms", metrics.audioMs },
			{ "processed_audio_ms", metrics.processedAudioMs },
			{ "rtf", metrics.realTimeFactor },
			{ "clipping_count", metrics.clippingCount },
			{ "clipping_definition", "absolute output sample >= 1.0" },
			{ "input_saturated_sample_count", metrics.inputSaturatedSampleCount },
			{ "input_out_of_range_sample_count", metrics.inputOutOfRangeSampleCount },
			{ "saturated_sample_count", metrics.saturatedSampleCount },
			{ "out_of_range_sample_count", metrics.outOfRangeSampleCount },
			{ "non_finite_sample_count", metrics.nonFiniteSampleCount },
			{ "peak", metrics.peak },
			{ "rms", metrics.rms },
		};

		if (cleanReference) {
			report["clean_reference_path"] = parser.value(cleanReferenceOption).toStdString();
			report["clean_reference_format"] = cleanReference->resolvedFormat.toStdString();
			report["clean_reference_sample_count"] = cleanReference->samples.size();
			report["alignment_lag_samples"] = metrics.alignmentLagSamples;
			report["alignment_lag_ms"] = static_cast< double >(metrics.alignmentLagSamples) * 1000.0 / SAMPLE_RATE;
			report["si_sdr"] = *metrics.siSdr;
			report["segmental_snr"] = *metrics.segmentalSnr;
		}

		std::ofstream reportStream(parser.value(reportOption).toStdString(), std::ios::binary);
		if (!reportStream) {
			throw std::runtime_error(QStringLiteral("Failed to open report path %1")
										 .arg(parser.value(reportOption))
										 .toStdString());
		}
		reportStream << report.dump(2) << '\n';
		reportStream.close();
		return 0;
	} catch (const std::exception &e) {
		qCritical("speech_cleanup_benchmark: %s", e.what());
		return 1;
	}
}
