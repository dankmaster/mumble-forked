// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ModernConnectController.h"
#include "ModernDialogController.h"
#include "ModernSettingsController.h"

class TestModernDialogControllers : public QObject {
	Q_OBJECT

private slots:
	void connectControllerSelectsAndSavesFavorites();
	void settingsControllerForcesModernAndAppliesDraft();
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

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("NetworkConfig"));

	QCOMPARE(controller.activePage(), QStringLiteral("network"));
	QCOMPARE(controller.draft().modernLayoutPolicy, Settings::ModernLayoutForced);
	QCOMPARE(controller.draft().wlWindowLayout, Settings::LayoutHybrid);

	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("id")).toString(), QStringLiteral("settings"));
	QCOMPARE(state.value(QStringLiteral("activePage")).toString(), QStringLiteral("network"));

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

	controller.open(settings, QStringLiteral("AudioInput"));
	QCOMPARE(controller.activePage(), QStringLiteral("audioInput"));
	const QVariantList audioInputSections = controller.state().value(QStringLiteral("sections")).toList();
	bool foundInputMeter                  = false;
	for (const QVariant &sectionValue : audioInputSections) {
		const QVariantMap section = sectionValue.toMap();
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == QLatin1String("audio.inputMeter")) {
				foundInputMeter = field.value(QStringLiteral("type")).toString() == QLatin1String("voiceMeter");
			}
		}
	}
	QVERIFY(foundInputMeter);

	controller.updateField(QStringLiteral("audio.quality"), 72000);
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
