// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationRuntime.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using namespace Mumble::InputEnhancement;

namespace {
template< typename T > int enumValue(T value) {
	return static_cast< int >(value);
}

DeviceIdentity identity() {
	DeviceIdentity value;
	value.backendId   = QStringLiteral("WASAPI");
	value.physicalId  = QStringLiteral("endpoint-calibration-test");
	value.displayName = QStringLiteral("Test microphone");
	value.stable      = true;
	return value;
}

DefaultPreference preference(Profile profile, int reduction, int character, bool autoAdapt = false) {
	DefaultPreference value;
	value.profile   = profile;
	value.reduction = reduction;
	value.character = character;
	value.autoAdapt = autoAdapt;
	return value;
}

CalibrationSession::Selection selection(Profile profile, int reduction, int character, std::uint32_t token) {
	return { profile, reduction, character, token };
}

Recipe recipeFor(Profile profile, int reduction, int character) {
	ResolveRequest request;
	request.profile                           = profile;
	request.noiseReduction                    = reduction;
	request.naturalCrisp                      = character;
	request.cpuClass                          = CpuClass::High;
	request.backendAvailability.rnnoise       = true;
	request.backendAvailability.deepFilterNet = true;
	request.backendAvailability.dtln          = true;
	return RecipeCatalog::resolve(request);
}

RecipeBinding bindingFor(Profile profile, int reduction, int character, QChar hashCharacter = QLatin1Char('a')) {
	const Recipe recipe       = recipeFor(profile, reduction, character);
	const QString modelSha256 = recipe.usesNeuralProcessor() ? QString(64, hashCharacter) : QString{};
	QString safeModelId       = recipe.modelId();
	safeModelId.replace(QLatin1Char(':'), QLatin1Char('-'));
	const QString modelRelativePath =
		recipe.usesNeuralProcessor() ? QStringLiteral("input-models/%1.bin").arg(safeModelId) : QString{};
	return recipeBindingForRecipe(recipe, QStringLiteral("calibration-test-catalog-v1"), modelSha256,
								  modelRelativePath);
}

bool appendConstant(CalibrationRuntimeBridge &bridge, std::size_t samples, short value) {
	std::array< short, frameSamples > frame;
	frame.fill(value);
	while (samples > 0) {
		const unsigned int count   = static_cast< unsigned int >(std::min< std::size_t >(samples, frame.size()));
		const std::size_t accepted = bridge.appendPcmFromCallback(frame.data(), count);
		if (accepted != count) {
			return false;
		}
		samples -= accepted;
	}
	return true;
}

bool reachEvaluation(CalibrationRuntimeBridge &bridge) {
	return appendConstant(bridge, CalibrationSession::levelCheckSamples, 3277) && bridge.advance()
		   && appendConstant(bridge, CalibrationSession::roomNoiseSamples, 328) && bridge.advance()
		   && appendConstant(bridge, CalibrationSession::guidedVoiceSamples, 4915) && bridge.advance();
}

class FakeEvaluator final : public CalibrationCandidateEvaluator {
public:
	explicit FakeEvaluator(bool fail = false) : m_fail(fail) {}

	bool evaluate(const CalibrationSession::CaptureView &, const CalibrationSession::Selection &candidate,
				  Output &output) override {
		if (m_fail) {
			return false;
		}
		output.candidate.selection                     = candidate;
		output.candidate.objectiveScore                = static_cast< double >(candidate.recipeToken);
		output.candidate.eligible                      = true;
		output.candidate.localPipelineAndOpusEvaluated = true;
		output.candidate.loudnessMatched               = true;
		output.playbackPcm.assign(64, static_cast< float >(candidate.recipeToken) / 100.0f);
		output.recipeBinding = bindingFor(candidate.profile, candidate.noiseReduction, candidate.naturalCrisp);
		return true;
	}

private:
	bool m_fail;
};

class ThrowingEvaluator final : public CalibrationCandidateEvaluator {
public:
	bool evaluate(const CalibrationSession::CaptureView &, const CalibrationSession::Selection &,
				  Output &output) override {
		output.playbackPcm.assign(64, 0.75f);
		throw std::runtime_error("injected evaluator failure");
	}
};

struct ProgressProbe final {
	std::atomic_bool *cancel = nullptr;
	std::size_t completed    = 0;
	std::size_t total        = 0;
};

void captureProgress(void *context, std::size_t completed, std::size_t total) noexcept {
	auto &probe     = *static_cast< ProgressProbe * >(context);
	probe.completed = completed;
	probe.total     = total;
	if (completed == 1 && probe.cancel) {
		probe.cancel->store(true, std::memory_order_release);
	}
}
} // namespace

static_assert(noexcept(std::declval< CalibrationRuntimeBridge & >().appendPcmFromCallback(nullptr, 0)));
static_assert(noexcept(std::declval< InputEnhancementProbationController & >().observeFrame(10, false)));

class TestInputEnhancementCalibrationRuntime : public QObject {
	Q_OBJECT

private slots:
	void callbackCaptureBlocksTransmissionAndApplyPersistsPerDeviceDraft();
	void cancelErrorAndDestructionWipeAllAudio();
	void invalidOrExceptionalEvaluationWipesAndUnblocks();
	void fullCalibratedProfileStoreMakesApplyAbortAndWipe();
	void localEvaluatorUsesPipelineAndOpusForOriginal();
	void localEvaluatorIncludesOptionalLocalNoiseInObjective();
	void standardCandidatesAlwaysStartWithOriginalAndExcludeAuto();
	void calibrationRejectsAutoAsRollbackBaseline();
	void probationMarksHealthyOnlyAfterBothThresholds();
	void probationFailureRollsBackAndUndoRestoresCandidate();
	void probationPersistsEveryFailureReason_data();
	void probationPersistsEveryFailureReason();
	void immutablePackageAuthorizationFailsClosed();
	void evaluationObserverCancelsBetweenCandidatesAndWipes();
	void transmissionBlockClosesNullBridgeInterleaving();
};

void TestInputEnhancementCalibrationRuntime::callbackCaptureBlocksTransmissionAndApplyPersistsPerDeviceDraft() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationRuntimeBridge bridge(storage, std::make_unique< FakeEvaluator >());
	const DefaultPreference previous    = preference(Profile::Light, 35, 45);
	const RecipeBinding previousBinding = bindingFor(Profile::Light, 35, 45);
	QVERIFY(bridge.start(identity(), previous, false, 0x1234, previousBinding));
	QVERIFY(bridge.transmissionBlocked());
	QVERIFY(reachEvaluation(bridge));
	QCOMPARE(enumValue(bridge.state()), enumValue(CalibrationSession::State::Evaluating));
	QVERIFY(bridge.transmissionBlocked());

	const std::array candidates = {
		selection(Profile::Original, 0, 50, 11),
		selection(Profile::Balanced, 55, 55, 22),
		selection(Profile::Crisp, 65, 70, 33),
	};
	QVERIFY(bridge.evaluateCandidates(candidates));
	const CalibrationSession::BlindPair pair = bridge.blindPair();
	QVERIFY(pair.leftPlaybackToken != 0);
	QVERIFY(!bridge.playbackForToken(pair.leftPlaybackToken).empty());
	QVERIFY(bridge.selectBlindWinner(pair.leftPlaybackToken));
	QVERIFY(bridge.draftPreference());
	const DefaultPreference expected = *bridge.draftPreference();
	QVERIFY(bridge.transmissionBlocked());

	Settings settings;
	QVERIFY(bridge.apply(settings, 123456));
	const DeviceProfileState *saved = findDeviceProfile(settings, identity());
	QVERIFY(saved);
	QVERIFY(saved->preference == expected);
	QVERIFY(saved->lastKnownGood.has_value());
	QVERIFY(*saved->lastKnownGood == previous);
	QVERIFY(saved->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*saved->lastKnownGoodRecipeBinding == previousBinding);
	QVERIFY(saved->pendingRecipeBinding.has_value());
	QVERIFY(*saved->pendingRecipeBinding == bindingFor(expected.profile, expected.reduction, expected.character));
	QVERIFY(saved->calibrated);
	QVERIFY(saved->pendingValidation);
	QCOMPARE(saved->lastUsedEpochMs, qint64{ 123456 });
	QVERIFY(!bridge.transmissionBlocked());
	QVERIFY(bridge.rawAudioCleared());
	QVERIFY(bridge.playbackBuffersCleared());
}

void TestInputEnhancementCalibrationRuntime::cancelErrorAndDestructionWipeAllAudio() {
	std::vector< float > cancelledStorage(CalibrationSession::requiredStorageSamples);
	CalibrationRuntimeBridge cancelled(cancelledStorage, std::make_unique< FakeEvaluator >());
	QVERIFY(cancelled.start(identity(), preference(Profile::Original, 50, 50), false, 9));
	QVERIFY(appendConstant(cancelled, CalibrationSession::levelCheckSamples, 3277));
	QVERIFY(cancelled.advance());
	QVERIFY(appendConstant(cancelled, 48'000, 500));
	QVERIFY(!cancelled.rawAudioCleared());
	QVERIFY(cancelled.cancel());
	QVERIFY(cancelled.rawAudioCleared());
	QVERIFY(!cancelled.transmissionBlocked());

	std::vector< float > failedStorage(CalibrationSession::requiredStorageSamples);
	CalibrationRuntimeBridge failed(failedStorage, std::make_unique< FakeEvaluator >(true));
	QVERIFY(failed.start(identity(), preference(Profile::Original, 50, 50), false, 10));
	QVERIFY(reachEvaluation(failed));
	const std::array candidates = { selection(Profile::Original, 0, 50, 1), selection(Profile::Balanced, 50, 50, 2) };
	QVERIFY(!failed.evaluateCandidates(candidates));
	QCOMPARE(enumValue(failed.state()), enumValue(CalibrationSession::State::Error));
	QVERIFY(failed.rawAudioCleared());
	QVERIFY(failed.playbackBuffersCleared());
	QVERIFY(!failed.transmissionBlocked());

	std::vector< float > destructorStorage(CalibrationSession::requiredStorageSamples);
	{
		CalibrationRuntimeBridge active(destructorStorage, std::make_unique< FakeEvaluator >());
		QVERIFY(active.start(identity(), preference(Profile::Original, 50, 50), false, 11));
		QVERIFY(appendConstant(active, CalibrationSession::levelCheckSamples, 3277));
		QVERIFY(active.advance());
		QVERIFY(appendConstant(active, 48'000, 900));
	}
	QVERIFY(
		std::all_of(destructorStorage.cbegin(), destructorStorage.cend(), [](float sample) { return sample == 0.0f; }));
}

void TestInputEnhancementCalibrationRuntime::invalidOrExceptionalEvaluationWipesAndUnblocks() {
	for (const bool throwFromEvaluator : { false, true }) {
		std::vector< float > storage(CalibrationSession::requiredStorageSamples);
		std::unique_ptr< CalibrationCandidateEvaluator > evaluator = throwFromEvaluator
			? std::unique_ptr< CalibrationCandidateEvaluator >(std::make_unique< ThrowingEvaluator >())
			: std::unique_ptr< CalibrationCandidateEvaluator >(std::make_unique< FakeEvaluator >());
		CalibrationRuntimeBridge bridge(storage, std::move(evaluator));
		QVERIFY(bridge.start(identity(), preference(Profile::Original, 50, 50), false,
						 throwFromEvaluator ? 0x1002 : 0x1001));
		QVERIFY(reachEvaluation(bridge));
		if (throwFromEvaluator) {
			const std::array candidates = { selection(Profile::Original, 0, 50, 1),
										 selection(Profile::Balanced, 50, 50, 2) };
			QVERIFY(!bridge.evaluateCandidates(candidates));
		} else {
			QVERIFY(!bridge.evaluateCandidates({}));
		}
		QCOMPARE(enumValue(bridge.state()), enumValue(CalibrationSession::State::Error));
		QVERIFY(bridge.rawAudioCleared());
		QVERIFY(bridge.playbackBuffersCleared());
		QVERIFY(!bridge.transmissionBlocked());
	}
}

void TestInputEnhancementCalibrationRuntime::fullCalibratedProfileStoreMakesApplyAbortAndWipe() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationRuntimeBridge bridge(storage, std::make_unique< FakeEvaluator >());
	QVERIFY(bridge.start(identity(), preference(Profile::Original, 50, 50), false, 0x2001));
	QVERIFY(reachEvaluation(bridge));
	const std::array candidates = { selection(Profile::Original, 0, 50, 1),
								  selection(Profile::Balanced, 50, 50, 2) };
	QVERIFY(bridge.evaluateCandidates(candidates));
	const auto pair = bridge.blindPair();
	QVERIFY(bridge.selectBlindWinner(pair.leftPlaybackToken));

	Settings settings;
	for (int index = 0; index < MAX_DEVICE_PROFILES; ++index) {
		DeviceProfileState occupied;
		occupied.identity             = identity();
		occupied.identity.physicalId += QStringLiteral("-occupied-%1").arg(index);
		occupied.calibrated           = true;
		occupied.lastUsedEpochMs      = index + 1;
		QVERIFY(upsertDeviceProfile(settings, std::move(occupied)));
	}
	QCOMPARE(settings.deviceProfiles.size(), MAX_DEVICE_PROFILES);
	QVERIFY(!bridge.apply(settings, 123456));
	QCOMPARE(enumValue(bridge.state()), enumValue(CalibrationSession::State::Aborted));
	QVERIFY(bridge.rawAudioCleared());
	QVERIFY(bridge.playbackBuffersCleared());
	QVERIFY(!bridge.transmissionBlocked());
}

void TestInputEnhancementCalibrationRuntime::localEvaluatorUsesPipelineAndOpusForOriginal() {
	std::array< float, frameSamples > room;
	std::array< float, frameSamples * 2 > voice;
	for (std::size_t index = 0; index < room.size(); ++index) {
		room[index] = (index % 2 == 0) ? 0.01f : -0.01f;
	}
	for (std::size_t index = 0; index < voice.size(); ++index) {
		voice[index] = static_cast< float >(0.15 * std::sin(static_cast< double >(index) * 0.07));
	}
	LocalCalibrationCandidateEvaluator evaluator;
	CalibrationCandidateEvaluator::Output output;
	QVERIFY(evaluator.evaluate({ room, voice, {} }, selection(Profile::Original, 0, 50, 7), output));
	QVERIFY(output.candidate.eligible);
	QVERIFY(output.candidate.localPipelineAndOpusEvaluated);
	QVERIFY(output.candidate.loudnessMatched);
	QCOMPARE(output.playbackPcm.size(), voice.size());
	QVERIFY(std::isfinite(output.candidate.objectiveScore));

	CalibrationCandidateEvaluator::Output light;
	QVERIFY(evaluator.evaluate({ room, voice, {} }, selection(Profile::Light, 50, 50, 8), light));
	QVERIFY(light.candidate.eligible);
	QVERIFY(light.candidate.localPipelineAndOpusEvaluated);
	QVERIFY(light.candidate.loudnessMatched);
	QCOMPARE(light.playbackPcm.size(), voice.size());
}

void TestInputEnhancementCalibrationRuntime::localEvaluatorIncludesOptionalLocalNoiseInObjective() {
	std::array< float, frameSamples * 2 > room;
	std::array< float, frameSamples * 2 > voice;
	std::array< float, frameSamples * 2 > localNoise;
	for (std::size_t index = 0; index < room.size(); ++index) {
		room[index]  = (index % 2 == 0) ? 0.006f : -0.006f;
		voice[index] = static_cast< float >(0.12 * std::sin(static_cast< double >(index) * 0.09));
		// A keyboard-like transient train that differs materially from the
		// stationary room capture and therefore must affect candidate ranking.
		localNoise[index] = index % 96 < 5 ? 0.45f : 0.0f;
	}

	LocalCalibrationCandidateEvaluator evaluator;
	CalibrationCandidateEvaluator::Output roomOnly;
	CalibrationCandidateEvaluator::Output withLocalNoise;
	const auto light = selection(Profile::Light, 50, 50, 81);
	QVERIFY(evaluator.evaluate({ room, voice, {} }, light, roomOnly));
	QVERIFY(evaluator.evaluate({ room, voice, localNoise }, light, withLocalNoise));
	QVERIFY(std::isfinite(roomOnly.candidate.objectiveScore));
	QVERIFY(std::isfinite(withLocalNoise.candidate.objectiveScore));
	QVERIFY(std::abs(roomOnly.candidate.objectiveScore - withLocalNoise.candidate.objectiveScore) > 1.0e-6);
}

void TestInputEnhancementCalibrationRuntime::standardCandidatesAlwaysStartWithOriginalAndExcludeAuto() {
	const auto candidates = CalibrationRuntimeBridge::standardCandidateSet(preference(Profile::Auto, 63, 71, true));
	QCOMPARE(enumValue(candidates.front().profile), enumValue(Profile::Original));
	for (const CalibrationSession::Selection &candidate : candidates) {
		QVERIFY(candidate.profile != Profile::Auto);
		QVERIFY(candidate.recipeToken != 0);
		QCOMPARE(candidate.noiseReduction, 63);
		QCOMPARE(candidate.naturalCrisp, 71);
	}
}

void TestInputEnhancementCalibrationRuntime::calibrationRejectsAutoAsRollbackBaseline() {
	CalibrationRuntimeBridge bridge(std::make_unique< FakeEvaluator >());
	QVERIFY(!bridge.start(identity(), preference(Profile::Auto, 63, 71, true), false, 0x1234));
	QVERIFY(!bridge.transmissionBlocked());
	QVERIFY(!bridge.start(identity(), preference(Profile::Balanced, 63, 71, true), false, 0x1234,
						  bindingFor(Profile::Balanced, 63, 71)));
	QVERIFY(!bridge.transmissionBlocked());
}

void TestInputEnhancementCalibrationRuntime::immutablePackageAuthorizationFailsClosed() {
	ResolveRequest request;
	request.profile             = Profile::Balanced;
	request.noiseReduction      = 60;
	request.naturalCrisp        = 55;
	request.cpuClass            = CpuClass::High;
	request.backendAvailability = BackendAvailability::compiled();
	const Recipe balanced       = RecipeCatalog::resolve(request);
	if (balanced.effectiveProfile() != Profile::Balanced) {
		QSKIP("Balanced backend is not compiled in this test configuration");
	}

	QString sha;
	QString modelPath;
	QString relativeModelPath;
	CalibrationPackageAuthorization denied;
	QVERIFY(!denied.recipeAuthorized(balanced, sha, modelPath, &relativeModelPath));
	QVERIFY(sha.isEmpty());
	QVERIFY(modelPath.isEmpty());
	QVERIFY(relativeModelPath.isEmpty());
	const QString signedSha(64, QLatin1Char('a'));
	const QString signedModelPath                             = QStringLiteral("/signed/rnnoise-model");
	const QString signedRelativeModelPath                     = QStringLiteral("input-models/rnnoise-model.bin");
	const CalibrationPackageAuthorization signedAuthorization = CalibrationPackageAuthorization::signedPackage(
		QStringLiteral("signed-calibration-catalog-v7"),
		{ CalibrationPackageAuthorization::authorizeRecipe(balanced, signedSha, signedModelPath,
														   signedRelativeModelPath) });
	QVERIFY(signedAuthorization.recipeAuthorized(balanced, sha, modelPath, &relativeModelPath));
	QCOMPARE(sha, signedSha);
	QCOMPARE(modelPath, signedModelPath);
	QCOMPARE(relativeModelPath, signedRelativeModelPath);
	QCOMPARE(signedAuthorization.catalogRevision, QStringLiteral("signed-calibration-catalog-v7"));
	const RecipeBinding signedBinding =
		recipeBindingForRecipe(balanced, signedAuthorization.catalogRevision, sha, relativeModelPath);
	QVERIFY(isValidRecipeBinding(signedBinding));
	QVERIFY(recipeBindingMatches(signedBinding, balanced, signedAuthorization.catalogRevision, signedSha,
								 signedRelativeModelPath));

	request.noiseReduction       = 61;
	const Recipe changedControls = RecipeCatalog::resolve(request);
	QVERIFY(!signedAuthorization.recipeAuthorized(changedControls, sha, modelPath, &relativeModelPath));
	QVERIFY(relativeModelPath.isEmpty());

	request.profile             = Profile::Crisp;
	const Recipe unsignedRecipe = RecipeCatalog::resolve(request);
	if (unsignedRecipe.effectiveProfile() == Profile::Crisp) {
		QVERIFY(!signedAuthorization.recipeAuthorized(unsignedRecipe, sha, modelPath, &relativeModelPath));
		QVERIFY(relativeModelPath.isEmpty());
	}
	const CalibrationPackageAuthorization unmanaged = CalibrationPackageAuthorization::explicitUnmanagedBuildZero();
	QVERIFY(unmanaged.recipeAuthorized(balanced, sha, modelPath, &relativeModelPath));
	QVERIFY(sha.isEmpty());
	QVERIFY(modelPath.isEmpty());
	QVERIFY(relativeModelPath.isEmpty());
}

void TestInputEnhancementCalibrationRuntime::evaluationObserverCancelsBetweenCandidatesAndWipes() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationRuntimeBridge bridge(storage, std::make_unique< FakeEvaluator >());
	QVERIFY(bridge.start(identity(), preference(Profile::Original, 50, 50), false, 0x9988));
	QVERIFY(reachEvaluation(bridge));
	const std::array candidates = {
		selection(Profile::Original, 0, 50, 1),
		selection(Profile::Light, 50, 50, 2),
		selection(Profile::Balanced, 50, 50, 3),
	};
	std::atomic_bool cancel{ false };
	ProgressProbe probe{ &cancel };
	CalibrationEvaluationObserver observer;
	observer.cancelRequested = &cancel;
	observer.progress        = &captureProgress;
	observer.context         = &probe;
	QVERIFY(!bridge.evaluateCandidates(candidates, observer));
	QCOMPARE(probe.completed, std::size_t{ 1 });
	QCOMPARE(probe.total, candidates.size());
	QCOMPARE(enumValue(bridge.state()), enumValue(CalibrationSession::State::Aborted));
	QVERIFY(bridge.rawAudioCleared());
	QVERIFY(bridge.playbackBuffersCleared());
	QVERIFY(!bridge.transmissionBlocked());
}

void TestInputEnhancementCalibrationRuntime::transmissionBlockClosesNullBridgeInterleaving() {
	CalibrationTransmissionBlock block;
	std::atomic_bool runtimePublished{ false };
	bool callbackSawNullRuntime = false;
	bool callbackSawBlock       = false;
	std::latch callbackLoadedRuntime(1);
	std::latch gatePublished(1);
	std::thread callback([&]() {
		callbackSawNullRuntime = !runtimePublished.load(std::memory_order_acquire);
		callbackLoadedRuntime.count_down();
		gatePublished.wait();
		callbackSawBlock = block.blocked();
	});
	callbackLoadedRuntime.wait();
	block.begin();
	gatePublished.count_down();
	callback.join();
	QVERIFY(callbackSawNullRuntime);
	QVERIFY(callbackSawBlock);

	// A packet path that linearized before begin() is allowed to complete, but
	// begin() must not return until it has left. This prevents an Opus packet
	// from straddling the start of the local-only calibration session.
	QVERIFY(block.tryEnterPacketPath() == false);
	block.endAfterCallbackQuiescence();
	QVERIFY(!block.blocked());
	QVERIFY(block.tryEnterPacketPath());
	std::atomic_bool beginReturned{ false };
	std::thread beginThread([&]() {
		block.begin();
		beginReturned.store(true, std::memory_order_release);
	});
	while (!block.blocked()) {
		std::this_thread::yield();
	}
	const bool returnedWhilePacketPathActive = beginReturned.load(std::memory_order_acquire);
	block.leavePacketPath();
	beginThread.join();
	QVERIFY(!returnedWhilePacketPathActive);
	QVERIFY(beginReturned.load(std::memory_order_acquire));
	QVERIFY(block.blocked());
	block.endAfterCallbackQuiescence();
	QVERIFY(!block.blocked());
}

void TestInputEnhancementCalibrationRuntime::probationMarksHealthyOnlyAfterBothThresholds() {
	Settings settings;
	DeviceProfileState state;
	state.identity                   = identity();
	state.preference                 = preference(Profile::Balanced, 60, 55);
	state.lastKnownGood              = preference(Profile::Light, 35, 45);
	state.pendingRecipeBinding       = bindingFor(Profile::Balanced, 60, 55);
	state.lastKnownGoodRecipeBinding = bindingFor(Profile::Light, 35, 45);
	state.pendingValidation          = true;
	QVERIFY(upsertDeviceProfile(settings, state));

	InputEnhancementProbationController probation;
	QVERIFY(probation.start(state.identity, state.preference, *state.lastKnownGood, *state.pendingRecipeBinding,
							state.lastKnownGoodRecipeBinding));
	for (int frame = 0; frame < 5'999; ++frame) {
		const bool speech = frame < 1'000;
		QCOMPARE(enumValue(probation.observeFrame(10, speech)), enumValue(AutoV1::ProbationAction::None));
	}
	QVERIFY(probation.running());
	QCOMPARE(enumValue(probation.observeFrame(10, false)), enumValue(AutoV1::ProbationAction::MarkHealthy));
	QCOMPARE(enumValue(probation.serviceSettings(settings)), enumValue(ProbationSettingsResult::MarkedHealthy));
	const DeviceProfileState *saved = findDeviceProfile(settings, identity());
	QVERIFY(saved);
	QVERIFY(!saved->pendingValidation);
	QVERIFY(saved->lastKnownGood.has_value());
	QVERIFY(*saved->lastKnownGood == state.preference);
	QVERIFY(saved->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*saved->lastKnownGoodRecipeBinding == *state.pendingRecipeBinding);
	QVERIFY(!saved->pendingRecipeBinding.has_value());
	QVERIFY(!saved->rollbackUndoPreference.has_value());
	QVERIFY(!saved->rollbackUndoRecipeBinding.has_value());
}

void TestInputEnhancementCalibrationRuntime::probationFailureRollsBackAndUndoRestoresCandidate() {
	const DefaultPreference candidate = preference(Profile::Crisp, 70, 75);
	const DefaultPreference previous  = preference(Profile::Balanced, 50, 50);
	Settings settings;
	DeviceProfileState state;
	state.identity                   = identity();
	state.preference                 = candidate;
	state.lastKnownGood              = previous;
	state.pendingRecipeBinding       = bindingFor(Profile::Crisp, 70, 75);
	state.lastKnownGoodRecipeBinding = bindingFor(Profile::Balanced, 50, 50, QLatin1Char('b'));
	state.pendingValidation          = true;
	QVERIFY(upsertDeviceProfile(settings, state));

	InputEnhancementProbationController probation;
	QVERIFY(probation.start(state.identity, candidate, previous, *state.pendingRecipeBinding,
							state.lastKnownGoodRecipeBinding));
	QCOMPARE(enumValue(probation.observeFrame(10, true, ProbationHealthSignal::DeadlineMiss)),
			 enumValue(AutoV1::ProbationAction::Rollback));
	QCOMPARE(enumValue(probation.serviceSettings(settings)), enumValue(ProbationSettingsResult::RolledBack));
	const DeviceProfileState *rolledBack = findDeviceProfile(settings, identity());
	QVERIFY(rolledBack);
	QVERIFY(rolledBack->preference == previous);
	QVERIFY(rolledBack->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*rolledBack->lastKnownGoodRecipeBinding == *state.lastKnownGoodRecipeBinding);
	QVERIFY(!rolledBack->pendingRecipeBinding.has_value());
	QVERIFY(!rolledBack->pendingValidation);
	QCOMPARE(rolledBack->lastRollbackReason, QStringLiteral("deadline_miss"));
	QVERIFY(probation.undoAvailable());
	QVERIFY(rolledBack->rollbackUndoPreference.has_value());
	QVERIFY(*rolledBack->rollbackUndoPreference == candidate);
	QVERIFY(rolledBack->rollbackUndoRecipeBinding.has_value());
	QVERIFY(*rolledBack->rollbackUndoRecipeBinding == *state.pendingRecipeBinding);

	// Audio::restartInput() destroys the controller that observed the failure.
	// A new AudioInput must restore the one-shot Undo from persisted state.
	InputEnhancementProbationController afterRestart;
	QVERIFY(afterRestart.restoreUndo(*rolledBack));
	QVERIFY(afterRestart.undoAvailable());
	QVERIFY(afterRestart.undoRollback(settings));
	const DeviceProfileState *restored = findDeviceProfile(settings, identity());
	QVERIFY(restored);
	QVERIFY(restored->preference == candidate);
	QVERIFY(restored->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*restored->lastKnownGoodRecipeBinding == *state.lastKnownGoodRecipeBinding);
	QVERIFY(restored->pendingRecipeBinding.has_value());
	QVERIFY(*restored->pendingRecipeBinding == *state.pendingRecipeBinding);
	QVERIFY(restored->pendingValidation);
	QVERIFY(!restored->rollbackUndoPreference.has_value());
	QVERIFY(!restored->rollbackUndoRecipeBinding.has_value());
	QVERIFY(afterRestart.running());
	QVERIFY(!afterRestart.undoAvailable());
}

void TestInputEnhancementCalibrationRuntime::probationPersistsEveryFailureReason_data() {
	QTest::addColumn< int >("health");
	QTest::addColumn< QString >("reason");
	QTest::newRow("initialization") << enumValue(ProbationHealthSignal::InitializationFailure)
									<< QStringLiteral("initialization_failure");
	QTest::newRow("invalid-output") << enumValue(ProbationHealthSignal::InvalidOutput)
									<< QStringLiteral("invalid_output");
	QTest::newRow("deadline") << enumValue(ProbationHealthSignal::DeadlineMiss) << QStringLiteral("deadline_miss");
	QTest::newRow("crash") << enumValue(ProbationHealthSignal::CrashDetected) << QStringLiteral("crash_detected");
}

void TestInputEnhancementCalibrationRuntime::probationPersistsEveryFailureReason() {
	QFETCH(int, health);
	QFETCH(QString, reason);
	Settings settings;
	DeviceProfileState state;
	state.identity                   = identity();
	state.preference                 = preference(Profile::Balanced, 62, 58);
	state.lastKnownGood              = preference(Profile::Light, 30, 50);
	state.pendingRecipeBinding       = bindingFor(Profile::Balanced, 62, 58);
	state.lastKnownGoodRecipeBinding = bindingFor(Profile::Light, 30, 50);
	state.pendingValidation          = true;
	QVERIFY(upsertDeviceProfile(settings, state));

	InputEnhancementProbationController probation;
	QVERIFY(probation.start(state.identity, state.preference, *state.lastKnownGood, *state.pendingRecipeBinding,
							state.lastKnownGoodRecipeBinding));
	QCOMPARE(enumValue(probation.observeFrame(0, false, static_cast< ProbationHealthSignal >(health))),
			 enumValue(AutoV1::ProbationAction::Rollback));
	QCOMPARE(enumValue(probation.serviceSettings(settings)), enumValue(ProbationSettingsResult::RolledBack));
	const DeviceProfileState *saved = findDeviceProfile(settings, state.identity);
	QVERIFY(saved);
	QVERIFY(saved->preference == *state.lastKnownGood);
	QVERIFY(saved->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*saved->lastKnownGoodRecipeBinding == *state.lastKnownGoodRecipeBinding);
	QVERIFY(!saved->pendingRecipeBinding.has_value());
	QCOMPARE(saved->lastRollbackReason, reason);
	QVERIFY(saved->rollbackUndoPreference.has_value());
	QVERIFY(saved->rollbackUndoRecipeBinding.has_value());
}

QTEST_GUILESS_MAIN(TestInputEnhancementCalibrationRuntime)

#include "TestInputEnhancementCalibrationRuntime.moc"
