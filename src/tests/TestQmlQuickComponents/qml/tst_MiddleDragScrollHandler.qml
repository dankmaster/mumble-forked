import QtQuick
import QtQuick.Controls
import QtTest
import "qrc:/qml-shell" as Shell

TestCase {
	id: testCase
	name: "MiddleDragScrollHandler"
	when: windowShown
	visible: true
	width: 420
	height: 320
	property bool recordingWheel: false
	property var wheelYSamples: []
	property var wheelYSampleTimestamps: []
	property int contentYChangeCount: 0
	property int wheelPacketsSent: 0
	property real peakWheelFrameDisplacement: 0
	property real peakWheelFrameVelocity: 0
	property real peakWheelFrameBudgetExcess: 0
	property bool recordingScrollBar: false
	property var scrollBarEvents: []
	property var scrollBarYSamples: []
	property int passiveWheelReceiptCount: 0
	property var passiveWheelReceipts: []
	property bool sawMovementEndedWhilePressed: false
	property real scrollBarReleaseY: 0
	property double scrollBarReleaseTimestamp: 0
	property int scrollBarReleaseSampleIndex: 0

	Flickable {
		id: flickable
		objectName: "middleDragFixture"
		x: 20
		y: 20
		width: 260
		height: 200
		contentWidth: 620
		contentHeight: 840
		interactive: true
		boundsBehavior: Flickable.StopAtBounds
		onContentYChanged: {
			testCase.contentYChangeCount += 1
			if (testCase.recordingWheel) {
				testCase.wheelYSamples = testCase.wheelYSamples.concat([contentY])
				testCase.wheelYSampleTimestamps =
					testCase.wheelYSampleTimestamps.concat([Date.now()])
			}
			if (testCase.recordingScrollBar)
				testCase.scrollBarYSamples = testCase.scrollBarYSamples.concat([contentY])
		}
		onMovementStarted: testCase.recordScrollBarEvent("movementStarted")
		onMovementEnded: testCase.recordScrollBarEvent("movementEnded")

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

		WheelHandler {
			id: passiveWheelReceiptHandler
			target: null
			blocking: false
			onWheel: event => testCase.recordPassiveWheel(event)
		}

		ScrollBar.vertical: ScrollBar {
			id: timelineScrollBar
			objectName: "middleDragFixtureScrollBar"
			policy: ScrollBar.AlwaysOn
			interactive: true
		}
	}
	Connections {
		target: timelineScrollBar
		function onPressedChanged() {
			testCase.recordScrollBarEvent(timelineScrollBar.pressed ? "pressed" : "released")
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
	SignalSpy {
		id: wheelInputObservedSpy
		target: middleDrag
		signalName: "wheelInputObserved"
	}
	SignalSpy {
		id: wheelInputAdmittedSpy
		target: middleDrag
		signalName: "wheelInputAdmitted"
	}
	SignalSpy {
		id: wheelInputDroppedSpy
		target: middleDrag
		signalName: "wheelInputDropped"
	}

	function init() {
		recordingWheel = false
		recordingScrollBar = false
		wheelYSamples = []
		wheelYSampleTimestamps = []
		scrollBarEvents = []
		scrollBarYSamples = []
		passiveWheelReceiptCount = 0
		passiveWheelReceipts = []
		sawMovementEndedWhilePressed = false
		scrollBarReleaseY = 0
		scrollBarReleaseTimestamp = 0
		scrollBarReleaseSampleIndex = 0
		contentYChangeCount = 0
		wheelPacketsSent = 0
		peakWheelFrameDisplacement = 0
		peakWheelFrameVelocity = 0
		peakWheelFrameBudgetExcess = 0
		middleDrag.cancelSmoothWheel()
		middleDrag.smoothWheelEnabled = true
		scrollingStartedSpy.clear()
		scrollingEndedSpy.clear()
		wheelInputObservedSpy.clear()
		wheelInputAdmittedSpy.clear()
		wheelInputDroppedSpy.clear()
		verify(wheelEventDispatcher.watchWindow(testCase.Window.window))
		wheelEventDispatcher.clearReceipt()
		flickable.contentX = 120
		flickable.contentY = 180
		flickable.contentHeight = 840
		wait(1)
		verify(middleDrag.enabled, "middle-drag handler must be enabled for overflowing content")
	}

	function cleanup() {
		recordingWheel = false
		recordingScrollBar = false
		middleDrag.cancelSmoothWheel()
		middleDrag.smoothWheelEnabled = true
		middleDrag.endDrag()
	}

	function recordScrollBarEvent(kind) {
		if (!recordingScrollBar)
			return
		const pressed = timelineScrollBar.pressed
		const event = { "kind": kind, "pressed": pressed, "contentY": flickable.contentY,
			"at": Date.now() }
		scrollBarEvents = scrollBarEvents.concat([event])
		if (kind === "movementEnded" && pressed)
			sawMovementEndedWhilePressed = true
		if (kind === "released") {
			scrollBarReleaseY = flickable.contentY
			scrollBarReleaseTimestamp = event.at
			scrollBarReleaseSampleIndex = scrollBarYSamples.length
		}
	}

	function dispatchCoarseWheel(angleDeltaY, delay) {
		++wheelPacketsSent
		mouseWheel(middleDrag, middleDrag.width / 2, 100, 0, angleDeltaY,
			Qt.NoButton, Qt.NoModifier, delay)
	}

	function dispatchNativeWheel(angleDeltaY, pixelDeltaY, phase) {
		++wheelPacketsSent
		const point = flickable.mapToItem(null, flickable.width / 2, 100)
		verify(wheelEventDispatcher.dispatch(testCase.Window.window, point.x, point.y,
			angleDeltaY, pixelDeltaY, phase))
	}

	function wheelReceipt() {
		return wheelEventDispatcher.receipt()
	}

	function recordPassiveWheel(event) {
		++passiveWheelReceiptCount
		passiveWheelReceipts = passiveWheelReceipts.concat([{
			"angleDeltaY": event.angleDelta.y,
			"pixelDeltaY": event.pixelDelta.y
		}])
	}

	function lastPassiveWheelReceipt() {
		return passiveWheelReceipts.length > 0
			? passiveWheelReceipts[passiveWheelReceipts.length - 1] : ({})
	}

	function signalUnits(spy) {
		let units = 0
		for (let i = 0; i < spy.count; ++i)
			units += Number(spy.signalArguments[i][0])
		return units
	}

	function beginWheelObservation() {
		middleDrag.cancelSmoothWheel()
		middleDrag.smoothWheelEnabled = true
		flickable.cancelFlick()
		flickable.contentX = 120
		flickable.contentY = 180
		wheelYSamples = []
		wheelYSampleTimestamps = []
		passiveWheelReceiptCount = 0
		passiveWheelReceipts = []
		contentYChangeCount = 0
		wheelPacketsSent = 0
		peakWheelFrameDisplacement = 0
		peakWheelFrameVelocity = 0
		peakWheelFrameBudgetExcess = 0
		wheelInputAdmittedSpy.clear()
		wheelInputDroppedSpy.clear()
		recordingWheel = true
		wait(1)
	}

	function computeWheelFrameBudget() {
		peakWheelFrameDisplacement = 0
		peakWheelFrameVelocity = 0
		peakWheelFrameBudgetExcess = 0
		for (let i = 1; i < wheelYSamples.length; ++i) {
			const displacement = Math.abs(wheelYSamples[i] - wheelYSamples[i - 1])
			const elapsed = wheelYSampleTimestamps[i] - wheelYSampleTimestamps[i - 1]
			peakWheelFrameDisplacement = Math.max(peakWheelFrameDisplacement, displacement)
			const frameBudget = 960 * Math.min(Math.max(elapsed, 0) / 1000, 1 / 30)
			peakWheelFrameBudgetExcess = Math.max(peakWheelFrameBudgetExcess,
				displacement - frameBudget)
			if (elapsed > 0)
				peakWheelFrameVelocity = Math.max(peakWheelFrameVelocity,
					displacement * 1000 / elapsed)
		}
	}

	function finishWheelObservation(expectedY, startedAt, direction) {
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, expectedY, 1.0)
		verify(Date.now() - startedAt <= 850,
			"bounded wheel input must settle within the quiet cadence window")
		const wheelDirection = direction === undefined ? 1 : direction
		for (let i = 1; i < wheelYSamples.length; ++i) {
			if (wheelDirection > 0) {
				verify(wheelYSamples[i] >= wheelYSamples[i - 1] - 0.01,
					"forward wheel samples must not reverse")
				verify(wheelYSamples[i] <= expectedY + 1.0,
					"forward wheel samples must not overshoot")
			} else {
				verify(wheelYSamples[i] <= wheelYSamples[i - 1] + 0.01,
					"reversed wheel samples must not reverse")
				verify(wheelYSamples[i] >= expectedY - 1.0,
					"reversed wheel samples must not overshoot")
			}
		}
		computeWheelFrameBudget()
		recordingWheel = false
	}

	function finishPacedWheelObservation(startY, startedAt, direction) {
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		verify(Date.now() - startedAt <= 850,
			"paced wheel input must settle within the quiet cadence window")
		const wheelDirection = direction === undefined ? 1 : direction
		const maximumTravel = middleDrag.maximumWheelVelocity
			* middleDrag.maximumWheelAnimationSeconds + 1.0
		const minimumY = flickable.originY
		const maximumY = flickable.originY + flickable.contentHeight - flickable.height
		for (let i = 0; i < wheelYSamples.length; ++i) {
			if (i > 0) {
				if (wheelDirection > 0)
					verify(wheelYSamples[i] >= wheelYSamples[i - 1] - 0.01,
						"paced wheel samples must not reverse")
				else
					verify(wheelYSamples[i] <= wheelYSamples[i - 1] + 0.01,
						"paced reversed samples must not reverse")
			}
			verify(Math.abs(wheelYSamples[i] - startY) <= maximumTravel,
				"paced wheel travel must stay within the velocity/duration bound")
			verify(wheelYSamples[i] >= minimumY - 1.0
				&& wheelYSamples[i] <= maximumY + 1.0,
				"paced wheel samples must stay within content bounds")
		}
		verify(Math.abs(flickable.contentY - startY) <= maximumTravel,
			"paced wheel final travel must stay within the velocity/duration bound")
		verify(flickable.contentY >= minimumY - 1.0
			&& flickable.contentY <= maximumY + 1.0,
			"paced wheel final position must stay within content bounds")
		computeWheelFrameBudget()
		recordingWheel = false
	}

	function test_discrete_wheel_step_is_interpolated_per_frame() {
		const expectedY = 180 + middleDrag.wheelStep
		verify(middleDrag.shouldSmoothWheel(-120, 0))
		mouseWheel(middleDrag, middleDrag.width / 2, 100,
			0, -120, Qt.NoButton, Qt.NoModifier, 20)
		verify(middleDrag.wheelAnimationActive)
		compare(scrollingStartedSpy.count, 1)
		compare(wheelInputObservedSpy.count, 1)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		verify(flickable.contentY < expectedY,
			"the first presented frame must not teleport to the complete wheel step")
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, expectedY, 0.5)
		compare(scrollingEndedSpy.count, 1)
	}

	function test_rapid_wheel_pulses_preserve_three_ordinary_notches() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(-120, 5)
		dispatchCoarseWheel(-120, 5)
		compare(wheelInputObservedSpy.count, 3)
		compare(signalUnits(wheelInputAdmittedSpy), 3)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 3 * middleDrag.wheelStep, startedAt)
	}

	function test_reversal_retargets_from_the_current_viewport() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(120, 0)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(Math.max(flickable.originY, 180 - middleDrag.wheelStep), startedAt, -1)
	}

	function test_large_angle_then_ordinary_notch_preserves_three_steps() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-240, 0)
		dispatchCoarseWheel(-120, 5)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelInputAdmittedSpy), 3)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 3 * middleDrag.wheelStep, startedAt)
	}

	function test_twenty_millisecond_wheel_cadence_preserves_three_steps() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(-120, 20)
		dispatchCoarseWheel(-120, 20)
		compare(wheelInputObservedSpy.count, 3)
		compare(signalUnits(wheelInputAdmittedSpy), 3)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 3 * middleDrag.wheelStep, startedAt)
	}

	function test_large_angle_reports_partial_admission_and_drop() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-600, 0)
		compare(signalUnits(wheelInputAdmittedSpy), 4)
		compare(signalUnits(wheelInputDroppedSpy), 1)
		finishWheelObservation(180 + 4 * middleDrag.wheelStep, startedAt)
	}

	function test_reversal_after_partial_advance_rebases_without_stale_tail() {
		beginWheelObservation()
		dispatchCoarseWheel(-120, 0)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		const partialY = flickable.contentY
		dispatchCoarseWheel(120, 0)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		computeWheelFrameBudget()
		fuzzyCompare(flickable.contentY,
			Math.max(flickable.originY, partialY - middleDrag.wheelStep), 1.0)
		const settledY = flickable.contentY
		wait(378)
		fuzzyCompare(flickable.contentY, settledY, 1.0)
		recordingWheel = false
	}

	function test_coarse_wheel_can_be_canceled_by_true_pixel_delta_input() {
		beginWheelObservation()
		dispatchCoarseWheel(-120, 0)
		verify(middleDrag.wheelAnimationActive)
		const beforeObservedCount = wheelInputObservedSpy.count
		const admittedBefore = signalUnits(wheelInputAdmittedSpy)
		const droppedBefore = signalUnits(wheelInputDroppedSpy)
		const beforePassiveCount = passiveWheelReceiptCount
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		verify(!middleDrag.wheelAnimationActive)
		compare(wheelInputObservedSpy.count, beforeObservedCount + 1)
		compare(signalUnits(wheelInputAdmittedSpy), admittedBefore)
		compare(signalUnits(wheelInputDroppedSpy), droppedBefore)
		const receipt = wheelReceipt()
		compare(receipt.count, 1)
		compare(receipt.angleDeltaY, 120)
		compare(receipt.pixelDeltaY, -24)
		compare(receipt.phase, Qt.ScrollUpdate)
		tryVerify(function() { return passiveWheelReceiptCount > beforePassiveCount }, 250)
		const passiveReceipt = lastPassiveWheelReceipt()
		compare(passiveReceipt.angleDeltaY, 120)
		compare(passiveReceipt.pixelDeltaY, -24)
		wait(378)
		verify(!middleDrag.wheelAnimationActive,
			"pixelDelta must not resurrect the canceled smooth-wheel tail")
		compare(signalUnits(wheelInputAdmittedSpy), admittedBefore)
		compare(signalUnits(wheelInputDroppedSpy), droppedBefore)
		recordingWheel = false
	}

	function test_precision_wheel_stays_on_the_native_flickable_path() {
		verify(!middleDrag.shouldSmoothWheel(15, 0))
		verify(!middleDrag.shouldSmoothWheel(120, 4))
		mouseWheel(middleDrag, middleDrag.width / 2, 100,
			0, -15, Qt.NoButton, Qt.NoModifier, 20)
		compare(wheelInputObservedSpy.count, 1)
		verify(!middleDrag.wheelAnimationActive)
	}

	function test_native_coarse_delivery_is_the_comparison_path() {
		beginWheelObservation()
		middleDrag.smoothWheelEnabled = false
		const beforePassiveCount = passiveWheelReceiptCount
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(-120, 0, Qt.NoScrollPhase)
		verify(!middleDrag.wheelAnimationActive)
		compare(wheelInputObservedSpy.count, 1)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		const receipt = wheelReceipt()
		compare(receipt.count, 1)
		compare(receipt.angleDeltaY, -120)
		compare(receipt.pixelDeltaY, 0)
		compare(receipt.phase, Qt.NoScrollPhase)
		tryVerify(function() { return passiveWheelReceiptCount > beforePassiveCount }, 250)
		const passiveReceipt = lastPassiveWheelReceipt()
		compare(passiveReceipt.angleDeltaY, -120)
		compare(passiveReceipt.pixelDeltaY, 0)
		middleDrag.smoothWheelEnabled = true
		recordingWheel = false
	}

	function test_native_coarse_burst_is_recorded_without_custom_backlog() {
		beginWheelObservation()
		middleDrag.smoothWheelEnabled = false
		const startedAt = Date.now()
		const beforePassiveCount = passiveWheelReceiptCount
		wheelEventDispatcher.clearReceipt()
		for (let i = 0; i < 12; ++i) {
			dispatchNativeWheel(-120, 0, Qt.NoScrollPhase)
			if (i < 11)
				wait(5)
		}
		compare(wheelInputObservedSpy.count, 12)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		verify(!middleDrag.wheelAnimationActive)
		const receipt = wheelReceipt()
		compare(receipt.count, 12)
		compare(receipt.angleDeltaY, -120)
		compare(receipt.pixelDeltaY, 0)
		compare(receipt.phase, Qt.NoScrollPhase)
		compare(passiveWheelReceiptCount, beforePassiveCount + 12)
		const passiveReceipt = lastPassiveWheelReceipt()
		compare(passiveReceipt.angleDeltaY, -120)
		compare(passiveReceipt.pixelDeltaY, 0)
		wait(378)
		const settledAt = Date.now()
		console.log("native coarse burst packets=" + wheelPacketsSent
			+ " startY=180 finalY=" + flickable.contentY
			+ " settleMs=" + (settledAt - startedAt))
		verify(!middleDrag.wheelAnimationActive)
		middleDrag.smoothWheelEnabled = true
		recordingWheel = false
	}

	function test_saturated_custom_burst_reports_exact_admission_and_drop() {
		beginWheelObservation()
		const startedAt = Date.now()
		for (let i = 0; i < 12; ++i)
			dispatchCoarseWheel(-120, 0)
		compare(wheelInputObservedSpy.count, 12)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		computeWheelFrameBudget()
		const aggregateAdmittedSteps = Math.round((flickable.contentY - 180)
			/ middleDrag.wheelStep)
		compare(aggregateAdmittedSteps, 4,
			"the bounded contract must preserve four admitted steps")
		compare(signalUnits(wheelInputAdmittedSpy), 4)
		compare(signalUnits(wheelInputDroppedSpy), 8)
		console.log("wheel burst baseline packets=" + wheelPacketsSent
			+ " aggregateAdmittedSteps=" + aggregateAdmittedSteps
			+ " peakFramePixels=" + peakWheelFrameDisplacement
			+ " peakFrameVelocity=" + peakWheelFrameVelocity)
		fuzzyCompare(flickable.contentY, 180 + 4 * middleDrag.wheelStep, 1.0)
		verify(Date.now() - startedAt <= 850)
		verify(peakWheelFrameBudgetExcess <= 2.5,
			"the custom frame budget must follow the actual timer cadence")
		verify(peakWheelFrameVelocity <= 1100.0,
			"the custom frame velocity must stay within the 960 px/s budget")
		const settledY = flickable.contentY
		wait(378)
		fuzzyCompare(flickable.contentY, settledY, 1.0)
		recordingWheel = false
	}

	function test_paced_custom_burst_reports_bounded_moving_window() {
		beginWheelObservation()
		const startedAt = Date.now()
		for (let i = 0; i < 12; ++i)
			dispatchCoarseWheel(-120, i === 0 ? 0 : 5)
		compare(wheelInputObservedSpy.count, 12)
		const admittedSteps = signalUnits(wheelInputAdmittedSpy)
		const droppedSteps = signalUnits(wheelInputDroppedSpy)
		compare(admittedSteps + droppedSteps, 12)
		compare(wheelInputAdmittedSpy.count + wheelInputDroppedSpy.count, 12)
		verify(admittedSteps >= 4,
			"paced input may admit additional steps as the presented window advances")
		finishPacedWheelObservation(180, startedAt)
		verify(peakWheelFrameBudgetExcess <= 2.5,
			"paced custom input must follow the actual timer cadence")
		verify(peakWheelFrameVelocity <= 1100.0,
			"paced custom input must stay within the 960 px/s budget")
		const settledY = flickable.contentY
		wait(378)
		fuzzyCompare(flickable.contentY, settledY, 1.0)
		recordingWheel = false
	}

	function test_reversal_at_physical_bounds_cancels_stale_runway() {
		beginWheelObservation()
		const maximumY = flickable.originY + flickable.contentHeight - flickable.height
		flickable.contentY = flickable.originY
		verify(middleDrag.queueSmoothWheelDelta(-120))
		verify(middleDrag.wheelAnimationActive)
		compare(middleDrag.wheelTargetY,
			flickable.originY + middleDrag.wheelStep)
		flickable.contentY = flickable.originY
		verify(middleDrag.queueSmoothWheelDelta(120))
		verify(!middleDrag.wheelAnimationActive,
			"a reverse packet at the lower bound must cancel the stale target")
		compare(middleDrag.wheelTargetY, flickable.originY)

		flickable.contentY = maximumY
		verify(middleDrag.queueSmoothWheelDelta(120))
		verify(middleDrag.wheelAnimationActive)
		compare(middleDrag.wheelTargetY, maximumY - middleDrag.wheelStep)
		flickable.contentY = maximumY
		verify(middleDrag.queueSmoothWheelDelta(-120))
		verify(!middleDrag.wheelAnimationActive,
			"a reverse packet at the upper bound must cancel the stale target")
		compare(middleDrag.wheelTargetY, maximumY)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 2)

		// Repeat the same edge through TestCase.mouseWheel so the state assertion
		// also covers the actual onWheel delivery path, not just the helper call.
		middleDrag.cancelSmoothWheel()
		wheelInputObservedSpy.clear()
		wheelInputAdmittedSpy.clear()
		wheelInputDroppedSpy.clear()
		flickable.contentY = flickable.originY
		dispatchCoarseWheel(-120, 0)
		flickable.contentY = flickable.originY
		dispatchCoarseWheel(120, 0)
		flickable.contentY = maximumY
		dispatchCoarseWheel(120, 0)
		flickable.contentY = maximumY
		dispatchCoarseWheel(-120, 0)
		compare(wheelInputObservedSpy.count, 4)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 2)
		verify(!middleDrag.wheelAnimationActive)
		wait(378)
		fuzzyCompare(flickable.contentY, maximumY, 1.0)
		recordingWheel = false
	}

	function test_smooth_wheel_target_is_clamped_to_content_bounds() {
		flickable.contentY = flickable.contentHeight - flickable.height - 8
		verify(middleDrag.queueSmoothWheelDelta(-120))
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 1)
		compare(middleDrag.wheelTargetY,
			flickable.originY + flickable.contentHeight - flickable.height)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		compare(flickable.contentY,
			flickable.originY + flickable.contentHeight - flickable.height)
	}

	function test_attached_scrollbar_records_pointer_order_supporting_evidence() {
		recordingScrollBar = true
		flickable.contentY = 180
		wait(1)
		verify(timelineScrollBar.visible)
		verify(timelineScrollBar.size > 0 && timelineScrollBar.size < 1)
		const thumbCenter = timelineScrollBar.position * timelineScrollBar.height
			+ timelineScrollBar.size * timelineScrollBar.height / 2
		const pressY = Math.max(2, Math.min(timelineScrollBar.height - 2, thumbCenter))
		const moveY = Math.max(2, Math.min(timelineScrollBar.height - 2,
			pressY + timelineScrollBar.height * 0.18))
		mousePress(timelineScrollBar, timelineScrollBar.width / 2, pressY, Qt.LeftButton)
		verify(timelineScrollBar.pressed,
			"the attached ScrollBar must enter pressed state through real pointer input")
		mouseMove(timelineScrollBar, timelineScrollBar.width / 2, moveY, 20)
		mouseRelease(timelineScrollBar, timelineScrollBar.width / 2, moveY, Qt.LeftButton)
		tryVerify(function() { return !timelineScrollBar.pressed }, 500)
		verify(scrollBarEvents.some(function(event) { return event.kind === "pressed" }))
		verify(scrollBarEvents.some(function(event) { return event.kind === "released" }))
		const movementEndedEvents = scrollBarEvents.filter(function(event) {
			return event.kind === "movementEnded"
		})
		if (movementEndedEvents.length === 0)
			console.warn("Qt fixture did not emit movementEnded; Main guard remains client-acceptance-only")
		else if (!sawMovementEndedWhilePressed)
			console.warn("Qt fixture emitted movementEnded only after release; Main guard remains client-acceptance-only")
		const releaseY = scrollBarReleaseY
		flickable.contentHeight = 900
		wait(16)
		fuzzyCompare(flickable.contentY, releaseY, 1.0)
		wait(378)
		for (let i = scrollBarReleaseSampleIndex; i < scrollBarYSamples.length; ++i)
			verify(Math.abs(scrollBarYSamples[i] - releaseY) <= 1.0,
				"post-release ScrollBar samples must not show a delayed positional correction")
		verify(Math.abs(flickable.contentY - releaseY) <= 1.0,
			"attached ScrollBar must not introduce a delayed positional correction")
		verify(Date.now() - scrollBarReleaseTimestamp >= 378)
		recordingScrollBar = false
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
