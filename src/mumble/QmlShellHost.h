// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLSHELLHOST_H_
#define MUMBLE_MUMBLE_QMLSHELLHOST_H_

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariantMap>

#include <memory>

class AsyncOperationModel;
class ActionModel;
class ActiveScopeController;
class ChatTimelineModel;
class ClientActionRegistry;
class ClientSessionController;
class ParticipantModel;
class QmlSelectionState;
class QQmlApplicationEngine;
class QQuickWindow;
class RoomModel;
class UiCommandController;

class QmlShellHost final : public QObject {
	Q_OBJECT

public:
	explicit QmlShellHost(ClientActionRegistry *actionRegistry, QObject *parent = nullptr);
	~QmlShellHost() override;

	bool start(QString *error = nullptr);
	void showRaise();
	QQuickWindow *window() const;
	ClientSessionController *sessionController() const;
	ActiveScopeController *activeScopeController() const;
	UiCommandController *commandController() const;
	RoomModel *roomModel() const;
	ParticipantModel *participantModel() const;
	ChatTimelineModel *chatModel() const;
	AsyncOperationModel *operationModel() const;
	ActionModel *actionModel() const;
	QmlSelectionState *selectionState() const;
	QVariantMap stateSnapshot() const;
	bool captureWindow(const QString &path, QString *error = nullptr) const;

signals:
	void closeRequested();

private:
	ClientActionRegistry *m_actionRegistry = nullptr;
	std::unique_ptr< QQmlApplicationEngine > m_engine;
	QPointer< QQuickWindow > m_window;
	std::unique_ptr< ClientSessionController > m_sessionController;
	std::unique_ptr< ActiveScopeController > m_activeScopeController;
	std::unique_ptr< UiCommandController > m_commandController;
	std::unique_ptr< RoomModel > m_roomModel;
	std::unique_ptr< ParticipantModel > m_participantModel;
	std::unique_ptr< ChatTimelineModel > m_chatModel;
	std::unique_ptr< AsyncOperationModel > m_operationModel;
	std::unique_ptr< ActionModel > m_actionModel;
	std::unique_ptr< QmlSelectionState > m_selectionState;
};

#endif // MUMBLE_MUMBLE_QMLSHELLHOST_H_
