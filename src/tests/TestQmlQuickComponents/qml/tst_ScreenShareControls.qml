import QtQuick
import QtTest
import "../../../mumble/qml-shell" as Shell

TestCase {
	id: testCase
	name: "ScreenShareControls"
	when: windowShown
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

	function init() {
		testCase.width = 640
		shareBackend.title = "Shared desktop"
		shareBackend.detail = "Host · 1920 × 1080"
		shareBackend.paused = false
		shareBackend.audioAvailable = true
		shareBackend.audioMuted = false
		shareBackend.audioVolume = 72
		shareBackend.pauseCalls = 0
		shareBackend.muteCalls = 0
		shareBackend.volumeCalls = 0
		shareBackend.stopCalls = 0

		findChild(controls, "screenSharePauseButton").text = "Pause view"
		findChild(controls, "screenShareMuteButton").text = "Mute audio"
		findChild(controls, "screenShareStopButton").text = "Stop"
	}

	function verifyControlFits(flow, control, name) {
		verify(control !== null, name + " exists")
		verify(control.width > 0 && control.height > 0, name + " keeps an operable hit target")
		verify(control.Accessible.name.length > 0, name + " keeps an accessible name")
		verify(control.x >= -0.5 && control.x + control.width <= flow.width + 0.5,
			name + " remains inside the responsive action flow")
	}

	function test_minimum_width_keeps_every_control_inside_the_action_flow() {
		const flow = findChild(controls, "screenShareHeaderActions")
		verify(flow !== null)
		verifyControlFits(flow, findChild(controls, "screenSharePauseButton"), "pause")
		verifyControlFits(flow, findChild(controls, "screenShareMuteButton"), "mute")
		verifyControlFits(flow, findChild(controls, "screenShareVolumeSlider"), "volume")
		verifyControlFits(flow, findChild(controls, "screenShareStopButton"), "stop")
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
}
