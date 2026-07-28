import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

	MenuItem {
	id: item
	required property var payload
	// Visual automation and accessibility focus recovery must be able to
	// distinguish equal delegate types that live in several closed/open menus.
	// Action IDs are stable across model refreshes and already form the command ABI.
	objectName: "payloadMenuItem_"
		+ String(menu && menu.objectName ? menu.objectName : "menu") + "_"
		+ String(payload.id || payload.kind || "item")
	readonly property string itemKind: String(payload.kind || "action")
	readonly property string itemTone: String(payload.tone || "").toLowerCase()
	readonly property string iconName: String(payload.icon || "")
	readonly property string secondaryText: String(payload.secondary
		|| ((itemKind === "header" || itemKind === "label") ? payload.hint : "") || "")
	readonly property string trailingState: String(payload.state || payload.trailing || "")
	readonly property int hierarchyDepth: Math.max(0, Math.min(3, Number(payload.depth || 0)))
	readonly property bool hasSubmenu: itemKind === "submenu"
		|| (itemKind === "action" && (!!payload.hasSubmenu || !!payload.submenu || !!payload.items))
	readonly property color toneColor: itemTone === "danger" || itemTone === "error" ? Theme.danger
		: itemTone === "warning" || itemTone === "retry" ? Theme.warning
		: itemTone === "success" ? Theme.success
		: itemTone === "accent" || itemTone === "primary" ? Theme.accent : "transparent"
	signal actionRequested(string actionId)
	signal valueRequested(string actionId, int value, bool finalValue)

	function openAttachedSubmenu(focusChild) {
		if (!hasSubmenu || !enabled || !subMenu)
			return false
		const ownerMenu = menu
		if (ownerMenu && ownerMenu["openSubmenuFor"] !== undefined)
			return ownerMenu["openSubmenuFor"](item, subMenu, !!focusChild)
		// Passing both the visual parent and its owning MenuItem selects Qt Quick
		// Controls' cascade positioning path (including left-edge flipping).
		subMenu.popup(item, item)
		return true
	}

	function closeOwningSubmenu() {
		const ownerMenu = menu
		if (!ownerMenu || ownerMenu["closeToOpener"] === undefined)
			return false
		return ownerMenu["closeToOpener"]()
	}

	readonly property real rowImplicitHeight: itemKind === "separator" ? 9
		: itemKind === "section" ? (Theme.compact ? 22 : 25)
		: itemKind === "header" ? (Theme.compact ? 44 : 50)
		: itemKind === "slider" ? Math.max(44, Theme.rowHeight)
		: secondaryText.length > 0 ? Math.max(42, Theme.rowHeight) : Theme.controlHeight
	visible: payload.visible !== false
	implicitHeight: rowImplicitHeight
	leftPadding: Theme.space2 + hierarchyDepth * Theme.space3
	rightPadding: Theme.space2
	topPadding: 0
	bottomPadding: 0
	activeFocusOnTab: activeFocus || enabled
	hoverEnabled: enabled
	enabled: (itemKind === "action" || itemKind === "slider" || itemKind === "submenu")
		&& (payload.enabled === undefined || !!payload.enabled)
	checkable: itemKind === "action" && !!payload.checkable
	checked: checkable && !!payload.checked
	text: payload.label || payload.id || ""
	Accessible.ignored: itemKind === "separator"
	Accessible.name: itemKind === "separator" ? "" : text
	Accessible.description: String(payload.hint || secondaryText || "")
	Accessible.checked: checked

	// The custom leading slot below owns selection rendering. Suppress Qt
	// Quick Controls' style-provided indicator so checked rows never paint a
	// second checkmark behind or beside our icon.
	indicator: Item {
		implicitWidth: 0
		implicitHeight: 0
		visible: false
		Accessible.ignored: true
	}

	Keys.onRightPressed: event => event.accepted = item.openAttachedSubmenu(true)
	Keys.onReturnPressed: event => event.accepted = item.openAttachedSubmenu(true)
	Keys.onEnterPressed: event => event.accepted = item.openAttachedSubmenu(true)
	Keys.onSpacePressed: event => event.accepted = item.openAttachedSubmenu(true)
	Keys.onLeftPressed: event => event.accepted = item.closeOwningSubmenu()
	Keys.onEscapePressed: event => event.accepted = item.closeOwningSubmenu()
	Keys.onPressed: event => {
		if (item.menu && item.menu["setPointerNavigationActive"] !== undefined)
			item.menu["setPointerNavigationActive"](false)
	}
	Shortcut {
		enabled: item.hasSubmenu && item.enabled && item.activeFocus
		sequences: [Qt.Key_Right, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space]
		context: Qt.WindowShortcut
		onActivated: item.openAttachedSubmenu(true)
	}
	Shortcut {
		enabled: item.activeFocus && item.menu && item.menu["openerItem"]
		sequences: [Qt.Key_Left, Qt.Key_Escape]
		context: Qt.WindowShortcut
		onActivated: item.closeOwningSubmenu()
	}

	onHoveredChanged: {
		if (hovered && item.menu) {
			if (item.menu["setPointerNavigationActive"] !== undefined)
				item.menu["setPointerNavigationActive"](true)
			if (item.menu["activatePointerItem"] !== undefined)
				item.menu["activatePointerItem"](item)
		}
		if (!item.hasSubmenu)
			return
		// The menu tree and its delegates are already materialized. Opening here is
		// cheap and removes the previous fixed 120 ms hover latency.
		if (hovered)
			item.openAttachedSubmenu(false)
	}

	background: Rectangle {
		id: focusBackground
		objectName: "payloadFocusBackground"
		color: (item.hovered || item.highlighted || item.down) && item.enabled
			? Theme.popupSelected : "transparent"
		border.color: item.activeFocus && item.enabled
			&& !(item.menu && item.menu.pointerNavigationActive)
			? Theme.focus : "transparent"
		border.width: item.activeFocus && item.enabled
			&& !(item.menu && item.menu.pointerNavigationActive)
			? Theme.focusRingWidth : 0
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
			visible: item.itemKind === "action" || item.itemKind === "submenu"
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
				Accessible.ignored: true
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
				Accessible.ignored: true
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
			Accessible.ignored: true
		}

		ColumnLayout {
			visible: item.itemKind === "action" || item.itemKind === "submenu"
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
				Accessible.ignored: true
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
				Accessible.ignored: true
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
