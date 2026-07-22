import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
    id: viewer
	objectName: "attachmentViewerWindow"
	readonly property string surfaceId: "attachmentViewer.window"

	required property var attachment
	signal saveRequested(var attachment)
	signal refreshRequested(var attachment)
	signal originalRetryRequested(var attachment)
	property string lastRefreshSource: ""
	property bool retryingSource: false
	property bool originalTimedOut: false
	property int originalLoadTimeout: 15000
	property real zoom: 1.0
	property real panX: 0
	property real panY: 0
	property bool grabbing: false
	property var geometryStore: typeof windowStateStore !== "undefined" ? windowStateStore : null
	readonly property string fullSource: safeRenderImageSource(attachment ? attachment.url : "")
	readonly property string thumbnailSource: safeRenderImageSource(attachment ? attachment.thumbnailUrl : "")
	readonly property string sourceUrl: fullSource || thumbnailSource
	readonly property string displayTitle: safeText(attachment
		? (attachment.alt || attachment.name || qsTr("Image attachment")) : "", 512)
		|| qsTr("Image attachment")
	readonly property string renderSource: safeRenderImageSource(sourceUrl)
	readonly property bool managedAnimated: /^file:\/\//i.test(renderSource)
	readonly property int loadStatus: managedAnimated ? animation.status : picture.status
	readonly property bool loadFailed: loadStatus === Image.Error || renderSource.length === 0
	readonly property bool compactLayout: width < 560 || height < 440
	readonly property string originalState: safeText(attachment ? attachment.originalState : "", 32).toLowerCase()
	readonly property bool originalPending: originalState === "loading" && fullSource.length === 0
	readonly property bool originalLoading: originalPending && !originalTimedOut
	readonly property bool originalFailed: (originalState === "error" || originalTimedOut)
		&& fullSource.length === 0
	readonly property real actualSizeZoom: Math.min(256, 1 / Math.max(0.0001, fitScale()))
	readonly property real maximumZoom: Math.max(12, actualSizeZoom)
	readonly property bool canSaveOriginal: attachment && (safeText(attachment.inlineToken, 128).length > 0
		|| safeText(attachment.assetId !== undefined ? attachment.assetId : attachment.assetID, 128).length > 0)

	palette.window: Theme.shellBackground
	palette.active.base: Theme.surfaceRaised
	palette.inactive.base: Theme.surfaceRaised
	palette.alternateBase: Theme.panel
	palette.active.button: Theme.surfaceRaised
	palette.inactive.button: Theme.surfaceRaised
	palette.active.text: Theme.textMain
	palette.inactive.text: Theme.textMain
	palette.active.windowText: Theme.textMain
	palette.inactive.windowText: Theme.textMain
	palette.active.buttonText: Theme.textStrong
	palette.inactive.buttonText: Theme.textStrong
	palette.active.brightText: Theme.textStrong
	palette.inactive.brightText: Theme.textStrong
	palette.active.highlight: Theme.accent
	palette.inactive.highlight: Theme.accent
	palette.active.highlightedText: Theme.contrastText(Theme.accent)
	palette.inactive.highlightedText: Theme.contrastText(Theme.accent)
	palette.placeholderText: Theme.textMuted
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.surfaceRaised
	palette.inactive.toolTipBase: Theme.surfaceRaised
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.disabled.window: Theme.shellBackground
	palette.disabled.base: Theme.panel
	palette.disabled.alternateBase: Theme.panel
	palette.disabled.button: Theme.panel
	palette.disabled.text: Theme.textMuted
	palette.disabled.windowText: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
	palette.disabled.brightText: Theme.textMuted
	palette.disabled.highlight: Theme.surfaceBorder
	palette.disabled.highlightedText: Theme.textMuted
	palette.disabled.placeholderText: Theme.textMuted
	palette.disabled.light: Theme.surfaceBorder
	palette.disabled.midlight: Theme.panel
	palette.disabled.mid: Theme.divider
	palette.disabled.dark: Theme.rail
	palette.disabled.shadow: Theme.strip
	palette.disabled.link: Theme.textMuted
	palette.disabled.linkVisited: Theme.textMuted
	palette.disabled.toolTipBase: Theme.panel
	palette.disabled.toolTipText: Theme.textMuted

	function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
		if (/^(image:\/\/mumble\/|qrc:\/)/i.test(source))
			return source
		return /^file:\/\//i.test(source)
			&& /\/mumble-qml-images-[A-Za-z0-9]+\/[0-9a-f]{64}-[0-9a-f-]{36}\.gif$/i.test(source)
			? source : ""
    }

	function focusRetryIfFailed() {
		if (!visible || !loadFailed)
			return
		requestActivate()
		Qt.callLater(function() {
			if (viewer.visible && viewer.loadFailed && retryButton.visible && retryButton.enabled)
				retryButton.forceActiveFocus(Qt.OtherFocusReason)
		})
	}

	function retryLoad() {
		lastRefreshSource = renderSource
		refreshRequested(attachment)
		if (renderSource.length > 0) {
			retryingSource = true
			Qt.callLater(function() { viewer.retryingSource = false })
		}
	}

	function retryOriginal() {
		originalTimedOut = false
		originalRetryRequested(attachment)
	}

	function declaredImageSize() {
		const sourceItem = viewer.managedAnimated ? animation : picture
		const sourceSize = sourceItem.sourceSize
		const sourceWidth = Math.max(0, Number(sourceSize.width || sourceItem.implicitWidth || 0))
		const sourceHeight = Math.max(0, Number(sourceSize.height || sourceItem.implicitHeight || 0))
		// Once the original has loaded, its decoded dimensions are authoritative.
		// Attachment metadata remains the stable fit target while only the preview
		// is available, avoiding a visible resize during the background swap.
		if (fullSource.length > 0 && loadStatus === Image.Ready
				&& sourceWidth > 0 && sourceHeight > 0)
			return Qt.size(sourceWidth, sourceHeight)
		const width = Math.max(0, Number(attachment ? attachment.width : 0))
		const height = Math.max(0, Number(attachment ? attachment.height : 0))
		if (width > 0 && height > 0)
			return Qt.size(width, height)
		// Without an explicit sourceSize request Qt reports the provider's decoded
		// pixel dimensions here. Retaining that natural size gives old attachments
		// correct fit and 1:1 behaviour without a second downsample in QML.
		return Qt.size(Math.max(1, sourceWidth), Math.max(1, sourceHeight))
	}

	function fitScale() {
		const source = declaredImageSize()
		return Math.min(1, imageStage.width / Math.max(1, source.width),
			imageStage.height / Math.max(1, source.height))
	}

	function clampPan() {
		const target = viewer.managedAnimated ? animation : picture
		const overflowX = Math.max(0, target.width - imageStage.width) / 2
		const overflowY = Math.max(0, target.height - imageStage.height) / 2
		panX = Math.max(-overflowX, Math.min(overflowX, panX))
		panY = Math.max(-overflowY, Math.min(overflowY, panY))
	}

	function resetZoom() {
		zoom = 1
		panX = 0
		panY = 0
	}

	function zoomBy(factor) {
		zoom = Math.max(0.25, Math.min(maximumZoom, zoom * factor))
		Qt.callLater(clampPan)
	}

	function setActualSize() {
		zoom = actualSizeZoom
		Qt.callLater(clampPan)
	}

	function panBy(dx, dy) {
		panX += dx
		panY += dy
		clampPan()
	}

	onLoadFailedChanged: focusRetryIfFailed()
	onOriginalStateChanged: if (originalState !== "loading") originalTimedOut = false
	onFullSourceChanged: if (fullSource.length > 0) originalTimedOut = false
	onVisibleChanged: focusRetryIfFailed()
	onActiveChanged: if (active) focusRetryIfFailed()
	onWidthChanged: Qt.callLater(clampPan)
	onHeightChanged: Qt.callLater(clampPan)
	Component.onCompleted: {
		if (geometryStore)
			geometryStore.restoreWindow(viewer, "attachment-viewer", minimumWidth, minimumHeight)
		focusRetryIfFailed()
		Qt.callLater(function() {
			if (viewer.visible && !viewer.loadFailed)
				imageStage.forceActiveFocus(Qt.OtherFocusReason)
		})
	}

    width: 900
    height: 680
    minimumWidth: 420
    minimumHeight: 320
    visible: true
    color: Theme.shellBackground
    title: displayTitle

	Shortcut {
		sequence: "Escape"
		onActivated: viewer.close()
	}

	Timer {
		id: originalLoadTimer
		interval: Math.max(100, viewer.originalLoadTimeout)
		running: viewer.originalPending && !viewer.originalTimedOut
		repeat: false
		onTriggered: viewer.originalTimedOut = true
	}

    Rectangle {
        anchors.fill: parent
        color: Theme.shellBackground
        border.color: Theme.divider
		Accessible.role: Accessible.Pane
		Accessible.name: viewer.displayTitle

		Rectangle {
			id: viewerHeader
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			height: Theme.controlHeight + (viewer.compactLayout ? Theme.space3 : Theme.space4)
			color: Theme.panel
			border.color: Theme.divider

			Label {
				anchors.left: parent.left
				anchors.right: saveButton.visible ? saveButton.left : closeButton.left
				anchors.leftMargin: viewer.compactLayout ? Theme.space3 : Theme.space4
				anchors.rightMargin: Theme.space2
				anchors.verticalCenter: parent.verticalCenter
				textFormat: Text.PlainText
				text: viewer.displayTitle
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.bold: true
				elide: Text.ElideMiddle
			}

			ModernIconButton {
				id: saveButton
				objectName: "attachmentViewerSaveButton"
				anchors.right: closeButton.left
				anchors.rightMargin: Theme.space1
				anchors.verticalCenter: parent.verticalCenter
				visible: viewer.canSaveOriginal
				iconName: "download"
				text: qsTr("Save original")
				Accessible.name: qsTr("Save original %1").arg(viewer.displayTitle)
				ToolTip.visible: hovered
				ToolTip.text: text
				onClicked: viewer.saveRequested(viewer.attachment)
			}

			ModernIconButton {
				id: closeButton
				objectName: "attachmentViewerCloseButton"
				anchors.right: parent.right
				anchors.rightMargin: Theme.space2
				anchors.verticalCenter: parent.verticalCenter
				iconName: "close"
				Accessible.name: qsTr("Close attachment viewer")
				onClicked: viewer.close()
			}
		}

		Item {
			id: imageStage
			objectName: "attachmentViewerStage"
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: viewerHeader.bottom
			anchors.bottom: parent.bottom
			clip: true
			focus: true
			activeFocusOnTab: true
			Accessible.role: Accessible.Pane
			Accessible.name: qsTr("Image viewer. Use the mouse wheel or plus and minus to zoom, drag to pan, and zero to fit.")

			Image {
				id: picture
				objectName: "attachmentViewerImage"
				source: viewer.retryingSource || viewer.managedAnimated ? "" : viewer.renderSource
				asynchronous: true
				cache: false
				width: viewer.declaredImageSize().width * viewer.fitScale() * viewer.zoom
				height: viewer.declaredImageSize().height * viewer.fitScale() * viewer.zoom
				x: Math.round((imageStage.width - width) / 2 + viewer.panX)
				y: Math.round((imageStage.height - height) / 2 + viewer.panY)
				fillMode: Image.PreserveAspectFit
				smooth: true
				visible: !viewer.managedAnimated
				Accessible.role: Accessible.Graphic
				Accessible.name: viewer.displayTitle
				Accessible.ignored: !visible
				onStatusChanged: if (status === Image.Error && viewer.renderSource.length > 0
						&& viewer.lastRefreshSource !== viewer.renderSource) {
					viewer.lastRefreshSource = viewer.renderSource
					viewer.refreshRequested(viewer.attachment)
				}
            }

			AnimatedImage {
				id: animation
				objectName: "attachmentViewerAnimation"
				source: viewer.retryingSource || !viewer.managedAnimated ? "" : viewer.renderSource
				asynchronous: true
				cache: false
				width: viewer.declaredImageSize().width * viewer.fitScale() * viewer.zoom
				height: viewer.declaredImageSize().height * viewer.fitScale() * viewer.zoom
				x: Math.round((imageStage.width - width) / 2 + viewer.panX)
				y: Math.round((imageStage.height - height) / 2 + viewer.panY)
				fillMode: Image.PreserveAspectFit
				playing: viewer.visible && status === AnimatedImage.Ready
				visible: viewer.managedAnimated
				Accessible.role: Accessible.Graphic
				Accessible.name: viewer.displayTitle
				Accessible.ignored: !visible
				onStatusChanged: if (status === AnimatedImage.Error && viewer.renderSource.length > 0
						&& viewer.lastRefreshSource !== viewer.renderSource) {
					viewer.lastRefreshSource = viewer.renderSource
					viewer.refreshRequested(viewer.attachment)
				}
			}

			ModernBusyIndicator {
				objectName: "attachmentViewerBusyIndicator"
				anchors.centerIn: parent
				running: viewer.loadStatus === Image.Loading
				visible: running
				Accessible.name: qsTr("Loading %1").arg(viewer.displayTitle)
				Accessible.ignored: !visible
			}

			ColumnLayout {
				z: 10
				anchors.centerIn: parent
				width: Math.max(1, parent.width - Theme.space6 * 2)
				visible: viewer.loadFailed
				spacing: Theme.space3

				Label {
					objectName: "attachmentViewerError"
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: qsTr("Attachment unavailable")
					color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
					horizontalAlignment: Text.AlignHCenter
					wrapMode: Text.Wrap
					Accessible.role: Accessible.AlertMessage
					Accessible.name: text
					Accessible.description: viewer.displayTitle
					Accessible.ignored: !visible
				}

				ModernButton {
					id: retryButton
					objectName: "attachmentViewerRetryButton"
					Layout.alignment: Qt.AlignHCenter
					text: qsTr("Retry")
					tone: "retry"
					Accessible.name: qsTr("Retry loading %1").arg(viewer.displayTitle)
					onClicked: viewer.retryLoad()
				}
			}

			MouseArea {
				id: panArea
				anchors.fill: parent
					acceptedButtons: Qt.LeftButton | Qt.MiddleButton
				cursorShape: viewer.grabbing ? Qt.ClosedHandCursor
					: ((viewer.managedAnimated ? animation.width : picture.width) > imageStage.width
						|| (viewer.managedAnimated ? animation.height : picture.height) > imageStage.height
						? Qt.OpenHandCursor : Qt.ArrowCursor)
				property real previousX
				property real previousY
				onPressed: event => {
					previousX = event.x
					previousY = event.y
					viewer.grabbing = true
					imageStage.forceActiveFocus()
				}
				onPositionChanged: event => {
					if (!pressed || !viewer.grabbing)
						return
					viewer.panBy(event.x - previousX, event.y - previousY)
					previousX = event.x
					previousY = event.y
				}
				onReleased: viewer.grabbing = false
				onCanceled: viewer.grabbing = false
					onDoubleClicked: event => {
						if (event.button === Qt.LeftButton)
							viewer.zoom > 1.001 ? viewer.resetZoom() : viewer.setActualSize()
					}
			}

			WheelHandler {
				target: null
				onWheel: event => viewer.zoomBy(event.angleDelta.y >= 0 ? 1.2 : 1 / 1.2)
			}

			Rectangle {
				id: originalStatusPill
				objectName: "attachmentViewerOriginalStatus"
				anchors.horizontalCenter: parent.horizontalCenter
				anchors.top: parent.top
				anchors.topMargin: Theme.space3
				z: 5
				// Keep the preview interactive while the original swaps in silently. Only
				// surface a status when the background request needs user attention.
				visible: viewer.originalFailed
				width: originalStatusContent.implicitWidth + Theme.space3 * 2
				height: Theme.controlHeight
				radius: height / 2
				color: Theme.panel
				border.color: viewer.originalFailed ? Theme.warning : Theme.surfaceBorder
				Row {
					id: originalStatusContent
					anchors.centerIn: parent
					spacing: Theme.space2
					Label {
						id: originalStatusLabel
						objectName: "attachmentViewerOriginalStatusLabel"
						anchors.verticalCenter: parent.verticalCenter
						textFormat: Text.PlainText
						text: viewer.originalTimedOut ? qsTr("Preview shown · full resolution delayed")
							: qsTr("Preview shown · full resolution unavailable")
						color: viewer.originalFailed ? Theme.warning : Theme.textMuted
						font.pixelSize: Theme.fontCaption
					}
					ModernButton {
						id: originalRetryButton
						objectName: "attachmentViewerOriginalRetryButton"
						anchors.verticalCenter: parent.verticalCenter
						visible: viewer.originalFailed
						dense: true
						tone: "retry"
						text: qsTr("Retry")
						Accessible.name: qsTr("Retry loading the full-resolution image")
						onClicked: viewer.retryOriginal()
					}
				}
			}

			Rectangle {
				id: zoomToolbar
				objectName: "attachmentViewerZoomToolbar"
				anchors.horizontalCenter: parent.horizontalCenter
				anchors.bottom: parent.bottom
				anchors.bottomMargin: Theme.space3
				z: 6
				width: zoomControls.implicitWidth + Theme.space2 * 2
				height: zoomControls.implicitHeight + Theme.space1 * 2
				radius: Theme.innerRadius
				color: Theme.panel
				border.color: Theme.surfaceBorder
				Accessible.role: Accessible.ToolBar
				Accessible.name: qsTr("Image zoom controls")

				Row {
					id: zoomControls
					anchors.centerIn: parent
					spacing: Theme.space1
					ModernIconButton {
						objectName: "attachmentViewerZoomOut"
						iconName: "minimize"
						enabled: viewer.zoom > 0.251
						Accessible.name: qsTr("Zoom out")
						onClicked: viewer.zoomBy(1 / 1.2)
					}
					ModernIconButton {
						objectName: "attachmentViewerFit"
						width: Theme.controlHeight + Theme.space6
						text: Math.round(viewer.fitScale() * viewer.zoom * 100) + "%"
						Accessible.name: qsTr("Fit image to window")
						onClicked: viewer.resetZoom()
					}
					ModernIconButton {
						objectName: "attachmentViewerActualSize"
						width: Theme.controlHeight + Theme.space4
						text: "1:1"
						Accessible.name: qsTr("Show image at actual size")
						onClicked: viewer.setActualSize()
					}
					ModernIconButton {
						objectName: "attachmentViewerZoomIn"
						iconName: "add"
						enabled: viewer.zoom < viewer.maximumZoom - 0.001
						Accessible.name: qsTr("Zoom in")
						onClicked: viewer.zoomBy(1.2)
					}
				}
			}

			Keys.onPressed: event => {
				const step = event.modifiers & Qt.ShiftModifier ? 80 : 32
				if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
					viewer.zoomBy(1.2)
				else if (event.key === Qt.Key_Minus)
					viewer.zoomBy(1 / 1.2)
				else if (event.key === Qt.Key_0)
					viewer.resetZoom()
				else if (event.key === Qt.Key_1)
					viewer.setActualSize()
				else if (event.key === Qt.Key_Left)
					viewer.panBy(step, 0)
				else if (event.key === Qt.Key_Right)
					viewer.panBy(-step, 0)
				else if (event.key === Qt.Key_Up)
					viewer.panBy(0, step)
				else if (event.key === Qt.Key_Down)
					viewer.panBy(0, -step)
				else
					return
				event.accepted = true
			}
		}
    }

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}
}
