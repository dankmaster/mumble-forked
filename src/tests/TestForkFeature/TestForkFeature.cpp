// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ForkFeature.h"

class TestForkFeature : public QObject {
	Q_OBJECT

private slots:
	void advertisesSupportedFeaturesInVersion();
	void advertisesSupportedFeaturesInServerConfig();
	void sanitizesUnknownAndFutureFeatures();
	void exposesFallbackPolicy();
};

void TestForkFeature::advertisesSupportedFeaturesInVersion() {
	MumbleProto::Version version;
	Mumble::ForkFeatures::addSupportedFeatures(version);

	QCOMPARE(version.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);

	const QList< int > features = Mumble::ForkFeatures::featuresFromVersion(version);
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureServerLinkPreviewProxy));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureWatchTogetherRooms));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureScreenShareSessionPresence));
	QVERIFY(Mumble::ForkFeatures::contains(features, MumbleProto::ForkFeatureVirtualizedChatPresentation));
}

void TestForkFeature::advertisesSupportedFeaturesInServerConfig() {
	MumbleProto::ServerConfig config;
	Mumble::ForkFeatures::addSupportedFeatures(config);

	QCOMPARE(config.fork_extension_protocol_version(), Mumble::ForkFeatures::CURRENT_PROTOCOL_VERSION);

	const QList< int > features = Mumble::ForkFeatures::featuresFromServerConfig(config);
	QCOMPARE(features, Mumble::ForkFeatures::supportedFeatureList());
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
}

QTEST_MAIN(TestForkFeature)
#include "TestForkFeature.moc"
