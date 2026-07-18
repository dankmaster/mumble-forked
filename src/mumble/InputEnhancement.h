// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENT_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENT_H_

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class SpeechCleanupProcessor;

namespace Mumble::InputEnhancement {

inline constexpr unsigned int sampleRateHz                   = 48'000;
inline constexpr unsigned int frameSamples                   = sampleRateHz / 100;
inline constexpr unsigned int originalLatencyBudgetSamples   = 0;
inline constexpr unsigned int lightLatencyBudgetSamples      = 480;
inline constexpr unsigned int balancedLatencyBudgetSamples   = 1440;
inline constexpr unsigned int qualityLatencyBudgetSamples    = 2400;
inline constexpr unsigned int voiceFocusLatencyBudgetSamples = 2400;
// Source-compatibility alias for locally developed v2 integrations. New code
// and every persisted/public representation use Quality.
inline constexpr unsigned int crispLatencyBudgetSamples = qualityLatencyBudgetSamples;

// These revisions are part of the signed recipe contract and of the
// persisted execution fingerprint. Any change to recipe execution, the
// qualified mix curve, or callback-safe adaptation policy must increment the
// corresponding value.
inline constexpr std::uint32_t recipeExecutionSemanticsVersion = 8;
inline constexpr std::uint32_t qualifiedMixCurveVersion        = 6;
inline constexpr std::uint32_t adaptationPolicyVersion         = 1;

inline QString productRecipeCatalogRevision() {
	return QStringLiteral("input-recipes-v4");
}

enum class Profile : std::uint8_t {
	Original = 0,
	Light    = 1,
	Balanced = 2,
	Quality  = 3,
	// Read-only/source compatibility alias. Serializers never emit "Crisp".
	Crisp      = Quality,
	Auto       = 4,
	VoiceFocus = 5
};
enum class CpuClass : std::uint8_t { Low, Standard, High };
enum class Engine : std::uint8_t { None, Speex, RNNoise, DeepFilterNet, DTLN };

/// Result of the real, off-audio-thread manual-profile pipeline probe. Auto's
/// policy probe is intentionally not part of this contract: a synthetic Auto
/// workload must never make an explicitly selected Quality profile disappear.
struct ManualProfileCapabilityMetrics final {
	bool rnnoiseAvailable       = false;
	bool deepFilterNetAvailable = false;
	bool qualityPipelineReady   = false;
	std::uint64_t callbackP99Nanoseconds = 0;
	std::uint64_t callbackMaximumNanoseconds = 0;
	std::uint64_t workerFrames = 0;
	std::uint64_t workerP99Nanoseconds = 0;
	std::uint64_t workerMaximumNanoseconds = 0;
};

CpuClass manualProfileCpuClassForMetrics(const ManualProfileCapabilityMetrics &metrics) noexcept;

/// The deterministic two-client harness may request a tier only when both of
/// its existing authentication gates are active. Production callers pass false
/// for these flags, so an ambient environment variable cannot raise a tier.
CpuClass cpuClassWithAuthenticatedE2EOverride(CpuClass measured, bool harnessEnabled, bool tokenPresent,
											  const QString &requestedTier) noexcept;

struct ValidatedControls final {
	int noiseReduction = 0;
	int naturalCrisp   = 0;
};

/// Public 0-100 controls applied only when the user explicitly activates a
/// fixed enhanced profile. Existing and migrated preferences are never
/// rewritten merely by loading settings or opening the dialog.
struct ExplicitProfileControlPreset final {
	int noiseReduction = 0;
	int naturalCrisp   = 0;

	bool operator==(const ExplicitProfileControlPreset &) const = default;
};

/// Returns the exact public controls represented by the qualified recipe
/// candidate. Original retains the user's dormant controls and experimental
/// Auto restores its complete pre-Auto preference instead of using a preset.
std::optional< ExplicitProfileControlPreset > qualifiedExplicitSelectionPreset(Profile profile) noexcept;

/// Maps the public 0-100 controls into the concrete profile's qualified
/// interval. Original always maps to zero. This scalar helper is also used by
/// live Auto and is safe on the audio callback.
ValidatedControls validatedControlsForProfile(Profile profile, int noiseReduction, int naturalCrisp) noexcept;

/// Maps the public 0-100 controls to the qualified dry/wet mix for the active
/// concrete profile. Engine/model changes still require off-callback setup.
float mixFactorForControls(Profile profile, int noiseReduction, int naturalCrisp) noexcept;

/// Maps Light's public reduction control to the bounded Speex suppression
/// used by the product recipe. Legacy Speex settings deliberately bypass this
/// helper and retain their historical direct dB value.
int lightSpeexSuppressionDb(int noiseReduction) noexcept;

enum class FallbackReason : std::uint8_t {
	None,
	ProcessorUnavailable,
	ProcessorNotReady,
	ProcessorFallback,
	UnexpectedModel,
	LatencyBudgetExceeded,
	InvalidFrame,
	InvalidOutput,
	DeadlineExceeded,
	ProcessorException
};

struct BackendAvailability final {
	bool rnnoise       = false;
	bool deepFilterNet = false;
	bool dtln          = false;

	static BackendAvailability compiled();
};

/// Identifies where a recipe will run without coupling the product recipe to a
/// concrete AudioInput backend. Offline benchmarks and test harnesses retain
/// their default context: they may execute every recipe, but can never be
/// mistaken for production-qualified live capture evidence.
struct CaptureDeviceContext final {
	enum class Kind : std::uint8_t { OfflineHarness, LiveDevice };

	Kind kind = Kind::OfflineHarness;
	QString backendId;
	bool stablePhysicalIdentity = false;

	static CaptureDeviceContext liveDevice(QString backendId, bool stablePhysicalIdentity);
	bool productionQualified() const noexcept;
};

struct ResolveRequest final {
	Profile profile                         = Profile::Original;
	int noiseReduction                      = 50;
	int naturalCrisp                        = 50;
	CpuClass cpuClass                       = CpuClass::Standard;
	BackendAvailability backendAvailability = {};
	CaptureDeviceContext captureDevice      = {};
};

enum class ProfileReadinessReason : std::uint8_t {
	Ready,
	ExperimentalAuto,
	AutoRuntimeUnavailable,
	InsufficientCpu,
	BackendUnavailable,
	PackageUnavailable,
	RecipeUnauthorized,
	ModelUnavailable
};

/// Shared preflight result used before settings are offered or an input
/// processor is prepared. A selectable experimental profile carries a reason
/// so the UI can label it without treating it as unavailable.
struct ProfileReadiness final {
	bool selectable               = false;
	bool productionQualified      = false;
	ProfileReadinessReason reason = ProfileReadinessReason::BackendUnavailable;
};

/// Capability-only readiness. Signed package/model readiness is layered on by
/// InputEnhancementPackageVerifier. Voice Focus is explicit-only; Auto never
/// resolves to it.
ProfileReadiness profileReadiness(const ResolveRequest &request) noexcept;

/// A catalog-produced, immutable description of one validated input recipe.
/// The recipe identifies both the user request and the concrete profile that
/// will run after deterministic capability fallback.
class Recipe final {
public:
	static constexpr std::uint32_t schemaVersion = 2;

	Recipe(const Recipe &)            = default;
	Recipe(Recipe &&)                 = default;
	Recipe &operator=(const Recipe &) = delete;
	Recipe &operator=(Recipe &&)      = delete;

	const QString &id() const noexcept;
	std::uint32_t revision() const noexcept;
	Profile requestedProfile() const noexcept;
	Profile effectiveProfile() const noexcept;
	Engine engine() const noexcept;
	const QString &modelId() const noexcept;
	int noiseReduction() const noexcept;
	int naturalCrisp() const noexcept;
	float mixFactor() const noexcept;
	unsigned int latencyBudgetSamples() const noexcept;
	CpuClass minimumCpuClass() const noexcept;
	bool usesNeuralProcessor() const noexcept;

private:
	friend class RecipeCatalog;

	Recipe(QString id, std::uint32_t revision, Profile requestedProfile, Profile effectiveProfile, Engine engine,
		   QString modelId, int noiseReduction, int naturalCrisp, float mixFactor, unsigned int latencyBudgetSamples,
		   CpuClass minimumCpuClass);

	const QString m_id;
	const std::uint32_t m_revision;
	const Profile m_requestedProfile;
	const Profile m_effectiveProfile;
	const Engine m_engine;
	const QString m_modelId;
	const int m_noiseReduction;
	const int m_naturalCrisp;
	const float m_mixFactor;
	const unsigned int m_latencyBudgetSamples;
	const CpuClass m_minimumCpuClass;
};

/// Stable SHA-256 over every executable Recipe field, including the exact IEEE
/// 754 mix-factor bits and the versioned mix/adaptation semantics. This is
/// deliberately independent of model asset identity, which RecipeBinding
/// binds separately by signed relative path and content hash.
QString recipeExecutionFingerprint(const Recipe &recipe);

class RecipeCatalog final {
public:
	static constexpr std::uint32_t currentRevision = 1;

	/// Deterministically resolves the request. Public controls are clamped to
	/// [0, 100] and then mapped into the selected recipe's qualified interval.
	/// The result always names a runnable profile according to the supplied
	/// backend availability and CPU class.
	static Recipe resolve(const ResolveRequest &request);

private:
	static Recipe makeRecipe(Profile requestedProfile, Profile effectiveProfile, int noiseReduction, int naturalCrisp);
};

/// Immutable snapshot. Creating this snapshot may allocate QString storage and
/// must therefore not be done from the real-time audio callback.
class Diagnostics final {
public:
	static constexpr std::uint32_t schemaVersion = 4;

	const QString &requestedRecipeId() const noexcept;
	std::uint32_t recipeRevision() const noexcept;
	Profile requestedProfile() const noexcept;
	Profile activeProfile() const noexcept;
	Engine activeEngine() const noexcept;
	const QString &activeModelId() const noexcept;
	/// Lowercase SHA-256 from the verified package manifest. Empty means that
	/// no package-bound neural model is active (including unmanaged dev runs).
	const QString &activeModelSha256() const noexcept;
	unsigned int latencyBudgetSamples() const noexcept;
	unsigned int actualLatencySamples() const noexcept;
	std::uint64_t processedFrames() const noexcept;
	std::uint64_t neuralFrames() const noexcept;
	std::uint64_t sanitizedInputSamples() const noexcept;
	std::uint64_t clampedInputSamples() const noexcept;
	std::uint64_t clampedOutputSamples() const noexcept;
	std::uint64_t deadlineMisses() const noexcept;
	std::uint64_t fallbackCount() const noexcept;
	/// Full product callback path. Neural profiles time processFrame() from
	/// input sanitization through processor health and output validation; Light
	/// times the established external Speex preprocessor through
	/// recordClassicProcessingFrame(). Failed processor attempts are included;
	/// Original never starts this timer.
	std::uint64_t totalProcessingNanoseconds() const noexcept;
	std::uint64_t maximumProcessingNanoseconds() const noexcept;
	std::uint64_t processingP50Nanoseconds() const noexcept;
	std::uint64_t processingP95Nanoseconds() const noexcept;
	std::uint64_t processingP99Nanoseconds() const noexcept;
	std::uint64_t workerProcessingFrames() const noexcept;
	std::uint64_t workerTotalProcessingNanoseconds() const noexcept;
	std::uint64_t workerMaximumProcessingNanoseconds() const noexcept;
	std::uint64_t workerProcessingP99Nanoseconds() const noexcept;
	bool fallbackActive() const noexcept;
	FallbackReason fallbackReason() const noexcept;

private:
	friend class Pipeline;

	Diagnostics(QString requestedRecipeId, std::uint32_t recipeRevision, Profile requestedProfile,
				Profile activeProfile, Engine activeEngine, QString activeModelId, QString activeModelSha256,
				unsigned int latencyBudgetSamples, unsigned int actualLatencySamples, std::uint64_t processedFrames,
				std::uint64_t neuralFrames, std::uint64_t sanitizedInputSamples, std::uint64_t clampedInputSamples,
				std::uint64_t clampedOutputSamples, std::uint64_t deadlineMisses, std::uint64_t fallbackCount,
				std::uint64_t totalProcessingNanoseconds, std::uint64_t maximumProcessingNanoseconds,
				std::uint64_t processingP50Nanoseconds, std::uint64_t processingP95Nanoseconds,
				std::uint64_t processingP99Nanoseconds, std::uint64_t workerProcessingFrames,
				std::uint64_t workerTotalProcessingNanoseconds, std::uint64_t workerMaximumProcessingNanoseconds,
				std::uint64_t workerProcessingP99Nanoseconds, bool fallbackActive, FallbackReason fallbackReason);

	const QString m_requestedRecipeId;
	const std::uint32_t m_recipeRevision;
	const Profile m_requestedProfile;
	const Profile m_activeProfile;
	const Engine m_activeEngine;
	const QString m_activeModelId;
	const QString m_activeModelSha256;
	const unsigned int m_latencyBudgetSamples;
	const unsigned int m_actualLatencySamples;
	const std::uint64_t m_processedFrames;
	const std::uint64_t m_neuralFrames;
	const std::uint64_t m_sanitizedInputSamples;
	const std::uint64_t m_clampedInputSamples;
	const std::uint64_t m_clampedOutputSamples;
	const std::uint64_t m_deadlineMisses;
	const std::uint64_t m_fallbackCount;
	const std::uint64_t m_totalProcessingNanoseconds;
	const std::uint64_t m_maximumProcessingNanoseconds;
	const std::uint64_t m_processingP50Nanoseconds;
	const std::uint64_t m_processingP95Nanoseconds;
	const std::uint64_t m_processingP99Nanoseconds;
	const std::uint64_t m_workerProcessingFrames;
	const std::uint64_t m_workerTotalProcessingNanoseconds;
	const std::uint64_t m_workerMaximumProcessingNanoseconds;
	const std::uint64_t m_workerProcessingP99Nanoseconds;
	const bool m_fallbackActive;
	const FallbackReason m_fallbackReason;
};

class Pipeline final {
public:
	using ProcessorFactory = std::function< std::unique_ptr< SpeechCleanupProcessor >(const Recipe &) >;
	using NanosecondClock  = std::function< std::uint64_t() >;

	static constexpr std::uint64_t defaultFrameDeadlineNanoseconds = 10'000'000;
	static constexpr unsigned int processorWarmupFrames            = 3;
	static constexpr unsigned int processorPostResetProbeFrames    = 1;

	explicit Pipeline(ProcessorFactory processorFactory = {}, NanosecondClock clock = {},
					  std::uint64_t frameDeadlineNanoseconds = defaultFrameDeadlineNanoseconds);
	~Pipeline();

	Pipeline(const Pipeline &)            = delete;
	Pipeline(Pipeline &&)                 = delete;
	Pipeline &operator=(const Pipeline &) = delete;
	Pipeline &operator=(Pipeline &&)      = delete;

	/// Prepares a recipe and, for neural recipes only, creates and validates its
	/// processor. This function may load a model and must run off the audio
	/// callback before the owning AudioInput atomically installs this pipeline.
	/// When either authorization value is supplied, both are mandatory. The
	/// processor's final activeModelPath() is canonicalized and must identify
	/// that exact file, whose bytes are re-hashed after the final reset and
	/// real-time preparation. This binds package authorization to what the
	/// processor actually loaded before it can be published to AudioInput.
	bool configure(const Recipe &recipe, const QString &authorizedModelSha256 = {},
				   const QString &authorizedModelPath = {});
	/// Bounded off-callback pacing for benchmark/calibration drivers. This wait
	/// is deliberately separate from processFrame() and its callback timing.
	/// Real-time capture must never call this method.
	bool prepareOfflineFrame() noexcept;
	/// Bounded final drain for offline drivers. It observes errors from every
	/// submitted frame without emitting extra audio or changing the timeline.
	bool finishOfflineProcessing() noexcept;

	/// Processes exactly one preallocated 10 ms, 48 kHz mono frame. Returns true
	/// when neural processing was applied. Original and Light deliberately pass
	/// through bit-for-bit because their legacy processing lives outside this
	/// neural layer. After a runtime failure it returns false but continues to
	/// emit latency-aligned dry audio while alignedFallbackActive() is true. This
	/// method performs no intentional heap allocations.
	bool processFrame(float *samples, unsigned int sampleCount = frameSamples) noexcept;
	bool processFrame(std::array< float, frameSamples > &samples) noexcept;
	/// Applies the configured processor with a callback-safe, bounded mix.
	/// Engine/model changes still require configure() off the callback.
	bool processFrame(float *samples, unsigned int sampleCount, float mixFactor) noexcept;
	bool processFrame(std::array< float, frameSamples > &samples, float mixFactor) noexcept;
	/// Aligns the external Speex output with the recipe's one-frame dry delay and
	/// applies the speech-protected Light mix. The current dry frame and current
	/// Speex speech probability are captured after Speex has produced the delayed
	/// wet frame; both are retained in fixed storage for the following callback.
	bool mixClassicFrame(std::int16_t *processedSamples, const std::int16_t *currentDrySamples,
						 unsigned int sampleCount, int currentSpeechProbability,
						 std::uint64_t currentNoisePsdSum, std::uint64_t currentSignalPsdSum) noexcept;
	bool mixClassicFrame(std::int16_t *processedSamples, const std::int16_t *currentDrySamples,
						 unsigned int sampleCount, int currentSpeechProbability,
						 std::uint64_t currentNoisePsdSum, std::uint64_t currentSignalPsdSum,
						 float mixFactor) noexcept;
	/// Records one completed callback of the existing Speex/classic DSP path for
	/// a configured Light recipe. This only updates the common product
	/// diagnostics contract: it neither touches PCM nor creates a processor.
	void recordClassicProcessingFrame(std::uint64_t durationNanoseconds) noexcept;
	/// Latches an external Light processor failure into the same fail-closed
	/// state used by neural processors.
	void markClassicProcessingFailure(FallbackReason reason) noexcept;
	/// Replaces a just-produced Light frame with the already advanced aligned dry
	/// frame. This is used when full-path timing crosses the deadline after mix.
	bool restoreClassicAlignedDryFrame(std::int16_t *samples, unsigned int sampleCount) const noexcept;

	bool fallbackActive() const noexcept;
	FallbackReason fallbackReason() const noexcept;
	/// A runtime failure keeps the existing causal timeline alive as delayed dry
	/// audio. While this is true, callers must continue submitting frames and
	/// drain latencySamples() zeros before switching to zero-latency Original.
	bool alignedFallbackActive() const noexcept;
	unsigned int latencySamples() const noexcept;
	std::uint64_t lastProcessingNanoseconds() const noexcept;
	std::uint64_t lastWorkerProcessingNanoseconds() const noexcept;
	unsigned int workerPendingFrames() const noexcept;
	unsigned int workerSchedulingDelayFrames() const noexcept;
	unsigned int workerSchedulingSlackFrames() const noexcept;
	std::uint64_t frameDeadlineNanoseconds() const noexcept;
	Diagnostics diagnostics() const;

private:
	void failClosed(FallbackReason reason) noexcept;
	void recordProcessingDuration(std::uint64_t durationNanoseconds) noexcept;
	std::uint64_t processingPercentileNanoseconds(unsigned int percentile) const noexcept;
	void resetCounters() noexcept;
	void resetSpeechEdgeProtection() noexcept;
	float speechEdgeProtectedMixFactor(float requestedMixFactor) noexcept;
	void resetClassicMix() noexcept;
	static constexpr std::uint64_t processingHistogramBucketNanoseconds = 10'000;
	static constexpr std::size_t processingHistogramBucketCount         = 2'001;
	static constexpr unsigned int deepFilterOnsetRampFrames             = 8;

	ProcessorFactory m_processorFactory;
	NanosecondClock m_clock;
	const std::uint64_t m_frameDeadlineNanoseconds;
	std::unique_ptr< const Recipe > m_recipe;
	std::unique_ptr< SpeechCleanupProcessor > m_processor;
	std::array< float, frameSamples > m_alignedDryFrame              = {};
	std::array< float, crispLatencyBudgetSamples > m_alignedDryDelay = {};
	unsigned int m_alignedDryDelayPosition                           = 0;
	int m_classicPreviousSpeechProbability                           = 100;
	std::uint64_t m_classicPreviousNoisePsdSum                       = 0;
	std::uint64_t m_classicPreviousSignalPsdSum                      = 0;
	std::array< std::uint16_t, 121 > m_classicRmsHistogram          = {};
	std::array< std::uint16_t, 101 > m_classicSpeechProbabilityHistogram = {};
	std::uint32_t m_classicRmsHistogramCount                         = 0;
	std::uint32_t m_classicRmsHistogramFrames                        = 0;
	float m_classicSmoothedNoisePsdSum                               = 0.0f;
	float m_classicWetMix                                            = 0.0f;
	QString m_activeModelId;
	QString m_activeModelSha256;
	unsigned int m_actualLatencySamples                                               = 0;
	FallbackReason m_fallbackReason                                                   = FallbackReason::None;
	std::uint64_t m_processedFrames                                                   = 0;
	std::uint64_t m_neuralFrames                                                      = 0;
	std::uint64_t m_timedProcessingFrames                                             = 0;
	std::uint64_t m_sanitizedInputSamples                                             = 0;
	std::uint64_t m_clampedInputSamples                                               = 0;
	std::uint64_t m_clampedOutputSamples                                              = 0;
	std::uint64_t m_deadlineMisses                                                    = 0;
	std::uint64_t m_fallbackCount                                                     = 0;
	std::uint64_t m_totalProcessingNanoseconds                                        = 0;
	std::uint64_t m_maximumProcessingNanoseconds                                      = 0;
	std::uint64_t m_lastProcessingNanoseconds                                         = 0;
	std::array< std::uint64_t, processingHistogramBucketCount > m_processingHistogram = {};
	float m_speechEdgeNoiseFloorRms                                                  = 0.0f;
	float m_speechEdgePeakRms                                                        = 0.0f;
	float m_speechEdgePreviousRms                                                    = 0.0f;
	unsigned int m_speechEdgeBaselineFrames                                          = 0;
	unsigned int m_speechEdgeBelowReleaseFrames                                      = 0;
	unsigned int m_speechEdgeProtectionFrame                                         = deepFilterOnsetRampFrames;
	bool m_speechEdgeActive                                                          = false;
	bool m_fallbackActive                                                             = false;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENT_H_
