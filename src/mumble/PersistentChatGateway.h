// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_

#include "Mumble.pb.h"

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QPointer>

#include <optional>

class ServerHandler;

class PersistentChatGateway : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatGateway)

public:
	explicit PersistentChatGateway(QObject *parent = nullptr);

	void setServerHandler(ServerHandler *serverHandler);
	ServerHandler *serverHandler() const;
	quint64 connectionGeneration() const;
	bool isReady() const;

	bool requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID, quint64 requestID = 0);
	bool requestWarmupPages(const QList< QPair< MumbleProto::ChatScope, unsigned int > > &scopes,
							unsigned int limitPerScope, quint64 requestID = 0);
	bool requestOlder(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int beforeMessageID,
					  quint64 requestID = 0);
	void send(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &body,
			  MumbleProto::ChatBodyFormat bodyFormat = MumbleProto::ChatBodyFormatPlainText,
			  std::optional< unsigned int > replyToMessageID = std::nullopt,
			  const QList< unsigned int > &attachmentAssetIDs = {},
			  const QStringList &attachmentFileNames = {});
	void toggleReaction(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
						unsigned int messageID, const QString &emoji, bool active);
	void deleteMessage(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
					   unsigned int messageID);
	void markRead(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int lastReadMessageID);

	void handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response);
	void handleIncomingMessage(const MumbleProto::ChatMessage &message);
	void handleIncomingReadState(const MumbleProto::ChatReadStateUpdate &update);

signals:
	void connectionChanged(quint64 connectionGeneration);
	void historyReceived(const MumbleProto::ChatHistoryResponse &response, quint64 connectionGeneration);
	void messageReceived(const MumbleProto::ChatMessage &message);
	void readStateReceived(const MumbleProto::ChatReadStateUpdate &update);

private:
	QPointer< ServerHandler > m_serverHandler;
	quint64 m_serverHandlerRunEpoch = 0;
	quint64 m_connectionGeneration = 0;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_
