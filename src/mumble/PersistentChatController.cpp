// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatController.h"

#include <QtCore/QSet>
#include <QtCore/QTimer>

namespace {
	constexpr unsigned int WARMUP_HISTORY_PAGE_SIZE = 20;

	PersistentChatScopeKey scopeKeyFromMessage(const MumbleProto::ChatMessage &message) {
		const MumbleProto::ChatScope scope = message.has_scope() ? message.scope() : MumbleProto::Channel;
		const unsigned int scopeID         = message.has_scope_id() ? message.scope_id() : 0;
		return PersistentChatScopeKey::fromScope(scope, scopeID);
	}

	PersistentChatScopeKey scopeKeyFromHistory(const MumbleProto::ChatHistoryResponse &response) {
		const MumbleProto::ChatScope scope = response.has_scope() ? response.scope() : MumbleProto::Channel;
		const unsigned int scopeID         = response.has_scope_id() ? response.scope_id() : 0;
		return PersistentChatScopeKey::fromScope(scope, scopeID);
	}

	PersistentChatScopeKey scopeKeyFromReadState(const MumbleProto::ChatReadStateUpdate &update) {
		const MumbleProto::ChatScope scope = update.has_scope() ? update.scope() : MumbleProto::Channel;
		const unsigned int scopeID         = update.has_scope_id() ? update.scope_id() : 0;
		return PersistentChatScopeKey::fromScope(scope, scopeID);
	}

	PersistentChatScopeKey scopeKeyFromEmbedState(const MumbleProto::ChatEmbedState &state) {
		const MumbleProto::ChatScope scope = state.has_scope() ? state.scope() : MumbleProto::Channel;
		const unsigned int scopeID         = state.has_scope_id() ? state.scope_id() : 0;
		return PersistentChatScopeKey::fromScope(scope, scopeID);
	}

	PersistentChatScopeKey scopeKeyFromReactionState(const MumbleProto::ChatReactionState &state) {
		const MumbleProto::ChatScope scope = state.has_scope() ? state.scope() : MumbleProto::Channel;
		const unsigned int scopeID         = state.has_scope_id() ? state.scope_id() : 0;
		return PersistentChatScopeKey::fromScope(scope, scopeID);
	}
}

PersistentChatController::PersistentChatController(QObject *parent, int historyRequestTimeoutMs)
	: QObject(parent), m_historyRequestTimeoutMs(qMax(1, historyRequestTimeoutMs)) {
}

void PersistentChatController::setGateway(PersistentChatGateway *gateway) {
	if (m_gateway == gateway) {
		return;
	}

	if (m_gateway) {
		disconnect(m_gateway, nullptr, this, nullptr);
	}

	// A replacement gateway owns a different transport even if its numeric generation happens to match.
	reset();
	m_ambiguousLegacyScopes.clear();
	m_serverCorrelatesHistory = false;
	m_gateway = gateway;
	m_historyConnectionGeneration = gateway ? gateway->connectionGeneration() : 0;
	if (!m_gateway) {
		return;
	}

	connect(m_gateway, &PersistentChatGateway::historyReceived, this, &PersistentChatController::handleHistoryResponse);
	connect(m_gateway, &PersistentChatGateway::connectionChanged, this, &PersistentChatController::handleConnectionChanged);
	connect(m_gateway, &PersistentChatGateway::messageReceived, this, &PersistentChatController::handleMessage);
	connect(m_gateway, &PersistentChatGateway::readStateReceived, this, &PersistentChatController::handleReadState);
}

void PersistentChatController::reset() {
	// Reset may happen without replacing the transport. Do not let an abandoned legacy reply satisfy a later request.
	for (auto it = m_pendingHistoryRequests.cbegin(); it != m_pendingHistoryRequests.cend(); ++it) {
		m_ambiguousLegacyScopes.insert(it.key());
	}
	m_pendingHistoryRequests.clear();
	m_store.clear();
	m_activeScope = PersistentChatScopeKey();
	emit unreadStateChanged();
	emit activeSnapshotChanged();
}

void PersistentChatController::setActiveScope(const PersistentChatScopeKey &key, bool forceReload,
											  bool requestHistory) {
	if (!key.valid) {
		clearActiveScope();
		return;
	}

	m_activeScope = key;
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	if (requestHistory) {
		startInitialLoad(state, forceReload);
	} else if (!state.initialRequestInFlight && !state.olderRequestInFlight) {
		// A room can allow live chat while denying history. Keep the scope active so incoming messages are
		// published, but do not issue a history request the server would reject.
		state.snapshot.loadingState = PersistentChatLoadingState::Idle;
	}
	emit activeSnapshotChanged();
}

void PersistentChatController::clearActiveScope() {
	if (!m_activeScope.valid) {
		return;
	}

	m_activeScope = PersistentChatScopeKey();
	emit activeSnapshotChanged();
}

void PersistentChatController::warmupScopes(const QList< PersistentChatScopeKey > &keys) {
	if (keys.isEmpty() || !m_gateway || !m_gateway->isReady()) {
		return;
	}

	QList< QPair< MumbleProto::ChatScope, unsigned int > > requests;
	QList< PersistentChatScopeKey > requestedKeys;
	QSet< QString > seen;
	const QString activeCacheKey = m_activeScope.valid ? m_activeScope.cacheKey() : QString();
	for (const PersistentChatScopeKey &key : keys) {
		if (!key.valid || key.scope == MumbleProto::Aggregate || key.scope == MumbleProto::Private
			|| key.scope == MumbleProto::ServerGlobal) {
			continue;
		}

		const QString cacheKey = key.cacheKey();
		if (!activeCacheKey.isEmpty() && cacheKey == activeCacheKey) {
			continue;
		}
		if (seen.contains(cacheKey)) {
			continue;
		}
		seen.insert(cacheKey);

		PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
		if (state.snapshot.initialLoaded || !canRequestHistory(state)) {
			continue;
		}

		requests.push_back(qMakePair(key.scope, key.scopeID));
		requestedKeys.push_back(key);
		if (requestedKeys.size() == 20) {
			// Match the server's bounded warmup batch size.
			break;
		}
	}

	if (requests.isEmpty()) {
		return;
	}
	const quint64 requestID = nextHistoryRequestID();
	for (const PersistentChatScopeKey &key : requestedKeys) {
		PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
		state.initialRequestInFlight = true;
		state.snapshot.loadingState = state.snapshot.messages.isEmpty() ? PersistentChatLoadingState::Initial
																	   : PersistentChatLoadingState::Refreshing;
		trackHistoryRequest(key, requestID);
	}
	if (!m_gateway->requestWarmupPages(requests, WARMUP_HISTORY_PAGE_SIZE, requestID)) {
		for (const PersistentChatScopeKey &key : requestedKeys) {
			failHistoryRequest(key, { m_historyConnectionGeneration, requestID }, tr("Chat history is unavailable."));
		}
	}
}

PersistentChatScopeKey PersistentChatController::activeScope() const {
	return m_activeScope;
}

bool PersistentChatController::hasActiveScope() const {
	return m_activeScope.valid;
}

bool PersistentChatController::activeScopeMatches(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	return m_activeScope.matches(scope, scopeID);
}

PersistentChatScopeStateSnapshot PersistentChatController::activeSnapshot() const {
	if (!m_activeScope.valid) {
		return {};
	}

	const PersistentChatStore::ScopeState *state = m_store.scopeState(m_activeScope);
	return state ? state->snapshot : PersistentChatScopeStateSnapshot();
}

int PersistentChatController::unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	return m_store.unreadCount(scope, scopeID);
}

int PersistentChatController::totalUnreadCount() const {
	return m_store.totalUnreadCount();
}

bool PersistentChatController::requestOlderForActiveScope() {
	if (!m_activeScope.valid || !m_gateway || !m_gateway->isReady()) {
		return false;
	}

	PersistentChatStore::ScopeState &state = m_store.ensureScope(m_activeScope);
	if (!state.snapshot.hasOlder || state.snapshot.oldestLoadedMessageId == 0
		|| !canRequestHistory(state)) {
		emit activeSnapshotChanged();
		return false;
	}

	state.olderRequestInFlight      = true;
	state.snapshot.loadingState     = PersistentChatLoadingState::Older;
	const quint64 requestID = nextHistoryRequestID();
	trackHistoryRequest(m_activeScope, requestID);
	if (!m_gateway->requestOlder(m_activeScope.scope, m_activeScope.scopeID, state.snapshot.oldestLoadedMessageId,
								 requestID)) {
		failHistoryRequest(m_activeScope, { m_historyConnectionGeneration, requestID }, tr("Chat history is unavailable."));
		return false;
	}
	emit activeSnapshotChanged();
	return true;
}

bool PersistentChatController::markActiveScopeRead() {
	if (!m_activeScope.valid || !m_gateway || !m_gateway->isReady()) {
		return false;
	}

	PersistentChatStore::ScopeState &state = m_store.ensureScope(m_activeScope);
	if (state.snapshot.messages.isEmpty()) {
		return false;
	}

	const unsigned int lastVisibleMessageID = state.snapshot.messages.back().message_id();
	if (lastVisibleMessageID <= state.snapshot.lastReadMessageId) {
		return false;
	}

	state.snapshot.lastReadMessageId = lastVisibleMessageID;
	state.snapshot.unreadCount       = 0;
	m_gateway->markRead(m_activeScope.scope, m_activeScope.scopeID, lastVisibleMessageID);
	emit unreadStateChanged();
	emit activeReadStateChanged(state.snapshot.lastReadMessageId, state.snapshot.unreadCount);
	return true;
}

bool PersistentChatController::applyEmbedState(const MumbleProto::ChatEmbedState &state) {
	if (!state.has_thread_id() || !state.has_message_id()) {
		return false;
	}

	const PersistentChatScopeKey key = scopeKeyFromEmbedState(state);
	PersistentChatStore::ScopeState *scopeState = m_store.scopeState(key);
	if (!scopeState) {
		return false;
	}

	for (int i = 0; i < scopeState->snapshot.messages.size(); ++i) {
		MumbleProto::ChatMessage &message = scopeState->snapshot.messages[i];
		if (message.thread_id() != state.thread_id() || message.message_id() != state.message_id()) {
			continue;
		}

		message.clear_embeds();
		for (const MumbleProto::ChatEmbedRef &embed : state.embeds()) {
			*message.add_embeds() = embed;
		}
		return true;
	}

	return false;
}

bool PersistentChatController::applyReactionState(const MumbleProto::ChatReactionState &state) {
	if (!state.has_thread_id() || !state.has_message_id()) {
		return false;
	}

	const PersistentChatScopeKey key = scopeKeyFromReactionState(state);
	PersistentChatStore::ScopeState *scopeState = m_store.scopeState(key);
	if (!scopeState) {
		return false;
	}

	for (int i = 0; i < scopeState->snapshot.messages.size(); ++i) {
		MumbleProto::ChatMessage &message = scopeState->snapshot.messages[i];
		if (message.thread_id() != state.thread_id() || message.message_id() != state.message_id()) {
			continue;
		}

		message.clear_reactions();
		for (const MumbleProto::ChatReactionAggregate &reaction : state.reactions()) {
			*message.add_reactions() = reaction;
		}
		return true;
	}

	return false;
}

void PersistentChatController::handleHistoryResponse(const MumbleProto::ChatHistoryResponse &response,
													  quint64 connectionGeneration) {
	const PersistentChatScopeKey key = scopeKeyFromHistory(response);
	PersistentChatStore::ScopeState *state = m_store.scopeState(key);
	if (!state) {
		// A response from a retired connection must not recreate a scope after
		// reset. Only an explicit request, warmup, live message, or selection may
		// establish cache ownership.
		return;
	}
	const bool responseOwnsOlderRequest   = state->olderRequestInFlight;
	const bool responseOwnsInitialRequest = state->initialRequestInFlight;
	const auto requestIt = m_pendingHistoryRequests.constFind(key.cacheKey());
	if ((!responseOwnsOlderRequest && !responseOwnsInitialRequest)
		|| requestIt == m_pendingHistoryRequests.cend()
		|| requestIt->connectionGeneration != connectionGeneration
		|| !m_gateway || m_gateway->connectionGeneration() != connectionGeneration) {
		return;
	}
	if (response.has_request_id()) {
		if (response.request_id() != requestIt->requestID) {
			return;
		}
		m_serverCorrelatesHistory = true;
	} else if (m_serverCorrelatesHistory || m_ambiguousLegacyScopes.contains(key.cacheKey())) {
		return;
	}
	if (response.error() != MumbleProto::ChatHistoryResponse_Error_NoError) {
		const QString message = response.has_error_message() ? QString::fromStdString(response.error_message()).trimmed()
															: QString();
		failHistoryRequest(key, requestIt.value(), message.isEmpty() ? tr("Chat history could not be loaded.") : message);
		return;
	}
	m_pendingHistoryRequests.erase(requestIt);
	if (responseOwnsOlderRequest) {
		state->olderRequestInFlight = false;
	} else if (responseOwnsInitialRequest) {
		state->initialRequestInFlight = false;
	}
	PersistentChatStore::ScopeState &ownedState = *state;
	ownedState.snapshot.loadingState       = PersistentChatLoadingState::Idle;
	ownedState.snapshot.initialLoaded      = true;
	ownedState.snapshot.errorMessage.clear();
	ownedState.snapshot.hasOlder =
		response.has_has_older() ? response.has_older() : (response.has_has_more() ? response.has_more() : false);
	if (response.has_last_read_message_id()) {
		ownedState.snapshot.lastReadMessageId = response.last_read_message_id();
	}

	QVector< MumbleProto::ChatMessage > incomingMessages;
	incomingMessages.reserve(response.messages_size());
	for (const MumbleProto::ChatMessage &message : response.messages()) {
		incomingMessages.push_back(message);
	}

	const PersistentChatStore::MergeMessagesResult mergeResult =
		PersistentChatStore::mergeMessages(ownedState.snapshot.messages, incomingMessages);
	ownedState.snapshot.prependedMessageCount = mergeResult.prependedMessageCount;
	if (mergeResult.changed) {
		++ownedState.snapshot.messagesRevision;
	}

	ownedState.snapshot.oldestLoadedMessageId =
		response.has_oldest_message_id() ? response.oldest_message_id()
										 : PersistentChatStore::oldestMessageID(ownedState.snapshot.messages);
	setUnreadFromMessages(ownedState);

	if (m_activeScope.cacheKey() == key.cacheKey()) {
		emit activeSnapshotChanged();
	}
	emit unreadStateChanged();
}

void PersistentChatController::handleMessage(const MumbleProto::ChatMessage &message) {
	const PersistentChatScopeKey key = scopeKeyFromMessage(message);
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	const bool inserted = PersistentChatStore::mergeMessage(state.snapshot.messages, message);
	state.snapshot.prependedMessageCount = 0;
	++state.snapshot.messagesRevision;
	state.snapshot.oldestLoadedMessageId = PersistentChatStore::oldestMessageID(state.snapshot.messages);
	setUnreadFromMessages(state);

	if (!m_activeScope.matches(key.scope, key.scopeID) && inserted
		&& message.message_id() > state.snapshot.lastReadMessageId) {
		state.snapshot.unreadCount =
			static_cast< int >(PersistentChatStore::unreadMessagesAfter(state.snapshot.messages, state.snapshot.lastReadMessageId));
	}

	if (m_activeScope.cacheKey() == key.cacheKey()) {
		emit activeSnapshotChanged();
	}
	emit unreadStateChanged();
}

void PersistentChatController::handleReadState(const MumbleProto::ChatReadStateUpdate &update) {
	const PersistentChatScopeKey key = scopeKeyFromReadState(update);
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	state.snapshot.lastReadMessageId = update.has_last_read_message_id() ? update.last_read_message_id() : 0;
	setUnreadFromMessages(state);

	if (m_activeScope.cacheKey() == key.cacheKey()) {
		emit activeReadStateChanged(state.snapshot.lastReadMessageId, state.snapshot.unreadCount);
	}
	emit unreadStateChanged();
}

void PersistentChatController::setUnreadFromMessages(PersistentChatStore::ScopeState &state) {
	state.snapshot.unreadCount = static_cast< int >(
		PersistentChatStore::unreadMessagesAfter(state.snapshot.messages, state.snapshot.lastReadMessageId));
}

bool PersistentChatController::startInitialLoad(PersistentChatStore::ScopeState &state, bool forceReload) {
	if (!forceReload && state.snapshot.initialLoaded) {
		if (!state.initialRequestInFlight && !state.olderRequestInFlight) {
			state.snapshot.loadingState = PersistentChatLoadingState::Idle;
		}
		return false;
	}
	if (!canRequestHistory(state)) {
		return false;
	}

	state.initialRequestInFlight      = true;
	state.snapshot.loadingState =
		state.snapshot.initialLoaded ? PersistentChatLoadingState::Refreshing : PersistentChatLoadingState::Initial;
	const quint64 requestID = nextHistoryRequestID();
	trackHistoryRequest(state.snapshot.key, requestID);
	if (!m_gateway->requestInitialPage(state.snapshot.key.scope, state.snapshot.key.scopeID, requestID)) {
		failHistoryRequest(state.snapshot.key, { m_historyConnectionGeneration, requestID },
						   tr("Chat history is unavailable."));
		return false;
	}
	return true;
}

void PersistentChatController::handleConnectionChanged(quint64 connectionGeneration) {
	if (m_historyConnectionGeneration == connectionGeneration) {
		return;
	}
	const auto pending = m_pendingHistoryRequests;
	for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
		failHistoryRequest(it->key, it.value(), tr("The connection changed. Try loading history again."));
	}
	m_historyConnectionGeneration = connectionGeneration;
	m_ambiguousLegacyScopes.clear();
	m_serverCorrelatesHistory = false;
}

bool PersistentChatController::canRequestHistory(PersistentChatStore::ScopeState &state) {
	if (!m_gateway || !m_gateway->isReady()) {
		return false;
	}
	handleConnectionChanged(m_gateway->connectionGeneration());
	if (state.initialRequestInFlight || state.olderRequestInFlight) {
		return false;
	}
	if (!m_serverCorrelatesHistory && m_ambiguousLegacyScopes.contains(state.snapshot.key.cacheKey())) {
		state.snapshot.errorMessage = tr("Reconnect to load chat history again. This server cannot identify late replies.");
		state.snapshot.loadingState = PersistentChatLoadingState::Idle;
		return false;
	}
	return true;
}

quint64 PersistentChatController::nextHistoryRequestID() {
	if (++m_nextHistoryRequestID == 0) {
		++m_nextHistoryRequestID;
	}
	return m_nextHistoryRequestID;
}

void PersistentChatController::trackHistoryRequest(const PersistentChatScopeKey &key, quint64 requestID) {
	const PendingHistoryRequest request = { m_gateway->connectionGeneration(), requestID, key };
	m_pendingHistoryRequests.insert(key.cacheKey(), request);
	m_store.ensureScope(key).snapshot.errorMessage.clear();
	QTimer::singleShot(m_historyRequestTimeoutMs, this, [this, key, request]() {
		failHistoryRequest(key, request, m_serverCorrelatesHistory
			? tr("Chat history timed out. Try again.")
			: tr("Chat history timed out. Reconnect to try again."), true);
	});
}

void PersistentChatController::failHistoryRequest(const PersistentChatScopeKey &key,
												 const PendingHistoryRequest &request, const QString &errorMessage,
												 bool timedOut) {
	const auto it = m_pendingHistoryRequests.constFind(key.cacheKey());
	if (it == m_pendingHistoryRequests.cend() || it->requestID != request.requestID
		|| it->connectionGeneration != request.connectionGeneration) {
		return;
	}
	if (timedOut) {
		m_ambiguousLegacyScopes.insert(key.cacheKey());
	}
	m_pendingHistoryRequests.erase(it);
	PersistentChatStore::ScopeState *state = m_store.scopeState(key);
	if (!state) {
		return;
	}
	state->initialRequestInFlight = false;
	state->olderRequestInFlight = false;
	state->snapshot.loadingState = PersistentChatLoadingState::Idle;
	state->snapshot.errorMessage = errorMessage;
	if (m_activeScope.cacheKey() == key.cacheKey()) {
		emit activeSnapshotChanged();
	}
}
