// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "AudioInput.h"
#include "ModernConnectController.h"
#include "ModernDialogController.h"
#include "ModernSettingsController.h"

class TestModernDialogControllers : public QObject {
	Q_OBJECT

private slots:
	void connectControllerSelectsAndSavesFavorites();
	void settingsControllerForcesModernAndAppliesDraft();
	void settingsControllerClampsAudioSetupPayload();
	void audioInputVoiceActivityLevelUsesExpectedSignals();
	void dialogControllerBuildsFailedConnectionReconnect();
	void dialogControllerDispatchesGenericDialogAction();
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
	settings.modernLayoutPolicy = Settings::ModernLayoutFollowLegacy;
	settings.wlWindowLayout     = Settings::LayoutClassic;
	settings.bReconnect         = false;
	settings.iQuality           = 72000;

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("NetworkConfig"));

	QCOMPARE(controller.activePage(), QStringLiteral("network"));
	QCOMPARE(controller.draft().modernLayoutPolicy, Settings::ModernLayoutForced);
	QCOMPARE(controller.draft().wlWindowLayout, Settings::LayoutHybrid);

	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("settings"));
	QCOMPARE(state.value(QStringLiteral("activePage")).toString(), QStringLiteral("network"));

	auto verifySettingsTooltips = [](const Settings &sourceSettings) {
		ModernSettingsController tooltipController;
		const QStringList pages { QStringLiteral("look"), QStringLiteral("network"), QStringLiteral("screenShare"),
								  QStringLiteral("audioInput"), QStringLiteral("audioOutput") };
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

	controller.updateField(QStringLiteral("network.autoReconnect"), true);
	ModernSettingsController::ActionResult result = controller.invokeAction(QStringLiteral("ok"), QVariantMap());

	QVERIFY(result.settingsToApply.has_value());
	QCOMPARE(result.settingsToApply->modernLayoutPolicy, Settings::ModernLayoutForced);
	QCOMPARE(result.settingsToApply->wlWindowLayout, Settings::LayoutHybrid);
	QCOMPARE(result.settingsToApply->bReconnect, true);
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
	QCOMPARE(audioOutputResult.accepted, false);
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
					&& field.value(QStringLiteral("calibrationLabel")).toString() == QLatin1String("Audio setup")
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
	QVERIFY(autoSetResult.settingsToApply.has_value());
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
	QVERIFY(clampedResult.settingsToApply.has_value());
	QCOMPARE(controller.draft().vsVAD, Settings::Amplitude);
	QCOMPARE(controller.draft().iVoiceHold, 80);
	QCOMPARE(controller.draft().iMinLoudness, 20000);
	QCOMPARE(controller.draft().noiseCancelMode, Settings::NoiseCancelOff);
	QCOMPARE(controller.draft().inputGateMode, Settings::InputGateStrict);
	QCOMPARE(controller.draft().iSpeexNoiseCancelStrength, -100);
	QVERIFY(qFuzzyCompare(controller.draft().fVADmin, 0.10f));
	QVERIFY(qFuzzyCompare(controller.draft().fVADmax, 0.95f));

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
	QVERIFY(neuralResult.settingsToApply.has_value());
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
