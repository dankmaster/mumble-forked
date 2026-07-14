import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

SpinBox {
	id: control
	property bool invalid: false
	property bool dense: false

	Accessible.role: Accessible.SpinBox
	implicitHeight: dense ? Math.max(30, Theme.controlHeight - 4) : Theme.controlHeight
	leftPadding: Theme.space3
	rightPadding: 34
	font.pixelSize: Theme.fontBody

	contentItem: TextInput {
		z: 2
		text: control.displayText
		font: control.font
		color: control.enabled ? Theme.textStrong : Theme.textMuted
		selectionColor: Theme.accentSubtle
		selectedTextColor: Theme.textStrong
		horizontalAlignment: Qt.AlignLeft
		verticalAlignment: Qt.AlignVCenter
		readOnly: !control.editable
		validator: control.validator
		inputMethodHints: Qt.ImhFormattedNumbersOnly
		selectByMouse: true
		onEditingFinished: control.value = control.valueFromText(text, control.locale)
	}

	up.indicator: Rectangle {
		x: control.width - width
		y: 1
		implicitWidth: 32
		height: Math.floor((control.height - 2) / 2)
		color: !control.enabled ? "transparent"
			: control.up.pressed ? Theme.accentSubtle : control.up.hovered ? Theme.surfaceHover : "transparent"
		Text { anchors.centerIn: parent; text: "+"; color: control.enabled ? Theme.textMain : Theme.textMuted; font.pixelSize: 12 }
	}

	down.indicator: Rectangle {
		x: control.width - width
		y: Math.ceil(control.height / 2)
		implicitWidth: 32
		height: Math.floor((control.height - 2) / 2)
		color: !control.enabled ? "transparent"
			: control.down.pressed ? Theme.accentSubtle : control.down.hovered ? Theme.surfaceHover : "transparent"
		Text { anchors.centerIn: parent; text: "−"; color: control.enabled ? Theme.textMain : Theme.textMuted; font.pixelSize: 12 }
	}

	background: Rectangle {
		radius: Theme.innerRadius
		color: control.enabled ? Theme.surfaceRaised : Theme.panel
		border.color: !control.enabled ? Theme.divider
			: control.invalid ? Theme.danger
			: control.activeFocus ? Theme.focus : Theme.divider
		border.width: control.invalid || control.activeFocus ? Theme.focusRingWidth : 1
		Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.rightMargin: 32; width: 1; color: Theme.divider }
		Rectangle { anchors.right: parent.right; width: 32; height: 1; y: Math.floor(parent.height / 2); color: Theme.divider }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}
}
