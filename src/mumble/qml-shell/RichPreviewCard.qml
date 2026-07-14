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
	property bool animationsEnabled: true
	readonly property int animationDuration: animationsEnabled ? Theme.motionFast : 0
    property bool userExpanded: false
    property int selectedMediaIndex: 0
    property bool imageRefreshQueued: false
	property bool sensitiveMediaRevealed: false
	property bool restoreInlinePlaybackFocus: false
	property bool inlineFocusEligibleForRestore: false
	property string inlineFocusPreviewIdentity: ""
	property string inlineFocusSessionId: ""
    readonly property string previewState: preview
        ? (preview.state || (preview.failed ? "error" : preview.loading ? "loading" : "ready")) : ""
	readonly property string sanitizedDescription: safeText(preview ? preview.description : "", 4096)
	readonly property string mediaErrorDescription: firstSafeText([
		preview ? preview.errorDescription : "", preview ? preview.errorMessage : "",
		preview && typeof preview.error === "string" ? preview.error : ""
	], 1024)
	readonly property string errorDescription: previewState === "error" ? mediaErrorDescription : ""
	readonly property bool compact: preview && preview.previewSize === "compact" && !userExpanded
	readonly property bool expanded: preview && (preview.previewSize === "large" || userExpanded)
	readonly property int targetCardWidth: compact ? 460 : expanded ? 720 : 580
	readonly property bool narrowLayout: width < 440
	readonly property real actionAvailableWidth: Math.max(1, width - Theme.space4 * 2)
	readonly property string displayTitle: safeText(preview
		? (preview.title || preview.host || preview.loadingLabel || qsTr("Link preview")) : "", 512)
	readonly property string metadataLine: safeText(preview
		? (preview.subtitle || preview.host || (preview.metadata
			? (preview.metadata.xDisplayName || preview.metadata.xHandle || "") : "")) : "", 512)
	readonly property string providerLabel: displayProviderLabel(firstSafeText(preview ? [
		preview.metadata ? preview.metadata.gameStoreName : "",
		preview.metadata ? preview.metadata.providerName : "",
		preview.metadata ? preview.metadata.productProvider : "",
		preview.metadata ? preview.metadata.vehicleProvider : "",
		preview.metadata ? preview.metadata.audioProvider : "",
		preview.metadata ? preview.metadata.articlePublisher : "",
		preview.metadata ? preview.metadata.marketplaceProvider : "",
		preview.metadata ? preview.metadata.previewProvider : "",
		preview.metadata ? preview.metadata.provider : "",
		preview.providerName, preview.provider, preview.host, preview.embedKind
	] : [], 128))
	readonly property string safeEmbedUrl: safeProviderEmbedUrl(preview ? preview.embedUrl : "")
	readonly property string safeEmbedProvider: safeText(preview ? preview.embedKind : "", 64)
	readonly property string normalizedEmbedAspect: normalizeEmbedAspect(preview ? preview.embedAspect : "")
	readonly property bool hasEmbedPreview: safeEmbedUrl.length > 0 && safeEmbedProvider.length > 0
	readonly property bool watchTogetherSupported: [ "youtube", "twitch", "streamable", "vimeo",
		"dailymotion", "direct" ].indexOf(safeEmbedProvider.toLowerCase()) >= 0
	readonly property string openLabel: safeText(preview ? preview.openLabel : "", 128) || qsTr("Open")
	readonly property string playAccessibilityName: qsTr("Play %1 here").arg(displayTitle)
	readonly property string contentWarning: preview && preview.metadata
		? safeText(preview.metadata.contentWarning, 512) : ""
	readonly property bool thumbnailBlur: {
		const value = preview && preview.metadata ? preview.metadata.thumbnailBlur : false
		return value === true || value === 1 || String(value).toLowerCase() === "true"
	}
	// Content-warning state belongs to the media reveal surface. Do not also
	// expose it as an ordinary provider detail after the user has revealed it.
	readonly property var providerMetadata: metadataForProviderDetails()
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
	readonly property string embedPosterSource: safeRenderImageSource(preview
		? (preview.thumbnailUrl || preview.posterUrl || "") : "")
	readonly property real embedMediaWidth: normalizedEmbedAspect === "short"
		? Math.min(actionAvailableWidth, compact ? 240 : expanded ? 340 : 280)
		: normalizedEmbedAspect === "square"
			? Math.min(actionAvailableWidth, compact ? 240 : expanded ? 440 : 320)
		: normalizedEmbedAspect === "twitch"
			? Math.min(actionAvailableWidth, compact ? 340 : expanded ? 480 : 400)
		: actionAvailableWidth
	readonly property real embedMediaHeight: normalizedEmbedAspect === "short"
		? embedMediaWidth * 16 / 9
		: normalizedEmbedAspect === "square" ? embedMediaWidth
		: normalizedEmbedAspect === "twitch" ? embedMediaWidth * 3 / 4
		: normalizedEmbedAspect === "compact-audio" ? 166
		: normalizedEmbedAspect === "audio" ? 352
		: embedMediaWidth * 9 / 16
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
	readonly property bool mediaRequiresReveal: (hasRevealableMedia || hasEmbedPreview) && !sensitiveMediaRevealed
		&& (contentWarning.length > 0 || thumbnailBlur)
	readonly property bool hasExpandedDescription: previewState === "ready"
		&& sanitizedDescription.length > 0
		&& !providerDetails.ownsDescription
	readonly property bool canExpand: !!preview && preview.previewSize !== "large"
		&& (providerDetails.canExpand || hasExpandedDescription || hasRevealableMedia
			|| hasEmbedPreview || mediaItems.length > 1)
	readonly property bool hasDetails: canExpand
	readonly property bool hasPrimaryDirectAction: !hasEmbedPreview && !mediaRequiresReveal
		&& (hasDirectMedia || hasExternalMedia || hasExternalImage)
	readonly property bool hasPrimaryOpenAction: !hasPrimaryDirectAction
		&& safeExternalUrl(preview ? preview.url : "").length > 0
	readonly property bool hasOverflowActions: hasEmbedPreview || canExpand
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
	color: cardHover.hovered ? Theme.previewCardHover : Theme.previewCardBackground
	border.color: root.previewState === "error" ? root.withAlpha(Theme.danger, 0.65)
		: cardHover.hovered ? root.withAlpha(providerDetails.providerAccent, 0.72) : Theme.previewCardBorder
	border.width: 1
	Behavior on color { ColorAnimation { duration: root.animationDuration } }
	Behavior on border.color { ColorAnimation { duration: root.animationDuration } }
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

	HoverHandler {
		id: cardHover
	}

	MouseArea {
		id: cardOpenSurface
		objectName: "previewCardOpenSurface"
		anchors.fill: parent
		z: 0
		enabled: !root.hasEmbedPreview
			&& root.safeExternalUrl(root.preview ? root.preview.url : "").length > 0
		visible: enabled
		hoverEnabled: true
		onClicked: root.externalOpenRequested(root.safeExternalUrl(root.preview.url))
	}

	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 3
		radius: root.radius
		color: root.previewState === "error" ? Theme.danger : providerDetails.providerAccent
	}

    function resetForReuse() {
        userExpanded = false
        selectedMediaIndex = 0
        imageRefreshQueued = false
		sensitiveMediaRevealed = false
		restoreInlinePlaybackFocus = false
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = ""
		inlineFocusSessionId = ""
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

	function normalizeEmbedAspect(value) {
		const token = safeText(value, 32).toLowerCase()
		if (token === "short" || token === "portrait" || token === "9:16")
			return "short"
		if (token === "square" || token === "1:1")
			return "square"
		if (token === "audio" || token === "compact-audio" || token === "twitch")
			return token
		const provider = safeText(preview ? preview.embedKind : "", 64).toLowerCase()
		if (provider === "tiktok")
			return "short"
		if (provider === "instagram") {
			const mediaKind = safeText(preview && preview.metadata
				? preview.metadata.instagramMediaKind : "", 32).toLowerCase()
			return mediaKind === "reel" ? "short" : "square"
		}
		if (provider === "spotify")
			return "compact-audio"
		if (provider === "twitch")
			return "twitch"
		if (provider === "soundcloud" || provider === "bandcamp")
			return "audio"
		return "wide"
	}

	function displayProviderLabel(value) {
		const text = safeText(value, 128)
		const token = text.toLowerCase().replace(/[^a-z0-9]/g, "")
		const names = {
			"youtube": "YouTube", "spotify": "Spotify", "tiktok": "TikTok",
			"instagram": "Instagram", "twitch": "Twitch", "google": "Google",
			"googlesearch": "Google", "steam": "Steam", "gamestore": qsTr("Game store"),
			"yahoofinance": "Yahoo Finance", "blocket": "Blocket", "tradera": "Tradera",
			"bytbil": "Bytbil", "bilweb": "Bilweb", "booli": "Booli", "hemnet": "Hemnet",
			"flashback": "Flashback", "existenz": "Existenz", "sverigesradio": "Sveriges Radio"
		}
		return names[token] || text
	}

	function requestInlinePlayback() {
		userExpanded = true
		inlinePlayRequested(safeEmbedUrl, safeEmbedProvider)
	}

	function requestInlinePlaybackWithFocus() {
		restoreInlinePlaybackFocus = true
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = previewIdentity
		inlineFocusSessionId = String(mediaSessionId || "")
		requestInlinePlayback()
	}

	function itemIsWithin(item, ancestor) {
		let candidate = item
		while (candidate) {
			if (candidate === ancestor)
				return true
			candidate = candidate.parent
		}
		return false
	}

	function inlineFocusRequestMatchesCurrentSession() {
		return restoreInlinePlaybackFocus
			&& inlineFocusPreviewIdentity === previewIdentity
			&& inlineFocusSessionId.length > 0
			&& inlineFocusSessionId === String(mediaSessionId || "")
			&& inlineFocusSessionId === String(mediaSessionController
				? mediaSessionController.sessionId || "" : "")
	}

	function handOffInlinePlaybackFocus() {
		if (!inlineFocusRequestMatchesCurrentSession() || !inlineMediaLoader.item
				|| !inlineMediaLoader.item.focusInitialControl)
			return false
		return inlineMediaLoader.item.focusInitialControl()
	}

	function captureInlinePlaybackFocusForRestore() {
		const activeFocusItem = root.Window ? root.Window.activeFocusItem : null
		inlineFocusEligibleForRestore = inlineFocusRequestMatchesCurrentSession()
			&& !!inlineMediaLoader.item
			&& itemIsWithin(activeFocusItem, inlineMediaLoader.item)
	}

	function clearInlinePlaybackFocusRequest() {
		restoreInlinePlaybackFocus = false
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = ""
		inlineFocusSessionId = ""
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
	onInlinePlaybackActiveChanged: {
		if (inlinePlaybackActive || !restoreInlinePlaybackFocus)
			return
		Qt.callLater(function() {
			if (root.inlineFocusEligibleForRestore
					&& root.inlineFocusPreviewIdentity === root.previewIdentity
					&& previewPlayButton.visible && previewPlayButton.enabled)
				previewPlayButton.forceActiveFocus()
			root.clearInlinePlaybackFocusRequest()
		})
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

	function metadataForProviderDetails() {
		const source = preview && preview.metadata ? preview.metadata : ({})
		const result = ({})
		const keys = Object.keys(source)
		for (let index = 0; index < keys.length; ++index) {
			const key = keys[index]
			if (key !== "contentWarning" && key !== "thumbnailBlur")
				result[key] = source[key]
		}
		return result
	}

	function withAlpha(color, alpha) {
		return Qt.rgba(color.r, color.g, color.b, alpha)
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
			const fallbackKind = mediaKind({ "kind": preview.mediaKind || preview.kind,
				"mime": preview.mediaMime })
			if (fallbackKind === "image") {
				// The C++ image pipeline rewrites inline/data images to image://mumble.
				// Keep that managed source on the image path instead of passing it
				// through the audio/video URL validator.
				const renderSource = safeRenderImageSource(preview.mediaUrl || "", !!preview.mediaAnimated)
				const externalUrl = safeExternalUrl(preview.mediaExternalUrl
					|| (renderSource.length === 0 ? preview.mediaUrl : ""))
				if (renderSource.length > 0 || externalUrl.length > 0) {
					result.push({
						"url": renderSource,
						"externalUrl": externalUrl,
						"mime": safeText(preview.mediaMime, 128),
						"kind": "image",
						"thumbnail": safeRenderImageSource(preview.thumbnailUrl || ""),
						"poster": safeRenderImageSource(preview.thumbnailUrl || ""),
						"title": root.displayTitle,
						"directPlayable": renderSource.length > 0,
						"managedAnimated": !!preview.mediaAnimated && /^file:\/\//i.test(renderSource)
					})
				}
				return result
			}
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
		z: 1

		Rectangle {
			id: embedMediaPanel
			objectName: "previewEmbedMediaPanel"
			Layout.alignment: Qt.AlignHCenter
			Layout.preferredWidth: root.embedMediaWidth
			Layout.maximumWidth: root.actionAvailableWidth
			Layout.preferredHeight: root.hasEmbedPreview ? root.embedMediaHeight : 0
			visible: root.hasEmbedPreview
			radius: Theme.innerRadius
			color: Theme.embedSurface
			gradient: Gradient {
				GradientStop {
					position: 0.0
					color: root.withAlpha(providerDetails.providerAccent, 0.34)
				}
				GradientStop {
					position: 1.0
					color: root.withAlpha(Theme.embedSurface, 0.98)
				}
			}
			border.color: root.withAlpha(providerDetails.providerAccent, 0.46)
			border.width: 1
			clip: true

			Image {
				id: embedPoster
				objectName: "previewEmbedPoster"
				anchors.fill: parent
				source: root.renderActive && !root.inlinePlaybackActive && !root.mediaRequiresReveal
					? root.embedPosterSource : ""
				asynchronous: true
				cache: false
				sourceSize: Qt.size(Math.min(1280, root.embedMediaWidth * Screen.devicePixelRatio),
					Math.min(1280, root.embedMediaHeight * Screen.devicePixelRatio))
				// Match the production preview treatment: the poster is a backdrop for
				// the provider action, including audio embeds. PreserveAspectFit left
				// square album art floating in a wide empty strip for Spotify.
				fillMode: Image.PreserveAspectCrop
				visible: status === Image.Ready && !root.inlinePlaybackActive
				onStatusChanged: if (status === Image.Error && root.embedPosterSource.length > 0)
					root.requestImageRefresh()
			}

			Rectangle {
				anchors.fill: parent
				visible: !root.inlinePlaybackActive
				color: root.withAlpha(Theme.embedOverlayBase, embedPoster.status === Image.Ready ? 0.30 : 0.18)
			}

			Button {
				id: embedPosterAction
				objectName: "previewEmbedPosterAction"
				anchors.fill: parent
				z: 1
				visible: !root.inlinePlaybackActive
				enabled: root.renderActive && root.previewState !== "loading"
				hoverEnabled: true
				focusPolicy: Qt.NoFocus
				background: Rectangle {
					color: embedPosterAction.down ? root.withAlpha(Theme.accentSubtle, 0.28)
						: embedPosterAction.hovered ? root.withAlpha(Theme.embedHover, 0.18)
						: "transparent"
					radius: Theme.innerRadius
					Behavior on color {
						ColorAnimation { duration: root.animationDuration }
					}
				}
				contentItem: Item {}
				Accessible.ignored: true
				onClicked: {
					if (root.mediaRequiresReveal)
						root.sensitiveMediaRevealed = true
					else
						root.requestInlinePlaybackWithFocus()
				}
			}

			Rectangle {
				objectName: "previewEmbedProviderBadge"
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.margins: Theme.space2
				z: 2
				visible: root.providerLabel.length > 0 && !root.inlinePlaybackActive
				width: Math.min(parent.width - Theme.space4,
					embedProviderLabel.implicitWidth + Theme.space2 * 2)
				height: Theme.space5
				radius: height / 2
				color: root.withAlpha(providerDetails.providerAccent, 0.92)

				Label {
					id: embedProviderLabel
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space2
					text: root.providerLabel
					textFormat: Text.PlainText
					color: Theme.contrastText(providerDetails.providerAccent)
					font.pixelSize: Theme.fontCaption
					font.bold: true
					elide: Text.ElideRight
					horizontalAlignment: Text.AlignHCenter
					verticalAlignment: Text.AlignVCenter
				}
			}

			Rectangle {
				objectName: "previewEmbedProviderState"
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space2
				z: 2
				visible: providerDetails.providerStateLabel.length > 0 && !root.inlinePlaybackActive
				width: Math.min(parent.width / 2, embedProviderStateLabel.implicitWidth + Theme.space2 * 2)
				height: Theme.space5
				radius: height / 2
				color: root.withAlpha(providerDetails.providerStateColor, 0.92)

				Label {
					id: embedProviderStateLabel
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space2
					text: providerDetails.providerStateLabel
					textFormat: Text.PlainText
					color: Theme.contrastText(providerDetails.providerStateColor)
					font.pixelSize: Theme.fontCaption
					font.bold: true
					elide: Text.ElideRight
					horizontalAlignment: Text.AlignHCenter
					verticalAlignment: Text.AlignVCenter
				}
			}

			ColumnLayout {
				objectName: "previewTwitchPosterCopy"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.bottom: parent.bottom
				anchors.margins: Theme.space3
				anchors.rightMargin: Math.max(Theme.space3, parent.width * 0.25)
				z: 2
				visible: providerDetails.variant === "twitch" && !root.inlinePlaybackActive
				spacing: Theme.space1
				Label {
					objectName: "previewTwitchPosterTitle"
					Layout.fillWidth: true
					text: root.displayTitle.length > 0 ? root.displayTitle : "Twitch"
					textFormat: Text.PlainText
					color: "#ffffff"
					font.pixelSize: Theme.fontTitle
					font.bold: true
					elide: Text.ElideRight
				}
				Label {
					objectName: "previewTwitchPosterNote"
					Layout.fillWidth: true
					visible: text.length > 0
					text: root.safeText(root.preview && root.preview.metadata
						? (root.preview.metadata.twitchDisclaimer
							|| root.preview.metadata.twitchPlaybackNote || "") : "", 512)
					textFormat: Text.PlainText
					color: "#dce4ee"
					font.pixelSize: Theme.fontCaption
					font.bold: true
					wrapMode: Text.Wrap
					maximumLineCount: 2
					elide: Text.ElideRight
				}
			}

			ModernIcon {
				anchors.centerIn: parent
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal
					&& root.previewState !== "loading" && embedPoster.status !== Image.Ready
				name: "play"
				size: Theme.space6
				color: Theme.textMuted
				Accessible.ignored: true
			}

			ModernBusyIndicator {
				objectName: "previewEmbedBusyIndicator"
				anchors.centerIn: parent
				running: !root.inlinePlaybackActive && !root.mediaRequiresReveal
					&& (root.previewState === "loading" || embedPoster.status === Image.Loading)
				visible: running
				animated: root.animationsEnabled
				Accessible.name: qsTr("Loading provider preview")
			}

			ModernButton {
				id: previewPlayButton
				objectName: "previewPlayButton"
				readonly property bool posterBacked: embedPoster.status === Image.Ready
				anchors.centerIn: parent
				z: 3
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal
					&& root.previewState !== "loading" && embedPoster.status !== Image.Loading
				enabled: root.renderActive
				text: qsTr("Play here")
				implicitWidth: posterBacked ? Math.max(54, Theme.controlHeight + Theme.space5)
					: Math.max(132, playButtonContent.implicitWidth + Theme.space4 * 2)
				implicitHeight: posterBacked ? implicitWidth : Theme.controlHeight
				contentItem: Item {
					Row {
						id: playButtonContent
						objectName: "previewPlayButtonContent"
						anchors.centerIn: parent
						spacing: Theme.space2
						ModernIcon {
							objectName: "previewPlayIcon"
							name: "play"
							size: previewPlayButton.posterBacked ? Theme.avatarMedium : Theme.avatarSmall
							color: Theme.contrastText(providerDetails.providerAccent)
						}
						Label {
							objectName: "previewPlayText"
							visible: !previewPlayButton.posterBacked
							text: previewPlayButton.text
							textFormat: Text.PlainText
							color: Theme.contrastText(providerDetails.providerAccent)
							font: previewPlayButton.font
							verticalAlignment: Text.AlignVCenter
						}
					}
				}
				background: Rectangle {
					objectName: "previewPlayButtonSurface"
					radius: previewPlayButton.posterBacked ? width / 2 : Theme.innerRadius
					color: previewPlayButton.down
						? Qt.darker(providerDetails.providerAccent, 1.08)
						: previewPlayButton.hovered ? Qt.lighter(providerDetails.providerAccent, 1.08)
						: root.withAlpha(providerDetails.providerAccent, 0.94)
					border.color: previewPlayButton.activeFocus ? Theme.focus
						: root.withAlpha(Theme.textStrong, 0.46)
					border.width: previewPlayButton.activeFocus ? Theme.focusRingWidth : 1
					Behavior on color { ColorAnimation { duration: root.animationDuration } }
				}
				Accessible.name: root.playAccessibilityName
				Accessible.description: qsTr("Loads the provider player in this preview")
				onClicked: root.requestInlinePlaybackWithFocus()
			}

			Rectangle {
				objectName: "previewEmbedRevealSurface"
				anchors.fill: parent
				z: 2
				visible: !root.inlinePlaybackActive && root.mediaRequiresReveal
				color: Theme.embedRevealSurface
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
						objectName: "previewEmbedRevealButton"
						Layout.alignment: Qt.AlignHCenter
						text: qsTr("Reveal media")
						tone: "accent"
						Accessible.description: qsTr("Show this provider preview until the card is reused")
						onClicked: root.sensitiveMediaRevealed = true
					}
				}
			}

			Loader {
				id: inlineMediaLoader
				objectName: "previewInlineMediaLoader"
				anchors.fill: parent
				active: root.inlinePlaybackActive && root.renderActive && !root.mediaRequiresReveal
				asynchronous: true
				function updateSource() {
					if (active) {
						setSource(Qt.resolvedUrl("InlineMediaPlayer.qml"), {
							"session": root.mediaSessionController,
							"aspect": root.normalizedEmbedAspect
						})
					} else {
						source = ""
					}
				}
				onActiveChanged: {
					if (!active)
						root.captureInlinePlaybackFocusForRestore()
					updateSource()
				}
				onLoaded: if (root.restoreInlinePlaybackFocus)
					Qt.callLater(function() { root.handOffInlinePlaybackFocus() })
				Component.onCompleted: updateSource()
			}
		}

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            Rectangle {
				Layout.preferredWidth: root.compact ? 56 : 92
				Layout.preferredHeight: root.compact ? 56 : 64
				visible: !root.hasEmbedPreview && (root.imageSource.length > 0
					|| root.previewState !== "ready" || root.hasExternalImage
					|| root.hasDirectMedia || root.hasExternalMedia)
				radius: Theme.innerRadius
				color: Theme.embedSurface
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
					animated: root.animationsEnabled
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
					background: Rectangle {
						objectName: "previewCompactMediaState"
						visible: compactMediaAction.visible && (!compactMediaAction.enabled
							|| compactMediaAction.hovered || compactMediaAction.down)
						color: !compactMediaAction.enabled
							? root.withAlpha(Theme.embedSurface, 0.48)
							: compactMediaAction.down ? Theme.embedSelection
							: root.withAlpha(Theme.embedHover, 0.34)
						radius: Theme.innerRadius
						Behavior on color {
							ColorAnimation { duration: root.animationDuration }
						}
					}
					contentItem: Item {}
					Accessible.name: root.currentMediaKind === "image" ? qsTr("Open preview image") : qsTr("Play direct media")
					Accessible.ignored: !enabled
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
					objectName: "previewCompactRevealSurface"
					anchors.fill: parent
					visible: root.mediaRequiresReveal && !root.expanded
					color: Theme.embedRevealSurface
					radius: parent.radius
					border.color: Theme.embedBorder

					Button {
						id: compactRevealButton
						objectName: "previewCompactRevealButton"
						anchors.fill: parent
						visible: root.mediaRequiresReveal && !root.expanded
						hoverEnabled: true
						background: Rectangle {
							objectName: "previewCompactRevealState"
							visible: compactRevealButton.hovered || compactRevealButton.down
							color: compactRevealButton.down ? Theme.embedSelection
								: root.withAlpha(Theme.embedHover, 0.42)
							radius: Theme.innerRadius
							Behavior on color {
								ColorAnimation { duration: root.animationDuration }
							}
						}
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
					visible: root.providerLabel.length > 0 && !root.hasEmbedPreview
					radius: height / 2
					color: providerDetails.providerAccentSubtle
					border.color: providerDetails.providerAccentBorder
					Label {
						id: providerText
						objectName: "previewProviderLabel"
						anchors.fill: parent
						anchors.leftMargin: Theme.space2
						anchors.rightMargin: Theme.space2
						textFormat: Text.PlainText
						text: root.providerLabel.toUpperCase()
						color: providerDetails.providerAccent
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
					objectName: "previewDescription"
					textFormat: Text.PlainText
                    Layout.fillWidth: true
					visible: root.previewState === "ready"
						&& root.hasExpandedDescription
					text: root.sanitizedDescription
                    color: Theme.textMain
					font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 12 : 3
					elide: root.expanded ? Text.ElideNone : Text.ElideRight
                }
            }
        }

        Rectangle {
			id: expandedMediaPanel
			objectName: "previewExpandedMediaPanel"
			Layout.alignment: Qt.AlignHCenter
			Layout.preferredWidth: root.width
			Layout.minimumWidth: root.width
			Layout.maximumWidth: root.width
			transform: Translate { x: -Theme.space4 }
			Layout.preferredHeight: !root.hasEmbedPreview && root.expanded && (root.imageSource.length > 0
                                    || root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
									? Math.min(420, Math.max(180, root.width * 9 / 16)) : 0
            visible: Layout.preferredHeight > 0
			color: Theme.embedSurface
			radius: 0
			border.color: Theme.embedBorder
			border.width: 1
            clip: true

			Image {
				id: expandedStaticImage
				objectName: "previewExpandedStaticImage"
                anchors.fill: parent
				source: !root.inlinePlaybackActive && root.renderActive && root.expanded && !root.mediaRequiresReveal
					&& !root.currentMediaManagedAnimated
					? root.imageSource : ""
                asynchronous: true
				cache: false
				fillMode: Image.PreserveAspectCrop
				visible: !root.inlinePlaybackActive && source.toString().length > 0 && status === Image.Ready
                onStatusChanged: if (status === Image.Error && root.imageSource.length > 0)
                                     root.requestImageRefresh()
            }
			Loader {
				id: expandedAnimationLoader
				objectName: "previewExpandedAnimatedLoader"
				property int mediaStatus: Image.Null
				anchors.fill: parent
				active: !root.inlinePlaybackActive && root.renderActive && root.expanded && !root.mediaRequiresReveal
					&& root.currentMediaManagedAnimated
				onActiveChanged: mediaStatus = active ? Image.Loading : Image.Null
				sourceComponent: AnimatedImage {
					objectName: "previewExpandedAnimatedImage"
					readonly property string requestedSource: root.imageSource
					source: requestedSource
					asynchronous: true
					cache: false
					playing: root.animationsEnabled && visible && status === Image.Ready
					fillMode: Image.PreserveAspectCrop
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
				animated: root.animationsEnabled
				Accessible.name: qsTr("Loading preview media")
            }
			Rectangle {
				objectName: "previewExpandedMediaScrim"
				anchors.fill: parent
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal && (root.hasDirectMedia || root.hasExternalMedia
					|| root.hasExternalImage || root.mediaItems.length > 1)
				color: root.withAlpha(Theme.embedOverlayBase, 0.38)
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
				background: Rectangle {
					objectName: "previewExpandedMediaState"
					visible: expandedMediaAction.visible && (!expandedMediaAction.enabled
						|| expandedMediaAction.hovered || expandedMediaAction.down)
					color: !expandedMediaAction.enabled
						? root.withAlpha(Theme.embedSurface, 0.48)
						: expandedMediaAction.down ? Theme.embedSelection
						: root.withAlpha(Theme.embedHover, 0.34)
					radius: Theme.innerRadius
					Behavior on color {
						ColorAnimation { duration: root.animationDuration }
					}
				}
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
				objectName: "previewExpandedRevealSurface"
				anchors.fill: parent
				visible: !root.inlinePlaybackActive && root.mediaRequiresReveal
				color: Theme.embedRevealSurface
				radius: parent.radius
				border.color: Theme.embedBorder

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
        }

		ListView {
			id: steamMediaRail
			objectName: "previewSteamMediaRail"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? 52 : 0
			visible: root.expanded && providerDetails.steamPresentation
				&& root.mediaItems.length > 1 && !root.inlinePlaybackActive
			orientation: ListView.Horizontal
			spacing: Theme.space1
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			model: root.mediaItems
			currentIndex: root.selectedMediaIndex
			Accessible.role: Accessible.List
			Accessible.name: qsTr("Steam media gallery")
			delegate: Button {
				id: steamMediaThumbnail
				required property var modelData
				required property int index
				readonly property string mediaKind: root.safeText(modelData.kind, 32).toLowerCase()
				readonly property string posterSource: root.safeRenderImageSource(modelData.thumbnail
					|| modelData.poster || (mediaKind === "image" ? modelData.url : ""))
				objectName: "previewSteamMediaThumbnail_" + index
				width: 82
				height: 48
				hoverEnabled: true
				background: Rectangle {
					radius: Theme.space1
					color: steamMediaThumbnail.down ? Theme.embedSelection
						: steamMediaThumbnail.hovered ? Theme.embedHover : Theme.embedSurface
					border.width: steamMediaThumbnail.index === root.selectedMediaIndex ? 2 : 1
					border.color: steamMediaThumbnail.index === root.selectedMediaIndex
						? providerDetails.providerAccent : Theme.embedBorder
				}
				contentItem: Item {
					clip: true
					Image {
						anchors.fill: parent
						anchors.margins: Theme.space1
						source: steamMediaThumbnail.posterSource
						asynchronous: true
						cache: false
						fillMode: Image.PreserveAspectCrop
						visible: status === Image.Ready
					}
					ModernIcon {
						anchors.centerIn: parent
						visible: steamMediaThumbnail.posterSource.length === 0
						name: steamMediaThumbnail.mediaKind === "video" ? "play" : "external"
						size: Theme.avatarSmall
						color: providerDetails.providerAccent
						Accessible.ignored: true
					}
					Rectangle {
						anchors.left: parent.left
						anchors.bottom: parent.bottom
						anchors.margins: Theme.space1
						visible: steamMediaThumbnail.mediaKind === "video"
						width: steamPlayLabel.implicitWidth + Theme.space1 * 2
						height: Theme.space4
						radius: height / 2
						color: root.withAlpha(Theme.embedOverlayBase, 0.88)
						Label {
							id: steamPlayLabel
							anchors.centerIn: parent
							text: qsTr("PLAY")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: 9
							font.bold: true
							Accessible.ignored: true
						}
					}
				}
				Accessible.role: Accessible.ListItem
				Accessible.name: mediaKind === "video"
					? qsTr("Steam trailer %1").arg(index + 1)
					: qsTr("Steam screenshot %1").arg(index + 1)
				Accessible.selected: index === root.selectedMediaIndex
				onClicked: root.selectedMediaIndex = index
			}
		}

		ProviderDetails {
			id: providerDetails
			Layout.fillWidth: true
			metadata: root.providerMetadata
			previewKind: root.preview ? String(root.preview.kind || "") : ""
			providerHint: root.preview ? String(root.preview.embedKind || root.providerLabel || "") : ""
			previewTitle: root.displayTitle
			previewSubtitle: root.metadataLine
			previewDescription: root.sanitizedDescription
			expanded: root.expanded
			onExternalOpenRequested: (url) => root.externalOpenRequested(url)
		}

		RowLayout {
			id: previewActionFlow
			objectName: "previewActionFlow"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			spacing: Theme.space2
            visible: root.previewState !== "loading"

            ModernButton {
				objectName: "previewOpenButton"
				visible: root.hasPrimaryOpenAction
				Layout.maximumWidth: Math.max(1, root.actionAvailableWidth
					- (previewOverflowButton.visible ? previewOverflowButton.implicitWidth + Theme.space2 : 0))
				Layout.fillWidth: root.narrowLayout
				text: root.openLabel
				dense: true
				Accessible.role: Accessible.Link
				Accessible.name: root.openLabel + ": " + root.displayTitle
				Accessible.description: root.sanitizedDescription
				function openDestination() {
					root.externalOpenRequested(root.safeExternalUrl(root.preview.url))
				}
				onClicked: openDestination()
				Keys.onReturnPressed: event => {
					openDestination()
					event.accepted = true
				}
				Keys.onEnterPressed: event => {
					openDestination()
					event.accepted = true
				}
            }
            ModernButton {
                objectName: "previewDirectMediaButton"
				visible: root.hasPrimaryDirectAction
				Layout.maximumWidth: Math.max(1, root.actionAvailableWidth
					- (previewOverflowButton.visible ? previewOverflowButton.implicitWidth + Theme.space2 : 0))
				Layout.fillWidth: root.narrowLayout
                text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
                    : root.currentMediaKind === "audio" ? qsTr("Play audio") : qsTr("Play video")
				dense: true
                onClicked: root.requestCurrentMedia()
            }
			Item {
				Layout.fillWidth: true
				Layout.preferredWidth: 1
			}
			ModernIconButton {
				id: previewOverflowButton
				objectName: "previewOverflowButton"
				visible: root.hasOverflowActions
				dense: true
				iconName: "more"
				Accessible.name: qsTr("Preview actions")
				Accessible.description: qsTr("Open secondary playback and preview actions")
				onClicked: previewOverflowMenu.open()

				ModernMenu {
					id: previewOverflowMenu
					objectName: "previewOverflowMenu"
					x: Math.min(0, previewOverflowButton.width - width)
					y: previewOverflowButton.height
					implicitWidth: 210

					MenuItem {
						objectName: "previewPopoutButton"
						visible: root.hasEmbedPreview
						text: qsTr("Open player")
						Accessible.description: qsTr("Open the provider player in a separate window")
						onTriggered: root.popoutPlayRequested(root.safeEmbedUrl, root.safeEmbedProvider)
					}
					MenuItem {
						objectName: "previewWatchTogetherButton"
						visible: root.hasEmbedPreview && root.watchTogetherSupported
						enabled: root.watchTogetherAvailable
						text: root.watchTogetherAvailable ? qsTr("Watch together") : qsTr("Session active")
						Accessible.description: root.watchTogetherAvailable
							? qsTr("Start a synchronized media session")
							: qsTr("End or leave the active media session first")
						onTriggered: root.watchTogetherRequested(root.safeEmbedUrl,
							root.safeEmbedProvider, root.displayTitle)
					}
					MenuItem {
						objectName: "previewExpandButton"
						visible: root.canExpand
						text: root.userExpanded ? qsTr("Less") : qsTr("More")
						Accessible.name: root.userExpanded ? qsTr("Collapse preview") : qsTr("Expand preview")
						onTriggered: root.userExpanded = !root.userExpanded
					}
				}
			}
        }
    }
}
