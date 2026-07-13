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
	palette.base: Theme.surfaceRaised
	palette.button: Theme.surfaceRaised
	palette.text: Theme.textMain
	palette.windowText: Theme.textMain
	palette.buttonText: Theme.textMain
	palette.highlight: Theme.selected
	palette.highlightedText: Theme.textStrong
	palette.disabled.text: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted

	background: Rectangle {
		color: Theme.surfaceRaised
		border.color: Theme.surfaceBorder
		border.width: 1
		radius: Theme.innerRadius
	}
}
