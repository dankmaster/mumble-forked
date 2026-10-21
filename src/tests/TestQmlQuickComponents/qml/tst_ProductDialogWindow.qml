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
	// Workspace surfaces keep a stable, persisted viewport; page navigation is
	// the simplest DTO shape that selects that presentation.
	readonly property var stablePages: [ { "id": "general", "label": "General" } ]
	QtObject {
		id: geometryStoreDouble
		property var calls: []
		property int releases: 0
		property bool restoreResult: false
		function releaseWindow(window) {
			releases += 1
		}
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

	function openProduct(id, width, height, pages) {
		dialogState.setSpecialState("generic", {
			"id": id,
			"pages": pages || [],
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
		geometryStoreDouble.releases = 0
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
		openProduct("first-frame-size", 720, 600, stablePages)
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
			"id": "first-frame-size", "pages": stablePages, "width": 1040, "height": 780
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

	function test_activate_restores_a_minimized_dialog() {
		openProduct("restore-dialog", 620, 420)
		productWindow.showMinimized()
		tryCompare(productWindow, "visibility", Window.Minimized)
		productWindow.activateDialog()
		tryCompare(productWindow, "visibility", Window.Windowed)
		compare(productWindow.presentedDialogId, "restore-dialog")
	}

	function test_new_dialog_restores_minimized_host_but_background_update_does_not() {
		openProduct("dialog-a", 620, 420)
		productWindow.showMinimized()
		tryCompare(productWindow, "visibility", Window.Minimized)
		dialogState.setSpecialState("generic", {
			"id": "dialog-a", "pages": [], "width": 620, "height": 420
		})
		wait(0)
		compare(productWindow.visibility, Window.Minimized)
		dialogState.setSpecialState("generic", {
			"id": "dialog-b", "pages": [], "width": 760, "height": 620
		})
		tryCompare(productWindow, "presentedDialogId", "dialog-b")
		tryCompare(productWindow, "visibility", Window.Windowed)
	}

	function test_activate_restores_maximized_dialog_state() {
		openProduct("restore-maximized-dialog", 620, 420)
		productWindow.showMaximized()
		tryCompare(productWindow, "visibility", Window.Maximized)
		productWindow.showMinimized()
		tryCompare(productWindow, "visibility", Window.Minimized)
		productWindow.activateDialog()
		tryCompare(productWindow, "visibility", Window.Maximized)
	}

	function test_each_logical_dialog_uses_its_own_persisted_geometry_key() {
		geometryStoreDouble.restoreResult = true
		openProduct("server-browser", 820, 640, stablePages)
		compare(geometryStoreDouble.calls[geometryStoreDouble.calls.length - 1],
			"dialog:server-browser")
		compare(productWindow.x, 211)
		compare(productWindow.y, 133)
		compare(productWindow.width, 630)
		compare(productWindow.height, 470)

		dialogState.setSpecialState("generic", {
			"id": "stonks", "pages": stablePages, "width": 1000, "height": 760
		})
		tryCompare(productWindow, "presentedDialogId", "stonks")
		compare(geometryStoreDouble.calls[geometryStoreDouble.calls.length - 1],
			"dialog:stonks")
		compare(productWindow.width, 630)
		compare(productWindow.height, 470)
	}

	function test_sparse_dialog_is_sized_to_content_before_its_first_frame() {
		geometryStoreDouble.restoreResult = true
		dialogState.setSections([{ "title": "Confirmation", "fields": [
			{ "type": "note", "text": "Quitting will disconnect from the current server and close Mumble." },
			{ "id": "remember", "type": "checkbox", "label": "Remember this setting", "value": false }
		] }])
		let shownHeight = -1
		const recordShownHeight = function() {
			if (productWindow.visible && shownHeight < 0)
				shownHeight = productWindow.height
		}
		productWindow.visibleChanged.connect(recordShownHeight)
		openProduct("quit-like", 560, 700)
		productWindow.visibleChanged.disconnect(recordShownHeight)

		verify(productWindow.contentFitPresentation)
		verify(geometryStoreDouble.calls.indexOf("dialog:quit-like") < 0,
			"A content-sized dialog must not restore a persisted size")
		verify(geometryStoreDouble.releases > 0)
		compare(productWindow.width, 560)
		const dialog = productWindow.hostedDialog
		tryCompare(dialog, "height", productWindow.height)
		wait(50)
		compare(productWindow.height, shownHeight,
			"The pre-show measurement must already be the settled content height")
		verify(productWindow.height < 400, "Sparse content must not inherit the nominal DTO height")
		compare(productWindow.height, Math.max(dialog.minimumContentSizedHeight,
			dialog.naturalContentSizedHeight))
		const scroll = findChild(dialog.contentItem, "dialogContentScroll")
		const footer = findChild(dialog.contentItem, "dialogFooter")
		verify(scroll.contentHeight <= scroll.height + 1, "The complete body must be visible without scrolling")
		verify(footer.mapToItem(null, 0, footer.height).y <= productWindow.height + 1)

		const sparseHeight = productWindow.height
		dialogState.setValidationError("remember", "Choose whether Mumble should remember this decision.")
		tryVerify(function() { return productWindow.height > sparseHeight },
			1000, "Live validation must grow a content-sized dialog instead of scrolling it")
		dialogState.setValidationError("remember", "")
		tryCompare(productWindow, "height", sparseHeight)
	}

	function test_workspace_dialog_keeps_its_persisted_viewport() {
		dialogState.setSections([{ "title": "Records", "fields": [
			{ "id": "records", "type": "resultList", "label": "Records", "items": [] }
		] }])
		openProduct("record-browser", 760, 620)
		verify(!productWindow.contentFitPresentation)
		compare(geometryStoreDouble.calls[geometryStoreDouble.calls.length - 1], "dialog:record-browser")
		compare(productWindow.height, Math.min(620,
			Math.max(productWindow.minimumHeight, productWindow.availableGeometry().height - Theme.space4 * 2)))
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
