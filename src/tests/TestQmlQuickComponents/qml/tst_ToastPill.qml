import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "ToastPill"
	when: windowShown
	visible: true
	width: 760
	height: 240
	property var currentController: null
	property var currentPill: null

	Component {
		id: controllerComponent
		QtObject {
			property bool visible: true
			property string tone: "info"
			property string title: "Connected"
			property string message: "Voice and chat are ready."
			property string actionId: "connection.details"
			property string actionLabel: "Details"
			property int repeatCount: 1
			property int dismissCalls: 0
			property int interactionCalls: 0
			property bool interactionActive: false
			function dismiss() {
				dismissCalls += 1
				visible = false
			}
			function setInteractionActive(active) {
				interactionCalls += 1
				interactionActive = !!active
			}
		}
	}

	SignalSpy {
		id: actionSpy
		target: testCase.currentPill
		signalName: "actionRequested"
	}

	function loadPill(overrides) {
		currentController = createTemporaryObject(controllerComponent, testCase)
		verify(currentController !== null)
		for (const key in overrides)
			currentController[key] = overrides[key]

		const component = Qt.createComponent("qrc:/qml-shell/ToastPill.qml")
		tryCompare(component, "status", Component.Ready)
		currentPill = createTemporaryObject(component, testCase, {
			"controller": currentController,
			"animationsEnabled": false,
			"width": 620
		})
		verify(currentPill !== null)
		currentPill.x = Math.round((testCase.width - currentPill.width) / 2)
		currentPill.y = Math.round((testCase.height - currentPill.height) / 2)
		actionSpy.clear()
		return currentPill
	}

	function cleanup() {
		currentPill = null
		currentController = null
		wait(0)
	}

	function test_single_center_pill_uses_alert_semantics_and_one_line_copy() {
		const pill = loadPill({})
		compare(pill.visible, true)
		compare(pill.Accessible.role, Accessible.AlertMessage)
		verify(pill.Accessible.name.indexOf("Connected") >= 0)
		verify(pill.Accessible.name.indexOf("Voice and chat are ready") >= 0)

		const background = findChild(pill, "toastPillBackground")
		const title = findChild(pill, "toastTitleLabel")
		const message = findChild(pill, "toastMessageLabel")
		verify(background !== null)
		compare(background.radius, background.height / 2)
		compare(title.maximumLineCount, 1)
		compare(message.maximumLineCount, 1)
	}

	function test_duplicate_counter_reuses_the_same_pill() {
		const pill = loadPill({ "repeatCount": 4 })
		const badge = findChild(pill, "toastRepeatBadge")
		verify(badge !== null)
		compare(badge.visible, true)
		compare(badge.text, "\u00d74")
		verify(pill.Accessible.name.indexOf("Repeated 4 times") >= 0)
	}

	function test_action_and_dismiss_are_keyboard_accessible() {
		const pill = loadPill({})
		const action = findChild(pill, "toastActionButton")
		const dismiss = findChild(pill, "toastDismissButton")
		verify(action !== null && action.visible)
		verify(dismiss !== null && dismiss.visible)
		compare(action.text, "Details")

		action.forceActiveFocus()
		tryCompare(action, "activeFocus", true)
		tryCompare(currentController, "interactionActive", true)
		keyClick(Qt.Key_Return)
		compare(actionSpy.count, 1)
		compare(actionSpy.signalArguments[0][0], "connection.details")
		compare(currentController.dismissCalls, 1)
		compare(currentController.visible, false)

		currentController.visible = true
		tryCompare(pill, "enabled", true)
		dismiss.forceActiveFocus()
		tryCompare(dismiss, "activeFocus", true)
		keyClick(Qt.Key_Space)
		compare(currentController.dismissCalls, 2)
	}

	function test_tone_is_normalized_by_the_visual_contract() {
		const pill = loadPill({ "tone": "danger", "actionId": "", "actionLabel": "" })
		compare(pill.toastTone, "danger")
		compare(pill.toneIcon, "warning")
		compare(findChild(pill, "toastActionButton").visible, false)
	}
}
