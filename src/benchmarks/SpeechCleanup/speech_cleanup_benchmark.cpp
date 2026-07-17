// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Audio.h"
#include "InputEnhancement.h"
#include "InputEnhancementLightProcessor.h"
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
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
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
		QString activeModelSha256;
		QString requestedRecipeId;
		QString requestedProfile;
		QString activeProfile;
		QString activeEngine;
		QString fallbackReason;
		std::uint32_t recipeRevision = 0;
		std::uint64_t processedFrames = 0;
		std::uint64_t neuralFrames = 0;
		std::uint64_t deadlineMisses = 0;
		std::uint64_t fallbackCount = 0;
		std::uint64_t maximumProcessingNanoseconds = 0;
		std::uint64_t workerProcessingFrames = 0;
		std::uint64_t workerTotalProcessingNanoseconds = 0;
		std::uint64_t workerMaximumProcessingNanoseconds = 0;
		std::uint64_t workerProcessingP99Nanoseconds = 0;
		std::uint64_t callbackP50Nanoseconds = 0;
		std::uint64_t callbackP95Nanoseconds = 0;
		std::uint64_t callbackP99Nanoseconds = 0;
		std::uint64_t lightSpeechProbabilityP10 = 0;
		std::uint64_t lightSpeechProbabilityP50 = 0;
		std::uint64_t lightSpeechProbabilityP90 = 0;
		std::uint64_t lightNoisePsdSumP10 = 0;
		std::uint64_t lightNoisePsdSumP50 = 0;
		std::uint64_t lightNoisePsdSumP90 = 0;
		std::uint64_t lightNoiseToSignalPpmP10 = 0;
		std::uint64_t lightNoiseToSignalPpmP50 = 0;
		std::uint64_t lightNoiseToSignalPpmP90 = 0;
		std::uint64_t lightDryRmsPpbP10 = 0;
		std::uint64_t lightDryRmsPpbP50 = 0;
		std::uint64_t lightDryRmsPpbP90 = 0;
		bool usedFallback       = false;
		unsigned int reportedLatencySamples = 0;
		int alignmentLagSamples = 0;
		std::optional< double > siSdr;
		std::optional< double > segmentalSnr;
	};

	std::uint64_t nearestRankPercentile(std::vector< std::uint64_t > values, double percentile) {
		if (values.empty()) {
			return 0;
		}
		std::sort(values.begin(), values.end());
		const double rank = std::ceil(std::clamp(percentile, 0.0, 1.0) * static_cast< double >(values.size()));
		const std::size_t index = static_cast< std::size_t >(std::max(1.0, rank)) - 1;
		return values[std::min(index, values.size() - 1)];
	}

	QString profileName(Mumble::InputEnhancement::Profile profile) {
		using Profile = Mumble::InputEnhancement::Profile;
		switch (profile) {
			case Profile::Original:
				return QStringLiteral("Original");
			case Profile::Light:
				return QStringLiteral("Light");
			case Profile::Balanced:
				return QStringLiteral("Balanced");
			case Profile::Quality:
				return QStringLiteral("Quality");
			case Profile::Auto:
				return QStringLiteral("Auto");
			case Profile::VoiceFocus:
				return QStringLiteral("VoiceFocus");
		}
		return QStringLiteral("Original");
	}

	Mumble::InputEnhancement::Profile parseProfile(const QString &value) {
		using Profile = Mumble::InputEnhancement::Profile;
		const QString normalized = value.trimmed();
		if (normalized.compare(QLatin1String("Crisp"), Qt::CaseInsensitive) == 0) {
			return Profile::Quality;
		}
		for (const Profile profile : { Profile::Original, Profile::Light, Profile::Balanced, Profile::Quality,
									   Profile::Auto, Profile::VoiceFocus }) {
			if (profileName(profile).compare(normalized, Qt::CaseInsensitive) == 0) {
				return profile;
			}
		}
		throw std::runtime_error(QStringLiteral("Unsupported input enhancement profile: %1").arg(value).toStdString());
	}

	QString engineName(Mumble::InputEnhancement::Engine engine) {
		using Engine = Mumble::InputEnhancement::Engine;
		switch (engine) {
			case Engine::None:
				return QStringLiteral("None");
			case Engine::Speex:
				return QStringLiteral("Speex");
			case Engine::RNNoise:
				return QStringLiteral("RNNoise");
			case Engine::DeepFilterNet:
				return QStringLiteral("DeepFilterNet");
			case Engine::DTLN:
				return QStringLiteral("DTLN");
		}
		return QStringLiteral("None");
	}

	QString fallbackReasonName(Mumble::InputEnhancement::FallbackReason reason) {
		using Reason = Mumble::InputEnhancement::FallbackReason;
		switch (reason) {
			case Reason::None:
				return QStringLiteral("None");
			case Reason::ProcessorUnavailable:
				return QStringLiteral("ProcessorUnavailable");
			case Reason::ProcessorNotReady:
				return QStringLiteral("ProcessorNotReady");
			case Reason::ProcessorFallback:
				return QStringLiteral("ProcessorFallback");
			case Reason::UnexpectedModel:
				return QStringLiteral("UnexpectedModel");
			case Reason::LatencyBudgetExceeded:
				return QStringLiteral("LatencyBudgetExceeded");
			case Reason::InvalidFrame:
				return QStringLiteral("InvalidFrame");
			case Reason::InvalidOutput:
				return QStringLiteral("InvalidOutput");
			case Reason::DeadlineExceeded:
				return QStringLiteral("DeadlineExceeded");
			case Reason::ProcessorException:
				return QStringLiteral("ProcessorException");
		}
		return QStringLiteral("Unknown");
	}

	Mumble::InputEnhancement::CpuClass parseCpuClass(const QString &value) {
		using CpuClass = Mumble::InputEnhancement::CpuClass;
		if (value.compare(QLatin1String("low"), Qt::CaseInsensitive) == 0) {
			return CpuClass::Low;
		}
		if (value.compare(QLatin1String("standard"), Qt::CaseInsensitive) == 0) {
			return CpuClass::Standard;
		}
		if (value.compare(QLatin1String("high"), Qt::CaseInsensitive) == 0) {
			return CpuClass::High;
		}
		throw std::runtime_error(QStringLiteral("Unsupported CPU class: %1").arg(value).toStdString());
	}

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
		std::vector< std::uint64_t > callbackDurations;
		callbackDurations.reserve(framedInput.size() / kFrameSize);

		std::vector< float > frameBuffer(kFrameSize, 0.0f);
		for (std::size_t offset = 0; offset < framedInput.size(); offset += kFrameSize) {
			std::copy_n(framedInput.data() + offset, frameBuffer.size(), frameBuffer.data());
			const auto callbackStartedAt = std::chrono::steady_clock::now();
			processor.processInPlace(frameBuffer.data(), static_cast< unsigned int >(frameBuffer.size()),
									 metrics.mixFactor);
			const auto callbackFinishedAt = std::chrono::steady_clock::now();
			callbackDurations.push_back(static_cast< std::uint64_t >(
				std::chrono::duration_cast< std::chrono::nanoseconds >(callbackFinishedAt - callbackStartedAt).count()));
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
		metrics.callbackP50Nanoseconds = nearestRankPercentile(callbackDurations, 0.50);
		metrics.callbackP95Nanoseconds = nearestRankPercentile(callbackDurations, 0.95);
		metrics.callbackP99Nanoseconds = nearestRankPercentile(callbackDurations, 0.99);

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

	BenchmarkMetrics processProductProfile(const Mumble::InputEnhancement::ResolveRequest &request,
										std::vector< float > &samples, const QString &authorizedModelSha256,
										const QString &authorizedModelPath) {
		using namespace Mumble::InputEnhancement;
		if (samples.empty()) {
			throw std::runtime_error("Input contains no audio samples");
		}
		requireFiniteSignal(samples, QStringLiteral("Input audio"));

		const Recipe recipe = RecipeCatalog::resolve(request);
		// Product fallback uses the 10 ms catastrophe limit. Balanced/Quality
		// p99 targets (5/8 ms) are evaluated from benchmark diagnostics instead
		// of turning one scheduling outlier into an audible profile rollback.
		constexpr std::uint64_t deadline = 10'000'000;
		const auto initializationStart = std::chrono::steady_clock::now();
		Pipeline pipeline(Pipeline::ProcessorFactory {}, Pipeline::NanosecondClock {}, deadline);
		LightProcessor lightProcessor;
		const bool lightProfile = recipe.effectiveProfile() == Profile::Light;
		const bool configured = lightProfile ? lightProcessor.configure(recipe, pipeline)
															 : pipeline.configure(recipe, authorizedModelSha256, authorizedModelPath);
		if (!configured) {
			const Diagnostics diagnostics = pipeline.diagnostics();
			throw std::runtime_error(
				QStringLiteral("Product recipe %1 failed to initialize: %2")
					.arg(recipe.id(), fallbackReasonName(diagnostics.fallbackReason()))
					.toStdString());
		}

		const auto initializationEnd = std::chrono::steady_clock::now();

		BenchmarkMetrics metrics;
		metrics.initializationMs =
			std::chrono::duration< double, std::milli >(initializationEnd - initializationStart).count();
		metrics.mixFactor        = recipe.mixFactor();
		metrics.inputSampleCount = samples.size();
		metrics.reportedLatencySamples = pipeline.latencySamples();
		const SignalMetrics inputMetrics = measureSignal(samples);
		metrics.inputSaturatedSampleCount = inputMetrics.saturatedSampleCount;
		metrics.inputOutOfRangeSampleCount = inputMetrics.outOfRangeSampleCount;

		if (metrics.inputSampleCount
			> std::numeric_limits< std::size_t >::max() - metrics.reportedLatencySamples) {
			throw std::runtime_error("Input plus product-pipeline latency exceeds the supported sample count");
		}
		metrics.outputSampleCount = metrics.inputSampleCount + metrics.reportedLatencySamples;
		metrics.drainSampleCount  = metrics.reportedLatencySamples;
		const std::size_t framedSampleCount = roundUpToFrameSize(metrics.outputSampleCount);
		metrics.processingPaddingSampleCount = framedSampleCount - metrics.outputSampleCount;

		std::vector< float > framedInput(framedSampleCount, 0.0f);
		std::copy(samples.begin(), samples.end(), framedInput.begin());
		std::vector< float > processed(metrics.outputSampleCount, 0.0f);
		std::array< float, frameSamples > frame = {};
		std::array< std::int16_t, frameSamples > lightFrame = {};
		std::array< std::int32_t, frameSamples > lightSignalPsd = {};
		std::vector< std::uint64_t > callbackDurations;
		callbackDurations.reserve(framedInput.size() / frameSamples);
		std::vector< std::uint64_t > lightSpeechProbabilities;
		lightSpeechProbabilities.reserve(framedInput.size() / frameSamples);
		std::vector< std::uint64_t > lightNoisePsdSums;
		lightNoisePsdSums.reserve(framedInput.size() / frameSamples);
		std::vector< std::uint64_t > lightNoiseToSignalPpm;
		lightNoiseToSignalPpm.reserve(framedInput.size() / frameSamples);
		std::vector< std::uint64_t > lightDryRmsPpb;
		lightDryRmsPpb.reserve(framedInput.size() / frameSamples);
		const auto startedAt = std::chrono::steady_clock::now();
		for (std::size_t offset = 0; offset < framedInput.size(); offset += frameSamples) {
			std::copy_n(framedInput.data() + offset, frame.size(), frame.data());
			// Offline playback has no hardware callback interval in which an
			// asynchronous processor can advance. Pace it before starting the
			// callback timer so latency/RTF remain causal while callback timing
			// still measures only the real-time processFrame() path.
			if (!pipeline.prepareOfflineFrame()) {
				const Diagnostics diagnostics = pipeline.diagnostics();
				throw std::runtime_error(
					QStringLiteral("Product pipeline offline drain failed at frame %1: %2")
						.arg(static_cast< quint64 >(offset / frameSamples))
						.arg(fallbackReasonName(diagnostics.fallbackReason()))
						.toStdString());
			}
			const auto callbackStartedAt = std::chrono::steady_clock::now();
			bool neuralProcessed = false;
			if (recipe.usesNeuralProcessor()) {
				neuralProcessed = pipeline.processFrame(frame);
			}
			if (recipe.usesNeuralProcessor() && !neuralProcessed) {
				const Diagnostics diagnostics = pipeline.diagnostics();
				throw std::runtime_error(
					QStringLiteral("Product pipeline failed at frame %1: %2")
						.arg(static_cast< quint64 >(offset / frameSamples))
						.arg(fallbackReasonName(diagnostics.fallbackReason()))
						.toStdString());
			}
			if (lightProfile) {
				for (std::size_t index = 0; index < frame.size(); ++index) {
					const float scaled = std::clamp(frame[index] * 32768.0f, -32768.0f, 32767.0f);
					lightFrame[index] = static_cast< std::int16_t >(std::lrint(scaled));
				}
				double dryEnergy = 0.0;
				for (const std::int16_t sample : lightFrame) {
					const double normalized = static_cast< double >(sample) / 32768.0;
					dryEnergy += normalized * normalized;
				}
				lightDryRmsPpb.push_back(static_cast< std::uint64_t >(std::llround(
					std::sqrt(dryEnergy / static_cast< double >(lightFrame.size())) * 1'000'000'000.0)));
				if (!lightProcessor.processFrame(lightFrame.data(), frameSamples)) {
					const Diagnostics diagnostics = pipeline.diagnostics();
					throw std::runtime_error(
						QStringLiteral("Product Light pipeline failed at frame %1: %2")
							.arg(static_cast< quint64 >(offset / frameSamples))
							.arg(fallbackReasonName(diagnostics.fallbackReason()))
							.toStdString());
				}
				const int speechProbability = lightProcessor.lastSpeechProbability();
				lightSpeechProbabilities.push_back(static_cast< std::uint64_t >(speechProbability));
				const std::uint64_t noiseSum = lightProcessor.lastNoisePsdSum();
				lightNoisePsdSums.push_back(noiseSum);
				if (lightProcessor.copySignalPsd(lightSignalPsd.data(), lightSignalPsd.size())) {
					const std::uint64_t signalSum = std::accumulate(
						lightSignalPsd.cbegin(), lightSignalPsd.cend(), std::uint64_t { 0 },
						[](std::uint64_t total, std::int32_t value) {
							return total + static_cast< std::uint64_t >(std::max(value, std::int32_t { 0 }));
						});
					const double ratio = signalSum == 0
						? 0.0
						: (static_cast< double >(noiseSum) * 1'000'000.0) / static_cast< double >(signalSum);
					lightNoiseToSignalPpm.push_back(static_cast< std::uint64_t >(std::llround(ratio)));
				}
				for (std::size_t index = 0; index < frame.size(); ++index) {
					frame[index] = static_cast< float >(lightFrame[index]) / 32768.0f;
				}
			}
			const auto callbackFinishedAt = std::chrono::steady_clock::now();
			const std::uint64_t callbackDuration = static_cast< std::uint64_t >(
				std::chrono::duration_cast< std::chrono::nanoseconds >(callbackFinishedAt - callbackStartedAt).count());
			callbackDurations.push_back(callbackDuration);
			if (offset < processed.size()) {
				const std::size_t count = std::min< std::size_t >(frame.size(), processed.size() - offset);
				std::copy_n(frame.data(), count, processed.data() + offset);
			}
		}
		// Observe the two jobs still in flight at the fixed scheduling horizon.
		// This emits no extra audio and therefore leaves the 2400-sample timeline
		// and tail unchanged.
		if (!pipeline.finishOfflineProcessing()) {
			const Diagnostics diagnostics = pipeline.diagnostics();
			throw std::runtime_error(
				QStringLiteral("Product pipeline final offline drain failed: %1")
					.arg(fallbackReasonName(diagnostics.fallbackReason()))
					.toStdString());
		}
		const auto finishedAt = std::chrono::steady_clock::now();
		metrics.cpuMs = std::chrono::duration< double, std::milli >(finishedAt - startedAt).count();
		metrics.audioMs = static_cast< double >(metrics.inputSampleCount) * 1000.0 / SAMPLE_RATE;
		metrics.processedAudioMs = static_cast< double >(framedSampleCount) * 1000.0 / SAMPLE_RATE;
		metrics.realTimeFactor = metrics.audioMs > 0.0 ? metrics.cpuMs / metrics.audioMs : 0.0;
		metrics.callbackP50Nanoseconds = nearestRankPercentile(callbackDurations, 0.50);
		metrics.callbackP95Nanoseconds = nearestRankPercentile(callbackDurations, 0.95);
		metrics.callbackP99Nanoseconds = nearestRankPercentile(callbackDurations, 0.99);
		metrics.lightSpeechProbabilityP10 = nearestRankPercentile(lightSpeechProbabilities, 0.10);
		metrics.lightSpeechProbabilityP50 = nearestRankPercentile(lightSpeechProbabilities, 0.50);
		metrics.lightSpeechProbabilityP90 = nearestRankPercentile(lightSpeechProbabilities, 0.90);
		metrics.lightNoisePsdSumP10 = nearestRankPercentile(lightNoisePsdSums, 0.10);
		metrics.lightNoisePsdSumP50 = nearestRankPercentile(lightNoisePsdSums, 0.50);
		metrics.lightNoisePsdSumP90 = nearestRankPercentile(lightNoisePsdSums, 0.90);
		metrics.lightNoiseToSignalPpmP10 = nearestRankPercentile(lightNoiseToSignalPpm, 0.10);
		metrics.lightNoiseToSignalPpmP50 = nearestRankPercentile(lightNoiseToSignalPpm, 0.50);
		metrics.lightNoiseToSignalPpmP90 = nearestRankPercentile(lightNoiseToSignalPpm, 0.90);
		metrics.lightDryRmsPpbP10 = nearestRankPercentile(lightDryRmsPpb, 0.10);
		metrics.lightDryRmsPpbP50 = nearestRankPercentile(lightDryRmsPpb, 0.50);
		metrics.lightDryRmsPpbP90 = nearestRankPercentile(lightDryRmsPpb, 0.90);

		const Diagnostics diagnostics = pipeline.diagnostics();
		metrics.requestedRecipeId = diagnostics.requestedRecipeId();
		metrics.recipeRevision    = diagnostics.recipeRevision();
		metrics.requestedProfile  = profileName(diagnostics.requestedProfile());
		metrics.activeProfile     = profileName(diagnostics.activeProfile());
		metrics.activeEngine      = engineName(diagnostics.activeEngine());
		metrics.activeModelId     = diagnostics.activeModelId();
		metrics.activeModelSha256 = diagnostics.activeModelSha256();
		metrics.processedFrames   = diagnostics.processedFrames();
		metrics.neuralFrames      = diagnostics.neuralFrames();
		metrics.deadlineMisses    = diagnostics.deadlineMisses();
		metrics.fallbackCount     = diagnostics.fallbackCount();
		metrics.maximumProcessingNanoseconds = diagnostics.maximumProcessingNanoseconds();
		metrics.workerProcessingFrames = diagnostics.workerProcessingFrames();
		metrics.workerTotalProcessingNanoseconds = diagnostics.workerTotalProcessingNanoseconds();
		metrics.workerMaximumProcessingNanoseconds = diagnostics.workerMaximumProcessingNanoseconds();
		metrics.workerProcessingP99Nanoseconds = diagnostics.workerProcessingP99Nanoseconds();
		metrics.usedFallback      = diagnostics.fallbackActive();
		metrics.fallbackReason    = fallbackReasonName(diagnostics.fallbackReason());

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

		Mumble::InputEnhancement::ResolveRequest originalRequest;
		originalRequest.profile = Mumble::InputEnhancement::Profile::Original;
		originalRequest.backendAvailability = Mumble::InputEnhancement::BackendAvailability::compiled();
		std::vector< float > productOriginal(kFrameSize + 13, 0.0f);
		for (std::size_t index = 0; index < productOriginal.size(); ++index) {
			productOriginal[index] = static_cast< float >(static_cast< int >(index % 101) - 50) / 100.0f;
		}
		const std::vector< float > productOriginalReference = productOriginal;
		const BenchmarkMetrics productMetrics = processProductProfile(originalRequest, productOriginal, {}, {});
		require(productOriginal == productOriginalReference,
				"Product Original benchmark path changed PCM samples");
		require(productMetrics.reportedLatencySamples == 0 && productMetrics.drainSampleCount == 0,
				"Product Original benchmark path added latency");
		require(productMetrics.requestedProfile == QLatin1String("Original")
				&& productMetrics.activeProfile == QLatin1String("Original")
				&& productMetrics.activeEngine == QLatin1String("None") && !productMetrics.usedFallback,
				"Product Original benchmark diagnostics failed");
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
	const QCommandLineOption profileOption(
		QStringList() << QStringLiteral("profile"),
		QStringLiteral("Run an input-enhancement recipe: Original, Light, Balanced, Quality, VoiceFocus, or Auto"),
		QStringLiteral("profile"));
	const QCommandLineOption noiseReductionOption(
		QStringList() << QStringLiteral("noise-reduction"),
		QStringLiteral("Product noise-reduction control from 0 to 100"), QStringLiteral("value"),
		QStringLiteral("50"));
	const QCommandLineOption naturalCrispOption(
		QStringList() << QStringLiteral("natural-clear") << QStringLiteral("natural-crisp"),
		QStringLiteral("Product Natural-to-Clear control from 0 to 100"), QStringLiteral("value"),
		QStringLiteral("50"));
	const QCommandLineOption cpuClassOption(
		QStringList() << QStringLiteral("cpu-class"),
		QStringLiteral("CPU capability used by Auto: Low, Standard, or High"), QStringLiteral("class"),
		QStringLiteral("Standard"));
	const QCommandLineOption authorizedModelSha256Option(
		QStringList() << QStringLiteral("authorized-model-sha256"),
		QStringLiteral("Verified lowercase SHA-256 to attest in product-pipeline diagnostics"),
		QStringLiteral("sha256"));
	const QCommandLineOption authorizedModelPathOption(
		QStringList() << QStringLiteral("authorized-model-path"),
		QStringLiteral("Canonical model asset path bound to --authorized-model-sha256"),
		QStringLiteral("path"));
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
	parser.addOption(profileOption);
	parser.addOption(noiseReductionOption);
	parser.addOption(naturalCrispOption);
	parser.addOption(cpuClassOption);
	parser.addOption(authorizedModelSha256Option);
	parser.addOption(authorizedModelPathOption);
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

		const bool analysisOnly  = parser.isSet(analysisOnlyOption);
		const bool productProfileMode = parser.isSet(profileOption);
		const bool directBackendMode  = parser.isSet(backendOption) || parser.isSet(modelIdOption)
									|| parser.isSet(customModelPathOption);
		if (!parser.isSet(inputOption) || !parser.isSet(outputOption) || !parser.isSet(reportOption)
			|| (!analysisOnly && !productProfileMode
				&& (!parser.isSet(backendOption) || !parser.isSet(modelIdOption)))) {
			throw std::runtime_error(
				"Required options: --input, --output, --report and either --profile or --backend plus --model-id");
		}
		if (analysisOnly && !parser.isSet(cleanReferenceOption)) {
			throw std::runtime_error("--analysis-only requires --clean-reference");
		}
		if (analysisOnly && (productProfileMode || directBackendMode)) {
			throw std::runtime_error("--analysis-only cannot be combined with --profile or backend selection");
		}
		if (productProfileMode && directBackendMode) {
			throw std::runtime_error("--profile cannot be combined with --backend, --model-id, or --custom-model-path");
		}
		if (productProfileMode && parser.isSet(mixFactorOption)) {
			throw std::runtime_error("--mix-factor is Expert-only; product profiles use their versioned safe recipe");
		}
		if (!productProfileMode
			&& (parser.isSet(authorizedModelSha256Option) || parser.isSet(authorizedModelPathOption))) {
			throw std::runtime_error("Model authorization requires --profile");
		}
		if (parser.isSet(authorizedModelSha256Option) != parser.isSet(authorizedModelPathOption)) {
			throw std::runtime_error(
				"--authorized-model-sha256 and --authorized-model-path must be supplied together");
		}

		Mumble::SpeechCleanup::Selection selection;
		if (!analysisOnly && !productProfileMode) {
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

		Mumble::InputEnhancement::ResolveRequest productRequest;
		if (productProfileMode) {
			bool noiseReductionValid = false;
			bool naturalCrispValid    = false;
			const int noiseReduction = parser.value(noiseReductionOption).toInt(&noiseReductionValid);
			const int naturalCrisp    = parser.value(naturalCrispOption).toInt(&naturalCrispValid);
			if (!noiseReductionValid || noiseReduction < 0 || noiseReduction > 100) {
				throw std::runtime_error("--noise-reduction must be an integer from 0 to 100");
			}
			if (!naturalCrispValid || naturalCrisp < 0 || naturalCrisp > 100) {
				throw std::runtime_error("--natural-clear must be an integer from 0 to 100");
			}
			productRequest.profile             = parseProfile(parser.value(profileOption));
			productRequest.noiseReduction      = noiseReduction;
			productRequest.naturalCrisp        = naturalCrisp;
			productRequest.cpuClass            = parseCpuClass(parser.value(cpuClassOption));
			productRequest.backendAvailability = Mumble::InputEnhancement::BackendAvailability::compiled();
		}

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
		} else if (productProfileMode) {
			metrics = processProductProfile(productRequest, processed,
										parser.value(authorizedModelSha256Option),
										parser.value(authorizedModelPathOption));
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
			{ "processing_mode", analysisOnly ? "analysis-only" : (productProfileMode ? "product-profile" : "expert-backend") },
			{ "backend", analysisOnly ? "AnalysisOnly" : (productProfileMode ? metrics.activeEngine.toStdString() : Mumble::SpeechCleanup::backendDisplayName(selection.backend)) },
			{ "model_id", analysisOnly ? std::string() : (productProfileMode ? metrics.activeModelId.toStdString() : selection.modelId.toStdString()) },
			{ "custom_model_path", (!analysisOnly && !productProfileMode) ? selection.customModelPath.toStdString() : std::string() },
			{ "requested_profile", metrics.requestedProfile.toStdString() },
			{ "active_profile", metrics.activeProfile.toStdString() },
			{ "active_engine", metrics.activeEngine.toStdString() },
			{ "requested_recipe_id", metrics.requestedRecipeId.toStdString() },
			{ "recipe_revision", metrics.recipeRevision },
			{ "active_model_id", metrics.activeModelId.toStdString() },
			{ "active_model_path", metrics.activeModelPath.toStdString() },
			{ "active_model_sha256", metrics.activeModelSha256.toStdString() },
			{ "used_fallback", metrics.usedFallback },
			{ "fallback_reason", metrics.fallbackReason.toStdString() },
			{ "fallback_count", metrics.fallbackCount },
			{ "deadline_misses", metrics.deadlineMisses },
			{ "processed_frames", metrics.processedFrames },
			{ "neural_frames", metrics.neuralFrames },
			{ "maximum_processing_ns", metrics.maximumProcessingNanoseconds },
			{ "maximum_processing_ms", static_cast< double >(metrics.maximumProcessingNanoseconds) / 1'000'000.0 },
			{ "worker_processing_frames", metrics.workerProcessingFrames },
			{ "worker_processing_total_ms",
			  static_cast< double >(metrics.workerTotalProcessingNanoseconds) / 1'000'000.0 },
			{ "worker_processing_average_ms",
			  metrics.workerProcessingFrames > 0
				  ? static_cast< double >(metrics.workerTotalProcessingNanoseconds)
						/ static_cast< double >(metrics.workerProcessingFrames) / 1'000'000.0
				  : 0.0 },
			{ "worker_processing_maximum_ms",
			  static_cast< double >(metrics.workerMaximumProcessingNanoseconds) / 1'000'000.0 },
			{ "worker_processing_p99_ms",
			  static_cast< double >(metrics.workerProcessingP99Nanoseconds) / 1'000'000.0 },
			{ "worker_rtf",
			  metrics.audioMs > 0.0
				  ? static_cast< double >(metrics.workerTotalProcessingNanoseconds) / 1'000'000.0 / metrics.audioMs
				  : 0.0 },
			{ "callback_p50_ms", static_cast< double >(metrics.callbackP50Nanoseconds) / 1'000'000.0 },
			{ "callback_p95_ms", static_cast< double >(metrics.callbackP95Nanoseconds) / 1'000'000.0 },
			{ "callback_p99_ms", static_cast< double >(metrics.callbackP99Nanoseconds) / 1'000'000.0 },
			{ "light_speech_probability_p10", metrics.lightSpeechProbabilityP10 },
			{ "light_speech_probability_p50", metrics.lightSpeechProbabilityP50 },
			{ "light_speech_probability_p90", metrics.lightSpeechProbabilityP90 },
			{ "light_noise_psd_sum_p10", metrics.lightNoisePsdSumP10 },
			{ "light_noise_psd_sum_p50", metrics.lightNoisePsdSumP50 },
			{ "light_noise_psd_sum_p90", metrics.lightNoisePsdSumP90 },
			{ "light_noise_to_signal_ppm_p10", metrics.lightNoiseToSignalPpmP10 },
			{ "light_noise_to_signal_ppm_p50", metrics.lightNoiseToSignalPpmP50 },
			{ "light_noise_to_signal_ppm_p90", metrics.lightNoiseToSignalPpmP90 },
			{ "light_dry_rms_ppb_p10", metrics.lightDryRmsPpbP10 },
			{ "light_dry_rms_ppb_p50", metrics.lightDryRmsPpbP50 },
			{ "light_dry_rms_ppb_p90", metrics.lightDryRmsPpbP90 },
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
			// Kept alongside the historical cpu_ms key: the benchmark uses elapsed
			// wall time so asynchronous inference and bounded waits are included.
			{ "processing_wall_ms", metrics.cpuMs },
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
		// Qt logging can be compiled out or redirected by the client object
		// library. This executable is a CI tool, so failures must always reach the
		// invoking harness through stderr.
		std::fprintf(stderr, "speech_cleanup_benchmark: %s\n", e.what());
		std::fflush(stderr);
		return 1;
	}
}
