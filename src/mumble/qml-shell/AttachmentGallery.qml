import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
    id: root

    required property var attachments
    signal attachmentRequested(var attachment)
	signal attachmentDownloadRequested(var attachment)
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

	function attachmentKindLabel(kind, mime) {
		const normalizedKind = safeText(kind, 64).toLowerCase()
		const normalizedMime = safeText(mime, 128).toLowerCase()
		if (normalizedKind === "image" || normalizedMime.indexOf("image/") === 0)
			return qsTr("Image")
		if (normalizedKind === "video" || normalizedMime.indexOf("video/") === 0)
			return qsTr("Video")
		if (normalizedKind === "audio" || normalizedMime.indexOf("audio/") === 0)
			return qsTr("Audio")
		if (normalizedKind === "document")
			return qsTr("Document")
		return qsTr("File")
	}

	function formatByteSize(value) {
		let bytes = Number(value)
		if (!isFinite(bytes) || bytes <= 0)
			return ""
		const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB")]
		let unit = 0
		while (bytes >= 1024 && unit < units.length - 1) {
			bytes /= 1024
			++unit
		}
		const precision = unit === 0 || bytes >= 10 ? 0 : 1
		return bytes.toFixed(precision) + " " + units[unit]
	}

	function attachmentMetadata(kind, mime, byteSize) {
		const parts = [attachmentKindLabel(kind, mime)]
		const normalizedMime = safeText(mime, 128)
		if (normalizedMime.length > 0 && normalizedMime.toLowerCase() !== "application/octet-stream")
			parts.push(normalizedMime)
		const size = formatByteSize(byteSize)
		if (size.length > 0)
			parts.push(size)
		return parts.join(" · ")
	}

	function isImageAttachment(attachment) {
		if (!attachment)
			return false
		const kind = safeText(attachment.kind, 64).toLowerCase()
		const mime = safeText(attachment.mime, 128).toLowerCase()
		if (kind === "image" || mime.indexOf("image/") === 0)
			return true
		if (kind.length > 0 || mime.length > 0)
			return false
		// Backward compatibility for messages created before attachment kinds were exposed.
		return safeRenderImageSource(attachment.thumbnailUrl || attachment.url || "").length > 0
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
				readonly property string normalizedKind: root.safeText(modelData.kind, 64).toLowerCase()
				readonly property string normalizedMime: root.safeText(modelData.mime, 128).toLowerCase()
				readonly property bool imageAttachment: root.isImageAttachment(modelData)
				readonly property string attachmentState: root.safeText(modelData.state, 32).toLowerCase()
				readonly property bool previewLoading: imageAttachment && attachmentState === "loading"
				readonly property bool previewFailed: imageAttachment && attachmentState === "error"
				readonly property string assetId: root.safeText(
					modelData.assetId !== undefined ? modelData.assetId : modelData.assetID, 128)
				readonly property string sourceUrl: root.safeText(
					modelData.thumbnailUrl || modelData.url || "", 2048)
				readonly property string fileName: root.safeText(
					modelData.fileName || modelData.name || "", 1024)
				readonly property string kindLabel: root.attachmentKindLabel(normalizedKind, normalizedMime)
				readonly property string metadataText: root.attachmentMetadata(normalizedKind,
					normalizedMime, modelData.byteSize !== undefined ? modelData.byteSize : modelData.size)
				readonly property string label: root.safeText(
					modelData.alt || fileName || (imageAttachment ? qsTr("Image attachment")
						: qsTr("%1 attachment").arg(kindLabel)), 512)
					|| (imageAttachment ? qsTr("Image attachment") : qsTr("File attachment"))
				readonly property string stableId: root.safeText(
					modelData.id !== undefined ? modelData.id : modelData.assetId, 128)
					|| String(attachmentTile.index)
                readonly property real requestedWidth: Number(modelData.width) > 0 ? Number(modelData.width) : 240
                readonly property real requestedHeight: Number(modelData.height) > 0 ? Number(modelData.height) : 160
                readonly property real aspectRatio: Math.max(0.2, Math.min(5,
                    requestedHeight / Math.max(1, requestedWidth)))

				objectName: "attachment_" + stableId
				width: imageAttachment
					? Math.max(1, Math.min(Math.max(requestedWidth, 180),
						Math.min(320, Math.max(1, gallery.width))))
					: Math.max(1, Math.min(360, Math.max(1, gallery.width)))
				height: imageAttachment
					? Math.min(240, Math.max(root.compactLayout ? 96 : 120,
						Math.round(width * aspectRatio)))
					: 82
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
					visible: attachmentTile.imageAttachment
					source: attachmentTile.imageAttachment
						? root.safeRenderImageSource(attachmentTile.sourceUrl) : ""
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
					running: attachmentTile.previewLoading
						|| (attachmentTile.imageAttachment && attachmentImage.status === Image.Loading)
                    visible: running
					Accessible.name: qsTr("Loading %1").arg(attachmentTile.label)
                }

                Label {
					objectName: "attachmentError_" + attachmentTile.stableId
					textFormat: Text.PlainText
					anchors.centerIn: parent
					width: Math.max(1, parent.width - Theme.space3 * 2)
					visible: attachmentTile.imageAttachment && !attachmentTile.previewLoading
						&& (attachmentTile.previewFailed || attachmentImage.status === Image.Error
							|| root.safeRenderImageSource(attachmentTile.sourceUrl).length === 0)
                    text: qsTr("Attachment unavailable")
                    color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
					Accessible.role: Accessible.AlertMessage
					Accessible.name: text
					Accessible.description: attachmentTile.label
                }

				RowLayout {
					objectName: "attachmentFileDetails_" + attachmentTile.stableId
					anchors.fill: parent
					anchors.margins: Theme.space3
					spacing: Theme.space3
					visible: !attachmentTile.imageAttachment
					Accessible.ignored: true
					Rectangle {
						Layout.preferredWidth: 46
						Layout.preferredHeight: 46
						radius: Theme.innerRadius
						color: Theme.panel
						ModernIcon {
							anchors.centerIn: parent
							name: attachmentTile.normalizedKind === "video"
								|| attachmentTile.normalizedKind === "audio"
								|| /^(video|audio)\//.test(attachmentTile.normalizedMime)
								? "play" : "attach"
							size: 21
							color: Theme.accent
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2
						Label {
							objectName: "attachmentFileName_" + attachmentTile.stableId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: attachmentTile.fileName.length > 0
								? attachmentTile.fileName : attachmentTile.label
							color: Theme.textStrong
							font.pixelSize: Theme.fontBody
							font.weight: Font.DemiBold
							elide: Text.ElideMiddle
						}
						Label {
							objectName: "attachmentFileMetadata_" + attachmentTile.stableId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: attachmentTile.metadataText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}
					ModernIcon {
						Layout.preferredWidth: 18
						Layout.preferredHeight: 18
						name: "external"
						size: 18
						color: Theme.textMuted
					}
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
						? !attachmentTile.previewLoading
						: !!attachmentTile.modelData.enabled && !attachmentTile.previewLoading
                    background: null
                    contentItem: Item {}
                    Accessible.name: attachmentTile.label
					Accessible.description: attachmentTile.imageAttachment
						? qsTr("Attachment %1 of %2")
							.arg(attachmentTile.index + 1).arg(root.visibleAttachments.length)
						: qsTr("Download %1. Attachment %2 of %3")
							.arg(attachmentTile.label).arg(attachmentTile.index + 1)
							.arg(root.visibleAttachments.length)
					ToolTip.visible: hovered && !attachmentTile.imageAttachment
					ToolTip.text: qsTr("Download %1").arg(attachmentTile.label)
                    onClicked: root.attachmentRequested(attachmentTile.modelData)
                }

				ModernIconButton {
					objectName: "attachmentDownload_" + attachmentTile.stableId
					anchors.top: parent.top
					anchors.right: parent.right
					anchors.margins: Theme.space2
					z: 3
					dense: true
					visible: attachmentTile.imageAttachment && attachmentTile.assetId.length > 0
					enabled: attachmentTile.modelData.enabled === undefined
						|| !!attachmentTile.modelData.enabled
					iconName: "external"
					text: qsTr("Save original")
					Accessible.name: qsTr("Save original %1").arg(attachmentTile.label)
					ToolTip.visible: hovered
					ToolTip.text: text
					onClicked: root.attachmentDownloadRequested(attachmentTile.modelData)
				}
            }
        }
    }
}
