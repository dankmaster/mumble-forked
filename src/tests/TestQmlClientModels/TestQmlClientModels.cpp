// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"
#include "ChatPerfTrace.h"
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

Q_DECLARE_METATYPE(PttSafetyReason)

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void largeSynchronizationsStayResetFree();
	void roomAndParticipantStatesStayIncremental();
	void stableIdsRemainIndependentFromSourceMaps();
	void messageRolesExposeStructuredState();
	void chatTimelineAppliesDirectIncrementalMessages();
	void chatTimelinePreservesTypedAttachments();
	void participantPresenceUpdatesOnlyTypedRoles();
	void participantUpsertsAndRemovalsStayResetFree();
	void workloadSourceStateRoundTripsExactly();
	void steadyStateRejectsFullBootstrap();
	void duplicateStableIdsAreCoalesced();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void sessionPublishesTypedUpdateBanner();
	void commandsRejectEmptyStableIds();
	void pttStateIsIdempotentAndReleases();
	void pttSafetyTriggersReleaseExactlyOnce_data();
	void pttSafetyTriggersReleaseExactlyOnce();
	void dialogStateRoutesTypedRequests();
	void imageViewerStateRemainsStructured();
	void stonksStateAndActionsRemainStructured();
	void asyncOperationsExposeProgressAndCancellation();
	void asyncOperationsClampProgressAndInterruptByPrefix();
	void mediaSessionValidatesAndPublishesTypedState();
	void mediaSessionProviderAllowlist_data();
	void mediaSessionProviderAllowlist();
	void mediaSessionNavigationAndErrorLifecycle();
	void selectionStateOnlyNotifiesForRealChanges();
	void selectionStateInvalidatesRemovedStableIds();
	void selectionStateRejectsUnknownIdsAndSurvivesResync();
	void invalidRowsAndCommandsAreIgnored();
	void visualFixtureOverrideRejectsLiveMutations();
};

void TestQmlClientModels::workloadSourceStateRoundTripsExactly() {
	ChatTimelineModel chat;
	ParticipantModel participants;
	const QVariantList liveMessages {
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("live-1") },
					  { QStringLiteral("bodyText"), QStringLiteral("live body") },
					  { QStringLiteral("own"), true } }
	};
	const QVariantList liveParticipants {
		QVariantMap { { QStringLiteral("session"), QStringLiteral("7") },
					  { QStringLiteral("name"), QStringLiteral("Live user") },
					  { QStringLiteral("talkState"), QStringLiteral("passive") } }
	};
	chat.replaceMessages(liveMessages);
	participants.replaceParticipantStates(liveParticipants);
	QCOMPARE(chat.messages(), liveMessages);
	QCOMPARE(participants.participantStates(), liveParticipants);

	chat.replaceMessages({ QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("perf-1") },
									  { QStringLiteral("bodyText"), QStringLiteral("temporary") } } });
	participants.replaceParticipantStates({ QVariantMap { { QStringLiteral("session"), QStringLiteral("99") },
											 { QStringLiteral("name"), QStringLiteral("Temporary") },
											 { QStringLiteral("talkState"), QStringLiteral("passive") } } });
	participants.updatePresence(QStringLiteral("99"), QStringLiteral("talking"), QStringLiteral("Talking"),
								QStringLiteral("accent"), true, false, {}, {});
	QCOMPARE(participants.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("talking"));

	chat.replaceMessages(liveMessages);
	participants.replaceParticipantStates(liveParticipants);
	QCOMPARE(chat.messages(), liveMessages);
	QCOMPARE(participants.participantStates(), liveParticipants);
}

void TestQmlClientModels::visualFixtureOverrideRejectsLiveMutations() {
	ClientSessionController session;
	ActiveScopeController scope;
	RoomModel rooms;
	ParticipantModel participants;
	ChatTimelineModel chat;
	AsyncOperationModel operations;
	DialogStateController dialog;
	const QList< QObject * > guarded { &session, &scope, &rooms, &participants, &chat, &operations, &dialog };
	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::OverrideProperty, true);

	session.setServerName(QStringLiteral("live"));
	scope.setLabel(QStringLiteral("live"));
	rooms.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("live") } } }, {});
	participants.replaceParticipantStates({ QVariantMap { { QStringLiteral("session"), QStringLiteral("1") } } });
	chat.replaceMessages({ QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("live") } } });
	operations.startOperation(QStringLiteral("live"), QStringLiteral("live"), {}, false);
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("live") } });
	QCOMPARE(session.serverName(), QStringLiteral("Mumble"));
	QVERIFY(scope.label().isEmpty());
	QCOMPARE(rooms.rowCount(), 0);
	QCOMPARE(participants.rowCount(), 0);
	QCOMPARE(chat.rowCount(), 0);
	QCOMPARE(operations.rowCount(), 0);
	QVERIFY(!dialog.open());

	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::WriteProperty, true);
	session.setServerName(QStringLiteral("fixture"));
	scope.setLabel(QStringLiteral("fixture"));
	rooms.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("fixture") },
												{ QStringLiteral("label"), QStringLiteral("Fixture") } } }, {});
	participants.replaceParticipantStates({ QVariantMap { { QStringLiteral("session"), QStringLiteral("2") },
														 { QStringLiteral("name"), QStringLiteral("Fixture") } } });
	chat.replaceMessages({ QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture") },
										 { QStringLiteral("bodyText"), QStringLiteral("Fixture one") } },
						 QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:2") },
										 { QStringLiteral("bodyText"), QStringLiteral("Fixture two") } } });
	operations.startOperation(QStringLiteral("fixture"), QStringLiteral("Fixture"), {}, false);
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("fixture") } });
	QCOMPARE(session.serverName(), QStringLiteral("fixture"));
	QCOMPARE(scope.label(), QStringLiteral("fixture"));
	QCOMPARE(rooms.rowCount(), 1);
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(chat.rowCount(), 2);
	QCOMPARE(operations.rowCount(), 1);
	QVERIFY(dialog.open());
	QCOMPARE(dialog.dialogId(), QStringLiteral("fixture"));

	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::WriteProperty, false);
	session.setServerName(QStringLiteral("clobber"));
	rooms.clear();
	QCOMPARE(session.serverName(), QStringLiteral("fixture"));
	QCOMPARE(rooms.rowCount(), 1);
}

void TestQmlClientModels::stableRowsUpdateWithoutReset() {
	RoomModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
					  { QStringLiteral("title"), QStringLiteral("Lobby") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
					  { QStringLiteral("title"), QStringLiteral("Landing") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Landing"));

	model.removeRow(QStringLiteral("voice:1"));
	QCOMPARE(model.rowCount(), 0);
	QCOMPARE(removeSpy.count(), 1);
}

void TestQmlClientModels::activeScopeAppliesTypedState() {
	ActiveScopeController scope;
	QSignalSpy labelSpy(&scope, &ActiveScopeController::labelChanged);
	QSignalSpy sendSpy(&scope, &ActiveScopeController::canSendChanged);
	QSignalSpy replySpy(&scope, &ActiveScopeController::hasPendingReplyChanged);
	QSignalSpy olderSpy(&scope, &ActiveScopeController::canLoadOlderChanged);
	QSignalSpy loadingSpy(&scope, &ActiveScopeController::loadingStateChanged);
	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("canSend"), true }, { QStringLiteral("hasPendingReply"), true },
					   { QStringLiteral("replyActor"), QStringLiteral("Alice") },
					   { QStringLiteral("replySnippet"), QStringLiteral("Hello") },
					   { QStringLiteral("canAttachImages"), true }, { QStringLiteral("canLoadOlder"), true },
					   { QStringLiteral("loading"), true },
					   { QStringLiteral("loadingState"), QStringLiteral("older") } });
	QCOMPARE(scope.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(scope.label(), QStringLiteral("Lobby"));
	QVERIFY(scope.canSend());
	QVERIFY(scope.hasPendingReply());
	QCOMPARE(scope.replyActor(), QStringLiteral("Alice"));
	QCOMPARE(scope.replySnippet(), QStringLiteral("Hello"));
	QVERIFY(scope.canAttachImages());
	QVERIFY(scope.canLoadOlder());
	QVERIFY(scope.loading());
	QCOMPARE(scope.loadingState(), QStringLiteral("older"));
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
	QCOMPARE(loadingSpy.count(), 1);

	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("canSend"), true }, { QStringLiteral("hasPendingReply"), true },
					   { QStringLiteral("replyActor"), QStringLiteral("Alice") },
					   { QStringLiteral("replySnippet"), QStringLiteral("Hello") },
					   { QStringLiteral("canAttachImages"), true }, { QStringLiteral("canLoadOlder"), true },
					   { QStringLiteral("loading"), true },
					   { QStringLiteral("loadingState"), QStringLiteral("older") } });
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
	QCOMPARE(loadingSpy.count(), 1);
}

void TestQmlClientModels::synchronizeRowsUsesIncrementalSignals() {
	RoomModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:1") },
										 { QStringLiteral("title"), QStringLiteral("Lobby") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:2") },
										 { QStringLiteral("title"), QStringLiteral("Games") } } });
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(insertSpy.count(), 1);

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:2") },
										 { QStringLiteral("title"), QStringLiteral("Gaming") } } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("voice:2"));
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Gaming"));
	QCOMPARE(moveSpy.count(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::largeSynchronizationsStayResetFree() {
	ChatTimelineModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QVariantList messages;
	messages.reserve(10000);
	for (int row = 0; row < 10000; ++row) {
		messages.push_back(QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("message:%1").arg(row) },
									 { QStringLiteral("plainText"), QStringLiteral("Body %1").arg(row) } });
	}
	model.replaceMessages(messages);
	QCOMPARE(model.rowCount(), 10000);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);

	QVariantMap changed = messages.at(5000).toMap();
	changed.insert(QStringLiteral("plainText"), QStringLiteral("Updated body"));
	messages[5000] = changed;
	model.replaceMessages(messages);
	QCOMPARE(changedSpy.count(), 1);
	const QList< int > roles = changedSpy.takeFirst().at(2).value< QList< int > >();
	QVERIFY(roles.contains(StableListModel::PayloadRole));
	QVERIFY(roles.contains(StableListModel::SubtitleRole));
	QCOMPARE(resetSpy.count(), 0);

	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
	model.clear();
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::roomAndParticipantStatesStayIncremental() {
	RoomModel rooms;
	QSignalSpy roomResetSpy(&rooms, &QAbstractItemModel::modelReset);
	QSignalSpy roomInsertSpy(&rooms, &QAbstractItemModel::rowsInserted);
	QSignalSpy roomChangedSpy(&rooms, &QAbstractItemModel::dataChanged);
	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("joined"), true } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("text:2") },
						{ QStringLiteral("label"), QStringLiteral("General") } } });
	QCOMPARE(rooms.rowCount(), 2);
	QCOMPARE(roomInsertSpy.count(), 1);
	QCOMPARE(roomResetSpy.count(), 0);
	roomChangedSpy.clear();
	rooms.selectScope(QStringLiteral("text:2"));
	QVERIFY(!rooms.get(0).value(QStringLiteral("selected")).toBool());
	QVERIFY(rooms.get(1).value(QStringLiteral("selected")).toBool());
	QVERIFY(roomChangedSpy.count() > 0);
	QCOMPARE(roomResetSpy.count(), 0);
	roomChangedSpy.clear();

	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("joined"), true }, { QStringLiteral("unreadCount"), 3 } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("text:2") },
						{ QStringLiteral("label"), QStringLiteral("General") },
						{ QStringLiteral("selected"), true } } });
	QCOMPARE(rooms.rowCount(), 2);
	QCOMPARE(roomInsertSpy.count(), 1);
	QCOMPARE(roomChangedSpy.count(), 1);
	QCOMPARE(roomResetSpy.count(), 0);
	rooms.replaceDirectMessageStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("dm:7") },
						{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
						{ QStringLiteral("subtitle"), QStringLiteral("Direct message") },
						{ QStringLiteral("unreadCount"), 2 } } });
	QCOMPARE(rooms.rowCount(), 3);
	QCOMPARE(rooms.get(2).value(QStringLiteral("id")).toString(), QStringLiteral("direct:dm:7"));
	QCOMPARE(rooms.get(2).value(QStringLiteral("kind")).toString(), QStringLiteral("direct"));
	QCOMPARE(roomInsertSpy.count(), 2);
	QCOMPARE(roomResetSpy.count(), 0);
	rooms.replaceDirectMessageStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("dm:7") },
						{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
						{ QStringLiteral("subtitle"), QStringLiteral("Direct message") },
						{ QStringLiteral("unreadCount"), 0 }, { QStringLiteral("open"), true } } });
	QCOMPARE(rooms.rowCount(), 3);
	QVERIFY(rooms.get(2).value(QStringLiteral("selected")).toBool());
	QCOMPARE(roomResetSpy.count(), 0);

	ParticipantModel participants;
	QSignalSpy participantResetSpy(&participants, &QAbstractItemModel::modelReset);
	QSignalSpy participantChangedSpy(&participants, &QAbstractItemModel::dataChanged);
	const QVariantList initialParticipants { QVariantMap { { QStringLiteral("session"), 7 },
														{ QStringLiteral("name"), QStringLiteral("Alice") },
														{ QStringLiteral("talkState"), QStringLiteral("passive") } } };
	participants.replaceParticipantStates(initialParticipants);
	participants.replaceParticipantStates(
		{ QVariantMap { { QStringLiteral("session"), 7 }, { QStringLiteral("name"), QStringLiteral("Alice") },
						{ QStringLiteral("talkState"), QStringLiteral("talking") } } });
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(participantChangedSpy.count(), 1);
	QCOMPARE(participantResetSpy.count(), 0);
}

void TestQmlClientModels::stableIdsRemainIndependentFromSourceMaps() {
	RoomModel model;
	QVariantMap source { { QStringLiteral("id"), QStringLiteral("voice:1") },
						 { QStringLiteral("title"), QStringLiteral("Lobby") } };
	model.synchronizeRows({ source });
	source.insert(QStringLiteral("id"), QStringLiteral("mutated"));
	source.insert(QStringLiteral("title"), QStringLiteral("Mutated"));

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
						  { QStringLiteral("title"), QStringLiteral("Landing") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("voice:1"));
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Landing"));
}

void TestQmlClientModels::messageRolesExposeStructuredState() {
	ChatTimelineModel model;
	model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("message:7") },
						  { QStringLiteral("actor"), QStringLiteral("Alice") },
						  { QStringLiteral("bodyText"), QStringLiteral("Hello") },
						  { QStringLiteral("timeLabel"), QStringLiteral("12:30") },
						  { QStringLiteral("replyActor"), QStringLiteral("Bob") },
						  { QStringLiteral("replySnippet"), QStringLiteral("Earlier") },
						  { QStringLiteral("reactions"), QVariantList { QVariantMap {
							  { QStringLiteral("emoji"), QStringLiteral("+") }, { QStringLiteral("count"), 2 } } } },
						  { QStringLiteral("preview"), QVariantMap {
							  { QStringLiteral("title"), QStringLiteral("Example") } } },
						  { QStringLiteral("canReply"), true } });

	const QHash< int, QByteArray > roles = model.roleNames();
	const auto roleForName = [&roles](const QByteArray &name) {
		for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
			if (it.value() == name) return it.key();
		}
		return -1;
	};
	const QModelIndex row = model.index(0, 0);
	QCOMPARE(model.data(row, roleForName("timestamp")).toString(), QStringLiteral("12:30"));
	QCOMPARE(model.data(row, roleForName("replyActor")).toString(), QStringLiteral("Bob"));
	QCOMPARE(model.data(row, roleForName("reactions")).toList().size(), 1);
	QCOMPARE(model.data(row, roleForName("preview")).toMap().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Example"));
	QVERIFY(model.data(row, roleForName("canReply")).toBool());
	QCOMPARE(model.data(row, roleForName("source")).toMap().value(QStringLiteral("bodyText")).toString(),
			 QStringLiteral("Hello"));
	QCOMPARE(roles.value(StableListModel::SourceRole), QByteArray("source"));

	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QCOMPARE(model.applyMessage({ { QStringLiteral("messageKey"), QStringLiteral("message:7") },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("Hello") },
								 { QStringLiteral("timeLabel"), QStringLiteral("12:30") },
								 { QStringLiteral("replyActor"), QStringLiteral("Bob") },
								 { QStringLiteral("replySnippet"), QStringLiteral("Earlier") },
								 { QStringLiteral("reactions"), QVariantList { QVariantMap {
									 { QStringLiteral("emoji"), QStringLiteral("+") }, { QStringLiteral("count"), 2 } } } },
								 { QStringLiteral("preview"), QVariantMap {
									 { QStringLiteral("title"), QStringLiteral("Example") } } },
								 { QStringLiteral("canReply"), true },
								 { QStringLiteral("fixtureMarker"), QStringLiteral("source-only-change") } }),
			 ChatTimelineModel::MessageMutation::Updated);
	QCOMPARE(changedSpy.count(), 1);
	const QList< int > changedRoles = changedSpy.takeFirst().at(2).value< QList< int > >();
	QVERIFY(changedRoles.contains(StableListModel::SourceRole));
	QCOMPARE(model.data(row, StableListModel::SourceRole).toMap().value(QStringLiteral("fixtureMarker")).toString(),
			 QStringLiteral("source-only-change"));
}

void TestQmlClientModels::chatTimelineAppliesDirectIncrementalMessages() {
	ChatTimelineModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

	QCOMPARE(model.applyMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("First") } }),
			 ChatTimelineModel::MessageMutation::Inserted);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("41"));

	QCOMPARE(model.applyMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("Edited") },
								 { QStringLiteral("deliveryState"), QStringLiteral("delivered") } }),
			 ChatTimelineModel::MessageMutation::Updated);
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(changedSpy.first().at(0).toModelIndex().row(), 0);
	QCOMPARE(changedSpy.first().at(1).toModelIndex().row(), 0);
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(model.applyMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("Edited") },
								 { QStringLiteral("deliveryState"), QStringLiteral("delivered") } }),
			 ChatTimelineModel::MessageMutation::Unchanged);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Edited"));
	QCOMPARE(model.applyMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("deleted"), true } }),
			 ChatTimelineModel::MessageMutation::Updated);
	QCOMPARE(model.rowCount(), 1);
	QVERIFY(model.get(0).value(QStringLiteral("deleted")).toBool());
	QCOMPARE(changedSpy.count(), 2);

	const int appended = model.appendMessages(
		{ QVariantMap { { QStringLiteral("messageId"), 42 }, { QStringLiteral("bodyText"), QStringLiteral("Second") } },
		  QVariantMap { { QStringLiteral("bodyText"), QStringLiteral("Missing stable ID") } } });
	QCOMPARE(appended, 1);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(insertSpy.count(), 2);
	QCOMPARE(resetSpy.count(), 0);
	QVERIFY(model.removeMessage(QStringLiteral("41")));
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(removeSpy.first().at(1).toInt(), 0);
	QCOMPARE(removeSpy.first().at(2).toInt(), 0);
	QCOMPARE(model.rowCount(), 1);
	QVERIFY(!model.removeMessage(QStringLiteral("missing")));
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::chatTimelinePreservesTypedAttachments() {
	ChatTimelineModel model;
	const QVariantMap attachment { { QStringLiteral("id"), QStringLiteral("asset:1") },
								   { QStringLiteral("name"), QStringLiteral("image.png") },
								   { QStringLiteral("mime"), QStringLiteral("image/png") } };
	QVERIFY(model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("attachment:1") },
								  { QStringLiteral("actor"), QStringLiteral("Alice") },
								  { QStringLiteral("attachments"), QVariantList { attachment } } }));
	QCOMPARE(model.rowCount(), 1);
	const QVariantMap source = model.get(0).value(QStringLiteral("source")).toMap();
	QCOMPARE(source.value(QStringLiteral("attachments")).toList().size(), 1);
	QCOMPARE(source.value(QStringLiteral("attachments")).toList().first().toMap(), attachment);
	model.removeRow(QStringLiteral("attachment:1"));
	QCOMPARE(model.rowCount(), 0);
}

void TestQmlClientModels::participantPresenceUpdatesOnlyTypedRoles() {
	ParticipantModel model;
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("7") },
					  { QStringLiteral("title"), QStringLiteral("Alice") },
					  { QStringLiteral("avatarUrl"), QStringLiteral("image://avatars/7") },
					  { QStringLiteral("status"), QStringLiteral("passive") },
					  { QStringLiteral("source"), QVariantMap {
							{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
							{ QStringLiteral("talkState"), QStringLiteral("passive") } } } });
	changedSpy.clear();

	const QVariantList badges { QStringLiteral("Priority speaker") };
	const QVariantList statuses { QVariantMap { { QStringLiteral("id"), QStringLiteral("talking") } } };
	model.updatePresence(QStringLiteral(" 7 "), QStringLiteral("talking"), QStringLiteral("Talking"),
						 QStringLiteral("success"), true, false, badges, statuses);
	QCOMPARE(changedSpy.count(), 1);
	const QVariantMap row = model.get(0);
	QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Alice"));
	QCOMPARE(row.value(QStringLiteral("avatarUrl")).toString(), QStringLiteral("image://avatars/7"));
	QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("talking"));
	const QVariantMap source = row.value(QStringLiteral("source")).toMap();
	QCOMPARE(source.value(QStringLiteral("label")).toString(), QStringLiteral("Alice"));
	QCOMPARE(source.value(QStringLiteral("talkLabel")).toString(), QStringLiteral("Talking"));
	QCOMPARE(source.value(QStringLiteral("talkTone")).toString(), QStringLiteral("success"));
	QVERIFY(source.value(QStringLiteral("talking")).toBool());
	QCOMPARE(source.value(QStringLiteral("badges")).toList(), badges);
	model.updatePresence(QStringLiteral("7"), QStringLiteral("talking"), QStringLiteral("Talking"),
						 QStringLiteral("success"), true, false, badges, statuses);
	QCOMPARE(changedSpy.count(), 1);

	model.updatePresence(QStringLiteral("99"), QStringLiteral("talking"), QString(), QString(), true, false, {}, {});
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
}

void TestQmlClientModels::participantUpsertsAndRemovalsStayResetFree() {
	ParticipantModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
	model.upsertParticipantState({ { QStringLiteral("session"), 7 },
		{ QStringLiteral("name"), QStringLiteral("Alice") },
		{ QStringLiteral("talkState"), QStringLiteral("passive") } });
	model.upsertParticipantState({ { QStringLiteral("session"), 7 },
		{ QStringLiteral("name"), QStringLiteral("Alice 2") },
		{ QStringLiteral("talkState"), QStringLiteral("talking") } });
	model.removeParticipant(QStringLiteral("7"));
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::steadyStateRejectsFullBootstrap() {
	mumble::chatperf::FullBootstrapMonitor monitor;
	QVERIFY(!monitor.recordBootstrap());
	monitor.enterSteadyState();
	QVERIFY(monitor.recordBootstrap());
	QCOMPARE(monitor.steadyStateViolations(), 1U);
	monitor.leaveSteadyState();
	QVERIFY(!monitor.recordBootstrap());
	QCOMPARE(monitor.steadyStateViolations(), 1U);
}

void TestQmlClientModels::duplicateStableIdsAreCoalesced() {
	RoomModel model;
	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") },
										 { QStringLiteral("title"), QStringLiteral("Root voice") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") },
										 { QStringLiteral("title"), QStringLiteral("Root replacement") } } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Root replacement"));

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("text:0:0") } } });
	QCOMPARE(model.rowCount(), 2);
}

void TestQmlClientModels::sessionPropertiesOnlyNotifyOnChanges() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::connectedChanged);
	QSignalSpy motdSpy(&session, &ClientSessionController::motdHtmlChanged);
	session.setConnected(false);
	QCOMPARE(spy.count(), 0);
	session.setConnected(true);
	QCOMPARE(spy.count(), 1);
	QVERIFY(session.connected());
	session.setMotdHtml(QStringLiteral("<p>Welcome</p>"));
	session.setMotdHtml(QStringLiteral("<p>Welcome</p>"));
	session.setMotdSummary(QStringLiteral("Welcome"));
	QCOMPARE(motdSpy.count(), 1);
	QCOMPARE(session.motdHtml(), QStringLiteral("<p>Welcome</p>"));
	QCOMPARE(session.motdSummary(), QStringLiteral("Welcome"));
}

void TestQmlClientModels::sessionPublishesTypedUpdateBanner() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::updateBannerChanged);
	const QVariantMap banner { { QStringLiteral("visible"), true },
							 { QStringLiteral("phase"), QStringLiteral("downloading") },
							 { QStringLiteral("title"), QStringLiteral("Downloading update") },
							 { QStringLiteral("progressPercent"), 42 } };
	session.setUpdateBanner(banner);
	QCOMPARE(spy.count(), 1);
	QCOMPARE(session.updateBanner(), banner);
	session.setUpdateBanner(banner);
	QCOMPARE(spy.count(), 1);
	session.setUpdateBanner({});
	QCOMPARE(spy.count(), 2);
	QVERIFY(session.updateBanner().isEmpty());
}

void TestQmlClientModels::commandsRejectEmptyStableIds() {
	UiCommandController commands;
	QSignalSpy scopeSpy(&commands, &UiCommandController::scopeSelectionRequested);
	QSignalSpy actionSpy(&commands, &UiCommandController::actionRequested);
	QSignalSpy participantSpy(&commands, &UiCommandController::participantSelectionRequested);
	QSignalSpy directMessageSpy(&commands, &UiCommandController::directMessageOpenRequested);
	QSignalSpy replySpy(&commands, &UiCommandController::messageReplyRequested);
	QSignalSpy retrySpy(&commands, &UiCommandController::messageRetryRequested);
	QSignalSpy deleteSpy(&commands, &UiCommandController::messageDeleteRequested);
	QSignalSpy reactionSpy(&commands, &UiCommandController::messageReactionToggleRequested);
	QSignalSpy cancelReplySpy(&commands, &UiCommandController::pendingReplyCancelRequested);
	QSignalSpy attachmentSpy(&commands, &UiCommandController::attachmentChooseRequested);
	QSignalSpy olderSpy(&commands, &UiCommandController::olderMessagesRequested);
	commands.selectScope(QStringLiteral("   "));
	commands.invokeAction(QString());
	commands.selectParticipant(QStringLiteral("  "));
	commands.openDirectMessage(QString());
	commands.replyToMessage(QString());
	commands.retryMessage(QStringLiteral("  "));
	commands.deleteMessage(QString());
	commands.toggleMessageReaction(QStringLiteral("message:1"), QString());
	QCOMPARE(scopeSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(participantSpy.count(), 0);
	QCOMPARE(directMessageSpy.count(), 0);
	QCOMPARE(replySpy.count(), 0);
	QCOMPARE(retrySpy.count(), 0);
	QCOMPARE(deleteSpy.count(), 0);
	QCOMPARE(reactionSpy.count(), 0);
	commands.selectScope(QStringLiteral(" channel:42 "));
	commands.invokeAction(QStringLiteral(" qaAudioMute "));
	commands.selectParticipant(QStringLiteral(" 42 "));
	commands.openDirectMessage(QStringLiteral(" 7 "));
	commands.replyToMessage(QStringLiteral(" message:1 "));
	commands.retryMessage(QStringLiteral(" message:2 "));
	commands.deleteMessage(QStringLiteral(" message:3 "));
	commands.toggleMessageReaction(QStringLiteral(" message:4 "), QStringLiteral(" 👍 "));
	commands.cancelPendingReply();
	commands.chooseAttachment();
	commands.requestOlderMessages();
	QCOMPARE(scopeSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(actionSpy.takeFirst().at(0).toString(), QStringLiteral("qaAudioMute"));
	QCOMPARE(participantSpy.takeFirst().at(0).toString(), QStringLiteral("42"));
	QCOMPARE(directMessageSpy.takeFirst().at(0).toString(), QStringLiteral("7"));
	QCOMPARE(replySpy.takeFirst().at(0).toString(), QStringLiteral("message:1"));
	QCOMPARE(retrySpy.takeFirst().at(0).toString(), QStringLiteral("message:2"));
	QCOMPARE(deleteSpy.takeFirst().at(0).toString(), QStringLiteral("message:3"));
	const QList< QVariant > reaction = reactionSpy.takeFirst();
	QCOMPARE(reaction.at(0).toString(), QStringLiteral("message:4"));
	QCOMPARE(reaction.at(1).toString(), QStringLiteral("👍"));
	QCOMPARE(cancelReplySpy.count(), 1);
	QCOMPARE(attachmentSpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
}

void TestQmlClientModels::pttStateIsIdempotentAndReleases() {
	UiCommandController commands;
	QSignalSpy stateSpy(&commands, &UiCommandController::pttStateRequested);
	commands.setPttPressed(true);
	commands.setPttPressed(true);
	QVERIFY(commands.pttPressed());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(stateSpy.takeFirst().at(0).toBool(), true);

	commands.releasePtt();
	QVERIFY(!commands.pttPressed());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(stateSpy.takeFirst().at(0).toBool(), false);
	commands.releasePtt();
	QCOMPARE(stateSpy.count(), 0);
}

void TestQmlClientModels::pttSafetyTriggersReleaseExactlyOnce_data() {
	QTest::addColumn< PttSafetyReason >("reason");
	QTest::newRow("window-closing") << PttSafetyReason::WindowClosing;
	QTest::newRow("window-deactivated") << PttSafetyReason::WindowDeactivated;
	QTest::newRow("application-deactivated") << PttSafetyReason::ApplicationDeactivated;
	QTest::newRow("window-hidden") << PttSafetyReason::WindowHidden;
	QTest::newRow("scene-graph-error") << PttSafetyReason::SceneGraphError;
	QTest::newRow("host-destroyed") << PttSafetyReason::HostDestroyed;
}

void TestQmlClientModels::pttSafetyTriggersReleaseExactlyOnce() {
	QFETCH(PttSafetyReason, reason);
	UiCommandController commands;
	PttSafetyController safety(&commands);
	QSignalSpy stateSpy(&commands, &UiCommandController::pttStateRequested);

	commands.setPttPressed(true);
	safety.release(reason);
	safety.release(reason);

	QVERIFY(!commands.pttPressed());
	QCOMPARE(stateSpy.count(), 2);
	QCOMPARE(stateSpy.at(0).at(0).toBool(), true);
	QCOMPARE(stateSpy.at(1).at(0).toBool(), false);
}

void TestQmlClientModels::dialogStateRoutesTypedRequests() {
	DialogStateController dialog;
	QSignalSpy stateSpy(&dialog, &DialogStateController::stateChanged);
	QSignalSpy fieldSpy(&dialog, &DialogStateController::fieldUpdateRequested);
	QSignalSpy actionSpy(&dialog, &DialogStateController::actionRequested);
	QSignalSpy closeSpy(&dialog, &DialogStateController::closeRequested);

	dialog.updateField(QStringLiteral("audio.input"), 1);
	dialog.invokeAction(QStringLiteral("ok"));
	dialog.requestClose();
	QCOMPARE(fieldSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(closeSpy.count(), 0);

	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("settings") },
						{ QStringLiteral("kind"), QStringLiteral("settings") },
						{ QStringLiteral("title"), QStringLiteral("Settings") },
						{ QStringLiteral("sections"),
						  QVariantList { QVariantMap { { QStringLiteral("fields"),
												 QVariantList { QVariantMap {
													 { QStringLiteral("id"), QStringLiteral("audio.input") },
													 { QStringLiteral("value"), 2 } } } } } } },
						{ QStringLiteral("errors"),
						  QVariantMap { { QStringLiteral("audio.input"), QStringLiteral("Choose an input") } } } });
	QCOMPARE(stateSpy.count(), 1);
	QVERIFY(dialog.open());
	QCOMPARE(dialog.dialogId(), QStringLiteral("settings"));
	QCOMPARE(dialog.fieldValue(QStringLiteral("audio.input")).toInt(), 2);
	QCOMPARE(dialog.fieldError(QStringLiteral("audio.input")), QStringLiteral("Choose an input"));
	QVERIFY(!dialog.fieldValue(QStringLiteral("missing")).isValid());

	dialog.updateField(QStringLiteral(" audio.input "), 2);
	QCOMPARE(fieldSpy.count(), 1);
	QCOMPARE(fieldSpy.takeFirst().at(0).toString(), QStringLiteral("settings"));
	dialog.invokeAction(QStringLiteral(" selectPage "), { { QStringLiteral("pageId"), QStringLiteral("plugins") } });
	QCOMPARE(actionSpy.count(), 1);
	QCOMPARE(actionSpy.takeFirst().at(1).toString(), QStringLiteral("selectPage"));
	const QVariantMap screenSharePayload { { QStringLiteral("channelId"), 42 },
										 { QStringLiteral("sourceId"), QStringLiteral("monitor:0") },
										 { QStringLiteral("resolution"), QStringLiteral("1920x1080") },
										 { QStringLiteral("frameRate"), 60 },
										 { QStringLiteral("audio"), QStringLiteral("default-loopback") } };
	dialog.invokeAction(QStringLiteral("screenShare.start"), screenSharePayload);
	QCOMPARE(actionSpy.count(), 1);
	const QList< QVariant > screenShareAction = actionSpy.takeFirst();
	QCOMPARE(screenShareAction.at(1).toString(), QStringLiteral("screenShare.start"));
	QCOMPARE(screenShareAction.at(2).toMap(), screenSharePayload);
	dialog.requestClose();
	QCOMPARE(closeSpy.count(), 1);
}

void TestQmlClientModels::imageViewerStateRemainsStructured() {
	DialogStateController dialog;
	QSignalSpy closeSpy(&dialog, &DialogStateController::closeRequested);
	const QVariantMap image { { QStringLiteral("src"), QStringLiteral("data:image/png;base64,AA==") },
							  { QStringLiteral("width"), 1920 }, { QStringLiteral("height"), 1080 } };
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("imageViewer") },
						{ QStringLiteral("kind"), QStringLiteral("imageViewer") },
						{ QStringLiteral("title"), QStringLiteral("Screenshot") },
						{ QStringLiteral("imageViewer"), image } });

	QCOMPARE(dialog.kind(), QStringLiteral("imageViewer"));
	QCOMPARE(dialog.state().value(QStringLiteral("imageViewer")).toMap(), image);
	dialog.requestClose();
	QCOMPARE(closeSpy.count(), 1);
	QCOMPARE(closeSpy.takeFirst().at(0).toString(), QStringLiteral("imageViewer"));
}

void TestQmlClientModels::stonksStateAndActionsRemainStructured() {
	DialogStateController dialog;
	QSignalSpy actionSpy(&dialog, &DialogStateController::actionRequested);
	const QVariantMap stonks { { QStringLiteral("selectedPeriod"), QStringLiteral("30d") },
							 { QStringLiteral("periods"), QVariantList { QStringLiteral("7d"), QStringLiteral("30d") } },
							 { QStringLiteral("feedPreferences"),
							   QVariantMap { { QStringLiteral("showMine"), true },
										 { QStringLiteral("showPopular"), false },
										 { QStringLiteral("showPins"), true } } } };
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("stonks") },
						{ QStringLiteral("kind"), QStringLiteral("stonks") },
						{ QStringLiteral("title"), QStringLiteral("Stonks") }, { QStringLiteral("stonks"), stonks } });

	QCOMPARE(dialog.kind(), QStringLiteral("stonks"));
	QCOMPARE(dialog.state().value(QStringLiteral("stonks")).toMap(), stonks);
	const QVariantMap payload { { QStringLiteral("period"), QStringLiteral("7d") } };
	dialog.invokeAction(QStringLiteral("selectPeriod"), payload);
	QCOMPARE(actionSpy.count(), 1);
	const QList< QVariant > action = actionSpy.takeFirst();
	QCOMPARE(action.at(0).toString(), QStringLiteral("stonks"));
	QCOMPARE(action.at(1).toString(), QStringLiteral("selectPeriod"));
	QCOMPARE(action.at(2).toMap(), payload);
}

void TestQmlClientModels::asyncOperationsExposeProgressAndCancellation() {
	AsyncOperationModel operations;
	QSignalSpy cancelSpy(&operations, &AsyncOperationModel::cancellationRequested);
	operations.startOperation(QStringLiteral("plugin-update:7"), QStringLiteral("Example"),
							  QStringLiteral("Downloading"), true);
	QCOMPARE(operations.rowCount(), 1);
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("running"));

	operations.updateProgress(QStringLiteral("plugin-update:7"), 25, 100);
	QCOMPARE(operations.get(0).value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(),
			 25);
	operations.cancel(QStringLiteral(" plugin-update:7 "));
	QCOMPARE(cancelSpy.count(), 1);
	QCOMPARE(cancelSpy.takeFirst().at(0).toString(), QStringLiteral("plugin-update:7"));

	operations.finishOperation(QStringLiteral("plugin-update:7"), false, QStringLiteral("network-error"),
							   QStringLiteral("Offline"));
	const QVariantMap finished = operations.get(0);
	QCOMPARE(finished.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(finished.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Offline"));
	QVERIFY(!finished.value(QStringLiteral("payload")).toMap().value(QStringLiteral("cancellable")).toBool());

	operations.dismiss(QStringLiteral(" plugin-update:7 "));
	QCOMPARE(operations.rowCount(), 0);

	operations.startOperation(QStringLiteral("plugin-update:8"), QStringLiteral("Another plugin"),
							  QStringLiteral("Downloading"), true);
	operations.dismiss(QStringLiteral("plugin-update:8"));
	QCOMPARE(operations.rowCount(), 1);
}

void TestQmlClientModels::asyncOperationsClampProgressAndInterruptByPrefix() {
	AsyncOperationModel operations;
	operations.startOperation(QStringLiteral("plugin-update:one"), QStringLiteral("One"), QString(), true);
	operations.startOperation(QStringLiteral("plugin-update:two"), QStringLiteral("Two"), QString(), true);
	operations.startOperation(QStringLiteral("download:one"), QStringLiteral("Download"), QString(), true);

	operations.updateProgress(QStringLiteral("plugin-update:one"), 125, 100);
	QCOMPARE(operations.get(0).value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(),
			 100);
	operations.updateProgress(QStringLiteral("plugin-update:two"), 5, 0);
	const QVariantMap indeterminate = operations.get(1).value(QStringLiteral("payload")).toMap();
	QCOMPARE(indeterminate.value(QStringLiteral("progress")).toInt(), -1);
	QVERIFY(indeterminate.value(QStringLiteral("indeterminate")).toBool());

	operations.interruptOperations(QStringLiteral("plugin-update:"));
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(operations.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(operations.get(2).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
}

void TestQmlClientModels::selectionStateOnlyNotifiesForRealChanges() {
	QmlSelectionState selection;
	QSignalSpy scopeSpy(&selection, &QmlSelectionState::scopeTokenChanged);
	QSignalSpy scopeValueSpy(&selection, &QmlSelectionState::scopeValueChanged);
	QSignalSpy scopeIdSpy(&selection, &QmlSelectionState::scopeIdChanged);
	QSignalSpy userSpy(&selection, &QmlSelectionState::selectedUserSessionChanged);
	QSignalSpy channelSpy(&selection, &QmlSelectionState::selectedVoiceChannelIdChanged);

	selection.applySelection(QStringLiteral("channel:42"), 1, 42, 7, 42);
	selection.applySelection(QStringLiteral("channel:42"), 1, 42, 7, 42);

	QCOMPARE(scopeSpy.count(), 1);
	QCOMPARE(scopeValueSpy.count(), 1);
	QCOMPARE(scopeIdSpy.count(), 1);
	QCOMPARE(userSpy.count(), 1);
	QCOMPARE(channelSpy.count(), 1);
	QCOMPARE(selection.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(selection.scopeValue(), 1);
	QCOMPARE(selection.scopeId().toULongLong(), 42ULL);
	QCOMPARE(selection.selectedUserSession().toInt(), 7);
	QCOMPARE(selection.selectedVoiceChannelId().toInt(), 42);
}

void TestQmlClientModels::selectionStateInvalidatesRemovedStableIds() {
	RoomModel rooms;
	ParticipantModel participants;
	rooms.replaceRoomStates(
		{ QVariantMap{ { QStringLiteral("token"), QStringLiteral("channel:42") },
						 { QStringLiteral("label"), QStringLiteral("Lobby") } } },
		{});
	participants.replaceParticipantStates(
		{ QVariantMap{ { QStringLiteral("session"), 7 }, { QStringLiteral("name"), QStringLiteral("Alice") } } });
	QmlSelectionState selection;
	selection.bindModels(&rooms, &participants);
	selection.applySelection(QStringLiteral("channel:42"), 1, 42, 7, 42);

	participants.replaceParticipantStates({});
	QVERIFY(!selection.selectedUserSession().isValid());
	QCOMPARE(selection.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(selection.selectedVoiceChannelId().toULongLong(), 42ULL);

	rooms.replaceRoomStates({}, {});
	QVERIFY(selection.scopeToken().isEmpty());
	QCOMPARE(selection.scopeValue(), -1);
	QVERIFY(!selection.scopeId().isValid());
	QVERIFY(!selection.selectedVoiceChannelId().isValid());
}

void TestQmlClientModels::selectionStateRejectsUnknownIdsAndSurvivesResync() {
	RoomModel rooms;
	ParticipantModel participants;
	rooms.replaceRoomStates(
		{ QVariantMap{ { QStringLiteral("token"), QStringLiteral("channel:3") },
						 { QStringLiteral("label"), QStringLiteral("One") } } },
		{});
	participants.replaceParticipantStates(
		{ QVariantMap{ { QStringLiteral("session"), 9 }, { QStringLiteral("name"), QStringLiteral("Bob") } } });
	QmlSelectionState selection;
	selection.bindModels(&rooms, &participants);
	selection.setScopeToken(QStringLiteral("channel:404"));
	selection.setSelectedVoiceChannelId(404);
	selection.setSelectedUserSession(404);
	QVERIFY(selection.scopeToken().isEmpty());
	QVERIFY(!selection.selectedVoiceChannelId().isValid());
	QVERIFY(!selection.selectedUserSession().isValid());

	selection.setScopeToken(QStringLiteral("channel:3"));
	selection.setSelectedVoiceChannelId(3);
	selection.setSelectedUserSession(9);
	rooms.replaceRoomStates(
		{ QVariantMap{ { QStringLiteral("token"), QStringLiteral("channel:3") },
						 { QStringLiteral("label"), QStringLiteral("Renamed") } } },
		{});
	participants.replaceParticipantStates(
		{ QVariantMap{ { QStringLiteral("session"), QStringLiteral("9") },
						 { QStringLiteral("name"), QStringLiteral("Renamed Bob") } } });
	QCOMPARE(selection.scopeToken(), QStringLiteral("channel:3"));
	QCOMPARE(selection.selectedVoiceChannelId().toULongLong(), 3ULL);
	QCOMPARE(selection.selectedUserSession().toULongLong(), 9ULL);
}

void TestQmlClientModels::invalidRowsAndCommandsAreIgnored() {
	RoomModel model;
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy countSpy(&model, &StableListModel::countChanged);
	model.upsertRow({ { QStringLiteral("title"), QStringLiteral("Missing ID") } });
	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QString() } }, QVariantMap {} });
	QCOMPARE(model.rowCount(), 0);
	QCOMPARE(insertSpy.count(), 0);
	QCOMPARE(countSpy.count(), 0);

	UiCommandController commands;
	QSignalSpy joinSpy(&commands, &UiCommandController::voiceJoinRequested);
	QSignalSpy messageSpy(&commands, &UiCommandController::messageSendRequested);
	QSignalSpy scopeActionSpy(&commands, &UiCommandController::scopeActionRequested);
	QSignalSpy participantActionSpy(&commands, &UiCommandController::participantActionRequested);
	commands.joinVoiceChannel(QStringLiteral("  "));
	commands.sendMessage(QStringLiteral("  "));
	commands.invokeScopeAction(QString(), QStringLiteral("join"));
	commands.invokeParticipantAction(QStringLiteral("7"), QString());
	QCOMPARE(joinSpy.count(), 0);
	QCOMPARE(messageSpy.count(), 0);
	QCOMPARE(scopeActionSpy.count(), 0);
	QCOMPARE(participantActionSpy.count(), 0);
	commands.joinVoiceChannel(QStringLiteral(" channel:42 "));
	commands.sendMessage(QStringLiteral(" hello "));
	commands.invokeScopeAction(QStringLiteral(" channel:42 "), QStringLiteral(" join "));
	commands.invokeParticipantAction(QStringLiteral(" 7 "), QStringLiteral(" message "));
	QCOMPARE(joinSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(messageSpy.takeFirst().at(0).toString(), QStringLiteral(" hello "));
	const QList< QVariant > scopeAction = scopeActionSpy.takeFirst();
	QCOMPARE(scopeAction.at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(scopeAction.at(1).toString(), QStringLiteral("join"));
	const QList< QVariant > participantAction = participantActionSpy.takeFirst();
	QCOMPARE(participantAction.at(0).toString(), QStringLiteral("7"));
	QCOMPARE(participantAction.at(1).toString(), QStringLiteral("message"));
}

void TestQmlClientModels::mediaSessionValidatesAndPublishesTypedState() {
	MediaSessionBackend media;
	QSignalSpy playSpy(&media, &MediaSessionBackend::playRequested);
	QSignalSpy pauseSpy(&media, &MediaSessionBackend::pauseRequested);
	QSignalSpy seekSpy(&media, &MediaSessionBackend::seekRequested);
	QVERIFY(!media.open(QUrl(QStringLiteral("http://www.youtube.com/embed/abc")), QStringLiteral("youtube"),
						QStringLiteral("room:1")));
	QCOMPARE(media.state(), QStringLiteral("error"));
	QVERIFY(media.open(QUrl(QStringLiteral("https://www.youtube.com/embed/abc")), QStringLiteral("youtube"),
					   QStringLiteral("room:1")));
	QVERIFY(media.active());
	media.play();
	QCOMPARE(playSpy.count(), 1);
	media.reportPlaybackState(12.5, 90.0, false);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(media.position(), 12.5);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/next")), QStringLiteral("youtube"),
						   QStringLiteral("room:2"), 24.0, true, 42);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	QCOMPARE(media.position(), 24.0);
	QCOMPARE(media.state(), QStringLiteral("paused"));
	QCOMPARE(seekSpy.count(), 1);
	QCOMPARE(pauseSpy.count(), 1);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/stale")), QStringLiteral("youtube"),
						   QStringLiteral("stale"), 1.0, false, 41);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

void TestQmlClientModels::mediaSessionProviderAllowlist_data() {
	QTest::addColumn< QString >("provider");
	QTest::addColumn< QUrl >("url");
	QTest::addColumn< bool >("allowed");
	QTest::newRow("youtube") << QStringLiteral("youtube") << QUrl(QStringLiteral("https://www.youtube.com/embed/a")) << true;
	QTest::newRow("twitch") << QStringLiteral("twitch") << QUrl(QStringLiteral("https://player.twitch.tv/?video=1")) << true;
	QTest::newRow("streamable") << QStringLiteral("streamable") << QUrl(QStringLiteral("https://streamable.com/e/a")) << true;
	QTest::newRow("vimeo") << QStringLiteral("vimeo") << QUrl(QStringLiteral("https://player.vimeo.com/video/1")) << true;
	QTest::newRow("dailymotion") << QStringLiteral("dailymotion") << QUrl(QStringLiteral("https://geo.dailymotion.com/player.html?video=x")) << true;
	QTest::newRow("spotify") << QStringLiteral("spotify") << QUrl(QStringLiteral("https://open.spotify.com/embed/track/12345678")) << true;
	QTest::newRow("facebook") << QStringLiteral("facebook") << QUrl(QStringLiteral("https://www.facebook.com/plugins/video.php")) << true;
	QTest::newRow("tiktok") << QStringLiteral("tiktok") << QUrl(QStringLiteral("https://www.tiktok.com/player/v1/1")) << true;
	QTest::newRow("instagram") << QStringLiteral("instagram") << QUrl(QStringLiteral("https://www.instagram.com/p/abc/embed")) << true;
	QTest::newRow("soundcloud") << QStringLiteral("soundcloud") << QUrl(QStringLiteral("https://w.soundcloud.com/player/")) << true;
	QTest::newRow("unknown-provider") << QStringLiteral("direct") << QUrl(QStringLiteral("https://www.youtube.com/embed/a")) << false;
	QTest::newRow("arbitrary-https") << QStringLiteral("youtube") << QUrl(QStringLiteral("https://example.com/embed/a")) << false;
	QTest::newRow("host-spoof") << QStringLiteral("youtube") << QUrl(QStringLiteral("https://www.youtube.com.evil.test/embed/a")) << false;
	QTest::newRow("provider-mismatch") << QStringLiteral("vimeo") << QUrl(QStringLiteral("https://www.youtube.com/embed/a")) << false;
}

void TestQmlClientModels::mediaSessionProviderAllowlist() {
	QFETCH(QString, provider);
	QFETCH(QUrl, url);
	QFETCH(bool, allowed);
	MediaSessionBackend media;
	QCOMPARE(media.open(url, provider, QStringLiteral("test")), allowed);
	QCOMPARE(media.active(), allowed);
	if (!allowed) {
		QCOMPARE(media.state(), QStringLiteral("error"));
		QVERIFY(!media.error().isEmpty());
	}
}

void TestQmlClientModels::mediaSessionNavigationAndErrorLifecycle() {
	MediaSessionBackend media;
	QVERIFY(media.isNavigationAllowed(QUrl(QStringLiteral("about:blank"))));
	QVERIFY(media.open(QUrl(QStringLiteral("https://player.vimeo.com/video/1")), QStringLiteral("vimeo"),
					   QStringLiteral("room")));
	QVERIFY(media.isNavigationAllowed(QUrl(QStringLiteral("https://player.vimeo.com/video/2"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://vimeo.com/2"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://player.vimeo.com.evil.test/video/2"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("file:///tmp/video"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("about:srcdoc"))));
	media.reportError(QStringLiteral("renderer crashed"));
	QCOMPARE(media.state(), QStringLiteral("error"));
	QCOMPARE(media.error(), QStringLiteral("renderer crashed"));
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
	QVERIFY(media.error().isEmpty());
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
