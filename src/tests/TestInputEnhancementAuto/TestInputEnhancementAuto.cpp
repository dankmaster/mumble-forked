// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementAuto.h"

#include <QtTest>

#include <array>
#include <type_traits>
#include <utility>

using namespace Mumble::InputEnhancement;
using namespace Mumble::InputEnhancement::AutoV1;

namespace {
template< typename T > int enumValue(T value) {
	return static_cast< int >(value);
}

Observation difficultHighCpuObservation(VadConfidenceBucket vad = VadConfidenceBucket::Silent) {
	Observation observation;
	observation.noiseFloor       = NoiseFloorBucket::VeryHigh;
	observation.snr              = SnrBucket::Poor;
	observation.stationaryScore  = 20;
	observation.vadConfidence    = vad;
	observation.cpuClass         = CpuClass::High;
	observation.deadlinePressure = DeadlinePressure::None;
	return observation;
}
} // namespace

static_assert(noexcept(std::declval< Policy & >().evaluate(std::declval< const Observation & >())));
static_assert(noexcept(std::declval< ObservationTracker & >().captureFrame(nullptr, 0)));
static_assert(noexcept(std::declval< SafeProfileSwitchGate & >().poll(false, false)));
static_assert(noexcept(std::declval< Probation & >().observe(std::declval< const ProbationObservation & >())));
static_assert(std::is_nothrow_destructible_v< Policy >);
static_assert(std::is_nothrow_destructible_v< Probation >);

class TestInputEnhancementAuto : public QObject {
	Q_OBJECT

private slots:
	void deterministicPolicyUsesOnlyAllowedBuckets();
	void profileChangesRequireHysteresisAndSilence();
	void transientCandidatesDoNotDefeatHysteresis();
	void deadlineAndCpuPressureChooseLowerCostProfiles();
	void pressureDemotionCooldownPreventsProfileFlapping();
	void automaticControlAdjustmentIsBoundedAndHysteretic();
	void fixedProfileAndPreparedAvailabilityConstrainSwitching();
	void observationTrackerEmitsOnlyCoarsePeriodicState();
	void safeSwitchGateRequiresIdleAndSupportsRepeatedPreparedCandidates();
	void acousticSilenceBoundaryWorksDuringAnOpenTransportStream();
	void preparedPipelineBankReplenishesRetiredNeuralCandidates();
	void probationRequiresBothElapsedAndSpeechThresholds();
	void probationImmediatelyRollsBackEveryFailureSignal_data();
	void probationImmediatelyRollsBackEveryFailureSignal();
	void probationFailureWinsAtHealthBoundary();
	void probationClampsImpossibleSpeechDuration();
};

void TestInputEnhancementAuto::deterministicPolicyUsesOnlyAllowedBuckets() {
	Policy left(Profile::Balanced);
	Policy right(Profile::Balanced);
	const std::array< Observation, 5 > sequence = {
		difficultHighCpuObservation(VadConfidenceBucket::High),
		difficultHighCpuObservation(VadConfidenceBucket::Low),
		difficultHighCpuObservation(VadConfidenceBucket::Silent),
		Observation{},
		Observation{},
	};

	for (const Observation &observation : sequence) {
		const Decision leftDecision  = left.evaluate(observation);
		const Decision rightDecision = right.evaluate(observation);
		QCOMPARE(enumValue(leftDecision.activeProfile), enumValue(rightDecision.activeProfile));
		QCOMPARE(enumValue(leftDecision.activeEngine), enumValue(rightDecision.activeEngine));
		QCOMPARE(leftDecision.noiseReduction, rightDecision.noiseReduction);
		QCOMPARE(leftDecision.naturalCrisp, rightDecision.naturalCrisp);
		QCOMPARE(leftDecision.profileSwitchApplied, rightDecision.profileSwitchApplied);
		QCOMPARE(leftDecision.profileSwitchDeferred, rightDecision.profileSwitchDeferred);
	}
}

void TestInputEnhancementAuto::profileChangesRequireHysteresisAndSilence() {
	Policy policy(Profile::Balanced);
	const Observation speaking = difficultHighCpuObservation(VadConfidenceBucket::High);

	for (int index = 0; index < Policy::profileHysteresisObservations - 1; ++index) {
		const Decision decision = policy.evaluate(speaking);
		QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Balanced));
		QVERIFY(decision.hysteresisPending);
		QVERIFY(!decision.profileSwitchApplied);
		QVERIFY(!decision.profileSwitchDeferred);
	}

	const Decision deferred = policy.evaluate(speaking);
	QCOMPARE(enumValue(deferred.activeProfile), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(deferred.candidateProfile), enumValue(Profile::Crisp));
	QCOMPARE(enumValue(deferred.activeEngine), enumValue(Engine::RNNoise));
	QVERIFY(deferred.profileSwitchDeferred);
	QVERIFY(!deferred.profileSwitchApplied);

	const Decision applied = policy.evaluate(difficultHighCpuObservation(VadConfidenceBucket::Silent));
	QCOMPARE(enumValue(applied.activeProfile), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(applied.candidateProfile), enumValue(Profile::Crisp));
	QCOMPARE(enumValue(applied.activeEngine), enumValue(Engine::RNNoise));
	QVERIFY(applied.profileSwitchApplied);
	QVERIFY(!applied.profileSwitchDeferred);
	QCOMPARE(enumValue(policy.activeProfile()), enumValue(Profile::Balanced));
	policy.commitProfile(applied.candidateProfile);
	QCOMPARE(enumValue(policy.activeProfile()), enumValue(Profile::Crisp));
	QCOMPARE(enumValue(policy.activeEngine()), enumValue(Engine::DeepFilterNet));
}

void TestInputEnhancementAuto::transientCandidatesDoNotDefeatHysteresis() {
	Policy policy(Profile::Balanced);
	Observation crisp = difficultHighCpuObservation();
	Observation easy;
	easy.cpuClass        = CpuClass::High;
	easy.noiseFloor      = NoiseFloorBucket::VeryLow;
	easy.snr             = SnrBucket::Excellent;
	easy.stationaryScore = 90;

	QVERIFY(policy.evaluate(crisp).hysteresisPending);
	QVERIFY(policy.evaluate(easy).hysteresisPending);
	QVERIFY(policy.evaluate(crisp).hysteresisPending);
	QVERIFY(policy.evaluate(easy).hysteresisPending);
	QCOMPARE(enumValue(policy.activeProfile()), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(policy.activeEngine()), enumValue(Engine::RNNoise));
}

void TestInputEnhancementAuto::deadlineAndCpuPressureChooseLowerCostProfiles() {
	Observation observation;
	observation.cpuClass      = CpuClass::Low;
	observation.vadConfidence = VadConfidenceBucket::Silent;
	Policy lowCpu(Profile::Balanced);
	Decision decision;
	for (int index = 0; index < Policy::profileHysteresisObservations; ++index) {
		decision = lowCpu.evaluate(observation);
	}
	QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(decision.candidateProfile), enumValue(Profile::Light));
	QVERIFY(decision.profileSwitchApplied);
	lowCpu.commitProfile(decision.candidateProfile);
	QCOMPARE(enumValue(lowCpu.activeEngine()), enumValue(Engine::Speex));

	observation.cpuClass         = CpuClass::High;
	observation.deadlinePressure = DeadlinePressure::Critical;
	Policy pressured(Profile::Crisp);
	for (int index = 0; index < Policy::profileHysteresisObservations; ++index) {
		decision = pressured.evaluate(observation);
	}
	QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Crisp));
	QCOMPARE(enumValue(decision.candidateProfile), enumValue(Profile::Light));
	QVERIFY(decision.profileSwitchApplied);
	pressured.commitProfile(decision.candidateProfile);
	QCOMPARE(enumValue(pressured.activeEngine()), enumValue(Engine::Speex));
}

void TestInputEnhancementAuto::pressureDemotionCooldownPreventsProfileFlapping() {
	Policy policy(Profile::Crisp);
	Observation pressured      = difficultHighCpuObservation();
	pressured.deadlinePressure = DeadlinePressure::Critical;
	Decision decision;
	for (int index = 0; index < Policy::profileHysteresisObservations; ++index) {
		decision = policy.evaluate(pressured);
	}
	QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Crisp));
	QCOMPARE(enumValue(decision.candidateProfile), enumValue(Profile::Light));
	QVERIFY(decision.profileSwitchApplied);
	policy.commitProfile(decision.candidateProfile);

	const Observation recovered = difficultHighCpuObservation();
	for (int index = 0; index < Policy::pressureDemotionCooldownObservations; ++index) {
		decision = policy.evaluate(recovered);
		QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Light));
		QVERIFY(!decision.profileSwitchApplied);
	}
	for (int index = 0; index < Policy::profileHysteresisObservations; ++index) {
		decision = policy.evaluate(recovered);
	}
	QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Light));
	QCOMPARE(enumValue(decision.candidateProfile), enumValue(Profile::Crisp));
	QVERIFY(decision.profileSwitchApplied);
	policy.commitProfile(decision.candidateProfile);
	QCOMPARE(enumValue(policy.activeProfile()), enumValue(Profile::Crisp));
}

void TestInputEnhancementAuto::automaticControlAdjustmentIsBoundedAndHysteretic() {
	Policy policy(Profile::Balanced);
	Observation observation        = difficultHighCpuObservation();
	observation.userNoiseReduction = 35;
	observation.userNaturalCrisp   = 65;

	const Decision first = policy.evaluate(observation);
	QCOMPARE(first.noiseReduction, 35);
	QCOMPARE(first.naturalCrisp, 65);

	const Decision second = policy.evaluate(observation);
	QVERIFY(second.noiseReduction >= 15 && second.noiseReduction <= 55);
	QVERIFY(second.naturalCrisp >= 45 && second.naturalCrisp <= 85);
	QCOMPARE(second.noiseReduction, 52);
	QCOMPARE(second.naturalCrisp, 51);

	observation.userNoiseReduction = 100;
	observation.userNaturalCrisp   = 0;
	const Decision clamped         = policy.evaluate(observation);
	QCOMPARE(clamped.noiseReduction, 100);
	QCOMPARE(clamped.naturalCrisp, 0);
}

void TestInputEnhancementAuto::fixedProfileAndPreparedAvailabilityConstrainSwitching() {
	Policy fixed(Profile::Balanced);
	Observation observation        = difficultHighCpuObservation();
	observation.allowProfileSwitch = false;
	for (int index = 0; index < Policy::profileHysteresisObservations + 1; ++index) {
		const Decision decision = fixed.evaluate(observation);
		QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Balanced));
		QVERIFY(!decision.profileSwitchApplied);
	}

	Policy constrained(Profile::Light);
	observation.allowProfileSwitch = true;
	observation.crispAvailable     = false;
	observation.balancedAvailable  = true;
	Decision decision;
	for (int index = 0; index < Policy::profileHysteresisObservations; ++index) {
		decision = constrained.evaluate(observation);
	}
	QCOMPARE(enumValue(decision.candidateProfile), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(decision.activeProfile), enumValue(Profile::Light));
	QVERIFY(decision.profileSwitchApplied);
	constrained.commitProfile(decision.candidateProfile);
	QCOMPARE(enumValue(constrained.activeProfile()), enumValue(Profile::Balanced));
}

void TestInputEnhancementAuto::observationTrackerEmitsOnlyCoarsePeriodicState() {
	ObservationTracker tracker;
	std::array< short, Mumble::InputEnhancement::frameSamples > silence = {};
	Observation observation;
	for (std::uint16_t frame = 0; frame < ObservationTracker::framesPerObservation - 1; ++frame) {
		tracker.captureFrame(silence.data(), static_cast< unsigned int >(silence.size()));
		QVERIFY(!tracker.produceObservation(0.0f, CpuClass::Standard, DeadlinePressure::None, 48, 62, observation));
	}
	tracker.captureFrame(silence.data(), static_cast< unsigned int >(silence.size()));
	QVERIFY(tracker.produceObservation(0.0f, CpuClass::Standard, DeadlinePressure::Elevated, 48, 62, observation));
	QCOMPARE(enumValue(observation.noiseFloor), enumValue(NoiseFloorBucket::VeryLow));
	QCOMPARE(enumValue(observation.vadConfidence), enumValue(VadConfidenceBucket::Silent));
	QCOMPARE(enumValue(observation.deadlinePressure), enumValue(DeadlinePressure::Elevated));
	QCOMPARE(observation.stationaryScore, std::uint8_t{ 100 });
	QCOMPARE(observation.userNoiseReduction, 48);
	QCOMPARE(observation.userNaturalCrisp, 62);
}

void TestInputEnhancementAuto::safeSwitchGateRequiresIdleAndSupportsRepeatedPreparedCandidates() {
	SafeProfileSwitchGate gate(Profile::Crisp);
	Decision decision;
	decision.candidateProfile     = Profile::Balanced;
	decision.profileSwitchApplied = true;
	gate.reserve(decision);
	QVERIFY(gate.pending());
	QCOMPARE(enumValue(gate.poll(false, true).action), enumValue(SwitchGateAction::None));
	const SwitchGateResult applied = gate.poll(true, true);
	QCOMPARE(enumValue(applied.action), enumValue(SwitchGateAction::ApplyPrepared));
	QCOMPARE(enumValue(applied.profile), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(gate.activeProfile()), enumValue(Profile::Balanced));
	QVERIFY(gate.switchingAllowed());

	decision.candidateProfile = Profile::Light;
	gate.reserve(decision);
	QVERIFY(gate.pending());
	QCOMPARE(enumValue(gate.poll(true, true).action), enumValue(SwitchGateAction::ApplyPrepared));
	QCOMPARE(enumValue(gate.activeProfile()), enumValue(Profile::Light));
	QVERIFY(gate.switchingAllowed());

	decision.candidateProfile = Profile::Crisp;
	gate.reserve(decision);
	QCOMPARE(enumValue(gate.poll(true, false).action), enumValue(SwitchGateAction::RejectUnavailable));
	QCOMPARE(enumValue(gate.activeProfile()), enumValue(Profile::Light));
	QVERIFY(gate.switchingAllowed());

	gate.reserve(decision);
	QCOMPARE(enumValue(gate.poll(true, true).action), enumValue(SwitchGateAction::ApplyPrepared));
	QCOMPARE(enumValue(gate.activeProfile()), enumValue(Profile::Crisp));
}

void TestInputEnhancementAuto::acousticSilenceBoundaryWorksDuringAnOpenTransportStream() {
	AcousticSilenceSwitchBoundary boundary;
	for (std::uint16_t frame = 0; frame < AcousticSilenceSwitchBoundary::minimumSilentFrames - 1; ++frame) {
		QVERIFY(!boundary.observe(false, false));
	}
	QVERIFY(boundary.observe(false, false));
	QCOMPARE(boundary.silentFrames(), AcousticSilenceSwitchBoundary::minimumSilentFrames);

	// Transport/PTT state is deliberately not an input: acoustic speech and an
	// active causal drain are the only conditions that can invalidate the safe
	// handoff boundary.
	QVERIFY(!boundary.observe(true, false));
	QCOMPARE(boundary.silentFrames(), std::uint16_t{ 0 });
	for (std::uint16_t frame = 0; frame < AcousticSilenceSwitchBoundary::minimumSilentFrames; ++frame) {
		(void) boundary.observe(false, false);
	}
	QVERIFY(!boundary.observe(false, true));
	QCOMPARE(boundary.silentFrames(), std::uint16_t{ 0 });
}

void TestInputEnhancementAuto::preparedPipelineBankReplenishesRetiredNeuralCandidates() {
	std::atomic_int balancedBuilds{ 0 };
	std::atomic_int crispBuilds{ 0 };
	auto balancedFactory = [&balancedBuilds] {
		balancedBuilds.fetch_add(1, std::memory_order_relaxed);
		return std::make_unique< Pipeline >();
	};
	auto crispFactory = [&crispBuilds] {
		crispBuilds.fetch_add(1, std::memory_order_relaxed);
		return std::make_unique< Pipeline >();
	};

	PreparedPipelineBank bank;
	QVERIFY(bank.initialize(Profile::Crisp, balancedFactory, crispFactory));
	QCOMPARE(enumValue(bank.activeProfile()), enumValue(Profile::Crisp));
	QVERIFY(bank.activePipeline());
	Pipeline *firstCrisp = bank.activePipeline();
	QCOMPARE(balancedBuilds.load(std::memory_order_relaxed), 1);
	QCOMPARE(crispBuilds.load(std::memory_order_relaxed), 1);

	QVERIFY(bank.candidatePrepared(Profile::Balanced));
	QVERIFY(bank.switchTo(Profile::Balanced));
	QCOMPARE(enumValue(bank.activeProfile()), enumValue(Profile::Balanced));
	QVERIFY(bank.activePipeline());
	QVERIFY(bank.activePipeline() != firstCrisp);
	QTRY_COMPARE_WITH_TIMEOUT(crispBuilds.load(std::memory_order_relaxed), 2, 2000);
	QTRY_VERIFY_WITH_TIMEOUT(bank.candidatePrepared(Profile::Crisp), 2000);

	QVERIFY(bank.switchTo(Profile::Crisp));
	QVERIFY(bank.activePipeline() != firstCrisp);
	QTRY_COMPARE_WITH_TIMEOUT(balancedBuilds.load(std::memory_order_relaxed), 2, 2000);
	QTRY_VERIFY_WITH_TIMEOUT(bank.candidatePrepared(Profile::Balanced), 2000);

	QVERIFY(bank.switchTo(Profile::Light));
	QCOMPARE(enumValue(bank.activeProfile()), enumValue(Profile::Light));
	QVERIFY(!bank.activePipeline());
	QTRY_COMPARE_WITH_TIMEOUT(crispBuilds.load(std::memory_order_relaxed), 3, 2000);
	QVERIFY(bank.switchTo(Profile::Balanced));
	QCOMPARE(enumValue(bank.activeProfile()), enumValue(Profile::Balanced));
	bank.stop();
	QVERIFY(!bank.activePipeline());
}

void TestInputEnhancementAuto::probationRequiresBothElapsedAndSpeechThresholds() {
	Probation probation;
	probation.start(0xCAFE, 0xBEEF);

	ProbationResult result = probation.observe({ 50'000, 9'000, ProbationFailure::None });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::Running));
	QCOMPARE(enumValue(result.action), enumValue(ProbationAction::None));
	QCOMPARE(result.activeRecipeToken, std::uint64_t{ 0xCAFE });

	result = probation.observe({ 10'000, 999, ProbationFailure::None });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::Running));
	QCOMPARE(result.elapsedMilliseconds, std::uint64_t{ 60'000 });
	QCOMPARE(result.processedSpeechMilliseconds, std::uint64_t{ 9'999 });

	result = probation.observe({ 1, 1, ProbationFailure::None });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::Passed));
	QCOMPARE(enumValue(result.action), enumValue(ProbationAction::MarkHealthy));
	QCOMPARE(result.activeRecipeToken, std::uint64_t{ 0xCAFE });

	const ProbationResult repeated = probation.observe({ 1'000, 1'000, ProbationFailure::None });
	QCOMPARE(enumValue(repeated.action), enumValue(ProbationAction::None));
	QCOMPARE(repeated.elapsedMilliseconds, result.elapsedMilliseconds);
}

void TestInputEnhancementAuto::probationImmediatelyRollsBackEveryFailureSignal_data() {
	QTest::addColumn< int >("failure");
	QTest::newRow("initialization") << enumValue(ProbationFailure::InitializationFailure);
	QTest::newRow("invalid-output") << enumValue(ProbationFailure::InvalidOutput);
	QTest::newRow("deadline-miss") << enumValue(ProbationFailure::DeadlineMiss);
	QTest::newRow("crash") << enumValue(ProbationFailure::CrashDetected);
}

void TestInputEnhancementAuto::probationImmediatelyRollsBackEveryFailureSignal() {
	QFETCH(int, failure);
	const auto failureSignal = static_cast< ProbationFailure >(failure);
	Probation probation;
	probation.start(17, 9);

	const ProbationResult result = probation.observe({ 0, 0, failureSignal });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::RolledBack));
	QCOMPARE(enumValue(result.action), enumValue(ProbationAction::Rollback));
	QCOMPARE(enumValue(result.failure), failure);
	QCOMPARE(result.activeRecipeToken, std::uint64_t{ 9 });
	QCOMPARE(probation.activeRecipeToken(), std::uint64_t{ 9 });
}

void TestInputEnhancementAuto::probationFailureWinsAtHealthBoundary() {
	Probation probation;
	probation.start(22, 11);
	const ProbationResult result = probation.observe(
		{ static_cast< std::uint32_t >(Probation::requiredElapsedMilliseconds),
		  static_cast< std::uint32_t >(Probation::requiredSpeechMilliseconds), ProbationFailure::InvalidOutput });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::RolledBack));
	QCOMPARE(enumValue(result.action), enumValue(ProbationAction::Rollback));
	QCOMPARE(result.activeRecipeToken, std::uint64_t{ 11 });
}

void TestInputEnhancementAuto::probationClampsImpossibleSpeechDuration() {
	Probation probation;
	probation.start(2, 1);
	const ProbationResult result = probation.observe({ 1'000, 5'000, ProbationFailure::None });
	QCOMPARE(result.elapsedMilliseconds, std::uint64_t{ 1'000 });
	QCOMPARE(result.processedSpeechMilliseconds, std::uint64_t{ 1'000 });
	QCOMPARE(enumValue(result.status), enumValue(ProbationStatus::Running));
}

QTEST_GUILESS_MAIN(TestInputEnhancementAuto)

#include "TestInputEnhancementAuto.moc"
