// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#pragma once

#include "Mumble.pb.h"

#include <QHash>
#include <QString>

namespace Mumble::WatchTogether {

enum class SessionEventAdmission {
	Allowed,
	UnknownSession,
	DuplicateSessionID,
	RoomOccupied,
	ScopeMismatch,
};

inline bool sessionIsInRoom(const MumbleProto::WatchTogetherSync &session, const unsigned int roomID) {
	const MumbleProto::ChatScope scope = session.has_scope() ? session.scope() : MumbleProto::Channel;
	return scope == MumbleProto::Channel && session.has_scope_id() && session.scope_id() == roomID;
}

inline SessionEventAdmission validateSessionEvent(
	const QHash< QString, MumbleProto::WatchTogetherSync > &sessions, const QString &sessionID,
	const unsigned int roomID, const MumbleProto::WatchTogetherEvent event) {
	const auto stored = sessions.constFind(sessionID);
	if (event == MumbleProto::WatchTogetherEventStart) {
		if (stored != sessions.cend()) return SessionEventAdmission::DuplicateSessionID;
		for (const MumbleProto::WatchTogetherSync &session : sessions) {
			if (sessionIsInRoom(session, roomID)) return SessionEventAdmission::RoomOccupied;
		}
		return SessionEventAdmission::Allowed;
	}

	if (stored == sessions.cend()) return SessionEventAdmission::UnknownSession;
	return sessionIsInRoom(*stored, roomID) ? SessionEventAdmission::Allowed
										 : SessionEventAdmission::ScopeMismatch;
}

} // namespace Mumble::WatchTogether
