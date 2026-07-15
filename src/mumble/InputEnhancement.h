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

class SpeechCleanupProcessor;

namespace Mumble::InputEnhancement {

inline constexpr unsigned int frameSamples                 = 480;
inline constexpr unsigned int originalLatencyBudgetSamples = 0;
inline constexpr unsigned int lightLatencyBudgetSamples    = 480;
inline constexpr unsigned int balancedLatencyBudgetSamples = 1440;
inline constexpr unsigned int crispLatencyBudgetSamples    = 2400;

// These revisions are part of the signed recipe contract and of the
// persisted execution fingerprint. Any change to recipe execution, the
// qualified mix curve, or callback-safe adaptation policy must increment the
// corresponding value.
inline constexpr std::uint32_t recipeExecutionSemanticsVersion = 1;
inline constexpr std::uint32_t qualifiedMixCurveVersion        = 1;
inline constexpr std::uint32_t adaptationPolicyVersion         = 1;

enum class Profile : std::uint8_t { Original, Light, Balanced, Crisp, Auto };
enum class CpuClass : std::uint8_t { Low, Standard, High };
enum class Engine : std::uint8_t { None, Speex, RNNoise, DeepFilterNet, DTLN };

struct ValidatedControls final {
	int noiseReduction = 0;
	int naturalCrisp   = 0;
};

/// Maps the public 0-100 controls into the concrete profile's qualified
/// interval. Original always maps to zero. This scalar helper is also used by
/// live Auto and is safe on the audio callback.
ValidatedControls validatedControlsForProfile(Profile profile, int noiseReduction, int naturalCrisp) noexcept;

/// Maps the public 0-100 controls to the qualified dry/wet mix for the active
/// concrete profile. Engine/model changes still require off-callback setup.
float mixFactorForControls(Profile profile, int noiseReduction, int naturalCrisp) noexcept;

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

struct ResolveRequest final {
	Profile profile                         = Profile::Original;
	int noiseReduction                      = 50;
	int naturalCrisp                        = 50;
	CpuClass cpuClass                       = CpuClass::Standard;
	BackendAvailability backendAvailability = {};
};

/// A catalog-produced, immutable description of one validated input recipe.
/// The recipe identifies both the user request and the concrete profile that
/// will run after deterministic capability fallback.
class Recipe final {
public:
	static constexpr std::uint32_t schemaVersion = 1;

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
	/// Full valid neural processFrame path, from input sanitization through
	/// processor health and output validation. Failed processor attempts are
	/// included; Original and Light never start this timer.
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
	static constexpr std::uint64_t processingHistogramBucketNanoseconds = 10'000;
	static constexpr std::size_t processingHistogramBucketCount         = 2'001;

	ProcessorFactory m_processorFactory;
	NanosecondClock m_clock;
	const std::uint64_t m_frameDeadlineNanoseconds;
	std::unique_ptr< const Recipe > m_recipe;
	std::unique_ptr< SpeechCleanupProcessor > m_processor;
	std::array< float, frameSamples > m_alignedDryFrame              = {};
	std::array< float, crispLatencyBudgetSamples > m_alignedDryDelay = {};
	unsigned int m_alignedDryDelayPosition                           = 0;
	QString m_activeModelId;
	QString m_activeModelSha256;
	unsigned int m_actualLatencySamples                                               = 0;
	FallbackReason m_fallbackReason                                                   = FallbackReason::None;
	std::uint64_t m_processedFrames                                                   = 0;
	std::uint64_t m_neuralFrames                                                      = 0;
	std::uint64_t m_sanitizedInputSamples                                             = 0;
	std::uint64_t m_clampedInputSamples                                               = 0;
	std::uint64_t m_clampedOutputSamples                                              = 0;
	std::uint64_t m_deadlineMisses                                                    = 0;
	std::uint64_t m_fallbackCount                                                     = 0;
	std::uint64_t m_totalProcessingNanoseconds                                        = 0;
	std::uint64_t m_maximumProcessingNanoseconds                                      = 0;
	std::uint64_t m_lastProcessingNanoseconds                                         = 0;
	std::array< std::uint64_t, processingHistogramBucketCount > m_processingHistogram = {};
	bool m_fallbackActive                                                             = false;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENT_H_
