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
	signal scrollingStarted()
	signal scrollingEnded()

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
		if (moved && !scrolling) {
			scrolling = true
			scrollingStarted()
		}
		return moved
	}

	function endDrag() {
		if (!gestureActive)
			return
		gestureActive = false
		if (scrolling) {
			scrolling = false
			scrollingEnded()
		}
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

	onWheel: event => {
		// The underlying Flickable keeps native wheel/touchpad kinetics.
		event.accepted = false
	}
}
