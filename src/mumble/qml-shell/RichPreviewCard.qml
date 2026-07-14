pragma ComponentBehavior: Bound

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
	property var mediaSessionController: null
	property string mediaSessionId: ""
	property bool renderActive: true
    property bool userExpanded: false
    property int selectedMediaIndex: 0
    property bool imageRefreshQueued: false
	property bool sensitiveMediaRevealed: false
    readonly property string previewState: preview
        ? (preview.state || (preview.failed ? "error" : preview.loading ? "loading" : "ready")) : ""
	readonly property string sanitizedDescription: safeText(preview ? preview.description : "", 4096)
	readonly property string mediaErrorDescription: firstSafeText([
		preview ? preview.errorDescription : "", preview ? preview.errorMessage : "",
		preview && typeof preview.error === "string" ? preview.error : "", sanitizedDescription
	], 1024)
	readonly property string errorDescription: previewState === "error" ? mediaErrorDescription : ""
    readonly property bool compact: preview && preview.previewSize === "compact" && !userExpanded
    readonly property bool expanded: preview && (preview.previewSize === "large" || userExpanded)
    readonly property bool narrowLayout: width < 440
	readonly property real actionAvailableWidth: Math.max(1, width - Theme.space4 * 2)
    readonly property bool actionsWrapped: previewActionFlow.implicitHeight > Theme.controlHeight + 1
	readonly property string displayTitle: safeText(preview
		? (preview.title || preview.host || preview.loadingLabel || qsTr("Link preview")) : "", 512)
	readonly property string metadataLine: safeText(preview
		? (preview.subtitle || preview.host || (preview.metadata
			? (preview.metadata.xDisplayName || preview.metadata.xHandle || "") : "")) : "", 512)
	readonly property string providerLabel: safeText(preview ? String(
		(preview.metadata && (preview.metadata.providerName || preview.metadata.previewProvider))
		|| preview.providerName || preview.host || preview.embedKind || "") : "", 128)
	readonly property string safeEmbedUrl: safeProviderEmbedUrl(preview ? preview.embedUrl : "")
	readonly property string safeEmbedProvider: safeText(preview ? preview.embedKind : "", 64)
	readonly property string openLabel: safeText(preview ? preview.openLabel : "", 128) || qsTr("Open")
	readonly property string contentWarning: preview && preview.metadata
		? safeText(preview.metadata.contentWarning, 512) : ""
	readonly property bool thumbnailBlur: {
		const value = preview && preview.metadata ? preview.metadata.thumbnailBlur : false
		return value === true || value === 1 || String(value).toLowerCase() === "true"
	}
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
	readonly property bool currentMediaManagedAnimated: currentMediaKind === "image"
		&& !!currentMedia.managedAnimated && /^file:\/\//i.test(imageSource)
		&& /\.gif$/i.test(imageSource)
    readonly property bool hasExternalImage: currentMediaKind === "image"
        && imageSource.length === 0 && currentMediaExternalUrl.length > 0
    readonly property bool hasDirectMedia: currentMediaDirectPlayable
        && (currentMediaKind === "video" || currentMediaKind === "audio") && currentMediaUrl.length > 0
    readonly property bool hasExternalMedia: !currentMediaDirectPlayable
        && (currentMediaKind === "video" || currentMediaKind === "audio")
        && safeExternalUrl(currentMediaUrl).length > 0
	readonly property bool hasRevealableMedia: imageSource.length > 0 || hasExternalImage
		|| hasDirectMedia || hasExternalMedia
	readonly property bool mediaRequiresReveal: hasRevealableMedia && !sensitiveMediaRevealed
		&& (contentWarning.length > 0 || thumbnailBlur)
	readonly property bool hasExpandedDescription: previewState === "ready"
		&& sanitizedDescription.length > 0
		&& !providerDetails.ownsDescription
	readonly property bool canExpand: !!preview && preview.previewSize !== "large"
		&& (providerDetails.canExpand || hasExpandedDescription || hasRevealableMedia
			|| mediaItems.length > 1)
	readonly property bool hasDetails: canExpand
	readonly property bool inlinePlaybackActive: !!mediaSessionController
		&& mediaSessionController.active && !mediaSessionController.detached
		&& String(mediaSessionController.sessionId || "") === mediaSessionId

    signal externalOpenRequested(string url)
    signal imageOpenRequested(string source, string title)
    signal imageRefreshRequested()
    signal directMediaRequested(string url, string mime, string audioUrl, string audioMime, string title)
	signal inlinePlayRequested(string url, string provider)
	signal popoutPlayRequested(string url, string provider)
    signal watchTogetherRequested(string url, string provider, string title)

	implicitHeight: content.implicitHeight + Theme.space4 * 2
	radius: Theme.innerRadius
	color: Theme.surfaceRaised
	border.color: root.previewState === "error" ? root.withAlpha(Theme.danger, 0.65)
		: Theme.surfaceBorder
    Accessible.role: Accessible.Grouping
    Accessible.name: displayTitle + (metadataLine.length > 0 ? ": " + metadataLine : "")
	Accessible.description: previewState === "loading" ? qsTr("Preview loading")
		: previewState === "error" ? [qsTr("Preview unavailable"), errorDescription]
			.filter(function(value) { return value.length > 0 }).join(". ")
		: sanitizedDescription

    onPreviewIdentityChanged: resetForReuse()
    onPreviewChanged: {
		selectedMediaIndex = Math.max(0, Math.min(selectedMediaIndex, Math.max(0, mediaItems.length - 1)))
        imageRefreshQueued = false
    }
	onContentWarningChanged: sensitiveMediaRevealed = false
	onThumbnailBlurChanged: sensitiveMediaRevealed = false

	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 3
		radius: root.radius
		color: root.previewState === "error" ? Theme.danger : Theme.accent
	}

    function resetForReuse() {
        userExpanded = false
        selectedMediaIndex = 0
        imageRefreshQueued = false
		sensitiveMediaRevealed = false
    }

	function requestImageRefresh() {
        if (imageRefreshQueued)
            return
        imageRefreshQueued = true
        imageRefreshRequested()
    }

    function safeExternalUrl(value) {
		const url = String(value === undefined || value === null ? "" : value).trim().slice(0, 2048)
        return /^(https?:\/\/|mailto:|mumble:\/\/)/i.test(url) ? url : ""
    }

	function safeProviderEmbedUrl(value) {
		const url = String(value === undefined || value === null ? "" : value).trim().slice(0, 2048)
		return /^https:\/\//i.test(url) ? url : ""
	}

	function requestInlinePlayback() {
		userExpanded = true
		inlinePlayRequested(safeEmbedUrl, safeEmbedProvider)
	}

	function preserveInlinePlayback() {
		if (!mediaSessionController || !mediaSessionController.active
				|| mediaSessionController.detached
				|| String(mediaSessionController.sessionId || "") !== mediaSessionId)
			return false
		mediaSessionController.detach()
		return true
	}

	onRenderActiveChanged: {
		if (!renderActive)
			preserveInlinePlayback()
	}
	onVisibleChanged: {
		if (!visible)
			preserveInlinePlayback()
	}
	Component.onDestruction: preserveInlinePlayback()

	function safeDirectMediaUrl(value, kind) {
		const url = String(value === undefined || value === null ? "" : value).trim()
		if (/^https:\/\//i.test(url))
			return url.slice(0, 2048)
		const expectedKind = String(kind || "").toLowerCase()
		if ((expectedKind === "audio" || expectedKind === "video")
				&& new RegExp("^data:" + expectedKind + "\/[a-z0-9.+-]+;base64,", "i").test(url))
			return url
		return ""
	}

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}

	function firstSafeText(values, maximum) {
		for (let index = 0; index < values.length; ++index) {
			const text = safeText(values[index], maximum)
			if (text.length > 0)
				return text
		}
		return ""
	}

	function withAlpha(color, alpha) {
		return Qt.rgba(color.r, color.g, color.b, alpha)
	}

	function actionButtonWidth(implicitButtonWidth) {
		return Math.max(1, narrowLayout ? actionAvailableWidth
			: Math.min(implicitButtonWidth, actionAvailableWidth))
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
		const sourceItems = Array.isArray(preview.mediaItems) ? preview.mediaItems : []
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
						"mime": safeText(item.mime, 128),
                        "url": renderSource,
                        "externalUrl": externalSource,
						"title": safeText(item.title, 512),
                        "directPlayable": renderSource.length > 0,
                        "managedAnimated": !!item.managedAnimated && /^file:\/\//i.test(renderSource),
                        "thumbnail": safeRenderImageSource(item.thumbnail || ""),
                        "poster": safeRenderImageSource(item.poster || "")
                    })
                }
		} else {
				const kind = mediaKind(item)
				const playbackUrl = safeDirectMediaUrl(item.url || "", kind)
				const externalUrl = safeExternalUrl(item.externalUrl || item.url)
				const directPlayable = item.directPlayable !== false && playbackUrl.length > 0
				const targetUrl = directPlayable ? playbackUrl : externalUrl
				if (kind.length > 0 && targetUrl.length > 0) {
					result.push({
						"kind": kind,
						"mime": safeText(item.mime, 128),
						"url": targetUrl,
						"externalUrl": externalUrl,
						"title": safeText(item.title, 512),
						"directPlayable": directPlayable,
						"thumbnail": safeRenderImageSource(item.thumbnail || ""),
						"poster": safeRenderImageSource(item.poster || "")
					})
				}
            }
        }
        if (result.length === 0 && (String(preview.mediaUrl || "").length > 0
                                   || safeExternalUrl(preview.mediaExternalUrl || "").length > 0)) {
			const fallbackKind = mediaKind({ "kind": preview.mediaKind, "mime": preview.mediaMime })
			const playbackUrl = safeDirectMediaUrl(preview.mediaUrl || "", fallbackKind)
			const externalUrl = safeExternalUrl(preview.mediaExternalUrl
				|| (playbackUrl.length === 0 ? preview.mediaUrl : ""))
			const directPlayable = playbackUrl.length > 0
			const targetUrl = directPlayable ? playbackUrl : externalUrl
			if (fallbackKind.length > 0 && targetUrl.length > 0) {
				result.push({
					"url": targetUrl,
					"externalUrl": externalUrl,
					"mime": safeText(preview.mediaMime, 128),
					"kind": fallbackKind,
					"thumbnail": safeRenderImageSource(preview.thumbnailUrl || ""),
					"poster": safeRenderImageSource(preview.thumbnailUrl || ""),
					"title": root.displayTitle,
					"directPlayable": directPlayable,
					"managedAnimated": !!preview.mediaAnimated
				})
			}
        }
        return result
    }

    function requestCurrentMedia() {
		if (mediaRequiresReveal) {
			sensitiveMediaRevealed = true
			return
		}
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
		const pairedAudioUrl = currentMediaUrl === safeDirectMediaUrl(preview.mediaUrl || "", currentMediaKind)
			? safeDirectMediaUrl(preview.mediaAudioUrl || "", "audio") : ""
		const pairedAudioMime = pairedAudioUrl.length > 0 ? safeText(preview.mediaAudioMime, 128) : ""
        directMediaRequested(currentMediaUrl, String(currentMedia.mime || preview.mediaMime || ""),
                             pairedAudioUrl, pairedAudioMime,
							 safeText(currentMedia.title || displayTitle, 512))
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
		anchors.margins: Theme.space4
		spacing: Theme.space3

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            Rectangle {
				Layout.preferredWidth: root.compact ? 56 : 92
				Layout.preferredHeight: root.compact ? 56 : 64
                visible: root.imageSource.length > 0 || root.previewState !== "ready" || root.hasExternalImage
                         || root.hasDirectMedia || root.hasExternalMedia
				radius: Theme.innerRadius
				color: Theme.panel
                clip: true

                Image {
                    id: previewImage
					objectName: "previewCompactImage"
                    anchors.fill: parent
					source: root.renderActive && !root.mediaRequiresReveal ? root.imageSource : ""
                    asynchronous: true
                    cache: false
                    sourceSize: Qt.size(Math.min(640, width * Screen.devicePixelRatio),
                                        Math.min(480, height * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                    onStatusChanged: if (status === Image.Error && root.imageSource.length > 0)
                                         root.requestImageRefresh()
                }
				ModernBusyIndicator {
					objectName: "previewCompactBusyIndicator"
                    anchors.centerIn: parent
                    running: root.previewState === "loading" || previewImage.status === Image.Loading
                    visible: running
					Accessible.name: qsTr("Loading link preview")
                }
                ModernIcon {
                    anchors.centerIn: parent
                    visible: root.previewState === "error" || (root.imageSource.length > 0 && previewImage.status === Image.Error)
					name: "warning"
                    color: Theme.danger
					size: Theme.avatarSmall
					Accessible.ignored: true
                }
                ModernIcon {
                    anchors.centerIn: parent
                    visible: (root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
                             && root.imageSource.length === 0
					name: root.hasExternalImage ? "external" : "play"
                    color: Theme.textStrong
					size: Theme.avatarSmall
                }
                Button {
					id: compactMediaAction
					objectName: "previewCompactMediaButton"
                    anchors.fill: parent
                    hoverEnabled: true
					enabled: !root.mediaRequiresReveal && (root.currentMediaKind === "image"
						? (previewImage.status === Image.Ready || root.hasExternalImage)
                             : (root.hasDirectMedia || root.hasExternalMedia)
					)
                    background: null
                    contentItem: Item {}
                    Accessible.name: root.currentMediaKind === "image" ? qsTr("Open preview image") : qsTr("Play direct media")
                    onClicked: root.requestCurrentMedia()
                }
				Rectangle {
					objectName: "previewCompactMediaFocus"
					anchors.fill: parent
					anchors.margins: Theme.space1
					visible: compactMediaAction.activeFocus
					color: "transparent"
					radius: Theme.innerRadius
					border.color: Theme.focus
					border.width: Theme.focusRingWidth
				}
				Rectangle {
					anchors.fill: parent
					visible: root.mediaRequiresReveal && !root.expanded
					color: Theme.strip
					radius: parent.radius
					border.color: Theme.surfaceBorder

					Button {
						id: compactRevealButton
						objectName: "previewCompactRevealButton"
						anchors.fill: parent
						visible: root.mediaRequiresReveal && !root.expanded
						hoverEnabled: true
						background: null
						contentItem: Label {
							text: qsTr("Reveal")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontCaption
							font.bold: true
							horizontalAlignment: Text.AlignHCenter
							verticalAlignment: Text.AlignVCenter
						}
						Accessible.name: qsTr("Reveal sensitive preview media")
						Accessible.description: root.contentWarning.length > 0
							? root.contentWarning : qsTr("This preview is hidden by default")
						onClicked: root.sensitiveMediaRevealed = true
					}
					Rectangle {
						objectName: "previewCompactRevealFocus"
						anchors.fill: parent
						anchors.margins: Theme.space1
						visible: compactRevealButton.activeFocus
						color: "transparent"
						radius: Theme.innerRadius
						border.color: Theme.focus
						border.width: Theme.focusRingWidth
					}
				}
            }

            ColumnLayout {
                Layout.fillWidth: true
				spacing: Theme.space1
				Rectangle {
					objectName: "previewProviderChip"
					Layout.preferredWidth: Math.min(providerText.implicitWidth + Theme.space2 * 2,
						Math.max(1, parent.width))
					Layout.maximumWidth: Math.max(1, parent.width)
					Layout.preferredHeight: Theme.space5
					visible: root.providerLabel.length > 0
					radius: height / 2
					color: Theme.accentSubtle
					Label {
						id: providerText
						objectName: "previewProviderLabel"
						anchors.fill: parent
						anchors.leftMargin: Theme.space2
						anchors.rightMargin: Theme.space2
						textFormat: Text.PlainText
						text: root.providerLabel.toUpperCase()
						color: Theme.accent
						font.pixelSize: Theme.fontCaption
						font.bold: true
						font.letterSpacing: 0.6
						elide: Text.ElideRight
						horizontalAlignment: Text.AlignHCenter
						verticalAlignment: Text.AlignVCenter
					}
				}
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: root.displayTitle
                    color: Theme.textStrong
					font.bold: true
					font.pixelSize: Theme.fontTitle
					wrapMode: root.narrowLayout ? Text.Wrap : Text.NoWrap
					maximumLineCount: root.narrowLayout ? 2 : 1
					elide: Text.ElideRight
                }
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: root.metadataLine.length > 0
                    text: root.metadataLine
                    color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: root.narrowLayout ? Text.Wrap : Text.NoWrap
					maximumLineCount: root.narrowLayout ? 2 : 1
					elide: Text.ElideRight
                }
                Label {
					objectName: "previewErrorText"
					textFormat: Text.PlainText
                    Layout.fillWidth: true
					visible: root.previewState === "error"
						|| (root.imageSource.length > 0 && previewImage.status === Image.Error)
					text: root.previewState === "error"
						? (root.errorDescription.length > 0
							? qsTr("Preview unavailable: %1").arg(root.errorDescription)
							: qsTr("Preview unavailable. You can still open the original link."))
						: (root.mediaErrorDescription.length > 0
							? qsTr("Media unavailable: %1").arg(root.mediaErrorDescription)
							: qsTr("Media unavailable"))
                    color: Theme.danger
					font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
					Accessible.role: Accessible.AlertMessage
					Accessible.name: root.previewState === "error" ? qsTr("Preview unavailable")
						: qsTr("Media unavailable")
					Accessible.description: root.previewState === "error"
						? root.errorDescription : root.mediaErrorDescription
                }
                Label {
					textFormat: Text.PlainText
                    Layout.fillWidth: true
					visible: root.expanded && root.previewState === "ready"
						&& root.hasExpandedDescription
					text: root.sanitizedDescription
                    color: Theme.textMain
					font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
			id: expandedMediaPanel
			objectName: "previewExpandedMediaPanel"
			Layout.fillWidth: true
			Layout.preferredHeight: root.inlinePlaybackActive
				? Math.max(420, Math.min(520, root.actionAvailableWidth * 9 / 16 + 132))
				: root.expanded && (root.imageSource.length > 0
                                    || root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
									? Math.min(Theme.rowHeight * 6, root.actionAvailableWidth * 9 / 16) : 0
            visible: Layout.preferredHeight > 0
            color: Theme.panel
			radius: Theme.innerRadius
            clip: true

			Image {
				id: expandedStaticImage
				objectName: "previewExpandedStaticImage"
                anchors.fill: parent
				anchors.margins: Theme.space1
				source: !root.inlinePlaybackActive && root.renderActive && root.expanded && !root.mediaRequiresReveal
					&& !root.currentMediaManagedAnimated
					? root.imageSource : ""
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
				visible: !root.inlinePlaybackActive && source.toString().length > 0 && status === Image.Ready
                onStatusChanged: if (status === Image.Error && root.imageSource.length > 0)
                                     root.requestImageRefresh()
            }
			Loader {
				id: expandedAnimationLoader
				objectName: "previewExpandedAnimatedLoader"
				property int mediaStatus: Image.Null
				anchors.fill: parent
				anchors.margins: Theme.space1
				active: !root.inlinePlaybackActive && root.renderActive && root.expanded && !root.mediaRequiresReveal
					&& root.currentMediaManagedAnimated
				onActiveChanged: mediaStatus = active ? Image.Loading : Image.Null
				sourceComponent: AnimatedImage {
					objectName: "previewExpandedAnimatedImage"
					readonly property string requestedSource: root.imageSource
					source: requestedSource
					asynchronous: true
					cache: false
					playing: visible && status === Image.Ready
					fillMode: Image.PreserveAspectFit
					visible: status === Image.Ready
					onStatusChanged: {
						expandedAnimationLoader.mediaStatus = status
						if (status === Image.Error && root.imageSource.length > 0)
							root.requestImageRefresh()
					}
					Component.onCompleted: expandedAnimationLoader.mediaStatus = status
				}
			}
			ModernBusyIndicator {
				objectName: "previewExpandedBusyIndicator"
                anchors.centerIn: parent
				running: !root.inlinePlaybackActive && !root.mediaRequiresReveal && (root.currentMediaManagedAnimated
					? expandedAnimationLoader.mediaStatus === Image.Loading
					: expandedStaticImage.status === Image.Loading)
                visible: running
				Accessible.name: qsTr("Loading preview media")
            }
			Rectangle {
				objectName: "previewExpandedMediaScrim"
				anchors.fill: parent
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal && (root.hasDirectMedia || root.hasExternalMedia
					|| root.hasExternalImage || root.mediaItems.length > 1)
				color: root.withAlpha(Theme.strip, 0.38)
			}
			Label {
				objectName: "previewExpandedError"
				anchors.centerIn: parent
				width: Math.max(1, parent.width - Theme.space5 * 2)
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal && (root.previewState === "error"
					|| (root.imageSource.length > 0 && (root.currentMediaManagedAnimated
						? expandedAnimationLoader.mediaStatus === Image.Error
						: expandedStaticImage.status === Image.Error)))
				text: root.mediaErrorDescription.length > 0
					? qsTr("Media unavailable: %1").arg(root.mediaErrorDescription)
					: qsTr("Media unavailable")
				textFormat: Text.PlainText
				color: Theme.danger
				font.pixelSize: Theme.fontLabel
				font.bold: true
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
				Accessible.role: Accessible.AlertMessage
				Accessible.name: qsTr("Media unavailable")
				Accessible.description: root.mediaErrorDescription
			}
            ModernButton {
                anchors.centerIn: parent
				visible: !root.inlinePlaybackActive && root.previewState !== "error" && !root.mediaRequiresReveal
					&& (root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
                text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
                    : root.currentMediaKind === "audio" ? qsTr("Play audio") : qsTr("Play video")
                onClicked: root.requestCurrentMedia()
            }
            Button {
				id: expandedMediaAction
				objectName: "previewExpandedMediaButton"
                anchors.fill: parent
				visible: !root.inlinePlaybackActive && root.currentMediaKind === "image"
					&& (root.currentMediaManagedAnimated
						? expandedAnimationLoader.mediaStatus === Image.Ready
						: expandedStaticImage.status === Image.Ready)
                hoverEnabled: true
                background: null
                contentItem: Item {}
                Accessible.name: qsTr("Open preview image")
                onClicked: root.requestCurrentMedia()
            }
			Rectangle {
				objectName: "previewExpandedMediaFocus"
				anchors.fill: parent
				anchors.margins: Theme.space1
				visible: expandedMediaAction.activeFocus
				color: "transparent"
				radius: Theme.innerRadius
				border.color: Theme.focus
				border.width: Theme.focusRingWidth
			}
			ModernIconButton {
                objectName: "previewPreviousMediaButton"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
				visible: !root.inlinePlaybackActive && root.mediaItems.length > 1
                enabled: root.selectedMediaIndex > 0
				iconName: "previous"
                Accessible.name: qsTr("Previous media")
                onClicked: --root.selectedMediaIndex
            }
			ModernIconButton {
                objectName: "previewNextMediaButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
				visible: !root.inlinePlaybackActive && root.mediaItems.length > 1
                enabled: root.selectedMediaIndex + 1 < root.mediaItems.length
				iconName: "next"
                Accessible.name: qsTr("Next media")
                onClicked: ++root.selectedMediaIndex
            }
            Label {
				textFormat: Text.PlainText
                anchors.right: parent.right
                anchors.bottom: parent.bottom
				anchors.margins: Theme.space2
				visible: !root.inlinePlaybackActive && root.mediaItems.length > 1
                text: qsTr("%1 of %2").arg(root.selectedMediaIndex + 1).arg(root.mediaItems.length)
                color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
            }
			Rectangle {
				anchors.fill: parent
				visible: !root.inlinePlaybackActive && root.mediaRequiresReveal
				color: Theme.strip
				radius: parent.radius
				border.color: Theme.surfaceBorder

				ColumnLayout {
					anchors.centerIn: parent
					width: Math.min(parent.width - Theme.space4 * 2, 360)
					spacing: Theme.space2

					Label {
						Layout.fillWidth: true
						text: root.contentWarning.length > 0 ? root.contentWarning
							: qsTr("This preview is hidden by default")
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.bold: true
						wrapMode: Text.Wrap
						horizontalAlignment: Text.AlignHCenter
					}
					ModernButton {
						objectName: "previewExpandedRevealButton"
						Layout.alignment: Qt.AlignHCenter
						visible: root.mediaRequiresReveal && root.expanded
						text: qsTr("Reveal media")
						tone: "accent"
						Accessible.description: qsTr("Show this preview until the card is reused")
						onClicked: root.sensitiveMediaRevealed = true
					}
				}
			}
			Loader {
				id: inlineMediaLoader
				objectName: "previewInlineMediaLoader"
				anchors.fill: parent
				active: root.inlinePlaybackActive && root.renderActive
				asynchronous: true
				function updateSource() {
					if (active) {
						setSource(Qt.resolvedUrl("InlineMediaPlayer.qml"), {
							"session": root.mediaSessionController
						})
					} else {
						source = ""
					}
				}
				onActiveChanged: updateSource()
				Component.onCompleted: updateSource()
			}
        }

		ProviderDetails {
			id: providerDetails
			Layout.fillWidth: true
			metadata: root.preview && root.preview.metadata ? root.preview.metadata : ({})
			previewKind: root.preview ? String(root.preview.kind || "") : ""
			providerHint: root.preview ? String(root.preview.embedKind || root.providerLabel || "") : ""
			previewTitle: root.displayTitle
			previewSubtitle: root.metadataLine
			previewDescription: root.sanitizedDescription
			expanded: root.expanded
			onExternalOpenRequested: (url) => root.externalOpenRequested(url)
		}

        Flow {
			id: previewActionFlow
			objectName: "previewActionFlow"
			Layout.fillWidth: false
			Layout.minimumWidth: root.actionAvailableWidth
			Layout.preferredWidth: root.actionAvailableWidth
			Layout.maximumWidth: root.actionAvailableWidth
			Layout.preferredHeight: visible ? implicitHeight : 0
			spacing: Theme.space2
            visible: root.previewState !== "loading"

            ModernButton {
				objectName: "previewOpenButton"
                visible: root.safeExternalUrl(root.preview.url).length > 0
				text: root.openLabel
				dense: true
				width: root.actionButtonWidth(implicitWidth)
				Accessible.name: root.openLabel
                onClicked: root.externalOpenRequested(root.safeExternalUrl(root.preview.url))
            }
            ModernButton {
                objectName: "previewDirectMediaButton"
				visible: !root.mediaRequiresReveal
					&& (root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
                text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
                    : root.currentMediaKind === "audio" ? qsTr("Play audio") : qsTr("Play video")
				dense: true
				width: root.actionButtonWidth(implicitWidth)
                onClicked: root.requestCurrentMedia()
            }
            ModernButton {
                objectName: "previewPlayButton"
				visible: root.safeEmbedUrl.length > 0 && root.safeEmbedProvider.length > 0
				text: root.inlinePlaybackActive ? qsTr("Playing here") : qsTr("Play here")
				dense: true
				tone: "accent"
				width: root.actionButtonWidth(implicitWidth)
				onClicked: root.requestInlinePlayback()
			}
			ModernButton {
				objectName: "previewPopoutButton"
				visible: root.safeEmbedUrl.length > 0 && root.safeEmbedProvider.length > 0
				text: qsTr("Open player")
				dense: true
				width: root.actionButtonWidth(implicitWidth)
				onClicked: root.popoutPlayRequested(root.safeEmbedUrl, root.safeEmbedProvider)
            }
            ModernButton {
                objectName: "previewWatchTogetherButton"
				visible: root.safeEmbedUrl.length > 0 && root.safeEmbedProvider.length > 0
                enabled: root.watchTogetherAvailable
				dense: true
				width: root.actionButtonWidth(implicitWidth)
                text: root.watchTogetherAvailable ? qsTr("Watch together") : qsTr("Session active")
                Accessible.description: root.watchTogetherAvailable
                    ? qsTr("Start a synchronized media session")
                    : qsTr("End or leave the active media session first")
				onClicked: root.watchTogetherRequested(root.safeEmbedUrl, root.safeEmbedProvider, root.displayTitle)
            }
			ModernButton {
                objectName: "previewExpandButton"
				visible: root.canExpand
				dense: true
				width: root.actionButtonWidth(implicitWidth)
                text: root.userExpanded ? qsTr("Less") : qsTr("More")
                Accessible.name: root.userExpanded ? qsTr("Collapse preview") : qsTr("Expand preview")
                onClicked: root.userExpanded = !root.userExpanded
            }
        }
    }
}
