import QtQuick

// Accessible.ignored only removes the item it is attached to. Accessible
// descendants are promoted instead of being pruned, so a modal host needs to
// make every background item inert. Temporary Binding objects preserve and
// restore each item's original value or binding when the modal surface closes.
Item {
	id: root

	property bool active: false
	property var targets: []
	property var activeBindings: []
	property var activeBindingMap: new Map()
	property var visitedItemGenerations: new Map()
	property bool refreshPending: false
	readonly property int bindingCount: activeBindings.length

	width: 0
	height: 0
	visible: false
	Accessible.ignored: true

	Component {
		id: ignoredBindingComponent

		Item {
			id: scopeBinding
			required property Item scopeItem
			property var ownerBarrier: root
			property bool overrideActive: true
			property bool reassertPending: false
			width: 0
			height: 0
			visible: false
			Accessible.ignored: true

			Timer {
				id: reassertTimer
				interval: 0
				repeat: false
				onTriggered: {
					scopeBinding.reassertPending = false
					const owner = scopeBinding.ownerBarrier
					if (owner && owner.active && scopeBinding.overrideActive && scopeBinding.scopeItem
							&& !scopeBinding.scopeItem.Accessible.ignored)
						owner.reassertItem(scopeBinding.scopeItem)
				}
			}

			function scheduleReassert() {
				const owner = ownerBarrier
				if (reassertPending || !owner || !owner.active || !overrideActive || !scopeItem)
					return
				reassertPending = true
				reassertTimer.restart()
			}

			Binding {
				target: scopeBinding.scopeItem
				property: "Accessible.ignored"
				value: true
				when: scopeBinding.overrideActive && !!scopeBinding.scopeItem
				restoreMode: Binding.RestoreBindingOrValue
			}

			Connections {
				target: scopeBinding.scopeItem
				function onChildrenChanged() {
					const owner = scopeBinding.ownerBarrier
					if (owner)
						owner.scheduleRefresh()
				}
			}

			// Product bindings may legitimately update their ordinary ignored state
			// while a modal is open (for example when an ItemView delegate enters or
			// leaves its pool). Keep the temporary modal binding authoritative, then
			// let RestoreBindingOrValue expose the latest product state on release.
			// Qt's private ItemView overwrite does not emit this signal and is handled
			// separately by the bounded materialized-delegate reassertion in the rail.
			Connections {
				target: scopeBinding.scopeItem ? scopeBinding.scopeItem.Accessible : null
				function onIgnoredChanged() {
					if (scopeBinding.overrideActive && scopeBinding.scopeItem
							&& !scopeBinding.scopeItem.Accessible.ignored)
						scopeBinding.scheduleReassert()
				}
			}
		}
	}

	Timer {
		id: refreshTimer
		interval: 0
		repeat: false
		onTriggered: {
			root.refreshPending = false
			if (root.active)
				root.refresh()
		}
	}

	function bindingFor(item) {
		return item ? (activeBindingMap.get(item) || null) : null
	}

	// QQuickItemViewFxItem::setVisible() writes the delegate's private
	// isAccessible flag directly in Qt 6.9, bypassing an otherwise-active QML
	// Binding. Re-arm this barrier's temporary Binding without replacing the
	// original expression captured by RestoreBindingOrValue.
	function reassertItem(item) {
		if (!active || !item)
			return false
		let binding = bindingFor(item)
		if (!binding) {
			suppressItem(item, [])
			binding = bindingFor(item)
		}
		if (!binding)
			return false
		try {
			if (item.Accessible.ignored)
				return true
		} catch (error) {
			return false
		}
		// Both transitions are synchronous. No platform query can observe the
		// restored value between them, while the second edge reapplies true.
		binding.overrideActive = false
		binding.overrideActive = true
		return item.Accessible.ignored
	}

	function hostedItem(item, propertyName) {
		try {
			const candidate = item[propertyName]
			return candidate && candidate !== item ? candidate : null
		} catch (error) {
			return null
		}
	}

	function suppressItem(item) {
		// A delegate-local barrier may need to suppress its owning ItemView row.
		// That target contains this helper as a visual child, so stop traversal at
		// the helper itself instead of recursively trying to bind the barrier.
		if (!item || item === root)
			return
		if (visitedItemGenerations.has(item))
			return
		visitedItemGenerations.set(item, true)
		const existingBinding = bindingFor(item)
		if (!existingBinding) {
			const binding = ignoredBindingComponent.createObject(root, { "scopeItem": item })
			if (binding) {
				activeBindings.push({ "item": item, "binding": binding })
				activeBindingMap.set(item, binding)
			}
		} else {
			// ItemView may overwrite its delegate's private accessibility flag
			// after this barrier first installed the Binding. A deferred refresh must
			// therefore re-arm existing bindings as well as discover late children.
			// Descendants normally remain true, so this is a cheap no-op for them.
			try {
				if (!item.Accessible.ignored)
					reassertItem(item)
			} catch (error) {
				// The visual item may have been destroyed between childrenChanged and
				// the deferred traversal. The next refresh prunes the stale binding.
			}
		}
		const children = item.children || []
		for (let index = 0; index < children.length; ++index)
			suppressItem(children[index])

		// Flickable and ItemView delegates live below a separately hosted
		// contentItem on some Qt Quick backends. Loader and Popup-like hosts have
		// the same shape. These items are not guaranteed to appear in children,
		// so traverse the public host links explicitly and rely on the traversal
		// visited map to avoid binding the same visual item twice.
		const hostedPropertyNames = [ "contentItem", "item", "headerItem", "footerItem", "popupItem" ]
		for (let index = 0; index < hostedPropertyNames.length; ++index)
			suppressItem(hostedItem(item, hostedPropertyNames[index]))
	}

	function scheduleRefresh() {
		if (!active || refreshPending)
			return
		refreshPending = true
		refreshTimer.restart()
	}

	function refresh() {
		if (!active)
			return
		// Keep existing Binding objects alive. Rebuilding the complete set would
		// briefly restore the background accessibility tree and is expensive for
		// the persistent product scene when one modal is replaced by another.
		// Each bound item also observes childrenChanged, so late model delegates are
		// picked up once on the next event-loop turn without a polling traversal.
		// Keep traversal membership bounded to the current visual graph. A
		// persistent Map would retain every transient ItemView delegate ever seen.
		visitedItemGenerations = new Map()
		for (let index = 0; index < targets.length; ++index) {
			suppressItem(targets[index])
		}
		// Pooled rows and nested prompts can reuse a barrier with a different target.
		// Retain only wrappers reached from the current graph so reuse cannot retain
		// stale item references indefinitely.
		const retainedBindings = []
		for (let index = 0; index < activeBindings.length; ++index) {
			const entry = activeBindings[index]
			if (entry && entry.item && entry.binding
					&& visitedItemGenerations.has(entry.item)) {
				retainedBindings.push(entry)
			} else if (entry && entry.binding) {
				if (entry.item)
					activeBindingMap.delete(entry.item)
				entry.binding.overrideActive = false
				entry.binding.destroy()
			}
		}
		if (retainedBindings.length !== activeBindings.length)
			activeBindings = retainedBindings
	}

	function release(destroyBindings) {
		refreshTimer.stop()
		refreshPending = false
		const bindings = activeBindings
		if (destroyBindings) {
			activeBindings = []
			activeBindingMap.clear()
			visitedItemGenerations.clear()
		}
		for (let index = bindings.length - 1; index >= 0; --index) {
			const binding = bindings[index] ? bindings[index].binding : null
			if (binding) {
				// Disable the Binding while its target is still alive so
				// RestoreBindingOrValue can reinstate the original expression before
				// either reuse or final destruction.
				binding.overrideActive = false
				if (destroyBindings)
					binding.destroy()
			}
		}
	}

	onActiveChanged: {
		if (active) {
			refresh()
			scheduleRefresh()
		} else {
			release(false)
		}
	}
	onTargetsChanged: {
		if (active) {
			release(false)
			refresh()
			scheduleRefresh()
		}
	}
	Component.onDestruction: release(true)
}
