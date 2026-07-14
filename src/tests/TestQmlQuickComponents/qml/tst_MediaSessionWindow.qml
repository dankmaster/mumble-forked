import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "MediaSessionWindow"
	when: windowShown
	width: 320
	height: 240
	property alias mediaSession: session

	QtObject {
		id: session
		property bool active: false
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property int sharedParticipantCount: 0
		property string sharedTitle: ""
		property string provider: "youtube"
		property string url: "https://www.youtube.com/embed/test"
		property string audioUrl: ""
		property string state: "paused"
		property string error: ""
		property real position: 0
		property real duration: 0
		property int loadProgress: 0
		property int volume: 100
		property bool muted: false

		function play() {}
		function pause() {}
		function seek(value) {}
		function setVolume(value) {}
		function toggleMuted() {}
		function closePlayer() {}
		function endShared() {}
		function leaveShared() {}
	}

	Loader {
		id: windowLoader
		Component.onCompleted: source = Qt.resolvedUrl("../../../mumble/qml-shell/MediaSessionWindow.qml")
	}

	function init() {
		tryVerify(function() { return windowLoader.item !== null })
		session.provider = "youtube"
		session.url = "https://www.youtube.com/embed/test"
		wait(0)
	}

	function test_provider_and_url_inference_covers_every_supported_shape() {
		const window = windowLoader.item
		const cases = [
			[ "youtube", "https://www.youtube.com/embed/test", "wide" ],
			[ "twitch", "https://player.twitch.tv/?video=1", "twitch" ],
			[ "tiktok", "https://www.tiktok.com/player/v1/1", "short" ],
			[ "instagram", "https://www.instagram.com/reel/example/embed/", "short" ],
			[ "instagram", "https://www.instagram.com/p/example/embed/", "square" ],
			[ "spotify", "https://open.spotify.com/embed/track/example", "compact-audio" ],
			[ "spotify", "https://open.spotify.com/embed/episode/example", "compact-audio" ],
			[ "spotify", "https://open.spotify.com/embed/playlist/example", "audio" ],
			[ "soundcloud", "https://w.soundcloud.com/player/?url=test", "compact-audio" ]
		]
		for (let i = 0; i < cases.length; ++i) {
			session.provider = cases[i][0]
			session.url = cases[i][1]
			compare(window.inferMediaAspect(), cases[i][2], cases[i][0] + " aspect")
		}
	}

	function test_initial_size_is_aspect_aware_without_binding_future_resizes() {
		const window = windowLoader.item
		session.provider = "tiktok"
		session.url = "https://www.tiktok.com/player/v1/1"
		wait(0)
		window.applyInitialWindowSize()
		compare(window.width, 640)
		compare(window.height, 820)

		window.width = 713
		window.height = 677
		session.provider = "spotify"
		session.url = "https://open.spotify.com/embed/track/example"
		wait(0)
		compare(window.width, 713)
		compare(window.height, 677)
	}
}
