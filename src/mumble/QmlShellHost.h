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
class ComposerController;
class ClientActionRegistry;
class ClientSessionController;
class DialogStateController;
class MediaSessionBackend;
class ParticipantModel;
enum class PttSafetyReason;
class PttSafetyController;
class QEvent;
class QmlSelectionState;
class QmlPerformanceMonitor;
class QmlImagePipeline;
class QmlThemeController;
class QmlWindowStateController;
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
	ComposerController *composerController() const;
	AsyncOperationModel *operationModel() const;
	ActionModel *actionModel() const;
	DialogStateController *dialogController() const;
	MediaSessionBackend *mediaSession() const;
	QmlSelectionState *selectionState() const;
	QmlPerformanceMonitor *performanceMonitor() const;
	std::shared_ptr< QmlImagePipeline > imagePipeline() const;
	QmlThemeController *themeController() const;
	bool captureWindow(const QString &path, QString *error = nullptr) const;
	void showPttTool(bool visible);
	QObject *createScreenShareView(QObject *backend);
	void closeScreenShareView(QObject *view);

signals:
	void closeRequested();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void releasePttForSafety(PttSafetyReason reason);

	ClientActionRegistry *m_actionRegistry = nullptr;
	std::unique_ptr< QQmlApplicationEngine > m_engine;
	QPointer< QQuickWindow > m_window;
	std::unique_ptr< ClientSessionController > m_sessionController;
	std::unique_ptr< ActiveScopeController > m_activeScopeController;
	std::unique_ptr< UiCommandController > m_commandController;
	std::unique_ptr< PttSafetyController > m_pttSafetyController;
	std::unique_ptr< RoomModel > m_roomModel;
	std::unique_ptr< ParticipantModel > m_participantModel;
	std::unique_ptr< ChatTimelineModel > m_chatModel;
	std::unique_ptr< AsyncOperationModel > m_operationModel;
	std::unique_ptr< ActionModel > m_actionModel;
	std::unique_ptr< DialogStateController > m_dialogController;
	std::unique_ptr< MediaSessionBackend > m_mediaSession;
	std::unique_ptr< QmlSelectionState > m_selectionState;
	std::unique_ptr< QmlPerformanceMonitor > m_performanceMonitor;
	std::shared_ptr< QmlImagePipeline > m_imagePipeline;
	std::unique_ptr< ComposerController > m_composerController;
	std::unique_ptr< QmlThemeController > m_themeController;
	std::unique_ptr< QmlWindowStateController > m_windowStateController;
};

#endif // MUMBLE_MUMBLE_QMLSHELLHOST_H_
