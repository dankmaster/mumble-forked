// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancement.h"
#include "InputEnhancementSettings.h"
#include "JSONSerialization.h"
#include "Settings.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <limits>
#include <tuple>

#include <nlohmann/json.hpp>

namespace {
using namespace Mumble::InputEnhancement;

constexpr auto testCatalogRevision = "input-recipes-v2";

DefaultPreference preference(Profile profile, int reduction = 30, int character = 50) {
	DefaultPreference result;
	result.profile   = profile;
	result.reduction = reduction;
	result.character = character;
	result.autoAdapt = profile == Profile::Auto;
	return result;
}

Recipe resolvedRecipe(Profile profile, int reduction = 30, int character = 50) {
	ResolveRequest request;
	request.profile                           = profile;
	request.noiseReduction                    = reduction;
	request.naturalCrisp                      = character;
	request.cpuClass                          = CpuClass::High;
	request.backendAvailability.rnnoise       = true;
	request.backendAvailability.deepFilterNet = true;
	request.backendAvailability.dtln          = true;
	return RecipeCatalog::resolve(request);
}

RecipeBinding exactBinding(Profile profile, int reduction = 30, int character = 50,
						   QChar hashCharacter = QLatin1Char('a')) {
	const Recipe recipe = resolvedRecipe(profile, reduction, character);
	if (!recipe.usesNeuralProcessor()) {
		return recipeBindingForRecipe(recipe, QString::fromLatin1(testCatalogRevision));
	}

	QString relativePath;
	switch (recipe.engine()) {
		case Engine::RNNoise:
			relativePath = QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin");
			break;
		case Engine::DeepFilterNet:
			relativePath = QStringLiteral("deepfilternet/DeepFilterNet3_ll_onnx.tar.gz");
			break;
		case Engine::DTLN:
			relativePath = QStringLiteral("dtln/model.onnx");
			break;
		case Engine::None:
		case Engine::Speex:
			Q_UNREACHABLE();
	}
	return recipeBindingForRecipe(recipe, QString::fromLatin1(testCatalogRevision), QString(64, hashCharacter),
								  relativePath);
}
} // namespace

class TestInputEnhancementSettings : public QObject {
	Q_OBJECT

private slots:
	void newInstallDefaultsToOriginal();
	void migratesLegacyTuple_data();
	void migratesLegacyTuple();
	void roundTripsSchemaV3();
	void autoRecipeSetFingerprintRoundTripsAndFailsClosed();
	void migratesSchemaV2CrispAlias();
	void corruptOrUnknownSchemaUsesSafeOriginal();
	void rejectsCorruptLegacyOverridesAndSafelyBoundsMigration();
	void deviceKeyExcludesDisplayName();
	void selectsProfileByOpenedPhysicalDevice();
	void editableDeviceProfileInheritsDefaultWithoutMutatingIt();
	void autoProfileAlwaysEnablesAdaptationOnLoad();
	void fixedProfileAutoAdaptationIsPersistedButRuntimeDormant();
	void evictsOnlyLeastRecentlyUsedUncalibratedProfile();
	void openedPhysicalDeviceAdvancesLruWithoutCreatingEntries();
	void sessionOnlyProfilesAreNotPersistedAndKeysAreVerified();
	void migratesNonEmptyLegacyQSettings();
	void pendingWithoutLastKnownGoodFailsSafeOnParseAndSelection();
	void rejectsMalformedRecipeBindings();
	void qualityExpertBindingsRequireQualifiedCpuAndLatency();
	void exactRecipeBindingDetectsCatalogRecipeAndModelDrift();
	void unmanagedBuildZeroNeuralBindingRemainsHashBound();
	void abnormalExitRollsBackPendingValidation();
	void abnormalExitWithoutExactLastKnownGoodUsesOriginal();
	void abnormalExitRollbackIsDurablyPersisted();
	void normalExitKeepsPendingValidationForResume();
};

void TestInputEnhancementSettings::newInstallDefaultsToOriginal() {
	::Settings settings;
	QCOMPARE(settings.inputEnhancement.schemaVersion, Mumble::InputEnhancement::SETTINGS_SCHEMA_VERSION);
	QCOMPARE(settings.inputEnhancement.defaultPreference.profile, Mumble::InputEnhancement::Profile::Original);
	QCOMPARE(settings.inputEnhancement.defaultPreference.reduction, 30);
	QCOMPARE(settings.inputEnhancement.defaultPreference.character, 50);
	QVERIFY(!settings.inputEnhancement.defaultPreference.autoAdapt);
	QVERIFY(!settings.inputEnhancement.legacyOverride.has_value());
}

void TestInputEnhancementSettings::fixedProfileAutoAdaptationIsPersistedButRuntimeDormant() {
	using namespace Mumble::InputEnhancement;
	for (const Profile profile : { Profile::Original, Profile::Light, Profile::Balanced, Profile::Quality,
								   Profile::VoiceFocus }) {
		DefaultPreference stored = preference(profile);
		stored.autoAdapt         = true;
		QVERIFY(!runtimeAutoAdaptationEnabled(stored));
	}

	DefaultPreference experimentalAuto = preference(Profile::Auto);
	QVERIFY(runtimeAutoAdaptationEnabled(experimentalAuto));

	Mumble::InputEnhancement::Settings settings;
	settings.defaultPreference.profile   = Profile::Balanced;
	settings.defaultPreference.autoAdapt = true;
	Mumble::InputEnhancement::Settings restored;
	QVERIFY(deserializeSettings(serializeSettings(settings), restored));
	QVERIFY(restored.defaultPreference.autoAdapt);
	QVERIFY(!runtimeAutoAdaptationEnabled(restored.defaultPreference));
}

void TestInputEnhancementSettings::migratesLegacyTuple_data() {
	QTest::addColumn< QString >("mode");
	QTest::addColumn< QString >("backend");
	QTest::addColumn< int >("expectedProfile");

	QTest::newRow("off") << QStringLiteral("Off") << QStringLiteral("RNNoise")
						 << static_cast< int >(Mumble::InputEnhancement::Profile::Original);
	QTest::newRow("speex") << QStringLiteral("Speex") << QStringLiteral("RNNoise")
						   << static_cast< int >(Mumble::InputEnhancement::Profile::Light);
	QTest::newRow("rnnoise") << QStringLiteral("RNN") << QStringLiteral("RNNoise")
							 << static_cast< int >(Mumble::InputEnhancement::Profile::Balanced);
	QTest::newRow("dtln") << QStringLiteral("RNN") << QStringLiteral("DTLN")
						  << static_cast< int >(Mumble::InputEnhancement::Profile::Quality);
	QTest::newRow("deepfilter") << QStringLiteral("RNN") << QStringLiteral("DeepFilterNet")
								<< static_cast< int >(Mumble::InputEnhancement::Profile::Quality);
	QTest::newRow("combined") << QStringLiteral("Speex&RNN") << QStringLiteral("RNNoise")
							  << static_cast< int >(Mumble::InputEnhancement::Profile::Quality);
}

void TestInputEnhancementSettings::migratesLegacyTuple() {
	QFETCH(QString, mode);
	QFETCH(QString, backend);
	QFETCH(int, expectedProfile);

	nlohmann::json legacy;
	legacy["settings_version"]                        = 1;
	legacy["audio"]["noise_cancel_mode"]              = mode.toStdString();
	legacy["audio"]["noise_cancel_backend"]           = backend.toStdString();
	legacy["audio"]["noise_cancel_model_id"]          = "legacy:model-id";
	legacy["audio"]["noise_cancel_custom_model_path"] = "C:/models/exact.bin";
	legacy["audio"]["speex_noise_cancel_strength"]    = -73;

	::Settings restored = legacy.get< ::Settings >();
	QCOMPARE(static_cast< int >(restored.inputEnhancement.defaultPreference.profile), expectedProfile);
	QVERIFY(!restored.inputEnhancement.defaultPreference.autoAdapt);
	QCOMPARE(restored.inputEnhancement.defaultPreference.reduction, 73);
	QVERIFY(restored.inputEnhancement.legacyOverride.has_value());
	const Mumble::InputEnhancement::LegacyOverride &captured = *restored.inputEnhancement.legacyOverride;
	QCOMPARE(captured.noiseCancelMode, mode == QLatin1String("Off")     ? 0
									   : mode == QLatin1String("Speex") ? 1
									   : mode == QLatin1String("RNN")   ? 2
																		: 3);
	QCOMPARE(captured.backend, backend == QLatin1String("RNNoise") ? 0 : backend == QLatin1String("DTLN") ? 1 : 2);
	QCOMPARE(captured.modelId, QStringLiteral("legacy:model-id"));
	QCOMPARE(captured.customModelPath, QStringLiteral("C:/models/exact.bin"));
	QCOMPARE(captured.speexNoiseCancelStrength, -73);
}

void TestInputEnhancementSettings::roundTripsSchemaV3() {
	::Settings original;
	original.inputEnhancement.defaultPreference.profile   = Mumble::InputEnhancement::Profile::Quality;
	original.inputEnhancement.defaultPreference.reduction = 87;
	original.inputEnhancement.defaultPreference.character = 21;
	original.inputEnhancement.defaultPreference.autoAdapt = false;

	Mumble::InputEnhancement::DeviceProfileState device;
	device.identity.backendId            = QStringLiteral("WASAPI");
	device.identity.physicalId           = QStringLiteral("{endpoint-guid}");
	device.identity.displayName          = QStringLiteral("Desk microphone");
	device.identity.followsSystemDefault = true;
	device.preference.profile            = Mumble::InputEnhancement::Profile::Balanced;
	device.preference.reduction          = 61;
	device.preference.character          = 44;
	device.preference.autoAdapt          = true;
	device.calibrated                    = true;
	device.lastUsedEpochMs               = 123456789;
	device.lastKnownGood                 = preference(Mumble::InputEnhancement::Profile::Light, 38, 57);
	device.lastKnownGoodRecipeBinding    = exactBinding(Mumble::InputEnhancement::Profile::Light, 38, 57);
	device.pendingRecipeBinding          = exactBinding(Mumble::InputEnhancement::Profile::Balanced, 61, 44);
	device.pendingValidation             = true;
	device.lastRollbackReason            = QStringLiteral("deadline");
	device.legacyOverride = Mumble::InputEnhancement::LegacyOverride{ 2, 0, QStringLiteral("rnnoise:custom"),
																	  QStringLiteral("C:/models/custom.bin"), -41 };
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(original.inputEnhancement, device));
	original.inputEnhancement.legacyOverride = device.legacyOverride;

	nlohmann::json json = original;
	QVERIFY(json.at("audio").contains("input_enhancement"));
	QCOMPARE(json.at("audio").at("input_enhancement").at("schema_version").get< int >(), 3);
	QCOMPARE(QString::fromStdString(
				 json.at("audio").at("input_enhancement").at("default").at("profile").get< std::string >()),
			 QStringLiteral("Quality"));
	const auto &persistedDevice = json.at("audio").at("input_enhancement").at("devices").at(0);
	QVERIFY(persistedDevice.contains("last_known_good_recipe"));
	QVERIFY(persistedDevice.contains("pending_recipe"));
	QCOMPARE(QString::fromStdString(persistedDevice.at("pending_recipe").at("model_sha256").get< std::string >()),
			 QString(64, QLatin1Char('a')));

	const ::Settings restored = json.get< ::Settings >();
	QVERIFY(restored.inputEnhancement == original.inputEnhancement);
}

void TestInputEnhancementSettings::autoRecipeSetFingerprintRoundTripsAndFailsClosed() {
	using namespace Mumble::InputEnhancement;
	const QString setFingerprint(64, QLatin1Char('d'));
	QVERIFY(isValidAutoRecipeSetFingerprint(setFingerprint));
	QVERIFY(!isValidAutoRecipeSetFingerprint(QString(64, QLatin1Char('D'))));

	DeviceProfileState state;
	state.identity.backendId              = QStringLiteral("WASAPI");
	state.identity.physicalId             = QStringLiteral("auto-set-endpoint");
	state.preference                      = preference(Profile::Auto, 65, 70);
	state.lastKnownGood                   = preference(Profile::Balanced, 55, 60);
	state.lastKnownGoodRecipeBinding      = exactBinding(Profile::Balanced, 55, 60);
	state.pendingAutoRecipeSetFingerprint = setFingerprint;
	state.pendingValidation               = true;
	QVERIFY(executionBindingMatchesPreference(state.preference, state.pendingRecipeBinding,
											  state.pendingAutoRecipeSetFingerprint));

	Mumble::InputEnhancement::Settings settings;
	QVERIFY(upsertDeviceProfile(settings, state));
	const nlohmann::json persisted = serializeSettings(settings);
	const auto &deviceJson         = persisted.at("devices").at(0);
	QCOMPARE(QString::fromStdString(deviceJson.at("pending_auto_recipe_set_fingerprint").get< std::string >()),
			 setFingerprint);
	QVERIFY(!deviceJson.contains("pending_recipe"));

	Mumble::InputEnhancement::Settings restored;
	QVERIFY(deserializeSettings(persisted, restored));
	const DeviceProfileState *pending = findDeviceProfile(restored, state.identity);
	QVERIFY(pending);
	QVERIFY(pending->pendingAutoRecipeSetFingerprint.has_value());
	QCOMPARE(*pending->pendingAutoRecipeSetFingerprint, setFingerprint);
	QCOMPARE(preferenceForDevice(restored, state.identity).profile, Profile::Auto);

	nlohmann::json missing = persisted;
	missing["devices"][0].erase("pending_auto_recipe_set_fingerprint");
	QVERIFY(deserializeSettings(missing, restored));
	const DeviceProfileState *safe = findDeviceProfile(restored, state.identity);
	QVERIFY(safe);
	QCOMPARE(safe->preference.profile, Profile::Original);
	QVERIFY(!safe->pendingValidation);
	QCOMPARE(safe->lastRollbackReason, QStringLiteral("missing_exact_recipe_binding"));

	nlohmann::json malformed                                       = persisted;
	malformed["devices"][0]["pending_auto_recipe_set_fingerprint"] = std::string(64, 'D');
	QVERIFY(!deserializeSettings(malformed, restored));
	QCOMPARE(restored.defaultPreference.profile, Profile::Original);

	Mumble::InputEnhancement::Settings abnormal;
	QVERIFY(upsertDeviceProfile(abnormal, state));
	QVERIFY(rollbackPendingValidationAfterAbnormalExit(abnormal));
	const DeviceProfileState *rolledBack = findDeviceProfile(abnormal, state.identity);
	QVERIFY(rolledBack);
	QCOMPARE(rolledBack->preference.profile, Profile::Balanced);
	QVERIFY(!rolledBack->pendingAutoRecipeSetFingerprint.has_value());
	QVERIFY(rolledBack->rollbackUndoAutoRecipeSetFingerprint.has_value());
	QCOMPARE(*rolledBack->rollbackUndoAutoRecipeSetFingerprint, setFingerprint);
	QVERIFY(!rolledBack->rollbackUndoRecipeBinding.has_value());
}

void TestInputEnhancementSettings::migratesSchemaV2CrispAlias() {
	nlohmann::json input = {
		{ "schema_version", 2 },
		{ "default", { { "profile", "Crisp" }, { "reduction", 87 }, { "character", 21 }, { "auto_adapt", false } } },
		{ "devices", nlohmann::json::array() },
	};
	Mumble::InputEnhancement::Settings restored;
	QVERIFY(deserializeSettings(input, restored));
	QCOMPARE(restored.schemaVersion, SETTINGS_SCHEMA_VERSION);
	QCOMPARE(restored.defaultPreference.profile, Profile::Quality);
	QCOMPARE(restored.defaultPreference.reduction, 87);
	QCOMPARE(restored.defaultPreference.character, 21);

	const nlohmann::json persisted = serializeSettings(restored);
	QCOMPARE(persisted.at("schema_version").get< int >(), 3);
	QCOMPARE(QString::fromStdString(persisted.at("default").at("profile").get< std::string >()),
			 QStringLiteral("Quality"));
}

void TestInputEnhancementSettings::corruptOrUnknownSchemaUsesSafeOriginal() {
	for (const nlohmann::json &invalid : {
			 nlohmann::json{ { "schema_version", 99 },
							 { "default", nlohmann::json::object() },
							 { "devices", nlohmann::json::array() } },
			 nlohmann::json{
				 { "schema_version", 2 },
				 { "default",
				   { { "profile", "Future" }, { "reduction", 50 }, { "character", 50 }, { "auto_adapt", true } } },
				 { "devices", nlohmann::json::array() } },
			 nlohmann::json{ { "schema_version", 2 }, { "default", "broken" }, { "devices", nlohmann::json::array() } },
		 }) {
		nlohmann::json root;
		root["settings_version"]           = 1;
		root["audio"]["input_enhancement"] = invalid;
		const ::Settings restored          = root.get< ::Settings >();
		QCOMPARE(restored.inputEnhancement.defaultPreference.profile, Mumble::InputEnhancement::Profile::Original);
		QVERIFY(!restored.inputEnhancement.defaultPreference.autoAdapt);
		QVERIFY(restored.inputEnhancement.deviceProfiles.isEmpty());
		QVERIFY(!restored.inputEnhancement.legacyOverride.has_value());
	}
}

void TestInputEnhancementSettings::rejectsCorruptLegacyOverridesAndSafelyBoundsMigration() {
	QCOMPARE(Mumble::InputEnhancement::reductionForLegacySpeexStrength(std::numeric_limits< int >::min()), 100);
	QCOMPARE(Mumble::InputEnhancement::reductionForLegacySpeexStrength(-73), 73);
	QCOMPARE(Mumble::InputEnhancement::reductionForLegacySpeexStrength(0), 0);

	for (const auto &[mode, backend, strength] : {
			 std::tuple{ 99, 0, -30 },
			 std::tuple{ 2, 99, -30 },
			 std::tuple{ 2, 0, std::numeric_limits< int >::min() },
			 std::tuple{ 2, 0, 30 },
		 }) {
		nlohmann::json input = {
			{ "schema_version", 2 },
			{ "default",
			  { { "profile", "Balanced" }, { "reduction", 50 }, { "character", 50 }, { "auto_adapt", false } } },
			{ "devices", nlohmann::json::array() },
			{ "legacy_override",
			  { { "noise_cancel_mode", mode },
				{ "backend", backend },
				{ "model_id", "rnnoise:embedded" },
				{ "custom_model_path", "" },
				{ "speex_noise_cancel_strength", strength } } },
		};
		Mumble::InputEnhancement::Settings restored;
		QVERIFY(!Mumble::InputEnhancement::deserializeSettings(input, restored));
		QCOMPARE(restored.defaultPreference.profile, Mumble::InputEnhancement::Profile::Original);
		QVERIFY(!restored.legacyOverride.has_value());
	}
}

void TestInputEnhancementSettings::deviceKeyExcludesDisplayName() {
	Mumble::InputEnhancement::DeviceIdentity first;
	first.backendId   = QStringLiteral("WASAPI");
	first.physicalId  = QStringLiteral("endpoint-1");
	first.displayName = QStringLiteral("Old display name");

	auto renamed        = first;
	renamed.displayName = QStringLiteral("Renamed microphone");
	QCOMPARE(Mumble::InputEnhancement::stableDeviceKey(first), Mumble::InputEnhancement::stableDeviceKey(renamed));
	QCOMPARE(Mumble::InputEnhancement::stableDeviceKey(first).size(), 64);

	auto otherPhysical       = first;
	otherPhysical.physicalId = QStringLiteral("endpoint-2");
	QVERIFY(Mumble::InputEnhancement::stableDeviceKey(first)
			!= Mumble::InputEnhancement::stableDeviceKey(otherPhysical));
	auto otherBackend      = first;
	otherBackend.backendId = QStringLiteral("PipeWire");
	QVERIFY(Mumble::InputEnhancement::stableDeviceKey(first)
			!= Mumble::InputEnhancement::stableDeviceKey(otherBackend));
}

void TestInputEnhancementSettings::selectsProfileByOpenedPhysicalDevice() {
	using namespace Mumble::InputEnhancement;
	Mumble::InputEnhancement::Settings settings;

	DeviceProfileState desk;
	desk.identity.backendId            = QStringLiteral("WASAPI");
	desk.identity.physicalId           = QStringLiteral("endpoint-desk");
	desk.identity.displayName          = QStringLiteral("Desk microphone");
	desk.identity.followsSystemDefault = false;
	desk.preference.profile            = Profile::Crisp;
	QVERIFY(upsertDeviceProfile(settings, desk));

	DeviceProfileState headset;
	headset.identity.backendId  = QStringLiteral("WASAPI");
	headset.identity.physicalId = QStringLiteral("endpoint-headset");
	headset.preference.profile  = Profile::Balanced;
	QVERIFY(upsertDeviceProfile(settings, headset));

	// The configured choice was "System default", but the backend resolved and
	// opened the desk endpoint. Default-following and a renamed display label do
	// not alter the physical-device key.
	DeviceIdentity openedDefault       = desk.identity;
	openedDefault.displayName          = QStringLiteral("Renamed desk microphone");
	openedDefault.followsSystemDefault = true;
	const DeviceProfileState *selected = findDeviceProfile(settings, openedDefault);
	QVERIFY(selected);
	QCOMPARE(selected->preference.profile, Profile::Crisp);
	QVERIFY(deviceIdentitiesMatch(desk.identity, openedDefault));

	openedDefault.physicalId = QStringLiteral("endpoint-headset");
	selected                 = findDeviceProfile(settings, openedDefault);
	QVERIFY(selected);
	QCOMPARE(selected->preference.profile, Profile::Balanced);

	openedDefault.physicalId.clear();
	QVERIFY(!findDeviceProfile(settings, openedDefault));
	QVERIFY(!deviceIdentitiesMatch(desk.identity, openedDefault));
}

void TestInputEnhancementSettings::editableDeviceProfileInheritsDefaultWithoutMutatingIt() {
	using namespace Mumble::InputEnhancement;
	Mumble::InputEnhancement::Settings settings;
	settings.defaultPreference.profile   = Profile::Light;
	settings.defaultPreference.reduction = 31;

	DeviceIdentity stable;
	stable.backendId            = QStringLiteral("WASAPI");
	stable.physicalId           = QStringLiteral("physical-endpoint");
	DeviceProfileState *profile = ensureDeviceProfile(settings, stable);
	QVERIFY(profile);
	QVERIFY(profile->preference == settings.defaultPreference);
	profile->preference.profile   = Profile::Crisp;
	profile->preference.reduction = 72;
	QCOMPARE(preferenceForDevice(settings, stable).profile, Profile::Crisp);
	QCOMPARE(settings.defaultPreference.profile, Profile::Light);
	QCOMPARE(settings.defaultPreference.reduction, 31);

	DeviceIdentity unstable;
	unstable.backendId  = QStringLiteral("PipeWire");
	unstable.physicalId = QStringLiteral("session-node-42");
	unstable.stable     = false;
	QVERIFY(ensureDeviceProfile(settings, unstable));
	QVERIFY(findDeviceProfile(settings, unstable));
	const nlohmann::json serialized = serializeSettings(settings);
	QCOMPARE(serialized.at("devices").size(), std::size_t{ 1 });

	DeviceIdentity incomplete;
	incomplete.backendId = QStringLiteral("WASAPI");
	QVERIFY(!ensureDeviceProfile(settings, incomplete));
	QVERIFY(&preferenceForDevice(settings, incomplete) == &settings.defaultPreference);
}

void TestInputEnhancementSettings::autoProfileAlwaysEnablesAdaptationOnLoad() {
	nlohmann::json input = {
		{ "schema_version", 2 },
		{ "default", { { "profile", "Auto" }, { "reduction", 44 }, { "character", 66 }, { "auto_adapt", false } } },
		{ "devices", nlohmann::json::array() },
	};
	Mumble::InputEnhancement::Settings restored;
	QVERIFY(Mumble::InputEnhancement::deserializeSettings(input, restored));
	QCOMPARE(restored.defaultPreference.profile, Mumble::InputEnhancement::Profile::Auto);
	QVERIFY(restored.defaultPreference.autoAdapt);
}

void TestInputEnhancementSettings::evictsOnlyLeastRecentlyUsedUncalibratedProfile() {
	Mumble::InputEnhancement::Settings settings;
	for (int i = 0; i < Mumble::InputEnhancement::MAX_DEVICE_PROFILES; ++i) {
		Mumble::InputEnhancement::DeviceProfileState state;
		state.identity.backendId  = QStringLiteral("test");
		state.identity.physicalId = QStringLiteral("device-%1").arg(i);
		state.calibrated          = i != 7 && i != 11;
		state.lastUsedEpochMs     = i == 11 ? 50 : i == 7 ? 100 : i + 1000;
		QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(settings, state));
	}

	Mumble::InputEnhancement::DeviceProfileState replacement;
	replacement.identity.backendId  = QStringLiteral("test");
	replacement.identity.physicalId = QStringLiteral("replacement");
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(settings, replacement));
	QCOMPARE(settings.deviceProfiles.size(), Mumble::InputEnhancement::MAX_DEVICE_PROFILES);

	bool foundReplacement = false;
	bool foundSeven       = false;
	bool foundEleven      = false;
	for (const auto &state : settings.deviceProfiles) {
		foundReplacement |= state.identity.physicalId == QLatin1String("replacement");
		foundSeven |= state.identity.physicalId == QLatin1String("device-7");
		foundEleven |= state.identity.physicalId == QLatin1String("device-11");
	}
	QVERIFY(foundReplacement);
	QVERIFY(foundSeven);
	QVERIFY(!foundEleven);

	for (auto &state : settings.deviceProfiles) {
		state.calibrated = true;
	}
	Mumble::InputEnhancement::DeviceProfileState rejected;
	rejected.identity.backendId  = QStringLiteral("test");
	rejected.identity.physicalId = QStringLiteral("must-not-evict");
	QVERIFY(!Mumble::InputEnhancement::upsertDeviceProfile(settings, rejected));
	QCOMPARE(settings.deviceProfiles.size(), Mumble::InputEnhancement::MAX_DEVICE_PROFILES);
}

void TestInputEnhancementSettings::openedPhysicalDeviceAdvancesLruWithoutCreatingEntries() {
	Mumble::InputEnhancement::Settings settings;
	Mumble::InputEnhancement::DeviceProfileState first;
	first.identity.backendId  = QStringLiteral("WASAPI");
	first.identity.physicalId = QStringLiteral("endpoint-first");
	first.identity.stable     = true;
	first.lastUsedEpochMs     = 100;
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(settings, first));

	QVERIFY(Mumble::InputEnhancement::markDeviceProfileUsed(settings, first.identity, 250));
	const auto *updated = Mumble::InputEnhancement::findDeviceProfile(settings, first.identity);
	QVERIFY(updated);
	QCOMPARE(updated->lastUsedEpochMs, qint64{ 250 });
	QVERIFY(!Mumble::InputEnhancement::markDeviceProfileUsed(settings, first.identity, 200));
	QCOMPARE(Mumble::InputEnhancement::findDeviceProfile(settings, first.identity)->lastUsedEpochMs, qint64{ 250 });

	Mumble::InputEnhancement::DeviceIdentity unknown = first.identity;
	unknown.physicalId                               = QStringLiteral("endpoint-unknown");
	QVERIFY(!Mumble::InputEnhancement::markDeviceProfileUsed(settings, unknown, 500));
	QCOMPARE(settings.deviceProfiles.size(), 1);
}

void TestInputEnhancementSettings::sessionOnlyProfilesAreNotPersistedAndKeysAreVerified() {
	Mumble::InputEnhancement::Settings inputSettings;
	Mumble::InputEnhancement::DeviceProfileState sessionOnly;
	sessionOnly.identity.backendId  = QStringLiteral("PipeWire");
	sessionOnly.identity.physicalId = QStringLiteral("session-node");
	sessionOnly.identity.stable     = false;
	sessionOnly.preference.profile  = Mumble::InputEnhancement::Profile::Crisp;
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(inputSettings, sessionOnly));
	const auto *selected = Mumble::InputEnhancement::findDeviceProfile(inputSettings, sessionOnly.identity);
	QVERIFY(selected);
	QCOMPARE(selected->preference.profile, Mumble::InputEnhancement::Profile::Crisp);
	QVERIFY(Mumble::InputEnhancement::serializeSettings(inputSettings).at("devices").empty());

	::Settings root;
	Mumble::InputEnhancement::DeviceProfileState stable;
	stable.identity.backendId  = QStringLiteral("WASAPI");
	stable.identity.physicalId = QStringLiteral("endpoint");
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(root.inputEnhancement, stable));
	nlohmann::json json                                     = root;
	json["audio"]["input_enhancement"]["devices"][0]["key"] = "mismatch";
	const ::Settings restored                               = json.get< ::Settings >();
	QCOMPARE(static_cast< int >(restored.inputEnhancement.defaultPreference.profile),
			 static_cast< int >(Mumble::InputEnhancement::Profile::Original));
	QVERIFY(restored.inputEnhancement.deviceProfiles.isEmpty());
}

void TestInputEnhancementSettings::migratesNonEmptyLegacyQSettings() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("legacy.ini"));
	{
		QSettings legacy(path, QSettings::IniFormat);
		legacy.setValue(QStringLiteral("audio/noiseCancelMode"), static_cast< int >(::Settings::NoiseCancelRNN));
		legacy.setValue(QStringLiteral("audio/noiseCancelBackend"), static_cast< int >(::Settings::DTLNBackend));
		legacy.setValue(QStringLiteral("audio/noiseCancelModelId"), QStringLiteral("dtln:norm40h"));
		legacy.setValue(QStringLiteral("audio/noiseCancelCustomModelPath"), QStringLiteral("C:/exact/model"));
		legacy.setValue(QStringLiteral("audio/speexNoiseCancelStrength"), -67);
		legacy.sync();
	}

	::Settings restored;
	restored.legacyLoad(path);
	QVERIFY(restored.inputEnhancement.legacyOverride.has_value());
	const auto &captured = *restored.inputEnhancement.legacyOverride;
	QCOMPARE(captured.noiseCancelMode, static_cast< int >(::Settings::NoiseCancelRNN));
	QCOMPARE(captured.backend, static_cast< int >(::Settings::DTLNBackend));
	QCOMPARE(captured.modelId, QStringLiteral("dtln:norm40h"));
	QCOMPARE(captured.customModelPath, QStringLiteral("C:/exact/model"));
	QCOMPARE(captured.speexNoiseCancelStrength, -67);
	QCOMPARE(static_cast< int >(restored.inputEnhancement.defaultPreference.profile),
			 static_cast< int >(Mumble::InputEnhancement::Profile::Crisp));

	const QString emptyPath = directory.filePath(QStringLiteral("empty.ini"));
	QSettings(emptyPath, QSettings::IniFormat).sync();
	::Settings empty;
	empty.legacyLoad(emptyPath);
	QVERIFY(!empty.inputEnhancement.legacyOverride.has_value());
}

void TestInputEnhancementSettings::pendingWithoutLastKnownGoodFailsSafeOnParseAndSelection() {
	using namespace Mumble::InputEnhancement;
	DeviceProfileState state;
	state.identity.backendId  = QStringLiteral("WASAPI");
	state.identity.physicalId = QStringLiteral("unsafe-pending-endpoint");
	state.preference.profile  = Profile::Crisp;
	state.pendingValidation   = true;
	state.legacyOverride      = LegacyOverride{ 2, 2, QStringLiteral("deepfilternet:default"), {}, -30 };

	Mumble::InputEnhancement::Settings inMemory;
	QVERIFY(upsertDeviceProfile(inMemory, state));
	QCOMPARE(preferenceForDevice(inMemory, state.identity).profile, Profile::Original);
	QVERIFY(!preferenceForDevice(inMemory, state.identity).autoAdapt);

	// Build a structurally valid persisted candidate and remove each piece of
	// its exact rollback contract in turn. A missing preference, candidate
	// binding, or non-Original LKG binding must all fail safe without rejecting
	// the rest of schema v2.
	state.lastKnownGood              = preference(Profile::Light, 36, 48);
	state.lastKnownGoodRecipeBinding = exactBinding(Profile::Light, 36, 48);
	state.pendingRecipeBinding       = exactBinding(Profile::Crisp, 30, 50);
	Mumble::InputEnhancement::Settings valid;
	QVERIFY(upsertDeviceProfile(valid, state));
	const nlohmann::json validJson = serializeSettings(valid);

	for (const char *missingField : { "last_known_good", "last_known_good_recipe", "pending_recipe" }) {
		nlohmann::json json = validJson;
		json["devices"][0].erase(missingField);

		Mumble::InputEnhancement::Settings parsed;
		QVERIFY(deserializeSettings(json, parsed));
		const DeviceProfileState *safe = findDeviceProfile(parsed, state.identity);
		QVERIFY(safe);
		QCOMPARE(safe->preference.profile, Profile::Original);
		QVERIFY(!safe->pendingValidation);
		QVERIFY(safe->lastKnownGood.has_value());
		QCOMPARE(safe->lastKnownGood->profile, Profile::Original);
		QVERIFY(!safe->lastKnownGoodRecipeBinding.has_value());
		QVERIFY(!safe->pendingRecipeBinding.has_value());
		QCOMPARE(safe->lastRollbackReason, QStringLiteral("missing_exact_recipe_binding"));
		QVERIFY(!safe->legacyOverride.has_value());
	}
}

void TestInputEnhancementSettings::rejectsMalformedRecipeBindings() {
	using namespace Mumble::InputEnhancement;
	DeviceProfileState state;
	state.identity.backendId         = QStringLiteral("WASAPI");
	state.identity.physicalId        = QStringLiteral("malformed-binding-endpoint");
	state.preference                 = preference(Profile::Balanced, 62, 41);
	state.pendingValidation          = true;
	state.pendingRecipeBinding       = exactBinding(Profile::Balanced, 62, 41);
	state.lastKnownGood              = preference(Profile::Light, 34, 47);
	state.lastKnownGoodRecipeBinding = exactBinding(Profile::Light, 34, 47);

	Mumble::InputEnhancement::Settings settings;
	QVERIFY(upsertDeviceProfile(settings, state));
	const nlohmann::json validJson = serializeSettings(settings);

	const QList< std::function< void(nlohmann::json &) > > corruptions{
		[](nlohmann::json &json) { json["devices"][0]["pending_recipe"].erase("execution_fingerprint"); },
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["execution_fingerprint"] = std::string(63, 'a');
		},
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["execution_fingerprint"] = std::string(64, 'A');
		},
		[](nlohmann::json &json) { json["devices"][0]["pending_recipe"]["model_sha256"] = std::string(63, 'a'); },
		[](nlohmann::json &json) { json["devices"][0]["pending_recipe"]["model_sha256"] = std::string(64, 'G'); },
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["model_sha256"] = QString(64, QChar(0x0661)).toStdString();
		},
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["model_relative_path"] = "../escaped-model.bin";
		},
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["model_relative_path"] = "C:/absolute/model.bin";
		},
		[](nlohmann::json &json) {
			json["devices"][0]["pending_recipe"]["model_relative_path"] = "models\\escaped.bin";
		},
	};

	for (const auto &corrupt : corruptions) {
		nlohmann::json json = validJson;
		corrupt(json);
		Mumble::InputEnhancement::Settings restored;
		QVERIFY(!deserializeSettings(json, restored));
		QCOMPARE(restored.defaultPreference.profile, Profile::Original);
		QVERIFY(restored.deviceProfiles.isEmpty());
	}
}

void TestInputEnhancementSettings::qualityExpertBindingsRequireQualifiedCpuAndLatency() {
	using namespace Mumble::InputEnhancement;
	RecipeBinding dtln = exactBinding(Profile::Quality);
	dtln.recipeId          = QStringLiteral("input.expert.dtln-norm40h");
	dtln.engine            = Engine::DTLN;
	dtln.modelId           = QStringLiteral("dtln:norm40h:model-1");
	dtln.modelRelativePath = QStringLiteral("dtln/norm_40h/model_1.onnx");
	QVERIFY(isValidRecipeBinding(dtln));

	RecipeBinding underClassed = dtln;
	underClassed.minimumCpuClass  = CpuClass::Standard;
	QVERIFY(!isValidRecipeBinding(underClassed));

	RecipeBinding underBudgeted = exactBinding(Profile::Quality);
	underBudgeted.recipeId              = QStringLiteral("input.expert.deepfilternet-low-latency");
	underBudgeted.latencyBudgetSamples = 40U * 48U;
	QVERIFY(!isValidRecipeBinding(underBudgeted));
	underBudgeted.latencyBudgetSamples = qualityLatencyBudgetSamples;
	QVERIFY(isValidRecipeBinding(underBudgeted));
}

void TestInputEnhancementSettings::exactRecipeBindingDetectsCatalogRecipeAndModelDrift() {
	using namespace Mumble::InputEnhancement;
	const Recipe recipe         = resolvedRecipe(Profile::Balanced, 62, 41);
	const QString hash          = QString(64, QLatin1Char('a'));
	const QString path          = QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin");
	const RecipeBinding binding = recipeBindingForRecipe(recipe, QString::fromLatin1(testCatalogRevision), hash, path);

	QVERIFY(isValidRecipeBinding(binding));
	QVERIFY(recipeBindingMatches(binding, recipe, QString::fromLatin1(testCatalogRevision), hash, path));
	QVERIFY(!recipeBindingMatches(binding, recipe, QStringLiteral("input-recipes-v3"), hash, path));
	QVERIFY(!recipeBindingMatches(binding, resolvedRecipe(Profile::Balanced, 63, 41),
								  QString::fromLatin1(testCatalogRevision), hash, path));
	QVERIFY(!recipeBindingMatches(binding, recipe, QString::fromLatin1(testCatalogRevision),
								  QString(64, QLatin1Char('b')), path));
	QVERIFY(!recipeBindingMatches(binding, recipe, QString::fromLatin1(testCatalogRevision), hash,
								  QStringLiteral("rnnoise/replaced-model.bin")));
	QCOMPARE(binding.executionFingerprint, recipeExecutionFingerprint(recipe));
	QVERIFY(binding.executionFingerprint != recipeExecutionFingerprint(resolvedRecipe(Profile::Balanced, 63, 41)));
	RecipeBinding executionDrift = binding;
	executionDrift.executionFingerprint[0] =
		executionDrift.executionFingerprint[0] == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0');
	QVERIFY(!recipeBindingMatches(executionDrift, recipe, QString::fromLatin1(testCatalogRevision), hash, path));

	RecipeBinding recipeRevisionDrift = binding;
	++recipeRevisionDrift.recipeRevision;
	QVERIFY(!recipeBindingMatches(recipeRevisionDrift, recipe, QString::fromLatin1(testCatalogRevision), hash, path));
	RecipeBinding recipeIdDrift = binding;
	recipeIdDrift.recipeId.append(QStringLiteral(".changed"));
	QVERIFY(!recipeBindingMatches(recipeIdDrift, recipe, QString::fromLatin1(testCatalogRevision), hash, path));
}

void TestInputEnhancementSettings::unmanagedBuildZeroNeuralBindingRemainsHashBound() {
	using namespace Mumble::InputEnhancement;
	const Recipe recipe = resolvedRecipe(Profile::Balanced, 60, 60);
	const QString hash(64, QLatin1Char('a'));
	const QString path = QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin");
	const RecipeBinding binding = recipeBindingForRecipe(
		recipe, QStringLiteral("unmanaged-build-zero"), hash, path);

	QVERIFY(isValidRecipeBinding(binding));
	QVERIFY(recipeBindingMatches(binding, recipe, QStringLiteral("unmanaged-build-zero"), hash, path));
	QVERIFY(!recipeBindingMatches(binding, recipe, QString::fromLatin1(testCatalogRevision), hash, path));

	RecipeBinding missingHash = binding;
	missingHash.modelSha256.clear();
	QVERIFY(!isValidRecipeBinding(missingHash));

	RecipeBinding missingPath = binding;
	missingPath.modelRelativePath.clear();
	QVERIFY(!isValidRecipeBinding(missingPath));
}

void TestInputEnhancementSettings::abnormalExitRollsBackPendingValidation() {
	using namespace Mumble::InputEnhancement;
	Mumble::InputEnhancement::Settings settings;
	DeviceProfileState state;
	state.identity.backendId         = QStringLiteral("WASAPI");
	state.identity.physicalId        = QStringLiteral("probation-endpoint");
	state.preference                 = preference(Profile::Crisp, 71, 82);
	state.pendingRecipeBinding       = exactBinding(Profile::Crisp, 71, 82);
	DefaultPreference knownGood      = preference(Profile::Balanced, 54, 39);
	state.lastKnownGood              = knownGood;
	state.lastKnownGoodRecipeBinding = exactBinding(Profile::Balanced, 54, 39);
	state.pendingValidation          = true;
	state.legacyOverride             = LegacyOverride{ 2, 2, QStringLiteral("deepfilternet:default"), {}, -30 };
	QVERIFY(upsertDeviceProfile(settings, state));

	QVERIFY(rollbackPendingValidationAfterAbnormalExit(settings));
	const DeviceProfileState *rolledBack = findDeviceProfile(settings, state.identity);
	QVERIFY(rolledBack);
	QCOMPARE(rolledBack->preference.profile, Profile::Balanced);
	QVERIFY(rolledBack->preference == knownGood);
	QVERIFY(rolledBack->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*rolledBack->lastKnownGoodRecipeBinding == exactBinding(Profile::Balanced, 54, 39));
	QVERIFY(!rolledBack->pendingRecipeBinding.has_value());
	QVERIFY(!rolledBack->pendingValidation);
	QCOMPARE(rolledBack->lastRollbackReason, QStringLiteral("crash_detected"));
	QVERIFY(rolledBack->rollbackUndoPreference.has_value());
	QVERIFY(*rolledBack->rollbackUndoPreference == state.preference);
	QVERIFY(rolledBack->rollbackUndoRecipeBinding.has_value());
	QVERIFY(*rolledBack->rollbackUndoRecipeBinding == *state.pendingRecipeBinding);
	QVERIFY(!rolledBack->legacyOverride.has_value());
	QVERIFY(!rollbackPendingValidationAfterAbnormalExit(settings));
}

void TestInputEnhancementSettings::abnormalExitWithoutExactLastKnownGoodUsesOriginal() {
	using namespace Mumble::InputEnhancement;
	Mumble::InputEnhancement::Settings settings;
	DeviceProfileState state;
	state.identity.backendId   = QStringLiteral("WASAPI");
	state.identity.physicalId  = QStringLiteral("incomplete-lkg-endpoint");
	state.preference           = preference(Profile::Crisp, 74, 83);
	state.pendingRecipeBinding = exactBinding(Profile::Crisp, 74, 83);
	state.lastKnownGood        = preference(Profile::Balanced, 52, 46);
	state.pendingValidation    = true;
	QVERIFY(upsertDeviceProfile(settings, state));

	QVERIFY(rollbackPendingValidationAfterAbnormalExit(settings));
	const DeviceProfileState *rolledBack = findDeviceProfile(settings, state.identity);
	QVERIFY(rolledBack);
	QCOMPARE(rolledBack->preference.profile, Profile::Original);
	QVERIFY(rolledBack->lastKnownGood.has_value());
	QCOMPARE(rolledBack->lastKnownGood->profile, Profile::Original);
	QVERIFY(!rolledBack->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(!rolledBack->pendingRecipeBinding.has_value());
	QVERIFY(!rolledBack->pendingValidation);
	QCOMPARE(rolledBack->lastRollbackReason, QStringLiteral("crash_detected"));
	QVERIFY(rolledBack->rollbackUndoPreference.has_value());
	QVERIFY(*rolledBack->rollbackUndoPreference == state.preference);
	QVERIFY(rolledBack->rollbackUndoRecipeBinding.has_value());
	QVERIFY(*rolledBack->rollbackUndoRecipeBinding == *state.pendingRecipeBinding);
}

void TestInputEnhancementSettings::abnormalExitRollbackIsDurablyPersisted() {
	using namespace Mumble::InputEnhancement;
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("mumble_settings.json"));

	::Settings candidate;
	DeviceProfileState state;
	state.identity.backendId         = QStringLiteral("WASAPI");
	state.identity.physicalId        = QStringLiteral("durable-probation-endpoint");
	state.preference                 = preference(Profile::Crisp, 79, 67);
	state.pendingRecipeBinding       = exactBinding(Profile::Crisp, 79, 67);
	DefaultPreference knownGood      = preference(Profile::Light, 42, 58);
	state.lastKnownGood              = knownGood;
	state.lastKnownGoodRecipeBinding = exactBinding(Profile::Light, 42, 58);
	state.pendingValidation          = true;
	QVERIFY(upsertDeviceProfile(candidate.inputEnhancement, state));
	candidate.mumbleQuitNormally = false;
	candidate.save(path);

	::Settings recovered;
	recovered.load(path, true);
	const DeviceProfileState *runtime = findDeviceProfile(recovered.inputEnhancement, state.identity);
	QVERIFY(runtime);
	QCOMPARE(runtime->preference.profile, Profile::Light);
	QVERIFY(runtime->preference == knownGood);
	QVERIFY(runtime->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*runtime->lastKnownGoodRecipeBinding == exactBinding(Profile::Light, 42, 58));
	QVERIFY(!runtime->pendingRecipeBinding.has_value());
	QVERIFY(!runtime->pendingValidation);
	QCOMPARE(runtime->lastRollbackReason, QStringLiteral("crash_detected"));
	QVERIFY(runtime->rollbackUndoPreference.has_value());
	QVERIFY(*runtime->rollbackUndoPreference == state.preference);
	QVERIFY(runtime->rollbackUndoRecipeBinding.has_value());
	QVERIFY(*runtime->rollbackUndoRecipeBinding == *state.pendingRecipeBinding);

	QFile persistedFile(path);
	QVERIFY(persistedFile.open(QIODevice::ReadOnly));
	const nlohmann::json persisted   = nlohmann::json::parse(persistedFile.readAll().toStdString());
	::Settings persistedSettings     = persisted.get< ::Settings >();
	const DeviceProfileState *onDisk = findDeviceProfile(persistedSettings.inputEnhancement, state.identity);
	QVERIFY(onDisk);
	QCOMPARE(onDisk->preference.profile, Profile::Light);
	QVERIFY(onDisk->preference == knownGood);
	QVERIFY(onDisk->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*onDisk->lastKnownGoodRecipeBinding == exactBinding(Profile::Light, 42, 58));
	QVERIFY(!onDisk->pendingRecipeBinding.has_value());
	QVERIFY(!onDisk->pendingValidation);
	QCOMPARE(onDisk->lastRollbackReason, QStringLiteral("crash_detected"));
	QVERIFY(onDisk->rollbackUndoPreference.has_value());
	QVERIFY(*onDisk->rollbackUndoPreference == state.preference);
	QVERIFY(onDisk->rollbackUndoRecipeBinding.has_value());
	QVERIFY(*onDisk->rollbackUndoRecipeBinding == *state.pendingRecipeBinding);
}

void TestInputEnhancementSettings::normalExitKeepsPendingValidationForResume() {
	using namespace Mumble::InputEnhancement;
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("mumble_settings.json"));

	::Settings candidate;
	DeviceProfileState state;
	state.identity.backendId         = QStringLiteral("WASAPI");
	state.identity.physicalId        = QStringLiteral("resume-probation-endpoint");
	state.preference                 = preference(Profile::Balanced, 63, 37);
	state.pendingRecipeBinding       = exactBinding(Profile::Balanced, 63, 37);
	DefaultPreference knownGood      = preference(Profile::Light, 41, 55);
	state.lastKnownGood              = knownGood;
	state.lastKnownGoodRecipeBinding = exactBinding(Profile::Light, 41, 55);
	state.pendingValidation          = true;
	QVERIFY(upsertDeviceProfile(candidate.inputEnhancement, state));
	candidate.mumbleQuitNormally = true;
	candidate.save(path);

	::Settings resumed;
	resumed.load(path, true);
	const DeviceProfileState *pending = findDeviceProfile(resumed.inputEnhancement, state.identity);
	QVERIFY(pending);
	QCOMPARE(pending->preference.profile, Profile::Balanced);
	QVERIFY(pending->pendingValidation);
	QVERIFY(pending->lastKnownGood.has_value());
	QCOMPARE(pending->lastKnownGood->profile, Profile::Light);
	QVERIFY(pending->lastKnownGoodRecipeBinding.has_value());
	QVERIFY(*pending->lastKnownGoodRecipeBinding == exactBinding(Profile::Light, 41, 55));
	QVERIFY(pending->pendingRecipeBinding.has_value());
	QVERIFY(*pending->pendingRecipeBinding == exactBinding(Profile::Balanced, 63, 37));
	QVERIFY(pending->lastRollbackReason.isEmpty());
}

QTEST_GUILESS_MAIN(TestInputEnhancementSettings)
#include "TestInputEnhancementSettings.moc"
