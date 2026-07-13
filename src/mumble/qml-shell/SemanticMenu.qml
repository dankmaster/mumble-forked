import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ModernMenu {
	id: menu
	property var groups: []
	property string headerTitle: ""
	property string headerSubtitle: ""
	property string headerTone: ""
	property bool showSectionLabels: true
	property real maximumHeight: 620
	readonly property var renderedEntries: flattenGroups(groups)
	signal actionRequested(string actionId, var payload)

	function normalizedItems(items) {
		const output = []
		let previousWasSeparator = true
		const source = items || []
		for (let index = 0; index < source.length; ++index) {
			const entry = source[index] || ({})
			if (entry.visible !== undefined && !entry.visible)
				continue
			const kind = String(entry.kind || "action")
			if (kind === "separator") {
				if (!previousWasSeparator) {
					output.push(entry)
					previousWasSeparator = true
				}
				continue
			}
			output.push(entry)
			previousWasSeparator = false
		}
		if (output.length > 0 && String(output[output.length - 1].kind || "") === "separator")
			output.pop()
		return output
	}

	function flattenGroups(sourceGroups) {
		const output = []
		if (headerTitle.length > 0) {
			output.push({
				"kind": "header",
				"id": "semantic-menu-header",
				"label": headerTitle,
				"hint": headerSubtitle,
				"tone": headerTone,
				"enabled": false
			})
		}
		const source = sourceGroups || []
		for (let groupIndex = 0; groupIndex < source.length; ++groupIndex) {
			const group = source[groupIndex] || ({})
			const items = normalizedItems(group.items || [])
			if (items.length === 0)
				continue
			if (showSectionLabels && String(group.label || "").length > 0) {
				output.push({
					"kind": "section",
					"id": "semantic-menu-section-" + String(group.id || groupIndex),
					"label": String(group.label || ""),
					"enabled": false
				})
			}
			for (let itemIndex = 0; itemIndex < items.length; ++itemIndex)
				output.push(items[itemIndex])
		}
		return output
	}

	width: 292
	height: Math.min(implicitHeight, maximumHeight)
	focus: true
	closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

	Repeater {
		model: menu.renderedEntries
		delegate: PayloadMenuItem {
			required property var modelData
			payload: modelData
			onActionRequested: actionId => menu.actionRequested(actionId, modelData)
		}
	}
}
