import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Button {
    id: control
	property string tone: "neutral"
	property bool dense: false
	readonly property color toneColor: {
		const normalized = String(tone || "").toLowerCase()
		if (normalized === "danger" || normalized === "error") return Theme.danger
		if (normalized === "warning" || normalized === "retry") return Theme.warning
		if (normalized === "success") return Theme.success
		return Theme.accent
	}
	readonly property color hoverToneColor: {
		const normalized = String(tone || "").toLowerCase()
		if (normalized === "primary" || normalized === "accent" || normalized === "neutral" || normalized === "")
			return Theme.accentHover
		return Qt.lighter(toneColor, 1.08)
	}
	readonly property bool emphasized: highlighted || checked
		|| ["primary", "accent", "danger", "error", "warning", "retry", "success"]
			.indexOf(String(tone || "").toLowerCase()) >= 0
    Accessible.role: Accessible.Button
    Accessible.name: text
	hoverEnabled: true
	implicitHeight: dense ? Math.max(28, Theme.controlHeight - 4) : Theme.controlHeight
	implicitWidth: Math.max(implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
	leftPadding: dense ? Theme.space2 : Theme.space3
	rightPadding: leftPadding
	topPadding: Theme.space1
	bottomPadding: Theme.space1
	font.pixelSize: Theme.fontLabel
	font.weight: emphasized ? Font.DemiBold : Font.Medium
    palette.buttonText: Theme.textStrong
	scale: down ? 0.98 : 1.0
	Keys.onReturnPressed: event => {
		if (control.enabled) control.clicked()
		event.accepted = true
	}
	Keys.onEnterPressed: event => {
		if (control.enabled) control.clicked()
		event.accepted = true
	}
	Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }
	contentItem: Text {
		text: control.text
		font: control.font
		color: !control.enabled ? Theme.textMuted
			: control.emphasized ? Theme.contrastText(control.toneColor) : Theme.textStrong
		horizontalAlignment: Text.AlignHCenter
		verticalAlignment: Text.AlignVCenter
		elide: Text.ElideRight
	}
    background: Rectangle {
		radius: Math.min(Theme.innerRadius, Math.round(control.implicitHeight / 3))
		color: !control.enabled ? Theme.panel
			: control.down ? Qt.darker(control.emphasized ? control.toneColor : Theme.surfaceHover, 1.08)
			: control.emphasized ? (control.hovered ? control.hoverToneColor : control.toneColor)
			: control.hovered ? Theme.surfaceHover : Theme.surfaceRaised
		border.color: !control.enabled ? Theme.divider
			: control.visualFocus ? Theme.focus
			: control.emphasized ? Qt.rgba(control.toneColor.r, control.toneColor.g, control.toneColor.b, 0.72)
			: Theme.surfaceBorder
		border.width: control.visualFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }
}
