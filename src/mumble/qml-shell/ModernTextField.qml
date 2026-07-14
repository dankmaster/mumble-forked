import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

TextField {
	id: control
	property bool invalid: false
	property bool dense: false

	Accessible.role: Accessible.EditableText
	hoverEnabled: true
	implicitHeight: dense ? Math.max(30, Theme.controlHeight - 4) : Theme.controlHeight
	leftPadding: Theme.space3
	rightPadding: Theme.space3
	topPadding: Theme.space2
	bottomPadding: Theme.space2
	font.pixelSize: Theme.fontBody
	color: control.enabled ? Theme.textStrong : Theme.textMuted
	placeholderTextColor: Theme.textMuted
	selectionColor: Theme.accentSubtle
	selectedTextColor: Theme.textStrong

	background: Rectangle {
		radius: Theme.innerRadius
		color: control.enabled ? Theme.surfaceRaised : Theme.panel
		border.color: control.invalid ? Theme.danger
			: control.activeFocus ? Theme.focus
			: control.hovered ? Theme.surfaceBorder : Theme.divider
		border.width: control.invalid || control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}
}
