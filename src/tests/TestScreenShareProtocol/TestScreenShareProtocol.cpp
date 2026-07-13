// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShare.h"

#include <QStringList>
#include <QtTest>

namespace {
int codecValue(const MumbleProto::ScreenShareCodec codec) {
	return static_cast< int >(codec);
}
} // namespace

class TestScreenShareProtocol : public QObject {
	Q_OBJECT

private slots:
	void sanitizesRelayEndpointsUsedByTheServer();
	void negotiatesBoundedServerParameters();
};

void TestScreenShareProtocol::sanitizesRelayEndpointsUsedByTheServer() {
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral(" wss://relay.example.com/mumble/../room ")),
			 QStringLiteral("wss://relay.example.com/room"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("https://relay.example.com/mumble-screen")),
			 QStringLiteral("https://relay.example.com/mumble-screen"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("rtmps://relay.example.com/live/stream")),
			 QStringLiteral("rtmps://relay.example.com/live/stream"));

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

void TestScreenShareProtocol::negotiatesBoundedServerParameters() {
	const QList< int > webRtcPreferences = Mumble::ScreenShare::webRtcRelayCodecPreferenceList();
	QCOMPARE(Mumble::ScreenShare::selectPreferredCodec(
			 webRtcPreferences,
			 { codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264) }),
		 MumbleProto::ScreenShareCodecH264);
	QCOMPARE(Mumble::ScreenShare::selectPreferredCodec(
			 webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8) }),
		 MumbleProto::ScreenShareCodecVP8);

	QCOMPARE(Mumble::ScreenShare::negotiateLimit(3840, 1920, 1280, 1280, 3840), 1280U);
	QCOMPARE(Mumble::ScreenShare::negotiateLimit(0, 0, 0, 720, 2160), 720U);
	QCOMPARE(Mumble::ScreenShare::sanitizeBitrateKbps(
			 Mumble::ScreenShare::HARD_MAX_BITRATE_KBPS + 1,
			 MumbleProto::ScreenShareCodecH264,
			 Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
			 Mumble::ScreenShare::DEFAULT_MAX_HEIGHT,
			 Mumble::ScreenShare::DEFAULT_MAX_FPS),
		 Mumble::ScreenShare::HARD_MAX_BITRATE_KBPS);
}

QTEST_GUILESS_MAIN(TestScreenShareProtocol)
#include "TestScreenShareProtocol.moc"
