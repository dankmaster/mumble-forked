import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ToolButton {
	id: control
	property bool selected: false
	property bool overlay: false
	property string tone: "neutral"
	property bool dense: false
	property string iconName: ""
	property string toolTipText: String(Accessible.name || text || "")
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
	ToolTip.visible: hovered && toolTipText.length > 0
	ToolTip.text: toolTipText
	ToolTip.delay: 500
	ToolTip.timeout: 15000
	scale: down ? 0.94 : 1.0
	Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }

	contentItem: Item {
		readonly property color foreground: !control.enabled
			? (control.overlay ? Theme.mediaOverlayTextMuted : Theme.textMuted)
			: control.selected || control.hovered
				? (control.overlay ? Theme.mediaOverlayTextStrong : Theme.textStrong)
				: control.overlay ? Theme.mediaOverlayTextMuted : Theme.textMain
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
		color: !control.enabled ? (control.overlay ? "transparent" : Theme.panel)
			: control.selected ? (control.overlay
				? Theme.withAlpha(Theme.accent, 0.22) : Theme.accentSubtle)
			: control.hovered || control.down ? (control.overlay
				? Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.12) : Theme.surfaceHover)
			: "transparent"
		border.color: !control.enabled ? (control.overlay ? "transparent" : Theme.divider)
			: control.visualFocus ? Theme.focus
			: control.selected ? control.toneColor : "transparent"
		border.width: control.visualFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}
}
