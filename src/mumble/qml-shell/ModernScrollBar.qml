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
	opacity: size < 1 ? (active || hovered || pressed ? 1 : 0.58) : 0

	Behavior on opacity {
		NumberAnimation { duration: Theme.motionFast }
	}

	background: Item {
		implicitWidth: 12
	}

	contentItem: Rectangle {
		implicitWidth: control.pressed ? 7 : control.hovered ? 6 : 4
		implicitHeight: 28
		radius: width / 2
		color: control.pressed ? Theme.accent
			: control.hovered ? Theme.accentHover : Theme.textMuted

		Behavior on implicitWidth {
			NumberAnimation { duration: Theme.motionFast }
		}
		Behavior on color {
			ColorAnimation { duration: Theme.motionFast }
		}
	}
}
