import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "InlineMediaPlayer"
	when: windowShown
	visible: true
	width: 800
	height: 700

	QtObject {
		id: session
		property bool active: false
		property bool detached: false
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property int sharedParticipantCount: 0
		property string provider: "youtube"
		property string url: "https://www.youtube.com/embed/test"
		property string state: "paused"
		property string error: ""
		property real position: 0
		property real duration: 0
		property int loadProgress: 0
		property int volume: 100
		property bool muted: false
		property int retryCalls: 0

		function play() {}
		function pause() {}
		function seek(value) {}
		function setVolume(value) {}
		function toggleMuted() {}
		function retry() { retryCalls += 1 }
		function reportLoadProgress(value) {}
		function reportError(value) {}
		function reportPlaybackState(position, duration, paused) {}
		function isNavigationAllowed(url) { return true }
		function detach() {}
		function closePlayer() {}
	}

	Loader {
		id: playerLoader
		anchors.fill: parent
		Component.onCompleted: setSource(
			Qt.resolvedUrl("../../../mumble/qml-shell/InlineMediaPlayer.qml"),
			{ "session": session, "aspect": "wide" })
	}

	function init() {
		tryVerify(function() { return playerLoader.item !== null })
		playerLoader.item.invalidateMediaDocument()
		playerLoader.item.aspect = "wide"
		session.active = false
		session.detached = false
		session.state = "paused"
		session.error = ""
		session.retryCalls = 0
		wait(0)
	}

	function test_document_readiness_gates_state_polling() {
		const player = playerLoader.item
		const generation = player.beginMediaDocumentLoad("https://www.youtube.com/embed/first")
		compare(player.mediaGeneration, generation)
		verify(player.rendererHealthy)
		verify(!player.documentReady)
		verify(!player.claimStatePoll(generation))

		verify(player.markMediaDocumentReady(generation))
		verify(player.documentReady)
		verify(player.claimStatePoll(generation))
		verify(player.statePollInFlight)
		verify(!player.claimStatePoll(generation))
		verify(player.cancelStatePoll(generation))
		verify(!player.statePollInFlight)
	}

	function test_late_poll_callback_cannot_mutate_new_document_generation() {
		const player = playerLoader.item
		const firstGeneration = player.beginMediaDocumentLoad("https://www.youtube.com/embed/first")
		verify(player.markMediaDocumentReady(firstGeneration))
		verify(player.claimStatePoll(firstGeneration))

		const secondGeneration = player.beginMediaDocumentLoad("https://www.youtube.com/embed/second")
		verify(secondGeneration > firstGeneration)
		verify(player.markMediaDocumentReady(secondGeneration))
		verify(player.claimStatePoll(secondGeneration))
		verify(player.statePollInFlight)

		verify(!player.completeStatePoll(firstGeneration,
			{ "position": 15, "duration": 90, "paused": false }))
		compare(player.mediaGeneration, secondGeneration)
		verify(player.documentReady)
		verify(player.statePollInFlight)
		verify(player.cancelStatePoll(secondGeneration))
	}

	function test_renderer_failure_invalidates_document_and_pending_poll() {
		const player = playerLoader.item
		const generation = player.beginMediaDocumentLoad("https://www.youtube.com/embed/failure")
		verify(player.markMediaDocumentReady(generation))
		verify(player.claimStatePoll(generation))

		verify(player.failMediaDocument(generation))
		verify(player.mediaGeneration > generation)
		verify(!player.rendererHealthy)
		verify(!player.documentReady)
		verify(!player.statePollInFlight)
		verify(!player.markMediaDocumentReady(generation))
		verify(!player.completeStatePoll(generation,
			{ "position": 20, "duration": 100, "paused": true }))
	}

	function test_aspect_values_are_normalized_and_unknown_values_fail_safe() {
		const player = playerLoader.item
		compare(player.normalizeAspect("TWITCH"), "twitch")
		compare(player.normalizeAspect(" compact-audio "), "compact-audio")
		compare(player.normalizeAspect("unsupported"), "wide")
		player.aspect = "SHORT"
		tryCompare(player, "normalizedAspect", "short")
	}

	function test_visual_surface_preserves_wide_short_and_square_geometry() {
		const player = playerLoader.item
		const canvas = findChild(player, "inlineMediaCanvas")
		const surface = findChild(player, "inlineMediaWebSurface")
		verify(canvas !== null && surface !== null)

		player.aspect = "wide"
		wait(0)
		verify(Math.abs(surface.width / surface.height - 16 / 9) < 0.01)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))

		player.aspect = "short"
		wait(0)
		verify(Math.abs(surface.width / surface.height - 9 / 16) < 0.01)
		verify(surface.width < canvas.width)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))

		player.aspect = "square"
		wait(0)
		verify(Math.abs(surface.width - surface.height) < 0.01)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))
	}

	function test_audio_surfaces_stay_low_and_controls_keep_full_width() {
		const player = playerLoader.item
		const canvas = findChild(player, "inlineMediaCanvas")
		const surface = findChild(player, "inlineMediaWebSurface")

		player.aspect = "audio"
		wait(0)
		compare(surface.width, canvas.width)
		verify(surface.height <= 352)
		const audioHeight = surface.height

		player.aspect = "compact-audio"
		wait(0)
		compare(surface.width, canvas.width)
		verify(surface.height <= 166)
		verify(surface.height < audioHeight)
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))
	}

	function test_accessibility_exposes_one_named_player_and_toolbar_actions() {
		const player = playerLoader.item
		compare(player.Accessible.role, Accessible.Pane)
		compare(player.Accessible.name, "Inline media player")

		const popoutButton = findChild(player, "inlineMediaPopoutButton")
		const externalButton = findChild(player, "inlineMediaExternalButton")
		verify(popoutButton !== null && externalButton !== null)
		compare(popoutButton.Accessible.role, Accessible.Button)
		compare(popoutButton.Accessible.name, "Pop out")
		compare(externalButton.Accessible.role, Accessible.Button)
		compare(externalButton.Accessible.name, "Browser")
	}

	function test_focus_handoff_targets_first_transport_control() {
		const player = playerLoader.item
		const playButton = findChild(player, "mediaPlayButton")
		verify(playButton !== null)
		verify(player.focusInitialControl())
		compare(playButton.activeFocus, true)
	}

	function test_failure_moves_focus_to_retry_and_retry_is_the_initial_control() {
		const player = playerLoader.item
		const failureOverlay = findChild(player, "inlineMediaFailureOverlay")
		const retryButton = findChild(player, "inlineMediaRetryButton")
		verify(failureOverlay !== null && retryButton !== null)

		session.state = "error"
		session.error = "The provider renderer stopped."
		session.active = true
		tryCompare(failureOverlay, "visible", true)
		tryCompare(retryButton, "activeFocus", true)
		verify(player.focusInitialControl())
		compare(retryButton.activeFocus, true)
		compare(retryButton.Accessible.name, "Retry")

		retryButton.clicked()
		compare(session.retryCalls, 1)
	}
}
