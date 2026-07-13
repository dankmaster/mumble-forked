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
		compare(menuLoader.item.background.color, Theme.panel)
		compare(menuLoader.item.background.border.color, Theme.divider)
	}
}
