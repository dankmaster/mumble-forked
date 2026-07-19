import QtQuick

// Desktop-style grab scrolling for long product surfaces. This transparent
// overlay accepts only the middle mouse button, so normal selection, links,
// drag/drop and context menus keep their existing pointer semantics.
MouseArea {
	id: handler
	required property Flickable targetFlickable
	property bool horizontalEnabled: true
	property bool verticalEnabled: true
	property real previousX: 0
	property real previousY: 0

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
		targetFlickable.cancelFlick()
		previousX = x
		previousY = y
	}

	function dragTo(x, y) {
		if (horizontalEnabled)
			targetFlickable.contentX = clampedContentX(
				targetFlickable.contentX - (x - previousX))
		if (verticalEnabled)
			targetFlickable.contentY = clampedContentY(
				targetFlickable.contentY - (y - previousY))
		previousX = x
		previousY = y
	}

	onPressed: event => {
		beginDrag(event.x, event.y)
	}

	onPositionChanged: event => {
		if (!pressed || !(pressedButtons & Qt.MiddleButton))
			return
		dragTo(event.x, event.y)
	}

	onWheel: event => {
		// The underlying Flickable keeps native wheel/touchpad kinetics.
		event.accepted = false
	}
}
