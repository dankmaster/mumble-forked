// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementAutoV2.h"

#include <QtTest>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

using namespace Mumble::InputEnhancement;
using namespace Mumble::InputEnhancement::AutoV2;

namespace {
constexpr auto profileIndex(const Profile profile) {
	return static_cast< std::size_t >(profile);
}

constexpr auto abortIndex(const TransitionAbortReason reason) {
	return static_cast< std::size_t >(reason);
}

constexpr auto fallbackIndex(const FallbackReason reason) {
	return static_cast< std::size_t >(reason);
}

AutoRecipeCandidateBinding candidateBinding(const Profile profile) {
	AutoRecipeCandidateBinding candidate;
	candidate.profile            = profile;
	RecipeBinding &binding       = candidate.recipe;
	binding.catalogRevision      = QStringLiteral("input-recipes-v2");
	binding.recipeRevision       = 2;
	binding.requestedProfile     = Profile::Auto;
	binding.effectiveProfile     = profile;
	binding.executionFingerprint = QString(64, QLatin1Char('a'));
	binding.noiseReduction       = 60;
	binding.naturalCrisp         = 60;

	switch (profile) {
		case Profile::Light:
			binding.recipeId             = QStringLiteral("input.auto.light.speex");
			binding.engine               = Engine::Speex;
			binding.latencyBudgetSamples = lightLatencyBudgetSamples;
			binding.minimumCpuClass      = CpuClass::Low;
			break;
		case Profile::Balanced:
			binding.recipeId             = QStringLiteral("input.auto.balanced.rnnoise-embedded");
			binding.engine               = Engine::RNNoise;
			binding.modelId              = QStringLiteral("rnnoise:embedded");
			binding.modelSha256          = QString(64, QLatin1Char('b'));
			binding.modelRelativePath    = QStringLiteral("rnnoise/embedded.weights");
			binding.latencyBudgetSamples = balancedLatencyBudgetSamples;
			binding.minimumCpuClass      = CpuClass::Standard;
			break;
		case Profile::Quality:
			binding.recipeId             = QStringLiteral("input.auto.quality.deepfilternet-low-latency");
			binding.engine               = Engine::DeepFilterNet;
			binding.modelId              = QStringLiteral("deepfilternet:low-latency");
			binding.modelSha256          = QString(64, QLatin1Char('c'));
			binding.modelRelativePath    = QStringLiteral("deepfilternet/DeepFilterNet3_ll.tar.gz");
			binding.latencyBudgetSamples = qualityLatencyBudgetSamples;
			binding.minimumCpuClass      = CpuClass::High;
			break;
		case Profile::VoiceFocus:
			binding.recipeId             = QStringLiteral("input.auto.voice-focus.deepfilternet-low-latency");
			binding.engine               = Engine::DeepFilterNet;
			binding.modelId              = QStringLiteral("deepfilternet:low-latency");
			binding.modelSha256          = QString(64, QLatin1Char('d'));
			binding.modelRelativePath    = QStringLiteral("deepfilternet/DeepFilterNet3_ll.tar.gz");
			binding.latencyBudgetSamples = voiceFocusLatencyBudgetSamples;
			binding.minimumCpuClass      = CpuClass::High;
			break;
		case Profile::Original:
		case Profile::Auto:
			break;
	}
	return candidate;
}

AutoRecipeSetBinding validRecipeSet() {
	return makeAutoRecipeSetBinding(
		QStringLiteral("input-recipes-v2"), adaptationPolicyVersion, qualifiedMixCurveVersion, CpuClass::High,
		{ candidateBinding(Profile::Quality), candidateBinding(Profile::Light), candidateBinding(Profile::Balanced) });
}

TransitionFrameHealth qualifiedSilence() {
	TransitionFrameHealth health;
	health.verifiedSilentFrames = AutoV1::AcousticSilenceSwitchBoundary::minimumSilentFrames;
	health.acousticSpeech       = false;
	health.tailDrainActive      = false;
	return health;
}

TransitionCoordinator::Frame constantFrame(const float value) {
	TransitionCoordinator::Frame frame;
	frame.fill(value);
	return frame;
}
} // namespace

static_assert(noexcept(std::declval< TransitionCoordinator & >().processDualPipelineFrame(
	std::declval< const TransitionCoordinator::Frame & >(), std::declval< const TransitionCoordinator::Frame & >(),
	std::declval< const TransitionFrameHealth & >(), std::declval< TransitionCoordinator::Frame & >())));
static_assert(noexcept(std::declval< SessionDiagnostics & >().recordCallbackFrame(Profile::Balanced, 0)));
static_assert(std::is_nothrow_constructible_v< TransitionCoordinator >);

class TestInputEnhancementAutoV2 : public QObject {
	Q_OBJECT

private slots:
	void capabilityProbeIsBoundCachedAndClassifiedWithoutThreadCount();
	void recipeSetFingerprintIsCanonicalAndExact();
	void recipeSetRejectsMissingForbiddenAndDriftedMembers();
	void sessionDiagnosticsCoverEveryRequiredSignal();
	void transitionAlignsAndCrossfadesAfterVerifiedSilence();
	void transitionQuarantinesInvalidAndLateCandidates();
	void transitionRequiresContinuedSilenceForRebase();
};

void TestInputEnhancementAutoV2::capabilityProbeIsBoundCachedAndClassifiedWithoutThreadCount() {
	CapabilityProbeKey key{ QString(64, QLatin1Char('a')), QString(64, QLatin1Char('b')),
							QString(64, QLatin1Char('c')) };
	const QString fingerprint = capabilityProbeBindingFingerprint(key);
	QCOMPARE(fingerprint.size(), 64);

	CapabilityProbeKey drifted = key;
	drifted.modelSetSha256     = QString(64, QLatin1Char('d'));
	QVERIFY(capabilityProbeBindingFingerprint(drifted) != fingerprint);
	drifted             = key;
	drifted.buildSha256 = QString(64, QLatin1Char('e'));
	QVERIFY(capabilityProbeBindingFingerprint(drifted) != fingerprint);
	drifted           = key;
	drifted.cpuSha256 = QString(64, QLatin1Char('f'));
	QVERIFY(capabilityProbeBindingFingerprint(drifted) != fingerprint);

	QCOMPARE(classifyCapabilityProbeDuration(2'500'000), CpuClass::High);
	QCOMPARE(classifyCapabilityProbeDuration(2'500'001), CpuClass::Standard);
	QCOMPARE(classifyCapabilityProbeDuration(7'500'000), CpuClass::Standard);
	QCOMPARE(classifyCapabilityProbeDuration(7'500'001), CpuClass::Low);

	const CapabilityProbeResult first  = cachedCapabilityProbe(key);
	const CapabilityProbeResult second = cachedCapabilityProbe(key);
	QVERIFY(first.key == key);
	QCOMPARE(first.bindingFingerprint, fingerprint);
	QCOMPARE(second.elapsedNanoseconds, first.elapsedNanoseconds);
	QCOMPARE(second.cpuTier, first.cpuTier);
	QCOMPARE(second.valid, first.valid);
	QVERIFY(first.elapsedNanoseconds > 0);
	if (first.valid) {
		QVERIFY(first.elapsedNanoseconds <= capabilityProbeMaximumNanoseconds);
		QCOMPARE(first.cpuTier, classifyCapabilityProbeDuration(first.elapsedNanoseconds));
	} else {
		QCOMPARE(first.cpuTier, CpuClass::Low);
	}

	const CapabilityProbeKey current = currentCapabilityProbeKey(key.modelSetSha256);
	QVERIFY(isValidAutoRecipeSetFingerprint(current.buildSha256));
	QVERIFY(isValidAutoRecipeSetFingerprint(current.cpuSha256));
	QCOMPARE(current.modelSetSha256, key.modelSetSha256);
}

void TestInputEnhancementAutoV2::recipeSetFingerprintIsCanonicalAndExact() {
	const AutoRecipeSetBinding binding = validRecipeSet();
	QCOMPARE(binding.candidates[0].profile, Profile::Light);
	QCOMPARE(binding.candidates[1].profile, Profile::Balanced);
	QCOMPARE(binding.candidates[2].profile, Profile::Quality);
	QCOMPARE(binding.candidates[2].recipe.requestedProfile, Profile::Auto);
	QCOMPARE(binding.setFingerprint.size(), 64);
	QCOMPARE(binding.setFingerprint, autoRecipeSetFingerprint(binding));

	const RecipeSetValidationResult valid = validateAutoRecipeSetBinding(
		binding, QStringLiteral("input-recipes-v2"), adaptationPolicyVersion, qualifiedMixCurveVersion, CpuClass::High);
	QVERIFY(valid);

	AutoRecipeSetBinding permuted = binding;
	std::swap(permuted.candidates[0], permuted.candidates[2]);
	// The digest is canonical regardless of caller order, while persisted form
	// must itself remain ordered to avoid ambiguous set representations.
	QCOMPARE(autoRecipeSetFingerprint(permuted), binding.setFingerprint);
	QCOMPARE(validateAutoRecipeSetBinding(permuted, QStringLiteral("input-recipes-v2"), adaptationPolicyVersion,
										  qualifiedMixCurveVersion, CpuClass::High)
				 .error,
			 RecipeSetValidationError::NonCanonicalOrder);

	AutoRecipeSetBinding tampered = binding;
	tampered.candidates[1].recipe.noiseReduction += 1;
	QCOMPARE(validateAutoRecipeSetBinding(tampered, QStringLiteral("input-recipes-v2"), adaptationPolicyVersion,
										  qualifiedMixCurveVersion, CpuClass::High)
				 .error,
			 RecipeSetValidationError::FingerprintMismatch);

	AutoRecipeSetBinding concreteInsteadOfAuto                  = binding;
	concreteInsteadOfAuto.candidates[0].recipe.requestedProfile = Profile::Light;
	concreteInsteadOfAuto.setFingerprint                        = autoRecipeSetFingerprint(concreteInsteadOfAuto);
	QCOMPARE(validateAutoRecipeSetBinding(concreteInsteadOfAuto, QStringLiteral("input-recipes-v2"),
										  adaptationPolicyVersion, qualifiedMixCurveVersion, CpuClass::High)
				 .error,
			 RecipeSetValidationError::InvalidCandidateBinding);
}

void TestInputEnhancementAutoV2::recipeSetRejectsMissingForbiddenAndDriftedMembers() {
	const AutoRecipeSetBinding valid = validRecipeSet();

	AutoRecipeSetBinding duplicate = makeAutoRecipeSetBinding(
		valid.catalogRevision, valid.policyVersion, valid.mixVersion, valid.cpuTier,
		{ candidateBinding(Profile::Light), candidateBinding(Profile::Balanced), candidateBinding(Profile::Balanced) });
	RecipeSetValidationResult result = validateAutoRecipeSetBinding(
		duplicate, valid.catalogRevision, valid.policyVersion, valid.mixVersion, valid.cpuTier);
	QCOMPARE(result.error, RecipeSetValidationError::DuplicateCandidate);
	QCOMPARE(result.safeProfile, Profile::Original);

	AutoRecipeSetBinding voiceFocus =
		makeAutoRecipeSetBinding(valid.catalogRevision, valid.policyVersion, valid.mixVersion, valid.cpuTier,
								 { candidateBinding(Profile::Light), candidateBinding(Profile::Balanced),
								   candidateBinding(Profile::VoiceFocus) });
	result = validateAutoRecipeSetBinding(voiceFocus, valid.catalogRevision, valid.policyVersion, valid.mixVersion,
										  valid.cpuTier);
	QCOMPARE(result.error, RecipeSetValidationError::ForbiddenCandidate);
	QCOMPARE(result.safeProfile, Profile::Original);

	result = validateAutoRecipeSetBinding(valid, QStringLiteral("input-recipes-v3"), valid.policyVersion,
										  valid.mixVersion, valid.cpuTier);
	QCOMPARE(result.error, RecipeSetValidationError::CatalogDrift);
	QCOMPARE(result.safeProfile, Profile::Original);
	result = validateAutoRecipeSetBinding(valid, valid.catalogRevision, valid.policyVersion + 1, valid.mixVersion,
										  valid.cpuTier);
	QCOMPARE(result.error, RecipeSetValidationError::PolicyDrift);
	result = validateAutoRecipeSetBinding(valid, valid.catalogRevision, valid.policyVersion, valid.mixVersion + 1,
										  valid.cpuTier);
	QCOMPARE(result.error, RecipeSetValidationError::MixDrift);
	result = validateAutoRecipeSetBinding(valid, valid.catalogRevision, valid.policyVersion, valid.mixVersion,
										  CpuClass::Standard);
	QCOMPARE(result.error, RecipeSetValidationError::CpuTierDrift);
}

void TestInputEnhancementAutoV2::sessionDiagnosticsCoverEveryRequiredSignal() {
	SessionDiagnostics diagnostics;
	diagnostics.recordDecision(Profile::Balanced);
	diagnostics.recordDecision(Profile::Quality);
	diagnostics.recordCallbackFrame(Profile::Balanced, 500'000);
	diagnostics.recordCallbackFrame(Profile::Quality, 5'100'000);
	diagnostics.recordCallbackFrame(Profile::Quality, 12'000'000);
	diagnostics.recordTransitionStarted(Profile::Balanced, Profile::Quality);
	diagnostics.recordTransitionState(TransitionState::Fading);
	diagnostics.recordQuarantine(Profile::Quality);
	diagnostics.recordFallback(FallbackReason::InvalidOutput);
	diagnostics.recordTransitionAbort(TransitionAbortReason::CandidateInvalidOutput);

	const SessionDiagnosticsSnapshot snapshot = diagnostics.snapshot();
	QCOMPARE(snapshot.decisions, std::uint64_t{ 2 });
	QCOMPARE(snapshot.decisionsByProfile[profileIndex(Profile::Balanced)], std::uint64_t{ 1 });
	QCOMPARE(snapshot.decisionsByProfile[profileIndex(Profile::Quality)], std::uint64_t{ 1 });
	QCOMPARE(snapshot.profileResidencyFrames[profileIndex(Profile::Balanced)], std::uint64_t{ 1 });
	QCOMPARE(snapshot.profileResidencyFrames[profileIndex(Profile::Quality)], std::uint64_t{ 2 });
	QCOMPARE(snapshot.transitionsStarted, std::uint64_t{ 1 });
	QCOMPARE(snapshot.transitionsAborted, std::uint64_t{ 1 });
	QCOMPARE(snapshot.transitionAbortsByReason[abortIndex(TransitionAbortReason::CandidateInvalidOutput)],
			 std::uint64_t{ 1 });
	QCOMPARE(snapshot.quarantineByProfile[profileIndex(Profile::Quality)], std::uint64_t{ 1 });
	QCOMPARE(snapshot.fallbackByReason[fallbackIndex(FallbackReason::InvalidOutput)], std::uint64_t{ 1 });
	QCOMPARE(snapshot.callbackFrames, std::uint64_t{ 3 });
	QCOMPARE(snapshot.callbackMaximumNanoseconds, std::uint64_t{ 12'000'000 });
	QCOMPARE(snapshot.callbackHistogram[0], std::uint64_t{ 1 });
	QCOMPARE(snapshot.callbackHistogram[4], std::uint64_t{ 1 });
	QCOMPARE(snapshot.callbackHistogram[6], std::uint64_t{ 1 });
	QCOMPARE(snapshot.transitionState, TransitionState::Abort);
	QCOMPARE(SessionDiagnostics::callbackBucketUpperBoundNanoseconds(6), std::numeric_limits< std::uint64_t >::max());
}

void TestInputEnhancementAutoV2::transitionAlignsAndCrossfadesAfterVerifiedSilence() {
	SessionDiagnostics diagnostics;
	TransitionCoordinator coordinator(&diagnostics);
	QVERIFY(
		coordinator.begin(Profile::Light, lightLatencyBudgetSamples, Profile::Quality, qualityLatencyBudgetSamples));
	QVERIFY(coordinator.dualPipelineRequired());

	TransitionFrameHealth notYetQualified = qualifiedSilence();
	notYetQualified.verifiedSilentFrames  = AutoV1::AcousticSilenceSwitchBoundary::minimumSilentFrames - 1;
	TransitionCoordinator::Frame output;
	for (int frame = 1; frame <= 4; ++frame) {
		const auto result = coordinator.processDualPipelineFrame(constantFrame(static_cast< float >(frame)),
																 constantFrame(0.0f), notYetQualified, output);
		QCOMPARE(result.state, TransitionState::Priming);
		QCOMPARE(output.front(), static_cast< float >(frame));
	}

	const TransitionFrameHealth silence = qualifiedSilence();
	auto result = coordinator.processDualPipelineFrame(constantFrame(5.0f), constantFrame(1.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Fading);
	QCOMPARE(output.front(), 5.0f);

	// The source has four frames less natural latency than the candidate. Its
	// value 6 is delayed to 2, exactly matching the candidate's value 2 before
	// the first equal-power sample is emitted.
	result = coordinator.processDualPipelineFrame(constantFrame(6.0f), constantFrame(2.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Fading);
	QVERIFY(std::abs(output.front() - 2.0f) < 0.0001f);
	QVERIFY(output[frameSamples / 2] > 2.0f);

	result = coordinator.processDualPipelineFrame(constantFrame(7.0f), constantFrame(3.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Fading);
	result = coordinator.processDualPipelineFrame(constantFrame(8.0f), constantFrame(4.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Fading);
	result = coordinator.processDualPipelineFrame(constantFrame(9.0f), constantFrame(5.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Rebase);
	QCOMPARE(coordinator.fadedSamples(), TransitionCoordinator::crossfadeSamples);
	QVERIFY(std::abs(output.back() - 5.0f) < 0.0001f);

	result = coordinator.processDualPipelineFrame(constantFrame(10.0f), constantFrame(6.0f), silence, output);
	QCOMPARE(result.state, TransitionState::Active);
	QVERIFY(result.completedThisFrame);
	QVERIFY(!result.dualPipelineRequired);
	QCOMPARE(result.outputProfile, Profile::Quality);
	QCOMPARE(output.front(), 6.0f);

	const auto snapshot = diagnostics.snapshot();
	QCOMPARE(snapshot.transitionsStarted, std::uint64_t{ 1 });
	QCOMPARE(snapshot.transitionsCompleted, std::uint64_t{ 1 });
	QCOMPARE(snapshot.transitionsAborted, std::uint64_t{ 0 });
}

void TestInputEnhancementAutoV2::transitionQuarantinesInvalidAndLateCandidates() {
	SessionDiagnostics diagnostics;
	TransitionCoordinator coordinator(&diagnostics);
	QVERIFY(coordinator.begin(Profile::Balanced, balancedLatencyBudgetSamples, Profile::Quality,
							  qualityLatencyBudgetSamples));
	TransitionCoordinator::Frame output;
	const auto source = constantFrame(0.25f);
	auto candidate    = constantFrame(0.5f);
	auto health       = qualifiedSilence();
	(void) coordinator.processDualPipelineFrame(source, candidate, health, output);

	candidate[17] = std::numeric_limits< float >::quiet_NaN();
	auto result   = coordinator.processDualPipelineFrame(source, candidate, health, output);
	QCOMPARE(result.state, TransitionState::Abort);
	QCOMPARE(result.abortReason, TransitionAbortReason::CandidateInvalidOutput);
	QVERIFY(result.abortedThisFrame);
	QCOMPARE(output.front(), 0.25f);

	auto snapshot = diagnostics.snapshot();
	QCOMPARE(snapshot.quarantineEvents, std::uint64_t{ 1 });
	QCOMPARE(snapshot.fallbackByReason[fallbackIndex(FallbackReason::InvalidOutput)], std::uint64_t{ 1 });

	coordinator.reset();
	QVERIFY(coordinator.begin(Profile::Balanced, balancedLatencyBudgetSamples, Profile::Quality,
							  qualityLatencyBudgetSamples));
	candidate                   = constantFrame(0.5f);
	health.candidateDeadlineMet = false;
	result                      = coordinator.processDualPipelineFrame(source, candidate, health, output);
	QCOMPARE(result.state, TransitionState::Abort);
	QCOMPARE(result.abortReason, TransitionAbortReason::CandidateDeadlineMiss);
	snapshot = diagnostics.snapshot();
	QCOMPARE(snapshot.quarantineEvents, std::uint64_t{ 2 });
	QCOMPARE(snapshot.fallbackByReason[fallbackIndex(FallbackReason::DeadlineExceeded)], std::uint64_t{ 1 });

	coordinator.reset();
	QVERIFY(!coordinator.begin(Profile::Balanced, balancedLatencyBudgetSamples, Profile::VoiceFocus,
							   voiceFocusLatencyBudgetSamples));
	QCOMPARE(coordinator.abortReason(), TransitionAbortReason::InvalidConfiguration);
}

void TestInputEnhancementAutoV2::transitionRequiresContinuedSilenceForRebase() {
	TransitionCoordinator coordinator;
	QVERIFY(coordinator.begin(Profile::Balanced, balancedLatencyBudgetSamples, Profile::Quality,
							  qualityLatencyBudgetSamples));
	TransitionCoordinator::Frame output;
	const auto source    = constantFrame(0.0f);
	const auto candidate = constantFrame(0.0f);
	auto health          = qualifiedSilence();

	// Two priming frames cover the 20 ms latency difference, then four fade
	// frames reach Rebase.
	(void) coordinator.processDualPipelineFrame(source, candidate, health, output);
	QCOMPARE(coordinator.state(), TransitionState::Priming);
	(void) coordinator.processDualPipelineFrame(source, candidate, health, output);
	QCOMPARE(coordinator.state(), TransitionState::Fading);
	for (int frame = 0; frame < 4; ++frame) {
		(void) coordinator.processDualPipelineFrame(source, candidate, health, output);
	}
	QCOMPARE(coordinator.state(), TransitionState::Rebase);

	health.verifiedSilentFrames = 0;
	health.acousticSpeech       = true;
	const auto result           = coordinator.processDualPipelineFrame(source, candidate, health, output);
	QCOMPARE(result.state, TransitionState::Abort);
	QCOMPARE(result.abortReason, TransitionAbortReason::SilenceLost);
	QVERIFY(!result.completedThisFrame);
}

QTEST_GUILESS_MAIN(TestInputEnhancementAutoV2)

#include "TestInputEnhancementAutoV2.moc"
