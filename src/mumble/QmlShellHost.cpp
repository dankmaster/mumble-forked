// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlShellHost.h"

#include "ClientActionRegistry.h"
#include "QmlClientModels.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtGui/QImage>
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
	  m_roomModel(std::make_unique< RoomModel >(this)),
	  m_participantModel(std::make_unique< ParticipantModel >(this)),
	  m_chatModel(std::make_unique< ChatTimelineModel >(this)),
	  m_operationModel(std::make_unique< AsyncOperationModel >(this)),
	  m_actionModel(std::make_unique< ActionModel >(actionRegistry, this)),
	  m_dialogController(std::make_unique< DialogStateController >(this)),
	  m_selectionState(std::make_unique< QmlSelectionState >(this)) {
}

QmlShellHost::~QmlShellHost() {
	m_commandController->releasePtt();
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
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	m_engine = std::make_unique< QQmlApplicationEngine >();
	for (QObject *object : { static_cast< QObject * >(m_sessionController.get()),
						  static_cast< QObject * >(m_activeScopeController.get()),
						  static_cast< QObject * >(m_commandController.get()),
						  static_cast< QObject * >(m_roomModel.get()),
						  static_cast< QObject * >(m_participantModel.get()),
						  static_cast< QObject * >(m_chatModel.get()),
						  static_cast< QObject * >(m_operationModel.get()),
						  static_cast< QObject * >(m_actionModel.get()),
						  static_cast< QObject * >(m_dialogController.get()),
						  static_cast< QObject * >(m_selectionState.get()) }) {
		QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
	}
	QQmlContext *context = m_engine->rootContext();
	context->setContextProperty(QStringLiteral("clientSession"), m_sessionController.get());
	context->setContextProperty(QStringLiteral("activeScope"), m_activeScopeController.get());
	context->setContextProperty(QStringLiteral("uiCommands"), m_commandController.get());
	context->setContextProperty(QStringLiteral("roomModel"), m_roomModel.get());
	context->setContextProperty(QStringLiteral("participantModel"), m_participantModel.get());
	context->setContextProperty(QStringLiteral("chatModel"), m_chatModel.get());
	context->setContextProperty(QStringLiteral("operationModel"), m_operationModel.get());
	context->setContextProperty(QStringLiteral("actionModel"), m_actionModel.get());
	context->setContextProperty(QStringLiteral("dialogState"), m_dialogController.get());
	context->setContextProperty(QStringLiteral("selectionState"), m_selectionState.get());
	context->setContextProperty(QStringLiteral("clientActions"), m_actionRegistry);

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
	connect(m_window, &QQuickWindow::closing, this, [this](QQuickCloseEvent *) { emit closeRequested(); });
	connect(m_window, &QQuickWindow::sceneGraphError, this,
			[this](QQuickWindow::SceneGraphError, const QString &) { m_commandController->releasePtt(); });
	connect(m_window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
		if (visibility == QWindow::Hidden || visibility == QWindow::Minimized) m_commandController->releasePtt();
	});
	return true;
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
AsyncOperationModel *QmlShellHost::operationModel() const { return m_operationModel.get(); }
ActionModel *QmlShellHost::actionModel() const { return m_actionModel.get(); }
DialogStateController *QmlShellHost::dialogController() const { return m_dialogController.get(); }
QmlSelectionState *QmlShellHost::selectionState() const { return m_selectionState.get(); }

QVariantMap QmlShellHost::stateSnapshot() const {
	QVariantMap snapshot;
	QVariantMap app;
	app.insert(QStringLiteral("serverTitle"), m_sessionController->serverName());
	app.insert(QStringLiteral("selfName"), m_sessionController->selfName());
	app.insert(QStringLiteral("selfStatusLabel"), m_sessionController->connectionLabel());
	app.insert(QStringLiteral("selfMuted"), m_sessionController->selfMuted());
	app.insert(QStringLiteral("selfDeafened"), m_sessionController->selfDeafened());
	app.insert(QStringLiteral("connected"), m_sessionController->connected());
	app.insert(QStringLiteral("pttPressed"), m_commandController->pttPressed());
	snapshot.insert(QStringLiteral("app"), app);

	QVariantMap activeScope;
	activeScope.insert(QStringLiteral("scopeToken"), m_activeScopeController->scopeToken());
	activeScope.insert(QStringLiteral("label"), m_activeScopeController->label());
	activeScope.insert(QStringLiteral("description"), m_activeScopeController->description());
	activeScope.insert(QStringLiteral("kindLabel"), m_activeScopeController->kindLabel());
	activeScope.insert(QStringLiteral("composerPlaceholder"), m_activeScopeController->composerPlaceholder());
	activeScope.insert(QStringLiteral("composerHint"), m_activeScopeController->composerHint());
	activeScope.insert(QStringLiteral("canSend"), m_activeScopeController->canSend());
	snapshot.insert(QStringLiteral("activeScope"), activeScope);
	QVariantMap selection;
	selection.insert(QStringLiteral("scopeToken"), m_selectionState->scopeToken());
	selection.insert(QStringLiteral("selectedUserSession"), m_selectionState->selectedUserSession());
	selection.insert(QStringLiteral("selectedVoiceChannelId"), m_selectionState->selectedVoiceChannelId());
	snapshot.insert(QStringLiteral("selection"), selection);

	QVariantList voiceRooms;
	QVariantList textRooms;
	for (int row = 0; row < m_roomModel->rowCount(); ++row) {
		const QVariantMap modelRow = m_roomModel->get(row);
		QVariantMap state = modelRow.value(QStringLiteral("source")).toMap();
		if (state.isEmpty()) state = modelRow;
		if (!state.contains(QStringLiteral("token"))) {
			state.insert(QStringLiteral("token"), modelRow.value(QStringLiteral("id")));
		}
		if (modelRow.value(QStringLiteral("kind")).toString() == QLatin1String("voice")) {
			voiceRooms.push_back(state);
		} else {
			textRooms.push_back(state);
		}
	}
	snapshot.insert(QStringLiteral("voiceRooms"), voiceRooms);
	snapshot.insert(QStringLiteral("textRooms"), textRooms);

	QVariantList participants;
	for (int row = 0; row < m_participantModel->rowCount(); ++row) {
		const QVariantMap modelRow = m_participantModel->get(row);
		const QVariantMap state = modelRow.value(QStringLiteral("source")).toMap();
		participants.push_back(state.isEmpty() ? modelRow : state);
	}
	snapshot.insert(QStringLiteral("participants"), participants);

	QVariantList messages;
	for (int row = 0; row < m_chatModel->rowCount(); ++row) {
		const QVariantMap modelRow = m_chatModel->get(row);
		const QVariantMap state = modelRow.value(QStringLiteral("source")).toMap();
		messages.push_back(state.isEmpty() ? modelRow : state);
	}
	snapshot.insert(QStringLiteral("messages"), messages);

	QVariantList actions;
	for (int row = 0; row < m_actionModel->rowCount(); ++row) {
		actions.push_back(m_actionModel->get(row));
	}
	snapshot.insert(QStringLiteral("actions"), actions);
	snapshot.insert(QStringLiteral("dialog"), m_dialogController->state());
	return snapshot;
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
	if (!visible) m_commandController->releasePtt();
	m_window->setProperty("pttToolVisible", visible);
}
