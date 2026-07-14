// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"
#include "ChatPerfTrace.h"
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <limits>

Q_DECLARE_METATYPE(PttSafetyReason)

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void largeSynchronizationsStayResetFree();
	void roomAndParticipantStatesStayIncremental();
	void navigationRailFlattensRoomsAndParticipantsIncrementally();
	void roomRowsExposeActionsOnlySource();
	void participantRowsPreserveTypedVoiceStateAndListenerIdentity();
	void directMessageHistoryMergePublishesOnce();
	void stableIdsRemainIndependentFromSourceMaps();
	void messageRolesExposeStructuredState();
	void chatTimelineAppliesDirectIncrementalMessages();
	void chatTimelineTracksUserHistory();
	void chatTimelineReplacesDisjointScopesWithoutMixedRows();
	void chatTimelinePreservesTypedAttachments();
	void chatTimelineBoundsReactionDelegates();
	void chatTimelineSanitizesStructuredRichText();
	void chatTimelineDrainsBoundedRichTextBacklog();
	void chatTimelineNormalizesPreviewAndAttachments();
	void chatTimelineNormalizesProviderMetadata();
	void participantPresenceUpdatesOnlyTypedRoles();
	void participantUpsertsAndRemovalsStayResetFree();
	void workloadSourceStateRoundTripsExactly();
	void steadyStateRejectsFullBootstrap();
	void pendingBootstrapCompletesBeforeSteadyState();
	void duplicateStableIdsAreCoalesced();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void sessionParsesManagedMotdImagesAndTracksSourceIdentity();
	void sessionAppliesTypedConnectionState();
	void sessionDerivesTypedMotdState();
	void sessionPublishesTypedUpdateBanner();
	void sessionPublishesTypedStonksState();
	void sessionPublishesSemanticMenus();
	void commandsRouteTypedAppActions();
	void commandsRejectEmptyStableIds();
	void commandsValidateStableMoveIds();
	void commandsBatchPreviewHydration();
	void commandsRequestScopeActionsLazily();
	void pttStateIsIdempotentAndReleases();
	void pttSafetyTriggersReleaseExactlyOnce_data();
	void pttSafetyTriggersReleaseExactlyOnce();
	void dialogStateRoutesTypedRequests();
	void dialogPresentationValuesStayBoundedAndTransient();
	void imageViewerStateRemainsStructured();
	void stonksStateAndActionsRemainStructured();
	void asyncOperationsExposeProgressAndCancellation();
	void asyncOperationsClampProgressAndInterruptByPrefix();
	void asyncOperationsExposeStructuredPluginResults();
	void asyncOperationItemResultsAreLosslessAndPaginated();
	void mediaSessionLocalPlaybackControlsRemainTyped();
	void mediaSessionSwitchesBetweenInlineAndDetachedPresentation();
	void mediaSessionValidatesAndPublishesTypedState();
	void mediaSessionValidatesDirectMedia();
	void mediaSessionProviderAllowlist_data();
	void mediaSessionProviderAllowlist();
	void mediaSessionNavigationAndErrorLifecycle();
	void mediaSessionSharedHostLifecycle();
	void mediaSessionRequiresExplicitJoinForRemoteSessions();
	void mediaSessionRejectsConflictingPlaybackDuringSharedSession();
	void selectionStateOnlyNotifiesForRealChanges();
	void selectionStateAcceptsAuthoritativeSelectionBeforeModelSync();
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
	NavigationRailModel navigation;
	ParticipantModel participants;
	ChatTimelineModel chat;
	AsyncOperationModel operations;
	DialogStateController dialog;
	const QList< QObject * > guarded {
		&session, &scope, &rooms, &navigation, &participants, &chat, &operations, &dialog
	};
	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::OverrideProperty, true);

	session.setServerName(QStringLiteral("live"));
	scope.setLabel(QStringLiteral("live"));
	rooms.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("live") } } }, {});
	navigation.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("live") } } }, {});
	participants.replaceParticipantStates({ QVariantMap { { QStringLiteral("session"), QStringLiteral("1") } } });
	chat.replaceMessages({ QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("live") } } });
	operations.startOperation(QStringLiteral("live"), QStringLiteral("live"), {}, false);
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("live") } });
	QCOMPARE(session.serverName(), QStringLiteral("Mumble"));
	QVERIFY(scope.label().isEmpty());
	QCOMPARE(rooms.rowCount(), 0);
	QCOMPARE(navigation.rowCount(), 0);
	QCOMPARE(participants.rowCount(), 0);
	QCOMPARE(chat.rowCount(), 0);
	QCOMPARE(operations.rowCount(), 0);
	QVERIFY(!dialog.open());

	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::WriteProperty, true);
	session.setServerName(QStringLiteral("fixture"));
	scope.setLabel(QStringLiteral("fixture"));
	rooms.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("fixture") },
												{ QStringLiteral("label"), QStringLiteral("Fixture") } } }, {});
	navigation.replaceRoomStates({ QVariantMap { { QStringLiteral("token"), QStringLiteral("fixture") },
											 { QStringLiteral("label"), QStringLiteral("Fixture") },
											 { QStringLiteral("participants"),
											   QVariantList { QVariantMap {
												   { QStringLiteral("session"), QStringLiteral("2") },
												   { QStringLiteral("name"), QStringLiteral("Fixture") },
												   { QStringLiteral("talkState"), QStringLiteral("passive") } } } } } }, {});
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
	QCOMPARE(navigation.rowCount(), 2);
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(chat.rowCount(), 2);
	QVERIFY(chat.hasUserHistory());
	QCOMPARE(operations.rowCount(), 1);
	QVERIFY(dialog.open());
	QCOMPARE(dialog.dialogId(), QStringLiteral("fixture"));

	for (QObject *object : guarded) object->setProperty(QmlVisualFixtureMutation::WriteProperty, false);
	session.setServerName(QStringLiteral("clobber"));
	rooms.clear();
	navigation.clear();
	navigation.updatePresence(QStringLiteral("2"), QStringLiteral("talking"), QStringLiteral("Talking"),
							  QStringLiteral("success"), true, false, {}, {});
	navigation.removeParticipant(QStringLiteral("2"));
	QVERIFY(!chat.removeMessage(QStringLiteral("fixture")));
	QCOMPARE(session.serverName(), QStringLiteral("fixture"));
	QCOMPARE(rooms.rowCount(), 1);
	QCOMPARE(navigation.rowCount(), 2);
	QCOMPARE(navigation.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("passive"));
	QCOMPARE(chat.rowCount(), 2);
	QVERIFY(chat.hasUserHistory());
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
	QSignalSpy screenShareSpy(&scope, &ActiveScopeController::screenShareChanged);
	const QVariantMap screenShare { { QStringLiteral("mode"), QStringLiteral("available") },
									 { QStringLiteral("streamId"), QStringLiteral("stream-42") } };
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
					   { QStringLiteral("loadingState"), QStringLiteral("older") },
					   { QStringLiteral("screenShare"), screenShare } });
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
	QCOMPARE(scope.screenShare(), screenShare);
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
	QCOMPARE(loadingSpy.count(), 1);
	QCOMPARE(screenShareSpy.count(), 1);

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
					   { QStringLiteral("loadingState"), QStringLiteral("older") },
					   { QStringLiteral("screenShare"), screenShare } });
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
	QCOMPARE(loadingSpy.count(), 1);
	QCOMPARE(screenShareSpy.count(), 1);
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

	QVariantList prepended;
	prepended.reserve(128 + messages.size());
	for (int row = 0; row < 128; ++row) {
		prepended.push_back(QVariantMap {
			{ QStringLiteral("messageKey"), QStringLiteral("older:%1").arg(row) },
			{ QStringLiteral("plainText"), QStringLiteral("Older %1").arg(row) }
		});
	}
	prepended.append(messages);
	insertSpy.clear();
	model.replaceMessages(prepended);
	QCOMPARE(model.rowCount(), 10128);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(insertSpy.constFirst().at(1).toInt(), 0);
	QCOMPARE(insertSpy.constFirst().at(2).toInt(), 127);
	QCOMPARE(model.rowForStableId(QStringLiteral("message:0")), 128);
	QCOMPARE(model.rowForStableId(QStringLiteral("older:127")), 127);
	QCOMPARE(resetSpy.count(), 0);

	QVariantMap changed = prepended.at(5128).toMap();
	changed.insert(QStringLiteral("plainText"), QStringLiteral("Updated body"));
	prepended[5128] = changed;
	model.replaceMessages(prepended);
	QCOMPARE(changedSpy.count(), 1);
	const QList< int > roles = changedSpy.takeFirst().at(2).value< QList< int > >();
	QVERIFY(roles.contains(StableListModel::PayloadRole));
	QVERIFY(roles.contains(StableListModel::SubtitleRole));
	QCOMPARE(resetSpy.count(), 0);

	QVariantList replacement;
	replacement.reserve(10000);
	for (int row = 0; row < 10000; ++row) {
		replacement.push_back(QVariantMap {
			{ QStringLiteral("messageKey"), QStringLiteral("other-scope:%1").arg(row) },
			{ QStringLiteral("plainText"), QStringLiteral("Other body %1").arg(row) }
		});
	}
	insertSpy.clear();
	QSignalSpy replacementRemoveSpy(&model, &QAbstractItemModel::rowsRemoved);
	model.replaceMessages(replacement);
	QCOMPARE(model.rowCount(), 10000);
	QCOMPARE(replacementRemoveSpy.count(), 1);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(insertSpy.constFirst().at(1).toInt(), 0);
	QCOMPARE(insertSpy.constFirst().at(2).toInt(), 9999);
	QCOMPARE(model.rowForStableId(QStringLiteral("other-scope:9999")), 9999);
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
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("-2:7") },
						{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
						{ QStringLiteral("subtitle"), QStringLiteral("Direct message") },
						{ QStringLiteral("unreadCount"), 2 } } });
	QCOMPARE(rooms.rowCount(), 3);
	QCOMPARE(rooms.get(2).value(QStringLiteral("id")).toString(), QStringLiteral("direct:-2:7"));
	QCOMPARE(rooms.get(2).value(QStringLiteral("kind")).toString(), QStringLiteral("direct"));
	QCOMPARE(roomInsertSpy.count(), 2);
	QCOMPARE(roomResetSpy.count(), 0);
	rooms.replaceDirectMessageStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("-2:7") },
						{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
						{ QStringLiteral("subtitle"), QStringLiteral("Direct message") },
						{ QStringLiteral("unreadCount"), 0 }, { QStringLiteral("open"), true } } });
	QCOMPARE(rooms.rowCount(), 3);
	QVERIFY(rooms.get(2).value(QStringLiteral("selected")).toBool());
	QCOMPARE(roomResetSpy.count(), 0);

	RoomModel railRooms;
	railRooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby chat") },
						{ QStringLiteral("selected"), true } } });
	railRooms.selectScopeFromRail(QStringLiteral(" channel:1 "), QStringLiteral(" Voice "));
	QVERIFY(railRooms.get(0).value(QStringLiteral("selected")).toBool());
	QVERIFY(!railRooms.get(1).value(QStringLiteral("selected")).toBool());
	railRooms.selectScopeFromRail(QStringLiteral("channel:1"), QStringLiteral("text"));
	QVERIFY(!railRooms.get(0).value(QStringLiteral("selected")).toBool());
	QVERIFY(railRooms.get(1).value(QStringLiteral("selected")).toBool());

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

void TestQmlClientModels::roomRowsExposeActionsOnlySource() {
	RoomModel rooms;
	const QVariantList actions { QVariantMap { { QStringLiteral("id"), QStringLiteral("join") },
													 { QStringLiteral("label"), QStringLiteral("Join") } } };
	const QVariantList badges { QStringLiteral("Pinned") };
	const QVariantMap screenShare { { QStringLiteral("visible"), true },
		{ QStringLiteral("mode"), QStringLiteral("publishing") },
		{ QStringLiteral("badgeLabel"), QStringLiteral("Live") },
		{ QStringLiteral("primaryActionId"), QStringLiteral("screenShareOpenWindow") } };
	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:7") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("pathLabel"), QStringLiteral("Root / Lobby") },
						{ QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
						{ QStringLiteral("joined"), false }, { QStringLiteral("canJoin"), true },
						{ QStringLiteral("badges"), badges },
						{ QStringLiteral("actions"), actions },
						{ QStringLiteral("participants"), QVariantList { QVariantMap {
							{ QStringLiteral("session"), 42 },
							{ QStringLiteral("avatarUrl"), QStringLiteral("image://mumble/avatar/42") } } } },
						{ QStringLiteral("screenShare"), screenShare },
						{ QStringLiteral("messages"), QVariantList { QStringLiteral("must-not-escape") } },
						{ QStringLiteral("windows"), QVariantList { QStringLiteral("must-not-escape") } } } },
		{});

	QCOMPARE(rooms.rowCount(), 1);
	const QVariantMap source = rooms.get(0).value(QStringLiteral("source")).toMap();
	QCOMPARE(source.size(), 1);
	QCOMPARE(source.value(QStringLiteral("actions")).toList(), actions);
	const QVariantMap row = rooms.get(0);
	QVERIFY(row.value(QStringLiteral("canJoin")).toBool());
	QVERIFY(!row.value(QStringLiteral("joined")).toBool());
	QCOMPARE(row.value(QStringLiteral("actions")).toList(), actions);
	QCOMPARE(row.value(QStringLiteral("badges")).toList(), badges);
	QCOMPARE(row.value(QStringLiteral("screenShare")).toMap(), screenShare);
	QCOMPARE(row.value(QStringLiteral("pathLabel")).toString(), QStringLiteral("Root / Lobby"));
	QCOMPARE(row.value(QStringLiteral("kindLabel")).toString(), QStringLiteral("Voice room"));
	QVERIFY(!source.contains(QStringLiteral("participants")));
	QVERIFY(!source.contains(QStringLiteral("screenShare")));
	QVERIFY(!source.contains(QStringLiteral("messages")));
	QVERIFY(!source.contains(QStringLiteral("windows")));

	rooms.replaceDirectMessageStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("-2:42") },
						{ QStringLiteral("label"), QStringLiteral("Alice") },
						{ QStringLiteral("messages"), QVariantList { QStringLiteral("hidden") } },
						{ QStringLiteral("windows"), QVariantList { QStringLiteral("hidden") } } } });
	const QVariantMap directSource = rooms.get(1).value(QStringLiteral("source")).toMap();
	QVERIFY(directSource.isEmpty());
}

void TestQmlClientModels::navigationRailFlattensRoomsAndParticipantsIncrementally() {
	NavigationRailModel navigation;
	QSignalSpy resetSpy(&navigation, &QAbstractItemModel::modelReset);
	QSignalSpy changedSpy(&navigation, &QAbstractItemModel::dataChanged);
	const QVariantMap listenerStatus { { QStringLiteral("kind"), QStringLiteral("listener") },
		{ QStringLiteral("label"), QStringLiteral("Listener") },
		{ QStringLiteral("tone"), QStringLiteral("accent") } };
	navigation.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("participants"), QVariantList {
							QVariantMap { { QStringLiteral("session"), 42 },
								{ QStringLiteral("participantKey"), QStringLiteral("user:42") },
								{ QStringLiteral("label"), QStringLiteral("Alice") },
								{ QStringLiteral("talkState"), QStringLiteral("passive") } },
							QVariantMap { { QStringLiteral("session"), 77 },
								{ QStringLiteral("participantKey"), QStringLiteral("listener:1:77") },
								{ QStringLiteral("entryKind"), QStringLiteral("listener") },
								{ QStringLiteral("label"), QStringLiteral("Bob") },
								{ QStringLiteral("badges"), QVariantList { QStringLiteral("Listener") } },
								{ QStringLiteral("statuses"), QVariantList { listenerStatus } } }
						} } },
		  QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:2") },
						{ QStringLiteral("label"), QStringLiteral("Games") },
						{ QStringLiteral("participants"), QVariantList { QVariantMap {
							{ QStringLiteral("session"), 7 },
							{ QStringLiteral("participantKey"), QStringLiteral("user:7") },
							{ QStringLiteral("label"), QStringLiteral("Carol") } } } } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("-2:0") },
						{ QStringLiteral("label"), QStringLiteral("Activity") } } });

	QCOMPARE(navigation.rowCount(), 6);
	QCOMPARE(navigation.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("voice:channel:1"));
	QCOMPARE(navigation.get(1).value(QStringLiteral("id")).toString(), QStringLiteral("user:42"));
	QCOMPARE(navigation.get(1).value(QStringLiteral("parentScopeToken")).toString(),
			 QStringLiteral("channel:1"));
	QCOMPARE(navigation.get(2).value(QStringLiteral("id")).toString(), QStringLiteral("listener:1:77"));
	QCOMPARE(navigation.get(3).value(QStringLiteral("id")).toString(), QStringLiteral("voice:channel:2"));
	QCOMPARE(navigation.get(4).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));
	QCOMPARE(navigation.get(5).value(QStringLiteral("sectionKind")).toString(), QStringLiteral("text"));
	QCOMPARE(resetSpy.count(), 0);

	changedSpy.clear();
	navigation.updatePresence(QStringLiteral("77"), QStringLiteral("talking"), QStringLiteral("Talking"),
		QStringLiteral("speaking"), true, false, { QStringLiteral("Talking") },
		{ QVariantMap { { QStringLiteral("kind"), QStringLiteral("talking") },
						{ QStringLiteral("label"), QStringLiteral("Talking") } } });
	const QVariantMap listener = navigation.get(2);
	QVERIFY(listener.value(QStringLiteral("talking")).toBool());
	QVERIFY(listener.value(QStringLiteral("badges")).toList().contains(QStringLiteral("Listener")));
	QCOMPARE(listener.value(QStringLiteral("statuses")).toList().constFirst().toMap()
			 .value(QStringLiteral("kind")).toString(), QStringLiteral("listener"));
	QVERIFY(changedSpy.count() > 0);
	QCOMPARE(resetSpy.count(), 0);

	navigation.selectScopeFromRail(QStringLiteral("channel:2"), QStringLiteral("voice"));
	QVERIFY(!navigation.get(0).value(QStringLiteral("selected")).toBool());
	QVERIFY(navigation.get(2).value(QStringLiteral("talking")).toBool());
	QCOMPARE(navigation.get(2).value(QStringLiteral("talkLabel")).toString(), QStringLiteral("Talking"));
	QVERIFY(navigation.get(3).value(QStringLiteral("selected")).toBool());
	navigation.removeParticipant(QStringLiteral("42"));
	QCOMPARE(navigation.rowForStableId(QStringLiteral("user:42")), -1);
	QCOMPARE(navigation.rowCount(), 5);
	navigation.selectScopeFromRail(QStringLiteral("channel:1"), QStringLiteral("voice"));
	QCOMPARE(navigation.rowForStableId(QStringLiteral("user:42")), -1);
	QCOMPARE(navigation.rowCount(), 5);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::participantRowsPreserveTypedVoiceStateAndListenerIdentity() {
	ParticipantModel participants;
	const QVariantList actions { QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
		{ QStringLiteral("id"), QStringLiteral("user.info") },
		{ QStringLiteral("label"), QStringLiteral("User information") } } };
	const QVariantList statuses {
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("selfMuted") },
			{ QStringLiteral("label"), QStringLiteral("Muted") },
			{ QStringLiteral("tone"), QStringLiteral("danger") } },
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("selfDeafened") },
			{ QStringLiteral("label"), QStringLiteral("Deafened") },
			{ QStringLiteral("tone"), QStringLiteral("danger") } }
	};
	const QVariantMap localVolume { { QStringLiteral("db"), -6 }, { QStringLiteral("factor"), 0.5 },
		{ QStringLiteral("compactLabel"), QStringLiteral("-6") }, { QStringLiteral("visible"), true } };
	participants.replaceParticipantStates({
		QVariantMap { { QStringLiteral("participantKey"), QStringLiteral("user:7") },
			{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
			{ QStringLiteral("entryKind"), QStringLiteral("user") },
			{ QStringLiteral("talkState"), QStringLiteral("passive") },
			{ QStringLiteral("badges"), QVariantList { QStringLiteral("You") } },
			{ QStringLiteral("statuses"), statuses }, { QStringLiteral("localVolume"), localVolume },
			{ QStringLiteral("canMessage"), true }, { QStringLiteral("canJoin"), true },
			{ QStringLiteral("actions"), actions } },
		QVariantMap { { QStringLiteral("participantKey"), QStringLiteral("listener:1:7") },
			{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
			{ QStringLiteral("entryKind"), QStringLiteral("listener") },
			{ QStringLiteral("scopeToken"), QStringLiteral("channel:1") },
			{ QStringLiteral("talkState"), QStringLiteral("passive") },
			{ QStringLiteral("badges"), QVariantList { QStringLiteral("Listener") } },
			{ QStringLiteral("statuses"), QVariantList { QVariantMap {
				{ QStringLiteral("kind"), QStringLiteral("listener") },
				{ QStringLiteral("label"), QStringLiteral("Listener") } } } },
			{ QStringLiteral("localVolume"), localVolume }, { QStringLiteral("actions"), actions } },
		QVariantMap { { QStringLiteral("participantKey"), QStringLiteral("listener:2:7") },
			{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
			{ QStringLiteral("entryKind"), QStringLiteral("listener") },
			{ QStringLiteral("scopeToken"), QStringLiteral("channel:2") },
			{ QStringLiteral("talkState"), QStringLiteral("passive") } }
	});

	QCOMPARE(participants.rowCount(), 3);
	QCOMPARE(participants.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));
	QCOMPARE(participants.get(1).value(QStringLiteral("id")).toString(), QStringLiteral("listener:1:7"));
	QCOMPARE(participants.get(2).value(QStringLiteral("id")).toString(), QStringLiteral("listener:2:7"));
	for (int rowIndex = 0; rowIndex < participants.rowCount(); ++rowIndex)
		QCOMPARE(participants.get(rowIndex).value(QStringLiteral("participantSession")).toString(), QStringLiteral("7"));
	const QVariantMap user = participants.get(0);
	QCOMPARE(user.value(QStringLiteral("badges")).toList(), QVariantList { QStringLiteral("You") });
	QCOMPARE(user.value(QStringLiteral("statuses")).toList(), statuses);
	QCOMPARE(user.value(QStringLiteral("localVolume")).toMap(), localVolume);
	QCOMPARE(user.value(QStringLiteral("actions")).toList(), actions);
	QVERIFY(user.value(QStringLiteral("muted")).toBool());
	QVERIFY(user.value(QStringLiteral("deafened")).toBool());
	QVERIFY(participants.get(1).value(QStringLiteral("listener")).toBool());

	QmlSelectionState selection;
	selection.bindModels(nullptr, &participants);
	selection.setSelectedUserSession(7);
	QCOMPARE(selection.selectedUserSession().toULongLong(), 7ULL);

	QSignalSpy changedSpy(&participants, &QAbstractItemModel::dataChanged);
	participants.updatePresence(QStringLiteral("7"), QStringLiteral("talking"), QStringLiteral("Talking"),
		QStringLiteral("speaking"), true, false, { QStringLiteral("Talking") }, {});
	QCOMPARE(changedSpy.count(), 3);
	for (int rowIndex = 0; rowIndex < participants.rowCount(); ++rowIndex)
		QCOMPARE(participants.get(rowIndex).value(QStringLiteral("status")).toString(), QStringLiteral("talking"));

	QSignalSpy removedSpy(&participants, &QAbstractItemModel::rowsRemoved);
	participants.removeParticipant(QStringLiteral("7"));
	QCOMPARE(participants.rowCount(), 0);
	QCOMPARE(removedSpy.count(), 3);
	QVERIFY(!selection.selectedUserSession().isValid());
}

void TestQmlClientModels::directMessageHistoryMergePublishesOnce() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());

	const qsizetype mergeStart = source.indexOf(QStringLiteral("bool MainWindow::mergeModernDirectMessageHistory"));
	const qsizetype mergeEnd = source.indexOf(QStringLiteral("bool MainWindow::setModernDirectMessageMode"), mergeStart);
	QVERIFY(mergeStart >= 0);
	QVERIFY(mergeEnd > mergeStart);
	const QString mergeBody = source.mid(mergeStart, mergeEnd - mergeStart);
	QCOMPARE(mergeBody.count(QStringLiteral("publishQmlDirectMessagesState();")), 1);
	QVERIFY(QRegularExpression(QStringLiteral(
		R"(appendModernPersistentDirectMessage\s*\(\s*response\.messages\s*\(\s*i\s*\)\s*,\s*false\s*,\s*false\s*\))"))
			.match(mergeBody).hasMatch());

	const qsizetype summariesStart = source.indexOf(QStringLiteral("QVariantMap MainWindow::buildModernShellDirectMessagesState"));
	const qsizetype summariesEnd = source.indexOf(QStringLiteral("bool MainWindow::openModernDirectMessage"), summariesStart);
	QVERIFY(summariesStart >= 0);
	QVERIFY(summariesEnd > summariesStart);
	const QString summariesBody = source.mid(summariesStart, summariesEnd - summariesStart);
	QVERIFY(!summariesBody.contains(QStringLiteral("QStringLiteral(\"windows\")")));
	QVERIFY(QRegularExpression(QStringLiteral(
		R"(buildModernShellDirectMessageConversationState\s*\(\s*conversation\s*,\s*false\s*\))"))
			.match(summariesBody).hasMatch());
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

void TestQmlClientModels::chatTimelineTracksUserHistory() {
	ChatTimelineModel model;
	QSignalSpy historySpy(&model, &ChatTimelineModel::hasUserHistoryChanged);
	QVERIFY(!model.hasUserHistory());

	model.upsertMessage({ { QStringLiteral("messageId"), QStringLiteral("100") },
						  { QStringLiteral("bodyText"), QStringLiteral("Connected") },
						  { QStringLiteral("system"), true } });
	QVERIFY(!model.hasUserHistory());
	QCOMPARE(historySpy.count(), 0);

	model.upsertMessage({ { QStringLiteral("messageId"), QStringLiteral("101") },
						  { QStringLiteral("bodyText"), QStringLiteral("Hello") } });
	QVERIFY(model.hasUserHistory());
	QCOMPARE(historySpy.count(), 1);

	model.upsertMessage({ { QStringLiteral("messageId"), QStringLiteral("101") },
						  { QStringLiteral("deleted"), true } });
	QVERIFY(!model.hasUserHistory());
	QCOMPARE(historySpy.count(), 2);

	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageId"), QStringLiteral("102") },
					  { QStringLiteral("system"), true } },
		QVariantMap { { QStringLiteral("messageId"), QStringLiteral("103") },
					  { QStringLiteral("bodyText"), QStringLiteral("History") } }
	});
	QVERIFY(model.hasUserHistory());
	QCOMPARE(historySpy.count(), 3);

	model.appendMessages({ QVariantMap { { QStringLiteral("messageId"), QStringLiteral("104") },
										 { QStringLiteral("bodyText"), QStringLiteral("More") } } });
	QVERIFY(model.removeMessage(QStringLiteral("103")));
	QVERIFY(model.hasUserHistory());
	QCOMPARE(historySpy.count(), 3);
	QVERIFY(model.removeMessage(QStringLiteral("104")));
	QVERIFY(!model.hasUserHistory());
	QCOMPARE(historySpy.count(), 4);

	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageId"), QStringLiteral("105") },
					  { QStringLiteral("bodyText"), QStringLiteral("Superseded") } },
		QVariantMap { { QStringLiteral("messageId"), QStringLiteral("105") },
					  { QStringLiteral("system"), true } }
	});
	QCOMPARE(model.rowCount(), 1);
	QVERIFY(!model.hasUserHistory());
	QCOMPARE(historySpy.count(), 4);
}

void TestQmlClientModels::chatTimelineReplacesDisjointScopesWithoutMixedRows() {
	ChatTimelineModel model;
	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:1") },
					  { QStringLiteral("actor"), QStringLiteral("Kira Mockup") },
					  { QStringLiteral("bodyText"), QStringLiteral("First direct message") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:2") },
					  { QStringLiteral("actor"), QStringLiteral("You") },
					  { QStringLiteral("bodyText"), QStringLiteral("Direct reply") },
					  { QStringLiteral("own"), true } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:3") },
					  { QStringLiteral("actor"), QStringLiteral("Kira Mockup") },
					  { QStringLiteral("bodyText"), QStringLiteral("Last direct message") } }
	});
	QCOMPARE(model.rowCount(), 3);
	QVERIFY(!model.get(0).value(QStringLiteral("own")).toBool());
	QVERIFY(model.get(1).value(QStringLiteral("own")).toBool());
	QVERIFY(!model.get(2).value(QStringLiteral("own")).toBool());

	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

	const QVariantList deliveryMessages {
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("delivery:1") },
					  { QStringLiteral("actor"), QStringLiteral("You") },
					  { QStringLiteral("bodyText"), QStringLiteral("Delivered") },
					  { QStringLiteral("own"), true } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("delivery:2") },
					  { QStringLiteral("actor"), QStringLiteral("You") },
					  { QStringLiteral("bodyText"), QStringLiteral("Uploading") },
					  { QStringLiteral("deliveryState"), QStringLiteral("sending") },
					  { QStringLiteral("deliveryLabel"), QStringLiteral("Sending...") },
					  { QStringLiteral("own"), true } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("delivery:3") },
					  { QStringLiteral("actor"), QStringLiteral("You") },
					  { QStringLiteral("bodyText"), QStringLiteral("Network error") },
					  { QStringLiteral("deliveryState"), QStringLiteral("failed") },
					  { QStringLiteral("deliveryLabel"), QStringLiteral("Not delivered") },
					  { QStringLiteral("deliveryCanRetry"), true },
					  { QStringLiteral("own"), true } }
	};
	model.replaceMessages(deliveryMessages);

	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(moveSpy.count(), 0);
	QCOMPARE(changedSpy.count(), 0);
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(removeSpy.first().at(1).toInt(), 0);
	QCOMPARE(removeSpy.first().at(2).toInt(), 2);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(insertSpy.first().at(1).toInt(), 0);
	QCOMPARE(insertSpy.first().at(2).toInt(), 2);
	QCOMPARE(model.rowCount(), 3);

	const QStringList expectedIds { QStringLiteral("delivery:1"), QStringLiteral("delivery:2"),
									QStringLiteral("delivery:3") };
	for (int row = 0; row < model.rowCount(); ++row) {
		const QVariantMap state = model.get(row);
		QCOMPARE(state.value(QStringLiteral("id")).toString(), expectedIds.at(row));
		QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("You"));
		QVERIFY(!state.value(QStringLiteral("subtitle")).toString().contains(QStringLiteral("direct"),
														 Qt::CaseInsensitive));
	}
	QCOMPARE(model.get(1).value(QStringLiteral("source")).toMap()
			 .value(QStringLiteral("deliveryLabel")).toString(), QStringLiteral("Sending..."));
	QVERIFY(model.get(2).value(QStringLiteral("source")).toMap()
				.value(QStringLiteral("deliveryCanRetry")).toBool());
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

void TestQmlClientModels::chatTimelineBoundsReactionDelegates() {
	ChatTimelineModel model;
	QVariantList reactions;
	for (int reactionIndex = 0; reactionIndex < 80; ++reactionIndex) {
		QVariantList actorNames;
		for (int actorIndex = 0; actorIndex < 80; ++actorIndex) {
			actorNames.push_back(QStringLiteral("actor-%1-%2").arg(reactionIndex).arg(actorIndex));
		}
		reactions.push_back(QVariantMap {
			{ QStringLiteral("emoji"), QStringLiteral("reaction-%1").arg(reactionIndex) },
			{ QStringLiteral("count"), std::numeric_limits< qulonglong >::max() },
			{ QStringLiteral("selfReacted"), reactionIndex == 0 },
			{ QStringLiteral("actorNames"), actorNames }
		});
	}
	// A duplicate aggregate must not create a second delegate.
	reactions.prepend(reactions.first());

	QVERIFY(model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("reactions:1") },
								  { QStringLiteral("reactions"), reactions } }));
	const QVariantList normalized = model.get(0).value(QStringLiteral("reactions")).toList();
	QCOMPARE(normalized.size(), 32);
	QCOMPARE(normalized.first().toMap().value(QStringLiteral("actorNames")).toList().size(), 32);
	QCOMPARE(normalized.first().toMap().value(QStringLiteral("count")).toULongLong(),
			 static_cast< qulonglong >(std::numeric_limits< unsigned int >::max()));
	QVERIFY(normalized.first().toMap().value(QStringLiteral("selfReacted")).toBool());
}

void TestQmlClientModels::chatTimelineSanitizesStructuredRichText() {
	ChatTimelineModel model;
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("rich:1") },
		  { QStringLiteral("bodyText"), QStringLiteral("Hello safe bad good") },
		  { QStringLiteral("bodyHtml"),
			QStringLiteral("<p>Hello <strong>safe</strong> <a href='javascript:alert(1)'>bad</a> "
						   "<a href='https://example.com/a?q=1'>good</a><img src='file:///secret' "
						   "onerror='alert(1)'><script>alert(2)</script></p>") } }));

	// Rich HTML parsing is deliberately kept off the UI thread. The synchronous
	// insertion exposes safe plain text until the bounded worker result arrives.
	QCOMPARE(model.get(0).value(QStringLiteral("bodySegments")).toList().size(), 1);
	const auto hasParsedFormatting = [&model] {
		const QVariantList current = model.get(0).value(QStringLiteral("bodySegments")).toList();
		for (const QVariant &entry : current) {
			const QVariantMap segment = entry.toMap();
			if (segment.value(QStringLiteral("bold")).toBool()
				|| !segment.value(QStringLiteral("href")).toString().isEmpty()) {
				return true;
			}
		}
		return false;
	};
	QTRY_VERIFY_WITH_TIMEOUT(hasParsedFormatting(), 5000);
	QVERIFY(!changedSpy.isEmpty());
	const QVariantList segments = model.get(0).value(QStringLiteral("bodySegments")).toList();
	QVERIFY(!segments.isEmpty());
	QString renderedText;
	bool foundBold = false;
	bool foundGoodLink = false;
	for (const QVariant &entry : segments) {
		const QVariantMap segment = entry.toMap();
		const QString text = segment.value(QStringLiteral("text")).toString();
		renderedText += text;
		QVERIFY(!text.contains(QLatin1Char('<')));
		QVERIFY(!text.contains(QChar::ObjectReplacementCharacter));
		const QString href = segment.value(QStringLiteral("href")).toString();
		QVERIFY(!href.startsWith(QLatin1String("javascript:"), Qt::CaseInsensitive));
		QVERIFY(!href.startsWith(QLatin1String("file:"), Qt::CaseInsensitive));
		if (text.contains(QLatin1String("safe"))) foundBold = segment.value(QStringLiteral("bold")).toBool();
		if (text.contains(QLatin1String("good"))) {
			foundGoodLink = href == QLatin1String("https://example.com/a?q=1");
		}
	}
	QVERIFY(renderedText.contains(QLatin1String("Hello")));
	QVERIFY(renderedText.contains(QLatin1String("bad")));
	QVERIFY(!renderedText.contains(QLatin1String("alert(2)")));
	QVERIFY(foundBold);
	QVERIFY(foundGoodLink);
	QCOMPARE(model.roleNames().value(StableListModel::BodySegmentsRole), QByteArray("bodySegments"));
}

void TestQmlClientModels::chatTimelineDrainsBoundedRichTextBacklog() {
	ChatTimelineModel model;
	QVariantList messages;
	messages.reserve(540);
	for (int index = 0; index < 540; ++index) {
		messages.push_back(QVariantMap {
			{ QStringLiteral("messageKey"), QStringLiteral("rich-backlog:%1").arg(index) },
			{ QStringLiteral("bodyText"), QStringLiteral("Rich %1").arg(index) },
			{ QStringLiteral("bodyHtml"), QStringLiteral("<b>Rich %1</b>").arg(index) }
		});
	}
	model.replaceMessages(messages);
	QCOMPARE(model.rowCount(), 540);
	const auto fullyHydrated = [&model] {
		for (int row = 0; row < model.rowCount(); ++row) {
			const QVariantList segments = model.get(row).value(QStringLiteral("bodySegments")).toList();
			bool bold = false;
			for (const QVariant &segment : segments) bold = bold || segment.toMap().value(QStringLiteral("bold")).toBool();
			if (!bold) return false;
		}
		return true;
	};
	QTRY_VERIFY_WITH_TIMEOUT(fullyHydrated(), 20000);
}

void TestQmlClientModels::chatTimelineNormalizesPreviewAndAttachments() {
	ChatTimelineModel model;
	const QVariantMap validAttachment {
		{ QStringLiteral("id"), QStringLiteral("asset:1") },
		{ QStringLiteral("kind"), QStringLiteral("image") },
		{ QStringLiteral("url"), QStringLiteral("image://mumble/asset:1?g=1") },
		{ QStringLiteral("thumbnailUrl"), QStringLiteral("image://mumble/thumb:1?g=1") },
		{ QStringLiteral("alt"), QStringLiteral("A safe image") }
	};
	const QVariantMap invalidAttachment {
		{ QStringLiteral("id"), QStringLiteral("asset:2") },
		{ QStringLiteral("url"), QStringLiteral("https://cdn.example.com/private-image.png") },
		{ QStringLiteral("thumbnailUrl"), QStringLiteral("data:image/png;base64,AAAA") }
	};
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("previewStub"),
			QVariantMap { { QStringLiteral("url"), QStringLiteral("https://example.com/card") },
						  { QStringLiteral("host"), QStringLiteral("example.com") },
						  { QStringLiteral("loadingLabel"), QStringLiteral("Load preview") } } },
		  { QStringLiteral("attachments"), QVariantList { validAttachment, invalidAttachment } } }));

	QVariantMap row = model.get(0);
	QVariantMap preview = row.value(QStringLiteral("preview")).toMap();
	QCOMPARE(preview.value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
	QVERIFY(preview.value(QStringLiteral("loading")).toBool());
	const QVariantList attachments = row.value(QStringLiteral("attachments")).toList();
	QCOMPARE(attachments.size(), 1);
	QCOMPARE(attachments.first().toMap().value(QStringLiteral("url")).toString(),
			 QStringLiteral("image://mumble/asset:1?g=1"));

	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("preview"),
			QVariantMap { { QStringLiteral("url"), QStringLiteral("javascript:alert(1)") },
						  { QStringLiteral("thumbnailUrl"), QStringLiteral("file:///private/thumb.png") },
						  { QStringLiteral("title"), QStringLiteral("Preview unavailable") },
						  { QStringLiteral("failed"), true },
						  { QStringLiteral("metadata"), QVariantMap { { QStringLiteral("xLikeCount"), 42 } } } } } }));
	row = model.get(0);
	preview = row.value(QStringLiteral("preview")).toMap();
	QCOMPARE(preview.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
	QVERIFY(preview.value(QStringLiteral("failed")).toBool());
	QVERIFY(!preview.contains(QStringLiteral("url")));
	QVERIFY(!preview.contains(QStringLiteral("thumbnailUrl")));
	QCOMPARE(preview.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("xLikeCount")).toInt(), 42);

	QVariantMap oversizedMetadata {
		{ QStringLiteral("xDisplayName"), QString(5000, QLatin1Char('x')) },
		{ QStringLiteral("nested"), QVariantMap { { QStringLiteral("secret"), QStringLiteral("ignored") } } },
		{ QStringLiteral("invalid key"), QStringLiteral("ignored") }
	};
	for (int index = 0; index < 80; ++index) {
		oversizedMetadata.insert(QStringLiteral("field%1").arg(index), index);
	}
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("preview"), QVariantMap { { QStringLiteral("metadata"), oversizedMetadata } } } }));
	const QVariantMap boundedMetadata =
		model.get(0).value(QStringLiteral("preview")).toMap().value(QStringLiteral("metadata")).toMap();
	QCOMPARE(boundedMetadata.size(), 32);
	QVERIFY(!boundedMetadata.contains(QStringLiteral("nested")));
	QVERIFY(!boundedMetadata.contains(QStringLiteral("invalid key")));
	QCOMPARE(boundedMetadata.value(QStringLiteral("xDisplayName")).toString().size(), 4096);

	QVariantList sparkline;
	QVariantList specs;
	QVariantList tags;
	QVariantList replyContext;
	for (int index = 0; index < 90; ++index) {
		sparkline.push_back(QVariantMap { { QStringLiteral("close"), 100.0 + index },
									  { QStringLiteral("timestamp"), 1700000000 + index },
									  { QStringLiteral("private"), QStringLiteral("drop") } });
	}
	sparkline.push_front(std::numeric_limits< double >::quiet_NaN());
	for (int index = 0; index < 12; ++index) {
		specs.push_back(QVariantMap { { QStringLiteral("label"), QStringLiteral("Spec %1").arg(index) },
								  { QStringLiteral("value"), QStringLiteral("Value %1").arg(index) },
								  { QStringLiteral("rawHtml"), QStringLiteral("<b>drop</b>") } });
		tags.push_back(QStringLiteral("tag-%1").arg(index));
	}
	for (int index = 0; index < 5; ++index) {
		replyContext.push_back(QVariantMap { { QStringLiteral("id"), QString::number(index) },
										{ QStringLiteral("displayName"), QStringLiteral("Author %1").arg(index) },
										{ QStringLiteral("text"), QString(1400, QLatin1Char('t')) },
										{ QStringLiteral("url"), QStringLiteral("file:///private") },
										{ QStringLiteral("raw"), QVariantMap { { QStringLiteral("secret"), true } } } });
	}
	const QVariantMap structuredMetadata {
		{ QStringLiteral("provider"), QStringLiteral("fixture") },
		{ QStringLiteral("financeSparkline"), sparkline },
		{ QStringLiteral("productSpecs"), specs },
		{ QStringLiteral("listingSpecs"), specs },
		{ QStringLiteral("vehicleSpecs"), specs },
		{ QStringLiteral("gameStoreTags"), tags },
		{ QStringLiteral("vehicleHighlights"), tags },
		{ QStringLiteral("githubTopics"), tags },
		{ QStringLiteral("xQuotedPost"), replyContext.first() },
		{ QStringLiteral("xReplyContext"), replyContext },
		{ QStringLiteral("unknownList"), tags },
		{ QStringLiteral("unknownMap"), QVariantMap { { QStringLiteral("secret"), true } } }
	};
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("preview"), QVariantMap { { QStringLiteral("metadata"), structuredMetadata } } } }));
	const QVariantMap structured =
		model.get(0).value(QStringLiteral("preview")).toMap().value(QStringLiteral("metadata")).toMap();
	const QVariantList normalizedSparkline = structured.value(QStringLiteral("financeSparkline")).toList();
	QCOMPARE(normalizedSparkline.size(), 64);
	QCOMPARE(normalizedSparkline.first().toMap().value(QStringLiteral("close")).toDouble(), 100.0);
	QVERIFY(!normalizedSparkline.first().toMap().contains(QStringLiteral("private")));
	for (const QString &key : { QStringLiteral("productSpecs"), QStringLiteral("listingSpecs"),
			 QStringLiteral("vehicleSpecs") }) {
		const QVariantList normalizedSpecs = structured.value(key).toList();
		QCOMPARE(normalizedSpecs.size(), 8);
		QCOMPARE(normalizedSpecs.first().toMap().size(), 2);
		QVERIFY(!normalizedSpecs.first().toMap().contains(QStringLiteral("rawHtml")));
	}
	for (const QString &key : { QStringLiteral("gameStoreTags"), QStringLiteral("vehicleHighlights"),
			 QStringLiteral("githubTopics") }) {
		QCOMPARE(structured.value(key).toList().size(), 8);
	}
	const QVariantMap quotedPost = structured.value(QStringLiteral("xQuotedPost")).toMap();
	QVERIFY(!quotedPost.contains(QStringLiteral("url")));
	QVERIFY(!quotedPost.contains(QStringLiteral("raw")));
	QCOMPARE(quotedPost.value(QStringLiteral("text")).toString().size(), 1024);
	const QVariantList normalizedContext = structured.value(QStringLiteral("xReplyContext")).toList();
	QCOMPARE(normalizedContext.size(), 3);
	QCOMPARE(normalizedContext.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("2"));
	QCOMPARE(normalizedContext.last().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("4"));
	QVERIFY(!structured.contains(QStringLiteral("unknownList")));
	QVERIFY(!structured.contains(QStringLiteral("unknownMap")));

	QVariantMap noisyProviderMetadata {
		{ QStringLiteral("provider"), QStringLiteral("marketplace") },
		{ QStringLiteral("previewKind"), QStringLiteral("vehicleListing") },
		{ QStringLiteral("contentWarning"), QStringLiteral("Check listing status") },
		{ QStringLiteral("listingPrice"), QStringLiteral("245 000 kr") },
		{ QStringLiteral("vehicleYear"), QStringLiteral("2024") },
		{ QStringLiteral("vehicleMileage"), QStringLiteral("1 200 mil") },
		{ QStringLiteral("vehicleFuel"), QStringLiteral("Electric") },
		{ QStringLiteral("vehicleTransmission"), QStringLiteral("Automatic") },
		{ QStringLiteral("vehicleLocation"), QStringLiteral("Stockholm") }
	};
	for (int index = 0; index < 80; ++index)
		noisyProviderMetadata.insert(QStringLiteral("aaaDiagnostic%1").arg(index, 2, 10, QLatin1Char('0')), index);
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("preview"), QVariantMap { { QStringLiteral("metadata"), noisyProviderMetadata } } } }));
	const QVariantMap prioritized =
		model.get(0).value(QStringLiteral("preview")).toMap().value(QStringLiteral("metadata")).toMap();
	QCOMPARE(prioritized.size(), 32);
	for (const QString &key : { QStringLiteral("contentWarning"), QStringLiteral("listingPrice"),
			 QStringLiteral("vehicleYear"), QStringLiteral("vehicleMileage"),
			 QStringLiteral("vehicleFuel"), QStringLiteral("vehicleTransmission"),
			 QStringLiteral("vehicleLocation") }) {
		QVERIFY2(prioritized.contains(key), qPrintable(QStringLiteral("missing prioritized field %1").arg(key)));
	}

	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("preview"),
			QVariantMap { { QStringLiteral("title"), QStringLiteral("Pipeline image") },
						  { QStringLiteral("mediaKind"), QStringLiteral("image") },
						  { QStringLiteral("mediaMime"), QStringLiteral("image/png") },
						  { QStringLiteral("mediaUrl"), QStringLiteral("image://mumble/preview?g=2") },
						  { QStringLiteral("mediaExternalUrl"), QStringLiteral("https://cdn.example.com/preview.png") },
						  { QStringLiteral("thumbnailUrl"), QStringLiteral("data:image/png;base64,AAAA") } } } }));
	preview = model.get(0).value(QStringLiteral("preview")).toMap();
	QCOMPARE(preview.value(QStringLiteral("mediaUrl")).toString(), QStringLiteral("image://mumble/preview?g=2"));
	QCOMPARE(preview.value(QStringLiteral("mediaExternalUrl")).toString(),
			 QStringLiteral("https://cdn.example.com/preview.png"));
	QVERIFY(!preview.contains(QStringLiteral("thumbnailUrl")));

	QVariantList boundedAttachments;
	QVariantList boundedMediaItems;
	for (int index = 0; index < 24; ++index) {
		boundedAttachments.push_back(QVariantMap {
			{ QStringLiteral("id"), QStringLiteral("attachment:%1").arg(index) },
			{ QStringLiteral("url"), QStringLiteral("image://mumble/attachment:%1?g=1").arg(index) }
		});
		boundedMediaItems.push_back(QVariantMap {
			{ QStringLiteral("kind"), QStringLiteral("image") },
			{ QStringLiteral("mime"), QStringLiteral("image/jpeg") },
			{ QStringLiteral("url"), QStringLiteral("https://cdn.example.com/image-%1.jpg").arg(index) }
		});
	}
	const QString directVideo = QStringLiteral("data:video/mp4;base64,AAAA");
	const QString directAudio = QStringLiteral("data:audio/mp4;base64,AAAA");
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("attachments"), boundedAttachments },
		  { QStringLiteral("preview"),
			QVariantMap { { QStringLiteral("title"), QStringLiteral("Direct media") },
						  { QStringLiteral("mediaKind"), QStringLiteral("video") },
						  { QStringLiteral("mediaMime"), QStringLiteral("video/mp4") },
						  { QStringLiteral("mediaUrl"), directVideo },
						  { QStringLiteral("mediaAudioMime"), QStringLiteral("audio/mp4") },
						  { QStringLiteral("mediaAudioUrl"), directAudio },
						  { QStringLiteral("mediaItems"), boundedMediaItems } } } }));
	row = model.get(0);
	preview = row.value(QStringLiteral("preview")).toMap();
	QCOMPARE(preview.value(QStringLiteral("mediaUrl")).toString(), directVideo);
	QCOMPARE(preview.value(QStringLiteral("mediaAudioUrl")).toString(), directAudio);
	QCOMPARE(preview.value(QStringLiteral("mediaAudioMime")).toString(), QStringLiteral("audio/mp4"));
	QCOMPARE(preview.value(QStringLiteral("mediaItems")).toList().size(), 16);
	const QVariantMap firstImageItem = preview.value(QStringLiteral("mediaItems")).toList().first().toMap();
	QVERIFY(!firstImageItem.contains(QStringLiteral("url")));
	QCOMPARE(firstImageItem.value(QStringLiteral("externalUrl")).toString(),
			 QStringLiteral("https://cdn.example.com/image-0.jpg"));
	QCOMPARE(row.value(QStringLiteral("attachments")).toList().size(), 16);
}

void TestQmlClientModels::chatTimelineNormalizesProviderMetadata() {
	ChatTimelineModel model;
	int sequence = 0;
	const auto withNoise = [](QVariantMap metadata) {
		for (int index = 0; index < 80; ++index) {
			metadata.insert(QStringLiteral("diagnostic%1").arg(index, 2, 10, QLatin1Char('0')), index);
		}
		return metadata;
	};
	const auto normalize = [&](const QVariantMap &metadata,
							   const QString &url = QStringLiteral("https://example.com/provider")) {
		const QString messageKey = QStringLiteral("provider:%1").arg(++sequence);
		if (!model.upsertMessage({ { QStringLiteral("messageKey"), messageKey },
				{ QStringLiteral("preview"), QVariantMap { { QStringLiteral("url"), url },
					{ QStringLiteral("metadata"), metadata } } } })) {
			return QVariantMap {};
		}
		return model.get(model.rowCount() - 1)
			.value(QStringLiteral("preview"))
			.toMap()
			.value(QStringLiteral("metadata"))
			.toMap();
	};

	const QString twitchEmbedUrl = QStringLiteral("https://player.twitch.tv/?channel=qt_test&parent=localhost");
	const QVariantMap twitch = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("twitch") },
		{ QStringLiteral("previewProvider"), QStringLiteral("twitch") },
		{ QStringLiteral("providerName"), QStringLiteral("Twitch") },
		{ QStringLiteral("previewKind"), QStringLiteral("video") },
		{ QStringLiteral("twitchKind"), QStringLiteral("channel") },
		{ QStringLiteral("twitchBadge"), QStringLiteral("Live") },
		{ QStringLiteral("twitchPlaybackNote"), QString(1800, QLatin1Char('p')) },
		{ QStringLiteral("twitchChannel"), QStringLiteral("qt_test") },
		{ QStringLiteral("twitchGame"), QStringLiteral("Qt Quick") },
		{ QStringLiteral("twitchLiveState"), QStringLiteral("live") },
		{ QStringLiteral("twitchViewerCount"), 12345 },
		{ QStringLiteral("twitchDisclaimer"), QStringLiteral("Provider playback notice") },
		{ QStringLiteral("twitchEmbedMode"), QStringLiteral("live") },
		{ QStringLiteral("twitchSuggestedEmbedUrl"), twitchEmbedUrl },
		{ QStringLiteral("twitchSuggestedVideoId"), QStringLiteral("123456") },
		{ QStringLiteral("twitchThumbnailUrl"), QStringLiteral("https://cdn.example.test/thumb.jpg") }
	}));
	QCOMPARE(twitch.size(), 32);
	for (const QString &key : { QStringLiteral("twitchBadge"), QStringLiteral("twitchChannel"),
			 QStringLiteral("twitchGame"), QStringLiteral("twitchLiveState"),
			 QStringLiteral("twitchViewerCount"), QStringLiteral("twitchDisclaimer"),
			 QStringLiteral("twitchEmbedMode"), QStringLiteral("twitchSuggestedEmbedUrl"),
			 QStringLiteral("twitchSuggestedVideoId") }) {
		QVERIFY2(twitch.contains(key), qPrintable(QStringLiteral("missing Twitch field %1").arg(key)));
	}
	QCOMPARE(twitch.value(QStringLiteral("twitchPlaybackNote")).toString().size(), 1024);
	QCOMPARE(twitch.value(QStringLiteral("twitchSuggestedEmbedUrl")).toString(),
		QUrl(twitchEmbedUrl).toString(QUrl::FullyEncoded));
	QVERIFY(!twitch.contains(QStringLiteral("twitchThumbnailUrl")));

	QVariantList githubTopics;
	for (int index = 0; index < 12; ++index) githubTopics.push_back(QStringLiteral("topic-%1").arg(index));
	const QVariantMap github = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("github") },
		{ QStringLiteral("githubOwner"), QStringLiteral("mumble-voip") },
		{ QStringLiteral("githubRepo"), QStringLiteral("mumble") },
		{ QStringLiteral("githubFullName"), QStringLiteral("mumble-voip/mumble") },
		{ QStringLiteral("githubHtmlUrl"), QStringLiteral("https://github.com/mumble-voip/mumble") },
		{ QStringLiteral("githubDescription"), QString(1800, QLatin1Char('d')) },
		{ QStringLiteral("githubLanguage"), QStringLiteral("C++") },
		{ QStringLiteral("githubDefaultBranch"), QStringLiteral("master") },
		{ QStringLiteral("githubPushedAt"), QStringLiteral("2026-07-13T18:00:00Z") },
		{ QStringLiteral("githubOwnerLogin"), QStringLiteral("mumble-voip") },
		{ QStringLiteral("githubOwnerAvatarUrl"), QStringLiteral("https://avatars.githubusercontent.com/u/1") },
		{ QStringLiteral("githubLicense"), QStringLiteral("BSD-3-Clause") },
		{ QStringLiteral("githubStars"), 7300 }, { QStringLiteral("githubForks"), 1200 },
		{ QStringLiteral("githubOpenIssues"), 540 }, { QStringLiteral("githubArchived"), false },
		{ QStringLiteral("githubTopics"), githubTopics },
		{ QStringLiteral("githubLatestReleaseTag"), QStringLiteral("v1.5.0") },
		{ QStringLiteral("githubLatestReleaseName"), QStringLiteral("Mumble 1.5.0") },
		{ QStringLiteral("githubLatestReleaseUrl"),
		  QStringLiteral("https://github.com/mumble-voip/mumble/releases/tag/v1.5.0") },
		{ QStringLiteral("githubLatestReleasePublishedAt"), QStringLiteral("2026-07-01T12:00:00Z") },
		{ QStringLiteral("githubLatestReleaseNotes"), QString(2400, QLatin1Char('n')) },
		{ QStringLiteral("githubLatestReleasePrerelease"), false },
		{ QStringLiteral("githubLatestReleaseAssetCount"), 4 },
		{ QStringLiteral("githubLatestReleaseAssetName"), QStringLiteral("mumble-x64.msi") },
		{ QStringLiteral("githubLatestReleaseAssetUrl"),
		  QStringLiteral("https://github.com/mumble-voip/mumble/releases/download/v1.5.0/mumble-x64.msi") },
		{ QStringLiteral("githubLatestReleaseDownloadCount"), 987654 },
		{ QStringLiteral("githubDangerUrl"), QStringLiteral("javascript:alert(1)") }
	}));
	QCOMPARE(github.size(), 32);
	for (const QString &key : { QStringLiteral("githubFullName"), QStringLiteral("githubDescription"),
			 QStringLiteral("githubTopics"), QStringLiteral("githubLatestReleaseTag"),
			 QStringLiteral("githubLatestReleaseName"), QStringLiteral("githubLatestReleaseUrl"),
			 QStringLiteral("githubLatestReleasePublishedAt"),
			 QStringLiteral("githubLatestReleaseNotes"),
			 QStringLiteral("githubLatestReleaseAssetCount"),
			 QStringLiteral("githubLatestReleaseAssetName"),
			 QStringLiteral("githubLatestReleaseAssetUrl"),
			 QStringLiteral("githubLatestReleaseDownloadCount") }) {
		QVERIFY2(github.contains(key), qPrintable(QStringLiteral("missing GitHub field %1").arg(key)));
	}
	QCOMPARE(github.value(QStringLiteral("githubDescription")).toString().size(), 1024);
	QCOMPARE(github.value(QStringLiteral("githubLatestReleaseNotes")).toString().size(), 1024);
	QCOMPARE(github.value(QStringLiteral("githubTopics")).toList().size(), 8);
	QVERIFY(!github.contains(QStringLiteral("githubOwnerAvatarUrl")));
	QVERIFY(!github.contains(QStringLiteral("githubDangerUrl")));

	QVariantMap forumMetadata {
		{ QStringLiteral("provider"), QStringLiteral("flashback") },
		{ QStringLiteral("previewProvider"), QStringLiteral("flashback") },
		{ QStringLiteral("providerName"), QStringLiteral("Flashback") },
		{ QStringLiteral("previewKind"), QStringLiteral("forum") },
		{ QStringLiteral("threadId"), QStringLiteral("123456") },
		{ QStringLiteral("forumThreadId"), QStringLiteral("123456") },
		{ QStringLiteral("forumThreadTitle"), QStringLiteral("A bounded forum thread") },
		{ QStringLiteral("forumLinkKind"), QStringLiteral("post") },
		{ QStringLiteral("postId"), QStringLiteral("778899") },
		{ QStringLiteral("forumLinkedPostId"), QStringLiteral("778899") },
		{ QStringLiteral("forumThreadPostUrl"), QStringLiteral("https://www.flashback.org/t123456p7") },
		{ QStringLiteral("forumCategory"), QStringLiteral("Datorer") },
		{ QStringLiteral("forumName"), QStringLiteral("Programmering") },
		{ QStringLiteral("forumPage"), QStringLiteral("7") },
		{ QStringLiteral("forumPageCount"), QStringLiteral("42") },
		{ QStringLiteral("forumPostCount"), QStringLiteral("839") },
		{ QStringLiteral("forumQuoteAuthor"), QStringLiteral("quoted-user") },
		{ QStringLiteral("forumQuoteExcerpt"), QString(1600, QLatin1Char('q')) },
		{ QStringLiteral("forumQuotePostUrl"), QStringLiteral("https://www.flashback.org/sp7654321") },
		{ QStringLiteral("forumQuotePostId"), QStringLiteral("7654321") },
		{ QStringLiteral("forumQuotePostNumber"), QStringLiteral("6") },
		{ QStringLiteral("rawHtml"), QStringLiteral("<script>alert(1)</script>") }
	};
	for (const QString &suffix : { QStringLiteral("Id"), QStringLiteral("Time"),
			 QStringLiteral("Number"), QStringLiteral("Author"), QStringLiteral("AuthorTitle"),
			 QStringLiteral("AuthorRegistered"), QStringLiteral("AuthorPosts"),
			 QStringLiteral("Excerpt") }) {
		const QString value = suffix == QLatin1String("Excerpt") ? QString(1600, QLatin1Char('e'))
			: QStringLiteral("value-%1").arg(suffix);
		forumMetadata.insert(QStringLiteral("forumPost%1").arg(suffix), value);
		forumMetadata.insert(QStringLiteral("forumFirstPost%1").arg(suffix), value);
	}
	forumMetadata.insert(QStringLiteral("forumPostAuthorAvatarUrl"),
		QStringLiteral("image://mumble/forum-avatar?g=1"));
	forumMetadata.insert(QStringLiteral("forumFirstPostAuthorAvatarUrl"),
		QStringLiteral("https://www.flashback.org/avatar.jpg"));
	const QVariantMap forum = normalize(withNoise(forumMetadata));
	QCOMPARE(forum.size(), 32);
	QCOMPARE(forum.value(QStringLiteral("forumPostExcerpt")).toString().size(), 1024);
	QCOMPARE(forum.value(QStringLiteral("forumQuoteExcerpt")).toString().size(), 1024);
	QCOMPARE(forum.value(QStringLiteral("forumPostAuthorAvatarUrl")).toString(),
		QStringLiteral("image://mumble/forum-avatar?g=1"));
	QVERIFY(!forum.contains(QStringLiteral("forumFirstPostExcerpt")));
	QVERIFY(!forum.contains(QStringLiteral("forumFirstPostAuthorAvatarUrl")));
	QVERIFY(!forum.contains(QStringLiteral("threadId")));
	QVERIFY(!forum.contains(QStringLiteral("forumLinkedPostId")));
	QVERIFY(!forum.contains(QStringLiteral("rawHtml")));
	QCOMPARE(forum.value(QStringLiteral("forumQuotePostUrl")).toString(),
		QStringLiteral("https://www.flashback.org/sp7654321"));

	const QVariantMap instagram = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("instagram") },
		{ QStringLiteral("instagramMetadataVersion"), 1 },
		{ QStringLiteral("instagramMediaKind"), QStringLiteral("reel") },
		{ QStringLiteral("instagramDisplayName"), QStringLiteral("Qt Project") },
		{ QStringLiteral("instagramHandle"), QStringLiteral("qtproject") },
		{ QStringLiteral("instagramCaption"), QString(1800, QLatin1Char('c')) },
		{ QStringLiteral("instagramCreatedAt"), QStringLiteral("2026-07-13") },
		{ QStringLiteral("instagramAvatarUrl"), QStringLiteral("https://cdninstagram.com/avatar.jpg") },
		{ QStringLiteral("instagramOwnerUserId"), QStringLiteral("123456789") },
		{ QStringLiteral("instagramLikeCount"), 2500 },
		{ QStringLiteral("instagramCommentCount"), 42 }
	}));
	QCOMPARE(instagram.size(), 32);
	QCOMPARE(instagram.value(QStringLiteral("instagramCaption")).toString().size(), 1024);
	QCOMPARE(instagram.value(QStringLiteral("instagramDisplayName")).toString(), QStringLiteral("Qt Project"));
	QCOMPARE(instagram.value(QStringLiteral("instagramLikeCount")).toULongLong(), 2500ULL);
	QVERIFY(!instagram.contains(QStringLiteral("instagramAvatarUrl")));

	const QVariantMap steam = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("steam") },
		{ QStringLiteral("steamAppId"), QStringLiteral("1234") },
		{ QStringLiteral("steamReviewSummary"), QStringLiteral("Very Positive") },
		{ QStringLiteral("steamReviewScore"), 8 },
		{ QStringLiteral("steamReviewTotal"), 123456 },
		{ QStringLiteral("steamReviewPositive"), 110000 },
		{ QStringLiteral("steamReviewNegative"), 13456 },
		{ QStringLiteral("steamReviewPercent"), 89 },
		{ QStringLiteral("steamStoreUrl"), QStringLiteral("https://store.steampowered.com/app/1234/") },
		{ QStringLiteral("steamHeaderImage"), QStringLiteral("https://cdn.example.test/header.jpg") }
	}));
	QCOMPARE(steam.size(), 32);
	for (const QString &key : { QStringLiteral("steamReviewSummary"), QStringLiteral("steamReviewScore"),
			 QStringLiteral("steamReviewTotal"), QStringLiteral("steamReviewPositive"),
			 QStringLiteral("steamReviewNegative"), QStringLiteral("steamReviewPercent") }) {
		QVERIFY2(steam.contains(key), qPrintable(QStringLiteral("missing Steam field %1").arg(key)));
	}
	QVERIFY(!steam.contains(QStringLiteral("steamHeaderImage")));

	const QVariantMap vehicle = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("bytbil") },
		{ QStringLiteral("previewKind"), QStringLiteral("vehicleListing") },
		{ QStringLiteral("vehicleWarning"), QString(600, QLatin1Char('w')) },
		{ QStringLiteral("vehicleListingId"), QStringLiteral("998877") },
		{ QStringLiteral("vehicleImage"), QStringLiteral("file:///private/vehicle.jpg") }
	}));
	QCOMPARE(vehicle.size(), 32);
	QCOMPARE(vehicle.value(QStringLiteral("vehicleWarning")).toString().size(), 256);
	QCOMPARE(vehicle.value(QStringLiteral("vehicleListingId")).toString(), QStringLiteral("998877"));
	QVERIFY(!vehicle.contains(QStringLiteral("vehicleImage")));

	const QVariantMap linkDigest = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("existenz") },
		{ QStringLiteral("previewProvider"), QStringLiteral("existenz") },
		{ QStringLiteral("previewKind"), QStringLiteral("linkDigest") },
		{ QStringLiteral("linkDigestTitle"), QString(800, QLatin1Char('t')) },
		{ QStringLiteral("linkDigestSource"), QStringLiteral("Existenz") },
		{ QStringLiteral("linkDigestCaption"), QString(1800, QLatin1Char('l')) },
		{ QStringLiteral("contentWarning"), QStringLiteral("NSFW") },
		{ QStringLiteral("thumbnailBlur"), true }
	}));
	QCOMPARE(linkDigest.size(), 32);
	QCOMPARE(linkDigest.value(QStringLiteral("contentWarning")).toString(), QStringLiteral("NSFW"));
	QVERIFY(linkDigest.value(QStringLiteral("thumbnailBlur")).toBool());
	QCOMPARE(linkDigest.value(QStringLiteral("linkDigestTitle")).toString().size(), 512);
	QCOMPARE(linkDigest.value(QStringLiteral("linkDigestCaption")).toString().size(), 1024);
	QCOMPARE(linkDigest.value(QStringLiteral("linkDigestSource")).toString(), QStringLiteral("Existenz"));

	const QVariantMap google = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("google-search") },
		{ QStringLiteral("previewProvider"), QStringLiteral("google-search") },
		{ QStringLiteral("previewKind"), QStringLiteral("search") },
		{ QStringLiteral("googleSearchQuery"), QString(1800, QLatin1Char('g')) },
		{ QStringLiteral("googleSearchMode"), QStringLiteral("images") },
		{ QStringLiteral("googleSearchModeLabel"), QStringLiteral("Images") }
	}), QStringLiteral("https://www.google.com/search?q=qt+quick&tbm=isch"));
	QCOMPARE(google.size(), 32);
	QCOMPARE(google.value(QStringLiteral("googleSearchQuery")).toString().size(), 1024);
	QCOMPARE(google.value(QStringLiteral("googleSearchMode")).toString(), QStringLiteral("images"));
	QCOMPARE(google.value(QStringLiteral("googleSearchModeLabel")).toString(), QStringLiteral("Images"));

	const QVariantMap sparse = normalize({
		{ QStringLiteral("githubLatestReleaseNotes"), QStringLiteral("Sparse release") },
		{ QStringLiteral("githubLatestReleaseUrl"), QStringLiteral("javascript:alert(1)") },
		{ QStringLiteral("githubLatestReleaseAssetUrl"),
		  QVariant(QStringLiteral("https://example.com/") + QString(20000, QLatin1Char('a'))) },
		{ QStringLiteral("githubLatestReleaseLoading"), true },
		{ QStringLiteral("githubLatestReleaseMissing"), QStringLiteral("true") },
		{ QStringLiteral("forumFirstPostExcerpt"), QStringLiteral("Sparse first post") },
		{ QStringLiteral("forumQuotePostUrl"), QStringLiteral("https://www.flashback.org/sp123") },
		{ QStringLiteral("instagramCaption"), QStringLiteral("Sparse caption") },
		{ QStringLiteral("instagramAvatarUrl"), QStringLiteral("image://mumble/instagram-avatar?g=2") },
		{ QStringLiteral("steamReviewTotal"), 7 },
		{ QStringLiteral("steamReviewPositive"), -1 },
		{ QStringLiteral("steamReviewPercent"), 101 },
		{ QStringLiteral("vehicleWarning"), QStringLiteral("Check listing") },
		{ QStringLiteral("googleSearchQuery"), QStringLiteral("Qt Quick") },
		{ QStringLiteral("rawHtml"), QStringLiteral("<b>unsafe shape</b>") }
	});
	QCOMPARE(sparse.value(QStringLiteral("githubLatestReleaseNotes")).toString(), QStringLiteral("Sparse release"));
	QVERIFY(!sparse.contains(QStringLiteral("githubLatestReleaseUrl")));
	QVERIFY(!sparse.contains(QStringLiteral("githubLatestReleaseAssetUrl")));
	QVERIFY(sparse.value(QStringLiteral("githubLatestReleaseLoading")).toBool());
	QVERIFY(!sparse.contains(QStringLiteral("githubLatestReleaseMissing")));
	QCOMPARE(sparse.value(QStringLiteral("forumPostExcerpt")).toString(), QStringLiteral("Sparse first post"));
	QVERIFY(!sparse.contains(QStringLiteral("forumFirstPostExcerpt")));
	QCOMPARE(sparse.value(QStringLiteral("instagramCaption")).toString(), QStringLiteral("Sparse caption"));
	QCOMPARE(sparse.value(QStringLiteral("instagramAvatarUrl")).toString(),
		QStringLiteral("image://mumble/instagram-avatar?g=2"));
	QCOMPARE(sparse.value(QStringLiteral("steamReviewTotal")).toULongLong(), 7ULL);
	QVERIFY(!sparse.contains(QStringLiteral("steamReviewPositive")));
	QVERIFY(!sparse.contains(QStringLiteral("steamReviewPercent")));
	QCOMPARE(sparse.value(QStringLiteral("vehicleWarning")).toString(), QStringLiteral("Check listing"));
	QCOMPARE(sparse.value(QStringLiteral("googleSearchQuery")).toString(), QStringLiteral("Qt Quick"));
	QVERIFY(!sparse.contains(QStringLiteral("rawHtml")));
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
		{ QStringLiteral("label"), QStringLiteral("Alice") },
		{ QStringLiteral("subtitle"), QStringLiteral("Listening in Lobby") },
		{ QStringLiteral("talkState"), QStringLiteral("passive") } });
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Alice"));
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Listening in Lobby"));
	model.upsertParticipantState({ { QStringLiteral("session"), 7 },
		{ QStringLiteral("label"), QStringLiteral("Alice 2") },
		{ QStringLiteral("subtitle"), QStringLiteral("Speaking") },
		{ QStringLiteral("talkState"), QStringLiteral("talking") } });
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Alice 2"));
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Speaking"));
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

void TestQmlClientModels::pendingBootstrapCompletesBeforeSteadyState() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString messagesPath = QFINDTESTDATA("../../mumble/Messages.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!messagesPath.isEmpty(), "Messages.cpp test data was not found");

	QFile mainWindowFile(mainWindowPath);
	QFile messagesFile(messagesPath);
	QVERIFY(mainWindowFile.open(QIODevice::ReadOnly | QIODevice::Text));
	QVERIFY(messagesFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString mainWindowSource = QString::fromUtf8(mainWindowFile.readAll());
	const QString messagesSource = QString::fromUtf8(messagesFile.readAll());

	const qsizetype lifecycleStart =
		mainWindowSource.indexOf(QStringLiteral("void MainWindow::enterQmlShellSteadyState()"));
	const qsizetype lifecycleEnd =
		mainWindowSource.indexOf(QStringLiteral("void MainWindow::publishModernShellTalkState"), lifecycleStart);
	QVERIFY(lifecycleStart >= 0);
	QVERIFY(lifecycleEnd > lifecycleStart);
	const QString lifecycleBody = mainWindowSource.mid(lifecycleStart, lifecycleEnd - lifecycleStart);
	const qsizetype activeTimerCheck = lifecycleBody.indexOf(QStringLiteral("m_modernShellSyncTimer->isActive()"));
	const qsizetype stopPendingTimer = lifecycleBody.indexOf(QStringLiteral("m_modernShellSyncTimer->stop()"));
	const qsizetype completePendingBootstrap = lifecycleBody.indexOf(QStringLiteral("runQmlShellStateSync()"));
	const qsizetype enterSteadyState = lifecycleBody.indexOf(QStringLiteral("enterSteadyState()"));
	QVERIFY(activeTimerCheck >= 0);
	QVERIFY(stopPendingTimer > activeTimerCheck);
	QVERIFY(completePendingBootstrap > stopPendingTimer);
	QVERIFY(enterSteadyState > completePendingBootstrap);

	const qsizetype serverSyncStart =
		messagesSource.indexOf(QStringLiteral("void MainWindow::msgServerSync"));
	const qsizetype serverSyncEnd =
		messagesSource.indexOf(QStringLiteral("void MainWindow::msgServerConfig"), serverSyncStart);
	QVERIFY(serverSyncStart >= 0);
	QVERIFY(serverSyncEnd > serverSyncStart);
	const QString serverSyncBody = messagesSource.mid(serverSyncStart, serverSyncEnd - serverSyncStart);
	const qsizetype synchronized =
		serverSyncBody.indexOf(QStringLiteral("setServerSynchronized(true)"));
	const qsizetype lifecycleTransition =
		serverSyncBody.indexOf(QStringLiteral("enterQmlShellSteadyState()"));
	QVERIFY(synchronized >= 0);
	QVERIFY(lifecycleTransition > synchronized);
	QVERIFY(!serverSyncBody.contains(QStringLiteral("fullBootstrapMonitor().enterSteadyState()")));
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
	QSignalSpy motdSegmentsSpy(&session, &ClientSessionController::motdSegmentsChanged);
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
	QTRY_COMPARE_WITH_TIMEOUT(motdSegmentsSpy.count(), 1, 5000);
	QTRY_COMPARE_WITH_TIMEOUT(session.motdSegments().size(), 1, 5000);
	QCOMPARE(session.motdSegments().first().toMap().value(QStringLiteral("text")).toString(),
			 QStringLiteral("Welcome"));
	QCOMPARE(session.motdSummary(), QStringLiteral("Welcome"));
}

void TestQmlClientModels::sessionParsesManagedMotdImagesAndTracksSourceIdentity() {
	ClientSessionController session;
	QSignalSpy htmlSpy(&session, &ClientSessionController::motdHtmlChanged);
	QSignalSpy segmentsSpy(&session, &ClientSessionController::motdSegmentsChanged);
	const QString renderedHtml = QStringLiteral(
		"<p>Above</p><img src=\"image://mumble/motd-inline?g=1\" width=\"640\" height=\"360\" "
		"alt=\"Server welcome art\"/><p>Below</p>");
	session.setMotdContent(renderedHtml, QStringLiteral("server supplied motd v1 with base64 image"));
	QCOMPARE(htmlSpy.count(), 1);
	QTRY_VERIFY_WITH_TIMEOUT(segmentsSpy.count() >= 1, 5000);

	const auto imageSegment = [&session]() {
		for (const QVariant &value : session.motdSegments()) {
			const QVariantMap segment = value.toMap();
			if (segment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) return segment;
		}
		return QVariantMap {};
	};
	QTRY_VERIFY_WITH_TIMEOUT(!imageSegment().isEmpty(), 5000);
	QCOMPARE(imageSegment().value(QStringLiteral("source")).toString(),
			 QStringLiteral("image://mumble/motd-inline?g=1"));
	QCOMPARE(imageSegment().value(QStringLiteral("width")).toInt(), 640);
	QCOMPARE(imageSegment().value(QStringLiteral("height")).toInt(), 360);
	QCOMPARE(imageSegment().value(QStringLiteral("alt")).toString(), QStringLiteral("Server welcome art"));

	const QString firstSignature = session.motdSignature();
	session.setMotdDismissedSignature(firstSignature);
	QVERIFY(session.motdDismissed());
	session.setMotdContent(renderedHtml, QStringLiteral("server supplied motd v2 with changed base64 image"));
	QCOMPARE(htmlSpy.count(), 1);
	QVERIFY(session.motdSignature() != firstSignature);
	QVERIFY(!session.motdDismissed());
	QVERIFY(session.motdChanged());

	session.setMotdContent(QStringLiteral("<p>Fallback identity</p>"), {});
	QVERIFY(!session.motdSignature().isEmpty());
	QCOMPARE(session.motdActions().constLast().toMap().value(QStringLiteral("payload")).toMap()
		.value(QStringLiteral("signature")).toString(), session.motdSignature());
}

void TestQmlClientModels::sessionAppliesTypedConnectionState() {
	ClientSessionController session;
	QSignalSpy stateSpy(&session, &ClientSessionController::connectionStateChanged);
	QSignalSpy detailSpy(&session, &ClientSessionController::connectionDetailChanged);
	QSignalSpy retrySpy(&session, &ClientSessionController::connectionRetryRemainingMsChanged);
	QSignalSpy cancelSpy(&session, &ClientSessionController::canCancelChanged);
	const QVariantMap retryingState {
		{ QStringLiteral("connectionState"), QStringLiteral(" RETRYING ") },
		{ QStringLiteral("connectionLabel"), QStringLiteral("Retry in 3s") },
		{ QStringLiteral("selfStatusLabel"), QStringLiteral("Muted") },
		{ QStringLiteral("connectionTone"), QStringLiteral("RETRY") },
		{ QStringLiteral("connectionTooltip"), QStringLiteral("Automatic reconnect is scheduled") },
		{ QStringLiteral("connectionRetryRemainingMs"), 2450 },
		{ QStringLiteral("canConnect"), false },
		{ QStringLiteral("canCancelConnection"), true }
	};
	session.applyState(retryingState);
	QCOMPARE(session.connectionState(), QStringLiteral("retrying"));
	QCOMPARE(session.connectionLabel(), QStringLiteral("Retry in 3s"));
	QCOMPARE(session.selfStatusLabel(), QStringLiteral("Muted"));
	QCOMPARE(session.connectionTone(), QStringLiteral("retry"));
	QCOMPARE(session.connectionDetail(), QStringLiteral("Automatic reconnect is scheduled"));
	QCOMPARE(session.connectionRetryRemainingMs(), 2450);
	QVERIFY(!session.canConnect());
	QVERIFY(session.canCancel());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(detailSpy.count(), 1);
	QCOMPARE(retrySpy.count(), 1);
	QCOMPARE(cancelSpy.count(), 1);

	session.applyState(retryingState);
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(detailSpy.count(), 1);
	QCOMPARE(retrySpy.count(), 1);
	QCOMPARE(cancelSpy.count(), 1);

	session.applyState({ { QStringLiteral("connectionState"), QStringLiteral("disconnected") },
						 { QStringLiteral("connectionRetryRemainingMs"), -100 },
						 { QStringLiteral("canConnect"), true },
						 { QStringLiteral("canDisconnect"), false } });
	QCOMPARE(session.connectionRetryRemainingMs(), 0);
	QVERIFY(session.canConnect());
	QVERIFY(!session.canCancel());
}

void TestQmlClientModels::sessionDerivesTypedMotdState() {
	ClientSessionController session;
	QSignalSpy signatureSpy(&session, &ClientSessionController::motdSignatureChanged);
	QSignalSpy dismissedSpy(&session, &ClientSessionController::motdDismissedChanged);
	QSignalSpy actionsSpy(&session, &ClientSessionController::motdActionsChanged);
	session.applyState({ { QStringLiteral("motdHtml"), QStringLiteral("<p>Welcome</p>") },
						 { QStringLiteral("motdSummary"), QStringLiteral("Welcome") },
						 { QStringLiteral("motdExpanded"), false },
						 { QStringLiteral("motdDismissedSignature"), QString() },
						 { QStringLiteral("motdLastSeenSignature"), QStringLiteral("v1:old") } });
	QVERIFY(session.hasMotd());
	QVERIFY(!session.motdExpanded());
	QVERIFY(!session.motdDismissed());
	QVERIFY(session.motdSignature().startsWith(QStringLiteral("v1:14:")));
	QVERIFY(session.motdChanged());
	QCOMPARE(session.motdActions().size(), 2);
	QCOMPARE(session.motdActions().at(0).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("motd.show"));
	QVERIFY(signatureSpy.count() >= 1);
	QVERIFY(actionsSpy.count() >= 1);

	const QString signature = session.motdSignature();
	session.setMotdDismissedSignature(signature);
	QVERIFY(session.motdDismissed());
	QCOMPARE(dismissedSpy.count(), 1);
	QVERIFY(session.motdActions().isEmpty());
	session.setMotdLastSeenSignature(signature);
	QVERIFY(!session.motdChanged());

	// Older settings stored the content itself instead of the versioned signature.
	session.setMotdDismissedSignature(session.motdHtml());
	QVERIFY(session.motdDismissed());
	session.setMotdHtml(QStringLiteral("<p>Updated welcome</p>"));
	QVERIFY(!session.motdDismissed());
	QVERIFY(session.motdChanged());
	session.setMotdHtml({});
	QVERIFY(!session.hasMotd());
	QVERIFY(session.motdActions().isEmpty());
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

void TestQmlClientModels::sessionPublishesTypedStonksState() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::stonksChanged);
	const QVariantMap populated {
		{ QStringLiteral("supported"), true },
		{ QStringLiteral("enabled"), true },
		{ QStringLiteral("automationHeaderVisible"), true },
		{ QStringLiteral("pinnedTickers"), QVariantList {
			QVariantMap { { QStringLiteral("symbol"), QStringLiteral("RKLB") } } } },
		{ QStringLiteral("tickerQuotes"), QVariantMap {
			{ QStringLiteral("RKLB"), QVariantMap {
				{ QStringLiteral("ok"), true }, { QStringLiteral("price"), 18.42 },
				{ QStringLiteral("changePercent"), 4.7 } } } } }
	};
	session.applyState({ { QStringLiteral("stonks"), populated } });
	QCOMPARE(spy.count(), 1);
	QCOMPARE(session.stonks(), populated);
	session.applyState({ { QStringLiteral("stonks"), populated } });
	QCOMPARE(spy.count(), 1);

	QVariantMap loading = populated;
	loading.insert(QStringLiteral("loading"), true);
	loading.insert(QStringLiteral("status"), QStringLiteral("Loading Stonks ticker quotes"));
	session.applyState({ { QStringLiteral("stonks"), loading } });
	QCOMPARE(spy.count(), 2);
	QVERIFY(session.stonks().value(QStringLiteral("loading")).toBool());
}

void TestQmlClientModels::sessionPublishesSemanticMenus() {
	ClientSessionController session;
	QSignalSpy menusSpy(&session, &ClientSessionController::appMenusChanged);
	QSignalSpy selfMenuSpy(&session, &ClientSessionController::selfMenuChanged);
	const QVariantList serverItems {
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
					  { QStringLiteral("id"), QStringLiteral("server.connect") },
					  { QStringLiteral("label"), QStringLiteral("Connect to a server...") },
					  { QStringLiteral("enabled"), true } },
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("separator") } },
		QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
					  { QStringLiteral("id"), QStringLiteral("server.quit") },
					  { QStringLiteral("label"), QStringLiteral("Quit Mumble...") },
					  { QStringLiteral("tone"), QStringLiteral("danger") },
					  { QStringLiteral("enabled"), true } }
	};
	const QVariantList menus {
		QVariantMap { { QStringLiteral("id"), QStringLiteral("server") },
					  { QStringLiteral("label"), QStringLiteral("Server") },
					  { QStringLiteral("items"), serverItems } }
	};
	const QVariantMap selfMenu {
		{ QStringLiteral("name"), QStringLiteral("Alice") },
		{ QStringLiteral("statusLabel"), QStringLiteral("Muted") },
		{ QStringLiteral("statusTone"), QStringLiteral("warning") },
		{ QStringLiteral("presence"),
		  QVariantList { QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
									 { QStringLiteral("id"), QStringLiteral("self.presence.online") },
									 { QStringLiteral("label"), QStringLiteral("Online") },
									 { QStringLiteral("checked"), false },
									 { QStringLiteral("enabled"), true } } } },
		{ QStringLiteral("actions"),
		  QVariantList { QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
									 { QStringLiteral("id"), QStringLiteral("configure.settings") },
									 { QStringLiteral("label"), QStringLiteral("Settings...") },
									 { QStringLiteral("enabled"), true } } } }
	};

	session.applyState({ { QStringLiteral("menus"), menus }, { QStringLiteral("selfMenu"), selfMenu } });
	QCOMPARE(session.appMenus(), menus);
	QCOMPARE(session.selfMenu(), selfMenu);
	QCOMPARE(menusSpy.count(), 1);
	QCOMPARE(selfMenuSpy.count(), 1);

	// Identical room-state publications must not rebuild either menu.
	session.applyState({ { QStringLiteral("menus"), menus }, { QStringLiteral("selfMenu"), selfMenu } });
	QCOMPARE(menusSpy.count(), 1);
	QCOMPARE(selfMenuSpy.count(), 1);

	QVariantList disabledMenus = menus;
	QVariantMap serverMenu = disabledMenus.first().toMap();
	QVariantList disabledItems = serverMenu.value(QStringLiteral("items")).toList();
	QVariantMap connectItem = disabledItems.first().toMap();
	connectItem.insert(QStringLiteral("enabled"), false);
	disabledItems[0] = connectItem;
	serverMenu.insert(QStringLiteral("items"), disabledItems);
	disabledMenus[0] = serverMenu;
	session.setAppMenus(disabledMenus);
	QCOMPARE(menusSpy.count(), 2);
	QVERIFY(!session.appMenus().first().toMap().value(QStringLiteral("items")).toList().first().toMap()
				.value(QStringLiteral("enabled")).toBool());
}

void TestQmlClientModels::commandsRouteTypedAppActions() {
	UiCommandController commands;
	QSignalSpy spy(&commands, &UiCommandController::appActionRequested);
	commands.invokeAppAction(QStringLiteral("  "), { { QStringLiteral("signature"), QStringLiteral("ignored") } });
	QCOMPARE(spy.count(), 0);
	const QVariantMap payload { { QStringLiteral("signature"), QStringLiteral("v1:14:abcd") } };
	commands.invokeAppAction(QStringLiteral(" motd.dismiss "), payload);
	QCOMPARE(spy.count(), 1);
	const QList< QVariant > arguments = spy.takeFirst();
	QCOMPARE(arguments.at(0).toString(), QStringLiteral("motd.dismiss"));
	QCOMPARE(arguments.at(1).toMap(), payload);
}

void TestQmlClientModels::commandsRejectEmptyStableIds() {
	UiCommandController commands;
	QSignalSpy scopeSpy(&commands, &UiCommandController::scopeSelectionRequested);
	QSignalSpy railScopeSpy(&commands, &UiCommandController::scopeRailSelectionRequested);
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
	commands.selectScopeFromRail(QStringLiteral("   "), QStringLiteral("voice"));
	commands.invokeAction(QString());
	commands.selectParticipant(QStringLiteral("  "));
	commands.openDirectMessage(QString());
	commands.selectParticipant(QStringLiteral("4294967296"));
	commands.openDirectMessage(QStringLiteral("18446744073709551615"));
	commands.replyToMessage(QString());
	commands.retryMessage(QStringLiteral("  "));
	commands.deleteMessage(QString());
	commands.toggleMessageReaction(QStringLiteral("message:1"), QString());
	QCOMPARE(scopeSpy.count(), 0);
	QCOMPARE(railScopeSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(participantSpy.count(), 0);
	QCOMPARE(directMessageSpy.count(), 0);
	QCOMPARE(replySpy.count(), 0);
	QCOMPARE(retrySpy.count(), 0);
	QCOMPARE(deleteSpy.count(), 0);
	QCOMPARE(reactionSpy.count(), 0);
	commands.selectScope(QStringLiteral(" channel:42 "));
	commands.selectScopeFromRail(QStringLiteral(" channel:42 "), QStringLiteral(" Voice "));
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
	const QList< QVariant > railSelection = railScopeSpy.takeFirst();
	QCOMPARE(railSelection.at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(railSelection.at(1).toString(), QStringLiteral("voice"));
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

void TestQmlClientModels::commandsBatchPreviewHydration() {
	UiCommandController commands;
	QSignalSpy hydrationSpy(&commands, &UiCommandController::previewHydrationRequested);
	commands.requestPreviewHydration(QStringLiteral("  "), { 1, 2 }, true);
	commands.requestPreviewHydration(QStringLiteral("channel:42"), { 0, -1, QStringLiteral("bad"), 1.5 }, false);
	QCOMPARE(hydrationSpy.count(), 0);

	QVariantList ids { QStringLiteral(" 7 "), 8, 7, 0, QStringLiteral("invalid"),
		QStringLiteral("4294967295"), QStringLiteral("4294967296"),
		QStringLiteral("18446744073709551615") };
	for (int index = 9; index < 50; ++index) ids.push_back(index);
	commands.requestPreviewHydration(QStringLiteral(" channel:42 "), ids, true);
	QCOMPARE(hydrationSpy.count(), 1);
	const QList< QVariant > arguments = hydrationSpy.takeFirst();
	QCOMPARE(arguments.at(0).toString(), QStringLiteral("channel:42"));
	const QVariantList normalizedIds = arguments.at(1).toList();
	QCOMPARE(normalizedIds.size(), 32);
	QCOMPARE(normalizedIds.at(0).toULongLong(), 7ULL);
	QCOMPARE(normalizedIds.at(1).toULongLong(), 8ULL);
	QCOMPARE(normalizedIds.at(2).toULongLong(),
		static_cast< qulonglong >(std::numeric_limits< unsigned int >::max()));
	QCOMPARE(normalizedIds.at(3).toULongLong(), 9ULL);
	QVERIFY(arguments.at(2).toBool());
}

void TestQmlClientModels::commandsRequestScopeActionsLazily() {
	UiCommandController commands;
	int providerCalls = 0;
	QString requestedScope;
	QString requestedKind;
	commands.setScopeActionsProvider(
		[&](const QString &scopeToken, const QString &kind) {
			++providerCalls;
			requestedScope = scopeToken;
			requestedKind = kind;
			return QVariantList { QVariantMap { { QStringLiteral("id"), QStringLiteral("join") },
														  { QStringLiteral("label"), QStringLiteral("Join") } } };
		});

	QVERIFY(commands.requestScopeActions(QStringLiteral("   "), QStringLiteral("voice")).isEmpty());
	QCOMPARE(providerCalls, 0);
	const QVariantList actions =
		commands.requestScopeActions(QStringLiteral(" channel:42 "), QStringLiteral(" Voice "));
	QCOMPARE(providerCalls, 1);
	QCOMPARE(requestedScope, QStringLiteral("channel:42"));
	QCOMPARE(requestedKind, QStringLiteral("voice"));
	QCOMPARE(actions.size(), 1);
	QCOMPARE(actions.constFirst().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("join"));

	int participantProviderCalls = 0;
	QString requestedParticipant;
	QString requestedEntryKind;
	commands.setParticipantActionsProvider(
		[&](const QString &sessionId, const QString &entryKind, const QString &scopeToken) {
			++participantProviderCalls;
			requestedParticipant = sessionId;
			requestedEntryKind = entryKind;
			requestedScope = scopeToken;
			return QVariantList { QVariantMap { { QStringLiteral("id"), QStringLiteral("message") } } };
		});
	QVERIFY(commands.requestParticipantActions(QString(), QStringLiteral("user"),
		QStringLiteral("channel:42")).isEmpty());
	QCOMPARE(participantProviderCalls, 0);
	const QVariantList participantActions = commands.requestParticipantActions(
		QStringLiteral(" 7 "), QStringLiteral(" Listener "), QStringLiteral(" channel:42 "));
	QCOMPARE(participantProviderCalls, 1);
	QCOMPARE(requestedParticipant, QStringLiteral("7"));
	QCOMPARE(requestedEntryKind, QStringLiteral("listener"));
	QCOMPARE(requestedScope, QStringLiteral("channel:42"));
	QCOMPARE(participantActions.size(), 1);
}

void TestQmlClientModels::commandsValidateStableMoveIds() {
	UiCommandController commands;
	QSignalSpy participantSpy(&commands, &UiCommandController::participantMoveRequested);
	QSignalSpy scopeSpy(&commands, &UiCommandController::scopeMoveRequested);
	commands.moveParticipant(QString(), QStringLiteral("channel:1"));
	commands.moveParticipant(QStringLiteral("0"), QStringLiteral("channel:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("voice:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("channel:bad"));
	commands.moveParticipant(QStringLiteral("+7"), QStringLiteral("channel:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("channel:+1"));
	commands.moveParticipant(QStringLiteral("4294967296"), QStringLiteral("channel:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("channel:4294967296"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("1:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("2:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("3:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("4:7"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("-1:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("-0:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("00:1"));
	commands.moveParticipant(QStringLiteral("7"), QStringLiteral("0:-1"));
	commands.moveScope(QStringLiteral("channel:0"), QStringLiteral("channel:2"), QStringLiteral("inside"));
	commands.moveScope(QStringLiteral("channel:1"), QStringLiteral("channel:2"), QStringLiteral("around"));
	commands.moveScope(QStringLiteral("channel:1"), QStringLiteral("channel:1"), QStringLiteral("inside"));
	commands.moveScope(QStringLiteral("3:1"), QStringLiteral("0:2"), QStringLiteral("inside"));
	commands.moveScope(QStringLiteral("0:1"), QStringLiteral("4:2"), QStringLiteral("inside"));
	commands.moveScope(QStringLiteral("-1:1"), QStringLiteral("0:2"), QStringLiteral("inside"));
	QCOMPARE(participantSpy.count(), 0);
	QCOMPARE(scopeSpy.count(), 0);

	commands.moveParticipant(QStringLiteral(" 7 "), QStringLiteral(" channel:42 "));
	QCOMPARE(participantSpy.count(), 1);
	QCOMPARE(participantSpy.at(0).at(0).toULongLong(), 7ULL);
	QCOMPARE(participantSpy.at(0).at(1).toString(), QStringLiteral("channel:42"));
	commands.moveParticipant(QStringLiteral("8"), QStringLiteral(" channel:000 "));
	QCOMPARE(participantSpy.count(), 2);
	QCOMPARE(participantSpy.at(1).at(0).toULongLong(), 8ULL);
	QCOMPARE(participantSpy.at(1).at(1).toString(), QStringLiteral("channel:0"));
	commands.moveScope(QStringLiteral(" channel:7 "), QStringLiteral(" channel:42 "),
						   QStringLiteral(" Before "));
	QCOMPARE(scopeSpy.count(), 1);
	QCOMPARE(scopeSpy.at(0).at(0).toString(), QStringLiteral("channel:7"));
	QCOMPARE(scopeSpy.at(0).at(1).toString(), QStringLiteral("channel:42"));
	QCOMPARE(scopeSpy.at(0).at(2).toString(), QStringLiteral("before"));

	commands.moveParticipant(QStringLiteral("9"), QStringLiteral(" 0:0042 "));
	QCOMPARE(participantSpy.count(), 3);
	QCOMPARE(participantSpy.at(2).at(0).toULongLong(), 9ULL);
	QCOMPARE(participantSpy.at(2).at(1).toString(), QStringLiteral("0:42"));
	commands.moveScope(QStringLiteral(" 0:7 "), QStringLiteral(" 0:42 "), QStringLiteral(" After "));
	QCOMPARE(scopeSpy.count(), 2);
	QCOMPARE(scopeSpy.at(1).at(0).toString(), QStringLiteral("0:7"));
	QCOMPARE(scopeSpy.at(1).at(1).toString(), QStringLiteral("0:42"));
	QCOMPARE(scopeSpy.at(1).at(2).toString(), QStringLiteral("after"));
	commands.moveScope(QStringLiteral("0:0"), QStringLiteral("0:42"), QStringLiteral("inside"));
	QCOMPARE(scopeSpy.count(), 2);
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
	QSignalSpy presentationSpy(&dialog, &DialogStateController::presentationFieldValuesChanged);
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
						{ QStringLiteral("primaryActionId"), QStringLiteral("ok") },
						{ QStringLiteral("loading"), true },
						{ QStringLiteral("loadingScaffold"), QStringLiteral("records") },
						{ QStringLiteral("status"), QVariantMap { { QStringLiteral("message"), QStringLiteral("Loading devices") } } },
						{ QStringLiteral("tone"), QStringLiteral("warning") },
						{ QStringLiteral("width"), 720 }, { QStringLiteral("height"), 640 },
						{ QStringLiteral("initialFocusId"), QStringLiteral("audio.input") },
						{ QStringLiteral("selectedFavoriteIndex"), 1 },
						{ QStringLiteral("editorOpen"), true },
						{ QStringLiteral("editorTitle"), QStringLiteral("Edit server") },
						{ QStringLiteral("favorites"), QVariantList { QVariantMap { { QStringLiteral("label"), QStringLiteral("Saved") } } } },
						{ QStringLiteral("sections"),
						  QVariantList { QVariantMap { { QStringLiteral("fields"),
											 QVariantList { QVariantMap {
												 { QStringLiteral("id"), QStringLiteral("audio.input") },
												 { QStringLiteral("type"), QStringLiteral("voiceMeter") },
												 { QStringLiteral("value"), 2 } } } } } } },
						{ QStringLiteral("errors"),
						  QVariantMap { { QStringLiteral("audio.input"), QStringLiteral("Choose an input") } } } });
	QCOMPARE(stateSpy.count(), 1);
	QVERIFY(dialog.open());
	QCOMPARE(dialog.dialogId(), QStringLiteral("settings"));
	QCOMPARE(dialog.fieldValue(QStringLiteral("audio.input")).toInt(), 2);
	QCOMPARE(dialog.fieldError(QStringLiteral("audio.input")), QStringLiteral("Choose an input"));
	QVERIFY(!dialog.fieldValue(QStringLiteral("missing")).isValid());
	QCOMPARE(dialog.primaryActionId(), QStringLiteral("ok"));
	QVERIFY(dialog.loading());
	QCOMPARE(dialog.loadingScaffold(), QStringLiteral("records"));
	QCOMPARE(dialog.statusMessage(), QStringLiteral("Loading devices"));
	QCOMPARE(dialog.tone(), QStringLiteral("warning"));
	QCOMPARE(dialog.preferredWidth(), 720);
	QCOMPARE(dialog.preferredHeight(), 640);
	QCOMPARE(dialog.initialFocusId(), QStringLiteral("audio.input"));
	QCOMPARE(dialog.selectedFavoriteIndex(), 1);
	QVERIFY(dialog.editorOpen());
	QCOMPARE(dialog.editorTitle(), QStringLiteral("Edit server"));
	QCOMPARE(dialog.favorites().size(), 1);

	const QVariantMap telemetry { { QStringLiteral("available"), true },
		{ QStringLiteral("amplitude"), 73 }, { QStringLiteral("transmitting"), true } };
	const qulonglong structuralRevision = dialog.revision();
	QVERIFY(dialog.updatePresentationFieldValue(QStringLiteral("audio.input"), telemetry));
	QCOMPARE(dialog.fieldValue(QStringLiteral("audio.input")).toInt(), 2);
	QCOMPARE(dialog.presentationFieldValue(QStringLiteral("audio.input")).toMap(), telemetry);
	QCOMPARE(dialog.presentationFieldValues().value(QStringLiteral("audio.input")).toMap(), telemetry);
	QCOMPARE(dialog.revision(), structuralRevision);
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(presentationSpy.count(), 1);
	QVERIFY(!dialog.updatePresentationFieldValue(QStringLiteral("audio.input"), telemetry));
	QVERIFY(!dialog.updatePresentationFieldValue(QStringLiteral("missing"), telemetry));
	QCOMPARE(presentationSpy.count(), 1);

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

	QVariantMap nextDialogState = dialog.state();
	nextDialogState.insert(QStringLiteral("id"), QStringLiteral("settings-reopened"));
	dialog.applyState(nextDialogState);
	QVERIFY(dialog.presentationFieldValues().isEmpty());
	QCOMPARE(presentationSpy.count(), 2);
	QVERIFY(dialog.updatePresentationFieldValue(QStringLiteral("audio.input"), telemetry));
	QCOMPARE(stateSpy.count(), 2);
	QVariantMap closedState = dialog.state();
	closedState.insert(QStringLiteral("open"), false);
	dialog.applyState(closedState);
	QVERIFY(dialog.presentationFieldValues().isEmpty());
	QCOMPARE(stateSpy.count(), 3);
	QCOMPARE(presentationSpy.count(), 4);
}

void TestQmlClientModels::dialogPresentationValuesStayBoundedAndTransient() {
	DialogStateController dialog;
	QVariantList fields;
	for (int index = 0; index < 40; ++index) {
		fields.push_back(QVariantMap { { QStringLiteral("id"), QStringLiteral("meter.%1").arg(index) },
			{ QStringLiteral("type"), QStringLiteral("voiceMeter") },
			{ QStringLiteral("value"), QVariantMap { { QStringLiteral("amplitude"), 0 } } } });
	}
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("meter-grid") },
		{ QStringLiteral("sections"), QVariantList { QVariantMap { { QStringLiteral("fields"), fields } } } } });
	QSignalSpy stateSpy(&dialog, &DialogStateController::stateChanged);
	QSignalSpy presentationSpy(&dialog, &DialogStateController::presentationFieldValuesChanged);
	const qulonglong structuralRevision = dialog.revision();
	for (int index = 0; index < fields.size(); ++index) {
		QCOMPARE(dialog.updatePresentationFieldValue(QStringLiteral("meter.%1").arg(index), index), index < 32);
	}
	QCOMPARE(dialog.presentationFieldValues().size(), 32);
	QCOMPARE(dialog.revision(), structuralRevision);
	QCOMPARE(stateSpy.count(), 0);
	QCOMPARE(presentationSpy.count(), 32);

	QVariantMap replacementState = dialog.state();
	replacementState.insert(QStringLiteral("id"), QStringLiteral("replacement"));
	dialog.applyState(replacementState);
	QVERIFY(dialog.presentationFieldValues().isEmpty());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(presentationSpy.count(), 33);
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
	const QVariantMap position { { QStringLiteral("symbol"), QStringLiteral("RKLB") },
								 { QStringLiteral("quantity"), 42.0 }, { QStringLiteral("price"), 18.42 },
								 { QStringLiteral("marketValue"), 773.64 },
								 { QStringLiteral("currency"), QStringLiteral("USD") },
								 { QStringLiteral("providerId"), QStringLiteral("yahoo") },
								 { QStringLiteral("providerSymbol"), QStringLiteral("RKLB") },
								 { QStringLiteral("exchange"), QStringLiteral("Nasdaq") },
								 { QStringLiteral("quoteTime"), 1779926300ULL },
								 { QStringLiteral("quoteSourceUrl"), QStringLiteral("https://finance.yahoo.com/quote/RKLB") },
								 { QStringLiteral("quoteConfidence"), 0.9 } };
	const QVariantMap snapshot { { QStringLiteral("snapshotId"), 77 }, { QStringLiteral("userId"), 1 },
								 { QStringLiteral("currency"), QStringLiteral("USD") },
								 { QStringLiteral("totalValue"), 773.64 },
								 { QStringLiteral("positions"), QVariantList { position } } };
	const QVariantMap stonks {
		{ QStringLiteral("selectedPeriod"), QStringLiteral("30d") },
		{ QStringLiteral("periods"), QVariantList { QStringLiteral("7d"), QStringLiteral("30d") } },
		{ QStringLiteral("snapshots"), QVariantList { snapshot } },
		{ QStringLiteral("leaderboard"),
		  QVariantList { QVariantMap { { QStringLiteral("rank"), 1 }, { QStringLiteral("userId"), 2 },
									 { QStringLiteral("userName"), QStringLiteral("Trader") },
									 { QStringLiteral("returnPercent"), 12.5 } } } },
		{ QStringLiteral("feedPreferences"),
		  QVariantMap { { QStringLiteral("showMine"), true }, { QStringLiteral("showPopular"), false },
						{ QStringLiteral("showPins"), true } } }
	};
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("stonks") },
						{ QStringLiteral("kind"), QStringLiteral("stonks") },
						{ QStringLiteral("title"), QStringLiteral("Stonks") }, { QStringLiteral("stonks"), stonks } });

	QCOMPARE(dialog.kind(), QStringLiteral("stonks"));
	QCOMPARE(dialog.state().value(QStringLiteral("stonks")).toMap(), stonks);

	const QVariantList actionCases {
		QVariantMap { { QStringLiteral("action"), QStringLiteral("selectPeriod") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("period"), QStringLiteral("7d") } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("savePortfolio") },
					  { QStringLiteral("payload"),
						QVariantMap { { QStringLiteral("userId"), 1 },
									  { QStringLiteral("currency"), QStringLiteral("USD") },
									  { QStringLiteral("note"), QStringLiteral("rebalance") },
									  { QStringLiteral("positions"), QVariantList { position } } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("clearPortfolio") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("userId"), 1 },
																 { QStringLiteral("currency"), QStringLiteral("USD") } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("deleteSnapshot") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("userId"), 1 },
																 { QStringLiteral("snapshotId"), 77 } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("follow") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("userId"), 2 } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("unfollow") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("userName"), QStringLiteral("Trader") } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("setTickerPin") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("symbol"), QStringLiteral("RKLB") },
																 { QStringLiteral("pinned"), true } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("setFeedPreferences") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("showMine"), true },
																 { QStringLiteral("showPopular"), false },
																 { QStringLiteral("showPins"), true } } } },
		QVariantMap { { QStringLiteral("action"), QStringLiteral("configure") },
					  { QStringLiteral("payload"), QVariantMap { { QStringLiteral("enabled"), true },
																 { QStringLiteral("textChannelId"), 7 },
																 { QStringLiteral("socialAnnouncementsEnabled"), false } } } }
	};
	for (const QVariant &actionCaseValue : actionCases) {
		const QVariantMap actionCase = actionCaseValue.toMap();
		const QString actionID = actionCase.value(QStringLiteral("action")).toString();
		const QVariantMap payload = actionCase.value(QStringLiteral("payload")).toMap();
		dialog.invokeAction(actionID, payload);
		QCOMPARE(actionSpy.count(), 1);
		const QList< QVariant > action = actionSpy.takeFirst();
		QCOMPARE(action.at(0).toString(), QStringLiteral("stonks"));
		QCOMPARE(action.at(1).toString(), actionID);
		QCOMPARE(action.at(2).toMap(), payload);
	}
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
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("cancelling"));
	operations.cancel(QStringLiteral("plugin-update:7"));
	QCOMPARE(cancelSpy.count(), 0);

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
	operations.updateProgress(QStringLiteral("plugin-update:one"), std::numeric_limits< qint64 >::max(),
		std::numeric_limits< qint64 >::max());
	QCOMPARE(operations.get(0).value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(),
		100);
	QVERIFY(operations.updateStructuredProgress(QStringLiteral("download:one"), QStringLiteral("download"),
		0, -1, 0, std::numeric_limits< qint64 >::max(), std::numeric_limits< qint64 >::max()));
	QCOMPARE(operations.get(2).value(QStringLiteral("progress")).toInt(), 100);
	operations.updateProgress(QStringLiteral("plugin-update:two"), 5, 0);
	const QVariantMap indeterminate = operations.get(1).value(QStringLiteral("payload")).toMap();
	QCOMPARE(indeterminate.value(QStringLiteral("progress")).toInt(), -1);
	QVERIFY(indeterminate.value(QStringLiteral("indeterminate")).toBool());

	operations.interruptOperations(QStringLiteral("plugin-update:"));
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
	QCOMPARE(operations.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
	QCOMPARE(operations.get(2).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
}

void TestQmlClientModels::asyncOperationsExposeStructuredPluginResults() {
	AsyncOperationModel operations;
	operations.startStructuredOperation(QStringLiteral("plugin-update:batch"), QStringLiteral("plugin-update"),
		QStringLiteral("Updating plugins"), QStringLiteral("Preparing"), 3, true);
	QVERIFY(operations.hasOperation(QStringLiteral(" plugin-update:batch ")));
	QVariantMap row = operations.get(0);
	QCOMPARE(row.value(QStringLiteral("kind")).toString(), QStringLiteral("plugin-update"));
	QVERIFY(row.value(QStringLiteral("cancellable")).toBool());
	QCOMPARE(row.value(QStringLiteral("totalItems")).toInt(), 3);
	QCOMPARE(row.value(QStringLiteral("payload")).toMap().value(QStringLiteral("status")).toString(),
			 QStringLiteral("running"));

	QVERIFY(operations.updateStructuredProgress(QStringLiteral("plugin-update:batch"),
		QStringLiteral("download"), 1, 3, 42, 50, 100));
	row = operations.get(0);
	QCOMPARE(row.value(QStringLiteral("phase")).toString(), QStringLiteral("download"));
	QCOMPARE(row.value(QStringLiteral("completedItems")).toInt(), 1);
	QCOMPARE(row.value(QStringLiteral("currentPluginId")).toULongLong(), 42ULL);
	QCOMPARE(row.value(QStringLiteral("progress")).toInt(), 50);
	QCOMPARE(row.value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(), 50);
	QVERIFY(operations.appendItemResult(QStringLiteral("plugin-update:batch"), QStringLiteral("42"), 42,
		true, false, QString(), QStringLiteral("Updated")));
	QVERIFY(operations.appendItemResult(QStringLiteral("plugin-update:batch"), QStringLiteral("43"), 43,
		false, false, QStringLiteral("network-error"), QStringLiteral("Offline")));
	QCOMPARE(operations.get(0).value(QStringLiteral("itemResultCount")).toInt(), 2);
	QCOMPARE(operations.itemResultPage(QStringLiteral("plugin-update:batch"), 0, 64).size(), 2);

	QVERIFY(operations.updateStructuredProgress(QStringLiteral("plugin-update:batch"),
		QStringLiteral("apply-noncancellable"), 2, 3, 43, -1, -1));
	QVERIFY(!operations.get(0).value(QStringLiteral("cancellable")).toBool());
	QVERIFY(operations.finishStructuredOperation(QStringLiteral("plugin-update:batch"),
		QStringLiteral("partial"), 1, 1, 1));
	row = operations.get(0);
	QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("partial"));
	QCOMPARE(row.value(QStringLiteral("successfulItems")).toInt(), 1);
	QCOMPARE(row.value(QStringLiteral("failedItems")).toInt(), 1);
	QCOMPARE(row.value(QStringLiteral("cancelledItems")).toInt(), 1);
	QCOMPARE(row.value(QStringLiteral("completedItems")).toInt(), 3);
	QCOMPARE(row.value(QStringLiteral("totalItems")).toInt(), 3);
	QVERIFY(row.value(QStringLiteral("subtitle")).toString().contains(QStringLiteral("cancelled"),
		Qt::CaseInsensitive));
	QCOMPARE(row.value(QStringLiteral("progress")).toInt(), 100);
	QCOMPARE(row.value(QStringLiteral("phase")).toString(), QStringLiteral("finished"));
	QVERIFY(!row.value(QStringLiteral("currentPluginId")).isValid());
	QVERIFY(!operations.updateStructuredProgress(QStringLiteral("plugin-update:batch"),
		QStringLiteral("late"), 3, 3, 44, 100, 100));
	QVERIFY(!operations.appendItemResult(QStringLiteral("plugin-update:batch"), QStringLiteral("44"), 44,
		true, false, QString(), QStringLiteral("Late result")));
	QVERIFY(!operations.finishStructuredOperation(QStringLiteral("missing"), QStringLiteral("failed"), 0, 1, 0));
}

void TestQmlClientModels::asyncOperationItemResultsAreLosslessAndPaginated() {
	AsyncOperationModel operations;
	const QString operationId = QStringLiteral("plugin-update:large-batch");
	operations.startStructuredOperation(operationId, QStringLiteral("plugin-update"),
		QStringLiteral("Updating plugins"), QStringLiteral("Preparing"), 130, true);

	for (int index = 0; index < 130; ++index) {
		const bool success = index % 10 != 0;
		QVERIFY(operations.appendItemResult(operationId, QString::number(index + 1), index + 1, success, false,
			success ? QString() : QStringLiteral("load-failed"),
			success ? QStringLiteral("Updated") : QStringLiteral("Could not load")));
	}

	QCOMPARE(operations.itemResultCount(operationId), 130);
	QCOMPARE(operations.itemResultCount(operationId, true), 13);
	QVariantMap row = operations.get(0);
	QCOMPARE(row.value(QStringLiteral("itemResultCount")).toInt(), 130);
	QCOMPARE(row.value(QStringLiteral("unsuccessfulItemResultCount")).toInt(), 13);
	QCOMPARE(operations.itemResultPage(operationId, 0, 1000).size(), 64);
	QCOMPARE(operations.itemResultPage(operationId, 64, 64).size(), 64);
	const QVariantList finalPage = operations.itemResultPage(operationId, 128, 64);
	QCOMPARE(finalPage.size(), 2);
	QCOMPARE(finalPage.constLast().toMap().value(QStringLiteral("itemId")).toString(), QStringLiteral("130"));
	QCOMPARE(operations.itemResultPage(operationId, 0, 64, true).size(), 13);
	QVERIFY(operations.itemResultPage(operationId, -1, 8).isEmpty());
	QVERIFY(operations.itemResultPage(operationId, 0, 0).isEmpty());

	// Replacing a result retains stable order and total count while updating failure counts.
	QVERIFY(operations.appendItemResult(operationId, QStringLiteral("1"), 1, true, false, QString(),
		QStringLiteral("Recovered")));
	QCOMPARE(operations.itemResultCount(operationId), 130);
	QCOMPARE(operations.itemResultCount(operationId, true), 12);
	QCOMPARE(operations.itemResultPage(operationId, 0, 1).constFirst().toMap()
		.value(QStringLiteral("message")).toString(), QStringLiteral("Recovered"));

	QVERIFY(operations.finishStructuredOperation(operationId, QStringLiteral("partial"), 118, 12, 0));
	QCOMPARE(operations.itemResultPage(operationId, 128, 64).size(), 2);
	operations.dismiss(operationId);
	QCOMPARE(operations.itemResultCount(operationId), 0);
	QVERIFY(operations.itemResultPage(operationId, 0, 64).isEmpty());

	operations.startStructuredOperation(operationId, QStringLiteral("plugin-update"),
		QStringLiteral("Updating plugins"), QStringLiteral("Preparing"), 1, false);
	QVERIFY(operations.appendItemResult(operationId, QStringLiteral("new"), 7, true, false, QString(),
		QStringLiteral("Updated")));
	operations.clear();
	QCOMPARE(operations.rowCount(), 0);
	QCOMPARE(operations.itemResultCount(operationId), 0);
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

void TestQmlClientModels::selectionStateAcceptsAuthoritativeSelectionBeforeModelSync() {
	RoomModel rooms;
	ParticipantModel participants;
	QmlSelectionState selection;
	selection.bindModels(&rooms, &participants);

	// Server/controller state can lead its model patch by one event-loop turn.
	selection.applySelection(QStringLiteral("0:42"), 0, 42, {}, 42);
	QCOMPARE(selection.scopeToken(), QStringLiteral("0:42"));
	QCOMPARE(selection.scopeValue(), 0);
	QCOMPARE(selection.scopeId().toULongLong(), 42ULL);
	QCOMPARE(selection.selectedVoiceChannelId().toULongLong(), 42ULL);

	// User/QML property writes remain guarded while the row is unknown.
	selection.setScopeToken(QStringLiteral("0:404"));
	QVERIFY(selection.scopeToken().isEmpty());
	selection.applySelection(QStringLiteral("0:42"), 0, 42, {}, 42);
	selection.setSelectedVoiceChannelId(404);
	QVERIFY(!selection.selectedVoiceChannelId().isValid());
	selection.applySelection(QStringLiteral("0:42"), 0, 42, {}, 42);

	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("0:42") },
							{ QStringLiteral("label"), QStringLiteral("Landing") } } },
		{});
	QCOMPARE(selection.scopeToken(), QStringLiteral("0:42"));
	QCOMPARE(selection.selectedVoiceChannelId().toULongLong(), 42ULL);

	rooms.replaceRoomStates({}, {});
	QVERIFY(selection.scopeToken().isEmpty());
	QVERIFY(!selection.selectedVoiceChannelId().isValid());
}

void TestQmlClientModels::selectionStateInvalidatesRemovedStableIds() {
	RoomModel rooms;
	ParticipantModel participants;
	rooms.replaceRoomStates(
		{ QVariantMap{ { QStringLiteral("token"), QStringLiteral("0:42") },
						 { QStringLiteral("label"), QStringLiteral("Lobby") } } },
		{});
	participants.replaceParticipantStates(
		{ QVariantMap{ { QStringLiteral("session"), 7 }, { QStringLiteral("name"), QStringLiteral("Alice") } } });
	QmlSelectionState selection;
	selection.bindModels(&rooms, &participants);
	selection.applySelection(QStringLiteral("0:42"), 0, 42, 7, 42);

	participants.replaceParticipantStates({});
	QVERIFY(!selection.selectedUserSession().isValid());
	QCOMPARE(selection.scopeToken(), QStringLiteral("0:42"));
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

	rooms.replaceDirectMessageStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("-2:7") },
						 { QStringLiteral("session"), 7 },
						 { QStringLiteral("label"), QStringLiteral("Direct peer") } } });
	selection.setScopeToken(QStringLiteral("-2:7"));
	QCOMPARE(selection.scopeToken(), QStringLiteral("-2:7"));
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
	QSignalSpy scopeActionValueSpy(&commands, &UiCommandController::scopeActionValueRequested);
	QSignalSpy participantActionSpy(&commands, &UiCommandController::participantActionRequested);
	QSignalSpy participantActionValueSpy(&commands, &UiCommandController::participantActionValueRequested);
	commands.joinVoiceChannel(QStringLiteral("  "));
	commands.sendMessage(QStringLiteral("  "));
	commands.invokeScopeAction(QString(), QStringLiteral("join"));
	commands.invokeScopeActionValue(QString(), QStringLiteral("volume"), 80, true);
	commands.invokeParticipantAction(QStringLiteral("7"), QString());
	commands.invokeParticipantActionValue(QString(), QStringLiteral("volume"), 80, true);
	commands.invokeParticipantAction(QStringLiteral("4294967296"), QStringLiteral("message"));
	commands.invokeParticipantActionValue(QStringLiteral("18446744073709551615"), QStringLiteral("volume"), 80, true);
	QCOMPARE(joinSpy.count(), 0);
	QCOMPARE(messageSpy.count(), 0);
	QCOMPARE(scopeActionSpy.count(), 0);
	QCOMPARE(scopeActionValueSpy.count(), 0);
	QCOMPARE(participantActionSpy.count(), 0);
	QCOMPARE(participantActionValueSpy.count(), 0);
	commands.joinVoiceChannel(QStringLiteral(" channel:42 "));
	commands.sendMessage(QStringLiteral(" hello "));
	commands.invokeScopeAction(QStringLiteral(" channel:42 "), QStringLiteral(" join "));
	commands.invokeScopeActionValue(QStringLiteral(" channel:42 "), QStringLiteral(" volume "), 80, false);
	commands.invokeParticipantAction(QStringLiteral(" 7 "), QStringLiteral(" message "));
	commands.invokeParticipantActionValue(QStringLiteral(" 7 "), QStringLiteral(" volume "), 65, true);
	QCOMPARE(joinSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(messageSpy.takeFirst().at(0).toString(), QStringLiteral(" hello "));
	const QList< QVariant > scopeAction = scopeActionSpy.takeFirst();
	QCOMPARE(scopeAction.at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(scopeAction.at(1).toString(), QStringLiteral("join"));
	const QList< QVariant > scopeValueAction = scopeActionValueSpy.takeFirst();
	QCOMPARE(scopeValueAction.at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(scopeValueAction.at(1).toString(), QStringLiteral("volume"));
	QCOMPARE(scopeValueAction.at(2).toInt(), 80);
	QVERIFY(!scopeValueAction.at(3).toBool());
	const QList< QVariant > participantAction = participantActionSpy.takeFirst();
	QCOMPARE(participantAction.at(0).toString(), QStringLiteral("7"));
	QCOMPARE(participantAction.at(1).toString(), QStringLiteral("message"));
	const QList< QVariant > participantValueAction = participantActionValueSpy.takeFirst();
	QCOMPARE(participantValueAction.at(0).toString(), QStringLiteral("7"));
	QCOMPARE(participantValueAction.at(1).toString(), QStringLiteral("volume"));
	QCOMPARE(participantValueAction.at(2).toInt(), 65);
	QVERIFY(participantValueAction.at(3).toBool());
}

void TestQmlClientModels::mediaSessionLocalPlaybackControlsRemainTyped() {
	MediaSessionBackend media;
	QSignalSpy volumeChangedSpy(&media, &MediaSessionBackend::volumeChanged);
	QSignalSpy mutedChangedSpy(&media, &MediaSessionBackend::mutedChanged);
	QSignalSpy volumeRequestedSpy(&media, &MediaSessionBackend::volumeRequested);
	QSignalSpy mutedRequestedSpy(&media, &MediaSessionBackend::mutedRequested);
	QSignalSpy progressSpy(&media, &MediaSessionBackend::loadProgressChanged);

	QCOMPARE(media.volume(), 100);
	QVERIFY(!media.muted());
	QCOMPARE(media.loadProgress(), 0);
	media.setVolume(75);
	media.setVolume(75);
	QCOMPARE(media.volume(), 75);
	QCOMPARE(volumeChangedSpy.count(), 1);
	QCOMPARE(volumeRequestedSpy.count(), 0);
	media.setVolume(-10);
	QCOMPARE(media.volume(), 0);
	media.setVolume(250);
	QCOMPARE(media.volume(), 100);

	QVERIFY(media.open(QUrl(QStringLiteral("https://www.youtube.com/embed/audio")),
		QStringLiteral("youtube"), QStringLiteral("local")));
	QVERIFY(media.playbackControlAllowed());
	media.reportLoadProgress(42);
	QCOMPARE(media.loadProgress(), 42);
	QCOMPARE(progressSpy.count(), 1);
	media.reportLoadProgress(142);
	QCOMPARE(media.loadProgress(), 100);
	media.setVolume(64);
	QCOMPARE(volumeRequestedSpy.count(), 1);
	QCOMPARE(volumeRequestedSpy.takeFirst().at(0).toInt(), 64);
	media.setMuted(true);
	QCOMPARE(mutedChangedSpy.count(), 1);
	QCOMPARE(mutedRequestedSpy.count(), 1);
	QVERIFY(media.muted());
	media.toggleMuted();
	QVERIFY(!media.muted());
	QCOMPARE(mutedRequestedSpy.count(), 2);

	const QUrl sharedUrl(QStringLiteral("https://www.youtube.com/embed/shared-safe-close"));
	MediaSessionBackend shared;
	QSignalSpy sharedEventSpy(&shared, &MediaSessionBackend::sharedEventRequested);
	QSignalSpy sharedPlaySpy(&shared, &MediaSessionBackend::playRequested);
	QSignalSpy sharedSeekSpy(&shared, &MediaSessionBackend::seekRequested);
	QVERIFY(shared.startShared(sharedUrl, QStringLiteral("youtube"), QStringLiteral("Release night")));
	const QString sharedId = shared.sharedSessionId();
	shared.applySharedState(sharedId, sharedUrl, QStringLiteral("youtube"), QStringLiteral("Release night"),
		42, 17, 17, { 17 }, QStringLiteral("start"), 8.0, true, 100, 17);
	QVERIFY(shared.sharedJoined());
	QVERIFY(shared.sharedHost());
	QVERIFY(shared.playbackControlAllowed());
	QVERIFY(shared.active());
	shared.closePlayer();
	QVERIFY(!shared.active());
	QVERIFY(shared.sharedAvailable());
	QVERIFY(shared.sharedJoined());
	QCOMPARE(sharedEventSpy.count(), 0);
	shared.closePlayer();
	QVERIFY(shared.reopenSharedPlayer());
	QCOMPARE(shared.position(), 8.0);
	shared.closePlayer();
	shared.applySharedState(sharedId, sharedUrl, QStringLiteral("youtube"), QStringLiteral("Release night"),
		42, 17, 17, { 17 }, QStringLiteral("playback"), 18.0, true, 200, 17);
	QVERIFY(!shared.active());
	QVERIFY(shared.reopenSharedPlayer());
	QVERIFY(shared.active());
	QCOMPARE(shared.position(), 18.0);
	QCOMPARE(shared.state(), QStringLiteral("paused"));

	shared.applySharedState(sharedId, sharedUrl, QStringLiteral("youtube"), QStringLiteral("Release night"),
		42, 9, 9, { 9, 17 }, QStringLiteral("host-transfer"), 19.0, true, 300, 17);
	QVERIFY(!shared.sharedHost());
	QVERIFY(!shared.playbackControlAllowed());
	sharedPlaySpy.clear();
	sharedSeekSpy.clear();
	shared.play();
	shared.seek(12.0);
	QCOMPARE(sharedPlaySpy.count(), 0);
	QCOMPARE(sharedSeekSpy.count(), 0);
}

void TestQmlClientModels::mediaSessionSwitchesBetweenInlineAndDetachedPresentation() {
	MediaSessionBackend media;
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/inline-test"));
	QVERIFY(media.detached());
	QVERIFY(media.openInline(url, QStringLiteral("youtube"), QStringLiteral("message:42")));
	QVERIFY(media.active());
	QVERIFY(!media.detached());
	QCOMPARE(media.sessionId(), QStringLiteral("message:42"));

	media.detach();
	QVERIFY(media.detached());
	QVERIFY(media.active());
	media.closePlayer();
	QVERIFY(!media.active());
	QVERIFY(media.detached());

	QVERIFY(media.open(url, QStringLiteral("youtube"), QStringLiteral("message:43")));
	QVERIFY(media.detached());
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
	QVERIFY(media.playbackControllable());
	media.play();
	QCOMPARE(playSpy.count(), 1);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	media.seek(4.5);
	QCOMPARE(media.position(), 4.5);
	seekSpy.clear();
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
	seekSpy.clear();
	pauseSpy.clear();
	playSpy.clear();
	media.reportPlaybackState(24.2, 90.0, false);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/next")), QStringLiteral("youtube"),
		QStringLiteral("room:2"), 24.8, false, 43);
	QCOMPARE(seekSpy.count(), 0);
	QCOMPARE(playSpy.count(), 0);
	QCOMPARE(pauseSpy.count(), 0);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/next")), QStringLiteral("youtube"),
		QStringLiteral("room:2"), 28.0, false, 44);
	QCOMPARE(seekSpy.count(), 1);
	QCOMPARE(playSpy.count(), 0);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/next")), QStringLiteral("youtube"),
		QStringLiteral("room:2"), 28.0, true, 45);
	QCOMPARE(seekSpy.count(), 1);
	QCOMPARE(pauseSpy.count(), 1);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/stale")), QStringLiteral("youtube"),
						   QStringLiteral("stale"), 1.0, false, 41);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

void TestQmlClientModels::mediaSessionValidatesDirectMedia() {
	MediaSessionBackend media;
	const QUrl video(QStringLiteral("data:video/mp4;base64,AAAA"));
	const QUrl audio(QStringLiteral("data:audio/mp4;base64,AAAA"));
	QVERIFY(media.openDirect(video, QStringLiteral("video/mp4"), audio, QStringLiteral("audio/mp4"),
								QStringLiteral("message:7")));
	QCOMPARE(media.provider(), QStringLiteral("direct"));
	QCOMPARE(media.mediaMime(), QStringLiteral("video/mp4"));
	QCOMPARE(media.audioMime(), QStringLiteral("audio/mp4"));
	QCOMPARE(media.audioUrl(), audio);
	QVERIFY(media.isNavigationAllowed(video));
	QVERIFY(media.isNavigationAllowed(audio));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("data:text/html;base64,AAAA"))));
	media.close();
	QVERIFY(media.audioUrl().isEmpty());
	QVERIFY(media.mediaMime().isEmpty());

	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:video/mp4;base64,AAAA")),
								 QStringLiteral("video/webm"), {}, {}, QStringLiteral("message:8")));
	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:text/html;base64,AAAA")),
								 QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:9")));
	QVERIFY(media.openDirect(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=one")),
							   QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:10")));
	QVERIFY(media.isNavigationAllowed(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=one#fragment"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=two"))));
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
	QTest::newRow("legacy-provider-inferred") << QStringLiteral("direct") << QUrl(QStringLiteral("https://player.vimeo.com/video/1")) << true;
	QTest::newRow("arbitrary-direct-origin") << QStringLiteral("direct") << QUrl(QStringLiteral("https://cdn.example.com/media/a.mp4")) << false;
	QTest::newRow("unknown-provider") << QStringLiteral("unknown") << QUrl(QStringLiteral("https://www.youtube.com/embed/a")) << false;
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
	if (allowed) {
		const QString canonicalProvider = media.provider();
		const bool controllable = QSet< QString > { QStringLiteral("direct"), QStringLiteral("youtube"),
			QStringLiteral("twitch"), QStringLiteral("streamable"), QStringLiteral("vimeo"),
			QStringLiteral("dailymotion") }.contains(canonicalProvider);
		QCOMPARE(media.playbackControllable(), controllable);
		QCOMPARE(media.supportsSynchronizedPlayback(canonicalProvider), controllable);
		media.reportLoadProgress(100);
		QCOMPARE(media.loadProgress(), 100);
		QCOMPARE(media.state(), controllable ? QStringLiteral("loading") : QStringLiteral("ready"));
	}
	if (!allowed) {
		QCOMPARE(media.state(), QStringLiteral("error"));
		QVERIFY(!media.error().isEmpty());
	}
}

void TestQmlClientModels::mediaSessionNavigationAndErrorLifecycle() {
	MediaSessionBackend media;
	QSignalSpy retrySpy(&media, &MediaSessionBackend::retryRequested);
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
	media.retry();
	QCOMPARE(retrySpy.count(), 1);
	QCOMPARE(media.state(), QStringLiteral("loading"));
	QVERIFY(media.error().isEmpty());
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
	QVERIFY(media.error().isEmpty());
}

void TestQmlClientModels::mediaSessionSharedHostLifecycle() {
	MediaSessionBackend media;
	QSignalSpy startSpy(&media, &MediaSessionBackend::sharedStartRequested);
	QSignalSpy eventSpy(&media, &MediaSessionBackend::sharedEventRequested);
	QSignalSpy stateSpy(&media, &MediaSessionBackend::sharedPlaybackStateRequested);
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/shared"));
	QVERIFY(!media.startShared(QUrl(QStringLiteral("https://open.spotify.com/embed/track/12345678")),
		QStringLiteral("spotify"), QStringLiteral("Unsupported shared track")));
	QVERIFY(!media.sharedAvailable());

	QVERIFY(media.startShared(url, QStringLiteral("youtube"), QStringLiteral("Shared clip")));
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.active());
	QCOMPARE(startSpy.count(), 1);
	const QString sessionId = media.sharedSessionId();
	QVERIFY(!sessionId.isEmpty());

	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Shared clip"), 42, 17, 17,
							 { 17 }, QStringLiteral("start"), 0.0, true, 100, 17);
	QVERIFY(media.active());
	QVERIFY(media.sharedJoined());
	QVERIFY(media.sharedHost());
	QCOMPARE(media.sharedParticipantCount(), 1);
	media.reportPlaybackState(1.25, 90.0, false);
	QCOMPARE(stateSpy.count(), 1);
	media.transferSharedHost(QStringLiteral("4294967295"));
	QCOMPARE(eventSpy.count(), 1);
	QCOMPARE(eventSpy.constFirst().at(1).toString(), QStringLiteral("host-transfer"));
	media.transferSharedHost(QStringLiteral("4294967296"));
	QCOMPARE(eventSpy.count(), 1);

	media.endShared();
	QCOMPARE(eventSpy.count(), 2);
	QCOMPARE(eventSpy.constLast().at(1).toString(), QStringLiteral("end"));
	QVERIFY(!media.sharedAvailable());
	QVERIFY(!media.active());
}

void TestQmlClientModels::mediaSessionRequiresExplicitJoinForRemoteSessions() {
	MediaSessionBackend media;
	QSignalSpy eventSpy(&media, &MediaSessionBackend::sharedEventRequested);
	media.applySharedState(QStringLiteral("unsafe-session"),
		QUrl(QStringLiteral("https://cdn.example.com/media/shared.mp4")), QStringLiteral("direct"),
		QStringLiteral("Unsafe clip"), 8, 9, 9, { 9 }, QStringLiteral("start"), 0.0, true, 99, 17);
	QVERIFY(!media.sharedAvailable());
	QVERIFY(!media.active());

	const QUrl url(QStringLiteral("https://player.vimeo.com/video/123"));
	const QString sessionId = QStringLiteral("remote-session");

	media.applySharedState(sessionId, url, QStringLiteral("direct"), QStringLiteral("Remote clip"), 8, 9, 9,
							 { 9 }, QStringLiteral("start"), 3.0, false, 100, 17);
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	QVERIFY(!media.active());

	media.joinShared();
	QCOMPARE(eventSpy.count(), 1);
	QCOMPARE(eventSpy.at(0).at(1).toString(), QStringLiteral("join"));
	media.applySharedState(sessionId, url, QStringLiteral("direct"), QStringLiteral("Remote clip"), 8, 17, 9,
							 { 9, 17 }, QStringLiteral("join"), 3.0, true, 200, 17);
	QVERIFY(media.sharedJoined());
	QVERIFY(!media.sharedHost());
	QVERIFY(media.active());
	QCOMPARE(media.provider(), QStringLiteral("vimeo"));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://cdn.example.com/media/next.mp4"))));
	QVERIFY(media.isNavigationAllowed(url));
	QVERIFY(media.isNavigationAllowed(QUrl(QStringLiteral("https://player.vimeo.com/video/next"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://other.example.com/media/next.mp4"))));

	media.leaveShared();
	QCOMPARE(eventSpy.count(), 2);
	QCOMPARE(eventSpy.at(1).at(1).toString(), QStringLiteral("leave"));
	QVERIFY(!media.sharedJoined());
	QVERIFY(!media.active());
}

void TestQmlClientModels::mediaSessionRejectsConflictingPlaybackDuringSharedSession() {
	MediaSessionBackend media;
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);
	const QUrl sharedUrl(QStringLiteral("https://www.youtube.com/embed/shared"));
	const QUrl otherUrl(QStringLiteral("https://player.vimeo.com/video/other"));

	QVERIFY(media.startShared(sharedUrl, QStringLiteral("youtube"), QStringLiteral("Shared clip")));
	const QString sharedSessionId = media.sharedSessionId();
	QVERIFY(!sharedSessionId.isEmpty());
	QVERIFY(!media.open(otherUrl, QStringLiteral("vimeo"), QStringLiteral("message:other")));
	QCOMPARE(rejectionSpy.count(), 1);
	QVERIFY(!rejectionSpy.constFirst().constFirst().toString().isEmpty());
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("starting"));
	QVERIFY(media.error().isEmpty());

	media.applySharedState(sharedSessionId, sharedUrl, QStringLiteral("youtube"), QStringLiteral("Shared clip"),
						   42, 17, 17, { 17 }, QStringLiteral("start"), 8.0, true, 100, 17);
	QVERIFY(media.sharedJoined());
	QVERIFY(media.active());
	QCOMPARE(media.sessionId(), sharedSessionId);
	QCOMPARE(media.url(), sharedUrl);
	QCOMPARE(media.state(), QStringLiteral("paused"));
	QCOMPARE(media.position(), 8.0);

	QVERIFY(!media.open(otherUrl, QStringLiteral("vimeo"), QStringLiteral("message:other")));
	QCOMPARE(rejectionSpy.count(), 2);
	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:video/mp4;base64,AAAA")),
								 QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:direct")));
	QCOMPARE(rejectionSpy.count(), 3);
	QCOMPARE(media.sessionId(), sharedSessionId);
	QCOMPARE(media.url(), sharedUrl);
	QCOMPARE(media.state(), QStringLiteral("paused"));
	QCOMPARE(media.position(), 8.0);
	QVERIFY(media.error().isEmpty());

	QVERIFY(media.open(sharedUrl, QStringLiteral("youtube"), sharedSessionId));
	QCOMPARE(media.sessionId(), sharedSessionId);
	QCOMPARE(media.state(), QStringLiteral("loading"));
	QCOMPARE(rejectionSpy.count(), 3);
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
