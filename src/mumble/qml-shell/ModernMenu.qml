import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Menu {
	id: menu
	modal: false
	dim: false
	padding: 6
	Component.onCompleted: {
		// Qt 6.2-6.7 only implement item-backed popups and do not expose popupType.
		// Qt 6.8+ may otherwise choose a separate native/window surface.
		if (menu["popupType"] !== undefined)
			menu["popupType"] = 0 // Popup.Item
	}

	palette.window: Theme.panel
	palette.base: Theme.panel
	palette.button: Theme.panel
	palette.text: Theme.textMain
	palette.windowText: Theme.textMain
	palette.buttonText: Theme.textMain
	palette.highlight: Theme.selected
	palette.highlightedText: Theme.textStrong
	palette.disabled.text: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted

	background: Rectangle {
		color: Theme.panel
		border.color: Theme.divider
		border.width: 1
		radius: Theme.innerRadius
	}
}
