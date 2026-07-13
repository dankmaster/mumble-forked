import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "ShortcutEditor"
	when: windowShown
	width: 980
	height: 760
	visible: true

	Loader {
		id: loader
		anchors.fill: parent
		source: "qrc:/qml-shell/ShortcutEditor.qml"
	}

	function option(label, value) {
		return { "label": label, "value": value, "enabled": true }
	}

	function baseField(rows) {
		return {
			"id": "keys.shortcuts",
			"label": "Shortcuts",
			"enabled": true,
			"canCapture": true,
			"canSuppress": true,
			"actionOptions": [option("Unassigned", -1), option("Whisper/Shout", 12)],
			"toggleOptions": [option("Off", -1), option("Toggle", 0), option("On", 1)],
			"channelOptions": [option("Root", 0), option("Lobby", 7)],
			"targetModeOptions": [
				option("Current selection", "selection"),
				option("List of users", "users"),
				option("Channel", "channel")
			],
			"targetChannelOptions": [option("Current", -3), option("Root", -1), option("Lobby", 7)],
			"targetUserOptions": [option("Alice", "hash-alice"), option("Bob", "hash-bob")],
			"rows": rows
		}
	}

	function targetRow(mode, editable) {
		return {
			"index": 0,
			"actionIndex": 12,
			"actionLabel": "Whisper/Shout",
			"dataType": "target",
			"dataLabel": "Lobby",
			"dataEditable": editable === undefined ? true : editable,
			"inputLabel": "Ctrl + Space",
			"assigned": true,
			"suppress": false,
			"capturing": false,
			"target": {
				"mode": mode || "channel",
				"channelId": 7,
				"channelLabel": "Lobby",
				"group": "admins",
				"links": false,
				"children": true,
				"forceCenter": false,
				"users": mode === "users" ? [option("Alice", "hash-alice")] : [],
				"summary": "Lobby and subchannels"
			}
		}
	}

	function init() {
		verify(loader.item !== null)
		loader.item.field = baseField([targetRow("channel")])
		tryVerify(function() { return findChild(loader.item, "shortcutRow_0") !== null })
	}

	function test_target_mode_channel_group_and_flags_route_typed_payloads() {
		const mode = findChild(loader.item, "shortcutTargetMode_0")
		verify(mode !== null && mode.enabled)
		compare(mode.currentValue, "channel")
		mode.currentIndex = 0
		mode.activated(0)
		compare(dialogState.lastAction, "keys.shortcutTarget")
		compare(dialogState.lastPayload.index, 0)
		compare(dialogState.lastPayload.targetAction, "mode")
		compare(dialogState.lastPayload.mode, "selection")

		const channel = findChild(loader.item, "shortcutTargetChannel_0")
		verify(channel !== null && channel.visible)
		compare(channel.currentValue, 7)
		channel.currentIndex = 1
		channel.activated(1)
		compare(dialogState.lastPayload.targetAction, "channel")
		compare(dialogState.lastPayload.channelId, -1)

		const group = findChild(loader.item, "shortcutTargetGroup_0")
		verify(group !== null && group.visible)
		group.text = "moderators"
		group.editingFinished()
		compare(dialogState.lastPayload.targetAction, "group")
		compare(dialogState.lastPayload.group, "moderators")

		const links = findChild(loader.item, "shortcutTargetLinks_0")
		verify(links !== null && !links.checked)
		mouseClick(links)
		compare(dialogState.lastPayload.targetAction, "links")
		compare(dialogState.lastPayload.enabled, true)

		const children = findChild(loader.item, "shortcutTargetChildren_0")
		verify(children !== null && children.checked)
		mouseClick(children)
		compare(dialogState.lastPayload.targetAction, "children")
		compare(dialogState.lastPayload.enabled, false)

		const forceCenter = findChild(loader.item, "shortcutTargetForceCenter_0")
		verify(forceCenter !== null && !forceCenter.checked)
		mouseClick(forceCenter)
		compare(dialogState.lastPayload.targetAction, "forceCenter")
		compare(dialogState.lastPayload.enabled, true)
	}

	function test_user_targets_can_be_added_and_removed() {
		loader.item.field = baseField([targetRow("users")])
		const picker = findChild(loader.item, "shortcutTargetUser_0")
		const add = findChild(loader.item, "shortcutTargetAddUser_0")
		tryVerify(function() { return picker !== null && add !== null && add.visible })
		picker.currentIndex = 1
		add.clicked()
		compare(dialogState.lastAction, "keys.shortcutTarget")
		compare(dialogState.lastPayload.targetAction, "addUser")
		compare(dialogState.lastPayload.hash, "hash-bob")

		const remove = findChild(loader.item, "shortcutTargetRemoveUser_0_hash-alice")
		verify(remove !== null)
		remove.clicked()
		compare(dialogState.lastPayload.targetAction, "removeUser")
		compare(dialogState.lastPayload.hash, "hash-alice")
	}

	function test_toggle_channel_text_and_suppress_are_editable() {
		let row = targetRow("channel")
		row.dataType = "toggle"
		row.dataValue = 0
		loader.item.field = baseField([row])
		let editor = findChild(loader.item, "shortcutToggleData_0")
		tryVerify(function() { return editor !== null && editor.visible })
		editor.currentIndex = 2
		editor.activated(2)
		compare(dialogState.lastAction, "keys.shortcutData")
		compare(dialogState.lastPayload.value, 1)

		row = targetRow("channel")
		row.dataType = "channel"
		row.dataValue = 7
		loader.item.field = baseField([row])
		editor = findChild(loader.item, "shortcutChannelData_0")
		tryVerify(function() { return editor !== null && editor.visible })
		editor.currentIndex = 0
		editor.activated(0)
		compare(dialogState.lastPayload.value, 0)

		row = targetRow("channel")
		row.dataType = "text"
		row.dataValue = "old"
		loader.item.field = baseField([row])
		editor = findChild(loader.item, "shortcutTextData_0")
		tryVerify(function() { return editor !== null && editor.visible })
		editor.text = "new message"
		editor.editingFinished()
		compare(dialogState.lastPayload.value, "new message")

		const suppress = findChild(loader.item, "shortcutSuppress_0")
		verify(suppress !== null && !suppress.checked)
		mouseClick(suppress)
		compare(dialogState.lastAction, "keys.shortcutSuppress")
		compare(dialogState.lastPayload.index, 0)
		compare(dialogState.lastPayload.suppress, true)
	}

	function test_data_editability_and_global_enabled_state_are_respected() {
		loader.item.field = baseField([targetRow("channel", false)])
		let mode = findChild(loader.item, "shortcutTargetMode_0")
		tryVerify(function() { return mode !== null })
		verify(!mode.enabled)

		const disabledField = baseField([targetRow("channel", true)])
		disabledField.enabled = false
		loader.item.field = disabledField
		mode = findChild(loader.item, "shortcutTargetMode_0")
		tryVerify(function() { return mode !== null && !mode.enabled })
		verify(!findChild(loader.item, "shortcutAction_0").enabled)
		verify(!findChild(loader.item, "shortcutAddButton").enabled)
		verify(!findChild(loader.item, "shortcutCapture_0").enabled)
	}
}
