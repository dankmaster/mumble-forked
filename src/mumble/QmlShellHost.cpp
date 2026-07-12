// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlShellHost.h"

#include "ClientActionRegistry.h"
#include "QmlClientModels.h"

#include <QtCore/QUrl>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtQml/qqml.h>

QmlShellHost::QmlShellHost(ClientActionRegistry *actionRegistry, QObject *parent)
	: QObject(parent), m_actionRegistry(actionRegistry),
	  m_sessionController(std::make_unique< ClientSessionController >()),
	  m_commandController(std::make_unique< UiCommandController >()), m_roomModel(std::make_unique< RoomModel >()),
	  m_participantModel(std::make_unique< ParticipantModel >()),
	  m_chatModel(std::make_unique< ChatTimelineModel >()),
	  m_operationModel(std::make_unique< AsyncOperationModel >()),
	  m_actionModel(std::make_unique< ActionModel >(actionRegistry)),
	  m_selectionState(std::make_unique< QmlSelectionState >()) {
}

QmlShellHost::~QmlShellHost() = default;

bool QmlShellHost::start(QString *error) {
	if (m_window) {
		return true;
	}
	static const int themeType = qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml-shell/Theme.qml")),
														 "Mumble.Theme", 1, 0, "Theme");
	Q_UNUSED(themeType)
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	m_engine = std::make_unique< QQmlApplicationEngine >();
	QQmlContext *context = m_engine->rootContext();
	context->setContextProperty(QStringLiteral("clientSession"), m_sessionController.get());
	context->setContextProperty(QStringLiteral("uiCommands"), m_commandController.get());
	context->setContextProperty(QStringLiteral("roomModel"), m_roomModel.get());
	context->setContextProperty(QStringLiteral("participantModel"), m_participantModel.get());
	context->setContextProperty(QStringLiteral("chatModel"), m_chatModel.get());
	context->setContextProperty(QStringLiteral("operationModel"), m_operationModel.get());
	context->setContextProperty(QStringLiteral("actionModel"), m_actionModel.get());
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
UiCommandController *QmlShellHost::commandController() const { return m_commandController.get(); }
RoomModel *QmlShellHost::roomModel() const { return m_roomModel.get(); }
ParticipantModel *QmlShellHost::participantModel() const { return m_participantModel.get(); }
ChatTimelineModel *QmlShellHost::chatModel() const { return m_chatModel.get(); }
AsyncOperationModel *QmlShellHost::operationModel() const { return m_operationModel.get(); }
ActionModel *QmlShellHost::actionModel() const { return m_actionModel.get(); }
QmlSelectionState *QmlShellHost::selectionState() const { return m_selectionState.get(); }
