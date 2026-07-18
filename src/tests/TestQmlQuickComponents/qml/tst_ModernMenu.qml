import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ModernMenu"
	when: windowShown
	width: 480
	height: 320

	Loader {
		id: menuLoader
		source: "qrc:/qml-shell/ModernMenu.qml"
	}
	Button {
		id: menuOpener
		objectName: "menuFocusOpener"
		text: "Open"
	}
	Button {
		id: focusDestination
		objectName: "menuFocusDestination"
		anchors.right: parent.right
		text: "Destination"
	}

	function init() {
		tryVerify(function() { return menuLoader.item !== null })
	}

	function closeAndSettle(menu) {
		menu.close()
		tryVerify(function() { return !menu.visible })
		// QQuickMenu finishes its ListView current-index and focus bookkeeping
		// after the exit transition. Keep dynamically owned items alive until both
		// queued turns have completed so the next test never inherits that work.
		wait(0)
		wait(0)
	}

	function cleanup() {
		closeAndSettle(menuLoader.item)
		menuLoader.item.openerItem = null
	}

	function test_root_menu_restores_its_opener_without_stealing_a_new_destination() {
		const menu = menuLoader.item
		const hostWindow = menuOpener.Window.window
		verify(hostWindow !== null)
		hostWindow.requestActivate()
		tryCompare(hostWindow, "active", true)
		menu.openerItem = menuOpener
		menuOpener.forceActiveFocus()
		tryCompare(menuOpener, "activeFocus", true)
		menu.open()
		tryVerify(function() { return menu.visible })
		menu.contentItem.forceActiveFocus()
		menu.close()
		tryVerify(function() { return menuOpener.activeFocus })

		menu.open()
		tryVerify(function() { return menu.visible })
		menu.contentItem.forceActiveFocus()
		menu.close()
		focusDestination.forceActiveFocus()
		tryVerify(function() { return focusDestination.activeFocus })
		wait(0)
		wait(0)
		verify(focusDestination.activeFocus,
			"Closing an older menu must not steal focus from a newly opened surface")
	}

	function test_open_with_initial_focus_skips_disabled_scaffolding() {
		const menu = menuLoader.item
		const disabled = Qt.createQmlObject(
			'import QtQuick.Controls; MenuItem { text: "Unavailable"; enabled: false }',
			menu.contentItem)
		const actionable = Qt.createQmlObject(
			'import QtQuick.Controls; MenuItem { objectName: "initialAction"; text: "Settings" }',
			menu.contentItem)
		verify(disabled !== null && actionable !== null)
		menu.addItem(disabled)
		menu.addItem(actionable)
		try {
			menu.openWithInitialFocus()
			tryVerify(function() { return menu.visible })
			tryCompare(actionable, "activeFocus", true)
			compare(menu.currentIndex, 1)
		} finally {
			closeAndSettle(menu)
			menu.removeItem(actionable)
			menu.removeItem(disabled)
			actionable.destroy()
			disabled.destroy()
			wait(0)
		}
	}

	function test_open_with_initial_focus_preserves_an_existing_menu_selection() {
		const menu = menuLoader.item
		const first = Qt.createQmlObject(
			'import QtQuick.Controls; MenuItem { text: "First" }', menu.contentItem)
		const selected = Qt.createQmlObject(
			'import QtQuick.Controls; MenuItem { text: "Selected" }', menu.contentItem)
		verify(first !== null && selected !== null)
		menu.addItem(first)
		menu.addItem(selected)
		try {
			menu.openWithInitialFocus()
			menu.currentIndex = 1
			selected.forceActiveFocus()
			wait(0)
			verify(selected.activeFocus,
				"The queued initial-focus pass must not reset an existing menu selection")
			compare(menu.currentIndex, 1)
		} finally {
			closeAndSettle(menu)
			menu.removeItem(selected)
			menu.removeItem(first)
			selected.destroy()
			first.destroy()
			wait(0)
		}
	}

	function test_actionable_detection_suppresses_empty_scaffolding_but_preserves_mixed_menus() {
		const menu = menuLoader.item
		verify(!menu.hasActionableEntries([]))
		verify(!menu.hasActionableEntries([
			{ "kind": "label", "label": "No actions", "enabled": false },
			{ "kind": "separator" },
			{ "kind": "action", "label": "Unavailable", "enabled": false }
		]))
		verify(!menu.hasActionableEntries([{
			"kind": "submenu", "label": "Empty", "items": [
				{ "kind": "label", "label": "Nothing here", "enabled": false }
			]
		}]))
		verify(menu.hasActionableEntries([
			{ "kind": "label", "label": "Explanation", "enabled": false },
			{ "kind": "action", "id": "retry", "label": "Retry", "enabled": true }
		]))
		verify(menu.hasActionableEntries([{
			"kind": "submenu", "label": "Manage", "items": [
				{ "kind": "action", "id": "settings", "label": "Settings" }
			]
		}]))
	}

	function test_menu_stays_in_the_qml_scene_and_uses_theme_tokens() {
		const popupType = menuLoader.item["popupType"]
		if (popupType !== undefined)
			compare(Number(popupType), 0) // Popup.Item on Qt 6.8+
		compare(menuLoader.item.modal, false)
		compare(menuLoader.item.dim, false)
		compare(menuLoader.item.contentItem.Accessible.role, Accessible.PopupMenu)
		compare(menuLoader.item.contentItem.Accessible.name, "Menu")
		compare(menuLoader.item.background.color, Theme.popupBackground)
		compare(menuLoader.item.background.border.color, Theme.popupBorder)
		compare(menuLoader.item.palette.highlight, Theme.popupSelected)
		compare(menuLoader.item.palette.link, Theme.accent)
		compare(menuLoader.item.palette.toolTipBase, Theme.popupBackground)
		compare(menuLoader.item.palette.toolTipText, Theme.textStrong)
		compare(menuLoader.item.palette.disabled.highlight, Theme.popupBorder)
		compare(menuLoader.item.palette.disabled.link, Theme.textMuted)
		compare(menuLoader.item.palette.disabled.toolTipBase, Theme.panel)
		verify(menuLoader.item.background.implicitWidth >= 220)

		const shadow = findChild(menuLoader.item.background, "modernMenuShadow")
		verify(shadow !== null)
		compare(shadow.color, Theme.elevationShadow)
		compare(shadow.y, Theme.elevationMenuOffset)
		compare(shadow.layer.enabled, false)
	}

	function test_semantic_component_roles_follow_the_canonical_theme() {
		compare(Theme.chatCanvas, Theme.shellBackground)
		compare(Theme.chatSurface, Theme.panel)
		compare(Theme.chatIncomingSurface, Theme.panel)
		compare(Theme.chatIncomingBorder, Theme.divider)
		compare(Theme.chatMetadata, Theme.textMuted)
		compare(Theme.chatReplySurface, Theme.strip)
		compare(Theme.composerBackground, Theme.surfaceRaised)
		compare(Theme.composerBorder, Theme.surfaceBorder)
		compare(Theme.composerFocusBorder, Theme.focus)
		compare(Theme.popupBackground, Theme.surfaceRaised)
		compare(Theme.popupBorder, Theme.surfaceBorder)
		compare(Theme.selfCardBackground, Theme.rail)
		compare(Theme.selfCardBorder, Theme.divider)
		compare(Theme.previewCardBackground, Theme.surfaceRaised)
		compare(Theme.previewCardHover, Theme.surfaceHover)
		compare(Theme.previewCardBorder, Theme.surfaceBorder)
		compare(Theme.embedCanvas, Theme.mediaCanvas)
		compare(Theme.embedSurface, Theme.panel)
		compare(Theme.embedBorder, Theme.surfaceBorder)
		compare(Theme.embedHover, Theme.surfaceHover)
		compare(Theme.embedRevealSurface, Theme.strip)
		compare(Theme.embedSelection, Theme.selected)
		compare(Theme.embedOverlayBase, Theme.strip)
		verify(Theme.chatOwnBorder.a > 0)
		verify(Theme.composerShadow.a > 0)
		verify(Theme.elevationShadow.a > 0)
		verify(Theme.onAccent.a > 0)
		verify(Theme.textFaint.a > 0 && Theme.textFaint.a < 1)
	}
}
