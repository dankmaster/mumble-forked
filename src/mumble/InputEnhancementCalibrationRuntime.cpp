// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationRuntime.h"

#include "InputEnhancementLightProcessor.h"

#include <opus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace Mumble::InputEnhancement {
namespace {
	template< typename T > void secureClear(std::vector< T > &values) noexcept {
		volatile T *storage = values.empty() ? nullptr : values.data();
		for (std::size_t index = 0; index < values.size(); ++index) {
			storage[index] = T{};
		}
		std::atomic_signal_fence(std::memory_order_seq_cst);
		values.clear();
	}

	template< typename T, std::size_t Size > void secureClear(std::array< T, Size > &values) noexcept {
		volatile T *storage = values.data();
		for (std::size_t index = 0; index < values.size(); ++index) {
			storage[index] = T{};
		}
		std::atomic_signal_fence(std::memory_order_seq_cst);
	}

	template< typename T, std::size_t Size > struct SecureArray final {
		std::array< T, Size > values = {};
		~SecureArray() {
			volatile T *storage = values.data();
			for (std::size_t index = 0; index < values.size(); ++index) {
				storage[index] = T{};
			}
			std::atomic_signal_fence(std::memory_order_seq_cst);
		}
	};

	struct SecureFloatVector final {
		std::vector< float > values;
		~SecureFloatVector() { secureClear(values); }
	};

	double meanSquare(std::span< const float > samples) noexcept {
		double sum = 0.0;
		for (float sample : samples) {
			sum += static_cast< double >(sample) * static_cast< double >(sample);
		}
		return samples.empty() ? 0.0 : sum / static_cast< double >(samples.size());
	}

	bool opusRoundTrip(std::span< const float > input, const CalibrationOpusConfiguration &configuration,
					   SecureFloatVector &decoded) {
		if (configuration.framesPerPacket < 1 || configuration.framesPerPacket > 6) {
			return false;
		}
		const int application               = configuration.allowLowDelay && configuration.bitrate >= 64'000
												  ? OPUS_APPLICATION_RESTRICTED_LOWDELAY
											  : configuration.bitrate >= 32'000 ? OPUS_APPLICATION_AUDIO
																				: OPUS_APPLICATION_VOIP;
		const unsigned int opusFrameSamples = frameSamples * configuration.framesPerPacket;
		int error                           = OPUS_OK;
		OpusEncoder *encoder = opus_encoder_create(CalibrationSession::sampleRate, 1, application, &error);
		if (!encoder || error != OPUS_OK) {
			if (encoder) {
				opus_encoder_destroy(encoder);
			}
			return false;
		}
		OpusDecoder *decoder = opus_decoder_create(CalibrationSession::sampleRate, 1, &error);
		if (!decoder || error != OPUS_OK) {
			opus_encoder_destroy(encoder);
			if (decoder) {
				opus_decoder_destroy(decoder);
			}
			return false;
		}
		opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(std::clamp(configuration.bitrate, 8'000, 512'000)));

		decoded.values.clear();
		decoded.values.reserve(((input.size() + opusFrameSamples - 1) / opusFrameSamples) * opusFrameSamples);
		SecureArray< float, frameSamples * 6 > inputFrame;
		SecureArray< float, frameSamples * 6 > decodedFrame;
		SecureArray< unsigned char, 1275 > packet;
		for (std::size_t offset = 0; offset < input.size(); offset += opusFrameSamples) {
			inputFrame.values.fill(0.0f);
			const std::size_t count = std::min< std::size_t >(opusFrameSamples, input.size() - offset);
			std::copy_n(input.begin() + static_cast< std::ptrdiff_t >(offset), count, inputFrame.values.begin());
			const int encoded =
				opus_encode_float(encoder, inputFrame.values.data(), static_cast< int >(opusFrameSamples),
								  packet.values.data(), static_cast< opus_int32 >(packet.values.size()));
			if (encoded <= 0) {
				opus_decoder_destroy(decoder);
				opus_encoder_destroy(encoder);
				return false;
			}
			const int samples = opus_decode_float(decoder, packet.values.data(), encoded, decodedFrame.values.data(),
												  static_cast< int >(opusFrameSamples), 0);
			std::fill(packet.values.begin(), packet.values.end(), 0);
			if (samples != static_cast< int >(opusFrameSamples)) {
				opus_decoder_destroy(decoder);
				opus_encoder_destroy(encoder);
				return false;
			}
			decoded.values.insert(decoded.values.end(), decodedFrame.values.begin(),
								  decodedFrame.values.begin() + static_cast< std::ptrdiff_t >(opusFrameSamples));
		}
		opus_decoder_destroy(decoder);
		opus_encoder_destroy(encoder);
		decoded.values.resize(input.size());
		return true;
	}

	bool runProductPipeline(std::span< const float > input, const CalibrationSession::Selection &selection,
							const CalibrationPackageAuthorization &authorization, const CpuClass cpuClass,
							SecureFloatVector &alignedOutput) {
		ResolveRequest request;
		request.profile             = selection.profile;
		request.noiseReduction      = selection.noiseReduction;
		request.naturalCrisp        = selection.naturalCrisp;
		request.cpuClass            = cpuClass;
		request.backendAvailability = BackendAvailability::compiled();
		const Recipe recipe         = RecipeCatalog::resolve(request);
		if (recipe.effectiveProfile() != selection.profile) {
			return false;
		}
		QString authorizedModelSha256;
		QString authorizedModelPath;
		if (!authorization.recipeAuthorized(recipe, authorizedModelSha256, authorizedModelPath)) {
			return false;
		}

		Pipeline pipeline;
		LightProcessor lightProcessor;
		const bool lightProfile = recipe.effectiveProfile() == Profile::Light;
		const bool configured = lightProfile ? lightProcessor.configure(recipe, pipeline)
										 : pipeline.configure(recipe, authorizedModelSha256, authorizedModelPath);
		if (!configured) {
			return false;
		}
		const unsigned int latency = pipeline.latencySamples();
		const std::size_t required = input.size() + latency;
		const std::size_t frames   = (required + frameSamples - 1) / frameSamples;
		SecureFloatVector causal;
		causal.values.reserve(frames * frameSamples);
		std::array< float, frameSamples > frame = {};
		SecureArray< std::int16_t, frameSamples > lightFrame;
		for (std::size_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
			frame.fill(0.0f);
			const std::size_t inputOffset = frameIndex * frameSamples;
			if (inputOffset < input.size()) {
				const std::size_t count = std::min< std::size_t >(frameSamples, input.size() - inputOffset);
				std::copy_n(input.begin() + static_cast< std::ptrdiff_t >(inputOffset), count, frame.begin());
			}
			if (!pipeline.prepareOfflineFrame()) {
				return false;
			}
			if (lightProfile) {
				for (std::size_t index = 0; index < frame.size(); ++index) {
					const float scaled = std::clamp(frame[index] * 32768.0f, -32768.0f, 32767.0f);
					lightFrame.values[index] = static_cast< std::int16_t >(std::lrint(scaled));
				}
				if (!lightProcessor.processFrame(lightFrame.values.data(), frameSamples)) {
					return false;
				}
				for (std::size_t index = 0; index < frame.size(); ++index) {
					frame[index] = static_cast< float >(lightFrame.values[index]) / 32768.0f;
				}
			} else {
				const bool processed = pipeline.processFrame(frame);
				if (recipe.usesNeuralProcessor() && !processed) {
					return false;
				}
			}
			causal.values.insert(causal.values.end(), frame.begin(), frame.end());
		}
		if (!pipeline.finishOfflineProcessing()) {
			return false;
		}
		if (pipeline.fallbackActive() || causal.values.size() < latency + input.size()) {
			return false;
		}
		alignedOutput.values.assign(causal.values.begin() + static_cast< std::ptrdiff_t >(latency),
									causal.values.begin() + static_cast< std::ptrdiff_t >(latency + input.size()));
		return true;
	}

	double combinedNoiseImprovementDb(std::span< const float > roomNoise, std::span< const float > processedRoom,
									  std::span< const float > localNoise,
									  std::span< const float > processedLocalNoise) noexcept {
		double inputEnergy      = meanSquare(roomNoise) * static_cast< double >(roomNoise.size());
		double outputEnergy     = meanSquare(processedRoom) * static_cast< double >(processedRoom.size());
		std::size_t sampleCount = roomNoise.size();
		if (!localNoise.empty() && processedLocalNoise.size() == localNoise.size()) {
			inputEnergy += meanSquare(localNoise) * static_cast< double >(localNoise.size());
			outputEnergy += meanSquare(processedLocalNoise) * static_cast< double >(processedLocalNoise.size());
			sampleCount += localNoise.size();
		}
		if (sampleCount == 0) {
			return 0.0;
		}
		const double inputPower  = inputEnergy / static_cast< double >(sampleCount);
		const double outputPower = outputEnergy / static_cast< double >(sampleCount);
		return 10.0 * std::log10((inputPower + 1.0e-12) / (outputPower + 1.0e-12));
	}

	DeviceProfileState deviceStateFor(const Settings &settings, const DeviceIdentity &identity) {
		if (const DeviceProfileState *existing = findDeviceProfile(settings, identity)) {
			return *existing;
		}
		DeviceProfileState state;
		state.identity = identity;
		return state;
	}
} // namespace

CalibrationCandidateEvaluator::Output::~Output() {
	// Candidate playback is derived from captured speech and follows the same
	// in-memory-only lifetime contract as the raw calibration capture. Owning the
	// wipe here closes every return and exception path.
	secureClear(playbackPcm);
}

CalibrationPackageAuthorization
	CalibrationPackageAuthorization::signedPackage(std::vector< AuthorizedRecipe > recipes) {
	return signedPackage(QStringLiteral("input-recipes-v2"), std::move(recipes));
}

CalibrationPackageAuthorization
	CalibrationPackageAuthorization::signedPackage(QString catalogRevision, std::vector< AuthorizedRecipe > recipes) {
	return catalogBoundPackage(std::move(catalogRevision), std::move(recipes));
}

CalibrationPackageAuthorization CalibrationPackageAuthorization::catalogBoundPackage(
	QString catalogRevision, std::vector< AuthorizedRecipe > recipes) {
	CalibrationPackageAuthorization authorization;
	authorization.mode            = Mode::CatalogBound;
	authorization.catalogRevision = std::move(catalogRevision);
	authorization.recipes         = std::move(recipes);
	return authorization;
}

CalibrationPackageAuthorization CalibrationPackageAuthorization::explicitUnmanagedBuildZero() {
	CalibrationPackageAuthorization authorization;
	authorization.mode            = Mode::ExplicitUnmanagedBuildZero;
	authorization.catalogRevision = QStringLiteral("unmanaged-build-zero");
	return authorization;
}

CalibrationPackageAuthorization::AuthorizedRecipe
	CalibrationPackageAuthorization::authorizeRecipe(const Recipe &recipe, QString sha256Hex,
													 QString canonicalModelPath, QString relativeModelPath) {
	AuthorizedRecipe authorized;
	authorized.recipeId             = recipe.id();
	authorized.revision             = recipe.revision();
	authorized.requestedProfile     = recipe.requestedProfile();
	authorized.effectiveProfile     = recipe.effectiveProfile();
	authorized.engine               = recipe.engine();
	authorized.modelId              = recipe.modelId();
	authorized.noiseReduction       = recipe.noiseReduction();
	authorized.naturalCrisp         = recipe.naturalCrisp();
	authorized.mixFactor            = recipe.mixFactor();
	authorized.latencyBudgetSamples = recipe.latencyBudgetSamples();
	authorized.minimumCpuClass      = recipe.minimumCpuClass();
	authorized.sha256Hex            = std::move(sha256Hex);
	authorized.canonicalModelPath   = std::move(canonicalModelPath);
	authorized.relativeModelPath    = std::move(relativeModelPath);
	return authorized;
}

bool CalibrationPackageAuthorization::recipeAuthorized(const Recipe &recipe, QString &authorizedSha256Hex,
													   QString &authorizedModelPath,
													   QString *authorizedRelativeModelPath) const {
	authorizedSha256Hex.clear();
	authorizedModelPath.clear();
	if (authorizedRelativeModelPath) {
		authorizedRelativeModelPath->clear();
	}
	if (mode == Mode::ExplicitUnmanagedBuildZero) {
		// A manifest-free developer client may calibrate Original and Light, but
		// it must not invent a neural model identity. Build-0 packages with parsed
		// manifests use an exact catalog-bound authorization assembled by
		// AudioInput instead of this fallback mode.
		return !recipe.usesNeuralProcessor();
	}
	if (mode == Mode::DenyNeural) {
		return !recipe.usesNeuralProcessor();
	}
	if (mode != Mode::CatalogBound) {
		return false;
	}
	for (const AuthorizedRecipe &authorized : recipes) {
		if (authorized.recipeId != recipe.id() || authorized.revision != recipe.revision()
			|| authorized.requestedProfile != recipe.requestedProfile()
			|| authorized.effectiveProfile != recipe.effectiveProfile() || authorized.engine != recipe.engine()
			|| authorized.modelId != recipe.modelId() || authorized.noiseReduction != recipe.noiseReduction()
			|| authorized.naturalCrisp != recipe.naturalCrisp() || authorized.mixFactor != recipe.mixFactor()
			|| authorized.latencyBudgetSamples != recipe.latencyBudgetSamples()
			|| authorized.minimumCpuClass != recipe.minimumCpuClass()) {
			continue;
		}
		if (!recipe.usesNeuralProcessor()) {
			return authorized.sha256Hex.isEmpty() && authorized.relativeModelPath.isEmpty();
		}
		if (authorized.sha256Hex.size() != 64 || authorized.canonicalModelPath.isEmpty()) {
			continue;
		}
		bool validHex = true;
		for (const QChar character : authorized.sha256Hex) {
			const ushort value = character.unicode();
			if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F'))) {
				validHex = false;
				break;
			}
		}
		if (validHex) {
			authorizedSha256Hex = authorized.sha256Hex.toLower();
			authorizedModelPath = authorized.canonicalModelPath;
			if (authorizedRelativeModelPath) {
				*authorizedRelativeModelPath = authorized.relativeModelPath;
			}
			return true;
		}
	}
	return false;
}

LocalCalibrationCandidateEvaluator::LocalCalibrationCandidateEvaluator(CalibrationOpusConfiguration opus,
																	   CalibrationPackageAuthorization authorization,
																	   const CpuClass cpuClass,
																	   CaptureDeviceContext captureDevice) noexcept
	: m_opus(opus), m_authorization(std::move(authorization)), m_cpuClass(cpuClass),
	  m_captureDevice(std::move(captureDevice)) {
}

bool LocalCalibrationCandidateEvaluator::evaluate(const CalibrationSession::CaptureView &capture,
												  const CalibrationSession::Selection &selection, Output &output) {
	secureClear(output.playbackPcm);
	output.candidate = {};
	output.recipeBinding.reset();
	output.candidate.selection = selection;
	if (capture.guidedVoice.empty() || capture.roomNoise.empty() || selection.recipeToken == 0) {
		return false;
	}
	// Auto is a live policy rather than one executable recipe. The UI may show
	// it as the eventual mode, but a blind clip must always name exact DSP.
	if (selection.profile == Profile::Auto) {
		output.candidate.eligible = false;
		return true;
	}
	ResolveRequest availabilityRequest;
	availabilityRequest.profile             = selection.profile;
	availabilityRequest.noiseReduction      = selection.noiseReduction;
	availabilityRequest.naturalCrisp        = selection.naturalCrisp;
	availabilityRequest.cpuClass            = m_cpuClass;
	availabilityRequest.backendAvailability = BackendAvailability::compiled();
	availabilityRequest.captureDevice       = m_captureDevice;
	if (!profileReadiness(availabilityRequest).selectable) {
		output.candidate.eligible = false;
		return true;
	}
	const Recipe candidateRecipe            = RecipeCatalog::resolve(availabilityRequest);
	if (candidateRecipe.effectiveProfile() != selection.profile) {
		output.candidate.eligible = false;
		return true;
	}
	QString authorizedModelSha256;
	QString authorizedModelPath;
	QString authorizedRelativeModelPath;
	if (!m_authorization.recipeAuthorized(candidateRecipe, authorizedModelSha256, authorizedModelPath,
										  &authorizedRelativeModelPath)) {
		output.candidate.eligible = false;
		return true;
	}
	const QString bindingCatalogRevision = m_authorization.catalogRevision.isEmpty()
											   ? QStringLiteral("deny-neural-local")
											   : m_authorization.catalogRevision;
	output.recipeBinding = recipeBindingForRecipe(candidateRecipe, bindingCatalogRevision, authorizedModelSha256,
												  authorizedRelativeModelPath);

	SecureFloatVector referenceVoice;
	SecureFloatVector processedVoice;
	SecureFloatVector processedRoom;
	SecureFloatVector processedLocalNoise;
	SecureFloatVector opusVoice;
	const bool processed =
		runProductPipeline(capture.guidedVoice, selection, m_authorization, m_cpuClass, processedVoice)
		&& runProductPipeline(capture.roomNoise, selection, m_authorization, m_cpuClass, processedRoom)
		&& (capture.localNoise.empty()
			|| runProductPipeline(capture.localNoise, selection, m_authorization, m_cpuClass,
								  processedLocalNoise));
	if (!opusRoundTrip(capture.guidedVoice, m_opus, referenceVoice) || !processed
		|| !opusRoundTrip(processedVoice.values, m_opus, opusVoice)) {
		return false;
	}

	const double referencePower = meanSquare(referenceVoice.values);
	const double outputPower    = meanSquare(opusVoice.values);
	const double gain = outputPower > 1.0e-12 ? std::clamp(std::sqrt(referencePower / outputPower), 0.25, 4.0) : 1.0;
	for (float &sample : opusVoice.values) {
		sample = std::clamp(static_cast< float >(static_cast< double >(sample) * gain), -1.0f, 1.0f);
	}

	double errorPower = 0.0;
	for (std::size_t index = 0; index < opusVoice.values.size(); ++index) {
		const double error = static_cast< double >(opusVoice.values[index]) - referenceVoice.values[index];
		errorPower += error * error;
	}
	errorPower /= static_cast< double >(opusVoice.values.size());
	const double normalizedError    = errorPower / std::max(referencePower, 1.0e-9);
	const double fidelity           = 1.0 / (1.0 + 8.0 * normalizedError);
	const double noiseImprovementDb = combinedNoiseImprovementDb(capture.roomNoise, processedRoom.values,
																 capture.localNoise, processedLocalNoise.values);
	const double noiseScore         = std::clamp(noiseImprovementDb / 20.0, -1.0, 1.0);

	output.candidate.objectiveScore                = 0.75 * fidelity + 0.25 * noiseScore;
	output.candidate.eligible                      = true;
	output.candidate.localPipelineAndOpusEvaluated = true;
	output.candidate.loudnessMatched               = true;
	output.playbackPcm                             = std::move(opusVoice.values);
	return true;
}

CalibrationRuntimeBridge::CalibrationRuntimeBridge(std::unique_ptr< CalibrationCandidateEvaluator > evaluator,
																	   const CpuClass readinessCpuClass)
	: m_evaluator(std::move(evaluator)), m_readinessCpuClass(readinessCpuClass) {
	if (!m_evaluator) {
		m_evaluator = std::make_unique< LocalCalibrationCandidateEvaluator >();
	}
	publishState();
}

CalibrationRuntimeBridge::CalibrationRuntimeBridge(std::span< float > preallocatedStorage,
													   std::unique_ptr< CalibrationCandidateEvaluator > evaluator,
													   const CpuClass readinessCpuClass)
	: m_session(preallocatedStorage), m_evaluator(std::move(evaluator)), m_readinessCpuClass(readinessCpuClass) {
	if (!m_evaluator) {
		m_evaluator = std::make_unique< LocalCalibrationCandidateEvaluator >();
	}
	publishState();
}

CalibrationRuntimeBridge::~CalibrationRuntimeBridge() {
	pauseCallback();
	if (!m_session.transmissionAllowed()) {
		m_session.abort();
	}
	clearPlayback();
}

bool CalibrationRuntimeBridge::start(const DeviceIdentity &identity, const DefaultPreference &previousPreference,
									 bool captureOptionalLocalNoise, std::uint64_t blindSeed,
									 std::optional< RecipeBinding > previousRecipeBinding) {
	if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()
		|| runtimeAutoAdaptationEnabled(previousPreference)
		|| (previousRecipeBinding && !recipeBindingMatchesPreference(*previousRecipeBinding, previousPreference))
		|| (previousPreference.profile != Profile::Original && !previousRecipeBinding)) {
		return false;
	}
	pauseCallback();
	clearPlayback();
	m_identity              = identity;
	m_previousPreference    = previousPreference;
	m_previousRecipeBinding = std::move(previousRecipeBinding);
	m_hasDraftPreference    = false;
	m_draftRecipeBinding.reset();
	const bool started =
		m_session.start(selectionForPreference(previousPreference), captureOptionalLocalNoise, blindSeed);
	publishState();
	return started;
}

std::size_t CalibrationRuntimeBridge::appendPcmFromCallback(const short *samples, unsigned int sampleCount) noexcept {
	if (!samples || sampleCount == 0 || !m_captureEnabled.load(std::memory_order_acquire)) {
		return 0;
	}
	if (m_callbackActive.test_and_set(std::memory_order_acquire)) {
		return 0;
	}
	if (!m_captureEnabled.load(std::memory_order_acquire)) {
		m_callbackActive.clear(std::memory_order_release);
		return 0;
	}

	std::size_t consumed = 0;
	while (consumed < sampleCount && captureState(m_session.state())) {
		const unsigned int count = static_cast< unsigned int >(
			std::min< std::size_t >(frameSamples, static_cast< std::size_t >(sampleCount) - consumed));
		for (unsigned int index = 0; index < count; ++index) {
			m_callbackFrame[index] = static_cast< float >(samples[consumed + index]) / 32768.0f;
		}
		const std::size_t accepted = m_session.appendPcm(std::span< const float >(m_callbackFrame.data(), count));
		consumed += accepted;
		if (accepted < count) {
			break;
		}
	}
	// This scratch frame is raw microphone audio too. Do not retain the most
	// recent callback after it has been copied into the bounded session store.
	clearCallbackFrame();
	const CalibrationSession::State current = m_session.state();
	m_publishedState.store(current, std::memory_order_release);
	if (!captureState(current)) {
		m_captureEnabled.store(false, std::memory_order_release);
	}
	if (m_session.transmissionAllowed()) {
		m_transmissionBlocked.store(false, std::memory_order_release);
	}
	m_callbackActive.clear(std::memory_order_release);
	return consumed;
}

bool CalibrationRuntimeBridge::advance() noexcept {
	pauseCallback();
	const bool advanced = m_session.advance();
	publishState();
	return advanced;
}

bool CalibrationRuntimeBridge::skipOptionalLocalNoise() noexcept {
	pauseCallback();
	const bool skipped = m_session.skipOptionalLocalNoise();
	publishState();
	return skipped;
}

bool CalibrationRuntimeBridge::evaluateCandidates(std::span< const CalibrationSession::Selection > candidates) {
	return evaluateCandidates(candidates, {});
}

bool CalibrationRuntimeBridge::evaluateCandidates(std::span< const CalibrationSession::Selection > candidates,
												  const CalibrationEvaluationObserver &observer) {
	pauseCallback();
	clearPlayback();
	if (m_session.state() != CalibrationSession::State::Evaluating) {
		publishState();
		return false;
	}
	if (candidates.empty() || candidates.size() > CalibrationSession::maximumCandidates) {
		m_session.failEvaluation();
		m_hasDraftPreference = false;
		m_draftRecipeBinding.reset();
		publishState();
		return false;
	}
	auto cancellationRequested = [&observer]() noexcept {
		return observer.cancelRequested && observer.cancelRequested->load(std::memory_order_acquire);
	};
	auto reportProgress = [&observer, total = candidates.size()](std::size_t completed) noexcept {
		if (observer.progress) {
			observer.progress(observer.context, completed, total);
		}
	};
	if (cancellationRequested()) {
		m_session.abort();
		publishState();
		return false;
	}
	reportProgress(0);

	const CalibrationSession::CaptureView capture = m_session.captureView();
	for (std::size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
		const CalibrationSession::Selection &selection = candidates[candidateIndex];
		if (cancellationRequested()) {
			m_session.abort();
			clearPlayback();
			publishState();
			return false;
		}
		CalibrationCandidateEvaluator::Output output;
		bool evaluated = false;
		try {
			evaluated = evaluator().evaluate(capture, selection, output);
		} catch (...) {
			evaluated = false;
		}
		if (!evaluated) {
			secureClear(output.playbackPcm);
			m_session.failEvaluation();
			clearPlayback();
			publishState();
			return false;
		}
		if (cancellationRequested()) {
			secureClear(output.playbackPcm);
			m_session.abort();
			clearPlayback();
			publishState();
			return false;
		}
		output.candidate.selection = selection;
		if (!m_session.recordCandidate(output.candidate)) {
			secureClear(output.playbackPcm);
			clearPlayback();
			m_hasDraftPreference = false;
			m_draftRecipeBinding.reset();
			publishState();
			return false;
		}
		m_evaluated[m_evaluatedCount].selection     = selection;
		m_evaluated[m_evaluatedCount].playbackPcm   = std::move(output.playbackPcm);
		m_evaluated[m_evaluatedCount].recipeBinding = std::move(output.recipeBinding);
		++m_evaluatedCount;
		reportProgress(candidateIndex + 1);
	}
	const bool finished = m_session.finishEvaluation();
	if (!finished) {
		clearPlayback();
	}
	publishState();
	return finished;
}

CalibrationSession::BlindPair CalibrationRuntimeBridge::blindPair() noexcept {
	pauseCallback();
	const CalibrationSession::BlindPair pair = m_session.blindPair();
	publishState();
	return pair;
}

std::span< const float > CalibrationRuntimeBridge::playbackForToken(std::uint64_t playbackToken) noexcept {
	pauseCallback();
	const CalibrationSession::Selection *selection = m_session.selectionForPlaybackToken(playbackToken);
	if (!selection) {
		publishState();
		return {};
	}
	for (std::size_t index = 0; index < m_evaluatedCount; ++index) {
		if (m_evaluated[index].selection.recipeToken == selection->recipeToken) {
			const std::span< const float > playback = m_evaluated[index].playbackPcm;
			publishState();
			return playback;
		}
	}
	publishState();
	return {};
}

bool CalibrationRuntimeBridge::selectBlindWinner(std::uint64_t playbackToken) noexcept {
	pauseCallback();
	const bool selected = m_session.selectBlindWinner(playbackToken);
	if (selected) {
		const CalibrationSession::Selection *selection = m_session.draftSelection();
		if (selection) {
			m_draftPreference           = preferenceForSelection(*selection);
			m_draftPreference.autoAdapt = selection->profile == Profile::Auto || m_previousPreference.autoAdapt;
			m_hasDraftPreference        = true;
			m_draftRecipeBinding.reset();
			for (std::size_t index = 0; index < m_evaluatedCount; ++index) {
				if (m_evaluated[index].selection.recipeToken == selection->recipeToken) {
					m_draftRecipeBinding = m_evaluated[index].recipeBinding;
					break;
				}
			}
		}
	}
	publishState();
	return selected;
}

bool CalibrationRuntimeBridge::apply(Settings &settings, qint64 nowEpochMs) {
	pauseCallback();
	ResolveRequest readinessRequest;
	readinessRequest.profile             = m_draftPreference.profile;
	readinessRequest.noiseReduction      = m_draftPreference.reduction;
	readinessRequest.naturalCrisp        = m_draftPreference.character;
	readinessRequest.cpuClass            = m_readinessCpuClass;
	readinessRequest.backendAvailability = BackendAvailability::compiled();
	readinessRequest.captureDevice      = CaptureDeviceContext::liveDevice(m_identity.backendId, m_identity.stable);
	const ProfileReadiness readiness = profileReadiness(readinessRequest);
	const Recipe readyRecipe         = RecipeCatalog::resolve(readinessRequest);
	if (!m_hasDraftPreference || !m_draftRecipeBinding || !readiness.selectable
		|| readyRecipe.effectiveProfile() != m_draftPreference.profile
		|| !recipeBindingMatches(*m_draftRecipeBinding, readyRecipe, m_draftRecipeBinding->catalogRevision,
								 m_draftRecipeBinding->modelSha256, m_draftRecipeBinding->modelRelativePath)
		|| !recipeBindingMatchesPreference(*m_draftRecipeBinding, m_draftPreference)
		|| (m_previousPreference.profile != Profile::Original
			&& (!m_previousRecipeBinding
				|| !recipeBindingMatchesPreference(*m_previousRecipeBinding, m_previousPreference)))
		|| m_session.state() != CalibrationSession::State::DraftReady) {
		m_session.abort();
		clearPlayback();
		m_hasDraftPreference = false;
		m_draftRecipeBinding.reset();
		publishState();
		return false;
	}
	try {
		Settings updated                 = settings;
		DeviceProfileState state         = deviceStateFor(updated, m_identity);
		state.identity                   = m_identity;
		state.lastKnownGood              = m_previousPreference;
		state.lastKnownGoodRecipeBinding = m_previousRecipeBinding;
		state.preference                 = m_draftPreference;
		state.pendingRecipeBinding       = m_draftRecipeBinding;
		state.calibrated                 = true;
		state.lastUsedEpochMs            = nowEpochMs;
		state.pendingValidation          = true;
		state.lastRollbackReason.clear();
		state.legacyOverride.reset();
		state.rollbackUndoPreference.reset();
		state.rollbackUndoRecipeBinding.reset();
		if (!upsertDeviceProfile(updated, std::move(state)) || !m_session.apply()) {
			m_session.abort();
			clearPlayback();
			m_hasDraftPreference = false;
			m_draftRecipeBinding.reset();
			publishState();
			return false;
		}
		settings = std::move(updated);
	} catch (...) {
		m_session.abort();
		clearPlayback();
		m_hasDraftPreference = false;
		m_draftRecipeBinding.reset();
		publishState();
		return false;
	}
	clearPlayback();
	publishState();
	return true;
}

bool CalibrationRuntimeBridge::cancel() noexcept {
	pauseCallback();
	const bool cancelled = m_session.cancel();
	clearPlayback();
	m_hasDraftPreference = false;
	m_draftRecipeBinding.reset();
	publishState();
	return cancelled;
}

bool CalibrationRuntimeBridge::abort() noexcept {
	pauseCallback();
	const bool aborted = m_session.abort();
	clearPlayback();
	m_hasDraftPreference = false;
	m_draftRecipeBinding.reset();
	publishState();
	return aborted;
}

CalibrationSession::State CalibrationRuntimeBridge::state() const noexcept {
	return m_publishedState.load(std::memory_order_acquire);
}

CalibrationSession::LevelMetrics CalibrationRuntimeBridge::levelMetrics() noexcept {
	pauseCallback();
	const CalibrationSession::LevelMetrics metrics = m_session.levelMetrics();
	publishState();
	return metrics;
}

bool CalibrationRuntimeBridge::transmissionBlocked() const noexcept {
	return m_transmissionBlocked.load(std::memory_order_acquire);
}

bool CalibrationRuntimeBridge::rawAudioCleared() noexcept {
	pauseCallback();
	const bool callbackFrameCleared =
		std::all_of(m_callbackFrame.cbegin(), m_callbackFrame.cend(), [](float sample) { return sample == 0.0f; });
	const bool cleared = m_session.rawAudioCleared() && callbackFrameCleared;
	publishState();
	return cleared;
}

bool CalibrationRuntimeBridge::playbackBuffersCleared() const noexcept {
	for (const EvaluatedCandidate &candidate : m_evaluated) {
		for (float sample : candidate.playbackPcm) {
			if (sample != 0.0f) {
				return false;
			}
		}
	}
	return true;
}

const DefaultPreference *CalibrationRuntimeBridge::draftPreference() noexcept {
	pauseCallback();
	publishState();
	return m_hasDraftPreference ? &m_draftPreference : nullptr;
}

const RecipeBinding *CalibrationRuntimeBridge::draftRecipeBinding() noexcept {
	return m_hasDraftPreference && m_draftRecipeBinding ? &*m_draftRecipeBinding : nullptr;
}

CalibrationSession::Selection
	CalibrationRuntimeBridge::selectionForPreference(const DefaultPreference &preference) noexcept {
	CalibrationSession::Selection selection;
	selection.profile        = preference.profile;
	selection.noiseReduction = std::clamp(preference.reduction, 0, 100);
	selection.naturalCrisp   = std::clamp(preference.character, 0, 100);
	std::uint32_t token      = 2166136261U;
	auto mix                 = [&token](std::uint32_t value) noexcept {
        token ^= value;
        token *= 16777619U;
	};
	mix(static_cast< std::uint32_t >(selection.profile));
	mix(static_cast< std::uint32_t >(selection.noiseReduction));
	mix(static_cast< std::uint32_t >(selection.naturalCrisp));
	selection.recipeToken = token == 0 ? 1 : token;
	return selection;
}

DefaultPreference
	CalibrationRuntimeBridge::preferenceForSelection(const CalibrationSession::Selection &selection) noexcept {
	DefaultPreference preference;
	preference.profile   = selection.profile;
	preference.reduction = std::clamp(selection.noiseReduction, 0, 100);
	preference.character = std::clamp(selection.naturalCrisp, 0, 100);
	preference.autoAdapt = selection.profile == Profile::Auto;
	return preference;
}

std::array< CalibrationSession::Selection, 4 >
	CalibrationRuntimeBridge::standardCandidateSet(const DefaultPreference &controls) noexcept {
	std::array< CalibrationSession::Selection, 4 > candidates;
	// Voice Focus is explicit-only. It replaces Quality in the blind set only
	// after the user selected Voice Focus before starting calibration.
	const std::array profiles = { Profile::Original, Profile::Light, Profile::Balanced,
								  controls.profile == Profile::VoiceFocus ? Profile::VoiceFocus : Profile::Quality };
	for (std::size_t index = 0; index < profiles.size(); ++index) {
		DefaultPreference exact = controls;
		exact.profile           = profiles[index];
		exact.autoAdapt         = false;
		candidates[index]       = selectionForPreference(exact);
	}
	return candidates;
}

bool CalibrationRuntimeBridge::captureState(CalibrationSession::State state) noexcept {
	using State = CalibrationSession::State;
	return state == State::LevelCheck || state == State::RoomNoise || state == State::GuidedVoice
		   || state == State::LocalNoise;
}

void CalibrationRuntimeBridge::pauseCallback() noexcept {
	m_captureEnabled.store(false, std::memory_order_release);
	while (m_callbackActive.test(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	clearCallbackFrame();
}

void CalibrationRuntimeBridge::clearCallbackFrame() noexcept {
	secureClear(m_callbackFrame);
}

void CalibrationRuntimeBridge::publishState() noexcept {
	const CalibrationSession::State current = m_session.state();
	m_publishedState.store(current, std::memory_order_release);
	m_transmissionBlocked.store(!m_session.transmissionAllowed(), std::memory_order_release);
	m_captureEnabled.store(captureState(current), std::memory_order_release);
}

void CalibrationRuntimeBridge::clearPlayback() noexcept {
	for (EvaluatedCandidate &candidate : m_evaluated) {
		secureClear(candidate.playbackPcm);
		candidate.selection = {};
		candidate.recipeBinding.reset();
	}
	m_evaluatedCount = 0;
}

CalibrationCandidateEvaluator &CalibrationRuntimeBridge::evaluator() noexcept {
	return *m_evaluator;
}

bool InputEnhancementProbationController::start(const DeviceIdentity &identity, const DefaultPreference &candidate,
												const DefaultPreference &lastKnownWorking,
												const RecipeBinding &candidateRecipeBinding,
												std::optional< RecipeBinding > lastKnownWorkingRecipeBinding) {
	return startWithExecutionBinding(identity, candidate, lastKnownWorking, candidateRecipeBinding, std::nullopt,
									 std::move(lastKnownWorkingRecipeBinding), std::nullopt);
}

bool InputEnhancementProbationController::startAuto(const DeviceIdentity &identity, const DefaultPreference &candidate,
													const DefaultPreference &lastKnownWorking,
													const QString &candidateAutoRecipeSetFingerprint,
													std::optional< RecipeBinding > lastKnownWorkingRecipeBinding,
													std::optional< QString > lastKnownWorkingAutoRecipeSetFingerprint) {
	return startWithExecutionBinding(identity, candidate, lastKnownWorking, std::nullopt,
									 candidateAutoRecipeSetFingerprint, std::move(lastKnownWorkingRecipeBinding),
									 std::move(lastKnownWorkingAutoRecipeSetFingerprint));
}

bool InputEnhancementProbationController::startWithExecutionBinding(
	const DeviceIdentity &identity, const DefaultPreference &candidate, const DefaultPreference &lastKnownWorking,
	std::optional< RecipeBinding > candidateRecipeBinding, std::optional< QString > candidateAutoRecipeSetFingerprint,
	std::optional< RecipeBinding > lastKnownWorkingRecipeBinding,
	std::optional< QString > lastKnownWorkingAutoRecipeSetFingerprint) {
	if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()
		|| !executionBindingMatchesPreference(candidate, candidateRecipeBinding, candidateAutoRecipeSetFingerprint)
		|| !executionBindingMatchesPreference(lastKnownWorking, lastKnownWorkingRecipeBinding,
											  lastKnownWorkingAutoRecipeSetFingerprint)
		|| m_running.load(std::memory_order_acquire)) {
		return false;
	}
	m_identity                                 = identity;
	m_candidate                                = candidate;
	m_lastKnownWorking                         = lastKnownWorking;
	m_undoPreference                           = candidate;
	m_candidateRecipeBinding                   = std::move(candidateRecipeBinding);
	m_lastKnownWorkingRecipeBinding            = std::move(lastKnownWorkingRecipeBinding);
	m_undoRecipeBinding                        = m_candidateRecipeBinding;
	m_candidateAutoRecipeSetFingerprint        = std::move(candidateAutoRecipeSetFingerprint);
	m_lastKnownWorkingAutoRecipeSetFingerprint = std::move(lastKnownWorkingAutoRecipeSetFingerprint);
	m_undoAutoRecipeSetFingerprint             = m_candidateAutoRecipeSetFingerprint;
	m_pendingAction.store(AutoV1::ProbationAction::None, std::memory_order_relaxed);
	m_failure.store(AutoV1::ProbationFailure::None, std::memory_order_relaxed);
	m_undoAvailable.store(false, std::memory_order_relaxed);
	m_probation.start(
		bindingToken(candidate, m_candidateRecipeBinding, m_candidateAutoRecipeSetFingerprint),
		bindingToken(lastKnownWorking, m_lastKnownWorkingRecipeBinding, m_lastKnownWorkingAutoRecipeSetFingerprint));
	m_running.store(true, std::memory_order_release);
	return true;
}

bool InputEnhancementProbationController::restoreUndo(const DeviceProfileState &state) {
	if (m_running.load(std::memory_order_acquire) || state.identity.backendId.isEmpty()
		|| state.identity.physicalId.isEmpty() || state.pendingValidation || !state.lastKnownGood
		|| !state.rollbackUndoPreference || state.preference != *state.lastKnownGood
		|| !executionBindingMatchesPreference(*state.rollbackUndoPreference, state.rollbackUndoRecipeBinding,
											  state.rollbackUndoAutoRecipeSetFingerprint)
		|| !executionBindingMatchesPreference(*state.lastKnownGood, state.lastKnownGoodRecipeBinding,
											  state.lastKnownGoodAutoRecipeSetFingerprint)) {
		return false;
	}

	m_identity                                 = state.identity;
	m_candidate                                = *state.rollbackUndoPreference;
	m_lastKnownWorking                         = *state.lastKnownGood;
	m_undoPreference                           = *state.rollbackUndoPreference;
	m_candidateRecipeBinding                   = state.rollbackUndoRecipeBinding;
	m_lastKnownWorkingRecipeBinding            = state.lastKnownGoodRecipeBinding;
	m_undoRecipeBinding                        = state.rollbackUndoRecipeBinding;
	m_candidateAutoRecipeSetFingerprint        = state.rollbackUndoAutoRecipeSetFingerprint;
	m_lastKnownWorkingAutoRecipeSetFingerprint = state.lastKnownGoodAutoRecipeSetFingerprint;
	m_undoAutoRecipeSetFingerprint             = state.rollbackUndoAutoRecipeSetFingerprint;
	m_pendingAction.store(AutoV1::ProbationAction::None, std::memory_order_relaxed);
	m_failure.store(AutoV1::ProbationFailure::None, std::memory_order_relaxed);
	m_undoAvailable.store(true, std::memory_order_release);
	return true;
}

AutoV1::ProbationAction InputEnhancementProbationController::observeFrame(std::uint32_t elapsedMilliseconds,
																		  bool speech,
																		  ProbationHealthSignal health) noexcept {
	if (!m_running.load(std::memory_order_acquire)) {
		return AutoV1::ProbationAction::None;
	}
	const AutoV1::ProbationFailure failure = probationFailure(health);
	const AutoV1::ProbationResult result =
		m_probation.observe({ elapsedMilliseconds, speech ? elapsedMilliseconds : 0U, failure });
	if (result.action != AutoV1::ProbationAction::None) {
		m_failure.store(result.failure, std::memory_order_relaxed);
		m_pendingAction.store(result.action, std::memory_order_release);
		m_running.store(false, std::memory_order_release);
	}
	return result.action;
}

ProbationSettingsResult InputEnhancementProbationController::serviceSettings(Settings &settings) {
	const AutoV1::ProbationAction action =
		m_pendingAction.exchange(AutoV1::ProbationAction::None, std::memory_order_acq_rel);
	if (action == AutoV1::ProbationAction::None) {
		return ProbationSettingsResult::None;
	}
	const bool rollback = action == AutoV1::ProbationAction::Rollback;
	if (!updateDeviceSettings(settings, rollback)) {
		m_pendingAction.store(action, std::memory_order_release);
		return ProbationSettingsResult::None;
	}
	if (rollback) {
		m_undoAvailable.store(true, std::memory_order_release);
		return ProbationSettingsResult::RolledBack;
	}
	return ProbationSettingsResult::MarkedHealthy;
}

bool InputEnhancementProbationController::undoRollback(Settings &settings) {
	if (!m_undoAvailable.load(std::memory_order_acquire)) {
		return false;
	}
	Settings updated                            = settings;
	DeviceProfileState state                    = deviceStateFor(updated, m_identity);
	state.identity                              = m_identity;
	state.lastKnownGood                         = m_lastKnownWorking;
	state.lastKnownGoodRecipeBinding            = m_lastKnownWorkingRecipeBinding;
	state.lastKnownGoodAutoRecipeSetFingerprint = m_lastKnownWorkingAutoRecipeSetFingerprint;
	state.preference                            = m_undoPreference;
	state.pendingRecipeBinding                  = m_undoRecipeBinding;
	state.pendingAutoRecipeSetFingerprint       = m_undoAutoRecipeSetFingerprint;
	state.pendingValidation                     = true;
	state.lastRollbackReason.clear();
	state.legacyOverride.reset();
	state.rollbackUndoPreference.reset();
	state.rollbackUndoRecipeBinding.reset();
	state.rollbackUndoAutoRecipeSetFingerprint.reset();
	if (!upsertDeviceProfile(updated, std::move(state))) {
		return false;
	}
	settings = std::move(updated);
	m_undoAvailable.store(false, std::memory_order_release);
	return startWithExecutionBinding(m_identity, m_undoPreference, m_lastKnownWorking, m_undoRecipeBinding,
									 m_undoAutoRecipeSetFingerprint, m_lastKnownWorkingRecipeBinding,
									 m_lastKnownWorkingAutoRecipeSetFingerprint);
}

bool InputEnhancementProbationController::running() const noexcept {
	return m_running.load(std::memory_order_acquire);
}

bool InputEnhancementProbationController::undoAvailable() const noexcept {
	return m_undoAvailable.load(std::memory_order_acquire);
}

AutoV1::ProbationFailure InputEnhancementProbationController::failure() const noexcept {
	return m_failure.load(std::memory_order_acquire);
}

std::uint64_t InputEnhancementProbationController::bindingToken(
	const DefaultPreference &preference, const std::optional< RecipeBinding > &binding,
	const std::optional< QString > &autoRecipeSetFingerprint) noexcept {
	const CalibrationSession::Selection selection = CalibrationRuntimeBridge::selectionForPreference(preference);
	std::uint64_t token                           = (static_cast< std::uint64_t >(selection.recipeToken) << 32U)
						  | static_cast< std::uint32_t >(selection.recipeToken ^ 0x9e3779b9U);
	if (binding) {
		token ^= static_cast< std::uint64_t >(qHash(binding->catalogRevision)) << 1U;
		token ^= static_cast< std::uint64_t >(qHash(binding->recipeId)) << 17U;
		token ^= static_cast< std::uint64_t >(qHash(binding->modelSha256)) << 33U;
		token ^= binding->recipeRevision;
	}
	if (autoRecipeSetFingerprint) {
		// The callback's scalar token is only a probation-session discriminator.
		// The complete 256-bit value remains in the controller and persisted
		// DeviceProfileState; it is never reconstructed from this token.
		std::uint64_t fingerprintToken = 1469598103934665603ULL;
		for (const QChar character : *autoRecipeSetFingerprint) {
			fingerprintToken ^= character.unicode();
			fingerprintToken *= 1099511628211ULL;
		}
		token ^= fingerprintToken;
	}
	return token;
}

AutoV1::ProbationFailure InputEnhancementProbationController::probationFailure(ProbationHealthSignal health) noexcept {
	switch (health) {
		case ProbationHealthSignal::Healthy:
			return AutoV1::ProbationFailure::None;
		case ProbationHealthSignal::InitializationFailure:
			return AutoV1::ProbationFailure::InitializationFailure;
		case ProbationHealthSignal::InvalidOutput:
			return AutoV1::ProbationFailure::InvalidOutput;
		case ProbationHealthSignal::DeadlineMiss:
			return AutoV1::ProbationFailure::DeadlineMiss;
		case ProbationHealthSignal::CrashDetected:
			return AutoV1::ProbationFailure::CrashDetected;
	}
	return AutoV1::ProbationFailure::InvalidOutput;
}

QString InputEnhancementProbationController::failureText(AutoV1::ProbationFailure failure) {
	switch (failure) {
		case AutoV1::ProbationFailure::None:
			return {};
		case AutoV1::ProbationFailure::InitializationFailure:
			return QStringLiteral("initialization_failure");
		case AutoV1::ProbationFailure::InvalidOutput:
			return QStringLiteral("invalid_output");
		case AutoV1::ProbationFailure::DeadlineMiss:
			return QStringLiteral("deadline_miss");
		case AutoV1::ProbationFailure::CrashDetected:
			return QStringLiteral("crash_detected");
	}
	return QStringLiteral("invalid_output");
}

bool InputEnhancementProbationController::updateDeviceSettings(Settings &settings, bool rollback) {
	Settings updated         = settings;
	DeviceProfileState state = deviceStateFor(updated, m_identity);
	state.identity           = m_identity;
	state.calibrated         = true;
	state.legacyOverride.reset();
	if (rollback) {
		state.preference                            = m_lastKnownWorking;
		state.lastKnownGood                         = m_lastKnownWorking;
		state.lastKnownGoodRecipeBinding            = m_lastKnownWorkingRecipeBinding;
		state.lastKnownGoodAutoRecipeSetFingerprint = m_lastKnownWorkingAutoRecipeSetFingerprint;
		state.pendingRecipeBinding.reset();
		state.pendingAutoRecipeSetFingerprint.reset();
		state.rollbackUndoPreference               = m_candidate;
		state.rollbackUndoRecipeBinding            = m_candidateRecipeBinding;
		state.rollbackUndoAutoRecipeSetFingerprint = m_candidateAutoRecipeSetFingerprint;
		state.pendingValidation                    = false;
		state.lastRollbackReason                   = failureText(m_failure.load(std::memory_order_acquire));
	} else {
		state.preference                            = m_candidate;
		state.lastKnownGood                         = m_candidate;
		state.lastKnownGoodRecipeBinding            = m_candidateRecipeBinding;
		state.lastKnownGoodAutoRecipeSetFingerprint = m_candidateAutoRecipeSetFingerprint;
		state.pendingRecipeBinding.reset();
		state.pendingAutoRecipeSetFingerprint.reset();
		state.rollbackUndoPreference.reset();
		state.rollbackUndoRecipeBinding.reset();
		state.rollbackUndoAutoRecipeSetFingerprint.reset();
		state.pendingValidation = false;
		state.lastRollbackReason.clear();
	}
	if (!upsertDeviceProfile(updated, std::move(state))) {
		return false;
	}
	settings = std::move(updated);
	return true;
}

} // namespace Mumble::InputEnhancement
