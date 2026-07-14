// Copyright The Mumble Developers. All rights reserved.

#ifndef MUMBLE_MUMBLE_SCREENSHAREVIEWBACKEND_H_
#define MUMBLE_MUMBLE_SCREENSHAREVIEWBACKEND_H_

#include "ScreenShareManager.h"
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtGui/QWindow>
#include <QtGui/QImage>

class QTimer;
class QThread;
class ScreenShareNativeFrameReader;

class ScreenShareViewBackend final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString streamId READ streamId CONSTANT)
	Q_PROPERTY(QString title READ title NOTIFY sessionChanged)
	Q_PROPERTY(QString detail READ detail NOTIFY sessionChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
	Q_PROPERTY(bool audioMuted READ audioMuted WRITE setAudioMuted NOTIFY audioMutedChanged)
	Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY sessionChanged)
	Q_PROPERTY(int audioVolume READ audioVolume WRITE setAudioVolume NOTIFY audioVolumeChanged)
	Q_PROPERTY(qint64 processId READ processId WRITE setProcessId NOTIFY processIdChanged)
	Q_PROPERTY(QWindow *videoWindow READ videoWindow NOTIFY videoWindowChanged)
	Q_PROPERTY(QString renderTransport READ renderTransport NOTIFY nativeFrameActiveChanged)
	Q_PROPERTY(bool nativeFrameTransportAvailable READ nativeFrameTransportAvailable CONSTANT)
	Q_PROPERTY(QString nativeFrameTransportBlocker READ nativeFrameTransportBlocker CONSTANT)
	Q_PROPERTY(bool nativeFrameActive READ nativeFrameActive NOTIFY nativeFrameActiveChanged)
	Q_PROPERTY(QImage currentFrame READ currentFrame NOTIFY frameChanged)
	Q_PROPERTY(bool hasCurrentFrame READ hasCurrentFrame NOTIFY frameChanged)
	Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY operationStateChanged)
	Q_PROPERTY(QString operationError READ operationError NOTIFY operationStateChanged)
	Q_PROPERTY(bool operationCancellable READ operationCancellable NOTIFY operationStateChanged)

public:
	explicit ScreenShareViewBackend(const ScreenShareSession &session, QObject *parent = nullptr);
	~ScreenShareViewBackend() override;

	QString streamId() const;
	QString title() const;
	QString detail() const;
	QString status() const;
	bool paused() const;
	bool audioMuted() const;
	bool audioAvailable() const;
	int audioVolume() const;
	qint64 processId() const;
	QWindow *videoWindow() const;
	QString renderTransport() const;
	bool nativeFrameTransportAvailable() const;
	QString nativeFrameTransportBlocker() const;
	bool nativeFrameActive() const;
	QImage currentFrame() const;
	bool hasCurrentFrame() const;
	QString operationStatus() const;
	QString operationError() const;
	bool operationCancellable() const;

	void updateSession(const ScreenShareSession &session);
	void setProcessId(qint64 processId);
	void setNativeFrameTransport(const QString &sharedMemoryKey, quint64 generation);
	void setOperationState(const QString &status, const QString &error, bool cancellable);
	Q_INVOKABLE void setPaused(bool paused);
	Q_INVOKABLE void setAudioMuted(bool muted);
	Q_INVOKABLE void setAudioVolume(int percent);
	Q_INVOKABLE void requestRetry();
	Q_INVOKABLE void requestStop();
	Q_INVOKABLE void requestClose();

signals:
	void sessionChanged();
	void statusChanged();
	void pausedChanged();
	void audioMutedChanged();
	void audioVolumeChanged();
	void processIdChanged();
	void videoWindowChanged();
	void nativeFrameActiveChanged();
	void frameChanged();
	void operationStateChanged();
	void pauseToggled(const QString &streamId, bool paused);
	void audioMuteToggled(const QString &streamId, bool muted);
	void retryRequested(const QString &streamId);
	void stopRequested(const QString &streamId);
	void closeRequested(const QString &streamId);

private slots:
	void pollForVideoWindow();
	void retryAudioControls();

private:
	void clearVideoWindow();
	bool applyAudioControls();
	void setStatus(const QString &status);

	ScreenShareSession m_session;
	QTimer *m_windowPollTimer = nullptr;
	QTimer *m_audioRetryTimer = nullptr;
	QPointer< QWindow > m_videoWindow;
	QString m_status;
	qint64 m_processId = 0;
	bool m_paused = false;
	bool m_audioMuted = false;
	int m_audioVolume = 100;
	int m_audioRetryAttempts = 0;
	QImage m_currentFrame;
	QThread *m_frameThread = nullptr;
	ScreenShareNativeFrameReader *m_frameReader = nullptr;
	bool m_nativeFrameActive = false;
	QString m_operationStatus = QStringLiteral("idle");
	QString m_operationError;
	bool m_operationCancellable = false;
};

#endif
