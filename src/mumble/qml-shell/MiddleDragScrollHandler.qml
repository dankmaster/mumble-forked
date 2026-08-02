import QtQuick

// Desktop-style autoscrolling for long product surfaces. Holding the middle
// mouse button establishes a stationary origin; moving away from it scrolls in
// that direction and increases the speed with distance. This transparent
// overlay accepts only the middle mouse button, so normal selection, links,
// drag/drop and context menus keep their existing pointer semantics.
MouseArea {
	id: handler
	required property Flickable targetFlickable
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
	// Traditional mouse wheels arrive as coarse 120-unit angle steps. Let product
	// surfaces opt into a frame-synchronised interpolation for those steps so a
	// virtualized list does not have to construct and present a new row in the
	// same instant. Pixel-based touchpads remain on Flickable's native path.
	property bool smoothWheelEnabled: false
	property real wheelStep: 96
	property real wheelResponse: 28
	// Keep a short, bounded input runway so a burst cannot turn into a long
	// delayed scroll. A reversal is retargeted from the viewport immediately.
	readonly property int maximumQueuedWheelSteps: 4
	readonly property real maximumWheelVelocity: 960
	readonly property real maximumWheelAnimationSeconds: 0.65
	readonly property real maximumWheelBufferedDistance: Math.min(
		Math.abs(wheelStep) * maximumQueuedWheelSteps,
		maximumWheelVelocity * maximumWheelAnimationSeconds)
	property real wheelTargetY: 0
	property bool wheelAnimationActive: false
	property double wheelAnimationStartedAt: 0
	signal scrollingStarted()
	signal scrollingEnded()
	signal wheelInputObserved()
	signal wheelInputAdmitted(int notchUnits)
	signal wheelInputDropped(int notchUnits)

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
		if (gestureActive)
			endDrag()
		targetFlickable.cancelFlick()
		originX = x
		originY = y
		pointerX = x
		pointerY = y
		lastTickTimestamp = Date.now()
		gestureActive = true
		// A middle drag takes ownership without briefly ending the active scrolling
		// state between the wheel animation and the pointer-driven continuation.
		cancelSmoothWheel()
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
		if (!smoothWheelEnabled || !verticalEnabled)
			return false
		const pixels = Math.abs(Number(pixelDeltaY) || 0)
		const angle = Math.abs(Number(angleDeltaY) || 0)
		// High-resolution wheels and precision touchpads already provide fine-grained
		// deltas. Only replace the coarse, conventional mouse-wheel step.
		return pixels < 0.01 && angle >= 120
	}

	function queueSmoothWheelDelta(angleDeltaY) {
		if (!smoothWheelEnabled || !verticalEnabled)
			return false
		const angle = Number(angleDeltaY) || 0
		if (Math.abs(angle) < 0.01)
			return false
		targetFlickable.cancelFlick()
		const currentY = targetFlickable.contentY
		const requestedOffset = -(angle / 120) * wheelStep
		const requestedSteps = Math.max(1, Math.round(Math.abs(angle) / 120))
		const stepMagnitude = Math.max(0.01, Math.abs(Number(wheelStep) || 0))
		const maximumDistance = Math.max(0.01, maximumWheelBufferedDistance)
		const direction = requestedOffset < 0 ? -1 : 1
		let baseY = currentY
		let sameDirection = false
		if (wheelAnimationActive) {
			wheelTargetY = clampedContentY(wheelTargetY)
			const activeOffset = wheelTargetY - currentY
			sameDirection = Math.abs(activeOffset) > 0.01
				&& (activeOffset < 0) === (requestedOffset < 0)
			// Same-direction notches extend the existing runway. Opposite-direction
			// input starts from the current viewport instead of walking back through
			// stale backlog.
			if (sameDirection)
				baseY = wheelTargetY
		}
		const requestedTargetY = clampedContentY(baseY + requestedOffset)
		const maximumTargetY = clampedContentY(currentY + direction * maximumDistance)
		let nextTargetY = requestedTargetY
		if (direction < 0)
			nextTargetY = Math.max(nextTargetY, maximumTargetY)
		else
			nextTargetY = Math.min(nextTargetY, maximumTargetY)
		nextTargetY = clampedContentY(nextTargetY)
		const admittedDistance = Math.abs(nextTargetY - baseY)
		const admittedSteps = Math.min(requestedSteps,
			Math.max(0, Math.floor(admittedDistance / stepMagnitude + 0.01)))
		const droppedSteps = Math.max(0, requestedSteps - admittedSteps)
		if (admittedSteps > 0)
			wheelInputAdmitted(admittedSteps)
		if (droppedSteps > 0)
			wheelInputDropped(droppedSteps)
		// A deliberate cap/bound drop is still consumed by this handler. Letting it
		// fall through would make Flickable apply the same coarse packet natively.
		if (Math.abs(nextTargetY - baseY) <= 0.01) {
			// An opposite packet at a physical bound must also cancel the old target.
			// Otherwise the consumed packet leaves a stale runway active and the next
			// frame resumes motion in the direction the user just rejected.
			if (wheelAnimationActive && !sameDirection)
				finishSmoothWheel()
			return true
		}
		if (!wheelAnimationActive || !sameDirection)
			wheelAnimationStartedAt = Date.now()
		wheelTargetY = nextTargetY
		wheelAnimationActive = true
		beginScrollingActivity()
		return true
	}

	function finishSmoothWheel() {
		if (!wheelAnimationActive)
			return
		wheelAnimationActive = false
		wheelTargetY = clampedContentY(targetFlickable.contentY)
		wheelAnimationStartedAt = 0
		endScrollingActivityIfIdle()
	}

	function cancelSmoothWheel() {
		finishSmoothWheel()
	}

	function advanceSmoothWheel(elapsedSeconds) {
		if (!wheelAnimationActive)
			return false
		wheelTargetY = clampedContentY(wheelTargetY)
		const currentY = targetFlickable.contentY
		const distance = wheelTargetY - currentY
		if (Math.abs(distance) <= 0.35) {
			targetFlickable.contentY = wheelTargetY
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
		const response = Math.max(1, Number(wheelResponse) || 1)
		const progress = 1 - Math.exp(-response * frameSeconds)
		const requestedDisplacement = Math.abs(distance) * progress
		const maximumFrameDisplacement = maximumWheelVelocity * frameSeconds
		const frameDisplacement = Math.min(requestedDisplacement, maximumFrameDisplacement)
		const nextY = clampedContentY(currentY
			+ (distance < 0 ? -frameDisplacement : frameDisplacement))
		targetFlickable.contentY = nextY
		if (Math.abs(wheelTargetY - targetFlickable.contentY) <= 0.35) {
			targetFlickable.contentY = wheelTargetY
			finishSmoothWheel()
		} else if (wheelAnimationStartedAt > 0
				&& (Date.now() - wheelAnimationStartedAt) / 1000
				>= maximumWheelAnimationSeconds) {
			const remainingDistance = Math.abs(wheelTargetY - targetFlickable.contentY)
			const finalFrameBudget = Math.max(0,
				maximumFrameDisplacement - frameDisplacement) + 0.35
			// Expiry may only close a legal residual. If a stalled renderer left more
			// distance than one frame can cover, finish at the already-presented value
			// and clear the target rather than teleporting beyond the velocity budget.
			if (remainingDistance <= finalFrameBudget)
				targetFlickable.contentY = wheelTargetY
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
		running: handler.enabled && handler.wheelAnimationActive
		onTriggered: handler.advanceSmoothWheel(frameTime)
	}

	onWheel: event => {
		const angleY = event.angleDelta ? event.angleDelta.y : 0
		const pixelY = event.pixelDelta ? event.pixelDelta.y : 0
		// Owners need to classify native wheel/touchpad movement before this
		// handler deliberately passes precision deltas through to Flickable.
		if (Math.abs(angleY) > 0.01 || Math.abs(pixelY) > 0.01)
			wheelInputObserved()
		if (shouldSmoothWheel(angleY, pixelY)) {
			event.accepted = queueSmoothWheelDelta(angleY)
			return
		}
		// The underlying Flickable keeps native precision-wheel/touchpad kinetics.
		cancelSmoothWheel()
		event.accepted = false
	}

	onEnabledChanged: if (!enabled) cancelSmoothWheel()
}
