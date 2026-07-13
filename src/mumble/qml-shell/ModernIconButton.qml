import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ToolButton {
	id: control
	property bool selected: false
	property string tone: "neutral"
	property bool dense: false
	property string iconName: ""
	readonly property color toneColor: tone === "danger" ? Theme.danger
		: tone === "success" ? Theme.success
		: tone === "warning" ? Theme.warning : Theme.accent

	Accessible.role: Accessible.Button
	Accessible.name: text
	hoverEnabled: true
	implicitWidth: dense ? 30 : Theme.controlHeight
	implicitHeight: implicitWidth
	padding: 0
	font.pixelSize: dense ? 15 : 18
	scale: down ? 0.94 : 1.0
	Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }

	contentItem: Item {
		readonly property color foreground: !control.enabled ? Theme.textMuted
			: control.selected ? Theme.textStrong
			: control.hovered ? Theme.textStrong : Theme.textMain
		ModernIcon {
			anchors.centerIn: parent
			visible: control.iconName.length > 0
			name: control.iconName
			size: control.dense ? 16 : 18
			color: parent.foreground
		}
		Text {
			anchors.fill: parent
			visible: control.iconName.length === 0
			text: control.text
			font: control.font
			color: parent.foreground
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignVCenter
		}
	}

	background: Rectangle {
		radius: Theme.innerRadius
		color: control.selected ? Theme.accentSubtle
			: control.hovered || control.down ? Theme.surfaceHover : "transparent"
		border.color: control.activeFocus ? Theme.focus
			: control.selected ? control.toneColor : "transparent"
		border.width: control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}
}
