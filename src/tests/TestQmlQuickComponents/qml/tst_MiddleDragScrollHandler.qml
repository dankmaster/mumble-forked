import QtQuick
import QtTest
import "qrc:/qml-shell" as Shell

TestCase {
	id: testCase
	name: "MiddleDragScrollHandler"
	when: windowShown
	width: 420
	height: 320

	Flickable {
		id: flickable
		objectName: "middleDragFixture"
		x: 20
		y: 20
		width: 260
		height: 200
		contentWidth: 620
		contentHeight: 840
		interactive: false
		boundsBehavior: Flickable.StopAtBounds

		Rectangle {
			width: flickable.contentWidth
			height: flickable.contentHeight
			color: "#202030"
		}

		Shell.MiddleDragScrollHandler {
			id: middleDrag
			targetFlickable: flickable
		}
	}

	function init() {
		flickable.contentX = 120
		flickable.contentY = 180
		wait(1)
		verify(middleDrag.enabled, "middle-drag handler must be enabled for overflowing content")
	}

	function test_middle_drag_grabs_and_scrolls_both_axes() {
		middleDrag.beginDrag(130, 100)
		middleDrag.dragTo(90, 55)
		verify(flickable.contentX > 145)
		verify(flickable.contentY > 210)
	}

	function test_left_drag_keeps_product_pointer_semantics() {
		compare(middleDrag.acceptedButtons, Qt.MiddleButton)
		verify(!(middleDrag.acceptedButtons & Qt.LeftButton))
		compare(flickable.contentX, 120)
		compare(flickable.contentY, 180)
	}

	function test_middle_drag_clamps_to_content_bounds() {
		flickable.contentX = 2
		flickable.contentY = 3
		middleDrag.beginDrag(80, 80)
		middleDrag.dragTo(220, 190)
		compare(flickable.contentX, flickable.originX)
		compare(flickable.contentY, flickable.originY)
	}
}
