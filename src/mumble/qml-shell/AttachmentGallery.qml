import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Item {
    id: root

    required property var attachments
    signal attachmentRequested(var attachment)
    signal attachmentRefreshRequested()

    function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
    }

    implicitHeight: gallery.implicitHeight
    visible: !!attachments && attachments.length > 0
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Message attachments")

    Flow {
        id: gallery
        anchors.left: parent.left
        anchors.right: parent.right
        height: implicitHeight
        spacing: 8

        Repeater {
            model: root.attachments || []
            delegate: Rectangle {
                id: attachmentTile
                required property var modelData
                readonly property string sourceUrl: modelData.thumbnailUrl || modelData.url || ""
                readonly property string label: modelData.alt || modelData.name || qsTr("Image attachment")
                readonly property real requestedWidth: Number(modelData.width) > 0 ? Number(modelData.width) : 240
                readonly property real requestedHeight: Number(modelData.height) > 0 ? Number(modelData.height) : 160

                objectName: "attachment_" + (modelData.id || index)
                width: Math.min(Math.max(requestedWidth, 180), Math.min(320, gallery.width))
                height: Math.min(Math.max(requestedHeight, 120), 240)
                radius: 8
                color: Theme.strip
                border.color: attachmentAction.activeFocus ? Theme.focus : Theme.divider
                clip: true
                Image {
                    id: attachmentImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: root.safeRenderImageSource(attachmentTile.sourceUrl)
                    asynchronous: true
                    cache: false
                    sourceSize: Qt.size(Math.min(640, width * Screen.devicePixelRatio),
                                        Math.min(480, height * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectFit
                    onStatusChanged: if (status === Image.Error && source.toString().length > 0)
                                         root.attachmentRefreshRequested()
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: attachmentImage.status === Image.Loading
                    visible: running
                }

                Label {
					textFormat: Text.PlainText
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: attachmentImage.status === Image.Error
                    text: qsTr("Attachment unavailable")
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Button {
                    id: attachmentAction
                    objectName: "attachmentAction_" + (attachmentTile.modelData.id || index)
                    anchors.fill: parent
                    hoverEnabled: true
                    background: null
                    contentItem: Item {}
                    Accessible.name: attachmentTile.label
                    onClicked: root.attachmentRequested(attachmentTile.modelData)
                }
            }
        }
    }
}
