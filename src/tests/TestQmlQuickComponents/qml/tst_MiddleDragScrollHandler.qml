import QtQuick
import QtTest
import "qrc:/qml-shell" as Shell

TestCase {
	id: testCase
	name: "MiddleDragScrollHandler"
	when: windowShown
	visible: true
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
			smoothWheelEnabled: true
		}
	}
	SignalSpy {
		id: scrollingStartedSpy
		target: middleDrag
		signalName: "scrollingStarted"
	}
	SignalSpy {
		id: scrollingEndedSpy
		target: middleDrag
		signalName: "scrollingEnded"
	}

	function init() {
		middleDrag.cancelSmoothWheel()
		scrollingStartedSpy.clear()
		scrollingEndedSpy.clear()
		flickable.contentX = 120
		flickable.contentY = 180
		wait(1)
		verify(middleDrag.enabled, "middle-drag handler must be enabled for overflowing content")
	}

	function cleanup() {
		middleDrag.cancelSmoothWheel()
		middleDrag.endDrag()
	}

	function test_discrete_wheel_step_is_interpolated_per_frame() {
		const expectedY = 180 + middleDrag.wheelStep
		verify(middleDrag.shouldSmoothWheel(-120, 0))
		mouseWheel(middleDrag, middleDrag.width / 2, 100,
			0, -120, Qt.NoButton, Qt.NoModifier, 20)
		verify(middleDrag.wheelAnimationActive)
		compare(scrollingStartedSpy.count, 1)
		compare(middleDrag.wheelTargetY, expectedY)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		verify(flickable.contentY < expectedY,
			"the first presented frame must not teleport to the complete wheel step")
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, expectedY, 0.5)
		compare(scrollingEndedSpy.count, 1)
	}

	function test_repeated_wheel_steps_extend_the_active_target() {
		verify(middleDrag.queueSmoothWheelDelta(-120))
		const firstTarget = middleDrag.wheelTargetY
		verify(middleDrag.queueSmoothWheelDelta(-120))
		compare(middleDrag.wheelTargetY, firstTarget + middleDrag.wheelStep)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, firstTarget + middleDrag.wheelStep, 0.5)
	}

	function test_precision_wheel_stays_on_the_native_flickable_path() {
		verify(!middleDrag.shouldSmoothWheel(15, 0))
		verify(!middleDrag.shouldSmoothWheel(120, 4))
		verify(!middleDrag.wheelAnimationActive)
	}

	function test_smooth_wheel_target_is_clamped_to_content_bounds() {
		flickable.contentY = flickable.contentHeight - flickable.height - 8
		verify(middleDrag.queueSmoothWheelDelta(-120))
		compare(middleDrag.wheelTargetY,
			flickable.originY + flickable.contentHeight - flickable.height)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		compare(flickable.contentY,
			flickable.originY + flickable.contentHeight - flickable.height)
	}

	function test_held_middle_button_scrolls_continuously() {
		const initialY = flickable.contentY
		verify(flickable.visible)
		verify(middleDrag.visible)
		mousePress(testCase, flickable.x + 130, flickable.y + 100, Qt.MiddleButton)
		verify(middleDrag.pressed)
		verify(middleDrag.gestureActive)
		mouseMove(testCase, flickable.x + 130, flickable.y + 170, 10)
		compare(middleDrag.pointerY, 170)
		tryVerify(function() { return flickable.contentY > initialY }, 500)
		verify(middleDrag.scrolling)

		const firstScrolledY = flickable.contentY
		wait(40)
		verify(flickable.contentY > firstScrolledY)
		mouseRelease(testCase, flickable.x + 130, flickable.y + 170, Qt.MiddleButton)
		verify(!middleDrag.gestureActive)
		verify(!middleDrag.scrolling)
	}

	function test_middle_drag_scrolls_toward_pointer_on_both_axes() {
		middleDrag.beginDrag(130, 100)
		middleDrag.dragTo(90, 55)
		middleDrag.advanceScroll(16)
		verify(flickable.contentX < 120)
		verify(flickable.contentY < 180)

		middleDrag.dragTo(180, 160)
		middleDrag.advanceScroll(16)
		verify(flickable.contentX > 113)
		verify(flickable.contentY > 172)
	}

	function test_middle_drag_speed_increases_with_distance() {
		middleDrag.beginDrag(100, 100)
		middleDrag.dragTo(100, 130)
		middleDrag.advanceScroll(16)
		const nearDistanceDelta = flickable.contentY - 180

		flickable.contentY = 180
		middleDrag.dragTo(100, 190)
		middleDrag.advanceScroll(16)
		const farDistanceDelta = flickable.contentY - 180

		verify(nearDistanceDelta > 0)
		verify(farDistanceDelta > nearDistanceDelta)
	}

	function test_middle_drag_has_neutral_dead_zone() {
		middleDrag.beginDrag(100, 100)
		middleDrag.dragTo(105, 94)
		verify(!middleDrag.advanceScroll(16))
		compare(flickable.contentX, 120)
		compare(flickable.contentY, 180)
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
		middleDrag.dragTo(-120, -120)
		middleDrag.advanceScroll(50)
		compare(flickable.contentX, flickable.originX)
		compare(flickable.contentY, flickable.originY)

		flickable.contentX = flickable.contentWidth - flickable.width - 2
		flickable.contentY = flickable.contentHeight - flickable.height - 3
		middleDrag.dragTo(280, 280)
		middleDrag.advanceScroll(50)
		compare(flickable.contentX, flickable.originX + flickable.contentWidth - flickable.width)
		compare(flickable.contentY, flickable.originY + flickable.contentHeight - flickable.height)
	}
}
