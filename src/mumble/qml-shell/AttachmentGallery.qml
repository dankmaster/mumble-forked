import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Item {
    id: root

    required property var attachments
    signal attachmentRequested(var attachment)
    signal attachmentRefreshRequested()
    readonly property bool compactLayout: width < 360
	readonly property var visibleAttachments: boundedAttachments()

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}

	function boundedAttachments() {
		const source = attachments || []
		const result = []
		const count = Math.min(16, Math.max(0, Number(source.length) || 0))
		for (let index = 0; index < count; ++index) {
			const attachment = source[index]
			if (attachment && typeof attachment === "object" && !Array.isArray(attachment))
				result.push(attachment)
		}
		return result
	}

    function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
    }

    implicitHeight: gallery.implicitHeight
	visible: visibleAttachments.length > 0
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Message attachments")
	Accessible.description: qsTr("%1 attachments").arg(visibleAttachments.length)

    Flow {
        id: gallery
        anchors.left: parent.left
        anchors.right: parent.right
        height: implicitHeight
		spacing: Theme.space2

        Repeater {
            model: root.visibleAttachments
            delegate: Rectangle {
                id: attachmentTile
                required property var modelData
                required property int index
				readonly property string sourceUrl: root.safeText(
					modelData.thumbnailUrl || modelData.url || "", 2048)
				readonly property string label: root.safeText(
					modelData.alt || modelData.name || qsTr("Image attachment"), 512)
					|| qsTr("Image attachment")
				readonly property string stableId: root.safeText(modelData.id, 128)
					|| String(attachmentTile.index)
                readonly property real requestedWidth: Number(modelData.width) > 0 ? Number(modelData.width) : 240
                readonly property real requestedHeight: Number(modelData.height) > 0 ? Number(modelData.height) : 160
                readonly property real aspectRatio: Math.max(0.2, Math.min(5,
                    requestedHeight / Math.max(1, requestedWidth)))

				objectName: "attachment_" + stableId
                width: Math.max(1, Math.min(Math.max(requestedWidth, 180),
                    Math.min(320, Math.max(1, gallery.width))))
                height: Math.min(240, Math.max(root.compactLayout ? 96 : 120,
                    Math.round(width * aspectRatio)))
				radius: Theme.innerRadius
                color: Theme.strip
				border.color: !attachmentAction.enabled ? Theme.divider
					: attachmentAction.activeFocus ? Theme.focus
					: attachmentAction.down ? Theme.accent
					: attachmentAction.hovered ? Theme.surfaceBorder : Theme.divider
				border.width: attachmentAction.activeFocus ? Theme.focusRingWidth : 1
                clip: true
                Image {
                    id: attachmentImage
                    anchors.fill: parent
					anchors.margins: attachmentTile.border.width
                    source: root.safeRenderImageSource(attachmentTile.sourceUrl)
                    asynchronous: true
                    cache: false
                    sourceSize: Qt.size(Math.min(640, width * Screen.devicePixelRatio),
                                        Math.min(480, height * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectFit
                    onStatusChanged: if (status === Image.Error && source.toString().length > 0)
                                         root.attachmentRefreshRequested()
                }

				ModernBusyIndicator {
					objectName: "attachmentBusyIndicator_" + attachmentTile.stableId
                    anchors.centerIn: parent
                    running: attachmentImage.status === Image.Loading
                    visible: running
					Accessible.name: qsTr("Loading %1").arg(attachmentTile.label)
                }

                Label {
					objectName: "attachmentError_" + attachmentTile.stableId
					textFormat: Text.PlainText
                    anchors.centerIn: parent
					width: Math.max(1, parent.width - Theme.space3 * 2)
					visible: attachmentImage.status === Image.Error
						|| root.safeRenderImageSource(attachmentTile.sourceUrl).length === 0
                    text: qsTr("Attachment unavailable")
                    color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
					Accessible.role: Accessible.AlertMessage
					Accessible.name: text
					Accessible.description: attachmentTile.label
                }

				Rectangle {
					objectName: "attachmentStateOverlay_" + attachmentTile.stableId
					anchors.fill: parent
					anchors.margins: attachmentTile.border.width
					radius: Math.max(0, attachmentTile.radius - attachmentTile.border.width)
					color: !attachmentAction.enabled
						? Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.68)
						: attachmentAction.down ? Theme.accentSubtle
						: attachmentAction.hovered
							? Qt.rgba(Theme.surfaceHover.r, Theme.surfaceHover.g, Theme.surfaceHover.b, 0.32)
							: "transparent"
					Behavior on color { ColorAnimation { duration: Theme.motionFast } }
				}

                Button {
                    id: attachmentAction
					objectName: "attachmentAction_" + attachmentTile.stableId
                    anchors.fill: parent
                    hoverEnabled: true
					enabled: attachmentTile.modelData.enabled === undefined
						|| !!attachmentTile.modelData.enabled
                    background: null
                    contentItem: Item {}
                    Accessible.name: attachmentTile.label
                    Accessible.description: qsTr("Attachment %1 of %2")
						.arg(attachmentTile.index + 1).arg(root.visibleAttachments.length)
                    onClicked: root.attachmentRequested(attachmentTile.modelData)
                }
            }
        }
    }
}
