import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ModernIcon"
	when: windowShown
	width: 120
	height: 120
	visible: true

	Loader {
		id: loader
		anchors.centerIn: parent
		source: "qrc:/qml-shell/ModernIcon.qml"
		onLoaded: {
			item.name = "search"
			item.color = "#5ec8b0"
			item.size = 24
		}
	}

	Loader {
		id: defaultToneLoader
		visible: false
		source: "qrc:/qml-shell/ModernIcon.qml"
		onLoaded: item.name = "search"
	}

	function iconShape() {
		return findChild(loader.item, "modernIconShape")
	}

	function init() {
		verify(loader.item !== null)
		loader.item.name = "search"
		loader.item.color = "#5ec8b0"
		loader.item.size = 24
		tryVerify(function() { return iconShape() !== null && iconShape().visible })
	}

	function test_vector_source_tracks_size_and_tone() {
		const shape = iconShape()
		compare(loader.item.implicitWidth, 24)
		compare(loader.item.implicitHeight, 24)
		compare(shape.width, 24)
		compare(shape.height, 24)
		compare(shape.scale, 1)
		const searchPath = loader.item.pathData
		verify(searchPath.length > 0)

		loader.item.color = "#ef4444"
		compare(loader.item.pathData, searchPath)
		verify(shape.visible)
		loader.item.size = 18
		compare(shape.scale, 0.75)
	}

	function test_default_tone_uses_theme_text_token() {
		verify(defaultToneLoader.item !== null)
		compare(defaultToneLoader.item.color, Theme.textStrong)
		compare(defaultToneLoader.item.Accessible.ignored, true)
	}

	function test_unknown_icon_is_safely_hidden() {
		loader.item.name = "not-an-icon"
		compare(loader.item.pathData, "")
		verify(!iconShape().visible)
	}

	function test_more_icon_uses_stable_primitive_dots() {
		loader.item.name = "more"
		verify(loader.item.pathData.length > 0)
		verify(!iconShape().visible)
		const dots = findChild(loader.item, "modernMoreIcon")
		verify(dots !== null)
		verify(dots.visible)
	}

	function test_semantic_navigation_and_media_icons_have_vector_paths() {
		const names = [
			"chevron-down", "chevron-up", "previous", "next", "external", "play", "pause",
			"volume-off", "fullscreen", "fullscreen-exit", "warning"
		]
		for (let index = 0; index < names.length; ++index) {
			loader.item.name = names[index]
			verify(loader.item.pathData.length > 0, names[index])
			verify(iconShape().visible, names[index])
		}
	}

	function test_action_menu_vocabulary_has_stable_vector_paths() {
		const names = [
			"action", "add", "certificate", "connect", "copy", "delete", "disconnect", "edit",
			"eye", "eye-off", "history", "info", "join", "key", "link", "message", "minimize", "move", "pin",
			"plugin", "quit", "record", "refresh", "reply", "screen-share", "send", "settings", "shield",
			"terminal", "unlink", "user", "user-add", "user-remove", "volume"
		]
		for (let index = 0; index < names.length; ++index) {
			loader.item.name = names[index]
			verify(loader.item.pathData.length > 0, names[index])
			verify(iconShape().visible, names[index])
		}
	}
}
