import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Menu {
	id: menu
	// PayloadMenu uses Qt Quick Controls' native submenu relationship instead of
	// painting a disconnected chevron. Keeping the payload on the child menu lets
	// this delegate preserve typed IDs and metadata without flattening the tree.
	property var menuPayload: ({})
	property var openerItem: null
	property var parentMenu: null
	property var activeSubmenu: null
	// Pointer hover and keyboard focus deliberately use different emphasis. A
	// pointer-opened child menu must not leave a bright focus ring behind on its
	// parent row, while keyboard navigation must retain the focus indicator.
	property bool pointerNavigationActive: false
	property int submenuOpenRequestCount: 0
	property string accessibleName: title.length > 0 ? title : qsTr("Menu")
	readonly property var windowActiveFocusItem: contentItem && contentItem.Window.window
		? contentItem.Window.window.activeFocusItem : null
	readonly property bool menuEntryOwnsActiveFocus: {
		const focusedItem = windowActiveFocusItem
		if (!focusedItem)
			return false
		for (let index = 0; index < count; ++index) {
			const candidate = itemAt(index)
			let ancestor = focusedItem
			while (candidate && ancestor) {
				if (ancestor === candidate)
					return true
				ancestor = ancestor.parent
			}
		}
		return false
	}
	readonly property bool surfaceOwnsActiveFocus: activeFocus
		&& !menuEntryOwnsActiveFocus
		&& !(activeSubmenu && activeSubmenu.visible)
	modal: false
	dim: false
	focus: true
	cascade: true
	overlap: -Theme.space1
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
		border.color: menu.surfaceOwnsActiveFocus && !menu.pointerNavigationActive
			? Theme.focus : Theme.popupBorder
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
	Binding {
		target: menu.contentItem
		property: "Accessible.role"
		value: Accessible.PopupMenu
		when: menu.contentItem !== null
	}
	Binding {
		target: menu.contentItem
		property: "Accessible.name"
		value: menu.accessibleName
		when: menu.contentItem !== null
	}

	function focusInitialItem() {
		for (let index = 0; index < count; ++index) {
			const candidate = itemAt(index)
			if (candidate && candidate.visible && candidate.enabled) {
				currentIndex = index
				candidate.forceActiveFocus()
				return candidate
			}
		}
		return null
	}
	onOpened: {
		// Popup instances are reused. Seed the current input modality from the
		// owning cascade instead of leaking a hover state from an earlier opening.
		pointerNavigationActive = parentMenu ? parentMenu.pointerNavigationActive : false
	}

	function setPointerNavigationActive(value) {
		pointerNavigationActive = !!value
		if (parentMenu && parentMenu["setPointerNavigationActive"] !== undefined)
			parentMenu["setPointerNavigationActive"](value)
	}

	function openWithInitialFocus() {
		open()
		Qt.callLater(function() {
			// QQuickMenu may already have placed focus, or the user may have moved it
			// during the queued popup-layout turn. Only seed an otherwise unfocused
			// menu so opening never resets a valid roving selection.
			if (menu.visible && !menu.menuEntryOwnsActiveFocus)
				menu.focusInitialItem()
		})
		return visible
	}

	function effectivePopupHeight(targetMenu) {
		if (!targetMenu)
			return 0
		const contentHeight = targetMenu.contentItem
			? Number(targetMenu.contentItem.height || 0) : 0
		return Math.max(Number(targetMenu.height || 0),
			Number(targetMenu.implicitHeight || 0), contentHeight + Theme.space1 * 2)
	}

	function configureLogicalViewport(targetMenu, hostItem, inset, bottomInset) {
		if (!targetMenu || !hostItem)
			return
		const safeInset = Math.max(0, Number(inset || 0))
		const safeBottomInset = Math.max(safeInset, Number(bottomInset || safeInset))
		if (targetMenu["logicalViewportWidth"] !== undefined)
			targetMenu.logicalViewportWidth = Math.max(1, hostItem.width - safeInset * 2)
		if (targetMenu["logicalViewportHeight"] !== undefined)
			targetMenu.logicalViewportHeight = Math.max(1,
				hostItem.height - safeInset - safeBottomInset)
	}

	function positionAtLogicalPoint(targetMenu, hostItem, logicalPoint, inset, bottomInset) {
		if (!targetMenu || !hostItem)
			return false
		// Window/contentItem geometry and mapToItem() are already logical pixels on
		// Windows, including mixed-DPI monitors. Applying devicePixelRatio here is a
		// correctness bug: the scene graph handles the native conversion afterwards.
		const safeInset = Math.max(0, Number(inset || 0))
		const safeBottomInset = Math.max(safeInset, Number(bottomInset || safeInset))
		configureLogicalViewport(targetMenu, hostItem, safeInset, safeBottomInset)
		const targetWidth = Math.min(Number(targetMenu.width || 0),
			Math.max(1, hostItem.width - safeInset * 2))
		const targetHeight = Math.min(effectivePopupHeight(targetMenu),
			Math.max(1, hostItem.height - safeInset - safeBottomInset))
		const pointX = logicalPoint && logicalPoint.x !== undefined
			? Number(logicalPoint.x) : safeInset
		const pointY = logicalPoint && logicalPoint.y !== undefined
			? Number(logicalPoint.y) : safeInset
		targetMenu.x = Math.round(Math.max(safeInset,
			Math.min(hostItem.width - targetWidth - safeInset, pointX)))
		targetMenu.y = Math.round(Math.max(safeInset,
			Math.min(hostItem.height - targetHeight - safeBottomInset, pointY)))
		return true
	}

	function openAtLogicalPoint(hostItem, logicalPoint, inset, bottomInset) {
		if (!hostItem)
			return false
		parent = hostItem
		const place = function() {
			menu.positionAtLogicalPoint(menu, hostItem, logicalPoint, inset, bottomInset)
		}
		place()
		openWithInitialFocus()
		// QQuickMenu performs an internal placement pass in open(). Restore the
		// product inset immediately, then once after dynamic row geometry settles.
		place()
		Qt.callLater(function() {
			if (menu.visible)
				place()
		})
		return visible
	}

	function positionCascadeInLogicalPixels(opener, childMenu) {
		if (!opener || !childMenu || !opener.Window.window
				|| !opener.Window.window.contentItem)
			return false
		const hostItem = opener.Window.window.contentItem
		const inset = Theme.space1
		configureLogicalViewport(childMenu, hostItem, inset,
			inset + Theme.elevationMenuOffset)
		const openerPosition = opener.mapToItem(hostItem, 0, 0)
		const currentPosition = childMenu.contentItem
			? childMenu.contentItem.mapToItem(hostItem, 0, 0)
			: Qt.point(Number(childMenu.x || 0), Number(childMenu.y || 0))
		const childWidth = Math.min(Number(childMenu.width || 0),
			Math.max(1, hostItem.width - inset * 2))
		const childHeight = Math.min(effectivePopupHeight(childMenu),
			Math.max(1, hostItem.height - inset * 2 - Theme.elevationMenuOffset))
		const rightX = openerPosition.x + opener.width - Theme.space1
		const leftX = openerPosition.x - childWidth + Theme.space1
		const desiredX = rightX + childWidth + inset <= hostItem.width
			? rightX : Math.max(inset, leftX)
		const desiredY = Math.max(inset, Math.min(openerPosition.y,
			hostItem.height - childHeight - inset - Theme.elevationMenuOffset))
		// Adjust by the scene-space delta. The popup parent may differ between Qt
		// 6 releases, while this delta remains valid for item-backed popups.
		childMenu.x += Math.round(desiredX - currentPosition.x)
		childMenu.y += Math.round(desiredY - currentPosition.y)
		return true
	}

	function focusMenuItem(target) {
		if (!target || !target.visible || !target.enabled)
			return false
		for (let index = 0; index < count; ++index) {
			if (itemAt(index) === target) {
				currentIndex = index
				break
			}
		}
		target.forceActiveFocus()
		return true
	}

	function openSubmenuFor(opener, childMenu, focusChild) {
		if (!opener || !childMenu || !opener.enabled)
			return false
		// PayloadMenu deliberately defers the first recursive expansion so its
		// shared QQmlComponent is never re-entered while the parent is being built.
		// A real first hover/keyboard open can arrive in the queued turn before that
		// expansion. Finish the already-local reconciliation here so the submenu is
		// populated and focusable in the same input turn; no I/O or backend work is
		// involved.
		if (childMenu["reconcilePending"] === true
				&& childMenu["componentCompleted"] === true
				&& childMenu["reconcileEntries"] !== undefined)
			childMenu["reconcileEntries"]()
		submenuOpenRequestCount += 1
		const previous = activeSubmenu
		activeSubmenu = childMenu
		childMenu.openerItem = opener
		childMenu.parentMenu = menu
		// Closing a sibling used to wait for its 90 ms exit transition before the
		// new submenu could open. Open the replacement in the same input turn; Qt
		// Quick safely owns both transition lifetimes.
		if (previous && previous !== childMenu && previous.visible)
			previous.close()
		if (activeSubmenu !== childMenu)
			return false
		childMenu.popup(opener, opener)
		positionCascadeInLogicalPixels(opener, childMenu)
		Qt.callLater(function() {
			if (menu.activeSubmenu === childMenu && childMenu.visible)
				menu.positionCascadeInLogicalPixels(opener, childMenu)
		})
		if (focusChild)
			Qt.callLater(function() { childMenu.focusInitialItem() })
		return true
	}

	function closeToOpener() {
		if (!openerItem)
			return false
		close()
		return true
	}

	function usableExternalFocusItem(candidate) {
		// Keep the item-level focus chain intact while the application window is
		// inactive. forceActiveFocus() does not activate the OS window; it only
		// prepares the opener to receive active focus when that window is active.
		return candidate && candidate.enabled !== false && candidate.forceActiveFocus
	}

	function ownsFocusItem(candidate) {
		let current = candidate
		while (current) {
			if (current === contentItem)
				return true
			current = current.parent
		}
		return false
	}

	function hasActionableEntries(entries) {
		const source = entries || []
		for (let index = 0; index < source.length; ++index) {
			const entry = source[index] || ({})
			if (entry.visible === false || entry.enabled === false)
				continue
			const kind = String(entry.kind || "action").trim().toLowerCase()
			if (kind === "separator" || kind === "label" || kind === "header"
					|| kind === "section")
				continue
			if (kind === "submenu") {
				const nested = entry.items || entry.entries
					|| (entry.submenu && (entry.submenu.items || entry.submenu.entries)) || []
				if (hasActionableEntries(nested))
					return true
				continue
			}
			return true
		}
		return false
	}

	onClosed: {
		if (activeSubmenu && activeSubmenu.visible)
			activeSubmenu.close()
		activeSubmenu = null
		const target = openerItem
		const ownerMenu = parentMenu
		const shouldRestore = target && (!parentMenu || !parentMenu.activeSubmenu
			|| parentMenu.activeSubmenu === menu)
		if (shouldRestore) {
			Qt.callLater(function() {
				// Let QQuickMenu finish its own close/focus bookkeeping before restoring
				// the opener's roving index. A second queued turn is stable across Qt
				// 6.2-6.9 and avoids focus falling back to the menu surface.
				Qt.callLater(function() {
					if (ownerMenu) {
						if (ownerMenu.visible)
							ownerMenu.focusMenuItem(target)
					} else if (menu.usableExternalFocusItem(target)) {
						// An action can close the menu and synchronously open a dialog or
						// move focus into another product surface. In that case the new
						// destination wins; only restore the opener when focus is still
						// unowned (Escape/outside-click) or Qt already restored it there.
						const current = menu.windowActiveFocusItem
						if (current && current !== target && !menu.ownsFocusItem(current))
							return
						target.forceActiveFocus(Qt.OtherFocusReason)
					}
				})
			})
		}
	}

	delegate: Component {
		PayloadMenuItem {
			payload: {
				const childMenu = subMenu
				if (childMenu && childMenu["menuPayload"] !== undefined)
					return childMenu["menuPayload"]
				return {
					"kind": "submenu",
					"label": childMenu ? childMenu.title : text,
					"enabled": childMenu ? childMenu.enabled : true,
					"hasSubmenu": true
				}
			}
		}
	}
}
