// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatGateway.h"

#include "ServerHandler.h"

namespace {
// Keep the first visible tail small enough to become interactive in one frame-paced
// admission window. Older history remains available through the larger page below.
constexpr unsigned int INITIAL_HISTORY_PAGE_SIZE = 10;
constexpr unsigned int WARMUP_HISTORY_PAGE_SIZE  = 20;
constexpr unsigned int OLDER_HISTORY_PAGE_SIZE   = 20;
}

PersistentChatGateway::PersistentChatGateway(QObject *parent) : QObject(parent) {
}

void PersistentChatGateway::setServerHandler(ServerHandler *serverHandler) {
	const quint64 runEpoch = serverHandler ? serverHandler->connectionRunEpoch() : 0;
	if (m_serverHandler.data() == serverHandler && m_serverHandlerRunEpoch == runEpoch) {
		return;
	}
	m_serverHandler         = serverHandler;
	m_serverHandlerRunEpoch = runEpoch;
	if (++m_connectionGeneration == 0) {
		++m_connectionGeneration;
	}
	emit connectionChanged(m_connectionGeneration);
}

ServerHandler *PersistentChatGateway::serverHandler() const {
	return m_serverHandler.data();
}

quint64 PersistentChatGateway::connectionGeneration() const {
	return m_connectionGeneration;
}

bool PersistentChatGateway::isReady() const {
	return m_serverHandler && m_serverHandler->isRunning();
}

bool PersistentChatGateway::requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID, quint64 requestID) {
	if (!isReady()) {
		return false;
	}

	return m_serverHandler->requestChatHistory(scope, scopeID, 0, INITIAL_HISTORY_PAGE_SIZE, std::nullopt, requestID);
}

bool PersistentChatGateway::requestWarmupPages(
	const QList< QPair< MumbleProto::ChatScope, unsigned int > > &scopes, unsigned int limitPerScope, quint64 requestID) {
	if (!isReady() || scopes.isEmpty()) {
		return false;
	}

	return m_serverHandler->requestChatHistoryWarmup(scopes,
													 limitPerScope == 0 ? WARMUP_HISTORY_PAGE_SIZE : limitPerScope, requestID);
}

bool PersistentChatGateway::requestOlder(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int beforeMessageID,
										quint64 requestID) {
	if (!isReady() || beforeMessageID == 0) {
		return false;
	}

	return m_serverHandler->requestChatHistory(scope, scopeID, 0, OLDER_HISTORY_PAGE_SIZE, beforeMessageID, requestID);
}

void PersistentChatGateway::send(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &body,
								 MumbleProto::ChatBodyFormat bodyFormat,
								 std::optional< unsigned int > replyToMessageID,
								 const QList< unsigned int > &attachmentAssetIDs,
								 const QStringList &attachmentFileNames) {
	if (!isReady()) {
		return;
	}

	m_serverHandler->sendChatMessage(scope, scopeID, body, bodyFormat, replyToMessageID, attachmentAssetIDs,
								 attachmentFileNames);
}

void PersistentChatGateway::toggleReaction(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
										   unsigned int messageID, const QString &emoji, bool active) {
	if (!isReady() || messageID == 0 || emoji.trimmed().isEmpty()) {
		return;
	}

	m_serverHandler->sendChatReactionToggle(scope, scopeID, threadID, messageID, emoji, active);
}

void PersistentChatGateway::deleteMessage(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
										  unsigned int messageID) {
	if (!isReady() || messageID == 0) {
		return;
	}

	m_serverHandler->sendChatMessageDelete(scope, scopeID, threadID, messageID);
}

void PersistentChatGateway::markRead(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int lastReadMessageID) {
	if (!isReady() || lastReadMessageID == 0) {
		return;
	}

	m_serverHandler->updateChatReadState(scope, scopeID, lastReadMessageID);
}

void PersistentChatGateway::handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response) {
	// The caller dispatches only after MainWindow::customEvent has checked the event's
	// actual source handler and connection run epoch. Never attach a pending request ID
	// here: only the server can correlate a reply to the request that produced it.
	emit historyReceived(response, m_connectionGeneration);
}

void PersistentChatGateway::handleIncomingMessage(const MumbleProto::ChatMessage &message) {
	emit messageReceived(message);
}

void PersistentChatGateway::handleIncomingReadState(const MumbleProto::ChatReadStateUpdate &update) {
	emit readStateReceived(update);
}
