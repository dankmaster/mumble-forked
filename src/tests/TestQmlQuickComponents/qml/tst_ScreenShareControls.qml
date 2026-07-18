import QtQuick
import QtTest
import Mumble.Theme 1.0
import "../../../mumble/qml-shell" as Shell

TestCase {
	id: testCase
	name: "ScreenShareControls"
	when: windowShown
	visible: true
	width: 640
	height: 260

	QtObject {
		id: shareBackend
		property string title: "Shared desktop"
		property string detail: "Host · 1920 × 1080"
		property bool paused: false
		property bool audioAvailable: true
		property bool audioMuted: false
		property int audioVolume: 72
		property string operationStatus: "ready"
		property string operationError: ""
		property int pauseCalls: 0
		property int muteCalls: 0
		property int volumeCalls: 0
		property int stopCalls: 0

		function setPaused(value) { pauseCalls += 1; paused = value }
		function setAudioMuted(value) { muteCalls += 1; audioMuted = value }
		function setAudioVolume(value) { volumeCalls += 1; audioVolume = Math.round(value) }
		function requestStop() { stopCalls += 1 }
	}

	Shell.ScreenShareControls {
		id: controls
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		height: implicitHeight
		backend: shareBackend
	}
	SignalSpy {
		id: fullscreenSpy
		target: controls
		signalName: "fullscreenRequested"
	}

	function init() {
		testCase.width = 640
		shareBackend.title = "Shared desktop"
		shareBackend.detail = "Host · 1920 × 1080"
		shareBackend.paused = false
		shareBackend.audioAvailable = true
		shareBackend.audioMuted = false
		shareBackend.audioVolume = 72
		shareBackend.operationStatus = "ready"
		shareBackend.operationError = ""
		shareBackend.pauseCalls = 0
		shareBackend.muteCalls = 0
		shareBackend.volumeCalls = 0
		shareBackend.stopCalls = 0
		controls.fullscreen = false
		fullscreenSpy.clear()

		findChild(controls, "screenSharePauseButton").text = Qt.binding(function() {
			return shareBackend.paused ? "Start playback" : "Stop playback"
		})
		findChild(controls, "screenShareMuteButton").text = Qt.binding(function() {
			return shareBackend.audioMuted ? "Unmute audio" : "Mute audio"
		})
		findChild(controls, "screenShareStopButton").text = "Stop"
	}

	function verifyControlFits(flow, control, name) {
		verify(control !== null, name + " exists")
		verify(control.width > 0 && control.height > 0, name + " keeps an operable hit target")
		verify(control.Accessible.name.length > 0, name + " keeps an accessible name")
		verify(control.x >= -0.5 && control.x + control.width <= flow.width + 0.5,
			name + " remains inside the responsive action flow; x=" + control.x
				+ ", width=" + control.width + ", flow=" + flow.width)
	}

	function test_minimum_width_keeps_every_control_inside_the_action_flow() {
		const flow = findChild(controls, "screenShareHeaderActions")
		verify(flow !== null)
		verifyControlFits(flow, findChild(controls, "screenSharePauseButton"), "pause")
		verifyControlFits(flow, findChild(controls, "screenShareMuteButton"), "mute")
		verifyControlFits(flow, findChild(controls, "screenShareVolumeSlider"), "volume")
		verifyControlFits(flow, findChild(controls, "screenShareFullscreenButton"), "full screen")
		verifyControlFits(flow, findChild(controls, "screenShareStopButton"), "stop")
	}

	function test_fullscreen_control_exposes_state_and_requests_both_transitions() {
		const button = findChild(controls, "screenShareFullscreenButton")
		verify(button !== null)
		compare(button.Accessible.name, "Enter full screen")
		button.clicked()
		compare(fullscreenSpy.count, 1)
		compare(fullscreenSpy.signalArguments[0][0], true)

		controls.fullscreen = true
		compare(button.Accessible.name, "Exit full screen")
		verify(button.selected)
		button.clicked()
		compare(fullscreenSpy.count, 2)
		compare(fullscreenSpy.signalArguments[1][0], false)
	}

	function test_playback_control_stops_and_starts_the_local_receiver() {
		const button = findChild(controls, "screenSharePauseButton")
		compare(button.text, "Stop playback")
		button.clicked()
		compare(shareBackend.pauseCalls, 1)
		verify(shareBackend.paused)
		compare(button.text, "Start playback")

		button.clicked()
		compare(shareBackend.pauseCalls, 2)
		verify(!shareBackend.paused)
		compare(button.text, "Stop playback")
	}

	function test_mute_control_is_local_to_the_stream_player() {
		const button = findChild(controls, "screenShareMuteButton")
		compare(button.text, "Mute audio")
		button.clicked()
		compare(shareBackend.muteCalls, 1)
		verify(shareBackend.audioMuted)
		compare(button.text, "Unmute audio")
	}

	function test_long_translations_wrap_without_hiding_actions() {
		const flow = findChild(controls, "screenShareHeaderActions")
		const pauseButton = findChild(controls, "screenSharePauseButton")
		const muteButton = findChild(controls, "screenShareMuteButton")
		const stopButton = findChild(controls, "screenShareStopButton")
		pauseButton.text = "Pause this shared desktop view at the current live frame"
		muteButton.text = "Mute all audio received from this shared desktop stream"
		stopButton.text = "Stop receiving this shared desktop stream"

		tryVerify(function() { return controls.controlsWrapped })
		wait(0)
		verifyControlFits(flow, pauseButton, "long pause action")
		verifyControlFits(flow, muteButton, "long mute action")
		verifyControlFits(flow, stopButton, "long stop action")

		pauseButton.clicked()
		compare(shareBackend.pauseCalls, 1)
		verify(shareBackend.paused)
		stopButton.clicked()
		compare(shareBackend.stopCalls, 1)
	}

	function test_stream_volume_remains_operable_at_minimum_width() {
		const slider = findChild(controls, "screenShareVolumeSlider")
		verify(slider.width > 0 && slider.height > 0)
		verify(slider.enabled)
		compare(slider.Accessible.name, "Stream volume")
		slider.value = 41
		slider.moved()
		compare(shareBackend.volumeCalls, 1)
		compare(shareBackend.audioVolume, 41)
	}

	function test_operation_states_reduce_controls_to_the_only_available_actions() {
		const flow = findChild(controls, "screenShareHeaderActions")
		const pauseButton = findChild(controls, "screenSharePauseButton")
		const muteButton = findChild(controls, "screenShareMuteButton")
		const volumeSlider = findChild(controls, "screenShareVolumeSlider")
		const fullscreenButton = findChild(controls, "screenShareFullscreenButton")
		const stopButton = findChild(controls, "screenShareStopButton")
		const badge = findChild(controls, "screenShareStateBadge")
		verify(badge !== null)

		shareBackend.operationStatus = "loading"
		tryCompare(controls, "stateLabel", "Reconnecting")
		verify(flow.visible)
		verify(!pauseButton.visible)
		verify(!muteButton.visible)
		verify(!volumeSlider.visible)
		verify(!fullscreenButton.visible)
		verify(!pauseButton.enabled)
		verify(!muteButton.enabled)
		verify(!volumeSlider.enabled)
		verify(stopButton.enabled)
		compare(controls.stateTone, Theme.warning)

		shareBackend.operationStatus = "error"
		tryCompare(controls, "stateLabel", "Viewer unavailable")
		compare(controls.stateTone, Theme.danger)
		verify(!flow.visible)
		verify(!controls.focusInitialControl())
	}
}
