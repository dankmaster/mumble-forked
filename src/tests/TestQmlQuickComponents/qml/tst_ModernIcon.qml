import QtQuick
import QtTest

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

	function test_unknown_icon_is_safely_hidden() {
		loader.item.name = "not-an-icon"
		compare(loader.item.pathData, "")
		verify(!iconShape().visible)
	}

	function test_semantic_navigation_and_media_icons_have_vector_paths() {
		const names = ["chevron-down", "chevron-up", "previous", "next", "external", "play", "warning"]
		for (let index = 0; index < names.length; ++index) {
			loader.item.name = names[index]
			verify(loader.item.pathData.length > 0, names[index])
			verify(iconShape().visible, names[index])
		}
	}
}
