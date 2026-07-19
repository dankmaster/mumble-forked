// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ChatFeature.h"
#include "Channel.h"
#include "Group.h"
#include "Meta.h"
#include "MumbleConstants.h"
#include "Server.h"
#include "ServerUser.h"
#include "database/SQLiteConnectionParameter.h"

#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <memory>

class TestChatHistoryGrantHandler : public QObject {
	Q_OBJECT

private slots:
	void syncGrantRevokeAreDeadlockFreeAndIdempotent();
	void rejectsInvalidTargetsPermissionsAndMultiItemMutations();
	void databaseFailureRollsBackMemoryAndDurableState();
	void channelAclWriteFailureRollsBackGrantAndMemory();
	void revokeFailureRestoresGrantAndMembership();
	void committedGrantSurvivesServerReload();
	void legacySingleItemMutationRemainsCompatible();
};

namespace {
	struct Fixture {
		QTemporaryDir directory;
		std::unique_ptr< mumble::db::SQLiteConnectionParameter > connection;
		std::unique_ptr< Meta > metaInstance;
		std::unique_ptr< Server > server;
		unsigned int serverID = 0;
		unsigned int targetID = 0;
		QTcpServer transport;
		QSslSocket *actorSocket = nullptr;
		std::unique_ptr< QTcpSocket > peer;
		std::unique_ptr< ServerUser > actor;
		QByteArray responseBuffer;

		Fixture() {
			QVERIFY(directory.isValid());
			Meta::mp = std::make_unique< MetaParams >();
			Meta::mp->qlBind.clear();
			Meta::mp->bBonjour     = false;
			Meta::mp->iMessageLimit = 100;
			Meta::mp->iMessageBurst = 100;
			Meta::mp->kdfIterations = 1000;
			connection = std::make_unique< mumble::db::SQLiteConnectionParameter >(
				directory.filePath(QStringLiteral("handler.sqlite")).toStdString(), false);
			metaInstance = std::make_unique< Meta >(*connection);
			meta = metaInstance.get();
			serverID = metaInstance->dbWrapper.addServer();
			server = std::make_unique< Server >(serverID, *connection);

			ServerUserInfo target;
			target.qsName = QStringLiteral("RegisteredTarget");
			target.qsHash = QStringLiteral("target-cert-hash");
			const int registeredID = server->registerUser(target);
			QVERIFY(registeredID > 0);
			targetID = static_cast< unsigned int >(registeredID);

			QVERIFY(transport.listen(QHostAddress::LocalHost, 0));
			actorSocket = new QSslSocket();
			actorSocket->connectToHost(QHostAddress::LocalHost, transport.serverPort());
			QVERIFY(actorSocket->waitForConnected(3000));
			QVERIFY(transport.waitForNewConnection(3000));
			peer.reset(transport.nextPendingConnection());
			QVERIFY(peer != nullptr);

			actor = std::make_unique< ServerUser >(server.get(), actorSocket);
			actor->sState = ServerUser::Authenticated;
			actor->uiSession = 1;
			actor->iId = Mumble::SUPERUSER_ID;
			actor->qsName = QStringLiteral("SuperUser");
			actor->cChannel = server->qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			actor->bSupportsPersistentChat = true;
			actor->uiPersistentChatProtocolVersion = Mumble::ChatFeatures::CURRENT_PROTOCOL_VERSION;
			actor->qlSupportedChatFeatures = Mumble::ChatFeatures::supportedFeatureList();
			server->qhUsers.insert(actor->uiSession, actor.get());
		}

		~Fixture() {
			if (server && actor) {
				server->qhUsers.remove(actor->uiSession);
			}
			actor.reset();
			actorSocket = nullptr;
			peer.reset();
			server.reset();
			meta = nullptr;
			metaInstance.reset();
			Meta::mp.reset();
		}

		void restartServerWithoutClient() {
			server->qhUsers.remove(actor->uiSession);
			actor.reset();
			actorSocket = nullptr;
			peer.reset();
			server.reset();
			server = std::make_unique< Server >(serverID, *connection);
		}

		void executeSql(const QString &sql, const QString &connectionName) const {
			{
				QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
				db.setDatabaseName(directory.filePath(QStringLiteral("handler.sqlite")));
				QVERIFY(db.open());
				QSqlQuery query(db);
				QVERIFY2(query.exec(sql), qPrintable(query.lastError().text()));
				db.close();
			}
			QSqlDatabase::removeDatabase(connectionName);
		}

		MumbleProto::ChatHistoryGrantSync invoke(MumbleProto::ChatHistoryGrantSync_Action action,
														 MumbleProto::ChatScope scope, unsigned int scopeID,
														 unsigned int userID, quint64 requestID,
														 quint64 visibleAfter = 0, int itemCount = 1) {
			MumbleProto::ChatHistoryGrantSync request;
			request.set_action(action);
			if (requestID != 0) {
				request.set_request_id(requestID);
			}
			for (int i = 0; i < itemCount; ++i) {
				auto *grant = request.add_grants();
				grant->set_user_id(userID);
				grant->set_scope(scope);
				grant->set_scope_id(scopeID);
				grant->set_visible_after(visibleAfter);
			}

			QElapsedTimer elapsed;
			elapsed.start();
			server->msgChatHistoryGrantSync(actor.get(), request);
			if (elapsed.elapsed() >= 2000) {
				QTest::qFail("ChatHistoryGrantSync handler exceeded the deadlock regression budget", __FILE__, __LINE__);
				return {};
			}

			if (!(actorSocket->waitForBytesWritten(3000) || actorSocket->bytesToWrite() == 0)
				|| !(peer->waitForReadyRead(3000) || peer->bytesAvailable() >= 6 || responseBuffer.size() >= 6)) {
				QTest::qFail("Grant response was not written to the test transport", __FILE__, __LINE__);
				return {};
			}
			responseBuffer += peer->readAll();
			for (;;) {
				while (responseBuffer.size() < 6) {
					if (!peer->waitForReadyRead(3000)) {
						QTest::qFail("Grant response header timed out", __FILE__, __LINE__);
						return {};
					}
					responseBuffer += peer->readAll();
				}
				const quint16 type = qFromBigEndian< quint16 >(
					reinterpret_cast< const uchar * >(responseBuffer.constData()));
				const quint32 size = qFromBigEndian< quint32 >(
					reinterpret_cast< const uchar * >(responseBuffer.constData() + 2));
				while (responseBuffer.size() < 6 + static_cast< int >(size)) {
					if (!peer->waitForReadyRead(3000)) {
						QTest::qFail("Grant response body timed out", __FILE__, __LINE__);
						return {};
					}
					responseBuffer += peer->readAll();
				}
				const QByteArray payload = responseBuffer.mid(6, static_cast< int >(size));
				responseBuffer.remove(0, 6 + static_cast< int >(size));
				if (type != static_cast< quint16 >(Mumble::Protocol::TCPMessageType::ChatHistoryGrantSync)) {
					continue;
				}
				MumbleProto::ChatHistoryGrantSync response;
				if (!response.ParseFromArray(payload.constData(), payload.size())) {
					QTest::qFail("Grant response protobuf could not be parsed", __FILE__, __LINE__);
					return {};
				}
				return response;
			}
		}

		Group *historyGroup(Channel *channel) const {
			for (Group *group : channel->qhGroups) {
				if (group && group->qsName.compare(QStringLiteral("chathistory"), Qt::CaseInsensitive) == 0) {
					return group;
				}
			}
			return nullptr;
		}
	};
}

void TestChatHistoryGrantHandler::syncGrantRevokeAreDeadlockFreeAndIdempotent() {
	Fixture fixture;
	Channel *root = fixture.server->qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	QVERIFY(root);
	Channel *normal = fixture.server->createNewChannel(root, QStringLiteral("Normal"));
	QVERIFY(normal);
	const auto textChannels = fixture.server->m_dbWrapper.getTextChannels(fixture.serverID);
	QVERIFY(!textChannels.empty());

	auto sync = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Sync, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 1, 0, 0);
	QCOMPARE(sync.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);

	struct ScopeCase { MumbleProto::ChatScope scope; unsigned int scopeID; Channel *permissionChannel; };
	const QList< ScopeCase > scopes {
		{ MumbleProto::Channel, Mumble::ROOT_CHANNEL_ID, root },
		{ MumbleProto::Channel, normal->iId, normal },
		{ MumbleProto::TextChannel, textChannels.front().textChannelID, root }
	};
	quint64 requestID = 10;
	for (const ScopeCase &scope : scopes) {
		auto granted = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, scope.scope, scope.scopeID,
			fixture.targetID, requestID++, 1234);
		QCOMPARE(granted.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
		QVERIFY(fixture.server->m_dbWrapper.getChatHistoryGrant(
			fixture.serverID, fixture.targetID,
			scope.scope == MumbleProto::TextChannel ? mumble::server::db::ChatThreadScope::TextChannel
				: mumble::server::db::ChatThreadScope::Channel,
			scope.scopeID));
		QVERIFY(fixture.historyGroup(scope.permissionChannel));
		QVERIFY(fixture.historyGroup(scope.permissionChannel)->qsAdd.contains(static_cast< int >(fixture.targetID)));
		QCOMPARE(static_cast< int >(fixture.server->m_dbWrapper.getChatHistoryGrants(fixture.serverID).size()), 1);

		auto duplicateGrant = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, scope.scope, scope.scopeID,
			fixture.targetID, requestID++, 1234);
		QCOMPARE(duplicateGrant.result(), MumbleProto::ChatHistoryGrantSync_Result_NoOp);
		QCOMPARE(static_cast< int >(fixture.server->m_dbWrapper.getChatHistoryGrants(fixture.serverID).size()), 1);

		auto revoked = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Revoke, scope.scope, scope.scopeID,
			fixture.targetID, requestID++);
		QCOMPARE(revoked.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
		QVERIFY(!fixture.historyGroup(scope.permissionChannel)
			|| !fixture.historyGroup(scope.permissionChannel)->qsAdd.contains(static_cast< int >(fixture.targetID)));
		QVERIFY(fixture.server->m_dbWrapper.getChatHistoryGrants(fixture.serverID).empty());

		auto duplicateRevoke = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Revoke, scope.scope, scope.scopeID,
			fixture.targetID, requestID++);
		QCOMPARE(duplicateRevoke.result(), MumbleProto::ChatHistoryGrantSync_Result_NoOp);
	}
}

void TestChatHistoryGrantHandler::rejectsInvalidTargetsPermissionsAndMultiItemMutations() {
	Fixture fixture;
	auto missingCorrelation = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 0);
	QCOMPARE(missingCorrelation.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(missingCorrelation.error_code()), QStringLiteral("missing_request_id"));

	auto invalidTarget = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, 99999, 100);
	QCOMPARE(invalidTarget.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(invalidTarget.error_code()), QStringLiteral("target_not_registered"));

	auto multi = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 101, 0, 2);
	QCOMPARE(multi.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(multi.error_code()), QStringLiteral("invalid_item_count"));

	fixture.actor->iId = static_cast< int >(fixture.targetID);
	auto denied = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 102);
	QCOMPARE(denied.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(denied.error_code()), QStringLiteral("permission_denied"));
	QVERIFY(!fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
}

void TestChatHistoryGrantHandler::databaseFailureRollsBackMemoryAndDurableState() {
	Fixture fixture;
	fixture.executeSql(QStringLiteral(
			"CREATE TRIGGER reject_chat_history_grant BEFORE INSERT ON chat_history_grants "
			"BEGIN SELECT RAISE(FAIL, 'injected grant failure'); END"),
		QStringLiteral("chat-history-grant-failure"));

	Channel *root = fixture.server->qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	QVERIFY(root);
	Group *before = fixture.historyGroup(root);
	const bool memberBefore = before && before->qsAdd.contains(static_cast< int >(fixture.targetID));
	auto rejected = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 200);
	QCOMPARE(rejected.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(rejected.error_code()), QStringLiteral("database_error"));
	QVERIFY(!fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
	Group *after = fixture.historyGroup(root);
	QCOMPARE(after && after->qsAdd.contains(static_cast< int >(fixture.targetID)), memberBefore);
}

void TestChatHistoryGrantHandler::channelAclWriteFailureRollsBackGrantAndMemory() {
	Fixture fixture;
	fixture.executeSql(QStringLiteral(
		"CREATE TRIGGER reject_group_write BEFORE INSERT ON groups "
		"BEGIN SELECT RAISE(FAIL, 'injected group failure'); END"),
		QStringLiteral("chat-history-group-failure"));

	Channel *root = fixture.server->qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	QVERIFY(root);
	Group *before = fixture.historyGroup(root);
	const bool memberBefore = before && before->qsAdd.contains(static_cast< int >(fixture.targetID));
	const auto rejected = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 300);
	QCOMPARE(rejected.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(rejected.error_code()), QStringLiteral("database_error"));
	QVERIFY(!fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
	Group *after = fixture.historyGroup(root);
	QCOMPARE(after && after->qsAdd.contains(static_cast< int >(fixture.targetID)), memberBefore);
}

void TestChatHistoryGrantHandler::revokeFailureRestoresGrantAndMembership() {
	Fixture fixture;
	const auto granted = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 400);
	QCOMPARE(granted.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
	fixture.executeSql(QStringLiteral(
		"CREATE TRIGGER reject_group_write BEFORE INSERT ON groups "
		"BEGIN SELECT RAISE(FAIL, 'injected group failure'); END"),
		QStringLiteral("chat-history-revoke-group-failure"));

	const auto rejected = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Revoke, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 401);
	QCOMPARE(rejected.result(), MumbleProto::ChatHistoryGrantSync_Result_Rejected);
	QCOMPARE(QString::fromStdString(rejected.error_code()), QStringLiteral("database_error"));
	QVERIFY(fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
	Group *group = fixture.historyGroup(fixture.server->qhChannels.value(Mumble::ROOT_CHANNEL_ID));
	QVERIFY(group);
	QVERIFY(group->qsAdd.contains(static_cast< int >(fixture.targetID)));
}

void TestChatHistoryGrantHandler::committedGrantSurvivesServerReload() {
	Fixture fixture;
	const auto granted = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 500);
	QCOMPARE(granted.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
	fixture.restartServerWithoutClient();

	QVERIFY(fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
	Group *group = fixture.historyGroup(fixture.server->qhChannels.value(Mumble::ROOT_CHANNEL_ID));
	QVERIFY(group);
	QVERIFY(group->qsAdd.contains(static_cast< int >(fixture.targetID)));
}

void TestChatHistoryGrantHandler::legacySingleItemMutationRemainsCompatible() {
	Fixture fixture;
	fixture.actor->qlSupportedChatFeatures.removeAll(
		static_cast< int >(MumbleProto::ChatFeatureHistoryGrantAcks));
	const auto granted = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Grant, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 0);
	QCOMPARE(granted.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
	QVERIFY(!granted.has_request_id());
	QVERIFY(fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));

	const auto revoked = fixture.invoke(MumbleProto::ChatHistoryGrantSync_Action_Revoke, MumbleProto::Channel,
		Mumble::ROOT_CHANNEL_ID, fixture.targetID, 0);
	QCOMPARE(revoked.result(), MumbleProto::ChatHistoryGrantSync_Result_Accepted);
	QVERIFY(!fixture.server->m_dbWrapper.getChatHistoryGrant(fixture.serverID, fixture.targetID,
		mumble::server::db::ChatThreadScope::Channel, Mumble::ROOT_CHANNEL_ID));
}

QTEST_GUILESS_MAIN(TestChatHistoryGrantHandler)
#include "TestChatHistoryGrantHandler.moc"
