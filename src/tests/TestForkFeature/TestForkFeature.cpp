// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include <QtCore/QFile>

#include "ACL.h"
#include "ForkFeature.h"
#include "MumbleProtocol.h"

class TestForkFeature : public QObject {
	Q_OBJECT

private slots:
	void protocolVersionDefaultsMatchCurrent();
	void advertisesSupportedFeaturesInVersion();
	void advertisesSupportedFeaturesInServerConfig();
	void sanitizesUnknownAndFutureFeatures();
	void exposesFallbackPolicy();
	void toolsAclPermissionContract();
	void legacyClientsCannotMutateToolsAcl();
	void serverLogStreamContract();
};

void TestForkFeature::protocolVersionDefaultsMatchCurrent() {
	MumbleProto::Version version;
	MumbleProto::ServerConfig config;

	QCOMPARE(version.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);
	QCOMPARE(config.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);
}

void TestForkFeature::advertisesSupportedFeaturesInVersion() {
	MumbleProto::Version version;
	Mumble::ForkFeatures::addSupportedFeatures(version);

	QCOMPARE(version.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);

	const QList< int > features = Mumble::ForkFeatures::featuresFromVersion(version);
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureServerLinkPreviewProxy));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureWatchTogetherRooms));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureScreenShareSessionPresence));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureVirtualizedChatPresentation));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureStonksLedger));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureClientAssistedLinkPreviews));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureInAppFeedback));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureToolsAcl));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureServerLogStream));
}

void TestForkFeature::advertisesSupportedFeaturesInServerConfig() {
	MumbleProto::ServerConfig config;
	Mumble::ForkFeatures::addSupportedFeatures(config);

	QCOMPARE(config.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);

	const QList< int > features = Mumble::ForkFeatures::featuresFromServerConfig(config);
	QCOMPARE(features, Mumble::ForkFeatures::supportedFeatureList());
	QVERIFY(Mumble::ForkFeatures::serverAllowsClientFeature(features, MumbleProto::ForkFeatureInAppFeedback));
	QVERIFY(!Mumble::ForkFeatures::serverAllowsClientFeature({}, MumbleProto::ForkFeatureInAppFeedback));
}

void TestForkFeature::sanitizesUnknownAndFutureFeatures() {
	const QList< int > input = {
		static_cast< int >(MumbleProto::ForkFeatureWatchTogetherRooms),
		999,
		static_cast< int >(MumbleProto::ForkFeatureServerLinkPreviewProxy),
		static_cast< int >(MumbleProto::ForkFeatureWatchTogetherRooms),
	};

	QVERIFY(Mumble::ForkFeatures::sanitizeFeatureList(input, 0).isEmpty());
	QCOMPARE(Mumble::ForkFeatures::sanitizeFeatureList(input, Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION),
			 (QList< int >{ static_cast< int >(MumbleProto::ForkFeatureWatchTogetherRooms),
							static_cast< int >(MumbleProto::ForkFeatureServerLinkPreviewProxy) }));
}

void TestForkFeature::exposesFallbackPolicy() {
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureServerLinkPreviewProxy)),
			 QStringLiteral("server_only"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureWatchTogetherRooms)),
			 QStringLiteral("server_only"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureVirtualizedChatPresentation)),
			 QStringLiteral("none"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureClientAssistedLinkPreviews)),
			 QStringLiteral("server_only"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureInAppFeedback)),
			 QStringLiteral("server_only"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureToolsAcl)),
			 QStringLiteral("server_only"));
	QCOMPARE(Mumble::ForkFeatures::fallbackPolicyName(Mumble::ForkFeatures::fallbackPolicy(
				 MumbleProto::ForkFeatureServerLogStream)),
			 QStringLiteral("server_only"));
}

void TestForkFeature::toolsAclPermissionContract() {
	QCOMPARE(static_cast< unsigned int >(ChanACL::UseTools), 0x00200000u);
	QVERIFY(ChanACL::Permissions(ChanACL::All).testFlag(ChanACL::UseTools));
	QCOMPARE(Mumble::ForkFeatures::minProtocolVersion(MumbleProto::ForkFeatureToolsAcl), 3u);
}

void TestForkFeature::legacyClientsCannotMutateToolsAcl() {
	const QString sourcePath = QFINDTESTDATA("../../murmur/Messages.cpp");
	QVERIFY2(!sourcePath.isEmpty(), "Murmur Messages.cpp test data was not found");
	QFile sourceFile(sourcePath);
	QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString source = QString::fromUtf8(sourceFile.readAll());
	const qsizetype start = source.indexOf(QStringLiteral("void Server::msgACL"));
	const qsizetype end   = source.indexOf(QStringLiteral("void Server::msgQueryUsers"), start);
	QVERIFY(start >= 0 && end > start);
	const QString handler = source.mid(start, end - start);

	QVERIFY(handler.contains(QStringLiteral("ForkFeatureToolsAcl")));
	QVERIFY(handler.contains(QStringLiteral("!toolsAclCapableClient")));
	QVERIFY(handler.contains(QStringLiteral("grant &= ~ChanACL::UseTools")));
	QVERIFY(handler.contains(QStringLiteral("a->pAllow &= ~ChanACL::UseTools")));
	QVERIFY(handler.contains(QStringLiteral("PreservedToolsAclRule")));
	QVERIFY(handler.contains(QStringLiteral("rootPermissionUpdates")));
}

void TestForkFeature::serverLogStreamContract() {
	QCOMPARE(Mumble::ForkFeatures::minProtocolVersion(MumbleProto::ForkFeatureServerLogStream), 3u);
	QCOMPARE(static_cast< int >(Mumble::Protocol::TCPMessageType::ServerLogState), 59);
	QCOMPARE(QString::fromStdString(Mumble::Protocol::messageTypeName(
			 Mumble::Protocol::TCPMessageType::ServerLogState)),
		 QStringLiteral("ServerLogState"));

	MumbleProto::ServerLogState source;
	source.set_authorized(true);
	source.set_reset(true);
	MumbleProto::ServerLogEntry *entry = source.add_entries();
	entry->set_sequence(42);
	entry->set_timestamp_ms(1700000000123ULL);
	entry->set_text("<7:admin(1)> exact Murmur log text");

	std::string encoded;
	QVERIFY(source.SerializeToString(&encoded));
	MumbleProto::ServerLogState parsed;
	QVERIFY(parsed.ParseFromString(encoded));
	QVERIFY(parsed.authorized());
	QVERIFY(parsed.reset());
	QCOMPARE(parsed.entries_size(), 1);
	QCOMPARE(parsed.entries(0).sequence(), 42ULL);
	QCOMPARE(parsed.entries(0).timestamp_ms(), 1700000000123ULL);
	QCOMPARE(QString::fromStdString(parsed.entries(0).text()),
			 QStringLiteral("<7:admin(1)> exact Murmur log text"));

	const QString serverPath = QFINDTESTDATA("../../murmur/Server.cpp");
	QVERIFY2(!serverPath.isEmpty(), "Murmur Server.cpp test data was not found");
	QFile serverFile(serverPath);
	QVERIFY(serverFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString serverSource = QString::fromUtf8(serverFile.readAll());

	const qsizetype authorizationStart =
		serverSource.indexOf(QStringLiteral("bool Server::serverLogStreamAllowedForUser"));
	const qsizetype synchronizationStart =
		serverSource.indexOf(QStringLiteral("void Server::syncServerLogStateForUser"), authorizationStart);
	const qsizetype broadcastStart =
		serverSource.indexOf(QStringLiteral("void Server::broadcastServerLogEntry"), synchronizationStart);
	const qsizetype broadcastEnd =
		serverSource.indexOf(QStringLiteral("void Server::screenShareDiagnosticLog"), broadcastStart);
	QVERIFY(authorizationStart >= 0 && synchronizationStart > authorizationStart);
	QVERIFY(broadcastStart > synchronizationStart && broadcastEnd > broadcastStart);

	const QString authorization = serverSource.mid(authorizationStart, synchronizationStart - authorizationStart);
	QVERIFY(authorization.contains(QStringLiteral("ForkFeatureServerLogStream")));
	QVERIFY(authorization.contains(QStringLiteral("ChanACL::UseTools")));
	QVERIFY(authorization.contains(QStringLiteral("Mumble::ROOT_CHANNEL_ID")));

	const QString synchronization = serverSource.mid(synchronizationStart, broadcastStart - synchronizationStart);
	QVERIFY(synchronization.contains(QStringLiteral("state.set_authorized(authorized)")));
	QVERIFY(synchronization.contains(QStringLiteral("state.set_reset(true)")));
	QVERIFY(synchronization.contains(QStringLiteral("m_dbWrapper.getLogs")));
	QVERIFY(synchronization.contains(QStringLiteral("SERVER_LOG_BACKLOG_MAX_ENTRIES")));

	const QString broadcast = serverSource.mid(broadcastStart, broadcastEnd - broadcastStart);
	QVERIFY(broadcast.contains(QStringLiteral("user->bServerLogStreamActive")));
	QVERIFY(broadcast.contains(QStringLiteral("state.set_reset(false)")));

	const qsizetype clearCacheStart = serverSource.indexOf(QStringLiteral("void Server::clearACLCache"));
	const qsizetype clearCacheEnd =
		serverSource.indexOf(QStringLiteral("void Server::clearWhisperTargetCache"), clearCacheStart);
	QVERIFY(clearCacheStart >= 0 && clearCacheEnd > clearCacheStart);
	QVERIFY(serverSource.mid(clearCacheStart, clearCacheEnd - clearCacheStart)
			.contains(QStringLiteral("syncServerLogStateForUser")));

	const QString clientMessagesPath = QFINDTESTDATA("../../mumble/Messages.cpp");
	QVERIFY2(!clientMessagesPath.isEmpty(), "Client Messages.cpp test data was not found");
	QFile clientMessagesFile(clientMessagesPath);
	QVERIFY(clientMessagesFile.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString clientMessages = QString::fromUtf8(clientMessagesFile.readAll());
	QVERIFY(clientMessages.contains(QStringLiteral("void MainWindow::msgServerLogState")));
	QVERIFY(clientMessages.contains(QStringLiteral("applyModernServerLogState(msg)")));
}

QTEST_MAIN(TestForkFeature)
#include "TestForkFeature.moc"
