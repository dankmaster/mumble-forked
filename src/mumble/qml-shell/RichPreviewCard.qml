import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    required property var preview
    property string previewIdentity: preview
        ? [preview.url || "", preview.embedUrl || "", preview.mediaUrl || "",
           preview.mediaExternalUrl || "", preview.title || ""].join("|") : ""
    property bool watchTogetherAvailable: true
	property bool renderActive: true
    property bool userExpanded: false
    property int selectedMediaIndex: 0
    property bool imageRefreshQueued: false
    readonly property string previewState: preview
        ? (preview.state || (preview.failed ? "error" : preview.loading ? "loading" : "ready")) : ""
    readonly property bool compact: preview && preview.previewSize === "compact" && !userExpanded
    readonly property bool expanded: preview && (preview.previewSize === "large" || userExpanded)
    readonly property string displayTitle: preview
        ? (preview.title || preview.host || preview.loadingLabel || qsTr("Link preview")) : ""
    readonly property string metadataLine: preview
        ? (preview.subtitle || preview.host || (preview.metadata ? (preview.metadata.xDisplayName || preview.metadata.xHandle || "") : "")) : ""
    readonly property var mediaItems: normalizedMediaItems()
    readonly property var currentMedia: mediaItems.length > 0
        ? mediaItems[Math.max(0, Math.min(selectedMediaIndex, mediaItems.length - 1))] : ({})
    readonly property string currentMediaKind: mediaKind(currentMedia)
    readonly property string currentMediaUrl: currentMediaKind === "image"
        ? safeRenderImageSource(currentMedia.url || "", !!currentMedia.managedAnimated) : String(currentMedia.url || "")
    readonly property string currentMediaExternalUrl: safeExternalUrl(currentMedia.externalUrl || "")
    readonly property bool currentMediaDirectPlayable: currentMedia.directPlayable !== false
    readonly property string imageSource: safeRenderImageSource(currentMediaKind === "image" ? currentMediaUrl
        : String(currentMedia.poster || currentMedia.thumbnail || (preview ? preview.thumbnailUrl : "") || ""),
        currentMediaKind === "image" && !!currentMedia.managedAnimated)
    readonly property bool hasExternalImage: currentMediaKind === "image"
        && imageSource.length === 0 && currentMediaExternalUrl.length > 0
    readonly property bool hasDirectMedia: currentMediaDirectPlayable
        && (currentMediaKind === "video" || currentMediaKind === "audio") && currentMediaUrl.length > 0
    readonly property bool hasExternalMedia: !currentMediaDirectPlayable
        && (currentMediaKind === "video" || currentMediaKind === "audio")
        && safeExternalUrl(currentMediaUrl).length > 0
    readonly property bool hasDetails: !!preview && ((preview.description || "").length > 0
                                                     || (!!preview.metadata && Object.keys(preview.metadata).length > 0)
                                                     || mediaItems.length > 1)

    signal externalOpenRequested(string url)
    signal imageOpenRequested(string source, string title)
    signal imageRefreshRequested()
    signal directMediaRequested(string url, string mime, string audioUrl, string audioMime, string title)
    signal playRequested(string url, string provider)
    signal watchTogetherRequested(string url, string provider, string title)

    implicitHeight: content.implicitHeight + 16
    radius: 8
    color: Theme.strip
    border.color: Theme.divider
    Accessible.role: Accessible.Grouping
    Accessible.name: displayTitle + (metadataLine.length > 0 ? ": " + metadataLine : "")
    Accessible.description: previewState === "loading" ? qsTr("Preview loading")
        : previewState === "error" ? qsTr("Preview unavailable") : (preview.description || "")

    onPreviewIdentityChanged: resetForReuse()
    onPreviewChanged: {
		selectedMediaIndex = Math.max(0, Math.min(selectedMediaIndex, Math.max(0, mediaItems.length - 1)))
        imageRefreshQueued = false
    }

    function resetForReuse() {
        userExpanded = false
        selectedMediaIndex = 0
        imageRefreshQueued = false
    }

    function requestImageRefresh() {
        if (imageRefreshQueued)
            return
        imageRefreshQueued = true
        imageRefreshRequested()
    }

    function safeExternalUrl(value) {
        const url = String(value === undefined || value === null ? "" : value).trim()
        return /^(https?:\/\/|mailto:|mumble:\/\/)/i.test(url) ? url : ""
    }

    function safeRenderImageSource(value, managedAnimated) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        if (/^(image:\/\/mumble\/|qrc:\/)/i.test(source))
            return source
        if (!!managedAnimated && /^file:\/\//i.test(source)
                && /\/mumble-qml-images-[A-Za-z0-9]+\/[0-9a-f]{64}-[0-9a-f-]{36}\.gif$/i.test(source))
            return source
        return ""
    }

    function mediaKind(item) {
        if (!item)
            return ""
        const kind = String(item.kind || "").toLowerCase()
        const mime = String(item.mime || "").toLowerCase()
        if (kind === "image" || kind === "gif" || mime.indexOf("image/") === 0)
            return "image"
        if (kind === "audio" || mime.indexOf("audio/") === 0)
            return "audio"
        if (kind === "video" || mime.indexOf("video/") === 0)
            return "video"
        return ""
    }

    function normalizedMediaItems() {
        if (!preview)
            return []
        const sourceItems = preview.mediaItems || []
        const result = []
        for (let index = 0; index < sourceItems.length && result.length < 16; ++index) {
            const item = sourceItems[index] || {}
            if (mediaKind(item) === "image") {
                const renderSource = safeRenderImageSource(item.url || "", !!item.managedAnimated)
                const externalSource = safeExternalUrl(item.externalUrl
                                                       || (renderSource.length === 0 ? item.url : ""))
                if (renderSource.length > 0 || externalSource.length > 0) {
                    result.push({
                        "kind": "image",
                        "mime": String(item.mime || ""),
                        "url": renderSource,
                        "externalUrl": externalSource,
                        "title": String(item.title || ""),
                        "directPlayable": renderSource.length > 0,
                        "managedAnimated": !!item.managedAnimated && /^file:\/\//i.test(renderSource),
                        "thumbnail": safeRenderImageSource(item.thumbnail || ""),
                        "poster": safeRenderImageSource(item.poster || "")
                    })
                }
            } else if (String(item.url || "").length > 0) {
                result.push(item)
            }
        }
        if (result.length === 0 && (String(preview.mediaUrl || "").length > 0
                                   || safeExternalUrl(preview.mediaExternalUrl || "").length > 0)) {
            result.push({
                "url": String(preview.mediaUrl),
                "externalUrl": safeExternalUrl(preview.mediaExternalUrl || ""),
                "mime": String(preview.mediaMime || ""),
                "kind": String(preview.mediaKind || ""),
                "thumbnail": String(preview.thumbnailUrl || ""),
                "poster": String(preview.thumbnailUrl || ""),
                "title": root.displayTitle,
                "managedAnimated": !!preview.mediaAnimated
            })
        }
        return result
    }

    function requestCurrentMedia() {
        if (currentMediaKind === "image") {
            if (imageSource.length > 0)
                imageOpenRequested(imageSource, String(currentMedia.title || displayTitle))
            else if (currentMediaExternalUrl.length > 0)
                externalOpenRequested(currentMediaExternalUrl)
            return
        }
        if (hasExternalMedia) {
            externalOpenRequested(safeExternalUrl(currentMediaUrl))
            return
        }
        if (!hasDirectMedia)
            return
        const pairedAudioUrl = currentMediaUrl === String(preview.mediaUrl || "")
            ? String(preview.mediaAudioUrl || "") : ""
        const pairedAudioMime = pairedAudioUrl.length > 0 ? String(preview.mediaAudioMime || "") : ""
        directMediaRequested(currentMediaUrl, String(currentMedia.mime || preview.mediaMime || ""),
                             pairedAudioUrl, pairedAudioMime,
                             String(currentMedia.title || displayTitle))
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: root.compact ? 52 : 72
                Layout.preferredHeight: root.compact ? 52 : 54
                visible: root.imageSource.length > 0 || root.previewState !== "ready" || root.hasExternalImage
                         || root.hasDirectMedia || root.hasExternalMedia
                radius: 6
                color: Theme.panel
                clip: true

                Image {
                    id: previewImage
					objectName: "previewCompactImage"
                    anchors.fill: parent
					source: root.renderActive ? root.imageSource : ""
                    asynchronous: true
                    cache: false
                    sourceSize: Qt.size(Math.min(640, width * Screen.devicePixelRatio),
                                        Math.min(480, height * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                    onStatusChanged: if (status === Image.Error && root.imageSource.length > 0)
                                         root.requestImageRefresh()
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.previewState === "loading" || previewImage.status === Image.Loading
                    visible: running
                }
                Label {
					textFormat: Text.PlainText
                    anchors.centerIn: parent
                    visible: root.previewState === "error" || (root.imageSource.length > 0 && previewImage.status === Image.Error)
                    text: "!"
                    color: Theme.danger
                    font.bold: true
                    font.pixelSize: 18
                    Accessible.name: qsTr("Preview unavailable")
                }
                Label {
					textFormat: Text.PlainText
                    anchors.centerIn: parent
                    visible: (root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
                             && root.imageSource.length === 0
                    text: root.currentMediaKind === "audio" ? "♪" : root.hasExternalImage ? "↗" : "▶"
                    color: Theme.textStrong
                    font.pixelSize: 20
                }
                Button {
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: root.currentMediaKind === "image" ? (previewImage.status === Image.Ready || root.hasExternalImage)
                             : (root.hasDirectMedia || root.hasExternalMedia)
                    background: null
                    contentItem: Item {}
                    Accessible.name: root.currentMediaKind === "image" ? qsTr("Open preview image") : qsTr("Play direct media")
                    onClicked: root.requestCurrentMedia()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: root.displayTitle
                    color: Theme.textStrong
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: root.metadataLine.length > 0
                    text: root.metadataLine
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: root.previewState === "error"
                    text: qsTr("Preview unavailable. You can still open the original link.")
                    color: Theme.danger
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: root.expanded && root.previewState === "ready" && (root.preview.description || "").length > 0
                    text: root.preview.description || ""
                    color: Theme.textMain
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded && (root.imageSource.length > 0
                                    || root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
                                    ? Math.min(280, width * 9 / 16) : 0
            visible: Layout.preferredHeight > 0
            color: Theme.panel
            radius: 6
            clip: true

            AnimatedImage {
                id: expandedImage
				objectName: "previewExpandedAnimatedImage"
				readonly property string requestedSource: root.renderActive && root.expanded
					? root.imageSource : ""
                anchors.fill: parent
                anchors.margins: 2
				source: requestedSource
                asynchronous: true
                cache: false
				playing: visible && source.toString().length > 0
                fillMode: Image.PreserveAspectFit
                visible: root.imageSource.length > 0 && status === Image.Ready
                onStatusChanged: if (status === Image.Error && root.imageSource.length > 0)
                                     root.requestImageRefresh()
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: expandedImage.status === Image.Loading
                visible: running
            }
            ModernButton {
                anchors.centerIn: parent
                visible: root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage
                text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
                    : root.currentMediaKind === "audio" ? qsTr("Play audio") : qsTr("Play video")
                onClicked: root.requestCurrentMedia()
            }
            Button {
                anchors.fill: parent
                visible: root.currentMediaKind === "image" && expandedImage.status === Image.Ready
                hoverEnabled: true
                background: null
                contentItem: Item {}
                Accessible.name: qsTr("Open preview image")
                onClicked: root.requestCurrentMedia()
            }
            ToolButton {
                objectName: "previewPreviousMediaButton"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.mediaItems.length > 1
                enabled: root.selectedMediaIndex > 0
                text: "‹"
                Accessible.name: qsTr("Previous media")
                onClicked: --root.selectedMediaIndex
            }
            ToolButton {
                objectName: "previewNextMediaButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.mediaItems.length > 1
                enabled: root.selectedMediaIndex + 1 < root.mediaItems.length
                text: "›"
                Accessible.name: qsTr("Next media")
                onClicked: ++root.selectedMediaIndex
            }
            Label {
				textFormat: Text.PlainText
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                visible: root.mediaItems.length > 1
                text: qsTr("%1 of %2").arg(root.selectedMediaIndex + 1).arg(root.mediaItems.length)
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.previewState !== "loading"

            ModernButton {
                objectName: "previewOpenButton"
                visible: root.safeExternalUrl(root.preview.url).length > 0
                text: root.preview.openLabel || qsTr("Open")
                onClicked: root.externalOpenRequested(root.safeExternalUrl(root.preview.url))
            }
            ModernButton {
                objectName: "previewDirectMediaButton"
                visible: root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage
                text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
                    : root.currentMediaKind === "audio" ? qsTr("Play audio") : qsTr("Play video")
                onClicked: root.requestCurrentMedia()
            }
            ModernButton {
                objectName: "previewPlayButton"
                visible: (root.preview.embedUrl || "").length > 0 && (root.preview.embedKind || "").length > 0
                text: qsTr("Play")
                onClicked: root.playRequested(root.preview.embedUrl, root.preview.embedKind)
            }
            ModernButton {
                objectName: "previewWatchTogetherButton"
                visible: (root.preview.embedUrl || "").length > 0 && (root.preview.embedKind || "").length > 0
                enabled: root.watchTogetherAvailable
                text: root.watchTogetherAvailable ? qsTr("Watch together") : qsTr("Session active")
                Accessible.description: root.watchTogetherAvailable
                    ? qsTr("Start a synchronized media session")
                    : qsTr("End or leave the active media session first")
                onClicked: root.watchTogetherRequested(root.preview.embedUrl, root.preview.embedKind, root.displayTitle)
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                objectName: "previewExpandButton"
                visible: root.hasDetails && root.preview.previewSize !== "large"
                text: root.userExpanded ? qsTr("Less") : qsTr("More")
                Accessible.name: root.userExpanded ? qsTr("Collapse preview") : qsTr("Expand preview")
                onClicked: root.userExpanded = !root.userExpanded
            }
        }
    }
}
