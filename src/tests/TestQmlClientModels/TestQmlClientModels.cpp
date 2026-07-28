// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"
#include "ChatPerfTrace.h"
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <limits>
#include <tuple>

Q_DECLARE_METATYPE(PttSafetyReason)

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void largeSynchronizationsStayResetFree();
	void roomAndParticipantStatesStayIncremental();
	void connectionResetDropsRowsBeforeSameIdsAreReused();
	void navigationRailFlattensRoomsAndParticipantsIncrementally();
	void navigationRailGroupsToolTextChannelsAfterRegularTextRooms();
	void navigationRailPresentationStateStaysStableAndIncremental();
	void roomRowsExposeActionsOnlySource();
	void participantRowsPreserveTypedVoiceStateAndListenerIdentity();
	void voiceScopeParticipantsCoalescePresenceAndListenerOverlap();
	void directMessageHistoryMergePublishesOnce();
	void qmlRoomAndAvatarHydrationAvoidSynchronousUiDatabaseWork();
	void mainWindowDatabaseBlobReadsStayAsync();
	void persistentChatVideoPosterDetachesFrameBeforePreviewWorker();
	void toolsRequireNegotiatedRootAclPermission();
	void stonksRequiresNegotiatedRootAclPermission();
	void aclEntryPointsUseExplicitRoomTargets();
	void directMessageSummariesStayTypedAndIncremental();
	void directMessageControllerKeepsConversationDraftsSeparate();
	void directMessageControllerRoutesRichMessageIntents();
	void directMessageBackendUsesPrivateRichGateway();
	void directMessageTargetedUpdatesStayIncremental();
	void stableIdsRemainIndependentFromSourceMaps();
	void messageRolesExposeStructuredState();
	void activityLogRowsUseProtocolStableKeysAndSeparateLegacyBuffer();
	void chatTimelineAppliesDirectIncrementalMessages();
	void chatTimelineTracksUserHistory();
	void chatTimelineReplacesDisjointScopesWithoutMixedRows();
	void chatTimelinePreservesTypedAttachments();
	void chatTimelineSearchesConversationFields();
	void chatTimelineSearchStateTracksIncrementalMutations();
	void chatTimelineBoundsReactionDelegates();
	void chatTimelineSanitizesStructuredRichText();
	void chatTimelineParsesLinkedManagedImage();
	void chatTimelineCompletesOwnedRichParseUnderVisualOverride();
	void chatTimelineDrainsBoundedRichTextBacklog();
	void chatTimelineNormalizesPreviewAndAttachments();
	void chatTimelinePreservesStreamingManifestsAndManagedArtwork();
	void chatTimelineNormalizesProviderMetadata();
	void participantPresenceUpdatesOnlyTypedRoles();
	void participantUpsertsAndRemovalsStayResetFree();
	void workloadSourceStateRoundTripsExactly();
	void steadyStateRejectsFullBootstrap();
	void pendingBootstrapCompletesBeforeSteadyState();
	void automationProbesStayIncrementalInSteadyState();
	void duplicateStableIdsAreCoalesced();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void sessionNormalizesCollapsedNavigationSections();
	void sessionParsesManagedMotdImagesAndTracksSourceIdentity();
	void sessionParsesSemanticMotdDocument();
	void motdServerViewStateIsIsolatedByServer();
	void sessionAppliesTypedConnectionState();
	void sessionDerivesTypedMotdState();
	void sessionPublishesTypedUpdateBanner();
	void sessionPublishesTypedStonksState();
	void sessionPublishesSemanticMenus();
	void commandsRouteTypedAppActions();
	void commandsRouteActiveScopeMarkRead();
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
	void toastControllerCoalescesReplacesAndPausesTimeout();
	void asyncOperationsExposeProgressAndCancellation();
	void asyncOperationsClampProgressAndInterruptByPrefix();
	void asyncOperationsExposeStructuredPluginResults();
	void asyncOperationOverlayExcludesPluginWork();
	void asyncOperationItemResultsAreLosslessAndPaginated();
	void mediaSessionLocalPlaybackControlsRemainTyped();
	void mediaSessionSwitchesBetweenInlineAndDetachedPresentation();
	void mediaSessionAutomationLifecycleStaysInlineAndAllowlisted();
	void mediaSessionValidatesAndPublishesTypedState();
	void mediaSessionValidatesDirectMedia();
	void mediaSessionMaterializesDataSourcesWithoutStaleGenerationLeaks();
	void mediaSessionProviderAllowlist_data();
	void mediaSessionProviderAllowlist();
	void mediaSessionNavigationAndErrorLifecycle();
	void mediaSessionPublishesTypedRendererErrors();
	void mediaSessionSharedStartRequiresVoiceScope();
	void mediaSessionSharedHostLifecycle();
	void mediaSessionRequiresExplicitJoinForRemoteSessions();
	void mediaSessionRejectsStaleSharedScopeAfterRoomMove();
	void mediaSessionSharedStartTimeoutRecoversToIdle();
	void mediaSessionSharedJoinTimeoutRestoresAvailableSession();
	void mediaSessionSharedSuccessCancelsAcknowledgementTimeout();
	void mediaSessionStaleAcknowledgementTimeoutCannotAffectNewSession();
	void mediaSessionSharedClockSkewUsesReceiveOrder();
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
	QSignalSpy activitySpy(&scope, &ActiveScopeController::activityChanged);
	QSignalSpy sendSpy(&scope, &ActiveScopeController::canSendChanged);
	QSignalSpy replySpy(&scope, &ActiveScopeController::hasPendingReplyChanged);
	QSignalSpy attachFilesSpy(&scope, &ActiveScopeController::canAttachFilesChanged);
	QSignalSpy olderSpy(&scope, &ActiveScopeController::canLoadOlderChanged);
	QSignalSpy unreadSpy(&scope, &ActiveScopeController::unreadCountChanged);
	QSignalSpy canMarkReadSpy(&scope, &ActiveScopeController::canMarkReadChanged);
	QSignalSpy loadingSpy(&scope, &ActiveScopeController::loadingStateChanged);
	QSignalSpy screenShareSpy(&scope, &ActiveScopeController::screenShareChanged);
	const QVariantMap screenShare { { QStringLiteral("mode"), QStringLiteral("available") },
									 { QStringLiteral("streamId"), QStringLiteral("stream-42") } };
	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("activity"), true },
					   { QStringLiteral("canSend"), true },
					   { QStringLiteral("hasPendingReply"), true },
					   { QStringLiteral("replyActor"), QStringLiteral("Alice") },
					   { QStringLiteral("replySnippet"), QStringLiteral("Hello") },
					   { QStringLiteral("canAttachImages"), true },
					   { QStringLiteral("canAttachFiles"), true },
					   { QStringLiteral("canLoadOlder"), true },
					   { QStringLiteral("unreadCount"), 7 },
					   { QStringLiteral("canMarkRead"), true },
					   { QStringLiteral("loading"), true },
					   { QStringLiteral("loadingState"), QStringLiteral("older") },
					   { QStringLiteral("screenShare"), screenShare } });
	QCOMPARE(scope.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(scope.label(), QStringLiteral("Lobby"));
	QVERIFY(scope.activity());
	QVERIFY(scope.canSend());
	QVERIFY(scope.hasPendingReply());
	QCOMPARE(scope.replyActor(), QStringLiteral("Alice"));
	QCOMPARE(scope.replySnippet(), QStringLiteral("Hello"));
	QVERIFY(scope.canAttachImages());
	QVERIFY(scope.canAttachFiles());
	QVERIFY(scope.canLoadOlder());
	QCOMPARE(scope.unreadCount(), 7ULL);
	QVERIFY(scope.canMarkRead());
	QVERIFY(scope.loading());
	QCOMPARE(scope.loadingState(), QStringLiteral("older"));
	QCOMPARE(scope.screenShare(), screenShare);
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(activitySpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(attachFilesSpy.count(), 1);
	QCOMPARE(olderSpy.count(), 1);
	QCOMPARE(unreadSpy.count(), 1);
	QCOMPARE(canMarkReadSpy.count(), 1);
	QCOMPARE(loadingSpy.count(), 1);
	QCOMPARE(screenShareSpy.count(), 1);

	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("activity"), true },
					   { QStringLiteral("canSend"), true },
					   { QStringLiteral("hasPendingReply"), true },
					   { QStringLiteral("replyActor"), QStringLiteral("Alice") },
					   { QStringLiteral("replySnippet"), QStringLiteral("Hello") },
					   { QStringLiteral("canAttachImages"), true },
					   { QStringLiteral("canAttachFiles"), true },
					   { QStringLiteral("canLoadOlder"), true },
					   { QStringLiteral("loading"), true },
					   { QStringLiteral("loadingState"), QStringLiteral("older") },
					   { QStringLiteral("screenShare"), screenShare } });
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(activitySpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(attachFilesSpy.count(), 1);
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
	QSignalSpy prependSpy(&model, &StableListModel::rowsAboutToBePrepended);
	QStringList prependSignalOrder;
	connect(&model, &StableListModel::rowsAboutToBePrepended, this, [&prependSignalOrder](const int count) {
		prependSignalOrder.push_back(QStringLiteral("pre:%1").arg(count));
	});
	connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, this,
		[&prependSignalOrder](const QModelIndex &, const int first, const int last) {
			prependSignalOrder.push_back(QStringLiteral("insert:%1-%2").arg(first).arg(last));
		});
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
	prependSignalOrder.clear();
	model.replaceMessages(prepended);
	QCOMPARE(model.rowCount(), 10128);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(prependSpy.count(), 1);
	QCOMPARE(prependSpy.constFirst().constFirst().toInt(), 128);
	QCOMPARE(prependSignalOrder,
		QStringList({ QStringLiteral("pre:128"), QStringLiteral("insert:0-127") }));
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

void TestQmlClientModels::connectionResetDropsRowsBeforeSameIdsAreReused() {
	RoomModel rooms;
	NavigationRailModel navigation;
	ParticipantModel participants;

	const QVariantMap oldUser {
		{ QStringLiteral("participantKey"), QStringLiteral("user:7") },
		{ QStringLiteral("session"), 7 },
		{ QStringLiteral("scopeToken"), QStringLiteral("0:1") },
		{ QStringLiteral("label"), QStringLiteral("Old server user") }
	};
	const QVariantMap oldRoom {
		{ QStringLiteral("token"), QStringLiteral("0:1") },
		{ QStringLiteral("label"), QStringLiteral("Lobby") },
		{ QStringLiteral("participants"), QVariantList { oldUser } }
	};
	rooms.replaceRoomStates({ oldRoom }, {});
	navigation.replaceRoomStates({ oldRoom }, {});
	participants.replaceParticipantStates({ oldUser });
	navigation.setRoomExpanded(QStringLiteral("0:1"), false);

	rooms.clearConnectionState();
	navigation.clearConnectionState();
	participants.clear();

	QCOMPARE(rooms.rowCount(), 0);
	QCOMPARE(navigation.rowCount(), 0);
	QCOMPARE(participants.rowCount(), 0);
	QVERIFY(navigation.isRoomExpanded(QStringLiteral("0:1")));

	const QVariantMap newUser {
		{ QStringLiteral("participantKey"), QStringLiteral("user:11") },
		{ QStringLiteral("session"), 11 },
		{ QStringLiteral("scopeToken"), QStringLiteral("0:1") },
		{ QStringLiteral("label"), QStringLiteral("New server user") }
	};
	const QVariantMap newRoom {
		{ QStringLiteral("token"), QStringLiteral("0:1") },
		{ QStringLiteral("label"), QStringLiteral("Lobby") },
		{ QStringLiteral("participants"), QVariantList { newUser } }
	};
	rooms.replaceRoomStates({ newRoom }, {});
	navigation.replaceRoomStates({ newRoom }, {});
	participants.replaceParticipantStates({ newUser });

	QCOMPARE(rooms.rowCount(), 1);
	QCOMPARE(navigation.rowCount(), 2);
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(navigation.get(1).value(QStringLiteral("title")).toString(), QStringLiteral("New server user"));
	QCOMPARE(participants.get(0).value(QStringLiteral("participantSession")).toString(), QStringLiteral("11"));
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

void TestQmlClientModels::navigationRailGroupsToolTextChannelsAfterRegularTextRooms() {
	NavigationRailModel navigation;
	navigation.replaceRoomStates({ QVariantMap{ { QStringLiteral("token"), QStringLiteral("channel:1") },
												{ QStringLiteral("label"), QStringLiteral("Landing") } } },
								 { QVariantMap{ { QStringLiteral("token"), QStringLiteral("-2:0") },
												{ QStringLiteral("label"), QStringLiteral("Activity") },
												{ QStringLiteral("sectionKind"), QStringLiteral("tool") } },
								   QVariantMap{ { QStringLiteral("token"), QStringLiteral("text:7") },
												{ QStringLiteral("label"), QStringLiteral("#Chat") } },
								   QVariantMap{ { QStringLiteral("token"), QStringLiteral("text:9") },
												{ QStringLiteral("label"), QStringLiteral("#TestStuff") },
												{ QStringLiteral("sectionKind"), QStringLiteral("tool") } } });

	QCOMPARE(navigation.rowCount(), 4);
	QCOMPARE(navigation.get(0).value(QStringLiteral("sectionKind")).toString(), QStringLiteral("voice"));
	QCOMPARE(navigation.get(1).value(QStringLiteral("title")).toString(), QStringLiteral("#Chat"));
	QCOMPARE(navigation.get(1).value(QStringLiteral("sectionKind")).toString(), QStringLiteral("text"));
	QCOMPARE(navigation.get(2).value(QStringLiteral("title")).toString(), QStringLiteral("Activity"));
	QCOMPARE(navigation.get(2).value(QStringLiteral("sectionKind")).toString(), QStringLiteral("tool"));
	QCOMPARE(navigation.get(3).value(QStringLiteral("title")).toString(), QStringLiteral("#TestStuff"));
	QCOMPARE(navigation.get(3).value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
	QCOMPARE(navigation.get(3).value(QStringLiteral("sectionKind")).toString(), QStringLiteral("tool"));

	navigation.selectScopeFromRail(QStringLiteral("text:9"), QStringLiteral("text"));
	QVERIFY(navigation.get(3).value(QStringLiteral("selected")).toBool());
	QVERIFY(!navigation.get(1).value(QStringLiteral("selected")).toBool());
}

void TestQmlClientModels::navigationRailPresentationStateStaysStableAndIncremental() {
	NavigationRailModel navigation;
	QSignalSpy resetSpy(&navigation, &QAbstractItemModel::modelReset);
	QSignalSpy changedSpy(&navigation, &QAbstractItemModel::dataChanged);
	QSignalSpy insertedSpy(&navigation, &QAbstractItemModel::rowsInserted);
	QSignalSpy removedSpy(&navigation, &QAbstractItemModel::rowsRemoved);
	navigation.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("unreadCount"), 4 },
						{ QStringLiteral("participants"), QVariantList {
							QVariantMap { { QStringLiteral("session"), 42 },
								{ QStringLiteral("participantKey"), QStringLiteral("user:42") },
								{ QStringLiteral("label"), QStringLiteral("Alice") },
								{ QStringLiteral("talkState"), QStringLiteral("talking") },
								{ QStringLiteral("talking"), true } },
							QVariantMap { { QStringLiteral("session"), 77 },
								{ QStringLiteral("participantKey"), QStringLiteral("user:77") },
								{ QStringLiteral("label"), QStringLiteral("Bob") } }
						} } },
		  QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:2") },
						{ QStringLiteral("label"), QStringLiteral("Games") },
						{ QStringLiteral("participants"), QVariantList { QVariantMap {
							{ QStringLiteral("session"), 9 },
							{ QStringLiteral("participantKey"), QStringLiteral("user:9") },
							{ QStringLiteral("label"), QStringLiteral("Carol") } } } } } },
		{});
	QCOMPARE(navigation.rowCount(), 5);
	QVariantMap lobby = navigation.get(0);
	QCOMPARE(lobby.value(QStringLiteral("participantCount")).toInt(), 2);
	QCOMPARE(lobby.value(QStringLiteral("talkingParticipantCount")).toInt(), 1);
	QCOMPARE(lobby.value(QStringLiteral("unreadCount")).toInt(), 4);
	QVERIFY(lobby.value(QStringLiteral("expanded")).toBool());
	QVERIFY(lobby.value(QStringLiteral("railVisible")).toBool());
	QCOMPARE(resetSpy.count(), 0);

	changedSpy.clear();
	insertedSpy.clear();
	removedSpy.clear();
	navigation.setRoomExpanded(QStringLiteral("channel:1"), false);
	QVERIFY(!navigation.isRoomExpanded(QStringLiteral("channel:1")));
	QVERIFY(!navigation.get(0).value(QStringLiteral("expanded")).toBool());
	QVERIFY(!navigation.get(1).value(QStringLiteral("railVisible")).toBool());
	QVERIFY(!navigation.get(2).value(QStringLiteral("railVisible")).toBool());
	QCOMPARE(navigation.get(0).value(QStringLiteral("participantCount")).toInt(), 2);
	QCOMPARE(navigation.get(0).value(QStringLiteral("talkingParticipantCount")).toInt(), 1);
	QCOMPARE(navigation.get(0).value(QStringLiteral("unreadCount")).toInt(), 4);
	QVERIFY(changedSpy.count() >= 3);
	QCOMPARE(insertedSpy.count(), 0);
	QCOMPARE(removedSpy.count(), 0);
	QCOMPARE(resetSpy.count(), 0);

	changedSpy.clear();
	navigation.updatePresence(QStringLiteral("77"), QStringLiteral("talking"), QStringLiteral("Talking"),
		QStringLiteral("speaking"), true, false, {}, {});
	QCOMPARE(navigation.get(0).value(QStringLiteral("talkingParticipantCount")).toInt(), 2);
	QVERIFY(!navigation.get(2).value(QStringLiteral("railVisible")).toBool());
	QVERIFY(changedSpy.count() >= 2);
	QCOMPARE(resetSpy.count(), 0);

	changedSpy.clear();
	navigation.setFilterText(QStringLiteral(" Carol "));
	QCOMPARE(navigation.filterText(), QStringLiteral(" Carol "));
	QVERIFY(!navigation.get(0).value(QStringLiteral("railVisible")).toBool());
	QVERIFY(navigation.get(3).value(QStringLiteral("railVisible")).toBool());
	QVERIFY(navigation.get(4).value(QStringLiteral("railVisible")).toBool());
	QVERIFY(changedSpy.count() > 0);
	QCOMPARE(insertedSpy.count(), 0);
	QCOMPARE(removedSpy.count(), 0);
	QCOMPARE(resetSpy.count(), 0);

	// Collapse/filter preferences are keyed by the stable scope token and survive
	// a normal backend republish without changing row identity.
	navigation.setFilterText({});
	navigation.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby renamed") },
						{ QStringLiteral("participants"), QVariantList {
							QVariantMap { { QStringLiteral("session"), 42 },
								{ QStringLiteral("participantKey"), QStringLiteral("user:42") },
								{ QStringLiteral("label"), QStringLiteral("Alice") } } } } },
		  QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:2") },
						{ QStringLiteral("label"), QStringLiteral("Games") },
						{ QStringLiteral("participants"), QVariantList {} } } }, {});
	QVERIFY(!navigation.isRoomExpanded(QStringLiteral("channel:1")));
	QVERIFY(!navigation.get(0).value(QStringLiteral("expanded")).toBool());
	QCOMPARE(navigation.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Lobby renamed"));
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

void TestQmlClientModels::qmlRoomAndAvatarHydrationAvoidSynchronousUiDatabaseWork() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const auto methodBody = [&source](const QString &startMarker, const QString &endMarker) {
		const qsizetype start = source.indexOf(startMarker);
		const qsizetype end   = source.indexOf(endMarker, start);
		return start >= 0 && end > start ? source.mid(start, end - start) : QString();
	};

	const QString topicHelper = methodBody(
		QStringLiteral("QString voiceRoomTopicSummary"), QStringLiteral("QString voiceRoomChatDescription"));
	QVERIFY(!topicHelper.isEmpty());
	QVERIFY(!topicHelper.contains(QStringLiteral("Global::get().db")));
	QVERIFY(!topicHelper.contains(QStringLiteral("->blob(")));

	const QString roomBuilder = methodBody(
		QStringLiteral("QVariantMap MainWindow::buildQmlRoomState"), QStringLiteral("QVariantMap modernServerLogMessageState"));
	QVERIFY(!roomBuilder.isEmpty());
	QVERIFY(roomBuilder.contains(QStringLiteral("qmlVoiceRoomTopicSummary(channel)")));
	QVERIFY(roomBuilder.contains(QStringLiteral("flushQmlChannelTopicReads();")));
	QVERIFY(!roomBuilder.contains(QStringLiteral("Global::get().db->blob")));

	const QString avatarQueue = methodBody(
		QStringLiteral("void MainWindow::queueQmlAvatarHydration"), QStringLiteral("bool MainWindow::ensureUserTextureAvailable"));
	QVERIFY(!avatarQueue.isEmpty());
	QVERIFY(avatarQueue.contains(QStringLiteral("persistentChatPreviewWorkerQueue().submit")));
	QVERIFY(avatarQueue.contains(QStringLiteral("Database database(")));
	QVERIFY(avatarQueue.contains(QStringLiteral("normalizeUserTextureBytes(sourceBytes)")));
	QVERIFY(avatarQueue.contains(QStringLiteral("QStringLiteral(\"qml-database\")")));

	const QString avatarEnsure = methodBody(
		QStringLiteral("bool MainWindow::ensureUserTextureAvailable"), QStringLiteral("QString MainWindow::modernShellAvatarDataUrl"));
	QVERIFY(!avatarEnsure.isEmpty());
	QVERIFY(!avatarEnsure.contains(QStringLiteral("Global::get().db")));
	QVERIFY(!avatarEnsure.contains(QStringLiteral("QImageReader")));
	QVERIFY(!avatarEnsure.contains(QStringLiteral("QPainter")));

	const QString textureBlob = methodBody(
		QStringLiteral("void MainWindow::handleUserTextureBlob"), QStringLiteral("void MainWindow::refreshUserTextureViews"));
	QVERIFY(!textureBlob.isEmpty());
	QVERIFY(textureBlob.contains(QStringLiteral("queueQmlAvatarHydration")));
	QVERIFY(!textureBlob.contains(QStringLiteral("Global::get().db")));
	QVERIFY(!textureBlob.contains(QStringLiteral("normalizeUserTexture")));

	const QString historicalAvatar = methodBody(
		QStringLiteral("QString MainWindow::modernShellAvatarDataUrlForTextureHash"),
		QStringLiteral("QString MainWindow::modernShellActorAvatarDataUrl"));
	QVERIFY(!historicalAvatar.isEmpty());
	QVERIFY(historicalAvatar.contains(QStringLiteral("queueQmlAvatarHydration")));
	QVERIFY(!historicalAvatar.contains(QStringLiteral("Global::get().db")));
	QVERIFY(!historicalAvatar.contains(QStringLiteral("normalizeUserTexture")));
	QVERIFY(source.contains(QStringLiteral("publishPersistentChatAttachmentImageUpdate(m_qmlAvatarMessageKeys.take(textureHash))")));
}

void TestQmlClientModels::mainWindowDatabaseBlobReadsStayAsync() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const auto methodBody = [&source](const QString &startMarker, const QString &endMarker) {
		const qsizetype start = source.indexOf(startMarker);
		const qsizetype end   = source.indexOf(endMarker, start);
		return start >= 0 && end > start ? source.mid(start, end - start) : QString();
	};

	QVERIFY2(!source.contains(QStringLiteral("Global::get().db->blob(")),
		"MainWindow must not read database blobs synchronously through the UI-owned Database instance");

	const QString worker = methodBody(
		QStringLiteral("void MainWindow::flushMainWindowBlobReads"),
		QStringLiteral("void MainWindow::resolveMainWindowBlobRead"));
	QVERIFY(!worker.isEmpty());
	QVERIFY(worker.contains(QStringLiteral("persistentChatPreviewWorkerQueue().submit")));
	QVERIFY(worker.contains(QStringLiteral("Database database(")));
	QVERIFY(worker.contains(QStringLiteral("database.blob(hash)")));
	QVERIFY(worker.contains(QStringLiteral("QStringLiteral(\"qml-database\")")));
	QVERIFY(!worker.contains(QStringLiteral("QEventLoop")));
	QVERIFY(!worker.contains(QStringLiteral("waitFor")));

	const QString commentEnsure = methodBody(
		QStringLiteral("bool MainWindow::ensureUserCommentAvailable"),
		QStringLiteral("void MainWindow::cacheMainWindowBlob"));
	QVERIFY(!commentEnsure.isEmpty());
	QVERIFY(commentEnsure.contains(QStringLiteral("queueMainWindowBlobRead(expectedHash)")));
	QVERIFY(commentEnsure.contains(QStringLiteral("queueUserCommentRequest(user, expectedHash)")));
	QVERIFY(!commentEnsure.contains(QStringLiteral("->blob(")));

	const QString modernAcl = methodBody(
		QStringLiteral("void MainWindow::openModernAclRequestDialog"),
		QStringLiteral("void MainWindow::openModernAclDialog"));
	QVERIFY(!modernAcl.isEmpty());
	QVERIFY(modernAcl.contains(QStringLiteral("queueChannelAclDescriptionRead(channel, true)")));
	QVERIFY(!modernAcl.contains(QStringLiteral("->blob(")));

	const QString selfComment = methodBody(
		QStringLiteral("void MainWindow::openModernSelfCommentDialog"),
		QStringLiteral("void MainWindow::openModernKickUserDialog"));
	QVERIFY(!selfComment.isEmpty());
	QVERIFY(selfComment.contains(QStringLiteral("ensureUserCommentAvailable(user, true)")));
	QVERIFY(!selfComment.contains(QStringLiteral("->blob(")));

	const QString userComment = methodBody(
		QStringLiteral("void MainWindow::openModernUserCommentDialog"),
		QStringLiteral("void MainWindow::openModernUserCommentResetDialog"));
	QVERIFY(!userComment.isEmpty());
	QVERIFY(userComment.contains(QStringLiteral("ensureUserCommentAvailable(user, true)")));
	QVERIFY(!userComment.contains(QStringLiteral("->blob(")));

	const QString legacyAcl = methodBody(
		QStringLiteral("void MainWindow::on_qaChannelACL_triggered"),
		QStringLiteral("void MainWindow::on_qaChannelLink_triggered"));
	QVERIFY(!legacyAcl.isEmpty());
	QVERIFY(legacyAcl.contains(QStringLiteral("queueChannelAclDescriptionRead(c, false)")));
	QVERIFY(!legacyAcl.contains(QStringLiteral("->blob(")));

	const QString resolver = methodBody(
		QStringLiteral("void MainWindow::resolveMainWindowBlobRead"),
		QStringLiteral("void MainWindow::queueChannelAclDescriptionRead"));
	QVERIFY(!resolver.isEmpty());
	QVERIFY(resolver.contains(QStringLiteral("user->qbaCommentHash != hash")));
	QVERIFY(resolver.contains(QStringLiteral("m_modernDialogController->activeDialogID() != expectedDialogID")));
	QVERIFY(resolver.contains(QStringLiteral("publishQmlParticipantState(user)")));
	QVERIFY(!resolver.contains(QStringLiteral("modelReset")));
}

void TestQmlClientModels::persistentChatVideoPosterDetachesFrameBeforePreviewWorker() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());

	const qsizetype methodStart =
		source.indexOf(QStringLiteral("void MainWindow::startNextPersistentChatVideoPoster()"));
	const qsizetype methodEnd =
		source.indexOf(QStringLiteral("void MainWindow::finishPersistentChatVideoPoster("), methodStart);
	QVERIFY(methodStart >= 0);
	QVERIFY(methodEnd > methodStart);
	const QString method = source.mid(methodStart, methodEnd - methodStart);

	const qsizetype detach =
		method.indexOf(QStringLiteral("const QImage detachedFrame = frame.toImage();"));
	const qsizetype submit =
		method.indexOf(QStringLiteral("persistentChatPreviewWorkerQueue().submit< QImage >"));
	const qsizetype detachedCapture =
		method.indexOf(QStringLiteral("[detachedFrame]()"));
	QVERIFY2(detach >= 0,
		"The hardware-backed QVideoFrame must be materialized on its owning GUI thread");
	QVERIFY(submit > detach);
	QVERIFY(detachedCapture > submit);
	QVERIFY(method.contains(QStringLiteral("if (detachedFrame.isNull())")));
	QVERIFY(method.contains(QStringLiteral("persistentChatThumbnailImage(detachedFrame)")));
	QVERIFY2(!method.contains(QStringLiteral("[frame]()")),
		"A QVideoFrame captured by a preview worker can create a short-lived Qt Multimedia QRhi");
	QCOMPARE(method.count(QStringLiteral("frame.toImage()")), 1);
}

void TestQmlClientModels::toolsRequireNegotiatedRootAclPermission() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const auto methodBody = [&source](const QString &startMarker, const QString &endMarker) {
		const qsizetype start = source.indexOf(startMarker);
		const qsizetype end   = source.indexOf(endMarker, start);
		return start >= 0 && end > start ? source.mid(start, end - start) : QString();
	};

	const QString permissionGate = methodBody(
		QStringLiteral("bool modernShellToolsAclSupported()"),
		QStringLiteral("QStringList modernShellToolTextChannelSelectors()"));
	QVERIFY(!permissionGate.isEmpty());
	QVERIFY(permissionGate.contains(QStringLiteral("ForkFeatureToolsAcl")));
	QVERIFY(permissionGate.contains(QStringLiteral("ChanACL::UseTools")));

	const QString aclPermissions = methodBody(
		QStringLiteral("QVariantList modernAclPermissions"),
		QStringLiteral("void modernAclCacheOnlineUsers"));
	QVERIFY(!aclPermissions.isEmpty());
	QVERIFY(aclPermissions.contains(QStringLiteral("permission == ChanACL::UseTools")));
	QVERIFY(aclPermissions.contains(QStringLiteral("!modernShellToolsAclSupported()")));

	const QString roomState = methodBody(
		QStringLiteral("QVariantMap MainWindow::buildQmlRoomState()"),
		QStringLiteral("QVariantMap modernServerLogMessageState"));
	QVERIFY(!roomState.isEmpty());
	QVERIFY(roomState.contains(QRegularExpression(
		QStringLiteral(R"(const\s+bool\s+canUseTools\s*=\s*modernShellToolsAllowed\(\))"))));
	QVERIFY(roomState.contains(QRegularExpression(
		QStringLiteral(R"(const\s+bool\s+canUseServerLog\s*=\s*modernServerLogAvailable\(\))"))));
	QVERIFY(roomState.contains(QStringLiteral("if (canUseServerLog)")));
	QVERIFY(roomState.contains(QStringLiteral("if (debugTool && !canUseTools)")));

	const QString selection = methodBody(
		QStringLiteral("bool MainWindow::handleModernShellScopeSelection"),
		QStringLiteral("bool MainWindow::handleModernShellScopeRailSelection"));
	QVERIFY(!selection.isEmpty());
	QVERIFY(selection.contains(QStringLiteral("scopeValue == LocalServerLogScope")));
	QVERIFY(selection.contains(QStringLiteral("configuredToolTextChannel")));
	QVERIFY(selection.contains(QStringLiteral("!modernServerLogAvailable()")));
}

void TestQmlClientModels::stonksRequiresNegotiatedRootAclPermission() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const auto methodBody = [&source](const QString &startMarker, const QString &endMarker) {
		const qsizetype start = source.indexOf(startMarker);
		const qsizetype end   = source.indexOf(endMarker, start);
		return start >= 0 && end > start ? source.mid(start, end - start) : QString();
	};

	const QString permissionGate = methodBody(
		QStringLiteral("bool modernShellStonksAclSupported()"),
		QStringLiteral("QStringList modernShellToolTextChannelSelectors()"));
	QVERIFY(!permissionGate.isEmpty());
	QVERIFY(permissionGate.contains(QStringLiteral("ForkFeatureStonksAcl")));
	QVERIFY(permissionGate.contains(QStringLiteral("ForkFeatureStonksLedger")));
	QVERIFY(permissionGate.contains(QStringLiteral("ChanACL::UseStonks")));

	const QString aclPermissions = methodBody(
		QStringLiteral("QVariantList modernAclPermissions"),
		QStringLiteral("void modernAclCacheOnlineUsers"));
	QVERIFY(!aclPermissions.isEmpty());
	QVERIFY(aclPermissions.contains(QStringLiteral("permission == ChanACL::UseStonks")));
	QVERIFY(aclPermissions.contains(QStringLiteral("!modernShellStonksAclSupported()")));

	const QString headerState = methodBody(
		QStringLiteral("QVariantMap stonksHeaderState()"),
		QStringLiteral("QStringList stonksTickerSymbols"));
	QVERIFY(!headerState.isEmpty());
	QVERIFY(headerState.contains(QStringLiteral("\"accessSupported\"")));
	QVERIFY(headerState.contains(QStringLiteral("\"allowed\"")));

	const QString permissionUpdate = methodBody(
		QStringLiteral("void MainWindow::handleStonksPermissionUpdate()"),
		QStringLiteral("void MainWindow::refreshStonksTickerQuotes"));
	QVERIFY(!permissionUpdate.isEmpty());
	QVERIFY(permissionUpdate.contains(QStringLiteral("m_stonksTickerQuoteRequests.clear()")));
	QVERIFY(permissionUpdate.contains(QStringLiteral("m_stonksTickerQuoteCache.clear()")));
	QVERIFY(permissionUpdate.contains(QStringLiteral("navigateToPersistentChatScope(MumbleProto::Channel")));

	const QString roomState = methodBody(
		QStringLiteral("QVariantMap MainWindow::buildQmlRoomState()"),
		QStringLiteral("QVariantMap modernServerLogMessageState"));
	QVERIFY(!roomState.isEmpty());
	QVERIFY(roomState.contains(QStringLiteral("canUseStonks")));
	QVERIFY(roomState.contains(QStringLiteral("modernShellStonksAllowed()")));
	QVERIFY(roomState.contains(QStringLiteral("if (stonksRoom && !canUseStonks)")));

	const QString actions = methodBody(
		QStringLiteral("bool MainWindow::handleModernShellAppActionPayload"),
		QStringLiteral("bool MainWindow::hasPendingUpdateResumeState"));
	QVERIFY(!actions.isEmpty());
	QVERIFY(actions.contains(QStringLiteral("stonks.selectPeriod")));
	QVERIFY(actions.contains(QStringLiteral("stonks.follow")));
	QVERIFY(actions.contains(QStringLiteral("stonks.unfollow")));
	QVERIFY(actions.contains(QStringLiteral("!modernShellStonksAllowed()")));

	const QString messagesPath = QFINDTESTDATA("../../mumble/Messages.cpp");
	QVERIFY2(!messagesPath.isEmpty(), "Messages.cpp test data was not found");
	QFile messagesFile(messagesPath);
	QVERIFY(messagesFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString messagesSource = QString::fromUtf8(messagesFile.readAll());
	QVERIFY(messagesSource.contains(QStringLiteral("handleStonksPermissionUpdate()")));
}

void TestQmlClientModels::aclEntryPointsUseExplicitRoomTargets() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const auto methodBody = [&source](const QString &startMarker, const QString &endMarker) {
		const qsizetype start = source.indexOf(startMarker);
		const qsizetype end   = source.indexOf(endMarker, start);
		return start >= 0 && end > start ? source.mid(start, end - start) : QString();
	};

	const QString requestDialog = methodBody(
		QStringLiteral("void MainWindow::openModernAclRequestDialog"),
		QStringLiteral("void MainWindow::openModernAclDialog"));
	QVERIFY(!requestDialog.isEmpty());
	QVERIFY(requestDialog.contains(
		QStringLiteral("navigateToPersistentChatScope(MumbleProto::Channel, channel->iId, false, true)")));
	QVERIFY(requestDialog.contains(QStringLiteral("persistentTextAclChannelLabel(channel)")));
	QVERIFY(!requestDialog.contains(QStringLiteral("channel = Channel::get(Mumble::ROOT_CHANNEL_ID)")));

	const QString dispatcher = methodBody(
		QStringLiteral("bool MainWindow::handleModernShellLegacyDialogAction"),
		QStringLiteral("bool MainWindow::openModernFailedConnectionDialog"));
	QVERIFY(!dispatcher.isEmpty());
	QVERIFY(dispatcher.contains(QStringLiteral("if (action == QLatin1String(\"server.acl\"))")));
	QVERIFY(dispatcher.contains(QStringLiteral("Channel *rootChannel = Channel::get(Mumble::ROOT_CHANNEL_ID)")));
	QVERIFY(dispatcher.contains(QStringLiteral("openModernAclRequestDialog(rootChannel)")));
	QVERIFY(dispatcher.contains(QStringLiteral("if (action == QLatin1String(\"acl\"))")));
	QVERIFY(dispatcher.contains(QStringLiteral("if (!contextChannel)")));
	QVERIFY(dispatcher.contains(QStringLiteral("openModernAclRequestDialog(contextChannel)")));
	QVERIFY(!dispatcher.contains(QStringLiteral(
		"action == QLatin1String(\"server.acl\") || action == QLatin1String(\"acl\")")));

	const QString textSource = methodBody(
		QStringLiteral("void MainWindow::editPersistentTextChannelACL(const unsigned int textChannelID)"),
		QStringLiteral("void MainWindow::setDefaultPersistentTextChannel()"));
	QVERIFY(!textSource.isEmpty());
	QVERIFY(textSource.contains(QStringLiteral("Channel::get(textChannelIt->aclChannelID)")));
	QVERIFY(textSource.contains(QStringLiteral("openModernAclRequestDialog(channel)")));
	QVERIFY(!textSource.contains(QStringLiteral("cContextChannel = channel")));

	const QString scopeActions = methodBody(
		QStringLiteral("QVariantList MainWindow::buildQmlScopeActions"),
		QStringLiteral("QVariantList MainWindow::buildQmlParticipantActions"));
	QVERIFY(!scopeActions.isEmpty());
	QVERIFY(scopeActions.contains(QStringLiteral("Edit source room access...")));
	QVERIFY(scopeActions.contains(QStringLiteral("Go to source room")));
	QVERIFY(scopeActions.contains(QStringLiteral("accessSourceLabel")));

	const QString aclPermissions = methodBody(
		QStringLiteral("QVariantList modernAclPermissions"),
		QStringLiteral("void modernAclCacheOnlineUsers"));
	QVERIFY(!aclPermissions.isEmpty());
	QVERIFY(aclPermissions.contains(QStringLiteral("for (int i = 0; i < (root ? 30 : 16); ++i)")));
	QVERIFY(aclPermissions.contains(QStringLiteral("ChanACL::permName(permission)")));
}

void TestQmlClientModels::directMessageSummariesStayTypedAndIncremental() {
	DirectMessageSummaryModel model;
	QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

	model.replaceConversationStates({
		QVariantMap { { QStringLiteral("peerSession"), 7 }, { QStringLiteral("token"), QStringLiteral("-1:7") },
			{ QStringLiteral("label"), QStringLiteral("Alice") },
			{ QStringLiteral("subtitle"), QStringLiteral("In Lobby") },
			{ QStringLiteral("lastMessagePreview"), QStringLiteral("Hello") },
			{ QStringLiteral("unreadCount"), 2 }, { QStringLiteral("open"), false } },
		QVariantMap { { QStringLiteral("peerSession"), QStringLiteral("0") },
			{ QStringLiteral("label"), QStringLiteral("Invalid") } },
		QVariantMap { { QStringLiteral("peerSession"), QStringLiteral("4294967296") },
			{ QStringLiteral("label"), QStringLiteral("Too large") } }
	});
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("7"));
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Alice"));
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Hello"));
	QCOMPARE(model.get(0).value(QStringLiteral("unreadCount")).toULongLong(), 2ULL);
	QCOMPARE(insertedSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);

	model.replaceConversationStates({
		QVariantMap { { QStringLiteral("peerSession"), 7 }, { QStringLiteral("token"), QStringLiteral("-1:7") },
			{ QStringLiteral("label"), QStringLiteral("Alice") },
			{ QStringLiteral("lastMessagePreview"), QStringLiteral("Updated") },
			{ QStringLiteral("unreadCount"), 0 }, { QStringLiteral("open"), true } },
		QVariantMap { { QStringLiteral("peerSession"), 9 }, { QStringLiteral("token"), QStringLiteral("-1:9") },
			{ QStringLiteral("label"), QStringLiteral("Bob") }, { QStringLiteral("unreadCount"), 1 } }
	});
	QCOMPARE(model.rowCount(), 2);
	QVERIFY(changedSpy.count() >= 1);
	QCOMPARE(insertedSpy.count(), 2);
	QCOMPARE(resetSpy.count(), 0);

	model.replaceConversationStates({ QVariantMap { { QStringLiteral("peerSession"), 9 },
		{ QStringLiteral("label"), QStringLiteral("Bob") } } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("9"));
	QCOMPARE(removedSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::directMessageControllerKeepsConversationDraftsSeparate() {
	DirectMessageController controller;
	QSignalSpy openSpy(&controller, &DirectMessageController::openRequested);
	QSignalSpy closeSpy(&controller, &DirectMessageController::closeRequested);
	QSignalSpy readSpy(&controller, &DirectMessageController::markReadRequested);
	QSignalSpy modeSpy(&controller, &DirectMessageController::modeChangeRequested);
	QSignalSpy sendSpy(&controller, &DirectMessageController::sendRequested);
	QSignalSpy traySpy(&controller, &DirectMessageController::trayOpenChangeRequested);

	const auto conversation = [](const int session, const QString &label, const bool open) {
		return QVariantMap { { QStringLiteral("peerSession"), session },
			{ QStringLiteral("token"), QStringLiteral("-1:%1").arg(session) },
			{ QStringLiteral("label"), label }, { QStringLiteral("subtitle"), QStringLiteral("In Lobby") },
			{ QStringLiteral("open"), open }, { QStringLiteral("canSend"), true },
			{ QStringLiteral("unreadCount"), open ? 3 : 1 }, { QStringLiteral("mode"), QStringLiteral("private") },
			{ QStringLiteral("persistentHistory"), false },
			{ QStringLiteral("persistentHistoryAvailable"), true },
			{ QStringLiteral("messages"), QVariantList { QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("local:%1").arg(session) },
				{ QStringLiteral("actorName"), label }, { QStringLiteral("plainText"), QStringLiteral("Hello") },
				{ QStringLiteral("own"), false }, { QStringLiteral("createdAtMs"), 1000 } } } } };
	};

	QVariantMap alice = conversation(7, QStringLiteral("Alice"), true);
	controller.applyState({ { QStringLiteral("available"), true }, { QStringLiteral("title"), QStringLiteral("DMs") },
		{ QStringLiteral("trayOpen"), false }, { QStringLiteral("unreadTotal"), 4 },
		{ QStringLiteral("conversations"), QVariantList { alice, conversation(9, QStringLiteral("Bob"), false) } },
		{ QStringLiteral("activeConversation"), alice } });
	QVERIFY(controller.available());
	QCOMPARE(controller.summaryModel()->rowCount(), 2);
	QVERIFY(controller.conversationOpen());
	QCOMPARE(controller.activeSessionId(), QStringLiteral("7"));
	QCOMPARE(controller.activeLabel(), QStringLiteral("Alice"));
	QCOMPARE(controller.timelineModel()->rowCount(), 1);
	QCOMPARE(controller.timelineModel()->get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Hello"));

	controller.setDraft(QStringLiteral("Alice draft"));
	QCOMPARE(controller.draft(), QStringLiteral("Alice draft"));
	QVariantMap bob = conversation(9, QStringLiteral("Bob"), true);
	controller.applyState({ { QStringLiteral("available"), true },
		{ QStringLiteral("conversations"), QVariantList { conversation(7, QStringLiteral("Alice"), false), bob } },
		{ QStringLiteral("activeConversation"), bob } });
	controller.setDraft(QStringLiteral("Bob draft"));
	QCOMPARE(controller.draft(), QStringLiteral("Bob draft"));
	controller.applyState({ { QStringLiteral("available"), true },
		{ QStringLiteral("conversations"), QVariantList { alice, conversation(9, QStringLiteral("Bob"), false) } },
		{ QStringLiteral("activeConversation"), alice } });
	QCOMPARE(controller.draft(), QStringLiteral("Alice draft"));

	controller.openConversation(QStringLiteral("9"));
	controller.markRead();
	controller.setMode(QStringLiteral("history"));
	controller.sendDraft();
	controller.setTrayOpen(true);
	controller.closeConversation();
	QCOMPARE(openSpy.count(), 1);
	QCOMPARE(openSpy.first().first().toString(), QStringLiteral("9"));
	QCOMPARE(readSpy.count(), 1);
	QCOMPARE(modeSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
	QCOMPARE(sendSpy.first().at(1).toString(), QStringLiteral("Alice draft"));
	QCOMPARE(traySpy.count(), 1);
	QCOMPARE(closeSpy.count(), 1);
	controller.clearDraft();
	QVERIFY(controller.draft().isEmpty());
}

void TestQmlClientModels::voiceScopeParticipantsCoalescePresenceAndListenerOverlap() {
	const QVariantMap staleListener {
		{ QStringLiteral("participantKey"), QStringLiteral("listener:1:7") },
		{ QStringLiteral("session"), 7 },
		{ QStringLiteral("scopeToken"), QStringLiteral("channel:1") },
		{ QStringLiteral("entryKind"), QStringLiteral("listener") },
		{ QStringLiteral("label"), QStringLiteral("Alice") },
		{ QStringLiteral("isSelf"), false }
	};
	const QVariantMap presentUser {
		{ QStringLiteral("participantKey"), QStringLiteral("user:7") },
		{ QStringLiteral("session"), 7 },
		{ QStringLiteral("scopeToken"), QStringLiteral("channel:1") },
		{ QStringLiteral("entryKind"), QStringLiteral("user") },
		{ QStringLiteral("label"), QStringLiteral("Alice") },
		{ QStringLiteral("isSelf"), true }
	};

	ParticipantModel participants;
	participants.replaceParticipantStates({ staleListener, presentUser, presentUser });
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(participants.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));
	QVERIFY(participants.get(0).value(QStringLiteral("isSelf")).toBool());
	participants.clear();
	participants.upsertParticipantState(staleListener);
	participants.upsertParticipantState(presentUser);
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(participants.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));
	participants.upsertParticipantState(staleListener);
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(participants.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));

	NavigationRailModel navigation;
	QSignalSpy resetSpy(&navigation, &QAbstractItemModel::modelReset);
	navigation.replaceRoomStates({ QVariantMap {
		{ QStringLiteral("token"), QStringLiteral("channel:1") },
		{ QStringLiteral("label"), QStringLiteral("Lobby") },
		{ QStringLiteral("participants"), QVariantList { staleListener, presentUser, presentUser } }
	} }, {});
	QCOMPARE(navigation.rowCount(), 2);
	QCOMPARE(navigation.get(0).value(QStringLiteral("participantCount")).toInt(), 1);
	QCOMPARE(navigation.get(1).value(QStringLiteral("id")).toString(), QStringLiteral("user:7"));
	QVERIFY(navigation.get(1).value(QStringLiteral("isSelf")).toBool());
	QCOMPARE(resetSpy.count(), 0);

	// Listener identities remain independent when the same session listens to
	// other scopes and has no physical user row in those rooms.
	navigation.replaceRoomStates({ QVariantMap {
		{ QStringLiteral("token"), QStringLiteral("channel:2") },
		{ QStringLiteral("label"), QStringLiteral("Games") },
		{ QStringLiteral("participants"), QVariantList { QVariantMap {
			{ QStringLiteral("participantKey"), QStringLiteral("listener:2:7") },
			{ QStringLiteral("session"), 7 },
			{ QStringLiteral("scopeToken"), QStringLiteral("channel:2") },
			{ QStringLiteral("entryKind"), QStringLiteral("listener") },
			{ QStringLiteral("label"), QStringLiteral("Alice") }
		} } }
	} }, {});
	QCOMPARE(navigation.rowCount(), 2);
	QCOMPARE(navigation.get(1).value(QStringLiteral("id")).toString(), QStringLiteral("listener:2:7"));
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::directMessageControllerRoutesRichMessageIntents() {
	DirectMessageController controller;
	QSignalSpy replySpy(&controller, &DirectMessageController::messageReplyRequested);
	QSignalSpy retrySpy(&controller, &DirectMessageController::messageRetryRequested);
	QSignalSpy deleteSpy(&controller, &DirectMessageController::messageDeleteRequested);
	QSignalSpy reactionSpy(&controller, &DirectMessageController::messageReactionToggleRequested);
	QSignalSpy chooseSpy(&controller, &DirectMessageController::attachmentChooseRequested);
	QSignalSpy removeAttachmentSpy(&controller, &DirectMessageController::draftAttachmentRemoveRequested);
	QSignalSpy retryAttachmentSpy(&controller, &DirectMessageController::draftAttachmentRetryRequested);
	QSignalSpy openAttachmentSpy(&controller, &DirectMessageController::attachmentOpenRequested);
	QSignalSpy downloadAttachmentSpy(&controller, &DirectMessageController::attachmentDownloadRequested);
	QSignalSpy previewRetrySpy(&controller, &DirectMessageController::attachmentPreviewRetryRequested);
	QSignalSpy hydrationSpy(&controller, &DirectMessageController::contentHydrationRequested);
	QSignalSpy plainSendSpy(&controller, &DirectMessageController::sendRequested);
	QSignalSpy richSendSpy(&controller, &DirectMessageController::richSendRequested);

	const QVariantMap message {
		{ QStringLiteral("messageId"), 41 },
		{ QStringLiteral("actor"), QStringLiteral("Alice") },
		{ QStringLiteral("bodyText"), QStringLiteral("Earlier message") },
		{ QStringLiteral("canReply"), true },
		{ QStringLiteral("canReact"), true },
		{ QStringLiteral("canDelete"), true },
		{ QStringLiteral("deliveryState"), QStringLiteral("failed") },
		{ QStringLiteral("deliveryCanRetry"), true },
		{ QStringLiteral("attachments"), QVariantList { QVariantMap {
			{ QStringLiteral("id"), QStringLiteral("asset:52") },
			{ QStringLiteral("assetId"), 52 },
			{ QStringLiteral("kind"), QStringLiteral("image") },
			{ QStringLiteral("state"), QStringLiteral("error") },
			{ QStringLiteral("previewCanRetry"), true }
		} } }
	};
	QVariantMap conversation {
		{ QStringLiteral("peerSession"), 7 },
		{ QStringLiteral("token"), QStringLiteral("-1:7") },
		{ QStringLiteral("label"), QStringLiteral("Alice") },
		{ QStringLiteral("open"), true },
		{ QStringLiteral("canSend"), true },
		{ QStringLiteral("canAttachImages"), true },
		{ QStringLiteral("canAttachFiles"), true },
		{ QStringLiteral("draftAttachments"), QVariantList { QVariantMap {
			{ QStringLiteral("id"), QStringLiteral("draft:1") },
			{ QStringLiteral("fileName"), QStringLiteral("folder/report.pdf") },
			{ QStringLiteral("kind"), QStringLiteral("file") },
			{ QStringLiteral("status"), QStringLiteral("ready") },
			{ QStringLiteral("progress"), 0.5 }
		} } },
		{ QStringLiteral("messages"), QVariantList { message } }
	};
	controller.applyState({ { QStringLiteral("available"), true },
		{ QStringLiteral("conversations"), QVariantList { conversation } },
		{ QStringLiteral("activeConversation"), conversation } });

	QVERIFY(controller.canAttachImages());
	QVERIFY(controller.canAttachFiles());
	QCOMPARE(controller.draftAttachments().size(), 1);
	QCOMPARE(controller.draftAttachments().constFirst().toMap().value(QStringLiteral("fileName")).toString(),
		QStringLiteral("report.pdf"));

	controller.replyToMessage(QStringLiteral("41"));
	QVERIFY(controller.hasPendingReply());
	QCOMPARE(controller.pendingReplyMessageId(), QStringLiteral("41"));
	QCOMPARE(controller.pendingReplyActor(), QStringLiteral("Alice"));
	QCOMPARE(controller.pendingReplySnippet(), QStringLiteral("Earlier message"));
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(replySpy.first().at(0).toString(), QStringLiteral("7"));
	QCOMPARE(replySpy.first().at(1).toString(), QStringLiteral("41"));

	controller.retryMessage(QStringLiteral("41"));
	controller.deleteMessage(QStringLiteral("41"));
	controller.toggleMessageReaction(QStringLiteral("41"), QStringLiteral(" 👍 "));
	controller.chooseAttachment();
	controller.removeDraftAttachment(QStringLiteral("draft:1"));
	controller.retryDraftAttachment(QStringLiteral("draft:1"));
	controller.openAttachment(QStringLiteral("52"), QStringLiteral("folder/report.pdf"));
	controller.downloadAttachment(QStringLiteral("52"), QStringLiteral("folder/report.pdf"));
	controller.retryAttachmentPreview(QStringLiteral("41"), QStringLiteral("52"));
	controller.requestContentHydration(QStringLiteral("41"), true);
	QCOMPARE(retrySpy.count(), 1);
	QCOMPARE(deleteSpy.count(), 1);
	QCOMPARE(reactionSpy.count(), 1);
	QCOMPARE(reactionSpy.first().at(2).toString(), QStringLiteral("👍"));
	QCOMPARE(chooseSpy.count(), 1);
	QCOMPARE(removeAttachmentSpy.count(), 1);
	QCOMPARE(retryAttachmentSpy.count(), 1);
	QCOMPARE(openAttachmentSpy.count(), 1);
	QCOMPARE(openAttachmentSpy.first().at(1).toUInt(), 52U);
	QCOMPARE(openAttachmentSpy.first().at(2).toString(), QStringLiteral("report.pdf"));
	QCOMPARE(downloadAttachmentSpy.count(), 1);
	QCOMPARE(previewRetrySpy.count(), 1);
	QCOMPARE(previewRetrySpy.first().at(0).toString(), QStringLiteral("7"));
	QCOMPARE(previewRetrySpy.first().at(1).toString(), QStringLiteral("41"));
	QCOMPARE(previewRetrySpy.first().at(2).toUInt(), 52U);
	QCOMPARE(hydrationSpy.count(), 1);
	QCOMPARE(hydrationSpy.first().at(1).toList(), QVariantList { QStringLiteral("41") });
	QVERIFY(hydrationSpy.first().at(2).toBool());

	controller.setDraft(QStringLiteral("A rich reply"));
	controller.sendDraft();
	QCOMPARE(richSendSpy.count(), 1);
	QCOMPARE(plainSendSpy.count(), 0);
	QCOMPARE(richSendSpy.first().at(0).toString(), QStringLiteral("7"));
	QCOMPARE(richSendSpy.first().at(1).toString(), QStringLiteral("A rich reply"));
	QCOMPARE(richSendSpy.first().at(2).toString(), QStringLiteral("41"));
	QCOMPARE(richSendSpy.first().at(3).toList().size(), 1);

	controller.cancelPendingReply();
	QVERIFY(!controller.hasPendingReply());
	conversation.remove(QStringLiteral("draftAttachments"));
	controller.applyState({ { QStringLiteral("available"), true },
		{ QStringLiteral("conversations"), QVariantList { conversation } },
		{ QStringLiteral("activeConversation"), conversation } });
	controller.sendDraft();
	QCOMPARE(plainSendSpy.count(), 1);

	controller.replyToMessage(QStringLiteral("missing"));
	controller.deleteMessage(QStringLiteral("missing"));
	controller.toggleMessageReaction(QStringLiteral("missing"), QStringLiteral("👍"));
	controller.openAttachment(QStringLiteral("0"), QStringLiteral("bad"));
	QCOMPARE(replySpy.count(), 1);
	QCOMPARE(deleteSpy.count(), 1);
	QCOMPARE(reactionSpy.count(), 1);
	QCOMPARE(openAttachmentSpy.count(), 1);
}

void TestQmlClientModels::directMessageBackendUsesPrivateRichGateway() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());

	QVERIFY(source.contains(QStringLiteral("entry.persistentMessage = message")));
	QVERIFY(source.contains(QStringLiteral("message, target, canReply, canReact, false);")));
	QVERIFY(source.contains(QStringLiteral("&DirectMessageController::richSendRequested")));
	QVERIFY(source.contains(QStringLiteral("m_persistentChatGateway->send(MumbleProto::Private")));
	QVERIFY(source.contains(QStringLiteral("m_persistentChatGateway->toggleReaction(MumbleProto::Private")));
	QVERIFY(source.contains(QStringLiteral("target.scope             = MumbleProto::Private")));
	QVERIFY(source.contains(QStringLiteral("requestedScopeValue == LocalDirectMessageScope")));
	QVERIFY(source.contains(QStringLiteral("publishQmlDirectMessagesState();")));
	QVERIFY(source.contains(QStringLiteral("armModernDirectMessageDeliveryTimeout")));
	QVERIFY(source.contains(QStringLiteral("deliveryCanRetry = true")));
	QVERIFY(source.contains(QStringLiteral("publishQmlDirectMessageEntryUpdate")));
}

void TestQmlClientModels::directMessageTargetedUpdatesStayIncremental() {
	DirectMessageController controller;
	QVariantMap message { { QStringLiteral("id"), QStringLiteral("dm-local:7:1") },
		{ QStringLiteral("messageId"), 0 }, { QStringLiteral("actor"), QStringLiteral("You") },
		{ QStringLiteral("bodyText"), QStringLiteral("Hello") }, { QStringLiteral("own"), true },
		{ QStringLiteral("deliveryState"), QStringLiteral("sending") } };
	QVariantMap conversation { { QStringLiteral("peerSession"), 7 }, { QStringLiteral("open"), true },
		{ QStringLiteral("messages"), QVariantList { message } } };
	controller.applyState({ { QStringLiteral("conversations"), QVariantList { conversation } },
		{ QStringLiteral("activeConversation"), conversation } });
	QSignalSpy resetSpy(controller.timelineModel(), &QAbstractItemModel::modelReset);
	QSignalSpy changedSpy(controller.timelineModel(), &QAbstractItemModel::dataChanged);
	QSignalSpy insertedSpy(controller.timelineModel(), &QAbstractItemModel::rowsInserted);

	message.insert(QStringLiteral("deliveryState"), QStringLiteral("failed"));
	message.insert(QStringLiteral("deliveryCanRetry"), true);
	QVERIFY(controller.applyMessageState(QStringLiteral("7"), message));
	QCOMPARE(resetSpy.count(), 0);
	QVERIFY(changedSpy.count() > 0);

	QVariantMap second = message;
	second.insert(QStringLiteral("id"), QStringLiteral("dm-local:7:2"));
	QVERIFY(controller.applyMessageState(QStringLiteral("7"), second));
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(insertedSpy.count(), 1);
	QVERIFY(!controller.applyMessageState(QStringLiteral("8"), second));
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

void TestQmlClientModels::activityLogRowsUseProtocolStableKeysAndSeparateLegacyBuffer() {
	const QString sourcePath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "MainWindow.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const qsizetype helperStart = source.indexOf(QStringLiteral("QVariantMap modernServerLogMessageState"));
	const qsizetype helperEnd = source.indexOf(
		QStringLiteral("QVariantMap modernEphemeralLogMessageState"), helperStart);
	QVERIFY(helperStart >= 0);
	QVERIFY(helperEnd > helperStart);
	const QString helperBody = source.mid(helperStart, helperEnd - helperStart);
	QVERIFY(helperBody.contains(QStringLiteral("QStringLiteral(\"messageKey\")")));
	QVERIFY(!helperBody.contains(QStringLiteral("QStringLiteral(\"messageId\")")));
	QVERIFY(helperBody.contains(QStringLiteral("QStringLiteral(\"createdAtMs\")")));
	QVERIFY(helperBody.contains(QStringLiteral("QStringLiteral(\"timeLabel\")")));
	QVERIFY(!helperBody.contains(QStringLiteral("multilinePlainTextFromHtml")));
	const qsizetype ephemeralHelperEnd = source.indexOf(
		QStringLiteral("bool MainWindow::modernServerLogAvailable"), helperEnd);
	QVERIFY(ephemeralHelperEnd > helperEnd);
	const QString ephemeralHelperBody = source.mid(helperEnd, ephemeralHelperEnd - helperEnd);
	QVERIFY(ephemeralHelperBody.contains(QStringLiteral("QStringLiteral(\"session-log:%1\")")));
	QVERIFY(ephemeralHelperBody.contains(QStringLiteral("multilinePlainTextFromHtml")));
	QVERIFY(ephemeralHelperBody.contains(QStringLiteral("Session log")));
	QVERIFY(source.contains(QStringLiteral("void MainWindow::applyModernServerLogState")));
	QVERIFY(!source.contains(QStringLiteral("void MainWindow::appendModernServerLogEntry")));
	QVERIFY(source.contains(QStringLiteral("void MainWindow::appendModernEphemeralLogEntry")));
	QVERIFY(source.contains(QStringLiteral("target.serverLog ? m_modernServerLogEntries : m_modernEphemeralLogEntries")));
	const qsizetype visibleStart = source.indexOf(QStringLiteral("bool MainWindow::isModernEphemeralLogViewVisible"));
	const qsizetype visibleEnd = source.indexOf(QStringLiteral("void MainWindow::setServerLogMaximumBlockCount"),
											 visibleStart);
	QVERIFY(visibleStart >= 0 && visibleEnd > visibleStart);
	const QString visibleBody = source.mid(visibleStart, visibleEnd - visibleStart);
	QVERIFY(visibleBody.contains(QStringLiteral("ephemeralTextPath")));
	QVERIFY(!visibleBody.contains(QStringLiteral(".serverLog")));

	ChatTimelineModel model;
	model.replaceMessages({ QVariantMap {
		{ QStringLiteral("messageKey"), QStringLiteral("server-log:2") },
		{ QStringLiteral("actor"), QStringLiteral("Server") },
		{ QStringLiteral("bodyText"), QStringLiteral("<7:alice(42)> Authenticated") },
		{ QStringLiteral("createdAtMs"), 1721664529123LL },
		{ QStringLiteral("timeLabel"), QStringLiteral("16:08:49") },
		{ QStringLiteral("deliveryState"), QStringLiteral("delivered") },
		{ QStringLiteral("system"), true },
		{ QStringLiteral("canReply"), false },
		{ QStringLiteral("canReact"), false },
		{ QStringLiteral("canDelete"), false }
	} });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("server-log:2"));
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(),
			 QStringLiteral("<7:alice(42)> Authenticated"));
	QCOMPARE(model.get(0).value(QStringLiteral("timestamp")).toString(), QStringLiteral("16:08:49"));
	QVERIFY(model.get(0).value(QStringLiteral("source")).toMap().value(QStringLiteral("system")).toBool());
	QVERIFY(!model.hasUserHistory());
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

void TestQmlClientModels::chatTimelineSearchesConversationFields() {
	ChatTimelineModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy querySpy(&model, &ChatTimelineModel::queryChanged);
	QSignalSpy matchCountSpy(&model, &ChatTimelineModel::matchCountChanged);
	QSignalSpy currentMatchSpy(&model, &ChatTimelineModel::currentMatchChanged);

	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("sender") },
					  { QStringLiteral("actor"), QStringLiteral("Ålice") },
					  { QStringLiteral("bodyText"), QStringLiteral("Ordinary message") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("body") },
					  { QStringLiteral("actor"), QStringLiteral("Bob") },
					  { QStringLiteral("plainText"), QStringLiteral("Projekt Élan är klart") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("reply") },
					  { QStringLiteral("actor"), QStringLiteral("Carol") },
					  { QStringLiteral("bodyText"), QStringLiteral("Following up") },
					  { QStringLiteral("replyActor"), QStringLiteral("Zoë") },
					  { QStringLiteral("replySnippet"), QStringLiteral("Résumé attached") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("attachment") },
					  { QStringLiteral("actor"), QStringLiteral("Dora") },
					  { QStringLiteral("attachments"), QVariantList { QVariantMap {
						  { QStringLiteral("assetId"), 17 },
						  { QStringLiteral("fileName"), QStringLiteral("Årsrapport.PDF") },
						  { QStringLiteral("mime"), QStringLiteral("application/pdf") } } } } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("needle-1") },
					  { QStringLiteral("bodyText"), QStringLiteral("First needle") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("needle-2") },
					  { QStringLiteral("bodyText"), QStringLiteral("Second NEEDLE") } }
	});
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(model.query(), QString());
	QCOMPARE(model.matchCount(), 0);
	QCOMPARE(model.currentMatchIndex(), -1);
	QCOMPARE(model.currentMatchRow(), -1);
	QCOMPARE(model.currentMatchStableId(), QString());
	QCOMPARE(querySpy.count(), 0);
	QCOMPARE(matchCountSpy.count(), 0);
	QCOMPARE(currentMatchSpy.count(), 0);

	model.setQuery(QStringLiteral("åLICE"));
	QCOMPARE(model.query(), QStringLiteral("åLICE"));
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchIndex(), 0);
	QCOMPARE(model.currentMatchRow(), 0);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("sender"));
	QCOMPARE(querySpy.count(), 1);
	QCOMPARE(matchCountSpy.count(), 1);
	QCOMPARE(currentMatchSpy.count(), 1);

	model.setQuery(QStringLiteral("éLAN"));
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchRow(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("body"));
	QCOMPARE(querySpy.count(), 2);
	QCOMPARE(matchCountSpy.count(), 1);
	QCOMPARE(currentMatchSpy.count(), 2);

	model.setQuery(QStringLiteral("résumé"));
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchRow(), 2);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("reply"));

	model.setQuery(QStringLiteral("ÅRSRAPPORT.pdf"));
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchRow(), 3);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("attachment"));

	model.setQuery(QStringLiteral("needle"));
	QCOMPARE(model.matchCount(), 2);
	QCOMPARE(model.currentMatchIndex(), 0);
	QCOMPARE(model.currentMatchRow(), 4);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("needle-1"));
	QVERIFY(model.nextMatch());
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 5);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("needle-2"));
	QVERIFY(model.nextMatch());
	QCOMPARE(model.currentMatchIndex(), 0);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("needle-1"));
	QVERIFY(model.previousMatch());
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("needle-2"));
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::chatTimelineSearchStateTracksIncrementalMutations() {
	ChatTimelineModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy querySpy(&model, &ChatTimelineModel::queryChanged);
	QSignalSpy matchCountSpy(&model, &ChatTimelineModel::matchCountChanged);
	QSignalSpy currentMatchSpy(&model, &ChatTimelineModel::currentMatchChanged);

	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("a") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit alpha") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("b") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit beta") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("c") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit gamma") } }
	});
	model.setQuery(QStringLiteral("hit"));
	QCOMPARE(model.matchCount(), 3);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("a"));
	QVERIFY(model.nextMatch());
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("b"));

	QCOMPARE(model.applyMessage({ { QStringLiteral("messageKey"), QStringLiteral("b") },
								 { QStringLiteral("bodyText"), QStringLiteral("no longer matching") } }),
			 ChatTimelineModel::MessageMutation::Updated);
	QCOMPARE(model.matchCount(), 2);
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 2);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("c"));

	QCOMPARE(model.applyMessage({ { QStringLiteral("messageKey"), QStringLiteral("d") },
								 { QStringLiteral("bodyText"), QStringLiteral("another hit") } }),
			 ChatTimelineModel::MessageMutation::Inserted);
	QCOMPARE(model.matchCount(), 3);
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 2);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("c"));

	model.replaceMessages({
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("x") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit prepended") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("c") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit gamma") } },
		QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("a") },
					  { QStringLiteral("bodyText"), QStringLiteral("hit alpha") } }
	});
	QCOMPARE(model.matchCount(), 3);
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("c"));
	QVERIFY(model.removeMessage(QStringLiteral("c")));
	QCOMPARE(model.matchCount(), 2);
	QCOMPARE(model.currentMatchIndex(), 1);
	QCOMPARE(model.currentMatchRow(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("a"));

	model.replaceMessages({ QVariantMap {
		{ QStringLiteral("messageKey"), QStringLiteral("z") },
		{ QStringLiteral("bodyText"), QStringLiteral("hit replacement") } } });
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchIndex(), 0);
	QCOMPARE(model.currentMatchRow(), 0);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("z"));

	model.clear();
	QCOMPARE(model.query(), QStringLiteral("hit"));
	QCOMPARE(model.matchCount(), 0);
	QCOMPARE(model.currentMatchIndex(), -1);
	QCOMPARE(model.currentMatchRow(), -1);
	QCOMPARE(model.currentMatchStableId(), QString());
	QCOMPARE(querySpy.count(), 1);
	QVERIFY(matchCountSpy.count() > 0);
	QVERIFY(currentMatchSpy.count() > 0);

	QVERIFY(model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("after-clear") },
								  { QStringLiteral("bodyText"), QStringLiteral("hit restored") } }));
	QCOMPARE(model.matchCount(), 1);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("after-clear"));
	model.clearSearch();
	QCOMPARE(model.query(), QString());
	QCOMPARE(model.matchCount(), 0);
	QCOMPARE(model.currentMatchIndex(), -1);
	QCOMPARE(model.currentMatchRow(), -1);
	QCOMPARE(model.currentMatchStableId(), QString());
	QCOMPARE(querySpy.count(), 2);

	const int matchSignalsAfterClear = matchCountSpy.count();
	const int currentSignalsAfterClear = currentMatchSpy.count();
	QVERIFY(model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("inactive") },
								  { QStringLiteral("bodyText"), QStringLiteral("hit while inactive") } }));
	QCOMPARE(model.matchCount(), 0);
	QCOMPARE(model.currentMatchStableId(), QString());
	QCOMPARE(matchCountSpy.count(), matchSignalsAfterClear);
	QCOMPARE(currentMatchSpy.count(), currentSignalsAfterClear);
	QCOMPARE(resetSpy.count(), 0);
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
	QVERIFY(model.get(0).value(QStringLiteral("source")).toMap()
				.value(QStringLiteral("bodyHydrationPending")).toBool());
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
	QTRY_VERIFY_WITH_TIMEOUT(!model.get(0).value(QStringLiteral("source")).toMap()
								.value(QStringLiteral("bodyHydrationPending")).toBool(), 5000);
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

void TestQmlClientModels::chatTimelineParsesLinkedManagedImage() {
	ChatTimelineModel model;
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("rich-image:1") },
		  { QStringLiteral("bodyText"), QStringLiteral("Open the linked release artwork.") },
		  { QStringLiteral("bodyHtml"),
			QStringLiteral("<p><a href=\"https://example.com/qt-quick-release\">"
						   "<img src=\"image://mumble/fixture-inline?g=1\" "
						   "alt=\"Qt Quick release artwork\" width=\"640\" height=\"360\"></a></p>") } }));

	const auto parsedImage = [&model] {
		for (const QVariant &entry : model.get(0).value(QStringLiteral("bodySegments")).toList()) {
			const QVariantMap segment = entry.toMap();
			if (segment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) return segment;
		}
		return QVariantMap {};
	};
	QTRY_VERIFY_WITH_TIMEOUT(!parsedImage().isEmpty(), 5000);
	const QVariantMap image = parsedImage();
	QCOMPARE(image.value(QStringLiteral("source")).toString(),
			 QStringLiteral("image://mumble/fixture-inline?g=1"));
	QCOMPARE(image.value(QStringLiteral("href")).toString(),
			 QStringLiteral("https://example.com/qt-quick-release"));
	QCOMPARE(image.value(QStringLiteral("alt")).toString(), QStringLiteral("Qt Quick release artwork"));
	QCOMPARE(image.value(QStringLiteral("width")).toInt(), 640);
	QCOMPARE(image.value(QStringLiteral("height")).toInt(), 360);
}

void TestQmlClientModels::chatTimelineCompletesOwnedRichParseUnderVisualOverride() {
	ChatTimelineModel model;
	model.setProperty(QmlVisualFixtureMutation::OverrideProperty, true);
	model.setProperty(QmlVisualFixtureMutation::WriteProperty, true);
	model.replaceMessages({ QVariantMap {
		{ QStringLiteral("messageKey"), QStringLiteral("fixture-rich-image:1") },
		{ QStringLiteral("bodyText"), QStringLiteral("Fixture image fallback") },
		{ QStringLiteral("bodyHtml"),
		  QStringLiteral("<p><a href=\"https://example.com/fixture-owned\">"
						 "<img src=\"image://mumble/fixture-owned?g=7\" "
						 "alt=\"Fixture-owned image\"></a></p>") }
	} });
	model.setProperty(QmlVisualFixtureMutation::WriteProperty, false);
	QCOMPARE(model.rowCount(), 1);

	const auto parsedImage = [&model] {
		for (const QVariant &entry : model.get(0).value(QStringLiteral("bodySegments")).toList()) {
			const QVariantMap segment = entry.toMap();
			if (segment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) return segment;
		}
		return QVariantMap {};
	};
	QTRY_VERIFY_WITH_TIMEOUT(!parsedImage().isEmpty(), 5000);
	QCOMPARE(parsedImage().value(QStringLiteral("href")).toString(),
			 QStringLiteral("https://example.com/fixture-owned"));

	QVERIFY(!model.upsertMessage({
		{ QStringLiteral("messageKey"), QStringLiteral("live-write") },
		{ QStringLiteral("bodyText"), QStringLiteral("must remain rejected") }
	}));
	QCOMPARE(model.rowCount(), 1);
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
	const QVariantMap fileAttachment {
		{ QStringLiteral("assetId"), 77 },
		{ QStringLiteral("kind"), QStringLiteral("document") },
		{ QStringLiteral("mime"), QStringLiteral("application/pdf") },
		{ QStringLiteral("fileName"), QStringLiteral("notes.pdf") },
		{ QStringLiteral("byteSize"), 4096 }
	};
	const QVariantMap inlineAttachment {
		{ QStringLiteral("id"), QStringLiteral("message:1:inline:0") },
		{ QStringLiteral("kind"), QStringLiteral("image") },
		{ QStringLiteral("mime"), QStringLiteral("image/png") },
		{ QStringLiteral("fileName"), QStringLiteral("embedded-image.png") },
		{ QStringLiteral("inlineToken"), QStringLiteral("abcdef0123456789abcdef01") },
		{ QStringLiteral("state"), QStringLiteral("loading") },
		{ QStringLiteral("byteSize"), 598100 }
	};
	const QVariantMap invalidInlineAttachment {
		{ QStringLiteral("id"), QStringLiteral("message:1:inline:forged") },
		{ QStringLiteral("kind"), QStringLiteral("image") },
		{ QStringLiteral("inlineToken"), QStringLiteral("../../not-a-token") }
	};
	const QVariantMap retryableAttachment {
		{ QStringLiteral("id"), QStringLiteral("asset:88") },
		{ QStringLiteral("assetId"), 88 },
		{ QStringLiteral("kind"), QStringLiteral("image") },
		{ QStringLiteral("mime"), QStringLiteral("image/png") },
		{ QStringLiteral("fileName"), QStringLiteral("retry.png") },
		{ QStringLiteral("state"), QStringLiteral("error") },
		{ QStringLiteral("previewCanRetry"), true },
		{ QStringLiteral("previewErrorCode"), QStringLiteral("PREVIEW-TIMEOUT") },
		{ QStringLiteral("previewError"), QStringLiteral("The preview timed out. You can try again.") }
	};
	QVERIFY(model.upsertMessage(
		{ { QStringLiteral("messageKey"), QStringLiteral("preview:1") },
		  { QStringLiteral("previewStub"),
			QVariantMap { { QStringLiteral("url"), QStringLiteral("https://example.com/card") },
						  { QStringLiteral("host"), QStringLiteral("example.com") },
						  { QStringLiteral("loadingLabel"), QStringLiteral("Load preview") },
						  { QStringLiteral("errorDescription"), QStringLiteral("Provider metadata timed out") },
						  { QStringLiteral("embedKind"), QStringLiteral("youtube") },
						  { QStringLiteral("embedUrl"), QStringLiteral("https://www.youtube.com/embed/test") },
						  { QStringLiteral("embedAspect"), QStringLiteral("wide") },
						  { QStringLiteral("presentationFamily"), QStringLiteral("embed") },
						  { QStringLiteral("caseVariant"), QStringLiteral("youtube") } } },
		  { QStringLiteral("attachments"),
			QVariantList { validAttachment, invalidAttachment, fileAttachment, inlineAttachment,
				invalidInlineAttachment, retryableAttachment } } }));

	QVariantMap row = model.get(0);
	QVariantMap preview = row.value(QStringLiteral("preview")).toMap();
	QCOMPARE(preview.value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
	QVERIFY(preview.value(QStringLiteral("loading")).toBool());
	QCOMPARE(preview.value(QStringLiteral("embedKind")).toString(), QStringLiteral("youtube"));
	QCOMPARE(preview.value(QStringLiteral("embedUrl")).toString(),
			 QStringLiteral("https://www.youtube.com/embed/test"));
	QCOMPARE(preview.value(QStringLiteral("embedAspect")).toString(), QStringLiteral("wide"));
	QCOMPARE(preview.value(QStringLiteral("presentationFamily")).toString(), QStringLiteral("embed"));
	QCOMPARE(preview.value(QStringLiteral("caseVariant")).toString(), QStringLiteral("youtube"));
	QCOMPARE(preview.value(QStringLiteral("errorDescription")).toString(),
			 QStringLiteral("Provider metadata timed out"));
	const QVariantList attachments = row.value(QStringLiteral("attachments")).toList();
	QCOMPARE(attachments.size(), 4);
	QCOMPARE(attachments.first().toMap().value(QStringLiteral("url")).toString(),
			 QStringLiteral("image://mumble/asset:1?g=1"));
	const QVariantMap normalizedFile = attachments.at(1).toMap();
	QCOMPARE(normalizedFile.value(QStringLiteral("assetId")).toULongLong(), 77ULL);
	QCOMPARE(normalizedFile.value(QStringLiteral("kind")).toString(), QStringLiteral("document"));
	QCOMPARE(normalizedFile.value(QStringLiteral("fileName")).toString(), QStringLiteral("notes.pdf"));
	QCOMPARE(normalizedFile.value(QStringLiteral("byteSize")).toULongLong(), 4096ULL);
	QVERIFY(normalizedFile.value(QStringLiteral("url")).toString().isEmpty());
	const QVariantMap normalizedInline = attachments.at(2).toMap();
	QCOMPARE(normalizedInline.value(QStringLiteral("inlineToken")).toString(),
		QStringLiteral("abcdef0123456789abcdef01"));
	QCOMPARE(normalizedInline.value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
	QCOMPARE(normalizedInline.value(QStringLiteral("byteSize")).toULongLong(), 598100ULL);
	const QVariantMap normalizedRetry = attachments.at(3).toMap();
	QCOMPARE(normalizedRetry.value(QStringLiteral("assetId")).toULongLong(), 88ULL);
	QCOMPARE(normalizedRetry.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
	QVERIFY(normalizedRetry.value(QStringLiteral("previewCanRetry")).toBool());
	QCOMPARE(normalizedRetry.value(QStringLiteral("previewErrorCode")).toString(),
		QStringLiteral("preview-timeout"));
	QCOMPARE(normalizedRetry.value(QStringLiteral("previewError")).toString(),
		QStringLiteral("The preview timed out. You can try again."));

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
	QVERIFY(!row.value(QStringLiteral("source")).toMap().contains(QStringLiteral("preview")));
	const QVariantMap firstImageItem = preview.value(QStringLiteral("mediaItems")).toList().first().toMap();
	QVERIFY(!firstImageItem.contains(QStringLiteral("url")));
	QCOMPARE(firstImageItem.value(QStringLiteral("externalUrl")).toString(),
			 QStringLiteral("https://cdn.example.com/image-0.jpg"));
	QCOMPARE(row.value(QStringLiteral("attachments")).toList().size(), 16);
}

void TestQmlClientModels::chatTimelinePreservesStreamingManifestsAndManagedArtwork() {
	ChatTimelineModel model;
	const QVariantList mediaItems {
		QVariantMap {
			{ QStringLiteral("kind"), QStringLiteral("video") },
			{ QStringLiteral("mime"), QStringLiteral("application/vnd.apple.mpegurl") },
			{ QStringLiteral("streamKind"), QStringLiteral("hls") },
			{ QStringLiteral("url"), QStringLiteral("https://cdn.cloudflare.steamstatic.com/trailer/master.m3u8") },
			{ QStringLiteral("thumbnail"), QStringLiteral("image://mumble/steam-hls-thumb?g=1") },
			{ QStringLiteral("poster"), QStringLiteral("image://mumble/steam-hls-poster?g=2") }
		},
		QVariantMap {
			{ QStringLiteral("kind"), QStringLiteral("video") },
			{ QStringLiteral("mime"), QStringLiteral("application/dash+xml") },
			{ QStringLiteral("streamKind"), QStringLiteral("untrusted-label") },
			{ QStringLiteral("url"), QStringLiteral("https://cdn.cloudflare.steamstatic.com/trailer/manifest.mpd") },
			{ QStringLiteral("poster"), QStringLiteral("https://raw.example.test/not-managed.jpg") }
		},
		QVariantMap {
			{ QStringLiteral("kind"), QStringLiteral("video") },
			{ QStringLiteral("mime"), QStringLiteral("video/mp4") },
			{ QStringLiteral("streamKind"), QStringLiteral("bogus") },
			{ QStringLiteral("url"), QStringLiteral("https://cdn.cloudflare.steamstatic.com/trailer/movie.mp4") }
		}
	};
	QVERIFY(model.upsertMessage({
		{ QStringLiteral("messageKey"), QStringLiteral("steam:manifest") },
		{ QStringLiteral("preview"), QVariantMap {
			{ QStringLiteral("title"), QStringLiteral("Steam manifest trailers") },
			{ QStringLiteral("mediaItems"), mediaItems }
		} }
	}));
	const QVariantList normalized = model.get(0).value(QStringLiteral("preview"))
		.toMap().value(QStringLiteral("mediaItems")).toList();
	QCOMPARE(normalized.size(), 3);
	const QVariantMap hls = normalized.at(0).toMap();
	QCOMPARE(hls.value(QStringLiteral("mime")).toString(),
		QStringLiteral("application/vnd.apple.mpegurl"));
	QCOMPARE(hls.value(QStringLiteral("streamKind")).toString(), QStringLiteral("hls"));
	QCOMPARE(hls.value(QStringLiteral("thumbnail")).toString(),
		QStringLiteral("image://mumble/steam-hls-thumb?g=1"));
	QCOMPARE(hls.value(QStringLiteral("poster")).toString(),
		QStringLiteral("image://mumble/steam-hls-poster?g=2"));
	QVERIFY(hls.value(QStringLiteral("directPlayable")).toBool());

	const QVariantMap dash = normalized.at(1).toMap();
	QCOMPARE(dash.value(QStringLiteral("mime")).toString(), QStringLiteral("application/dash+xml"));
	QCOMPARE(dash.value(QStringLiteral("streamKind")).toString(), QStringLiteral("dash"));
	QVERIFY(!dash.contains(QStringLiteral("poster")));
	QVERIFY(dash.value(QStringLiteral("directPlayable")).toBool());

	const QVariantMap direct = normalized.at(2).toMap();
	QCOMPARE(direct.value(QStringLiteral("mime")).toString(), QStringLiteral("video/mp4"));
	QVERIFY(!direct.contains(QStringLiteral("streamKind")));
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
	for (const auto &[provider, key, managedSource] : {
			 std::tuple { QStringLiteral("x"), QStringLiteral("xAvatarUrl"),
				 QStringLiteral("image://mumble/x-avatar?g=11") },
			 std::tuple { QStringLiteral("instagram"), QStringLiteral("instagramAvatarUrl"),
				 QStringLiteral("image://mumble/instagram-avatar?g=12") },
			 std::tuple { QStringLiteral("github"), QStringLiteral("githubOwnerAvatarUrl"),
				 QStringLiteral("image://mumble/github-avatar?g=13") },
			 std::tuple { QStringLiteral("steam"), QStringLiteral("steamHeaderImage"),
				 QStringLiteral("image://mumble/steam-header?g=14") },
			 std::tuple { QStringLiteral("steam"), QStringLiteral("steamCapsuleImage"),
				 QStringLiteral("image://mumble/steam-capsule?g=15") } }) {
		const QVariantMap managed = normalize(withNoise({
			{ QStringLiteral("provider"), provider }, { key, managedSource }
		}));
		QCOMPARE(managed.value(key).toString(), managedSource);
		const QVariantMap rawRemote = normalize(withNoise({
			{ QStringLiteral("provider"), provider },
			{ key, QStringLiteral("https://cdn.example.test/raw-provider-image.png") }
		}));
		QVERIFY(!rawRemote.contains(key));
	}

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

	for (const QString &kind : { QStringLiteral("weather"), QStringLiteral("place"),
			 QStringLiteral("traffic") }) {
		const QVariantMap geo = normalize(withNoise({
			{ QStringLiteral("provider"), QStringLiteral("fixture-geo") },
			{ QStringLiteral("previewKind"), kind },
			{ QStringLiteral("providerName"), QStringLiteral("Fixture Geo") },
			{ QStringLiteral("locationLabel"), QStringLiteral("Stockholm, Sweden") },
			{ QStringLiteral("statusLabel"), QStringLiteral("Deterministic status") }
		}));
		QCOMPARE(geo.size(), 32);
		QCOMPARE(geo.value(QStringLiteral("previewKind")).toString(), kind);
		QCOMPARE(geo.value(QStringLiteral("locationLabel")).toString(), QStringLiteral("Stockholm, Sweden"));
		QCOMPARE(geo.value(QStringLiteral("statusLabel")).toString(), QStringLiteral("Deterministic status"));
	}

	const QVariantMap marketplace = normalize(withNoise({
		{ QStringLiteral("provider"), QStringLiteral("blocket") },
		{ QStringLiteral("previewKind"), QStringLiteral("marketplaceListing") },
		{ QStringLiteral("listingPrice"), QStringLiteral("8 500 kr") },
		{ QStringLiteral("listingCondition"), QStringLiteral("Very good condition") },
		{ QStringLiteral("listingLocation"), QStringLiteral("Stockholm") },
		{ QStringLiteral("listingSaleType"), QStringLiteral("Buy now") },
		{ QStringLiteral("listingEndsAt"), QStringLiteral("Tomorrow 18:00") },
		{ QStringLiteral("listingId"), QStringLiteral("fixture-2048") }
	}));
	QCOMPARE(marketplace.size(), 32);
	for (const QString &key : { QStringLiteral("listingPrice"), QStringLiteral("listingCondition"),
			 QStringLiteral("listingLocation"), QStringLiteral("listingSaleType"),
			 QStringLiteral("listingEndsAt"), QStringLiteral("listingId") }) {
		QVERIFY2(marketplace.contains(key),
			qPrintable(QStringLiteral("missing marketplace field %1").arg(key)));
	}

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

void TestQmlClientModels::automationProbesStayIncrementalInSteadyState() {
	const QString mainWindowPath = QFINDTESTDATA("../../mumble/MainWindow.cpp");
	const QString automationPath = QFINDTESTDATA("../../mumble/ModernUiAutomationServer.cpp");
	QVERIFY2(!mainWindowPath.isEmpty(), "MainWindow.cpp test data was not found");
	QVERIFY2(!automationPath.isEmpty(), "ModernUiAutomationServer.cpp test data was not found");

	QFile mainWindowFile(mainWindowPath);
	QFile automationFile(automationPath);
	QVERIFY(mainWindowFile.open(QIODevice::ReadOnly | QIODevice::Text));
	QVERIFY(automationFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString mainWindowSource = QString::fromUtf8(mainWindowFile.readAll());
	const QString automationSource = QString::fromUtf8(automationFile.readAll());
	QVERIFY2(!automationSource.contains(QStringLiteral("scheduleQmlShellStateSync")),
		"Automation probes must publish typed controller/model state without scheduling a full QML bootstrap");
	QVERIFY2(!automationSource.contains(QStringLiteral("syncQmlShellState(")),
		"Automation state/query/capture paths must never invoke the full QML bootstrap directly");
	for (const QString &typedPublisher : {
			 QStringLiteral("applyQmlStonksProbeState"),
			 QStringLiteral("applyQmlConnectionStateProbe"),
			 QStringLiteral("applyQmlScreenShareStateProbe"),
			 QStringLiteral("applyQmlRichPreviewProbeMessages"),
			 QStringLiteral("applyQmlMessageDeliveryProbeMessages"),
			 QStringLiteral("publishQmlDirectMessagesState"),
			 QStringLiteral("setMotdExpanded") }) {
		QVERIFY2(automationSource.contains(typedPublisher), qPrintable(
			QStringLiteral("missing typed automation publisher %1").arg(typedPublisher)));
	}

	const qsizetype helperStart =
		mainWindowSource.indexOf(QStringLiteral("void MainWindow::publishQmlAutomationChatProbeState"));
	const qsizetype helperEnd =
		mainWindowSource.indexOf(QStringLiteral("void MainWindow::publishModernShellTalkState"), helperStart);
	QVERIFY(helperStart >= 0);
	QVERIFY(helperEnd > helperStart);
	const QString helperBody = mainWindowSource.mid(helperStart, helperEnd - helperStart);
	QVERIFY(helperBody.contains(QStringLiteral("chatModel()->replaceMessages")));
	QVERIFY(!helperBody.contains(QStringLiteral("scheduleQmlShellStateSync")));
	QVERIFY(!helperBody.contains(QStringLiteral("syncQmlShellState")));
	QVERIFY(!helperBody.contains(QStringLiteral("buildQmlRoomState")));

	const auto commandBody = [&automationSource](const QString &beginToken, const QString &endToken) {
		const qsizetype start = automationSource.indexOf(beginToken);
		const qsizetype end   = automationSource.indexOf(endToken, start);
		return start >= 0 && end > start ? automationSource.mid(start, end - start) : QString();
	};
	const QString setProbeBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"setRichPreviewProbe\")"),
		QStringLiteral("if (command == QLatin1String(\"getQmlRichPreviewProbeState\")"));
	const QString queryBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"getQmlRichPreviewProbeState\")"),
		QStringLiteral("if (command == QLatin1String(\"setQmlRichPreviewProbeCardState\")"));
	const QString cardStateBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"setQmlRichPreviewProbeCardState\")"),
		QStringLiteral("if (command == QLatin1String(\"clearRichPreviewProbe\")"));
	const QString clearProbeBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"clearRichPreviewProbe\")"),
		QStringLiteral("if (command == QLatin1String(\"setMessageDeliveryProbe\")"));
	for (const QString &body : { setProbeBody, queryBody, cardStateBody, clearProbeBody }) {
		QVERIFY(!body.isEmpty());
		QVERIFY(!body.contains(QStringLiteral("scheduleQmlShellStateSync")));
		QVERIFY(!body.contains(QStringLiteral("syncQmlShellState")));
	}
	QVERIFY(setProbeBody.contains(QStringLiteral("applyQmlRichPreviewProbeMessages")));
	QVERIFY(clearProbeBody.contains(QStringLiteral("applyQmlRichPreviewProbeMessages")));

	const QString viewportBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"setHostViewport\")"),
		QStringLiteral("if (command == QLatin1String(\"captureQml\")"));
	const QString captureBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"captureQml\")"),
		QStringLiteral("if (command == QLatin1String(\"setQmlPttTool\")"));
	const QString snapshotBody = commandBody(
		QStringLiteral("if (command == QLatin1String(\"snapshot\")"),
		QStringLiteral("if (command.startsWith(QLatin1String(\"qmlPerformance\"))"));
	for (const QString &body : { viewportBody, captureBody, snapshotBody }) {
		QVERIFY(!body.isEmpty());
		QVERIFY(!body.contains(QStringLiteral("scheduleQmlShellStateSync")));
		QVERIFY(!body.contains(QStringLiteral("syncQmlShellState")));
	}
	QVERIFY(snapshotBody.contains(QStringLiteral("return buildStateResponse()")));
	const qsizetype stateResponseStart = automationSource.indexOf(
		QStringLiteral("QVariantMap ModernUiAutomationServer::buildStateResponse() const"));
	const qsizetype stateResponseEnd = automationSource.indexOf(
		QStringLiteral("bool ModernUiAutomationServer::authorizeRequest"), stateResponseStart);
	QVERIFY(stateResponseStart >= 0);
	QVERIFY(stateResponseEnd > stateResponseStart);
	const QString stateResponseBody =
		automationSource.mid(stateResponseStart, stateResponseEnd - stateResponseStart);
	QVERIFY(!stateResponseBody.contains(QStringLiteral("scheduleQmlShellStateSync")));
	QVERIFY(!stateResponseBody.contains(QStringLiteral("syncQmlShellState")));
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
	QSignalSpy monogramSpy(&session, &ClientSessionController::serverMonogramChanged);
	QSignalSpy imageSpy(&session, &ClientSessionController::serverImageUrlChanged);
	QSignalSpy motdSpy(&session, &ClientSessionController::motdHtmlChanged);
	QSignalSpy motdSegmentsSpy(&session, &ClientSessionController::motdSegmentsChanged);
	QSignalSpy motdBlocksSpy(&session, &ClientSessionController::motdBlocksChanged);
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
	QTRY_COMPARE_WITH_TIMEOUT(motdBlocksSpy.count(), 1, 5000);
	QTRY_COMPARE_WITH_TIMEOUT(session.motdBlocks().size(), 1, 5000);
	QCOMPARE(session.motdSegments().first().toMap().value(QStringLiteral("text")).toString(),
			 QStringLiteral("Welcome"));
	QCOMPARE(session.motdBlocks().first().toMap().value(QStringLiteral("kind")).toString(),
			 QStringLiteral("paragraph"));
	QCOMPARE(session.motdSummary(), QStringLiteral("Welcome"));
	session.setServerMonogram(QStringLiteral("  1M  "));
	session.setServerMonogram(QStringLiteral("1M"));
	QCOMPARE(monogramSpy.count(), 1);
	QCOMPARE(session.serverMonogram(), QStringLiteral("1M"));
	session.setServerImageUrl(QStringLiteral("https://example.com/unmanaged.png"));
	QCOMPARE(imageSpy.count(), 0);
	const QString managedImage = QStringLiteral("image://mumble/server-identity-test?g=4");
	session.setServerImageUrl(managedImage);
	session.setServerImageUrl(managedImage);
	QCOMPARE(imageSpy.count(), 1);
	QCOMPARE(session.serverImageUrl(), managedImage);
	session.applyState({ { QStringLiteral("serverMonogram"), QStringLiteral("TS") },
		{ QStringLiteral("serverImageUrl"), QString() } });
	QCOMPARE(session.serverMonogram(), QStringLiteral("TS"));
	QVERIFY(session.serverImageUrl().isEmpty());
}

void TestQmlClientModels::sessionNormalizesCollapsedNavigationSections() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::collapsedNavigationSectionsChanged);
	QVERIFY(session.collapsedNavigationSections().isEmpty());

	session.setCollapsedNavigationSections({ QStringLiteral(" TOOL "), QStringLiteral("voice"),
											QStringLiteral("unknown"), QStringLiteral("tool"),
											QStringLiteral("direct") });
	QCOMPARE(session.collapsedNavigationSections(),
			 QStringList({ QStringLiteral("voice"), QStringLiteral("direct"), QStringLiteral("tool") }));
	QCOMPARE(spy.count(), 1);

	session.setNavigationSectionExpanded(QStringLiteral("VOICE"), true);
	QCOMPARE(session.collapsedNavigationSections(),
			 QStringList({ QStringLiteral("direct"), QStringLiteral("tool") }));
	session.setNavigationSectionExpanded(QStringLiteral("text"), false);
	QCOMPARE(session.collapsedNavigationSections(),
			 QStringList({ QStringLiteral("text"), QStringLiteral("direct"), QStringLiteral("tool") }));
	QCOMPARE(spy.count(), 3);

	session.applyState({ { QStringLiteral("collapsedNavigationSections"),
						 QStringList { QStringLiteral("tool"), QStringLiteral("voice"),
										QStringLiteral("voice") } } });
	QCOMPARE(session.collapsedNavigationSections(),
			 QStringList({ QStringLiteral("voice"), QStringLiteral("tool") }));
	QCOMPARE(spy.count(), 4);
	session.setNavigationSectionExpanded(QStringLiteral("unsupported"), false);
	QCOMPARE(spy.count(), 4);
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

void TestQmlClientModels::sessionParsesSemanticMotdDocument() {
	ClientSessionController session;
	QSignalSpy blocksSpy(&session, &ClientSessionController::motdBlocksChanged);
	const QString renderedHtml = QStringLiteral(
		"<h2>Welcome aboard</h2>"
		"<p style='text-align:center'>Pick a room and say hello.</p>"
		"<ul><li><b>Be kind</b></li><li>Have fun</li></ul>"
		"<p><img src='image://mumble/motd-inline?g=2' alt='Server crest'/></p>");
	session.setMotdContent(renderedHtml, QStringLiteral("semantic motd v1"));
	QTRY_VERIFY_WITH_TIMEOUT(blocksSpy.count() >= 1, 5000);
	QTRY_COMPARE_WITH_TIMEOUT(session.motdBlocks().size(), 5, 5000);

	const QVariantList blocks = session.motdBlocks();
	QCOMPARE(blocks.at(0).toMap().value(QStringLiteral("kind")).toString(),
			 QStringLiteral("heading"));
	QCOMPARE(blocks.at(0).toMap().value(QStringLiteral("headingLevel")).toInt(), 2);
	QCOMPARE(blocks.at(1).toMap().value(QStringLiteral("kind")).toString(),
			 QStringLiteral("paragraph"));
	QCOMPARE(blocks.at(1).toMap().value(QStringLiteral("alignment")).toString(),
			 QStringLiteral("center"));
	for (int index = 2; index <= 3; ++index) {
		QCOMPARE(blocks.at(index).toMap().value(QStringLiteral("kind")).toString(),
				 QStringLiteral("list-item"));
		QVERIFY(!blocks.at(index).toMap().value(QStringLiteral("marker")).toString().isEmpty());
	}
	QCOMPARE(blocks.at(4).toMap().value(QStringLiteral("kind")).toString(),
			 QStringLiteral("image"));
	QCOMPARE(blocks.at(4).toMap().value(QStringLiteral("plainText")).toString(),
			 QStringLiteral("Server crest"));
	QCOMPARE(ModernMotd::documentBlocks(renderedHtml), blocks);
}

void TestQmlClientModels::motdServerViewStateIsIsolatedByServer() {
	const QString firstKey = ModernMotd::serverStateKey(QByteArrayLiteral("server-a"), {}, 0);
	const QString secondKey = ModernMotd::serverStateKey(QByteArrayLiteral("server-b"), {}, 0);
	QVERIFY(!firstKey.isEmpty());
	QVERIFY(firstKey != secondKey);
	QCOMPARE(ModernMotd::serverStateKey({}, QStringLiteral(" MUMBLE.EXAMPLE.COM "), 64738),
			 ModernMotd::serverStateKey({}, QStringLiteral("mumble.example.com"), 64738));

	QString serialized;
	serialized = ModernMotd::withServerViewState(serialized, firstKey,
		QVariantMap { { QStringLiteral("expanded"), true },
			{ QStringLiteral("dismissedSignature"), QStringLiteral("v1:first") },
			{ QStringLiteral("lastSeenSignature"), QStringLiteral("v1:first") } });
	QVariantMap first = ModernMotd::serverViewState(serialized, firstKey);
	QVERIFY(first.value(QStringLiteral("exists")).toBool());
	QVERIFY(first.value(QStringLiteral("expanded")).toBool());
	QCOMPARE(first.value(QStringLiteral("dismissedSignature")).toString(), QStringLiteral("v1:first"));
	QVERIFY(!ModernMotd::serverViewState(serialized, secondKey)
		.value(QStringLiteral("exists")).toBool());

	serialized = ModernMotd::withServerViewState(serialized, secondKey,
		QVariantMap { { QStringLiteral("expanded"), false },
			{ QStringLiteral("dismissedSignature"), QStringLiteral("v1:second") },
			{ QStringLiteral("lastSeenSignature"), QStringLiteral("v1:second") } });
	first = ModernMotd::serverViewState(serialized, firstKey);
	const QVariantMap second = ModernMotd::serverViewState(serialized, secondKey);
	QCOMPARE(first.value(QStringLiteral("dismissedSignature")).toString(), QStringLiteral("v1:first"));
	QCOMPARE(second.value(QStringLiteral("dismissedSignature")).toString(), QStringLiteral("v1:second"));
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

void TestQmlClientModels::commandsRouteActiveScopeMarkRead() {
	UiCommandController commands;
	QSignalSpy markReadSpy(&commands, &UiCommandController::activeScopeMarkReadRequested);
	commands.markActiveScopeRead();
	QCOMPARE(markReadSpy.count(), 1);
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
	QSignalSpy attachmentOpenSpy(&commands, &UiCommandController::chatAttachmentOpenRequested);
	QSignalSpy attachmentDownloadSpy(&commands, &UiCommandController::chatAttachmentDownloadRequested);
	QSignalSpy attachmentImageSpy(&commands, &UiCommandController::chatAttachmentImageRequested);
	QSignalSpy inlineImageRequestSpy(&commands, &UiCommandController::chatInlineImageRequested);
	QSignalSpy attachmentPreviewRetrySpy(&commands,
		&UiCommandController::chatAttachmentPreviewRetryRequested);
	QSignalSpy inlineImageSaveSpy(&commands, &UiCommandController::chatInlineImageSaveRequested);
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
	commands.downloadChatAttachment(QStringLiteral("0"), QStringLiteral("ignored.bin"));
	commands.downloadChatAttachment(QStringLiteral("4294967296"), QStringLiteral("ignored.bin"));
	commands.openChatAttachment(QStringLiteral("0"), QStringLiteral("ignored.pdf"));
	commands.openChatAttachment(QStringLiteral("4294967296"), QStringLiteral("ignored.pdf"));
	commands.requestChatAttachmentImage(QStringLiteral("42"), QStringLiteral("0"));
	commands.requestChatAttachmentImage(QStringLiteral("4294967296"), QStringLiteral("12"));
	commands.requestChatInlineImage(QStringLiteral("bad"), QStringLiteral("12"));
	commands.requestChatInlineImage(QStringLiteral("abcdef0123456789abcdef01"), QStringLiteral("0"));
	commands.retryChatAttachmentPreview(QStringLiteral("channel:1"), QStringLiteral("0"), QStringLiteral("42"));
	commands.retryChatAttachmentPreview(QString(), QStringLiteral("12"), QStringLiteral("42"));
	commands.retryChatAttachmentPreview(QStringLiteral("channel:1"), QStringLiteral("12"),
		QStringLiteral("4294967296"));
	commands.saveChatInlineImage(QStringLiteral("../../bad"), QStringLiteral("ignored.png"));
	QCOMPARE(scopeSpy.count(), 0);
	QCOMPARE(railScopeSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(participantSpy.count(), 0);
	QCOMPARE(directMessageSpy.count(), 0);
	QCOMPARE(replySpy.count(), 0);
	QCOMPARE(retrySpy.count(), 0);
	QCOMPARE(deleteSpy.count(), 0);
	QCOMPARE(reactionSpy.count(), 0);
	QCOMPARE(attachmentOpenSpy.count(), 0);
	QCOMPARE(attachmentDownloadSpy.count(), 0);
	QCOMPARE(attachmentImageSpy.count(), 0);
	QCOMPARE(inlineImageRequestSpy.count(), 0);
	QCOMPARE(attachmentPreviewRetrySpy.count(), 0);
	QCOMPARE(inlineImageSaveSpy.count(), 0);
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
	commands.openChatAttachment(QStringLiteral(" 78 "), QStringLiteral("folder/report.pdf"));
	commands.downloadChatAttachment(QStringLiteral(" 77 "), QStringLiteral("folder/notes.pdf"));
	commands.requestChatAttachmentImage(QStringLiteral(" 76 "), QStringLiteral(" 13 "));
	commands.requestChatInlineImage(QStringLiteral(" ABCDEF0123456789ABCDEF01 "), QStringLiteral(" 14 "));
	commands.retryChatAttachmentPreview(QStringLiteral(" channel:42 "), QStringLiteral(" 12 "),
		QStringLiteral(" 88 "));
	commands.saveChatInlineImage(QStringLiteral(" ABCDEF0123456789ABCDEF01 "),
		QStringLiteral("folder/embedded.png"));
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
	QCOMPARE(attachmentOpenSpy.count(), 1);
	QCOMPARE(attachmentOpenSpy.first().at(0).toUInt(), 78U);
	QCOMPARE(attachmentOpenSpy.first().at(1).toString(), QStringLiteral("report.pdf"));
	QCOMPARE(attachmentDownloadSpy.count(), 1);
	QCOMPARE(attachmentDownloadSpy.first().at(0).toUInt(), 77U);
	QCOMPARE(attachmentDownloadSpy.first().at(1).toString(), QStringLiteral("notes.pdf"));
	QCOMPARE(attachmentImageSpy.count(), 1);
	QCOMPARE(attachmentImageSpy.first().at(0).toUInt(), 76U);
	QCOMPARE(attachmentImageSpy.first().at(1).toString(), QStringLiteral("13"));
	QCOMPARE(inlineImageRequestSpy.count(), 1);
	QCOMPARE(inlineImageRequestSpy.first().at(0).toString(), QStringLiteral("abcdef0123456789abcdef01"));
	QCOMPARE(inlineImageRequestSpy.first().at(1).toString(), QStringLiteral("14"));
	QCOMPARE(attachmentPreviewRetrySpy.count(), 1);
	QCOMPARE(attachmentPreviewRetrySpy.first().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(attachmentPreviewRetrySpy.first().at(1).toString(), QStringLiteral("12"));
	QCOMPARE(attachmentPreviewRetrySpy.first().at(2).toUInt(), 88U);
	QCOMPARE(inlineImageSaveSpy.count(), 1);
	QCOMPARE(inlineImageSaveSpy.first().at(0).toString(), QStringLiteral("abcdef0123456789abcdef01"));
	QCOMPARE(inlineImageSaveSpy.first().at(1).toString(), QStringLiteral("embedded.png"));
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

void TestQmlClientModels::toastControllerCoalescesReplacesAndPausesTimeout() {
	ToastController toast;
	QSignalSpy stateSpy(&toast, &ToastController::stateChanged);

	toast.publish(QStringLiteral("error"), QStringLiteral("Upload failed"),
		QStringLiteral("The network is unavailable."), QStringLiteral("retry"), QStringLiteral("Retry"), 300);
	QVERIFY(toast.visible());
	QCOMPARE(toast.tone(), QStringLiteral("danger"));
	QCOMPARE(toast.title(), QStringLiteral("Upload failed"));
	QCOMPARE(toast.message(), QStringLiteral("The network is unavailable."));
	QCOMPARE(toast.actionId(), QStringLiteral("retry"));
	QCOMPARE(toast.actionLabel(), QStringLiteral("Retry"));
	QCOMPARE(toast.repeatCount(), 1);
	const qulonglong firstRevision = toast.revision();

	toast.publish(QStringLiteral("danger"), QStringLiteral("Upload failed"),
		QStringLiteral("The network is unavailable."), QStringLiteral("retry"), QStringLiteral("Retry"), 300);
	QCOMPARE(toast.repeatCount(), 2);
	QVERIFY(toast.revision() > firstRevision);

	toast.setInteractionActive(true);
	QTest::qWait(350);
	QVERIFY(toast.visible());
	toast.setInteractionActive(false);
	QTRY_VERIFY_WITH_TIMEOUT(!toast.visible(), 500);

	toast.publish(QStringLiteral("unknown"), QStringLiteral("Connected"), QString(), {}, {}, 1000);
	QVERIFY(toast.visible());
	QCOMPARE(toast.tone(), QStringLiteral("info"));
	QCOMPARE(toast.repeatCount(), 1);
	toast.publish(QStringLiteral("success"), QStringLiteral("Saved"), QStringLiteral("Settings updated."));
	QCOMPARE(toast.tone(), QStringLiteral("success"));
	QCOMPARE(toast.title(), QStringLiteral("Saved"));
	QCOMPARE(toast.repeatCount(), 1);
	toast.dismiss();
	QVERIFY(!toast.visible());
	QCOMPARE(toast.repeatCount(), 0);
	QVERIFY(stateSpy.count() >= 6);
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
	QVERIFY(!operations.hasOperation(QStringLiteral("plugin-update:one")));
	QVERIFY(!operations.hasOperation(QStringLiteral("plugin-update:two")));
	QCOMPARE(operations.rowCount(), 1);
	QCOMPARE(operations.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("download:one"));
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
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

void TestQmlClientModels::asyncOperationOverlayExcludesPluginWork() {
	AsyncOperationModel operations;
	AsyncOperationOverlayProxyModel overlay;
	overlay.setSourceModel(&operations);

	operations.startStructuredOperation(QStringLiteral("plugin-update-check:startup"),
		QStringLiteral("plugin-update-check"), QStringLiteral("Checking plugin updates"),
		QStringLiteral("Contacting the plugin update service"), 1, false);
	QCOMPARE(operations.rowCount(), 1);
	QCOMPARE(overlay.rowCount(), 0);

	operations.startOperation(QStringLiteral("chat-attachment-save:1"), QStringLiteral("Saving attachment"),
		QStringLiteral("Writing the selected file"), false);
	QCOMPARE(operations.rowCount(), 2);
	QCOMPARE(overlay.rowCount(), 1);

	QVERIFY(operations.finishStructuredOperation(QStringLiteral("plugin-update-check:startup"),
		QStringLiteral("succeeded"), 1, 0, 0));
	QCOMPARE(operations.rowCount(), 1);
	QCOMPARE(overlay.rowCount(), 1);

	operations.startStructuredOperation(QStringLiteral("plugin-load:broken"),
		QStringLiteral("plugin-load"), QStringLiteral("Loading plugin"),
		QStringLiteral("Starting Broken Plugin"), 1, false);
	QVERIFY(operations.finishStructuredOperation(QStringLiteral("plugin-load:broken"),
		QStringLiteral("failed"), 0, 1, 0));
	QCOMPARE(operations.rowCount(), 2);
	QCOMPARE(overlay.rowCount(), 1);
	QCOMPARE(operations.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
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
	shared.setCurrentVoiceScopeId(42);
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
	QSignalSpy sourceSpy(&media, &MediaSessionBackend::sourceChanged);
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/inline-test"));
	QVERIFY(media.detached());
	QVERIFY(media.openInline(url, QStringLiteral("youtube"), QStringLiteral("message:42")));
	QVERIFY(media.active());
	QVERIFY(!media.detached());
	QCOMPARE(media.sessionId(), QStringLiteral("message:42"));
	QCOMPARE(sourceSpy.count(), 1);
	media.reportPlaybackState(23.5, 90.0, false);

#ifdef Q_OS_WIN
	QVERIFY(!media.detachedPlaybackSupported());
	media.detach();
	QVERIFY(!media.detached());
	QVERIFY(media.active());
	QCOMPARE(media.position(), 23.5);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(sourceSpy.count(), 1);
	media.closePlayer();
	QVERIFY(!media.active());
	QVERIFY(media.detached());

	// Requests for a separate player degrade to the proven inline presentation
	// rather than exposing a second D3D11-backed QQuickWindow.
	QVERIFY(media.open(url, QStringLiteral("youtube"), QStringLiteral("message:43")));
	QVERIFY(!media.detached());
#else
	QVERIFY(media.detachedPlaybackSupported());
	media.detach();
	QVERIFY(media.detached());
	QVERIFY(media.active());
	QCOMPARE(media.position(), 23.5);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(sourceSpy.count(), 2);
	media.attach();
	QVERIFY(!media.detached());
	QVERIFY(media.active());
	QCOMPARE(media.position(), 23.5);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(sourceSpy.count(), 3);
	media.closePlayer();
	QVERIFY(!media.active());
	QVERIFY(media.detached());

	QVERIFY(media.open(url, QStringLiteral("youtube"), QStringLiteral("message:43")));
	QVERIFY(media.detached());
#endif
}

void TestQmlClientModels::mediaSessionAutomationLifecycleStaysInlineAndAllowlisted() {
	MediaSessionBackend media;
	const QUrl url(QStringLiteral("https://www.youtube-nocookie.com/embed/automation-lifecycle"));
	QVERIFY(!media.openInline(QUrl(QStringLiteral("https://example.com/watch-together")),
		QStringLiteral("direct"), QStringLiteral("automation-room")));
	QVERIFY(!media.active());

	const qulonglong baselineGeneration = media.syncGeneration();
	QVERIFY(media.openInline(url, QStringLiteral("youtube"), QStringLiteral("automation-room")));
	QVERIFY(media.active());
	QVERIFY(!media.detached());
	QCOMPARE(media.provider(), QStringLiteral("youtube"));
	QCOMPARE(media.url(), url);
	const qulonglong openedGeneration = media.syncGeneration();
	QVERIFY(openedGeneration > baselineGeneration);

	media.applyRemoteState(url, QStringLiteral("youtube"), QStringLiteral("automation-room"),
		12.5, false, openedGeneration + 1);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(media.position(), 12.5);
	QVERIFY(media.syncGeneration() > openedGeneration);
	QVERIFY(!media.detached());

	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

void TestQmlClientModels::mediaSessionValidatesAndPublishesTypedState() {
	MediaSessionBackend media;
	QSignalSpy playSpy(&media, &MediaSessionBackend::playRequested);
	QSignalSpy pauseSpy(&media, &MediaSessionBackend::pauseRequested);
	QSignalSpy seekSpy(&media, &MediaSessionBackend::seekRequested);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);
	QVERIFY(!media.open(QUrl(QStringLiteral("http://www.youtube.com/embed/abc")), QStringLiteral("youtube"),
						QStringLiteral("room:1")));
	QCOMPARE(media.state(), QStringLiteral("error"));
	QCOMPARE(rejectionSpy.count(), 1);
	QCOMPARE(rejectionSpy.constFirst().constFirst().toString(), media.error());
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
						   QStringLiteral("room:2"), 1.0, false, 41);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	QCOMPARE(media.position(), 28.0);
	media.applyRemoteState(QUrl(QStringLiteral("https://www.youtube.com/embed/fresh-session")),
		QStringLiteral("youtube"), QStringLiteral("room:3"), 1.0, false, 2);
	QCOMPARE(media.sessionId(), QStringLiteral("room:3"));
	QCOMPARE(media.position(), 1.0);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

void TestQmlClientModels::mediaSessionValidatesDirectMedia() {
	MediaSessionBackend media;
	QSignalSpy sourceSpy(&media, &MediaSessionBackend::sourceChanged);
	const QUrl video(QStringLiteral("data:video/mp4;base64,AAAA"));
	const QUrl audio(QStringLiteral("data:audio/mp4;base64,AAAA"));
	QVERIFY(media.openDirect(video, QStringLiteral("video/mp4"), audio, QStringLiteral("audio/mp4"),
								QStringLiteral("message:7")));
	QCOMPARE(media.provider(), QStringLiteral("direct"));
	QCOMPARE(media.mediaMime(), QStringLiteral("video/mp4"));
	QCOMPARE(media.audioMime(), QStringLiteral("audio/mp4"));
	QCOMPARE(media.audioUrl(), audio);
	QCOMPARE(media.detached(), media.detachedPlaybackSupported());
	QCOMPARE(sourceSpy.count(), 1);
	QVERIFY(media.isNavigationAllowed(video));
	QVERIFY(media.isNavigationAllowed(audio));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("data:text/html;base64,AAAA"))));
	media.close();
	QCOMPARE(sourceSpy.count(), 2);
	QVERIFY(media.audioUrl().isEmpty());
	QVERIFY(media.mediaMime().isEmpty());

	QVERIFY(media.openDirectInline(video, QStringLiteral("video/mp4"), audio, QStringLiteral("audio/mp4"),
									 QStringLiteral("message:inline-direct")));
	QCOMPARE(media.provider(), QStringLiteral("direct"));
	QVERIFY(!media.detached());
	QCOMPARE(media.sessionId(), QStringLiteral("message:inline-direct"));
	QCOMPARE(sourceSpy.count(), 3);
	media.close();
	QCOMPARE(sourceSpy.count(), 4);

	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:video/mp4;base64,AAAA")),
								 QStringLiteral("video/webm"), {}, {}, QStringLiteral("message:8")));
	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:text/html;base64,AAAA")),
								 QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:9")));
	QVERIFY(media.openDirect(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=one")),
							   QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:10")));
	QCOMPARE(sourceSpy.count(), 5);
	QVERIFY(media.isNavigationAllowed(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=one#fragment"))));
	QVERIFY(!media.isNavigationAllowed(QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=two"))));

	const QUrl hls(QStringLiteral("https://cdn.cloudflare.steamstatic.com/trailer/master.m3u8"));
	QVERIFY(media.openDirect(hls, QStringLiteral("application/vnd.apple.mpegurl"), {}, {},
		QStringLiteral("steam:hls")));
	QCOMPARE(media.mediaMime(), QStringLiteral("application/vnd.apple.mpegurl"));
	QCOMPARE(media.url(), hls);
	QVERIFY(media.isNavigationAllowed(hls));
	const QUrl dash(QStringLiteral("https://cdn.cloudflare.steamstatic.com/trailer/manifest.mpd"));
	QVERIFY(media.openDirectInline(dash, QStringLiteral("application/dash+xml"), {}, {},
		QStringLiteral("steam:dash")));
	QVERIFY(!media.detached());
	QCOMPARE(media.mediaMime(), QStringLiteral("application/dash+xml"));
	QVERIFY(!media.openDirect(QUrl(QStringLiteral("data:application/dash+xml;base64,AAAA")),
		QStringLiteral("application/dash+xml"), {}, {}, QStringLiteral("steam:unsafe-manifest")));
}

void TestQmlClientModels::mediaSessionMaterializesDataSourcesWithoutStaleGenerationLeaks() {
	MediaSessionBackend media;
	QSignalSpy playbackSourceSpy(&media, &MediaSessionBackend::playbackSourceChanged);
	const QByteArray wavBytes = QByteArray::fromBase64(
		QByteArrayLiteral("UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="));
	const QUrl wavDataUrl(QStringLiteral("data:audio/wav;base64,")
		+ QString::fromLatin1(wavBytes.toBase64()));

	QVERIFY(media.openDirectInline(wavDataUrl, QStringLiteral("audio/wav"), {}, {},
		QStringLiteral("materialized-a")));
	QVERIFY(media.playbackSourcePreparing());
	QVERIFY(!media.playbackSourceReady());
	QTRY_VERIFY_WITH_TIMEOUT(media.playbackSourceReady(), 10000);
	QVERIFY(!media.playbackSourcePreparing());
	QVERIFY(media.playbackUrl().isLocalFile());
	QCOMPARE(media.url(), wavDataUrl);
	const QString firstPath = media.playbackUrl().toLocalFile();
	const QString sessionDirectory = QFileInfo(firstPath).absolutePath();
	QFile firstFile(firstPath);
	QVERIFY(firstFile.open(QIODevice::ReadOnly));
	QCOMPARE(firstFile.readAll(), wavBytes);
	firstFile.close();
	const qulonglong firstGeneration = media.playbackSourceGeneration();

	QByteArray stalePayload(2 * 1024 * 1024, '\x2a');
	const QUrl staleDataUrl(QStringLiteral("data:video/mp4;base64,")
		+ QString::fromLatin1(stalePayload.toBase64()));
	QVERIFY(media.openDirectInline(staleDataUrl, QStringLiteral("video/mp4"), {}, {},
		QStringLiteral("materialized-b")));
	const qulonglong staleGeneration = media.playbackSourceGeneration();
	QVERIFY(staleGeneration > firstGeneration);
	QVERIFY(media.playbackSourcePreparing());

	const QUrl finalHttpsUrl(QStringLiteral("https://cdn.example.com/final.mp4"));
	QVERIFY(media.openDirectInline(finalHttpsUrl, QStringLiteral("video/mp4"), {}, {},
		QStringLiteral("materialized-c")));
	const qulonglong finalGeneration = media.playbackSourceGeneration();
	QVERIFY(finalGeneration > staleGeneration);
	QVERIFY(media.playbackSourceReady());
	QVERIFY(!media.playbackSourcePreparing());
	QCOMPARE(media.playbackUrl(), finalHttpsUrl);
	const int signalsAfterFinalSource = playbackSourceSpy.count();

	QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(firstPath), 10000);
	QTRY_VERIFY_WITH_TIMEOUT(QDir(sessionDirectory).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty(),
		10000);
	QTest::qWait(100);
	QCOMPARE(media.playbackSourceGeneration(), finalGeneration);
	QCOMPARE(media.playbackUrl(), finalHttpsUrl);
	QCOMPARE(playbackSourceSpy.count(), signalsAfterFinalSource);

	media.close();
	QVERIFY(!media.playbackSourceReady());
	QVERIFY(media.playbackUrl().isEmpty());

	MediaSessionBackend degradedSecondary;
	const QUrl originalSecondary(QStringLiteral("data:audio/mp4;base64,!!!!"));
	QVERIFY(degradedSecondary.openDirectInline(
		QUrl(QStringLiteral("data:video/mp4;base64,AAAA")), QStringLiteral("video/mp4"),
		originalSecondary, QStringLiteral("audio/mp4"), QStringLiteral("degraded-secondary")));
	QTRY_VERIFY_WITH_TIMEOUT(degradedSecondary.playbackSourceReady(), 10000);
	QVERIFY(degradedSecondary.playbackUrl().isLocalFile());
	QVERIFY(degradedSecondary.playbackAudioUrl().isEmpty());
	QCOMPARE(degradedSecondary.audioUrl(), originalSecondary);
	QVERIFY(!degradedSecondary.playbackAudioWarning().isEmpty());
	QCOMPARE(degradedSecondary.state(), QStringLiteral("loading"));
	const QString degradedPrimaryPath = degradedSecondary.playbackUrl().toLocalFile();
	degradedSecondary.close();
	QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(degradedPrimaryPath), 10000);

	MediaSessionBackend invalidBase64;
	QVERIFY(invalidBase64.openDirectInline(
		QUrl(QStringLiteral("data:audio/wav;base64,!!!!")), QStringLiteral("audio/wav"), {}, {},
		QStringLiteral("invalid-base64")));
	QTRY_COMPARE_WITH_TIMEOUT(invalidBase64.state(), QStringLiteral("error"), 10000);
	QCOMPARE(invalidBase64.errorCode(), QStringLiteral("native-source-prepare-failed"));
	QVERIFY(!invalidBase64.playbackSourceReady());
	QVERIFY(invalidBase64.playbackUrl().isEmpty());

	constexpr qsizetype MaximumInlineMediaBytes = 24 * 1024 * 1024;
	const qsizetype maximumBase64Characters = ((MaximumInlineMediaBytes + 2) / 3) * 4;
	QString oversizedSource = QStringLiteral("data:audio/wav;base64,");
	oversizedSource += QString(maximumBase64Characters + 1, QLatin1Char('A'));
	MediaSessionBackend oversized;
	QVERIFY(!oversized.openDirectInline(QUrl(oversizedSource), QStringLiteral("audio/wav"), {}, {},
		QStringLiteral("oversized")));
	QCOMPARE(oversized.errorCode(), QStringLiteral("source-rejected"));
	QVERIFY(!oversized.active());
}

void TestQmlClientModels::mediaSessionProviderAllowlist_data() {
	QTest::addColumn< QString >("provider");
	QTest::addColumn< QUrl >("url");
	QTest::addColumn< bool >("allowed");
	QTest::newRow("youtube") << QStringLiteral("youtube") << QUrl(QStringLiteral("https://www.youtube.com/embed/a")) << true;
	QTest::newRow("twitch") << QStringLiteral("twitch") << QUrl(QStringLiteral("https://player.twitch.tv/?video=1")) << true;
	QTest::newRow("twitch-clip") << QStringLiteral("twitch") << QUrl(QStringLiteral("https://clips.twitch.tv/embed?clip=SafeClip")) << true;
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
		const bool controllable = QSet< QString > { QStringLiteral("direct"), QStringLiteral("youtube") }
			.contains(canonicalProvider);
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

void TestQmlClientModels::mediaSessionPublishesTypedRendererErrors() {
	MediaSessionBackend media;
	QVERIFY(!media.open(QUrl(QStringLiteral("https://example.com/embed/a")), QStringLiteral("youtube"),
		QStringLiteral("rejected")));
	QCOMPARE(media.errorCode(), QStringLiteral("source-rejected"));

	QVERIFY(media.open(QUrl(QStringLiteral("https://www.youtube.com/embed/a")), QStringLiteral("youtube"),
		QStringLiteral("room")));
	QVERIFY(media.errorCode().isEmpty());
	media.reportTypedError(QStringLiteral("renderer-component-unavailable"),
		QStringLiteral("The isolated media player is unavailable."));
	QCOMPARE(media.state(), QStringLiteral("error"));
	QCOMPARE(media.errorCode(), QStringLiteral("renderer-component-unavailable"));
	QCOMPARE(media.error(), QStringLiteral("The isolated media player is unavailable."));

	media.retry();
	QCOMPARE(media.state(), QStringLiteral("loading"));
	QVERIFY(media.error().isEmpty());
	QVERIFY(media.errorCode().isEmpty());

	media.reportTypedError(QStringLiteral(" invalid code "), {});
	QCOMPARE(media.errorCode(), QStringLiteral("playback-failed"));
	QCOMPARE(media.error(), QStringLiteral("Media playback failed."));
	media.close();
	QVERIFY(media.errorCode().isEmpty());
}

void TestQmlClientModels::mediaSessionSharedStartRequiresVoiceScope() {
	MediaSessionBackend media(nullptr, 25);
	QSignalSpy startSpy(&media, &MediaSessionBackend::sharedStartRequested);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);

	QVERIFY(!media.startShared(QUrl(QStringLiteral("https://www.youtube.com/embed/no-room")),
		QStringLiteral("youtube"), QStringLiteral("No room")));
	QCOMPARE(startSpy.count(), 0);
	QCOMPARE(rejectionSpy.count(), 1);
	QVERIFY(!media.sharedAvailable());
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("idle"));
	QVERIFY(media.error().contains(QStringLiteral("voice room"), Qt::CaseInsensitive));
}

void TestQmlClientModels::mediaSessionSharedHostLifecycle() {
	MediaSessionBackend media;
	media.setCurrentVoiceScopeId(42);
	QSignalSpy startSpy(&media, &MediaSessionBackend::sharedStartRequested);
	QSignalSpy eventSpy(&media, &MediaSessionBackend::sharedEventRequested);
	QSignalSpy stateSpy(&media, &MediaSessionBackend::sharedPlaybackStateRequested);
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/shared"));
	QVERIFY(!media.startShared(QUrl(QStringLiteral("https://open.spotify.com/embed/track/12345678")),
		QStringLiteral("spotify"), QStringLiteral("Unsupported shared track")));
	QVERIFY(!media.sharedAvailable());

	QVERIFY(media.startShared(url, QStringLiteral("youtube"), QStringLiteral("Shared clip"),
		QStringLiteral("short")));
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.active());
	QCOMPARE(media.sharedAspect(), QStringLiteral("short"));
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("starting"));
	QVERIFY(media.sharedOperationError().isEmpty());
	QCOMPARE(startSpy.count(), 1);
	QCOMPARE(startSpy.constFirst().at(4).toString(), QStringLiteral("short"));
	const QString sessionId = media.sharedSessionId();
	QVERIFY(!sessionId.isEmpty());

	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Shared clip"), 42, 17, 17,
							 { 17 }, QStringLiteral("start"), 0.0, true, 100, 17, QStringLiteral("short"));
	QVERIFY(media.active());
	QVERIFY(media.sharedJoined());
	QVERIFY(media.sharedHost());
	QCOMPARE(media.sharedAspect(), QStringLiteral("short"));
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
	QCOMPARE(media.sharedParticipantCount(), 1);
	media.reportPlaybackState(1.25, 90.0, false);
	QCOMPARE(stateSpy.count(), 1);
	media.closePlayer();
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
	QVERIFY(media.reopenSharedPlayer());
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("reconnecting"));
	media.reportError(QStringLiteral("sync renderer failed"));
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("error"));
	QCOMPARE(media.sharedOperationError(), QStringLiteral("sync renderer failed"));
	media.retry();
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("reconnecting"));
	media.reportLoadProgress(100);
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
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
	QCOMPARE(media.sharedAspect(), QStringLiteral("wide"));
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("idle"));
	QVERIFY(media.sharedOperationError().isEmpty());
}

void TestQmlClientModels::mediaSessionRequiresExplicitJoinForRemoteSessions() {
	MediaSessionBackend media;
	media.setCurrentVoiceScopeId(8);
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
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("available"));

	media.joinShared();
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("starting"));
	QCOMPARE(eventSpy.count(), 1);
	QCOMPARE(eventSpy.at(0).at(1).toString(), QStringLiteral("join"));
	media.applySharedState(sessionId, url, QStringLiteral("direct"), QStringLiteral("Remote clip"), 8, 17, 9,
							 { 9, 17 }, QStringLiteral("join"), 3.0, true, 200, 17);
	QVERIFY(media.sharedJoined());
	QVERIFY(!media.sharedHost());
	QVERIFY(media.active());
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
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
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("available"));
}

void TestQmlClientModels::mediaSessionRejectsStaleSharedScopeAfterRoomMove() {
	MediaSessionBackend media;
	QSignalSpy eventSpy(&media, &MediaSessionBackend::sharedEventRequested);
	const QUrl oldRoomUrl(QStringLiteral("https://www.youtube.com/embed/old-room"));
	const QString oldSessionId = QStringLiteral("old-room-session");

	media.setCurrentVoiceScopeId(10);
	media.applySharedState(oldSessionId, oldRoomUrl, QStringLiteral("youtube"), QStringLiteral("Old room"),
		10, 9, 9, { 9 }, QStringLiteral("start"), 3.0, true, 100, 17);
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	media.joinShared();
	QCOMPARE(eventSpy.count(), 1);
	media.applySharedState(oldSessionId, oldRoomUrl, QStringLiteral("youtube"), QStringLiteral("Old room"),
		10, 17, 9, { 9, 17 }, QStringLiteral("join"), 3.0, true, 200, 17);
	QVERIFY(media.sharedJoined());
	QVERIFY(media.active());

	media.setCurrentVoiceScopeId(20);
	QVERIFY(!media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));

	// Delayed join/state traffic from the old room must neither restore the
	// session nor make a stale join request possible after the move.
	media.applySharedState(oldSessionId, oldRoomUrl, QStringLiteral("youtube"), QStringLiteral("Old room"),
		10, 9, 9, { 9, 17 }, QStringLiteral("state"), 12.0, false, 300, 17);
	QVERIFY(!media.sharedAvailable());
	media.joinShared();
	QCOMPARE(eventSpy.count(), 1);

	const QUrl newRoomUrl(QStringLiteral("https://www.youtube.com/embed/new-room"));
	media.applySharedState(QStringLiteral("new-room-session"), newRoomUrl, QStringLiteral("youtube"),
		QStringLiteral("New room"), 20, 19, 19, { 19 }, QStringLiteral("state"), 0.0, true, 400, 17);
	QVERIFY(media.sharedAvailable());
	QCOMPARE(media.sharedScopeId(), 20ULL);
	QVERIFY(!media.sharedJoined());
}

void TestQmlClientModels::mediaSessionSharedStartTimeoutRecoversToIdle() {
	MediaSessionBackend media(nullptr, 25);
	media.setCurrentVoiceScopeId(42);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);

	QVERIFY(media.startShared(QUrl(QStringLiteral("https://www.youtube.com/embed/start-timeout")),
		QStringLiteral("youtube"), QStringLiteral("Unacknowledged start")));
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("starting"));
	QTRY_COMPARE_WITH_TIMEOUT(media.sharedOperationStatus(), QStringLiteral("idle"), 1000);
	QVERIFY(!media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	QCOMPARE(media.state(), QStringLiteral("idle"));
	QVERIFY(!media.sharedOperationError().isEmpty());
	QCOMPARE(rejectionSpy.count(), 1);
	QCOMPARE(rejectionSpy.constFirst().constFirst().toString(), media.sharedOperationError());
}

void TestQmlClientModels::mediaSessionSharedJoinTimeoutRestoresAvailableSession() {
	MediaSessionBackend media(nullptr, 25);
	media.setCurrentVoiceScopeId(42);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);
	const QString sessionId = QStringLiteral("join-timeout-session");
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/join-timeout"));
	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Remote session"),
		42, 9, 9, { 9 }, QStringLiteral("start"), 0.0, true, 1, 17);

	QVERIFY(media.sharedAvailable());
	media.joinShared();
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("starting"));
	QTRY_COMPARE_WITH_TIMEOUT(media.sharedOperationStatus(), QStringLiteral("available"), 1000);
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	QCOMPARE(media.sharedSessionId(), sessionId);
	QCOMPARE(media.state(), QStringLiteral("available"));
	QVERIFY(!media.sharedOperationError().isEmpty());
	QCOMPARE(rejectionSpy.count(), 1);
}

void TestQmlClientModels::mediaSessionSharedSuccessCancelsAcknowledgementTimeout() {
	MediaSessionBackend media(nullptr, 25);
	media.setCurrentVoiceScopeId(42);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/acknowledged-start"));
	QVERIFY(media.startShared(url, QStringLiteral("youtube"), QStringLiteral("Acknowledged start"),
		QStringLiteral("short")));
	const QString sessionId = media.sharedSessionId();
	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Acknowledged start"),
		42, 17, 17, { 17 }, QStringLiteral("start"), 0.0, true, 1, 17);

	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
	QVERIFY(media.sharedJoined());
	QCOMPARE(media.sharedAspect(), QStringLiteral("short"));
	QTest::qWait(75);
	QCOMPARE(media.sharedSessionId(), sessionId);
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
	QVERIFY(media.sharedOperationError().isEmpty());
	QCOMPARE(rejectionSpy.count(), 0);
}

void TestQmlClientModels::mediaSessionStaleAcknowledgementTimeoutCannotAffectNewSession() {
	MediaSessionBackend media(nullptr, 30);
	QSignalSpy rejectionSpy(&media, &MediaSessionBackend::playbackRejected);
	media.setCurrentVoiceScopeId(10);
	QVERIFY(media.startShared(QUrl(QStringLiteral("https://www.youtube.com/embed/old-attempt")),
		QStringLiteral("youtube"), QStringLiteral("Old attempt")));
	const QString oldSessionId = media.sharedSessionId();

	media.setCurrentVoiceScopeId(20);
	QVERIFY(!media.sharedAvailable());
	const QUrl newUrl(QStringLiteral("https://www.youtube.com/embed/new-attempt"));
	QVERIFY(media.startShared(newUrl, QStringLiteral("youtube"), QStringLiteral("New attempt")));
	const QString newSessionId = media.sharedSessionId();
	QVERIFY(newSessionId != oldSessionId);
	media.applySharedState(newSessionId, newUrl, QStringLiteral("youtube"), QStringLiteral("New attempt"),
		20, 17, 17, { 17 }, QStringLiteral("start"), 0.0, true, 2, 17);

	QTest::qWait(90);
	QCOMPARE(media.sharedSessionId(), newSessionId);
	QVERIFY(media.sharedAvailable());
	QVERIFY(media.sharedJoined());
	QCOMPARE(media.sharedOperationStatus(), QStringLiteral("ready"));
	QVERIFY(media.sharedOperationError().isEmpty());
	QCOMPARE(rejectionSpy.count(), 0);
}

void TestQmlClientModels::mediaSessionSharedClockSkewUsesReceiveOrder() {
	MediaSessionBackend media;
	media.setCurrentVoiceScopeId(42);
	QSignalSpy seekSpy(&media, &MediaSessionBackend::seekRequested);
	const QUrl url(QStringLiteral("https://www.youtube.com/embed/clock-skew"));
	const QString sessionId = QStringLiteral("clock-skew-session");
	const qulonglong selfSession = 17;
	const qulonglong hostSession = 9;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qulonglong serverOneHourAhead = static_cast< qulonglong >(now + 60 * 60 * 1000);
	const qulonglong serverOneHourBehind = static_cast< qulonglong >(now - 60 * 60 * 1000);

	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Clock-safe clip"),
		42, hostSession, hostSession, { hostSession }, QStringLiteral("start"), 2.0, false,
		serverOneHourAhead, selfSession);
	QVERIFY(media.sharedAvailable());
	QVERIFY(!media.sharedJoined());
	media.joinShared();

	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Clock-safe clip"),
		42, selfSession, hostSession, { hostSession, selfSession }, QStringLiteral("join"), 12.0, false,
		serverOneHourAhead + 1000, selfSession);
	QVERIFY(media.sharedJoined());
	QVERIFY(!media.sharedHost());
	QCOMPARE(media.position(), 12.0);
	const qulonglong firstLocalGeneration = media.syncGeneration();
	seekSpy.clear();

	// A new event with a drastically regressed server wall clock is still the
	// next event on this connection. It must neither be rejected nor add decades
	// of apparent playback age.
	media.applySharedState(sessionId, url, QStringLiteral("youtube"), QStringLiteral("Clock-safe clip"),
		42, hostSession, hostSession, { hostSession, selfSession }, QStringLiteral("playback"), 20.0, false,
		serverOneHourBehind, selfSession);
	QCOMPARE(media.position(), 20.0);
	QVERIFY(media.position() < 60.0);
	QVERIFY(media.syncGeneration() > firstLocalGeneration);
	QCOMPARE(seekSpy.count(), 1);
	QCOMPARE(seekSpy.constFirst().constFirst().toDouble(), 20.0);
}

void TestQmlClientModels::mediaSessionRejectsConflictingPlaybackDuringSharedSession() {
	MediaSessionBackend media;
	media.setCurrentVoiceScopeId(42);
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
	QVERIFY(!media.openDirectInline(QUrl(QStringLiteral("data:video/mp4;base64,AAAA")),
									   QStringLiteral("video/mp4"), {}, {}, QStringLiteral("message:inline-direct")));
	QCOMPARE(rejectionSpy.count(), 4);
	QCOMPARE(media.sessionId(), sharedSessionId);
	QCOMPARE(media.url(), sharedUrl);
	QCOMPARE(media.state(), QStringLiteral("paused"));
	QCOMPARE(media.position(), 8.0);
	QVERIFY(media.error().isEmpty());

	QVERIFY(media.open(sharedUrl, QStringLiteral("youtube"), sharedSessionId));
	QCOMPARE(media.sessionId(), sharedSessionId);
	QCOMPARE(media.state(), QStringLiteral("loading"));
	QCOMPARE(rejectionSpy.count(), 4);
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
