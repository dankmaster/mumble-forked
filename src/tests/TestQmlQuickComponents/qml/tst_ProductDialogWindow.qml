import QtQuick.Controls
import QtQuick.Window
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ProductDialogWindow"
	when: windowShown
	width: 900
	height: 700
	property var productWindow: null
	QtObject {
		id: geometryStoreDouble
		property var calls: []
		property bool restoreResult: false
		function restoreWindow(window, key, minimumWidth, minimumHeight) {
			calls = calls.concat([ key ])
			if (restoreResult) {
				window.x = 211
				window.y = 133
				window.width = Math.max(minimumWidth, 630)
				window.height = Math.max(minimumHeight, 470)
			}
			return restoreResult
		}
	}

	function createProductWindow() {
		const component = Qt.createComponent("qrc:/qml-shell/ProductDialogWindow.qml")
		tryCompare(component, "status", Component.Ready)
		const created = component.createObject(null, {
			"controller": dialogState,
			"parentWindow": testCase.Window.window,
			"geometryStore": geometryStoreDouble
		})
		verify(created !== null, component.errorString())
		return created
	}

	function openProduct(id, width, height) {
		dialogState.setSpecialState("generic", {
			"id": id,
			"pages": [],
			"width": width,
			"height": height,
			"primaryActionId": "save"
		})
		dialogState.open = true
		tryCompare(productWindow, "visible", true)
		tryCompare(productWindow.hostedDialog, "visible", true)
	}

	function init() {
		dialogState.open = false
		dialogState.resetSections()
		dialogState.setSpecialState("generic", {})
		geometryStoreDouble.calls = []
		geometryStoreDouble.restoreResult = false
		productWindow = createProductWindow()
	}

	function cleanup() {
		dialogState.open = false
		if (productWindow) {
			productWindow.destroy()
			productWindow = null
		}
	}

	function test_host_is_warm_and_native_before_first_open() {
		verify(productWindow.hostedDialog !== null)
		compare(productWindow.visible, false)
		verify((productWindow.flags & Qt.FramelessWindowHint) === 0)
		verify((productWindow.flags & Qt.Window) !== 0)
		compare(productWindow.modality, Qt.WindowModal)
		compare(productWindow.hostedDialog.nativeWindowHosted, true)
		compare(productWindow.hostedDialog.excludeSettings, true)
	}

	function test_native_show_is_deferred_past_the_controller_callback() {
		dialogState.setSpecialState("deferred-show", {
			"id": "deferred-show", "pages": [], "width": 720, "height": 560
		})
		dialogState.open = true
		// Showing synchronously here can re-enter QSGThreadedRenderLoop while the
		// menu/controller callback that published the state is still on the stack.
		compare(productWindow.visible, false)
		tryCompare(productWindow, "visible", true)
		tryCompare(productWindow.hostedDialog, "visible", true)
	}

	function test_preferred_geometry_is_committed_before_show_and_stays_stable() {
		openProduct("first-frame-size", 720, 600)
		const available = productWindow.availableGeometry()
		const margin = Theme.space4
		compare(productWindow.width,
			Math.min(720, Math.max(productWindow.minimumWidth, available.width - margin * 2)))
		compare(productWindow.height,
			Math.min(600, Math.max(productWindow.minimumHeight, available.height - margin * 2)))
		tryCompare(productWindow.hostedDialog, "width", productWindow.width)
		tryCompare(productWindow.hostedDialog, "height", productWindow.height)

		const committedWidth = productWindow.width
		const committedHeight = productWindow.height
		// Hydration and validation updates for the same dialog may change content
		// metrics, but must not resize the native window after its first frame.
		dialogState.setSpecialState("generic", {
			"id": "first-frame-size", "pages": [], "width": 1040, "height": 780
		})
		wait(0)
		compare(productWindow.width, committedWidth)
		compare(productWindow.height, committedHeight)
	}

	function test_new_dialog_gets_its_own_controller_geometry() {
		openProduct("dialog-a", 520, 300)
		const firstWidth = productWindow.width
		dialogState.setSpecialState("generic", {
			"id": "dialog-b", "pages": [], "width": 760, "height": 620
		})
		tryVerify(function() { return productWindow.width !== firstWidth })
		compare(productWindow.presentedDialogId, "dialog-b")
	}

	function test_each_logical_dialog_uses_its_own_persisted_geometry_key() {
		geometryStoreDouble.restoreResult = true
		openProduct("server-browser", 820, 640)
		compare(geometryStoreDouble.calls[geometryStoreDouble.calls.length - 1],
			"dialog:server-browser")
		compare(productWindow.x, 211)
		compare(productWindow.y, 133)
		compare(productWindow.width, 630)
		compare(productWindow.height, 470)

		dialogState.setSpecialState("generic", {
			"id": "stonks", "pages": [], "width": 1000, "height": 760
		})
		tryCompare(productWindow, "presentedDialogId", "stonks")
		compare(geometryStoreDouble.calls[geometryStoreDouble.calls.length - 1],
			"dialog:stonks")
		compare(productWindow.width, 630)
		compare(productWindow.height, 470)
	}

	function test_system_close_uses_controller_owned_close_path() {
		openProduct("native-close", 620, 420)
		const before = dialogState.closeRequests
		productWindow.close()
		tryCompare(dialogState, "closeRequests", before + 1)
		compare(productWindow.visible, true)
		dialogState.open = false
		tryCompare(productWindow, "visible", false)
	}

	function test_settings_and_image_viewer_are_not_claimed() {
		dialogState.setSpecialState("settings", { "id": "settings", "width": 920, "height": 700 })
		dialogState.open = true
		wait(0)
		compare(productWindow.visible, false)
		dialogState.setSpecialState("imageViewer", { "id": "viewer" })
		wait(0)
		compare(productWindow.visible, false)
	}
}
