// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementSettings.h"

#include "EnumStringConversions.h"
#include "InputEnhancement.h"

#include <QByteArrayView>
#include <QCryptographicHash>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace Mumble::InputEnhancement {
namespace {
	DefaultPreference safeOriginalPreference() {
		DefaultPreference preference;
		preference.profile   = Profile::Original;
		preference.autoAdapt = false;
		return preference;
	}

	const DefaultPreference &safeOriginalPreferenceReference() {
		static const DefaultPreference preference = safeOriginalPreference();
		return preference;
	}

	bool failSafePendingWithoutLastKnownGood(DeviceProfileState &state) {
		if (!state.pendingValidation
			|| (state.lastKnownGood
				&& executionBindingMatchesPreference(state.preference, state.pendingRecipeBinding,
													 state.pendingAutoRecipeSetFingerprint)
				&& executionBindingMatchesPreference(*state.lastKnownGood, state.lastKnownGoodRecipeBinding,
													 state.lastKnownGoodAutoRecipeSetFingerprint))) {
			return false;
		}
		state.preference    = safeOriginalPreference();
		state.lastKnownGood = state.preference;
		state.lastKnownGoodRecipeBinding.reset();
		state.lastKnownGoodAutoRecipeSetFingerprint.reset();
		state.pendingRecipeBinding.reset();
		state.pendingAutoRecipeSetFingerprint.reset();
		state.rollbackUndoPreference.reset();
		state.rollbackUndoRecipeBinding.reset();
		state.rollbackUndoAutoRecipeSetFingerprint.reset();
		state.pendingValidation  = false;
		state.lastRollbackReason = QStringLiteral("missing_exact_recipe_binding");
		state.legacyOverride.reset();
		return true;
	}

	bool hasExactRollbackUndo(const DeviceProfileState &state) {
		if (state.pendingValidation || !state.rollbackUndoPreference || !state.lastKnownGood
			|| state.preference != *state.lastKnownGood
			|| !executionBindingMatchesPreference(*state.rollbackUndoPreference, state.rollbackUndoRecipeBinding,
												  state.rollbackUndoAutoRecipeSetFingerprint)) {
			return false;
		}
		return executionBindingMatchesPreference(*state.lastKnownGood, state.lastKnownGoodRecipeBinding,
												 state.lastKnownGoodAutoRecipeSetFingerprint);
	}

	void sanitizeRollbackUndo(DeviceProfileState &state) {
		if (!hasExactRollbackUndo(state)) {
			state.rollbackUndoPreference.reset();
			state.rollbackUndoRecipeBinding.reset();
			state.rollbackUndoAutoRecipeSetFingerprint.reset();
		}
	}

	int boundedControl(const int value) {
		if (value < 0 || value > 100) {
			throw std::out_of_range("Input enhancement control must be between 0 and 100");
		}
		return value;
	}

	nlohmann::json serializePreference(const DefaultPreference &preference) {
		return {
			{ "profile", enumToString(preference.profile) },
			{ "reduction", preference.reduction },
			{ "character", preference.character },
			{ "auto_adapt", preference.autoAdapt },
		};
	}

	DefaultPreference deserializePreference(const nlohmann::json &json) {
		DefaultPreference preference;
		preference.profile   = stringToEnum< Profile >(json.at("profile").get< std::string >());
		preference.reduction = boundedControl(json.at("reduction").get< int >());
		preference.character = boundedControl(json.at("character").get< int >());
		preference.autoAdapt = json.at("auto_adapt").get< bool >();
		if (preference.profile == Profile::Auto) {
			preference.autoAdapt = true;
		}
		return preference;
	}

	nlohmann::json serializeIdentity(const DeviceIdentity &identity) {
		return {
			{ "backend_id", identity.backendId.toStdString() },
			{ "physical_id", identity.physicalId.toStdString() },
			{ "display_name", identity.displayName.toStdString() },
			{ "follows_system_default", identity.followsSystemDefault },
			{ "stable", identity.stable },
		};
	}

	DeviceIdentity deserializeIdentity(const nlohmann::json &json) {
		DeviceIdentity identity;
		identity.backendId            = QString::fromStdString(json.at("backend_id").get< std::string >());
		identity.physicalId           = QString::fromStdString(json.at("physical_id").get< std::string >());
		identity.displayName          = QString::fromStdString(json.value("display_name", std::string()));
		identity.followsSystemDefault = json.value("follows_system_default", false);
		identity.stable               = json.value("stable", true);
		return identity;
	}

	nlohmann::json serializeLegacyOverride(const LegacyOverride &legacy) {
		return {
			{ "noise_cancel_mode", legacy.noiseCancelMode },
			{ "backend", legacy.backend },
			{ "model_id", legacy.modelId.toStdString() },
			{ "custom_model_path", legacy.customModelPath.toStdString() },
			{ "speex_noise_cancel_strength", legacy.speexNoiseCancelStrength },
		};
	}

	LegacyOverride deserializeLegacyOverride(const nlohmann::json &json) {
		LegacyOverride legacy;
		legacy.noiseCancelMode          = json.at("noise_cancel_mode").get< int >();
		legacy.backend                  = json.at("backend").get< int >();
		legacy.modelId                  = QString::fromStdString(json.at("model_id").get< std::string >());
		legacy.customModelPath          = QString::fromStdString(json.at("custom_model_path").get< std::string >());
		legacy.speexNoiseCancelStrength = json.at("speex_noise_cancel_strength").get< int >();
		if (!isValidLegacyOverride(legacy)) {
			throw std::out_of_range("Legacy input enhancement override is invalid");
		}
		return legacy;
	}

	const char *engineName(const Engine engine) {
		switch (engine) {
			case Engine::None:
				return "None";
			case Engine::Speex:
				return "Speex";
			case Engine::RNNoise:
				return "RNNoise";
			case Engine::DeepFilterNet:
				return "DeepFilterNet";
			case Engine::DTLN:
				return "DTLN";
		}
		throw std::out_of_range("Unsupported input enhancement engine");
	}

	Engine deserializeEngine(const std::string &value) {
		if (value == "None")
			return Engine::None;
		if (value == "Speex")
			return Engine::Speex;
		if (value == "RNNoise")
			return Engine::RNNoise;
		if (value == "DeepFilterNet")
			return Engine::DeepFilterNet;
		if (value == "DTLN")
			return Engine::DTLN;
		throw std::out_of_range("Unsupported input enhancement engine");
	}

	const char *cpuClassName(const CpuClass cpuClass) {
		switch (cpuClass) {
			case CpuClass::Low:
				return "Low";
			case CpuClass::Standard:
				return "Standard";
			case CpuClass::High:
				return "High";
		}
		throw std::out_of_range("Unsupported input enhancement CPU class");
	}

	CpuClass deserializeCpuClass(const std::string &value) {
		if (value == "Low")
			return CpuClass::Low;
		if (value == "Standard")
			return CpuClass::Standard;
		if (value == "High")
			return CpuClass::High;
		throw std::out_of_range("Unsupported input enhancement CPU class");
	}

	bool safeBindingIdentifier(const QString &value, const int maximumLength) {
		if (value.isEmpty() || value.size() > maximumLength) {
			return false;
		}
		return std::all_of(value.cbegin(), value.cend(), [](const QChar character) {
			return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
				   || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
				   || (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || character == QLatin1Char('.')
				   || character == QLatin1Char('_') || character == QLatin1Char(':') || character == QLatin1Char('-');
		});
	}

	QString deserializeAutoRecipeSetFingerprint(const nlohmann::json &json) {
		const QString fingerprint = QString::fromStdString(json.get< std::string >());
		if (!isValidAutoRecipeSetFingerprint(fingerprint)) {
			throw std::out_of_range("Input enhancement Auto recipe-set fingerprint is invalid");
		}
		return fingerprint;
	}

	nlohmann::json serializeRecipeBinding(const RecipeBinding &binding) {
		if (!isValidRecipeBinding(binding)) {
			throw std::out_of_range("Input enhancement recipe binding is invalid");
		}
		return {
			{ "catalog_revision", binding.catalogRevision.toStdString() },
			{ "recipe_id", binding.recipeId.toStdString() },
			{ "recipe_revision", binding.recipeRevision },
			{ "requested_profile", enumToString(binding.requestedProfile) },
			{ "effective_profile", enumToString(binding.effectiveProfile) },
			{ "engine", engineName(binding.engine) },
			{ "model_id", binding.modelId.toStdString() },
			{ "model_sha256", binding.modelSha256.toStdString() },
			{ "model_relative_path", binding.modelRelativePath.toStdString() },
			{ "execution_fingerprint", binding.executionFingerprint.toStdString() },
			{ "noise_reduction", binding.noiseReduction },
			{ "natural_crisp", binding.naturalCrisp },
			{ "latency_budget_samples", binding.latencyBudgetSamples },
			{ "minimum_cpu_class", cpuClassName(binding.minimumCpuClass) },
		};
	}

	RecipeBinding deserializeRecipeBinding(const nlohmann::json &json) {
		RecipeBinding binding;
		binding.catalogRevision      = QString::fromStdString(json.at("catalog_revision").get< std::string >());
		binding.recipeId             = QString::fromStdString(json.at("recipe_id").get< std::string >());
		binding.recipeRevision       = json.at("recipe_revision").get< std::uint32_t >();
		binding.requestedProfile     = stringToEnum< Profile >(json.at("requested_profile").get< std::string >());
		binding.effectiveProfile     = stringToEnum< Profile >(json.at("effective_profile").get< std::string >());
		binding.engine               = deserializeEngine(json.at("engine").get< std::string >());
		binding.modelId              = QString::fromStdString(json.at("model_id").get< std::string >());
		binding.modelSha256          = QString::fromStdString(json.at("model_sha256").get< std::string >());
		binding.modelRelativePath    = QString::fromStdString(json.at("model_relative_path").get< std::string >());
		binding.executionFingerprint = QString::fromStdString(json.at("execution_fingerprint").get< std::string >());
		binding.noiseReduction       = boundedControl(json.at("noise_reduction").get< int >());
		binding.naturalCrisp         = boundedControl(json.at("natural_crisp").get< int >());
		binding.latencyBudgetSamples = json.at("latency_budget_samples").get< unsigned int >();
		binding.minimumCpuClass      = deserializeCpuClass(json.at("minimum_cpu_class").get< std::string >());
		if (!isValidRecipeBinding(binding)) {
			throw std::out_of_range("Input enhancement recipe binding is invalid");
		}
		return binding;
	}

	nlohmann::json serializeDeviceState(const DeviceProfileState &state) {
		DeviceProfileState safeState = state;
		failSafePendingWithoutLastKnownGood(safeState);
		sanitizeRollbackUndo(safeState);
		nlohmann::json json{
			{ "key", stableDeviceKey(safeState.identity).toStdString() },
			{ "identity", serializeIdentity(safeState.identity) },
			{ "preference", serializePreference(safeState.preference) },
			{ "calibrated", safeState.calibrated },
			{ "last_used_epoch_ms", safeState.lastUsedEpochMs },
			{ "pending_validation", safeState.pendingValidation },
			{ "last_rollback_reason", safeState.lastRollbackReason.toStdString() },
		};
		if (safeState.lastKnownGood) {
			json["last_known_good"] = serializePreference(*safeState.lastKnownGood);
		}
		if (safeState.lastKnownGoodRecipeBinding) {
			json["last_known_good_recipe"] = serializeRecipeBinding(*safeState.lastKnownGoodRecipeBinding);
		}
		if (safeState.lastKnownGoodAutoRecipeSetFingerprint) {
			if (!isValidAutoRecipeSetFingerprint(*safeState.lastKnownGoodAutoRecipeSetFingerprint)) {
				throw std::out_of_range("Input enhancement last-known-good Auto fingerprint is invalid");
			}
			json["last_known_good_auto_recipe_set_fingerprint"] =
				safeState.lastKnownGoodAutoRecipeSetFingerprint->toStdString();
		}
		if (safeState.pendingRecipeBinding) {
			json["pending_recipe"] = serializeRecipeBinding(*safeState.pendingRecipeBinding);
		}
		if (safeState.pendingAutoRecipeSetFingerprint) {
			if (!isValidAutoRecipeSetFingerprint(*safeState.pendingAutoRecipeSetFingerprint)) {
				throw std::out_of_range("Input enhancement pending Auto fingerprint is invalid");
			}
			json["pending_auto_recipe_set_fingerprint"] = safeState.pendingAutoRecipeSetFingerprint->toStdString();
		}
		if (safeState.rollbackUndoPreference) {
			json["rollback_undo_preference"] = serializePreference(*safeState.rollbackUndoPreference);
			if (safeState.rollbackUndoRecipeBinding) {
				json["rollback_undo_recipe"] = serializeRecipeBinding(*safeState.rollbackUndoRecipeBinding);
			}
			if (safeState.rollbackUndoAutoRecipeSetFingerprint) {
				if (!isValidAutoRecipeSetFingerprint(*safeState.rollbackUndoAutoRecipeSetFingerprint)) {
					throw std::out_of_range("Input enhancement rollback-undo Auto fingerprint is invalid");
				}
				json["rollback_undo_auto_recipe_set_fingerprint"] =
					safeState.rollbackUndoAutoRecipeSetFingerprint->toStdString();
			}
		}
		if (safeState.legacyOverride) {
			json["legacy_override"] = serializeLegacyOverride(*safeState.legacyOverride);
		}
		return json;
	}

	DeviceProfileState deserializeDeviceState(const nlohmann::json &json) {
		DeviceProfileState state;
		state.identity = deserializeIdentity(json.at("identity"));
		if (QString::fromStdString(json.at("key").get< std::string >()) != stableDeviceKey(state.identity)) {
			throw std::invalid_argument("Input enhancement device key does not match its identity");
		}
		state.preference         = deserializePreference(json.at("preference"));
		state.calibrated         = json.value("calibrated", false);
		state.lastUsedEpochMs    = json.value("last_used_epoch_ms", qint64(0));
		state.pendingValidation  = json.value("pending_validation", false);
		state.lastRollbackReason = QString::fromStdString(json.value("last_rollback_reason", std::string()));
		if (json.contains("last_known_good")) {
			state.lastKnownGood = deserializePreference(json.at("last_known_good"));
		}
		if (json.contains("last_known_good_recipe")) {
			state.lastKnownGoodRecipeBinding = deserializeRecipeBinding(json.at("last_known_good_recipe"));
		}
		if (json.contains("last_known_good_auto_recipe_set_fingerprint")) {
			state.lastKnownGoodAutoRecipeSetFingerprint =
				deserializeAutoRecipeSetFingerprint(json.at("last_known_good_auto_recipe_set_fingerprint"));
		}
		if (json.contains("pending_recipe")) {
			state.pendingRecipeBinding = deserializeRecipeBinding(json.at("pending_recipe"));
		}
		if (json.contains("pending_auto_recipe_set_fingerprint")) {
			state.pendingAutoRecipeSetFingerprint =
				deserializeAutoRecipeSetFingerprint(json.at("pending_auto_recipe_set_fingerprint"));
		}
		if (json.contains("rollback_undo_preference")) {
			state.rollbackUndoPreference = deserializePreference(json.at("rollback_undo_preference"));
		}
		if (json.contains("rollback_undo_recipe")) {
			state.rollbackUndoRecipeBinding = deserializeRecipeBinding(json.at("rollback_undo_recipe"));
		}
		if (json.contains("rollback_undo_auto_recipe_set_fingerprint")) {
			state.rollbackUndoAutoRecipeSetFingerprint =
				deserializeAutoRecipeSetFingerprint(json.at("rollback_undo_auto_recipe_set_fingerprint"));
		}
		if (json.contains("legacy_override")) {
			state.legacyOverride = deserializeLegacyOverride(json.at("legacy_override"));
		}
		failSafePendingWithoutLastKnownGood(state);
		sanitizeRollbackUndo(state);
		return state;
	}
} // namespace

DefaultPreference::DefaultPreference() : profile(Profile::Original) {
}

bool DefaultPreference::operator==(const DefaultPreference &other) const {
	return profile == other.profile && reduction == other.reduction && character == other.character
		   && autoAdapt == other.autoAdapt;
}

bool runtimeAutoAdaptationEnabled(const DefaultPreference &preference) noexcept {
	return preference.profile == Profile::Auto;
}

bool DeviceIdentity::operator==(const DeviceIdentity &other) const {
	return backendId == other.backendId && physicalId == other.physicalId && displayName == other.displayName
		   && followsSystemDefault == other.followsSystemDefault && stable == other.stable;
}

bool LegacyOverride::operator==(const LegacyOverride &other) const {
	return noiseCancelMode == other.noiseCancelMode && backend == other.backend && modelId == other.modelId
		   && customModelPath == other.customModelPath && speexNoiseCancelStrength == other.speexNoiseCancelStrength;
}

bool legacyOverrideProcessingEnabled(const LegacyOverride &legacyOverride) noexcept {
	return legacyOverride.noiseCancelMode != 0;
}

RecipeBinding::RecipeBinding()
	: requestedProfile(Profile::Original), effectiveProfile(Profile::Original), engine(Engine::None),
	  minimumCpuClass(CpuClass::Low) {
}

bool RecipeBinding::operator==(const RecipeBinding &other) const {
	return catalogRevision == other.catalogRevision && recipeId == other.recipeId
		   && recipeRevision == other.recipeRevision && requestedProfile == other.requestedProfile
		   && effectiveProfile == other.effectiveProfile && engine == other.engine && modelId == other.modelId
		   && modelSha256 == other.modelSha256 && modelRelativePath == other.modelRelativePath
		   && executionFingerprint == other.executionFingerprint && noiseReduction == other.noiseReduction
		   && naturalCrisp == other.naturalCrisp && latencyBudgetSamples == other.latencyBudgetSamples
		   && minimumCpuClass == other.minimumCpuClass;
}

bool DeviceProfileState::operator==(const DeviceProfileState &other) const {
	return identity == other.identity && preference == other.preference && calibrated == other.calibrated
		   && lastUsedEpochMs == other.lastUsedEpochMs && lastKnownGood == other.lastKnownGood
		   && lastKnownGoodRecipeBinding == other.lastKnownGoodRecipeBinding
		   && lastKnownGoodAutoRecipeSetFingerprint == other.lastKnownGoodAutoRecipeSetFingerprint
		   && pendingRecipeBinding == other.pendingRecipeBinding
		   && pendingAutoRecipeSetFingerprint == other.pendingAutoRecipeSetFingerprint
		   && rollbackUndoPreference == other.rollbackUndoPreference
		   && rollbackUndoRecipeBinding == other.rollbackUndoRecipeBinding
		   && rollbackUndoAutoRecipeSetFingerprint == other.rollbackUndoAutoRecipeSetFingerprint
		   && pendingValidation == other.pendingValidation && lastRollbackReason == other.lastRollbackReason
		   && legacyOverride == other.legacyOverride;
}

bool Settings::operator==(const Settings &other) const {
	return schemaVersion == other.schemaVersion && defaultPreference == other.defaultPreference
		   && deviceProfiles == other.deviceProfiles && legacyOverride == other.legacyOverride;
}

bool Settings::operator!=(const Settings &other) const {
	return !(*this == other);
}

QString stableDeviceKey(const DeviceIdentity &identity) {
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(identity.backendId.toUtf8());
	hash.addData(QByteArrayView("\0", 1));
	hash.addData(identity.physicalId.toUtf8());
	return QString::fromLatin1(hash.result().toHex());
}

bool deviceIdentitiesMatch(const DeviceIdentity &first, const DeviceIdentity &second) noexcept {
	return !first.backendId.isEmpty() && !first.physicalId.isEmpty() && first.backendId == second.backendId
		   && first.physicalId == second.physicalId;
}

const DeviceProfileState *findDeviceProfile(const Settings &settings, const DeviceIdentity &identity) noexcept {
	if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()) {
		return nullptr;
	}

	for (const DeviceProfileState &candidate : settings.deviceProfiles) {
		if (deviceIdentitiesMatch(candidate.identity, identity)) {
			return &candidate;
		}
	}
	return nullptr;
}

const DefaultPreference &preferenceForDevice(const Settings &settings, const DeviceIdentity &identity) noexcept {
	if (const DeviceProfileState *profile = findDeviceProfile(settings, identity)) {
		if (profile->pendingValidation
			&& (!profile->lastKnownGood
				|| !executionBindingMatchesPreference(profile->preference, profile->pendingRecipeBinding,
													  profile->pendingAutoRecipeSetFingerprint)
				|| !executionBindingMatchesPreference(*profile->lastKnownGood, profile->lastKnownGoodRecipeBinding,
													  profile->lastKnownGoodAutoRecipeSetFingerprint))) {
			return safeOriginalPreferenceReference();
		}
		return profile->preference;
	}
	return settings.defaultPreference;
}

RecipeBinding recipeBindingForRecipe(const Recipe &recipe, const QString &catalogRevision, const QString &modelSha256,
									 const QString &modelRelativePath) {
	RecipeBinding binding;
	binding.catalogRevision      = catalogRevision;
	binding.recipeId             = recipe.id();
	binding.recipeRevision       = recipe.revision();
	binding.requestedProfile     = recipe.requestedProfile();
	binding.effectiveProfile     = recipe.effectiveProfile();
	binding.engine               = recipe.engine();
	binding.modelId              = recipe.modelId();
	binding.modelSha256          = modelSha256.toLower();
	binding.modelRelativePath    = modelRelativePath;
	binding.executionFingerprint = recipeExecutionFingerprint(recipe);
	binding.noiseReduction       = recipe.noiseReduction();
	binding.naturalCrisp         = recipe.naturalCrisp();
	binding.latencyBudgetSamples = recipe.latencyBudgetSamples();
	binding.minimumCpuClass      = recipe.minimumCpuClass();
	return binding;
}

bool isValidRecipeBinding(const RecipeBinding &binding) {
	if (!safeBindingIdentifier(binding.catalogRevision, 64) || !safeBindingIdentifier(binding.recipeId, 128)
		|| binding.recipeRevision == 0 || binding.noiseReduction < 0 || binding.noiseReduction > 100
		|| binding.naturalCrisp < 0 || binding.naturalCrisp > 100
		|| binding.latencyBudgetSamples > crispLatencyBudgetSamples || binding.requestedProfile == Profile::Auto
		|| binding.requestedProfile != binding.effectiveProfile) {
		return false;
	}
	if (binding.executionFingerprint.size() != 64
		|| binding.executionFingerprint != binding.executionFingerprint.toLower()) {
		return false;
	}
	for (const QChar character : binding.executionFingerprint) {
		if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
			  || (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
			return false;
		}
	}
	switch (binding.effectiveProfile) {
		case Profile::Original:
			if (binding.engine != Engine::None || binding.noiseReduction != 0 || binding.naturalCrisp != 0
				|| binding.latencyBudgetSamples != originalLatencyBudgetSamples
				|| binding.minimumCpuClass != CpuClass::Low) {
				return false;
			}
			break;
		case Profile::Light:
			if (binding.engine != Engine::Speex || binding.latencyBudgetSamples != lightLatencyBudgetSamples
				|| binding.minimumCpuClass != CpuClass::Low) {
				return false;
			}
			break;
		case Profile::Balanced:
			if (binding.engine != Engine::RNNoise || binding.latencyBudgetSamples != balancedLatencyBudgetSamples
				|| binding.minimumCpuClass != CpuClass::Standard) {
				return false;
			}
			break;
		case Profile::Quality:
			if ((binding.engine != Engine::DeepFilterNet && binding.engine != Engine::DTLN)
				|| binding.latencyBudgetSamples != qualityLatencyBudgetSamples
				|| binding.minimumCpuClass != CpuClass::High) {
				return false;
			}
			break;
		case Profile::Auto:
			return false;
		case Profile::VoiceFocus:
			if (binding.engine != Engine::DeepFilterNet
				|| binding.latencyBudgetSamples != voiceFocusLatencyBudgetSamples
				|| binding.minimumCpuClass != CpuClass::High) {
				return false;
			}
			break;
	}
	const bool neural =
		binding.engine == Engine::RNNoise || binding.engine == Engine::DeepFilterNet || binding.engine == Engine::DTLN;
	if (!neural) {
		return binding.modelId.isEmpty() && binding.modelSha256.isEmpty() && binding.modelRelativePath.isEmpty();
	}
	// Build-number-zero packages use the reserved catalog revision below, so a
	// development binding can never match a signed production catalog. Neural
	// development recipes are still bound to the exact manifest-attested model
	// hash and relative path; being unsigned must not make the local contract
	// weaker than the release contract.
	if (!safeBindingIdentifier(binding.modelId, 128) || binding.modelSha256.size() != 64
		|| binding.modelSha256 != binding.modelSha256.toLower() || binding.modelRelativePath.isEmpty()
		|| binding.modelRelativePath.size() > 240 || binding.modelRelativePath.startsWith(QLatin1Char('/'))
		|| binding.modelRelativePath.contains(QLatin1Char('\\')) || binding.modelRelativePath.contains(QLatin1Char(':'))
		|| binding.modelRelativePath.contains(QLatin1Char('*')) || binding.modelRelativePath.contains(QLatin1Char('?'))
		|| binding.modelRelativePath.contains(QLatin1Char('<')) || binding.modelRelativePath.contains(QLatin1Char('>'))
		|| binding.modelRelativePath.contains(QLatin1Char('|'))
		|| binding.modelRelativePath.contains(QLatin1Char('"'))) {
		return false;
	}
	for (const QChar character : binding.modelSha256) {
		if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
			  || (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
			return false;
		}
	}
	const QStringList segments = binding.modelRelativePath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
	return std::none_of(segments.cbegin(), segments.cend(), [](const QString &segment) {
		return segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..")
			   || segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' '));
	});
}

bool isValidAutoRecipeSetFingerprint(const QString &fingerprint) noexcept {
	if (fingerprint.size() != 64 || fingerprint != fingerprint.toLower()) {
		return false;
	}
	return std::all_of(fingerprint.cbegin(), fingerprint.cend(), [](const QChar character) {
		return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
			   || (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
	});
}

bool recipeBindingMatches(const RecipeBinding &binding, const Recipe &recipe, const QString &catalogRevision,
						  const QString &modelSha256, const QString &modelRelativePath) {
	return isValidRecipeBinding(binding)
		   && binding == recipeBindingForRecipe(recipe, catalogRevision, modelSha256, modelRelativePath);
}

bool recipeBindingMatchesPreference(const RecipeBinding &binding, const DefaultPreference &preference) {
	if (!isValidRecipeBinding(binding) || preference.profile == Profile::Auto
		|| binding.requestedProfile != preference.profile || binding.effectiveProfile != preference.profile) {
		return false;
	}
	const ValidatedControls controls =
		validatedControlsForProfile(preference.profile, preference.reduction, preference.character);
	return binding.noiseReduction == controls.noiseReduction && binding.naturalCrisp == controls.naturalCrisp;
}

bool executionBindingMatchesPreference(const DefaultPreference &preference,
									   const std::optional< RecipeBinding > &recipeBinding,
									   const std::optional< QString > &autoRecipeSetFingerprint) noexcept {
	if (preference.profile == Profile::Auto) {
		return !recipeBinding && autoRecipeSetFingerprint && isValidAutoRecipeSetFingerprint(*autoRecipeSetFingerprint);
	}
	if (autoRecipeSetFingerprint) {
		return false;
	}
	if (preference.profile == Profile::Original && !recipeBinding) {
		return true;
	}
	return recipeBinding && recipeBindingMatchesPreference(*recipeBinding, preference);
}

bool armManualProfileProbation(Settings &settings, const DeviceIdentity &identity,
								 const DefaultPreference &candidate, const RecipeBinding &candidateBinding,
								 const DefaultPreference &lastKnownGood,
								 std::optional< RecipeBinding > lastKnownGoodBinding, const qint64 nowEpochMs) {
	if (identity.backendId.isEmpty() || identity.physicalId.isEmpty() || candidate.profile == Profile::Original
		|| candidate.profile == Profile::Auto || !recipeBindingMatchesPreference(candidateBinding, candidate)
		|| lastKnownGood.profile == Profile::Auto
		|| !executionBindingMatchesPreference(lastKnownGood, lastKnownGoodBinding, std::nullopt)) {
		return false;
	}

	Settings updated = settings;
	DeviceProfileState *state = ensureDeviceProfile(updated, identity);
	if (!state) {
		return false;
	}
	DefaultPreference fixedCandidate = candidate;
	fixedCandidate.autoAdapt         = false;
	state->identity                   = identity;
	state->lastKnownGood              = lastKnownGood;
	state->lastKnownGoodRecipeBinding = std::move(lastKnownGoodBinding);
	state->lastKnownGoodAutoRecipeSetFingerprint.reset();
	state->preference                = fixedCandidate;
	state->pendingRecipeBinding      = candidateBinding;
	state->pendingAutoRecipeSetFingerprint.reset();
	state->calibrated        = false;
	state->pendingValidation = true;
	state->lastUsedEpochMs   = std::max(state->lastUsedEpochMs, nowEpochMs);
	state->lastRollbackReason.clear();
	state->legacyOverride.reset();
	state->rollbackUndoPreference.reset();
	state->rollbackUndoRecipeBinding.reset();
	state->rollbackUndoAutoRecipeSetFingerprint.reset();
	settings = std::move(updated);
	return true;
}

bool rollbackPendingValidationAfterAbnormalExit(Settings &settings) {
	bool changed = false;
	for (DeviceProfileState &state : settings.deviceProfiles) {
		if (!state.pendingValidation) {
			continue;
		}
		const DefaultPreference candidatePreference             = state.preference;
		const std::optional< RecipeBinding > candidateBinding   = state.pendingRecipeBinding;
		const std::optional< QString > candidateAutoFingerprint = state.pendingAutoRecipeSetFingerprint;
		const bool exactCandidate =
			executionBindingMatchesPreference(candidatePreference, candidateBinding, candidateAutoFingerprint);
		const bool exactLastKnownGood =
			state.lastKnownGood
			&& executionBindingMatchesPreference(*state.lastKnownGood, state.lastKnownGoodRecipeBinding,
												 state.lastKnownGoodAutoRecipeSetFingerprint);
		state.preference    = exactLastKnownGood ? *state.lastKnownGood : safeOriginalPreference();
		state.lastKnownGood = state.preference;
		if (!exactLastKnownGood || state.preference.profile == Profile::Original) {
			state.lastKnownGoodRecipeBinding.reset();
			state.lastKnownGoodAutoRecipeSetFingerprint.reset();
		}
		state.pendingRecipeBinding.reset();
		state.pendingAutoRecipeSetFingerprint.reset();
		if (exactCandidate) {
			state.rollbackUndoPreference               = candidatePreference;
			state.rollbackUndoRecipeBinding            = candidateBinding;
			state.rollbackUndoAutoRecipeSetFingerprint = candidateAutoFingerprint;
		} else {
			state.rollbackUndoPreference.reset();
			state.rollbackUndoRecipeBinding.reset();
			state.rollbackUndoAutoRecipeSetFingerprint.reset();
		}
		state.pendingValidation  = false;
		state.lastRollbackReason = QStringLiteral("crash_detected");
		state.legacyOverride.reset();
		changed = true;
	}
	return changed;
}

DeviceProfileState *ensureDeviceProfile(Settings &settings, const DeviceIdentity &identity) {
	if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()) {
		return nullptr;
	}
	for (DeviceProfileState &existing : settings.deviceProfiles) {
		if (deviceIdentitiesMatch(existing.identity, identity)) {
			return &existing;
		}
	}

	DeviceProfileState state;
	state.identity   = identity;
	state.preference = settings.defaultPreference;
	if (!upsertDeviceProfile(settings, std::move(state))) {
		return nullptr;
	}
	for (DeviceProfileState &candidate : settings.deviceProfiles) {
		if (deviceIdentitiesMatch(candidate.identity, identity)) {
			return &candidate;
		}
	}
	return nullptr;
}

bool upsertDeviceProfile(Settings &settings, DeviceProfileState state) {
	const QString key = stableDeviceKey(state.identity);
	for (int i = 0; i < settings.deviceProfiles.size(); ++i) {
		if (stableDeviceKey(settings.deviceProfiles.at(i).identity) == key) {
			settings.deviceProfiles[i] = std::move(state);
			return true;
		}
	}

	if (settings.deviceProfiles.size() < MAX_DEVICE_PROFILES) {
		settings.deviceProfiles.push_back(std::move(state));
		return true;
	}

	int evictionIndex = -1;
	qint64 oldestUse  = std::numeric_limits< qint64 >::max();
	for (int i = 0; i < settings.deviceProfiles.size(); ++i) {
		const DeviceProfileState &candidate = settings.deviceProfiles.at(i);
		if (!candidate.calibrated && candidate.lastUsedEpochMs < oldestUse) {
			evictionIndex = i;
			oldestUse     = candidate.lastUsedEpochMs;
		}
	}

	if (evictionIndex < 0) {
		return false;
	}

	settings.deviceProfiles[evictionIndex] = std::move(state);
	return true;
}

bool markDeviceProfileUsed(Settings &settings, const DeviceIdentity &identity, const qint64 nowEpochMs) noexcept {
	if (nowEpochMs <= 0) {
		return false;
	}
	for (DeviceProfileState &state : settings.deviceProfiles) {
		if (!deviceIdentitiesMatch(state.identity, identity) || state.lastUsedEpochMs >= nowEpochMs) {
			continue;
		}
		state.lastUsedEpochMs = nowEpochMs;
		return true;
	}
	return false;
}

Settings safeOriginalSettings() {
	Settings settings;
	settings.defaultPreference.profile   = Profile::Original;
	settings.defaultPreference.autoAdapt = false;
	return settings;
}

Profile profileForLegacy(const int noiseCancelMode, const int backend) {
	switch (static_cast< ::Settings::NoiseCancel >(noiseCancelMode)) {
		case ::Settings::NoiseCancelOff:
			return Profile::Original;
		case ::Settings::NoiseCancelSpeex:
			return Profile::Light;
		case ::Settings::NoiseCancelRNN:
			return backend == static_cast< int >(::Settings::RNNoiseBackend) ? Profile::Balanced : Profile::Quality;
		case ::Settings::NoiseCancelBoth:
			return Profile::Quality;
	}
	return Profile::Original;
}

int reductionForLegacySpeexStrength(const int strength) noexcept {
	const std::int64_t widened   = strength;
	const std::int64_t magnitude = widened < 0 ? -widened : widened;
	return static_cast< int >(std::clamp< std::int64_t >(magnitude, 0, 100));
}

bool isValidLegacyOverride(const LegacyOverride &legacy) noexcept {
	return legacy.noiseCancelMode >= static_cast< int >(::Settings::NoiseCancelOff)
		   && legacy.noiseCancelMode <= static_cast< int >(::Settings::NoiseCancelBoth)
		   && legacy.backend >= static_cast< int >(::Settings::RNNoiseBackend)
		   && legacy.backend <= static_cast< int >(::Settings::DeepFilterNetBackend)
		   && legacy.speexNoiseCancelStrength >= -100 && legacy.speexNoiseCancelStrength <= 0;
}

nlohmann::json serializeSettings(const Settings &settings) {
	nlohmann::json json{
		{ "schema_version", SETTINGS_SCHEMA_VERSION },
		{ "default", serializePreference(settings.defaultPreference) },
		{ "devices", nlohmann::json::array() },
	};
	for (const DeviceProfileState &device : settings.deviceProfiles) {
		if (device.identity.stable && !device.identity.backendId.isEmpty() && !device.identity.physicalId.isEmpty()) {
			json["devices"].push_back(serializeDeviceState(device));
		}
	}
	if (settings.legacyOverride) {
		json["legacy_override"] = serializeLegacyOverride(*settings.legacyOverride);
	}
	return json;
}

bool deserializeSettings(const nlohmann::json &json, Settings &settings) {
	try {
		if (!json.is_object()) {
			settings = safeOriginalSettings();
			return false;
		}
		const int sourceSchemaVersion = json.at("schema_version").get< int >();
		if (sourceSchemaVersion != 2 && sourceSchemaVersion != SETTINGS_SCHEMA_VERSION) {
			settings = safeOriginalSettings();
			return false;
		}

		Settings parsed;
		parsed.defaultPreference = deserializePreference(json.at("default"));
		if (!json.at("devices").is_array()) {
			throw std::invalid_argument("Input enhancement devices must be an array");
		}
		for (const nlohmann::json &deviceJson : json.at("devices")) {
			DeviceProfileState device = deserializeDeviceState(deviceJson);
			if (!device.identity.stable) {
				continue;
			}
			if (device.identity.backendId.isEmpty() || device.identity.physicalId.isEmpty()) {
				throw std::invalid_argument("Stable input enhancement device identity is incomplete");
			}
			if (!upsertDeviceProfile(parsed, std::move(device))) {
				throw std::length_error("Too many protected input enhancement device profiles");
			}
		}
		if (json.contains("legacy_override")) {
			parsed.legacyOverride = deserializeLegacyOverride(json.at("legacy_override"));
		}
		if (sourceSchemaVersion == 2) {
			// v2 exact bindings name the old Crisp/v1 recipe catalog. Preserve
			// the audible preference (Crisp parses as the numeric Quality value),
			// but discard stale attestations so the v2 catalog is preflighted and
			// rebound instead of failing silently at audio startup.
			for (DeviceProfileState &device : parsed.deviceProfiles) {
				if (device.pendingValidation) {
					device.preference         = device.lastKnownGood.value_or(safeOriginalPreference());
					device.lastRollbackReason = QStringLiteral("settings_v3_recipe_rebind");
				}
				device.lastKnownGood.reset();
				device.lastKnownGoodRecipeBinding.reset();
				device.lastKnownGoodAutoRecipeSetFingerprint.reset();
				device.pendingRecipeBinding.reset();
				device.pendingAutoRecipeSetFingerprint.reset();
				device.rollbackUndoPreference.reset();
				device.rollbackUndoRecipeBinding.reset();
				device.rollbackUndoAutoRecipeSetFingerprint.reset();
				device.pendingValidation = false;
			}
		}
		parsed.schemaVersion = SETTINGS_SCHEMA_VERSION;
		settings             = std::move(parsed);
		return true;
	} catch (const std::exception &) {
		settings = safeOriginalSettings();
		return false;
	} catch (...) {
		settings = safeOriginalSettings();
		return false;
	}
}

} // namespace Mumble::InputEnhancement
