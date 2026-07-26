import QtQuick
import QtQuick.Controls
import QtTest
import QtMultimedia

TestCase {
	id: testCase
	name: "NativeDirectMediaPlayer"
	width: 640
	height: 360
	visible: true
	when: windowShown

	QtObject {
		id: session
		property string url: ""
		property string playbackUrl: ""
		property string audioUrl: ""
		property string playbackAudioUrl: ""
		property string playbackAudioWarning: ""
		property bool playbackSourcePreparing: false
		property string state: "paused"
		property int volume: 100
		property bool muted: false
	}

	Loader {
		id: playerLoader
		anchors.fill: parent
		Component.onCompleted: setSource(
			Qt.resolvedUrl("../../../mumble/qml-shell/NativeDirectMediaPlayer.qml"),
			{ "session": session })
	}

	function init() {
		tryVerify(function() { return playerLoader.item !== null })
		session.playbackUrl = ""
		session.playbackSourcePreparing = false
		session.playbackAudioWarning = ""
		session.state = "paused"
		playerLoader.item._mainFailed = false
		playerLoader.item._mainReady = false
		playerLoader.item.secondaryAudioWarning = ""
	}

	function test_playback_phase_contract_covers_the_native_lifecycle() {
		const player = playerLoader.item

		compare(player.resolvePlaybackPhase(false, false, false, false,
			MediaPlayer.StoppedState, false), "empty")
		compare(player.resolvePlaybackPhase(false, true, false, false,
			MediaPlayer.StoppedState, false), "loading")
		compare(player.resolvePlaybackPhase(true, false, false, false,
			MediaPlayer.StoppedState, false), "loading")
		compare(player.resolvePlaybackPhase(true, false, false, true,
			MediaPlayer.StoppedState, false), "ready")
		compare(player.resolvePlaybackPhase(true, false, false, true,
			MediaPlayer.PlayingState, false), "playing")
		compare(player.resolvePlaybackPhase(true, false, false, true,
			MediaPlayer.PausedState, false), "paused")
		compare(player.resolvePlaybackPhase(true, false, false, true,
			MediaPlayer.StoppedState, true), "ended")
		compare(player.resolvePlaybackPhase(true, false, true, true,
			MediaPlayer.PlayingState, false), "error")
	}

	function test_audio_output_initialization_waits_for_the_deferred_player_attempt() {
		const player = playerLoader.item
		compare(player.primaryPlayer, null)
		verify(findChild(player, "nativeDirectPrimaryAudioOutput") === null)
		verify(findChild(player, "nativeDirectSecondaryAudioOutput") === null)

		session.playbackUrl =
			"data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="

		tryVerify(function() { return player.primaryPlayer !== null })
		verify(findChild(player, "nativeDirectPrimaryAudioOutput") !== null)
		verify(findChild(player, "nativeDirectSecondaryAudioOutput") === null)

		session.playbackUrl = ""
		tryVerify(function() { return player.primaryPlayer === null })
	}

	function test_status_copy_and_accessibility_share_the_same_state() {
		const player = playerLoader.item
		const videoOutput = findChild(player, "nativeDirectMediaVideoOutput")
		verify(videoOutput !== null)

		compare(player.playbackPhase, "empty")
		compare(player.playbackStatusText, "No media selected")
		compare(videoOutput.Accessible.description, player.playbackStatusDetail)

		session.playbackSourcePreparing = true
		tryCompare(player, "playbackPhase", "loading")
		compare(player.playbackStatusText, "Loading media")
		compare(videoOutput.Accessible.description, "Loading media")

		player.playbackInputEnabled = false
		compare(videoOutput.Accessible.description,
			"Loading media Playback is controlled by the session host.")
	}

	function test_secondary_audio_degradation_is_part_of_the_status_detail() {
		const player = playerLoader.item
		player.secondaryAudioWarning = "Decoder unavailable"
		compare(player.playbackStatusDetail,
			"No media selected Separate audio is unavailable.")
	}
}
