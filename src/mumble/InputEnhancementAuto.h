// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTAUTO_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTAUTO_H_

#include "InputEnhancement.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace Mumble::InputEnhancement::AutoV1 {

/// Coarse, privacy-preserving signal observations. The policy deliberately
/// accepts no speaker, language, identity, demographic, device-name or raw
/// audio inputs.
enum class NoiseFloorBucket : std::uint8_t { VeryLow, Low, Medium, High, VeryHigh };
enum class SnrBucket : std::uint8_t { VeryPoor, Poor, Fair, Good, Excellent };
enum class VadConfidenceBucket : std::uint8_t { Silent, Low, Medium, High };
enum class DeadlinePressure : std::uint8_t { None, Elevated, High, Critical };

struct Observation final {
	NoiseFloorBucket noiseFloor = NoiseFloorBucket::Medium;
	SnrBucket snr               = SnrBucket::Fair;
	/// 0 is strongly transient and 100 is strongly stationary.
	std::uint8_t stationaryScore      = 50;
	VadConfidenceBucket vadConfidence = VadConfidenceBucket::Silent;
	CpuClass cpuClass                 = CpuClass::Standard;
	DeadlinePressure deadlinePressure = DeadlinePressure::None;
	int userNoiseReduction            = 50;
	int userNaturalCrisp              = 50;
	/// Runtime candidate readiness is established before the audio callback.
	/// The policy never asks the callback to construct or load a processor.
	bool lightAvailable    = true;
	bool balancedAvailable = true;
	bool crispAvailable    = true;
	/// Fixed profiles may use the bounded control adaptation without allowing
	/// Auto to change their selected engine.
	bool allowProfileSwitch = true;
};

struct Decision final {
	Profile activeProfile           = Profile::Balanced;
	Engine activeEngine             = Engine::RNNoise;
	Profile candidateProfile        = Profile::Balanced;
	int noiseReduction              = 50;
	int naturalCrisp                = 50;
	std::uint8_t stableObservations = 0;
	/// The policy has authorized a request after hysteresis. The active profile
	/// is not committed until the prepared-pipeline gate succeeds.
	bool profileSwitchApplied       = false;
	bool profileSwitchDeferred      = false;
	bool hysteresisPending          = false;
};

/// Stateful deterministic Auto v1 policy. evaluate() uses fixed-size scalar
/// state only and is safe to call without allocating or taking locks.
class Policy final {
public:
	static constexpr std::uint8_t profileHysteresisObservations = 3;
	static constexpr std::uint8_t controlHysteresisObservations = 2;
	/// Ten seconds at the 500 ms observation cadence. A pressure-driven
	/// downgrade must prove stable before Auto may increase processor cost
	/// again, otherwise a fresh processor would immediately erase the pressure
	/// history and flap between profiles.
	static constexpr std::uint8_t pressureDemotionCooldownObservations = 20;
	static constexpr int maximumAutomaticAdjustment                    = 20;

	explicit Policy(Profile initialProfile = Profile::Balanced) noexcept;

	Decision evaluate(const Observation &observation) noexcept;
	void reset(Profile activeProfile = Profile::Balanced) noexcept;
	/// Commit a switch only after the prepared audio pipeline has been published.
	/// Control hysteresis and pressure cooldown survive the transaction.
	void commitProfile(Profile activeProfile) noexcept;

	Profile activeProfile() const noexcept;
	Engine activeEngine() const noexcept;

private:
	static Profile normalizeConcreteProfile(Profile profile) noexcept;
	static Profile chooseProfile(const Observation &observation) noexcept;
	static Profile availableProfile(Profile desired, const Observation &observation) noexcept;
	static Engine engineForProfile(Profile profile) noexcept;
	static int profileCost(Profile profile) noexcept;
	static int clampControl(int value) noexcept;
	static int noiseAdjustment(const Observation &observation) noexcept;
	static int characterAdjustment(const Observation &observation) noexcept;

	Profile m_activeProfile                 = Profile::Balanced;
	Profile m_pendingProfile                = Profile::Balanced;
	std::uint8_t m_profileObservations      = 0;
	int m_activeNoiseAdjustment             = 0;
	int m_activeCharacterAdjustment         = 0;
	int m_pendingNoiseAdjustment            = 0;
	int m_pendingCharacterAdjustment        = 0;
	std::uint8_t m_controlObservations      = 0;
	std::uint8_t m_pressureDemotionCooldown = 0;
};

/// Reduces raw 48 kHz mono frames to fixed-size, coarse observations. No raw
/// samples are retained. The tracker contains scalar state only and neither
/// allocates nor locks in captureFrame()/produceObservation().
class ObservationTracker final {
public:
	static constexpr std::uint16_t framesPerObservation = 50; // 500 ms at 10 ms/frame

	void reset() noexcept;
	void captureFrame(const short *samples, unsigned int sampleCount) noexcept;
	bool produceObservation(float vadConfidence, CpuClass cpuClass, DeadlinePressure deadlinePressure,
							int userNoiseReduction, int userNaturalCrisp, Observation &observation) noexcept;

private:
	static NoiseFloorBucket noiseFloorBucket(double power) noexcept;
	static SnrBucket snrBucket(double signalPower, double noisePower) noexcept;
	static VadConfidenceBucket vadBucket(float confidence) noexcept;

	double m_currentPower      = 0.0;
	double m_smoothedPower     = 0.0;
	double m_noisePower        = 1.0e-7;
	double m_intervalPower     = 0.0;
	double m_intervalTransient = 0.0;
	std::uint16_t m_frameCount = 0;
	bool m_hasPowerEstimate    = false;
	bool m_frameCaptured       = false;
};

enum class SwitchGateAction : std::uint8_t { None, ApplyPrepared, RejectUnavailable };

struct SwitchGateResult final {
	SwitchGateAction action = SwitchGateAction::None;
	Profile profile         = Profile::Balanced;
};

/// Cross-engine gate for processors prepared outside the capture callback.
/// Every transition still requires a confirmed idle boundary. Candidate
/// lifecycle and replenishment are handled by PreparedPipelineBank below.
class SafeProfileSwitchGate final {
public:
	explicit SafeProfileSwitchGate(Profile activeProfile = Profile::Balanced) noexcept;

	void reset(Profile activeProfile = Profile::Balanced) noexcept;
	void reserve(const Decision &decision) noexcept;
	SwitchGateResult poll(bool idleBoundary, bool candidatePrepared) noexcept;
	bool switchingAllowed() const noexcept;
	bool pending() const noexcept;
	Profile activeProfile() const noexcept;
	Profile pendingProfile() const noexcept;

private:
	Profile m_activeProfile  = Profile::Balanced;
	Profile m_pendingProfile = Profile::Balanced;
	bool m_pending           = false;
};

/// Establishes a quiescent acoustic handoff boundary independently of the
/// transport mode. Continuous transmission and a held PTT may keep sending
/// packets while the microphone has in fact been silent long enough for both
/// the old and new product pipelines to contain no speech tail.
class AcousticSilenceSwitchBoundary final {
public:
	/// 300 ms is longer than the sum of two maximum qualified Quality latencies
	/// (2 * 50 ms) and provides margin for VAD/room-noise jitter.
	static constexpr std::uint16_t minimumSilentFrames = 30;

	void reset() noexcept;
	bool observe(bool acousticSpeech, bool drainActive) noexcept;
	std::uint16_t silentFrames() const noexcept;

private:
	std::uint16_t m_silentFrames = 0;
};

static_assert(static_cast< unsigned int >(AcousticSilenceSwitchBoundary::minimumSilentFrames) * frameSamples
				  >= 2U * crispLatencyBudgetSamples,
			  "Auto handoff silence must cover both maximum qualified pipeline latencies");

/// Owns the neural Auto candidates and continuously rebuilds a processor after
/// it has been retired at an idle boundary. The capture callback only performs
/// atomic state changes and raw-pointer publication; construction, reset and
/// destruction happen on the owned lifecycle worker.
class PreparedPipelineBank final {
public:
	using PipelineFactory = std::function< std::unique_ptr< Pipeline >() >;

	PreparedPipelineBank() = default;
	~PreparedPipelineBank();

	PreparedPipelineBank(const PreparedPipelineBank &)            = delete;
	PreparedPipelineBank(PreparedPipelineBank &&)                 = delete;
	PreparedPipelineBank &operator=(const PreparedPipelineBank &) = delete;
	PreparedPipelineBank &operator=(PreparedPipelineBank &&)      = delete;

	/// Builds every supplied candidate and publishes initialProfile. This is an
	/// off-callback lifecycle operation. Light needs no factory.
	bool initialize(Profile initialProfile, PipelineFactory balancedFactory, PipelineFactory crispFactory);
	/// Stops the lifecycle worker and destroys all processors. The capture
	/// callback must already be quiescent.
	void stop() noexcept;

	/// Callback-safe readiness check. Light is always locally available.
	bool candidatePrepared(Profile profile) const noexcept;
	/// Callback-safe idle-boundary handoff. The previous neural pipeline is
	/// retired for off-callback reconstruction before it can be selected again.
	bool switchTo(Profile profile) noexcept;

	Pipeline *activePipeline() noexcept;
	const Pipeline *activePipeline() const noexcept;
	Profile activeProfile() const noexcept;
	bool candidateFailed(Profile profile) const noexcept;

private:
	enum class NodeState : std::uint8_t { Unavailable, Preparing, Ready, Active, Retired, Failed };
	static_assert(std::atomic< NodeState >::is_always_lock_free,
				  "Auto candidate state must remain lock-free on the capture callback");

	struct Node final {
		Profile profile = Profile::Original;
		PipelineFactory factory;
		std::unique_ptr< Pipeline > pipeline;
		std::atomic< NodeState > state{ NodeState::Unavailable };
	};

	Node *nodeFor(Profile profile) noexcept;
	const Node *nodeFor(Profile profile) const noexcept;
	bool buildNode(Node &node) noexcept;
	void signalLifecycleWorker() noexcept;
	void lifecycleWorkerLoop() noexcept;

	std::array< Node, 2 > m_nodes;
	std::atomic< Pipeline * > m_activePipeline{ nullptr };
	std::atomic< Profile > m_activeProfile{ Profile::Original };
	std::atomic_bool m_stopRequested{ true };
	std::atomic< std::uint64_t > m_lifecycleGeneration{ 0 };
	std::thread m_lifecycleWorker;
};

static_assert(std::atomic< Pipeline * >::is_always_lock_free,
			  "Auto pipeline publication must remain lock-free on the capture callback");
static_assert(std::atomic< Profile >::is_always_lock_free,
			  "Auto profile publication must remain lock-free on the capture callback");

enum class ProbationStatus : std::uint8_t { Idle, Running, Passed, RolledBack };
enum class ProbationFailure : std::uint8_t { None, InitializationFailure, InvalidOutput, DeadlineMiss, CrashDetected };
enum class ProbationAction : std::uint8_t { None, MarkHealthy, Rollback };

struct ProbationObservation final {
	std::uint32_t elapsedMilliseconds         = 0;
	std::uint32_t processedSpeechMilliseconds = 0;
	ProbationFailure failure                  = ProbationFailure::None;
};

struct ProbationResult final {
	ProbationStatus status                    = ProbationStatus::Idle;
	ProbationAction action                    = ProbationAction::None;
	ProbationFailure failure                  = ProbationFailure::None;
	std::uint64_t activeRecipeToken           = 0;
	std::uint64_t elapsedMilliseconds         = 0;
	std::uint64_t processedSpeechMilliseconds = 0;
};

/// Probation for a newly applied recipe. Recipe tokens are opaque stable IDs
/// supplied by the settings/recipe layer; this class never parses or allocates
/// recipe metadata.
class Probation final {
public:
	static constexpr std::uint64_t requiredElapsedMilliseconds = 60'000;
	static constexpr std::uint64_t requiredSpeechMilliseconds  = 10'000;

	void start(std::uint64_t candidateRecipeToken, std::uint64_t lastKnownWorkingRecipeToken) noexcept;
	ProbationResult observe(const ProbationObservation &observation) noexcept;
	void reset() noexcept;

	ProbationStatus status() const noexcept;
	ProbationFailure failure() const noexcept;
	std::uint64_t activeRecipeToken() const noexcept;
	std::uint64_t elapsedMilliseconds() const noexcept;
	std::uint64_t processedSpeechMilliseconds() const noexcept;

private:
	ProbationResult result(ProbationAction action) const noexcept;

	ProbationStatus m_status                    = ProbationStatus::Idle;
	ProbationFailure m_failure                  = ProbationFailure::None;
	std::uint64_t m_candidateRecipeToken        = 0;
	std::uint64_t m_lastKnownWorkingToken       = 0;
	std::uint64_t m_activeRecipeToken           = 0;
	std::uint64_t m_elapsedMilliseconds         = 0;
	std::uint64_t m_processedSpeechMilliseconds = 0;
};

} // namespace Mumble::InputEnhancement::AutoV1

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTAUTO_H_
