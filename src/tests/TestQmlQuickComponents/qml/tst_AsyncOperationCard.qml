import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "AsyncOperationCard"
	when: windowShown
	visible: true
	width: 420
	height: 320
	property var currentCard: null

	SignalSpy {
		id: cancelSpy
		target: testCase.currentCard
		signalName: "cancelRequested"
	}

	SignalSpy {
		id: dismissSpy
		target: testCase.currentCard
		signalName: "dismissRequested"
	}

	function loadCard(overrides) {
		const properties = {
			"stableId": "plugin-update:test",
			"title": "Updating plugins",
			"subtitle": "Downloading and applying selected updates",
			"status": "running",
			"payload": {
				"cancellable": true,
				"phase": "download",
				"completedItems": 2,
				"totalItems": 8,
				"progress": 25,
				"itemResultCount": 0,
				"itemResultRevision": 0
			},
			"width": 380,
			"maximumHeight": 180,
			"narrowLayout": true
		}
		for (const key in overrides)
			properties[key] = overrides[key]
		const component = Qt.createComponent("qrc:/qml-shell/AsyncOperationCard.qml")
		tryCompare(component, "status", Component.Ready)
		currentCard = createTemporaryObject(component, testCase, properties)
		verify(currentCard !== null)
		currentCard.x = Math.round((testCase.width - currentCard.width) / 2)
		currentCard.y = Math.round((testCase.height - currentCard.height) / 2)
		cancelSpy.clear()
		dismissSpy.clear()
		return currentCard
	}

	function cleanup() {
		currentCard = null
		wait(0)
	}

	function test_narrow_card_starts_compact_and_remains_bounded_when_expanded() {
		const card = loadCard({})
		compare(card.expanded, false)
		const compactHeight = card.height
		verify(compactHeight < 100)

		const expandButton = findChild(card, "operationExpandButton")
		verify(expandButton !== null)
		compare(expandButton.Accessible.name, "Expand Updating plugins")
		mouseClick(expandButton)
		tryCompare(card, "expanded", true)
		tryVerify(function() { return card.height > compactHeight })
		verify(card.height <= card.maximumHeight)
		compare(expandButton.Accessible.name, "Collapse Updating plugins")
	}

	function test_successful_terminal_card_starts_compact_on_wide_layout() {
		const card = loadCard({
			"status": "succeeded",
			"narrowLayout": false,
			"payload": {
				"cancellable": false,
				"successfulItems": 1,
				"failedItems": 0,
				"cancelledItems": 0,
				"itemResultCount": 1,
				"itemResultRevision": 1
			}
		})
		compare(card.expanded, false)
		verify(card.height < 100)

		const expandButton = findChild(card, "operationExpandButton")
		verify(expandButton !== null)
		compare(expandButton.iconName, "chevron-down")
		mouseClick(expandButton)
		tryCompare(card, "expanded", true)
		compare(expandButton.iconName, "chevron-up")
	}

	function test_successful_operation_auto_dismisses_after_reading_delay() {
		const card = loadCard({
			"status": "succeeded",
			"successfulAutoDismissDelay": 20,
			"payload": {
				"cancellable": false,
				"successfulItems": 1,
				"failedItems": 0,
				"cancelledItems": 0,
				"itemResultCount": 0,
				"itemResultRevision": 1
			}
		})
		compare(card.successfulAutoDismissEligible, true)
		tryCompare(dismissSpy, "count", 1, 500)
		compare(dismissSpy.signalArguments[0][0], "plugin-update:test")
	}

	function test_non_successful_operations_never_auto_dismiss() {
		for (const terminalStatus of [ "partial", "failed", "cancelled" ]) {
			const card = loadCard({
				"status": terminalStatus,
				"successfulAutoDismissDelay": 10,
				"payload": {
					"cancellable": false,
					"successfulItems": 0,
					"failedItems": terminalStatus === "failed" ? 1 : 0,
					"cancelledItems": terminalStatus === "cancelled" ? 1 : 0,
					"itemResultCount": 0,
					"itemResultRevision": 1
				}
			})
			compare(card.successfulAutoDismissEligible, false)
			wait(30)
			compare(dismissSpy.count, 0, terminalStatus)
			card.destroy()
			currentCard = null
		}
	}

	function test_successful_operation_stays_while_focused_or_explicitly_expanded() {
		const card = loadCard({
			"status": "succeeded",
			"successfulAutoDismissDelay": 20,
			"payload": {
				"cancellable": false,
				"successfulItems": 1,
				"failedItems": 0,
				"cancelledItems": 0,
				"itemResultCount": 0,
				"itemResultRevision": 1
			}
		})
		const expandButton = findChild(card, "operationExpandButton")
		verify(expandButton !== null)
		expandButton.forceActiveFocus()
		tryCompare(expandButton, "activeFocus", true)
		wait(50)
		compare(dismissSpy.count, 0)

		mouseClick(expandButton)
		compare(card.expansionOverride, 1)
		compare(card.successfulAutoDismissEligible, false)
		testCase.forceActiveFocus()
		wait(50)
		compare(dismissSpy.count, 0)
	}

	function test_cancel_and_dismiss_keep_operation_ids() {
		let card = loadCard({})
		const cancelButton = findChild(card, "operationCancelButton")
		verify(cancelButton !== null)
		verify(cancelButton.visible, "status=" + card.status + " payload=" + JSON.stringify(card.payload)
			+ " cardVisible=" + card.visible + " buttonEnabled=" + cancelButton.enabled)
		mouseClick(cancelButton)
		compare(cancelSpy.count, 1)
		compare(cancelSpy.signalArguments[0][0], "plugin-update:test")

		card.status = "failed"
		card.payload = {
			"cancellable": false,
			"itemResultCount": 0,
			"itemResultRevision": 1
		}
		const dismissButton = findChild(card, "visualFixtureDismissOperation")
		verify(dismissButton !== null)
		tryVerify(function() { return dismissButton.visible })
		mouseClick(dismissButton)
		compare(dismissSpy.count, 1)
		compare(dismissSpy.signalArguments[0][0], "plugin-update:test")
	}

	function test_item_results_scroll_inside_bounded_details() {
		const card = loadCard({
			"status": "partial",
			"payload": {
				"cancellable": false,
				"successfulItems": 30,
				"failedItems": 10,
				"cancelledItems": 0,
				"itemResultCount": 40,
				"itemResultRevision": 4
			},
			"maximumHeight": 190
		})
		card.itemResultPageProvider = function(operationId, offset, limit, unsuccessfulOnly) {
			const rows = []
			const count = unsuccessfulOnly ? 3 : Math.min(limit, 40 - offset)
			for (let index = 0; index < count; ++index) {
				rows.push({
					"itemId": "plugin-" + (offset + index),
					"message": "Detailed result for plugin " + (offset + index)
						+ " with enough content to require a scrollable details region",
					"success": !unsuccessfulOnly && index % 3 !== 0,
					"cancelled": false
				})
			}
			return rows
		}

		const expandButton = findChild(card, "operationExpandButton")
		mouseClick(expandButton)
		tryCompare(card, "expanded", true)
		const resultsButton = findChild(card, "operationResultDetailsButton")
		tryVerify(function() { return resultsButton.visible })
		resultsButton.clicked()
		tryCompare(card, "resultDetailsExpanded", true)

		const details = findChild(card, "operationDetailsScroll")
		verify(details !== null)
		tryVerify(function() {
			return details.contentItem && details.contentItem.contentHeight > details.height
		})
		verify(card.height <= card.maximumHeight)
		const oldY = details.contentItem.contentY
		details.contentItem.contentY = Math.min(details.contentItem.contentHeight - details.height, oldY + 40)
		verify(details.contentItem.contentY > oldY)
	}
}
