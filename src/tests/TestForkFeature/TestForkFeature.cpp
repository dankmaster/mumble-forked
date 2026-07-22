// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include <QtCore/QFile>

#include "ACL.h"
#include "ForkFeature.h"

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

QTEST_MAIN(TestForkFeature)
#include "TestForkFeature.moc"
