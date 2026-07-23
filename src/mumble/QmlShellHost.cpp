// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlShellHost.h"

#include "ClientActionRegistry.h"
#include "ComposerController.h"
#include "ModernServerAdminController.h"
#include "ModernRecorderController.h"
#include "ModernRecorderRuntimeAdapter.h"
#include "QmlClientModels.h"
#include "QmlPerformanceMonitor.h"
#include "QmlImageProvider.h"
#include "QmlMediaProfileFactory.h"
#include "QmlThemeController.h"
#include "QmlWindowStateController.h"
#include "QmlWindowStateStore.h"
#include "ScreenShareVideoItem.h"
#include "UiTheme.h"
#include "Global.h"
#ifdef USE_MANUAL_PLUGIN
#	include "ManualPluginController.h"
#endif

#include <QtCore/QAbstractItemModel>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtQml/qqml.h>

QmlShellHost::QmlShellHost(ClientActionRegistry *actionRegistry, QObject *parent)
	: QObject(parent), m_actionRegistry(actionRegistry),
	  m_sessionController(std::make_unique< ClientSessionController >(this)),
	  m_activeScopeController(std::make_unique< ActiveScopeController >(this)),
	  m_commandController(std::make_unique< UiCommandController >(this)),
	  m_pttSafetyController(std::make_unique< PttSafetyController >(m_commandController.get())),
	  m_roomModel(std::make_unique< RoomModel >(this)),
	  m_navigationModel(std::make_unique< NavigationRailModel >(this)),
	  m_participantModel(std::make_unique< ParticipantModel >(this)),
	  m_chatModel(std::make_unique< ChatTimelineModel >(this)),
	  m_toastController(std::make_unique< ToastController >(this)),
	  m_operationModel(std::make_unique< AsyncOperationModel >(this)),
	  m_operationOverlayModel(std::make_unique< AsyncOperationOverlayProxyModel >(this)),
	  m_actionModel(std::make_unique< ActionModel >(actionRegistry, this)),
	  m_dialogController(std::make_unique< DialogStateController >(this)),
	  m_directMessageController(std::make_unique< DirectMessageController >(this)),
	  m_mediaSession(std::make_unique< MediaSessionBackend >(this)),
	  m_serverAdminController(std::make_unique< ModernServerAdminController >(this)),
	  m_recorderRuntime(std::make_unique< Mumble::ModernRecorderRuntimeAdapter >(this)),
	  m_recorderController(std::make_unique< Mumble::ModernRecorderController >(this)),
	  m_mediaProfileFactory(std::make_unique< QmlMediaProfileFactory >(m_mediaSession.get(), this)),
	  m_selectionState(std::make_unique< QmlSelectionState >(this)),
	  m_performanceMonitor(std::make_unique< QmlPerformanceMonitor >(this)),
	  m_imagePipeline(std::make_shared< QmlImagePipeline >()),
	  m_composerController(std::make_unique< ComposerController >(m_imagePipeline, this)),
	  m_themeController(std::make_unique< QmlThemeController >(this)),
	  m_windowStateController(std::make_unique< QmlWindowStateController >(this)),
	  m_pttWindowStateController(std::make_unique< QmlWindowStateController >(this)),
	  m_auxiliaryWindowStateStore(std::make_unique< QmlWindowStateStore >(
		  Global::get().s.qbaModernAuxiliaryWindowGeometries, this)) {
	m_operationOverlayModel->setSourceModel(m_operationModel.get());
	m_recorderController->setRuntime(m_recorderRuntime.get());
	m_selectionState->bindModels(m_roomModel.get(), m_participantModel.get());
#ifdef USE_MANUAL_PLUGIN
	m_manualPluginController = std::make_unique< ManualPluginController >(this);
#endif
	connect(m_activeScopeController.get(), &ActiveScopeController::canSendChanged, this, [this]() {
		m_composerController->setCanSend(m_activeScopeController->canSend());
	});
	const auto trackModelReset = [this](QAbstractItemModel *model, const QString &modelName) {
		connect(model, &QAbstractItemModel::modelReset, m_performanceMonitor.get(),
			[monitor = m_performanceMonitor.get(), modelName]() { monitor->recordModelReset(modelName); });
	};
	trackModelReset(m_roomModel.get(), QStringLiteral("room"));
	trackModelReset(m_navigationModel.get(), QStringLiteral("navigation"));
	trackModelReset(m_participantModel.get(), QStringLiteral("participant"));
	trackModelReset(m_chatModel.get(), QStringLiteral("chat"));
	trackModelReset(m_operationModel.get(), QStringLiteral("operation"));
	trackModelReset(m_actionModel.get(), QStringLiteral("action"));
}

QmlShellHost::~QmlShellHost() {
	releasePttForSafety(PttSafetyReason::HostDestroyed);
	m_windowStateController->flush();
	m_pttWindowStateController->flush();
	m_auxiliaryWindowStateStore->flush();
	destroyScreenShareViews();
	delete m_pttToolWindow.data();
#ifdef USE_MANUAL_PLUGIN
	delete m_manualPluginWindow.data();
#endif
	m_engine.reset();
	m_window = nullptr;
}

bool QmlShellHost::start(QString *error) {
	if (m_window) {
		return true;
	}
	static const int themeType = qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml-shell/Theme.qml")),
												 "Mumble.Theme", 1, 0, "Theme");
	Q_UNUSED(themeType)
	static const int providerPresentationType = qmlRegisterSingletonType(
		QUrl(QStringLiteral("qrc:/qml-shell/ProviderPresentation.qml")),
		"Mumble.ProviderPresentation", 1, 0, "ProviderPresentation");
	Q_UNUSED(providerPresentationType)
	static const int screenShareVideoType =
		qmlRegisterType< ScreenShareVideoItem >("Mumble.ScreenShare", 1, 0, "ScreenShareVideoItem");
	Q_UNUSED(screenShareVideoType)
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	m_engine = std::make_unique< QQmlApplicationEngine >();
	m_engine->addImageProvider(QStringLiteral("mumble"), new QmlAsyncImageProvider(m_imagePipeline));
	for (QObject *object : { static_cast< QObject * >(m_sessionController.get()),
						  static_cast< QObject * >(m_activeScopeController.get()),
						  static_cast< QObject * >(m_commandController.get()),
						  static_cast< QObject * >(m_roomModel.get()),
						  static_cast< QObject * >(m_navigationModel.get()),
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_composerController.get()),
						  static_cast< QObject * >(m_toastController.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_actionModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_directMessageController.get()),
						  static_cast< QObject * >(m_directMessageController->summaryModel()),
						  static_cast< QObject * >(m_directMessageController->timelineModel()),
						  static_cast< QObject * >(m_mediaSession.get()),
						  static_cast< QObject * >(m_serverAdminController.get()),
						  static_cast< QObject * >(m_serverAdminController->users()),
						  static_cast< QObject * >(m_serverAdminController->bans()),
						  static_cast< QObject * >(m_recorderController.get()),
						  static_cast< QObject * >(m_mediaProfileFactory.get()),
						  static_cast< QObject * >(m_selectionState.get()),
						  static_cast< QObject * >(m_performanceMonitor.get()),
						  static_cast< QObject * >(m_themeController.get()),
						  static_cast< QObject * >(m_auxiliaryWindowStateStore.get()) }) {
		QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
	}
	QQmlContext *context = m_engine->rootContext();
	context->setContextProperty(QStringLiteral("clientSession"), m_sessionController.get());
	context->setContextProperty(QStringLiteral("activeScope"), m_activeScopeController.get());
	context->setContextProperty(QStringLiteral("uiCommands"), m_commandController.get());
	context->setContextProperty(QStringLiteral("roomModel"), m_roomModel.get());
	context->setContextProperty(QStringLiteral("navigationModel"), m_navigationModel.get());
	context->setContextProperty(QStringLiteral("participantModel"), m_participantModel.get());
	context->setContextProperty(QStringLiteral("chatModel"), m_chatModel.get());
	context->setContextProperty(QStringLiteral("composer"), m_composerController.get());
	context->setContextProperty(QStringLiteral("toastState"), m_toastController.get());
	context->setContextProperty(QStringLiteral("operationModel"), m_operationModel.get());
	context->setContextProperty(QStringLiteral("operationOverlayModel"), m_operationOverlayModel.get());
	context->setContextProperty(QStringLiteral("actionModel"), m_actionModel.get());
	context->setContextProperty(QStringLiteral("dialogState"), m_dialogController.get());
	context->setContextProperty(QStringLiteral("directMessages"), m_directMessageController.get());
	context->setContextProperty(QStringLiteral("mediaSession"), m_mediaSession.get());
	context->setContextProperty(QStringLiteral("serverAdmin"), m_serverAdminController.get());
	context->setContextProperty(QStringLiteral("recorder"), m_recorderController.get());
	context->setContextProperty(QStringLiteral("mediaProfiles"), m_mediaProfileFactory.get());
	context->setContextProperty(QStringLiteral("selectionState"), m_selectionState.get());
	context->setContextProperty(QStringLiteral("qmlPerformance"), m_performanceMonitor.get());
	context->setContextProperty(QStringLiteral("clientActions"), m_actionRegistry);
	context->setContextProperty(QStringLiteral("uiTheme"), m_themeController.get());
	context->setContextProperty(QStringLiteral("windowStateStore"), m_auxiliaryWindowStateStore.get());
	connect(m_auxiliaryWindowStateStore.get(), &QmlWindowStateStore::encodedStatesChanged, this,
			[](const QByteArray &states) { Global::get().s.qbaModernAuxiliaryWindowGeometries = states; });
#ifdef USE_MANUAL_PLUGIN
	QQmlEngine::setObjectOwnership(m_manualPluginController.get(), QQmlEngine::CppOwnership);
	context->setContextProperty(QStringLiteral("manualPlugin"), m_manualPluginController.get());
#endif

	const QUrl rootUrl(QStringLiteral("qrc:/qml-shell/Main.qml"));
	m_engine->load(rootUrl);
	if (m_engine->rootObjects().isEmpty()) {
		if (error) {
			*error = tr("The Qt Quick shell could not load its root window.");
		}
		m_engine.reset();
		return false;
	}
	m_window = qobject_cast< QQuickWindow * >(m_engine->rootObjects().constFirst());
	if (!m_window) {
		if (error) {
			*error = tr("The Qt Quick shell root object is not a QQuickWindow.");
		}
		m_engine.reset();
		return false;
	}
	registerCaptureWindow(m_window);
	applyUiThemeNativeTitleBar(m_window);
	connect(m_themeController.get(), &QmlThemeController::themeChanged, this, [this]() {
		const QColor caption = m_themeController->shellBackground();
		const QColor text = m_themeController->textStrong();
		const UiThemeWindowChrome chrome { caption, text, m_themeController->surfaceBorder(),
			text.lightness() > caption.lightness() };
		for (QWindow *window : QGuiApplication::topLevelWindows()) {
			if (qobject_cast< QQuickWindow * >(window)) {
				applyUiThemeNativeTitleBar(window, chrome);
			}
		}
	});
	m_windowStateController->attach(m_window, Global::get().s.qbaModernMainWindowGeometry,
		m_window->minimumSize());
	connect(m_windowStateController.get(), &QmlWindowStateController::encodedStateChanged, this,
			[](const QByteArray &state) { Global::get().s.qbaModernMainWindowGeometry = state; });
	m_window->installEventFilter(this);
	m_performanceMonitor->installInputObserver(m_window);
	connect(m_window, &QQuickWindow::sceneGraphError, this,
			[this](QQuickWindow::SceneGraphError, const QString &) {
				releasePttForSafety(PttSafetyReason::SceneGraphError);
			});
	connect(m_window, &QQuickWindow::beforeRendering, m_performanceMonitor.get(),
			&QmlPerformanceMonitor::markFrameRenderingStarted, Qt::DirectConnection);
	connect(m_window, &QQuickWindow::afterRendering, m_performanceMonitor.get(),
			&QmlPerformanceMonitor::markFrameRenderingFinished, Qt::DirectConnection);
	connect(m_window, &QQuickWindow::frameSwapped, m_performanceMonitor.get(),
			&QmlPerformanceMonitor::markFramePresented, Qt::DirectConnection);
	connect(m_window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
		if (visibility == QWindow::Hidden || visibility == QWindow::Minimized) {
			releasePttForSafety(PttSafetyReason::WindowHidden);
		}
	});
	connect(m_window, &QWindow::activeChanged, this, [this]() {
		if (m_window && !m_window->isActive()) releasePttForSafety(PttSafetyReason::WindowDeactivated);
	});
	connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
		if (state != Qt::ApplicationActive) releasePttForSafety(PttSafetyReason::ApplicationDeactivated);
	});
	connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *window) {
		if (!window || !qobject_cast< QQuickWindow * >(window)) {
			return;
		}
		const QColor caption = m_themeController->shellBackground();
		const QColor text = m_themeController->textStrong();
		applyUiThemeNativeTitleBar(window, { caption, text, m_themeController->surfaceBorder(),
			text.lightness() > caption.lightness() });
	});
	return true;
}

bool QmlShellHost::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_pttToolWindow && event) {
		switch (event->type()) {
			case QEvent::Close:
			case QEvent::Hide:
			case QEvent::WindowDeactivate: releasePttForSafety(PttSafetyReason::WindowDeactivated); break;
			default: break;
		}
	}
	if (watched == m_window && event && event->type() == QEvent::Close) {
		releasePttForSafety(PttSafetyReason::WindowClosing);
		emit closeRequested();
		return true;
	}
	return QObject::eventFilter(watched, event);
}

void QmlShellHost::releasePttForSafety(const PttSafetyReason reason) {
	if (m_pttSafetyController) m_pttSafetyController->release(reason);
}

void QmlShellHost::showRaise() {
	if (!m_window) return;
	m_window->show();
	m_window->raise();
	m_window->requestActivate();
}

QQuickWindow *QmlShellHost::window() const { return m_window; }
ClientSessionController *QmlShellHost::sessionController() const { return m_sessionController.get(); }
ActiveScopeController *QmlShellHost::activeScopeController() const { return m_activeScopeController.get(); }
UiCommandController *QmlShellHost::commandController() const { return m_commandController.get(); }
RoomModel *QmlShellHost::roomModel() const { return m_roomModel.get(); }
NavigationRailModel *QmlShellHost::navigationModel() const { return m_navigationModel.get(); }
ParticipantModel *QmlShellHost::participantModel() const { return m_participantModel.get(); }

void QmlShellHost::clearConnectionState() {
	// Channel IDs and user session IDs are scoped to one server connection. Clear
	// the retained state before another server can publish rows with the same IDs,
	// otherwise Qt Quick may reuse an old delegate as if it represented the same
	// room or participant.
	m_selectionState->applySelection({}, -1, {}, {}, {});
	m_roomModel->clearConnectionState();
	m_navigationModel->clearConnectionState();
	m_participantModel->clear();
	m_composerController->setAutocompleteSources({}, {});
}

ChatTimelineModel *QmlShellHost::chatModel() const { return m_chatModel.get(); }
ComposerController *QmlShellHost::composerController() const { return m_composerController.get(); }
ToastController *QmlShellHost::toastController() const { return m_toastController.get(); }
AsyncOperationModel *QmlShellHost::operationModel() const { return m_operationModel.get(); }
ActionModel *QmlShellHost::actionModel() const { return m_actionModel.get(); }
DialogStateController *QmlShellHost::dialogController() const { return m_dialogController.get(); }
DirectMessageController *QmlShellHost::directMessageController() const { return m_directMessageController.get(); }
MediaSessionBackend *QmlShellHost::mediaSession() const { return m_mediaSession.get(); }
ModernServerAdminController *QmlShellHost::serverAdminController() const {
	return m_serverAdminController.get();
}
Mumble::ModernRecorderController *QmlShellHost::recorderController() const {
	return m_recorderController.get();
}
QmlSelectionState *QmlShellHost::selectionState() const { return m_selectionState.get(); }
QmlPerformanceMonitor *QmlShellHost::performanceMonitor() const { return m_performanceMonitor.get(); }
std::shared_ptr< QmlImagePipeline > QmlShellHost::imagePipeline() const { return m_imagePipeline; }
QString QmlShellHost::registerServerIdentityImage(const QByteArray &imageBytes) {
	if (imageBytes.isEmpty()) {
		m_serverIdentityImageHash.clear();
		m_serverIdentityImageUrl.clear();
		return QString();
	}
	const QByteArray hash = QCryptographicHash::hash(imageBytes, QCryptographicHash::Sha256);
	if (hash == m_serverIdentityImageHash && !m_serverIdentityImageUrl.isEmpty())
		return m_serverIdentityImageUrl;
	const QString stableKey = QStringLiteral("server-identity:%1").arg(QString::fromLatin1(hash.toHex()));
	const QString url = m_imagePipeline->registerEncoded(imageBytes, QByteArrayLiteral("image/png"), stableKey);
	if (url.isEmpty()) return QString();
	m_serverIdentityImageHash = hash;
	m_serverIdentityImageUrl  = url;
	return m_serverIdentityImageUrl;
}
QmlThemeController *QmlShellHost::themeController() const { return m_themeController.get(); }

void QmlShellHost::setVisualFixtureOverrideActive(const bool active) {
	m_visualFixtureOverrideActive = active;
	if (m_window) m_window->setProperty("visualFixtureOverrideActive", active);
	for (QObject *object : { static_cast< QObject * >(m_sessionController.get()),
						  static_cast< QObject * >(m_activeScopeController.get()),
						  static_cast< QObject * >(m_roomModel.get()),
						  static_cast< QObject * >(m_navigationModel.get()),
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_directMessageController.get()),
						  static_cast< QObject * >(m_directMessageController->summaryModel()),
						  static_cast< QObject * >(m_directMessageController->timelineModel()),
						  static_cast< QObject * >(m_serverAdminController.get()),
						  static_cast< QObject * >(m_serverAdminController->users()),
						  static_cast< QObject * >(m_serverAdminController->bans()),
						  static_cast< QObject * >(m_themeController.get()) }) {
		object->setProperty(QmlVisualFixtureMutation::OverrideProperty, active);
		if (!active) object->setProperty(QmlVisualFixtureMutation::WriteProperty, false);
	}
}

void QmlShellHost::setVisualFixtureMutationActive(const bool active) {
	if (!m_visualFixtureOverrideActive && active) return;
	for (QObject *object : { static_cast< QObject * >(m_sessionController.get()),
						  static_cast< QObject * >(m_activeScopeController.get()),
						  static_cast< QObject * >(m_roomModel.get()),
						  static_cast< QObject * >(m_navigationModel.get()),
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_directMessageController.get()),
						  static_cast< QObject * >(m_directMessageController->summaryModel()),
						  static_cast< QObject * >(m_directMessageController->timelineModel()),
						  static_cast< QObject * >(m_serverAdminController.get()),
						  static_cast< QObject * >(m_serverAdminController->users()),
						  static_cast< QObject * >(m_serverAdminController->bans()),
						  static_cast< QObject * >(m_themeController.get()) }) {
		object->setProperty(QmlVisualFixtureMutation::WriteProperty, active);
	}
}

QQuickWindow *QmlShellHost::captureWindowTarget(const QString &windowId, QString *error) const {
	const QString normalizedWindowId = windowId.trimmed().toLower();
	if (normalizedWindowId == QLatin1String("ptt")) {
		return m_pttToolWindow;
	}
#ifdef USE_MANUAL_PLUGIN
	if (normalizedWindowId == QLatin1String("manual-plugin")
		|| normalizedWindowId == QLatin1String("manualplugin")) {
		return m_manualPluginWindow;
	}
#endif
	if (normalizedWindowId == QLatin1String("product-dialog")
		|| normalizedWindowId == QLatin1String("settings")
		|| normalizedWindowId == QLatin1String("direct-message")
		|| normalizedWindowId == QLatin1String("media-session")
		|| normalizedWindowId == QLatin1String("screen-share")
		|| normalizedWindowId == QLatin1String("attachment-viewer")
		|| normalizedWindowId == QLatin1String("image-viewer")) {
		QString surfaceId;
		if (normalizedWindowId == QLatin1String("product-dialog")) {
			surfaceId = QStringLiteral("product-dialog.window");
		} else if (normalizedWindowId == QLatin1String("settings")) {
			surfaceId = QStringLiteral("settings.window");
		} else if (normalizedWindowId == QLatin1String("direct-message")) {
			surfaceId = QStringLiteral("directMessage.window");
		} else if (normalizedWindowId == QLatin1String("media-session")) {
			surfaceId = QStringLiteral("mediaSession.window");
		} else if (normalizedWindowId == QLatin1String("screen-share")) {
			surfaceId = QStringLiteral("screenShare.viewer");
		} else if (normalizedWindowId == QLatin1String("attachment-viewer")) {
			surfaceId = QStringLiteral("attachmentViewer.window");
		} else {
			surfaceId = QStringLiteral("imageViewer.window");
		}
		QQuickWindow *fallback = nullptr;
		for (QWindow *candidate : QGuiApplication::topLevelWindows()) {
			auto *quickWindow = qobject_cast< QQuickWindow * >(candidate);
			if (!quickWindow || quickWindow->property("surfaceId").toString() != surfaceId) continue;
			if (!fallback) fallback = quickWindow;
			if (quickWindow->isVisible() && quickWindow->isExposed()) return quickWindow;
		}
		return fallback;
	}
	if (!normalizedWindowId.isEmpty() && normalizedWindowId != QLatin1String("main")) {
		if (error) *error = tr("Unknown Qt Quick window '%1'.").arg(windowId);
		return nullptr;
	}
	return m_window;
}

void QmlShellHost::registerCaptureWindow(QQuickWindow *window) {
	if (!window) return;
	m_captureReadyWindows.remove(window);
	const QPointer< QQuickWindow > guardedWindow(window);
	connect(window, &QQuickWindow::frameSwapped, this, [this, guardedWindow]() {
		if (guardedWindow && guardedWindow->isVisible() && guardedWindow->isExposed()) {
			m_captureReadyWindows.insert(guardedWindow.data());
		}
	}, Qt::QueuedConnection);
	connect(window, &QQuickWindow::sceneGraphInvalidated, this, [this, guardedWindow]() {
		if (guardedWindow) m_captureReadyWindows.remove(guardedWindow.data());
	}, Qt::QueuedConnection);
	connect(window, &QWindow::visibilityChanged, this, [this, guardedWindow](const QWindow::Visibility visibility) {
		if (!guardedWindow) return;
		m_captureReadyWindows.remove(guardedWindow.data());
		if (visibility != QWindow::Hidden && visibility != QWindow::Minimized) guardedWindow->requestUpdate();
	});
	connect(window, &QObject::destroyed, this, [this, window]() { m_captureReadyWindows.remove(window); });
	if (window->isVisible()) window->requestUpdate();
}

bool QmlShellHost::captureWindowReady(const QString &windowId) const {
	QQuickWindow *targetWindow = captureWindowTarget(windowId);
	const QString normalizedWindowId = windowId.trimmed().toLower();
	if (normalizedWindowId == QLatin1String("product-dialog")
		|| normalizedWindowId == QLatin1String("settings")
		|| normalizedWindowId == QLatin1String("direct-message")
		|| normalizedWindowId == QLatin1String("media-session")
		|| normalizedWindowId == QLatin1String("screen-share")
		|| normalizedWindowId == QLatin1String("attachment-viewer")
		|| normalizedWindowId == QLatin1String("image-viewer")) {
		return targetWindow && targetWindow->isVisible() && targetWindow->isExposed();
	}
	return targetWindow && targetWindow->isVisible() && targetWindow->isExposed()
		&& m_captureReadyWindows.contains(targetWindow);
}

bool QmlShellHost::captureWindow(const QString &path, QString *error, const QString &windowId) const {
	QQuickWindow *targetWindow = captureWindowTarget(windowId, error);

	if (!targetWindow) {
		if (error) {
			const QString normalizedWindowId = windowId.trimmed().toLower();
			if (error->isEmpty()) *error = normalizedWindowId.isEmpty() || normalizedWindowId == QLatin1String("main")
				? tr("The Qt Quick window is not available.")
				: tr("The requested Qt Quick tool window is not visible.");
		}
		return false;
	}
	if (!targetWindow->isVisible()) {
		if (error) *error = tr("The requested Qt Quick window is not visible.");
		return false;
	}
	if (!captureWindowReady(windowId)) {
		if (error) *error = tr("The requested Qt Quick window has not presented its first frame yet; retry capture.");
		return false;
	}
	const QFileInfo fileInfo(path);
	if (fileInfo.filePath().trimmed().isEmpty()) {
		if (error) *error = tr("No capture path was provided.");
		return false;
	}
	if (!QDir().mkpath(fileInfo.absolutePath())) {
		if (error) *error = tr("The capture directory could not be created.");
		return false;
	}
	const QImage image = targetWindow->grabWindow();
	if (image.isNull() || !image.save(fileInfo.absoluteFilePath(), "PNG")) {
		if (error) *error = tr("The Qt Quick window could not be captured.");
		return false;
	}
	return true;
}

bool QmlShellHost::ensurePttToolWindow() {
	if (m_pttToolWindow) return true;
	if (!m_window || !m_engine) return false;

	QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc:/qml-shell/PttToolWindow.qml")));
	if (component.isError()) {
		qWarning("Unable to load the Qt Quick PTT tool: %s", qPrintable(component.errorString()));
		return false;
	}
	QObject *created = component.create(m_engine->rootContext());
	QQuickWindow *window = qobject_cast< QQuickWindow * >(created);
	if (!window) {
		qWarning("The Qt Quick PTT tool root object is not a QQuickWindow");
		delete created;
		return false;
	}
	QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
	m_pttToolWindow = window;
	window->setTransientParent(m_window);
	registerCaptureWindow(window);
	applyUiThemeNativeTitleBar(window);
	window->installEventFilter(this);
	m_pttWindowStateController->attach(window, Global::get().s.qbaPTTButtonWindowGeometry, QSize(240, 140));
	connect(m_pttWindowStateController.get(), &QmlWindowStateController::encodedStateChanged, this,
			[](const QByteArray &state) { Global::get().s.qbaPTTButtonWindowGeometry = state; });
	connect(window, &QQuickWindow::sceneGraphError, this,
			[this](QQuickWindow::SceneGraphError, const QString &) {
				releasePttForSafety(PttSafetyReason::SceneGraphError);
			});
	connect(window, &QObject::destroyed, this, [this]() {
		releasePttForSafety(PttSafetyReason::HostDestroyed);
		m_pttToolWindow = nullptr;
	});
	return true;
}

void QmlShellHost::showPttTool(const bool visible) {
	if (!visible) {
		releasePttForSafety(PttSafetyReason::WindowHidden);
		if (m_pttToolWindow) m_pttToolWindow->hide();
		return;
	}
	if (!ensurePttToolWindow()) return;
	m_pttToolWindow->show();
	m_pttToolWindow->raise();
	m_pttToolWindow->requestActivate();
}

bool QmlShellHost::pttToolVisible() const {
	return m_pttToolWindow && m_pttToolWindow->isVisible();
}

#ifdef USE_MANUAL_PLUGIN
ManualPluginController *QmlShellHost::manualPluginController() const {
	return m_manualPluginController.get();
}

bool QmlShellHost::ensureManualPluginWindow() {
	if (m_manualPluginWindow) return true;
	if (!m_window || !m_engine || !m_manualPluginController) return false;

	QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc:/qml-shell/ManualPluginWindow.qml")));
	if (component.isError()) {
		qWarning("Unable to load the Qt Quick Manual Plugin tool: %s", qPrintable(component.errorString()));
		return false;
	}
	QObject *created = component.create(m_engine->rootContext());
	QQuickWindow *window = qobject_cast< QQuickWindow * >(created);
	if (!window) {
		qWarning("The Qt Quick Manual Plugin tool root object is not a QQuickWindow");
		delete created;
		return false;
	}
	QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
	m_manualPluginWindow = window;
	window->setTransientParent(m_window);
	registerCaptureWindow(window);
	applyUiThemeNativeTitleBar(window);
	m_auxiliaryWindowStateStore->restoreWindow(window, QStringLiteral("manual-plugin"),
		window->minimumSize().width(), window->minimumSize().height());
	connect(window, &QObject::destroyed, this, [this]() { m_manualPluginWindow = nullptr; });
	return true;
}

void QmlShellHost::showManualPluginTool(const bool visible) {
	if (!visible) {
		if (m_manualPluginWindow) m_manualPluginWindow->hide();
		return;
	}
	if (!ensureManualPluginWindow()) return;
	m_manualPluginController->refresh();
	m_manualPluginWindow->show();
	m_manualPluginWindow->raise();
	m_manualPluginWindow->requestActivate();
}

bool QmlShellHost::manualPluginToolVisible() const {
	return m_manualPluginWindow && m_manualPluginWindow->isVisible();
}
#endif

QObject *QmlShellHost::createScreenShareView(QObject *backend) {
	if (!m_window || !backend) return nullptr;
	QQmlEngine::setObjectOwnership(backend, QQmlEngine::CppOwnership);
	QVariant result;
	const bool invoked = QMetaObject::invokeMethod(m_window, "createScreenShareView", Q_RETURN_ARG(QVariant, result),
											  Q_ARG(QVariant, QVariant::fromValue(backend)));
	QObject *view = invoked ? result.value< QObject * >() : nullptr;
	if (view) {
		QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
		if (auto *window = qobject_cast< QQuickWindow * >(view)) {
			window->setTransientParent(m_window);
			registerCaptureWindow(window);
			applyUiThemeNativeTitleBar(window);
			m_auxiliaryWindowStateStore->restoreWindow(window, QStringLiteral("screen-share-viewer"),
				window->minimumSize().width(), window->minimumSize().height());
			// ApplicationWindow's initial visible binding is not a sufficient
			// reopen contract on Windows while the previous viewer is completing
			// deferred destruction. Explicitly show and schedule a frame so a
			// replacement viewer becomes exposed deterministically.
			window->show();
			window->raise();
			window->requestActivate();
			window->requestUpdate();
		}
		m_screenShareViews.insert(view);
		connect(view, &QObject::destroyed, this, [this](QObject *destroyedView) {
			m_screenShareViews.remove(destroyedView);
			m_closingScreenShareViews.remove(destroyedView);
		});
	}
	return view;
}

void QmlShellHost::closeScreenShareView(QObject *view) {
	if (!view || !m_screenShareViews.contains(view) || m_closingScreenShareViews.contains(view)) return;
	m_closingScreenShareViews.insert(view);
	if (!QMetaObject::invokeMethod(view, "closeFromHost")) {
		QMetaObject::invokeMethod(view, "close");
	}
	view->deleteLater();
}

void QmlShellHost::destroyScreenShareViews() {
	// These windows are C++-owned so QML's garbage collector cannot reclaim a live detached view.
	// Destroy them synchronously while their engine and QML context are still valid.
	const QSet< QObject * > views = m_screenShareViews;
	m_screenShareViews.clear();
	m_closingScreenShareViews.clear();
	for (QObject *view : views) {
		if (!view) continue;
		if (!QMetaObject::invokeMethod(view, "closeFromHost")) {
			QMetaObject::invokeMethod(view, "close");
		}
		delete view;
	}
}
