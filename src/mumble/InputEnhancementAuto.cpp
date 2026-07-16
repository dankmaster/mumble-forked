// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementAuto.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Mumble::InputEnhancement::AutoV1 {
namespace {
	template< typename T > T saturatingAdd(T left, T right) noexcept {
		if (right > std::numeric_limits< T >::max() - left) {
			return std::numeric_limits< T >::max();
		}
		return left + right;
	}
} // namespace

Policy::Policy(Profile initialProfile) noexcept {
	reset(initialProfile);
}

Decision Policy::evaluate(const Observation &observation) noexcept {
	Profile candidate =
		observation.allowProfileSwitch ? availableProfile(chooseProfile(observation), observation) : m_activeProfile;
	const bool pressureCooldownActive = m_pressureDemotionCooldown > 0;
	if (pressureCooldownActive) {
		--m_pressureDemotionCooldown;
		if (profileCost(candidate) > profileCost(m_activeProfile)) {
			candidate = m_activeProfile;
		}
	}
	bool switchApplied  = false;
	bool switchDeferred = false;

	if (candidate == m_activeProfile) {
		m_pendingProfile      = m_activeProfile;
		m_profileObservations = 0;
	} else {
		if (candidate != m_pendingProfile) {
			m_pendingProfile      = candidate;
			m_profileObservations = 1;
		} else if (m_profileObservations < profileHysteresisObservations) {
			++m_profileObservations;
		}

		if (m_profileObservations >= profileHysteresisObservations) {
			if (observation.vadConfidence == VadConfidenceBucket::Silent) {
				const Profile previousProfile = m_activeProfile;
				m_pendingProfile              = candidate;
				m_profileObservations         = 0;
				switchApplied                 = true;
				if (profileCost(candidate) < profileCost(previousProfile)
					&& observation.deadlinePressure != DeadlinePressure::None) {
					m_pressureDemotionCooldown = pressureDemotionCooldownObservations;
				}
			} else {
				switchDeferred = true;
			}
		}
	}

	const int targetNoiseAdjustment     = noiseAdjustment(observation);
	const int targetCharacterAdjustment = characterAdjustment(observation);
	if (targetNoiseAdjustment == m_pendingNoiseAdjustment
		&& targetCharacterAdjustment == m_pendingCharacterAdjustment) {
		if (m_controlObservations < controlHysteresisObservations) {
			++m_controlObservations;
		}
	} else {
		m_pendingNoiseAdjustment     = targetNoiseAdjustment;
		m_pendingCharacterAdjustment = targetCharacterAdjustment;
		m_controlObservations        = 1;
	}
	if (m_controlObservations >= controlHysteresisObservations) {
		m_activeNoiseAdjustment     = m_pendingNoiseAdjustment;
		m_activeCharacterAdjustment = m_pendingCharacterAdjustment;
	}

	Decision decision;
	decision.activeProfile    = m_activeProfile;
	decision.activeEngine     = activeEngine();
	decision.candidateProfile = candidate;
	decision.noiseReduction   = clampControl(clampControl(observation.userNoiseReduction) + m_activeNoiseAdjustment);
	decision.naturalCrisp     = clampControl(clampControl(observation.userNaturalCrisp) + m_activeCharacterAdjustment);
	decision.stableObservations    = m_profileObservations;
	decision.profileSwitchApplied  = switchApplied;
	decision.profileSwitchDeferred = switchDeferred;
	decision.hysteresisPending     = candidate != m_activeProfile && !switchDeferred && !switchApplied;
	return decision;
}

void Policy::reset(Profile activeProfile) noexcept {
	m_activeProfile              = normalizeConcreteProfile(activeProfile);
	m_pendingProfile             = m_activeProfile;
	m_profileObservations        = 0;
	m_activeNoiseAdjustment      = 0;
	m_activeCharacterAdjustment  = 0;
	m_pendingNoiseAdjustment     = 0;
	m_pendingCharacterAdjustment = 0;
	m_controlObservations        = 0;
	m_pressureDemotionCooldown   = 0;
}

void Policy::commitProfile(Profile activeProfile) noexcept {
	m_activeProfile      = normalizeConcreteProfile(activeProfile);
	m_pendingProfile     = m_activeProfile;
	m_profileObservations = 0;
}

Profile Policy::activeProfile() const noexcept {
	return m_activeProfile;
}

Engine Policy::activeEngine() const noexcept {
	return engineForProfile(m_activeProfile);
}

Profile Policy::normalizeConcreteProfile(Profile profile) noexcept {
	switch (profile) {
		case Profile::Light:
		case Profile::Balanced:
		case Profile::Quality:
			return profile;
		case Profile::Original:
		case Profile::Auto:
		case Profile::VoiceFocus:
			return Profile::Balanced;
	}
	return Profile::Balanced;
}

Profile Policy::chooseProfile(const Observation &observation) noexcept {
	if (observation.cpuClass == CpuClass::Low || observation.deadlinePressure == DeadlinePressure::High
		|| observation.deadlinePressure == DeadlinePressure::Critical) {
		return Profile::Light;
	}

	if (observation.cpuClass == CpuClass::Standard) {
		return observation.deadlinePressure == DeadlinePressure::Elevated ? Profile::Light : Profile::Balanced;
	}

	if (observation.deadlinePressure == DeadlinePressure::Elevated) {
		return Profile::Balanced;
	}

	const bool difficultNoise = observation.snr == SnrBucket::VeryPoor || observation.snr == SnrBucket::Poor
								|| observation.noiseFloor == NoiseFloorBucket::High
								|| observation.noiseFloor == NoiseFloorBucket::VeryHigh;
	const bool dynamicNoise = observation.stationaryScore < 55;
	if (difficultNoise && dynamicNoise) {
		return Profile::Quality;
	}

	const bool easyStationaryEnvironment =
		observation.snr == SnrBucket::Excellent
		&& (observation.noiseFloor == NoiseFloorBucket::VeryLow || observation.noiseFloor == NoiseFloorBucket::Low)
		&& observation.stationaryScore >= 70;
	return easyStationaryEnvironment ? Profile::Light : Profile::Balanced;
}

Profile Policy::availableProfile(Profile desired, const Observation &observation) noexcept {
	if (desired == Profile::Quality && observation.crispAvailable) {
		return Profile::Quality;
	}
	if ((desired == Profile::Quality || desired == Profile::Balanced) && observation.balancedAvailable) {
		return Profile::Balanced;
	}
	if (observation.lightAvailable) {
		return Profile::Light;
	}
	// Light is the non-neural product baseline and is expected to be available.
	// If a caller reports otherwise, retaining the current profile is safer than
	// asking for an unprepared callback-time transition.
	return Profile::Original;
}

int Policy::profileCost(Profile profile) noexcept {
	switch (profile) {
		case Profile::Original:
			return 0;
		case Profile::Light:
			return 1;
		case Profile::Balanced:
			return 2;
		case Profile::Quality:
		case Profile::VoiceFocus:
			return 3;
		case Profile::Auto:
			return 2;
	}
	return 0;
}

Engine Policy::engineForProfile(Profile profile) noexcept {
	switch (profile) {
		case Profile::Light:
			return Engine::Speex;
		case Profile::Quality:
		case Profile::VoiceFocus:
			return Engine::DeepFilterNet;
		case Profile::Balanced:
		case Profile::Original:
		case Profile::Auto:
			return profile == Profile::Original ? Engine::None : Engine::RNNoise;
	}
	return Engine::RNNoise;
}

int Policy::clampControl(int value) noexcept {
	return std::clamp(value, 0, 100);
}

int Policy::noiseAdjustment(const Observation &observation) noexcept {
	int adjustment = 0;
	switch (observation.snr) {
		case SnrBucket::VeryPoor:
			adjustment = 20;
			break;
		case SnrBucket::Poor:
			adjustment = 14;
			break;
		case SnrBucket::Fair:
			adjustment = 8;
			break;
		case SnrBucket::Good:
			adjustment = 0;
			break;
		case SnrBucket::Excellent:
			adjustment = -8;
			break;
	}

	if (observation.noiseFloor == NoiseFloorBucket::High) {
		adjustment += 3;
	} else if (observation.noiseFloor == NoiseFloorBucket::VeryHigh) {
		adjustment += 6;
	}
	if (observation.stationaryScore >= 75) {
		adjustment += 3;
	} else if (observation.stationaryScore <= 25) {
		adjustment -= 3;
	}
	return std::clamp(adjustment, -maximumAutomaticAdjustment, maximumAutomaticAdjustment);
}

int Policy::characterAdjustment(const Observation &observation) noexcept {
	int adjustment = 0;
	if (observation.stationaryScore >= 75) {
		adjustment += 5;
	} else if (observation.stationaryScore <= 25) {
		adjustment -= 10;
	}

	if (observation.snr == SnrBucket::VeryPoor) {
		adjustment -= 8;
	} else if (observation.snr == SnrBucket::Poor) {
		adjustment -= 4;
	} else if (observation.snr == SnrBucket::Excellent) {
		adjustment += 4;
	}
	return std::clamp(adjustment, -maximumAutomaticAdjustment, maximumAutomaticAdjustment);
}

void ObservationTracker::reset() noexcept {
	m_currentPower      = 0.0;
	m_smoothedPower     = 0.0;
	m_noisePower        = 1.0e-7;
	m_intervalPower     = 0.0;
	m_intervalTransient = 0.0;
	m_frameCount        = 0;
	m_hasPowerEstimate  = false;
	m_frameCaptured     = false;
}

void ObservationTracker::captureFrame(const short *samples, unsigned int sampleCount) noexcept {
	double power = 0.0;
	if (samples && sampleCount > 0) {
		constexpr double inverseFullScale = 1.0 / 32768.0;
		for (unsigned int i = 0; i < sampleCount; ++i) {
			const double normalized = static_cast< double >(samples[i]) * inverseFullScale;
			power += normalized * normalized;
		}
		power /= static_cast< double >(sampleCount);
	}

	if (!std::isfinite(power) || power < 0.0) {
		power = 0.0;
	}
	if (!m_hasPowerEstimate) {
		m_smoothedPower    = power;
		m_hasPowerEstimate = true;
	}
	const double reference      = std::max(m_smoothedPower, 1.0e-9);
	const double relativeChange = std::min(std::abs(power - m_smoothedPower) / reference, 4.0);
	m_smoothedPower += 0.08 * (power - m_smoothedPower);
	m_currentPower = power;
	m_intervalTransient += relativeChange;
	m_frameCaptured = true;
}

bool ObservationTracker::produceObservation(float vadConfidence, CpuClass cpuClass, DeadlinePressure deadlinePressure,
											int userNoiseReduction, int userNaturalCrisp,
											Observation &observation) noexcept {
	if (!m_frameCaptured) {
		return false;
	}
	m_frameCaptured = false;
	vadConfidence   = std::isfinite(vadConfidence) ? std::clamp(vadConfidence, 0.0f, 1.0f) : 0.0f;
	if (vadConfidence < 0.35f) {
		const double alpha = m_frameCount == 0 ? 0.20 : 0.04;
		m_noisePower += alpha * (m_currentPower - m_noisePower);
		m_noisePower = std::max(m_noisePower, 1.0e-9);
	}
	m_intervalPower += m_currentPower;
	if (m_frameCount < framesPerObservation) {
		++m_frameCount;
	}
	if (m_frameCount < framesPerObservation) {
		return false;
	}

	const double averagePower     = m_intervalPower / static_cast< double >(framesPerObservation);
	const double averageTransient = m_intervalTransient / static_cast< double >(framesPerObservation);
	const double stationary       = std::clamp(100.0 * (1.0 - averageTransient / 2.0), 0.0, 100.0);

	observation.noiseFloor         = noiseFloorBucket(m_noisePower);
	observation.snr                = snrBucket(averagePower, m_noisePower);
	observation.stationaryScore    = static_cast< std::uint8_t >(stationary + 0.5);
	observation.vadConfidence      = vadBucket(vadConfidence);
	observation.cpuClass           = cpuClass;
	observation.deadlinePressure   = deadlinePressure;
	observation.userNoiseReduction = std::clamp(userNoiseReduction, 0, 100);
	observation.userNaturalCrisp   = std::clamp(userNaturalCrisp, 0, 100);

	m_frameCount        = 0;
	m_intervalPower     = 0.0;
	m_intervalTransient = 0.0;
	return true;
}

NoiseFloorBucket ObservationTracker::noiseFloorBucket(double power) noexcept {
	if (power < 1.0e-6) {
		return NoiseFloorBucket::VeryLow;
	}
	if (power < 1.0e-5) {
		return NoiseFloorBucket::Low;
	}
	if (power < 1.0e-4) {
		return NoiseFloorBucket::Medium;
	}
	if (power < 1.0e-3) {
		return NoiseFloorBucket::High;
	}
	return NoiseFloorBucket::VeryHigh;
}

SnrBucket ObservationTracker::snrBucket(double signalPower, double noisePower) noexcept {
	const double ratio = std::max(signalPower, 0.0) / std::max(noisePower, 1.0e-9);
	if (ratio < 1.6) {
		return SnrBucket::VeryPoor;
	}
	if (ratio < 3.2) {
		return SnrBucket::Poor;
	}
	if (ratio < 10.0) {
		return SnrBucket::Fair;
	}
	if (ratio < 31.7) {
		return SnrBucket::Good;
	}
	return SnrBucket::Excellent;
}

VadConfidenceBucket ObservationTracker::vadBucket(float confidence) noexcept {
	if (confidence < 0.20f) {
		return VadConfidenceBucket::Silent;
	}
	if (confidence < 0.45f) {
		return VadConfidenceBucket::Low;
	}
	if (confidence < 0.75f) {
		return VadConfidenceBucket::Medium;
	}
	return VadConfidenceBucket::High;
}

SafeProfileSwitchGate::SafeProfileSwitchGate(Profile activeProfile) noexcept {
	reset(activeProfile);
}

void SafeProfileSwitchGate::reset(Profile activeProfile) noexcept {
	switch (activeProfile) {
		case Profile::Light:
		case Profile::Balanced:
		case Profile::Quality:
			m_activeProfile = activeProfile;
			break;
		case Profile::Original:
		case Profile::Auto:
		case Profile::VoiceFocus:
			m_activeProfile = Profile::Balanced;
			break;
	}
	m_pendingProfile = m_activeProfile;
	m_pending        = false;
}

void SafeProfileSwitchGate::reserve(const Decision &decision) noexcept {
	if (m_pending || !decision.profileSwitchApplied || decision.candidateProfile == m_activeProfile) {
		return;
	}
	m_pendingProfile = decision.candidateProfile;
	m_pending        = true;
}

SwitchGateResult SafeProfileSwitchGate::poll(bool idleBoundary, bool candidatePrepared) noexcept {
	SwitchGateResult result;
	result.profile = m_pendingProfile;
	if (!m_pending || !idleBoundary) {
		return result;
	}
	m_pending = false;
	if (!candidatePrepared) {
		result.action = SwitchGateAction::RejectUnavailable;
		return result;
	}
	m_activeProfile = m_pendingProfile;
	result.action   = SwitchGateAction::ApplyPrepared;
	return result;
}

bool SafeProfileSwitchGate::switchingAllowed() const noexcept {
	return !m_pending;
}

bool SafeProfileSwitchGate::pending() const noexcept {
	return m_pending;
}

Profile SafeProfileSwitchGate::activeProfile() const noexcept {
	return m_activeProfile;
}

Profile SafeProfileSwitchGate::pendingProfile() const noexcept {
	return m_pendingProfile;
}

void AcousticSilenceSwitchBoundary::reset() noexcept {
	m_silentFrames = 0;
}

bool AcousticSilenceSwitchBoundary::observe(bool acousticSpeech, bool drainActive) noexcept {
	if (acousticSpeech || drainActive) {
		reset();
		return false;
	}
	if (m_silentFrames < std::numeric_limits< std::uint16_t >::max()) {
		++m_silentFrames;
	}
	return m_silentFrames >= minimumSilentFrames;
}

std::uint16_t AcousticSilenceSwitchBoundary::silentFrames() const noexcept {
	return m_silentFrames;
}

PreparedPipelineBank::~PreparedPipelineBank() {
	stop();
}

bool PreparedPipelineBank::initialize(Profile initialProfile, PipelineFactory balancedFactory,
									  PipelineFactory crispFactory) {
	stop();

	m_nodes[0].profile = Profile::Balanced;
	m_nodes[0].factory = std::move(balancedFactory);
	m_nodes[1].profile = Profile::Quality;
	m_nodes[1].factory = std::move(crispFactory);

	for (Node &node : m_nodes) {
		if (!node.factory) {
			node.state.store(NodeState::Unavailable, std::memory_order_relaxed);
			continue;
		}
		if (!buildNode(node)) {
			node.state.store(NodeState::Failed, std::memory_order_release);
		}
	}

	m_activePipeline.store(nullptr, std::memory_order_relaxed);
	m_activeProfile.store(Profile::Original, std::memory_order_relaxed);
	if (!switchTo(initialProfile)) {
		stop();
		return false;
	}

	m_stopRequested.store(false, std::memory_order_release);
	try {
		m_lifecycleWorker = std::thread([this] { lifecycleWorkerLoop(); });
	} catch (...) {
		stop();
		return false;
	}
	return true;
}

void PreparedPipelineBank::stop() noexcept {
	m_stopRequested.store(true, std::memory_order_release);
	signalLifecycleWorker();
	if (m_lifecycleWorker.joinable()) {
		m_lifecycleWorker.join();
	}

	m_activePipeline.store(nullptr, std::memory_order_release);
	m_activeProfile.store(Profile::Original, std::memory_order_release);
	for (Node &node : m_nodes) {
		node.state.store(NodeState::Unavailable, std::memory_order_release);
		node.pipeline.reset();
		node.factory = {};
	}
}

bool PreparedPipelineBank::candidatePrepared(Profile profile) const noexcept {
	if (profile == Profile::Light) {
		return true;
	}
	if (profile == m_activeProfile.load(std::memory_order_acquire)) {
		return true;
	}
	const Node *node = nodeFor(profile);
	return node && node->state.load(std::memory_order_acquire) == NodeState::Ready;
}

bool PreparedPipelineBank::switchTo(Profile profile) noexcept {
	const Profile previousProfile = m_activeProfile.load(std::memory_order_acquire);
	if (profile == previousProfile) {
		return true;
	}
	if (profile != Profile::Light && profile != Profile::Balanced && profile != Profile::Quality) {
		return false;
	}

	Node *nextNode         = nodeFor(profile);
	Pipeline *nextPipeline = nullptr;
	if (nextNode) {
		NodeState expected = NodeState::Ready;
		if (!nextNode->state.compare_exchange_strong(expected, NodeState::Active, std::memory_order_acq_rel,
													 std::memory_order_acquire)) {
			return false;
		}
		nextPipeline = nextNode->pipeline.get();
		if (!nextPipeline) {
			nextNode->state.store(NodeState::Failed, std::memory_order_release);
			return false;
		}
	}

	Pipeline *previousPipeline = m_activePipeline.exchange(nextPipeline, std::memory_order_acq_rel);
	m_activeProfile.store(profile, std::memory_order_release);
	Node *previousNode = nodeFor(previousProfile);
	if (previousNode) {
		// No callback access to previousPipeline occurs after this release. The
		// lifecycle worker may now destroy and reconstruct it safely.
		(void) previousPipeline;
		previousNode->state.store(NodeState::Retired, std::memory_order_release);
		signalLifecycleWorker();
	}
	return true;
}

Pipeline *PreparedPipelineBank::activePipeline() noexcept {
	return m_activePipeline.load(std::memory_order_acquire);
}

const Pipeline *PreparedPipelineBank::activePipeline() const noexcept {
	return m_activePipeline.load(std::memory_order_acquire);
}

Profile PreparedPipelineBank::activeProfile() const noexcept {
	return m_activeProfile.load(std::memory_order_acquire);
}

bool PreparedPipelineBank::candidateFailed(Profile profile) const noexcept {
	const Node *node = nodeFor(profile);
	return node && node->state.load(std::memory_order_acquire) == NodeState::Failed;
}

PreparedPipelineBank::Node *PreparedPipelineBank::nodeFor(Profile profile) noexcept {
	for (Node &node : m_nodes) {
		if (node.profile == profile) {
			return &node;
		}
	}
	return nullptr;
}

const PreparedPipelineBank::Node *PreparedPipelineBank::nodeFor(Profile profile) const noexcept {
	for (const Node &node : m_nodes) {
		if (node.profile == profile) {
			return &node;
		}
	}
	return nullptr;
}

bool PreparedPipelineBank::buildNode(Node &node) noexcept {
	std::unique_ptr< Pipeline > pipeline;
	try {
		pipeline = node.factory ? node.factory() : nullptr;
	} catch (...) {
		pipeline.reset();
	}
	if (!pipeline) {
		node.pipeline.reset();
		return false;
	}
	node.pipeline = std::move(pipeline);
	node.state.store(NodeState::Ready, std::memory_order_release);
	return true;
}

void PreparedPipelineBank::signalLifecycleWorker() noexcept {
	m_lifecycleGeneration.fetch_add(1, std::memory_order_release);
	m_lifecycleGeneration.notify_one();
}

void PreparedPipelineBank::lifecycleWorkerLoop() noexcept {
	std::uint64_t observedGeneration = m_lifecycleGeneration.load(std::memory_order_acquire);
	while (!m_stopRequested.load(std::memory_order_acquire)) {
		bool rebuilt = false;
		for (Node &node : m_nodes) {
			NodeState expected = NodeState::Retired;
			if (!node.state.compare_exchange_strong(expected, NodeState::Preparing, std::memory_order_acq_rel,
													std::memory_order_acquire)) {
				continue;
			}
			rebuilt = true;
			if (!buildNode(node)) {
				node.state.store(NodeState::Failed, std::memory_order_release);
			}
		}
		if (rebuilt) {
			continue;
		}

		m_lifecycleGeneration.wait(observedGeneration, std::memory_order_acquire);
		observedGeneration = m_lifecycleGeneration.load(std::memory_order_acquire);
	}
}

void Probation::start(std::uint64_t candidateRecipeToken, std::uint64_t lastKnownWorkingRecipeToken) noexcept {
	m_status                      = ProbationStatus::Running;
	m_failure                     = ProbationFailure::None;
	m_candidateRecipeToken        = candidateRecipeToken;
	m_lastKnownWorkingToken       = lastKnownWorkingRecipeToken;
	m_activeRecipeToken           = candidateRecipeToken;
	m_elapsedMilliseconds         = 0;
	m_processedSpeechMilliseconds = 0;
}

ProbationResult Probation::observe(const ProbationObservation &observation) noexcept {
	if (m_status != ProbationStatus::Running) {
		return result(ProbationAction::None);
	}

	if (observation.failure != ProbationFailure::None) {
		m_status            = ProbationStatus::RolledBack;
		m_failure           = observation.failure;
		m_activeRecipeToken = m_lastKnownWorkingToken;
		return result(ProbationAction::Rollback);
	}

	m_elapsedMilliseconds =
		saturatingAdd(m_elapsedMilliseconds, static_cast< std::uint64_t >(observation.elapsedMilliseconds));
	const std::uint32_t validSpeech =
		std::min(observation.processedSpeechMilliseconds, observation.elapsedMilliseconds);
	m_processedSpeechMilliseconds =
		saturatingAdd(m_processedSpeechMilliseconds, static_cast< std::uint64_t >(validSpeech));

	if (m_elapsedMilliseconds >= requiredElapsedMilliseconds
		&& m_processedSpeechMilliseconds >= requiredSpeechMilliseconds) {
		m_status                = ProbationStatus::Passed;
		m_failure               = ProbationFailure::None;
		m_lastKnownWorkingToken = m_candidateRecipeToken;
		m_activeRecipeToken     = m_candidateRecipeToken;
		return result(ProbationAction::MarkHealthy);
	}

	return result(ProbationAction::None);
}

void Probation::reset() noexcept {
	m_status                      = ProbationStatus::Idle;
	m_failure                     = ProbationFailure::None;
	m_candidateRecipeToken        = 0;
	m_lastKnownWorkingToken       = 0;
	m_activeRecipeToken           = 0;
	m_elapsedMilliseconds         = 0;
	m_processedSpeechMilliseconds = 0;
}

ProbationStatus Probation::status() const noexcept {
	return m_status;
}

ProbationFailure Probation::failure() const noexcept {
	return m_failure;
}

std::uint64_t Probation::activeRecipeToken() const noexcept {
	return m_activeRecipeToken;
}

std::uint64_t Probation::elapsedMilliseconds() const noexcept {
	return m_elapsedMilliseconds;
}

std::uint64_t Probation::processedSpeechMilliseconds() const noexcept {
	return m_processedSpeechMilliseconds;
}

ProbationResult Probation::result(ProbationAction action) const noexcept {
	ProbationResult result;
	result.status                      = m_status;
	result.action                      = action;
	result.failure                     = m_failure;
	result.activeRecipeToken           = m_activeRecipeToken;
	result.elapsedMilliseconds         = m_elapsedMilliseconds;
	result.processedSpeechMilliseconds = m_processedSpeechMilliseconds;
	return result;
}

} // namespace Mumble::InputEnhancement::AutoV1
