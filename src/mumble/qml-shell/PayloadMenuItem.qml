import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

MenuItem {
	id: item
	objectName: "payloadMenuItem"
	required property var payload
	readonly property string itemKind: payload.kind || "action"
	readonly property string itemTone: String(payload.tone || "").toLowerCase()
	readonly property color toneColor: itemTone === "danger" || itemTone === "error" ? Theme.danger
		: itemTone === "warning" || itemTone === "retry" ? Theme.warning
		: itemTone === "success" ? Theme.success
		: itemTone === "accent" || itemTone === "primary" ? Theme.accent : "transparent"
	signal actionRequested(string actionId)
	signal valueRequested(string actionId, int value, bool finalValue)

	visible: payload.visible === undefined || !!payload.visible
	height: !visible ? 0 : itemKind === "separator" ? 9
		: itemKind === "section" ? 28 : itemKind === "header" ? 56
		: Math.max(implicitHeight, itemKind === "slider" ? 46 : Theme.controlHeight)
	enabled: (itemKind === "action" || itemKind === "slider")
		&& (payload.enabled === undefined || !!payload.enabled)
	checkable: itemKind === "action" && !!payload.checkable
	checked: checkable && !!payload.checked
	text: payload.label || payload.id || ""
	Accessible.ignored: itemKind === "separator"
	Accessible.name: itemKind === "separator" ? "" : text
	Accessible.description: payload.hint || ""
	background: Rectangle {
		color: item.highlighted && item.enabled ? Theme.selected : "transparent"
		radius: Math.max(4, Math.round(Theme.innerRadius / 2))
	}

	contentItem: RowLayout {
		spacing: Math.max(6, Math.round(Theme.spacing / 2))
		Rectangle {
			visible: item.itemKind === "separator"
			Layout.fillWidth: true
			Layout.preferredHeight: 1
			color: Theme.divider
		}
		Rectangle {
			visible: item.itemKind === "action" && item.toneColor.a > 0
			Layout.preferredWidth: 7
			Layout.preferredHeight: 7
			radius: 4
			color: item.toneColor
			Accessible.ignored: true
		}
		Label {
			textFormat: Text.PlainText
			visible: item.itemKind === "action" && item.checkable
			text: item.checked ? "✓" : ""
			color: item.enabled ? Theme.accent : Theme.textMuted
			font.bold: true
			Layout.preferredWidth: visible ? 14 : 0
			Accessible.ignored: true
		}
		ColumnLayout {
			visible: item.itemKind !== "separator"
			Layout.fillWidth: true
			spacing: 1
			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: item.itemKind === "section" ? item.text.toUpperCase() : item.text
				color: item.itemKind === "header" ? Theme.textStrong
					: item.itemKind === "section" ? Theme.textMuted
					: (item.enabled ? (item.itemTone === "danger" || item.itemTone === "error"
						? Theme.danger : Theme.textMain) : Theme.textMuted)
				font.pixelSize: item.itemKind === "section" ? Theme.fontCaption
					: item.itemKind === "header" ? Theme.fontBody : Theme.fontLabel
				font.weight: item.itemKind === "section" || item.itemKind === "header"
					? Font.DemiBold : Font.Medium
				font.letterSpacing: item.itemKind === "section" ? 0.6 : 0
				elide: Text.ElideRight
			}
			Label {
				textFormat: Text.PlainText
				visible: item.itemKind === "header" && String(item.payload.hint || "").length > 0
				Layout.fillWidth: true
				text: String(item.payload.hint || "")
				color: item.itemTone === "danger" || item.itemTone === "error" ? Theme.danger
					: item.itemTone === "warning" ? Theme.warning : Theme.textMuted
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
			}
		}
		ModernSlider {
			id: valueSlider
			objectName: "payloadValueSlider"
			Layout.preferredWidth: 112
			visible: item.itemKind === "slider"
			enabled: item.enabled
			from: Number(item.payload.min === undefined ? 0 : item.payload.min)
			to: Number(item.payload.max === undefined ? 100 : item.payload.max)
			stepSize: Number(item.payload.step === undefined ? 1 : item.payload.step)
			value: Number(item.payload.value === undefined ? from : item.payload.value)
			Accessible.name: item.text
			onMoved: item.valueRequested(item.payload.id || "", Math.round(value), !pressed)
			onPressedChanged: {
				if (!pressed)
					item.valueRequested(item.payload.id || "", Math.round(value), true)
			}
		}
		Label {
			textFormat: Text.PlainText
			visible: item.itemKind === "slider"
			text: Math.round(valueSlider.value) + (item.payload.suffix || "")
			color: Theme.textMuted
			font.pixelSize: 10
		}
		Label {
			textFormat: Text.PlainText
			visible: item.itemKind === "action" && String(item.payload.shortcut || "").length > 0
			text: String(item.payload.shortcut || "")
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			Layout.maximumWidth: 120
			elide: Text.ElideRight
		}
	}

	onTriggered: {
		if (itemKind === "action")
			actionRequested(payload.id || "")
	}
}
