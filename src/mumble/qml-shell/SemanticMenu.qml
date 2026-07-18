import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

PayloadMenu {
	id: menu
	property var groups: []
	property string headerTitle: ""
	property string headerSubtitle: ""
	property string headerTone: ""
	property bool showSectionLabels: true
	property real maximumHeight: 620
	readonly property var renderedEntries: hierarchicalGroups(groups)

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

	function representativeGroupIcon(group, items) {
		const explicitIcon = String((group || ({})).icon || "").trim()
		if (explicitIcon.length > 0)
			return explicitIcon
		const source = items || []
		for (let index = 0; index < source.length; ++index) {
			const icon = String((source[index] || ({})).icon || "").trim()
			if (icon.length > 0)
				return icon
		}
		return "action"
	}

	function hierarchicalGroups(sourceGroups) {
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
					"kind": "submenu",
					"id": "semantic-menu-group-" + String(group.id || groupIndex),
					"label": String(group.label || ""),
					"hint": String(group.hint || ""),
					// Group rows reuse the first real action glyph supplied by the
					// controller. This preserves the typed icon vocabulary instead of
					// painting the same generic menu mark on every top-level row.
					"icon": representativeGroupIcon(group, items),
					"enabled": group.enabled === undefined || !!group.enabled,
					"items": items
				})
			} else {
				for (let itemIndex = 0; itemIndex < items.length; ++itemIndex)
					output.push(items[itemIndex])
			}
		}
		return output
	}

	preferredWidth: 292
	height: Math.min(implicitHeight, maximumHeight,
		Math.max(1, logicalViewportHeight))
	focus: true
	entries: renderedEntries
}
