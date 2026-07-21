import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

CheckBox {
	id: control
	property bool dense: false

	Accessible.role: Accessible.CheckBox
	Accessible.name: text
	hoverEnabled: true
	spacing: Theme.space2
	font.pixelSize: Theme.fontBody
	font.weight: Font.Medium
	implicitHeight: dense ? 30 : Theme.controlHeight

	indicator: Rectangle {
		implicitWidth: control.dense ? 17 : 19
		implicitHeight: implicitWidth
		x: control.leftPadding
		y: Math.round((control.height - height) / 2)
		radius: 5
		color: !control.enabled ? (control.checked ? Theme.surfaceBorder : Theme.panel)
			: control.checked ? Theme.accent : Theme.surfaceRaised
		border.color: !control.enabled ? Theme.divider
			: control.activeFocus ? Theme.focus
			: control.checked ? Theme.accentHover
			: control.hovered ? Theme.surfaceBorder : Theme.divider
		border.width: control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		ModernIcon {
			anchors.centerIn: parent
			visible: control.checked
			name: "check"
			size: control.dense ? 11 : 12
			color: control.enabled ? Theme.contrastText(Theme.accent) : Theme.textMuted
		}
	}

	contentItem: Text {
		leftPadding: control.indicator.width + control.spacing
		text: control.text
		font: control.font
		color: control.enabled ? Theme.textMain : Theme.textMuted
		verticalAlignment: Text.AlignVCenter
		wrapMode: Text.Wrap
	}
}
