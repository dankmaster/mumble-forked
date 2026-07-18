// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WatchTogetherSession.h"

#include <QtTest>

class TestWatchTogetherSession : public QObject {
	Q_OBJECT

private slots:
	void rejectsRoomMoveEventsForStoredSession();
	void rejectsStaleJoinAndStateRequest();
	void rejectsSecondStartInRoom();
};

namespace {
MumbleProto::WatchTogetherSync sessionInRoom(const unsigned int roomID) {
	MumbleProto::WatchTogetherSync session;
	session.set_scope(MumbleProto::Channel);
	session.set_scope_id(roomID);
	return session;
}
} // namespace

void TestWatchTogetherSession::rejectsRoomMoveEventsForStoredSession() {
	QHash< QString, MumbleProto::WatchTogetherSync > sessions;
	sessions.insert(QStringLiteral("room-a-session"), sessionInRoom(10));

	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("room-a-session"), 10, MumbleProto::WatchTogetherEventState),
		Mumble::WatchTogether::SessionEventAdmission::Allowed);
	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("room-a-session"), 20, MumbleProto::WatchTogetherEventState),
		Mumble::WatchTogether::SessionEventAdmission::ScopeMismatch);
}

void TestWatchTogetherSession::rejectsStaleJoinAndStateRequest() {
	QHash< QString, MumbleProto::WatchTogetherSync > sessions;
	sessions.insert(QStringLiteral("old-room-session"), sessionInRoom(10));

	for (const MumbleProto::WatchTogetherEvent event : {
			 MumbleProto::WatchTogetherEventJoin, MumbleProto::WatchTogetherEventStateRequest }) {
		QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
				 sessions, QStringLiteral("old-room-session"), 20, event),
			Mumble::WatchTogether::SessionEventAdmission::ScopeMismatch);
	}
	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("missing-session"), 20, MumbleProto::WatchTogetherEventStateRequest),
		Mumble::WatchTogether::SessionEventAdmission::UnknownSession);
}

void TestWatchTogetherSession::rejectsSecondStartInRoom() {
	QHash< QString, MumbleProto::WatchTogetherSync > sessions;
	sessions.insert(QStringLiteral("first-session"), sessionInRoom(10));

	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("second-session"), 10, MumbleProto::WatchTogetherEventStart),
		Mumble::WatchTogether::SessionEventAdmission::RoomOccupied);
	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("second-session"), 20, MumbleProto::WatchTogetherEventStart),
		Mumble::WatchTogether::SessionEventAdmission::Allowed);
	QCOMPARE(Mumble::WatchTogether::validateSessionEvent(
			 sessions, QStringLiteral("first-session"), 20, MumbleProto::WatchTogetherEventStart),
		Mumble::WatchTogether::SessionEventAdmission::DuplicateSessionID);
}

QTEST_GUILESS_MAIN(TestWatchTogetherSession)
#include "TestWatchTogetherSession.moc"
