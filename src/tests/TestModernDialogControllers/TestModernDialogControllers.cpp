// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore/QFile>
#include <QtTest>

#include <algorithm>

#include "AudioInput.h"
#include "ModernConnectController.h"
#include "ModernDialogController.h"
#include "ModernSettingsController.h"
#include "ModernTheme.h"
#include "GlobalShortcut.h"

namespace {
QString readTestSource(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
	return QString::fromUtf8(file.readAll());
}

class TestShortcutEngine final : public GlobalShortcutEngine {
public:
	ButtonInfo buttonInfo(const QVariant &) override {
		ButtonInfo info;
		info.device = QStringLiteral("Test keyboard");
		info.name   = QStringLiteral("Test key");
		return info;
	}
};
} // namespace

class TestModernDialogControllers : public QObject {
	Q_OBJECT

private slots:
	void connectControllerSelectsAndSavesFavorites();
	void settingsControllerForcesModernAndAppliesDraft();
	void settingsAppearanceAutoAccentTracksDraftTheme();
	void settingsAppearancePreviewCommitsAndRollsBack();
	void settingsControllerEditsShortcutDataAndTargets();
	void settingsControllerClampsAudioSetupPayload();
	void settingsControllerRollsBackVoiceReplayPreview();
	void settingsControllerReconcilesPluginRuntimeState();
	void audioInputVoiceActivityLevelUsesExpectedSignals();
	void audioInputVoiceActivitySnapshotIsBounded();
	void nativeAutomationBoundariesRemainTypedAndDeterministic();
	void dialogControllerBuildsFailedConnectionReconnect();
	void dialogControllerDispatchesGenericDialogAction();
	void dialogControllerBuildsDisconnectConfirmation();
	void dialogControllerBuildsQuitConfirmation();
	void dialogControllerBuildsDeleteMessageConfirmation();
	void dialogControllerBuildsChangeAvatar();
	void dialogControllerBuildsMigrationNotice();
};

void TestModernDialogControllers::connectControllerSelectsAndSavesFavorites() {
	Settings settings;
	settings.qsUsername = QStringLiteral("fallback-user");

	FavoriteServer savedServer;
	savedServer.qsName     = QStringLiteral("Production");
	savedServer.qsHostname = QStringLiteral("voice.example.test");
	savedServer.usPort     = 64738;
	savedServer.qsUsername = QStringLiteral("saved-user");
	savedServer.qsPassword = QStringLiteral("saved-password");

	ModernConnectController controller;
	controller.open(QList< FavoriteServer > { savedServer }, settings);

	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("connect"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(state.value(QStringLiteral("favorites")).toList().size(), 1);
	QCOMPARE(state.value(QStringLiteral("selectedFavoriteIndex")).toInt(), 0);
	QCOMPARE(state.value(QStringLiteral("editorOpen")).toBool(), false);
	QCOMPARE(state.value(QStringLiteral("sections")).toList().size(), 0);
	QCOMPARE(state.value(QStringLiteral("width")).toInt(), 860);
	QCOMPARE(state.value(QStringLiteral("height")).toInt(), 640);
	QCOMPARE(state.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("connectFavoriteList"));
	const QVariantMap savedFavorite = state.value(QStringLiteral("favorites")).toList().at(0).toMap();
	QCOMPARE(savedFavorite.value(QStringLiteral("usersLabel")).toString(), QStringLiteral("Users: -"));
	QCOMPARE(savedFavorite.value(QStringLiteral("pingLabel")).toString(), QStringLiteral("Ping: -"));
	QVERIFY(controller.setFavoritePing(QStringLiteral("voice.example.test"), 64738, 42, 7, 128));
	state = controller.state();
	const QVariantMap pingedFavorite = state.value(QStringLiteral("favorites")).toList().at(0).toMap();
	QCOMPARE(pingedFavorite.value(QStringLiteral("usersLabel")).toString(), QStringLiteral("Users: 7/128"));
	QCOMPARE(pingedFavorite.value(QStringLiteral("pingLabel")).toString(), QStringLiteral("Ping: 42 ms"));

	ModernConnectController::ActionResult favoriteConnectResult =
		controller.invokeAction(QStringLiteral("connectFavorite"), QVariantMap { { QStringLiteral("index"), 0 } });
	QVERIFY(favoriteConnectResult.connectionRequest.has_value());
	QCOMPARE(favoriteConnectResult.connectionRequest->host, QStringLiteral("voice.example.test"));
	QCOMPARE(favoriteConnectResult.connectionRequest->port, 64738);
	QCOMPARE(favoriteConnectResult.connectionRequest->username, QStringLiteral("saved-user"));
	QCOMPARE(favoriteConnectResult.closeDialog, true);

	controller.open(QList< FavoriteServer > { savedServer }, settings);
	ModernConnectController::ActionResult clearResult =
		controller.invokeAction(QStringLiteral("newFavorite"), QVariantMap());
	QCOMPARE(clearResult.closeDialog, false);
	QCOMPARE(controller.state().value(QStringLiteral("selectedFavoriteIndex")).toInt(), -1);
	QCOMPARE(controller.state().value(QStringLiteral("editorOpen")).toBool(), true);
	QCOMPARE(controller.state().value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("dialogField_host"));

	controller.updateField(QStringLiteral("host"), QStringLiteral("mumble://dev.example.test/lobby"));
	controller.updateField(QStringLiteral("name"), QStringLiteral("Dev"));
	controller.updateField(QStringLiteral("username"), QStringLiteral("modern-user"));
	controller.updateField(QStringLiteral("port"), 65000);

	ModernConnectController::ActionResult saveResult =
		controller.invokeAction(QStringLiteral("saveFavorite"), QVariantMap());
	QVERIFY(saveResult.favoritesToSave.has_value());
	QCOMPARE(saveResult.favoritesToSave->size(), 2);
	QCOMPARE(saveResult.favoritesToSave->at(1).qsHostname, QStringLiteral("dev.example.test"));
	QCOMPARE(saveResult.favoritesToSave->at(1).usPort, 65000);
	QCOMPARE(saveResult.favoritesToSave->at(1).qsUsername, QStringLiteral("modern-user"));
	QCOMPARE(controller.state().value(QStringLiteral("editorOpen")).toBool(), false);

	ModernConnectController::ActionResult editResult =
		controller.invokeAction(QStringLiteral("editFavorite"), QVariantMap { { QStringLiteral("index"), 1 } });
	QCOMPARE(editResult.closeDialog, false);
	QCOMPARE(controller.state().value(QStringLiteral("editorOpen")).toBool(), true);
	controller.updateField(QStringLiteral("name"), QStringLiteral("Dev Renamed"));
	ModernConnectController::ActionResult editSaveResult =
		controller.invokeAction(QStringLiteral("saveFavorite"), QVariantMap());
	QVERIFY(editSaveResult.favoritesToSave.has_value());
	QCOMPARE(editSaveResult.favoritesToSave->size(), 2);
	QCOMPARE(editSaveResult.favoritesToSave->at(1).qsName, QStringLiteral("Dev Renamed"));

	ModernConnectController::ActionResult connectResult =
		controller.invokeAction(QStringLiteral("connect"), QVariantMap());
	QVERIFY(connectResult.connectionRequest.has_value());
	QCOMPARE(connectResult.connectionRequest->host, QStringLiteral("dev.example.test"));
	QCOMPARE(connectResult.connectionRequest->port, 65000);
	QCOMPARE(connectResult.connectionRequest->username, QStringLiteral("modern-user"));
	QCOMPARE(connectResult.closeDialog, true);

	controller.open(QList< FavoriteServer >(), settings);
	QCOMPARE(controller.state().value(QStringLiteral("initialFocusId")).toString(),
			 QStringLiteral("connectNewFavoriteButton"));
	controller.updateField(QStringLiteral("host"),
						   QStringLiteral("mumble://url-user:url-password@url.example.test:64739/root?title=URL"));
	ModernConnectController::ActionResult urlResult = controller.invokeAction(QStringLiteral("connect"), QVariantMap());
	QVERIFY(urlResult.connectionRequest.has_value());
	QCOMPARE(urlResult.connectionRequest->host, QStringLiteral("url.example.test"));
	QCOMPARE(urlResult.connectionRequest->port, 64739);
	QCOMPARE(urlResult.connectionRequest->username, QStringLiteral("url-user"));
	QCOMPARE(urlResult.connectionRequest->password, QStringLiteral("url-password"));
}

void TestModernDialogControllers::settingsControllerForcesModernAndAppliesDraft() {
	Settings settings;
	settings.modernLayoutPolicy       = Settings::ModernLayoutFollowLegacy;
	settings.wlWindowLayout           = Settings::LayoutClassic;
	settings.bReconnect               = false;
	settings.bReconnectToLastChannel  = false;
	settings.iQuality                 = 72000;

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("NetworkConfig"));

	QCOMPARE(controller.activePage(), QStringLiteral("network"));
	QCOMPARE(controller.draft().modernLayoutPolicy, Settings::ModernLayoutForced);
	QCOMPARE(controller.draft().wlWindowLayout, Settings::LayoutModern);

	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("settings"));
	QCOMPARE(state.value(QStringLiteral("activePage")).toString(), QStringLiteral("network"));
	QCOMPARE(state.value(QStringLiteral("preventBackdropClose")).toBool(), true);

	auto verifySettingsTooltips = [](const Settings &sourceSettings) {
		ModernSettingsController tooltipController;
		const QStringList pages { QStringLiteral("look"), QStringLiteral("ui"), QStringLiteral("messages"),
								  QStringLiteral("keys"), QStringLiteral("network"), QStringLiteral("screenShare"),
								  QStringLiteral("about"), QStringLiteral("audioInput"), QStringLiteral("audioOutput") };
		for (const QString &page : pages) {
			tooltipController.open(sourceSettings, page);
			const QVariantList sections = tooltipController.state().value(QStringLiteral("sections")).toList();
			for (const QVariant &sectionValue : sections) {
				const QVariantMap section = sectionValue.toMap();
				for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
					const QVariantMap field = fieldValue.toMap();
					const QString id        = field.value(QStringLiteral("id")).toString();
					if (id.isEmpty()) {
						continue;
					}
					QVERIFY2(!field.value(QStringLiteral("tooltip")).toString().trimmed().isEmpty(),
							 qPrintable(QStringLiteral("Missing tooltip for %1 on %2").arg(id, page)));
				}
			}
		}
	};
	verifySettingsTooltips(settings);

	auto findSettingsFieldById = [](const QVariantList &sections, const QString &id) {
		for (const QVariant &sectionValue : sections) {
			const QVariantMap section = sectionValue.toMap();
			for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("id")).toString() == id) {
					return field;
				}
			}
		}
		return QVariantMap();
	};
	auto findSettingsFieldByLabel = [](const QVariantList &sections, const QString &label) {
		for (const QVariant &sectionValue : sections) {
			const QVariantMap section = sectionValue.toMap();
			for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("label")).toString() == label) {
					return field;
				}
			}
		}
		return QVariantMap();
	};
	auto settingsPageSections = [&settings](const QString &page) {
		ModernSettingsController pageController;
		pageController.open(settings, page);
		return pageController.state().value(QStringLiteral("sections")).toList();
	};

	ModernSettingsController keyController;
	keyController.open(settings, QStringLiteral("keys"));
	QCOMPARE(keyController.activePage(), QStringLiteral("keys"));
	QVariantList keySections = keyController.state().value(QStringLiteral("sections")).toList();
	const QVariantMap globalShortcutsField =
		findSettingsFieldById(keySections, QStringLiteral("keys.globalShortcuts"));
	QCOMPARE(globalShortcutsField.value(QStringLiteral("type")).toString(), QStringLiteral("checkbox"));
	const QVariantMap shortcutEditorField =
		findSettingsFieldById(keySections, QStringLiteral("keys.shortcuts"));
	QCOMPARE(shortcutEditorField.value(QStringLiteral("type")).toString(), QStringLiteral("shortcutEditor"));
	keyController.updateField(QStringLiteral("keys.globalShortcuts"), true);
	QCOMPARE(keyController.draft().bShortcutEnable, true);

	ModernSettingsController aboutController;
	aboutController.open(settings, QStringLiteral("about"));
	QCOMPARE(aboutController.activePage(), QStringLiteral("about"));
	const QVariantList aboutSections = aboutController.state().value(QStringLiteral("sections")).toList();
	QCOMPARE(findSettingsFieldByLabel(aboutSections, QStringLiteral("Version")).value(QStringLiteral("type")).toString(),
			 QStringLiteral("readonly"));
	QCOMPARE(findSettingsFieldByLabel(aboutSections, QStringLiteral("Qt version"))
				 .value(QStringLiteral("type"))
				 .toString(),
			 QStringLiteral("readonly"));
	QCOMPARE(findSettingsFieldById(aboutSections, QStringLiteral("about.openMumble"))
				 .value(QStringLiteral("actionId"))
				 .toString(),
			 QStringLiteral("about.openMumble"));
	QCOMPARE(findSettingsFieldById(aboutSections, QStringLiteral("about.openQt"))
				 .value(QStringLiteral("actionId"))
				 .toString(),
			 QStringLiteral("about.openQt"));
	const QString tickerEnabledFieldID = QStringLiteral("look.modernTickerBannerEnabled");
	const QString tickerScrollFieldID  = QStringLiteral("look.modernTickerAlwaysScroll");
	const QVariantMap defaultTickerEnabledField =
		findSettingsFieldById(settingsPageSections(QStringLiteral("look")), tickerEnabledFieldID);
	const QVariantMap defaultTickerScrollField =
		findSettingsFieldById(settingsPageSections(QStringLiteral("look")), tickerScrollFieldID);
	QVERIFY(!defaultTickerEnabledField.isEmpty());
	QVERIFY(!defaultTickerScrollField.isEmpty());
	QCOMPARE(defaultTickerEnabledField.value(QStringLiteral("value")).toBool(), false);
	QCOMPARE(defaultTickerScrollField.value(QStringLiteral("value")).toBool(), true);
	QCOMPARE(defaultTickerScrollField.value(QStringLiteral("enabled"), true).toBool(), false);
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("ui")), tickerEnabledFieldID).isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("ui")), tickerScrollFieldID).isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("messages")), tickerEnabledFieldID).isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("messages")), tickerScrollFieldID).isEmpty());
	const QVariantMap reconnectLastChannelField =
		findSettingsFieldById(settingsPageSections(QStringLiteral("network")),
							  QStringLiteral("network.reconnectToLastChannel"));
	QCOMPARE(reconnectLastChannelField.value(QStringLiteral("type")).toString(), QStringLiteral("checkbox"));
	QCOMPARE(reconnectLastChannelField.value(QStringLiteral("value")).toBool(), false);

	ModernSettingsController tweakController;
	tweakController.open(settings, QStringLiteral("look"));
	const QVariantList initialLookSections = tweakController.state().value(QStringLiteral("sections")).toList();
	const QVariantMap themeField =
		findSettingsFieldById(initialLookSections, QStringLiteral("look.modernTheme"));
	QCOMPARE(themeField.value(QStringLiteral("presentation")).toString(), QStringLiteral("themeGrid"));
	const QVariantList themeOptions = themeField.value(QStringLiteral("options")).toList();
	QVERIFY(themeOptions.size() >= Mumble::ModernTheme::builtInThemeIds().size());
	for (const QString &themeID : Mumble::ModernTheme::builtInThemeIds()) {
		const auto optionIt = std::find_if(themeOptions.cbegin(), themeOptions.cend(), [&themeID](const QVariant &value) {
			return value.toMap().value(QStringLiteral("value")).toString() == themeID;
		});
		QVERIFY2(optionIt != themeOptions.cend(), qPrintable(QStringLiteral("Missing theme preview for %1").arg(themeID)));
		const QVariantMap preview = optionIt->toMap().value(QStringLiteral("preview")).toMap();
		for (const QString &token : { QStringLiteral("shell"), QStringLiteral("rail"), QStringLiteral("panel"),
									 QStringLiteral("surface"), QStringLiteral("border"), QStringLiteral("text"),
									 QStringLiteral("textMuted"), QStringLiteral("accent"), QStringLiteral("danger"),
									 QStringLiteral("success"), QStringLiteral("warning") }) {
			QVERIFY2(!preview.value(token).toString().isEmpty(),
					 qPrintable(QStringLiteral("Theme %1 is missing preview token %2").arg(themeID, token)));
		}
	}
	const QVariantMap accentField =
		findSettingsFieldById(initialLookSections, QStringLiteral("look.modernAccent"));
	QCOMPARE(accentField.value(QStringLiteral("presentation")).toString(), QStringLiteral("accentGrid"));
	const QVariantList accentOptions = accentField.value(QStringLiteral("options")).toList();
	QCOMPARE(accentOptions.size(), 7);
	for (const QVariant &accentOptionValue : accentOptions) {
		const QVariantMap accentOption = accentOptionValue.toMap();
		QVERIFY(!accentOption.value(QStringLiteral("swatch")).toMap()
					 .value(QStringLiteral("accent"))
					 .toString()
					 .isEmpty());
		const QString accentID = accentOption.value(QStringLiteral("value")).toString();
		if (accentID != QLatin1String("auto") && accentID != Mumble::ModernTheme::customAccentId()) {
			QCOMPARE(QColor(accentOption.value(QStringLiteral("swatch")).toMap()
							.value(QStringLiteral("accent"))
							.toString()),
					 Mumble::ModernTheme::accentColorOverride(accentID));
		}
	}
	QVERIFY(accentOptions.constFirst().toMap().value(QStringLiteral("automatic")).toBool());
	const QVariantMap reloadThemesField =
		findSettingsFieldById(initialLookSections, QStringLiteral("look.modernThemesReload"));
	QCOMPARE(reloadThemesField.value(QStringLiteral("actionId")).toString(),
			 QStringLiteral("look.reloadModernThemes"));
	QVERIFY(tweakController.invokeAction(QStringLiteral("look.reloadModernThemes"), QVariantMap()).stateChanged);
	tweakController.updateField(QStringLiteral("look.modernTheme"), QStringLiteral("latte"));
	tweakController.updateField(QStringLiteral("look.modernDensity"), QStringLiteral("compact"));
	tweakController.updateField(QStringLiteral("look.modernClassicUserIcons"), true);
	tweakController.updateField(QStringLiteral("look.modernRailSide"), QStringLiteral("left"));
	tweakController.updateField(QStringLiteral("look.modernAccent"), QStringLiteral("rose"));
	tweakController.updateField(QStringLiteral("look.modernTickerBannerEnabled"), false);
	tweakController.updateField(QStringLiteral("look.modernTickerAlwaysScroll"), true);
	const QVariantMap uiTweaks = tweakController.state().value(QStringLiteral("uiTweaks")).toMap();
	const QVariantMap disabledTickerScrollField =
		findSettingsFieldById(tweakController.state().value(QStringLiteral("sections")).toList(), tickerScrollFieldID);
	QCOMPARE(uiTweaks.value(QStringLiteral("theme")).toString(), QStringLiteral("latte"));
	QCOMPARE(uiTweaks.value(QStringLiteral("density")).toString(), QStringLiteral("compact"));
	QCOMPARE(uiTweaks.value(QStringLiteral("userIcons")).toString(), QStringLiteral("classic"));
	QCOMPARE(uiTweaks.value(QStringLiteral("classicUserIcons")).toBool(), true);
	QCOMPARE(uiTweaks.value(QStringLiteral("railSide")).toString(), QStringLiteral("left"));
	QCOMPARE(uiTweaks.value(QStringLiteral("accent")).toString(), QStringLiteral("rose"));
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerBannerEnabled")).toBool(), false);
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerBannerAlwaysScroll")).toBool(), true);
	QCOMPARE(disabledTickerScrollField.value(QStringLiteral("enabled"), true).toBool(), false);
	QCOMPARE(uiTweaks.value(QStringLiteral("accentDetails")).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("rose"));

	tweakController.updateField(QStringLiteral("look.modernAccent"), QStringLiteral("custom"));
	tweakController.updateField(QStringLiteral("look.modernCustomAccent"), QStringLiteral("#aabbcc"));
	tweakController.updateField(QStringLiteral("look.modernCustomAccentStrength"), 75);
	const QVariantMap customUiTweaks = tweakController.state().value(QStringLiteral("uiTweaks")).toMap();
	const QVariantMap customAccentField =
		findSettingsFieldById(tweakController.state().value(QStringLiteral("sections")).toList(),
							  QStringLiteral("look.modernCustomAccent"));
	const QVariantMap customAccentTokens = customUiTweaks.value(QStringLiteral("themeTokens")).toMap();
	QCOMPARE(customUiTweaks.value(QStringLiteral("accent")).toString(), QStringLiteral("custom"));
	QCOMPARE(customUiTweaks.value(QStringLiteral("accentDetails")).toMap().value(QStringLiteral("color")).toString(),
			 QStringLiteral("#aabbcc"));
	QCOMPARE(customUiTweaks.value(QStringLiteral("accentDetails")).toMap().value(QStringLiteral("strength")).toInt(),
			 75);
	QCOMPARE(customAccentField.value(QStringLiteral("type")).toString(), QStringLiteral("color"));
	QCOMPARE(customAccentTokens.value(QStringLiteral("--theme-accent-custom")).toString(), QStringLiteral("#aabbcc"));
	QCOMPARE(customAccentTokens.value(QStringLiteral("--theme-accent-custom-glow")).toString(),
			 QStringLiteral("rgba(170, 187, 204, 0.136)"));

	controller.updateField(QStringLiteral("network.autoReconnect"), true);
	controller.updateField(QStringLiteral("network.reconnectToLastChannel"), true);
	ModernSettingsController::ActionResult result = controller.invokeAction(QStringLiteral("ok"), QVariantMap());

	QVERIFY(result.settingsToApply.has_value());
	QCOMPARE(result.settingsToApply->modernLayoutPolicy, Settings::ModernLayoutForced);
	QCOMPARE(result.settingsToApply->wlWindowLayout, Settings::LayoutModern);
	QCOMPARE(result.settingsToApply->bReconnect, true);
	QCOMPARE(result.settingsToApply->bReconnectToLastChannel, true);
	QCOMPARE(result.accepted, true);
	QCOMPARE(result.closeDialog, true);

	controller.open(settings, QStringLiteral("AudioOutput"));
	QCOMPARE(controller.activePage(), QStringLiteral("audioOutput"));
	controller.updateField(QStringLiteral("audio.externalApplicationsVolume"), 25);
	controller.updateField(QStringLiteral("audio.positional"), true);
	ModernSettingsController::ActionResult audioOutputResult =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());

	QVERIFY(audioOutputResult.settingsToApply.has_value());
	QVERIFY(qFuzzyCompare(audioOutputResult.settingsToApply->fOtherVolume, 0.75f));
	QCOMPARE(audioOutputResult.settingsToApply->bPositionalAudio, true);
	QCOMPARE(audioOutputResult.accepted, true);
	QCOMPARE(audioOutputResult.closeDialog, false);

	Settings audioInputSettings = settings;
	audioInputSettings.atTransmit      = Settings::Continuous;
	audioInputSettings.vsVAD           = Settings::SignalToNoise;
	audioInputSettings.noiseCancelMode = Settings::NoiseCancelOff;
	controller.open(audioInputSettings, QStringLiteral("AudioInput"));
	QCOMPARE(controller.activePage(), QStringLiteral("audioInput"));
	const QVariantList audioInputSections = controller.state().value(QStringLiteral("sections")).toList();
	bool foundInputSignalSection          = false;
	bool foundInputMeterInVoiceActivation = false;
	bool foundDetectionMethodField        = false;
	bool foundInputGateField              = false;
	bool foundHiddenNeuralCleanupFields   = false;
	bool foundNoiseCancelOptionHints      = false;
	for (const QVariant &sectionValue : audioInputSections) {
		const QVariantMap section = sectionValue.toMap();
		if (section.value(QStringLiteral("title")).toString() == QLatin1String("Input signal")) {
			foundInputSignalSection = true;
		}
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (section.value(QStringLiteral("title")).toString() == QLatin1String("Voice activation")
				&& field.value(QStringLiteral("id")).toString() == QLatin1String("audio.inputMeter")) {
				foundInputMeterInVoiceActivation =
					field.value(QStringLiteral("type")).toString() == QLatin1String("voiceMeter")
					&& field.value(QStringLiteral("calibrationActionId")).toString()
						   == QLatin1String("finishAudioSetupWizard")
					&& field.value(QStringLiteral("calibrationLabel")).toString()
						   == QLatin1String("Use recommended settings")
					&& field.value(QStringLiteral("calibrationState")).toString() == QLatin1String("idle")
					&& field.contains(QStringLiteral("maxAmplification"))
					&& field.contains(QStringLiteral("noiseCancelMode"))
					&& field.contains(QStringLiteral("inputGateMode"))
					&& field.contains(QStringLiteral("neuralCleanupAvailable"))
					&& field.value(QStringLiteral("recommendedVadSource")).toInt() == Settings::Hybrid
					&& field.value(QStringLiteral("recommendedInputGateMode")).toInt() == Settings::InputGateBalanced
					&& field.contains(QStringLiteral("recommendedNoiseCancelMode"))
					&& field.contains(QStringLiteral("recommendedMaxAmplification"))
					&& !field.value(QStringLiteral("calibrationStatusText")).toString().trimmed().isEmpty();
			}
			if (section.value(QStringLiteral("title")).toString() == QLatin1String("Voice activation")
				&& field.value(QStringLiteral("id")).toString() == QLatin1String("audio.vadSource")) {
				const QVariantList options = field.value(QStringLiteral("options")).toList();
				bool optionsHaveHints       = options.size() == 3;
				for (const QVariant &optionValue : options) {
					const QVariantMap option = optionValue.toMap();
					optionsHaveHints =
						optionsHaveHints && !option.value(QStringLiteral("hint")).toString().trimmed().isEmpty();
				}
				foundDetectionMethodField =
					field.value(QStringLiteral("label")).toString() == QLatin1String("Detection method")
					&& !field.value(QStringLiteral("enabled")).toBool()
					&& field.value(QStringLiteral("value")).toInt() == Settings::SignalToNoise
					&& options.size() == 3
					&& options.at(0).toMap().value(QStringLiteral("label")).toString()
						   == QLatin1String("Speech probability")
					&& options.at(1).toMap().value(QStringLiteral("label")).toString() == QLatin1String("Speech + volume")
					&& options.at(2).toMap().value(QStringLiteral("label")).toString() == QLatin1String("Volume level")
					&& optionsHaveHints
					&& field.value(QStringLiteral("hint")).toString().contains(QLatin1String("Speech + volume"));
			}
			if (section.value(QStringLiteral("title")).toString() == QLatin1String("Voice activation")
				&& field.value(QStringLiteral("id")).toString() == QLatin1String("audio.inputGateMode")) {
				const QVariantList options = field.value(QStringLiteral("options")).toList();
				bool optionsHaveHints       = options.size() == 3;
				for (const QVariant &optionValue : options) {
					const QVariantMap option = optionValue.toMap();
					optionsHaveHints =
						optionsHaveHints && !option.value(QStringLiteral("hint")).toString().trimmed().isEmpty();
				}
				foundInputGateField =
					field.value(QStringLiteral("label")).toString() == QLatin1String("Input gate")
					&& !field.value(QStringLiteral("enabled")).toBool()
					&& field.value(QStringLiteral("value")).toInt() == Settings::InputGateOff && optionsHaveHints
					&& field.value(QStringLiteral("hint")).toString().contains(QLatin1String("original behavior"));
			}
			if (section.value(QStringLiteral("title")).toString() == QLatin1String("Audio processing")
				&& field.value(QStringLiteral("id")).toString() == QLatin1String("audio.noiseCancelMode")) {
				const QVariantList options = field.value(QStringLiteral("options")).toList();
				bool optionsHaveHints       = options.size() == 4;
				for (const QVariant &optionValue : options) {
					const QVariantMap option = optionValue.toMap();
					optionsHaveHints =
						optionsHaveHints && !option.value(QStringLiteral("hint")).toString().trimmed().isEmpty();
				}
				foundNoiseCancelOptionHints = optionsHaveHints;
			}
			if (section.value(QStringLiteral("title")).toString() == QLatin1String("Audio processing")
				&& field.value(QStringLiteral("id")).toString() == QLatin1String("audio.noiseCancelBackend")) {
				foundHiddenNeuralCleanupFields = field.value(QStringLiteral("type")).toString() == QLatin1String("hidden");
			}
		}
	}
	QVERIFY(!foundInputSignalSection);
	QVERIFY(foundInputMeterInVoiceActivation);
	QVERIFY(foundDetectionMethodField);
	QVERIFY(foundInputGateField);
	QVERIFY(foundHiddenNeuralCleanupFields);
	QVERIFY(foundNoiseCancelOptionHints);

	controller.updateField(QStringLiteral("audio.transmitMode"), Settings::VAD);
	QCOMPARE(controller.draft().atTransmit, Settings::VAD);
	QCOMPARE(controller.draft().vsVAD, Settings::SignalToNoise);

	controller.updateField(QStringLiteral("audio.vadSource"), Settings::Hybrid);
	QCOMPARE(controller.draft().vsVAD, Settings::Hybrid);
	controller.updateField(QStringLiteral("audio.inputGateMode"), Settings::InputGateStrict);
	QCOMPARE(controller.draft().inputGateMode, Settings::InputGateStrict);

	ModernSettingsController::ActionResult autoSetResult =
		controller.invokeAction(QStringLiteral("finishAudioSetupWizard"),
								QVariantMap { { QStringLiteral("silenceThreshold"), 22 },
											  { QStringLiteral("speechThreshold"), 48 },
											  { QStringLiteral("vadSource"), Settings::SignalToNoise },
											  { QStringLiteral("voiceHold"), 35 },
											  { QStringLiteral("maxAmplification"), 9000 },
											  { QStringLiteral("noiseCancelMode"), Settings::NoiseCancelSpeex },
											  { QStringLiteral("inputGateMode"), Settings::InputGateBalanced },
											  { QStringLiteral("speexNoiseStrength"), 34 } });
	QCOMPARE(autoSetResult.closeDialog, false);
	QVERIFY(!autoSetResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().atTransmit, Settings::VAD);
	QCOMPARE(controller.draft().vsVAD, Settings::SignalToNoise);
	QCOMPARE(controller.draft().iVoiceHold, 35);
	QCOMPARE(controller.draft().iMinLoudness, 11000);
	QCOMPARE(controller.draft().noiseCancelMode, Settings::NoiseCancelSpeex);
	QCOMPARE(controller.draft().inputGateMode, Settings::InputGateBalanced);
	QCOMPARE(controller.draft().iSpeexNoiseCancelStrength, -34);
	QVERIFY(qFuzzyCompare(controller.draft().fVADmin, 0.22f));
	QVERIFY(qFuzzyCompare(controller.draft().fVADmax, 0.48f));

	ModernSettingsController::ActionResult replayResult =
		controller.invokeAction(QStringLiteral("startVoiceReplay"),
								QVariantMap { { QStringLiteral("mode"), QStringLiteral("server") } });
	QVERIFY(replayResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().lmLoopMode, Settings::Server);
	QCOMPARE(controller.draft().dMaxPacketDelay, 0.0f);
	QCOMPARE(controller.draft().dPacketLoss, 0.0f);

	ModernSettingsController::ActionResult stopReplayResult =
		controller.invokeAction(QStringLiteral("stopVoiceReplay"), QVariantMap());
	QVERIFY(stopReplayResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().lmLoopMode, Settings::None);

	bool foundKbitBitrate = false;
	for (const QVariant &sectionValue : audioInputSections) {
		const QVariantMap section = sectionValue.toMap();
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == QLatin1String("audio.quality")) {
				foundKbitBitrate = field.value(QStringLiteral("value")).toInt() == 72
								   && field.value(QStringLiteral("suffix")).toString() == QLatin1String(" kbit/s");
			}
		}
	}
	QVERIFY(foundKbitBitrate);

	controller.updateField(QStringLiteral("audio.quality"), 72);
	controller.updateField(QStringLiteral("audio.vadMin"), 35);
	controller.updateField(QStringLiteral("audio.vadMax"), 70);
	ModernSettingsController::ActionResult audioInputResult =
		controller.invokeAction(QStringLiteral("ok"), QVariantMap());

	QVERIFY(audioInputResult.settingsToApply.has_value());
	QCOMPARE(audioInputResult.settingsToApply->iQuality, 72000);
	QVERIFY(qFuzzyCompare(audioInputResult.settingsToApply->fVADmin, 0.35f));
	QVERIFY(qFuzzyCompare(audioInputResult.settingsToApply->fVADmax, 0.70f));
	QCOMPARE(audioInputResult.accepted, true);
	QCOMPARE(audioInputResult.closeDialog, true);
}

void TestModernDialogControllers::settingsAppearanceAutoAccentTracksDraftTheme() {
	const auto findField = [](const QVariantMap &state, const QString &fieldID) {
		for (const QVariant &sectionValue : state.value(QStringLiteral("sections")).toList()) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("id")).toString() == fieldID) {
					return field;
				}
			}
		}
		return QVariantMap();
	};
	const auto findOption = [](const QVariantMap &field, const QString &optionID) {
		for (const QVariant &optionValue : field.value(QStringLiteral("options")).toList()) {
			const QVariantMap option = optionValue.toMap();
			if (option.value(QStringLiteral("value")).toString() == optionID) {
				return option;
			}
		}
		return QVariantMap();
	};
	const auto appearanceColors = [&findField, &findOption](const QVariantMap &state, const QString &themeID) {
		const QVariantMap themeField = findField(state, QStringLiteral("look.modernTheme"));
		const QVariantMap accentField = findField(state, QStringLiteral("look.modernAccent"));
		const QVariantMap themeOption = findOption(themeField, themeID);
		const QVariantMap automaticOption = findOption(accentField, QStringLiteral("auto"));
		return qMakePair(
			QColor(themeOption.value(QStringLiteral("preview")).toMap().value(QStringLiteral("accent")).toString()),
			QColor(automaticOption.value(QStringLiteral("swatch")).toMap().value(QStringLiteral("accent")).toString()));
	};

	Settings settings;
	settings.qsModernShellTheme = QStringLiteral("nord");
	settings.qsModernShellAccent = QStringLiteral("auto");
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("look"));

	QPair< QColor, QColor > colors = appearanceColors(controller.state(), QStringLiteral("nord"));
	QVERIFY(colors.first.isValid());
	QCOMPARE(colors.second, colors.first);
	const QColor nordAccent = colors.second;

	controller.updateField(QStringLiteral("look.modernTheme"), QStringLiteral("gruvbox"));
	colors = appearanceColors(controller.state(), QStringLiteral("gruvbox"));
	QVERIFY(colors.first.isValid());
	QCOMPARE(colors.second, colors.first);
	QVERIFY(colors.second != nordAccent);
	QCOMPARE(controller.draft().qsModernShellAccent, QStringLiteral("auto"));

	controller.updateField(QStringLiteral("look.modernAccent"), QStringLiteral("violet"));
	controller.updateField(QStringLiteral("look.modernTheme"), QStringLiteral("latte"));
	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("uiTweaks")).toMap().value(QStringLiteral("accent")).toString(),
		QStringLiteral("violet"));
	const QVariantMap violetOption = findOption(
		findField(state, QStringLiteral("look.modernAccent")), QStringLiteral("violet"));
	QCOMPARE(QColor(violetOption.value(QStringLiteral("swatch")).toMap()
		.value(QStringLiteral("accent")).toString()),
		Mumble::ModernTheme::accentColorOverride(QStringLiteral("violet")));

	controller.updateField(QStringLiteral("look.modernAccent"), QStringLiteral("custom"));
	controller.updateField(QStringLiteral("look.modernCustomAccent"), QStringLiteral("#3366cc"));
	controller.updateField(QStringLiteral("look.modernTheme"), QStringLiteral("dark"));
	state = controller.state();
	const QVariantMap customDetails = state.value(QStringLiteral("uiTweaks")).toMap()
		.value(QStringLiteral("accentDetails")).toMap();
	QCOMPARE(customDetails.value(QStringLiteral("id")).toString(), QStringLiteral("custom"));
	QCOMPARE(customDetails.value(QStringLiteral("color")).toString(), QStringLiteral("#3366cc"));
}

void TestModernDialogControllers::settingsAppearancePreviewCommitsAndRollsBack() {
	Settings settings;
	settings.qsModernShellTheme = QStringLiteral("dark");
	settings.qsModernShellDensity = QStringLiteral("comfortable");
	settings.qsModernShellAccent = QStringLiteral("auto");
	settings.qsModernShellCustomAccent = QStringLiteral("#112233");
	settings.iModernShellCustomAccentStrength = 40;

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("look"));
	const QVariantList actions = controller.state().value(QStringLiteral("actions")).toList();
	QCOMPARE(actions.size(), 3);
	QCOMPARE(actions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cancel"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("apply"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Apply"));
	QCOMPARE(actions.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ok"));
	const auto previewField = [&controller](const QString &fieldID, const QVariant &value) {
		return controller.invokeAction(QStringLiteral("look.previewAppearance"),
			QVariantMap { { QStringLiteral("fieldId"), fieldID }, { QStringLiteral("value"), value } });
	};

	ModernSettingsController::ActionResult preview =
		previewField(QStringLiteral("look.modernTheme"), QStringLiteral("nord"));
	QVERIFY(preview.stateChanged);
	QVERIFY(!preview.settingsToApply.has_value());
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->theme, QStringLiteral("nord"));
	QCOMPARE(preview.appearanceToPreview->density, QStringLiteral("comfortable"));
	QCOMPARE(preview.appearanceToPreview->accent, QStringLiteral("auto"));
	QCOMPARE(controller.draft().qsModernShellTheme, QStringLiteral("nord"));

	preview = previewField(QStringLiteral("look.modernAccent"), QStringLiteral("custom"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->theme, QStringLiteral("nord"));
	QCOMPARE(preview.appearanceToPreview->accent, QStringLiteral("custom"));
	QCOMPARE(preview.appearanceToPreview->customAccent, QStringLiteral("#112233"));
	QCOMPARE(preview.appearanceToPreview->customAccentStrength, 40);

	preview = previewField(QStringLiteral("look.modernCustomAccent"), QStringLiteral("#3366cc"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->customAccent, QStringLiteral("#3366cc"));
	preview = previewField(QStringLiteral("look.modernCustomAccentStrength"), 75);
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->customAccentStrength, 75);
	preview = previewField(QStringLiteral("look.modernTheme"), QStringLiteral("gruvbox"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->theme, QStringLiteral("gruvbox"));
	QCOMPARE(preview.appearanceToPreview->accent, QStringLiteral("custom"));
	QCOMPARE(preview.appearanceToPreview->customAccent, QStringLiteral("#3366cc"));

	ModernSettingsController::ActionResult apply =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(apply.settingsToApply.has_value());
	QVERIFY(!apply.appearanceToPreview.has_value());
	QVERIFY(apply.accepted);
	QVERIFY(!apply.closeDialog);
	QCOMPARE(apply.settingsToApply->qsModernShellTheme, QStringLiteral("gruvbox"));
	QCOMPARE(apply.settingsToApply->qsModernShellAccent, QStringLiteral("custom"));
	QCOMPARE(apply.settingsToApply->qsModernShellCustomAccent, QStringLiteral("#3366cc"));
	QCOMPARE(apply.settingsToApply->iModernShellCustomAccentStrength, 75);

	preview = previewField(QStringLiteral("look.modernTheme"), QStringLiteral("latte"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->theme, QStringLiteral("latte"));
	ModernSettingsController::ActionResult cancel =
		controller.invokeAction(QStringLiteral("cancel"), QVariantMap());
	QVERIFY(cancel.closeDialog);
	QVERIFY(!cancel.settingsToApply.has_value());
	QVERIFY(cancel.appearanceToPreview.has_value());
	QCOMPARE(cancel.appearanceToPreview->theme, QStringLiteral("gruvbox"));
	QCOMPARE(cancel.appearanceToPreview->accent, QStringLiteral("custom"));
	QCOMPARE(cancel.appearanceToPreview->customAccent, QStringLiteral("#3366cc"));
	QCOMPARE(cancel.appearanceToPreview->customAccentStrength, 75);

	controller.open(*apply.settingsToApply, QStringLiteral("look"));
	preview = previewField(QStringLiteral("look.modernDensity"), QStringLiteral("compact"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->density, QStringLiteral("compact"));
	const ModernSettingsController::ActionResult reset =
		controller.invokeAction(QStringLiteral("reset"), QVariantMap());
	QVERIFY(reset.appearanceToPreview.has_value());
	QCOMPARE(reset.appearanceToPreview->theme, QStringLiteral("gruvbox"));
	QCOMPARE(reset.appearanceToPreview->density, QStringLiteral("comfortable"));
	QCOMPARE(controller.draft().qsModernShellDensity, QStringLiteral("comfortable"));

	preview = previewField(QStringLiteral("look.modernTheme"), QStringLiteral("latte"));
	QVERIFY(preview.appearanceToPreview.has_value());
	const ModernSettingsController::ActionResult done =
		controller.invokeAction(QStringLiteral("ok"), QVariantMap());
	QVERIFY(done.settingsToApply.has_value());
	QVERIFY(done.accepted);
	QVERIFY(done.closeDialog);
	QVERIFY(!done.appearanceToPreview.has_value());
	QCOMPARE(done.settingsToApply->qsModernShellTheme, QStringLiteral("latte"));
	QCOMPARE(done.settingsToApply->qsModernShellDensity, QStringLiteral("comfortable"));
	QCOMPARE(done.settingsToApply->qsModernShellAccent, QStringLiteral("custom"));
	QCOMPARE(done.settingsToApply->qsModernShellCustomAccent, QStringLiteral("#3366cc"));

	const ModernSettingsController::ActionResult ignored =
		previewField(QStringLiteral("network.autoReconnect"), true);
	QVERIFY(!ignored.stateChanged);
	QVERIFY(!ignored.appearanceToPreview.has_value());

	ModernDialogController dialogController;
	dialogController.openSettings(settings, QStringLiteral("look"));
	ModernDialogController::ActionResult dialogPreview = dialogController.invokeAction(
		QStringLiteral("settings"), QStringLiteral("look.previewAppearance"),
		QVariantMap { { QStringLiteral("fieldId"), QStringLiteral("look.modernTheme") },
			{ QStringLiteral("value"), QStringLiteral("nord") } });
	QVERIFY(dialogPreview.appearanceToPreview.has_value());
	QCOMPARE(dialogPreview.appearanceToPreview->theme, QStringLiteral("nord"));
	QCOMPARE(dialogController.activeDialogID(), QStringLiteral("settings"));
	const ModernDialogController::ActionResult dialogCancel = dialogController.invokeAction(
		QStringLiteral("settings"), QStringLiteral("cancel"), QVariantMap());
	QVERIFY(dialogCancel.closeDialog);
	QVERIFY(dialogCancel.appearanceToPreview.has_value());
	QCOMPARE(dialogCancel.appearanceToPreview->theme, QStringLiteral("dark"));
	QCOMPARE(dialogCancel.appearanceToPreview->accent, QStringLiteral("auto"));
	QVERIFY(dialogController.activeDialogID().isEmpty());
}

void TestModernDialogControllers::settingsControllerEditsShortcutDataAndTargets() {
	QVERIFY(GlobalShortcutEngine::engine == nullptr);
	GlobalShortcutEngine::engine = new TestShortcutEngine();

	{
		ShortcutTarget defaultTarget;
		defaultTarget.bUsers   = false;
		defaultTarget.iChannel = SHORTCUT_TARGET_CURRENT;
		GlobalShortcut targetDefinition(nullptr, 501, QStringLiteral("Whisper/Shout"),
										QVariant::fromValue(defaultTarget));
		GlobalShortcut toggleDefinition(nullptr, 502, QStringLiteral("Mute Self"), 0);
		GlobalShortcut textDefinition(nullptr, 503, QStringLiteral("Send Text Message"), QString());
		GlobalShortcut channelDefinition(nullptr, 504, QStringLiteral("Join Channel"),
										 QVariant::fromValue(ChannelTarget(Mumble::ROOT_CHANNEL_ID)));

		Settings settings;
		Shortcut targetShortcut;
		targetShortcut.iIndex = 501;
		ShortcutTarget target;
		target.bUsers         = false;
		target.iChannel       = SHORTCUT_TARGET_CURRENT;
		target.qsGroup        = QStringLiteral("admins");
		target.bChildren      = true;
		targetShortcut.qvData = QVariant::fromValue(target);

		Shortcut toggleShortcut;
		toggleShortcut.iIndex = 502;
		toggleShortcut.qvData = 0;
		Shortcut textShortcut;
		textShortcut.iIndex = 503;
		textShortcut.qvData = QStringLiteral("hello");
		Shortcut channelShortcut;
		channelShortcut.iIndex = 504;
		channelShortcut.qvData = QVariant::fromValue(ChannelTarget(Mumble::ROOT_CHANNEL_ID));
		settings.qlShortcuts   = { targetShortcut, toggleShortcut, textShortcut, channelShortcut };

		ModernSettingsController controller;
		controller.open(settings, QStringLiteral("keys"));

		auto shortcutField = [&controller]() {
			const QVariantList sections = controller.state().value(QStringLiteral("sections")).toList();
			for (const QVariant &sectionValue : sections) {
				for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
					const QVariantMap field = fieldValue.toMap();
					if (field.value(QStringLiteral("id")).toString() == QLatin1String("keys.shortcuts")) {
						return field;
					}
				}
			}
			return QVariantMap();
		};

		const QVariantMap field = shortcutField();
		QCOMPARE(field.value(QStringLiteral("type")).toString(), QStringLiteral("shortcutEditor"));
		QCOMPARE(field.value(QStringLiteral("targetModeOptions")).toList().size(), 3);
		QVERIFY(field.value(QStringLiteral("targetChannelOptions")).toList().size() >= 19);
		QCOMPARE(field.value(QStringLiteral("toggleOptions")).toList().size(), 3);
		const QVariantList rows = field.value(QStringLiteral("rows")).toList();
		QCOMPARE(rows.size(), 4);
		QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("target"));
		QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dataEditable")).toBool(), true);
		const QVariantMap targetDetail = rows.at(0).toMap().value(QStringLiteral("target")).toMap();
		QCOMPARE(targetDetail.value(QStringLiteral("mode")).toString(), QStringLiteral("channel"));
		QCOMPARE(targetDetail.value(QStringLiteral("channelId")).toInt(), SHORTCUT_TARGET_CURRENT);
		QCOMPARE(targetDetail.value(QStringLiteral("group")).toString(), QStringLiteral("admins"));
		QCOMPARE(targetDetail.value(QStringLiteral("children")).toBool(), true);
		QCOMPARE(rows.at(1).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("toggle"));
		QCOMPARE(rows.at(2).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("text"));
		QCOMPARE(rows.at(3).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("channel"));

		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("mode") },
											 { QStringLiteral("mode"), QStringLiteral("users") } });
		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("addUser") },
											 { QStringLiteral("hash"), QStringLiteral("hash-alice") } });
		ShortcutTarget editedTarget = controller.draft().qlShortcuts.at(0).qvData.value< ShortcutTarget >();
		QCOMPARE(editedTarget.bUsers, true);
		QCOMPARE(editedTarget.bCurrentSelection, false);
		QCOMPARE(editedTarget.qlUsers, QStringList{ QStringLiteral("hash-alice") });

		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("removeUser") },
											 { QStringLiteral("hash"), QStringLiteral("hash-alice") } });
		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("mode") },
											 { QStringLiteral("mode"), QStringLiteral("channel") } });
		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("channel") },
											 { QStringLiteral("channelId"), SHORTCUT_TARGET_ROOT } });
		controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
								QVariantMap{ { QStringLiteral("index"), 0 },
											 { QStringLiteral("targetAction"), QStringLiteral("group") },
											 { QStringLiteral("group"), QStringLiteral("moderators") } });
		for (const QString &flag :
			 { QStringLiteral("links"), QStringLiteral("children"), QStringLiteral("forceCenter") }) {
			controller.invokeAction(QStringLiteral("keys.shortcutTarget"),
									QVariantMap{ { QStringLiteral("index"), 0 },
												 { QStringLiteral("targetAction"), flag },
												 { QStringLiteral("enabled"), true } });
		}
		editedTarget = controller.draft().qlShortcuts.at(0).qvData.value< ShortcutTarget >();
		QCOMPARE(editedTarget.bUsers, false);
		QCOMPARE(editedTarget.iChannel, SHORTCUT_TARGET_ROOT);
		QCOMPARE(editedTarget.qsGroup, QStringLiteral("moderators"));
		QCOMPARE(editedTarget.qlUsers.size(), 0);
		QVERIFY(editedTarget.bLinks);
		QVERIFY(editedTarget.bChildren);
		QVERIFY(editedTarget.bForceCenter);

		controller.invokeAction(QStringLiteral("keys.shortcutData"),
								QVariantMap{ { QStringLiteral("index"), 1 }, { QStringLiteral("value"), 1 } });
		controller.invokeAction(QStringLiteral("keys.shortcutData"),
								QVariantMap{ { QStringLiteral("index"), 2 },
											 { QStringLiteral("value"), QStringLiteral("updated message") } });
		controller.invokeAction(QStringLiteral("keys.shortcutData"),
								QVariantMap{ { QStringLiteral("index"), 3 }, { QStringLiteral("value"), 7 } });
		controller.invokeAction(QStringLiteral("keys.shortcutSuppress"),
								QVariantMap{ { QStringLiteral("index"), 0 }, { QStringLiteral("suppress"), true } });
		QCOMPARE(controller.draft().qlShortcuts.at(1).qvData.toInt(), 1);
		QCOMPARE(controller.draft().qlShortcuts.at(2).qvData.toString(), QStringLiteral("updated message"));
		QCOMPARE(controller.draft().qlShortcuts.at(3).qvData.value< ChannelTarget >().channelID, 7U);
		QCOMPARE(controller.draft().qlShortcuts.at(0).bSuppress, true);
	}

	QVERIFY(GlobalShortcutEngine::engine == nullptr);
}

void TestModernDialogControllers::settingsControllerReconcilesPluginRuntimeState() {
	Settings settings;
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("plugins"));

	QVERIFY(!controller.reconcilePluginLoadedState(QString(), true));
	QVERIFY(controller.reconcilePluginLoadedState(QStringLiteral(" C:/plugins/example.dll "), true));
	QCOMPARE(controller.draft().qhPluginSettings.size(), 1);
	PluginSetting setting = controller.draft().qhPluginSettings.constBegin().value();
	QCOMPARE(setting.path, QStringLiteral("C:/plugins/example.dll"));
	QVERIFY(setting.enabled);

	controller.updateField(QStringLiteral("plugins.runtimeLoaded"),
		QVariantMap { { QStringLiteral("path"), QStringLiteral("C:/plugins/example.dll") },
			{ QStringLiteral("loaded"), false } });
	setting = controller.draft().qhPluginSettings.constBegin().value();
	QVERIFY(!setting.enabled);

	controller.invokeAction(QStringLiteral("reset"), QVariantMap());
	setting = controller.draft().qhPluginSettings.constBegin().value();
	QVERIFY(!setting.enabled);
}

void TestModernDialogControllers::settingsControllerClampsAudioSetupPayload() {
	Settings settings;
	settings.vsVAD           = Settings::Amplitude;
	settings.noiseCancelMode = Settings::NoiseCancelOff;

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	ModernSettingsController::ActionResult clampedResult =
		controller.invokeAction(QStringLiteral("finishAudioSetupWizard"),
								QVariantMap { { QStringLiteral("silenceThreshold"), 95 },
											  { QStringLiteral("speechThreshold"), 10 },
											  { QStringLiteral("vadSource"), 999 },
											  { QStringLiteral("voiceHold"), 999 },
											  { QStringLiteral("maxAmplification"), -42 },
											  { QStringLiteral("noiseCancelMode"), 999 },
											  { QStringLiteral("inputGateMode"), 999 },
											  { QStringLiteral("speexNoiseStrength"), 999 } });
	QVERIFY(!clampedResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().vsVAD, Settings::Amplitude);
	QCOMPARE(controller.draft().iVoiceHold, 80);
	QCOMPARE(controller.draft().iMinLoudness, 20000);
	QCOMPARE(controller.draft().noiseCancelMode, Settings::NoiseCancelOff);
	QCOMPARE(controller.draft().inputGateMode, Settings::InputGateStrict);
	QCOMPARE(controller.draft().iSpeexNoiseCancelStrength, -100);
	QVERIFY(qFuzzyCompare(controller.draft().fVADmin, 0.10f));
	QVERIFY(qFuzzyCompare(controller.draft().fVADmax, 0.95f));

	controller.updateField(QStringLiteral("audio.maxAmplification"), 20000);
	QCOMPARE(controller.draft().iMinLoudness, 500);

	ModernSettingsController::ActionResult maxAmplificationResult =
		controller.invokeAction(QStringLiteral("finishAudioSetupWizard"),
								QVariantMap { { QStringLiteral("silenceThreshold"), 20 },
											  { QStringLiteral("speechThreshold"), 55 },
											  { QStringLiteral("maxAmplification"), 20000 } });
	QVERIFY(!maxAmplificationResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().iMinLoudness, 500);

	ModernSettingsController fallbackController;
	fallbackController.open(settings, QStringLiteral("AudioInput"));
	bool neuralCleanupAvailable = false;
	const QVariantList fallbackSections = fallbackController.state().value(QStringLiteral("sections")).toList();
	for (const QVariant &sectionValue : fallbackSections) {
		const QVariantMap section = sectionValue.toMap();
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == QLatin1String("audio.inputMeter")) {
				neuralCleanupAvailable = field.value(QStringLiteral("neuralCleanupAvailable")).toBool();
			}
		}
	}
	ModernSettingsController::ActionResult neuralResult =
		fallbackController.invokeAction(QStringLiteral("finishAudioSetupWizard"),
										QVariantMap { { QStringLiteral("silenceThreshold"), 20 },
													  { QStringLiteral("speechThreshold"), 55 },
													  { QStringLiteral("noiseCancelMode"), Settings::NoiseCancelRNN } });
	QVERIFY(!neuralResult.settingsToApply.has_value());
	QCOMPARE(fallbackController.draft().noiseCancelMode,
			 neuralCleanupAvailable ? Settings::NoiseCancelRNN : Settings::NoiseCancelSpeex);
}

void TestModernDialogControllers::audioInputVoiceActivityLevelUsesExpectedSignals() {
	QCOMPARE(AudioInput::voiceActivityLevelFor(Settings::Amplitude, -1.0f, 0.90f), 0.0f);
	QCOMPARE(AudioInput::voiceActivityLevelFor(Settings::Amplitude, 1.25f, 0.20f), 1.0f);
	QCOMPARE(AudioInput::voiceActivityLevelFor(Settings::SignalToNoise, 0.20f, 0.90f), 0.90f);
	QCOMPARE(AudioInput::voiceActivityLevelFor(Settings::Hybrid, 0.20f, 0.90f), 0.20f);

	const float quietSpeechLevel = AudioInput::voiceActivityLevelFor(Settings::SignalToNoise, 0.05f, 0.60f);
	QVERIFY(AudioInput::voiceActivityTriggers(quietSpeechLevel, 0.22f, 0.48f, false));
	QVERIFY(!AudioInput::voiceActivityTriggers(
		AudioInput::voiceActivityLevelFor(Settings::Hybrid, 0.05f, 0.60f), 0.22f, 0.48f, false));
	QVERIFY(AudioInput::voiceActivityTriggers(0.30f, 0.22f, 0.48f, true));
	QVERIFY(!AudioInput::voiceActivityTriggers(0.30f, 0.22f, 0.48f, false));
	QVERIFY(AudioInput::voiceActivityTriggers(0.90f, 0.80f, 0.20f, false));

	bool gateOpen  = false;
	int attack     = 0;
	int release    = 0;
	QVERIFY(AudioInput::inputGateAllowsSpeechFor(Settings::InputGateOff, true, 0.0f, 0.0f, gateOpen, attack, release));
	QVERIFY(!AudioInput::inputGateAllowsSpeechFor(Settings::InputGateBalanced, true, 0.05f, 0.80f, gateOpen, attack,
												  release));
	QVERIFY(AudioInput::inputGateAllowsSpeechFor(Settings::InputGateBalanced, true, 0.20f, 0.40f, gateOpen, attack,
												 release));
	QVERIFY(gateOpen);
	QVERIFY(AudioInput::inputGateAllowsSpeechFor(Settings::InputGateBalanced, false, 0.0f, 0.0f, gateOpen, attack,
												 release));

	gateOpen = false;
	attack   = 0;
	release  = 0;
	QVERIFY(!AudioInput::inputGateAllowsSpeechFor(Settings::InputGateStrict, true, 0.30f, 0.60f, gateOpen, attack,
												  release));
	QVERIFY(AudioInput::inputGateAllowsSpeechFor(Settings::InputGateStrict, true, 0.30f, 0.60f, gateOpen, attack,
												 release));
}

void TestModernDialogControllers::settingsControllerRollsBackVoiceReplayPreview() {
	Settings settings;
	settings.atTransmit = Settings::PushToTalk;
	settings.lmLoopMode = Settings::None;
	settings.noiseCancelMode = Settings::NoiseCancelOff;
	settings.dPacketLoss = 0.17f;
	settings.dMaxPacketDelay = 0.42f;

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("audioInput"));

	const ModernSettingsController::ActionResult recommendations =
		controller.invokeAction(QStringLiteral("finishAudioSetupWizard"),
			QVariantMap { { QStringLiteral("silenceThreshold"), 20 },
				{ QStringLiteral("speechThreshold"), 55 },
				{ QStringLiteral("vadSource"), Settings::Hybrid },
				{ QStringLiteral("noiseCancelMode"), Settings::NoiseCancelSpeex } });
	QVERIFY(!recommendations.settingsToApply.has_value());
	QCOMPARE(controller.draft().atTransmit, Settings::PushToTalk);
	QCOMPARE(controller.draft().noiseCancelMode, Settings::NoiseCancelSpeex);

	const ModernSettingsController::ActionResult replay =
		controller.invokeAction(QStringLiteral("startVoiceReplay"),
			QVariantMap { { QStringLiteral("mode"), QStringLiteral("server") } });
	QVERIFY(replay.settingsToApply.has_value());
	QVERIFY(!replay.announceApply);
	QCOMPARE(controller.draft().atTransmit, Settings::VAD);
	QCOMPARE(controller.draft().lmLoopMode, Settings::Server);

	const ModernSettingsController::ActionResult stop =
		controller.invokeAction(QStringLiteral("stopVoiceReplay"), {});
	QVERIFY(stop.settingsToApply.has_value());
	QVERIFY(!stop.announceApply);
	QCOMPARE(controller.draft().atTransmit, Settings::PushToTalk);
	QCOMPARE(controller.draft().lmLoopMode, Settings::None);
	QCOMPARE(controller.draft().dPacketLoss, 0.17f);
	QCOMPARE(controller.draft().dMaxPacketDelay, 0.42f);

	const ModernSettingsController::ActionResult cancel =
		controller.invokeAction(QStringLiteral("cancel"), {});
	QVERIFY(cancel.closeDialog);
	QVERIFY(cancel.settingsToApply.has_value());
	QVERIFY(!cancel.announceApply);
	QCOMPARE(cancel.settingsToApply->atTransmit, Settings::PushToTalk);
	QCOMPARE(cancel.settingsToApply->lmLoopMode, Settings::None);
	QCOMPARE(cancel.settingsToApply->noiseCancelMode, Settings::NoiseCancelOff);
}

void TestModernDialogControllers::audioInputVoiceActivitySnapshotIsBounded() {
	AudioInput::VoiceActivitySnapshot snapshot;
	QVERIFY(!snapshot.hasProcessedInput());

	snapshot.peakSignalDb      = -42.0f;
	snapshot.peakCleanMicDb    = -48.0f;
	snapshot.speechProbability = 0.75f;
	snapshot.bitrate           = 64000;
	snapshot.transmitting      = true;
	QCOMPARE(snapshot.amplitudeLevel(), 0.5f);
	QVERIFY(snapshot.hasProcessedInput());

	snapshot.peakCleanMicDb = -120.0f;
	QCOMPARE(snapshot.amplitudeLevel(), 0.0f);
	snapshot.peakCleanMicDb = 12.0f;
	QCOMPARE(snapshot.amplitudeLevel(), 1.0f);
}

void TestModernDialogControllers::nativeAutomationBoundariesRemainTypedAndDeterministic() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString audioInputPath = QFINDTESTDATA("../../mumble/AudioInput.h");
	const QString shellHostPath = QFINDTESTDATA("../../mumble/QmlShellHost.cpp");
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!audioInputPath.isEmpty(), "AudioInput.h test data was not found");
	QVERIFY2(!shellHostPath.isEmpty(), "QmlShellHost.cpp test data was not found");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");

	const QString mainWindowSource = readTestSource(mainWindowPath);
	const QString audioInputSource = readTestSource(audioInputPath);
	const QString shellHostSource = readTestSource(shellHostPath);
	const QString automationSource = readTestSource(automationPath);
	QVERIFY(!mainWindowSource.isEmpty());
	QVERIFY(!audioInputSource.isEmpty());
	QVERIFY(!shellHostSource.isEmpty());
	QVERIFY(!automationSource.isEmpty());

	QVERIFY(mainWindowSource.contains(QStringLiteral("voiceActivitySnapshot()")));
	for (const QString &unsafeRead : { QStringLiteral("audioInput->dPeak"),
								  QStringLiteral("audioInput->fSpeechProb"), QStringLiteral("audioInput->iBitrate"),
								  QStringLiteral("audioInput->amplitudeVoiceActivityLevel"),
								  QStringLiteral("audioInput->isTransmitting") }) {
		QVERIFY2(!mainWindowSource.contains(unsafeRead),
				 qPrintable(QStringLiteral("Unsafe UI audio read: %1").arg(unsafeRead)));
	}
	QVERIFY(audioInputSource.contains(QStringLiteral("std::atomic< float > m_voiceActivityPeakSignalDb")));
	QVERIFY(audioInputSource.contains(QStringLiteral("std::atomic< float > m_voiceActivityPeakCleanMicDb")));
	QVERIFY(audioInputSource.contains(QStringLiteral("std::atomic< float > m_voiceActivitySpeechProbability")));
	QVERIFY(audioInputSource.contains(QStringLiteral("std::atomic< int > m_voiceActivityBitrate")));
	QVERIFY(audioInputSource.contains(QStringLiteral("std::atomic< bool > m_voiceActivityTransmitting")));

	const qsizetype registrationStart =
		shellHostSource.indexOf(QStringLiteral("void QmlShellHost::registerCaptureWindow"));
	const qsizetype readinessStart =
		shellHostSource.indexOf(QStringLiteral("bool QmlShellHost::captureWindowReady"), registrationStart);
	const qsizetype captureStart =
		shellHostSource.indexOf(QStringLiteral("bool QmlShellHost::captureWindow("), readinessStart);
	const qsizetype captureEnd =
		shellHostSource.indexOf(QStringLiteral("bool QmlShellHost::ensurePttToolWindow"), captureStart);
	QVERIFY(registrationStart >= 0);
	QVERIFY(readinessStart > registrationStart);
	QVERIFY(captureStart > readinessStart);
	QVERIFY(captureEnd > captureStart);
	const QString registrationBody = shellHostSource.mid(registrationStart, readinessStart - registrationStart);
	const QString captureBody = shellHostSource.mid(captureStart, captureEnd - captureStart);
	QVERIFY(registrationBody.contains(QStringLiteral("frameSwapped")));
	QVERIFY(registrationBody.contains(QStringLiteral("isExposed()")));
	QVERIFY(captureBody.contains(QStringLiteral("captureWindowReady(windowId)")));
	QVERIFY(!captureBody.contains(QStringLiteral("QEventLoop")));
	QVERIFY(!captureBody.contains(QStringLiteral("processEvents")));

	const qsizetype stateStart =
		automationSource.indexOf(QStringLiteral("QVariantMap ModernUiAutomationServer::buildStateResponse"));
	QVERIFY(stateStart >= 0);
	const QString stateBody = automationSource.mid(stateStart);
	QVERIFY(stateBody.contains(QStringLiteral("session->appMenus()")));
	QVERIFY(stateBody.contains(QStringLiteral("session->selfMenu()")));
	QVERIFY(automationSource.contains(QStringLiteral("\"mainCaptureReady\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"pttToolCaptureReady\"")));
}

void TestModernDialogControllers::dialogControllerBuildsFailedConnectionReconnect() {
	QVariantMap context;
	context.insert(QStringLiteral("type"), QStringLiteral("authenticationFailure"));
	context.insert(QStringLiteral("host"), QStringLiteral("voice.example.test"));
	context.insert(QStringLiteral("port"), 64738);
	context.insert(QStringLiteral("username"), QStringLiteral("old-user"));
	context.insert(QStringLiteral("password"), QStringLiteral("old-password"));

	ModernDialogController controller;
	QVariantMap state = controller.openFailedConnection(context);
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("failedConnection"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);

	controller.updateField(QStringLiteral("failedConnection"), QStringLiteral("username"), QStringLiteral("new-user"));
	controller.updateField(QStringLiteral("failedConnection"), QStringLiteral("password"), QStringLiteral("new-password"));

	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("failedConnection"), QStringLiteral("reconnect"), QVariantMap());

	QVERIFY(result.connectionRequest.has_value());
	QCOMPARE(result.connectionRequest->host, QStringLiteral("voice.example.test"));
	QCOMPARE(result.connectionRequest->port, 64738);
	QCOMPARE(result.connectionRequest->username, QStringLiteral("new-user"));
	QCOMPARE(result.connectionRequest->password, QStringLiteral("new-password"));
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);
}

void TestModernDialogControllers::dialogControllerDispatchesGenericDialogAction() {
	QVariantMap field;
	field.insert(QStringLiteral("id"), QStringLiteral("reason"));
	field.insert(QStringLiteral("label"), QStringLiteral("Reason"));
	field.insert(QStringLiteral("type"), QStringLiteral("text"));
	field.insert(QStringLiteral("value"), QStringLiteral(""));

	QVariantMap section;
	section.insert(QStringLiteral("title"), QStringLiteral("Fields"));
	section.insert(QStringLiteral("fields"), QVariantList { field });

	QVariantMap action;
	action.insert(QStringLiteral("id"), QStringLiteral("confirm"));
	action.insert(QStringLiteral("label"), QStringLiteral("Confirm"));
	action.insert(QStringLiteral("closesDialog"), true);

	QVariantMap dialog;
	dialog.insert(QStringLiteral("id"), QStringLiteral("kickUser:7"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("confirm"));
	dialog.insert(QStringLiteral("title"), QStringLiteral("Kick user"));
	dialog.insert(QStringLiteral("sections"), QVariantList { section });
	dialog.insert(QStringLiteral("actions"), QVariantList { action });

	ModernDialogController controller;
	QVariantMap state = controller.openGenericDialog(dialog);
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("kickUser:7"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);

	controller.updateField(QStringLiteral("kickUser:7"), QStringLiteral("reason"), QStringLiteral("AFK cleanup"));
	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("kickUser:7"), QStringLiteral("confirm"), QVariantMap());

	QVERIFY(result.genericAction.has_value());
	QCOMPARE(result.genericAction->dialogID, QStringLiteral("kickUser:7"));
	QCOMPARE(result.genericAction->actionID, QStringLiteral("confirm"));
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("reason")).toString(),
			 QStringLiteral("AFK cleanup"));
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);
}

void TestModernDialogControllers::dialogControllerBuildsDisconnectConfirmation() {
	ModernDialogController controller;
	QVariantMap state = controller.openDisconnectConfirmation(QStringLiteral("Demo Server"));

	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("disconnectServer"));
	QCOMPARE(state.value(QStringLiteral("kind")).toString(), QStringLiteral("confirm"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Disconnect"));
	QCOMPARE(state.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Disconnect from this server?"));
	QCOMPARE(state.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("confirmDisconnect"));
	QCOMPARE(state.value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));

	const QVariantList fields = state.value(QStringLiteral("sections")).toList().at(0).toMap().value(
		QStringLiteral("fields")).toList();
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Server"));
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("Demo Server"));

	const QVariantList actions = state.value(QStringLiteral("actions")).toList();
	QCOMPARE(actions.size(), 2);
	QCOMPARE(actions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cancel"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("confirmDisconnect"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("closesDialog")).toBool(), true);

	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("disconnectServer"), QStringLiteral("confirmDisconnect"),
								QVariantMap());
	QVERIFY(result.genericAction.has_value());
	QCOMPARE(result.genericAction->dialogID, QStringLiteral("disconnectServer"));
	QCOMPARE(result.genericAction->actionID, QStringLiteral("confirmDisconnect"));
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);
}

void TestModernDialogControllers::dialogControllerBuildsQuitConfirmation() {
	ModernDialogController controller;
	QVariantMap state = controller.openQuitConfirmation(true, true);

	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("quitMumble"));
	QCOMPARE(state.value(QStringLiteral("kind")).toString(), QStringLiteral("confirm"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Quit Mumble"));
	QCOMPARE(state.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("confirmQuit"));
	QCOMPARE(state.value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));

	const QVariantList fields = state.value(QStringLiteral("sections")).toList().at(0).toMap().value(
		QStringLiteral("fields")).toList();
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("quit.connected"));
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("value")).toBool(), true);
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("quit.allowMinimize"));
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("value")).toBool(), true);
	QCOMPARE(fields.at(3).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("quit.remember"));
	QCOMPARE(fields.at(3).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("checkbox"));

	const QVariantList actions = state.value(QStringLiteral("actions")).toList();
	QCOMPARE(actions.size(), 3);
	QCOMPARE(actions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cancel"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("minimizeMumble"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("tone")).toString(), QStringLiteral("accent"));
	QCOMPARE(actions.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("confirmQuit"));
	QCOMPARE(actions.at(2).toMap().value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));

	controller.updateField(QStringLiteral("quitMumble"), QStringLiteral("quit.remember"), true);
	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("quitMumble"), QStringLiteral("minimizeMumble"), QVariantMap());
	QVERIFY(result.genericAction.has_value());
	QCOMPARE(result.genericAction->dialogID, QStringLiteral("quitMumble"));
	QCOMPARE(result.genericAction->actionID, QStringLiteral("minimizeMumble"));
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("quit.connected")).toBool(), true);
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("quit.remember")).toBool(), true);
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);

	state = controller.openQuitConfirmation(false, false);
	QCOMPARE(state.value(QStringLiteral("actions")).toList().size(), 2);
	const QVariantList quitOnlyFields = state.value(QStringLiteral("sections")).toList().at(0).toMap().value(
		QStringLiteral("fields")).toList();
	for (const QVariant &fieldValue : quitOnlyFields) {
		QVERIFY(fieldValue.toMap().value(QStringLiteral("id")).toString() != QLatin1String("quit.remember"));
	}
}

void TestModernDialogControllers::dialogControllerBuildsDeleteMessageConfirmation() {
	ModernDialogController controller;
	QVariantMap state = controller.openDeleteMessageConfirmation(42, QStringLiteral("#general"));

	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("deleteMessage:42"));
	QCOMPARE(state.value(QStringLiteral("kind")).toString(), QStringLiteral("confirm"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Delete message"));
	QCOMPARE(state.value(QStringLiteral("subtitle")).toString(),
			 QStringLiteral("Delete this message from chat history?"));
	QCOMPARE(state.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("confirmDeleteMessage"));
	QCOMPARE(state.value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));
	QCOMPARE(state.value(QStringLiteral("width")).toInt(), 430);
	QCOMPARE(state.value(QStringLiteral("height")).toInt(), 230);

	const QVariantList sections = state.value(QStringLiteral("sections")).toList();
	QCOMPARE(sections.size(), 1);
	const QVariantList fields = sections.at(0).toMap().value(QStringLiteral("fields")).toList();
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("message.id"));
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("value")).toULongLong(), 42ULL);
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Conversation"));
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("#general"));

	const QVariantList actions = state.value(QStringLiteral("actions")).toList();
	QCOMPARE(actions.size(), 2);
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("confirmDeleteMessage"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("closesDialog")).toBool(), true);

	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("deleteMessage:42"), QStringLiteral("confirmDeleteMessage"),
								QVariantMap());
	QVERIFY(result.genericAction.has_value());
	QCOMPARE(result.genericAction->dialogID, QStringLiteral("deleteMessage:42"));
	QCOMPARE(result.genericAction->actionID, QStringLiteral("confirmDeleteMessage"));
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("message.id")).toULongLong(), 42ULL);
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);
}

void TestModernDialogControllers::dialogControllerBuildsChangeAvatar() {
	ModernDialogController controller;
	QVariantMap fieldValues;
	fieldValues.insert(QStringLiteral("avatar.path"), QStringLiteral("C:/tmp/avatar.png"));
	QVariantMap errors;
	errors.insert(QStringLiteral("avatar.path"), QStringLiteral("Too large"));

	QVariantMap state = controller.openChangeAvatar(7, QStringLiteral("Demo User"), fieldValues, errors);

	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("changeAvatar:7"));
	QCOMPARE(state.value(QStringLiteral("kind")).toString(), QStringLiteral("form"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Change Avatar"));
	QCOMPARE(state.value(QStringLiteral("subtitle")).toString(),
			 QStringLiteral("Choose a new server-side avatar for Demo User."));
	QCOMPARE(state.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("confirmChangeAvatar"));
	QCOMPARE(state.value(QStringLiteral("width")).toInt(), 680);
	QCOMPARE(state.value(QStringLiteral("height")).toInt(), 440);
	QCOMPARE(state.value(QStringLiteral("errors")).toMap().value(QStringLiteral("avatar.path")).toString(),
			 QStringLiteral("Too large"));

	const QVariantList sections = state.value(QStringLiteral("sections")).toList();
	QCOMPARE(sections.size(), 1);
	const QVariantList fields = sections.at(0).toMap().value(QStringLiteral("fields")).toList();
	QCOMPARE(fields.size(), 4);
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("session"));
	QCOMPARE(fields.at(0).toMap().value(QStringLiteral("value")).toUInt(), 7U);
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("User"));
	QCOMPARE(fields.at(1).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("Demo User"));
	const QVariantMap pathField = fields.at(2).toMap();
	QCOMPARE(pathField.value(QStringLiteral("id")).toString(), QStringLiteral("avatar.path"));
	QCOMPARE(pathField.value(QStringLiteral("label")).toString(), QStringLiteral("Image file or URL"));
	QCOMPARE(pathField.value(QStringLiteral("type")).toString(), QStringLiteral("pathPicker"));
	QCOMPARE(pathField.value(QStringLiteral("value")).toString(), QStringLiteral("C:/tmp/avatar.png"));
	QCOMPARE(pathField.value(QStringLiteral("browseActionId")).toString(), QStringLiteral("browseAvatarImage"));
	QCOMPARE(pathField.value(QStringLiteral("browseLabel")).toString(), QStringLiteral("Browse"));
	QCOMPARE(fields.at(3).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("note"));

	const QVariantList actions = state.value(QStringLiteral("actions")).toList();
	QCOMPARE(actions.size(), 2);
	QCOMPARE(actions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cancel"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("confirmChangeAvatar"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("tone")).toString(), QStringLiteral("accent"));
	QCOMPARE(actions.at(1).toMap().value(QStringLiteral("closesDialog")).toBool(), false);

	controller.updateField(QStringLiteral("changeAvatar:7"), QStringLiteral("avatar.path"),
						   QStringLiteral("C:/tmp/new-avatar.jpg"));
	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("changeAvatar:7"), QStringLiteral("confirmChangeAvatar"),
								QVariantMap());
	QVERIFY(result.genericAction.has_value());
	QCOMPARE(result.genericAction->dialogID, QStringLiteral("changeAvatar:7"));
	QCOMPARE(result.genericAction->actionID, QStringLiteral("confirmChangeAvatar"));
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("session")).toUInt(), 7U);
	QCOMPARE(result.genericAction->fieldValues.value(QStringLiteral("avatar.path")).toString(),
			 QStringLiteral("C:/tmp/new-avatar.jpg"));
	QCOMPARE(result.closeDialog, false);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), true);
}

void TestModernDialogControllers::dialogControllerBuildsMigrationNotice() {
	ModernDialogController controller;
	QVariantMap state = controller.openMigrationNotice(QStringLiteral("aclMigration"), QStringLiteral("Room ACL"),
													   QStringLiteral("ACL editing is being migrated."));

	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("aclMigration"));
	QCOMPARE(state.value(QStringLiteral("kind")).toString(), QStringLiteral("migrationNotice"));
	QCOMPARE(state.value(QStringLiteral("open")).toBool(), true);
	QCOMPARE(controller.activeDialogID(), QStringLiteral("aclMigration"));

	ModernDialogController::ActionResult result =
		controller.invokeAction(QStringLiteral("aclMigration"), QStringLiteral("close"), QVariantMap());
	QCOMPARE(result.closeDialog, true);
	QCOMPARE(controller.state().value(QStringLiteral("open")).toBool(), false);
}

QTEST_MAIN(TestModernDialogControllers)
#include "TestModernDialogControllers.moc"
