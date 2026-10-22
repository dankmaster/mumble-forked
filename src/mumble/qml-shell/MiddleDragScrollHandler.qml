import QtQuick

// Desktop-style autoscrolling for long product surfaces. Holding the middle
// mouse button establishes a stationary origin; moving away from it scrolls in
// that direction and increases the speed with distance. This transparent
// overlay accepts only the middle mouse button, so normal selection, links,
// drag/drop and context menus keep their existing pointer semantics.
MouseArea {
	id: handler
	required property Flickable targetFlickable
	// The surface owner can temporarily reserve contentY for a scope transaction,
	// an attached scrollbar gesture, or a deterministic performance workload.
	// Keep the overlay present so blocked wheel packets are consumed instead of
	// leaking to Flickable, while all pointer-driven writes remain disabled.
	property bool navigationEnabled: true
	readonly property bool navigationAvailable: navigationEnabled
		&& targetFlickable !== null && targetFlickable.interactive
	property bool horizontalEnabled: true
	property bool verticalEnabled: true
	property real deadZone: 10
	property real speedPerPixel: 10
	property real maximumSpeed: 2600
	property real originX: 0
	property real originY: 0
	property real pointerX: 0
	property real pointerY: 0
	property double lastTickTimestamp: 0
	property bool gestureActive: false
	property bool scrolling: false
	// cancelFlick() may synchronously emit Flickable.movementEnded. Publish this
	// narrow handoff token before canceling the native writer so the surface owner
	// cannot settle tail/anchor state between the old and new input owners.
	property bool navigationTransitionActive: false
	// Traditional mouse wheels arrive as coarse 120-unit angle steps. Let product
	// surfaces opt into a frame-synchronised interpolation for those steps so a
	// virtualized list does not have to construct and present a new row in the
	// same instant. Pixel-based touchpads remain on Flickable's native path.
	property bool smoothWheelEnabled: false
	property real wheelStep: 96
	// Preserve every conventional wheel notch the app actually receives, while
	// bounding the outstanding distance to a short physical gesture. The linear
	// drain is intentionally fast: smoothing must not make the wheel feel slower.
	readonly property int maximumBufferedWheelUnits: 12
	readonly property int maximumQueuedWheelSteps: maximumBufferedWheelUnits
	readonly property real maximumWheelDrainUnitsPerSecond: 50
	readonly property real maximumWheelFrameUnits: 1.5
	readonly property real maximumWheelVelocity: Math.abs(wheelStep)
		* maximumWheelDrainUnitsPerSecond
	readonly property real maximumWheelAnimationSeconds: 0.4
	readonly property real maximumWheelBufferedDistance: Math.abs(wheelStep)
		* maximumBufferedWheelUnits
	property real wheelPendingDistance: 0
	property real wheelTargetY: 0
	property bool wheelAnimationActive: false
	property double wheelLastInputAt: 0
	property real wheelExpiredDistance: 0
	signal scrollingStarted()
	signal scrollingEnded()
	signal wheelInputObserved()
	signal wheelAngleUnitsObserved(int notchUnits, int signedNotchUnits)
	signal wheelInputAdmitted(int notchUnits)
	signal wheelInputDropped(int notchUnits)
	signal wheelInputExpired(real distance)
	signal wheelInputBlocked(int notchUnits, int signedNotchUnits, bool precision)
	signal wheelInputPassedThrough()

	anchors.fill: parent
	acceptedButtons: Qt.MiddleButton
	preventStealing: true
	propagateComposedEvents: true
	hoverEnabled: false
	// Hidden parents do not participate in hit testing, and clamping makes the
	// no-overflow case a no-op. Keeping the handler enabled avoids stale binding
	// decisions while ListView/ScrollView content sizes settle asynchronously.
	enabled: targetFlickable !== null

	function clampedContentX(value) {
		const minimum = targetFlickable.originX
		const maximum = minimum + Math.max(0,
			targetFlickable.contentWidth - targetFlickable.width)
		return Math.max(minimum, Math.min(maximum, value))
	}

	function clampedContentY(value) {
		const minimum = targetFlickable.originY
		const maximum = minimum + Math.max(0,
			targetFlickable.contentHeight - targetFlickable.height)
		return Math.max(minimum, Math.min(maximum, value))
	}

	function beginDrag(x, y) {
		if (!navigationAvailable)
			return false
		if (gestureActive)
			endDrag()
		originX = x
		originY = y
		pointerX = x
		pointerY = y
		lastTickTimestamp = Date.now()
		gestureActive = true
		beginScrollingActivity()
		navigationTransitionActive = true
		targetFlickable.cancelFlick()
		// A middle drag takes ownership without briefly ending the active scrolling
		// state between the wheel animation and the pointer-driven continuation.
		cancelSmoothWheel()
		navigationTransitionActive = false
		return true
	}

	function dragTo(x, y) {
		pointerX = x
		pointerY = y
	}

	function velocityForOffset(offset, axisEnabled) {
		const magnitude = Math.abs(offset)
		if (!axisEnabled || magnitude <= deadZone)
			return 0
		const direction = offset < 0 ? -1 : 1
		return direction * Math.min(maximumSpeed,
			(magnitude - deadZone) * speedPerPixel)
	}

	function beginScrollingActivity() {
		if (scrolling)
			return
		scrolling = true
		scrollingStarted()
	}

	function endScrollingActivityIfIdle() {
		if (!scrolling || gestureActive || wheelAnimationActive)
			return
		scrolling = false
		scrollingEnded()
	}

	function advanceScroll(elapsedMilliseconds) {
		if (!navigationAvailable) {
			cancelNavigationForOwnershipLoss()
			return false
		}
		if (!gestureActive)
			return false
		// Avoid a large jump when rendering or the process was temporarily stalled.
		const elapsedSeconds = Math.max(0,
			Math.min(50, Number(elapsedMilliseconds) || 0)) / 1000
		const horizontalVelocity = velocityForOffset(pointerX - originX, horizontalEnabled)
		const verticalVelocity = velocityForOffset(pointerY - originY, verticalEnabled)
		const previousContentX = targetFlickable.contentX
		const previousContentY = targetFlickable.contentY
		if (horizontalVelocity !== 0)
			targetFlickable.contentX = clampedContentX(
				targetFlickable.contentX + horizontalVelocity * elapsedSeconds)
		if (verticalVelocity !== 0)
			targetFlickable.contentY = clampedContentY(
				targetFlickable.contentY + verticalVelocity * elapsedSeconds)
		const moved = Math.abs(targetFlickable.contentX - previousContentX) > 0.01
			|| Math.abs(targetFlickable.contentY - previousContentY) > 0.01
		if (moved)
			beginScrollingActivity()
		return moved
	}

	function shouldSmoothWheel(angleDeltaY, pixelDeltaY) {
		if (!navigationAvailable || !smoothWheelEnabled || !verticalEnabled)
			return false
		const pixels = Math.abs(Number(pixelDeltaY) || 0)
		const angle = Math.abs(Number(angleDeltaY) || 0)
		// High-resolution wheels and precision touchpads already provide fine-grained
		// deltas. Only replace the coarse, conventional mouse-wheel step.
		const units = angle / 120
		return pixels < 0.01 && angle >= 120
			&& Math.abs(units - Math.round(units)) <= 0.001
	}

	function wheelPendingRange(currentY, stepMagnitude) {
		return {
			"maximumDistance": Math.max(stepMagnitude, maximumWheelBufferedDistance),
			"minimumPending": targetFlickable.originY - currentY,
			"maximumPending": clampedContentY(Number.POSITIVE_INFINITY) - currentY
		}
	}

	function normalizeWheelPendingDistance(pending, range) {
		return Math.max(range.minimumPending, Math.min(range.maximumPending,
			Math.max(-range.maximumDistance, Math.min(range.maximumDistance, pending))))
	}

	function accountWheelPendingNormalization(previousPending, normalizedPending) {
		const expiredDistance = Math.abs(previousPending - normalizedPending)
		if (expiredDistance <= 0.35)
			return
		wheelExpiredDistance += expiredDistance
		wheelInputExpired(expiredDistance)
	}

	function queueSmoothWheelDelta(angleDeltaY) {
		if (!navigationAvailable || !smoothWheelEnabled || !verticalEnabled)
			return false
		const angle = Number(angleDeltaY) || 0
		const requestedUnits = Math.round(Math.abs(angle) / 120)
		if (requestedUnits <= 0
				|| Math.abs(Math.abs(angle) / 120 - requestedUnits) > 0.001)
			return false
		const currentY = targetFlickable.contentY
		const stepMagnitude = Math.max(0.01, Math.abs(Number(wheelStep) || 0))
		const direction = angle < 0 ? 1 : -1
		const pendingRange = wheelPendingRange(currentY, stepMagnitude)
		const previousPending = wheelAnimationActive ? wheelPendingDistance : 0
		let pending = normalizeWheelPendingDistance(previousPending, pendingRange)
		accountWheelPendingNormalization(previousPending, pending)
		// A direction reversal is a new gesture, not a request to drain the old
		// runway first. Rebase at the presented viewport so no stale-direction tail
		// survives behind the opposite packet.
		if (Math.abs(pending) > 0.35 && direction * pending < 0)
			pending = 0
		let admittedUnits = 0
		let droppedUnits = 0
		// A signed transition can cross at most the full negative-to-positive
		// runway. Bound synchronous work to that state space, then account every
		// remaining unit as one aggregate drop instead of looping on raw input size.
		const maximumTransitionUnits = Math.max(1,
			maximumBufferedWheelUnits * 2 + 2)
		const transitionUnits = Math.min(requestedUnits, maximumTransitionUnits)
		for (let unit = 0; unit < transitionUnits; ++unit) {
			const candidate = Math.max(-pendingRange.maximumDistance,
				Math.min(pendingRange.maximumDistance,
				pending + direction * stepMagnitude))
			const boundedCandidate = Math.max(pendingRange.minimumPending,
				Math.min(pendingRange.maximumPending, candidate))
			const admittedDistance = direction * (boundedCandidate - pending)
			pending = boundedCandidate
			if (admittedDistance >= stepMagnitude - 0.5)
				++admittedUnits
			else
				++droppedUnits
		}
		droppedUnits += requestedUnits - transitionUnits
		if (admittedUnits > 0)
			wheelInputAdmitted(admittedUnits)
		if (droppedUnits > 0)
			wheelInputDropped(droppedUnits)
		wheelPendingDistance = pending
		wheelTargetY = clampedContentY(currentY + wheelPendingDistance)
		wheelLastInputAt = Date.now()
		// Capacity and physical-bound drops are consumed here. Falling through would
		// let Flickable apply the same coarse packet a second time.
		if (Math.abs(wheelPendingDistance) <= 0.35) {
			// Even a consumed physical-bound no-op owns this input turn. Publish a
			// balanced start/end pair so synchronous native movementEnded is ignored,
			// then settle exactly once through scrollingEnded below.
			navigationTransitionActive = true
			wheelAnimationActive = true
			beginScrollingActivity()
			targetFlickable.cancelFlick()
			navigationTransitionActive = false
			finishSmoothWheel()
			return true
		}
		navigationTransitionActive = true
		wheelAnimationActive = true
		beginScrollingActivity()
		targetFlickable.cancelFlick()
		navigationTransitionActive = false
		return true
	}

	function finishSmoothWheel() {
		const wasActive = wheelAnimationActive
		wheelAnimationActive = false
		wheelPendingDistance = 0
		wheelTargetY = targetFlickable !== null
			? clampedContentY(targetFlickable.contentY) : 0
		wheelLastInputAt = 0
		if (wasActive)
			endScrollingActivityIfIdle()
	}

	function cancelSmoothWheel() {
		finishSmoothWheel()
	}

	function cancelNavigationForOwnershipLoss() {
		// Another owner is about to write contentY. Do not emit scrollingEnded:
		// ordinary wheel settlement may classify tail intent or capture an anchor,
		// neither of which belongs inside a scope/thumb/performance handoff.
		gestureActive = false
		wheelAnimationActive = false
		wheelPendingDistance = 0
		wheelTargetY = targetFlickable !== null
			? clampedContentY(targetFlickable.contentY) : 0
		wheelLastInputAt = 0
		scrolling = false
		// Pixel wheels and non-discrete angle packets deliberately use Flickable's
		// native kinetic path. Ownership transitions that have already published
		// their guard must stop that writer as well as the custom runway.
		if (targetFlickable !== null) {
			navigationTransitionActive = true
			targetFlickable.cancelFlick()
			navigationTransitionActive = false
		}
	}

	function advanceSmoothWheel(elapsedSeconds) {
		if (!wheelAnimationActive)
			return false
		if (!navigationAvailable) {
			cancelNavigationForOwnershipLoss()
			return false
		}
		const currentY = targetFlickable.contentY
		const stepMagnitude = Math.max(0.01, Math.abs(Number(wheelStep) || 0))
		const pendingRange = wheelPendingRange(currentY, stepMagnitude)
		const previousPending = wheelPendingDistance
		wheelPendingDistance = normalizeWheelPendingDistance(previousPending, pendingRange)
		accountWheelPendingNormalization(previousPending, wheelPendingDistance)
		wheelTargetY = clampedContentY(currentY + wheelPendingDistance)
		if (Math.abs(wheelPendingDistance) <= 0.35) {
			finishSmoothWheel()
			return false
		}
		// FrameAnimation supplies real elapsed time, so the curve feels the same on
		// 60/120/144 Hz displays. Cap delayed frames to avoid a catch-up teleport if
		// a rich delegate or the process briefly stalls.
		const frameSeconds = Math.max(0,
			Math.min(1 / 30, Number(elapsedSeconds) || 0))
		if (frameSeconds <= 0)
			return false
		const maximumFrameDisplacement = Math.min(
			stepMagnitude * maximumWheelFrameUnits,
			maximumWheelVelocity * frameSeconds)
		const frameDisplacement = Math.min(Math.abs(wheelPendingDistance),
			maximumFrameDisplacement)
		const nextY = clampedContentY(currentY
			+ (wheelPendingDistance < 0 ? -frameDisplacement : frameDisplacement))
		targetFlickable.contentY = nextY
		// Count the step this handler took, not where the view ended up: a list
		// may move its own view in the same call (a row above the view changing
		// height), and that is layout, not the end of the reader's step.
		const appliedDisplacement = nextY - currentY
		wheelPendingDistance -= appliedDisplacement
		wheelTargetY = clampedContentY(targetFlickable.contentY + wheelPendingDistance)
		if (Math.abs(appliedDisplacement) + 0.01 < frameDisplacement) {
			// Geometry changed or a physical bound was reached after admission. Never
			// leave an unreachable target running behind the user's next gesture.
			finishSmoothWheel()
		} else if (Math.abs(wheelPendingDistance) <= 0.35) {
			finishSmoothWheel()
		} else if (wheelLastInputAt > 0
				&& (Date.now() - wheelLastInputAt) / 1000
				>= maximumWheelAnimationSeconds) {
			const expiredDistance = Math.abs(wheelPendingDistance)
			wheelExpiredDistance += expiredDistance
			wheelInputExpired(expiredDistance)
			finishSmoothWheel()
		}
		return Math.abs(targetFlickable.contentY - currentY) > 0.01
	}

	function endDrag() {
		if (!gestureActive)
			return
		gestureActive = false
		endScrollingActivityIfIdle()
	}

	onPressed: event => {
		const point = handler.mapToItem(targetFlickable, event.x, event.y)
		beginDrag(point.x, point.y)
	}

	onPositionChanged: event => {
		if (!pressed || !(pressedButtons & Qt.MiddleButton))
			return
		// The handler is normally parented to Flickable.contentItem. Map back to
		// the stationary viewport so content movement cannot amplify the offset.
		const point = handler.mapToItem(targetFlickable, event.x, event.y)
		dragTo(point.x, point.y)
	}
	onReleased: endDrag()
	onCanceled: endDrag()

	Timer {
		interval: 16
		repeat: true
		running: handler.pressed && handler.gestureActive
		onTriggered: {
			const now = Date.now()
			handler.advanceScroll(now - handler.lastTickTimestamp)
			handler.lastTickTimestamp = now
		}
	}

	FrameAnimation {
		id: smoothWheelFrame
		running: handler.enabled && handler.navigationAvailable
			&& handler.wheelAnimationActive
		onTriggered: handler.advanceSmoothWheel(frameTime)
	}

	onWheel: event => {
		const angleY = event.angleDelta ? event.angleDelta.y : 0
		const pixelY = event.pixelDelta ? event.pixelDelta.y : 0
		const hasWheelInput = Math.abs(angleY) > 0.01 || Math.abs(pixelY) > 0.01
		const observedNotchUnits = Math.floor(Math.abs(angleY) / 120 + 0.0001)
		const observedSignedNotchUnits = angleY < 0
			? observedNotchUnits : -observedNotchUnits
		// Owners need to classify native wheel/touchpad movement before this
		// handler deliberately passes precision deltas through to Flickable.
		if (hasWheelInput) {
			wheelInputObserved()
			if (observedNotchUnits > 0)
				wheelAngleUnitsObserved(observedNotchUnits,
					observedSignedNotchUnits)
		}
		if (!navigationAvailable) {
			cancelNavigationForOwnershipLoss()
			event.accepted = hasWheelInput
			if (hasWheelInput)
				wheelInputBlocked(observedNotchUnits,
					observedSignedNotchUnits, Math.abs(pixelY) > 0.01)
			return
		}
		if (gestureActive) {
			// The held middle-button gesture already owns contentY. Consume wheel
			// packets instead of starting a second custom or native writer.
			event.accepted = hasWheelInput
			if (hasWheelInput)
				wheelInputBlocked(observedNotchUnits,
					observedSignedNotchUnits, Math.abs(pixelY) > 0.01)
			return
		}
		if (shouldSmoothWheel(angleY, pixelY)) {
			event.accepted = queueSmoothWheelDelta(angleY)
			return
		}
		// Phase-only native lifecycle packets (notably a delayed ScrollEnd) must not
		// cancel a newer custom coarse-wheel runway. Only a packet with actual delta
		// hands motion back to Flickable and cancels custom work.
		if (hasWheelInput)
			cancelSmoothWheel()
		event.accepted = false
		if (hasWheelInput)
			wheelInputPassedThrough()
	}

	onEnabledChanged: if (!enabled) cancelNavigationForOwnershipLoss()
	onNavigationAvailableChanged: if (!navigationAvailable) {
		cancelNavigationForOwnershipLoss()
	}
}
