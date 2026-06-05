// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ScreenShare.h"

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
};

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

QTEST_MAIN(TestScreenShare)
#include "TestScreenShare.moc"
