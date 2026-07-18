import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "SemanticMenu"
	when: windowShown
	width: 760
	height: 420

	Loader {
		id: menuLoader
		Component.onCompleted: setSource("qrc:/qml-shell/SemanticMenu.qml", {
			"objectName": "semanticMenuTest",
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
						{ "kind": "action", "id": "server.connect", "label": "Connect", "enabled": true,
							"icon": "connect" },
						{ "kind": "separator" },
						{ "kind": "separator" },
						{ "kind": "action", "id": "server.disconnect", "label": "Disconnect", "enabled": false,
							"tone": "danger" },
						{ "kind": "action", "id": "server.administration", "label": "Administration",
							"enabled": true, "hasSubmenu": true, "submenu": { "items": [
								{ "kind": "action", "id": "server.banList", "label": "Ban list",
									"enabled": true, "payload": { "scope": "server", "revision": 7 } }
							] } },
						{ "kind": "separator" }
					]
				},
				{
					"id": "configure",
					"label": "Configure",
					"items": [
						{ "kind": "action", "id": "configure.settings", "label": "Settings", "enabled": true,
							"checkable": true, "checked": true, "shortcut": "Ctrl+,", "icon": "settings" },
						{ "kind": "slider", "id": "configure.volume", "label": "Volume", "enabled": true,
							"value": 80, "min": 0, "max": 200, "step": 5, "suffix": "%" },
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

	Item {
		id: narrowLogicalViewport
		width: 240
		height: 180
		visible: false
	}

	function init() {
		tryVerify(function() { return menuLoader.item !== null })
		actionSpy.clear()
	}

	function cleanup() {
		menuLoader.item.dismiss()
		// Popup.visible changes before the exit transition emits closed. Waiting for
		// the cascade to settle keeps the next test from inheriting a late close.
		tryVerify(function() { return !menuLoader.item.visible }, 1000)
		wait(Theme.motionFast + 40)
	}

	function itemForAction(actionId) {
		return itemForActionInMenu(menuLoader.item, actionId)
	}

	function itemForActionInMenu(targetMenu, actionId) {
		if (!targetMenu)
			return null
		for (let index = 0; index < targetMenu.count; ++index) {
			const candidate = targetMenu.itemAt(index)
			if (candidate && candidate.payload && candidate.payload.id === actionId)
				return candidate
			const child = targetMenu.menuAt(index)
			const nested = itemForActionInMenu(child, actionId)
			if (nested)
				return nested
		}
		return null
	}

	function menuForGroup(groupId) {
		for (let index = 0; index < menuLoader.item.count; ++index) {
			const child = menuLoader.item.menuAt(index)
			if (child && child.menuPayload
					&& child.menuPayload.id === "semantic-menu-group-" + groupId)
				return child
		}
		return null
	}

	function childMenuForAction(targetMenu, actionId) {
		if (!targetMenu)
			return null
		for (let index = 0; index < targetMenu.count; ++index) {
			const child = targetMenu.menuAt(index)
			if (child && child.menuPayload && child.menuPayload.id === actionId)
				return child
		}
		return null
	}

	function triggerForChildMenu(targetMenu, childMenu) {
		for (let index = 0; index < targetMenu.count; ++index) {
			if (targetMenu.menuAt(index) === childMenu)
				return targetMenu.itemAt(index)
		}
		return null
	}

	function triggerForMenu(childMenu) {
		for (let index = 0; index < menuLoader.item.count; ++index) {
			if (menuLoader.item.menuAt(index) === childMenu)
				return menuLoader.item.itemAt(index)
		}
		return null
	}

	function openSubmenuWithKey(item, key) {
		verify(item !== null)
		item.forceActiveFocus()
		keyClick(key)
	}

	function test_semantic_groups_preserve_headers_actions_and_separators() {
		const entries = menuLoader.item.renderedEntries
		compare(entries.length, 3)
		compare(entries[0].kind, "header")
		compare(entries[1].kind, "submenu")
		compare(entries[1].label, "Server")
		compare(entries[1].icon, "connect")
		compare(entries[1].items[0].id, "server.connect")
		compare(entries[1].items[1].kind, "separator")
		compare(entries[1].items[2].id, "server.disconnect")
		compare(entries[1].items[3].id, "server.administration")
		compare(entries[1].items[3].kind, "action")
		compare(entries[2].kind, "submenu")
		compare(entries[2].label, "Configure")
		compare(entries[2].icon, "settings")
		compare(entries[2].items[0].id, "configure.settings")
		verify(entries[2].items.every(function(entry) { return entry.id !== "hidden" }))
	}

	function test_group_rows_render_representative_typed_action_icons() {
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const serverTrigger = triggerForMenu(menuForGroup("server"))
		const configureTrigger = triggerForMenu(menuForGroup("configure"))
		verify(serverTrigger !== null && configureTrigger !== null)
		compare(serverTrigger.objectName,
			"payloadMenuItem_semanticMenuTest_semantic-menu-group-server")
		compare(configureTrigger.objectName,
			"payloadMenuItem_semanticMenuTest_semantic-menu-group-configure")
		compare(serverTrigger.iconName, "connect")
		compare(configureTrigger.iconName, "settings")
		const serverIcon = findChild(serverTrigger, "payloadLeadingIcon")
		const configureIcon = findChild(configureTrigger, "payloadLeadingIcon")
		verify(serverIcon !== null && configureIcon !== null)
		compare(serverIcon.name, "connect")
		compare(configureIcon.name, "settings")
		verify(serverIcon.visible && configureIcon.visible)
		compare(serverTrigger.Accessible.name, "Server")
		compare(findChild(serverTrigger, "payloadPrimaryLabel").Accessible.ignored, true)
	}

	function test_action_uses_semantic_id_and_full_typed_payload() {
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const serverMenu = menuForGroup("server")
		verify(serverMenu !== null)
		const serverTrigger = triggerForMenu(serverMenu)
		openSubmenuWithKey(serverTrigger, Qt.Key_Right)
		tryVerify(function() { return serverMenu.visible })
		const connectItem = itemForActionInMenu(serverMenu, "server.connect")
		verify(connectItem !== null, "server count=" + serverMenu.count
			+ " entries=" + JSON.stringify(serverMenu.entries)
			+ " normalized=" + JSON.stringify(serverMenu.normalizedEntries)
			+ " payload=" + JSON.stringify(serverMenu.menuPayload))
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

	function test_surface_focus_ring_yields_to_the_focused_menu_row() {
		const menu = menuLoader.item
		menu.open()
		tryVerify(function() { return menu.visible })
		const surface = menu.background

		menu.contentItem.forceActiveFocus()
		tryCompare(menu, "surfaceOwnsActiveFocus", true)
		compare(surface.border.color, Theme.focus)

		const serverMenu = menuForGroup("server")
		const serverTrigger = triggerForMenu(serverMenu)
		verify(serverTrigger !== null)
		const rowFocus = findChild(serverTrigger, "payloadFocusBackground")
		verify(rowFocus !== null)
		serverTrigger.forceActiveFocus()
		tryCompare(serverTrigger, "activeFocus", true)
		tryCompare(menu, "surfaceOwnsActiveFocus", false)
		compare(surface.border.color, Theme.popupBorder)
		compare(rowFocus.border.color, Theme.focus)
		compare(rowFocus.border.width, Theme.focusRingWidth)
	}

	function test_submenu_keyboard_navigation_restores_focus_and_does_not_dispatch_on_open() {
		mouseMove(testCase, testCase.width - 2, testCase.height - 2)
		wait(Theme.motionFast + 20)
		menuLoader.item.x = 24
		menuLoader.item.y = 24
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const serverMenu = menuForGroup("server")
		verify(serverMenu !== null)
		const serverTrigger = triggerForMenu(serverMenu)

		openSubmenuWithKey(serverTrigger, Qt.Key_Return)
		tryVerify(function() { return serverMenu.visible })
		compare(actionSpy.count, 0)
		const connectItem = itemForActionInMenu(serverMenu, "server.connect")
		connectItem.forceActiveFocus()
		keyClick(Qt.Key_Left)
		tryVerify(function() { return !serverMenu.visible })
		tryVerify(function() { return serverTrigger.activeFocus })
		verify(serverTrigger.hasSubmenu)
		compare(serverTrigger.subMenu, serverMenu)

		keyClick(Qt.Key_Space)
		tryVerify(function() { return serverMenu.visible }, 1000,
			"space target focus=" + serverTrigger.activeFocus
			+ " highlighted=" + serverTrigger.highlighted)
		compare(actionSpy.count, 0)
		keyClick(Qt.Key_Escape)
		tryVerify(function() { return !serverMenu.visible })
		tryVerify(function() { return !menuLoader.item.visible || serverTrigger.activeFocus }, 1000)
	}

	function test_nested_submenu_keeps_stable_action_id_and_typed_payload() {
		mouseMove(testCase, testCase.width - 2, testCase.height - 2)
		menuLoader.item.x = 24
		menuLoader.item.y = 24
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const serverMenu = menuForGroup("server")
		openSubmenuWithKey(triggerForMenu(serverMenu), Qt.Key_Right)
		tryVerify(function() { return serverMenu.visible })
		const administrationMenu = childMenuForAction(serverMenu, "server.administration")
		verify(administrationMenu !== null)
		const administrationTrigger = triggerForChildMenu(serverMenu, administrationMenu)
		openSubmenuWithKey(administrationTrigger, Qt.Key_Enter)
		tryVerify(function() { return administrationMenu.visible })
		const banListItem = itemForActionInMenu(administrationMenu, "server.banList")
		verify(banListItem !== null)
		banListItem.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(actionSpy.count, 1)
		compare(actionSpy.signalArguments[0][0], "server.banList")
		compare(actionSpy.signalArguments[0][1].payload.scope, "server")
		compare(actionSpy.signalArguments[0][1].payload.revision, 7)
	}

	function test_only_one_pointer_opened_submenu_stays_visible() {
		menuLoader.item.x = 24
		menuLoader.item.y = 24
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const serverMenu = menuForGroup("server")
		const configureMenu = menuForGroup("configure")
		verify(serverMenu !== null && configureMenu !== null)
		const serverTrigger = triggerForMenu(serverMenu)
		const configureTrigger = triggerForMenu(configureMenu)
		const rootOpenRequests = menuLoader.item.submenuOpenRequestCount
		mouseMove(serverTrigger, serverTrigger.width / 2, serverTrigger.height / 2)
		compare(serverMenu.visible, true,
			"hover must open the prepared Server submenu in the same input turn")
		compare(menuLoader.item.submenuOpenRequestCount, rootOpenRequests + 1)
		mouseMove(configureTrigger, configureTrigger.width / 2, configureTrigger.height / 2)
		compare(configureMenu.visible, true,
			"switching siblings must not wait for the Server exit transition")
		compare(menuLoader.item.activeSubmenu, configureMenu)
		tryVerify(function() { return !serverMenu.visible }, Theme.motionFast + 200)
		compare(actionSpy.count, 0)
	}

	function test_stable_payload_refresh_reuses_the_compiled_submenu_tree() {
		const menu = menuLoader.item
		const originalGroups = menu.groups
		const serverMenu = menuForGroup("server")
		const administrationMenu = childMenuForAction(serverMenu, "server.administration")
		verify(serverMenu !== null && administrationMenu !== null)
		const rootCreated = menu.entryObjectCreationCount
		const rootRebuilds = menu.entryTreeRebuildCount
		const serverCreated = serverMenu.entryObjectCreationCount
		const serverRebuilds = serverMenu.entryTreeRebuildCount
		const sharedFactory = menu.sharedSubmenuComponent
		verify(sharedFactory !== null)
		compare(serverMenu.sharedSubmenuComponent, sharedFactory)
		compare(administrationMenu.sharedSubmenuComponent, sharedFactory)

		const refreshedGroups = JSON.parse(JSON.stringify(originalGroups))
		refreshedGroups[0].items[1].label = "Connect now"
		refreshedGroups[0].items[1].enabled = false
		try {
			menu.groups = refreshedGroups
			tryCompare(itemForActionInMenu(serverMenu, "server.connect"), "text", "Connect now")
			compare(menuForGroup("server"), serverMenu)
			compare(childMenuForAction(serverMenu, "server.administration"), administrationMenu)
			compare(menu.entryObjectCreationCount, rootCreated)
			compare(menu.entryTreeRebuildCount, rootRebuilds)
			compare(serverMenu.entryObjectCreationCount, serverCreated)
			compare(serverMenu.entryTreeRebuildCount, serverRebuilds)
		} finally {
			menu.groups = originalGroups
			wait(0)
		}
	}

	function test_logical_pixel_limits_bound_menu_without_device_pixel_scaling() {
		const menu = menuLoader.item
		const oldPreferredWidth = menu.preferredWidth
		const oldViewportWidth = menu.logicalViewportWidth
		const oldViewportHeight = menu.logicalViewportHeight
		try {
			menu.preferredWidth = 292
			menu.configureLogicalViewport(menu, narrowLogicalViewport, 8, 12)
			wait(0)
			compare(menu.logicalViewportWidth, 224)
			compare(menu.logicalViewportHeight, 160)
			compare(menu.width, 224)
			const geometryProbe = {
				"x": 0, "y": 0, "width": 292, "height": 120,
				"implicitHeight": 120, "contentItem": null,
				"logicalViewportWidth": Number.POSITIVE_INFINITY,
				"logicalViewportHeight": Number.POSITIVE_INFINITY
			}
			verify(menu.positionAtLogicalPoint(geometryProbe, narrowLogicalViewport,
				Qt.point(999, 999), 8, 12))
			compare(geometryProbe.logicalViewportWidth, 224)
			compare(geometryProbe.logicalViewportHeight, 160)
			verify(geometryProbe.x >= 8)
			verify(geometryProbe.x + Math.min(geometryProbe.width, 224)
				<= narrowLogicalViewport.width - 8)
			verify(geometryProbe.y >= 8)
			verify(geometryProbe.y + geometryProbe.height
				<= narrowLogicalViewport.height - 12)
		} finally {
			menu.preferredWidth = oldPreferredWidth
			menu.logicalViewportWidth = oldViewportWidth
			menu.logicalViewportHeight = oldViewportHeight
		}
	}

	function test_submenu_flips_left_at_the_window_edge_and_preserves_row_types() {
		menuLoader.item.x = width - menuLoader.item.width - 4
		menuLoader.item.y = 24
		menuLoader.item.open()
		tryVerify(function() { return menuLoader.item.visible })
		const configureMenu = menuForGroup("configure")
		openSubmenuWithKey(triggerForMenu(configureMenu), Qt.Key_Right)
		tryVerify(function() { return configureMenu.visible })

		const rootPosition = menuLoader.item.contentItem.mapToItem(null, 0, 0)
		const childPosition = configureMenu.contentItem.mapToItem(null, 0, 0)
		verify(childPosition.x < rootPosition.x)
		const settingsItem = itemForActionInMenu(configureMenu, "configure.settings")
		const volumeItem = itemForActionInMenu(configureMenu, "configure.volume")
		verify(settingsItem !== null && volumeItem !== null)
		verify(settingsItem.checked)
		compare(settingsItem.payload.shortcut, "Ctrl+,")
		compare(volumeItem.itemKind, "slider")
		compare(findChild(volumeItem, "payloadValueSlider").value, 80)
	}
}
