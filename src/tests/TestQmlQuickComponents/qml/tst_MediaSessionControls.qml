import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "MediaSessionControls"
	when: windowShown
	width: 820
	height: 300

	QtObject {
		id: session
		property bool active: true
		property bool sharedAvailable: true
		property bool sharedJoined: true
		property bool sharedHost: true
		property int sharedParticipantCount: 3
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property string state: "paused"
		property real position: 65
		property real duration: 3665
		property int loadProgress: 0
		property int volume: 72
		property bool muted: false
		property int playCalls: 0
		property int pauseCalls: 0
		property int seekCalls: 0
		property int volumeCalls: 0
		property int muteCalls: 0

		function play() { playCalls += 1; state = "playing" }
		function pause() { pauseCalls += 1; state = "paused" }
		function seek(value) { seekCalls += 1; position = value }
		function setVolume(value) { volumeCalls += 1; volume = value }
		function toggleMuted() { muteCalls += 1; muted = !muted }
	}

	Loader {
		id: controlsLoader
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: item ? item.implicitHeight : 100
		Component.onCompleted: setSource(
			Qt.resolvedUrl("../../../mumble/qml-shell/MediaSessionControls.qml"),
			{ "session": session })
	}

	SignalSpy {
		id: exitSpy
		target: controlsLoader.item
		signalName: "exitConfirmed"
	}

	SignalSpy {
		id: fullscreenSpy
		target: controlsLoader.item
		signalName: "fullscreenRequested"
	}

	SignalSpy {
		id: externalSpy
		target: controlsLoader.item
		signalName: "externalRequested"
	}

	function init() {
		tryVerify(function() { return controlsLoader.item !== null })
		if (controlsLoader.item.confirmationVisible)
			controlsLoader.item.dismissClosePrompt()
		if (controlsLoader.item.volumePopupVisible)
			findChild(controlsLoader.item, "mediaVolumePopup").close()
		testCase.width = 820
		exitSpy.clear()
		fullscreenSpy.clear()
		externalSpy.clear()
		session.active = true
		session.sharedAvailable = true
		session.sharedJoined = true
		session.sharedHost = true
		session.sharedParticipantCount = 3
		session.playbackControllable = true
		session.playbackControlAllowed = true
		session.state = "paused"
		session.position = 65
		session.duration = 3665
		session.loadProgress = 0
		session.volume = 72
		session.muted = false
		session.playCalls = 0
		session.pauseCalls = 0
		session.seekCalls = 0
		session.volumeCalls = 0
		session.muteCalls = 0
		controlsLoader.item.externalAvailable = false
		controlsLoader.item.fullscreen = false
	}

	function test_time_formatting_and_accessible_toolbar() {
		const controls = controlsLoader.item
		compare(controls.formatTime(5), "0:05")
		compare(controls.formatTime(65), "1:05")
		compare(controls.formatTime(3665), "1:01:05")
		compare(controls.Accessible.role, Accessible.ToolBar)
		verify(controls.focusInitialControl())
	}

	function test_shared_window_close_requires_an_explicit_choice() {
		const controls = controlsLoader.item
		compare(controls.requestClose(), false)
		tryVerify(function() { return controls.confirmationVisible })
		compare(exitSpy.count, 0)

		controls.confirmClosePlayer()
		tryVerify(function() { return !controls.confirmationVisible })
		compare(exitSpy.count, 1)
		compare(exitSpy.signalArguments[0][0], "close-player")
	}

	function test_host_and_participant_destructive_choices_are_typed() {
		const controls = controlsLoader.item
		controls.requestClose()
		tryVerify(function() { return controls.confirmationVisible })
		controls.confirmSharedExit()
		compare(exitSpy.count, 1)
		compare(exitSpy.signalArguments[0][0], "end-shared")

		exitSpy.clear()
		session.sharedHost = false
		controls.requestClose()
		tryVerify(function() { return controls.confirmationVisible })
		controls.confirmSharedExit()
		compare(exitSpy.count, 1)
		compare(exitSpy.signalArguments[0][0], "leave-shared")
	}

	function test_solo_player_closes_without_a_shared_session_prompt() {
		const controls = controlsLoader.item
		session.sharedAvailable = false
		session.sharedJoined = false
		compare(controls.requestClose(), true)
		verify(!controls.confirmationVisible)
		compare(exitSpy.count, 1)
		compare(exitSpy.signalArguments[0][0], "close-player")
	}

	function test_guest_controls_are_read_only_but_local_audio_stays_available() {
		session.sharedHost = false
		session.playbackControlAllowed = false
		const playButton = findChild(controlsLoader.item, "mediaPlayButton")
		const muteButton = findChild(controlsLoader.item, "mediaMuteButton")
		verify(playButton !== null)
		verify(muteButton !== null)
		verify(!playButton.enabled)
		muteButton.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(session.muteCalls, 1)
		verify(session.muted)
	}

	function test_narrow_controls_keep_volume_accessible_through_a_popover() {
		testCase.width = 640
		tryCompare(controlsLoader.item, "compactControls", true)
		const wideSlider = findChild(controlsLoader.item, "mediaVolumeSlider")
		const compactButton = findChild(controlsLoader.item, "mediaCompactVolumeButton")
		const compactSlider = findChild(controlsLoader.item, "mediaCompactVolumeSlider")
		verify(wideSlider !== null)
		verify(compactButton !== null)
		verify(compactSlider !== null)
		verify(!wideSlider.visible)
		verify(controlsLoader.item.compactVolumeVisible)
		verify(compactButton.width > 0 && compactButton.height > 0)
		compare(compactButton.Accessible.name, "Adjust media volume")

		compactButton.clicked()
		tryCompare(controlsLoader.item, "volumePopupVisible", true)
		verify(compactSlider.width > 0 && compactSlider.height > 0)
		compactSlider.value = 37
		compactSlider.moved()
		compare(session.volumeCalls, 1)
		compare(session.volume, 37)
	}

	function test_long_action_labels_wrap_inside_the_minimum_width() {
		testCase.width = 640
		session.state = "error"
		controlsLoader.item.externalAvailable = true
		const flow = findChild(controlsLoader.item, "mediaTransportActions")
		const externalButton = findChild(controlsLoader.item, "mediaExternalButton")
		const closeButton = findChild(controlsLoader.item, "mediaCloseButton")
		externalButton.text = "Open this provider in the system browser using a deliberately long translated label"
		closeButton.text = "End the synchronized playback session for every participant in this room"
		tryVerify(function() { return controlsLoader.item.controlsWrapped })

		const controlNames = [ "mediaPlayButton", "mediaStateLabel", "mediaExternalButton",
			"mediaMuteButton", "mediaCompactVolumeButton", "mediaFullscreenButton", "mediaCloseButton" ]
		for (let i = 0; i < controlNames.length; ++i) {
			const control = findChild(controlsLoader.item, controlNames[i])
			verify(control !== null, controlNames[i] + " exists")
			if (control.visible)
				verify(control.x >= -0.5 && control.x + control.width <= flow.width + 0.5,
					controlNames[i] + " remains inside the responsive action flow")
		}
	}

	function test_loading_error_external_and_fullscreen_actions_remain_typed() {
		const stateLabel = findChild(controlsLoader.item, "mediaStateLabel")
		const playButton = findChild(controlsLoader.item, "mediaPlayButton")
		const externalButton = findChild(controlsLoader.item, "mediaExternalButton")
		const fullscreenButton = findChild(controlsLoader.item, "mediaFullscreenButton")

		session.state = "loading"
		session.loadProgress = 48
		tryVerify(function() { return stateLabel.text.indexOf("48%") >= 0 })
		verify(!playButton.enabled)

		session.state = "error"
		controlsLoader.item.externalAvailable = true
		tryCompare(controlsLoader.item, "externalActionAvailable", true)
		externalButton.clicked()
		compare(externalSpy.count, 1)

		fullscreenButton.clicked()
		compare(fullscreenSpy.count, 1)
		compare(fullscreenSpy.signalArguments[0][0], true)
	}
}
