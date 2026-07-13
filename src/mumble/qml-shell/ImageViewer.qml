import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Window {
    id: viewer
    required property var controller
    readonly property var imageState: controller.state.imageViewer || ({})
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    property bool grabbing: false

    function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
    }

    width: 900
    height: 680
    minimumWidth: 420
    minimumHeight: 320
    visible: true
    color: Theme.shellBackground
    flags: Qt.Window | Qt.FramelessWindowHint
    title: controller.title || qsTr("Image")

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

    onWidthChanged: clampPan()
    onHeightChanged: clampPan()
    onClosing: close => {
        close.accepted = false
        controller.requestClose()
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
            height: 42
            color: Theme.panel
            border.color: Theme.divider

            Label {
				textFormat: Text.PlainText
                anchors.left: parent.left
                anchors.right: windowControls.left
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: viewer.title
                color: Theme.textMuted
                font.pixelSize: 11
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
                ToolButton {
                    width: 42; height: parent.height; text: "−"
                    Accessible.name: qsTr("Minimize image viewer")
                    onClicked: viewer.showMinimized()
                }
                ToolButton {
                    width: 42; height: parent.height; text: "×"
                    Accessible.name: qsTr("Close image viewer")
                    onClicked: viewer.controller.requestClose()
                }
            }
        }

        Item {
            id: stage
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 14
            clip: true
            focus: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Image viewer. Use plus and minus to zoom, arrow keys to pan, and zero to fit.")

            Image {
                id: picture
                asynchronous: true
                cache: false
                smooth: true
                source: viewer.safeRenderImageSource(viewer.imageState.src || "")
                width: Math.max(1, Number(viewer.imageState.width || sourceSize.width || 1)) * viewer.fitScale() * viewer.zoom
                height: Math.max(1, Number(viewer.imageState.height || sourceSize.height || 1)) * viewer.fitScale() * viewer.zoom
                x: Math.round((stage.width - width) / 2 + viewer.panX)
                y: Math.round((stage.height - height) / 2 + viewer.panY)
                Accessible.name: viewer.title
                onStatusChanged: if (status === Image.Ready) viewer.resetZoom()
            }

            MouseArea {
                id: panArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
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
                onDoubleClicked: viewer.resetZoom()
            }

            WheelHandler {
                target: null
                onWheel: event => viewer.zoomBy(event.angleDelta.y >= 0 ? 1.2 : 1 / 1.2)
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 4
                padding: 4
                ToolButton {
                    text: "−"; enabled: viewer.zoom > 0.251
                    Accessible.name: qsTr("Zoom out")
                    onClicked: viewer.zoomBy(1 / 1.2)
                }
                ToolButton {
                    width: 62; text: Math.round(viewer.zoom * 100) + "%"
                    Accessible.name: qsTr("Fit image to window")
                    onClicked: viewer.resetZoom()
                }
                ToolButton {
                    text: "+"; enabled: viewer.zoom < 9.999
                    Accessible.name: qsTr("Zoom in")
                    onClicked: viewer.zoomBy(1.2)
                }
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
