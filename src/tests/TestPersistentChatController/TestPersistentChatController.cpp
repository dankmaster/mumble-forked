// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include <initializer_list>

#include "mumble/ChatRowBuilder.h"
#include "mumble/ChatRowModel.h"
#include "mumble/ChatTimelineFeed.h"
#include "mumble/PersistentChatController.h"

namespace {
	bool g_gatewayReady                          = false;
	int g_initialRequestCount                    = 0;
	int g_olderRequestCount                      = 0;
	int g_warmupRequestCount                     = 0;
	quint64 g_gatewayConnectionGeneration        = 1;
	bool g_warmupRequestSucceeds                 = true;
	bool g_initialRequestSucceeds                = true;
	bool g_olderRequestSucceeds                  = true;
	quint64 g_lastInitialRequestID               = 0;
	quint64 g_lastOlderRequestID                 = 0;
	quint64 g_lastWarmupRequestID                = 0;
	MumbleProto::ChatScope g_lastInitialRequestScope = MumbleProto::Channel;
	unsigned int g_lastInitialRequestScopeID     = 0;
	QList< QPair< MumbleProto::ChatScope, unsigned int > > g_lastWarmupScopes;
	unsigned int g_lastWarmupLimit = 0;

	void resetGatewayRequestState() {
		g_gatewayReady              = false;
		g_initialRequestCount       = 0;
		g_olderRequestCount         = 0;
		g_warmupRequestCount        = 0;
		g_warmupRequestSucceeds     = true;
		g_initialRequestSucceeds    = true;
		g_olderRequestSucceeds      = true;
		g_lastInitialRequestID      = 0;
		g_lastOlderRequestID        = 0;
		g_lastWarmupRequestID       = 0;
		g_lastInitialRequestScope   = MumbleProto::Channel;
		g_lastInitialRequestScopeID = 0;
		g_lastWarmupScopes.clear();
		g_lastWarmupLimit = 0;
		g_gatewayConnectionGeneration = 1;
	}
}

PersistentChatGateway::PersistentChatGateway(QObject *parent) : QObject(parent) {
}

void PersistentChatGateway::setServerHandler(ServerHandler *) {
}

ServerHandler *PersistentChatGateway::serverHandler() const {
	return nullptr;
}

quint64 PersistentChatGateway::connectionGeneration() const {
	return g_gatewayConnectionGeneration;
}

bool PersistentChatGateway::isReady() const {
	return g_gatewayReady;
}

bool PersistentChatGateway::requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID, quint64 requestID) {
	++g_initialRequestCount;
	g_lastInitialRequestScope   = scope;
	g_lastInitialRequestScopeID = scopeID;
	g_lastInitialRequestID      = requestID;
	return g_initialRequestSucceeds;
}

bool PersistentChatGateway::requestWarmupPages(
	const QList< QPair< MumbleProto::ChatScope, unsigned int > > &scopes, unsigned int limitPerScope, quint64 requestID) {
	g_lastWarmupRequestID = requestID;
	if (!g_warmupRequestSucceeds) {
		return false;
	}

	++g_warmupRequestCount;
	g_lastWarmupScopes = scopes;
	g_lastWarmupLimit  = limitPerScope;
	return true;
}

bool PersistentChatGateway::requestOlder(MumbleProto::ChatScope, unsigned int, unsigned int, quint64 requestID) {
	++g_olderRequestCount;
	g_lastOlderRequestID = requestID;
	return g_olderRequestSucceeds;
}

void PersistentChatGateway::send(MumbleProto::ChatScope, unsigned int, const QString &, MumbleProto::ChatBodyFormat,
								 std::optional< unsigned int >, const QList< unsigned int > &, const QStringList &) {
}

void PersistentChatGateway::toggleReaction(MumbleProto::ChatScope, unsigned int, unsigned int, unsigned int,
										   const QString &, bool) {
}

void PersistentChatGateway::deleteMessage(MumbleProto::ChatScope, unsigned int, unsigned int, unsigned int) {
}

void PersistentChatGateway::markRead(MumbleProto::ChatScope, unsigned int, unsigned int) {
}

void PersistentChatGateway::handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response) {
	emit historyReceived(response, g_gatewayConnectionGeneration);
}

void PersistentChatGateway::handleIncomingMessage(const MumbleProto::ChatMessage &message) {
	emit messageReceived(message);
}

void PersistentChatGateway::handleIncomingReadState(const MumbleProto::ChatReadStateUpdate &update) {
	emit readStateReceived(update);
}

namespace {
	MumbleProto::ChatMessage makeMessage(unsigned int messageID, quint64 createdAt, MumbleProto::ChatScope scope,
										 unsigned int scopeID, const QString &body = QStringLiteral("hello")) {
		MumbleProto::ChatMessage message;
		message.set_message_id(messageID);
		message.set_thread_id(messageID);
		message.set_created_at(createdAt);
		message.set_scope(scope);
		message.set_scope_id(scopeID);
		message.set_actor(1);
		message.set_actor_user_id(1);
		message.set_actor_name("Alice");
		message.set_body_text(body.toUtf8().constData());
		return message;
	}

	MumbleProto::ChatHistoryResponse makeHistory(MumbleProto::ChatScope scope, unsigned int scopeID,
												 std::initializer_list< MumbleProto::ChatMessage > messages,
												 unsigned int lastReadMessageID, unsigned int oldestMessageID, bool hasOlder) {
		MumbleProto::ChatHistoryResponse response;
		response.set_scope(scope);
		response.set_scope_id(scopeID);
		response.set_last_read_message_id(lastReadMessageID);
		response.set_oldest_message_id(oldestMessageID);
		response.set_has_older(hasOlder);
		for (const MumbleProto::ChatMessage &message : messages) {
			*response.add_messages() = message;
		}
		return response;
	}

	MumbleProto::ChatReadStateUpdate makeReadState(MumbleProto::ChatScope scope, unsigned int scopeID,
												   unsigned int lastReadMessageID) {
		MumbleProto::ChatReadStateUpdate update;
		update.set_scope(scope);
		update.set_scope_id(scopeID);
		update.set_last_read_message_id(lastReadMessageID);
		return update;
	}

	MumbleProto::ChatEmbedState makeEmbedState(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int messageID,
											   unsigned int threadID, const QString &url) {
		MumbleProto::ChatEmbedState state;
		state.set_scope(scope);
		state.set_scope_id(scopeID);
		state.set_message_id(messageID);
		state.set_thread_id(threadID);
		MumbleProto::ChatEmbedRef *embed = state.add_embeds();
		embed->set_canonical_url(url.toUtf8().constData());
		return state;
	}

	MumbleProto::ChatReactionAggregate makeReaction(const QString &emoji, unsigned int count, bool selfReacted,
													std::initializer_list< QString > actorNames = {}) {
		MumbleProto::ChatReactionAggregate reaction;
		reaction.set_emoji(emoji.toUtf8().constData());
		reaction.set_count(count);
		reaction.set_self_reacted(selfReacted);
		for (const QString &actorName : actorNames) {
			reaction.add_actor_names(actorName.toUtf8().constData());
		}
		return reaction;
	}

	MumbleProto::ChatReactionState makeReactionState(
		MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int messageID, unsigned int threadID,
		std::initializer_list< MumbleProto::ChatReactionAggregate > reactions) {
		MumbleProto::ChatReactionState state;
		state.set_scope(scope);
		state.set_scope_id(scopeID);
		state.set_message_id(messageID);
		state.set_thread_id(threadID);
		for (const MumbleProto::ChatReactionAggregate &reaction : reactions) {
			*state.add_reactions() = reaction;
		}
		return state;
	}
}

class TestPersistentChatController : public QObject {
	Q_OBJECT

private slots:
	void restoresCachedScopeSnapshots();
	void activeSnapshotChangedPublishesTwoHistoryMessages();
	void liveScopeWithoutHistoryPermissionPublishesMessages();
	void forceReloadCoalescesWithInitialRequestInFlight();
	void ignoresLateHistoryAfterReconnectToSameScope();
	void deniedHistoryMatchesRequestAndPreservesCache();
	void correlatedTimeoutAllowsSafeRetry();
	void legacyTimeoutRequiresReconnect();
	void resetRejectsLateCorrelatedReply();
	void connectionChangeReleasesAllPendingRequests();
	void unsentRequestsReleaseLoadingState();
	void warmupTimeoutAndFailureReleaseEachScope();
	void warmupBatchMatchesServerLimit();
	void completedRequestTimeoutDoesNotCancelItsSuccessor();
	void refreshRequestBlocksConcurrentOlderPage();
	void warmupScopesSkipsActiveScope();
	void warmupScopesRequestsBatchAndCachesResponses();
	void liveMessagesDoNotSatisfyInitialHistoryLoad();
	void keepsInactivePendingHistoryForCache();
	void mergesOlderHistoryAndReadState();
	void olderHistoryUsesStableRevisionAndBulkOrdering();
	void appliesEmbedUpdatesToCachedMessages();
	void preservesReplyMetadataFromHistory();
	void appliesReactionUpdatesToCachedMessages();
	void removesDeletedMessageFromCachedSnapshot();
	void reportsActiveMessageChanges();
	void rowBuilderSplitsMarkdownIntoBlocks();
	void rowBuilderSanitizesLinks();
	void rowBuilderKeepsLegacyFormattingAndSplitsInlineImages();
	void rowBuilderClassifiesMediaAndCards();
	void rowBuilderMarksSystemAndOwnMessages();
	void rowBuilderDecoratesAcrossPageBoundaries();
	void rowModelKeepsNewestFirstAndReportsExactRoles();
	void rowModelAppliesExtrasBeforeAndAfterRows();
	void rowModelSearchesNewestFirstAndKeepsTheCurrentMatch();
	void timelineFeedMirrorsActiveScope();
	void timelineFeedDropsWorkForRetiredScope();
	void rowBuilderBuildsLogLinesAndDirectMessages();
	void timelineFeedSyncsLocalScopes();
};

void TestPersistentChatController::activeSnapshotChangedPublishesTwoHistoryMessages() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 77), false);

	QSignalSpy changed(&controller, &PersistentChatController::activeSnapshotChanged);
	QList< PersistentChatScopeStateSnapshot > publishedSnapshots;
	connect(&controller, &PersistentChatController::activeSnapshotChanged, &controller,
			[&controller, &publishedSnapshots]() { publishedSnapshots.push_back(controller.activeSnapshot()); });

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 77,
					{ makeMessage(701, 1000, MumbleProto::TextChannel, 77, QStringLiteral("first live history row")),
					  makeMessage(702, 1010, MumbleProto::TextChannel, 77, QStringLiteral("second live history row")) },
					0, 701, false));

	QCOMPARE(changed.count(), 1);
	QCOMPARE(publishedSnapshots.size(), 1);
	const PersistentChatScopeStateSnapshot &published = publishedSnapshots.constFirst();
	QCOMPARE(published.key.scope, MumbleProto::TextChannel);
	QCOMPARE(published.key.scopeID, 77U);
	QCOMPARE(published.messages.size(), 2);
	QCOMPARE(QString::fromStdString(published.messages.at(0).body_text()), QStringLiteral("first live history row"));
	QCOMPARE(QString::fromStdString(published.messages.at(1).body_text()), QStringLiteral("second live history row"));
	QCOMPARE(controller.activeSnapshot().messages.size(), 2);
	resetGatewayRequestState();
}

void TestPersistentChatController::liveScopeWithoutHistoryPermissionPublishesMessages() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const PersistentChatScopeKey key = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 78);

	QSignalSpy changed(&controller, &PersistentChatController::activeSnapshotChanged);
	controller.setActiveScope(key, false, false);
	QCOMPARE(g_initialRequestCount, 0);
	QVERIFY(controller.hasActiveScope());
	QVERIFY(controller.activeScopeMatches(MumbleProto::TextChannel, 78));
	QCOMPARE(controller.activeSnapshot().key.scope, MumbleProto::TextChannel);
	QCOMPARE(controller.activeSnapshot().key.scopeID, 78U);
	QCOMPARE(controller.activeSnapshot().messages.size(), 0);
	QCOMPARE(changed.count(), 1);

	changed.clear();
	gateway.handleIncomingMessage(
		makeMessage(781, 2000, MumbleProto::TextChannel, 78, QStringLiteral("live without history permission")));
	QCOMPARE(g_initialRequestCount, 0);
	QCOMPARE(changed.count(), 1);
	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.constFirst().message_id(), 781U);
	QCOMPARE(QString::fromStdString(snapshot.messages.constFirst().body_text()),
			 QStringLiteral("live without history permission"));
	resetGatewayRequestState();
}

void TestPersistentChatController::restoresCachedScopeSnapshots() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 11,
					{ makeMessage(10, 1000, MumbleProto::TextChannel, 11),
					  makeMessage(11, 1010, MumbleProto::TextChannel, 11) },
					10, 10, true));

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 22), false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 22, { makeMessage(20, 2000, MumbleProto::TextChannel, 22) }, 0, 20, false));

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();

	QCOMPARE(snapshot.key.scope, MumbleProto::TextChannel);
	QCOMPARE(snapshot.key.scopeID, 11U);
	QCOMPARE(snapshot.messages.size(), 2);
	QCOMPARE(snapshot.messages.front().message_id(), 10U);
	QCOMPARE(snapshot.messages.back().message_id(), 11U);
	QCOMPARE(snapshot.unreadCount, 1);
	resetGatewayRequestState();
}

void TestPersistentChatController::forceReloadCoalescesWithInitialRequestInFlight() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);
	QCOMPARE(g_initialRequestCount, 1);
	QCOMPARE(g_lastInitialRequestScope, MumbleProto::Channel);
	QCOMPARE(g_lastInitialRequestScopeID, 7U);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);
	QCOMPARE(g_initialRequestCount, 1);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), true);
	QCOMPARE(g_initialRequestCount, 1);

	resetGatewayRequestState();
}

void TestPersistentChatController::ignoresLateHistoryAfterReconnectToSameScope() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const PersistentChatScopeKey key =
		PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	QCOMPARE(g_initialRequestCount, 1);

	controller.reset();
	++g_gatewayConnectionGeneration;
	controller.setActiveScope(key, false);
	QCOMPARE(g_initialRequestCount, 2);
	emit gateway.historyReceived(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(70, 7000, MumbleProto::Channel, 7) },
					0, 70, false),
		g_gatewayConnectionGeneration - 1);

	QCOMPARE(controller.activeSnapshot().messages.size(), 0);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Initial);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(71, 7100, MumbleProto::Channel, 7) },
					0, 71, false));
	QCOMPARE(controller.activeSnapshot().messages.size(), 1);
	QCOMPARE(controller.activeSnapshot().messages.constFirst().message_id(), 71U);
	QVERIFY(controller.activeSnapshot().initialLoaded);
	resetGatewayRequestState();
}

void TestPersistentChatController::refreshRequestBlocksConcurrentOlderPage() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const PersistentChatScopeKey key =
		PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(70, 7000, MumbleProto::Channel, 7) },
					0, 70, true));

	controller.setActiveScope(key, true);
	QCOMPARE(g_initialRequestCount, 2);
	QVERIFY(!controller.requestOlderForActiveScope());
	QCOMPARE(g_olderRequestCount, 0);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Refreshing);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(71, 7100, MumbleProto::Channel, 7) },
					0, 70, true));
	QVERIFY(controller.requestOlderForActiveScope());
	QCOMPARE(g_olderRequestCount, 1);
	resetGatewayRequestState();
}

void TestPersistentChatController::warmupScopesSkipsActiveScope() {
	resetGatewayRequestState();
	g_gatewayReady = false;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	QCOMPARE(g_initialRequestCount, 0);

	g_gatewayReady = true;
	controller.warmupScopes({ PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11),
							  PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 22) });

	QCOMPARE(g_initialRequestCount, 0);
	QCOMPARE(g_warmupRequestCount, 1);
	QCOMPARE(g_lastWarmupScopes.size(), 1);
	QCOMPARE(g_lastWarmupScopes.at(0).first, MumbleProto::TextChannel);
	QCOMPARE(g_lastWarmupScopes.at(0).second, 22U);

	resetGatewayRequestState();
}

void TestPersistentChatController::warmupScopesRequestsBatchAndCachesResponses() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.warmupScopes({ PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11),
							  PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 22),
							  PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11) });

	QCOMPARE(g_initialRequestCount, 0);
	QCOMPARE(g_warmupRequestCount, 1);
	QCOMPARE(g_lastWarmupLimit, 20U);
	QCOMPARE(g_lastWarmupScopes.size(), 2);
	QCOMPARE(g_lastWarmupScopes.at(0).first, MumbleProto::TextChannel);
	QCOMPARE(g_lastWarmupScopes.at(0).second, 11U);
	QCOMPARE(g_lastWarmupScopes.at(1).first, MumbleProto::TextChannel);
	QCOMPARE(g_lastWarmupScopes.at(1).second, 22U);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 11, { makeMessage(10, 1000, MumbleProto::TextChannel, 11) }, 0, 10, false));

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(g_initialRequestCount, 0);
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().message_id(), 10U);

	resetGatewayRequestState();
}

void TestPersistentChatController::liveMessagesDoNotSatisfyInitialHistoryLoad() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	gateway.handleIncomingMessage(makeMessage(90, 9000, MumbleProto::TextChannel, 99));
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 99), false);

	PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(g_initialRequestCount, 1);
	QCOMPARE(g_lastInitialRequestScope, MumbleProto::TextChannel);
	QCOMPARE(g_lastInitialRequestScopeID, 99U);
	QVERIFY(!snapshot.initialLoaded);
	QCOMPARE(snapshot.loadingState, PersistentChatLoadingState::Initial);
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().message_id(), 90U);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 99,
					{ makeMessage(88, 8800, MumbleProto::TextChannel, 99),
					  makeMessage(89, 8900, MumbleProto::TextChannel, 99) },
					89, 88, false));

	snapshot = controller.activeSnapshot();
	QVERIFY(snapshot.initialLoaded);
	QCOMPARE(snapshot.loadingState, PersistentChatLoadingState::Idle);
	QCOMPARE(snapshot.messages.size(), 3);
	QCOMPARE(snapshot.messages.front().message_id(), 88U);
	QCOMPARE(snapshot.messages.back().message_id(), 90U);

	resetGatewayRequestState();
}

void TestPersistentChatController::keepsInactivePendingHistoryForCache() {
	resetGatewayRequestState();
	g_gatewayReady = true;

	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 8), false);
	QCOMPARE(g_initialRequestCount, 2);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7, { makeMessage(70, 7000, MumbleProto::Channel, 7) }, 0, 70, false));
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(g_initialRequestCount, 2);
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().message_id(), 70U);

	resetGatewayRequestState();
}

void TestPersistentChatController::mergesOlderHistoryAndReadState() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(20, 2000, MumbleProto::Channel, 7),
					  makeMessage(21, 2010, MumbleProto::Channel, 7) },
					20, 20, true));
	QVERIFY(controller.requestOlderForActiveScope());
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(10, 1000, MumbleProto::Channel, 7),
					  makeMessage(11, 1010, MumbleProto::Channel, 7) },
					20, 10, false));
	gateway.handleIncomingReadState(makeReadState(MumbleProto::Channel, 7, 21));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 4);
	QCOMPARE(snapshot.messages.front().message_id(), 10U);
	QCOMPARE(snapshot.messages.back().message_id(), 21U);
	QCOMPARE(snapshot.oldestLoadedMessageId, 10U);
	QCOMPARE(snapshot.hasOlder, false);
	QCOMPARE(snapshot.unreadCount, 0);
	resetGatewayRequestState();
}

void TestPersistentChatController::olderHistoryUsesStableRevisionAndBulkOrdering() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 88), false);

	MumbleProto::ChatHistoryResponse initial;
	initial.set_scope(MumbleProto::TextChannel);
	initial.set_scope_id(88);
	initial.set_last_read_message_id(1024);
	initial.set_oldest_message_id(513);
	initial.set_has_older(true);
	for (unsigned int messageID = 513; messageID <= 1024; ++messageID) {
		*initial.add_messages() =
			makeMessage(messageID, messageID * 10, MumbleProto::TextChannel, 88);
	}
	gateway.handleIncomingHistory(initial);

	const PersistentChatScopeStateSnapshot loaded = controller.activeSnapshot();
	QCOMPARE(loaded.messages.size(), 512);
	QCOMPARE(loaded.messagesRevision, quint64(1));
	QVERIFY(controller.requestOlderForActiveScope());
	const PersistentChatScopeStateSnapshot loading = controller.activeSnapshot();
	QCOMPARE(loading.messagesRevision, loaded.messagesRevision);
	QCOMPARE(loading.messages.size(), loaded.messages.size());
	QCOMPARE(loading.loadingState, PersistentChatLoadingState::Older);

	MumbleProto::ChatHistoryResponse older;
	older.set_scope(MumbleProto::TextChannel);
	older.set_scope_id(88);
	older.set_last_read_message_id(1024);
	older.set_oldest_message_id(1);
	older.set_has_older(false);
	for (unsigned int messageID = 1; messageID <= 512; ++messageID) {
		*older.add_messages() =
			makeMessage(messageID, messageID * 10, MumbleProto::TextChannel, 88);
	}
	gateway.handleIncomingHistory(older);

	const PersistentChatScopeStateSnapshot merged = controller.activeSnapshot();
	QCOMPARE(merged.messagesRevision, loaded.messagesRevision + 1);
	QCOMPARE(merged.prependedMessageCount, qsizetype(512));
	QCOMPARE(merged.messages.size(), 1024);
	QCOMPARE(merged.messages.constFirst().message_id(), 1U);
	QCOMPARE(merged.messages.constLast().message_id(), 1024U);
	QCOMPARE(merged.loadingState, PersistentChatLoadingState::Idle);
	resetGatewayRequestState();
}

void TestPersistentChatController::appliesEmbedUpdatesToCachedMessages() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 33), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 33, { makeMessage(30, 3000, MumbleProto::TextChannel, 33) }, 0, 30, false));

	QVERIFY(controller.applyEmbedState(
		makeEmbedState(MumbleProto::TextChannel, 33, 30, 30, QStringLiteral("https://example.com/preview"))));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().embeds_size(), 1);
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().embeds(0).canonical_url().c_str()),
			 QStringLiteral("https://example.com/preview"));
	resetGatewayRequestState();
}

void TestPersistentChatController::preservesReplyMetadataFromHistory() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 44), false);

	MumbleProto::ChatMessage root = makeMessage(40, 4000, MumbleProto::TextChannel, 44, QStringLiteral("root"));
	MumbleProto::ChatMessage reply =
		makeMessage(41, 4010, MumbleProto::TextChannel, 44, QStringLiteral("reply"));
	reply.set_reply_to_message_id(40);
	reply.set_reply_actor_name("Alice");
	reply.set_reply_snippet("root");
	*reply.add_reactions() = makeReaction(QString::fromUtf8("🔥"), 2, true);

	gateway.handleIncomingHistory(makeHistory(MumbleProto::TextChannel, 44, { root, reply }, 0, 40, false));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 2);
	QCOMPARE(snapshot.messages.back().reply_to_message_id(), 40U);
	QCOMPARE(QString::fromUtf8(snapshot.messages.back().reply_actor_name().c_str()), QStringLiteral("Alice"));
	QCOMPARE(QString::fromUtf8(snapshot.messages.back().reply_snippet().c_str()), QStringLiteral("root"));
	QCOMPARE(snapshot.messages.back().reactions_size(), 1);
	QCOMPARE(QString::fromUtf8(snapshot.messages.back().reactions(0).emoji().c_str()), QString::fromUtf8("🔥"));
	QCOMPARE(snapshot.messages.back().reactions(0).count(), 2U);
	QCOMPARE(snapshot.messages.back().reactions(0).self_reacted(), true);
	resetGatewayRequestState();
}

void TestPersistentChatController::appliesReactionUpdatesToCachedMessages() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 55), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 55, { makeMessage(50, 5000, MumbleProto::TextChannel, 55) }, 0, 50, false));

	QVERIFY(controller.applyReactionState(
		makeReactionState(MumbleProto::TextChannel, 55, 50, 50,
						  { makeReaction(QString::fromUtf8("👍"), 1, true, { QStringLiteral("Alice") }),
							makeReaction(QString::fromUtf8("🎉"), 3, false,
										 { QStringLiteral("Bob"), QStringLiteral("Carol") }) })));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().reactions_size(), 2);
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().reactions(0).emoji().c_str()), QString::fromUtf8("👍"));
	QCOMPARE(snapshot.messages.front().reactions(0).count(), 1U);
	QCOMPARE(snapshot.messages.front().reactions(0).self_reacted(), true);
	QCOMPARE(snapshot.messages.front().reactions(0).actor_names_size(), 1);
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().reactions(0).actor_names(0).c_str()),
			 QStringLiteral("Alice"));
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().reactions(1).emoji().c_str()), QString::fromUtf8("🎉"));
	QCOMPARE(snapshot.messages.front().reactions(1).count(), 3U);
	QCOMPARE(snapshot.messages.front().reactions(1).self_reacted(), false);
	QCOMPARE(snapshot.messages.front().reactions(1).actor_names_size(), 2);
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().reactions(1).actor_names(0).c_str()),
			 QStringLiteral("Bob"));
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().reactions(1).actor_names(1).c_str()),
			 QStringLiteral("Carol"));
	resetGatewayRequestState();
}

void TestPersistentChatController::removesDeletedMessageFromCachedSnapshot() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 66), false);

	MumbleProto::ChatMessage original =
		makeMessage(60, 6000, MumbleProto::TextChannel, 66, QStringLiteral("remove me"));
	original.set_reply_to_message_id(59);
	*original.add_reactions() = makeReaction(QString::fromUtf8("👍"), 1, true);
	gateway.handleIncomingHistory(makeHistory(MumbleProto::TextChannel, 66, { original }, 0, 60, false));

	MumbleProto::ChatMessage tombstone = original;
	tombstone.clear_body_text();
	tombstone.clear_reply_to_message_id();
	tombstone.clear_reactions();
	tombstone.set_message("");
	tombstone.set_deleted_at(6100);
	gateway.handleIncomingMessage(tombstone);

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 0);
	resetGatewayRequestState();
}

void TestPersistentChatController::deniedHistoryMatchesRequestAndPreservesCache() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 7);
	controller.setActiveScope(key, false);
	auto history = makeHistory(key.scope, key.scopeID, { makeMessage(70, 7000, key.scope, key.scopeID) }, 70, 70, true);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	const auto cached = controller.activeSnapshot();
	QVERIFY(controller.requestOlderForActiveScope());
	const quint64 olderID = g_lastOlderRequestID;
	QVERIFY(olderID != history.request_id());

	auto denied = makeHistory(key.scope, key.scopeID, {}, 0, 0, false);
	denied.set_error(MumbleProto::ChatHistoryResponse_Error_PermissionDenied);
	denied.set_error_message("History access denied");
	denied.set_request_id(history.request_id());
	gateway.handleIncomingHistory(denied);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Older);
	denied.set_request_id(olderID);
	denied.set_scope_id(8);
	gateway.handleIncomingHistory(denied);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Older);
	denied.set_scope_id(key.scopeID);
	gateway.handleIncomingHistory(denied);
	const auto failed = controller.activeSnapshot();
	QCOMPARE(failed.loadingState, PersistentChatLoadingState::Idle);
	QCOMPARE(failed.errorMessage, QStringLiteral("History access denied"));
	QVERIFY(failed.initialLoaded);
	QVERIFY(failed.hasOlder);
	QCOMPARE(failed.oldestLoadedMessageId, cached.oldestLoadedMessageId);
	QCOMPARE(failed.lastReadMessageId, cached.lastReadMessageId);
	QCOMPARE(failed.messagesRevision, cached.messagesRevision);
	QCOMPARE(failed.messages.size(), cached.messages.size());

	QVERIFY(controller.requestOlderForActiveScope());
	QVERIFY(g_lastOlderRequestID != olderID);
	QVERIFY(controller.activeSnapshot().errorMessage.isEmpty());
	gateway.handleIncomingHistory(denied);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Older);
	auto older = makeHistory(key.scope, key.scopeID, { makeMessage(60, 6000, key.scope, key.scopeID) }, 70, 60, false);
	older.set_request_id(g_lastOlderRequestID);
	gateway.handleIncomingHistory(older);
	QCOMPARE(controller.activeSnapshot().messages.size(), 2);
	QVERIFY(!controller.activeSnapshot().hasOlder);
}

void TestPersistentChatController::correlatedTimeoutAllowsSafeRetry() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller(nullptr, 30);
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	auto history = makeHistory(key.scope, key.scopeID, { makeMessage(70, 7000, key.scope, key.scopeID) }, 0, 70, true);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	QVERIFY(controller.requestOlderForActiveScope());
	const quint64 timedOutID = g_lastOlderRequestID;
	QTRY_COMPARE_WITH_TIMEOUT(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle, 1000);
	QVERIFY(!controller.activeSnapshot().errorMessage.isEmpty());
	QVERIFY(controller.activeSnapshot().hasOlder);
	QCOMPARE(controller.activeSnapshot().messages.size(), 1);
	QVERIFY(controller.requestOlderForActiveScope());
	auto late = makeHistory(key.scope, key.scopeID, { makeMessage(60, 6000, key.scope, key.scopeID) }, 0, 60, false);
	late.set_request_id(timedOutID);
	gateway.handleIncomingHistory(late);
	QCOMPARE(controller.activeSnapshot().messages.size(), 1);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Older);
	late.set_request_id(g_lastOlderRequestID);
	gateway.handleIncomingHistory(late);
	QCOMPARE(controller.activeSnapshot().messages.size(), 2);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
}

void TestPersistentChatController::legacyTimeoutRequiresReconnect() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller(nullptr, 30);
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	QTRY_COMPARE_WITH_TIMEOUT(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle, 1000);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	controller.setActiveScope(key, true);
	QCOMPARE(g_initialRequestCount, 1);
	QVERIFY(controller.activeSnapshot().errorMessage.contains(QStringLiteral("Reconnect")));
	auto late = makeHistory(key.scope, key.scopeID, { makeMessage(70, 7000, key.scope, key.scopeID) }, 0, 70, false);
	gateway.handleIncomingHistory(late);
	QCOMPARE(controller.activeSnapshot().messages.size(), 0);
	++g_gatewayConnectionGeneration;
	emit gateway.connectionChanged(g_gatewayConnectionGeneration);
	controller.setActiveScope(key, true);
	QCOMPARE(g_initialRequestCount, 2);
	emit gateway.historyReceived(late, g_gatewayConnectionGeneration - 1);
	QCOMPARE(controller.activeSnapshot().messages.size(), 0);
	gateway.handleIncomingHistory(late);
	QVERIFY(controller.activeSnapshot().initialLoaded);
	QCOMPARE(controller.activeSnapshot().messages.size(), 1);
}

void TestPersistentChatController::resetRejectsLateCorrelatedReply() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	auto history = makeHistory(key.scope, key.scopeID, {}, 0, 0, false);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	controller.setActiveScope(key, true);
	const quint64 oldID = g_lastInitialRequestID;
	controller.reset();
	controller.setActiveScope(key, false);
	QVERIFY(g_lastInitialRequestID != oldID);
	history.set_request_id(oldID);
	gateway.handleIncomingHistory(history);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	history.clear_request_id();
	gateway.handleIncomingHistory(history);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	QVERIFY(controller.activeSnapshot().initialLoaded);
}

void TestPersistentChatController::connectionChangeReleasesAllPendingRequests() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const auto first = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	const auto second = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 8);
	controller.setActiveScope(first, false);
	controller.setActiveScope(second, false);
	++g_gatewayConnectionGeneration;
	emit gateway.connectionChanged(g_gatewayConnectionGeneration);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
	QVERIFY(!controller.activeSnapshot().errorMessage.isEmpty());
	controller.setActiveScope(first, false);
	QCOMPARE(g_initialRequestCount, 3);
	auto history = makeHistory(first.scope, first.scopeID, {}, 0, 0, false);
	history.set_request_id(g_lastInitialRequestID);
	emit gateway.historyReceived(history, g_gatewayConnectionGeneration - 1);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	gateway.handleIncomingHistory(history);
	QVERIFY(controller.activeSnapshot().initialLoaded);
	controller.setActiveScope(second, false);
	QCOMPARE(g_initialRequestCount, 4);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Initial);
}

void TestPersistentChatController::unsentRequestsReleaseLoadingState() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	g_initialRequestSucceeds = false;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
	QVERIFY(!controller.activeSnapshot().errorMessage.isEmpty());
	g_initialRequestSucceeds = true;
	controller.setActiveScope(key, false);
	QCOMPARE(g_initialRequestCount, 2);
	auto history = makeHistory(key.scope, key.scopeID, { makeMessage(70, 7000, key.scope, key.scopeID) }, 0, 70, true);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	g_olderRequestSucceeds = false;
	QVERIFY(!controller.requestOlderForActiveScope());
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
	QVERIFY(controller.activeSnapshot().hasOlder);
	QCOMPARE(controller.activeSnapshot().messages.size(), 1);
	g_olderRequestSucceeds = true;
	QVERIFY(controller.requestOlderForActiveScope());
}

void TestPersistentChatController::warmupTimeoutAndFailureReleaseEachScope() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller(nullptr, 30);
	controller.setGateway(&gateway);
	const auto first = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	const auto second = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 8);
	controller.warmupScopes({ first, second });
	QVERIFY(g_lastWarmupRequestID != 0);
	auto denied = makeHistory(first.scope, first.scopeID, {}, 0, 0, false);
	denied.set_request_id(g_lastWarmupRequestID);
	denied.set_error(MumbleProto::ChatHistoryResponse_Error_PermissionDenied);
	gateway.handleIncomingHistory(denied);
	controller.setActiveScope(first, false, false);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	controller.setActiveScope(second, false, false);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Initial);
	QTRY_COMPARE_WITH_TIMEOUT(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle, 1000);
	QVERIFY(!controller.activeSnapshot().initialLoaded);
	controller.setActiveScope(second, false);
	QCOMPARE(g_initialRequestCount, 1); // The first tagged failure established correlation for this transport.

	const auto third = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 9);
	g_warmupRequestSucceeds = false;
	controller.warmupScopes({ third });
	controller.setActiveScope(third, false, false);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
	QVERIFY(!controller.activeSnapshot().errorMessage.isEmpty());
	QVERIFY(!controller.activeSnapshot().initialLoaded);
}

void TestPersistentChatController::warmupBatchMatchesServerLimit() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	QList< PersistentChatScopeKey > keys;
	for (unsigned int i = 1; i <= 25; ++i) {
		keys.push_back(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, i));
	}
	controller.warmupScopes(keys);
	QCOMPARE(g_lastWarmupScopes.size(), 20);
	controller.setActiveScope(keys.back(), false);
	QCOMPARE(g_initialRequestCount, 1); // An unsent scope must not have an in-flight warmup marker.
}

void TestPersistentChatController::completedRequestTimeoutDoesNotCancelItsSuccessor() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller(nullptr, 300);
	controller.setGateway(&gateway);
	const auto key = PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7);
	controller.setActiveScope(key, false);
	QTest::qWait(180);
	auto history = makeHistory(key.scope, key.scopeID, {}, 0, 0, false);
	history.set_request_id(g_lastInitialRequestID);
	gateway.handleIncomingHistory(history);
	controller.setActiveScope(key, true);
	const quint64 currentID = g_lastInitialRequestID;
	QTest::qWait(160); // The retired request's timer has fired, while the new request still has time left.
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Refreshing);
	QVERIFY(controller.activeSnapshot().errorMessage.isEmpty());
	history.set_request_id(currentID);
	gateway.handleIncomingHistory(history);
	QCOMPARE(controller.activeSnapshot().loadingState, PersistentChatLoadingState::Idle);
}

namespace {
	MumbleProto::ChatMessage makeMarkdownMessage(unsigned int messageID, quint64 createdAt, unsigned int authorID,
												 const QString &body) {
		MumbleProto::ChatMessage message =
			makeMessage(messageID, createdAt, MumbleProto::TextChannel, 90, body);
		message.set_actor_user_id(authorID);
		message.set_actor_name(QStringLiteral("User %1").arg(authorID).toUtf8().constData());
		message.set_body_format(MumbleProto::ChatBodyFormatMarkdownLite);
		return message;
	}

	QVariantMap blockAt(const ChatTimelineRow &row, int index) {
		return row.bodyBlocks.value(index).toMap();
	}

	ChatTimelineRow rowWithKey(quint32 messageID, quint64 createdAt) {
		ChatTimelineRow row;
		row.key.createdAt = createdAt;
		row.key.threadId  = messageID;
		row.key.messageId = messageID;
		row.plainText     = QStringLiteral("message %1").arg(messageID);
		row.authorName    = QStringLiteral("Alice");
		return row;
	}

	QList< quint32 > modelMessageIDs(const ChatRowModel &model) {
		QList< quint32 > ids;
		for (int row = 0; row < model.count(); ++row) {
			ids << model.rowAt(row).key.messageId;
		}
		return ids;
	}
} // namespace

void TestPersistentChatController::reportsActiveMessageChanges() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	QList< PersistentChatMessageChange > changes;
	connect(&controller, &PersistentChatController::activeMessagesChanged, &controller,
			[&changes](const PersistentChatMessageChange &change) { changes << change; });

	const PersistentChatScopeKey key = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 90);
	controller.setActiveScope(key, false);
	QCOMPARE(changes.size(), 1);
	QVERIFY(changes.last().kind == PersistentChatMessageChange::Kind::Reset);
	QCOMPARE(changes.last().key.cacheKey(), key.cacheKey());

	gateway.handleIncomingHistory(makeHistory(MumbleProto::TextChannel, 90,
											  { makeMessage(1, 100, MumbleProto::TextChannel, 90),
												makeMessage(2, 110, MumbleProto::TextChannel, 90) },
											  0, 1, false));
	QCOMPARE(changes.size(), 2);
	QVERIFY(changes.last().kind == PersistentChatMessageChange::Kind::Messages);
	QCOMPARE(changes.last().identities.size(), 2);

	MumbleProto::ChatMessage live = makeMessage(3, 120, MumbleProto::TextChannel, 90);
	gateway.handleIncomingMessage(live);
	QCOMPARE(changes.size(), 3);
	QCOMPARE(changes.last().identities, QVector< quint64 >{ PersistentChatMessageChange::identity(live) });

	QVERIFY(controller.applyReactionState(
		makeReactionState(MumbleProto::TextChannel, 90, 3, 3, { makeReaction(QStringLiteral("+1"), 1, true) })));
	QCOMPARE(changes.size(), 4);
	QVERIFY(changes.last().kind == PersistentChatMessageChange::Kind::Patched);
	QVERIFY(controller.applyEmbedState(
		makeEmbedState(MumbleProto::TextChannel, 90, 3, 3, QStringLiteral("https://example.com/"))));
	QCOMPARE(changes.size(), 5);
	QVERIFY(changes.last().kind == PersistentChatMessageChange::Kind::Patched);

	MumbleProto::ChatMessage tombstone = live;
	tombstone.set_deleted_at(130);
	gateway.handleIncomingMessage(tombstone);
	QCOMPARE(changes.size(), 6);
	QCOMPARE(changes.last().identities, QVector< quint64 >{ PersistentChatMessageChange::identity(live) });

	// Traffic for another scope updates its cache but is not an active-scope change.
	gateway.handleIncomingMessage(makeMessage(4, 140, MumbleProto::TextChannel, 91));
	QCOMPARE(changes.size(), 6);

	controller.clearActiveScope();
	QCOMPARE(changes.size(), 7);
	QVERIFY(changes.last().kind == PersistentChatMessageChange::Kind::Reset);
	QVERIFY(!changes.last().key.valid);
	resetGatewayRequestState();
}

void TestPersistentChatController::rowBuilderSplitsMarkdownIntoBlocks() {
	const ChatRowBuilder::Body body = ChatRowBuilder::buildBody(
		QStringLiteral("Intro **bold** and `code()`\nsecond line\n\n> quoted\n> reply\n## Release notes\n```\nlet x = 1;\n```"),
		true);
	QCOMPARE(body.blocks.size(), 4);

	const QVariantMap intro = body.blocks.at(0).toMap();
	QCOMPARE(intro.value(QStringLiteral("kind")).toString(), QStringLiteral("p"));
	// Inline code needs RichText; StyledText has no monospace.
	QCOMPARE(intro.value(QStringLiteral("format")).toString(), QStringLiteral("rich"));
	QVERIFY(intro.value(QStringLiteral("text")).toString().contains(QStringLiteral("<b>bold</b>")));
	QVERIFY(intro.value(QStringLiteral("text")).toString().contains(QStringLiteral("<code>code()</code>")));
	// A single newline the user typed is kept.
	QVERIFY(intro.value(QStringLiteral("text")).toString().contains(QStringLiteral("<br>second line")));

	const QVariantMap quote = body.blocks.at(1).toMap();
	QCOMPARE(quote.value(QStringLiteral("kind")).toString(), QStringLiteral("quote"));
	QCOMPARE(quote.value(QStringLiteral("format")).toString(), QStringLiteral("plain"));
	QCOMPARE(quote.value(QStringLiteral("text")).toString(), QStringLiteral("quoted\nreply"));

	const QVariantMap heading = body.blocks.at(2).toMap();
	QCOMPARE(heading.value(QStringLiteral("kind")).toString(), QStringLiteral("h"));
	QCOMPARE(heading.value(QStringLiteral("level")).toInt(), 2);
	QCOMPARE(heading.value(QStringLiteral("text")).toString(), QStringLiteral("Release notes"));

	const QVariantMap code = body.blocks.at(3).toMap();
	QCOMPARE(code.value(QStringLiteral("kind")).toString(), QStringLiteral("code"));
	QCOMPARE(code.value(QStringLiteral("text")).toString(), QStringLiteral("let x = 1;"));

	const ChatRowBuilder::Body plain = ChatRowBuilder::buildBody(QStringLiteral("just words"), true);
	QCOMPARE(plain.blocks.size(), 1);
	QCOMPARE(plain.blocks.first().toMap().value(QStringLiteral("format")).toString(), QStringLiteral("plain"));
}

void TestPersistentChatController::rowBuilderSanitizesLinks() {
	const ChatRowBuilder::Body markdown =
		ChatRowBuilder::buildBody(QStringLiteral("[bad](javascript:alert(1)) and [good](https://example.com/ok)"), true);
	const QString markdownHtml = markdown.blocks.first().toMap().value(QStringLiteral("text")).toString();
	// No link may carry a script scheme. (The Markdown converter prefixes scheme-less
	// targets with http://, so "javascript" can survive as an http host name.)
	static const QRegularExpression scriptLink(QStringLiteral("href\\s*=\\s*[\"']?\\s*javascript:"),
											   QRegularExpression::CaseInsensitiveOption);
	QVERIFY2(!scriptLink.match(markdownHtml).hasMatch(), qPrintable(markdownHtml));
	QVERIFY(markdownHtml.contains(QStringLiteral("href=\"https://example.com/ok\"")));

	const ChatRowBuilder::Body plain =
		ChatRowBuilder::buildBody(QStringLiteral("see https://example.com/a?b=1&c=2 now"), false);
	const QVariantMap plainBlock = plain.blocks.first().toMap();
	QCOMPARE(plainBlock.value(QStringLiteral("format")).toString(), QStringLiteral("styled"));
	QVERIFY(plainBlock.value(QStringLiteral("text")).toString().contains(
		QStringLiteral("<a href=\"https://example.com/a?b=1&amp;c=2\">")));

	const ChatRowBuilder::Body legacy = ChatRowBuilder::buildBody(
		QStringLiteral("<p><a href=\"javascript:alert(1)\">bad</a> <a href=\"https://example.com/\">good</a>"
					   "<script>alert(2)</script></p>"),
		false);
	const QString legacyHtml = legacy.blocks.first().toMap().value(QStringLiteral("text")).toString();
	QVERIFY2(!legacyHtml.contains(QStringLiteral("javascript"), Qt::CaseInsensitive), qPrintable(legacyHtml));
	QVERIFY(!legacyHtml.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
	QVERIFY(legacyHtml.contains(QStringLiteral("bad")));
	QVERIFY(legacyHtml.contains(QStringLiteral("<a href=\"https://example.com/\">good</a>")));
}

void TestPersistentChatController::rowBuilderKeepsLegacyFormattingAndSplitsInlineImages() {
	MumbleProto::ChatMessage message = makeMessage(
		5, 500, MumbleProto::Channel, 1,
		QStringLiteral("<p><b>bold</b> <span style=\" font-family:'Courier New';\">mono()</span>"
					   "<img src=\"data:image/png;base64,iVBORw0KGgo=\" /></p>"));
	message.set_body_format(MumbleProto::ChatBodyFormatPlainText);
	const ChatTimelineRow row = ChatRowBuilder().build(message);

	QCOMPARE(row.bodyBlocks.size(), 1);
	const QVariantMap block = blockAt(row, 0);
	QCOMPARE(block.value(QStringLiteral("format")).toString(), QStringLiteral("rich"));
	QVERIFY(block.value(QStringLiteral("text")).toString().contains(QStringLiteral("<b>bold</b>")));
	QVERIFY(block.value(QStringLiteral("text")).toString().contains(QStringLiteral("<code>mono()</code>")));
	QVERIFY(!block.value(QStringLiteral("text")).toString().contains(QStringLiteral("data:image")));
	// The pasted image becomes media with a fixed default box until it is decoded.
	QCOMPARE(row.media.size(), 1);
	QVERIFY(row.media.first().kind == ChatTimelineMedia::Kind::InlineImage);
	QVERIFY(!row.media.first().geometryKnown);
	QVERIFY(row.kind == ChatTimelineRowKind::Media);
}

void TestPersistentChatController::rowBuilderClassifiesMediaAndCards() {
	const auto withAsset = [](MumbleProto::ChatMessage message, MumbleProto::ChatAssetKind kind, unsigned int width,
							  unsigned int height) {
		MumbleProto::ChatAssetRef *asset = message.add_attachments();
		asset->set_asset_id(static_cast< unsigned int >(message.attachments_size()) + 100);
		asset->set_kind(kind);
		asset->set_width(width);
		asset->set_height(height);
		return message;
	};
	const ChatRowBuilder builder;
	const MumbleProto::ChatMessage base = makeMessage(7, 700, MumbleProto::Channel, 1, QString());

	const ChatTimelineRow image = builder.build(withAsset(base, MumbleProto::ChatAssetKindImage, 1920, 1080));
	QVERIFY(image.kind == ChatTimelineRowKind::Media);
	QVERIFY(image.media.first().geometryKnown);
	QCOMPARE(image.media.first().width, 1920);

	const ChatTimelineRow unknown = builder.build(withAsset(base, MumbleProto::ChatAssetKindImage, 0, 0));
	QVERIFY(!unknown.media.first().geometryKnown);

	const ChatTimelineRow gallery = builder.build(withAsset(
		withAsset(base, MumbleProto::ChatAssetKindImage, 800, 600), MumbleProto::ChatAssetKindImage, 600, 800));
	QVERIFY(gallery.kind == ChatTimelineRowKind::Gallery);

	const ChatTimelineRow video = builder.build(withAsset(base, MumbleProto::ChatAssetKindVideo, 1280, 720));
	QVERIFY(video.kind == ChatTimelineRowKind::Video);

	const ChatTimelineRow file = builder.build(withAsset(base, MumbleProto::ChatAssetKindDocument, 0, 0));
	QVERIFY(file.kind == ChatTimelineRowKind::File);

	MumbleProto::ChatMessage linked = base;
	linked.set_body_text("https://example.com/article");
	MumbleProto::ChatEmbedRef *embed = linked.add_embeds();
	embed->set_canonical_url("https://example.com/article");
	embed->set_status(MumbleProto::ChatEmbedStatusReady);
	embed->set_title("Article");
	embed->set_preview_asset_id(55);
	const ChatTimelineRow card = builder.build(linked);
	QVERIFY(card.kind == ChatTimelineRowKind::LinkCard);
	QVERIFY(card.card.status == ChatTimelineCard::Status::Ready);
	QVERIFY(card.card.layout == ChatTimelineCard::Layout::SmallThumbnail);
	QCOMPARE(card.card.title, QStringLiteral("Article"));

	// A wide preview of known size takes the large slot; a tall one stays small.
	embed->set_preview_width(1200);
	embed->set_preview_height(630);
	const ChatTimelineRow wideCard = builder.build(linked);
	QVERIFY(wideCard.card.layout == ChatTimelineCard::Layout::LargeThumbnail);
	QCOMPARE(wideCard.card.previewWidth, 1200);
	embed->set_preview_width(400);
	embed->set_preview_height(600);
	QVERIFY(builder.build(linked).card.layout == ChatTimelineCard::Layout::SmallThumbnail);

	embed->set_status(MumbleProto::ChatEmbedStatusFailed);
	QVERIFY(builder.build(linked).kind == ChatTimelineRowKind::Text);
}

void TestPersistentChatController::rowBuilderMarksSystemAndOwnMessages() {
	ChatRowBuilder::Options options;
	options.ownUserId = 42;
	const ChatRowBuilder builder(options);

	const ChatTimelineRow system =
		builder.build(makeMessage(8, 800, MumbleProto::Channel, 1, QStringLiteral("[system] Alice joined")));
	QVERIFY(system.kind == ChatTimelineRowKind::System);
	QCOMPARE(system.plainText, QStringLiteral("Alice joined"));

	MumbleProto::ChatMessage mine = makeMessage(9, 900, MumbleProto::Channel, 1);
	mine.set_actor_user_id(42);
	QVERIFY(builder.build(mine).own);
	QVERIFY(!builder.build(makeMessage(10, 900, MumbleProto::Channel, 1)).own);
}

void TestPersistentChatController::rowBuilderDecoratesAcrossPageBoundaries() {
	// Two authors, a five-minute gap and a day change, spread over page boundaries.
	const quint64 day = 1784000000;
	QVector< MumbleProto::ChatMessage > messages;
	const quint64 offsets[]   = { 0, 30, 60, 400, 430, 460, 86400, 86430, 86460, 86490, 86520, 86900 };
	const unsigned int authors[] = { 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 1, 1 };
	for (int i = 0; i < 12; ++i) {
		messages << makeMarkdownMessage(static_cast< unsigned int >(i + 1), day + offsets[i], authors[i],
										QStringLiteral("message %1").arg(i));
	}
	const ChatRowBuilder builder;
	const QVector< ChatTimelineRow > single = builder.buildRange(messages, 0, 11);
	// The same messages in three-message pages, newest page first, as paging delivers them.
	QVector< ChatTimelineRow > paged;
	for (int first = 9; first >= 0; first -= 3) {
		paged = builder.buildRange(messages, first, first + 2) + paged;
	}
	QCOMPARE(paged.size(), single.size());
	for (int i = 0; i < single.size(); ++i) {
		QCOMPARE(paged.at(i).groupStart, single.at(i).groupStart);
		QCOMPARE(paged.at(i).dateLabel, single.at(i).dateLabel);
	}
	QVERIFY(single.at(0).groupStart);
	QVERIFY(!single.at(1).groupStart);
	QVERIFY(single.at(2).groupStart);     // other author
	QVERIFY(single.at(3).groupStart);     // more than five minutes later
	QVERIFY(!single.at(4).groupStart);
	QVERIFY(!single.at(6).dateLabel.isEmpty()); // next day
	QVERIFY(single.at(7).dateLabel.isEmpty());
	QVERIFY(single.at(11).groupStart);    // 380 s after the previous message
}

void TestPersistentChatController::rowModelKeepsNewestFirstAndReportsExactRoles() {
	ChatRowModel model;
	model.resetScope(QStringLiteral("3:90"));
	QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

	model.upsertRows({ rowWithKey(2, 200), rowWithKey(1, 100), rowWithKey(3, 300) });
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 3, 2, 1 }));
	QCOMPARE(inserted.size(), 1); // one contiguous run, one insertion

	// An older page appends at the end in one insertion; a live message goes to row 0.
	inserted.clear();
	model.upsertRows({ rowWithKey(0, 50), rowWithKey(4, 400) });
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 4, 3, 2, 1, 0 }));
	QCOMPARE(inserted.size(), 2);
	QCOMPARE(inserted.at(0).at(1).toInt(), 0);
	QCOMPARE(inserted.at(1).at(1).toInt(), 4);

	// Replacing with identical content signals nothing; a reaction change reports only reactions.
	model.upsertRows({ rowWithKey(2, 200) });
	QCOMPARE(changed.size(), 0);
	ChatTimelineRow reacted = rowWithKey(2, 200);
	reacted.reactions << ChatTimelineReaction{ QStringLiteral("+1"), 2, true, {} };
	model.upsertRows({ reacted });
	QCOMPARE(changed.size(), 1);
	QCOMPARE(changed.first().at(0).toModelIndex().row(), 2);
	QCOMPARE(qvariant_cast< QList< int > >(changed.first().at(2)), (QList< int >{ ChatRowModel::ReactionsRole }));

	QCOMPARE(model.rowForMessage(1, 1), 3);
	model.removeMessages({ rowWithKey(3, 300).key.identity() });
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 4, 2, 1, 0 }));
	QCOMPARE(model.rowForMessage(1, 1), 2);
	QCOMPARE(model.rowsMatching(QStringLiteral("message 1")), (QVariantList{ 2 }));

	model.resetScope(QStringLiteral("3:91"));
	QCOMPARE(model.count(), 0);
}

void TestPersistentChatController::rowModelAppliesExtrasBeforeAndAfterRows() {
	ChatRowModel model;
	model.resetScope(QStringLiteral("3:90"));
	const auto state = [](quint32 id, const QString &avatar, bool canReply) {
		return QVariantMap{ { QStringLiteral("threadId"), id },
							{ QStringLiteral("messageId"), id },
							{ QStringLiteral("avatarUrl"), avatar },
							{ QStringLiteral("canReply"), canReply },
							{ QStringLiteral("attachments"), QVariantList{ QVariantMap{ { QStringLiteral("id"), 7 } } } } };
	};
	const auto roleValue = [&model](int row, int role) { return model.data(model.index(row), role); };

	// Extras for a row the worker has not delivered yet are kept until it arrives.
	model.setExtras(state(2, QStringLiteral("image://mumble/avatar/2"), true));
	model.upsertRows({ rowWithKey(1, 100), rowWithKey(2, 200) });
	QCOMPARE(roleValue(0, ChatRowModel::StableIdRole).toString(), QStringLiteral("2"));
	QCOMPARE(roleValue(0, ChatRowModel::AvatarUrlRole).toString(), QStringLiteral("image://mumble/avatar/2"));
	QVERIFY(roleValue(0, ChatRowModel::CanReplyRole).toBool());
	QCOMPARE(roleValue(0, ChatRowModel::AttachmentsRole).toList().size(), 1);
	QVERIFY(roleValue(1, ChatRowModel::AvatarUrlRole).toString().isEmpty());
	QVERIFY(!roleValue(1, ChatRowModel::CanReplyRole).toBool());

	// Later extras for a shown row report only the roles that changed.
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setExtras(state(2, QStringLiteral("image://mumble/avatar/2"), true));
	QCOMPARE(changed.size(), 0);
	model.setExtras(state(2, QStringLiteral("image://mumble/avatar/2"), false));
	QCOMPARE(changed.size(), 1);
	QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
	QCOMPARE(qvariant_cast< QList< int > >(changed.first().at(2)), (QList< int >{ ChatRowModel::CanReplyRole }));
}

void TestPersistentChatController::rowModelSearchesNewestFirstAndKeepsTheCurrentMatch() {
	ChatRowModel model;
	model.resetScope(QStringLiteral("3:90"));
	model.upsertRows({ rowWithKey(1, 100), rowWithKey(2, 200), rowWithKey(3, 300), rowWithKey(12, 1200) });
	QSignalSpy current(&model, &ChatRowModel::currentMatchChanged);

	// "message 1" matches messages 1 and 12; the newest is selected first.
	model.setQuery(QStringLiteral("  MESSAGE 1 "));
	QCOMPARE(model.matchCount(), 2);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("12"));
	QCOMPARE(model.currentMatchRow(), 0);
	QVERIFY(current.size() > 0);

	QVERIFY(model.nextMatch());
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("1"));
	QCOMPARE(model.currentMatchRow(), 3);
	QVERIFY(model.nextMatch());
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("12"));
	QVERIFY(model.previousMatch());
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("1"));

	// A newer row arriving keeps the current match on the same message.
	model.upsertRows({ rowWithKey(13, 1300) });
	QCOMPARE(model.matchCount(), 3);
	QCOMPARE(model.currentMatchStableId(), QStringLiteral("1"));
	QCOMPARE(model.currentMatchRow(), 4);
	QCOMPARE(model.rowForStableId(QStringLiteral("13")), 0);

	model.clearSearch();
	QCOMPARE(model.matchCount(), 0);
	QCOMPARE(model.currentMatchRow(), -1);
	QVERIFY(model.currentMatchStableId().isEmpty());
}

void TestPersistentChatController::timelineFeedMirrorsActiveScope() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	ChatRowModel model;
	ChatTimelineFeed feed(&controller, &model);
	const auto settled = [&feed]() { return feed.pendingJobs() == 0; };

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 90), false);
	MumbleProto::ChatHistoryResponse page;
	page.set_scope(MumbleProto::TextChannel);
	page.set_scope_id(90);
	page.set_has_older(true);
	for (unsigned int id = 101; id <= 220; ++id) {
		*page.add_messages() = makeMarkdownMessage(id, 10000 + id * 20, id % 3, QStringLiteral("page %1").arg(id));
	}
	gateway.handleIncomingHistory(page);
	QTRY_VERIFY(settled());
	QCOMPARE(model.count(), 120);
	QCOMPARE(model.rowAt(0).key.messageId, 220U);
	QCOMPARE(model.rowAt(119).key.messageId, 101U);

	// Older history appends below the newest rows.
	QVERIFY(controller.requestOlderForActiveScope());
	MumbleProto::ChatHistoryResponse older;
	older.set_scope(MumbleProto::TextChannel);
	older.set_scope_id(90);
	for (unsigned int id = 51; id <= 100; ++id) {
		*older.add_messages() = makeMarkdownMessage(id, 10000 + id * 20, id % 3, QStringLiteral("older %1").arg(id));
	}
	gateway.handleIncomingHistory(older);
	QTRY_VERIFY(settled());
	QCOMPARE(model.count(), 170);
	QCOMPARE(model.rowAt(0).key.messageId, 220U);
	QCOMPARE(model.rowAt(169).key.messageId, 51U);
	// The oldest row of the newer page was decorated against its true older neighbour.
	QCOMPARE(model.rowAt(119).groupStart, ChatRowBuilder().buildRange(controller.activeSnapshot().messages, 49, 50)
											  .last()
											  .groupStart);

	// A live message lands at row 0.
	gateway.handleIncomingMessage(makeMarkdownMessage(221, 10000 + 221 * 20, 1, QStringLiteral("live")));
	QTRY_VERIFY(settled());
	QCOMPARE(model.rowAt(0).key.messageId, 221U);

	// A reaction patch changes only that row's reactions.
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	QVERIFY(controller.applyReactionState(makeReactionState(MumbleProto::TextChannel, 90, 220, 220,
															{ makeReaction(QStringLiteral("+1"), 3, false) })));
	QTRY_VERIFY(settled());
	QCOMPARE(changed.size(), 1);
	QCOMPARE(qvariant_cast< QList< int > >(changed.first().at(2)), (QList< int >{ ChatRowModel::ReactionsRole }));
	QCOMPARE(model.rowAt(1).reactions.first().count, 3U);

	// Deleting a message removes its row and re-decorates the one after it.
	MumbleProto::ChatMessage tombstone = makeMarkdownMessage(220, 10000 + 220 * 20, 220 % 3, QString());
	tombstone.set_deleted_at(99999);
	gateway.handleIncomingMessage(tombstone);
	QTRY_VERIFY(settled());
	QCOMPARE(model.count(), 170);
	QCOMPARE(model.rowAt(0).key.messageId, 221U);
	QCOMPARE(model.rowAt(1).key.messageId, 219U);
	QCOMPARE(model.rowForMessage(220, 220), -1);
	resetGatewayRequestState();
}

void TestPersistentChatController::timelineFeedDropsWorkForRetiredScope() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	ChatRowModel model;
	ChatTimelineFeed feed(&controller, &model);

	// Cache a large scope, then switch into it and straight out again.
	const PersistentChatScopeKey large = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 90);
	controller.setActiveScope(large, false);
	MumbleProto::ChatHistoryResponse page;
	page.set_scope(MumbleProto::TextChannel);
	page.set_scope_id(90);
	for (unsigned int id = 1; id <= 2000; ++id) {
		*page.add_messages() =
			makeMarkdownMessage(id, 10000 + id * 20, id % 5, QStringLiteral("**bold** %1 `code`").arg(id));
	}
	gateway.handleIncomingHistory(page);
	QTRY_VERIFY(feed.pendingJobs() == 0);
	QCOMPARE(model.count(), 2000);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 91), false);
	controller.setActiveScope(large, false);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 91), false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 91, { makeMessage(5000, 90000, MumbleProto::TextChannel, 91) }, 0,
					5000, false));
	QTRY_VERIFY(feed.pendingJobs() == 0);
	// Only the current scope's rows: none of the retired backfill was applied.
	QCOMPARE(model.scopeToken(), PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 91).cacheKey());
	QCOMPARE(model.count(), 1);
	QCOMPARE(model.rowAt(0).key.messageId, 5000U);
	resetGatewayRequestState();
}

namespace {
	ChatLocalEntry logLine(quint64 sequence, qint64 createdAtMs, const QString &text) {
		ChatLocalEntry entry;
		entry.sequence    = sequence;
		entry.createdAtMs = createdAtMs;
		entry.authorName  = QStringLiteral("Server");
		entry.text        = text;
		entry.logLine     = true;
		return entry;
	}
} // namespace

void TestPersistentChatController::rowBuilderBuildsLogLinesAndDirectMessages() {
	const ChatRowBuilder builder;

	const ChatTimelineRow log = builder.buildLocal(logLine(7, 1784000000123, QStringLiteral("Alice <joined>")));
	QVERIFY(log.kind == ChatTimelineRowKind::Log);
	QCOMPARE(log.key.messageId, 7U);
	QCOMPARE(log.key.threadId, 0U);
	QCOMPARE(log.key.createdAt, quint64(1784000000));
	QCOMPARE(blockAt(log, 0).value(QStringLiteral("format")).toString(), QStringLiteral("plain"));
	QCOMPARE(blockAt(log, 0).value(QStringLiteral("text")).toString(), QStringLiteral("Alice <joined>"));

	ChatLocalEntry transient;
	transient.sequence      = 8;
	transient.createdAtMs   = 1784000001000;
	transient.authorName    = QStringLiteral("Bob");
	transient.authorSession = 12;
	transient.own           = true;
	transient.html          = QStringLiteral("<b>hi</b> there");
	transient.deliveryState = QStringLiteral("pending");
	const ChatTimelineRow direct = builder.buildLocal(transient);
	QVERIFY(direct.kind == ChatTimelineRowKind::Text);
	QVERIFY(direct.own);
	QCOMPARE(direct.deliveryState, QStringLiteral("pending"));
	QVERIFY(blockAt(direct, 0).value(QStringLiteral("text")).toString().contains(QStringLiteral("<b>hi</b>")));

	ChatLocalEntry persistent = transient;
	persistent.persistent     = makeMarkdownMessage(900, 1784000002, 3, QStringLiteral("**saved**"));
	persistent.deliveryState  = QStringLiteral("failed");
	persistent.deliveryCanRetry = true;
	const ChatTimelineRow saved = builder.buildLocal(persistent);
	QCOMPARE(saved.key.messageId, 900U);
	QCOMPARE(saved.key.threadId, 900U);
	QVERIFY(blockAt(saved, 0).value(QStringLiteral("text")).toString().contains(QStringLiteral("<b>saved</b>")));
	QCOMPARE(saved.deliveryState, QStringLiteral("failed"));
	QVERIFY(saved.deliveryCanRetry);
	QVERIFY(saved.own);
}

void TestPersistentChatController::timelineFeedSyncsLocalScopes() {
	resetGatewayRequestState();
	g_gatewayReady = true;
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	ChatRowModel model;
	ChatTimelineFeed feed(&controller, &model);
	const auto settled = [&feed]() { return feed.pendingJobs() == 0; };

	const PersistentChatScopeKey room = PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 90);
	controller.setActiveScope(room, false);
	gateway.handleIncomingHistory(makeHistory(
		MumbleProto::TextChannel, 90, { makeMarkdownMessage(1, 100, 1, QStringLiteral("room")) }, 0, 1, false));
	QTRY_VERIFY(settled());
	QCOMPARE(model.count(), 1);

	// The server log replaces the room.
	feed.syncLocal(QStringLiteral("local:serverlog"),
				   { logLine(1, 1784000000000, QStringLiteral("one")), logLine(2, 1784000001000, QStringLiteral("two")),
					 logLine(3, 1784000002000, QStringLiteral("three")) });
	QTRY_VERIFY(settled());
	QCOMPARE(model.scopeToken(), QStringLiteral("local:serverlog"));
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 3, 2, 1 }));

	// A new line inserts at the bottom only.
	QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	feed.upsertLocal(QStringLiteral("local:serverlog"), { logLine(4, 1784000003000, QStringLiteral("four")) });
	QTRY_VERIFY(settled());
	QCOMPARE(inserted.size(), 1);
	QCOMPARE(inserted.first().at(1).toInt(), 0);
	QCOMPARE(changed.size(), 0);
	QCOMPARE(model.count(), 4);

	// A changed entry reports only what changed.
	ChatLocalEntry edited = logLine(2, 1784000001000, QStringLiteral("two (edited)"));
	feed.upsertLocal(QStringLiteral("local:serverlog"), { edited });
	QTRY_VERIFY(settled());
	QCOMPARE(changed.size(), 1);
	const QList< int > roles = qvariant_cast< QList< int > >(changed.first().at(2));
	QVERIFY(roles.contains(ChatRowModel::BodyBlocksRole));
	QVERIFY(!roles.contains(ChatRowModel::GroupStartRole));

	// A full sync without an entry removes it.
	feed.syncLocal(QStringLiteral("local:serverlog"),
				   { logLine(1, 1784000000000, QStringLiteral("one")), edited,
					 logLine(4, 1784000003000, QStringLiteral("four")) });
	QTRY_VERIFY(settled());
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 4, 2, 1 }));

	// The controller letting go of its room, or room traffic, does not touch the log.
	gateway.handleIncomingMessage(makeMarkdownMessage(2, 110, 1, QStringLiteral("room live")));
	controller.clearActiveScope();
	QTRY_VERIFY(settled());
	QCOMPARE(model.scopeToken(), QStringLiteral("local:serverlog"));
	QCOMPARE(model.count(), 3);

	// Activating a room takes over again.
	controller.setActiveScope(room, false);
	QTRY_VERIFY(settled());
	QCOMPARE(model.scopeToken(), room.cacheKey());
	QCOMPARE(modelMessageIDs(model), (QList< quint32 >{ 2, 1 }));
	resetGatewayRequestState();
}

QTEST_MAIN(TestPersistentChatController)
#include "TestPersistentChatController.moc"
