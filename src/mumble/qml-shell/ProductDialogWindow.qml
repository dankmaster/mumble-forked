import QtQuick
import QtQuick.Window
import Mumble.Theme 1.0

Window {
	id: productWindow
	objectName: "productDialogWindow"

	required property var controller
	property var parentWindow: null
	property bool visualFixtureMode: false
	property var beforeOpen: null
	property string presentedDialogId: ""
	property var geometryStore: typeof windowStateStore !== "undefined" ? windowStateStore : null
	readonly property string surfaceId: "product-dialog.window"
	readonly property var captureRect: Qt.rect(0, 0, width, height)
	readonly property bool productOpen: controller && controller.open
		&& String(controller.kind || "") !== "settings"
		&& String(controller.kind || "") !== "imageViewer"
	readonly property bool opened: visible && hostedDialog.opened
	readonly property real effectiveDevicePixelRatio: screen
		? Math.max(1, Number(screen.devicePixelRatio || 1)) : 1
	readonly property alias hostedDialog: hostedDialog

	signal openedWindow()

	title: controller && String(controller.title || "").length > 0
		? String(controller.title) : qsTr("Mumble")
	width: 920
	height: 700
	minimumWidth: Math.max(320,
		Math.min(420, availableGeometry().width - Theme.space5 * 2))
	minimumHeight: Math.max(240,
		Math.min(360, availableGeometry().height - Theme.space5 * 2))
	visible: false
	color: Theme.shellBackground
	flags: Qt.Window
	modality: Qt.WindowModal
	transientParent: parentWindow

	function availableGeometry() {
		const geometry = screen ? screen.availableGeometry : null
		if (geometry && Number(geometry.width) > 0 && Number(geometry.height) > 0)
			return geometry
		return Qt.rect(0, 0, Math.max(width, 1280), Math.max(height, 720))
	}

	function currentDialogId() {
		if (!controller)
			return ""
		return String(controller.dialogId || (controller.state || {}).id
			|| controller.kind || "product-dialog")
	}

	function ensureUsableGeometry(usePreferredSize, centerWindow) {
		const dialogId = currentDialogId()
		if (geometryStore && dialogId.length > 0
				&& geometryStore.restoreWindow(productWindow, "dialog:" + dialogId,
					minimumWidth, minimumHeight))
			return true
		const available = availableGeometry()
		const margin = Theme.space4
		const maximumUsableWidth = Math.max(minimumWidth, available.width - margin * 2)
		const maximumUsableHeight = Math.max(minimumHeight, available.height - margin * 2)
		if (usePreferredSize) {
			const preferredWidth = Math.max(minimumWidth,
				Number(controller && controller.preferredWidth || 920))
			const preferredHeight = Math.max(minimumHeight,
				Number(controller && controller.preferredHeight || 700))
			width = Math.min(preferredWidth, maximumUsableWidth)
			height = Math.min(preferredHeight, maximumUsableHeight)
		} else {
			width = Math.max(minimumWidth, Math.min(width, maximumUsableWidth))
			height = Math.max(minimumHeight, Math.min(height, maximumUsableHeight))
		}

		if (centerWindow) {
			x = parentWindow
				? parentWindow.x + Math.round((parentWindow.width - width) / 2)
				: available.x + Math.round((available.width - width) / 2)
			y = parentWindow
				? parentWindow.y + Math.round((parentWindow.height - height) / 2)
				: available.y + Math.round((available.height - height) / 2)
		}

		x = Math.max(available.x + margin,
			Math.min(x, available.x + available.width - width - margin))
		y = Math.max(available.y + margin,
			Math.min(y, available.y + available.height - height - margin))
		return false
	}

	function activateDialog() {
		if (!visible)
			return
		raise()
		requestActivate()
		Qt.callLater(function() {
			if (productWindow.visible)
				hostedDialog.applyInitialFocus()
		})
	}

	function syncVisibility() {
		if (!productOpen) {
			if (visible)
				hide()
			return
		}

		const nextDialogId = currentDialogId()
		const newSurface = nextDialogId !== presentedDialogId
		if (!visible) {
			if (beforeOpen && beforeOpen() === false)
				return
			// Resolve the native geometry before show(). QmlDialog may continue to
			// measure or hydrate its body, but the first presented frame and every
			// later frame retain this controller-owned viewport.
			ensureUsableGeometry(newSurface, true)
			presentedDialogId = nextDialogId
			show()
			openedWindow()
			activateDialog()
		} else if (newSurface) {
			ensureUsableGeometry(true, true)
			presentedDialogId = nextDialogId
			activateDialog()
		}
	}

	function applyInitialFocus() {
		return hostedDialog.applyInitialFocus()
	}

	function focusVisualFixture(state, surfaceVariant) {
		if (!visible || !hostedDialog.visible)
			return ""
		requestActivate()
		return String(hostedDialog.applyInitialFocus() || "")
	}

	Component.onCompleted: syncVisibility()
	onScreenChanged: Qt.callLater(function() {
		if (productWindow.visible && !productWindow.geometryStore)
			productWindow.ensureUsableGeometry(false, false)
	})
	onActiveChanged: {
		if (active && visible && !hostedDialog.hasActiveDialogFocus())
			Qt.callLater(function() { hostedDialog.applyInitialFocus() })
	}
	onClosing: function(closeEvent) {
		if (productOpen) {
			closeEvent.accepted = false
			controller.requestClose()
			return
		}
		closeEvent.accepted = true
	}

	Connections {
		target: controller
		function onStateChanged() { productWindow.syncVisibility() }
	}

	Item {
		id: productWindowContent
		anchors.fill: parent
	}

	QmlDialog {
		id: hostedDialog
		objectName: "productDialogWindowDialog"
		popupParent: productWindowContent
		excludeSettings: true
		nativeWindowHosted: true
		visualFixtureMode: productWindow.visualFixtureMode
	}
}
