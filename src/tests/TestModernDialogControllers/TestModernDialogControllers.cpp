// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#include <QtTest>

#include <algorithm>
#include <QTemporaryDir>

#include "AudioInput.h"
#include "Global.h"
#include "InputEnhancementPackageVerifier.h"
#include "ModernConnectController.h"
#include "ModernConnectDiscoveryService.h"
#include "ModernDialogController.h"
#include "ModernProductDialogStateFactory.h"
#include "ModernSettingsController.h"
#include "ModernTheme.h"
#include "GlobalShortcut.h"
#include "GlobalShortcutTypes.h"
#include "Log.h"
#include "MainWindow.h"
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
#	include "WASAPIDeviceRouting.h"
#endif

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

QVariantMap dialogSection(const QVariantMap &dialog, const QString &id) {
	for (const QVariant &value : dialog.value(QStringLiteral("sections")).toList()) {
		const QVariantMap section = value.toMap();
		if (section.value(QStringLiteral("id")).toString() == id) return section;
	}
	return {};
}

QVariantMap dialogField(const QVariantMap &dialog, const QString &id) {
	for (const QVariant &sectionValue : dialog.value(QStringLiteral("sections")).toList()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == id) return field;
		}
	}
	return {};
}

QVariantMap dialogAction(const QVariantMap &dialog, const QString &id) {
	for (const QVariant &value : dialog.value(QStringLiteral("actions")).toList()) {
		const QVariantMap action = value.toMap();
		if (action.value(QStringLiteral("id")).toString() == id) return action;
	}
	return {};
}
} // namespace

namespace {
	class DraftInputRegistrar final : public AudioInputRegistrar {
	public:
		DraftInputRegistrar() : AudioInputRegistrar(QStringLiteral("DraftTestInput"), 999) {}

		AudioInput *create() override { return nullptr; }
		const QVariant getDeviceChoice() override { return m_choice; }
		const QList< audioDevice > getDeviceChoices() override {
			return { audioDevice(QStringLiteral("Microphone A"), QStringLiteral("mic-a")),
					 audioDevice(QStringLiteral("Microphone B"), QStringLiteral("mic-b")) };
		}
		void setDeviceChoice(const QVariant &choice, Settings &settings) override {
			m_choice           = choice.toString();
			settings.qsOSSInput = m_choice.toString();
		}
		Mumble::InputEnhancement::DeviceIdentity resolveDeviceIdentity(const Settings &settings) override {
			Mumble::InputEnhancement::DeviceIdentity identity;
			identity.backendId  = name;
			identity.physicalId = settings.qsOSSInput;
			identity.displayName = identity.physicalId == QLatin1String("mic-b")
								   ? QStringLiteral("Microphone B")
								   : QStringLiteral("Microphone A");
			identity.stable = true;
			return identity;
		}
		bool canEcho(EchoCancelOptionID, const QString &) const override { return false; }
		bool isMicrophoneAccessDeniedByOS() override { return false; }

	private:
		QVariant m_choice = QStringLiteral("mic-a");
	};

	class ScopedGlobalOverride final {
	public:
		explicit ScopedGlobalOverride(Global *replacement) : m_previous(Global::g_global_struct) {
			Global::g_global_struct = replacement;
		}
		explicit ScopedGlobalOverride(Global &replacement) : ScopedGlobalOverride(&replacement) {}
		~ScopedGlobalOverride() { Global::g_global_struct = m_previous; }

	private:
		Global *m_previous;
	};
}

class TestModernDialogControllers : public QObject {
	Q_OBJECT

private slots:
	void connectControllerSelectsAndSavesFavorites();
	void connectControllerPublishesTypedDiscoverySourcesAndSafeEditorTransitions();
	void connectDiscoveryParsesRegistryRowsDeterministically();
	void connectControllerPublishesVisualContract();
	void productCertificateDialogUsesProductionContract();
	void productScreenShareEditorUsesProductionContract();
	void settingsControllerForcesModernAndAppliesDraft();
	void settingsControllerPublishesLiveStonksAndPermissionGatedAdministration();
	void settingsControllerPublishesPermissionAwareMotdEditor();
	void settingsAppearanceAutoAccentTracksDraftTheme();
	void settingsAppearancePreviewCommitsAndRollsBack();
	void settingsControllerEditsShortcutDataAndTargets();
	void settingsControllerClampsAudioSetupPayload();
	void settingsControllerRollsBackVoiceReplayPreview();
	void settingsControllerRoutesOpaqueCalibrationPlaybackActions();
	void settingsControllerReconcilesPluginRuntimeState();
	void pluginLoadActionUsesAsyncRuntimeReconciliation();
	void settingsControllerAutoProfileIsRuntimeGated();
	void settingsControllerEnhancementFollowsDraftSelectedMicrophone();
	void settingsControllerExplicitProfileSelectionUsesQualifiedPresetAtomically();
	void settingsControllerRevalidatesEnhancementBeforeApply();
	void settingsControllerPolicyBlockPreservesUnchangedEnhancement();
	void settingsControllerNewDeviceInheritedEnhancementStartsProbation();
	void settingsControllerRejectsStaleLastKnownGoodBinding();
	void settingsControllerPreservedLegacyOverrideBypassesProductPreflight();
	void settingsControllerStoresAdvancedLegacyOverrideOnCurrentMicrophone();
	void audioInputVoiceActivityLevelUsesExpectedSignals();
	void audioInputVoiceActivitySnapshotIsBounded();
	void audioInputRollbackBindingRequiresOpenedRunningCapture();
	void nativeAutomationBoundariesRemainTypedAndDeterministic();
	void visualFixturePresentationWaitIsBoundedAndDestroySafe();
	void mediaAutomationReadinessStateIsTyped();
	void conversationAutomationUsesLiveTypedControllers();
	void detachedMediaWindowWaitsForRuntimeReadiness();
	void automationWalkProbesUseDeterministicFixtures();
	void persistentChatAttachmentIoIsStrictAndAsynchronous();
	void persistentChatHistoryWarmupStaysViewportBounded();
	void persistentChatProviderImagesStayManagedAndCancelable();
	void serverIdentityImagePickerIsManagedAndAsynchronous();
	void dialogControllerBuildsFailedConnectionReconnect();
	void dialogControllerDispatchesGenericDialogAction();
	void dialogControllerRestoresTransientDialogParents();
	void dialogControllerRefreshesSameDialogWithoutStacking();
	void dialogControllerBuildsDisconnectConfirmation();
	void dialogControllerBuildsQuitConfirmation();
	void dialogControllerBuildsDeleteMessageConfirmation();
	void dialogControllerBuildsChangeAvatar();
	void dialogControllerBuildsMigrationNotice();
	void chatHistoryGrantDialogWaitsForMatchedAcknowledgement();
};

void TestModernDialogControllers::productCertificateDialogUsesProductionContract() {
	Mumble::ModernProductDialogs::CertificateDialogInput input;
	input.certificate = { true, QStringLiteral("Demo User"), QStringLiteral("demo@example.com"),
		QStringLiteral("Demo CA"), QStringLiteral("2034-05-16T10:30:00+02:00"),
		QStringLiteral("7a:18:93:24:df:8a:61:4b:15:8d:7c:60:9f:42:aa:b1:43:5c:de:72"),
		QStringLiteral("2034-05-16") };
	QVariantMap dialog = Mumble::ModernProductDialogs::certificateDialog(input);
	QCOMPARE(dialog.value(QStringLiteral("id")).toString(), QStringLiteral("certificate"));
	QCOMPARE(dialog.value(QStringLiteral("kind")).toString(), QStringLiteral("certificate"));
	QCOMPARE(dialog.value(QStringLiteral("width")).toInt(), 820);
	QCOMPARE(dialog.value(QStringLiteral("height")).toInt(), 680);
	QCOMPARE(dialog.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("runCertificateAction"));
	QCOMPARE(dialog.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("cert.mode"));
	QCOMPARE(dialogSection(dialog, QStringLiteral("certificate-current")).value(QStringLiteral("presentation")).toString(),
		QStringLiteral("certificate-current"));
	QCOMPARE(dialogSection(dialog, QStringLiteral("certificate-action")).value(QStringLiteral("presentation")).toString(),
		QStringLiteral("certificate-action"));
	QVERIFY(!dialogField(dialog, QStringLiteral("cert.current.name")).isEmpty());
	QVERIFY(!dialogField(dialog, QStringLiteral("cert.current.email")).isEmpty());
	QVERIFY(!dialogField(dialog, QStringLiteral("cert.name")).isEmpty());
	QVERIFY(!dialogField(dialog, QStringLiteral("cert.email")).isEmpty());
	QCOMPARE(dialogField(dialog, QStringLiteral("cert.mode")).value(QStringLiteral("value")).toString(),
		QStringLiteral("export"));
	QCOMPARE(dialogField(dialog, QStringLiteral("cert.mode")).value(QStringLiteral("valueType")).toString(),
		QStringLiteral("string"));
	QCOMPARE(dialogAction(dialog, QStringLiteral("cancel")).value(QStringLiteral("closesDialog")).toBool(), true);
	QCOMPARE(dialogAction(dialog, QStringLiteral("runCertificateAction")).value(QStringLiteral("tone")).toString(),
		QStringLiteral("accent"));

	input.fieldValues = { { QStringLiteral("cert.mode"), QStringLiteral("create") },
		{ QStringLiteral("cert.name"), QStringLiteral("Review User") },
		{ QStringLiteral("cert.email"), QStringLiteral("bad address") } };
	input.errors = { { QStringLiteral("cert.email"), QStringLiteral("Enter a valid email address.") } };
	dialog = Mumble::ModernProductDialogs::certificateDialog(input);
	QCOMPARE(dialogField(dialog, QStringLiteral("cert.name")).value(QStringLiteral("value")).toString(),
		QStringLiteral("Review User"));
	QCOMPARE(dialogField(dialog, QStringLiteral("cert.email")).value(QStringLiteral("value")).toString(),
		QStringLiteral("bad address"));
	QCOMPARE(dialog.value(QStringLiteral("errors")).toMap().value(QStringLiteral("cert.email")).toString(),
		QStringLiteral("Enter a valid email address."));

	input = {};
	dialog = Mumble::ModernProductDialogs::certificateDialog(input);
	QCOMPARE(dialogField(dialog, QStringLiteral("cert.mode")).value(QStringLiteral("value")).toString(),
		QStringLiteral("quick"));
	const QVariantList options = dialogField(dialog, QStringLiteral("cert.mode")).value(QStringLiteral("options")).toList();
	QCOMPARE(options.constLast().toMap().value(QStringLiteral("enabled")).toBool(), false);
}

void TestModernDialogControllers::productScreenShareEditorUsesProductionContract() {
	Mumble::ModernProductDialogs::ScreenShareEditorStateInput input;
	input.channelName = QStringLiteral("Lobby");
	input.channelId = QStringLiteral("7");
	input.selectedSourceId = QStringLiteral("monitor:0");
	input.sources = { QVariantMap { { QStringLiteral("id"), QStringLiteral("screens") },
		{ QStringLiteral("section"), QStringLiteral("Screens") },
		{ QStringLiteral("items"), QVariantList { QVariantMap {
			{ QStringLiteral("id"), QStringLiteral("monitor:0") },
			{ QStringLiteral("title"), QStringLiteral("Screen 1") } } } } } };
	input.resolutionOptions = { QVariantMap { { QStringLiteral("label"), QStringLiteral("1080p") },
		{ QStringLiteral("value"), QStringLiteral("1920x1080") } } };
	input.resolutionDefault = QStringLiteral("1920x1080");
	input.frameRateOptions = { QVariantMap { { QStringLiteral("label"), QStringLiteral("30 FPS") },
		{ QStringLiteral("value"), 30 } } };
	input.frameRateDefault = 30;
	input.audioOptions = { QVariantMap { { QStringLiteral("label"), QStringLiteral("No audio") },
		{ QStringLiteral("value"), QString() } } };
	input.sourcesLoading = false;
	const QVariantMap state = Mumble::ModernProductDialogs::screenShareEditorState(input);
	QCOMPARE(state.value(QStringLiteral("channelId")).toString(), QStringLiteral("7"));
	QCOMPARE(state.value(QStringLiteral("sourcesLoading")).toBool(), false);

	const QVariantMap dialog = Mumble::ModernProductDialogs::screenShareEditorDialog(state);
	QCOMPARE(dialog.value(QStringLiteral("id")).toString(), QStringLiteral("screenShare"));
	QCOMPARE(dialog.value(QStringLiteral("kind")).toString(), QStringLiteral("screenShare"));
	QCOMPARE(dialog.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Share to Lobby"));
	QCOMPARE(dialog.value(QStringLiteral("width")).toInt(), 720);
	QCOMPARE(dialog.value(QStringLiteral("height")).toInt(), 600);
	QCOMPARE(dialog.value(QStringLiteral("tone")).toString(), QStringLiteral("wide"));
	QCOMPARE(dialog.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("screenShareSource_monitor:0"));
	const QVariantMap startAction = dialogAction(dialog, QStringLiteral("screenShare.start"));
	QCOMPARE(startAction.value(QStringLiteral("enabled")).toBool(), true);
	QCOMPARE(startAction.value(QStringLiteral("closesDialog")).toBool(), false);

	QVariantMap invalidSelectionState = state;
	invalidSelectionState.insert(QStringLiteral("selectedSourceId"), QStringLiteral("window:missing"));
	const QVariantMap invalidSelectionDialog =
		Mumble::ModernProductDialogs::screenShareEditorDialog(invalidSelectionState);
	QCOMPARE(dialogAction(invalidSelectionDialog, QStringLiteral("screenShare.start"))
		.value(QStringLiteral("enabled")).toBool(), false);
	QCOMPARE(invalidSelectionDialog.value(QStringLiteral("initialFocusId")).toString(),
		QStringLiteral("screenShareSource_monitor:0"));

	invalidSelectionState.insert(QStringLiteral("selectedSourceId"), QString());
	invalidSelectionState.insert(QStringLiteral("sourceError"),
		QStringLiteral("That screen is no longer available."));
	const QVariantMap staleSourceDialog =
		Mumble::ModernProductDialogs::screenShareEditorDialog(invalidSelectionState);
	QCOMPARE(staleSourceDialog.value(QStringLiteral("statusMessage")).toString(),
		QStringLiteral("That screen is no longer available."));
	QCOMPARE(staleSourceDialog.value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));
	QCOMPARE(staleSourceDialog.value(QStringLiteral("errors")).toMap()
		.value(QStringLiteral("screenShare.source")).toString(),
		QStringLiteral("That screen is no longer available."));

	QVariantMap probingState = state;
	probingState.insert(QStringLiteral("runtimeProbePending"), true);
	const QVariantMap probingDialog = Mumble::ModernProductDialogs::screenShareEditorDialog(probingState);
	QCOMPARE(dialogAction(probingDialog, QStringLiteral("screenShare.start"))
		.value(QStringLiteral("enabled")).toBool(), false);
	QCOMPARE(dialogAction(probingDialog, QStringLiteral("screenShare.retryRuntime")).isEmpty(), true);
	QCOMPARE(probingDialog.value(QStringLiteral("runtimeStatus")).toString(),
		QStringLiteral("Checking the local screen-share runtime…"));
	QCOMPARE(probingDialog.value(QStringLiteral("statusMessage")).toString(), QString());
	QCOMPARE(probingDialog.value(QStringLiteral("initialFocusId")).toString(),
		QStringLiteral("dialogAction_cancel"));

	QVariantMap failedRuntimeState = state;
	failedRuntimeState.insert(QStringLiteral("runtimeError"),
		QStringLiteral("The bundled runtime is unavailable."));
	const QVariantMap failedRuntimeDialog =
		Mumble::ModernProductDialogs::screenShareEditorDialog(failedRuntimeState);
	QCOMPARE(dialogAction(failedRuntimeDialog, QStringLiteral("screenShare.start"))
		.value(QStringLiteral("enabled")).toBool(), false);
	QCOMPARE(dialogAction(failedRuntimeDialog, QStringLiteral("screenShare.retryRuntime"))
		.value(QStringLiteral("enabled")).toBool(), true);
	QCOMPARE(failedRuntimeDialog.value(QStringLiteral("errors")).toMap()
		.value(QStringLiteral("screenShare.runtime")).toString(),
		QStringLiteral("The bundled runtime is unavailable."));
	QCOMPARE(failedRuntimeDialog.value(QStringLiteral("statusMessage")).toString(), QString());
	QCOMPARE(failedRuntimeDialog.value(QStringLiteral("initialFocusId")).toString(),
		QStringLiteral("dialogAction_screenShare.retryRuntime"));
}

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
	controller.invokeAction(QStringLiteral("backToFavorites"), QVariantMap());
	QCOMPARE(controller.state().value(QStringLiteral("editorOpen")).toBool(), false);
	QCOMPARE(controller.state().value(QStringLiteral("selectedFavoriteIndex")).toInt(), 0);
	QCOMPARE(controller.state().value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("connectFavoriteList"));
	controller.invokeAction(QStringLiteral("newFavorite"), QVariantMap());

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
	const QStringList productionSettingsPages {
		QStringLiteral("audioInput"), QStringLiteral("audioOutput"), QStringLiteral("look"),
		QStringLiteral("ui"), QStringLiteral("messages"), QStringLiteral("stonks"), QStringLiteral("keys"),
		QStringLiteral("network"), QStringLiteral("screenShare"), QStringLiteral("plugins"),
		QStringLiteral("about")
	};
	QStringList advertisedSettingsPages;
	for (const QVariant &pageValue : state.value(QStringLiteral("pages")).toList()) {
		const QVariantMap page = pageValue.toMap();
		advertisedSettingsPages.push_back(page.value(QStringLiteral("id")).toString());
		if (page.value(QStringLiteral("id")).toString() == QLatin1String("screenShare")) {
			QVERIFY(page.value(QStringLiteral("contentFitCompact")).toBool());
		}
	}
	QCOMPARE(advertisedSettingsPages, productionSettingsPages);
	QStringList controllerStatePages = productionSettingsPages;
	// Plugin rows come from the live PluginManager and are exercised by the
	// running-app visual fixture. This controller unit has no Global lifecycle,
	// so only assert that the production page is advertised here.
	controllerStatePages.removeAll(QStringLiteral("plugins"));
	for (const QString &page : controllerStatePages) {
		ModernSettingsController pageController;
		pageController.open(settings, page);
		const QVariantMap pageState = pageController.state();
		QCOMPARE(pageState.value(QStringLiteral("activePage")).toString(), page);
		QCOMPARE(pageState.value(QStringLiteral("pages")).toList().size(), productionSettingsPages.size());
		QVERIFY2(!pageState.value(QStringLiteral("sections")).toList().isEmpty(),
				 qPrintable(QStringLiteral("Production Settings page '%1' has no sections").arg(page)));
		const QVariantList pageActions = pageState.value(QStringLiteral("actions")).toList();
		QCOMPARE(pageActions.size(), 3);
		QCOMPARE(pageActions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cancel"));
		QCOMPARE(pageActions.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("apply"));
		QCOMPARE(pageActions.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ok"));
	}

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

#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	Settings wasapiSettings          = settings;
	wasapiSettings.qsAudioInput      = QStringLiteral("WASAPI");
	wasapiSettings.qsAudioOutput     = QStringLiteral("WASAPI");
	ModernSettingsController wasapiInputController;
	wasapiInputController.open(wasapiSettings, QStringLiteral("audioInput"));
	const QVariantList wasapiInputSections =
		wasapiInputController.state().value(QStringLiteral("sections")).toList();
	const QVariantMap simpleMicrophoneField =
		findSettingsFieldById(wasapiInputSections, QStringLiteral("audio.inputDevice"));
	QCOMPARE(simpleMicrophoneField.value(QStringLiteral("type")).toString(), QStringLiteral("select"));
	QVERIFY(!simpleMicrophoneField.value(QStringLiteral("advanced")).toBool());
	const QVariantMap inputPriorityField =
		findSettingsFieldById(wasapiInputSections, QStringLiteral("audio.wasapiInputPriorities"));
	QCOMPARE(inputPriorityField.value(QStringLiteral("type")).toString(), QStringLiteral("devicePriorityList"));
	QVERIFY(inputPriorityField.value(QStringLiteral("advanced")).toBool());

	ModernSettingsController wasapiController;
	wasapiController.open(wasapiSettings, QStringLiteral("audioOutput"));
	const QVariantList wasapiOutputSections =
		wasapiController.state().value(QStringLiteral("sections")).toList();
	const QVariantMap simpleOutputDeviceField =
		findSettingsFieldById(wasapiOutputSections, QStringLiteral("audio.outputDevice"));
	QCOMPARE(simpleOutputDeviceField.value(QStringLiteral("type")).toString(), QStringLiteral("select"));
	QVERIFY(!simpleOutputDeviceField.value(QStringLiteral("advanced")).toBool());
	const QVariantMap latencyField = findSettingsFieldById(
		wasapiOutputSections,
		QStringLiteral("audio.wasapiLatencyProfile"));
	QVERIFY(!latencyField.isEmpty());
	QVERIFY(latencyField.value(QStringLiteral("tooltip")).toString().contains(QStringLiteral("server ping")));
	QVERIFY(latencyField.value(QStringLiteral("tooltip")).toString().contains(QStringLiteral("exclusive mode")));
	const QVariantList latencyOptions = latencyField.value(QStringLiteral("options")).toList();
	QCOMPARE(latencyOptions.size(), 3);
	QVERIFY(latencyOptions.at(0).toMap().value(QStringLiteral("label")).toString().contains(
		QStringLiteral("recommended")));
	for (const QVariant &latencyOption : latencyOptions) {
		QVERIFY(latencyOption.toMap().value(QStringLiteral("hint")).toString().size() > 100);
	}
	const QVariantMap priorityField = findSettingsFieldById(
		wasapiOutputSections,
		QStringLiteral("audio.wasapiOutputPriorities"));
	QCOMPARE(priorityField.value(QStringLiteral("type")).toString(), QStringLiteral("devicePriorityList"));
	QVERIFY(priorityField.value(QStringLiteral("advanced")).toBool());
	QVERIFY(priorityField.value(QStringLiteral("tooltip")).toString().contains(QStringLiteral("endpoint identifier")));
	const QVariantMap outputRoutingField =
		findSettingsFieldById(wasapiOutputSections, QStringLiteral("audio.wasapiOutputRouting"));
	QVERIFY(outputRoutingField.value(QStringLiteral("advanced")).toBool());

	Mumble::WASAPI::DeviceDescriptor primary;
	primary.endpointId       = QStringLiteral("primary-endpoint");
	primary.displayName      = QStringLiteral("AceZone Wireless");
	primary.parentInstanceId = QStringLiteral("USB\\VID_3842&PID_2600");
	primary.dataFlow         = 0; // eRender
	primary.formFactor       = 1;
	Mumble::WASAPI::DeviceDescriptor backup = primary;
	backup.endpointId       = QStringLiteral("backup-endpoint");
	backup.displayName      = QStringLiteral("Desktop speakers");
	backup.parentInstanceId = QStringLiteral("HDAUDIO\\FUNC_01");
	wasapiController.updateField(
		QStringLiteral("audio.wasapiOutputPriorities"),
		QVariantList { Mumble::WASAPI::serializeDeviceDescriptor(primary),
					   Mumble::WASAPI::serializeDeviceDescriptor(backup) });
	const QList< Mumble::WASAPI::DeviceDescriptor > savedPriorities =
		Mumble::WASAPI::deserializeDevicePriorityList(wasapiController.draft().qsWASAPIOutputDevicePriorities);
	QCOMPARE(savedPriorities.size(), 2);
	QCOMPARE(savedPriorities.at(0).endpointId, primary.endpointId);
	QCOMPARE(savedPriorities.at(1).endpointId, backup.endpointId);
	QCOMPARE(wasapiController.draft().qsWASAPIOutput, primary.endpointId);
	QCOMPARE(wasapiController.draft().qsWASAPIOutputDeviceIdentity,
			 Mumble::WASAPI::serializeDeviceDescriptor(primary));
#endif

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

	ModernSettingsController messageController;
	messageController.open(settings, QStringLiteral("messages"));
	const QString linkPreviewsFieldID = QStringLiteral("network.linkPreviews");
	QVERIFY(!findSettingsFieldById(messageController.state().value(QStringLiteral("sections")).toList(),
								 linkPreviewsFieldID)
			 .isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("network")), linkPreviewsFieldID).isEmpty());
	QVariantMap messageEvents = findSettingsFieldById(
		messageController.state().value(QStringLiteral("sections")).toList(),
		QStringLiteral("messages.events"));
	QCOMPARE(messageEvents.value(QStringLiteral("type")).toString(), QStringLiteral("messageEventEditor"));
	QCOMPARE(messageEvents.value(QStringLiteral("rows")).toList().size(),
			 static_cast< int >(Log::lastMsgType) - static_cast< int >(Log::firstMsgType) + 1);
	QVERIFY(messageController.invokeAction(QStringLiteral("messages.toggleEvent"),
		QVariantMap { { QStringLiteral("messageType"), static_cast< int >(Log::TextMessage) },
			{ QStringLiteral("property"), QStringLiteral("console") },
			{ QStringLiteral("value"), true } })
			.stateChanged);
	QVERIFY(messageController.invokeAction(QStringLiteral("messages.toggleEvent"),
		QVariantMap { { QStringLiteral("messageType"), static_cast< int >(Log::TextMessage) },
			{ QStringLiteral("property"), QStringLiteral("tts") },
			{ QStringLiteral("value"), true } })
			.stateChanged);
	QVERIFY(messageController.invokeAction(QStringLiteral("messages.toggleEvent"),
		QVariantMap { { QStringLiteral("messageType"), static_cast< int >(Log::TextMessage) },
			{ QStringLiteral("property"), QStringLiteral("sound") },
			{ QStringLiteral("value"), true } })
			.stateChanged);
	messageEvents = findSettingsFieldById(
		messageController.state().value(QStringLiteral("sections")).toList(),
		QStringLiteral("messages.events"));
	QVariantMap textMessageEvent;
	for (const QVariant &rowValue : messageEvents.value(QStringLiteral("rows")).toList()) {
		const QVariantMap row = rowValue.toMap();
		if (row.value(QStringLiteral("type")).toInt() == static_cast< int >(Log::TextMessage)) {
			textMessageEvent = row;
			break;
		}
	}
	QVERIFY(!textMessageEvent.isEmpty());
	QVERIFY(textMessageEvent.value(QStringLiteral("console")).toBool());
	QVERIFY(!textMessageEvent.value(QStringLiteral("tts")).toBool());
	QVERIFY(textMessageEvent.value(QStringLiteral("sound")).toBool());

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
	const QVariantMap defaultTickerEnabledField =
		findSettingsFieldById(settingsPageSections(QStringLiteral("look")), tickerEnabledFieldID);
	QVERIFY(defaultTickerEnabledField.isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("ui")), tickerEnabledFieldID).isEmpty());
	QVERIFY(findSettingsFieldById(settingsPageSections(QStringLiteral("messages")), tickerEnabledFieldID).isEmpty());
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
	const QVariantMap uiTweaks = tweakController.state().value(QStringLiteral("uiTweaks")).toMap();
	QCOMPARE(uiTweaks.value(QStringLiteral("theme")).toString(), QStringLiteral("latte"));
	QCOMPARE(uiTweaks.value(QStringLiteral("density")).toString(), QStringLiteral("compact"));
	QCOMPARE(uiTweaks.value(QStringLiteral("userIcons")).toString(), QStringLiteral("classic"));
	QCOMPARE(uiTweaks.value(QStringLiteral("classicUserIcons")).toBool(), true);
	QCOMPARE(uiTweaks.value(QStringLiteral("railSide")).toString(), QStringLiteral("left"));
	QCOMPARE(uiTweaks.value(QStringLiteral("accent")).toString(), QStringLiteral("rose"));
	QCOMPARE(uiTweaks.value(QStringLiteral("stonksProfileShortcutVisible")).toBool(), true);
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerBannerEnabled")).toBool(), false);
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerPlacement")).toString(), QStringLiteral("bottom"));
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerDirection")).toString(), QStringLiteral("left"));
	QCOMPARE(uiTweaks.value(QStringLiteral("tickerSpeed")).toString(), QStringLiteral("normal"));
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
	audioInputSettings.inputEnhancement.defaultPreference.profile =
		Mumble::InputEnhancement::Profile::Quality;
	controller.open(audioInputSettings, QStringLiteral("AudioInput"), true);
	const QVariantMap audioInputOnboarding = controller.state();
	QCOMPARE(audioInputOnboarding.value(QStringLiteral("initialFocusId")).toString(),
			 QStringLiteral("voiceMeterCalibration_audio.inputMeter"));
	QVERIFY(audioInputOnboarding.value(QStringLiteral("subtitle")).toString().contains(
		QLatin1String("choose how others hear you")));
	controller.open(audioInputSettings, QStringLiteral("AudioInput"));
	QCOMPARE(controller.activePage(), QStringLiteral("audioInput"));
	const QVariantList audioInputSections = controller.state().value(QStringLiteral("sections")).toList();
	const auto findSection = [](const QVariantList &sections, const QString &id) {
		for (const QVariant &sectionValue : sections) {
			const QVariantMap section = sectionValue.toMap();
			if (section.value(QStringLiteral("id")).toString() == id) {
				return section;
			}
		}
		return QVariantMap();
	};
	const auto findField = [](const QVariantMap &section, const QString &id) {
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == id) {
				return field;
			}
		}
		return QVariantMap();
	};

	QStringList audioInputSectionIds;
	for (const QVariant &sectionValue : audioInputSections) {
		audioInputSectionIds.push_back(sectionValue.toMap().value(QStringLiteral("id")).toString());
	}
	QCOMPARE(audioInputSectionIds,
			 QStringList({ QStringLiteral("microphone"), QStringLiteral("transmission"),
						   QStringLiteral("microphoneCheck"), QStringLiteral("voiceProcessing"),
						   QStringLiteral("processingCalibration"), QStringLiteral("processingDetails"),
						   QStringLiteral("networkVoice"), QStringLiteral("idleBehavior") }));

	const QVariantMap microphoneSection = findSection(audioInputSections, QStringLiteral("microphone"));
	QCOMPARE(microphoneSection.value(QStringLiteral("title")).toString(), QStringLiteral("Microphone"));
	QVERIFY(!microphoneSection.value(QStringLiteral("subtitle")).toString().trimmed().isEmpty());
	QCOMPARE(findField(microphoneSection, QStringLiteral("audio.inputDevice"))
			 .value(QStringLiteral("label"))
			 .toString(),
		 QStringLiteral("Microphone"));
	QVERIFY(findField(microphoneSection, QStringLiteral("audio.inputSystem"))
				.value(QStringLiteral("advanced"))
				.toBool());

	const QVariantMap transmissionSection = findSection(audioInputSections, QStringLiteral("transmission"));
	QCOMPARE(findField(transmissionSection, QStringLiteral("audio.transmitMode"))
			 .value(QStringLiteral("presentation"))
			 .toString(),
		 QStringLiteral("segmented"));
	QVERIFY(transmissionSection.value(QStringLiteral("subtitle"))
				.toString()
				.contains(QLatin1String("stays open")));

	const QVariantMap checkSection = findSection(audioInputSections, QStringLiteral("microphoneCheck"));
	const QVariantMap meterField   = findField(checkSection, QStringLiteral("audio.inputMeter"));
	QCOMPARE(meterField.value(QStringLiteral("type")).toString(), QStringLiteral("voiceMeter"));
	QCOMPARE(meterField.value(QStringLiteral("calibrationActionId")).toString(),
			 QStringLiteral("finishAudioSetupWizard"));
	QCOMPARE(meterField.value(QStringLiteral("calibrationLabel")).toString(),
			 QStringLiteral("Set up voice activation"));
	QCOMPARE(meterField.value(QStringLiteral("calibrationState")).toString(), QStringLiteral("idle"));
	QCOMPARE(meterField.value(QStringLiteral("recommendedVadSource")).toInt(), Settings::SignalToNoise);
	QCOMPARE(meterField.value(QStringLiteral("recommendedInputGateMode")).toInt(), Settings::InputGateOff);
	QVERIFY(meterField.value(QStringLiteral("calibrationStatusText"))
				.toString()
				.contains(QLatin1String("Advanced")));

	const QVariantMap calibrationSection =
		findSection(audioInputSections, QStringLiteral("processingCalibration"));
	QVERIFY(calibrationSection.value(QStringLiteral("advanced")).toBool());
	QVERIFY(calibrationSection.value(QStringLiteral("collapsible")).toBool());
	QVERIFY(!calibrationSection.value(QStringLiteral("expandedByDefault")).toBool());
	const QVariantMap calibrationField =
		findField(calibrationSection, QStringLiteral("audio.inputEnhancementCalibration"));
	QCOMPARE(calibrationField.value(QStringLiteral("type")).toString(),
			 QStringLiteral("inputEnhancementCalibration"));
	QVERIFY(!calibrationField.value(QStringLiteral("inputEnhancementCalibrationAvailable")).toBool());
	QVERIFY(calibrationField.value(QStringLiteral("inputEnhancementCalibrationUnavailableReason"))
				.toString()
				.contains(QLatin1String("Start the selected input device")));

	const QVariantMap processingDetails = findSection(audioInputSections, QStringLiteral("processingDetails"));
	QVERIFY(processingDetails.value(QStringLiteral("advanced")).toBool());
	QVERIFY(processingDetails.value(QStringLiteral("collapsible")).toBool());
	QVERIFY(!processingDetails.value(QStringLiteral("expandedByDefault")).toBool());
	const QVariantMap noiseCancelMode = findField(processingDetails, QStringLiteral("audio.noiseCancelMode"));
	const QVariantList noiseCancelOptions = noiseCancelMode.value(QStringLiteral("options")).toList();
	QCOMPARE(noiseCancelOptions.size(), 4);
	for (const QVariant &optionValue : noiseCancelOptions) {
		QVERIFY(!optionValue.toMap().value(QStringLiteral("hint")).toString().trimmed().isEmpty());
	}
	QCOMPARE(findField(processingDetails, QStringLiteral("audio.noiseCancelBackend"))
			 .value(QStringLiteral("type"))
			 .toString(),
		 QStringLiteral("hidden"));
	QVERIFY(findSettingsFieldById(audioInputSections, QStringLiteral("audio.cuePtt")).isEmpty());
	QVERIFY(findSection(audioInputSections, QStringLiteral("voiceActivation")).isEmpty());
	QVERIFY(findSection(audioInputSections, QStringLiteral("pushToTalk")).isEmpty());

	controller.updateField(QStringLiteral("audio.transmitMode"), Settings::VAD);
	QCOMPARE(controller.draft().atTransmit, Settings::VAD);
	QCOMPARE(controller.draft().vsVAD, Settings::SignalToNoise);
	const QVariantList voiceActivitySections = controller.state().value(QStringLiteral("sections")).toList();
	const QVariantMap voiceActivationSection =
		findSection(voiceActivitySections, QStringLiteral("voiceActivation"));
	QVERIFY(voiceActivationSection.value(QStringLiteral("advanced")).toBool());
	QVERIFY(voiceActivationSection.value(QStringLiteral("collapsible")).toBool());
	QVERIFY(!voiceActivationSection.value(QStringLiteral("expandedByDefault")).toBool());
	QVERIFY(findSection(voiceActivitySections, QStringLiteral("pushToTalk")).isEmpty());
	const QVariantMap vadSourceField = findField(voiceActivationSection, QStringLiteral("audio.vadSource"));
	const QVariantList vadSourceOptionList = vadSourceField.value(QStringLiteral("options")).toList();
	QCOMPARE(vadSourceOptionList.size(), 3);
	for (const QVariant &optionValue : vadSourceOptionList) {
		QVERIFY(!optionValue.toMap().value(QStringLiteral("hint")).toString().trimmed().isEmpty());
	}
	QCOMPARE(findField(voiceActivationSection, QStringLiteral("audio.vadMin"))
			 .value(QStringLiteral("label"))
			 .toString(),
		 QStringLiteral("Stop threshold"));
	QCOMPARE(findField(voiceActivationSection, QStringLiteral("audio.vadMax"))
			 .value(QStringLiteral("label"))
			 .toString(),
		 QStringLiteral("Start threshold"));
	QCOMPARE(findField(voiceActivationSection, QStringLiteral("audio.voiceHold"))
			 .value(QStringLiteral("suffix"))
			 .toString(),
		 QStringLiteral(" frames"));

	controller.updateField(QStringLiteral("audio.transmitMode"), Settings::PushToTalk);
	const QVariantList pushToTalkSections = controller.state().value(QStringLiteral("sections")).toList();
	const QVariantMap pushToTalkSection = findSection(pushToTalkSections, QStringLiteral("pushToTalk"));
	QVERIFY(!pushToTalkSection.isEmpty());
	QVERIFY(findSection(pushToTalkSections, QStringLiteral("voiceActivation")).isEmpty());
	QVERIFY(pushToTalkSection.value(QStringLiteral("collapsible")).toBool());
	QVERIFY(!pushToTalkSection.value(QStringLiteral("expandedByDefault")).toBool());
	QCOMPARE(findField(pushToTalkSection, QStringLiteral("audio.doublePush"))
			 .value(QStringLiteral("suffix"))
			 .toString(),
		 QStringLiteral(" ms"));
	controller.updateField(QStringLiteral("audio.transmitMode"), Settings::VAD);

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

	// The setup wizard deliberately exercises the migrated legacy Speex path.
	// Select a currently ready product state before testing the final settings
	// Apply/OK path; an unverified migrated Light profile is now fail-closed.
	controller.updateField(QStringLiteral("audio.inputEnhancementProfile"),
						   static_cast< int >(Mumble::InputEnhancement::Profile::Original));
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

void TestModernDialogControllers::settingsControllerPublishesLiveStonksAndPermissionGatedAdministration() {
	auto findField = [](const QVariantList &sections, const QString &id) {
		for (const QVariant &sectionValue : sections) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("id")).toString() == id) {
					return field;
				}
			}
		}
		return QVariantMap();
	};

	Settings settings;
	QCOMPARE(settings.bModernShellTickerBannerEnabled, false);
	QCOMPARE(settings.bModernShellStonksProfileShortcutVisible, true);
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("Stonks"));
	QCOMPARE(controller.activePage(), QStringLiteral("stonks"));
	QVariantList sections = controller.state().value(QStringLiteral("sections")).toList();
	QCOMPARE(findField(sections, QStringLiteral("stonks.client.enabled")).value(QStringLiteral("value")).toBool(),
			 false);
	QCOMPARE(findField(sections, QStringLiteral("stonks.client.profileShortcutVisible"))
			 .value(QStringLiteral("value"))
			 .toBool(),
			 true);
	const QVariantMap placementField = findField(sections, QStringLiteral("stonks.client.placement"));
	QCOMPARE(placementField.value(QStringLiteral("value")).toString(), QStringLiteral("bottom"));
	QCOMPARE(placementField.value(QStringLiteral("options")).toList().size(), 4);
	QCOMPARE(placementField.value(QStringLiteral("options")).toList().at(0).toMap().value(QStringLiteral("value"))
				 .toString(),
			 QStringLiteral("windowTop"));
	QCOMPARE(findField(sections, QStringLiteral("stonks.client.direction")).value(QStringLiteral("value")).toString(),
			 QStringLiteral("left"));
	QCOMPARE(findField(sections, QStringLiteral("stonks.client.speed")).value(QStringLiteral("value")).toString(),
			 QStringLiteral("normal"));
	QVERIFY(findField(sections, QStringLiteral("stonks.server.enabled")).isEmpty());
	const QVariantMap disconnectedPortfolio =
		findField(sections, QStringLiteral("stonks.client.openPortfolio"));
	QCOMPARE(disconnectedPortfolio.value(QStringLiteral("actionId")).toString(),
			 QStringLiteral("stonks.openPortfolio"));
	QCOMPARE(disconnectedPortfolio.value(QStringLiteral("enabled")).toBool(), false);

	const ModernSettingsController::ActionResult liveResult = controller.invokeAction(
		QStringLiteral("stonks.updateClient"),
		QVariantMap { { QStringLiteral("fieldId"), QStringLiteral("stonks.client.enabled") },
					  { QStringLiteral("value"), true } });
	QCOMPARE(liveResult.externalActionID, QStringLiteral("stonks.updateClient"));
	QCOMPARE(liveResult.externalActionPayload.value(QStringLiteral("tickerBannerEnabled")).toBool(), true);
	QCOMPARE(controller.draft().bModernShellTickerBannerEnabled, true);
	const ModernSettingsController::ActionResult shortcutResult = controller.invokeAction(
		QStringLiteral("stonks.updateClient"),
		QVariantMap { { QStringLiteral("fieldId"), QStringLiteral("stonks.client.profileShortcutVisible") },
					  { QStringLiteral("value"), false } });
	QCOMPARE(shortcutResult.externalActionPayload.value(QStringLiteral("profileShortcutVisible")).toBool(), false);
	QCOMPARE(controller.draft().bModernShellStonksProfileShortcutVisible, false);
	const ModernSettingsController::ActionResult placementResult = controller.invokeAction(
		QStringLiteral("stonks.updateClient"),
		QVariantMap { { QStringLiteral("fieldId"), QStringLiteral("stonks.client.placement") },
					  { QStringLiteral("value"), QStringLiteral("windowTop") } });
	QCOMPARE(placementResult.externalActionPayload.value(QStringLiteral("tickerPlacement")).toString(),
			 QStringLiteral("windowTop"));
	QCOMPARE(controller.draft().qsModernShellTickerPlacement, QStringLiteral("windowTop"));
	const ModernSettingsController::ActionResult cancelResult =
		controller.invokeAction(QStringLiteral("cancel"), QVariantMap());
	QVERIFY(!cancelResult.settingsToApply.has_value());

	QVariantMap adminContext {
		{ QStringLiteral("connected"), true },
		{ QStringLiteral("supported"), true },
		{ QStringLiteral("canAdmin"), true },
		{ QStringLiteral("enabled"), true },
		{ QStringLiteral("socialAnnouncementsEnabled"), true },
		{ QStringLiteral("textChannelId"), 4u },
		{ QStringLiteral("textChannels"),
		  QVariantList { QVariantMap { { QStringLiteral("textChannelId"), 4u },
								 { QStringLiteral("name"), QStringLiteral("markets") } } } }
	};
	controller.open(settings, QStringLiteral("stonks"), false, adminContext);
	sections = controller.state().value(QStringLiteral("sections")).toList();
	QCOMPARE(findField(sections, QStringLiteral("stonks.client.openPortfolio"))
			 .value(QStringLiteral("enabled"))
			 .toBool(),
			 true);
	QCOMPARE(findField(sections, QStringLiteral("stonks.server.enabled")).value(QStringLiteral("value")).toBool(),
			 true);
	QCOMPARE(findField(sections, QStringLiteral("stonks.server.textChannelId"))
			 .value(QStringLiteral("options"))
			 .toList()
			 .size(),
			 2);
	QCOMPARE(findField(sections, QStringLiteral("stonks.server.apply")).value(QStringLiteral("actionId")).toString(),
			 QStringLiteral("stonks.applyServer"));
	controller.updateField(QStringLiteral("stonks.server.enabled"), false);
	controller.updateField(QStringLiteral("stonks.server.socialAnnouncementsEnabled"), false);
	controller.updateField(QStringLiteral("stonks.server.textChannelId"), 0u);
	const ModernSettingsController::ActionResult serverResult =
		controller.invokeAction(QStringLiteral("stonks.applyServer"), QVariantMap());
	QCOMPARE(serverResult.externalActionID, QStringLiteral("stonks.applyServer"));
	QCOMPARE(serverResult.externalActionPayload.value(QStringLiteral("enabled")).toBool(), false);
	QCOMPARE(serverResult.externalActionPayload.value(QStringLiteral("socialAnnouncementsEnabled")).toBool(), false);
	QCOMPARE(serverResult.externalActionPayload.value(QStringLiteral("textChannelId")).toUInt(), 0u);
	const ModernSettingsController::ActionResult portfolioResult =
		controller.invokeAction(QStringLiteral("stonks.openPortfolio"), QVariantMap());
	QCOMPARE(portfolioResult.externalActionID, QStringLiteral("stonks.openPortfolio"));

	adminContext.insert(QStringLiteral("canAdmin"), false);
	controller.setStonksContext(adminContext);
	QVERIFY(findField(controller.state().value(QStringLiteral("sections")).toList(),
					  QStringLiteral("stonks.server.enabled"))
				.isEmpty());
	QVERIFY(!controller.invokeAction(QStringLiteral("stonks.applyServer"), QVariantMap()).stateChanged);

	ModernDialogController dialog;
	dialog.openSettings(settings, QStringLiteral("stonks"), false, adminContext);
	const ModernDialogController::ActionResult denied =
		dialog.invokeAction(QStringLiteral("settings"), QStringLiteral("stonks.applyServer"), QVariantMap());
	QVERIFY(!denied.genericAction.has_value());
}

void TestModernDialogControllers::settingsControllerPublishesPermissionAwareMotdEditor() {
	Settings settings;
	ModernSettingsController controller;
	QVariantMap readOnlyContext {
		{ QStringLiteral("connected"), true },
		{ QStringLiteral("available"), true },
		{ QStringLiteral("canEdit"), false },
		{ QStringLiteral("serverName"), QStringLiteral("Upstream Mumble") },
		{ QStringLiteral("html"), QStringLiteral("<h2>Welcome</h2>") },
		{ QStringLiteral("maximumLength"), 100000 }
	};
	controller.open(settings, QStringLiteral("motd"), false, {}, readOnlyContext);
	QCOMPARE(controller.activePage(), QStringLiteral("motd"));
	QVariantMap state = controller.state();
	const QVariantMap readOnlyField = dialogField(state, QStringLiteral("motd.html"));
	QCOMPARE(readOnlyField.value(QStringLiteral("type")).toString(), QStringLiteral("motdEditor"));
	QCOMPARE(readOnlyField.value(QStringLiteral("value")).toString(), QStringLiteral("<h2>Welcome</h2>"));
	QVERIFY(!readOnlyField.value(QStringLiteral("canEdit")).toBool());
	QVERIFY(!readOnlyField.value(QStringLiteral("showSaveAction")).toBool());
	const ModernSettingsController::ActionResult readOnlyPreview = controller.invokeAction(
		QStringLiteral("motd.preview"), QVariantMap { { QStringLiteral("html"), QStringLiteral("ignored") } });
	QCOMPARE(readOnlyPreview.externalActionID, QStringLiteral("motd.preview"));
	QCOMPARE(readOnlyPreview.externalActionPayload.value(QStringLiteral("html")).toString(),
			 QStringLiteral("<h2>Welcome</h2>"));
	const QVariantList previewBlocks {
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("heading") },
			{ QStringLiteral("plainText"), QStringLiteral("Welcome") } }
	};
	QVERIFY(controller.setMotdPreview(QStringLiteral("<h2>Welcome</h2>"), previewBlocks,
		QStringLiteral("Welcome")));
	const QVariantMap previewField = dialogField(controller.state(), QStringLiteral("motd.html"));
	QCOMPARE(previewField.value(QStringLiteral("previewSourceHtml")).toString(),
			 QStringLiteral("<h2>Welcome</h2>"));
	QCOMPARE(previewField.value(QStringLiteral("previewBlocks")).toList(), previewBlocks);
	controller.updateField(QStringLiteral("motd.html"), QStringLiteral("not allowed"));
	QCOMPARE(dialogField(controller.state(), QStringLiteral("motd.html"))
			 .value(QStringLiteral("value")).toString(), QStringLiteral("<h2>Welcome</h2>"));
	QVERIFY(!controller.invokeAction(QStringLiteral("motd.save"), {}).stateChanged);

	QVariantMap editableContext = readOnlyContext;
	editableContext.insert(QStringLiteral("canEdit"), true);
	controller.setMotdContext(editableContext);
	controller.updateField(QStringLiteral("motd.html"), QStringLiteral("<p>Updated</p>"));
	const ModernSettingsController::ActionResult save = controller.invokeAction(
		QStringLiteral("motd.save"), QVariantMap { { QStringLiteral("html"), QStringLiteral("<p>Updated</p>") } });
	QCOMPARE(save.externalActionID, QStringLiteral("motd.save"));
	QCOMPARE(save.externalActionPayload.value(QStringLiteral("html")).toString(),
			 QStringLiteral("<p>Updated</p>"));
	const ModernSettingsController::ActionResult editablePreview = controller.invokeAction(
		QStringLiteral("motd.preview"), QVariantMap { { QStringLiteral("html"), QStringLiteral("<p>Draft</p>") } });
	QCOMPARE(editablePreview.externalActionID, QStringLiteral("motd.preview"));
	QCOMPARE(editablePreview.externalActionPayload.value(QStringLiteral("html")).toString(),
			 QStringLiteral("<p>Draft</p>"));

	const ModernSettingsController::ActionResult image = controller.invokeAction(
		QStringLiteral("motd.insertImage"), QVariantMap { { QStringLiteral("selectionStart"), 3 } });
	QCOMPARE(image.externalActionID, QStringLiteral("motd.insertImage"));
	QCOMPARE(image.externalActionPayload.value(QStringLiteral("fieldId")).toString(), QStringLiteral("motd.html"));
	QCOMPARE(image.externalActionPayload.value(QStringLiteral("maximumLength")).toInt(), 100000);

	ModernDialogController dialog;
	dialog.openSettings(settings, QStringLiteral("motd"), false, {}, editableContext);
	const ModernDialogController::ActionResult routed = dialog.invokeAction(
		QStringLiteral("settings"), QStringLiteral("motd.save"),
		QVariantMap { { QStringLiteral("html"), QStringLiteral("<p>Compatible</p>") } });
	QVERIFY(routed.genericAction.has_value());
	QCOMPARE(routed.genericAction->actionID, QStringLiteral("motd.save"));
	QCOMPARE(routed.genericAction->payload.value(QStringLiteral("html")).toString(),
			 QStringLiteral("<p>Compatible</p>"));
	const ModernDialogController::ActionResult previewRouted = dialog.invokeAction(
		QStringLiteral("settings"), QStringLiteral("motd.preview"),
		QVariantMap { { QStringLiteral("html"), QStringLiteral("<p>Preview</p>") } });
	QVERIFY(previewRouted.genericAction.has_value());
	QCOMPARE(previewRouted.genericAction->actionID, QStringLiteral("motd.preview"));
	QVERIFY(dialog.setSettingsMotdPreview(QStringLiteral("<p>Preview</p>"), previewBlocks,
		QStringLiteral("Preview")));

	controller.open(settings, QStringLiteral("motd"));
	QCOMPARE(controller.activePage(), QStringLiteral("audioInput"));
	for (const QVariant &pageValue : controller.state().value(QStringLiteral("pages")).toList()) {
		QVERIFY(pageValue.toMap().value(QStringLiteral("id")).toString() != QLatin1String("motd"));
	}
}

void TestModernDialogControllers::connectControllerPublishesVisualContract() {
	Settings settings;
	settings.qsUsername = QStringLiteral("Demo User");

	FavoriteServer community;
	community.qsName     = QStringLiteral("Mumble Community");
	community.qsHostname = QStringLiteral("voice.example.invalid");
	community.usPort     = 64738;
	community.qsUsername = QStringLiteral("Demo User");

	FavoriteServer studio;
	studio.qsName     = QStringLiteral("Studio");
	studio.qsHostname = QStringLiteral("studio.example.invalid");
	studio.usPort     = 64739;
	studio.qsUsername = QStringLiteral("Producer");

	auto actionIDs = [](const QVariantMap &state) {
		QStringList ids;
		for (const QVariant &actionValue : state.value(QStringLiteral("actions")).toList()) {
			ids.push_back(actionValue.toMap().value(QStringLiteral("id")).toString());
		}
		return ids;
	};
	auto action = [](const QVariantMap &state, const QString &id) {
		for (const QVariant &actionValue : state.value(QStringLiteral("actions")).toList()) {
			const QVariantMap candidate = actionValue.toMap();
			if (candidate.value(QStringLiteral("id")).toString() == id) return candidate;
		}
		return QVariantMap {};
	};
	auto fieldIDs = [](const QVariantMap &state) {
		QStringList ids;
		for (const QVariant &sectionValue : state.value(QStringLiteral("sections")).toList()) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				ids.push_back(fieldValue.toMap().value(QStringLiteral("id")).toString());
			}
		}
		return ids;
	};

	ModernConnectController controller;
	controller.open({ community, studio }, settings);
	QVERIFY(controller.setFavoritePing(community.qsHostname, community.usPort, 28, 18, 128));
	QVERIFY(controller.setFavoritePing(studio.qsHostname, studio.usPort, 41, 6, 64));

	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Connect to a server"));
	QCOMPARE(state.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Choose a saved server or add one."));
	QCOMPARE(state.value(QStringLiteral("primaryActionId")).toString(), QStringLiteral("connect"));
	QCOMPARE(state.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("connectFavoriteList"));
	QCOMPARE(state.value(QStringLiteral("width")).toInt(), 860);
	QCOMPARE(state.value(QStringLiteral("height")).toInt(), 640);
	QCOMPARE(actionIDs(state), QStringList({ QStringLiteral("cancel"), QStringLiteral("editFavorite"),
											QStringLiteral("connect") }));
	QCOMPARE(action(state, QStringLiteral("connect")).value(QStringLiteral("tone")).toString(),
			 QStringLiteral("accent"));

	const QVariantList favorites = state.value(QStringLiteral("favorites")).toList();
	QCOMPARE(favorites.size(), 2);
	const QVariantMap first = favorites.at(0).toMap();
	QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Mumble Community"));
	QCOMPARE(first.value(QStringLiteral("host")).toString(), QStringLiteral("voice.example.invalid"));
	QCOMPARE(first.value(QStringLiteral("port")).toUInt(), 64738U);
	QCOMPARE(first.value(QStringLiteral("subtitle")).toString(),
			 QStringLiteral("voice.example.invalid:64738 / Demo User"));
	QCOMPARE(first.value(QStringLiteral("usersLabel")).toString(), QStringLiteral("Users: 18/128"));
	QCOMPARE(first.value(QStringLiteral("usersValue")).toString(), QStringLiteral("18/128"));
	QCOMPARE(first.value(QStringLiteral("pingLabel")).toString(), QStringLiteral("Ping: 28 ms"));
	QCOMPARE(first.value(QStringLiteral("pingValue")).toString(), QStringLiteral("28 ms"));
	QVERIFY(!first.contains(QStringLiteral("name")));
	QVERIFY(!first.contains(QStringLiteral("address")));

	controller.invokeAction(QStringLiteral("editFavorite"),
		QVariantMap { { QStringLiteral("index"), 1 } });
	state = controller.state();
	QVERIFY(state.value(QStringLiteral("editorOpen")).toBool());
	QCOMPARE(state.value(QStringLiteral("editorTitle")).toString(), QStringLiteral("Edit server"));
	QCOMPARE(state.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("dialogField_host"));
	QCOMPARE(fieldIDs(state), QStringList({ QStringLiteral("name"), QStringLiteral("host"),
										 QStringLiteral("port"), QStringLiteral("username"),
										 QStringLiteral("password") }));
	const QVariantList editorFields = state.value(QStringLiteral("sections")).toList().first().toMap()
		.value(QStringLiteral("fields")).toList();
	const auto portFieldIt = std::find_if(editorFields.cbegin(), editorFields.cend(), [](const QVariant &field) {
		return field.toMap().value(QStringLiteral("id")).toString() == QLatin1String("port");
	});
	QVERIFY(portFieldIt != editorFields.cend());
	QCOMPARE(portFieldIt->toMap().value(QStringLiteral("useGrouping")).toBool(), false);
	QCOMPARE(actionIDs(state), QStringList({ QStringLiteral("backToFavorites"), QStringLiteral("removeFavorite"),
											QStringLiteral("saveFavorite"), QStringLiteral("connect") }));
	QVERIFY(action(state, QStringLiteral("removeFavorite")).value(QStringLiteral("enabled")).toBool());
	QVERIFY(action(state, QStringLiteral("saveFavorite")).value(QStringLiteral("enabled")).toBool());
	QVERIFY(action(state, QStringLiteral("connect")).value(QStringLiteral("enabled")).toBool());

	controller.invokeAction(QStringLiteral("newFavorite"), {});
	controller.updateField(QStringLiteral("username"), QString());
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("editorTitle")).toString(), QStringLiteral("Add server"));
	QCOMPARE(state.value(QStringLiteral("errors")).toMap().value(QStringLiteral("host")).toString(),
			 QStringLiteral("Enter a server host."));
	QCOMPARE(state.value(QStringLiteral("errors")).toMap().value(QStringLiteral("username")).toString(),
			 QStringLiteral("Enter a username."));
	QVERIFY(!state.value(QStringLiteral("canSubmit")).toBool());
	QVERIFY(!action(state, QStringLiteral("removeFavorite")).value(QStringLiteral("enabled")).toBool());
	QVERIFY(!action(state, QStringLiteral("saveFavorite")).value(QStringLiteral("enabled")).toBool());
	QVERIFY(!action(state, QStringLiteral("connect")).value(QStringLiteral("enabled")).toBool());

	controller.open({}, settings);
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("favorites")).toList().size(), 0);
	QCOMPARE(state.value(QStringLiteral("sections")).toList().size(), 0);
	QCOMPARE(state.value(QStringLiteral("initialFocusId")).toString(), QStringLiteral("connectNewFavoriteButton"));
	QCOMPARE(actionIDs(state), QStringList({ QStringLiteral("cancel"), QStringLiteral("editFavorite"),
											QStringLiteral("connect") }));
	QVERIFY(!action(state, QStringLiteral("editFavorite")).value(QStringLiteral("enabled")).toBool());
	QVERIFY(!action(state, QStringLiteral("connect")).value(QStringLiteral("enabled")).toBool());
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
	QVariantMap automaticOption = findOption(
		findField(controller.state(), QStringLiteral("look.modernAccent")), QStringLiteral("auto"));
	QCOMPARE(automaticOption.value(QStringLiteral("themeLabel")).toString(), QStringLiteral("Nord"));
	QVERIFY(automaticOption.value(QStringLiteral("hint")).toString().contains(QStringLiteral("Nord")));
	const QColor nordAccent = colors.second;

	controller.updateField(QStringLiteral("look.modernTheme"), QStringLiteral("gruvbox"));
	colors = appearanceColors(controller.state(), QStringLiteral("gruvbox"));
	QVERIFY(colors.first.isValid());
	QCOMPARE(colors.second, colors.first);
	QVERIFY(colors.second != nordAccent);
	automaticOption = findOption(
		findField(controller.state(), QStringLiteral("look.modernAccent")), QStringLiteral("auto"));
	QCOMPARE(automaticOption.value(QStringLiteral("themeLabel")).toString(), QStringLiteral("Gruvbox"));
	QVERIFY(automaticOption.value(QStringLiteral("hint")).toString().contains(QStringLiteral("Gruvbox")));
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
	settings.bModernShellClassicUserIcons = false;
	settings.qsModernShellRailSide = QStringLiteral("right");
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
	QCOMPARE(preview.appearanceToPreview->classicUserIcons, false);
	QCOMPARE(preview.appearanceToPreview->railSide, QStringLiteral("right"));
	QCOMPARE(preview.appearanceToPreview->accent, QStringLiteral("auto"));
	QCOMPARE(controller.draft().qsModernShellTheme, QStringLiteral("nord"));

	preview = previewField(QStringLiteral("look.modernRailSide"), QStringLiteral("left"));
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->railSide, QStringLiteral("left"));
	preview = previewField(QStringLiteral("look.modernClassicUserIcons"), true);
	QVERIFY(preview.appearanceToPreview.has_value());
	QCOMPARE(preview.appearanceToPreview->classicUserIcons, true);

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
	QCOMPARE(apply.settingsToApply->qsModernShellRailSide, QStringLiteral("left"));
	QCOMPARE(apply.settingsToApply->bModernShellClassicUserIcons, true);
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
	QCOMPARE(cancel.appearanceToPreview->railSide, QStringLiteral("left"));
	QCOMPARE(cancel.appearanceToPreview->classicUserIcons, true);
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
	QCOMPARE(reset.appearanceToPreview->railSide, QStringLiteral("left"));
	QCOMPARE(reset.appearanceToPreview->classicUserIcons, true);
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
		targetShortcut.qlButtons = { QStringLiteral("test-control") };
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
		QCOMPARE(rows.at(0).toMap().value(QStringLiteral("assigned")).toBool(), true);
		QCOMPARE(rows.at(0).toMap().value(QStringLiteral("inputLabel")).toString(),
				 QStringLiteral("Test keyboard: Test key"));
		const QVariantMap targetDetail = rows.at(0).toMap().value(QStringLiteral("target")).toMap();
		QCOMPARE(targetDetail.value(QStringLiteral("mode")).toString(), QStringLiteral("channel"));
		QCOMPARE(targetDetail.value(QStringLiteral("channelId")).toInt(), SHORTCUT_TARGET_CURRENT);
		QCOMPARE(targetDetail.value(QStringLiteral("group")).toString(), QStringLiteral("admins"));
		QCOMPARE(targetDetail.value(QStringLiteral("children")).toBool(), true);
		QCOMPARE(rows.at(1).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("toggle"));
		QCOMPARE(rows.at(2).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("text"));
		QCOMPARE(rows.at(3).toMap().value(QStringLiteral("dataType")).toString(), QStringLiteral("channel"));

		controller.invokeAction(QStringLiteral("keys.beginShortcutCapture"),
			QVariantMap { { QStringLiteral("index"), 0 } });
		QCOMPARE(shortcutField().value(QStringLiteral("rows")).toList().at(0).toMap()
				 .value(QStringLiteral("capturing")).toBool(), true);
		controller.invokeAction(QStringLiteral("keys.cancelShortcutCapture"), QVariantMap());
		QCOMPARE(shortcutField().value(QStringLiteral("rows")).toList().at(0).toMap()
				 .value(QStringLiteral("capturing")).toBool(), false);
		controller.invokeAction(QStringLiteral("keys.beginShortcutCapture"),
			QVariantMap { { QStringLiteral("index"), 1 } });
		controller.invokeAction(QStringLiteral("keys.finishShortcutCapture"),
			QVariantMap { { QStringLiteral("index"), 1 },
				{ QStringLiteral("buttons"), QVariantList { QStringLiteral("test-alt") } } });
		const QVariantMap capturedRow = shortcutField().value(QStringLiteral("rows")).toList().at(1).toMap();
		QCOMPARE(capturedRow.value(QStringLiteral("capturing")).toBool(), false);
		QCOMPARE(capturedRow.value(QStringLiteral("assigned")).toBool(), true);
		QCOMPARE(capturedRow.value(QStringLiteral("inputLabel")).toString(),
				 QStringLiteral("Test keyboard: Test key"));

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

void TestModernDialogControllers::pluginLoadActionUsesAsyncRuntimeReconciliation() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	const QString source = readTestSource(mainWindowPath);
	QVERIFY(!source.isEmpty());

	const qsizetype loadActionStart =
		source.indexOf(QStringLiteral("if (actionID == QLatin1String(\"plugins.load\"))"));
	const qsizetype configureActionStart =
		source.indexOf(QStringLiteral("if (actionID == QLatin1String(\"plugins.configure\"))"), loadActionStart);
	QVERIFY(loadActionStart >= 0);
	QVERIFY(configureActionStart > loadActionStart);
	const QString loadAction = source.mid(loadActionStart, configureActionStart - loadActionStart);
	QVERIFY(loadAction.contains(QStringLiteral("setPluginLoadedAsync(pluginID, true)")));
	QVERIFY(loadAction.contains(QStringLiteral("m_pendingPluginLoadedTransitions.insert(operationID")));
	QVERIFY(loadAction.contains(
		QStringLiteral("plugin->name, true, true, plugin->positionalDataEnabled, true")));
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

void TestModernDialogControllers::settingsControllerAutoProfileIsRuntimeGated() {
	Settings settings;
	settings.inputEnhancement.defaultPreference.autoAdapt = false;
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	// The normal profile bridge must not provide a second way to opt into the
	// experimental profile.
	controller.updateField(QStringLiteral("audio.inputEnhancementProfile"),
						   static_cast< int >(Mumble::InputEnhancement::Profile::Auto));
	controller.updateField(QStringLiteral("audio.inputEnhancementExperimentalAuto"), true);

	bool foundBasicAutoOption        = false;
	bool foundAdvancedAutoControl    = false;
	bool advancedAutoControlDisabled = false;
	bool foundFixedProfileAutoAdapt  = false;
	for (const QVariant &sectionValue : controller.state().value(QStringLiteral("sections")).toList()) {
		const QVariantMap section = sectionValue.toMap();
		for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString()
				== QLatin1String("audio.inputEnhancementProfile")) {
				for (const QVariant &optionValue : field.value(QStringLiteral("options")).toList()) {
					const QVariantMap option = optionValue.toMap();
					if (option.value(QStringLiteral("value")).toInt()
						== static_cast< int >(Mumble::InputEnhancement::Profile::Auto)) {
						foundBasicAutoOption = true;
					}
				}
			} else if (field.value(QStringLiteral("id")).toString()
					   == QLatin1String("audio.inputEnhancementExperimentalAuto")) {
				foundAdvancedAutoControl = section.value(QStringLiteral("advanced")).toBool()
					|| field.value(QStringLiteral("advanced")).toBool();
				advancedAutoControlDisabled = !field.value(QStringLiteral("enabled"), true).toBool();
			} else if (field.value(QStringLiteral("id")).toString()
					   == QLatin1String("audio.inputEnhancementAutoAdapt")) {
				foundFixedProfileAutoAdapt = true;
			}
		}
	}
	QVERIFY(!foundBasicAutoOption);
	QVERIFY(foundAdvancedAutoControl);
	QVERIFY(advancedAutoControlDisabled);
	QVERIFY(!foundFixedProfileAutoAdapt);
	QCOMPARE(controller.draft().inputEnhancement.defaultPreference.profile,
			 Mumble::InputEnhancement::Profile::Original);
	QVERIFY(!controller.draft().inputEnhancement.defaultPreference.autoAdapt);
	controller.updateField(QStringLiteral("audio.inputEnhancementAutoAdapt"), true);
	QVERIFY(!controller.draft().inputEnhancement.defaultPreference.autoAdapt);
	settings.inputEnhancement.defaultPreference.profile   = Mumble::InputEnhancement::Profile::Original;
	settings.inputEnhancement.defaultPreference.autoAdapt = true;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.inputEnhancementAutoAdapt"), false);
	QVERIFY(controller.draft().inputEnhancement.defaultPreference.autoAdapt);

	// A previously persisted experimental selection remains readable, but the
	// user can always leave it even when the Auto runtime is unavailable.
	settings.inputEnhancement.defaultPreference.profile   = Mumble::InputEnhancement::Profile::Auto;
	settings.inputEnhancement.defaultPreference.autoAdapt = true;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.inputEnhancementExperimentalAuto"), false);
	QCOMPARE(controller.draft().inputEnhancement.defaultPreference.profile,
			 Mumble::InputEnhancement::Profile::Original);
	QVERIFY(!controller.draft().inputEnhancement.defaultPreference.autoAdapt);
}

void TestModernDialogControllers::settingsControllerEnhancementFollowsDraftSelectedMicrophone() {
	DraftInputRegistrar registrar;
	Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	Mumble::InputEnhancement::DeviceProfileState first;
	first.identity.backendId  = registrar.name;
	first.identity.physicalId = QStringLiteral("mic-a");
	first.identity.stable     = true;
	first.preference.profile  = Mumble::InputEnhancement::Profile::Balanced;
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(settings.inputEnhancement, first));
	Mumble::InputEnhancement::DeviceProfileState second;
	second.identity.backendId  = registrar.name;
	second.identity.physicalId = QStringLiteral("mic-b");
	second.identity.stable     = true;
	second.preference.profile  = Mumble::InputEnhancement::Profile::Light;
	QVERIFY(Mumble::InputEnhancement::upsertDeviceProfile(settings.inputEnhancement, second));

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.inputDevice"), 1);
	controller.updateField(QStringLiteral("audio.inputEnhancementProfile"),
						   static_cast< int >(Mumble::InputEnhancement::Profile::Original));

	Mumble::InputEnhancement::DeviceIdentity firstIdentity = first.identity;
	Mumble::InputEnhancement::DeviceIdentity secondIdentity = second.identity;
	const auto *savedFirst = Mumble::InputEnhancement::findDeviceProfile(
		controller.draft().inputEnhancement, firstIdentity);
	const auto *savedSecond = Mumble::InputEnhancement::findDeviceProfile(
		controller.draft().inputEnhancement, secondIdentity);
	QVERIFY(savedFirst);
	QVERIFY(savedSecond);
	QCOMPARE(static_cast< int >(savedFirst->preference.profile),
			 static_cast< int >(Mumble::InputEnhancement::Profile::Balanced));
	QCOMPARE(static_cast< int >(savedSecond->preference.profile),
			 static_cast< int >(Mumble::InputEnhancement::Profile::Original));
}

void TestModernDialogControllers::settingsControllerExplicitProfileSelectionUsesQualifiedPresetAtomically() {
	using namespace Mumble::InputEnhancement;
	DraftInputRegistrar registrar;
	QTemporaryDir root;
	QVERIFY(root.isValid());
	Global global(root.filePath(QStringLiteral("mumble-test.ini")));
	ScopedGlobalOverride globalOverride(global);
	InputEnhancementPackageVerifier verifier(
		{ QDir(root.path()), QByteArray(), 0, QStringLiteral("input-recipes-v4") });
	QVERIFY(verifier.verify().unmanaged);
	global.inputEnhancementPackageVerifier = &verifier;

	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	DeviceProfileState state;
	state.identity              = registrar.resolveDeviceIdentity(settings);
	state.preference.profile    = Profile::Light;
	state.preference.reduction  = 11;
	state.preference.character  = 22;
	state.pendingValidation     = true;
	state.legacyOverride        = LegacyOverride{};
	QVERIFY(upsertDeviceProfile(settings.inputEnhancement, state));

	// QML emits this only for an explicit activation. Even selecting the already
	// visible profile therefore means "use the qualified preset", while merely
	// opening the dialog leaves the custom/migrated controls untouched.
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	const DeviceProfileState *opened = findDeviceProfile(controller.draft().inputEnhancement, state.identity);
	QVERIFY(opened);
	QCOMPARE(opened->preference.reduction, 11);
	QCOMPARE(opened->preference.character, 22);
	QVERIFY(opened->legacyOverride.has_value());
	controller.updateField(QStringLiteral("audio.inputEnhancementProfile"), static_cast< int >(Profile::Light));
	const DeviceProfileState *selected = findDeviceProfile(controller.draft().inputEnhancement, state.identity);
	QVERIFY(selected);
	const std::optional< ExplicitProfileControlPreset > preset = qualifiedExplicitSelectionPreset(Profile::Light);
	QVERIFY(preset.has_value());
	QCOMPARE(selected->preference.profile, Profile::Light);
	QCOMPARE(selected->preference.reduction, preset->noiseReduction);
	QCOMPARE(selected->preference.character, preset->naturalCrisp);
	QVERIFY(!selected->preference.autoAdapt);
	QVERIFY(!selected->pendingValidation);
	QVERIFY(!selected->legacyOverride.has_value());

	// A readiness failure must not partially disarm probation, clear legacy, or
	// replace controls. The UI error is allowed to change; the complete draft
	// enhancement state is not.
	global.inputEnhancementPackageVerifier = nullptr;
	ModernSettingsController blocked;
	blocked.open(settings, QStringLiteral("AudioInput"));
	const Mumble::InputEnhancement::Settings before = blocked.draft().inputEnhancement;
	blocked.updateField(QStringLiteral("audio.inputEnhancementProfile"), static_cast< int >(Profile::Quality));
	QVERIFY(blocked.draft().inputEnhancement == before);
}

void TestModernDialogControllers::settingsControllerRevalidatesEnhancementBeforeApply() {
	using namespace Mumble::InputEnhancement;

	// A migrated/non-original draft without a currently verified package must
	// not become a newly saved product preference merely because it predated the
	// settings dialog.
	{
		ScopedGlobalOverride noGlobal(nullptr);
		::Settings migrated;
		migrated.inputEnhancement.defaultPreference.profile = Profile::Light;
		ModernSettingsController controller;
		controller.open(migrated, QStringLiteral("AudioInput"));
		const ModernSettingsController::ActionResult result =
			controller.invokeAction(QStringLiteral("ok"), QVariantMap());
		QVERIFY(!result.settingsToApply.has_value());
		QVERIFY(!result.accepted);
		QVERIFY(!result.closeDialog);
	}

	// Revalidation is performed at Apply/OK time, not only when the option is
	// selected. Simulate a verified development package disappearing after the
	// user chose Light.
	DraftInputRegistrar registrar;
	QTemporaryDir root;
	QVERIFY(root.isValid());
	Global global(root.filePath(QStringLiteral("mumble-test.ini")));
	ScopedGlobalOverride globalOverride(global);
	InputEnhancementPackageVerifier verifier(
		{ QDir(root.path()), QByteArray(), 0, QStringLiteral("input-recipes-v4") });
	QVERIFY(verifier.verify().unmanaged);
	global.inputEnhancementPackageVerifier = &verifier;

	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.inputEnhancementProfile"), static_cast< int >(Profile::Light));
	const DeviceIdentity identity = registrar.resolveDeviceIdentity(controller.draft());
	QCOMPARE(preferenceForDevice(controller.draft().inputEnhancement, identity).profile, Profile::Light);

	global.inputEnhancementPackageVerifier = nullptr;
	const ModernSettingsController::ActionResult rejected =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(!rejected.settingsToApply.has_value());
	QVERIFY(!rejected.accepted);
	QVERIFY(!rejected.closeDialog);
	bool foundReason = false;
	for (const QVariant &sectionValue : controller.state().value(QStringLiteral("sections")).toList()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString()
				== QLatin1String("audio.inputEnhancementProfile")) {
				foundReason = field.value(QStringLiteral("hint")).toString().contains(
					QStringLiteral("package"), Qt::CaseInsensitive);
			}
		}
	}
	QVERIFY(foundReason);

	global.inputEnhancementPackageVerifier = &verifier;
	const ModernSettingsController::ActionResult accepted =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(accepted.settingsToApply.has_value());
	QCOMPARE(preferenceForDevice(accepted.settingsToApply->inputEnhancement, identity).profile,
			 Profile::Light);
}

void TestModernDialogControllers::settingsControllerPolicyBlockPreservesUnchangedEnhancement() {
	using namespace Mumble::InputEnhancement;
	DraftInputRegistrar registrar;
	QTemporaryDir root;
	QVERIFY(root.isValid());
	Global global(root.filePath(QStringLiteral("mumble-test.ini")));
	ScopedGlobalOverride globalOverride(global);
	InputEnhancementPackageVerifier verifier(
		{ QDir(root.path()), QByteArray(), 0, QStringLiteral("input-recipes-v4") });
	QVERIFY(verifier.verify().unmanaged);
	global.inputEnhancementPackageVerifier  = &verifier;
	global.bInputEnhancementRecoveryDisabled = true;
	global.bDisableInputEnhancement           = true;

	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	DeviceProfileState state;
	state.identity           = registrar.resolveDeviceIdentity(settings);
	state.preference.profile = Profile::Light;
	state.preference.reduction = 44;
	QVERIFY(upsertDeviceProfile(settings.inputEnhancement, state));

	ModernSettingsController unrelatedController;
	unrelatedController.open(settings, QStringLiteral("AudioInput"));
	unrelatedController.updateField(QStringLiteral("audio.quality"), 80);
	const ModernSettingsController::ActionResult unrelated =
		unrelatedController.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(unrelated.accepted);
	QVERIFY(unrelated.settingsToApply.has_value());
	QCOMPARE(unrelated.settingsToApply->iQuality, 80000);
	QVERIFY(preferenceForDevice(unrelated.settingsToApply->inputEnhancement, state.identity) == state.preference);

	// A runtime block may be bypassed only when the underlying processor is
	// still qualified. It must not hide a simultaneous package/model failure.
	global.inputEnhancementPackageVerifier = nullptr;
	ModernSettingsController missingPackageController;
	missingPackageController.open(settings, QStringLiteral("AudioInput"));
	const ModernSettingsController::ActionResult missingPackage =
		missingPackageController.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(!missingPackage.accepted);
	QVERIFY(!missingPackage.settingsToApply.has_value());
	global.inputEnhancementPackageVerifier = &verifier;

	ModernSettingsController enhancementController;
	enhancementController.open(settings, QStringLiteral("AudioInput"));
	enhancementController.updateField(QStringLiteral("audio.inputEnhancementReduction"), 45);
	const ModernSettingsController::ActionResult enhancementEdit =
		enhancementController.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(!enhancementEdit.accepted);
	QVERIFY(!enhancementEdit.settingsToApply.has_value());

	// Also prove that a probation result produced while the dialog was open is
	// authoritative: an unrelated save must not resurrect the stale candidate.
	const DefaultPreference originalPreference = state.preference;
	ResolveRequest request;
	request.profile             = originalPreference.profile;
	request.noiseReduction      = originalPreference.reduction;
	request.naturalCrisp        = originalPreference.character;
	request.cpuClass            = CpuClass::Low;
	request.backendAvailability = BackendAvailability::compiled();
	request.captureDevice       = CaptureDeviceContext::liveDevice(registrar.name, true);
	state.pendingRecipeBinding =
		recipeBindingForRecipe(RecipeCatalog::resolve(request), QStringLiteral("unmanaged-build-zero"));
	state.lastKnownGood     = DefaultPreference{};
	state.pendingValidation = true;
	::Settings pending = settings;
	pending.inputEnhancement.deviceProfiles.clear();
	QVERIFY(upsertDeviceProfile(pending.inputEnhancement, state));
	global.s = pending;
	DeviceProfileState *resolved = ensureDeviceProfile(global.s.inputEnhancement, state.identity);
	QVERIFY(resolved);
	resolved->preference   = DefaultPreference{};
	resolved->lastKnownGood = resolved->preference;
	resolved->lastKnownGoodRecipeBinding.reset();
	resolved->pendingRecipeBinding.reset();
	resolved->pendingValidation = false;

	ModernSettingsController staleDialog;
	staleDialog.open(pending, QStringLiteral("AudioInput"));
	staleDialog.updateField(QStringLiteral("audio.quality"), 88);
	const ModernSettingsController::ActionResult reconciled =
		staleDialog.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(reconciled.accepted);
	QVERIFY(reconciled.settingsToApply.has_value());
	const DeviceProfileState *savedResolved =
		findDeviceProfile(reconciled.settingsToApply->inputEnhancement, state.identity);
	QVERIFY(savedResolved);
	QCOMPARE(savedResolved->preference.profile, Profile::Original);
	QVERIFY(!savedResolved->pendingValidation);

	// Success can resolve to the same preference as the pending candidate. The
	// reconciled non-pending runtime state must not be overwritten by the stale
	// pending snapshot when Apply continues through product preflight.
	global.bInputEnhancementRecoveryDisabled = false;
	global.bDisableInputEnhancement           = false;
	global.s = pending;
	resolved = ensureDeviceProfile(global.s.inputEnhancement, state.identity);
	QVERIFY(resolved);
	resolved->lastKnownGood              = resolved->preference;
	resolved->lastKnownGoodRecipeBinding = resolved->pendingRecipeBinding;
	resolved->pendingRecipeBinding.reset();
	resolved->pendingValidation = false;
	ModernSettingsController successfulProbationDialog;
	successfulProbationDialog.open(pending, QStringLiteral("AudioInput"));
	successfulProbationDialog.updateField(QStringLiteral("audio.quality"), 92);
	const ModernSettingsController::ActionResult successfulProbation =
		successfulProbationDialog.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(successfulProbation.accepted);
	QVERIFY(successfulProbation.settingsToApply.has_value());
	const DeviceProfileState *savedSuccessful =
		findDeviceProfile(successfulProbation.settingsToApply->inputEnhancement, state.identity);
	QVERIFY(savedSuccessful);
	QCOMPARE(savedSuccessful->preference.profile, Profile::Light);
	QVERIFY(!savedSuccessful->pendingValidation);
	QVERIFY(savedSuccessful->lastKnownGoodRecipeBinding.has_value());
}

void TestModernDialogControllers::settingsControllerNewDeviceInheritedEnhancementStartsProbation() {
	using namespace Mumble::InputEnhancement;
	DraftInputRegistrar registrar;
	QTemporaryDir root;
	QVERIFY(root.isValid());
	Global global(root.filePath(QStringLiteral("mumble-test.ini")));
	ScopedGlobalOverride globalOverride(global);
	InputEnhancementPackageVerifier verifier(
		{ QDir(root.path()), QByteArray(), 0, QStringLiteral("input-recipes-v4") });
	QVERIFY(verifier.verify().unmanaged);
	global.inputEnhancementPackageVerifier = &verifier;
	global.bInputEnhancementRecoveryDisabled = true;
	global.bDisableInputEnhancement           = true;

	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	settings.inputEnhancement.defaultPreference.profile   = Profile::Light;
	settings.inputEnhancement.defaultPreference.reduction = 47;
	const DeviceIdentity identity = registrar.resolveDeviceIdentity(settings);
	QVERIFY(!findDeviceProfile(settings.inputEnhancement, identity));

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	const ModernSettingsController::ActionResult result =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(result.accepted);
	QVERIFY(result.settingsToApply.has_value());
	const DeviceProfileState *created = findDeviceProfile(result.settingsToApply->inputEnhancement, identity);
	QVERIFY(created);
	QCOMPARE(created->preference.profile, Profile::Light);
	QVERIFY(created->pendingValidation);
	QVERIFY(created->pendingRecipeBinding.has_value());
	QVERIFY(created->lastKnownGood.has_value());
	QCOMPARE(created->lastKnownGood->profile, Profile::Original);
	QVERIFY(!created->lastKnownGoodRecipeBinding.has_value());
}

void TestModernDialogControllers::settingsControllerRejectsStaleLastKnownGoodBinding() {
	using namespace Mumble::InputEnhancement;
	DraftInputRegistrar registrar;
	QTemporaryDir root;
	QVERIFY(root.isValid());
	Global global(root.filePath(QStringLiteral("mumble-test.ini")));
	ScopedGlobalOverride globalOverride(global);
	InputEnhancementPackageVerifier verifier(
		{ QDir(root.path()), QByteArray(), 0, QStringLiteral("input-recipes-v4") });
	QVERIFY(verifier.verify().unmanaged);
	global.inputEnhancementPackageVerifier = &verifier;

	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	const auto bindingFor = [&registrar](const DefaultPreference &preference, const QString &catalog) {
		ResolveRequest request;
		request.profile             = preference.profile;
		request.noiseReduction      = preference.reduction;
		request.naturalCrisp        = preference.character;
		request.cpuClass            = CpuClass::Low;
		request.backendAvailability = BackendAvailability::compiled();
		request.captureDevice       = CaptureDeviceContext::liveDevice(registrar.name, true);
		return recipeBindingForRecipe(RecipeCatalog::resolve(request), catalog);
	};
	DeviceProfileState state;
	state.identity             = registrar.resolveDeviceIdentity(settings);
	state.preference.profile   = Profile::Light;
	state.preference.reduction = 40;
	state.preference.character = 55;
	state.pendingRecipeBinding = bindingFor(state.preference, QStringLiteral("unmanaged-build-zero"));
	state.lastKnownGood        = state.preference;
	state.lastKnownGood->reduction = 35;
	state.lastKnownGoodRecipeBinding = bindingFor(*state.lastKnownGood, QStringLiteral("stale-catalog"));
	state.pendingValidation = true;
	QVERIFY(upsertDeviceProfile(settings.inputEnhancement, state));

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.inputEnhancementReduction"), 45);
	const ModernSettingsController::ActionResult result =
		controller.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(result.accepted);
	QVERIFY(result.settingsToApply.has_value());
	const DeviceProfileState *pending = findDeviceProfile(result.settingsToApply->inputEnhancement, state.identity);
	QVERIFY(pending);
	QVERIFY(pending->pendingValidation);
	QCOMPARE(pending->preference.reduction, 45);
	QVERIFY(pending->lastKnownGood.has_value());
	QCOMPARE(pending->lastKnownGood->profile, Profile::Original);
	QVERIFY(!pending->lastKnownGoodRecipeBinding.has_value());

	// Even an exact persisted binding is not promoted to LKG from a non-pending
	// profile when there is no matching healthy AudioInput runtime instance.
	state.preference.reduction       = 40;
	state.lastKnownGood              = state.preference;
	state.lastKnownGoodRecipeBinding = bindingFor(state.preference, QStringLiteral("unmanaged-build-zero"));
	state.pendingRecipeBinding.reset();
	state.pendingValidation = false;
	::Settings inactiveRuntime = settings;
	inactiveRuntime.inputEnhancement.deviceProfiles.clear();
	QVERIFY(upsertDeviceProfile(inactiveRuntime.inputEnhancement, state));
	ModernSettingsController inactiveRuntimeController;
	inactiveRuntimeController.open(inactiveRuntime, QStringLiteral("AudioInput"));
	inactiveRuntimeController.updateField(QStringLiteral("audio.inputEnhancementReduction"), 46);
	const ModernSettingsController::ActionResult inactiveResult =
		inactiveRuntimeController.invokeAction(QStringLiteral("apply"), QVariantMap());
	QVERIFY(inactiveResult.accepted);
	QVERIFY(inactiveResult.settingsToApply.has_value());
	const DeviceProfileState *inactivePending =
		findDeviceProfile(inactiveResult.settingsToApply->inputEnhancement, state.identity);
	QVERIFY(inactivePending);
	QVERIFY(inactivePending->lastKnownGood.has_value());
	QCOMPARE(inactivePending->lastKnownGood->profile, Profile::Original);
	QVERIFY(!inactivePending->lastKnownGoodRecipeBinding.has_value());
}

void TestModernDialogControllers::settingsControllerPreservedLegacyOverrideBypassesProductPreflight() {
	using namespace Mumble::InputEnhancement;
	ScopedGlobalOverride noGlobal(nullptr);
	::Settings migrated;
	migrated.inputEnhancement.defaultPreference.profile = Profile::Quality;
	migrated.inputEnhancement.legacyOverride =
		LegacyOverride{ static_cast< int >(::Settings::NoiseCancelRNN),
						static_cast< int >(::Settings::DTLNBackend), QStringLiteral("dtln:norm40h"),
						QStringLiteral("C:/preserved/model"), -67 };

	ModernSettingsController controller;
	controller.open(migrated, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.quality"), 96);
	const ModernSettingsController::ActionResult result =
		controller.invokeAction(QStringLiteral("ok"), QVariantMap());

	QVERIFY(result.settingsToApply.has_value());
	QVERIFY(result.accepted);
	QVERIFY(result.closeDialog);
	QCOMPARE(result.settingsToApply->iQuality, 96000);
	QVERIFY(result.settingsToApply->inputEnhancement.legacyOverride.has_value());
	QVERIFY(*result.settingsToApply->inputEnhancement.legacyOverride
			== *migrated.inputEnhancement.legacyOverride);
}

void TestModernDialogControllers::settingsControllerStoresAdvancedLegacyOverrideOnCurrentMicrophone() {
	using namespace Mumble::InputEnhancement;
	ScopedGlobalOverride noGlobal(nullptr);
	DraftInputRegistrar registrar;
	::Settings settings;
	settings.qsAudioInput = registrar.name;
	settings.qsOSSInput   = QStringLiteral("mic-a");
	const DeviceIdentity micA = registrar.resolveDeviceIdentity(settings);
	DeviceProfileState productProfile;
	productProfile.identity           = micA;
	productProfile.preference.profile = Profile::Balanced;
	QVERIFY(upsertDeviceProfile(settings.inputEnhancement, productProfile));

	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("AudioInput"));
	controller.updateField(QStringLiteral("audio.noiseCancelMode"),
								   static_cast< int >(::Settings::NoiseCancelRNN));
	controller.updateField(QStringLiteral("audio.noiseCancelBackend"),
								   static_cast< int >(::Settings::DTLNBackend));
	controller.updateField(QStringLiteral("audio.noiseCancelModel"), QStringLiteral("dtln:norm40h"));
	controller.updateField(QStringLiteral("audio.speexNoiseStrength"), 67);
	const ModernSettingsController::ActionResult result =
		controller.invokeAction(QStringLiteral("ok"), QVariantMap());

	QVERIFY(result.settingsToApply.has_value());
	QVERIFY(result.accepted);
	const DeviceProfileState *saved = findDeviceProfile(result.settingsToApply->inputEnhancement, micA);
	QVERIFY(saved);
	QVERIFY(saved->legacyOverride.has_value());
	QCOMPARE(saved->legacyOverride->noiseCancelMode, static_cast< int >(::Settings::NoiseCancelRNN));
	QCOMPARE(saved->legacyOverride->backend, static_cast< int >(::Settings::DTLNBackend));
	QCOMPARE(saved->legacyOverride->modelId, QStringLiteral("dtln:norm40h"));
	QCOMPARE(saved->legacyOverride->speexNoiseCancelStrength, -67);
	QVERIFY(!saved->pendingValidation);
	QVERIFY(!result.settingsToApply->inputEnhancement.legacyOverride.has_value());
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

	const ModernSettingsController::ActionResult calibrationWhileReplaying =
		controller.invokeAction(QStringLiteral("startInputEnhancementCalibration"), {});
	QVERIFY(calibrationWhileReplaying.stateChanged);
	QVERIFY(!calibrationWhileReplaying.settingsToApply.has_value());
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

void TestModernDialogControllers::settingsControllerRoutesOpaqueCalibrationPlaybackActions() {
	Settings settings;
	ModernSettingsController controller;
	controller.open(settings, QStringLiteral("audioInput"));

	const QString token = QStringLiteral("18446744073709551001");
	const ModernSettingsController::ActionResult play = controller.invokeAction(
		QStringLiteral("playInputEnhancementCalibration"),
		QVariantMap { { QStringLiteral("playbackToken"), token },
					  { QStringLiteral("pcm"), QByteArrayLiteral("must-not-cross-the-controller") } });
	QVERIFY(!play.stateChanged);
	QCOMPARE(play.externalActionID, QStringLiteral("playInputEnhancementCalibration"));
	QCOMPARE(play.externalActionPayload.size(), 1);
	QCOMPARE(play.externalActionPayload.value(QStringLiteral("playbackToken")).toString(), token);
	QVERIFY(!play.externalActionPayload.contains(QStringLiteral("pcm")));

	const ModernSettingsController::ActionResult invalid = controller.invokeAction(
		QStringLiteral("playInputEnhancementCalibration"),
		QVariantMap { { QStringLiteral("playbackToken"), QStringLiteral("not-a-token") } });
	QVERIFY(!invalid.stateChanged);
	QVERIFY(invalid.externalActionID.isEmpty());

	const ModernSettingsController::ActionResult stop = controller.invokeAction(
		QStringLiteral("stopInputEnhancementCalibrationPlayback"), QVariantMap());
	QVERIFY(!stop.stateChanged);
	QCOMPARE(stop.externalActionID, QStringLiteral("stopInputEnhancementCalibrationPlayback"));
	QVERIFY(stop.externalActionPayload.isEmpty());

	ModernDialogController dialog;
	dialog.openSettings(settings, QStringLiteral("audioInput"));
	const ModernDialogController::ActionResult routed = dialog.invokeAction(
		QStringLiteral("settings"), QStringLiteral("playInputEnhancementCalibration"),
		QVariantMap { { QStringLiteral("playbackToken"), token },
					  { QStringLiteral("pcm"), QByteArrayLiteral("discard-me") } });
	QVERIFY(routed.genericAction.has_value());
	QCOMPARE(routed.genericAction->dialogID, QStringLiteral("settings"));
	QCOMPARE(routed.genericAction->actionID, QStringLiteral("playInputEnhancementCalibration"));
	QCOMPARE(routed.genericAction->payload.size(), 1);
	QCOMPARE(routed.genericAction->payload.value(QStringLiteral("playbackToken")).toString(), token);
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

void TestModernDialogControllers::audioInputRollbackBindingRequiresOpenedRunningCapture() {
	QVERIFY(!AudioInput::canAuthorizeInputEnhancementRollbackBinding(false, false, false));
	QVERIFY(!AudioInput::canAuthorizeInputEnhancementRollbackBinding(true, false, true));
	QVERIFY(!AudioInput::canAuthorizeInputEnhancementRollbackBinding(true, true, false));
	QVERIFY(AudioInput::canAuthorizeInputEnhancementRollbackBinding(true, true, true));
}

void TestModernDialogControllers::nativeAutomationBoundariesRemainTypedAndDeterministic() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString audioInputPath = QFINDTESTDATA("../../mumble/AudioInput.h");
	const QString shellHostPath = QFINDTESTDATA("../../mumble/QmlShellHost.cpp");
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	const QString mainQmlPath = QFINDTESTDATA("../../mumble/qml-shell/Main.qml");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!audioInputPath.isEmpty(), "AudioInput.h test data was not found");
	QVERIFY2(!shellHostPath.isEmpty(), "QmlShellHost.cpp test data was not found");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");
	QVERIFY2(!mainQmlPath.isEmpty(), "Main.qml test data was not found");

	const QString mainWindowSource = readTestSource(mainWindowPath);
	const QString audioInputSource = readTestSource(audioInputPath);
	const QString shellHostSource = readTestSource(shellHostPath);
	const QString automationSource = readTestSource(automationPath);
	const QString mainQmlSource = readTestSource(mainQmlPath);
	QVERIFY(!mainWindowSource.isEmpty());
	QVERIFY(!audioInputSource.isEmpty());
	QVERIFY(!shellHostSource.isEmpty());
	QVERIFY(!automationSource.isEmpty());
	QVERIFY(!mainQmlSource.isEmpty());

	QVERIFY(mainWindowSource.contains(QStringLiteral("voiceActivitySnapshot()")));
	const qsizetype policyRepublishStart =
		mainWindowSource.indexOf(QStringLiteral("InputEnhancementPolicyController::effectivePolicyChanged"));
	QVERIFY(policyRepublishStart >= 0);
	const QString policyRepublish = mainWindowSource.mid(policyRepublishStart, 900);
	QVERIFY(policyRepublish.contains(QStringLiteral("activeDialogID() != QLatin1String(\"settings\")")));
	QVERIFY(policyRepublish.contains(
		QStringLiteral("publishModernDialogState(m_modernDialogController->state())")));
	QVERIFY(policyRepublish.contains(QStringLiteral("Qt::QueuedConnection")));
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
	QVERIFY(shellHostSource.contains(QStringLiteral("QLatin1String(\"product-dialog\")")));
	QVERIFY(shellHostSource.contains(QStringLiteral("product-dialog.window")));

	const qsizetype stateStart =
		automationSource.indexOf(QStringLiteral("QVariantMap ModernUiAutomationServer::buildStateResponse"));
	QVERIFY(stateStart >= 0);
	const QString stateBody = automationSource.mid(stateStart);
	QVERIFY(stateBody.contains(QStringLiteral("session->appMenus()")));
	QVERIFY(stateBody.contains(QStringLiteral("session->selfMenu()")));
	QVERIFY(automationSource.contains(QStringLiteral("\"mainCaptureReady\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"pttToolCaptureReady\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"windowVisible\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"windowExposed\"")));
	QVERIFY(automationSource.contains(QStringLiteral("qmlPerformanceTalkRun")));
	QVERIFY(automationSource.contains(QStringLiteral("&QQuickWindow::frameSwapped")));
	QVERIFY(automationSource.contains(QStringLiteral("advanceTalkPerformanceWorkload")));
	QVERIFY(automationSource.contains(QStringLiteral("qmlPerformanceChatScrollRun")));
	QVERIFY(automationSource.contains(QStringLiteral("advanceChatPerformanceWorkload")));
	QVERIFY(automationSource.contains(QStringLiteral("chat-scroll:%1")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("function performanceChatFixtureState()")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("function preparePerformanceChatScrollWorkload(stepCount)")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("function advancePerformanceChatScrollWorkload(step, totalSteps)")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("timeline.contentY = performanceChatScrollStartY")));
	QVERIFY(!mainQmlSource.contains(QStringLiteral("id: timelineScrollWorkload")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("\"firstVisibleId\"")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("\"settled\"")));
}

void TestModernDialogControllers::visualFixturePresentationWaitIsBoundedAndDestroySafe() {
	const QString fixturePath = QFINDTESTDATA("../../mumble/QmlVisualFixtureController.cpp");
	QVERIFY2(!fixturePath.isEmpty(), "QmlVisualFixtureController.cpp test data was not found");
	const QString fixtureSource = readTestSource(fixturePath);
	QVERIFY(!fixtureSource.isEmpty());
	const qsizetype captureWaitStart = fixtureSource.indexOf(
		QStringLiteral("QQuickWindow *QmlVisualFixtureController::waitForCaptureWindow"));
	const qsizetype waitStart = fixtureSource.indexOf(
		QStringLiteral("bool QmlVisualFixtureController::waitForPresentedFrame"));
	QVERIFY(captureWaitStart >= 0);
	QVERIFY(waitStart > captureWaitStart);
	const QString captureWaitBody = fixtureSource.mid(captureWaitStart, waitStart - captureWaitStart);
	QVERIFY(captureWaitBody.contains(QStringLiteral("captureWindowTimeoutMilliseconds = 5000")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("QPointer< QmlShellHost > guardedHost")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("poll.setInterval(16)")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("m_host->captureWindowTarget(windowId")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("candidate->isVisible()")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("candidate->isExposed()")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("targetWindow.clear()")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("mediaSessionWindowComponentFailed")));
	QVERIFY(captureWaitBody.contains(QStringLiteral("Timed out waiting for Qt Quick capture window")));
	QVERIFY(!captureWaitBody.contains(QStringLiteral("processEvents")));

	const QString waitBody = fixtureSource.mid(waitStart);
	QVERIFY(waitBody.contains(QStringLiteral("QPointer< QQuickWindow > guardedWindow")));
	QVERIFY(waitBody.contains(QStringLiteral("exposureTimeoutMilliseconds = 5000")));
	QVERIFY(waitBody.contains(QStringLiteral("presentationTimeoutMilliseconds = 5000")));
	QVERIFY(waitBody.contains(QStringLiteral("QElapsedTimer exposureElapsed")));
	QVERIFY(waitBody.contains(QStringLiteral("guardedWindow->isExposed()")));
	QVERIFY(waitBody.contains(QStringLiteral("&QQuickWindow::frameSwapped")));
	QVERIFY(waitBody.contains(QStringLiteral("&QQuickWindow::afterFrameEnd")));
	QVERIFY(waitBody.contains(QStringLiteral("&QQuickWindow::afterRendering")));
	QVERIFY(waitBody.contains(QStringLiteral("&QQuickWindow::afterAnimating")));
	QVERIFY(waitBody.contains(QStringLiteral("std::atomic_bool")));
	QVERIFY(waitBody.contains(QStringLiteral("std::memory_order_release")));
	QVERIFY(waitBody.contains(QStringLiteral("std::memory_order_acquire")));
	QVERIFY(waitBody.contains(QStringLiteral("Qt::DirectConnection")));
	QVERIFY(waitBody.contains(QStringLiteral("QCoreApplication::processEvents")));
	QVERIFY(waitBody.contains(QStringLiteral("QThread::msleep(1)")));
	QVERIFY(waitBody.contains(QStringLiteral("frameEventTurns")));
	QVERIFY(waitBody.contains(QStringLiteral("nested QEventLoop::exec() inherits a process-level quit flag")));
	const qsizetype frameHookIndex = waitBody.indexOf(QStringLiteral("&QQuickWindow::afterAnimating"));
	const qsizetype boundedFrameRequestIndex =
		waitBody.indexOf(QStringLiteral("guardedWindow->requestUpdate()"));
	const qsizetype presentationTimerIndex =
		waitBody.indexOf(QStringLiteral("QElapsedTimer presentationElapsed"));
	QVERIFY(frameHookIndex >= 0);
	QVERIFY(boundedFrameRequestIndex > frameHookIndex);
	QVERIFY(presentationTimerIndex > boundedFrameRequestIndex);
	QVERIFY(!waitBody.contains(QStringLiteral("guardedWindow->update()")));
	QVERIFY(!waitBody.contains(QStringLiteral("guardedWindow->grabWindow()")));
	QVERIFY(waitBody.contains(QStringLiteral("QObject::disconnect(frameConnection)")));
	QVERIFY(waitBody.contains(QStringLiteral("QObject::disconnect(frameEndConnection)")));
	QVERIFY(waitBody.contains(QStringLiteral("QObject::disconnect(renderingConnection)")));
	QVERIFY(waitBody.contains(QStringLiteral("QObject::disconnect(animatingConnection)")));
	QVERIFY(waitBody.contains(QStringLiteral("Timed out waiting for Qt Quick window exposure.")));
	QVERIFY(waitBody.contains(QStringLiteral("Timed out waiting for a completed Qt Quick scene-graph frame after")));
	QVERIFY(fixtureSource.contains(QStringLiteral("productDialogTransitionActive")));
	QVERIFY(fixtureSource.contains(
		QStringLiteral("*captureWindow = QStringLiteral(\"product-dialog\")")));
	QVERIFY(fixtureSource.contains(
		QStringLiteral("captureWindow == QLatin1String(\"product-dialog\")")));
	QVERIFY(fixtureSource.contains(QStringLiteral("Focus ownership is a GUI-item contract")));
	QVERIFY(fixtureSource.contains(QStringLiteral("closeTimeout.start(2000)")));

	const qsizetype applyStart = fixtureSource.indexOf(
		QStringLiteral("QVariantMap QmlVisualFixtureController::apply("));
	const qsizetype requestUpdate = fixtureSource.indexOf(
		QStringLiteral("presentationWindow->requestUpdate();"), applyStart);
	const qsizetype captureWindowWait = fixtureSource.indexOf(
		QStringLiteral("waitForCaptureWindow(captureWindow, error)"), applyStart);
	const qsizetype firstFrameWait = fixtureSource.indexOf(
		QStringLiteral("waitForPresentedFrame(&frameError, presentationWindow)"), requestUpdate);
	const qsizetype navigationCloseTimer = fixtureSource.indexOf(
		QStringLiteral("QElapsedTimer navigationCloseTimer"), requestUpdate);
	const qsizetype focusAttempts = fixtureSource.indexOf(
		QStringLiteral("maximumFocusAttempts"), requestUpdate);
	QVERIFY(applyStart >= 0);
	QVERIFY(captureWindowWait > applyStart);
	QVERIFY(requestUpdate > captureWindowWait);
	QVERIFY(requestUpdate > applyStart);
	QVERIFY(firstFrameWait > requestUpdate);
	QVERIFY(navigationCloseTimer > firstFrameWait);
	QVERIFY(focusAttempts > navigationCloseTimer);

	const qsizetype applySurfaceStart = fixtureSource.indexOf(
		QStringLiteral("bool QmlVisualFixtureController::applySurface"));
	const qsizetype preSurfaceNavigationClose = fixtureSource.indexOf(
		QStringLiteral("preSurfaceNavigationCloseTimer"), applyStart);
	const qsizetype applySurfaceCall = fixtureSource.indexOf(
		QStringLiteral("applySurface(surfaceVariant, &captureWindow, error)"), applyStart);
	QVERIFY(preSurfaceNavigationClose > applyStart);
	QVERIFY(applySurfaceCall > preSurfaceNavigationClose);
	const qsizetype manualSurfaceStart = fixtureSource.indexOf(
		QStringLiteral("if (surfaceVariant == QLatin1String(\"manual-plugin\"))"), applySurfaceStart);
	const qsizetype manualShow = fixtureSource.indexOf(
		QStringLiteral("m_host->showManualPluginTool(true);"), manualSurfaceStart);
	const qsizetype manualFixtureSet = fixtureSource.indexOf(
		QStringLiteral("manual->setX(2.75)"), manualSurfaceStart);
	QVERIFY(applySurfaceStart >= 0);
	QVERIFY(manualSurfaceStart > applySurfaceStart);
	QVERIFY(manualShow > manualSurfaceStart);
	QVERIFY(manualFixtureSet > manualShow);
	QVERIFY(fixtureSource.contains(QStringLiteral("manual_plugin_state")));
	const qsizetype directMessageFixtureStart = fixtureSource.indexOf(
		QStringLiteral("if (surfaceVariant.startsWith(QLatin1String(\"direct-message-\")))"), applySurfaceStart);
	const qsizetype directMessageFixtureEnd = fixtureSource.indexOf(
		QStringLiteral("if (surfaceVariant == QLatin1String(\"attachment-viewer\"))"), directMessageFixtureStart);
	QVERIFY(directMessageFixtureStart > applySurfaceStart);
	QVERIFY(directMessageFixtureEnd > directMessageFixtureStart);
	const QString directMessageFixtureBody = fixtureSource.mid(
		directMessageFixtureStart, directMessageFixtureEnd - directMessageFixtureStart);
	QVERIFY(directMessageFixtureBody.contains(QStringLiteral("roomModel()->replaceDirectMessageStates")));
	QVERIFY(directMessageFixtureBody.contains(QStringLiteral("navigationModel()->replaceDirectMessageStates")));
	QVERIFY(directMessageFixtureBody.contains(QStringLiteral("roomModel()->selectScope")));
	QVERIFY(directMessageFixtureBody.contains(QStringLiteral("navigationModel()->selectScope")));
	const qsizetype pttFixtureStart = fixtureSource.indexOf(
		QStringLiteral("if (surfaceVariant.startsWith(QLatin1String(\"ptt-\")))"), applySurfaceStart);
	const qsizetype pttFixtureEnd = fixtureSource.indexOf(
		QStringLiteral("if (surfaceVariant == QLatin1String(\"manual-plugin\"))"), pttFixtureStart);
	QVERIFY(pttFixtureStart > applySurfaceStart);
	QVERIFY(pttFixtureEnd > pttFixtureStart);
	const QString pttFixtureBody = fixtureSource.mid(pttFixtureStart, pttFixtureEnd - pttFixtureStart);
	QVERIFY(pttFixtureBody.contains(QStringLiteral("captureWindowTarget(QStringLiteral(\"ptt\"))")));
	QVERIFY(pttFixtureBody.contains(QStringLiteral("&QWindow::activeChanged")));
	QVERIFY(pttFixtureBody.contains(QStringLiteral("Qt::SingleShotConnection")));
	QVERIFY(pttFixtureBody.contains(QStringLiteral("QMetaObject::invokeMethod(pttWindow, \"beginHold\")")));
	QVERIFY(!pttFixtureBody.contains(QStringLiteral("setPttPressed(")));

	for (const QString &variant : {
			 QStringLiteral("chat-message-states"), QStringLiteral("chat-composer-states"),
			 QStringLiteral("chat-attachment-states"), QStringLiteral("chat-history-prepend-anchor") }) {
		QVERIFY2(fixtureSource.contains(QStringLiteral("QStringLiteral(\"") + variant + QStringLiteral("\")")),
			qPrintable(QStringLiteral("Missing integrated chat fixture variant %1").arg(variant)));
	}
	QVERIFY(fixtureSource.contains(QStringLiteral("#include \"ComposerController.h\"")));
	QVERIFY(fixtureSource.contains(QStringLiteral("visualMessageCount(state, surfaceVariant, false)")));
	QVERIFY(fixtureSource.contains(QStringLiteral("visualMessageCount(state, surfaceVariant, true)")));
	QVERIFY(fixtureSource.contains(QStringLiteral("composer->attachments()->append(uploading)")));
	QVERIFY(fixtureSource.contains(QStringLiteral("composer->attachments()->append(failed)")));
	QVERIFY(fixtureSource.contains(
		QStringLiteral("scopeState.insert(QStringLiteral(\"hasPendingReply\"), true)")));
	QVERIFY(fixtureSource.count(QStringLiteral("composer_has_pending_reply")) >= 2);
	QVERIFY(fixtureSource.contains(QStringLiteral("previewCanRetry")));
	QVERIFY(fixtureSource.contains(QStringLiteral("chat->replaceMessages(fixtureMessages)")));
	QVERIFY(fixtureSource.contains(QStringLiteral("m_host->chatModel()->messages()")));
	QVERIFY(fixtureSource.contains(QStringLiteral("m_host->chatModel()->replaceMessages(prependedMessages)")));
	QVERIFY(fixtureSource.contains(QStringLiteral("positionVisualFixtureTimelineAt")));
	QVERIFY(fixtureSource.contains(QStringLiteral("visualFixtureTimelineState")));
	QVERIFY(fixtureSource.contains(QStringLiteral("anchorAfterRow == anchorBeforeRow + VisualHistoryPrependMessageCount")));
	QVERIFY(fixtureSource.contains(QStringLiteral("qAbs(afterOffset - anchorBeforeOffset) <= 1.0")));
	QVERIFY(fixtureSource.contains(QStringLiteral("chat_fixture_state")));
	for (const QString &field : {
			 QStringLiteral("reply_message_count"), QStringLiteral("reaction_message_count"),
			 QStringLiteral("sending_message_count"), QStringLiteral("failed_retry_message_count"),
			 QStringLiteral("deleted_message_count"), QStringLiteral("ready_attachment_count"),
			 QStringLiteral("loading_attachment_count"), QStringLiteral("error_attachment_count"),
			 QStringLiteral("composer_upload_progress_percent"), QStringLiteral("prepend_count"),
			 QStringLiteral("anchor_offset_delta"), QStringLiteral("anchor_preserved"),
			 QStringLiteral("pure_prepend_applied") }) {
		QVERIFY2(fixtureSource.contains(QStringLiteral("QStringLiteral(\"") + field + QStringLiteral("\")")),
			qPrintable(QStringLiteral("Missing normalized chat fixture state field %1").arg(field)));
	}
	const qsizetype positionHistoryAnchor = fixtureSource.indexOf(
		QStringLiteral("positionVisualFixtureTimelineAt"), firstFrameWait);
	const qsizetype applyHistoryPrepend = fixtureSource.indexOf(
		QStringLiteral("m_host->chatModel()->replaceMessages(prependedMessages)"), positionHistoryAnchor);
	QVERIFY(positionHistoryAnchor > firstFrameWait);
	QVERIFY(applyHistoryPrepend > positionHistoryAnchor);

	const qsizetype resetStart = fixtureSource.indexOf(
		QStringLiteral("void QmlVisualFixtureController::resetSurfaceFixtures"));
	const qsizetype resetEnd = fixtureSource.indexOf(
		QStringLiteral("bool QmlVisualFixtureController::applySurface"), resetStart);
	QVERIFY(resetStart >= 0);
	QVERIFY(resetEnd > resetStart);
	const QString resetBody = fixtureSource.mid(resetStart, resetEnd - resetStart);
	const qsizetype closePreviousView = resetBody.indexOf(
		QStringLiteral("m_host->closeScreenShareView(previousView)"));
	const qsizetype drainPreviousView = resetBody.indexOf(
		QStringLiteral("sendPostedEvents(previousView.data(), QEvent::DeferredDelete)"));
	const qsizetype drainPreviousBackend = resetBody.indexOf(
		QStringLiteral("sendPostedEvents(previousBackend, QEvent::DeferredDelete)"));
	QVERIFY(closePreviousView >= 0);
	QVERIFY(drainPreviousView > closePreviousView);
	QVERIFY(drainPreviousBackend > drainPreviousView);

	QVERIFY(resetBody.contains(QStringLiteral("if (!preserveDetachedMediaWindow)")));
	QVERIFY(resetBody.contains(QStringLiteral("m_host->mediaSession()->clearSharedState()")));
	const QString applySurfaceBody = fixtureSource.mid(resetEnd);
	const qsizetype preserveDetachedMedia = applySurfaceBody.indexOf(
		QStringLiteral("const bool preserveDetachedMediaWindow"));
	const qsizetype requireDetachedSurface = applySurfaceBody.indexOf(
		QStringLiteral("surfaceVariant.startsWith(QLatin1String(\"media-detached-\"))"), preserveDetachedMedia);
	const qsizetype requireActiveBackend = applySurfaceBody.indexOf(
		QStringLiteral("m_host->mediaSession()->active()"), requireDetachedSurface);
	const qsizetype resetSurface = applySurfaceBody.indexOf(
		QStringLiteral("resetSurfaceFixtures(preserveDetachedMediaWindow, preserveProductMenus);"),
		requireActiveBackend);
	const qsizetype openMediaSurface = applySurfaceBody.indexOf(
		QStringLiteral("if (surfaceVariant.startsWith(QLatin1String(\"media-\")))"), resetSurface);
	QVERIFY(preserveDetachedMedia >= 0);
	QVERIFY(requireDetachedSurface > preserveDetachedMedia);
	QVERIFY(requireActiveBackend > requireDetachedSurface);
	QVERIFY(resetSurface > requireActiveBackend);
	QVERIFY(openMediaSurface > resetSurface);
	QVERIFY(!applySurfaceBody.contains(QStringLiteral("mediaWindowCloseLoop")));
}

void TestModernDialogControllers::connectControllerPublishesTypedDiscoverySourcesAndSafeEditorTransitions() {
	Settings settings;
	settings.qsUsername = QStringLiteral("community-user");
	FavoriteServer favorite;
	favorite.qsName = QStringLiteral("Saved home");
	favorite.qsHostname = QStringLiteral("saved.example.test");
	favorite.usPort = 64738;
	favorite.qsUsername = QStringLiteral("saved-user");

	ModernConnectController controller;
	controller.open({ favorite }, settings);
	QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("activeSource")).toString(), QStringLiteral("favorites"));
	const QVariantList sources = state.value(QStringLiteral("sources")).toList();
	QCOMPARE(sources.size(), 3);
	QCOMPARE(sources.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("favorites"));
	QCOMPARE(sources.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("public"));
	QCOMPARE(sources.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("lan"));
	QVERIFY(!state.value(QStringLiteral("sourceRows")).toList().constFirst().toMap()
		.value(QStringLiteral("id")).toString().isEmpty());

	ModernConnectController::ActionResult publicRequest = controller.invokeAction(QStringLiteral("selectSource"),
		{ { QStringLiteral("sourceId"), QStringLiteral("public") } });
	QVERIFY(publicRequest.sourceOperation.has_value());
	QCOMPARE(publicRequest.sourceOperation->sourceID, QStringLiteral("public"));
	QCOMPARE(publicRequest.sourceOperation->operation, QStringLiteral("retry"));
	QVERIFY(publicRequest.sourceOperation->generation > 0);
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("activeSource")).toString(), QStringLiteral("public"));
	QCOMPARE(state.value(QStringLiteral("sources")).toList().at(1).toMap()
		.value(QStringLiteral("status")).toString(), QStringLiteral("loading"));
	QVERIFY(controller.requestPublicListConsent(publicRequest.sourceOperation->generation));
	state = controller.state();
	const QVariantMap publicConsent = state.value(QStringLiteral("pendingConfirmation")).toMap();
	QCOMPARE(publicConsent.value(QStringLiteral("kind")).toString(), QStringLiteral("publicConsent"));
	QCOMPARE(publicConsent.value(QStringLiteral("confirmActionId")).toString(),
		QStringLiteral("confirmEnablePublicSource"));
	QCOMPARE(publicConsent.value(QStringLiteral("confirmTone")).toString(), QStringLiteral("accent"));
	QVERIFY(publicConsent.value(QStringLiteral("message")).toString().contains(QStringLiteral("IP address")));

	Settings consentSettings;
	consentSettings.bDisablePublicList = true;
	const ModernConnectController::ActionResult consentCancelled =
		controller.invokeAction(QStringLiteral("dismissConfirmation"), {});
	QVERIFY(consentCancelled.publicListDisabledSetting.has_value());
	consentSettings.bDisablePublicList = *consentCancelled.publicListDisabledSetting;
	QVERIFY(consentSettings.bDisablePublicList);
	QCOMPARE(controller.state().value(QStringLiteral("sources")).toList().at(1).toMap()
		.value(QStringLiteral("status")).toString(), QStringLiteral("unavailable"));

	const ModernConnectController::ActionResult consentRetry = controller.invokeAction(
		QStringLiteral("retrySource"), { { QStringLiteral("sourceId"), QStringLiteral("public") } });
	QVERIFY(consentRetry.sourceOperation.has_value());
	QVERIFY(controller.requestPublicListConsent(consentRetry.sourceOperation->generation));
	const ModernConnectController::ActionResult consentConfirmed =
		controller.invokeAction(QStringLiteral("confirmEnablePublicSource"), {});
	QVERIFY(consentConfirmed.publicListDisabledSetting.has_value());
	consentSettings.bDisablePublicList = *consentConfirmed.publicListDisabledSetting;
	QVERIFY(!consentSettings.bDisablePublicList);
	QVERIFY(consentConfirmed.sourceOperation.has_value());
	QCOMPARE(consentConfirmed.sourceOperation->sourceID, QStringLiteral("public"));
	QCOMPARE(consentConfirmed.sourceOperation->generation, consentRetry.sourceOperation->generation);

	ModernConnectController::ServerEntry stockholm;
	stockholm.id = QStringLiteral("registry:stockholm-1");
	stockholm.label = QStringLiteral("Stockholm Community");
	stockholm.host = QStringLiteral("se.example.test");
	stockholm.port = 64738;
	stockholm.country = QStringLiteral("Sweden");
	stockholm.region = QStringLiteral("Stockholm");
	stockholm.ping = 18;
	stockholm.users = 42;
	stockholm.maxUsers = 128;
	ModernConnectController::ServerEntry gothenburg;
	gothenburg.label = QStringLiteral("Gothenburg Voice");
	gothenburg.host = QStringLiteral("gbg.example.test");
	gothenburg.port = 64739;
	gothenburg.country = QStringLiteral("Sweden");
	gothenburg.region = QStringLiteral("Gothenburg");

	QVERIFY(!controller.applySourceServers(QStringLiteral("public"),
		consentConfirmed.sourceOperation->generation + 1, { stockholm }));
	QVERIFY(controller.applySourceServers(QStringLiteral("public"), consentConfirmed.sourceOperation->generation,
		{ stockholm, gothenburg }));
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("sourceRows")).toList().size(), 2);
	const QVariantMap firstPublic = state.value(QStringLiteral("sourceRows")).toList().constFirst().toMap();
	QCOMPARE(firstPublic.value(QStringLiteral("sourceId")).toString(), QStringLiteral("public"));
	// Source IDs are opaque and their source-local component is percent encoded so
	// adapter-provided delimiters can never collide with the source separator.
	QCOMPARE(firstPublic.value(QStringLiteral("id")).toString(), QStringLiteral("public:registry%3Astockholm-1"));
	QCOMPARE(firstPublic.value(QStringLiteral("usersValue")).toString(), QStringLiteral("42/128"));

	controller.updateField(QStringLiteral("connect.filter"), QStringLiteral("gothenburg"));
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("sourceRows")).toList().size(), 1);
	const QVariantMap filteredPublic = state.value(QStringLiteral("sourceRows")).toList().constFirst().toMap();
	QCOMPARE(filteredPublic.value(QStringLiteral("host")).toString(), QStringLiteral("gbg.example.test"));
	controller.invokeAction(QStringLiteral("selectServer"),
		{ { QStringLiteral("sourceId"), QStringLiteral("public") },
		  { QStringLiteral("id"), filteredPublic.value(QStringLiteral("id")) } });
	const ModernConnectController::ActionResult connectPublic =
		controller.invokeAction(QStringLiteral("connect"), {});
	QVERIFY(connectPublic.connectionRequest.has_value());
	QCOMPARE(connectPublic.connectionRequest->host, QStringLiteral("gbg.example.test"));
	QCOMPARE(connectPublic.connectionRequest->port, 64739);
	QCOMPARE(connectPublic.connectionRequest->username, QStringLiteral("community-user"));

	controller.open({ favorite }, settings);
	controller.invokeAction(QStringLiteral("editFavorite"), { { QStringLiteral("index"), 0 } });
	controller.updateField(QStringLiteral("host"), QStringLiteral("draft.example.test"));
	QVERIFY(controller.state().value(QStringLiteral("editorDirty")).toBool());
	const ModernConnectController::ActionResult hiddenSourceChange = controller.invokeAction(
		QStringLiteral("selectSource"), { { QStringLiteral("sourceId"), QStringLiteral("public") } });
	QVERIFY(!hiddenSourceChange.stateChanged);
	state = controller.state();
	QVERIFY(state.value(QStringLiteral("editorOpen")).toBool());
	QCOMPARE(state.value(QStringLiteral("activeSource")).toString(), QStringLiteral("favorites"));
	const QVariantMap draftHost = state.value(QStringLiteral("sections")).toList().constFirst().toMap()
		.value(QStringLiteral("fields")).toList().at(1).toMap();
	QCOMPARE(draftHost.value(QStringLiteral("value")).toString(), QStringLiteral("draft.example.test"));

	controller.invokeAction(QStringLiteral("backToFavorites"), {});
	state = controller.state();
	QVERIFY(state.value(QStringLiteral("editorOpen")).toBool());
	QCOMPARE(state.value(QStringLiteral("pendingConfirmation")).toMap()
		.value(QStringLiteral("kind")).toString(), QStringLiteral("discard"));
	controller.invokeAction(QStringLiteral("dismissConfirmation"), {});
	QVERIFY(controller.state().value(QStringLiteral("editorOpen")).toBool());
	controller.invokeAction(QStringLiteral("backToFavorites"), {});
	controller.invokeAction(QStringLiteral("confirmDiscardEditor"), {});
	state = controller.state();
	QVERIFY(!state.value(QStringLiteral("editorOpen")).toBool());
	QCOMPARE(state.value(QStringLiteral("activeSource")).toString(), QStringLiteral("favorites"));

	controller.invokeAction(QStringLiteral("selectSource"),
		{ { QStringLiteral("sourceId"), QStringLiteral("favorites") } });
	controller.invokeAction(QStringLiteral("editFavorite"), { { QStringLiteral("index"), 0 } });
	ModernConnectController::ActionResult removeRequest =
		controller.invokeAction(QStringLiteral("removeFavorite"), {});
	QVERIFY(!removeRequest.favoritesToSave.has_value());
	QCOMPARE(controller.favorites().size(), 1);
	QCOMPARE(controller.state().value(QStringLiteral("pendingConfirmation")).toMap()
		.value(QStringLiteral("kind")).toString(), QStringLiteral("remove"));
	ModernConnectController::ActionResult removeConfirmed =
		controller.invokeAction(QStringLiteral("confirmRemoveFavorite"), {});
	QVERIFY(removeConfirmed.favoritesToSave.has_value());
	QVERIFY(removeConfirmed.favoritesToSave->isEmpty());

	ModernConnectController::ServerEntry lanServer;
	lanServer.id = QStringLiteral("bonjour:studio");
	lanServer.label = QStringLiteral("Studio LAN");
	lanServer.host = QStringLiteral("studio.local");
	lanServer.port = 64738;
	const quint64 liveLanGeneration = controller.beginSourceRefresh(QStringLiteral("lan"));
	QVERIFY(controller.applySourceServers(QStringLiteral("lan"), liveLanGeneration, { lanServer }));
	lanServer.host = QStringLiteral("studio-renamed.local");
	QVERIFY(controller.applySourceServers(QStringLiteral("lan"), liveLanGeneration, { lanServer }));
	controller.invokeAction(QStringLiteral("selectSource"),
		{ { QStringLiteral("sourceId"), QStringLiteral("lan") } });
	QCOMPARE(controller.state().value(QStringLiteral("sourceRows")).toList().constFirst().toMap()
		.value(QStringLiteral("host")).toString(), QStringLiteral("studio-renamed.local"));

	const quint64 lanGeneration = controller.beginSourceRefresh(QStringLiteral("lan"));
	QVERIFY(lanGeneration > 0);
	QVERIFY(controller.applySourceError(QStringLiteral("lan"), lanGeneration,
		QStringLiteral("Local discovery failed")));
	controller.invokeAction(QStringLiteral("selectSource"),
		{ { QStringLiteral("sourceId"), QStringLiteral("lan") } });
	state = controller.state();
	QCOMPARE(state.value(QStringLiteral("sources")).toList().at(2).toMap()
		.value(QStringLiteral("status")).toString(), QStringLiteral("error"));
	const ModernConnectController::ActionResult retry = controller.invokeAction(QStringLiteral("retrySource"), {});
	QVERIFY(retry.sourceOperation.has_value());
	const ModernConnectController::ActionResult cancel = controller.invokeAction(QStringLiteral("cancelSource"), {});
	QVERIFY(cancel.sourceOperation.has_value());
	QCOMPARE(cancel.sourceOperation->operation, QStringLiteral("cancel"));
	QVERIFY(!controller.applySourceServers(QStringLiteral("lan"), retry.sourceOperation->generation, {}));

	controller.open({ favorite }, settings);
	const ModernConnectController::ActionResult staleRequest = controller.invokeAction(
		QStringLiteral("selectSource"), { { QStringLiteral("sourceId"), QStringLiteral("public") } });
	QVERIFY(staleRequest.sourceOperation.has_value());
	QVERIFY(controller.requestPublicListConsent(staleRequest.sourceOperation->generation));
	const quint64 newerGeneration = controller.beginSourceRefresh(QStringLiteral("public"));
	QVERIFY(newerGeneration > staleRequest.sourceOperation->generation);
	const ModernConnectController::ActionResult staleConsent =
		controller.invokeAction(QStringLiteral("confirmEnablePublicSource"), {});
	QVERIFY(staleConsent.stateChanged);
	QVERIFY(!staleConsent.publicListDisabledSetting.has_value());
	QVERIFY(!staleConsent.sourceOperation.has_value());
	QVERIFY(controller.state().value(QStringLiteral("pendingConfirmation")).toMap().isEmpty());
}

void TestModernDialogControllers::connectDiscoveryParsesRegistryRowsDeterministically() {
	const QByteArray registryXml = QByteArrayLiteral(
		"<servers>"
		"<server name=\"Zeta Voice\" ip=\"zeta.example.test\" port=\"64738\" country=\"Sweden\" "
		"continent_code=\"eu\"/>"
		"<server name=\"Duplicate endpoint\" ip=\"zeta.example.test\" port=\"64738\"/>"
		"<server name=\"Alpha Voice\" url=\"mumble://alpha.example.test:64739\" country=\"Norway\" "
		"continent_code=\"eu\"/>"
		"<server name=\"Invalid\" ip=\"invalid.example.test\" port=\"0\"/>"
		"</servers>");
	const ModernConnectDiscoveryService::PublicListParseResult parsed =
		ModernConnectDiscoveryService::parsePublicServerList(registryXml);
	QVERIFY2(parsed.ok, qPrintable(parsed.error));
	QCOMPARE(parsed.servers.size(), 2);
	QCOMPARE(parsed.servers.at(0).label, QStringLiteral("Alpha Voice"));
	QCOMPARE(parsed.servers.at(0).host, QStringLiteral("alpha.example.test"));
	QCOMPARE(parsed.servers.at(0).port, static_cast< unsigned short >(64739));
	QCOMPARE(parsed.servers.at(0).country, QStringLiteral("Norway"));
	QCOMPARE(parsed.servers.at(0).region, QStringLiteral("Europe"));
	QCOMPARE(parsed.servers.at(1).id, QStringLiteral("registry:zeta.example.test:64738"));

	const ModernConnectDiscoveryService::PublicListParseResult malformed =
		ModernConnectDiscoveryService::parsePublicServerList(QByteArrayLiteral("<servers><server"));
	QVERIFY(!malformed.ok);
	QVERIFY(!malformed.error.isEmpty());

	const ModernConnectDiscoveryService::PublicListParseResult oversized =
		ModernConnectDiscoveryService::parsePublicServerList(QByteArray(4 * 1024 * 1024 + 1, 'x'));
	QVERIFY(!oversized.ok);
	QVERIFY(!oversized.error.isEmpty());
}

void TestModernDialogControllers::mediaAutomationReadinessStateIsTyped() {
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");
	const QString automationSource = readTestSource(automationPath);
	QVERIFY(!automationSource.isEmpty());

	const qsizetype helperStart = automationSource.indexOf(
		QStringLiteral("QVariantMap automationMediaControllerState(MediaSessionBackend *media)"));
	const qsizetype helperEnd = automationSource.indexOf(
		QStringLiteral("QObject *automationFindRichPreviewCard"), helperStart);
	QVERIFY(helperStart >= 0);
	QVERIFY(helperEnd > helperStart);
	const QString helperBody = automationSource.mid(helperStart, helperEnd - helperStart);

	for (const QString &field : { QStringLiteral("active"), QStringLiteral("state"),
			 QStringLiteral("errorCode"), QStringLiteral("detached"), QStringLiteral("provider"),
			 QStringLiteral("position"), QStringLiteral("duration"), QStringLiteral("syncGeneration"),
			 QStringLiteral("sharedAvailable"), QStringLiteral("sharedJoined"),
			 QStringLiteral("sharedHost"), QStringLiteral("sharedSessionId"),
			 QStringLiteral("sharedScopeId"), QStringLiteral("sharedHostSession"),
			 QStringLiteral("sharedParticipantSessions"), QStringLiteral("sharedOperationStatus"),
			 QStringLiteral("sharedOperationError"),
			 QStringLiteral("rendererPresent"), QStringLiteral("rendererState"),
			 QStringLiteral("rendererReady"), QStringLiteral("windowPresent"),
			 QStringLiteral("windowReady"), QStringLiteral("windowComponentFailed"),
			 QStringLiteral("surfaceVerified"), QStringLiteral("transportVerified"),
			 QStringLiteral("playbackVerified"), QStringLiteral("surfaceVerificationState"),
			 QStringLiteral("surfaceVerificationEvidence"), QStringLiteral("surfaceVerificationDetail") }) {
		QVERIFY2(helperBody.contains(QStringLiteral("\"%1\"").arg(field)),
			qPrintable(QStringLiteral("Missing typed media automation field: %1").arg(field)));
	}
	QVERIFY(helperBody.contains(QStringLiteral("media->active()")));
	QVERIFY(helperBody.contains(QStringLiteral("media->state()")));
	QVERIFY(helperBody.contains(QStringLiteral("media->errorCode()")));
	QVERIFY(helperBody.contains(QStringLiteral("media->detached()")));
	QVERIFY(helperBody.contains(QStringLiteral("media->provider()")));
	QVERIFY(helperBody.contains(QStringLiteral("\"mediaSession.inline\"")));
	QVERIFY(helperBody.contains(QStringLiteral("\"mediaSession.window\"")));
	QVERIFY(helperBody.contains(QStringLiteral("property(\"rendererState\")")));
	QVERIFY(helperBody.contains(QStringLiteral("property(\"documentReady\")")));

	const qsizetype commandStart = automationSource.indexOf(
		QStringLiteral("if (command == QLatin1String(\"qmlReadinessState\"))"));
	const qsizetype commandEnd = automationSource.indexOf(
		QStringLiteral("if (command == QLatin1String(\"pttLifecycleProbe\"))"), commandStart);
	QVERIFY(commandStart >= 0);
	QVERIFY(commandEnd > commandStart);
	const QString commandBody = automationSource.mid(commandStart, commandEnd - commandStart);
	QVERIFY(commandBody.contains(QStringLiteral("\"mediaActive\"")));
	QVERIFY(commandBody.contains(QStringLiteral("\"media\"), automationMediaLifecycleState(host)")));
}

void TestModernDialogControllers::conversationAutomationUsesLiveTypedControllers() {
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	const QString mainQmlPath = QFINDTESTDATA("../../mumble/qml-shell/Main.qml");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");
	QVERIFY2(!mainQmlPath.isEmpty(), "Main.qml test data was not found");
	const QString automationSource = readTestSource(automationPath);
	const QString mainQmlSource = readTestSource(mainQmlPath);
	QVERIFY(!automationSource.isEmpty());
	QVERIFY(!mainQmlSource.isEmpty());

	const qsizetype directMessageHelperStart = automationSource.indexOf(
		QStringLiteral("QVariantMap automationDirectMessageState(DirectMessageController *directMessages)"));
	const qsizetype directMessageHelperEnd = automationSource.indexOf(
		QStringLiteral("QVariantMap automationMediaControllerState"), directMessageHelperStart);
	QVERIFY(directMessageHelperStart >= 0);
	QVERIFY(directMessageHelperEnd > directMessageHelperStart);
	const QString directMessageHelper = automationSource.mid(
		directMessageHelperStart, directMessageHelperEnd - directMessageHelperStart);
	for (const QString &field : { QStringLiteral("activeSessionId"), QStringLiteral("activeScopeToken"),
			 QStringLiteral("conversationOpen"), QStringLiteral("canSend"), QStringLiteral("mode"),
			 QStringLiteral("historyLoading"), QStringLiteral("historyError"),
			 QStringLiteral("conversations"), QStringLiteral("messages") }) {
		QVERIFY2(directMessageHelper.contains(QStringLiteral("\"%1\"").arg(field)),
			qPrintable(QStringLiteral("Missing typed direct-message automation field: %1").arg(field)));
	}
	QVERIFY(directMessageHelper.contains(QStringLiteral("directMessages->summaryModel()")));
	QVERIFY(directMessageHelper.contains(QStringLiteral("directMessages->timelineModel()")));

	for (const QString &command : { QStringLiteral("directMessageState"), QStringLiteral("sendDirectMessage"),
			 QStringLiteral("closeDirectMessage"), QStringLiteral("setDirectMessageMode"),
			 QStringLiteral("markDirectMessageRead"), QStringLiteral("watchTogetherState"),
			 QStringLiteral("startWatchTogether"), QStringLiteral("watchTogetherAction"),
			 QStringLiteral("requestPreviewHydration"), QStringLiteral("richPreviewState"),
			 QStringLiteral("invokeRichPreviewAction"), QStringLiteral("screenShareViewerState"),
			 QStringLiteral("screenShareViewerAction"), QStringLiteral("toastState"),
			 QStringLiteral("publishToast"), QStringLiteral("dismissToast"),
			 QStringLiteral("directMessageReply"), QStringLiteral("directMessageCancelReply"),
			 QStringLiteral("directMessageRetry"), QStringLiteral("directMessageDelete"),
			 QStringLiteral("directMessageToggleReaction"), QStringLiteral("directMessageChooseAttachment"),
			 QStringLiteral("directMessageRemoveAttachment"), QStringLiteral("directMessageRetryAttachment"),
			 QStringLiteral("directMessageOpenAttachment"), QStringLiteral("directMessageDownloadAttachment"),
			 QStringLiteral("directMessageHydrateContent") }) {
		QVERIFY2(automationSource.contains(QStringLiteral("QLatin1String(\"%1\")").arg(command)),
			qPrintable(QStringLiteral("Missing live automation command: %1").arg(command)));
	}
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->sendDraft()")));
	QVERIFY(automationSource.contains(QStringLiteral("media->startShared(url, provider, title)")));
	QVERIFY(automationSource.contains(QStringLiteral("media->joinShared()")));
	QVERIFY(automationSource.contains(QStringLiteral("media->leaveShared()")));
	QVERIFY(automationSource.contains(QStringLiteral("media->endShared()")));
	QVERIFY(automationSource.contains(QStringLiteral("media->transferSharedHost(sessionId)")));
	QVERIFY(automationSource.contains(QStringLiteral("media->reopenSharedPlayer()")));
	QVERIFY(automationSource.contains(QStringLiteral(
		"host->commandController()->requestPreviewHydration(scopeToken, messageIds, highPriority)")));
	QVERIFY(automationSource.contains(QStringLiteral("automationLiveRichPreviewState(host, messageId)")));
	QVERIFY(automationSource.contains(QStringLiteral("timeline->rowForStableId(stableId)")));
	QVERIFY(automationSource.contains(QStringLiteral("\"requestInlinePlaybackWithFocus\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"requestCurrentDirectMediaPopout\"")));
	QVERIFY(automationSource.contains(QStringLiteral("\"watchTogetherRequested\"")));
	QVERIFY(automationSource.contains(QStringLiteral("QGuiApplication::topLevelWindows()")));
	QVERIFY(automationSource.contains(QStringLiteral("\"screenShare.viewer\"")));
	QVERIFY(automationSource.contains(QStringLiteral("backend->setPaused(true)")));
	QVERIFY(automationSource.contains(QStringLiteral("backend->setAudioMuted(true)")));
	QVERIFY(automationSource.contains(QStringLiteral("backend->setAudioVolume(volume)")));
	QVERIFY(automationSource.contains(QStringLiteral("backend->requestRetry()")));
	QVERIFY(automationSource.contains(QStringLiteral("backend->requestStop()")));
	QVERIFY(automationSource.contains(QStringLiteral("toast->publish(")));
	QVERIFY(automationSource.contains(QStringLiteral("toast->dismiss()")));
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->replyToMessage(stableId)")));
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->retryMessage(stableId)")));
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->deleteMessage(stableId)")));
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->toggleMessageReaction(stableId, emoji)")));
	QVERIFY(automationSource.contains(QStringLiteral("directMessages->requestContentHydration(stableId,")));
	QVERIFY(automationSource.contains(QStringLiteral("requiresNativeDialog")));
	for (const QString &field : { QStringLiteral("visible"), QStringLiteral("tone"),
			 QStringLiteral("title"), QStringLiteral("message"), QStringLiteral("actionId"),
			 QStringLiteral("repeatCount") }) {
		QVERIFY2(automationSource.contains(QStringLiteral("QStringLiteral(\"%1\"), toast->").arg(field)),
			qPrintable(QStringLiteral("Missing typed toast automation field: %1").arg(field)));
	}
	QVERIFY(automationSource.contains(QStringLiteral("QStringLiteral(\"revision\")")));
	QVERIFY(automationSource.contains(QStringLiteral("toast->revision()")));

	const qsizetype snapshotStart = automationSource.indexOf(
		QStringLiteral("QVariantMap ModernUiAutomationServer::buildStateResponse() const"));
	const qsizetype snapshotEnd = automationSource.indexOf(
		QStringLiteral("void ModernUiAutomationServer::writeResponse"), snapshotStart);
	QVERIFY(snapshotStart >= 0);
	QVERIFY(snapshotEnd > snapshotStart);
	const QString snapshotBody = automationSource.mid(snapshotStart, snapshotEnd - snapshotStart);
	QVERIFY(snapshotBody.contains(QStringLiteral("automationDirectMessageState(host->directMessageController())")));
	QVERIFY(snapshotBody.contains(QStringLiteral("automationMediaControllerState(host->mediaSession())")));
	QVERIFY(snapshotBody.contains(QStringLiteral("captureWindowReady(QStringLiteral(\"direct-message\"))")));
	QVERIFY(snapshotBody.contains(QStringLiteral("automationScreenShareViewerStates()")));
	QVERIFY(snapshotBody.contains(QStringLiteral("automationToastState(host->toastController())")));
}

void TestModernDialogControllers::detachedMediaWindowWaitsForRuntimeReadiness() {
	const QString mainPath = QFINDTESTDATA("../../mumble/qml-shell/Main.qml");
	const QString windowPath = QFINDTESTDATA("../../mumble/qml-shell/MediaSessionWindow.qml");
	const QString directMessagePath = QFINDTESTDATA("../../mumble/qml-shell/DirectMessageWindow.qml");
	const QString profileFactoryPath = QFINDTESTDATA("../../mumble/QmlMediaProfileFactory.cpp");
	QVERIFY2(!mainPath.isEmpty(), "Main.qml test data was not found");
	QVERIFY2(!windowPath.isEmpty(), "MediaSessionWindow.qml test data was not found");
	QVERIFY2(!directMessagePath.isEmpty(), "DirectMessageWindow.qml test data was not found");
	QVERIFY2(!profileFactoryPath.isEmpty(), "QmlMediaProfileFactory.cpp test data was not found");

	const QString mainSource = readTestSource(mainPath);
	const QString windowSource = readTestSource(windowPath);
	const QString directMessageSource = readTestSource(directMessagePath);
	const QString profileFactorySource = readTestSource(profileFactoryPath);
	QVERIFY(mainSource.contains(QStringLiteral("property bool runtimePresentationStarted: false")));
	QVERIFY(mainSource.contains(QStringLiteral(
		"active: root.mediaSessionWindowRequested && runtimePresentationStarted")));
	QVERIFY(mainSource.contains(QStringLiteral("root.mediaRuntimePreparing")));
	QVERIFY(mainSource.contains(QStringLiteral("root.mediaRuntimeError.length > 0")));
	QVERIFY(mainSource.contains(QStringLiteral(
		"readonly property bool mediaRuntimeReady")));
	QVERIFY(mainSource.contains(QStringLiteral(
		"visible: root.mediaSessionWindowUnavailable")));
	QVERIFY(mainSource.contains(QStringLiteral(
		"visible: root.mediaSessionWindowComponentFailed")));
	QVERIFY(mainSource.contains(QStringLiteral("mediaProfiles.retryRuntime()")));
	QVERIFY(windowSource.contains(QStringLiteral(
		"readonly property bool providerSurfaceAllowed: providerSurfaceRequested")));
	QVERIFY(windowSource.contains(QStringLiteral(
		"&& !nativeDirectMedia && mediaRuntimeReady")));
	QVERIFY(windowSource.contains(QStringLiteral("active: mediaWindow.providerSurfaceAllowed")));
	QVERIFY(windowSource.contains(QStringLiteral("mediaWindow.mediaProfileFactory.videoProfile")));
	QVERIFY(windowSource.contains(QStringLiteral("onClosing: function(close) { handleWindowClosing(close) }")));
	QVERIFY(windowSource.contains(QStringLiteral("if (!mediaSession.active)")));
	QVERIFY(windowSource.contains(QStringLiteral("close.accepted = true")));
	QVERIFY(windowSource.contains(QStringLiteral("close.accepted = false")));
	QVERIFY(windowSource.contains(QStringLiteral("controls.requestClose()")));
	QVERIFY(mainSource.contains(QStringLiteral("function startWatchTogether(url, provider, title)")));
	QVERIFY(mainSource.contains(QStringLiteral("onManagedImageOpenRequested")));
	QVERIFY(mainSource.contains(QStringLiteral("onPreviewSizePresetRequested")));
	QVERIFY(directMessageSource.contains(QStringLiteral("root.watchTogetherRequested(url, provider, title)")));
	QVERIFY(directMessageSource.contains(QStringLiteral("root.managedImageOpenRequested(")));
	QVERIFY(directMessageSource.contains(QStringLiteral("savedSizePreset: root.savedPreviewSizePreset")));
	QVERIFY(profileFactorySource.contains(QStringLiteral(
		"if (m_runtimeReady) emit profilesChanged();")));
}

void TestModernDialogControllers::automationWalkProbesUseDeterministicFixtures() {
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	const QString mainQmlPath = QFINDTESTDATA("../../mumble/qml-shell/Main.qml");
	const QString fixturePath = QFINDTESTDATA("../../mumble/QmlVisualFixtureController.cpp");
	const QString stonksHeaderPath = QFINDTESTDATA("../../mumble/qml-shell/StonksHeader.qml");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");
	QVERIFY2(!mainQmlPath.isEmpty(), "Main.qml test data was not found");
	QVERIFY2(!fixturePath.isEmpty(), "QmlVisualFixtureController.cpp test data was not found");
	QVERIFY2(!stonksHeaderPath.isEmpty(), "StonksHeader.qml test data was not found");

	const QString automationSource = readTestSource(automationPath);
	const QString mainQmlSource = readTestSource(mainQmlPath);
	const QString fixtureSource = readTestSource(fixturePath);
	const QString stonksHeaderSource = readTestSource(stonksHeaderPath);
	QVERIFY(!automationSource.isEmpty());
	QVERIFY(!mainQmlSource.isEmpty());
	QVERIFY(!fixtureSource.isEmpty());
	QVERIFY(!stonksHeaderSource.isEmpty());

	const qsizetype watchStart = automationSource.indexOf(
		QStringLiteral("if (command == QLatin1String(\"watchTogetherLifecycleProbe\"))"));
	const qsizetype watchEnd = automationSource.indexOf(
		QStringLiteral("if (command == QLatin1String(\"attachmentModelProbe\"))"), watchStart);
	QVERIFY(watchStart >= 0);
	QVERIFY(watchEnd > watchStart);
	const QString watchBody = automationSource.mid(watchStart, watchEnd - watchStart);
	QVERIFY(watchBody.contains(QStringLiteral("MediaSessionBackend media;")));
	QVERIFY(watchBody.contains(
		QStringLiteral("https://www.youtube-nocookie.com/embed/automation-lifecycle")));
	QVERIFY(watchBody.contains(QStringLiteral("media.openInline(url, provider, sessionID)")));
	QVERIFY(watchBody.contains(QStringLiteral("media.applyRemoteState(url, provider, sessionID")));
	QVERIFY(watchBody.contains(QStringLiteral("\"inlinePresentation\"")));
	QVERIFY(watchBody.contains(QStringLiteral("\"rendererActivated\"")));
	QVERIFY(watchBody.contains(QStringLiteral("\"remoteApplied\"")));
	QVERIFY(watchBody.contains(QStringLiteral("\"closedState\"")));
	QVERIFY(!watchBody.contains(QStringLiteral("https://example.com/watch-together")));
	QVERIFY(!watchBody.contains(QStringLiteral("host->mediaSession()")));

	const qsizetype stonksStart = automationSource.indexOf(
		QStringLiteral("QVariantMap automationStonksStateProbe(const QString &variant)"));
	const qsizetype stonksEnd = automationSource.indexOf(
		QStringLiteral("QVariantMap automationStonksDialogProbe(const QString &variant)"), stonksStart);
	QVERIFY(stonksStart >= 0);
	QVERIFY(stonksEnd > stonksStart);
	const QString stonksBody = automationSource.mid(stonksStart, stonksEnd - stonksStart);
	QVERIFY(stonksBody.contains(QStringLiteral("\"tickerBannerEnabled\"), true")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"tickerPlacement\"), QStringLiteral(\"bottom\")")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"tickerDirection\"), QStringLiteral(\"left\")")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"tickerSpeed\"), QStringLiteral(\"normal\")")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"disableTickerAnimation\"), true")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"automationHeaderVisible\"), true")));
	QVERIFY(stonksBody.contains(QStringLiteral("\"disableQuoteLookup\"), true")));
	QVERIFY(stonksHeaderSource.contains(
		QStringLiteral("stonks.disableTickerAnimation === true")));
	QVERIFY(stonksHeaderSource.contains(
		QStringLiteral("!automationAnimationDisabled && tickerRows.length > 0")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("objectName: \"stonksTickerTop\"")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("objectName: \"stonksTickerAboveComposer\"")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("objectName: \"stonksTickerWindowTop\"")));
	QVERIFY(mainQmlSource.contains(QStringLiteral("objectName: \"stonksTickerBottom\"")));
	QVERIFY(!mainQmlSource.contains(QStringLiteral("objectName: \"stonksConversationHeader\"")));

	const qsizetype menuStart = mainQmlSource.indexOf(QStringLiteral("function openAutomationMenuProbe(variant)"));
	const qsizetype menuEnd = mainQmlSource.indexOf(
		QStringLiteral("function directMessageAutomationSurfaceState(variant)"), menuStart);
	QVERIFY(menuStart >= 0);
	QVERIFY(menuEnd > menuStart);
	const QString menuBody = mainQmlSource.mid(menuStart, menuEnd - menuStart);
	QVERIFY(menuBody.contains(QStringLiteral("candidateIsSelf")));
	QVERIFY(menuBody.contains(QStringLiteral("root.navigationRailModel.get(index)")));
	QVERIFY(menuBody.contains(QStringLiteral("rail.requestParticipantMenu(participantId")));
	QVERIFY(menuBody.contains(QStringLiteral("\"fixtureUsed\": false")));
	QVERIFY(!menuBody.contains(QStringLiteral("automation:participant:4294967295")));
	QVERIFY(fixtureSource.contains(QStringLiteral("QStringLiteral(\"isSelf\"), true")));
}

void TestModernDialogControllers::persistentChatAttachmentIoIsStrictAndAsynchronous() {
	QImage image(3, 2, QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(QStringLiteral("#4b7bec")));
	QByteArray pngBytes;
	QBuffer pngBuffer(&pngBytes);
	QVERIFY(pngBuffer.open(QIODevice::WriteOnly));
	QVERIFY(image.save(&pngBuffer, "PNG"));
	pngBuffer.close();
	QVERIFY(!pngBytes.isEmpty());

	const auto dataUrl = [](const QString &mime, const QByteArray &bytes) {
		return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(bytes.toBase64()));
	};
	const mumble::chatattachmentio::InlineDataImagePayload valid =
		mumble::chatattachmentio::decodeAndValidateInlineDataImage(
			dataUrl(QStringLiteral("image/png"), pngBytes), 1024 * 1024);
	QVERIFY(valid.isValid());
	QCOMPARE(valid.bytes, pngBytes);
	QCOMPARE(valid.mimeType, QStringLiteral("image/png"));
	QCOMPARE(valid.fileExtension, QStringLiteral("png"));

	const auto mismatch = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/jpeg"), pngBytes), 1024 * 1024);
	QCOMPARE(mismatch.errorCode, QStringLiteral("mime-mismatch"));
	const auto invalidMime = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/svg+xml"), pngBytes), 1024 * 1024);
	QCOMPARE(invalidMime.errorCode, QStringLiteral("invalid-mime"));

	QByteArray truncated = pngBytes;
	truncated.chop(12);
	const auto truncatedResult = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/png"), truncated), 1024 * 1024);
	QCOMPARE(truncatedResult.errorCode, QStringLiteral("truncated-or-trailing-image"));
	QByteArray trailing = pngBytes;
	trailing.append("unexpected");
	const auto trailingResult = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/png"), trailing), 1024 * 1024);
	QCOMPARE(trailingResult.errorCode, QStringLiteral("truncated-or-trailing-image"));

	QByteArray jpegBytes;
	QBuffer jpegBuffer(&jpegBytes);
	QVERIFY(jpegBuffer.open(QIODevice::WriteOnly));
	QVERIFY(image.save(&jpegBuffer, "JPEG"));
	jpegBuffer.close();
	QVERIFY(mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/jpeg"), jpegBytes), 1024 * 1024).isValid());
	QByteArray trailingJpeg = jpegBytes;
	trailingJpeg.append("unexpected");
	trailingJpeg.append(QByteArray("\xFF\xD9", 2));
	QCOMPARE(mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/jpeg"), trailingJpeg), 1024 * 1024).errorCode,
		QStringLiteral("truncated-or-trailing-image"));

	const QByteArray gifBytes = QByteArray::fromBase64(
		QByteArrayLiteral("R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=="));
	QVERIFY(mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/gif"), gifBytes), 1024 * 1024).isValid());
	QByteArray trailingGif = gifBytes;
	trailingGif.append("unexpected");
	trailingGif.append('\x3B');
	QCOMPARE(mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/gif"), trailingGif), 1024 * 1024).errorCode,
		QStringLiteral("truncated-or-trailing-image"));

	const auto invalidEncoding = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/png"), pngBytes) + QLatin1Char('!'), 1024 * 1024);
	QCOMPARE(invalidEncoding.errorCode, QStringLiteral("invalid-encoding"));
	const auto tooLarge = mumble::chatattachmentio::decodeAndValidateInlineDataImage(
		dataUrl(QStringLiteral("image/png"), pngBytes), pngBytes.size() - 1);
	QCOMPARE(tooLarge.errorCode, QStringLiteral("too-large"));

	QString cleanupPath;
	{
		const auto lease = mumble::chatattachmentio::TemporaryDirectoryLease::create(
			QDir::temp().filePath(QStringLiteral("mumble-chat-attachments-test-XXXXXX")), 123);
		QVERIFY(lease);
		QCOMPARE(lease->retainedBytes(), 123ULL);
		cleanupPath = lease->path();
		QVERIFY(QFileInfo::exists(QDir(cleanupPath).filePath(QStringLiteral(".mumble-owner"))));
		QLockFile competingOwner(QDir(cleanupPath).filePath(QStringLiteral(".mumble-owner.lock")));
		competingOwner.setStaleLockTime(0);
		QVERIFY(!competingOwner.tryLock(0));
		QFile temporaryPayload(QDir(cleanupPath).filePath(QStringLiteral("payload.bin")));
		QVERIFY(temporaryPayload.open(QIODevice::WriteOnly));
		QCOMPARE(temporaryPayload.write("payload"), 7LL);
	}
	QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(cleanupPath), 5000);

	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString messagesPath = QFINDTESTDATA("../../mumble/Messages.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!messagesPath.isEmpty(), "Messages.cpp test data was not found");
	const QString mainWindowSource = readTestSource(mainWindowPath);
	const QString messagesSource = readTestSource(messagesPath);
	const qsizetype saveStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::savePersistentChatInlineDataImage"));
	const qsizetype saveEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::openImageDialog"), saveStart);
	QVERIFY(saveStart >= 0);
	QVERIFY(saveEnd > saveStart);
	const QString saveBody = mainWindowSource.mid(saveStart, saveEnd - saveStart);
	QVERIFY(saveBody.contains(QStringLiteral("queuePersistentChatAssetIo(")));
	QVERIFY(saveBody.indexOf(QStringLiteral("queuePersistentChatAssetIo("))
		< saveBody.indexOf(QStringLiteral("QSaveFile output(destination)")));
	QVERIFY(saveBody.contains(QStringLiteral("startOperation(operationID")));
	QVERIFY(saveBody.contains(QStringLiteral("finishOperation(operationID")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("directory->setAutoRemove(false)")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("PersistentChatAttachmentCleanupPool")));
	QVERIFY(mainWindowSource.contains(
		QStringLiteral("persistentChatAttachmentCleanupPool().start(")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("retryDelaysMs { 0, 100, 500, 2000, 5000 }")));
	QVERIFY(mainWindowSource.contains(
		QStringLiteral("scheduleStalePersistentChatAttachmentDirectoryCleanup()")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("static std::once_flag scheduled")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("std::call_once(scheduled")));
	QVERIFY(mainWindowSource.contains(QStringLiteral(".mumble-owner")));
	QVERIFY(mainWindowSource.contains(QStringLiteral(".mumble-owner.lock")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("ownerLock.setStaleLockTime(0)")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("if (!ownerLock.tryLock(0)) continue")));
	const qsizetype timeoutStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::armPersistentChatAssetDownloadTimeout"));
	const qsizetype timeoutEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::ensurePersistentChatPreviewSiteSnapshot"), timeoutStart);
	QVERIFY(timeoutStart >= 0);
	QVERIFY(timeoutEnd > timeoutStart);
	const QString timeoutBody = mainWindowSource.mid(timeoutStart, timeoutEnd - timeoutStart);
	QVERIFY(timeoutBody.indexOf(QStringLiteral("finishPersistentChatAssetDownloadOperations("))
		< timeoutBody.indexOf(QStringLiteral("m_persistentChatAssetDownloads.erase(timedOut)")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("void MainWindow::cancelPersistentChatAttachmentWork")));
	QVERIFY(mainWindowSource.contains(
		QStringLiteral("cancelGroupsWithPrefix(this, QStringLiteral(\"attachment-io:\"))")));
	QVERIFY(mainWindowSource.count(QStringLiteral("finishPersistentChatAssetDownloadOperations(")) >= 3);
	QVERIFY(mainWindowSource.contains(QStringLiteral("m_persistentChatAssetConnectionGeneration")));

	const qsizetype chunkStart = messagesSource.indexOf(
		QStringLiteral("void MainWindow::msgChatAssetChunk"));
	const qsizetype chunkEnd = messagesSource.indexOf(
		QStringLiteral("void MainWindow::msgChatEmbedState"), chunkStart);
	QVERIFY(chunkStart >= 0);
	QVERIFY(chunkEnd > chunkStart);
	const QString chunkBody = messagesSource.mid(chunkStart, chunkEnd - chunkStart);
	const qsizetype moveIndex = chunkBody.indexOf(
		QStringLiteral("PersistentChatAssetDownload download = std::move(*it)"));
	const qsizetype queueIndex = chunkBody.indexOf(QStringLiteral("queuePersistentChatAssetIo("));
	const qsizetype desktopIndex = chunkBody.indexOf(QStringLiteral("QDesktopServices::openUrl"));
	QVERIFY(moveIndex >= 0);
	QVERIFY(queueIndex > moveIndex);
	QVERIFY(desktopIndex > queueIndex);
	QVERIFY(chunkBody.contains(QStringLiteral("updateProgress(operationID")));
	QVERIFY(chunkBody.contains(QStringLiteral("connectionGeneration != m_persistentChatAssetConnectionGeneration")));
	QVERIFY(chunkBody.contains(QStringLiteral("m_persistentChatAttachmentIoOperationIDs.unite(ioOperationIDs)")));
	QVERIFY(chunkBody.contains(
		QStringLiteral("!previewKeys.isEmpty() && isPersistentChatPlayableMediaMime(mime)")));
	QVERIFY(chunkBody.indexOf(QStringLiteral("publishPersistentChatAttachmentImageUpdate(attachmentMessageKeys)"))
		< chunkBody.indexOf(QStringLiteral("schedulePendingPersistentChatAttachmentImageDownloads()")));
	QVERIFY(chunkBody.contains(QStringLiteral("maximumRetainedDirectories = 8")));
	QVERIFY(chunkBody.contains(QStringLiteral("maximumRetainedBytes = 256ULL * 1024ULL * 1024ULL")));
	QVERIFY(chunkBody.contains(QStringLiteral("m_persistentChatOpenAttachmentDirectories.erase(")));

	const qsizetype hydrateStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::ensurePersistentChatAttachmentImageDownload"));
	const qsizetype hydrateEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::openPersistentChatAttachment"), hydrateStart);
	QVERIFY(hydrateStart >= 0);
	QVERIFY(hydrateEnd > hydrateStart);
	const QString hydrateBody = mainWindowSource.mid(hydrateStart, hydrateEnd - hydrateStart);
	QVERIFY(mainWindowSource.contains(
		QStringLiteral("PERSISTENT_CHAT_ATTACHMENT_HYDRATION_MAX_PENDING_TRANSFERS = 256")));
	QVERIFY(mainWindowSource.contains(
		QStringLiteral("PERSISTENT_CHAT_ATTACHMENT_HYDRATION_MAX_MESSAGE_KEYS_PER_TRANSFER = 64")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		"m_persistentChatPendingAttachmentHydrations.size()")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		">= PERSISTENT_CHAT_ATTACHMENT_HYDRATION_MAX_PENDING_TRANSFERS")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		"m_persistentChatPendingAttachmentHydrationOrder.takeFirst()")));
	QVERIFY(hydrateBody.contains(QStringLiteral("downloadIt->savePaths.isEmpty()")));
	QVERIFY(hydrateBody.contains(QStringLiteral("downloadIt->openFileName.isEmpty()")));
	QVERIFY(hydrateBody.contains(QStringLiteral("pendingMessageKeyCount += assetIt->size()")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		">= PERSISTENT_CHAT_ATTACHMENT_HYDRATION_MAX_MESSAGE_KEYS_PER_TRANSFER")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		"chat.attachment_hydration.queue_transfer_dropped")));
	QVERIFY(hydrateBody.contains(QStringLiteral(
		"chat.attachment_hydration.queue_message_key_dropped")));

	const qsizetype snapshotStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::publishPersistentChatSnapshot()"));
	const qsizetype snapshotEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::syncPersistentChatGatewayHandler()"), snapshotStart);
	QVERIFY(snapshotStart >= 0);
	QVERIFY(snapshotEnd > snapshotStart);
	const QString snapshotBody = mainWindowSource.mid(snapshotStart, snapshotEnd - snapshotStart);
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"m_persistentChatPendingAttachmentHydrations.clear()")));
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"m_persistentChatPendingAttachmentHydrationOrder.clear()")));
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"!downloadIt->savePaths.isEmpty() || !downloadIt->openFileName.isEmpty()")));
	QVERIFY(snapshotBody.contains(QStringLiteral("downloadIt->attachmentAssetIDs.clear()")));
	QVERIFY(snapshotBody.contains(QStringLiteral("downloadIt->attachmentMessageKeys.clear()")));
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"downloadIt = m_persistentChatAssetDownloads.erase(downloadIt)")));
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"chat.attachment_hydration.scope_switch.pending_dropped")));
	QVERIFY(snapshotBody.contains(QStringLiteral(
		"chat.attachment_hydration.scope_switch.active_dropped")));

	const qsizetype refillStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::schedulePendingPersistentChatAttachmentImageDownloads"));
	const qsizetype refillEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::finishPersistentChatAssetDownloadOperations"), refillStart);
	QVERIFY(refillStart >= 0);
	QVERIFY(refillEnd > refillStart);
	const QString refillBody = mainWindowSource.mid(refillStart, refillEnd - refillStart);
	QVERIFY(refillBody.contains(QStringLiteral("m_persistentChatPendingAttachmentHydrationOrder.takeFirst()")));
	QVERIFY(refillBody.contains(QStringLiteral("ensurePersistentChatAttachmentImageDownload(")));
	QVERIFY(refillBody.contains(QStringLiteral("downloadIt->savePaths.isEmpty()")));
	QVERIFY(refillBody.contains(QStringLiteral("downloadIt->openFileName.isEmpty()")));
	QVERIFY(!refillBody.contains(QStringLiteral("publishPersistentChatAttachmentImageUpdate(")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("m_persistentChatMessageIndexValid")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("m_modernShellMessageDtoCacheKeysByMessage.take(")));
}

void TestModernDialogControllers::persistentChatHistoryWarmupStaysViewportBounded() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");
	const QString mainWindowSource = readTestSource(mainWindowPath);
	const QString automationSource = readTestSource(automationPath);

	const qsizetype historyStart = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::handlePersistentChatHistory"));
	const qsizetype historyEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::handlePersistentChatReadState"), historyStart);
	QVERIFY(historyStart >= 0);
	QVERIFY(historyEnd > historyStart);
	const QString historyBody = mainWindowSource.mid(historyStart, historyEnd - historyStart);
	QVERIFY(historyBody.contains(QStringLiteral("for (const MumbleProto::ChatMessage &message : msg.messages())")));
	QVERIFY(historyBody.contains(QStringLiteral("evictModernShellMessageDtoCacheForMessage(message)")));
	QVERIFY(historyBody.contains(QStringLiteral("chat.handle.history.evict")));
	QVERIFY(historyBody.contains(QStringLiteral("chat.handle.history.dispatch")));
	QVERIFY(!historyBody.contains(QStringLiteral("clearModernShellMessageDtoCache(")));
	QVERIFY(!historyBody.contains(QStringLiteral("warmupPersistentChatPreviews(msg)")));

	const qsizetype previewKeyStart = mainWindowSource.indexOf(
		QStringLiteral("std::optional< QString > MainWindow::persistentChatPreviewKey"));
	const qsizetype previewKeyEnd = mainWindowSource.indexOf(
		QStringLiteral("void MainWindow::rememberPersistentChatPreviewInputs"), previewKeyStart);
	QVERIFY(previewKeyStart >= 0);
	QVERIFY(previewKeyEnd > previewKeyStart);
	const QString previewKeyBody = mainWindowSource.mid(previewKeyStart, previewKeyEnd - previewKeyStart);
	QVERIFY(previewKeyBody.contains(QStringLiteral("m_persistentChatPreviewKeyCache.constFind(cacheKey)")));
	QVERIFY(previewKeyBody.contains(QStringLiteral("m_persistentChatPreviewKeyCache.insert(cacheKey")));
	QVERIFY(previewKeyBody.contains(QStringLiteral("chat.preview.key.cache_hit")));
	QVERIFY(previewKeyBody.contains(QStringLiteral("chat.preview.key.cache_miss")));

	QVERIFY(mainWindowSource.contains(QStringLiteral("persistentChatMessageContainsLegacyInlineImageHtml")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("fastLegacyInlineFirstPaint")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("%1:inline:pending")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("modern.message_dto_cache.full_first_paint_hit")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("knownEstimatedBytes")));

	QVERIFY(automationSource.contains(QStringLiteral("qmlTimelinePresentationState")));
	QVERIFY(automationSource.contains(QStringLiteral("timelinePresentationState")));
}

void TestModernDialogControllers::persistentChatProviderImagesStayManagedAndCancelable() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	const QString source = readTestSource(mainWindowPath);
	QVERIFY(!source.isEmpty());

	const qsizetype dtoStart = source.indexOf(
		QStringLiteral("QVariantMap MainWindow::modernShellPreviewStateForKey"));
	const qsizetype dtoEnd = source.indexOf(
		QStringLiteral("QVariantMap MainWindow::buildModernShellCachedMessageState"), dtoStart);
	QVERIFY(dtoStart >= 0);
	QVERIFY(dtoEnd > dtoStart);
	const QString dto = source.mid(dtoStart, dtoEnd - dtoStart);
	QVERIFY(dto.contains(QStringLiteral("QVariantMap metadataState = preview.metadata")));
	QVERIFY(dto.contains(QStringLiteral(
		"const QUrl canonicalPreviewUrl(preview.canonicalUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"const QString displayHost = previewDisplayHost(canonicalPreviewUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"previewState.insert(QStringLiteral(\"host\"), displayHost)")));
	QVERIFY(dto.contains(QStringLiteral(
		"trimmedPreviewText(preview.title, 512)")));
	QVERIFY(dto.contains(QStringLiteral(
		"trimmedPreviewText(preview.subtitle, 512)")));
	QVERIFY(dto.contains(QStringLiteral(
		"const QString sanitizedDescription = trimmedPreviewText(preview.description, 4096)")));
	QVERIFY(dto.contains(QStringLiteral(
		"previewDescriptionIsPlaceholder(sanitizedDescription)")));
	QVERIFY(dto.contains(QStringLiteral(
		"previewState.insert(QStringLiteral(\"errorDescription\"), sanitizedDescription)")));
	QVERIFY(dto.contains(QStringLiteral(
		"previewEmbedTargetForUrl(canonicalPreviewUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"previewOEmbedTargetForUrl(canonicalPreviewUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"richPreviewProviderForUrl(canonicalPreviewUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"gameStorePreviewInfoForUrl(canonicalPreviewUrl)")));
	QVERIFY(dto.contains(QStringLiteral(
		"metadataState.insert(QStringLiteral(\"provider\"), providerKey)")));
	QVERIFY(dto.contains(QStringLiteral(
		"metadataState.insert(QStringLiteral(\"previewProvider\"), providerKey)")));
	QVERIFY(dto.contains(QStringLiteral(
		"const std::optional< PersistentChatPreviewProviderInfo > providerInfo")));
	QVERIFY(dto.contains(QStringLiteral(
		"metadataState.insert(QStringLiteral(\"providerName\"), providerInfo->siteLabel)")));
	QVERIFY(dto.contains(QStringLiteral("metadataState.remove(metadataKey)")));
	QVERIFY(dto.contains(QStringLiteral("metadataState.insert(metadataKey, managedIt->providerUrl)")));
	for (const QString &field : {
			 QStringLiteral("forumPostAuthorAvatarUrl"), QStringLiteral("xAvatarUrl"),
			 QStringLiteral("instagramAvatarUrl"), QStringLiteral("githubOwnerAvatarUrl"),
			 QStringLiteral("steamHeaderImage"), QStringLiteral("steamCapsuleImage") }) {
		QVERIFY2(dto.contains(field), qPrintable(QStringLiteral("missing managed DTO field %1").arg(field)));
	}

	const qsizetype requestStart = source.indexOf(
		QStringLiteral("void MainWindow::requestPersistentChatPreviewMetadataImageProvider"));
	const qsizetype requestEnd = source.indexOf(
		QStringLiteral("bool MainWindow::requestPersistentChatRemotePlayableMediaCache"), requestStart);
	QVERIFY(requestStart >= 0);
	QVERIFY(requestEnd > requestStart);
	const QString request = source.mid(requestStart, requestEnd - requestStart);
	QVERIFY(request.contains(QStringLiteral("sourceIdentity.size() > 16384")));
	QVERIFY(request.contains(QStringLiteral("requestUrl.scheme().toLower() != QLatin1String(\"https\")")));
	QVERIFY(request.contains(QStringLiteral("!isSafePreviewTarget(requestUrl)")));
	QVERIFY(request.contains(QStringLiteral("ManualRedirectPolicy")));
	QVERIFY(request.contains(QStringLiteral("redirectCount > 3")));
	QVERIFY(request.contains(QStringLiteral("PREVIEW_MAX_IMAGE_BYTES")));
	QVERIFY(request.contains(QStringLiteral("supportedMimes")));
	QVERIFY(request.contains(QStringLiteral("startPersistentChatPreviewGet(request, previewKey)")));
	QVERIFY(request.contains(QStringLiteral("metadata.value(metadataKey).toString().trimmed() != sourceIdentity")));
	QVERIFY(request.contains(QStringLiteral("managedIt->source != sourceIdentity")));

	QVERIFY(source.contains(QStringLiteral("validatedSteamPreviewMediaItems(mediaItems)")));
	QVERIFY(source.contains(QStringLiteral("previewIt->mediaItems = validatedSteamPreviewMediaItems")));
	QVERIFY(source.contains(QStringLiteral("application/vnd.apple.mpegurl")));
	QVERIFY(source.contains(QStringLiteral("application/dash+xml")));
	QVERIFY(source.contains(QStringLiteral("normalized.thumbnail")));
	QVERIFY(source.contains(QStringLiteral("normalized.poster")));
	QVERIFY(source.contains(QStringLiteral("persistentChatMediaItemImageKey")));
	QVERIFY(!source.contains(QStringLiteral(
		"metadata.insert(QStringLiteral(\"steamMediaItems\"), mediaItems)")));
	QVERIFY(source.contains(QStringLiteral("cancelPersistentChatPreviewNetworkRequests")));
	QVERIFY(source.contains(QStringLiteral("constexpr int RICH_PREVIEW_METADATA_VERSION = 11")));
	QVERIFY(source.contains(QStringLiteral(
		"providerMetadata.insert(QStringLiteral(\"previewProvider\"), target->providerKey)")));
	QVERIFY(source.contains(QStringLiteral(
		"success && !target->socialPost && previewDescriptionIsPlaceholder(previewIt->description)")));
	QVERIFY(source.contains(QStringLiteral(
		"https://www.youtube-nocookie.com/embed/%1")));
	QVERIFY(source.contains(QStringLiteral(
		"PersistentChatPreviewEmbedTarget { QStringLiteral(\"twitch\"), embedUrl, QStringLiteral(\"wide\") }")));
}

void TestModernDialogControllers::serverIdentityImagePickerIsManagedAndAsynchronous() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	const QString source = readTestSource(mainWindowPath);
	QVERIFY(!source.isEmpty());

	QVERIFY(source.contains(QStringLiteral("browseServerIdentityImage")));
	QVERIFY(source.contains(QStringLiteral("removeServerIdentityImage")));
	QVERIFY(source.contains(QStringLiteral("browseActionId")));
	QVERIFY(source.contains(QStringLiteral("removeActionId")));
	QVERIFY(source.contains(QStringLiteral("ModernServerIdentityImageLoadResult")));
	QVERIFY(source.contains(QStringLiteral("QFutureWatcher< ModernServerIdentityImageLoadResult >")));
	QVERIFY(source.contains(QStringLiteral("QtConcurrent::run([path]()")));
	QVERIFY(source.contains(QStringLiteral("ModernServerIdentityImageMaxInputBytes + 1")));
	QVERIFY(source.contains(QStringLiteral("image://mumble/")));
	QVERIFY(source.contains(QStringLiteral("data:image/png;base64,%1")));
	QVERIFY(source.contains(QStringLiteral("startOperation(")));
	QVERIFY(source.contains(QStringLiteral("finishOperation(operationID")));
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

void TestModernDialogControllers::dialogControllerRestoresTransientDialogParents() {
	Settings settings;
	ModernDialogController controller;
	controller.openSettings(settings, QStringLiteral("look"));
	controller.updateField(QStringLiteral("settings"), QStringLiteral("look.modernTheme"),
						   QStringLiteral("latte"));
	const QVariantMap settingsDraft = controller.state();
	QCOMPARE(settingsDraft.value(QStringLiteral("id")).toString(), QStringLiteral("settings"));
	QCOMPARE(settingsDraft.value(QStringLiteral("activePage")).toString(), QStringLiteral("look"));

	QVariantMap updateAction;
	updateAction.insert(QStringLiteral("id"), QStringLiteral("plugins.startUpdates"));
	updateAction.insert(QStringLiteral("label"), QStringLiteral("Update selected"));
	updateAction.insert(QStringLiteral("closesDialog"), true);
	QVariantMap updateDialog;
	updateDialog.insert(QStringLiteral("id"), QStringLiteral("pluginUpdates"));
	updateDialog.insert(QStringLiteral("kind"), QStringLiteral("form"));
	updateDialog.insert(QStringLiteral("actions"), QVariantList { updateAction });

	QCOMPARE(controller.openGenericDialog(updateDialog).value(QStringLiteral("id")).toString(),
			 QStringLiteral("pluginUpdates"));
	const ModernDialogController::ActionResult updateResult = controller.invokeAction(
		QStringLiteral("pluginUpdates"), QStringLiteral("plugins.startUpdates"), {});
	QVERIFY(updateResult.closeDialog);
	QCOMPARE(controller.activeDialogID(), QStringLiteral("settings"));
	QCOMPARE(controller.state(), settingsDraft);

	QVariantMap parentField;
	parentField.insert(QStringLiteral("id"), QStringLiteral("note"));
	parentField.insert(QStringLiteral("value"), QStringLiteral("before"));
	QVariantMap parentSection;
	parentSection.insert(QStringLiteral("fields"), QVariantList { parentField });
	QVariantMap parentDialog;
	parentDialog.insert(QStringLiteral("id"), QStringLiteral("pluginAbout"));
	parentDialog.insert(QStringLiteral("kind"), QStringLiteral("about"));
	parentDialog.insert(QStringLiteral("sections"), QVariantList { parentSection });
	controller.openGenericDialog(parentDialog);
	controller.updateField(QStringLiteral("pluginAbout"), QStringLiteral("note"), QStringLiteral("edited"));
	const QVariantMap genericParent = controller.state();

	QVariantMap overwriteDialog;
	overwriteDialog.insert(QStringLiteral("id"), QStringLiteral("pluginInstallConfirm"));
	overwriteDialog.insert(QStringLiteral("kind"), QStringLiteral("confirm"));
	controller.openGenericDialog(overwriteDialog);
	QCOMPARE(controller.close(QStringLiteral("pluginInstallConfirm")), genericParent);
	QCOMPARE(controller.activeDialogID(), QStringLiteral("pluginAbout"));

	controller.close(QStringLiteral("pluginAbout"));
	QCOMPARE(controller.activeDialogID(), QStringLiteral("settings"));
	QCOMPARE(controller.state(), settingsDraft);

	// Opening a root surface intentionally discards transient ancestry. A later
	// close must not resurrect an unrelated stale Settings draft.
	controller.openGenericDialog(updateDialog);
	controller.openConnect({}, settings);
	QCOMPARE(controller.activeDialogID(), QStringLiteral("connect"));
	controller.close(QStringLiteral("connect"));
	QVERIFY(controller.activeDialogID().isEmpty());
	QVERIFY(!controller.state().value(QStringLiteral("open")).toBool());

	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	const QString mainWindowSource = readTestSource(mainWindowPath);
	const qsizetype helperStart =
		mainWindowSource.indexOf(QStringLiteral("void MainWindow::openModernGenericDialog"));
	const qsizetype helperEnd = mainWindowSource.indexOf(QStringLiteral("\n}"), helperStart);
	QVERIFY(helperStart >= 0 && helperEnd > helperStart);
	const QString helperSource = mainWindowSource.mid(helperStart, helperEnd - helperStart);
	QVERIFY2(!helperSource.contains(QStringLiteral("cancelPendingPluginInstallConfirmation")),
			 "Transient child presentation must not cancel the parent plugin overwrite operation");
}

void TestModernDialogControllers::dialogControllerRefreshesSameDialogWithoutStacking() {
	ModernDialogController controller;
	QVariantMap first;
	first.insert(QStringLiteral("id"), QStringLiteral("pluginUpdates"));
	first.insert(QStringLiteral("kind"), QStringLiteral("form"));
	first.insert(QStringLiteral("title"), QStringLiteral("First"));
	controller.openGenericDialog(first);

	QVariantMap refreshed = first;
	refreshed.insert(QStringLiteral("title"), QStringLiteral("Refreshed"));
	QCOMPARE(controller.openGenericDialog(refreshed).value(QStringLiteral("title")).toString(),
			 QStringLiteral("Refreshed"));
	controller.close(QStringLiteral("pluginUpdates"));
	QVERIFY(controller.activeDialogID().isEmpty());
	QVERIFY(!controller.state().value(QStringLiteral("open")).toBool());
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

void TestModernDialogControllers::chatHistoryGrantDialogWaitsForMatchedAcknowledgement() {
	auto hiddenField = [](const QString &id, const QVariant &value) {
		return QVariantMap { { QStringLiteral("id"), id }, { QStringLiteral("type"), QStringLiteral("hidden") },
			{ QStringLiteral("value"), value } };
	};
	QVariantMap apply { { QStringLiteral("id"), QStringLiteral("saveChatHistoryGrant") },
		{ QStringLiteral("label"), QStringLiteral("Apply") }, { QStringLiteral("enabled"), true },
		{ QStringLiteral("closesDialog"), false } };
	QVariantMap section { { QStringLiteral("fields"), QVariantList {
		hiddenField(QStringLiteral("session"), 17U),
		hiddenField(QStringLiteral("persistentUserId"), 8),
		hiddenField(QStringLiteral("connectionGeneration"), quint64(23)) } } };
	QVariantMap dialog { { QStringLiteral("id"), QStringLiteral("chatHistoryGrant:17") },
		{ QStringLiteral("kind"), QStringLiteral("form") },
		{ QStringLiteral("sections"), QVariantList { section } },
		{ QStringLiteral("actions"), QVariantList { apply } } };

	ModernDialogController controller;
	controller.openGenericDialog(dialog);
	const ModernDialogController::ActionResult submitted = controller.invokeAction(
		QStringLiteral("chatHistoryGrant:17"), QStringLiteral("saveChatHistoryGrant"), {});
	QVERIFY(submitted.genericAction.has_value());
	QCOMPARE(submitted.genericAction->fieldValues.value(QStringLiteral("session")).toUInt(), 17U);
	QCOMPARE(submitted.genericAction->fieldValues.value(QStringLiteral("persistentUserId")).toInt(), 8);
	QCOMPARE(submitted.genericAction->fieldValues.value(QStringLiteral("connectionGeneration")).toULongLong(),
		quint64(23));
	QVERIFY(!submitted.closeDialog);
	QCOMPARE(controller.activeDialogID(), QStringLiteral("chatHistoryGrant:17"));

	apply.insert(QStringLiteral("enabled"), false);
	dialog.insert(QStringLiteral("actions"), QVariantList { apply });
	const QVariantMap pendingState = controller.openGenericDialog(dialog);
	QVERIFY(!dialogAction(pendingState, QStringLiteral("saveChatHistoryGrant"))
		.value(QStringLiteral("enabled")).toBool());
	QCOMPARE(controller.activeDialogID(), QStringLiteral("chatHistoryGrant:17"));
	const ModernDialogController::ActionResult duplicate = controller.invokeAction(
		QStringLiteral("chatHistoryGrant:17"), QStringLiteral("saveChatHistoryGrant"), {});
	QVERIFY(!duplicate.genericAction.has_value());
	QVERIFY(!duplicate.stateChanged);

	apply.insert(QStringLiteral("enabled"), true);
	QVariantMap errorNote { { QStringLiteral("id"), QStringLiteral("grant.error") },
		{ QStringLiteral("type"), QStringLiteral("note") },
		{ QStringLiteral("value"), QStringLiteral("Permission changed; try again.") },
		{ QStringLiteral("tone"), QStringLiteral("danger") } };
	dialog.insert(QStringLiteral("actions"), QVariantList { apply });
	dialog.insert(QStringLiteral("sections"), QVariantList {
		section, QVariantMap { { QStringLiteral("id"), QStringLiteral("chatHistoryGrantStatus") },
			{ QStringLiteral("fields"), QVariantList { errorNote } } } });
	const QVariantMap rejectedState = controller.openGenericDialog(dialog);
	QVERIFY(dialogAction(rejectedState, QStringLiteral("saveChatHistoryGrant"))
		.value(QStringLiteral("enabled")).toBool());
	QCOMPARE(controller.activeDialogID(), QStringLiteral("chatHistoryGrant:17"));
	controller.close(QStringLiteral("chatHistoryGrant:17"));
	QVERIFY(controller.activeDialogID().isEmpty());

	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString messagesPath = QFINDTESTDATA("../../mumble/Messages.cpp");
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	QVERIFY(!mainWindowPath.isEmpty());
	QVERIFY(!messagesPath.isEmpty());
	QVERIFY(!automationPath.isEmpty());
	const QString mainWindowSource = readTestSource(mainWindowPath);
	const QString messagesSource = readTestSource(messagesPath);
	const QString automationSource = readTestSource(automationPath);
	QVERIFY(mainWindowSource.contains(QStringLiteral("if (user->iId < 0)")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("ChatFeatureHistoryGrantAcks")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("if (m_pendingChatHistoryGrant)")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("boundPersistentUserID")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("boundConnectionGeneration")));
	QVERIFY(mainWindowSource.contains(QStringLiteral("ack_timeout")));
	QVERIFY(messagesSource.contains(QStringLiteral("msg.request_id() != m_pendingChatHistoryGrant->requestID")));
	QVERIFY(messagesSource.contains(QStringLiteral("currentTarget != pending.target")));
	QVERIFY(messagesSource.contains(QStringLiteral("acknowledgedScope != pending.scope")));
	QVERIFY(messagesSource.contains(QStringLiteral("ChatHistoryGrantSync_Result_Rejected")));
	QVERIFY(messagesSource.contains(QStringLiteral("ChatHistoryGrantSync_Result_Accepted")));
	QVERIFY(messagesSource.contains(QStringLiteral("ChatHistoryGrantSync_Result_NoOp")));
	QVERIFY(automationSource.contains(QStringLiteral("persistentUserId")));
	QVERIFY(automationSource.contains(QStringLiteral("connectionGeneration")));
}

QTEST_MAIN(TestModernDialogControllers)
#include "TestModernDialogControllers.moc"
