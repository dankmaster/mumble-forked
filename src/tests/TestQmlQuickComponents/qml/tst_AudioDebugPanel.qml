import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "AudioDebugPanel"
	when: windowShown
	width: 720
	height: 520
	visible: true

	QtObject {
		id: fakeController
		property int invocationCount: 0
		property string lastAction: ""
		property var lastPayload: ({})
		function invokeAction(actionId, payload) {
			++invocationCount
			lastAction = String(actionId)
			lastPayload = payload || ({})
		}
	}

	Loader {
		id: panelLoader
		width: 680
		Component.onCompleted: setSource("qrc:/qml-shell/AudioDebugPanel.qml", {
			"controller": fakeController,
			"field": defaultField()
		})
	}

	function defaultField() {
		return {
			"id": "audio.audioDebug",
			"active": false,
			"finalizing": false,
			"hasCapture": false,
			"statusText": "Ready",
			"privacyText": "Nothing is uploaded.",
			"guidanceText": "Reproduce the issue.",
			"startActionId": "audioDebug.start",
			"stopActionId": "audioDebug.stop",
			"refreshActionId": "audioDebug.refresh",
			"openFolderActionId": "audioDebug.openFolder",
			"defaultDurationSeconds": 60,
			"defaultCaptureRawInput": true,
			"defaultCaptureServerMix": false
		}
	}

	function init() {
		tryVerify(function() { return panelLoader.item !== null })
		panelLoader.item.field = defaultField()
		fakeController.invocationCount = 0
		fakeController.lastAction = ""
		fakeController.lastPayload = ({})
	}

	function test_capture_is_explicit_and_received_voices_are_opt_in() {
		const rawInput = findChild(panelLoader.item, "audioDebugRawInput")
		const serverMix = findChild(panelLoader.item, "audioDebugServerMix")
		const captureButton = findChild(panelLoader.item, "audioDebugCaptureButton")
		verify(rawInput !== null && serverMix !== null && captureButton !== null)
		compare(rawInput.checked, true)
		compare(serverMix.checked, false)
		compare(captureButton.text, "Start 60-second capture")

		mouseClick(captureButton)
		compare(fakeController.invocationCount, 1)
		compare(fakeController.lastAction, "audioDebug.start")
		compare(fakeController.lastPayload.durationSeconds, 60)
		compare(fakeController.lastPayload.captureRawInput, true)
		compare(fakeController.lastPayload.captureServerMix, false)
	}

	function test_active_capture_offers_stop_and_disables_privacy_choices() {
		panelLoader.item.field = {
			"id": "audio.audioDebug",
			"active": true,
			"finalizing": false,
			"hasCapture": true,
			"statusText": "Recording locally",
			"captureRawInput": true,
			"captureServerMix": false,
			"stopActionId": "audioDebug.stop",
			"refreshActionId": "",
			"openFolderActionId": "audioDebug.openFolder"
		}
		const rawInput = findChild(panelLoader.item, "audioDebugRawInput")
		const serverMix = findChild(panelLoader.item, "audioDebugServerMix")
		const captureButton = findChild(panelLoader.item, "audioDebugCaptureButton")
		compare(rawInput.enabled, false)
		compare(serverMix.enabled, false)
		compare(captureButton.text, "Stop and save")

		mouseClick(captureButton)
		compare(fakeController.invocationCount, 1)
		compare(fakeController.lastAction, "audioDebug.stop")
	}
}
