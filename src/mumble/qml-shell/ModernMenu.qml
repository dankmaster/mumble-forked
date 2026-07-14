import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Menu {
	id: menu
	modal: false
	dim: false
	padding: Theme.space1
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

	palette.window: Theme.surfaceRaised
	palette.active.base: Theme.surfaceRaised
	palette.inactive.base: Theme.surfaceRaised
	palette.alternateBase: Theme.panel
	palette.active.button: Theme.surfaceRaised
	palette.inactive.button: Theme.surfaceRaised
	palette.active.text: Theme.textMain
	palette.inactive.text: Theme.textMain
	palette.active.windowText: Theme.textMain
	palette.inactive.windowText: Theme.textMain
	palette.active.buttonText: Theme.textMain
	palette.inactive.buttonText: Theme.textMain
	palette.active.brightText: Theme.textStrong
	palette.inactive.brightText: Theme.textStrong
	palette.active.highlight: Theme.selected
	palette.inactive.highlight: Theme.selected
	palette.active.highlightedText: Theme.textStrong
	palette.inactive.highlightedText: Theme.textStrong
	palette.placeholderText: Theme.textMuted
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.surfaceRaised
	palette.inactive.toolTipBase: Theme.surfaceRaised
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.disabled.window: Theme.surfaceRaised
	palette.disabled.base: Theme.panel
	palette.disabled.alternateBase: Theme.panel
	palette.disabled.button: Theme.panel
	palette.disabled.text: Theme.textMuted
	palette.disabled.windowText: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
	palette.disabled.brightText: Theme.textMuted
	palette.disabled.highlight: Theme.surfaceBorder
	palette.disabled.highlightedText: Theme.textMuted
	palette.disabled.placeholderText: Theme.textMuted
	palette.disabled.light: Theme.surfaceBorder
	palette.disabled.midlight: Theme.panel
	palette.disabled.mid: Theme.divider
	palette.disabled.dark: Theme.rail
	palette.disabled.shadow: Theme.strip
	palette.disabled.link: Theme.textMuted
	palette.disabled.linkVisited: Theme.textMuted
	palette.disabled.toolTipBase: Theme.panel
	palette.disabled.toolTipText: Theme.textMuted

	background: Rectangle {
		color: Theme.surfaceRaised
		border.color: Theme.surfaceBorder
		border.width: 1
		radius: Theme.innerRadius
	}
}
