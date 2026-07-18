import QtQuick
import QtQuick.Window
import QtTest

TestCase {
	id: testCase
	name: "ModalAccessibilityBarrier"
	when: windowShown
	width: 480
	height: 320

	property bool boundLeafIgnored: false
	property int initialVirtualRowCount: 2

	Window {
		id: semanticWindow
		width: 480
		height: 320
		visible: true
		title: "Modal accessibility fixture"
	}

	Loader {
		id: barrierLoader
		source: "qrc:/qml-shell/ModalAccessibilityBarrier.qml"
	}

	Item {
		id: background
		parent: semanticWindow.contentItem
		objectName: "backgroundProductScene"
		anchors.fill: parent
		Accessible.role: Accessible.Pane
		Accessible.name: "Background product scene"

		Item {
			id: boundLeaf
			objectName: "boundBackgroundLeaf"
			Accessible.role: Accessible.Button
			Accessible.name: "Background action"
			Accessible.ignored: testCase.boundLeafIgnored
		}

		Item {
			id: nestedContainer
			Item {
				id: nestedLeaf
				objectName: "nestedBackgroundLeaf"
				Accessible.role: Accessible.EditableText
				Accessible.name: "Background field"
			}
		}

		ListModel {
			id: virtualRowModel
			ListElement { label: "Lobby" }
			ListElement { label: "Alex" }
		}

		ListView {
			id: virtualRows
			objectName: "virtualBackgroundRows"
			width: 240
			height: 120
				model: virtualRowModel
				delegate: Item {
					id: virtualRowDelegate
					required property string label
				property bool accessibilityPooled: false
				property bool navigationVisible: true
				width: virtualRows.width
				height: 36
				visible: navigationVisible
					Accessible.role: Accessible.ListItem
					Accessible.name: label
					Binding {
						target: virtualRowDelegate
						property: "Accessible.ignored"
						value: virtualRowDelegate.accessibilityPooled
							|| !virtualRowDelegate.navigationVisible
					}
				}
		}

		Item {
			id: hostedContentOwner
			// Models/Flickables may host their delegates through a contentItem that
			// is not part of the owner's ordinary children list.
			property Item contentItem: detachedContentItem
		}
	}

	Item {
		id: detachedContentItem
		parent: semanticWindow.contentItem
		objectName: "detachedHostedContent"
		width: 120
		height: 32
		Accessible.role: Accessible.ListItem
		Accessible.name: "Hosted room"
	}

	Item {
		id: modalSurface
		parent: semanticWindow.contentItem
		objectName: "modalSurface"
		Accessible.role: Accessible.Dialog
		Accessible.name: "Active modal"

		Item {
			id: modalAction
			objectName: "modalAction"
			Accessible.role: Accessible.Button
			Accessible.name: "Confirm"
		}
	}

	Component {
		id: lateLeafComponent
		Item {
			objectName: "lateBackgroundLeaf"
			Accessible.role: Accessible.Button
			Accessible.name: "Late background action"
		}
	}

	function init() {
		tryCompare(barrierLoader, "status", Loader.Ready)
		barrierLoader.item.active = false
		barrierLoader.item.targets = [ background ]
		boundLeafIgnored = false
		wait(0)
	}

	function cleanup() {
		barrierLoader.item.active = false
		barrierLoader.item.targets = []
		boundLeafIgnored = false
		while (virtualRowModel.count > initialVirtualRowCount)
			virtualRowModel.remove(virtualRowModel.count - 1)
		wait(0)
	}

	function test_modal_background_is_suppressed_and_original_bindings_are_restored() {
		const barrier = barrierLoader.item
		tryVerify(function() { return !!virtualRows.itemAtIndex(0) && !!virtualRows.itemAtIndex(1) })
		const firstVirtualRow = virtualRows.itemAtIndex(0)
		tryVerify(function() {
			const names = accessibilityProbe.visibleNames()
			return names.indexOf("Lobby") >= 0 && names.indexOf("Hosted room") >= 0
		})
		verify(!background.Accessible.ignored)
		verify(!boundLeaf.Accessible.ignored)
		verify(!nestedLeaf.Accessible.ignored)
		verify(!firstVirtualRow.Accessible.ignored)
		verify(!detachedContentItem.Accessible.ignored)
		verify(!modalSurface.Accessible.ignored)
		verify(!modalAction.Accessible.ignored)

		barrier.active = true
		tryVerify(function() {
			return background.Accessible.ignored && boundLeaf.Accessible.ignored
				&& nestedContainer.Accessible.ignored && nestedLeaf.Accessible.ignored
				&& firstVirtualRow.Accessible.ignored && detachedContentItem.Accessible.ignored
		})
		tryVerify(function() {
			const names = accessibilityProbe.visibleNames()
			return names.indexOf("Lobby") < 0 && names.indexOf("Hosted room") < 0
		})
		firstVirtualRow.accessibilityPooled = true
		firstVirtualRow.accessibilityPooled = false
		tryVerify(function() { return firstVirtualRow.Accessible.ignored })
		verify(!modalSurface.Accessible.ignored)
		verify(!modalAction.Accessible.ignored)

		const previousChildCount = background.children.length
		const lateLeaf = createTemporaryObject(lateLeafComponent, background)
		verify(lateLeaf)
		compare(lateLeaf.parent, background)
		compare(background.children.length, previousChildCount + 1)
		wait(0)
		verify(barrier.bindingFor(lateLeaf) !== null,
			"A late visual child must receive a temporary accessibility binding")
		tryVerify(function() { return lateLeaf.Accessible.ignored })
		virtualRowModel.append({ "label": "Late room" })
		tryVerify(function() { return !!virtualRows.itemAtIndex(2) })
		const lateVirtualRow = virtualRows.itemAtIndex(2)
		tryVerify(function() { return lateVirtualRow.Accessible.ignored })

		barrier.active = false
		tryVerify(function() {
			return !background.Accessible.ignored && !boundLeaf.Accessible.ignored
				&& !nestedContainer.Accessible.ignored && !nestedLeaf.Accessible.ignored
				&& !firstVirtualRow.Accessible.ignored && !lateVirtualRow.Accessible.ignored
				&& !detachedContentItem.Accessible.ignored
		})
		tryVerify(function() {
			const names = accessibilityProbe.visibleNames()
			return names.indexOf("Lobby") >= 0 && names.indexOf("Hosted room") >= 0
		})
		verify(!lateLeaf.Accessible.ignored)

		// Destroying the temporary modal override must restore the original QML
		// binding, not only the value that happened to be active when it opened.
		boundLeafIgnored = true
		tryCompare(boundLeaf.Accessible, "ignored", true)
		boundLeafIgnored = false
		tryCompare(boundLeaf.Accessible, "ignored", false)
	}

	function test_reassert_repairs_itemview_overwrite_and_preserves_original_binding() {
		const barrier = barrierLoader.item
		tryVerify(function() { return !!virtualRows.itemAtIndex(0) })
		const row = virtualRows.itemAtIndex(0)
		verify(!row.Accessible.ignored)

		barrier.active = true
		tryCompare(row.Accessible, "ignored", true)
		// Simulate Qt 6.9 QQuickItemViewFxItem::setVisible(), which writes the
		// private isAccessible flag after the temporary Binding evaluated.
		row.Accessible.ignored = false
		compare(row.Accessible.ignored, false)
		verify(barrier.reassertItem(row))
		compare(row.Accessible.ignored, true)

		barrier.active = false
		tryCompare(row.Accessible, "ignored", false)
		row.accessibilityPooled = true
		tryCompare(row.Accessible, "ignored", true)
		row.accessibilityPooled = false
		tryCompare(row.Accessible, "ignored", false)
	}

	function test_pending_reassert_is_cancelled_safely_when_modal_closes() {
		const barrier = barrierLoader.item
		tryVerify(function() { return !!virtualRows.itemAtIndex(0) })
		const row = virtualRows.itemAtIndex(0)
		verify(!row.Accessible.ignored)

		barrier.active = true
		tryCompare(row.Accessible, "ignored", true)
		const wrapper = barrier.bindingFor(row)
		verify(wrapper !== null)
		// This emits ignoredChanged and queues the wrapper's reassertion. Close the
		// modal in the same turn, before that queued callback can run.
		row.Accessible.ignored = false
		compare(row.Accessible.ignored, false)
		compare(wrapper.reassertPending, true)
		barrier.active = false
		wait(0)

		// The deferred callback must neither revive the modal override nor replace
		// the delegate's ordinary pooled-state binding after its wrapper is gone.
		tryCompare(row.Accessible, "ignored", false)
		row.accessibilityPooled = true
		tryCompare(row.Accessible, "ignored", true)
		row.accessibilityPooled = false
		tryCompare(row.Accessible, "ignored", false)
	}

	function test_persistent_target_reuses_bindings_across_modal_reopen() {
		const barrier = barrierLoader.item
		barrier.active = true
		tryVerify(function() { return barrier.bindingFor(background) !== null })
		const backgroundBinding = barrier.bindingFor(background)
		const initialBindingCount = barrier.activeBindings.length
		verify(initialBindingCount > 0)

		barrier.active = false
		tryCompare(background.Accessible, "ignored", false)
		compare(barrier.bindingFor(background), backgroundBinding,
			"Closing a modal must retain the persistent target's wrapper for reuse")

		barrier.active = true
		tryCompare(background.Accessible, "ignored", true)
		compare(barrier.bindingFor(background), backgroundBinding)
		compare(barrier.activeBindings.length, initialBindingCount,
			"Reopening the same target must not rebuild or grow the binding graph")
	}
}
