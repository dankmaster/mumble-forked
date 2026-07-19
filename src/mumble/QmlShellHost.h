// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLSHELLHOST_H_
#define MUMBLE_MUMBLE_QMLSHELLHOST_H_

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
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
class DirectMessageController;
class MediaSessionBackend;
class ManualPluginController;
class ModernServerAdminController;
class NavigationRailModel;
class ParticipantModel;
enum class PttSafetyReason;
class PttSafetyController;
class QEvent;
class QmlSelectionState;
class QmlPerformanceMonitor;
class QmlImagePipeline;
class QmlMediaProfileFactory;
class QmlThemeController;
class QmlWindowStateController;
class QQmlApplicationEngine;
class QQuickWindow;
class RoomModel;
class ToastController;
class UiCommandController;

namespace Mumble {
class ModernRecorderController;
class ModernRecorderRuntimeAdapter;
}

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
	NavigationRailModel *navigationModel() const;
	ParticipantModel *participantModel() const;
	ChatTimelineModel *chatModel() const;
	ComposerController *composerController() const;
	ToastController *toastController() const;
	AsyncOperationModel *operationModel() const;
	ActionModel *actionModel() const;
	DialogStateController *dialogController() const;
	DirectMessageController *directMessageController() const;
	MediaSessionBackend *mediaSession() const;
	ModernServerAdminController *serverAdminController() const;
	Mumble::ModernRecorderController *recorderController() const;
	QmlSelectionState *selectionState() const;
	QmlPerformanceMonitor *performanceMonitor() const;
	std::shared_ptr< QmlImagePipeline > imagePipeline() const;
	QString registerServerIdentityImage(const QByteArray &imageBytes);
	QmlThemeController *themeController() const;
	bool visualFixtureOverrideActive() const { return m_visualFixtureOverrideActive; }
	void setVisualFixtureOverrideActive(bool active);
	void setVisualFixtureMutationActive(bool active);
	bool captureWindow(const QString &path, QString *error = nullptr,
					   const QString &windowId = QString()) const;
	bool captureWindowReady(const QString &windowId = QString()) const;
	QQuickWindow *captureWindowTarget(const QString &windowId = QString(), QString *error = nullptr) const;
	void showPttTool(bool visible);
	bool pttToolVisible() const;
#ifdef USE_MANUAL_PLUGIN
	ManualPluginController *manualPluginController() const;
	void showManualPluginTool(bool visible = true);
	bool manualPluginToolVisible() const;
#endif
	QObject *createScreenShareView(QObject *backend);
	void closeScreenShareView(QObject *view);

signals:
	void closeRequested();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void registerCaptureWindow(QQuickWindow *window);
	bool ensurePttToolWindow();
#ifdef USE_MANUAL_PLUGIN
	bool ensureManualPluginWindow();
#endif
	void releasePttForSafety(PttSafetyReason reason);
	void destroyScreenShareViews();

	ClientActionRegistry *m_actionRegistry = nullptr;
	std::unique_ptr< QQmlApplicationEngine > m_engine;
	QPointer< QQuickWindow > m_window;
	std::unique_ptr< ClientSessionController > m_sessionController;
	std::unique_ptr< ActiveScopeController > m_activeScopeController;
	std::unique_ptr< UiCommandController > m_commandController;
	std::unique_ptr< PttSafetyController > m_pttSafetyController;
	std::unique_ptr< RoomModel > m_roomModel;
	std::unique_ptr< NavigationRailModel > m_navigationModel;
	std::unique_ptr< ParticipantModel > m_participantModel;
	std::unique_ptr< ChatTimelineModel > m_chatModel;
	std::unique_ptr< ToastController > m_toastController;
	std::unique_ptr< AsyncOperationModel > m_operationModel;
	std::unique_ptr< ActionModel > m_actionModel;
	std::unique_ptr< DialogStateController > m_dialogController;
	std::unique_ptr< DirectMessageController > m_directMessageController;
	std::unique_ptr< MediaSessionBackend > m_mediaSession;
	std::unique_ptr< ModernServerAdminController > m_serverAdminController;
	std::unique_ptr< Mumble::ModernRecorderRuntimeAdapter > m_recorderRuntime;
	std::unique_ptr< Mumble::ModernRecorderController > m_recorderController;
	std::unique_ptr< QmlMediaProfileFactory > m_mediaProfileFactory;
	std::unique_ptr< QmlSelectionState > m_selectionState;
	std::unique_ptr< QmlPerformanceMonitor > m_performanceMonitor;
	std::shared_ptr< QmlImagePipeline > m_imagePipeline;
	QByteArray m_serverIdentityImageHash;
	QString m_serverIdentityImageUrl;
	std::unique_ptr< ComposerController > m_composerController;
	std::unique_ptr< QmlThemeController > m_themeController;
	std::unique_ptr< QmlWindowStateController > m_windowStateController;
	std::unique_ptr< QmlWindowStateController > m_pttWindowStateController;
	QPointer< QQuickWindow > m_pttToolWindow;
#ifdef USE_MANUAL_PLUGIN
	std::unique_ptr< ManualPluginController > m_manualPluginController;
	QPointer< QQuickWindow > m_manualPluginWindow;
#endif
	QSet< QObject * > m_screenShareViews;
	QSet< QObject * > m_closingScreenShareViews;
	QSet< QQuickWindow * > m_captureReadyWindows;
	bool m_visualFixtureOverrideActive = false;
};

#endif // MUMBLE_MUMBLE_QMLSHELLHOST_H_
