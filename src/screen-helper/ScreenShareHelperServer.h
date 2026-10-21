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
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>

#include <memory>
#include <functional>

class QLocalSocket;
class QProcess;
class ScreenShareProcessJob;
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
	using ReplyCallback = std::function< void(const QJsonObject &) >;
	void runSelfTest(ReplyCallback callback);

	struct ManagedSession {
		QJsonObject payload;
		QPointer< ScreenShareProcessJob > job;
		QPointer< QLocalSocket > requestSocket;
		qint64 ownerProcessID = 0;
		QString ownerProcessIdentity;
	};

private slots:
	void handleNewConnection();
	void handleSocketReadyRead();
	void handleSocketDisconnected();
	void handleIdleTimeout();

private:
	void dispatchRequest(const QJsonObject &request, ReplyCallback callback, QLocalSocket *socket = nullptr);
	QJsonObject handleQueryCapabilities() const;
	void startSession(const QJsonObject &payload, bool publish, ReplyCallback callback, QLocalSocket *socket);
	void stopSessionRequest(const QJsonObject &payload, bool publish, ReplyCallback callback);
	void logSessionPlanSummary(const QJsonObject &payload, const QString &label, const QString &phase) const;
	static void logPayloadWarnings(const QJsonObject &payload, const QString &label, const QString &streamID);
	void stopAllSessions();
	void stopSession(QHash< QString, ManagedSession > &sessions, const QString &streamID,
					 std::function< void() > stopped = {});
	void attachProcessLogging(const QString &streamID, bool publish, const QString &label);
	void refreshIdleTimer();
	bool acceptOwnerOperation(qint64 processID, const QString &identity, const QString &streamID,
		bool publish, bool stop, quint64 sequence, QString *error);
	struct OwnerOperations;

	QLocalServer *m_server;
	QTimer m_idleTimer;
	ScreenShareMediaSupport::CapabilitySummary m_capabilities;
	QHash< QLocalSocket *, QByteArray > m_socketBuffers;
	QHash< QString, ManagedSession > m_publishSessions;
	QHash< QString, ManagedSession > m_viewSessions;
	QHash< QString, std::shared_ptr< OwnerOperations > > m_ownerOperations;
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_
