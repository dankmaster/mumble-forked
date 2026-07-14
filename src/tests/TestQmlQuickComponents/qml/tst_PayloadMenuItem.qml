import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "PayloadMenuItem"
	when: windowShown
	width: 420
	height: 180
	visible: true

	Loader {
		id: actionLoader
		width: 320
		Component.onCompleted: setSource("qrc:/qml-shell/PayloadMenuItem.qml", {
			"payload": {
				"kind": "action", "id": "join", "label": "Join room",
				"enabled": true, "checkable": true, "checked": false,
				"hint": "Join the selected room", "icon": "join",
				"secondary": "Voice room", "state": "Available", "shortcut": "Ctrl+J"
			}
		})
	}

	Loader {
		id: dangerLoader
		y: 116
		width: 320
		Component.onCompleted: setSource("qrc:/qml-shell/PayloadMenuItem.qml", {
			"payload": {
				"kind": "action", "id": "server.disconnect", "label": "Disconnect",
				"enabled": true, "tone": "danger", "icon": "disconnect"
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
		tryVerify(function() {
			return actionLoader.item !== null && sliderLoader.item !== null && dangerLoader.item !== null
		})
		actionLoader.item.checked = false
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
		compare(actionLoader.item.Accessible.description, "Join the selected room")
		compare(actionLoader.item.checked, true)
		compare(findChild(actionLoader.item, "payloadLeadingIcon").name, "check")
	}

	function test_action_payload_renders_icon_secondary_state_shortcut_and_focus() {
		const icon = findChild(actionLoader.item, "payloadLeadingIcon")
		const secondary = findChild(actionLoader.item, "payloadSecondaryLabel")
		const state = findChild(actionLoader.item, "payloadStateLabel")
		const shortcut = findChild(actionLoader.item, "payloadShortcutLabel")
		const focusBackground = findChild(actionLoader.item, "payloadFocusBackground")
		verify(icon !== null && secondary !== null && state !== null && shortcut !== null)
		compare(actionLoader.item.visible, true)
		compare(actionLoader.item.contentItem.visible, true)
		compare(secondary.parent.visible, true)
		compare(secondary.text, "Voice room")
		verify(secondary.visible)
		compare(state.text, "Available")
		verify(state.visible)
		compare(shortcut.text, "Ctrl+J")
		verify(shortcut.visible)

		actionLoader.item.forceActiveFocus()
		tryCompare(focusBackground.border, "width", Theme.focusRingWidth, 500)
		compare(focusBackground.border.color, Theme.focus)
	}

	function test_danger_action_uses_semantic_icon_and_text_tone() {
		const icon = findChild(dangerLoader.item, "payloadLeadingIcon")
		const label = findChild(dangerLoader.item, "payloadPrimaryLabel")
		verify(icon !== null && label !== null)
		compare(icon.name, "disconnect")
		compare(icon.color, Theme.danger)
		compare(label.color, Theme.danger)
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
