// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include <initializer_list>

#include "mumble/PersistentChatController.h"

namespace {
	bool g_gatewayReady                          = false;
	int g_initialRequestCount                    = 0;
	int g_warmupRequestCount                     = 0;
	bool g_warmupRequestSucceeds                 = true;
	MumbleProto::ChatScope g_lastInitialRequestScope = MumbleProto::Channel;
	unsigned int g_lastInitialRequestScopeID     = 0;
	QList< QPair< MumbleProto::ChatScope, unsigned int > > g_lastWarmupScopes;
	unsigned int g_lastWarmupLimit = 0;

	void resetGatewayRequestState() {
		g_gatewayReady              = false;
		g_initialRequestCount       = 0;
		g_warmupRequestCount        = 0;
		g_warmupRequestSucceeds     = true;
		g_lastInitialRequestScope   = MumbleProto::Channel;
		g_lastInitialRequestScopeID = 0;
		g_lastWarmupScopes.clear();
		g_lastWarmupLimit = 0;
	}
}

PersistentChatGateway::PersistentChatGateway(QObject *parent) : QObject(parent) {
}

void PersistentChatGateway::setServerHandler(ServerHandler *) {
}

ServerHandler *PersistentChatGateway::serverHandler() const {
	return nullptr;
}

bool PersistentChatGateway::isReady() const {
	return g_gatewayReady;
}

void PersistentChatGateway::requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID) {
	++g_initialRequestCount;
	g_lastInitialRequestScope   = scope;
	g_lastInitialRequestScopeID = scopeID;
}

bool PersistentChatGateway::requestWarmupPages(
	const QList< QPair< MumbleProto::ChatScope, unsigned int > > &scopes, unsigned int limitPerScope) {
	if (!g_warmupRequestSucceeds) {
		return false;
	}

	++g_warmupRequestCount;
	g_lastWarmupScopes = scopes;
	g_lastWarmupLimit  = limitPerScope;
	return true;
}

void PersistentChatGateway::requestOlder(MumbleProto::ChatScope, unsigned int, unsigned int) {
}

void PersistentChatGateway::send(MumbleProto::ChatScope, unsigned int, const QString &, MumbleProto::ChatBodyFormat,
								 std::optional< unsigned int >) {
}

void PersistentChatGateway::toggleReaction(MumbleProto::ChatScope, unsigned int, unsigned int, unsigned int,
										   const QString &, bool) {
}

void PersistentChatGateway::deleteMessage(MumbleProto::ChatScope, unsigned int, unsigned int, unsigned int) {
}

void PersistentChatGateway::markRead(MumbleProto::ChatScope, unsigned int, unsigned int) {
}

void PersistentChatGateway::handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response) {
	emit historyReceived(response);
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
	void forceReloadSendsInitialRequestWhileInitialLoadIsInFlight();
	void warmupScopesSkipsActiveScope();
	void warmupScopesRequestsBatchAndCachesResponses();
	void liveMessagesDoNotSatisfyInitialHistoryLoad();
	void keepsInactivePendingHistoryForCache();
	void mergesOlderHistoryAndReadState();
	void appliesEmbedUpdatesToCachedMessages();
	void preservesReplyMetadataFromHistory();
	void appliesReactionUpdatesToCachedMessages();
	void removesDeletedMessageFromCachedSnapshot();
};

void TestPersistentChatController::activeSnapshotChangedPublishesTwoHistoryMessages() {
	resetGatewayRequestState();
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

void TestPersistentChatController::forceReloadSendsInitialRequestWhileInitialLoadIsInFlight() {
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
	QCOMPARE(g_initialRequestCount, 2);

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
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(20, 2000, MumbleProto::Channel, 7),
					  makeMessage(21, 2010, MumbleProto::Channel, 7) },
					20, 20, true));
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
}

void TestPersistentChatController::appliesEmbedUpdatesToCachedMessages() {
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
}

void TestPersistentChatController::preservesReplyMetadataFromHistory() {
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
}

void TestPersistentChatController::appliesReactionUpdatesToCachedMessages() {
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
}

void TestPersistentChatController::removesDeletedMessageFromCachedSnapshot() {
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
}

QTEST_MAIN(TestPersistentChatController)
#include "TestPersistentChatController.moc"
