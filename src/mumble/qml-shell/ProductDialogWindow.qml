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
	property bool visibilitySyncPending: false
	property int restoreVisibility: Window.Windowed
	// Decided whenever a dialog is presented: confirmations, notices and short
	// forms follow their content instead of the controller's nominal size.
	property bool contentFitPresentation: false
	property bool contentFitPending: false
	readonly property int contentFitMaximumHeight: 720
	// The hosted dialog lays out at this width. Pre-show measurement assigns it
	// synchronously because a hidden platform window may report its resize later.
	property real layoutWidth: width
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
	readonly property int standardMinimumHeight: Math.max(240,
		Math.min(360, availableGeometry().height - Theme.space5 * 2))
	minimumHeight: contentFitPresentation
		? Math.min(standardMinimumHeight, hostedDialog.minimumContentSizedHeight)
		: standardMinimumHeight
	visible: false
	color: Theme.shellBackground
	flags: Qt.Window
	modality: Qt.WindowModal
	transientParent: parentWindow

	function availableGeometry() {
		// Window.screen is a QQuickScreenInfo, which has no availableGeometry. Use
		// its size and position, bounded by the desktop's available size so a
		// single-monitor taskbar is still respected.
		const info = screen
		if (info && Number(info.width) > 0 && Number(info.height) > 0) {
			const availableWidth = Number(info.desktopAvailableWidth) > 0
				? Math.min(info.width, info.desktopAvailableWidth) : info.width
			const availableHeight = Number(info.desktopAvailableHeight) > 0
				? Math.min(info.height, info.desktopAvailableHeight) : info.height
			return Qt.rect(Number(info.virtualX || 0), Number(info.virtualY || 0), availableWidth, availableHeight)
		}
		return Qt.rect(0, 0, Math.max(width, 1280), Math.max(height, 720))
	}

	function currentDialogId() {
		if (!controller)
			return ""
		return String(controller.dialogId || (controller.state || {}).id
			|| controller.kind || "product-dialog")
	}

	function releaseGeometryStore() {
		if (geometryStore && typeof geometryStore.releaseWindow === "function")
			geometryStore.releaseWindow(productWindow)
	}

	function contentFitHeight(naturalHeight, maximumUsableHeight) {
		const preferredHeight = Number(controller && controller.preferredHeight || 700)
		const cap = Math.min(maximumUsableHeight, Math.max(preferredHeight, contentFitMaximumHeight))
		return Math.max(minimumHeight, Math.min(Math.ceil(naturalHeight), cap))
	}

	function ensureUsableGeometry(usePreferredSize, centerWindow) {
		const dialogId = currentDialogId()
		if (centerWindow)
			contentFitPresentation = !!hostedDialog.contentFitNativeSurface
		if (contentFitPresentation) {
			// A content-sized dialog is laid out afresh on every presentation. A
			// persisted size would restore the empty band this sizing removes, and
			// the previous dialog's key must not record this surface's geometry.
			releaseGeometryStore()
		} else if (geometryStore && dialogId.length > 0
				&& geometryStore.restoreWindow(productWindow, "dialog:" + dialogId,
					minimumWidth, minimumHeight)) {
			return true
		}
		const available = availableGeometry()
		const margin = Theme.space4
		const maximumUsableWidth = Math.max(minimumWidth, available.width - margin * 2)
		const maximumUsableHeight = Math.max(minimumHeight, available.height - margin * 2)
		if (usePreferredSize || (contentFitPresentation && centerWindow)) {
			const preferredWidth = Math.max(minimumWidth,
				Number(controller && controller.preferredWidth || 920))
			const preferredHeight = Math.max(minimumHeight,
				Number(controller && controller.preferredHeight || 700))
			const nextWidth = Math.min(preferredWidth, maximumUsableWidth)
			width = nextWidth
			layoutWidth = nextWidth
			height = contentFitPresentation
				? contentFitHeight(hostedDialog.measureNativeContentHeight(), maximumUsableHeight)
				: Math.min(preferredHeight, maximumUsableHeight)
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
		if (visibility === Window.Minimized) {
			if (restoreVisibility === Window.Maximized)
				showMaximized()
			else if (restoreVisibility === Window.FullScreen)
				showFullScreen()
			else
				showNormal()
		}
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
			// Resolve the native geometry before show(). Workspace dialogs may
			// continue to measure or hydrate their body, but the first presented
			// frame and every later frame retain this controller-owned viewport.
			// Content-sized dialogs are measured here, so their first frame is final.
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

	// Opening a second QQuickWindow synchronously from the controller callback can
	// re-enter the threaded scene-graph exposure path while the owning window is
	// still dispatching the menu action. Defer the native show until that callback
	// has unwound so each render loop can complete its current synchronization.
	function scheduleVisibilitySync() {
		if (visibilitySyncPending)
			return
		visibilitySyncPending = true
		Qt.callLater(function() {
			visibilitySyncPending = false
			syncVisibility()
		})
	}

	// Validation messages and other live edits can change a content-sized
	// dialog after it opens. Follow them, keeping the window on screen.
	function scheduleContentFit() {
		if (!contentFitPresentation || contentFitPending)
			return
		contentFitPending = true
		Qt.callLater(function() {
			productWindow.contentFitPending = false
			productWindow.applyContentFit()
		})
	}

	function applyContentFit() {
		if (!visible || !contentFitPresentation || visibility !== Window.Windowed
				|| presentedDialogId !== currentDialogId())
			return
		const available = availableGeometry()
		const margin = Theme.space4
		const nextHeight = contentFitHeight(Math.max(hostedDialog.minimumContentSizedHeight,
			hostedDialog.naturalContentSizedHeight), Math.max(minimumHeight, available.height - margin * 2))
		if (nextHeight === height)
			return
		height = nextHeight
		y = Math.max(available.y + margin,
			Math.min(y, available.y + available.height - nextHeight - margin))
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

	Component.onCompleted: scheduleVisibilitySync()
	onWidthChanged: layoutWidth = width
	onVisibilityChanged: {
		if (visibility === Window.Windowed || visibility === Window.Maximized
				|| visibility === Window.FullScreen)
			restoreVisibility = visibility
	}
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
		function onStateChanged() { productWindow.scheduleVisibilitySync() }
	}

	Connections {
		target: hostedDialog
		function onNaturalContentSizedHeightChanged() { productWindow.scheduleContentFit() }
	}

	Item {
		id: productWindowContent
		width: productWindow.layoutWidth
		height: productWindow.height
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
