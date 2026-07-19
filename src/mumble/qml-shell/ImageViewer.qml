import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ApplicationWindow {
    id: viewer
	objectName: "imageViewerWindow"
	readonly property string surfaceId: "imageViewer.window"
    required property var controller
    readonly property var imageState: controller.state.imageViewer || ({})
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    property bool grabbing: false
	readonly property bool compactLayout: width < 560 || height < 440
	readonly property string displayTitle: safeText(controller.title, 512) || qsTr("Image")
	property var geometryStore: typeof windowStateStore !== "undefined" ? windowStateStore : null

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

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}

    function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
    }

    width: 900
    height: 680
    minimumWidth: 420
    minimumHeight: 320
    visible: true
	modality: Qt.WindowModal
    color: Theme.shellBackground
    flags: Qt.Window | Qt.FramelessWindowHint
	title: displayTitle

    function fitScale() {
        const sourceWidth = Math.max(1, Number(imageState.width || picture.sourceSize.width || 1))
        const sourceHeight = Math.max(1, Number(imageState.height || picture.sourceSize.height || 1))
        return Math.min(stage.width / sourceWidth, stage.height / sourceHeight)
    }

    function clampPan() {
        const overflowX = Math.max(0, picture.width - stage.width) / 2
        const overflowY = Math.max(0, picture.height - stage.height) / 2
        panX = Math.max(-overflowX, Math.min(overflowX, panX))
        panY = Math.max(-overflowY, Math.min(overflowY, panY))
    }

    function resetZoom() {
        zoom = 1
        panX = 0
        panY = 0
    }

    function zoomBy(factor) {
        zoom = Math.max(0.25, Math.min(10, zoom * factor))
        clampPan()
    }

    function panBy(dx, dy) {
        panX += dx
        panY += dy
        clampPan()
    }

	Component.onCompleted: if (geometryStore)
		geometryStore.restoreWindow(viewer, "image-viewer", minimumWidth, minimumHeight)

    onWidthChanged: clampPan()
    onHeightChanged: clampPan()
    onClosing: close => {
        close.accepted = false
        controller.requestClose()
    }

	Shortcut {
		sequence: "Escape"
		onActivated: viewer.controller.requestClose()
	}

    Rectangle {
        anchors.fill: parent
        color: Theme.shellBackground
        border.color: Theme.divider
        radius: Theme.innerRadius

        Rectangle {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
			height: Theme.controlHeight + Theme.space2
            color: Theme.panel
            border.color: Theme.divider

            Label {
				textFormat: Text.PlainText
                anchors.left: parent.left
                anchors.right: windowControls.left
				anchors.leftMargin: Theme.space3
				anchors.rightMargin: Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                text: viewer.title
                color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
                font.bold: true
                elide: Text.ElideRight
            }

            DragHandler {
                target: null
                onActiveChanged: if (active) viewer.startSystemMove()
            }

            Row {
                id: windowControls
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.height
                ModernIconButton {
					objectName: "imageViewerMinimize"
					width: titleBar.height; height: parent.height; iconName: "minimize"
                    Accessible.name: qsTr("Minimize image viewer")
                    onClicked: viewer.showMinimized()
                }
                ModernIconButton {
					width: titleBar.height; height: parent.height; iconName: "close"
                    Accessible.name: qsTr("Close image viewer")
                    onClicked: viewer.controller.requestClose()
                }
            }
        }

        Item {
            id: stage
			objectName: "imageViewerStage"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
			anchors.margins: viewer.compactLayout ? Theme.space2 : Theme.space3
            clip: true
            focus: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Image viewer. Use plus and minus to zoom, arrow keys to pan, and zero to fit.")

            Image {
                id: picture
				objectName: "imageViewerImage"
                asynchronous: true
                cache: false
                smooth: true
                source: viewer.safeRenderImageSource(viewer.imageState.src || "")
                width: Math.max(1, Number(viewer.imageState.width || sourceSize.width || 1)) * viewer.fitScale() * viewer.zoom
                height: Math.max(1, Number(viewer.imageState.height || sourceSize.height || 1)) * viewer.fitScale() * viewer.zoom
                x: Math.round((stage.width - width) / 2 + viewer.panX)
                y: Math.round((stage.height - height) / 2 + viewer.panY)
				Accessible.role: Accessible.Graphic
                Accessible.name: viewer.title
                onStatusChanged: if (status === Image.Ready) viewer.resetZoom()
            }

			ModernBusyIndicator {
				objectName: "imageViewerBusyIndicator"
				anchors.centerIn: parent
				running: picture.status === Image.Loading
				visible: running
				Accessible.name: qsTr("Loading %1").arg(viewer.displayTitle)
				Accessible.ignored: !visible
			}

			Label {
				objectName: "imageViewerError"
				anchors.centerIn: parent
				width: Math.max(1, parent.width - Theme.space6 * 2)
				visible: picture.status === Image.Error || picture.source.toString().length === 0
				text: qsTr("Image unavailable")
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontLabel
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
				Accessible.role: Accessible.AlertMessage
				Accessible.name: text
				Accessible.description: viewer.displayTitle
				Accessible.ignored: !visible
			}

            MouseArea {
                id: panArea
                anchors.fill: parent
				acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                cursorShape: viewer.grabbing ? Qt.ClosedHandCursor
                    : (picture.width > stage.width || picture.height > stage.height ? Qt.OpenHandCursor : Qt.ArrowCursor)
                property real previousX
                property real previousY
                onPressed: event => {
                    previousX = event.x; previousY = event.y
                    viewer.grabbing = picture.width > stage.width || picture.height > stage.height
                    stage.forceActiveFocus()
                }
                onPositionChanged: event => {
                    if (!pressed || !viewer.grabbing) return
                    viewer.panBy(event.x - previousX, event.y - previousY)
                    previousX = event.x; previousY = event.y
                }
                onReleased: viewer.grabbing = false
                onCanceled: viewer.grabbing = false
				onDoubleClicked: event => {
					if (event.button === Qt.LeftButton)
						viewer.resetZoom()
				}
            }

            WheelHandler {
                target: null
                onWheel: event => viewer.zoomBy(event.angleDelta.y >= 0 ? 1.2 : 1 / 1.2)
            }

			Rectangle {
				id: zoomToolbar
				objectName: "imageViewerZoomToolbar"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
				anchors.bottomMargin: Theme.space2
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
						objectName: "imageViewerZoomOut"
						text: "−"; enabled: viewer.zoom > 0.251
						Accessible.name: qsTr("Zoom out")
						onClicked: viewer.zoomBy(1 / 1.2)
					}
					ModernIconButton {
						objectName: "imageViewerFit"
						width: Theme.controlHeight + Theme.space5
						text: Math.round(viewer.zoom * 100) + "%"
						Accessible.name: qsTr("Fit image to window")
						onClicked: viewer.resetZoom()
					}
					ModernIconButton {
						objectName: "imageViewerZoomIn"
						text: "+"; enabled: viewer.zoom < 9.999
						Accessible.name: qsTr("Zoom in")
						onClicked: viewer.zoomBy(1.2)
					}
				}
            }

			Rectangle {
				anchors.fill: parent
				anchors.margins: Theme.space1
				visible: stage.activeFocus
				color: "transparent"
				radius: Theme.innerRadius
				border.color: Theme.focus
				border.width: Theme.focusRingWidth
			}

            Keys.onPressed: event => {
                const step = event.modifiers & Qt.ShiftModifier ? 80 : 32
                if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) viewer.zoomBy(1.2)
                else if (event.key === Qt.Key_Minus) viewer.zoomBy(1 / 1.2)
                else if (event.key === Qt.Key_0) viewer.resetZoom()
                else if (event.key === Qt.Key_Left) viewer.panBy(step, 0)
                else if (event.key === Qt.Key_Right) viewer.panBy(-step, 0)
                else if (event.key === Qt.Key_Up) viewer.panBy(0, step)
                else if (event.key === Qt.Key_Down) viewer.panBy(0, -step)
                else return
                event.accepted = true
            }
        }
    }
}
