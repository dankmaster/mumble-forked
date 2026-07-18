// Copyright The Mumble Developers. All rights reserved.

#include "QmlMediaProfileFactory.h"

#include "QmlClientModels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QScopedPointer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>
#include <QtTest/QtTest>

#include <tuple>
#ifndef MUMBLE_TEST_DELAYED_WEBENGINE
#	include <QtWebEngineQuick/qtwebenginequickglobal.h>
#else
#	include <Windows.h>
#endif

class TestQmlMediaProfileFactory final : public QObject {
	Q_OBJECT

private slots:
	void providerResourcesAreDenyByDefault();
	void directMediaAllowsOnlyExactSources();
	void directAdaptiveManifestAllowsOnlySameOriginChildren();
	void directMediaRejectsLocalNetworkTargets();
	void activeSessionNavigationAcceptsSafeRedirectsAndNormalization();
	void explicitMediaActivationLoadsDelayedProfileAndView();
	void liveSteamAdaptiveManifestsReachRendererReady();
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

	const QUrl facebookPlayer(QStringLiteral("https://www.facebook.com/plugins/video.php?href=test"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("facebook"), facebookPlayer, {},
		QUrl(QStringLiteral("https://video.xx.fbcdn.net/media.mp4")), facebookPlayer));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("facebook"), facebookPlayer, {},
		QUrl(QStringLiteral("https://scontent.xx.fbsbx.com/media.mp4")), facebookPlayer));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("facebook"), facebookPlayer, {},
		QUrl(QStringLiteral("https://connect.facebook.net/sdk.js")), facebookPlayer));

	const QUrl tiktokPlayer(QStringLiteral("https://www.tiktok.com/player/v1/123456789"));
	for (const QUrl &resource : {
			 QUrl(QStringLiteral("https://sf16-website-login.neutral.tiktokcdn-eu.com/playback/player.js")),
			 QUrl(QStringLiteral("https://www.tiktokw.eu/player.css")),
			 QUrl(QStringLiteral("https://sf16-website-login.ttwstatic.com/player.js")),
			 QUrl(QStringLiteral("https://v16m.tiktokv.eu/video/tos/example.mp4")) }) {
		QVERIFY2(QmlMediaProfileFactory::isResourceRequestAllowed(
				 QStringLiteral("tiktok"), tiktokPlayer, {}, resource, tiktokPlayer),
			qPrintable(resource.toString()));
	}
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("tiktok"), tiktokPlayer, {},
		QUrl(QStringLiteral("https://tiktokcdn-eu.com.evil.test/player.js")), tiktokPlayer));
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

void TestQmlMediaProfileFactory::directAdaptiveManifestAllowsOnlySameOriginChildren() {
	const QUrl hls(QStringLiteral(
		"https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/hls_264_master.m3u8?t=1"));
	const QString hlsMime = QStringLiteral("application/vnd.apple.mpegurl");
	for (const QUrl &resource : {
			 QUrl(QStringLiteral(
				 "https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/hls_264_0_video.m3u8")),
			 QUrl(QStringLiteral(
				 "https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/hls_264_0_video/segment00001.ts?token=2")),
			 QUrl(QStringLiteral(
				 "https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/key.bin")) }) {
		QVERIFY2(QmlMediaProfileFactory::isResourceRequestAllowed(
			QStringLiteral("direct"), hls, {}, resource, hls, hlsMime), qPrintable(resource.toString()));
	}

	const QUrl dash(QStringLiteral(
		"https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/dash_h264.mpd?t=1"));
	const QString dashMime = QStringLiteral("application/dash+xml");
	for (const QUrl &resource : {
			 QUrl(QStringLiteral(
				 "https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/dash_h264/init-stream0.m4s")),
			 QUrl(QStringLiteral(
				 "https://video.akamai.steamstatic.com/store_trailers/730/612468/hash/1/dash_h264/chunk-stream0-00001.m4s")) }) {
		QVERIFY2(QmlMediaProfileFactory::isResourceRequestAllowed(
			QStringLiteral("direct"), dash, {}, resource, QUrl(QStringLiteral("about:blank")), dashMime),
			qPrintable(resource.toString()));
	}

	const QUrl sameOriginOtherPath(QStringLiteral(
		"https://video.akamai.steamstatic.com/unrelated/public-object"));
	QVERIFY(QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), hls, {}, sameOriginOtherPath, {}, hlsMime));

	// The manifest capability never broadens a normal direct video session or
	// crosses an origin, even to another Steam CDN hostname.
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), hls, {}, sameOriginOtherPath, hls, QStringLiteral("video/mp4")));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), hls, {},
		QUrl(QStringLiteral("https://cdn.cloudflare.steamstatic.com/store_trailers/segment.ts")),
		hls, hlsMime));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), hls, {},
		QUrl(QStringLiteral("https://video.akamai.steamstatic.com:444/store_trailers/segment.ts")),
		hls, hlsMime));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), hls, {},
		QUrl(QStringLiteral("https://video.akamai.steamstatic.com/store_trailers/segment.ts")),
		QUrl(QStringLiteral("https://attacker.example/player")), hlsMime));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"),
		QUrl(QStringLiteral("https://user@video.akamai.steamstatic.com/manifest.m3u8")), {},
		QUrl(QStringLiteral("https://video.akamai.steamstatic.com/segment.ts")), {}, hlsMime));
}

void TestQmlMediaProfileFactory::activeSessionNavigationAcceptsSafeRedirectsAndNormalization() {
	MediaSessionBackend providerSession;
	QVERIFY(providerSession.open(
		QUrl(QStringLiteral("https://www.youtube-nocookie.com/embed/provider-source")),
		QStringLiteral("youtube"), QStringLiteral("provider-session")));
	QmlMediaProfileFactory providerProfiles(&providerSession);
	QVERIFY(providerProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://www.youtube.com/embed/provider-source?canonical=1")),
		providerSession.url()));
	QVERIFY(!providerProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://youtube.com.evil.example/embed/provider-source")),
		providerSession.url()));
	QVERIFY(!providerProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://user@www.youtube.com/embed/provider-source")),
		providerSession.url()));
	QVERIFY(!providerProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://www.youtube.com:444/embed/provider-source")),
		providerSession.url()));

	MediaSessionBackend directSession;
	const QUrl primary(QStringLiteral("https://media.example.com/clips/../video.mp4#source"));
	const QUrl audio(QStringLiteral("https://media.example.com/tracks/../audio.m4a#source"));
	QVERIFY(directSession.openDirect(primary, QStringLiteral("video/mp4"), audio,
		QStringLiteral("audio/mp4"), QStringLiteral("direct-session")));
	QmlMediaProfileFactory directProfiles(&directSession);
	QVERIFY(directProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://media.example.com/video.mp4#renderer")), primary));
	QVERIFY(directProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://media.example.com/audio.m4a#renderer")), audio));
	QVERIFY(!directProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://cdn.example.com/audio.m4a")), audio));
	directSession.closePlayer();
	QVERIFY(!directProfiles.isNavigationRequestAllowed(
		QUrl(QStringLiteral("https://media.example.com/video.mp4")), primary));

	MediaSessionBackend manifestSession;
	const QUrl manifest(QStringLiteral(
		"https://video.akamai.steamstatic.com/store_trailers/example/hls_264_master.m3u8"));
	const QUrl segment(QStringLiteral(
		"https://video.akamai.steamstatic.com/store_trailers/example/segment00001.ts"));
	QVERIFY(manifestSession.openDirect(manifest, QStringLiteral("application/vnd.apple.mpegurl"),
		{}, {}, QStringLiteral("manifest-session")));
	QmlMediaProfileFactory manifestProfiles(&manifestSession);
	const QUrl documentUrl = manifestProfiles.videoDocumentUrl();
	QCOMPARE(documentUrl.scheme(), QStringLiteral("qrc"));
	QCOMPARE(documentUrl.path(), QStringLiteral("/media-player/AdaptiveMediaPlayer.html"));
	QCOMPARE(QUrl(QString::fromUtf8(QByteArray::fromBase64(documentUrl.fragment().toLatin1(),
		QByteArray::Base64UrlEncoding))), manifest);
	QVERIFY(manifestProfiles.isNavigationRequestAllowed(documentUrl, {}));
	QVERIFY(!manifestProfiles.isNavigationRequestAllowed(manifest, documentUrl));
	// Child objects are subresources only; the same-origin capability must not
	// become a browser navigation capability.
	QVERIFY(!manifestProfiles.isNavigationRequestAllowed(segment, documentUrl));

	for (const QUrl &resource : {
			 QUrl(QStringLiteral("qrc:/media-player/AdaptiveMediaPlayer.html")),
			 QUrl(QStringLiteral("qrc:/media-player/AdaptiveMediaPlayer.js")),
			 QUrl(QStringLiteral("qrc:/media-player/shaka-player.compiled.js")),
			 QUrl(QStringLiteral("blob:null/mumble-adaptive-media")) }) {
		QVERIFY2(QmlMediaProfileFactory::isResourceRequestAllowed(
			QStringLiteral("direct"), manifest, {}, resource, documentUrl,
			QStringLiteral("application/vnd.apple.mpegurl")), qPrintable(resource.toString()));
	}
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), manifest, {},
		QUrl(QStringLiteral("qrc:/qml-shell/Main.qml")), documentUrl,
		QStringLiteral("application/vnd.apple.mpegurl")));
	QVERIFY(!QmlMediaProfileFactory::isResourceRequestAllowed(
		QStringLiteral("direct"), manifest, {},
		QUrl(QStringLiteral("blob:null/mumble-adaptive-media")),
		QUrl(QStringLiteral("qrc:/qml-shell/Main.qml")),
		QStringLiteral("application/vnd.apple.mpegurl")));
}

void TestQmlMediaProfileFactory::explicitMediaActivationLoadsDelayedProfileAndView() {
#ifndef MUMBLE_TEST_DELAYED_WEBENGINE
	QSKIP("This toolchain/build does not support the Windows shared delay-load contract.");
#else
	auto moduleLoaded = [](const char *moduleName) { return GetModuleHandleA(moduleName) != nullptr; };
	const char *webEngineQuick = MUMBLE_TEST_WEBENGINE_QUICK_DLL;
	const char *webEngineCore = MUMBLE_TEST_WEBENGINE_CORE_DLL;
	const char *webChannelQuick = MUMBLE_TEST_WEBCHANNEL_QUICK_DLL;

	// Constructing the frontend-neutral state must not resolve the player
	// runtime. Explicit activation only queues the delayed DLL resolution on a
	// worker; profile/view creation remains on the GUI thread after it completes.
	QVERIFY(!moduleLoaded(webEngineQuick));
	QVERIFY(!moduleLoaded(webEngineCore));
	QVERIFY(!moduleLoaded(webChannelQuick));
	MediaSessionBackend session;
	QmlMediaProfileFactory profiles(&session);
	QSignalSpy runtimeStateSpy(&profiles, &QmlMediaProfileFactory::runtimeStateChanged);
	QSignalSpy profilesChangedSpy(&profiles, &QmlMediaProfileFactory::profilesChanged);
	QVERIFY(!profiles.runtimeReady());
	QVERIFY(!profiles.runtimePreparing());
	QElapsedTimer activationTimer;
	activationTimer.start();
	QVERIFY(session.open(QUrl(QStringLiteral("https://www.youtube-nocookie.com/embed/lazy-load-probe")),
						   QStringLiteral("youtube"), QStringLiteral("lazy-load-probe")));
	QVERIFY(session.detached());
	QVERIFY2(activationTimer.elapsed() < 50, "Explicit activation synchronously blocked while resolving WebEngine.");
	QVERIFY(profiles.runtimePreparing() || profiles.runtimeReady());
	QTRY_VERIFY_WITH_TIMEOUT(profiles.runtimeReady(), 5000);
	QVERIFY(profiles.runtimeError().isEmpty());
	QVERIFY(runtimeStateSpy.count() >= 1);
	QCOMPARE(profilesChangedSpy.count(), 1);
	QVERIFY(moduleLoaded(webEngineQuick));
	QVERIFY(moduleLoaded(webEngineCore));

	QObject *profile = profiles.videoProfile();
	QVERIFY(profile);
	QCOMPARE(profiles.videoProfile(), profile);
	QCOMPARE(QString::fromLatin1(profile->metaObject()->className()), QStringLiteral("QQuickWebEngineProfile"));
	QVERIFY(moduleLoaded(webEngineQuick));
	QVERIFY(moduleLoaded(webEngineCore));

	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("delayedMediaProfile"), profile);
	QQmlComponent component(&engine);
	component.setData(R"QML(
import QtQuick
import QtWebEngine

WebEngineView {
    objectName: "delayedMediaView"
    width: 1
    height: 1
    profile: delayedMediaProfile
    url: "about:blank"
}
)QML",
				  QUrl(QStringLiteral("qrc:/tests/DelayedMediaView.qml")));
	QVERIFY2(component.isReady(), qPrintable(component.errorString()));
	QScopedPointer< QObject > view(component.create());
	QVERIFY2(view, qPrintable(component.errorString()));
	QCOMPARE(view->objectName(), QStringLiteral("delayedMediaView"));
	QCOMPARE(view->property("profile").value< QObject * >(), profile);
	QVERIFY(moduleLoaded(webEngineQuick));
	QVERIFY(moduleLoaded(webEngineCore));
	QVERIFY(moduleLoaded(webChannelQuick));
#endif
}

void TestQmlMediaProfileFactory::liveSteamAdaptiveManifestsReachRendererReady() {
	if (qEnvironmentVariableIntValue("MUMBLE_TEST_LIVE_ADAPTIVE_MEDIA") != 1) {
		QSKIP("Set MUMBLE_TEST_LIVE_ADAPTIVE_MEDIA=1 to run the live Steam renderer gate.");
	}

	const auto reachesRendererReady = [](const QUrl &manifestUrl, const QString &mime,
											  QString *failure) -> bool {
		MediaSessionBackend session;
		QmlMediaProfileFactory profiles(&session);
		if (!session.openDirect(manifestUrl, mime, {}, {}, QStringLiteral("live-adaptive-renderer"))) {
			if (failure) *failure = QStringLiteral("The backend rejected %1").arg(manifestUrl.toString());
			return false;
		}
		QElapsedTimer runtimeTimer;
		runtimeTimer.start();
		while (!profiles.runtimeReady() && profiles.runtimeError().isEmpty()
			&& runtimeTimer.elapsed() < 10000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			QTest::qWait(10);
		}
		if (!profiles.runtimeReady()) {
			if (failure) *failure = profiles.runtimeError().isEmpty()
				? QStringLiteral("The WebEngine runtime did not become ready.") : profiles.runtimeError();
			return false;
		}

		QObject *profile = profiles.videoProfile();
		if (!profile) {
			if (failure) *failure = QStringLiteral("The isolated video profile was not created.");
			return false;
		}

		QQmlEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("adaptiveProfile"), profile);
		engine.rootContext()->setContextProperty(QStringLiteral("adaptiveProfiles"), &profiles);
		engine.rootContext()->setContextProperty(QStringLiteral("adaptiveDocumentUrl"),
			profiles.videoDocumentUrl());
		QQmlComponent component(&engine);
		component.setData(R"QML(
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: root
    x: -32000
    y: -32000
    width: 640
    height: 360
    visible: true
    property bool rendererReady: false
    property string rendererError: ""
    property string rendererStage: "starting"

    WebEngineView {
        id: renderer
        anchors.fill: parent
        profile: adaptiveProfile
        url: adaptiveDocumentUrl
        settings.playbackRequiresUserGesture: false
        settings.localContentCanAccessRemoteUrls: true
        onNavigationRequested: function(request) {
            if (!adaptiveProfiles.isNavigationRequestAllowed(request.url, adaptiveDocumentUrl))
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }
        onLoadingChanged: function(request) {
            if (request.status === WebEngineView.LoadFailedStatus)
                root.rendererError = String(request.errorString || "Adaptive document load failed")
        }
        onRenderProcessTerminated: function(status, exitCode) {
            root.rendererError = "Adaptive renderer terminated (" + exitCode + ")"
        }
        Timer {
            interval: 100
            repeat: true
            running: !root.rendererReady && root.rendererError.length === 0
            onTriggered: renderer.runJavaScript(
                "(function(){const s=window.__mumbleAdaptiveState||null;"
                + "const m=document.querySelector('video');"
                + "return {ready:!!(s&&s.ready&&m&&m.readyState>=1),"
                + "error:s?String(s.error||''):'',stage:s?String(s.stage||''):'bootstrap'};})()",
                function(value) {
                    if (!value) return
                    root.rendererStage = String(value.stage || "")
                    if (String(value.error || "").length > 0)
                        root.rendererError = String(value.error)
                    else if (value.ready)
                        root.rendererReady = true
                })
        }
    }
}
)QML",
			QUrl(QStringLiteral("qrc:/tests/LiveAdaptiveMedia.qml")));
		if (!component.isReady()) {
			if (failure) *failure = component.errorString();
			return false;
		}
		QScopedPointer< QObject > surface(component.create());
		if (!surface) {
			if (failure) *failure = component.errorString();
			return false;
		}

		QElapsedTimer playbackTimer;
		playbackTimer.start();
		while (!surface->property("rendererReady").toBool()
			&& surface->property("rendererError").toString().isEmpty()
			&& playbackTimer.elapsed() < 30000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
			QTest::qWait(25);
		}
		const bool ready = surface->property("rendererReady").toBool();
		if (!ready && failure) {
			const QString rendererError = surface->property("rendererError").toString();
			const QString rendererStage = surface->property("rendererStage").toString();
			*failure = rendererError.isEmpty()
				? QStringLiteral("Timed out in adaptive stage '%1'.").arg(rendererStage)
				: rendererError;
		}
		return ready;
	};

	const QString trailerRoot = QStringLiteral(
		"https://video.akamai.steamstatic.com/store_trailers/730/612468/"
		"aa5a28c78f12232e6b6839034550c28b162fad3e/1748810724/");
	for (const auto &[name, url, mime] : {
			 std::tuple { QStringLiteral("HLS"), QUrl(trailerRoot + QStringLiteral("hls_264_master.m3u8?t=1696005467")),
				 QStringLiteral("application/vnd.apple.mpegurl") },
			 std::tuple { QStringLiteral("DASH"), QUrl(trailerRoot + QStringLiteral("dash_h264.mpd?t=1696005467")),
				 QStringLiteral("application/dash+xml") } }) {
		QString failure;
		QVERIFY2(reachesRendererReady(url, mime, &failure),
			qPrintable(QStringLiteral("Steam %1 did not reach renderer-ready: %2").arg(name, failure)));
	}
}

int main(int argc, char **argv) {
	for (const char *variable : { "QTWEBENGINEPROCESS_PATH", "QTWEBENGINE_RESOURCES_PATH",
								 "QTWEBENGINE_LOCALES_PATH" }) {
		const QByteArray value = qgetenv(variable);
		if (!value.isEmpty()) {
			qputenv(variable, QDir::toNativeSeparators(
							  QDir::cleanPath(QString::fromLocal8Bit(value)))
							  .toLocal8Bit());
		}
	}
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	const QSGRendererInterface::GraphicsApi api = QQuickWindow::graphicsApi();
	if (api != QSGRendererInterface::OpenGL && api != QSGRendererInterface::Vulkan
		&& api != QSGRendererInterface::Metal && api != QSGRendererInterface::Direct3D11) {
		QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
	}
#ifndef MUMBLE_TEST_DELAYED_WEBENGINE
	QtWebEngineQuick::initialize();
#endif
	QGuiApplication application(argc, argv);
	TestQmlMediaProfileFactory test;
	return QTest::qExec(&test, argc, argv);
}

#include "TestQmlMediaProfileFactory.moc"
