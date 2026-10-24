import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ScrollBar {
	id: control

	orientation: Qt.Vertical
	policy: ScrollBar.AsNeeded
	interactive: true
	hoverEnabled: true
	minimumSize: 0.06
	implicitWidth: 12
	padding: 3
	opacity: size < 1 || holdingThumb ? (active || hovered || pressed ? 1 : 0.58) : 0
	// Eases the thumb to its new place and length, for jumps the reader did not
	// make (a different conversation loading). Scrolling itself stays direct.
	property bool animateThumb: false
	// Emptied while the next content loads: a thumb that was showing stays
	// where it was and eases from there once there is something to scroll again.
	property bool thumbShownAtAnimateStart: false
	readonly property bool holdingThumb: animateThumb && thumbShownAtAnimateStart && size >= 1
	onAnimateThumbChanged: thumbShownAtAnimateStart = animateThumb && size < 1

	function syncThumb() {
		if (holdingThumb)
			return
		thumb.y = contentItem.y
		thumb.height = contentItem.height
	}
	onHoldingThumbChanged: syncThumb()
	Component.onCompleted: syncThumb()
	Connections {
		target: control.contentItem
		function onYChanged() { control.syncThumb() }
		function onHeightChanged() { control.syncThumb() }
	}

	Behavior on opacity {
		NumberAnimation { duration: Theme.motionFast }
	}

	// The control places contentItem from C++, which a Behavior cannot see, so
	// the visible thumb follows it through syncThumb() instead.
	background: Item {
		implicitWidth: 12

		Rectangle {
			id: thumb
			readonly property bool animated: control.animateThumb && !control.pressed
			x: control.contentItem.x
			width: control.contentItem.width
			radius: width / 2
			color: control.pressed ? Theme.accent
				: control.hovered ? Theme.accentHover : Theme.textMuted

			Behavior on y {
				enabled: thumb.animated
				NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic }
			}
			Behavior on height {
				enabled: thumb.animated
				NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic }
			}
			Behavior on color {
				ColorAnimation { duration: Theme.motionFast }
			}
		}
	}

	contentItem: Item {
		implicitWidth: control.pressed ? 7 : control.hovered ? 6 : 4
		implicitHeight: 28

		Behavior on implicitWidth {
			NumberAnimation { duration: Theme.motionFast }
		}
	}
}
