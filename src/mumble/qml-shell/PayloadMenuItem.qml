import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

MenuItem {
	id: item
	objectName: "payloadMenuItem"
	required property var payload
	readonly property string itemKind: String(payload.kind || "action")
	readonly property string itemTone: String(payload.tone || "").toLowerCase()
	readonly property string iconName: String(payload.icon || "")
	readonly property string secondaryText: String(payload.secondary
		|| ((itemKind === "header" || itemKind === "label") ? payload.hint : "") || "")
	readonly property string trailingState: String(payload.state || payload.trailing || "")
	readonly property int hierarchyDepth: Math.max(0, Math.min(3, Number(payload.depth || 0)))
	readonly property bool hasSubmenu: itemKind === "action" && (!!payload.hasSubmenu || !!payload.submenu)
	readonly property color toneColor: itemTone === "danger" || itemTone === "error" ? Theme.danger
		: itemTone === "warning" || itemTone === "retry" ? Theme.warning
		: itemTone === "success" ? Theme.success
		: itemTone === "accent" || itemTone === "primary" ? Theme.accent : "transparent"
	signal actionRequested(string actionId)
	signal valueRequested(string actionId, int value, bool finalValue)

	visible: payload.visible !== false
	implicitHeight: itemKind === "separator" ? 9
		: itemKind === "section" ? (Theme.compact ? 22 : 25)
		: itemKind === "header" ? (Theme.compact ? 44 : 50)
		: itemKind === "slider" ? Math.max(44, Theme.rowHeight)
		: secondaryText.length > 0 ? Math.max(42, Theme.rowHeight) : Theme.controlHeight
	height: visible ? implicitHeight : 0
	leftPadding: Theme.space2 + hierarchyDepth * Theme.space3
	rightPadding: Theme.space2
	topPadding: 0
	bottomPadding: 0
	activeFocusOnTab: enabled
	enabled: (itemKind === "action" || itemKind === "slider")
		&& (payload.enabled === undefined || !!payload.enabled)
	checkable: itemKind === "action" && !!payload.checkable
	checked: checkable && !!payload.checked
	text: payload.label || payload.id || ""
	Accessible.ignored: itemKind === "separator"
	Accessible.name: itemKind === "separator" ? "" : text
	Accessible.description: String(payload.hint || secondaryText || "")
	Accessible.checked: checked

	background: Rectangle {
		id: focusBackground
		objectName: "payloadFocusBackground"
		color: (item.highlighted || item.down) && item.enabled ? Theme.popupSelected : "transparent"
		border.color: item.activeFocus && item.enabled ? Theme.focus : "transparent"
		border.width: item.activeFocus && item.enabled ? Theme.focusRingWidth : 0
		radius: Math.max(4, Math.round(Theme.innerRadius / 2))
	}

	contentItem: RowLayout {
		spacing: Theme.space2

		Rectangle {
			visible: item.itemKind === "separator"
			Layout.fillWidth: true
			Layout.preferredHeight: 1
			color: Theme.divider
			Accessible.ignored: true
		}

		Item {
			id: leadingSlot
			objectName: "payloadLeadingSlot"
			visible: item.itemKind === "action"
			Layout.preferredWidth: visible ? 20 : 0
			Layout.preferredHeight: 20
			Layout.alignment: Qt.AlignVCenter
			Accessible.ignored: true

			ModernIcon {
				id: leadingIcon
				objectName: "payloadLeadingIcon"
				anchors.centerIn: parent
				name: item.checked ? "check" : item.iconName
				size: 16
				color: !item.enabled ? Theme.textMuted
					: item.checked ? Theme.accent
					: item.toneColor.a > 0 ? item.toneColor : Theme.textMuted
				visible: pathData.length > 0
			}

			Rectangle {
				visible: !leadingIcon.visible && item.toneColor.a > 0
				anchors.centerIn: parent
				width: 7
				height: 7
				radius: 4
				color: item.toneColor
			}
		}

		ColumnLayout {
			visible: item.itemKind !== "separator"
			Layout.fillWidth: true
			Layout.minimumWidth: 32
			spacing: 1

			Label {
				id: primaryLabel
				objectName: "payloadPrimaryLabel"
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: item.itemKind === "section" ? item.text.toUpperCase() : item.text
				color: item.itemKind === "header" ? Theme.textStrong
					: item.itemKind === "section" ? Theme.textMuted
					: (!item.enabled ? Theme.textMuted
						: item.itemTone === "danger" || item.itemTone === "error"
							? Theme.danger : Theme.textMain)
				font.pixelSize: item.itemKind === "section" ? Theme.fontCaption
					: item.itemKind === "header" ? Theme.fontBody : Theme.fontLabel
				font.weight: item.itemKind === "section" || item.itemKind === "header"
					? Font.DemiBold : Font.Medium
				font.letterSpacing: item.itemKind === "section" ? 0.55 : 0
				elide: Text.ElideRight
			}

			Label {
				id: secondaryLabel
				objectName: "payloadSecondaryLabel"
				textFormat: Text.PlainText
				visible: item.secondaryText.length > 0
				Layout.fillWidth: true
				text: item.secondaryText
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
			font.pixelSize: Theme.fontCaption
		}

		ColumnLayout {
			visible: item.itemKind === "action"
			Layout.maximumWidth: 120
			spacing: 1

			Label {
				id: stateLabel
				objectName: "payloadStateLabel"
				textFormat: Text.PlainText
				visible: item.trailingState.length > 0
				Layout.alignment: Qt.AlignRight
				text: item.trailingState
				color: item.checked ? Theme.accent : Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.weight: Font.Medium
				elide: Text.ElideRight
			}

			Label {
				id: shortcutLabel
				objectName: "payloadShortcutLabel"
				textFormat: Text.PlainText
				visible: String(item.payload.shortcut || "").length > 0
				Layout.alignment: Qt.AlignRight
				text: String(item.payload.shortcut || "")
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
			}
		}

		ModernIcon {
			id: submenuIcon
			objectName: "payloadSubmenuIcon"
			visible: item.hasSubmenu
			name: "next"
			size: 14
			color: Theme.textMuted
			Layout.preferredWidth: visible ? 14 : 0
			Layout.preferredHeight: 14
			Layout.alignment: Qt.AlignVCenter
		}
	}

	onTriggered: {
		if (itemKind === "action")
			actionRequested(payload.id || "")
	}
}
