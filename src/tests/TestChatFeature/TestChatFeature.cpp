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
	void historyGrantAcknowledgementsRequireExplicitCapability();
	void historyGrantAcknowledgementFieldsRoundTrip();
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

void TestChatFeature::historyGrantAcknowledgementsRequireExplicitCapability() {
	MumbleProto::Version currentClient;
	Mumble::ChatFeatures::addSupportedFeatures(currentClient);
	QVERIFY(Mumble::ChatFeatures::contains(Mumble::ChatFeatures::featuresFromVersion(currentClient),
		MumbleProto::ChatFeatureHistoryGrantAcks));

	MumbleProto::Version legacyClient;
	legacyClient.set_supports_persistent_chat(true);
	legacyClient.set_persistent_chat_protocol_version(Mumble::ChatFeatures::CURRENT_PROTOCOL_VERSION);
	legacyClient.add_supported_chat_features(MumbleProto::ChatFeatureHistoryGrants);
	const QList< int > legacyFeatures = Mumble::ChatFeatures::featuresFromVersion(legacyClient);
	QVERIFY(Mumble::ChatFeatures::contains(legacyFeatures, MumbleProto::ChatFeatureHistoryGrants));
	QVERIFY(!Mumble::ChatFeatures::contains(legacyFeatures, MumbleProto::ChatFeatureHistoryGrantAcks));
}

void TestChatFeature::historyGrantAcknowledgementFieldsRoundTrip() {
	MumbleProto::ChatHistoryGrantSync outgoing;
	outgoing.set_action(MumbleProto::ChatHistoryGrantSync_Action_Grant);
	outgoing.set_request_id(0x123456789ULL);
	outgoing.set_result(MumbleProto::ChatHistoryGrantSync_Result_NoOp);
	outgoing.set_error_code("already_granted");
	outgoing.set_message("Already granted");
	auto *grant = outgoing.add_grants();
	grant->set_user_id(8);
	grant->set_scope(MumbleProto::TextChannel);
	grant->set_scope_id(42);

	MumbleProto::ChatHistoryGrantSync parsed;
	QVERIFY(parsed.ParseFromString(outgoing.SerializeAsString()));
	QCOMPARE(parsed.request_id(), quint64(0x123456789ULL));
	QCOMPARE(parsed.result(), MumbleProto::ChatHistoryGrantSync_Result_NoOp);
	QCOMPARE(QString::fromStdString(parsed.error_code()), QStringLiteral("already_granted"));
	QCOMPARE(parsed.grants_size(), 1);
	QCOMPARE(parsed.grants(0).user_id(), 8U);

	MumbleProto::ChatHistoryGrantSync legacyShape;
	legacyShape.set_action(MumbleProto::ChatHistoryGrantSync_Action_Grant);
	legacyShape.add_grants()->set_user_id(8);
	QVERIFY(!legacyShape.has_request_id());
	QVERIFY(!legacyShape.has_result());
}

QTEST_MAIN(TestChatFeature)
#include "TestChatFeature.moc"
