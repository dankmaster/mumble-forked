// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTAUTOV2_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTAUTOV2_H_

#include "InputEnhancement.h"
#include "InputEnhancementAuto.h"
#include "InputEnhancementSettings.h"

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Mumble::InputEnhancement::AutoV2 {

inline constexpr std::uint32_t recipeSetSchemaVersion            = 1;
inline constexpr std::size_t requiredCandidateCount              = 3;
inline constexpr std::uint32_t capabilityProbeSchemaVersion      = 1;
inline constexpr std::uint64_t capabilityProbeMaximumNanoseconds = 50'000'000;

/// Exact cache key for the bounded off-audio-thread capability probe. Every
/// member is a lowercase SHA-256: the running client executable, the local CPU
/// identity, and the verified model/recipe payload respectively.
struct CapabilityProbeKey final {
	QString buildSha256;
	QString cpuSha256;
	QString modelSetSha256;

	bool operator==(const CapabilityProbeKey &other) const;
};

struct CapabilityProbeResult final {
	CapabilityProbeKey key;
	QString bindingFingerprint;
	CpuClass cpuTier                 = CpuClass::Low;
	std::uint64_t elapsedNanoseconds = 0;
	bool valid                       = false;
};

/// Hashes the current executable and a privacy-preserving local CPU identity,
/// then binds them to the verified package payload fingerprint supplied by the
/// caller. This is a control-thread operation and must never run in an audio
/// callback.
CapabilityProbeKey currentCapabilityProbeKey(const QString &modelSetSha256);
QString capabilityProbeBindingFingerprint(const CapabilityProbeKey &key);
CpuClass classifyCapabilityProbeDuration(std::uint64_t elapsedNanoseconds) noexcept;
CapabilityProbeResult runCapabilityProbe(const CapabilityProbeKey &key);
/// Returns a cached result only for an exact key match. Build, CPU, or model
/// drift reruns the bounded probe before returning a tier.
CapabilityProbeResult cachedCapabilityProbe(const CapabilityProbeKey &key);

/// Exact identity of the three recipes Auto is allowed to select. The array is
/// canonical only in Light, Balanced, Quality order. Voice Focus is
/// intentionally absent because it is an explicit, user-selected profile.
struct AutoRecipeCandidateBinding final {
	Profile profile = Profile::Original;
	/// The executable input.auto.* recipe. requestedProfile is Auto and
	/// effectiveProfile must equal profile.
	RecipeBinding recipe;

	bool operator==(const AutoRecipeCandidateBinding &other) const;
};

struct AutoRecipeSetBinding final {
	QString catalogRevision;
	std::uint32_t policyVersion = 0;
	std::uint32_t mixVersion    = 0;
	CpuClass cpuTier            = CpuClass::Standard;
	std::array< AutoRecipeCandidateBinding, requiredCandidateCount > candidates;
	QString setFingerprint;

	bool operator==(const AutoRecipeSetBinding &other) const;
};

enum class RecipeSetValidationError : std::uint8_t {
	None,
	InvalidCatalogRevision,
	InvalidPolicyVersion,
	InvalidMixVersion,
	InvalidCpuTier,
	InvalidCandidateBinding,
	CandidateCatalogMismatch,
	ForbiddenCandidate,
	DuplicateCandidate,
	MissingCandidate,
	NonCanonicalOrder,
	FingerprintMismatch,
	CatalogDrift,
	PolicyDrift,
	MixDrift,
	CpuTierDrift
};

struct RecipeSetValidationResult final {
	RecipeSetValidationError error = RecipeSetValidationError::None;
	/// Every invalid or drifted set fails closed to Original.
	Profile safeProfile = Profile::Original;

	explicit operator bool() const noexcept { return error == RecipeSetValidationError::None; }
};

/// Sorts candidates into canonical order and computes the full SHA-256 set
/// fingerprint. This is an off-audio-thread operation and may allocate.
AutoRecipeSetBinding
	makeAutoRecipeSetBinding(QString catalogRevision, std::uint32_t policyVersion, std::uint32_t mixVersion,
							 CpuClass cpuTier,
							 std::array< AutoRecipeCandidateBinding, requiredCandidateCount > candidates);

/// Hashes all executable and package identity fields in canonical candidate
/// order. setFingerprint itself is deliberately excluded.
QString autoRecipeSetFingerprint(const AutoRecipeSetBinding &binding);

/// Validates both the binding's internal canonical form and its exact runtime
/// context. A missing member, Voice Focus member, hash mismatch, or runtime
/// drift returns Original through safeProfile.
RecipeSetValidationResult validateAutoRecipeSetBinding(const AutoRecipeSetBinding &binding,
													   const QString &expectedCatalogRevision,
													   std::uint32_t expectedPolicyVersion,
													   std::uint32_t expectedMixVersion, CpuClass expectedCpuTier);

enum class TransitionState : std::uint8_t { Idle, Priming, Fading, Rebase, Active, Abort };

enum class TransitionAbortReason : std::uint8_t {
	None,
	InvalidConfiguration,
	CandidateUnavailable,
	CandidateInvalidOutput,
	CandidateDeadlineMiss,
	SourceInvalidOutput,
	SilenceLost,
	TailDrainResumed,
	Count
};

inline constexpr std::size_t trackedProfileCount        = static_cast< std::size_t >(Profile::VoiceFocus) + 1U;
inline constexpr std::size_t transitionAbortReasonCount = static_cast< std::size_t >(TransitionAbortReason::Count);
inline constexpr std::size_t fallbackReasonCount = static_cast< std::size_t >(FallbackReason::ProcessorException) + 1U;
inline constexpr std::size_t callbackHistogramBucketCount = 7;

struct SessionDiagnosticsSnapshot final {
	std::uint64_t decisions                                                          = 0;
	std::array< std::uint64_t, trackedProfileCount > decisionsByProfile              = {};
	std::array< std::uint64_t, trackedProfileCount > profileResidencyFrames          = {};
	std::uint64_t transitionsStarted                                                 = 0;
	std::uint64_t transitionsCompleted                                               = 0;
	std::uint64_t transitionsAborted                                                 = 0;
	std::array< std::uint64_t, transitionAbortReasonCount > transitionAbortsByReason = {};
	std::uint64_t quarantineEvents                                                   = 0;
	std::array< std::uint64_t, trackedProfileCount > quarantineByProfile             = {};
	std::uint64_t fallbackEvents                                                     = 0;
	std::array< std::uint64_t, fallbackReasonCount > fallbackByReason                = {};
	std::uint64_t callbackFrames                                                     = 0;
	std::uint64_t callbackTotalNanoseconds                                           = 0;
	std::uint64_t callbackMaximumNanoseconds                                         = 0;
	std::array< std::uint64_t, callbackHistogramBucketCount > callbackHistogram      = {};
	Profile lastDecisionProfile                                                      = Profile::Original;
	Profile lastTransitionSource                                                     = Profile::Original;
	Profile lastTransitionCandidate                                                  = Profile::Original;
	TransitionState transitionState                                                  = TransitionState::Idle;
	TransitionAbortReason lastAbort                                                  = TransitionAbortReason::None;
};

/// Fixed-size, single-writer session diagnostics. Callback-side record methods
/// do not allocate or lock. snapshot() must be called after the capture writer
/// is quiescent (or by that same writer).
class SessionDiagnostics final {
public:
	void reset() noexcept;
	void recordDecision(Profile profile) noexcept;
	void recordCallbackFrame(Profile residentProfile, std::uint64_t durationNanoseconds) noexcept;
	void recordTransitionStarted(Profile source, Profile candidate) noexcept;
	void recordTransitionState(TransitionState state) noexcept;
	void recordTransitionCompleted() noexcept;
	void recordTransitionAbort(TransitionAbortReason reason) noexcept;
	void recordQuarantine(Profile profile) noexcept;
	void recordFallback(FallbackReason reason) noexcept;

	SessionDiagnosticsSnapshot snapshot() const noexcept;
	static std::uint64_t callbackBucketUpperBoundNanoseconds(std::size_t bucket) noexcept;

private:
	SessionDiagnosticsSnapshot m_snapshot;
};

struct TransitionFrameHealth final {
	/// Value from AutoV1::AcousticSilenceSwitchBoundary::silentFrames().
	std::uint16_t verifiedSilentFrames = 0;
	bool acousticSpeech                = true;
	bool tailDrainActive               = true;
	bool candidateAvailable            = true;
	bool candidateSucceeded            = true;
	bool candidateDeadlineMet          = true;
};

struct TransitionFrameResult final {
	TransitionState state             = TransitionState::Idle;
	TransitionAbortReason abortReason = TransitionAbortReason::None;
	Profile outputProfile             = Profile::Original;
	bool dualPipelineRequired         = false;
	bool completedThisFrame           = false;
	bool abortedThisFrame             = false;
};

/// Allocation-free Auto handoff coordinator. While dualPipelineRequired() is
/// true, the owner must run both prepared pipelines on the same capture frame
/// and pass their natural-latency outputs to processDualPipelineFrame(). The
/// coordinator aligns both outputs to the larger latency in preallocated delay
/// lines, performs a 40 ms equal-power crossfade, and drops the candidate's
/// temporary alignment delay only while the verified silence gate remains
/// satisfied.
class TransitionCoordinator final {
public:
	using Frame = std::array< float, frameSamples >;

	static constexpr std::uint32_t crossfadeMilliseconds = 40;
	static constexpr std::size_t crossfadeSamples =
		(static_cast< std::size_t >(crossfadeMilliseconds) * 48'000U) / 1'000U;
	static constexpr unsigned int maximumTransitionLatencySamples = qualityLatencyBudgetSamples;

	explicit TransitionCoordinator(SessionDiagnostics *diagnostics = nullptr) noexcept;

	void setDiagnostics(SessionDiagnostics *diagnostics) noexcept;
	void reset() noexcept;
	bool begin(Profile sourceProfile, unsigned int sourceLatencySamples, Profile candidateProfile,
			   unsigned int candidateLatencySamples) noexcept;
	TransitionFrameResult processDualPipelineFrame(const Frame &sourceFrame, const Frame &candidateFrame,
												   const TransitionFrameHealth &health, Frame &outputFrame) noexcept;

	TransitionState state() const noexcept;
	TransitionAbortReason abortReason() const noexcept;
	Profile sourceProfile() const noexcept;
	Profile candidateProfile() const noexcept;
	bool dualPipelineRequired() const noexcept;
	std::size_t fadedSamples() const noexcept;

private:
	class FixedDelayLine final {
	public:
		void reset(unsigned int delaySamples) noexcept;
		void process(const Frame &input, Frame &output) noexcept;
		bool primed() const noexcept;

	private:
		std::array< float, maximumTransitionLatencySamples > m_samples = {};
		unsigned int m_delaySamples                                    = 0;
		unsigned int m_position                                        = 0;
		unsigned int m_samplesWritten                                  = 0;
	};

	static bool isAutoCandidate(Profile profile) noexcept;
	static bool frameIsFinite(const Frame &frame) noexcept;
	static void copyFiniteOrSilence(const Frame &frame, Frame &output) noexcept;
	TransitionFrameResult result(bool completedThisFrame = false, bool abortedThisFrame = false) const noexcept;
	TransitionFrameResult abortTransition(TransitionAbortReason reason, const Frame &sourceFrame,
										  Frame &outputFrame) noexcept;
	void recordFallbackForAbort(TransitionAbortReason reason) noexcept;

	SessionDiagnostics *m_diagnostics = nullptr;
	FixedDelayLine m_sourceDelay;
	FixedDelayLine m_candidateDelay;
	Frame m_alignedSource               = {};
	Frame m_alignedCandidate            = {};
	Profile m_sourceProfile             = Profile::Original;
	Profile m_candidateProfile          = Profile::Original;
	TransitionState m_state             = TransitionState::Idle;
	TransitionAbortReason m_abortReason = TransitionAbortReason::None;
	std::size_t m_fadedSamples          = 0;
	float m_sourceGain                  = 1.0f;
	float m_candidateGain               = 0.0f;
	float m_rotationCos;
	float m_rotationSin;
};

static_assert(TransitionCoordinator::crossfadeSamples == 4U * frameSamples,
			  "Auto's qualified equal-power crossfade must span exactly four 10 ms frames");
static_assert(AutoV1::AcousticSilenceSwitchBoundary::minimumSilentFrames * frameSamples
				  >= TransitionCoordinator::crossfadeSamples,
			  "The verified silence gate must be longer than the Auto crossfade");

} // namespace Mumble::InputEnhancement::AutoV2

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTAUTOV2_H_
