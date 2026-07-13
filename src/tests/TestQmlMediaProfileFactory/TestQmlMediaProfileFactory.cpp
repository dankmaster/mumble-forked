// Copyright The Mumble Developers. All rights reserved.

#include "QmlMediaProfileFactory.h"

#include <QtTest/QtTest>

class TestQmlMediaProfileFactory final : public QObject {
	Q_OBJECT

private slots:
	void providerResourcesAreDenyByDefault();
	void directMediaAllowsOnlyExactSources();
	void directMediaRejectsLocalNetworkTargets();
};

void TestQmlMediaProfileFactory::providerResourcesAreDenyByDefault() {
	const QUrl player(QStringLiteral("https://www.youtube.com/embed/abc"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, player, player));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, QUrl(QStringLiteral("https://i.ytimg.com/vi/abc/hq.jpg")), player));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, QUrl(QStringLiteral("https://youtube.com.evil.test/script.js")), player));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, QUrl(QStringLiteral("https://192.168.1.1/private")), player));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, QUrl(QStringLiteral("file:///C:/secret")), player));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("youtube"), player, {}, QUrl(QStringLiteral("wss://www.youtube.com/socket")), player));
}

void TestQmlMediaProfileFactory::directMediaAllowsOnlyExactSources() {
	const QUrl video(QStringLiteral("https://cdn.example.com/video.mp4?token=one"));
	const QUrl audio(QStringLiteral("data:audio/mp4;base64,AAAA"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), video, audio, video, video));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), video, audio, QUrl(video.toString() + QStringLiteral("#fragment")), video));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), video, audio, audio, video));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), video, audio,
		QUrl(QStringLiteral("https://cdn.example.com/video.mp4?token=two")), video));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), video, audio, QUrl(QStringLiteral("https://cdn.example.com/other.mp4")), video));
}

void TestQmlMediaProfileFactory::directMediaRejectsLocalNetworkTargets() {
	for (const QUrl &url : {
			 QUrl(QStringLiteral("https://127.0.0.1/video.mp4")),
			 QUrl(QStringLiteral("https://[::1]/video.mp4")),
			 QUrl(QStringLiteral("https://10.0.0.8/video.mp4")),
			 QUrl(QStringLiteral("https://192.168.1.8/video.mp4")),
			 QUrl(QStringLiteral("https://169.254.169.254/latest/meta-data")),
			 QUrl(QStringLiteral("https://127.1/video.mp4")),
			 QUrl(QStringLiteral("https://0x7f.0.0.1/video.mp4")),
			 QUrl(QStringLiteral("https://0177.0.0.1/video.mp4")),
			 QUrl(QStringLiteral("https://2130706433/video.mp4")),
			 QUrl(QStringLiteral("https://router/video.mp4")),
			 QUrl(QStringLiteral("https://speaker.local/video.mp4")) }) {
		QVERIFY2(!QmlMediaProfileFactory::isResourceRequestAllowed(
				 QStringLiteral("direct"), url, {}, url, url), qPrintable(url.toString()));
	}

	const QUrl publicAddress(QStringLiteral("https://1.1.1.1/video.mp4"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), publicAddress, {}, publicAddress, publicAddress));
	const QUrl publicIpv6(QStringLiteral("https://[2606:4700:4700::1111]/video.mp4"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), publicIpv6, {}, publicIpv6, publicIpv6));
}

QTEST_GUILESS_MAIN(TestQmlMediaProfileFactory)
#include "TestQmlMediaProfileFactory.moc"
