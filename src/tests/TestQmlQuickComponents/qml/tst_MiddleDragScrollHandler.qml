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
	property bool sawMovementEndedDuringNavigationTransition: false
	property bool simulateScrollbarReleaseTakeover: false
	property bool simulatedScrollbarReleasePending: false
	property int simulatedScrollbarRecoveryCount: 0
	property int flickableMovementEndedCount: 0
	property real scrollBarReleaseY: 0
	// Simulates a list that moves its own view once while the wheel scrolls (a row
	// above the view changing height).
	property real layoutShiftAtY: -1
	property real layoutShift: 0
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
		contentHeight: 4000
		interactive: true
		boundsBehavior: Flickable.StopAtBounds
		onContentYChanged: {
			testCase.contentYChangeCount += 1
			if (testCase.layoutShiftAtY >= 0 && contentY > testCase.layoutShiftAtY) {
				testCase.layoutShiftAtY = -1
				contentY -= testCase.layoutShift
			}
			if (testCase.recordingWheel) {
				testCase.wheelYSamples = testCase.wheelYSamples.concat([contentY])
				testCase.wheelYSampleTimestamps =
					testCase.wheelYSampleTimestamps.concat([Date.now()])
			}
			if (testCase.recordingScrollBar)
				testCase.scrollBarYSamples = testCase.scrollBarYSamples.concat([contentY])
		}
		onMovementStarted: testCase.recordScrollBarEvent("movementStarted")
		onMovementEnded: {
			++testCase.flickableMovementEndedCount
			if (middleDrag.navigationTransitionActive)
				testCase.sawMovementEndedDuringNavigationTransition = true
			testCase.recordScrollBarEvent("movementEnded")
		}

		Rectangle {
			width: flickable.contentWidth
			height: flickable.contentHeight
			color: "#202030"
		}

		Shell.MiddleDragScrollHandler {
			id: middleDrag
			targetFlickable: flickable
			smoothWheelEnabled: true
			onWheelInputObserved: {
				if (testCase.simulateScrollbarReleaseTakeover
						&& testCase.simulatedScrollbarReleasePending) {
					testCase.simulatedScrollbarReleasePending = false
					navigationEnabled = true
					simulatedScrollbarReleaseGuardTimer.restart()
				}
			}
			onScrollingStarted: if (testCase.simulateScrollbarReleaseTakeover)
				simulatedScrollbarReleaseGuardTimer.stop()
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
	Timer {
		id: simulatedScrollbarReleaseGuardTimer
		interval: 128
		repeat: false
		onTriggered: ++testCase.simulatedScrollbarRecoveryCount
	}
	SignalSpy {
		id: wheelAngleUnitsObservedSpy
		target: middleDrag
		signalName: "wheelAngleUnitsObserved"
	}
	SignalSpy {
		id: wheelInputDroppedSpy
		target: middleDrag
		signalName: "wheelInputDropped"
	}
	SignalSpy {
		id: wheelInputExpiredSpy
		target: middleDrag
		signalName: "wheelInputExpired"
	}
	SignalSpy {
		id: wheelInputBlockedSpy
		target: middleDrag
		signalName: "wheelInputBlocked"
	}
	SignalSpy {
		id: wheelInputPassedThroughSpy
		target: middleDrag
		signalName: "wheelInputPassedThrough"
	}

	function init() {
		layoutShiftAtY = -1
		recordingWheel = false
		recordingScrollBar = false
		wheelYSamples = []
		wheelYSampleTimestamps = []
		scrollBarEvents = []
		scrollBarYSamples = []
		passiveWheelReceiptCount = 0
		passiveWheelReceipts = []
		sawMovementEndedWhilePressed = false
		sawMovementEndedDuringNavigationTransition = false
		simulateScrollbarReleaseTakeover = false
		simulatedScrollbarReleasePending = false
		simulatedScrollbarRecoveryCount = 0
		simulatedScrollbarReleaseGuardTimer.stop()
		flickableMovementEndedCount = 0
		scrollBarReleaseY = 0
		scrollBarReleaseTimestamp = 0
		scrollBarReleaseSampleIndex = 0
		contentYChangeCount = 0
		wheelPacketsSent = 0
		peakWheelFrameDisplacement = 0
		peakWheelFrameVelocity = 0
		peakWheelFrameBudgetExcess = 0
		middleDrag.cancelSmoothWheel()
		middleDrag.navigationEnabled = true
		middleDrag.smoothWheelEnabled = true
		middleDrag.wheelStep = 96
		flickable.interactive = true
		scrollingStartedSpy.clear()
		scrollingEndedSpy.clear()
		wheelInputObservedSpy.clear()
		wheelAngleUnitsObservedSpy.clear()
		wheelInputAdmittedSpy.clear()
		wheelInputDroppedSpy.clear()
		wheelInputExpiredSpy.clear()
		wheelInputBlockedSpy.clear()
		wheelInputPassedThroughSpy.clear()
		middleDrag.wheelExpiredDistance = 0
		verify(wheelEventDispatcher.watchWindow(testCase.Window.window))
		wheelEventDispatcher.clearReceipt()
		flickable.contentX = 120
		flickable.contentY = 180
		flickable.contentHeight = 4000
		wait(1)
		verify(middleDrag.enabled, "middle-drag handler must be enabled for overflowing content")
	}

	function cleanup() {
		recordingWheel = false
		recordingScrollBar = false
		simulateScrollbarReleaseTakeover = false
		simulatedScrollbarReleasePending = false
		simulatedScrollbarRecoveryCount = 0
		simulatedScrollbarReleaseGuardTimer.stop()
		middleDrag.cancelSmoothWheel()
		middleDrag.navigationEnabled = true
		middleDrag.smoothWheelEnabled = true
		middleDrag.wheelStep = 96
		flickable.interactive = true
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
		middleDrag.navigationEnabled = true
		middleDrag.smoothWheelEnabled = true
		flickable.interactive = true
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
		wheelInputExpiredSpy.clear()
		wheelInputBlockedSpy.clear()
		wheelInputPassedThroughSpy.clear()
		middleDrag.wheelExpiredDistance = 0
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
			const frameBudget = Math.min(
				middleDrag.wheelStep * middleDrag.maximumWheelFrameUnits,
				middleDrag.maximumWheelVelocity
					* Math.min(Math.max(elapsed, 0) / 1000, 1 / 30))
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
		verify(Date.now() - startedAt <= 600,
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
		verify(Date.now() - startedAt <= 600,
			"paced wheel input must settle within the quiet cadence window")
		const wheelDirection = direction === undefined ? 1 : direction
		const maximumTravel = middleDrag.maximumWheelBufferedDistance + 1.0
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

	function test_discrete_wheel_step_completes_quickly_without_overshoot() {
		const expectedY = 180 + middleDrag.wheelStep
		verify(middleDrag.shouldSmoothWheel(-120, 0))
		mouseWheel(middleDrag, middleDrag.width / 2, 100,
			0, -120, Qt.NoButton, Qt.NoModifier, 20)
		verify(middleDrag.wheelAnimationActive)
		compare(scrollingStartedSpy.count, 1)
		compare(wheelInputObservedSpy.count, 1)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		verify(flickable.contentY <= expectedY + 0.5,
			"the first presented frame must not overshoot the wheel step")
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, expectedY, 0.5)
		compare(scrollingEndedSpy.count, 1)
	}

	function test_layout_moving_the_view_does_not_cut_the_wheel_step_short() {
		// Three notches at once; the list pulls the view back 30 px during the
		// first frame, and the rest of the three steps still plays.
		layoutShift = 30
		layoutShiftAtY = 181
		mouseWheel(middleDrag, middleDrag.width / 2, 100,
			0, -360, Qt.NoButton, Qt.NoModifier, 20)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		layoutShiftAtY = -1
		fuzzyCompare(flickable.contentY, 180 + middleDrag.wheelStep * 3 - 30, 1)
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
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(120, 0)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, 180 - middleDrag.wheelStep, 0.5)
		wait(378)
		fuzzyCompare(flickable.contentY, 180 - middleDrag.wheelStep, 0.5)
		recordingWheel = false
	}

	function test_three_notch_backlog_reverses_immediately_from_presented_viewport() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(-120, 0)
		dispatchCoarseWheel(-120, 0)
		wheelYSamples = []
		wheelYSampleTimestamps = []
		dispatchCoarseWheel(120, 0)
		compare(wheelInputObservedSpy.count, 4)
		compare(signalUnits(wheelInputAdmittedSpy), 4)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 - middleDrag.wheelStep, startedAt, -1)
	}

	function signalSignedUnits(spy) {
		let units = 0
		for (let i = 0; i < spy.count; ++i)
			units += Number(spy.signalArguments[i][1])
		return units
	}

	function test_large_angle_then_ordinary_notch_preserves_three_steps() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-240, 0)
		dispatchCoarseWheel(-120, 5)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelAngleUnitsObservedSpy), 3)
		compare(signalSignedUnits(wheelAngleUnitsObservedSpy), 3)
		compare(signalUnits(wheelInputAdmittedSpy), 3)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 3 * middleDrag.wheelStep, startedAt)
	}

	function test_aggregated_three_unit_event_matches_three_packets() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-360, 0)
		compare(wheelInputObservedSpy.count, 1)
		compare(signalUnits(wheelAngleUnitsObservedSpy), 3)
		compare(signalSignedUnits(wheelAngleUnitsObservedSpy), 3)
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
		compare(signalUnits(wheelInputAdmittedSpy), 5)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 5 * middleDrag.wheelStep, startedAt)
	}

	function test_reversal_after_partial_advance_rebases_without_stale_tail() {
		beginWheelObservation()
		dispatchCoarseWheel(-120, 0)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		const reversalStartY = flickable.contentY
		wheelYSamples = [reversalStartY]
		wheelYSampleTimestamps = [Date.now()]
		dispatchCoarseWheel(120, 0)
		compare(wheelInputObservedSpy.count, 2)
		compare(signalUnits(wheelInputAdmittedSpy), 2)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		computeWheelFrameBudget()
		fuzzyCompare(flickable.contentY, reversalStartY - middleDrag.wheelStep, 1.0)
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
		compare(receipt.angleDeltaYTotal, 120)
		compare(receipt.pixelDeltaYTotal, -24)
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
		verify(!middleDrag.shouldSmoothWheel(180, 0))
		verify(!middleDrag.shouldSmoothWheel(120, 4))
		beginWheelObservation()
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(-15, 0, Qt.NoScrollPhase)
		dispatchNativeWheel(-180, 0, Qt.NoScrollPhase)
		compare(wheelInputObservedSpy.count, 2)
		compare(wheelInputPassedThroughSpy.count, 2)
		compare(wheelInputBlockedSpy.count, 0)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		const receipt = wheelReceipt()
		compare(receipt.count, 2)
		compare(receipt.angleDeltaYTotal, -195)
		compare(receipt.pixelDeltaYTotal, 0)
		tryVerify(function() { return passiveWheelReceiptCount >= 2 }, 250)
		verify(!middleDrag.wheelAnimationActive)
		recordingWheel = false
	}

	function test_navigation_ownership_cancels_and_consumes_coarse_wheel() {
		beginWheelObservation()
		dispatchCoarseWheel(-120, 0)
		verify(middleDrag.wheelAnimationActive)
		const yAtOwnershipTransfer = flickable.contentY
		const observedBefore = wheelInputObservedSpy.count
		const admittedBefore = signalUnits(wheelInputAdmittedSpy)
		middleDrag.navigationEnabled = false
		verify(!middleDrag.navigationAvailable)
		verify(!middleDrag.wheelAnimationActive)
		verify(!middleDrag.scrolling)
		compare(scrollingEndedSpy.count, 0)
		fuzzyCompare(middleDrag.wheelPendingDistance, 0, 0.01)
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(-120, 0, Qt.NoScrollPhase)
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		compare(wheelInputObservedSpy.count, observedBefore + 2)
		compare(wheelInputBlockedSpy.count, 2)
		compare(signalUnits(wheelInputBlockedSpy), 2)
		compare(signalSignedUnits(wheelInputBlockedSpy), 0)
		compare(wheelInputBlockedSpy.signalArguments[0][2], false)
		compare(wheelInputBlockedSpy.signalArguments[1][2], true)
		compare(wheelInputPassedThroughSpy.count, 0)
		compare(signalUnits(wheelInputAdmittedSpy), admittedBefore)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		const receipt = wheelReceipt()
		compare(receipt.count, 2)
		compare(receipt.angleDeltaY, 120)
		compare(receipt.pixelDeltaY, -24)
		compare(receipt.angleDeltaYTotal, 0)
		compare(receipt.pixelDeltaYTotal, -24)
		compare(receipt.phase, Qt.ScrollUpdate)
		compare(wheelInputExpiredSpy.count, 0)
		compare(scrollingStartedSpy.count, 1)
		wait(450)
		fuzzyCompare(flickable.contentY, yAtOwnershipTransfer, 0.5)
		verify(!middleDrag.wheelAnimationActive)
		compare(scrollingEndedSpy.count, 0)
		middleDrag.navigationEnabled = true
		verify(middleDrag.navigationAvailable)
		dispatchCoarseWheel(-120, 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY,
			yAtOwnershipTransfer + middleDrag.wheelStep, 1.0)
		recordingWheel = false
	}

	function test_scrollbar_release_guard_hands_first_coarse_packet_to_navigation() {
		beginWheelObservation()
		simulateScrollbarReleaseTakeover = true
		simulatedScrollbarReleasePending = true
		middleDrag.navigationEnabled = false
		verify(!middleDrag.navigationAvailable)
		dispatchCoarseWheel(-120, 0)
		verify(!simulatedScrollbarReleasePending)
		verify(middleDrag.navigationAvailable)
		compare(wheelInputObservedSpy.count, 1)
		compare(wheelInputBlockedSpy.count, 0)
		compare(signalUnits(wheelInputAdmittedSpy), 1)
		compare(wheelInputPassedThroughSpy.count, 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY, 180 + middleDrag.wheelStep, 1.0)
		compare(simulatedScrollbarRecoveryCount, 0)
		recordingWheel = false
	}

	function test_scrollbar_release_guard_hands_first_precision_packet_to_native() {
		beginWheelObservation()
		simulateScrollbarReleaseTakeover = true
		simulatedScrollbarReleasePending = true
		middleDrag.navigationEnabled = false
		verify(!middleDrag.navigationAvailable)
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		verify(!simulatedScrollbarReleasePending)
		verify(middleDrag.navigationAvailable)
		compare(wheelInputObservedSpy.count, 1)
		compare(wheelInputBlockedSpy.count, 0)
		compare(wheelInputPassedThroughSpy.count, 1)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		const receipt = wheelReceipt()
		compare(receipt.count, 1)
		compare(receipt.angleDeltaY, 120)
		compare(receipt.pixelDeltaY, -24)
		compare(receipt.phase, Qt.ScrollUpdate)
		verify(!middleDrag.wheelAnimationActive)
		recordingWheel = false
	}

	function test_scrollbar_release_precision_no_motion_keeps_recovery_wakeup() {
		beginWheelObservation()
		flickable.contentY = flickable.originY
			+ flickable.contentHeight - flickable.height
		const boundY = flickable.contentY
		simulateScrollbarReleaseTakeover = true
		simulatedScrollbarReleasePending = true
		middleDrag.navigationEnabled = false
		verify(!middleDrag.navigationAvailable)
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(-120, -24, Qt.ScrollUpdate)
		verify(!simulatedScrollbarReleasePending)
		verify(middleDrag.navigationAvailable)
		compare(wheelInputObservedSpy.count, 1)
		compare(wheelInputBlockedSpy.count, 0)
		compare(wheelInputPassedThroughSpy.count, 1)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		verify(!middleDrag.wheelAnimationActive)
		wait(180)
		compare(simulatedScrollbarRecoveryCount, 1)
		fuzzyCompare(flickable.contentY, boundY, 0.5)
		recordingWheel = false
	}

	function test_noninteractive_target_cancels_active_wheel_without_stale_tail() {
		beginWheelObservation()
		dispatchCoarseWheel(-240, 0)
		verify(middleDrag.wheelAnimationActive)
		tryVerify(function() { return flickable.contentY > 180 }, 500)
		const yAtScopeTransfer = flickable.contentY
		const observedBefore = wheelInputObservedSpy.count
		const admittedBefore = signalUnits(wheelInputAdmittedSpy)
		const scrollingEndedBefore = scrollingEndedSpy.count
		flickable.interactive = false
		verify(!middleDrag.navigationAvailable)
		verify(!middleDrag.wheelAnimationActive)
		verify(!middleDrag.scrolling)
		compare(scrollingEndedSpy.count, scrollingEndedBefore)
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(120, 0, Qt.NoScrollPhase)
		compare(wheelInputObservedSpy.count, observedBefore + 1)
		compare(wheelInputBlockedSpy.count, 1)
		compare(signalUnits(wheelInputBlockedSpy), 1)
		compare(signalSignedUnits(wheelInputBlockedSpy), -1)
		compare(wheelInputBlockedSpy.signalArguments[0][2], false)
		compare(signalUnits(wheelInputAdmittedSpy), admittedBefore)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		wait(450)
		fuzzyCompare(flickable.contentY, yAtScopeTransfer, 0.5)
		verify(!middleDrag.wheelAnimationActive)
		recordingWheel = false
	}

	function test_ownership_loss_cancels_native_flick_motion() {
		beginWheelObservation()
		middleDrag.smoothWheelEnabled = false
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		compare(wheelInputObservedSpy.count, 1)
		compare(wheelInputPassedThroughSpy.count, 1)
		const receipt = wheelReceipt()
		compare(receipt.count, 1)
		compare(receipt.pixelDeltaY, -24)
		// Offscreen native wheel physics is platform-dependent. Start the same
		// underlying Flickable kinetic owner deterministically after proving the
		// production pixel route handed the packet through.
		flickable.flick(0, -2400)
		tryVerify(function() { return flickable.flicking || flickable.moving }, 250)
		middleDrag.navigationEnabled = false
		verify(!middleDrag.navigationAvailable)
		tryVerify(function() { return !flickable.flicking && !flickable.moving }, 250)
		const yAfterOwnershipTransfer = flickable.contentY
		wait(450)
		fuzzyCompare(flickable.contentY, yAfterOwnershipTransfer, 0.5)
		verify(!middleDrag.wheelAnimationActive)
		verify(!middleDrag.scrolling)
		compare(scrollingEndedSpy.count, 0)
		middleDrag.navigationEnabled = true
		middleDrag.smoothWheelEnabled = true
		dispatchCoarseWheel(-120, 0)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY,
			yAfterOwnershipTransfer + middleDrag.wheelStep, 1.0)
		recordingWheel = false
	}

	function test_middle_drag_consumes_wheel_without_a_second_writer() {
		beginWheelObservation()
		mousePress(testCase, flickable.x + 120, flickable.y + 100, Qt.MiddleButton)
		verify(middleDrag.pressed)
		verify(middleDrag.gestureActive)
		verify(middleDrag.scrolling)
		mouseMove(testCase, flickable.x + 120, flickable.y + 145, 10)
		tryVerify(function() { return middleDrag.scrolling }, 250)
		const admittedBefore = signalUnits(wheelInputAdmittedSpy)
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(-120, 0, Qt.NoScrollPhase)
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		verify(middleDrag.gestureActive)
		compare(wheelInputBlockedSpy.count, 2)
		compare(signalUnits(wheelInputBlockedSpy), 2)
		compare(signalSignedUnits(wheelInputBlockedSpy), 0)
		compare(wheelInputPassedThroughSpy.count, 0)
		compare(signalUnits(wheelInputAdmittedSpy), admittedBefore)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		compare(wheelReceipt().count, 2)
		verify(!middleDrag.wheelAnimationActive)
		mouseRelease(testCase, flickable.x + 120, flickable.y + 145, Qt.MiddleButton)
		tryVerify(function() { return !middleDrag.scrolling }, 250)
		recordingWheel = false
	}

	function test_native_pixel_to_coarse_handoff_cancels_old_flick_writer() {
		beginWheelObservation()
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(120, -24, Qt.ScrollUpdate)
		compare(wheelInputPassedThroughSpy.count, 1)
		compare(wheelReceipt().count, 1)
		flickable.flick(0, -2400)
		tryVerify(function() { return flickable.flicking || flickable.moving }, 250)
		dispatchCoarseWheel(-120, 0)
		verify(middleDrag.wheelAnimationActive)
		verify(sawMovementEndedDuringNavigationTransition,
			"native movementEnded must observe the published coarse-wheel handoff")
		tryVerify(function() { return !flickable.flicking }, 250)
		const customStartY = flickable.contentY
		const endedBeforePhase = scrollingEndedSpy.count
		wheelEventDispatcher.clearReceipt()
		dispatchNativeWheel(0, 0, Qt.ScrollEnd)
		const phaseReceipt = wheelReceipt()
		verify(phaseReceipt.count >= 1)
		compare(phaseReceipt.angleDeltaY, 0)
		compare(phaseReceipt.pixelDeltaY, 0)
		compare(phaseReceipt.angleDeltaYTotal, 0)
		compare(phaseReceipt.pixelDeltaYTotal, 0)
		verify(phaseReceipt.scrollEndCount >= 1)
		compare(phaseReceipt.phase, Qt.ScrollEnd)
		verify(middleDrag.wheelAnimationActive,
			"a delayed native ScrollEnd must not cancel the newer coarse runway")
		compare(scrollingEndedSpy.count, endedBeforePhase)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		fuzzyCompare(flickable.contentY,
			customStartY + middleDrag.wheelStep, 1.0)
		const settledY = flickable.contentY
		wait(450)
		fuzzyCompare(flickable.contentY, settledY, 0.5)
		compare(wheelInputPassedThroughSpy.count, 1)
		compare(signalUnits(wheelInputAdmittedSpy), 1)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		recordingWheel = false
	}

	function test_native_flick_to_middle_drag_publishes_transition_owner_before_cancel() {
		beginWheelObservation()
		flickable.flick(0, -2400)
		tryVerify(function() { return flickable.flicking || flickable.moving }, 250)
		mousePress(testCase, flickable.x + 120, flickable.y + 100, Qt.MiddleButton)
		verify(middleDrag.pressed)
		verify(middleDrag.gestureActive)
		verify(sawMovementEndedDuringNavigationTransition,
			"native movementEnded must observe the published middle-drag handoff")
		verify(!middleDrag.navigationTransitionActive)
		tryVerify(function() { return !flickable.flicking }, 250)
		mouseRelease(testCase, flickable.x + 120, flickable.y + 100, Qt.MiddleButton)
		verify(!middleDrag.gestureActive)
		verify(!middleDrag.scrolling)
		compare(scrollingStartedSpy.count, 1)
		compare(scrollingEndedSpy.count, 1)
		recordingWheel = false
	}

	function test_native_flick_to_bound_noop_has_one_balanced_custom_settlement() {
		beginWheelObservation()
		const maximumY = flickable.originY + flickable.contentHeight - flickable.height
		flickable.contentY = maximumY
		flickable.flick(0, 2400)
		verify(flickable.flicking || flickable.moving,
			"the fixture must retain a native owner at the physical bound")
		fuzzyCompare(flickable.contentY, maximumY, 0.5)
		const nativeEndedBefore = flickableMovementEndedCount
		sawMovementEndedDuringNavigationTransition = false
		dispatchCoarseWheel(-120, 0)
		verify(!middleDrag.wheelAnimationActive)
		verify(!middleDrag.scrolling)
		compare(signalUnits(wheelInputAdmittedSpy), 0)
		compare(signalUnits(wheelInputDroppedSpy), 1)
		verify(sawMovementEndedDuringNavigationTransition)
		verify(flickableMovementEndedCount > nativeEndedBefore)
		compare(scrollingStartedSpy.count, 1)
		compare(scrollingEndedSpy.count, 1)
		fuzzyCompare(flickable.contentY, maximumY, 0.5)
		wait(378)
		fuzzyCompare(flickable.contentY, maximumY, 0.5)
		recordingWheel = false
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
		compare(receipt.angleDeltaYTotal, -120)
		compare(receipt.pixelDeltaYTotal, 0)
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
		compare(receipt.angleDeltaYTotal, -1440)
		compare(receipt.pixelDeltaYTotal, 0)
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

	function test_twelve_unit_custom_burst_preserves_every_received_notch() {
		beginWheelObservation()
		const startedAt = Date.now()
		for (let i = 0; i < 12; ++i)
			dispatchCoarseWheel(-120, 0)
		compare(wheelInputObservedSpy.count, 12)
		tryVerify(function() { return !middleDrag.wheelAnimationActive }, 1000)
		computeWheelFrameBudget()
		const aggregateAdmittedSteps = Math.round((flickable.contentY - 180)
			/ middleDrag.wheelStep)
		compare(aggregateAdmittedSteps, 12,
			"the bounded contract must preserve twelve received steps")
		compare(signalUnits(wheelInputAdmittedSpy), 12)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		console.log("wheel burst baseline packets=" + wheelPacketsSent
			+ " aggregateAdmittedSteps=" + aggregateAdmittedSteps
			+ " peakFramePixels=" + peakWheelFrameDisplacement
			+ " peakFrameVelocity=" + peakWheelFrameVelocity)
		fuzzyCompare(flickable.contentY, 180 + 12 * middleDrag.wheelStep, 1.0)
		verify(Date.now() - startedAt <= 600)
		verify(peakWheelFrameBudgetExcess <= middleDrag.wheelStep * 0.15,
			"the custom frame budget must tolerate only timestamp quantization")
		verify(peakWheelFrameVelocity <= middleDrag.maximumWheelVelocity * 1.15 + 50,
			"the custom frame velocity must stay within the linear drain budget")
		const settledY = flickable.contentY
		wait(378)
		fuzzyCompare(flickable.contentY, settledY, 1.0)
		recordingWheel = false
	}

	function test_six_aggregated_events_preserve_twelve_received_units() {
		beginWheelObservation()
		const startedAt = Date.now()
		for (let i = 0; i < 6; ++i)
			dispatchCoarseWheel(-240, 0)
		compare(wheelInputObservedSpy.count, 6)
		compare(signalUnits(wheelAngleUnitsObservedSpy), 12)
		compare(signalUnits(wheelInputAdmittedSpy), 12)
		compare(signalUnits(wheelInputDroppedSpy), 0)
		finishWheelObservation(180 + 12 * middleDrag.wheelStep, startedAt)
	}

	function test_thirteenth_aggregated_unit_is_consumed_at_the_cap() {
		beginWheelObservation()
		const startedAt = Date.now()
		dispatchCoarseWheel(-1560, 0)
		compare(wheelInputObservedSpy.count, 1)
		compare(signalUnits(wheelAngleUnitsObservedSpy), 13)
		compare(signalUnits(wheelInputAdmittedSpy), 12)
		compare(signalUnits(wheelInputDroppedSpy), 1)
		finishWheelObservation(180 + 12 * middleDrag.wheelStep, startedAt)
	}

	function test_extreme_aggregated_packet_has_bounded_synchronous_work() {
		beginWheelObservation()
		const requestedUnits = 1000000
		const startedAt = Date.now()
		verify(middleDrag.queueSmoothWheelDelta(-120 * requestedUnits))
		const queueElapsed = Date.now() - startedAt
		verify(queueElapsed <= 50,
			"raw angle magnitude must not create an unbounded QML loop")
		compare(signalUnits(wheelInputAdmittedSpy),
			middleDrag.maximumBufferedWheelUnits)
		compare(signalUnits(wheelInputDroppedSpy),
			requestedUnits - middleDrag.maximumBufferedWheelUnits)
		finishWheelObservation(180
			+ middleDrag.maximumBufferedWheelUnits * middleDrag.wheelStep, startedAt)
	}

	function test_active_pending_is_normalized_before_new_packet_accounting() {
		beginWheelObservation()
		middleDrag.wheelStep = 112
		verify(middleDrag.queueSmoothWheelDelta(-1440))
		compare(signalUnits(wheelInputAdmittedSpy), 12)
		fuzzyCompare(middleDrag.wheelPendingDistance, 12 * 112, 0.5)
		middleDrag.wheelStep = 80
		verify(!middleDrag.advanceSmoothWheel(0))
		compare(wheelInputExpiredSpy.count, 1)
		fuzzyCompare(middleDrag.wheelPendingDistance, 12 * 80, 0.5)
		verify(middleDrag.wheelExpiredDistance >= 12 * 32 - 0.5)
		verify(middleDrag.queueSmoothWheelDelta(-120))
		compare(signalUnits(wheelInputAdmittedSpy), 12,
			"runway shrink must not admit target travel opposite the new packet")
		compare(signalUnits(wheelInputDroppedSpy), 1)
		compare(wheelInputExpiredSpy.count, 1)
		fuzzyCompare(middleDrag.wheelPendingDistance, 12 * 80, 0.5)

		middleDrag.cancelSmoothWheel()
		wheelInputAdmittedSpy.clear()
		wheelInputDroppedSpy.clear()
		wheelInputExpiredSpy.clear()
		middleDrag.wheelExpiredDistance = 0
		middleDrag.wheelStep = 96
		flickable.contentHeight = 4000
		flickable.contentY = 180
		verify(middleDrag.queueSmoothWheelDelta(-1440))
		compare(signalUnits(wheelInputAdmittedSpy), 12)
		flickable.contentHeight = 700
		verify(!middleDrag.advanceSmoothWheel(0))
		compare(wheelInputExpiredSpy.count, 1)
		fuzzyCompare(middleDrag.wheelPendingDistance,
			flickable.originY + flickable.contentHeight - flickable.height
				- flickable.contentY, 0.5)
		verify(middleDrag.queueSmoothWheelDelta(-120))
		compare(signalUnits(wheelInputAdmittedSpy), 12,
			"range shrink must not admit a same-direction packet while retracting stale pending")
		compare(signalUnits(wheelInputDroppedSpy), 1)
		compare(wheelInputExpiredSpy.count, 1)
		fuzzyCompare(middleDrag.wheelTargetY,
			flickable.originY + flickable.contentHeight - flickable.height, 0.5)
		middleDrag.cancelSmoothWheel()
		recordingWheel = false
	}

	function test_expired_backlog_never_snaps_beyond_one_frame() {
		beginWheelObservation()
		dispatchCoarseWheel(-1440, 0)
		const beforeAdvanceY = flickable.contentY
		middleDrag.wheelLastInputAt = Date.now() - 500
		middleDrag.advanceSmoothWheel(1 / 60)
		const maximumFrameTravel = Math.min(
			middleDrag.wheelStep * middleDrag.maximumWheelFrameUnits,
			middleDrag.maximumWheelVelocity / 60)
		verify(Math.abs(flickable.contentY - beforeAdvanceY) <= maximumFrameTravel + 0.5)
		verify(!middleDrag.wheelAnimationActive)
		compare(wheelInputExpiredSpy.count, 1)
		verify(middleDrag.wheelExpiredDistance > 0)
		const expiredY = flickable.contentY
		wait(378)
		fuzzyCompare(flickable.contentY, expiredY, 0.5)
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
		compare(admittedSteps, 12)
		compare(droppedSteps, 0)
		finishWheelObservation(180 + 12 * middleDrag.wheelStep, startedAt)
		verify(peakWheelFrameBudgetExcess <= middleDrag.wheelStep * 0.15,
			"paced custom input must tolerate only timestamp quantization")
		verify(peakWheelFrameVelocity <= middleDrag.maximumWheelVelocity * 1.15 + 50,
			"paced custom input must stay within the linear drain budget")
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
		flickable.contentHeight += 60
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
		compare(scrollingStartedSpy.count, 1)
		middleDrag.endDrag()
		compare(scrollingEndedSpy.count, 1)
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
