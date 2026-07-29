// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatStore.h"

#include <QSet>

#include <algorithm>

namespace {
	bool chatMessageLessThan(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const quint64 lhsCreatedAt = lhs.has_created_at() ? lhs.created_at() : 0;
		const quint64 rhsCreatedAt = rhs.has_created_at() ? rhs.created_at() : 0;
		if (lhsCreatedAt != rhsCreatedAt) {
			return lhsCreatedAt < rhsCreatedAt;
		}
		if (lhs.thread_id() != rhs.thread_id()) {
			return lhs.thread_id() < rhs.thread_id();
		}

		return lhs.message_id() < rhs.message_id();
	}

	bool chatMessageDeleted(const MumbleProto::ChatMessage &message) {
		return message.has_deleted_at() && message.deleted_at() > 0;
	}
}

void PersistentChatStore::clear() {
	m_scopeStates.clear();
}

bool PersistentChatStore::hasScope(const PersistentChatScopeKey &key) const {
	return key.valid && m_scopeStates.contains(key.cacheKey());
}

PersistentChatStore::ScopeState *PersistentChatStore::scopeState(const PersistentChatScopeKey &key) {
	if (!key.valid) {
		return nullptr;
	}

	auto it = m_scopeStates.find(key.cacheKey());
	return it == m_scopeStates.end() ? nullptr : &it.value();
}

const PersistentChatStore::ScopeState *PersistentChatStore::scopeState(const PersistentChatScopeKey &key) const {
	if (!key.valid) {
		return nullptr;
	}

	const auto it = m_scopeStates.constFind(key.cacheKey());
	return it == m_scopeStates.cend() ? nullptr : &it.value();
}

PersistentChatStore::ScopeState &PersistentChatStore::ensureScope(const PersistentChatScopeKey &key) {
	ScopeState &state = m_scopeStates[key.cacheKey()];
	state.snapshot.key = key;
	return state;
}

QList< QString > PersistentChatStore::scopeKeys() const {
	return m_scopeStates.keys();
}

int PersistentChatStore::unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	const PersistentChatScopeKey key = PersistentChatScopeKey::fromScope(scope, scopeID);
	const ScopeState *state          = scopeState(key);
	return state ? state->snapshot.unreadCount : 0;
}

int PersistentChatStore::totalUnreadCount() const {
	int total = 0;
	for (auto it = m_scopeStates.cbegin(); it != m_scopeStates.cend(); ++it) {
		if (!it.value().snapshot.key.valid || it.value().snapshot.key.scope == MumbleProto::Aggregate) {
			continue;
		}

		total += it.value().snapshot.unreadCount;
	}
	return total;
}

std::size_t PersistentChatStore::unreadMessagesAfter(const QVector< MumbleProto::ChatMessage > &messages,
													 unsigned int lastReadMessageID) {
	std::size_t unreadCount = 0;
	for (const MumbleProto::ChatMessage &message : messages) {
		if (message.message_id() > lastReadMessageID) {
			++unreadCount;
		}
	}

	return unreadCount;
}

void PersistentChatStore::sortMessages(QVector< MumbleProto::ChatMessage > &messages) {
	std::sort(messages.begin(), messages.end(), chatMessageLessThan);
}

bool PersistentChatStore::mergeMessage(QVector< MumbleProto::ChatMessage > &messages,
									   const MumbleProto::ChatMessage &message) {
	for (int i = 0; i < messages.size(); ++i) {
		const MumbleProto::ChatMessage &current = messages.at(i);
		if (current.thread_id() == message.thread_id() && current.message_id() == message.message_id()) {
			if (chatMessageDeleted(message)) {
				messages.removeAt(i);
				return false;
			}

			messages[i] = message;
			return false;
		}
	}

	if (chatMessageDeleted(message)) {
		return false;
	}

	messages.push_back(message);
	sortMessages(messages);
	return true;
}

PersistentChatStore::MergeMessagesResult PersistentChatStore::mergeMessages(
	QVector< MumbleProto::ChatMessage > &messages, const QVector< MumbleProto::ChatMessage > &incomingMessages) {
	MergeMessagesResult result;
	if (incomingMessages.isEmpty()) {
		return result;
	}

	const auto identity = [](const MumbleProto::ChatMessage &message) {
		return (static_cast< quint64 >(message.thread_id()) << 32U)
			| static_cast< quint64 >(message.message_id());
	};

	QHash< quint64, int > rowByIdentity;
	rowByIdentity.reserve(messages.size() + incomingMessages.size());
	for (int row = 0; row < messages.size(); ++row) {
		rowByIdentity.insert(identity(messages.at(row)), row);
	}

	QSet< int > removedRows;
	QSet< quint64 > insertedIdentities;
	bool existingRowsChanged = false;
	for (const MumbleProto::ChatMessage &message : incomingMessages) {
		const quint64 messageIdentity = identity(message);
		const int existingRow         = rowByIdentity.value(messageIdentity, -1);
		if (existingRow >= 0) {
			if (chatMessageDeleted(message)) {
				result.changed = true;
				existingRowsChanged = true;
				removedRows.insert(existingRow);
				rowByIdentity.remove(messageIdentity);
			} else if (messages.at(existingRow).SerializeAsString() != message.SerializeAsString()) {
				result.changed = true;
				existingRowsChanged = true;
				messages[existingRow] = message;
			}
			continue;
		}

		if (chatMessageDeleted(message)) {
			continue;
		}

		result.changed = true;
		const int appendedRow = messages.size();
		messages.push_back(message);
		rowByIdentity.insert(messageIdentity, appendedRow);
		insertedIdentities.insert(messageIdentity);
	}

	if (!removedRows.isEmpty()) {
		QVector< MumbleProto::ChatMessage > retainedMessages;
		retainedMessages.reserve(messages.size() - removedRows.size());
		for (int row = 0; row < messages.size(); ++row) {
			if (!removedRows.contains(row)) {
				retainedMessages.push_back(std::move(messages[row]));
			}
		}
		messages.swap(retainedMessages);
	}

	if (result.changed) {
		sortMessages(messages);
	}

	if (!existingRowsChanged && removedRows.isEmpty() && !insertedIdentities.isEmpty()) {
		const qsizetype insertedCount = insertedIdentities.size();
		bool allInsertedRowsArePrefix = insertedCount <= messages.size();
		for (qsizetype row = 0; allInsertedRowsArePrefix && row < messages.size(); ++row) {
			const bool isInserted = insertedIdentities.contains(identity(messages.at(row)));
			allInsertedRowsArePrefix = row < insertedCount ? isInserted : !isInserted;
		}
		if (allInsertedRowsArePrefix) {
			result.prependedMessageCount = insertedCount;
		}
	}
	return result;
}

unsigned int PersistentChatStore::oldestMessageID(const QVector< MumbleProto::ChatMessage > &messages) {
	return messages.isEmpty() ? 0U : messages.front().message_id();
}
