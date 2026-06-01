// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_EXTERNALSCREENSHAREWINDOWHOST_H_
#define MUMBLE_MUMBLE_EXTERNALSCREENSHAREWINDOWHOST_H_

#include "ScreenShareManager.h"

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

class QCloseEvent;
class QEvent;
class QFrame;
class QLabel;
class QResizeEvent;
class QTimer;
class QToolButton;
class QVBoxLayout;

class ExternalScreenShareWindowHost : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ExternalScreenShareWindowHost)

public:
	explicit ExternalScreenShareWindowHost(const ScreenShareSession &session, QWidget *parent = nullptr);

	void closeFromManager();
	void setAudioMuted(bool muted);
	void setPaused(bool paused);
	void setProcessID(qint64 processID);
	void updateSession(const ScreenShareSession &session);

	QString streamID() const;

signals:
	void audioMuteToggled(const QString &streamID, bool muted);
	void closeRequested(const QString &streamID);
	void pauseToggled(const QString &streamID, bool paused);
	void stopRequested(const QString &streamID);

protected:
	void changeEvent(QEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void handleAudioButton();
	void handlePauseButton();
	void pollForExternalWindow();
	void retryApplyExternalAudioMute();

private:
	void applyExternalAudioMute(bool allowRetry);
	QString ownerLabel() const;
	QString windowTitle() const;
	void applyTheme();
	void clearEmbeddedWindow();
	void moveEmbeddedWindow();
	void setStatusText(const QString &status);
	void updateControls();
	void updatePlaceholder();

	ScreenShareSession m_session;
	QVBoxLayout *m_layout       = nullptr;
	QFrame *m_rootFrame         = nullptr;
	QFrame *m_headerFrame       = nullptr;
	QFrame *m_videoFrame        = nullptr;
	QWidget *m_videoSurface     = nullptr;
	QLabel *m_titleLabel        = nullptr;
	QLabel *m_detailLabel       = nullptr;
	QLabel *m_statusLabel       = nullptr;
	QLabel *m_placeholderLabel  = nullptr;
	QToolButton *m_pauseButton  = nullptr;
	QToolButton *m_audioButton  = nullptr;
	QToolButton *m_stopButton   = nullptr;
	QTimer *m_embedPollTimer    = nullptr;
	QTimer *m_audioMuteTimer    = nullptr;
	qint64 m_processID          = 0;
	quintptr m_externalWindowID = 0;
	int m_audioMuteAttempts     = 0;
	bool m_audioMuted           = false;
	bool m_applyingTheme        = false;
	bool m_closingFromManager   = false;
	bool m_paused               = false;
};

#endif // MUMBLE_MUMBLE_EXTERNALSCREENSHAREWINDOWHOST_H_
