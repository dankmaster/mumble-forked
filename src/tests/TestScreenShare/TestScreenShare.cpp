// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>
#include <QtCore/QUuid>
#include <QtCore/QSharedMemory>

#include <limits>

#include "ScreenShare.h"
#include "ScreenShareFrameTransport.h"
#include "ScreenShareViewBackend.h"

namespace {
int codecValue(const MumbleProto::ScreenShareCodec codec) {
	return static_cast< int >(codec);
}
} // namespace

class TestScreenShare : public QObject {
	Q_OBJECT

private slots:
	void parsesAndFormatsVp8CodecPreferences();
	void keepsDirectDefaultH264First();
	void keepsWebRtcRelayH264First();
	void negotiatesWebRtcRelayWithLegacyFallback();
	void recommendsVp8Bitrate();
	void exposesPublisherQualityCeiling();
	void normalizesSecureRelayUrls();
	void rejectsUnsafeRelayUrls();
	void qmlViewBackendPublishesLifecycleState();
	void qmlViewBackendReportsExternalWindowTransportHonestly();
	void nativeFrameTransportIsBoundedAndTracksDrops();
	void qmlViewBackendConsumesNativeFramesOffThread();
};

void TestScreenShare::qmlViewBackendPublishesLifecycleState() {
	ScreenShareSession session;
	session.streamID = QStringLiteral("stream:7");
	session.ownerSession = 42;
	session.captureAudio = true;
	ScreenShareViewBackend backend(session);
	QSignalSpy pauseSpy(&backend, &ScreenShareViewBackend::pauseToggled);
	QSignalSpy muteSpy(&backend, &ScreenShareViewBackend::audioMuteToggled);
	QSignalSpy stopSpy(&backend, &ScreenShareViewBackend::stopRequested);

	QCOMPARE(backend.streamId(), QStringLiteral("stream:7"));
	QVERIFY(backend.audioAvailable());
	backend.setPaused(true);
	backend.setAudioMuted(true);
	backend.setAudioVolume(125);
	backend.requestStop();
	QVERIFY(backend.paused());
	QVERIFY(backend.audioMuted());
	QCOMPARE(backend.audioVolume(), 100);
	QCOMPARE(pauseSpy.count(), 1);
	QCOMPARE(muteSpy.count(), 1);
	QCOMPARE(stopSpy.count(), 1);
}

void TestScreenShare::qmlViewBackendReportsExternalWindowTransportHonestly() {
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);

	QCOMPARE(backend.renderTransport(), QStringLiteral("external-process-window"));
	QVERIFY(backend.nativeFrameTransportAvailable());
	QVERIFY(!backend.nativeFrameTransportBlocker().isEmpty());
	QVERIFY(backend.nativeFrameTransportBlocker().contains(QStringLiteral("appsink adapter")));
	QVERIFY(backend.nativeFrameTransportBlocker().contains(QStringLiteral("decoded frames")));
}

void TestScreenShare::nativeFrameTransportIsBoundedAndTracksDrops() {
	const QString key = QStringLiteral("mumble-frame-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	Mumble::ScreenShare::FrameTransport consumer;
	QVERIFY(producer.create(key, 64));
	QVERIFY(consumer.attach(key));
	Mumble::ScreenShare::NativeFrame frame;
	frame.generation = 4;
	frame.width = 2;
	frame.height = 2;
	frame.stride = 8;
	frame.bgra = QByteArray(16, '\x7f');
	frame.sequence = 1;
	QVERIFY(producer.publish(frame));
	Mumble::ScreenShare::NativeFrame received;
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(received.sequence, 1ULL);
	frame.sequence = 4;
	QVERIFY(producer.publish(frame));
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(consumer.droppedFrames(), 2ULL);
	frame.generation = 5;
	frame.sequence = 1;
	frame.stride = 8;
	QVERIFY(producer.publish(frame));
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(received.generation, 5ULL);
	frame.stride = std::numeric_limits< quint32 >::max();
	QVERIFY(!producer.publish(frame));
	QCOMPARE(producer.droppedFrames(), 1ULL);
	producer.detach();
	QVERIFY(!producer.publish(frame));

	const QString malformedKey = key + QStringLiteral("-malformed");
	QSharedMemory malformed(malformedKey);
	QVERIFY(malformed.create(64));
	Mumble::ScreenShare::FrameTransport rejected;
	QVERIFY(!rejected.attach(malformedKey));
}

void TestScreenShare::qmlViewBackendConsumesNativeFramesOffThread() {
	const QString key = QStringLiteral("mumble-frame-backend-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	QVERIFY(producer.create(key, 64));
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	backend.setNativeFrameTransport(key, 9);
	QTRY_VERIFY(backend.nativeFrameActive());
	Mumble::ScreenShare::NativeFrame frame;
	frame.generation = 9;
	frame.width = 2;
	frame.height = 2;
	frame.stride = 8;
	frame.sequence = 1;
	frame.bgra = QByteArray(16, '\xff');
	QVERIFY(producer.publish(frame));
	QTRY_VERIFY(!backend.currentFrame().isNull());
	QCOMPARE(backend.currentFrame().size(), QSize(2, 2));
	QCOMPARE(backend.renderTransport(), QStringLiteral("native-shared-memory-bgra"));
	backend.setNativeFrameTransport({}, 0);
	QTRY_VERIFY(!backend.nativeFrameActive());
}

void TestScreenShare::parsesAndFormatsVp8CodecPreferences() {
	const QList< int > codecs =
		Mumble::ScreenShare::parseCodecPreferenceString(QStringLiteral("vp8 h264,av1 vp9 vp8 nope"));

	QCOMPARE(codecs, (QList< int >{
						 codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264),
						 codecValue(MumbleProto::ScreenShareCodecAV1), codecValue(MumbleProto::ScreenShareCodecVP9) }));
	QCOMPARE(Mumble::ScreenShare::codecPreferenceString(codecs), QStringLiteral("vp8 h264 av1 vp9"));
	QCOMPARE(Mumble::ScreenShare::codecToConfigToken(MumbleProto::ScreenShareCodecVP8), QStringLiteral("vp8"));
	QVERIFY(Mumble::ScreenShare::isValidCodec(MumbleProto::ScreenShareCodecVP8));
}

void TestScreenShare::keepsDirectDefaultH264First() {
	const QList< int > codecs = Mumble::ScreenShare::defaultCodecPreferenceList();

	QVERIFY(!codecs.isEmpty());
	QCOMPARE(codecs.first(), codecValue(MumbleProto::ScreenShareCodecH264));
	QVERIFY(codecs.contains(codecValue(MumbleProto::ScreenShareCodecVP8)));
}

void TestScreenShare::keepsWebRtcRelayH264First() {
	QCOMPARE(
		Mumble::ScreenShare::webRtcRelayCodecPreferenceList(),
		(QList< int >{ codecValue(MumbleProto::ScreenShareCodecH264), codecValue(MumbleProto::ScreenShareCodecAV1),
					   codecValue(MumbleProto::ScreenShareCodecVP9), codecValue(MumbleProto::ScreenShareCodecVP8) }));
}

void TestScreenShare::negotiatesWebRtcRelayWithLegacyFallback() {
	const QList< int > webRtcPreferences = Mumble::ScreenShare::webRtcRelayCodecPreferenceList();

	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8),
																	   codecValue(MumbleProto::ScreenShareCodecH264) }),
		MumbleProto::ScreenShareCodecH264);
	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8) }),
		MumbleProto::ScreenShareCodecVP8);
	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecH264),
																	   codecValue(MumbleProto::ScreenShareCodecAV1) }),
		MumbleProto::ScreenShareCodecH264);
}

void TestScreenShare::recommendsVp8Bitrate() {
	const unsigned int h264Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecH264, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);
	const unsigned int vp8Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecVP8, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);

	QCOMPARE(h264Bitrate, 4000U);
	QCOMPARE(vp8Bitrate, 4200U);
}

void TestScreenShare::exposesPublisherQualityCeiling() {
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH, 2560U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT, 1440U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS, 144U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH, 3840U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT, 2160U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS, 144U);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH >= Mumble::ScreenShare::DEFAULT_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT >= Mumble::ScreenShare::DEFAULT_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS >= Mumble::ScreenShare::DEFAULT_MAX_FPS);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH <= Mumble::ScreenShare::HARD_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT <= Mumble::ScreenShare::HARD_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS <= Mumble::ScreenShare::HARD_MAX_FPS);
}

void TestScreenShare::normalizesSecureRelayUrls() {
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral(" wss://relay.example.com/mumble/../room ")),
			 QStringLiteral("wss://relay.example.com/room"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("https://relay.example.com/mumble-screen")),
			 QStringLiteral("https://relay.example.com/mumble-screen"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("rtmps://relay.example.com/live/stream")),
			 QStringLiteral("rtmps://relay.example.com/live/stream"));
}

void TestScreenShare::rejectsUnsafeRelayUrls() {
	const QStringList rejected{
		QStringLiteral("file:///tmp/mumble-screen.mp4"),
		QStringLiteral("ws://relay.example.com/mumble-screen"),
		QStringLiteral("http://relay.example.com/mumble-screen"),
		QStringLiteral("rtmp://relay.example.com/live/stream"),
		QStringLiteral("wss:///missing-host"),
		QStringLiteral("wss://relay.example.com/mumble screen"),
		QStringLiteral("wss://relay.example.com/mumble!fakesink"),
		QStringLiteral("wss://relay.example.com/mumble\\fakesink"),
	};
	for (const QString &url : rejected) {
		QVERIFY2(Mumble::ScreenShare::normalizeRelayUrl(url).isEmpty(), qPrintable(url));
	}
}

QTEST_GUILESS_MAIN(TestScreenShare)
#include "TestScreenShare.moc"
