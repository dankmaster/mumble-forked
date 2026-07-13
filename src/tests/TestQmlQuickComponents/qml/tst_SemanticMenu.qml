import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "SemanticMenu"
	when: windowShown
	width: 520
	height: 420

	Loader {
		id: menuLoader
		Component.onCompleted: setSource("qrc:/qml-shell/SemanticMenu.qml", {
			"headerTitle": "Connected to Test",
			"headerSubtitle": "Alice · Muted",
			"headerTone": "warning",
			"maximumHeight": 260,
			"groups": [
				{
					"id": "server",
					"label": "Server",
					"items": [
						{ "kind": "separator" },
						{ "kind": "action", "id": "server.connect", "label": "Connect", "enabled": true },
						{ "kind": "separator" },
						{ "kind": "separator" },
						{ "kind": "action", "id": "server.disconnect", "label": "Disconnect", "enabled": false,
							"tone": "danger" },
						{ "kind": "separator" }
					]
				},
				{
					"id": "configure",
					"label": "Configure",
					"items": [
						{ "kind": "action", "id": "configure.settings", "label": "Settings", "enabled": true },
						{ "kind": "action", "id": "hidden", "label": "Hidden", "visible": false }
					]
				}
			]
		})
	}

	SignalSpy {
		id: actionSpy
		target: menuLoader.item
		signalName: "actionRequested"
	}

	function init() {
		tryVerify(function() { return menuLoader.item !== null })
		actionSpy.clear()
	}

	function cleanup() {
		menuLoader.item.close()
	}

	function itemForAction(actionId) {
		for (let index = 0; index < menuLoader.item.count; ++index) {
			const candidate = menuLoader.item.itemAt(index)
			if (candidate && candidate.payload && candidate.payload.id === actionId)
				return candidate
		}
		return null
	}

	function test_semantic_groups_preserve_headers_actions_and_separators() {
		const entries = menuLoader.item.renderedEntries
		compare(entries.length, 7)
		compare(entries[0].kind, "header")
		compare(entries[1].kind, "section")
		compare(entries[1].label, "Server")
		compare(entries[2].id, "server.connect")
		compare(entries[3].kind, "separator")
		compare(entries[4].id, "server.disconnect")
		compare(entries[5].kind, "section")
		compare(entries[6].id, "configure.settings")
		verify(entries.every(function(entry) { return entry.id !== "hidden" }))
	}

	function test_action_uses_semantic_id_and_full_typed_payload() {
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const connectItem = itemForAction("server.connect")
		verify(connectItem !== null)
		connectItem.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(actionSpy.count, 1)
		compare(actionSpy.signalArguments[0][0], "server.connect")
		compare(actionSpy.signalArguments[0][1].label, "Connect")
	}

	function test_menu_is_bounded_and_disabled_danger_action_is_expressive() {
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		verify(menuLoader.item.height <= menuLoader.item.maximumHeight)
		const disconnectItem = itemForAction("server.disconnect")
		verify(disconnectItem !== null)
		compare(disconnectItem.enabled, false)
		compare(disconnectItem.itemTone, "danger")
		compare(disconnectItem.Accessible.description, "")
	}
}
