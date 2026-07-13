import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

MenuItem {
	id: item
	objectName: "payloadMenuItem"
	required property var payload
	readonly property string itemKind: payload.kind || "action"
	signal actionRequested(string actionId)
	signal valueRequested(string actionId, int value, bool finalValue)

	height: itemKind === "separator" ? 9 : Math.max(implicitHeight, itemKind === "slider" ? 46 : 0)
	enabled: (itemKind === "action" || itemKind === "slider")
		&& (payload.enabled === undefined || !!payload.enabled)
	checkable: itemKind === "action" && !!payload.checkable
	checked: checkable && !!payload.checked
	text: payload.label || payload.id || ""
	Accessible.ignored: itemKind === "separator"
	Accessible.name: itemKind === "separator" ? "" : text
	Accessible.description: payload.hint || ""
	background: Rectangle {
		color: item.highlighted ? Theme.selected : "transparent"
		radius: Math.max(4, Math.round(Theme.innerRadius / 2))
	}

	contentItem: RowLayout {
		spacing: Math.max(6, Math.round(Theme.spacing / 2))
		Label {
			textFormat: Text.PlainText
			visible: item.checkable
			text: item.checked ? "✓" : ""
			color: item.enabled ? Theme.accent : Theme.textMuted
			font.bold: true
			Layout.preferredWidth: visible ? 14 : 0
			Accessible.ignored: true
		}
		Label {
			textFormat: Text.PlainText
			Layout.fillWidth: true
			text: item.itemKind === "separator" ? "────────────" : item.text
			color: item.itemKind === "separator" ? Theme.divider
				: (item.enabled ? Theme.textMain : Theme.textMuted)
			elide: Text.ElideRight
		}
		Slider {
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
	}

	onTriggered: {
		if (itemKind === "action")
			actionRequested(payload.id || "")
	}
}
