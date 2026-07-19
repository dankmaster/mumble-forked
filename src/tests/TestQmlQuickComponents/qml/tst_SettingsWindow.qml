import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest

TestCase {
	id: testCase
	name: "SettingsWindow"
	when: windowShown
	width: 900
	height: 700
	property var settingsWindow: null
	property string mainSource: ""

	function initTestCase() {
		mainSource = String(mainQmlSource || "")
		verify(mainSource.length > 1000, "Main.qml resource was unexpectedly empty")
	}

	function createSettingsWindow() {
		const component = Qt.createComponent("qrc:/qml-shell/SettingsWindow.qml")
		tryCompare(component, "status", Component.Ready)
		const created = component.createObject(null, {
			"controller": dialogState,
			"parentWindow": testCase.Window.window
		})
		verify(created !== null)
		return created
	}

	function openSettings() {
		dialogState.setSpecialState("settings", {
			"id": "settings-window-test",
			"pages": [
				{ "id": "general", "label": "General", "icon": "settings" },
				{ "id": "advanced", "label": "Advanced", "icon": "controls" }
			],
			"width": 920,
			"height": 700
		})
		dialogState.open = true
		tryCompare(settingsWindow, "visible", true)
		tryCompare(settingsWindow.hostedDialog, "visible", true)
	}

	function init() {
		dialogState.open = false
		dialogState.resetSections()
		settingsWindow = createSettingsWindow()
		openSettings()
	}

	function cleanup() {
		dialogState.open = false
		if (settingsWindow) {
			settingsWindow.destroy()
			settingsWindow = null
		}
		dialogState.setSpecialState("generic", {})
	}

	function test_uses_native_window_chrome_without_redundant_qml_close() {
		compare(settingsWindow.surfaceId, "settings.window")
		verify((settingsWindow.flags & Qt.FramelessWindowHint) === 0)
		verify((settingsWindow.flags & Qt.Window) !== 0)
		compare(settingsWindow.modality, Qt.NonModal)
		compare(settingsWindow.hostedDialog.nativeWindowHosted, true)
		compare(settingsWindow.hostedDialog.settingsOnly, true)

		const closeButton = findChild(settingsWindow.hostedDialog.contentItem,
			"dialogCloseButton")
		verify(closeButton !== null)
		compare(closeButton.visible, false)
		tryCompare(settingsWindow.hostedDialog, "width", settingsWindow.width)
		tryCompare(settingsWindow.hostedDialog, "height", settingsWindow.height)
	}

	function test_system_close_requests_controller_close_once() {
		const before = dialogState.closeRequests
		settingsWindow.close()
		tryCompare(dialogState, "closeRequests", before + 1)
		// The controller owns apply/cancel semantics. The native close event stays
		// rejected until that controller publishes its closed state.
		compare(settingsWindow.visible, true)
		dialogState.open = false
		tryCompare(settingsWindow, "visible", false)
	}

	function test_escape_uses_the_same_controller_owned_close_path() {
		const before = dialogState.closeRequests
		settingsWindow.requestActivate()
		settingsWindow.hostedDialog.applyInitialFocus()
		keyClick(Qt.Key_Escape)
		tryCompare(dialogState, "closeRequests", before + 1)
		// Escape is window-local and cannot fall through to Main's close handling.
		compare(settingsWindow.visible, true)
		compare(dialogState.open, true)
	}

	function test_reopen_preserves_usable_size_and_restores_focus() {
		settingsWindow.width = 720
		settingsWindow.height = 600
		dialogState.open = false
		tryCompare(settingsWindow, "visible", false)
		dialogState.open = true
		tryCompare(settingsWindow, "visible", true)
		tryCompare(settingsWindow.hostedDialog, "visible", true)
		compare(settingsWindow.width, 720)
		compare(settingsWindow.height, 600)
		verify(settingsWindow.effectiveDevicePixelRatio > 0)
		tryVerify(function() {
			return settingsWindow.hostedDialog.hasActiveDialogFocus()
		})
	}

	function test_settings_navigation_remains_keyboard_and_pointer_driven() {
		const pageList = findChild(settingsWindow.hostedDialog.contentItem,
			"dialogPageList")
		verify(pageList !== null)
		var advancedPage = null
		tryVerify(function() {
			advancedPage = findChild(settingsWindow.hostedDialog.contentItem,
				"dialogPage_advanced")
			return advancedPage !== null
		})
		pageList.forceActiveFocus(Qt.TabFocusReason)
		tryCompare(pageList, "activeFocus", true)
		mouseClick(advancedPage)
		tryCompare(dialogState, "activePage", "advanced")
		compare(dialogState.lastAction, "selectPage")
	}

	function test_settings_does_not_install_the_main_window_modal_barrier() {
		verify(/readonly property bool modalUiActive:\s*\(dialogState\.open\s*&&\s*dialogState\.kind\s*!==\s*"settings"\)/.test(mainSource))
		verify(/readonly property bool backgroundAccessibilitySuppressed:\s*navigationModalActive\s*\|\|\s*mediaSessionWindowUnavailable/.test(mainSource))
		verify(/ModalAccessibilityBarrier\s*\{[\s\S]*id:\s*modalAccessibilityBarrier[\s\S]*active:\s*root\.backgroundAccessibilitySuppressed/.test(mainSource))
		verify(/ProductDialogWindow\s*\{[\s\S]*id:\s*productDialog[\s\S]*controller:\s*dialogState/.test(mainSource))
		verify(/SettingsWindow\s*\{[\s\S]*controller:\s*dialogState[\s\S]*parentWindow:\s*root/.test(mainSource))
	}
}
