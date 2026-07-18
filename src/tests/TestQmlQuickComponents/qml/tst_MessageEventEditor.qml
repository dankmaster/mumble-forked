import QtQuick
import QtQuick.Controls
import QtTest
import "../../../mumble/qml-shell" as Shell
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "MessageEventEditor"
	when: windowShown
	visible: true
	width: 420
	height: 560

	readonly property var fixtureRows: [
		{ "type": 7, "name": "User started listening to a channel",
			"console": true, "notification": false, "highlight": false,
			"tts": true, "sound": false },
		{ "type": 8, "name": "Server disconnected",
			"console": true, "notification": true, "highlight": false,
			"tts": false, "sound": true },
		{ "type": 9, "name": "Critical", "console": true,
			"notification": true, "highlight": true, "tts": true, "sound": false },
		{ "type": 10, "name": "Warning", "console": true,
			"notification": true, "highlight": true, "tts": false, "sound": false },
		{ "type": 11, "name": "Information", "console": true,
			"notification": false, "highlight": false, "tts": false, "sound": false },
		{ "type": 12, "name": "User joined server", "console": true,
			"notification": false, "highlight": false, "tts": false, "sound": false },
		{ "type": 13, "name": "User left server", "console": true,
			"notification": false, "highlight": false, "tts": false, "sound": false },
		{ "type": 14, "name": "User stopped listening to a channel", "console": true,
			"notification": true, "highlight": false, "tts": true, "sound": false }
	]

	Shell.MessageEventEditor {
		id: editor
		visible: true
		anchors.fill: parent
		field: ({ "label": "Event behavior", "rows": testCase.fixtureRows })
	}

	function compactDelegate(type) {
		return findChild(editor, "messageEventRow_" + String(type))
	}

	function test_420_layout_uses_readable_event_cards_without_clipping_actions() {
		compare(editor.width, 420)
		verify(editor.compactCardLayout)

		const list = findChild(editor, "messageEventList")
		const header = findChild(editor, "messageEventHeader")
		const compactHeader = findChild(editor, "messageEventCompactHeader")
		let row = null
		tryVerify(function() {
			row = compactDelegate(7)
			return list !== null && header !== null && compactHeader !== null && row !== null
		})
		verify(compactHeader.visible,
			"compact=" + editor.compactCardLayout + ", header=" + header.visible
				+ ", compactHeader=" + compactHeader.visible + ", list=" + list.visible)
		verify(row.height >= 64)
		verify(list.width <= editor.width)
		verify(list.contentHeight > list.height)

		const name = findChild(row, "messageEventCompactName_7")
		const actions = findChild(row, "messageEventCompactActions_7")
		const controls = [
			findChild(row, "messageEventCompactConsole_7"),
			findChild(row, "messageEventCompactNotification_7"),
			findChild(row, "messageEventCompactHighlight_7"),
			findChild(row, "messageEventCompactTts_7"),
			findChild(row, "messageEventCompactSound_7")
		]
		const labels = [
			findChild(row, "messageEventCompactConsoleLabel_7"),
			findChild(row, "messageEventCompactNotificationLabel_7"),
			findChild(row, "messageEventCompactHighlightLabel_7"),
			findChild(row, "messageEventCompactTtsLabel_7"),
			findChild(row, "messageEventCompactSoundLabel_7")
		]
		verify(name !== null && actions !== null)
		compare(name.font.pixelSize, Theme.fontBody)
		verify(name.width > 300,
			"The event name should own a full readable line instead of sharing five action columns")
		const actionTopLeft = actions.mapToItem(row, 0, 0)
		verify(actionTopLeft.x >= 0 && actionTopLeft.y >= 0)
		verify(actionTopLeft.x + actions.width <= row.width + 0.5)
		verify(actionTopLeft.y + actions.height <= row.height + 0.5)

		for (let index = 0; index < controls.length; ++index) {
			const control = controls[index]
			const label = labels[index]
			verify(control !== null && control.visible)
			verify(label !== null && label.visible)
			verify(label.font.pixelSize >= 11,
				"Compact action labels must use a readable design token")
			const topLeft = control.mapToItem(row, 0, 0)
			verify(topLeft.x >= -0.5 && topLeft.x + control.width <= row.width + 0.5,
				control.objectName + " must remain inside the 420 px card")
			verify(label.paintedWidth <= label.width + 0.5,
				label.objectName + " must not clip its visible action label")
		}
	}

	function test_compact_cards_preserve_state_actions_keyboard_order_and_semantics() {
		let row = null
		tryVerify(function() {
			row = compactDelegate(7)
			return row !== null
		})
		const list = findChild(editor, "messageEventList")
		const log = findChild(row, "messageEventCompactConsole_7")
		const notify = findChild(row, "messageEventCompactNotification_7")
		const highlight = findChild(row, "messageEventCompactHighlight_7")
		const tts = findChild(row, "messageEventCompactTts_7")
		const sound = findChild(row, "messageEventCompactSound_7")

		compare(list.Accessible.role, Accessible.List)
		compare(list.Accessible.name, "Event behavior")
		compare(row.Accessible.role, Accessible.ListItem)
		compare(row.Accessible.name, "User started listening to a channel")
		compare(log.Accessible.role, Accessible.CheckBox)
		compare(log.Accessible.name, "User started listening to a channel: log")
		compare(log.checked, true)
		compare(notify.checked, false)
		compare(tts.checked, true)
		compare(sound.checked, false)

		log.forceActiveFocus()
		tryCompare(log, "activeFocus", true)
		keyClick(Qt.Key_Right)
		tryCompare(notify, "activeFocus", true)
		keyClick(Qt.Key_Right)
		tryCompare(highlight, "activeFocus", true)
		keyClick(Qt.Key_Space)
		compare(highlight.checked, true)
		compare(dialogState.lastAction, "messages.toggleEvent")
		compare(dialogState.lastPayload.messageType, 7)
		compare(dialogState.lastPayload.property, "highlight")
		compare(dialogState.lastPayload.value, true)
		keyClick(Qt.Key_Right)
		tryCompare(tts, "activeFocus", true)
		keyClick(Qt.Key_Right)
		tryCompare(sound, "activeFocus", true)
		keyClick(Qt.Key_Left)
		tryCompare(tts, "activeFocus", true)
	}

	function test_offscreen_rows_enter_accessibility_only_inside_the_event_viewport() {
		const list = findChild(editor, "messageEventList")
		verify(list !== null)
		list.positionViewAtBeginning()
		wait(0)

		let firstRow = null
		tryVerify(function() {
			firstRow = compactDelegate(7)
			return firstRow !== null
		})
		tryCompare(firstRow, "accessibilityExposed", true)
		const offscreenRow = list.itemAtIndex(6)
		if (offscreenRow !== null) {
			compare(offscreenRow.accessibilityExposed, false)
			const offscreenName = findChild(offscreenRow, "messageEventCompactName_13")
			const offscreenBarrier = findChild(offscreenRow, "messageEventRowAccessibilityBarrier_13")
			verify(offscreenName !== null && offscreenBarrier !== null)
			tryCompare(offscreenBarrier, "active", true)
			tryCompare(offscreenRow.Accessible, "ignored", true)
			tryCompare(offscreenName.Accessible, "ignored", true)
		}

		list.positionViewAtEnd()
		let visibleRow = null
		let visibleName = null
		let visibleBarrier = null
		tryVerify(function() {
			visibleRow = list.itemAtIndex(6)
			visibleName = visibleRow ? findChild(visibleRow, "messageEventCompactName_13") : null
			visibleBarrier = visibleRow
				? findChild(visibleRow, "messageEventRowAccessibilityBarrier_13") : null
			return visibleRow !== null && visibleName !== null && visibleBarrier !== null
				&& visibleRow.accessibilityExposed
		})
		tryCompare(visibleBarrier, "active", false)
		tryCompare(visibleRow.Accessible, "ignored", false)
		tryCompare(visibleName.Accessible, "ignored", false)
		list.positionViewAtBeginning()
	}
}
