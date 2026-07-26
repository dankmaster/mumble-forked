import QtQuick
import QtQuick.Controls
import QtTest
import "../../../mumble/qml-shell" as Shell
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "PluginEditor"
	when: windowShown
	width: 860
	height: 720

	property var populatedField
	Item {
		id: outerViewportProbe
		visible: true
		width: testCase.width
		height: testCase.height
	}
	QtObject {
		id: operationController
		property string cancelledId: ""
		property int cancelCalls: 0
		property var rows: []
		readonly property int count: rows.length
		signal dataChanged()
		signal modelReset()
		function cancel(operationId) { cancelledId = operationId; cancelCalls += 1 }
		function get(index) { return index >= 0 && index < rows.length ? rows[index] : ({}) }
		function itemResultPage(operationId, offset, limit, unsuccessfulOnly) {
			const operation = rows.find(candidate => String(candidate.id || "") === operationId)
			const results = operation && operation.itemResults ? operation.itemResults : []
			return results.filter(result => !unsuccessfulOnly || !result.success).slice(offset, offset + limit)
		}
	}

	Shell.PluginEditor {
		id: editor
		width: testCase.width
		height: implicitHeight
		field: testCase.populatedField
		asyncOperationController: operationController
	}

	function pluginRows() {
		return [
			{
				"id": "positional",
				"name": "Spatial Worlds",
				"description": "Supplies positional audio for supported games and virtual spaces.",
				"author": "Mumble Test Team",
				"version": "2.4.1",
				"path": "C:/Users/Test/AppData/Local/Mumble/Plugins/spatial-worlds.mumble_plugin",
				"loaded": true,
				"enabled": true,
				"positionalAvailable": true,
				"positionalEnabled": true,
				"keyboardMonitoringAllowed": false,
				"canConfigure": true,
				"canShowAbout": true,
				"builtIn": false
			},
			{
				"id": "builtin",
				"name": "Manual placement",
				"description": "Built-in positional-audio testing tool.",
				"version": "Built in",
				"loaded": true,
				"enabled": true,
				"positionalAvailable": true,
				"positionalEnabled": true,
				"keyboardMonitoringAllowed": false,
				"canConfigure": true,
				"canShowAbout": true,
				"builtIn": true
			}
		]
	}

	function init() {
		testCase.width = 860
		editor.accessibilityViewport = null
		outerViewportProbe.x = 0
		outerViewportProbe.y = 0
		outerViewportProbe.width = testCase.width
		outerViewportProbe.height = testCase.height
		editor.animationsEnabled = true
		operationController.cancelledId = ""
		operationController.cancelCalls = 0
		operationController.rows = []
		operationController.modelReset()
		populatedField = {
			"label": "Installed plugins",
			"rows": pluginRows()
		}
		wait(0)
	}

	function cleanup() {
		editor.accessibilityViewport = null
	}

	function test_populated_state_has_clear_hierarchy_and_accessible_list() {
		compare(editor.rowCount, 2)
		compare(editor.Accessible.role, Accessible.Pane)
		compare(editor.Accessible.name, "Installed plugins")

		const list = findChild(editor, "pluginList")
		verify(list !== null)
		compare(list.count, 2)
		compare(list.Accessible.role, Accessible.List)
		tryVerify(function() { return list.itemAtIndex(0) !== null })
		const firstCard = list.itemAtIndex(0)
		const firstSemanticCard = findChild(firstCard, "pluginSemanticCard_positional")
		verify(firstSemanticCard !== null)
		compare(firstCard.Accessible.ignored, true)
		compare(firstSemanticCard.Accessible.role, Accessible.ListItem)
		compare(firstSemanticCard.Accessible.name, "Spatial Worlds")
		verify(findChild(firstCard, "pluginConfigure_positional") !== null)
		verify(findChild(firstCard, "pluginAbout_positional") !== null)
		verify(findChild(firstCard, "pluginUnload_positional") !== null)
		tryVerify(function() { return list.itemAtIndex(1) !== null })
		const builtInMetadata = findChild(list.itemAtIndex(1), "pluginMetadata_builtin")
		verify(builtInMetadata !== null)
		compare(builtInMetadata.text, "Built in")
		verify(builtInMetadata.text.indexOf("Unknown") < 0)
	}

	function test_outer_scroll_viewport_withdraws_partially_clipped_plugin_heading() {
		const introduction = findChild(editor, "pluginIntroduction")
		const heading = findChild(editor, "pluginIntroductionHeading")
		const summary = findChild(editor, "pluginIntroductionSummary")
		const barrier = findChild(editor, "pluginIntroductionAccessibilityBarrier")
		verify(introduction !== null && heading !== null && summary !== null && barrier !== null)
		editor.accessibilityViewport = outerViewportProbe

		tryVerify(function() {
			const point = introduction.mapToItem(testCase, 0, 0)
			outerViewportProbe.x = point.x
			outerViewportProbe.y = point.y
			outerViewportProbe.width = introduction.width
			outerViewportProbe.height = introduction.height - 2
			return !introduction.accessibilityExposed && barrier.active
		})
		tryCompare(heading.Accessible, "ignored", true)
		tryCompare(summary.Accessible, "ignored", true)

		tryVerify(function() {
			outerViewportProbe.height = introduction.height + 2
			return introduction.accessibilityExposed && !barrier.active
		})
		tryCompare(heading.Accessible, "ignored", false)
		tryCompare(summary.Accessible, "ignored", false)
	}

	function test_toolbar_and_plugin_actions_publish_typed_intents() {
		const installButton = findChild(editor, "pluginInstallButton")
		const rescanButton = findChild(editor, "pluginRescanButton")
		const updatesButton = findChild(editor, "pluginCheckUpdatesButton")
		verify(installButton !== null && rescanButton !== null && updatesButton !== null)

		installButton.clicked()
		compare(dialogState.lastAction, "plugins.install")
		rescanButton.clicked()
		compare(dialogState.lastAction, "plugins.rescan")
		updatesButton.clicked()
		compare(dialogState.lastAction, "plugins.checkUpdates")

		const configureButton = findChild(editor, "pluginConfigure_positional")
		const aboutButton = findChild(editor, "pluginAbout_positional")
		const unloadButton = findChild(editor, "pluginUnload_positional")
		configureButton.clicked()
		compare(dialogState.lastAction, "plugins.configure")
		compare(dialogState.lastPayload.pluginId, "positional")
		aboutButton.clicked()
		compare(dialogState.lastAction, "plugins.about")
		unloadButton.clicked()
		compare(dialogState.lastAction, "plugins.unload")

		const enableCheck = findChild(editor, "pluginEnable_positional")
		verify(enableCheck !== null)
		enableCheck.checked = false
		enableCheck.toggled()
		compare(dialogState.lastAction, "plugins.toggle")
		compare(dialogState.lastPayload.pluginId, "positional")
		compare(dialogState.lastPayload.property, "enabled")
		compare(dialogState.lastPayload.value, false)
	}

	function test_unloaded_plugin_exposes_explicit_load_without_changing_enable_or_unload() {
		const unloadedPlugin = {
			"id": "sleeping",
			"name": "Sleeping Worlds",
			"description": "Installed but not currently loaded.",
			"author": "Mumble Test Team",
			"version": "1.0.0",
			"path": "C:/Users/Test/AppData/Local/Mumble/Plugins/sleeping-worlds.mumble_plugin",
			"loaded": false,
			"enabled": false,
			"positionalAvailable": true,
			"positionalEnabled": false,
			"keyboardMonitoringAllowed": false,
			"canConfigure": true,
			"canShowAbout": true,
			"builtIn": false
		}
		populatedField = { "label": "Installed plugins", "rows": [ unloadedPlugin ] }

		const list = findChild(editor, "pluginList")
		tryVerify(function() { return list.itemAtIndex(0) !== null })
		const card = list.itemAtIndex(0)
		const semanticCard = findChild(card, "pluginSemanticCard_sleeping")
		const loadButton = findChild(card, "pluginLoad_sleeping")
		const unloadButton = findChild(card, "pluginUnload_sleeping")
		const enableCheck = findChild(card, "pluginEnable_sleeping")
		verify(semanticCard !== null && loadButton !== null
			&& unloadButton !== null && enableCheck !== null)
		compare(semanticCard.Accessible.description, "Plugin is not loaded")
		// Quick Test may hide the complete TestCase while a function executes, so
		// assert that the action follows its owning editor instead of requiring the
		// fixture tree to become effectively visible.
		compare(loadButton.visible, editor.visible)
		compare(loadButton.enabled, true)
		compare(loadButton.Accessible.role, Accessible.Button)
		compare(loadButton.Accessible.name, "Load Sleeping Worlds")
		compare(unloadButton.visible, false)
		compare(enableCheck.visible, editor.visible)
		compare(enableCheck.checked, false)

		loadButton.clicked()
		compare(dialogState.lastAction, "plugins.load")
		compare(dialogState.lastPayload.pluginId, "sleeping")
		enableCheck.checked = true
		enableCheck.toggled()
		compare(dialogState.lastAction, "plugins.toggle")
		compare(dialogState.lastPayload.pluginId, "sleeping")
		compare(dialogState.lastPayload.property, "enabled")
		compare(dialogState.lastPayload.value, true)

		populatedField = { "label": "Installed plugins", "rows": [ unloadedPlugin ], "operation": {
			"id": "plugin-load:sleeping", "status": "running", "title": "Loading plugin",
			"subtitle": "Starting Sleeping Worlds", "progress": 0, "cancellable": false
		} }
		const runningLoadButton = findChild(editor, "pluginLoad_sleeping")
		const operationCard = findChild(editor, "pluginOperationCard")
		tryCompare(editor, "operationRunning", true)
		compare(runningLoadButton.enabled, false)
		compare(operationCard.Accessible.name, "Loading plugin: Starting Sleeping Worlds")
	}

	function test_compact_cards_wrap_controls_inside_the_available_width() {
		testCase.width = 430
		tryCompare(editor, "compactLayout", true)

		const toolbar = findChild(editor, "pluginToolbar")
		const list = findChild(editor, "pluginList")
		tryVerify(function() { return toolbar.height > 36 && list.itemAtIndex(0) !== null })
		const card = list.itemAtIndex(0)
		const permissionFlow = findChild(card, "pluginPermissionFlow_positional")
		const actionFlow = findChild(card, "pluginActionFlow_positional")
		verify(permissionFlow !== null && actionFlow !== null)
		verify(permissionFlow.height > 30,
			"permission flow height=" + permissionFlow.height
			+ " implicit=" + permissionFlow.implicitHeight
			+ " card=" + card.width + "x" + card.height
			+ " exposed=" + card.accessibilityExposed)
		verify(actionFlow.height > 0)

		for (const flow of [ toolbar, permissionFlow, actionFlow ]) {
			for (const control of flow.children) {
				if (!control.visible || control.width <= 0)
					continue
				verify(control.x >= -0.5)
				verify(control.x + control.width <= flow.width + 0.5,
					control.objectName + " must stay within its compact flow")
			}
		}
	}

	function test_empty_loading_error_and_progress_states_are_explicit() {
		populatedField = { "label": "Installed plugins", "rows": [], "loading": true }
		compare(editor.loading, true)
		compare(editor.rowCount, 0)
		compare(editor.errorText, "")
		compare(editor.showLoadingState, true)
		compare(editor.showEmptyState, false)
		const loadingIndicator = findChild(editor, "pluginLoadingBusyIndicator")
		verify(loadingIndicator !== null)
		tryCompare(loadingIndicator, "running", true)
		compare(loadingIndicator.indicatorColor, Theme.accent)
		compare(loadingIndicator.Accessible.role, Accessible.ProgressBar)
		compare(loadingIndicator.Accessible.name, "Loading installed plugins")

		populatedField = { "label": "Installed plugins", "rows": [] }
		compare(editor.showEmptyState, true)
		compare(editor.showLoadingState, false)

		populatedField = {
			"label": "Installed plugins",
			"rows": [],
			"error": "The plugin directory could not be read."
		}
		const errorCard = findChild(editor, "pluginErrorCard")
		compare(editor.showErrorState, true)
		compare(errorCard.Accessible.role, Accessible.AlertMessage)
		findChild(editor, "pluginRetryButton").clicked()
		compare(dialogState.lastAction, "plugins.rescan")

		populatedField = {
			"label": "Installed plugins",
			"rows": pluginRows(),
			"operation": {
				"status": "downloading",
				"label": "Updating Spatial Worlds",
				"progress": 42
			}
		}
		const operationCard = findChild(editor, "pluginOperationCard")
		const operationIndicator = findChild(editor, "pluginOperationBusyIndicator")
		const progress = findChild(editor, "pluginOperationProgress")
		verify(operationCard !== null && operationIndicator !== null)
		compare(editor.operationVisible, true)
		tryCompare(operationIndicator, "running", true)
		compare(operationIndicator.indicatorColor, Theme.accent)
		compare(operationIndicator.Accessible.name, "Plugin operation in progress")
		compare(editor.operationProgress, 0.42)
		compare(progress.indeterminate, false)
		compare(progress.value, 0.42)
	}

	function test_visual_fixture_freezes_plugin_operation_motion_without_hiding_state() {
		populatedField = {
			"label": "Installed plugins",
			"rows": pluginRows(),
			"operation": {
				"status": "downloading",
				"label": "Checking plugin updates…",
				"progress": 64
			}
		}
		editor.animationsEnabled = false
		const operationCard = findChild(editor, "pluginOperationCard")
		const operationIndicator = findChild(editor, "pluginOperationBusyIndicator")
		const progress = findChild(editor, "pluginOperationProgress")
		verify(operationCard !== null && operationIndicator !== null && progress !== null)
		tryCompare(editor, "operationVisible", true)
		compare(operationCard.visible, editor.visible && editor.operationVisible)
		verify(operationCard.implicitHeight > 0)
		compare(operationIndicator.running, true)
		compare(operationIndicator.animated, false)
		compare(operationIndicator.visible, false)
		compare(progress.visible, operationCard.visible)
		compare(progress.animated, false)
		compare(progress.value, 0.64)
	}

	function test_live_model_shows_only_active_or_failed_plugin_work() {
		populatedField = { "label": "Installed plugins", "rows": pluginRows() }
		operationController.rows = [
			{
				"id": "plugin-update-check:success",
				"kind": "plugin-update-check",
				"status": "succeeded",
				"title": "Checking plugin updates",
				"subtitle": "1 item completed"
			},
			{
				"id": "chat-attachment-save:1",
				"kind": "",
				"status": "failed",
				"title": "Saving attachment"
			}
		]
		operationController.dataChanged()
		compare(editor.operationVisible, false)

		operationController.rows = operationController.rows.concat([ {
			"id": "plugin-load:broken",
			"kind": "plugin-load",
			"status": "failed",
			"title": "Loading plugin",
			"subtitle": "1 item failed",
			"failedItems": 1,
			"itemResults": [ {
				"itemId": "broken",
				"success": false,
				"errorCode": "load-failed",
				"message": "The plugin could not be loaded"
			} ]
		} ])
		operationController.dataChanged()
		compare(editor.operationVisible, true)
		compare(editor.operationId, "plugin-load:broken")
		compare(editor.operationStatus, "failed")
		compare(editor.operationResultCount, 1)
		compare(findChild(editor, "pluginOperationCard").visible, editor.visible)
	}

	function test_outer_scroll_viewport_withdraws_fully_clipped_plugin_subtrees() {
		const list = findChild(editor, "pluginList")
		wait(0)
		tryVerify(function() { return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null })
		const firstCard = list.itemAtIndex(0)
		const secondCard = list.itemAtIndex(1)
		const firstSemanticCard = findChild(firstCard, "pluginSemanticCard_positional")
		const secondSemanticCard = findChild(secondCard, "pluginSemanticCard_builtin")
		verify(firstSemanticCard !== null && secondSemanticCard !== null)
		tryVerify(function() {
			list.forceLayout()
			return secondCard.y >= firstCard.y + firstCard.height
		}, 5000, "The outer plugin column must settle before viewport clipping is sampled")
		editor.accessibilityViewport = outerViewportProbe

		tryCompare(firstCard, "fitsListViewport", true)
		tryCompare(secondCard, "fitsListViewport", true)
		tryVerify(function() {
			// Wrapped text and Flow rows may polish on a later turn. Refit the
			// synthetic viewport to the card's current scene bounds on every probe so
			// the assertion exercises product clipping, not stale test coordinates.
			const point = firstCard.mapToItem(testCase, 0, 0)
			outerViewportProbe.x = point.x
			outerViewportProbe.y = point.y
			outerViewportProbe.width = firstCard.width
			outerViewportProbe.height = firstCard.height
			return firstCard.fitsOuterViewport && !secondCard.fitsOuterViewport
		})
		const mapped = firstCard.mapToItem(outerViewportProbe, 0, 0)
		verify(firstCard.fitsOuterViewport,
			"first card " + mapped.x + "," + mapped.y + " "
			+ firstCard.width + "x" + firstCard.height + " in viewport "
			+ outerViewportProbe.width + "x" + outerViewportProbe.height)
		tryCompare(secondCard, "fitsOuterViewport", false)
		tryCompare(firstCard, "accessibilityExposed", true)
		tryCompare(secondCard, "accessibilityExposed", false)
		compare(firstCard.Accessible.ignored, true)
		compare(secondCard.Accessible.ignored, true)
		compare(firstSemanticCard.Accessible.ignored, false)
		compare(secondSemanticCard.Accessible.ignored, true)

		tryVerify(function() {
			const point = secondCard.mapToItem(testCase, 0, 0)
			outerViewportProbe.x = point.x
			outerViewportProbe.y = point.y
			outerViewportProbe.width = secondCard.width
			outerViewportProbe.height = secondCard.height
			return !firstCard.fitsOuterViewport && secondCard.fitsOuterViewport
		})
		tryCompare(firstCard, "accessibilityExposed", false)
		tryCompare(secondCard, "accessibilityExposed", true)
		compare(firstCard.Accessible.ignored, true)
		compare(secondCard.Accessible.ignored, true)
		compare(firstSemanticCard.Accessible.ignored, true)
		compare(secondSemanticCard.Accessible.ignored, false)
	}

	function test_updating_state_exposes_progress_and_typed_cancellation() {
		const updateButton = findChild(editor, "pluginCheckUpdatesButton")
		verify(updateButton !== null)
		updateButton.forceActiveFocus()
		tryCompare(updateButton, "activeFocus", true)
		populatedField = { "label": "Installed plugins", "rows": pluginRows(), "operation": {
			"id": "update:fixture", "status": "running", "title": "Updating plugins",
			"subtitle": "Downloading Spatial Worlds", "progress": 54, "cancellable": true
		} }
		const card = findChild(editor, "pluginOperationCard")
		const progress = findChild(editor, "pluginOperationProgress")
		const cancelLoader = findChild(editor, "pluginOperationCancelLoader")
		tryCompare(editor, "operationCancellable", true)
		tryCompare(cancelLoader, "status", Loader.Ready)
		const cancel = cancelLoader.item
		verify(cancel !== null)
		compare(cancelLoader.active, true)
		// Quick Test may hide the complete TestCase item while executing a
		// function. QQuickItem::visible reports effective ancestor visibility,
		// so assert that the loaded control follows its owning surface instead
		// of requiring an invisible fixture tree to become visible.
		compare(cancelLoader.visible, editor.visible && editor.operationCancellable)
		compare(cancel.visible, cancelLoader.visible)
		compare(card.Accessible.name, "Updating plugins: Downloading Spatial Worlds")
		compare(progress.Accessible.name, "Plugin update progress")
		compare(progress.Accessible.description, "54 percent complete")
		compare(cancel.Accessible.name, "Cancel plugin update")
		tryCompare(cancel, "activeFocus", true)
		compare(findChild(editor, "pluginInstallButton").enabled, false)
		compare(findChild(editor, "pluginRescanButton").enabled, false)
		compare(findChild(editor, "pluginCheckUpdatesButton").enabled, false)
		compare(findChild(editor, "pluginEnable_positional").enabled, false)
		compare(findChild(editor, "pluginConfigure_positional").enabled, false)
		compare(findChild(editor, "pluginAbout_positional").enabled, false)
		compare(findChild(editor, "pluginUnload_positional").enabled, false)
		cancel.clicked()
		compare(operationController.cancelCalls, 1)
		compare(operationController.cancelledId, "update:fixture")
	}

	function test_background_plugin_operation_does_not_steal_external_focus() {
		const external = Qt.createQmlObject(
			'import QtQuick.Controls; Button { text: "Outside"; width: 80; height: 32 }', testCase)
		verify(external !== null)
		try {
			external.forceActiveFocus()
			tryCompare(external, "activeFocus", true)
			populatedField = { "label": "Installed plugins", "rows": pluginRows(), "operation": {
				"id": "update:background", "status": "running", "title": "Updating plugins",
				"progress": 12, "cancellable": true
			} }
			const cancelLoader = findChild(editor, "pluginOperationCancelLoader")
			tryCompare(cancelLoader, "status", Loader.Ready)
			wait(0)
			verify(external.activeFocus,
				"A background operation must not steal focus from another product surface")
		} finally {
			external.destroy()
		}
	}

	function test_partial_state_has_responsive_per_item_results_and_error_code() {
		testCase.width = 430
		populatedField = { "label": "Installed plugins", "rows": pluginRows(), "operation": {
			"id": "update:fixture", "status": "partial", "title": "Plugin update finished",
			"subtitle": "1 updated · 1 failed", "progress": 100, "cancellable": false,
			"successfulItems": 1, "failedItems": 1, "cancelledItems": 0,
			"itemResults": [
				{ "itemId": "ok", "name": "Manual placement", "success": true, "message": "Already current" },
				{ "itemId": "failed", "name": "Spatial Worlds", "success": false,
					"errorCode": "signature-invalid", "message": "Signature verification failed" }
			]
		} }
		const card = findChild(editor, "pluginOperationCard")
		const results = findChild(editor, "pluginOperationResults")
		const cancelLoader = findChild(editor, "pluginOperationCancelLoader")
		tryCompare(editor, "operationTerminal", true)
		tryCompare(editor, "operationResultCount", 2)
		tryVerify(function() {
			const item = findChild(editor, "pluginOperationResult_failed")
			return item !== null && item.implicitHeight > 0
		})
		const failed = findChild(editor, "pluginOperationResult_failed")
		compare(editor.operationResultCount, 2)
		compare(results.visible, editor.visible && editor.operationTerminal
			&& editor.operationResultCount > 0)
		compare(cancelLoader.active, false)
		compare(cancelLoader.item, null)
		compare(failed.Accessible.role, Accessible.ListItem)
		compare(failed.Accessible.name, "Failed: Spatial Worlds — Signature verification failed")
		compare(failed.Accessible.description, "Error code: signature-invalid")
		tryVerify(function() {
			return results.width <= card.width + 0.5
				&& failed.width <= results.width + 0.5
		}, 5000, "Plugin operation result geometry did not settle inside the card")
		verify(results.width <= card.width + 0.5,
			"results.width=" + results.width + ", card.width=" + card.width)
		verify(failed.width <= results.width + 0.5,
			"failed.width=" + failed.width + ", results.width=" + results.width)
	}
}
