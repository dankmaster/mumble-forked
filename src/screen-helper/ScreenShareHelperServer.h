// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_

#include "ScreenShareMediaSupport.h"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>

#include <memory>

class QLocalSocket;
class QProcess;
namespace Mumble::ScreenShare { class FrameTransport; }

class ScreenShareHelperServer : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareHelperServer)

public:
	explicit ScreenShareHelperServer(QObject *parent = nullptr);
	~ScreenShareHelperServer() override;

	bool start(QString *errorMessage = nullptr);
	bool start(const QString &socketBaseName, QString *errorMessage);
	QJsonObject capabilityPayload() const;
	QJsonObject runSelfTest();

	struct ManagedSession {
		QJsonObject payload;
		QProcess *process = nullptr;
		QTimer *nativeFrameTimer = nullptr;
		std::shared_ptr< Mumble::ScreenShare::FrameTransport > nativeFrameTransport;
		// The XDG ScreenCast session handle backing this publish, when the capture came from a
		// pre-negotiated portal source. Closed via the portal when the session stops.
		QString portalSessionHandle;
	};

private slots:
	void handleNewConnection();
	void handleSocketReadyRead();
	void handleSocketDisconnected();
	void handleIdleTimeout();

private:
	QJsonObject dispatchRequest(const QJsonObject &request);
	QJsonObject handleQueryCapabilities() const;
	QJsonObject handlePickSource();
	QJsonObject handleStartPublish(const QJsonObject &payload);
	QJsonObject handleStopPublish(const QJsonObject &payload);
	QJsonObject handleStartView(const QJsonObject &payload);
	QJsonObject handleStopView(const QJsonObject &payload);
	void logSessionPlanSummary(const QJsonObject &payload, const QString &label, const QString &phase) const;
	static void logPayloadWarnings(const QJsonObject &payload, const QString &label, const QString &streamID);
	void stopAllSessions();
	void stopSession(QHash< QString, ManagedSession > &sessions, const QString &streamID);
	void closePendingPortalSource();
	void attachProcessLogging(const QString &streamID, bool publish, const QString &label);
	void refreshIdleTimer();

	QLocalServer *m_server;
	QTimer m_idleTimer;
	ScreenShareMediaSupport::CapabilitySummary m_capabilities;
	QHash< QLocalSocket *, QByteArray > m_socketBuffers;
	QHash< QString, ManagedSession > m_publishSessions;
	QHash< QString, ManagedSession > m_viewSessions;
	bool m_hasPendingPortalSource = false;
	quint32 m_pendingPortalNodeId  = 0;
	quint32 m_pendingPortalWidth   = 0;
	quint32 m_pendingPortalHeight  = 0;
	QString m_pendingPortalSourceType;
	// ScreenCast session handle for the currently pending (not yet consumed) portal source. Empty
	// when there is no pending portal source.
	QString m_pendingPortalSessionHandle;
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_
