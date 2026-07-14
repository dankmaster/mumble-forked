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

	function init() {
		tryVerify(function() { return menuLoader.item !== null })
	}

	function cleanup() {
		menuLoader.item.close()
	}

	function test_menu_stays_in_the_qml_scene_and_uses_theme_tokens() {
		const popupType = menuLoader.item["popupType"]
		if (popupType !== undefined)
			compare(Number(popupType), 0) // Popup.Item on Qt 6.8+
		compare(menuLoader.item.modal, false)
		compare(menuLoader.item.dim, false)
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
		verify(Theme.chatOwnBorder.a > 0)
		verify(Theme.composerShadow.a > 0)
		verify(Theme.elevationShadow.a > 0)
		verify(Theme.onAccent.a > 0)
		verify(Theme.textFaint.a > 0 && Theme.textFaint.a < 1)
	}
}
