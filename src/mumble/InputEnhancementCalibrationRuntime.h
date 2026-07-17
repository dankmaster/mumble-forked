// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONRUNTIME_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONRUNTIME_H_

#include "InputEnhancementAuto.h"
#include "InputEnhancementCalibration.h"
#include "InputEnhancementSettings.h"

#include <QtCore/QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace Mumble::InputEnhancement {

class CalibrationCandidateEvaluator {
public:
	struct Output final {
		CalibrationSession::CandidateResult candidate;
		std::vector< float > playbackPcm;
		std::optional< RecipeBinding > recipeBinding;

		~Output();
	};

	virtual ~CalibrationCandidateEvaluator()                                              = default;
	virtual bool evaluate(const CalibrationSession::CaptureView &capture,
						  const CalibrationSession::Selection &selection, Output &output) = 0;
};

struct CalibrationOpusConfiguration final {
	int bitrate              = 40'000;
	unsigned framesPerPacket = 2;
	bool allowLowDelay       = false;
};

/// Immutable authorization copied from the verified package catalog before a
/// calibration worker starts. The evaluator never retains the global verifier
/// or consults mutable package state while processing captured audio.
struct CalibrationPackageAuthorization final {
	enum class Mode : std::uint8_t { DenyNeural, CatalogBound, ExplicitUnmanagedBuildZero };

	struct AuthorizedRecipe final {
		QString recipeId;
		std::uint32_t revision   = 0;
		Profile requestedProfile = Profile::Original;
		Profile effectiveProfile = Profile::Original;
		Engine engine            = Engine::None;
		QString modelId;
		int noiseReduction                = 0;
		int naturalCrisp                  = 0;
		float mixFactor                   = 0.0f;
		unsigned int latencyBudgetSamples = 0;
		CpuClass minimumCpuClass          = CpuClass::Low;
		QString sha256Hex;
		QString canonicalModelPath;
		QString relativeModelPath;
	};

	Mode mode = Mode::DenyNeural;
	QString catalogRevision;
	std::vector< AuthorizedRecipe > recipes;

	static CalibrationPackageAuthorization signedPackage(std::vector< AuthorizedRecipe > recipes);
	static CalibrationPackageAuthorization signedPackage(QString catalogRevision,
												 std::vector< AuthorizedRecipe > recipes);
	static CalibrationPackageAuthorization catalogBoundPackage(QString catalogRevision,
													std::vector< AuthorizedRecipe > recipes);
	/// Manifest-free build-0 fallback. Only Original/Light are authorized; a
	/// parsed unsigned catalog must instead provide exact recipe/model bindings.
	static CalibrationPackageAuthorization explicitUnmanagedBuildZero();
	static AuthorizedRecipe authorizeRecipe(const Recipe &recipe, QString sha256Hex, QString canonicalModelPath = {},
											QString relativeModelPath = {});
	bool recipeAuthorized(const Recipe &recipe, QString &authorizedSha256Hex, QString &authorizedModelPath,
						  QString *authorizedRelativeModelPath = nullptr) const;
};

/// Local evaluator used by the product controller. Neural candidates run
/// through InputEnhancement::Pipeline and all eligible candidates then pass
/// through an in-process libopus encode/decode round trip. It performs no I/O.
class LocalCalibrationCandidateEvaluator final : public CalibrationCandidateEvaluator {
public:
	explicit LocalCalibrationCandidateEvaluator(CalibrationOpusConfiguration opus             = {},
												CalibrationPackageAuthorization authorization = {},
												CpuClass cpuClass = CpuClass::High,
												CaptureDeviceContext captureDevice = {}) noexcept;
	bool evaluate(const CalibrationSession::CaptureView &capture, const CalibrationSession::Selection &selection,
				  Output &output) override;

private:
	CalibrationOpusConfiguration m_opus;
	const CalibrationPackageAuthorization m_authorization;
	const CpuClass m_cpuClass;
	const CaptureDeviceContext m_captureDevice;
};

struct CalibrationEvaluationObserver final {
	const std::atomic_bool *cancelRequested                                            = nullptr;
	void (*progress)(void *context, std::size_t completed, std::size_t total) noexcept = nullptr;
	void *context                                                                      = nullptr;
};

/// Independent final-encode gate. begin() is published before a runtime bridge
/// can become visible, closing the start interleaving where a callback had
/// already observed a null bridge pointer.
class CalibrationTransmissionBlock final {
public:
	/// Closes the gate and waits for a packet path that linearized before the
	/// close to finish. A path racing the close either observes m_blocked on its
	/// second check or is included in this wait; no packet can straddle a
	/// successful begin().
	void begin() noexcept {
		m_blocked.store(true, std::memory_order_release);
		while (m_packetPathActive.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
	}
	void endAfterCallbackQuiescence() noexcept {
		while (m_packetPathActive.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		m_blocked.store(false, std::memory_order_release);
	}
	bool blocked() const noexcept { return m_blocked.load(std::memory_order_acquire); }
	bool tryEnterPacketPath() noexcept {
		if (blocked()) {
			return false;
		}
		bool expected = false;
		if (!m_packetPathActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
														std::memory_order_relaxed)) {
			return false;
		}
		if (blocked()) {
			leavePacketPath();
			return false;
		}
		return true;
	}
	bool packetPathMayContinue() const noexcept {
		return m_packetPathActive.load(std::memory_order_acquire) && !blocked();
	}
	void leavePacketPath() noexcept { m_packetPathActive.store(false, std::memory_order_release); }

private:
	std::atomic_bool m_blocked{ false };
	std::atomic_bool m_packetPathActive{ false };
};

/// Thread bridge for one calibration session. Control methods pause ingestion
/// with atomics before touching the session. appendPcmFromCallback() performs
/// no heap allocation and takes no lock after start().
class CalibrationRuntimeBridge final {
public:
	explicit CalibrationRuntimeBridge(std::unique_ptr< CalibrationCandidateEvaluator > evaluator = {},
									CpuClass readinessCpuClass = CpuClass::High);
	CalibrationRuntimeBridge(std::span< float > preallocatedStorage,
								 std::unique_ptr< CalibrationCandidateEvaluator > evaluator = {},
								 CpuClass readinessCpuClass = CpuClass::High);
	~CalibrationRuntimeBridge();

	CalibrationRuntimeBridge(const CalibrationRuntimeBridge &)            = delete;
	CalibrationRuntimeBridge(CalibrationRuntimeBridge &&)                 = delete;
	CalibrationRuntimeBridge &operator=(const CalibrationRuntimeBridge &) = delete;
	CalibrationRuntimeBridge &operator=(CalibrationRuntimeBridge &&)      = delete;

	bool start(const DeviceIdentity &identity, const DefaultPreference &previousPreference,
			   bool captureOptionalLocalNoise, std::uint64_t blindSeed,
			   std::optional< RecipeBinding > previousRecipeBinding = std::nullopt);
	std::size_t appendPcmFromCallback(const short *samples, unsigned int sampleCount) noexcept;

	bool advance() noexcept;
	bool skipOptionalLocalNoise() noexcept;
	bool evaluateCandidates(std::span< const CalibrationSession::Selection > candidates);
	bool evaluateCandidates(std::span< const CalibrationSession::Selection > candidates,
							const CalibrationEvaluationObserver &observer);
	CalibrationSession::BlindPair blindPair() noexcept;
	std::span< const float > playbackForToken(std::uint64_t playbackToken) noexcept;
	bool selectBlindWinner(std::uint64_t playbackToken) noexcept;
	bool apply(Settings &settings, qint64 nowEpochMs);
	bool cancel() noexcept;
	bool abort() noexcept;

	CalibrationSession::State state() const noexcept;
	CalibrationSession::LevelMetrics levelMetrics() noexcept;
	bool transmissionBlocked() const noexcept;
	bool rawAudioCleared() noexcept;
	bool playbackBuffersCleared() const noexcept;
	const DefaultPreference *draftPreference() noexcept;
	const RecipeBinding *draftRecipeBinding() noexcept;

	static CalibrationSession::Selection selectionForPreference(const DefaultPreference &preference) noexcept;
	static DefaultPreference preferenceForSelection(const CalibrationSession::Selection &selection) noexcept;
	/// Exact recipes offered by the first product calibration. Original is
	/// always present; Auto is excluded because it is a live policy, not a clip.
	static std::array< CalibrationSession::Selection, 4 >
		standardCandidateSet(const DefaultPreference &controls) noexcept;

private:
	struct EvaluatedCandidate final {
		CalibrationSession::Selection selection;
		std::vector< float > playbackPcm;
		std::optional< RecipeBinding > recipeBinding;
	};

	static bool captureState(CalibrationSession::State state) noexcept;
	void pauseCallback() noexcept;
	void clearCallbackFrame() noexcept;
	void publishState() noexcept;
	void clearPlayback() noexcept;
	CalibrationCandidateEvaluator &evaluator() noexcept;

	CalibrationSession m_session;
	std::unique_ptr< CalibrationCandidateEvaluator > m_evaluator;
	CpuClass m_readinessCpuClass = CpuClass::High;
	DeviceIdentity m_identity;
	DefaultPreference m_previousPreference;
	std::optional< RecipeBinding > m_previousRecipeBinding;
	DefaultPreference m_draftPreference;
	std::optional< RecipeBinding > m_draftRecipeBinding;
	bool m_hasDraftPreference                                                           = false;
	std::array< float, frameSamples > m_callbackFrame                                   = {};
	std::array< EvaluatedCandidate, CalibrationSession::maximumCandidates > m_evaluated = {};
	std::size_t m_evaluatedCount                                                        = 0;
	std::atomic< CalibrationSession::State > m_publishedState{ CalibrationSession::State::Idle };
	std::atomic_bool m_captureEnabled{ false };
	std::atomic_bool m_transmissionBlocked{ false };
	std::atomic_flag m_callbackActive = ATOMIC_FLAG_INIT;
};

enum class ProbationHealthSignal : std::uint8_t {
	Healthy,
	InitializationFailure,
	InvalidOutput,
	DeadlineMiss,
	CrashDetected
};

enum class ProbationSettingsResult : std::uint8_t { None, MarkedHealthy, RolledBack };

/// Connects callback health to the persisted per-device probation fields. The
/// callback method is scalar/noexcept; Settings and QString work is deferred to
/// serviceSettings() on the control thread.
class InputEnhancementProbationController final {
public:
	bool start(const DeviceIdentity &identity, const DefaultPreference &candidate,
			   const DefaultPreference &lastKnownWorking, const RecipeBinding &candidateRecipeBinding,
			   std::optional< RecipeBinding > lastKnownWorkingRecipeBinding = std::nullopt);
	/// Starts probation for Auto using the complete canonical recipe-set
	/// fingerprint. The fingerprint remains intact through healthy, rollback,
	/// restart, and Undo paths; it is never replaced by the callback token.
	bool startAuto(const DeviceIdentity &identity, const DefaultPreference &candidate,
				   const DefaultPreference &lastKnownWorking, const QString &candidateAutoRecipeSetFingerprint,
				   std::optional< RecipeBinding > lastKnownWorkingRecipeBinding      = std::nullopt,
				   std::optional< QString > lastKnownWorkingAutoRecipeSetFingerprint = std::nullopt);
	/// Restores the one-shot Undo affordance from an exact, persisted rollback
	/// state after Audio::restartInput() has recreated AudioInput.
	bool restoreUndo(const DeviceProfileState &state);
	AutoV1::ProbationAction observeFrame(std::uint32_t elapsedMilliseconds, bool speech,
										 ProbationHealthSignal health = ProbationHealthSignal::Healthy) noexcept;
	ProbationSettingsResult serviceSettings(Settings &settings);
	bool undoRollback(Settings &settings);

	bool running() const noexcept;
	bool undoAvailable() const noexcept;
	AutoV1::ProbationFailure failure() const noexcept;

private:
	bool startWithExecutionBinding(const DeviceIdentity &identity, const DefaultPreference &candidate,
								   const DefaultPreference &lastKnownWorking,
								   std::optional< RecipeBinding > candidateRecipeBinding,
								   std::optional< QString > candidateAutoRecipeSetFingerprint,
								   std::optional< RecipeBinding > lastKnownWorkingRecipeBinding,
								   std::optional< QString > lastKnownWorkingAutoRecipeSetFingerprint);
	static std::uint64_t bindingToken(const DefaultPreference &preference,
									  const std::optional< RecipeBinding > &binding,
									  const std::optional< QString > &autoRecipeSetFingerprint) noexcept;
	static AutoV1::ProbationFailure probationFailure(ProbationHealthSignal health) noexcept;
	static QString failureText(AutoV1::ProbationFailure failure);
	bool updateDeviceSettings(Settings &settings, bool rollback);

	DeviceIdentity m_identity;
	DefaultPreference m_candidate;
	DefaultPreference m_lastKnownWorking;
	DefaultPreference m_undoPreference;
	std::optional< RecipeBinding > m_candidateRecipeBinding;
	std::optional< RecipeBinding > m_lastKnownWorkingRecipeBinding;
	std::optional< RecipeBinding > m_undoRecipeBinding;
	std::optional< QString > m_candidateAutoRecipeSetFingerprint;
	std::optional< QString > m_lastKnownWorkingAutoRecipeSetFingerprint;
	std::optional< QString > m_undoAutoRecipeSetFingerprint;
	AutoV1::Probation m_probation;
	std::atomic_bool m_running{ false };
	std::atomic_bool m_undoAvailable{ false };
	std::atomic< AutoV1::ProbationAction > m_pendingAction{ AutoV1::ProbationAction::None };
	std::atomic< AutoV1::ProbationFailure > m_failure{ AutoV1::ProbationFailure::None };
};

static_assert(std::atomic< CalibrationSession::State >::is_always_lock_free);
static_assert(std::atomic< AutoV1::ProbationAction >::is_always_lock_free);
static_assert(std::atomic_bool::is_always_lock_free);

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONRUNTIME_H_
