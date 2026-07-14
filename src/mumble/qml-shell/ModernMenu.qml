import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Menu {
	id: menu
	modal: false
	dim: false
	focus: true
	padding: Theme.space1
	spacing: 1
	transformOrigin: Item.TopRight
	enter: Transition {
		NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionFast; easing.type: Easing.OutCubic }
		NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.motionFast; easing.type: Easing.OutCubic }
	}
	exit: Transition {
		NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motionFast }
	}
	Component.onCompleted: {
		// Qt 6.2-6.7 only implement item-backed popups and do not expose popupType.
		// Qt 6.8+ may otherwise choose a separate native/window surface.
		if (menu["popupType"] !== undefined)
			menu["popupType"] = 0 // Popup.Item
	}

	palette.window: Theme.popupBackground
	palette.active.base: Theme.popupBackground
	palette.inactive.base: Theme.popupBackground
	palette.alternateBase: Theme.panel
	palette.active.button: Theme.popupBackground
	palette.inactive.button: Theme.popupBackground
	palette.active.text: Theme.textMain
	palette.inactive.text: Theme.textMain
	palette.active.windowText: Theme.textMain
	palette.inactive.windowText: Theme.textMain
	palette.active.buttonText: Theme.textMain
	palette.inactive.buttonText: Theme.textMain
	palette.active.brightText: Theme.textStrong
	palette.inactive.brightText: Theme.textStrong
	palette.active.highlight: Theme.popupSelected
	palette.inactive.highlight: Theme.popupSelected
	palette.active.highlightedText: Theme.textStrong
	palette.inactive.highlightedText: Theme.textStrong
	palette.placeholderText: Theme.textMuted
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.popupBackground
	palette.inactive.toolTipBase: Theme.popupBackground
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.active.light: Theme.popupHover
	palette.inactive.light: Theme.popupHover
	palette.active.midlight: Theme.popupBackground
	palette.inactive.midlight: Theme.popupBackground
	palette.active.mid: Theme.popupBorder
	palette.inactive.mid: Theme.popupBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.disabled.window: Theme.popupBackground
	palette.disabled.base: Theme.panel
	palette.disabled.alternateBase: Theme.panel
	palette.disabled.button: Theme.panel
	palette.disabled.text: Theme.textMuted
	palette.disabled.windowText: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
	palette.disabled.brightText: Theme.textMuted
	palette.disabled.highlight: Theme.popupBorder
	palette.disabled.highlightedText: Theme.textMuted
	palette.disabled.placeholderText: Theme.textMuted
	palette.disabled.light: Theme.popupBorder
	palette.disabled.midlight: Theme.panel
	palette.disabled.mid: Theme.divider
	palette.disabled.dark: Theme.rail
	palette.disabled.shadow: Theme.strip
	palette.disabled.link: Theme.textMuted
	palette.disabled.linkVisited: Theme.textMuted
	palette.disabled.toolTipBase: Theme.panel
	palette.disabled.toolTipText: Theme.textMuted

	background: Rectangle {
		id: menuSurface
		objectName: "modernMenuSurface"
		implicitWidth: 220
		implicitHeight: Theme.controlHeight
		color: Theme.popupBackground
		border.color: menu.activeFocus ? Theme.focus : Theme.popupBorder
		border.width: 1
		radius: Theme.innerRadius

		// A pair of ordinary scene-graph rectangles gives the popup depth without
		// allocating an offscreen layer or invoking a shader effect.
		Rectangle {
			id: menuShadow
			objectName: "modernMenuShadow"
			z: -1
			x: Math.max(1, Math.round(Theme.elevationMenuOffset / 2))
			y: Theme.elevationMenuOffset
			width: parent.width
			height: parent.height
			radius: parent.radius + 1
			color: Theme.elevationShadow
			border.width: 0
			Accessible.ignored: true
		}

		Rectangle {
			objectName: "modernMenuHighlight"
			x: 1
			y: 1
			width: Math.max(0, parent.width - 2)
			height: 1
			color: Theme.elevationHighlight
			Accessible.ignored: true
		}
	}
}
