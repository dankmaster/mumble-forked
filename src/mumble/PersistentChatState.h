// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_

#include "Mumble.pb.h"

#include <QtCore/QVector>
#include <QtCore/QString>

enum class PersistentChatLoadingState {
	Idle,
	Initial,
	Refreshing,
	Older
};

struct PersistentChatScopeKey {
	bool valid                = false;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID      = 0;

	static PersistentChatScopeKey fromScope(MumbleProto::ChatScope scopeValue, unsigned int scopeIDValue) {
		PersistentChatScopeKey key;
		key.valid   = true;
		key.scope   = scopeValue;
		key.scopeID = scopeIDValue;
		return key;
	}

	QString cacheKey() const {
		return valid ? QString::fromLatin1("%1:%2").arg(static_cast< int >(scope)).arg(scopeID) : QString();
	}

	bool matches(MumbleProto::ChatScope scopeValue, unsigned int scopeIDValue) const {
		return valid && scope == scopeValue && scopeID == scopeIDValue;
	}
};

struct PersistentChatScopeStateSnapshot {
	PersistentChatScopeKey key;
	QVector< MumbleProto::ChatMessage > messages;
	// Changes only when the message collection changes. Loading/read-state
	// transitions deliberately keep the same revision so the frontend can
	// update its chrome without rebuilding an unchanged timeline.
	quint64 messagesRevision                = 0;
	qsizetype prependedMessageCount         = 0;
	bool initialLoaded                    = false;
	bool hasOlder                         = false;
	unsigned int oldestLoadedMessageId    = 0;
	unsigned int lastReadMessageId        = 0;
	int unreadCount                       = 0;
	PersistentChatLoadingState loadingState = PersistentChatLoadingState::Idle;
	QString errorMessage;
};

// What changed in the active scope's messages, so a consumer can update only
// those rows instead of diffing the whole snapshot.
struct PersistentChatMessageChange {
	enum class Kind {
		// The active scope changed or the store was cleared; key is the new scope.
		Reset,
		// Messages were added, replaced or removed (deleted messages leave the store).
		Messages,
		// A message's embeds or reactions were replaced in place.
		Patched
	};
	Kind kind = Kind::Reset;
	PersistentChatScopeKey key;
	// (thread_id << 32) | message_id of every affected message.
	QVector< quint64 > identities;

	static quint64 identity(const MumbleProto::ChatMessage &message) {
		return (static_cast< quint64 >(message.thread_id()) << 32U) | static_cast< quint64 >(message.message_id());
	}
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_
