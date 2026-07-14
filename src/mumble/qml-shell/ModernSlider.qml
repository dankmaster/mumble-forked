import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Slider {
	id: control
	Accessible.role: Accessible.Slider
	implicitHeight: Theme.controlHeight

	background: Rectangle {
		x: control.leftPadding
		y: Math.round(control.topPadding + control.availableHeight / 2 - height / 2)
		implicitWidth: 180
		implicitHeight: 5
		width: control.availableWidth
		height: implicitHeight
		radius: height / 2
		color: control.enabled ? Theme.surfaceRaised : Theme.panel
		Rectangle {
			width: control.visualPosition * parent.width
			height: parent.height
			radius: parent.radius
			color: control.enabled ? Theme.accent : Theme.textMuted
		}
	}

	handle: Rectangle {
		x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
		y: control.topPadding + control.availableHeight / 2 - height / 2
		implicitWidth: control.pressed ? 18 : 16
		implicitHeight: implicitWidth
		radius: width / 2
		color: control.enabled ? Theme.accent : Theme.textMuted
		border.color: !control.enabled ? Theme.divider
			: control.activeFocus ? Theme.focus : Theme.accentHover
		border.width: control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on implicitWidth { NumberAnimation { duration: Theme.motionFast } }
	}
}
