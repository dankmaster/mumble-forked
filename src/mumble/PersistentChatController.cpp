// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatController.h"

#include <QtCore/QSet>

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

PersistentChatController::PersistentChatController(QObject *parent) : QObject(parent) {
}

void PersistentChatController::setGateway(PersistentChatGateway *gateway) {
	if (m_gateway == gateway) {
		return;
	}

	if (m_gateway) {
		disconnect(m_gateway, nullptr, this, nullptr);
	}

	m_gateway = gateway;
	if (!m_gateway) {
		return;
	}

	connect(m_gateway, &PersistentChatGateway::historyReceived, this, &PersistentChatController::handleHistoryResponse);
	connect(m_gateway, &PersistentChatGateway::messageReceived, this, &PersistentChatController::handleMessage);
	connect(m_gateway, &PersistentChatGateway::readStateReceived, this, &PersistentChatController::handleReadState);
}

void PersistentChatController::reset() {
	m_connectionGeneration++;
	m_store.clear();
	m_activeScope = PersistentChatScopeKey();
	emit unreadStateChanged();
	emit activeSnapshotChanged();
}

void PersistentChatController::setActiveScope(const PersistentChatScopeKey &key, bool forceReload) {
	if (!key.valid) {
		clearActiveScope();
		return;
	}

	m_activeScope = key;
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	startInitialLoad(state, forceReload);
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
	for (const PersistentChatScopeKey &key : keys) {
		if (!key.valid || key.scope == MumbleProto::Aggregate || key.scope == MumbleProto::Private
			|| key.scope == MumbleProto::ServerGlobal) {
			continue;
		}

		const QString cacheKey = key.cacheKey();
		if (seen.contains(cacheKey)) {
			continue;
		}
		seen.insert(cacheKey);

		PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
		if (state.snapshot.initialLoaded || state.initialRequestInFlight || state.olderRequestInFlight) {
			continue;
		}

		requests.push_back(qMakePair(key.scope, key.scopeID));
		requestedKeys.push_back(key);
	}

	if (!requests.isEmpty() && m_gateway->requestWarmupPages(requests, WARMUP_HISTORY_PAGE_SIZE)) {
		for (const PersistentChatScopeKey &key : requestedKeys) {
			PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
			state.initialRequestInFlight           = true;
			state.snapshot.loadingState = state.snapshot.messages.isEmpty() ? PersistentChatLoadingState::Initial
																		   : PersistentChatLoadingState::Refreshing;
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
	if (!state.snapshot.hasOlder || state.snapshot.oldestLoadedMessageId == 0 || state.olderRequestInFlight) {
		return false;
	}

	state.olderRequestInFlight      = true;
	state.snapshot.loadingState     = PersistentChatLoadingState::Older;
	m_gateway->requestOlder(m_activeScope.scope, m_activeScope.scopeID, state.snapshot.oldestLoadedMessageId);
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

void PersistentChatController::handleHistoryResponse(const MumbleProto::ChatHistoryResponse &response) {
	const PersistentChatScopeKey key = scopeKeyFromHistory(response);
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	const bool loadingOlder           = state.olderRequestInFlight;
	state.initialRequestInFlight      = false;
	state.olderRequestInFlight        = false;
	state.snapshot.loadingState       = PersistentChatLoadingState::Idle;
	state.snapshot.initialLoaded      = true;
	state.snapshot.errorMessage.clear();
	state.snapshot.hasOlder =
		response.has_has_older() ? response.has_older() : (response.has_has_more() ? response.has_more() : false);
	if (response.has_last_read_message_id()) {
		state.snapshot.lastReadMessageId = response.last_read_message_id();
	}

	if (!loadingOlder && !state.snapshot.messages.isEmpty()) {
		for (int i = 0; i < response.messages_size(); ++i) {
			PersistentChatStore::mergeMessage(state.snapshot.messages, response.messages(i));
		}
	} else {
		if (!loadingOlder) {
			state.snapshot.messages.clear();
		}
		for (int i = 0; i < response.messages_size(); ++i) {
			PersistentChatStore::mergeMessage(state.snapshot.messages, response.messages(i));
		}
	}

	state.snapshot.oldestLoadedMessageId =
		response.has_oldest_message_id() ? response.oldest_message_id()
										 : PersistentChatStore::oldestMessageID(state.snapshot.messages);
	setUnreadFromMessages(state);

	if (m_activeScope.cacheKey() == key.cacheKey()) {
		emit activeSnapshotChanged();
	}
	emit unreadStateChanged();
}

void PersistentChatController::handleMessage(const MumbleProto::ChatMessage &message) {
	const PersistentChatScopeKey key = scopeKeyFromMessage(message);
	PersistentChatStore::ScopeState &state = m_store.ensureScope(key);
	const bool inserted = PersistentChatStore::mergeMessage(state.snapshot.messages, message);
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
	if (!m_gateway || !m_gateway->isReady() || state.olderRequestInFlight
		|| (!forceReload && state.initialRequestInFlight)) {
		return false;
	}

	if (!forceReload && state.snapshot.initialLoaded) {
		state.snapshot.loadingState = PersistentChatLoadingState::Idle;
		return false;
	}

	state.initialRequestInFlight      = true;
	state.snapshot.loadingState =
		state.snapshot.initialLoaded ? PersistentChatLoadingState::Refreshing : PersistentChatLoadingState::Initial;
	m_gateway->requestInitialPage(state.snapshot.key.scope, state.snapshot.key.scopeID);
	return true;
}
