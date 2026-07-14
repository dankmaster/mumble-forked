import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

TextArea {
	id: control
	property bool invalid: false

	Accessible.role: Accessible.EditableText
	hoverEnabled: true
	leftPadding: Theme.space3
	rightPadding: Theme.space3
	topPadding: Theme.space3
	bottomPadding: Theme.space3
	font.pixelSize: Theme.fontBody
	color: control.enabled ? Theme.textStrong : Theme.textMuted
	placeholderTextColor: Theme.textMuted
	selectionColor: Theme.accentSubtle
	selectedTextColor: Theme.textStrong
	wrapMode: TextEdit.Wrap

	background: Rectangle {
		radius: Theme.innerRadius
		color: control.enabled ? Theme.surfaceRaised : Theme.panel
		border.color: control.invalid ? Theme.danger
			: control.activeFocus ? Theme.focus
			: control.hovered ? Theme.surfaceBorder : Theme.divider
		border.width: control.invalid || control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}
}
