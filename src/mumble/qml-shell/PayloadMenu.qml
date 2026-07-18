import QtQuick
import QtQuick.Controls

// A data-driven ModernMenu that keeps the backend's typed payload tree intact.
// Qt Quick Controls owns the actual Menu/MenuItem relationship, which gives us
// pointer hover, one-open-submenu semantics, keyboard cascade navigation, focus
// restoration and screen-edge flipping without a parallel popup state machine.
ModernMenu {
	id: menu
	property var entries: []
	property real maximumHeight: 620
	property real preferredWidth: 292
	// QML geometry is already expressed in device-independent logical pixels.
	// The opening menu supplies the current scene viewport; never multiply these
	// limits by Screen.devicePixelRatio (that would double-scale on Windows).
	property real logicalViewportWidth: Number.POSITIVE_INFINITY
	property real logicalViewportHeight: Number.POSITIVE_INFINITY
	property bool closeOnAction: true
	readonly property var normalizedEntries: normalizeEntries(entries)
	readonly property int generatedEntryCount: generatedEntries.length
	property int entryObjectCreationCount: 0
	property int entryTreeRebuildCount: 0
	property var generatedEntries: []
	property bool componentCompleted: false
	property bool reconcilePending: false
	// The root menu resolves this composite component once and passes the same
	// compiled factory to every descendant. Direct recursive Component syntax is
	// rejected by QML, while resolving the URL after completion is supported.
	property var sharedSubmenuComponent: null
	signal actionRequested(string actionId, var payload)
	signal valueRequested(string actionId, int value, bool finalValue, var payload)

	width: Math.min(preferredWidth, Math.max(1, logicalViewportWidth))
	height: Math.min(implicitHeight, maximumHeight,
		Math.max(1, logicalViewportHeight))
	closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

	function entryKind(entry) {
		if (!entry)
			return ""
		if (entry.kind)
			return String(entry.kind)
		return entry.separator ? "separator" : "action"
	}

	function submenuEntries(entry) {
		if (!entry)
			return []
		// C++ QVariantList values are array-like in QML but are not guaranteed to
		// satisfy JavaScript's Array.isArray after crossing a typed property.
		if (entry.items && entry.items.length !== undefined)
			return entry.items
		if (entry.submenu && entry.submenu.length !== undefined)
			return entry.submenu
		if (entry.submenu && entry.submenu.items && entry.submenu.items.length !== undefined)
			return entry.submenu.items
		if (entry.submenu && entry.submenu.entries && entry.submenu.entries.length !== undefined)
			return entry.submenu.entries
		return []
	}

	function isSubmenu(entry) {
		return entryKind(entry) === "submenu"
			|| (!!entry && (!!entry.hasSubmenu || !!entry.submenu || !!entry.items)
				&& submenuEntries(entry).length > 0)
	}

	function normalizeEntries(sourceEntries) {
		const output = []
		let previousWasSeparator = true
		const source = sourceEntries || []
		for (let index = 0; index < source.length; ++index) {
			const entry = source[index] || ({})
			if (entry.visible !== undefined && !entry.visible)
				continue
			const kind = entryKind(entry)
			if (kind === "submenu" || isSubmenu(entry)) {
				const children = normalizeEntries(submenuEntries(entry))
				if (children.length === 0)
					continue
				const copy = Object.assign({}, entry)
				copy.kind = "submenu"
				copy.items = children
				copy.hasSubmenu = true
				output.push(copy)
				previousWasSeparator = false
				continue
			}
			if (kind === "separator") {
				if (!previousWasSeparator) {
					output.push(entry)
					previousWasSeparator = true
				}
				continue
			}
			output.push(entry)
			previousWasSeparator = kind === "section" || kind === "header"
		}
		while (output.length > 0 && entryKind(output[output.length - 1]) === "separator")
			output.pop()
		return output
	}

	function submenuPayload(entry) {
		const result = Object.assign({}, entry || ({}))
		result.kind = "submenu"
		result.hasSubmenu = true
		result.enabled = result.enabled === undefined ? true : !!result.enabled
		return result
	}

	function stableEntryKey(index, entry) {
		const kind = isSubmenu(entry) ? "submenu" : entryKind(entry)
		const id = String((entry || ({})).id || "")
		// IDs are the command ABI and therefore the preferred identity. Positional
		// identity is intentionally limited to decorative rows that have no ID.
		return kind + ":" + (id.length > 0 ? id : "row-" + index)
	}

	function updateEntry(record, index, entry) {
		if (!record || !record.object)
			return
		record.key = stableEntryKey(index, entry)
		if (record.submenu) {
			const child = record.object
			child.title = String(entry.label || entry.id || "")
			child.objectName = "payloadSubmenu-" + String(menu.objectName || "menu")
				+ "-" + String(entry.id || index)
			child.menuPayload = submenuPayload(entry)
			child.enabled = entry.enabled === undefined || !!entry.enabled
			child.maximumHeight = maximumHeight
			child.closeOnAction = closeOnAction
			child.entries = submenuEntries(entry)
			if (!child.enabled && child.visible)
				child.close()
			return
		}
		record.object.payload = entry
		record.object.objectName = "payloadMenuEntry-" + String(menu.objectName || "menu")
			+ "-" + String(entry.id || index)
	}

	function submenuComponent() {
		if (sharedSubmenuComponent)
			return sharedSubmenuComponent
		if (parentMenu && parentMenu["submenuComponent"] !== undefined) {
			sharedSubmenuComponent = parentMenu.submenuComponent()
			return sharedSubmenuComponent
		}
		sharedSubmenuComponent = Qt.createComponent(Qt.resolvedUrl("PayloadMenu.qml"),
			Component.PreferSynchronous)
		return sharedSubmenuComponent
	}

	function createEntry(index, entry) {
		if (isSubmenu(entry)) {
			const component = submenuComponent()
			if (!component || component.status !== Component.Ready) {
				console.warn("Unable to create Modern submenu:",
					component ? component.errorString() : "component unavailable")
				return null
			}
			const child = component.createObject(menu, {
				"title": String(entry.label || entry.id || ""),
				"objectName": "payloadSubmenu-" + String(menu.objectName || "menu")
					+ "-" + String(entry.id || index),
				"menuPayload": submenuPayload(entry),
				"parentMenu": menu,
				"entries": submenuEntries(entry),
				"maximumHeight": maximumHeight,
				"closeOnAction": closeOnAction,
				"enabled": entry.enabled === undefined || !!entry.enabled,
				"sharedSubmenuComponent": component
			})
			if (!child) {
				console.warn("Unable to create Modern submenu")
				return null
			}
			// Qt.createComponent() completes this recursive type synchronously. Assign
			// the child list once more after completion so older Qt Quick revisions do
			// not restore the declarative [] default over the initial-property map.
			child.entries = submenuEntries(entry)
			child.actionRequested.connect(function(actionId, payload) {
				menu.actionRequested(actionId, payload)
			})
			child.valueRequested.connect(function(actionId, value, finalValue, payload) {
				menu.valueRequested(actionId, value, finalValue, payload)
			})
			child.closed.connect(function() {
				if (menu.activeSubmenu === child)
					menu.activeSubmenu = null
			})
			menu.insertMenu(index, child)
			entryObjectCreationCount += 1
			return {
				"object": child,
				"submenu": true,
				"key": stableEntryKey(index, entry)
			}
		}

		const item = payloadItemFactory.createObject(menu.contentItem, {
			"payload": entry,
			"objectName": "payloadMenuEntry-" + String(menu.objectName || "menu")
				+ "-" + String(entry.id || index)
		})
		if (!item)
			return null
		item.actionRequested.connect(function(actionId) {
			menu.actionRequested(actionId, entry)
			if (menu.closeOnAction)
				menu.dismiss()
		})
		item.valueRequested.connect(function(actionId, value, finalValue) {
			menu.valueRequested(actionId, value, finalValue, entry)
		})
		menu.insertItem(index, item)
		entryObjectCreationCount += 1
		return {
			"object": item,
			"submenu": false,
			"key": stableEntryKey(index, entry)
		}
	}

	function destroyEntry(record) {
		if (!record || !record.object)
			return
		if (record.submenu)
			menu.removeMenu(record.object)
		else
			menu.removeItem(record.object)
		record.object.destroy()
	}

	function canUpdateEntriesInPlace(nextEntries) {
		if (generatedEntries.length !== nextEntries.length)
			return false
		for (let index = 0; index < nextEntries.length; ++index) {
			const record = generatedEntries[index]
			if (!record || !record.object
					|| record.key !== stableEntryKey(index, nextEntries[index])
					|| record.submenu !== isSubmenu(nextEntries[index]))
				return false
		}
		return true
	}

	function reconcileEntries() {
		reconcilePending = false
		const nextEntries = normalizedEntries || []
		if (canUpdateEntriesInPlace(nextEntries)) {
			for (let index = 0; index < nextEntries.length; ++index)
				updateEntry(generatedEntries[index], index, nextEntries[index])
			return
		}

		for (let index = generatedEntries.length - 1; index >= 0; --index)
			destroyEntry(generatedEntries[index])
		const nextRecords = []
		for (let index = 0; index < nextEntries.length; ++index) {
			const record = createEntry(index, nextEntries[index])
			if (record)
				nextRecords.push(record)
		}
		generatedEntries = nextRecords
		entryTreeRebuildCount += 1
	}

	function scheduleReconcile() {
		if (!componentCompleted || reconcilePending)
			return
		reconcilePending = true
		Qt.callLater(function() {
			if (menu.componentCompleted)
				menu.reconcileEntries()
		})
	}

	Component {
		id: payloadItemFactory
		PayloadMenuItem { }
	}

	onNormalizedEntriesChanged: {
		if (componentCompleted) {
			// Existing trees update synchronously. A child created by the shared
			// QQmlComponent defers only its first recursive expansion so the same
			// compiled component is never re-entered while createObject() is active.
			if (generatedEntries.length === 0 && parentMenu)
				scheduleReconcile()
			else
				reconcileEntries()
		}
	}
	Component.onCompleted: {
		componentCompleted = true
		if (parentMenu)
			scheduleReconcile()
		else
			reconcileEntries()
	}
}
