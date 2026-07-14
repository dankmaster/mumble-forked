// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ChatFeature.h"

class TestChatFeature : public QObject {
	Q_OBJECT

private slots:
	void advertisesProtocolRevision();
	void gatesAttachmentTransfersByRevision();
};

void TestChatFeature::advertisesProtocolRevision() {
	MumbleProto::Version version;
	Mumble::ChatFeatures::addSupportedFeatures(version);

	QCOMPARE(version.persistent_chat_protocol_version(), Mumble::ChatFeatures::CURRENT_PROTOCOL_VERSION);
	QVERIFY(Mumble::ChatFeatures::contains(Mumble::ChatFeatures::featuresFromVersion(version),
									  MumbleProto::ChatFeatureAttachments));
}

void TestChatFeature::gatesAttachmentTransfersByRevision() {
	MumbleProto::Version legacyClient;
	legacyClient.set_supports_persistent_chat(true);
	legacyClient.add_supported_chat_features(MumbleProto::ChatFeaturePersistentHistory);
	legacyClient.add_supported_chat_features(MumbleProto::ChatFeatureAttachments);
	const QList< int > legacyClientFeatures = Mumble::ChatFeatures::featuresFromVersion(legacyClient);
	QVERIFY(Mumble::ChatFeatures::contains(legacyClientFeatures, MumbleProto::ChatFeaturePersistentHistory));
	QVERIFY(!Mumble::ChatFeatures::contains(legacyClientFeatures, MumbleProto::ChatFeatureAttachments));

	MumbleProto::ServerConfig legacyServer;
	legacyServer.set_persistent_chat_protocol_version(
		Mumble::ChatFeatures::ATTACHMENT_TRANSFER_PROTOCOL_VERSION - 1);
	legacyServer.add_supported_chat_features(MumbleProto::ChatFeaturePersistentHistory);
	legacyServer.add_supported_chat_features(MumbleProto::ChatFeatureAttachments);
	QVERIFY(!Mumble::ChatFeatures::contains(Mumble::ChatFeatures::featuresFromServerConfig(legacyServer),
									   MumbleProto::ChatFeatureAttachments));

	MumbleProto::ServerConfig currentServer;
	Mumble::ChatFeatures::addSupportedFeatures(currentServer);
	QVERIFY(Mumble::ChatFeatures::contains(Mumble::ChatFeatures::featuresFromServerConfig(currentServer),
									  MumbleProto::ChatFeatureAttachments));
}

QTEST_MAIN(TestChatFeature)
#include "TestChatFeature.moc"
