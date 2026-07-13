import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "PayloadMenuItem"
	when: windowShown
	width: 420
	height: 180

	Loader {
		id: actionLoader
		width: 320
		Component.onCompleted: setSource("qrc:/qml-shell/PayloadMenuItem.qml", {
			"payload": {
				"kind": "action", "id": "join", "label": "Join room",
				"enabled": true, "checkable": true, "checked": false,
				"hint": "Join the selected room"
			}
		})
	}

	Loader {
		id: sliderLoader
		y: 64
		width: 380
		Component.onCompleted: setSource("qrc:/qml-shell/PayloadMenuItem.qml", {
			"payload": {
				"kind": "slider", "id": "localVolume", "label": "Local volume",
				"enabled": true, "value": 75, "min": 0, "max": 200, "step": 5, "suffix": "%"
			}
		})
	}

	SignalSpy {
		id: actionSpy
		target: actionLoader.item
		signalName: "actionRequested"
	}

	function init() {
		tryVerify(function() { return actionLoader.item !== null && sliderLoader.item !== null })
		actionSpy.clear()
	}

	function test_action_payload_is_keyboard_triggerable() {
		compare(actionLoader.item.checkable, true)
		compare(actionLoader.item.checked, false)
		actionLoader.item.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(actionSpy.count, 1)
		compare(actionSpy.signalArguments[0][0], "join")
		compare(actionLoader.item.Accessible.name, "Join room")
		compare(actionLoader.item.checked, true)
	}

	function test_slider_payload_preserves_typed_range() {
		const slider = findChild(sliderLoader.item, "payloadValueSlider")
		verify(slider !== null)
		compare(slider.from, 0)
		compare(slider.to, 200)
		compare(slider.stepSize, 5)
		compare(slider.value, 75)
		compare(slider.Accessible.name, "Local volume")
	}
}
