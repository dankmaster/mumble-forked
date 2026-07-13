// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlShellHost.h"

#include "ClientActionRegistry.h"
#include "ComposerController.h"
#include "QmlClientModels.h"
#include "QmlPerformanceMonitor.h"
#include "QmlImageProvider.h"
#include "QmlThemeController.h"
#include "QmlWindowStateController.h"
#include "ScreenShareVideoItem.h"
#include "Global.h"

#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QGuiApplication>
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
	  m_participantModel(std::make_unique< ParticipantModel >(this)),
	  m_chatModel(std::make_unique< ChatTimelineModel >(this)),
	  m_operationModel(std::make_unique< AsyncOperationModel >(this)),
	  m_actionModel(std::make_unique< ActionModel >(actionRegistry, this)),
	  m_dialogController(std::make_unique< DialogStateController >(this)),
	  m_mediaSession(std::make_unique< MediaSessionBackend >(this)),
	  m_selectionState(std::make_unique< QmlSelectionState >(this)),
	  m_performanceMonitor(std::make_unique< QmlPerformanceMonitor >(this)),
	  m_imagePipeline(std::make_shared< QmlImagePipeline >()),
	  m_composerController(std::make_unique< ComposerController >(m_imagePipeline, this)),
	  m_themeController(std::make_unique< QmlThemeController >(this)),
	  m_windowStateController(std::make_unique< QmlWindowStateController >(this)) {
	m_selectionState->bindModels(m_roomModel.get(), m_participantModel.get());
	connect(m_activeScopeController.get(), &ActiveScopeController::canSendChanged, this, [this]() {
		m_composerController->setCanSend(m_activeScopeController->canSend());
	});
}

QmlShellHost::~QmlShellHost() {
	releasePttForSafety(PttSafetyReason::HostDestroyed);
	m_windowStateController->flush();
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
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_composerController.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_actionModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_mediaSession.get()),
						  static_cast< QObject * >(m_selectionState.get()),
						  static_cast< QObject * >(m_performanceMonitor.get()),
						  static_cast< QObject * >(m_themeController.get()) }) {
		QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
	}
	QQmlContext *context = m_engine->rootContext();
	context->setContextProperty(QStringLiteral("clientSession"), m_sessionController.get());
	context->setContextProperty(QStringLiteral("activeScope"), m_activeScopeController.get());
	context->setContextProperty(QStringLiteral("uiCommands"), m_commandController.get());
	context->setContextProperty(QStringLiteral("roomModel"), m_roomModel.get());
	context->setContextProperty(QStringLiteral("participantModel"), m_participantModel.get());
	context->setContextProperty(QStringLiteral("chatModel"), m_chatModel.get());
	context->setContextProperty(QStringLiteral("composer"), m_composerController.get());
	context->setContextProperty(QStringLiteral("operationModel"), m_operationModel.get());
	context->setContextProperty(QStringLiteral("actionModel"), m_actionModel.get());
	context->setContextProperty(QStringLiteral("dialogState"), m_dialogController.get());
	context->setContextProperty(QStringLiteral("mediaSession"), m_mediaSession.get());
	context->setContextProperty(QStringLiteral("selectionState"), m_selectionState.get());
	context->setContextProperty(QStringLiteral("qmlPerformance"), m_performanceMonitor.get());
	context->setContextProperty(QStringLiteral("clientActions"), m_actionRegistry);
	context->setContextProperty(QStringLiteral("uiTheme"), m_themeController.get());

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
	m_windowStateController->attach(m_window, Global::get().s.qbaModernMainWindowGeometry);
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
			&QmlPerformanceMonitor::markFramePresented, Qt::QueuedConnection);
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
	return true;
}

bool QmlShellHost::eventFilter(QObject *watched, QEvent *event) {
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
ParticipantModel *QmlShellHost::participantModel() const { return m_participantModel.get(); }
ChatTimelineModel *QmlShellHost::chatModel() const { return m_chatModel.get(); }
ComposerController *QmlShellHost::composerController() const { return m_composerController.get(); }
AsyncOperationModel *QmlShellHost::operationModel() const { return m_operationModel.get(); }
ActionModel *QmlShellHost::actionModel() const { return m_actionModel.get(); }
DialogStateController *QmlShellHost::dialogController() const { return m_dialogController.get(); }
MediaSessionBackend *QmlShellHost::mediaSession() const { return m_mediaSession.get(); }
QmlSelectionState *QmlShellHost::selectionState() const { return m_selectionState.get(); }
QmlPerformanceMonitor *QmlShellHost::performanceMonitor() const { return m_performanceMonitor.get(); }
std::shared_ptr< QmlImagePipeline > QmlShellHost::imagePipeline() const { return m_imagePipeline; }
QmlThemeController *QmlShellHost::themeController() const { return m_themeController.get(); }

void QmlShellHost::setVisualFixtureOverrideActive(const bool active) {
	m_visualFixtureOverrideActive = active;
	for (QObject *object : { static_cast< QObject * >(m_sessionController.get()),
						  static_cast< QObject * >(m_activeScopeController.get()),
						  static_cast< QObject * >(m_roomModel.get()),
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
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
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_themeController.get()) }) {
		object->setProperty(QmlVisualFixtureMutation::WriteProperty, active);
	}
}

bool QmlShellHost::captureWindow(const QString &path, QString *error) const {
	if (!m_window) {
		if (error) *error = tr("The Qt Quick window is not available.");
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
	const QImage image = m_window->grabWindow();
	if (image.isNull() || !image.save(fileInfo.absoluteFilePath(), "PNG")) {
		if (error) *error = tr("The Qt Quick window could not be captured.");
		return false;
	}
	return true;
}

void QmlShellHost::showPttTool(const bool visible) {
	if (!m_window || !m_engine) return;
	if (!visible) releasePttForSafety(PttSafetyReason::WindowHidden);
	m_window->setProperty("pttToolVisible", visible);
}

QObject *QmlShellHost::createScreenShareView(QObject *backend) {
	if (!m_window || !backend) return nullptr;
	QQmlEngine::setObjectOwnership(backend, QQmlEngine::CppOwnership);
	QVariant result;
	const bool invoked = QMetaObject::invokeMethod(m_window, "createScreenShareView", Q_RETURN_ARG(QVariant, result),
											  Q_ARG(QVariant, QVariant::fromValue(backend)));
	return invoked ? result.value< QObject * >() : nullptr;
}

void QmlShellHost::closeScreenShareView(QObject *view) {
	if (!view) return;
	QMetaObject::invokeMethod(view, "close");
	view->deleteLater();
}
