import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Mumble.Theme 1.0

Window {
	id: settingsWindow
	objectName: "settingsWindow"

	required property var controller
	property var parentWindow: null
	property bool visualFixtureMode: false
	property var beforeOpen: null
	property bool initialPlacementComplete: false
	readonly property string surfaceId: "settings.window"
	readonly property var captureRect: Qt.rect(0, 0, width, height)
	readonly property bool settingsOpen: controller && controller.open
		&& String(controller.kind || "") === "settings"
	readonly property real effectiveDevicePixelRatio: screen
		? Math.max(1, Number(screen.devicePixelRatio || 1)) : 1
	readonly property alias hostedDialog: settingsDialog

	title: controller && String(controller.title || "").length > 0
		? String(controller.title) : qsTr("Settings")
	width: 980
	height: 760
	minimumWidth: Math.max(360,
		Math.min(640, availableGeometry().width - Theme.space5 * 2))
	minimumHeight: Math.max(420,
		Math.min(520, availableGeometry().height - Theme.space5 * 2))
	visible: settingsOpen
	color: Theme.shellBackground
	flags: Qt.Window
	modality: Qt.NonModal
	transientParent: parentWindow

	function availableGeometry() {
		// Some headless/offscreen platform plugins expose a QScreen before its
		// availableGeometry value has been populated. Treat that transient state
		// like an absent screen instead of poisoning all size bindings with NaN.
		const geometry = screen ? screen.availableGeometry : null
		if (geometry && Number(geometry.width) > 0 && Number(geometry.height) > 0)
			return geometry
		return Qt.rect(0, 0, Math.max(width, 1280), Math.max(height, 720))
	}

	function ensureUsableGeometry(centerOnFirstPresentation) {
		const available = availableGeometry()
		const margin = Theme.space4
		const maximumUsableWidth = Math.max(minimumWidth, available.width - margin * 2)
		const maximumUsableHeight = Math.max(minimumHeight, available.height - margin * 2)
		width = Math.max(minimumWidth, Math.min(width, maximumUsableWidth))
		height = Math.max(minimumHeight, Math.min(height, maximumUsableHeight))

		if (centerOnFirstPresentation) {
			const preferredX = parentWindow
				? parentWindow.x + Math.round((parentWindow.width - width) / 2)
				: available.x + Math.round((available.width - width) / 2)
			const preferredY = parentWindow
				? parentWindow.y + Math.round((parentWindow.height - height) / 2)
				: available.y + Math.round((available.height - height) / 2)
			x = preferredX
			y = preferredY
		}

		x = Math.max(available.x + margin,
			Math.min(x, available.x + available.width - width - margin))
		y = Math.max(available.y + margin,
			Math.min(y, available.y + available.height - height - margin))
	}

	function activateSettings() {
		if (!visible)
			return
		raise()
		requestActivate()
		Qt.callLater(function() {
			if (settingsWindow.visible)
				settingsDialog.applyInitialFocus()
		})
	}

	function focusVisualFixture(state, surfaceVariant) {
		if (!visible || !settingsDialog.visible)
			return ""
		requestActivate()
		return String(settingsDialog.applyInitialFocus() || "")
	}

	onVisibleChanged: {
		if (!visible)
			return
		if (!initialPlacementComplete) {
			ensureUsableGeometry(true)
			initialPlacementComplete = true
		} else {
			ensureUsableGeometry(false)
		}
		activateSettings()
	}
	onScreenChanged: Qt.callLater(function() {
		if (settingsWindow.visible)
			settingsWindow.ensureUsableGeometry(false)
	})
	onActiveChanged: {
		if (active && visible && !settingsDialog.hasActiveDialogFocus())
			Qt.callLater(function() { settingsDialog.applyInitialFocus() })
	}
	onClosing: function(closeEvent) {
		if (settingsOpen) {
			closeEvent.accepted = false
			controller.requestClose()
			return
		}
		closeEvent.accepted = true
	}

	Item {
		id: settingsWindowContent
		anchors.fill: parent
	}

	QmlDialog {
		id: settingsDialog
		objectName: "settingsWindowDialog"
		popupParent: settingsWindowContent
		settingsOnly: true
		nativeWindowHosted: true
		visualFixtureMode: settingsWindow.visualFixtureMode
		beforeOpen: settingsWindow.beforeOpen
	}
}
