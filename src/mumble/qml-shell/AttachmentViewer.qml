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
	property string lastRefreshSource: ""
	property bool retryingSource: false
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

	onLoadFailedChanged: focusRetryIfFailed()
	onVisibleChanged: focusRetryIfFailed()
	onActiveChanged: if (active) focusRetryIfFailed()
	Component.onCompleted: focusRetryIfFailed()

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
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: viewerHeader.bottom
			anchors.bottom: parent.bottom
			clip: true

			Image {
				id: picture
				objectName: "attachmentViewerImage"
            anchors.fill: parent
				anchors.margins: viewer.compactLayout ? Theme.space3 : Theme.space4
			source: viewer.retryingSource || viewer.managedAnimated ? "" : viewer.renderSource
            asynchronous: true
            cache: false
            fillMode: Image.PreserveAspectFit
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
				anchors.fill: parent
				anchors.margins: viewer.compactLayout ? Theme.space3 : Theme.space4
				source: viewer.retryingSource || !viewer.managedAnimated ? "" : viewer.renderSource
				asynchronous: true
				cache: false
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
		}
    }

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}
}
