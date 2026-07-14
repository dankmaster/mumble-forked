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

	Shell.PluginEditor {
		id: editor
		width: testCase.width
		field: testCase.populatedField
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
		populatedField = {
			"label": "Installed plugins",
			"rows": pluginRows()
		}
		wait(0)
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
		compare(firstCard.Accessible.role, Accessible.ListItem)
		compare(firstCard.Accessible.name, "Spatial Worlds")
		verify(findChild(firstCard, "pluginConfigure_positional") !== null)
		verify(findChild(firstCard, "pluginAbout_positional") !== null)
		verify(findChild(firstCard, "pluginUnload_positional") !== null)
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
		verify(permissionFlow.height > 30)
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
}
