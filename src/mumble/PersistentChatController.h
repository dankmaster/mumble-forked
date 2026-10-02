// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_

#include "PersistentChatGateway.h"
#include "PersistentChatStore.h"

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QSet>

class PersistentChatController : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatController)

public:
	explicit PersistentChatController(QObject *parent = nullptr, int historyRequestTimeoutMs = 15000);

	void setGateway(PersistentChatGateway *gateway);
	void reset();

	void setActiveScope(const PersistentChatScopeKey &key, bool forceReload, bool requestHistory = true);
	void clearActiveScope();
	void warmupScopes(const QList< PersistentChatScopeKey > &keys);

	PersistentChatScopeKey activeScope() const;
	bool hasActiveScope() const;
	bool activeScopeMatches(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	PersistentChatScopeStateSnapshot activeSnapshot() const;
	int unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	int totalUnreadCount() const;

	bool requestOlderForActiveScope();
	bool markActiveScopeRead();
	bool applyEmbedState(const MumbleProto::ChatEmbedState &state);
	bool applyReactionState(const MumbleProto::ChatReactionState &state);

signals:
	void activeSnapshotChanged();
	void activeReadStateChanged(unsigned int lastReadMessageID, int unreadCount);
	void unreadStateChanged();

private slots:
	void handleHistoryResponse(const MumbleProto::ChatHistoryResponse &response, quint64 connectionGeneration);
	void handleMessage(const MumbleProto::ChatMessage &message);
	void handleReadState(const MumbleProto::ChatReadStateUpdate &update);

private:
	struct PendingHistoryRequest {
		quint64 connectionGeneration;
		quint64 requestID;
		PersistentChatScopeKey key;
	};

	void setUnreadFromMessages(PersistentChatStore::ScopeState &state);
	bool startInitialLoad(PersistentChatStore::ScopeState &state, bool forceReload);
	void handleConnectionChanged(quint64 connectionGeneration);
	bool canRequestHistory(PersistentChatStore::ScopeState &state);
	quint64 nextHistoryRequestID();
	void trackHistoryRequest(const PersistentChatScopeKey &key, quint64 requestID);
	void failHistoryRequest(const PersistentChatScopeKey &key, const PendingHistoryRequest &request,
							const QString &errorMessage, bool timedOut = false);

	QPointer< PersistentChatGateway > m_gateway;
	PersistentChatStore m_store;
	PersistentChatScopeKey m_activeScope;
	QHash< QString, PendingHistoryRequest > m_pendingHistoryRequests;
	// Legacy replies have no ID. Once a request is abandoned, only tagged replies can safely satisfy a retry.
	QSet< QString > m_ambiguousLegacyScopes;
	quint64 m_historyConnectionGeneration = 0;
	quint64 m_nextHistoryRequestID = 0;
	bool m_serverCorrelatesHistory = false;
	const int m_historyRequestTimeoutMs;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_
