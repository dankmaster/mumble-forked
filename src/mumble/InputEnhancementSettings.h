// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTSETTINGS_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTSETTINGS_H_

#include <QList>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace Mumble::InputEnhancement {

enum class Profile : std::uint8_t;
enum class CpuClass : std::uint8_t;
enum class Engine : std::uint8_t;
class Recipe;

constexpr int SETTINGS_SCHEMA_VERSION = 3;
constexpr int MAX_DEVICE_PROFILES     = 32;

struct DefaultPreference {
	DefaultPreference();

	Profile profile;
	int reduction  = 30;
	int character  = 50;
	bool autoAdapt = false;

	bool operator==(const DefaultPreference &other) const;
};

/// The schema retains the fixed-profile autoAdapt bit so a future qualified
/// implementation can restore the user's choice. The community core runtime
/// deliberately enables adaptation only for the explicit experimental Auto
/// profile; a stored bit on Original/Light/Balanced/Quality/VoiceFocus is
/// therefore dormant and must not change live audio or block calibration.
bool runtimeAutoAdaptationEnabled(const DefaultPreference &preference) noexcept;

struct DeviceIdentity {
	QString backendId;
	QString physicalId;
	QString displayName;
	bool followsSystemDefault = false;
	bool stable               = true;

	bool operator==(const DeviceIdentity &other) const;
};

/// The complete legacy input-cleanup tuple, captured before normalization or
/// backend fallback. It lets an upgraded client preserve the exact audible
/// behavior until the user explicitly selects a product profile.
struct LegacyOverride {
	int noiseCancelMode = 0;
	int backend         = 0;
	QString modelId;
	QString customModelPath;
	int speexNoiseCancelStrength = 0;

	bool operator==(const LegacyOverride &other) const;
};

/// Exact signed-catalog identity of a prepared product recipe. Absolute model
/// paths are deliberately excluded: modelRelativePath is re-resolved and
/// re-hashed by InputEnhancementPackageVerifier before every activation.
struct RecipeBinding {
	QString catalogRevision;
	QString recipeId;
	std::uint32_t recipeRevision = 0;
	Profile requestedProfile;
	Profile effectiveProfile;
	Engine engine;
	QString modelId;
	QString modelSha256;
	QString modelRelativePath;
	QString executionFingerprint;
	int noiseReduction                = 0;
	int naturalCrisp                  = 0;
	unsigned int latencyBudgetSamples = 0;
	CpuClass minimumCpuClass;

	RecipeBinding();
	bool operator==(const RecipeBinding &other) const;
};

struct DeviceProfileState {
	DeviceIdentity identity;
	DefaultPreference preference;
	bool calibrated        = false;
	qint64 lastUsedEpochMs = 0;
	std::optional< DefaultPreference > lastKnownGood;
	std::optional< RecipeBinding > lastKnownGoodRecipeBinding;
	/// Full AutoRecipeSetBinding fingerprint for an Auto last-known-good
	/// preference. Auto has three executable recipes, so reducing its identity
	/// to one RecipeBinding (or to a callback-sized token) is not sufficient.
	std::optional< QString > lastKnownGoodAutoRecipeSetFingerprint;
	std::optional< RecipeBinding > pendingRecipeBinding;
	std::optional< QString > pendingAutoRecipeSetFingerprint;
	/// One-shot candidate retained after a probation rollback so AudioInput can
	/// be recreated without losing the user's Undo affordance.
	std::optional< DefaultPreference > rollbackUndoPreference;
	std::optional< RecipeBinding > rollbackUndoRecipeBinding;
	std::optional< QString > rollbackUndoAutoRecipeSetFingerprint;
	bool pendingValidation = false;
	QString lastRollbackReason;
	std::optional< LegacyOverride > legacyOverride;

	bool operator==(const DeviceProfileState &other) const;
};

struct Settings {
	int schemaVersion = SETTINGS_SCHEMA_VERSION;
	DefaultPreference defaultPreference;
	QList< DeviceProfileState > deviceProfiles;
	std::optional< LegacyOverride > legacyOverride;

	bool operator==(const Settings &other) const;
	bool operator!=(const Settings &other) const;
};

QString stableDeviceKey(const DeviceIdentity &identity);

/// Returns whether two identities address the same physical/session device.
/// Display names and "follows default" are metadata and deliberately do not
/// participate in matching. An empty physical id never matches.
bool deviceIdentitiesMatch(const DeviceIdentity &first, const DeviceIdentity &second) noexcept;

/// Finds the preference for an opened device. Stable identities match their
/// persisted per-device entry; unstable identities may match an in-memory
/// session-only entry (which serializeSettings deliberately omits).
const DeviceProfileState *findDeviceProfile(const Settings &settings, const DeviceIdentity &identity) noexcept;

/// Returns the per-device preference when present, otherwise the global
/// default. The returned reference remains valid until settings is mutated.
const DefaultPreference &preferenceForDevice(const Settings &settings, const DeviceIdentity &identity) noexcept;

/// Rolls every in-flight per-device validation back after a detected abnormal
/// previous exit. Returns true when settings changed and therefore must be
/// durably saved before audio starts.
bool rollbackPendingValidationAfterAbnormalExit(Settings &settings);

/// Creates and compares exact signed-catalog bindings without persisting an
/// absolute path. These helpers are shared by calibration and AudioInput.
RecipeBinding recipeBindingForRecipe(const Recipe &recipe, const QString &catalogRevision,
									 const QString &modelSha256 = {}, const QString &modelRelativePath = {});
bool recipeBindingMatches(const RecipeBinding &binding, const Recipe &recipe, const QString &catalogRevision,
						  const QString &modelSha256 = {}, const QString &modelRelativePath = {});
bool recipeBindingMatchesPreference(const RecipeBinding &binding, const DefaultPreference &preference);
bool isValidRecipeBinding(const RecipeBinding &binding);
/// Auto persists the complete canonical recipe-set SHA-256. This helper is
/// intentionally independent of AutoV2.h so settings remain a lower-level
/// serialization contract without a circular include.
bool isValidAutoRecipeSetFingerprint(const QString &fingerprint) noexcept;
/// Verifies the exact persisted execution identity for any preference. Fixed
/// profiles use one RecipeBinding; Auto uses only the complete set fingerprint.
bool executionBindingMatchesPreference(const DefaultPreference &preference,
									   const std::optional< RecipeBinding > &recipeBinding,
									   const std::optional< QString > &autoRecipeSetFingerprint) noexcept;

/// Persists a directly selected fixed enhanced profile as an exact pending
/// candidate. AudioInput consumes these same fields through the existing
/// 60-second / 10-second-of-speech probation and rollback controller used by
/// calibration. Original and experimental Auto are intentionally rejected.
bool armManualProfileProbation(Settings &settings, const DeviceIdentity &identity,
								 const DefaultPreference &candidate, const RecipeBinding &candidateBinding,
								 const DefaultPreference &lastKnownGood,
								 std::optional< RecipeBinding > lastKnownGoodBinding, qint64 nowEpochMs);

/// Ensures that an editable entry exists for the physical/session device. A
/// new entry inherits the global default. Unstable identities are retained in
/// memory but deliberately omitted by serializeSettings().
DeviceProfileState *ensureDeviceProfile(Settings &settings, const DeviceIdentity &identity);

/// Inserts or replaces a device profile. When the 32-entry limit is reached,
/// only the least-recently-used uncalibrated entry may be evicted. Returns
/// false when all existing entries are calibrated and therefore protected.
bool upsertDeviceProfile(Settings &settings, DeviceProfileState state);

/// Advances the LRU timestamp of an existing device profile after the backend
/// has confirmed that the physical device really opened. This never creates a
/// profile and never moves the timestamp backwards.
bool markDeviceProfileUsed(Settings &settings, const DeviceIdentity &identity, qint64 nowEpochMs) noexcept;

Settings safeOriginalSettings();
Profile profileForLegacy(int noiseCancelMode, int backend);
int reductionForLegacySpeexStrength(int strength) noexcept;
bool isValidLegacyOverride(const LegacyOverride &legacy) noexcept;

nlohmann::json serializeSettings(const Settings &settings);
bool deserializeSettings(const nlohmann::json &json, Settings &settings);

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTSETTINGS_H_
