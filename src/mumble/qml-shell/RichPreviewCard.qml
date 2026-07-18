pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

Rectangle {
    id: root

    required property var preview
    property string previewIdentity: preview
        ? [preview.url || "", preview.embedUrl || "", preview.mediaUrl || "",
           preview.mediaExternalUrl || "", preview.title || ""].join("|") : ""
    property bool watchTogetherAvailable: true
	property var mediaSessionController: null
	property string mediaSessionId: ""
	property string visualMediaFixtureMode: ""
	property url inlinePlayerComponentUrl: Qt.resolvedUrl("InlineMediaPlayer.qml")
	property bool renderActive: true
	property bool animationsEnabled: true
	property bool hoverEffectsEnabled: true
	readonly property int animationDuration: animationsEnabled ? Theme.motionFast : 0
	readonly property bool effectiveHovered: hoverEffectsEnabled && cardHover.hovered
	property bool userExpanded: false
	property string sizePresetOverride: ""
	property string savedSizePreset: ""
    property int selectedMediaIndex: 0
    property bool imageRefreshQueued: false
	property bool sensitiveMediaRevealed: false
	property bool restoreInlinePlaybackFocus: false
	property bool inlineFocusEligibleForRestore: false
	property string inlineFocusPreviewIdentity: ""
	property string inlineFocusSessionId: ""
	property int inlineFocusRestoreGeneration: 0
	property var mediaProfileFactory: null
	readonly property bool mediaRuntimeContractAvailable: !!mediaProfileFactory
		&& typeof mediaProfileFactory.runtimeReady !== "undefined"
	readonly property bool mediaRuntimeReady: !mediaRuntimeContractAvailable
		|| !!mediaProfileFactory.runtimeReady
	readonly property bool mediaRuntimePreparing: mediaRuntimeContractAvailable
		&& !!mediaProfileFactory.runtimePreparing
	readonly property string mediaRuntimeError: mediaRuntimeContractAvailable
		? String(mediaProfileFactory.runtimeError || "") : ""
	readonly property bool mediaRuntimeCanRetry: mediaRuntimeError.length > 0
		&& !!mediaProfileFactory && typeof mediaProfileFactory.retryRuntime === "function"
    readonly property string previewState: preview
        ? (preview.state || (preview.failed ? "error" : preview.loading ? "loading" : "ready")) : ""
	readonly property string sanitizedDescription: safeText(preview ? preview.description : "", 4096)
	readonly property string mediaErrorDescription: firstSafeText([
		preview ? preview.errorDescription : "", preview ? preview.errorMessage : "",
		preview && typeof preview.error === "string" ? preview.error : ""
	], 1024)
	readonly property string errorDescription: previewState === "error" ? mediaErrorDescription : ""
	readonly property string previewStateLabel: previewState === "loading" ? qsTr("Loading")
		: previewState === "error" ? qsTr("Unavailable")
		: mediaRequiresReveal ? qsTr("Hidden") : ""
	readonly property color previewStateColor: previewState === "error" ? Theme.danger
		: mediaRequiresReveal ? Theme.warning : Theme.textMuted
	readonly property string effectiveSizePreset: userExpanded ? "large"
		: normalizeSizePreset(sizePresetOverride.length > 0 ? sizePresetOverride
			: savedSizePreset.length > 0 ? savedSizePreset
			: (preview ? preview.previewSize : ""))
	readonly property bool compact: effectiveSizePreset === "compact"
	readonly property bool expanded: effectiveSizePreset === "large"
	readonly property real responsiveViewportWidth: root.Window.window
		? Math.max(1, root.Window.window.width) : Math.max(1, width)
	readonly property int responsiveCompactCardWidth: Math.round(Math.max(300,
		Math.min(460, responsiveViewportWidth * 0.48)))
	readonly property int responsiveDefaultCardWidth: Math.round(Math.max(340,
		Math.min(580, responsiveViewportWidth * 0.62)))
	readonly property int responsiveLargeCardWidth: Math.round(Math.max(400,
		Math.min(720, responsiveViewportWidth * 0.74)))
	readonly property int targetCardWidth: normalizedEmbedAspect === "short"
		? (expanded ? 420 : compact ? 320 : 360)
		: normalizedEmbedAspect === "square" ? (expanded ? 520 : compact ? 340 : 460)
		: normalizedEmbedAspect === "twitch" ? (expanded ? 620 : compact ? 420 : 520)
		: compact ? responsiveCompactCardWidth
		: expanded ? responsiveLargeCardWidth : responsiveDefaultCardWidth
	readonly property bool narrowLayout: compact || width < 440
	readonly property real actionAvailableWidth: Math.max(1, width - Theme.space4 * 2)
	readonly property string previewHost: safeText(preview ? preview.host : "", 512)
	readonly property string rawDisplayTitle: safeText(preview
		? (preview.title || preview.loadingLabel || "") : "", 512)
	readonly property string rawMetadataLine: firstSafeText(preview ? [
		preview.subtitle,
		preview.metadata ? (preview.metadata.xDisplayName || preview.metadata.xHandle || "") : ""
	] : [], 512)
	readonly property string displayTitle: rawDisplayTitle.length > 0
		&& !equivalentPreviewText(rawDisplayTitle, previewHost) ? rawDisplayTitle
		: hasEmbedPreview ? fallbackEmbedTitle(safeEmbedProvider)
		: previewHost.length > 0 ? previewHost : qsTr("Link preview")
	readonly property string metadataLine: rawMetadataLine.length > 0
		&& !equivalentPreviewText(rawMetadataLine, displayTitle) ? rawMetadataLine
		: previewHost.length > 0 && !equivalentPreviewText(previewHost, displayTitle) ? previewHost : ""
	readonly property string inferredGenericSocialProviderToken: inferGenericSocialProviderToken()
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
		inferredGenericSocialProviderToken,
		preview.providerName, preview.provider, preview.embedKind, preview.host
	] : [], 128))
	readonly property string safeEmbedUrl: safeProviderEmbedUrl(preview ? preview.embedUrl : "")
	readonly property string safeEmbedProvider: safeText(preview ? preview.embedKind : "", 64)
	readonly property string normalizedEmbedProvider: safeEmbedProvider.toLowerCase()
	readonly property string normalizedEmbedAspect: normalizeEmbedAspect(preview ? preview.embedAspect : "")
	// Direct media is represented by typed media fields and never by a generic
	// sender-controlled WebEngine embed.
	readonly property bool hasEmbedPreview: safeEmbedUrl.length > 0 && safeEmbedProvider.length > 0
		&& normalizedEmbedProvider !== "direct"
	readonly property string instagramEmbedMediaKind: normalizedInstagramEmbedMediaKind()
	readonly property bool instagramStaticPost: normalizedEmbedProvider === "instagram"
		&& instagramEmbedMediaKind === "post"
	// Local rendering and synchronized room playback are separate capabilities.
	// A provider can be viewed or played in this card without implementing the
	// deterministic state contract required by Watch Together.
	readonly property bool localPlaybackSupported: hasDirectMedia
		|| (hasEmbedPreview && !instagramStaticPost)
	readonly property bool sharedPlaybackSupported: hasEmbedPreview
		&& normalizedEmbedProvider === "youtube" && isSupportedYouTubeSharedPlaybackUrl(safeEmbedUrl)
	// Compatibility alias for callers and automation that already consume this
	// property. Its meaning is now deliberately limited to shared playback.
	readonly property bool watchTogetherSupported: sharedPlaybackSupported
	readonly property string openLabel: safeText(preview ? preview.openLabel : "", 128) || qsTr("Open")
	readonly property bool inlineActionUsesPlaybackSemantics: !instagramStaticPost
	readonly property string inlineActionLabel: inlineActionUsesPlaybackSemantics
		? qsTr("Play here") : qsTr("View here")
	readonly property string inlineActionIconName: inlineActionUsesPlaybackSemantics ? "play" : "eye"
	readonly property string inlineActionAccessibilityName: inlineActionUsesPlaybackSemantics
		? qsTr("Play %1 here").arg(displayTitle) : qsTr("View %1 here").arg(displayTitle)
	readonly property string inlineActionAccessibilityDescription: inlineActionUsesPlaybackSemantics
		? qsTr("Loads the provider player in this preview")
		: qsTr("Loads the provider post in this preview")
	readonly property string playAccessibilityName: inlineActionAccessibilityName
	readonly property string popoutActionLabel: inlineActionUsesPlaybackSemantics
		? qsTr("Open in separate player") : qsTr("Open in separate viewer")
	readonly property string popoutActionDescription: inlineActionUsesPlaybackSemantics
		? qsTr("Open the provider player in a separate window")
		: qsTr("Open the provider post in a separate window")
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
	readonly property string embedPosterSource: hasEmbedPreview
		? safeRenderImageSource(preview ? (preview.thumbnailUrl || preview.posterUrl || "") : "")
		: imageSource
	readonly property bool fullBleedEmbed: hasEmbedPreview
		&& normalizedEmbedAspect !== "short" && normalizedEmbedAspect !== "square"
	readonly property real embedMediaWidth: normalizedEmbedAspect === "short"
		? Math.min(actionAvailableWidth, compact ? 240 : expanded ? 340 : 280)
		: normalizedEmbedAspect === "square"
			? actionAvailableWidth
		: fullBleedEmbed ? width : actionAvailableWidth
	readonly property real embedMediaHeight: normalizedEmbedAspect === "short"
		? embedMediaWidth * 16 / 9
		: normalizedEmbedAspect === "square" ? embedMediaWidth
		: normalizedEmbedAspect === "twitch" ? embedMediaWidth * 3 / 4
		: normalizedEmbedAspect === "compact-audio"
			? Math.min(166, Math.max(128, embedMediaWidth * 0.29))
		: normalizedEmbedAspect === "audio"
			? Math.min(352, Math.max(220, embedMediaWidth * 0.61))
		: embedMediaWidth * 9 / 16
	readonly property real inlineControlsEstimate: Theme.controlHeight + Theme.space2 * 2
	readonly property string inlineMediaAspect: hasEmbedPreview ? normalizedEmbedAspect
		: currentMediaKind === "audio" ? "compact-audio" : "wide"
	readonly property real directInlineMediaHeight: currentMediaKind === "audio"
		? Math.min(166, Math.max(128, actionAvailableWidth * 0.29)) : width * 9 / 16
	readonly property real inlineMediaViewportHeight: hasEmbedPreview ? embedMediaHeight
		: directInlineMediaHeight
	readonly property real inlineMediaStageWidth: hasEmbedPreview ? embedMediaWidth
		: currentMediaKind === "audio" ? actionAvailableWidth : width
	readonly property real embedPanelHeight: !inlineMediaStageVisible ? 0
		: inlinePlaybackActive
			? Math.max(inlineMediaViewportHeight + inlineControlsEstimate,
				inlineMediaLoader.item ? inlineMediaLoader.item.implicitHeight : 0)
			: inlineMediaViewportHeight
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
		&& !directInlinePlaybackActive && (hasExternalMedia || hasExternalImage)
	readonly property string originalProviderUrl: safeExternalUrl(preview ? preview.url : "")
	readonly property bool hasPrimaryOpenAction: !inlineMediaStageVisible && !hasPrimaryDirectAction
		&& originalProviderUrl.length > 0
	readonly property bool hasFooterOriginalOpenAction: hasEmbedPreview
		&& originalProviderUrl.length > 0
	readonly property bool hasSecondaryOriginalOpenAction: (hasPrimaryDirectAction || hasDirectMedia)
		&& originalProviderUrl.length > 0
	readonly property bool hasSizeActions: hasEmbedPreview || hasDirectMedia
	readonly property bool hasPopoutAction: hasEmbedPreview || hasDirectMedia
	readonly property bool hasOverflowActions: hasPopoutAction || canExpand
		|| hasSecondaryOriginalOpenAction || hasSizeActions
	readonly property bool inlinePlaybackActive: !!mediaSessionController
		&& mediaSessionController.active && !mediaSessionController.detached
		&& String(mediaSessionController.sessionId || "") === mediaSessionId
	readonly property bool directInlinePlaybackActive: inlinePlaybackActive && !hasEmbedPreview
		&& hasDirectMedia && String(mediaSessionController ? mediaSessionController.provider || "" : "") === "direct"
	readonly property bool inlineMediaStageVisible: hasEmbedPreview || hasDirectMedia
	readonly property bool fullBleedMediaStage: (hasDirectMedia && currentMediaKind !== "audio") || fullBleedEmbed
	// The centered short/square player keeps the ordinary card inset, while
	// wide providers intentionally meet the top edge. Keep these values in one
	// place so the delegate's implicit height always includes the same insets
	// that its anchored content uses.
	readonly property int contentTopInset: inlineMediaStageVisible && fullBleedMediaStage
		? 0 : Theme.space4
	readonly property int contentBottomInset: Theme.space4
	readonly property bool genericHeaderUsesThumbnailGeometry: imageSource.length > 0
	readonly property int genericHeaderVisualHeight: compact ? 56 : 64
	readonly property int genericHeaderVisualWidth: compact ? 56
		: genericHeaderUsesThumbnailGeometry ? 92 : 64
	readonly property bool inlinePlayerComponentFailed: inlineMediaLoader.status === Loader.Error
		&& inlineMediaLoader.active

    signal externalOpenRequested(string url)
    signal imageOpenRequested(string source, string title)
    signal imageRefreshRequested()
    signal directMediaRequested(string url, string mime, string audioUrl, string audioMime, string title)
	signal inlinePlayRequested(string url, string provider)
	signal popoutPlayRequested(string url, string provider)
	signal popoutDirectMediaRequested(string url, string mime, string audioUrl, string audioMime, string title)
	signal sizePresetRequested(string preset)
    signal watchTogetherRequested(string url, string provider, string title)

	implicitHeight: content.implicitHeight + contentTopInset + contentBottomInset
	radius: Theme.innerRadius
	color: effectiveHovered ? Theme.previewCardHover : Theme.previewCardBackground
	border.color: root.previewState === "error" ? root.withAlpha(Theme.danger, 0.65)
		: root.mediaRequiresReveal ? root.withAlpha(Theme.warning, 0.65)
		: effectiveHovered ? root.withAlpha(providerDetails.providerAccent, 0.38) : Theme.previewCardBorder
	border.width: 1
	Behavior on color { ColorAnimation { duration: root.animationDuration } }
	Behavior on border.color { ColorAnimation { duration: root.animationDuration } }
	Accessible.role: Accessible.Grouping
	Accessible.name: displayTitle + (metadataLine.length > 0 ? ": " + metadataLine : "")
	Accessible.description: previewAccessibilityDescription()

	onMediaRuntimeErrorChanged: focusInlineRuntimeRetry()
    onPreviewIdentityChanged: resetForReuse()
    onPreviewChanged: {
		// Required-property assignment can notify before the dependent mediaItems
		// binding has completed its first evaluation in a newly reused delegate.
		const itemCount = mediaItems && mediaItems.length !== undefined ? mediaItems.length : 0
		selectedMediaIndex = Math.max(0, Math.min(selectedMediaIndex, Math.max(0, itemCount - 1)))
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
		// Do not place an external-navigation hit target underneath a native media
		// card. Direct media owns the primary Play action and keeps the original
		// source in overflow, so a click can never mean two different things.
		enabled: root.hasPrimaryOpenAction
		visible: enabled
		hoverEnabled: true
		cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
		onClicked: root.externalOpenRequested(root.safeExternalUrl(root.preview.url))
	}

	ButtonGroup {
		id: previewSizeGroup
		exclusive: true
	}

	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		visible: !root.hasEmbedPreview || root.previewState === "error" || root.mediaRequiresReveal
		width: 2
		radius: root.radius
		color: root.previewState === "error" ? Theme.danger
			: root.mediaRequiresReveal ? Theme.warning
			: root.withAlpha(providerDetails.providerAccent, 0.72)
	}

	function previewAccessibilityDescription() {
		if (previewState === "loading")
			return qsTr("Preview loading")
		if (previewState === "error")
			return [qsTr("Preview unavailable"), errorDescription]
				.filter(function(value) { return value.length > 0 }).join(". ")
		if (mediaRequiresReveal) {
			return [qsTr("Preview media hidden"), contentWarning, sanitizedDescription,
				qsTr("Reveal the media to view it")]
				.filter(function(value) { return value.length > 0 }).join(". ")
		}
		return sanitizedDescription
	}

	function resetForReuse() {
        userExpanded = false
		sizePresetOverride = ""
        selectedMediaIndex = 0
        imageRefreshQueued = false
		sensitiveMediaRevealed = false
		restoreInlinePlaybackFocus = false
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = ""
		inlineFocusSessionId = ""
		inlineFocusRestoreGeneration += 1
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

	function normalizedInstagramEmbedMediaKind() {
		if (normalizedEmbedProvider !== "instagram")
			return ""
		const urlMatch = safeEmbedUrl.match(
			/^https:\/\/(?:www\.)?(?:instagram\.com|instagr\.am)\/(p|reel|reels|tv)\//i)
		if (urlMatch) {
			const pathKind = String(urlMatch[1] || "").toLowerCase()
			return pathKind === "p" ? "post" : pathKind === "reels" ? "reel" : pathKind
		}
		const metadataKind = safeText(preview && preview.metadata
			? preview.metadata.instagramMediaKind : "", 32).toLowerCase()
		if (metadataKind === "p")
			return "post"
		if (metadataKind === "reels")
			return "reel"
		return metadataKind
	}

	function isSupportedYouTubeSharedPlaybackUrl(value) {
		return /^https:\/\/(?:www\.)?(?:youtube\.com|youtube-nocookie\.com)\/embed\/[A-Za-z0-9_-]{3,128}(?:[\/?#]|$)/i
			.test(String(value || ""))
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
			const mediaKind = normalizedInstagramEmbedMediaKind()
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

	function normalizeSizePreset(value) {
		const token = safeText(value, 32).toLowerCase()
		if (token === "compact" || token === "small")
			return "compact"
		if (token === "large" || token === "expanded")
			return "large"
		return "default"
	}

	function setSizePreset(preset) {
		const normalized = normalizeSizePreset(preset)
		userExpanded = false
		sizePresetOverride = normalized
		sizePresetRequested(normalized)
	}

	function displayProviderLabel(value) {
		const text = safeText(value, 128)
		return ProviderPresentation.displayName(text) || text
	}

	function inferGenericSocialProviderToken() {
		if (!preview)
			return ""
		const metadata = preview.metadata || ({})
		const explicit = firstSafeText([
			metadata.previewProvider, metadata.provider, metadata.providerName,
			preview.providerName, preview.provider, preview.embedKind, preview.host
		], 128)
		const explicitIdentity = ProviderPresentation.resolve(explicit)
		if (["bluesky", "mastodon", "reddit"].indexOf(explicitIdentity.token) >= 0)
			return explicitIdentity.token

		const descriptionIdentity = ProviderPresentation.resolve(sanitizedDescription)
		if (["bluesky", "mastodon", "reddit"].indexOf(descriptionIdentity.token) >= 0)
			return descriptionIdentity.token

		const url = safeText(preview.url, 2048).toLowerCase()
		const match = /^(?:https?:\/\/)?([^\/?#]+)/.exec(url)
		const host = match ? match[1].replace(/:\d+$/, "").replace(/^www\./, "") : ""
		if (host === "bsky.app")
			return "bluesky"
		if (host === "reddit.com" || host === "old.reddit.com" || host === "redd.it"
				|| host === "v.redd.it")
			return "reddit"
		return ""
	}

	function equivalentPreviewText(left, right) {
		function normalized(value) {
			return safeText(value, 512).toLowerCase()
				.replace(/^https?:\/\//, "").replace(/^www\./, "")
				.replace(/[\s/]+$/g, "").replace(/\s+/g, " ")
		}
		const first = normalized(left)
		const second = normalized(right)
		return first.length > 0 && first === second
	}

	function fallbackEmbedTitle(provider) {
		switch (safeText(provider, 64).toLowerCase()) {
		case "youtube": return qsTr("YouTube video")
		case "twitch": return qsTr("Twitch stream")
		case "vimeo": return qsTr("Vimeo video")
		case "dailymotion": return qsTr("Dailymotion video")
		case "streamable": return qsTr("Streamable video")
		case "spotify": return qsTr("Spotify audio")
		case "soundcloud": return qsTr("SoundCloud audio")
		default:
			const label = displayProviderLabel(provider)
			return label.length > 0 ? qsTr("%1 media").arg(label) : qsTr("Embedded media")
		}
	}

	function requestInlinePlayback() {
		if (!hasEmbedPreview && hasDirectMedia) {
			requestCurrentMedia()
			return
		}
		inlinePlayRequested(safeEmbedUrl, safeEmbedProvider)
	}

	function requestInlinePlaybackWithFocus() {
		prepareInlinePlaybackFocus()
		requestInlinePlayback()
	}

	function prepareInlinePlaybackFocus() {
		restoreInlinePlaybackFocus = true
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = previewIdentity
		inlineFocusSessionId = String(mediaSessionId || "")
	}

	function externalMediaFallbackUrl() {
		const candidates = [ preview ? preview.url : "",
			mediaSessionController ? mediaSessionController.url : "" ]
		for (let index = 0; index < candidates.length; ++index) {
			const candidate = safeExternalUrl(candidates[index])
			if (/^https:\/\//i.test(candidate))
				return candidate
		}
		return ""
	}

	function reportInlinePlayerComponentFailure() {
		if (!inlinePlaybackActive || !mediaSessionController)
			return
		const message = qsTr("The inline media player is unavailable. Open this provider in your browser instead.")
		if (typeof mediaSessionController.reportTypedError === "function")
			mediaSessionController.reportTypedError("renderer-component-unavailable", message)
		else if (typeof mediaSessionController.reportError === "function")
			mediaSessionController.reportError(message)
		Qt.callLater(function() {
			if (inlinePlayerComponentFailed && inlineComponentRetryButton.visible)
				inlineComponentRetryButton.forceActiveFocus()
		})
	}

	function retryInlinePlayerComponent() {
		if (!mediaSessionController)
			return
		if (typeof mediaSessionController.retry === "function")
			mediaSessionController.retry()
		inlineMediaLoader.source = ""
		Qt.callLater(function() {
			if (inlineMediaLoader.active)
				inlineMediaLoader.updateSource()
		})
	}

	function retryInlineMediaRuntime() {
		if (!mediaRuntimeCanRetry)
			return false
		mediaProfileFactory.retryRuntime()
		if (mediaSessionController && typeof mediaSessionController.retry === "function")
			mediaSessionController.retry()
		return true
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

	function focusInlineRuntimeRetry() {
		if (!mediaRuntimeCanRetry || !inlineRuntimeLoadingSurface.visible
				|| !inlineFocusRequestMatchesCurrentSession())
			return
		Qt.callLater(function() {
			if (!root.mediaRuntimeCanRetry || !inlineRuntimeLoadingSurface.visible
					|| !inlineRuntimeRetryButton.visible
					|| !root.inlineFocusRequestMatchesCurrentSession())
				return
			const activeItem = root.Window ? root.Window.activeFocusItem : null
			// Runtime preparation is asynchronous. Do not pull focus back from a
			// different product surface when the user has moved on in the meantime.
			if (activeItem && !root.itemIsWithin(activeItem, root))
				return
			inlineRuntimeRetryButton.forceActiveFocus(Qt.OtherFocusReason)
			root.retainInlinePlaybackFocusForRestore(true)
		})
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
		inlineFocusEligibleForRestore = inlineFocusEligibleForRestore
			|| (inlineFocusRequestMatchesCurrentSession()
			&& ((!!inlineMediaLoader.item && itemIsWithin(activeFocusItem, inlineMediaLoader.item))
				|| itemIsWithin(activeFocusItem, inlineRuntimeLoadingSurface)
				|| itemIsWithin(activeFocusItem, inlineComponentFailure)))
	}

	function retainInlinePlaybackFocusForRestore(focused) {
		if (focused && inlineFocusRequestMatchesCurrentSession())
			inlineFocusEligibleForRestore = true
	}

	function restoreInlinePlaybackTriggerFocus(generation, attempt) {
		if (generation !== inlineFocusRestoreGeneration || inlinePlaybackActive)
			return
		if (!inlineFocusEligibleForRestore || !inlineFocusRequestMatchesCurrentSession()) {
			clearInlinePlaybackFocusRequest()
			return
		}
		const target = hasEmbedPreview ? previewPlayButton : previewDirectMediaButton
		if ((!inlineMediaLoader.active || inlineMediaLoader.status === Loader.Error)
				&& target.visible && target.enabled)
			target.forceActiveFocus()
		// Loader teardown can clear focus one event-loop turn after its active
		// binding changes. Confirm the handoff on a settled turn.
		if (target.activeFocus && attempt > 0) {
			clearInlinePlaybackFocusRequest()
			return
		}
		if (attempt >= 4) {
			clearInlinePlaybackFocusRequest()
			return
		}
		Qt.callLater(function() {
			root.restoreInlinePlaybackTriggerFocus(generation, attempt + 1)
		})
	}

	function scheduleInlinePlaybackFocusRestore() {
		const generation = ++inlineFocusRestoreGeneration
		Qt.callLater(function() {
			root.restoreInlinePlaybackTriggerFocus(generation, 0)
		})
	}

	function clearInlinePlaybackFocusRequest() {
		restoreInlinePlaybackFocus = false
		inlineFocusEligibleForRestore = false
		inlineFocusPreviewIdentity = ""
		inlineFocusSessionId = ""
	}

	function preserveInlinePlaybackWhenHidden() {
		if (!mediaSessionController || !mediaSessionController.active
				|| mediaSessionController.detached
				|| String(mediaSessionController.sessionId || "") !== mediaSessionId)
			return false
		// The visual matrix replaces the deterministic chat model before its
		// controller resets and reopens the next media state. Let that controller
		// own the fixture transition; closing here races the replacement delegate's
		// openInline() call after long multi-surface runs. Production cards never
		// carry a fixture mode and retain the lifecycle behavior below.
		if (String(visualMediaFixtureMode || "").length > 0)
			return false
		// A virtualized chat delegate must never turn a local lifecycle event into
		// a room-wide transport command. Keep an active host renderer alive by
		// moving it to the existing detached surface; guests and local-only media
		// may safely suppress their card-local renderer without affecting peers.
		if (mediaSessionController.sharedHost
				&& typeof mediaSessionController.detach === "function") {
			mediaSessionController.detach()
			return true
		}
		if (typeof mediaSessionController.closePlayer === "function")
			mediaSessionController.closePlayer()
		else if (typeof mediaSessionController.close === "function")
			mediaSessionController.close()
		return true
	}

	onRenderActiveChanged: {
		if (!renderActive)
			preserveInlinePlaybackWhenHidden()
	}
	onInlinePlaybackActiveChanged: {
		if (inlinePlaybackActive) {
			inlineFocusRestoreGeneration += 1
			return
		}
		if (!restoreInlinePlaybackFocus)
			return
		scheduleInlinePlaybackFocusRestore()
	}
	onVisibleChanged: {
		if (!visible)
			preserveInlinePlaybackWhenHidden()
	}
	Component.onDestruction: preserveInlinePlaybackWhenHidden()

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

	function requestCurrentMediaWithFocus() {
		if (hasDirectMedia)
			prepareInlinePlaybackFocus()
		requestCurrentMedia()
	}

	function requestCurrentDirectMediaPopout() {
		if (!hasDirectMedia)
			return
		const pairedAudioUrl = currentMediaUrl === safeDirectMediaUrl(preview.mediaUrl || "", currentMediaKind)
			? safeDirectMediaUrl(preview.mediaAudioUrl || "", "audio") : ""
		const pairedAudioMime = pairedAudioUrl.length > 0 ? safeText(preview.mediaAudioMime, 128) : ""
		popoutDirectMediaRequested(currentMediaUrl, String(currentMedia.mime || preview.mediaMime || ""),
			pairedAudioUrl, pairedAudioMime, safeText(currentMedia.title || displayTitle, 512))
	}

    ColumnLayout {
        id: content
		objectName: "previewContent"
		// This layout is narrower than a full-bleed media stage. Keep anonymous
		// layout wrappers out of the accessibility hierarchy so the stage and its
		// controls are parented by the full card bounds instead of a 16 px inset.
		Accessible.ignored: true
		anchors.fill: parent
		anchors.leftMargin: Theme.space4
		anchors.rightMargin: Theme.space4
		anchors.bottomMargin: root.contentBottomInset
		anchors.topMargin: root.contentTopInset
		spacing: Theme.space3
		z: 1

		Item {
			id: embedMediaSlot
			objectName: "previewEmbedMediaSlot"
			Accessible.ignored: true
			Layout.fillWidth: true
			Layout.preferredHeight: root.inlineMediaStageVisible ? root.embedPanelHeight : 0
			Layout.minimumHeight: Layout.preferredHeight
			visible: root.inlineMediaStageVisible

			Rectangle {
			id: embedMediaPanel
			objectName: "previewEmbedMediaPanel"
			anchors.horizontalCenter: parent.horizontalCenter
			width: root.inlineMediaStageWidth
			// Keep the media viewport and its transport row available immediately
			// when playback activates. ColumnLayout applies its preferred height on
			// the next polish pass, which otherwise leaves the controls clipped for
			// a frame (and longer in a height-constrained delegate).
			height: root.embedPanelHeight
			visible: embedMediaSlot.visible
			radius: Theme.innerRadius
			color: Theme.embedSurface
			gradient: Gradient {
				GradientStop {
					position: 0.0
					color: root.withAlpha(providerDetails.providerAccent, 0.14)
				}
				GradientStop {
					position: 1.0
					color: Theme.embedSurface
				}
			}
			border.color: root.inlinePlaybackActive ? Theme.previewCardBorder
				: root.withAlpha(providerDetails.providerAccent, 0.28)
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
				sourceSize: Qt.size(Math.min(1280, root.inlineMediaStageWidth * Screen.devicePixelRatio),
					Math.min(1280, root.inlineMediaViewportHeight * Screen.devicePixelRatio))
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

			Rectangle {
				objectName: "previewTwitchPosterScrim"
				anchors.fill: parent
				visible: providerDetails.variant === "twitch" && !root.inlinePlaybackActive
				color: "transparent"
				gradient: Gradient {
					orientation: Gradient.Vertical
					GradientStop {
						objectName: "previewTwitchPosterScrimTop"
						position: 0.42
						color: root.withAlpha(Theme.mediaCanvas, 0)
					}
					GradientStop {
						objectName: "previewTwitchPosterScrimMiddle"
						position: 0.72
						color: root.withAlpha(Theme.mediaCanvas, 0.84)
					}
					GradientStop {
						objectName: "previewTwitchPosterScrimBottom"
						position: 1.0
						color: root.withAlpha(Theme.mediaCanvas, 0.96)
					}
				}
				Accessible.ignored: true
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

			ProviderIdentityBadge {
				objectName: "previewEmbedProviderBadge"
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.margins: Theme.space2
				z: 2
				visible: root.providerLabel.length > 0 && !root.inlinePlaybackActive
					&& !(providerDetails.genericSocialPostPresentation && providerDetails.ownsHeader)
				width: Math.min(parent.width - Theme.space4, implicitWidth)
				height: implicitHeight
				providerToken: providerDetails.providerToken
				badgeText: root.providerLabel
				presentation: "overlay"
				accent: providerDetails.providerAccent
				foreground: Theme.contrastText(providerDetails.providerAccent)
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
					Accessible.ignored: true
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
				Accessible.ignored: true
				Label {
					objectName: "previewTwitchPosterTitle"
					Layout.fillWidth: true
					text: root.displayTitle.length > 0 ? root.displayTitle : qsTr("Twitch")
					textFormat: Text.PlainText
					color: Theme.mediaOverlayTextStrong
					font.pixelSize: Theme.fontTitle
					font.bold: true
					elide: Text.ElideRight
					Accessible.ignored: true
				}
				Label {
					objectName: "previewTwitchPosterNote"
					Layout.fillWidth: true
					visible: text.length > 0
					text: root.safeText(root.preview && root.preview.metadata
						? (root.preview.metadata.twitchDisclaimer
							|| root.preview.metadata.twitchPlaybackNote || "") : "", 512)
					textFormat: Text.PlainText
					color: Theme.mediaOverlayTextMuted
					font.pixelSize: Theme.fontCaption
					font.bold: true
					wrapMode: Text.Wrap
					maximumLineCount: 2
					elide: Text.ElideRight
					Accessible.ignored: true
				}
			}

			ModernIcon {
				objectName: "previewEmptyPosterIcon"
				anchors.centerIn: parent
				visible: !root.inlinePlaybackActive && !root.mediaRequiresReveal
					&& root.previewState !== "loading" && embedPoster.status !== Image.Ready
				name: root.inlineActionIconName
				size: Theme.space6
				color: Theme.mediaOverlayTextMuted
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
				text: root.inlineActionLabel
				implicitWidth: Math.max(54, Theme.controlHeight + Theme.space5)
				implicitHeight: implicitWidth
				contentItem: Item {
					Row {
						id: playButtonContent
						objectName: "previewPlayButtonContent"
						anchors.centerIn: parent
						spacing: Theme.space2
						ModernIcon {
							objectName: "previewPlayIcon"
							name: root.inlineActionIconName
							size: Theme.avatarMedium
							color: Theme.contrastText(providerDetails.providerAccent)
						}
						Label {
							objectName: "previewPlayText"
							visible: false
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
					radius: width / 2
					color: previewPlayButton.down
						? Qt.darker(providerDetails.providerAccent, 1.08)
						: previewPlayButton.hovered ? Qt.lighter(providerDetails.providerAccent, 1.08)
						: root.withAlpha(providerDetails.providerAccent, 0.94)
					border.color: previewPlayButton.activeFocus ? Theme.focus
						: root.withAlpha(Theme.mediaOverlayTextStrong, 0.46)
					border.width: previewPlayButton.activeFocus ? Theme.focusRingWidth : 1
					Behavior on color { ColorAnimation { duration: root.animationDuration } }
				}
				Accessible.name: root.inlineActionAccessibilityName
				Accessible.description: root.inlineActionAccessibilityDescription
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
					&& root.mediaRuntimeReady
				asynchronous: true
				function updateSource() {
					if (active) {
						setSource(root.inlinePlayerComponentUrl, {
							"session": root.mediaSessionController,
							"aspect": root.inlineMediaAspect,
							"mediaProfileFactory": root.mediaProfileFactory,
							"visualFixtureMode": root.visualMediaFixtureMode
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
				onStatusChanged: if (status === Loader.Error && active)
					root.reportInlinePlayerComponentFailure()
				Component.onCompleted: updateSource()
			}

			Rectangle {
				id: inlineRuntimeLoadingSurface
				objectName: "previewInlineMediaRuntimeLoadingSurface"
				property string surfaceId: visible ? "mediaSession.inline" : ""
				property bool webSurfaceActive: false
				property bool rendererHealthy: false
				property bool documentReady: false
				property string rendererState: root.mediaRuntimeError.length > 0 ? "error" : "loading"
				anchors.fill: parent
				visible: root.inlinePlaybackActive && root.renderActive && !root.mediaRequiresReveal
					&& !root.mediaRuntimeReady
				z: 10
				color: Theme.mediaCanvas
				Accessible.role: Accessible.AlertMessage
				Accessible.name: root.mediaRuntimeError.length > 0
					? qsTr("Media player unavailable") : qsTr("Preparing media player")
				Accessible.description: root.mediaRuntimeError
				onVisibleChanged: if (visible && root.mediaRuntimeError.length > 0)
					root.focusInlineRuntimeRetry()

				Column {
					anchors.centerIn: parent
					width: Math.max(1, Math.min(parent.width - Theme.space5 * 2, 420))
					spacing: Theme.space3
					ModernBusyIndicator {
						anchors.horizontalCenter: parent.horizontalCenter
						running: inlineRuntimeLoadingSurface.visible && root.mediaRuntimeError.length === 0
						visible: running
						animated: root.animationsEnabled
					}
					Label {
						objectName: "previewInlineMediaRuntimeStatus"
						width: parent.width
						text: root.mediaRuntimeError.length > 0 ? root.mediaRuntimeError
							: qsTr("Preparing secure playback…")
						textFormat: Text.PlainText
						color: root.mediaRuntimeError.length > 0
							? Theme.danger : Theme.mediaOverlayTextMuted
						wrapMode: Text.Wrap
						horizontalAlignment: Text.AlignHCenter
					}
					ModernButton {
						id: inlineRuntimeRetryButton
						objectName: "previewInlineMediaRuntimeRetryButton"
						anchors.horizontalCenter: parent.horizontalCenter
						visible: root.mediaRuntimeCanRetry
						text: qsTr("Retry")
						tone: "accent"
						Accessible.name: qsTr("Retry media player setup")
						Accessible.description: root.mediaRuntimeError
						onClicked: root.retryInlineMediaRuntime()
						onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
					}
				}
			}

			Rectangle {
				id: inlineComponentFailure
				objectName: "previewInlineMediaComponentFailure"
				anchors.fill: parent
				visible: root.inlinePlayerComponentFailed
				z: 12
				color: Theme.withAlpha(Theme.mediaCanvas, 0.97)
				Accessible.role: Accessible.AlertMessage
				Accessible.name: qsTr("Media player unavailable")
				Accessible.description: root.mediaSessionController
					? String(root.mediaSessionController.error || "") : ""

				Column {
					anchors.centerIn: parent
					width: Math.max(1, Math.min(parent.width - Theme.space5 * 2, 460))
					spacing: Theme.space3
					ModernIcon {
						anchors.horizontalCenter: parent.horizontalCenter
						name: "warning"
						size: Theme.avatarMedium
						color: Theme.danger
					}
					Label {
						objectName: "previewInlineMediaFailureHeading"
						width: parent.width
						text: qsTr("Media player unavailable")
						textFormat: Text.PlainText
						color: Theme.mediaOverlayTextStrong
						font.pixelSize: Theme.fontTitle
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
					}
					Label {
						objectName: "previewInlineMediaFailureDetail"
						width: parent.width
						text: root.mediaSessionController
							? String(root.mediaSessionController.error || "") : ""
						textFormat: Text.PlainText
						color: Theme.mediaOverlayTextMuted
						wrapMode: Text.Wrap
						horizontalAlignment: Text.AlignHCenter
					}
					Grid {
						id: inlineFailureActions
						width: parent.width
						columns: inlineComponentFailure.width >= 520 ? 3 : 1
						spacing: Theme.space2
						ModernButton {
							id: inlineComponentRetryButton
							objectName: "previewInlineMediaComponentRetryButton"
							text: qsTr("Retry")
							tone: "accent"
							width: inlineFailureActions.columns === 1 ? inlineFailureActions.width
								: (inlineFailureActions.width - inlineFailureActions.spacing * 2) / 3
							onClicked: root.retryInlinePlayerComponent()
							onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
						}
						ModernButton {
							id: inlineComponentExternalButton
							objectName: "previewInlineMediaComponentExternalButton"
							visible: root.externalMediaFallbackUrl().length > 0
							text: qsTr("Open in browser")
							width: inlineFailureActions.columns === 1 ? inlineFailureActions.width
								: (inlineFailureActions.width - inlineFailureActions.spacing * 2) / 3
							onClicked: root.externalOpenRequested(root.externalMediaFallbackUrl())
							onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
						}
						ModernButton {
							id: inlineComponentCloseButton
							objectName: "previewInlineMediaComponentCloseButton"
							text: qsTr("Close")
							width: inlineFailureActions.columns === 1 ? inlineFailureActions.width
								: (inlineFailureActions.width - inlineFailureActions.spacing * 2) / 3
							onClicked: {
								root.retainInlinePlaybackFocusForRestore(activeFocus)
								if (root.mediaSessionController
										&& typeof root.mediaSessionController.closePlayer === "function")
									root.mediaSessionController.closePlayer()
								if (!root.inlinePlaybackActive && root.restoreInlinePlaybackFocus)
									root.scheduleInlinePlaybackFocusRestore()
							}
							onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
						}
					}
				}
			}
			}
		}

		RowLayout {
			id: genericHeader
			objectName: "previewGenericHeader"
			Layout.fillWidth: true
			visible: !providerDetails.ownsHeader || root.previewState !== "ready"
			spacing: Theme.space3

			Rectangle {
				id: compactPreviewVisual
				objectName: "previewCompactVisual"
				Layout.preferredWidth: root.genericHeaderVisualWidth
				Layout.preferredHeight: root.genericHeaderVisualHeight
				visible: !root.inlineMediaStageVisible && (root.imageSource.length > 0
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
				ProviderIdentityBadge {
					objectName: "previewProviderChip"
					Layout.preferredWidth: Math.min(implicitWidth,
						Math.max(1, parent.width))
					Layout.maximumWidth: Math.max(1, parent.width)
					visible: root.providerLabel.length > 0 && !root.hasEmbedPreview
						&& !providerDetails.ownsHeader
					providerToken: providerDetails.providerToken
					badgeText: root.providerLabel
					presentation: "inline"
					labelObjectName: "previewProviderLabel"
					accent: providerDetails.providerAccent
					foreground: providerDetails.providerForeground
				}
				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					visible: !providerDetails.ownsHeader
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
					visible: root.metadataLine.length > 0 && !providerDetails.ownsHeader
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
					lineHeight: 1.25
                    wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 12 : 3
					elide: root.expanded ? Text.ElideNone : Text.ElideRight
				}
			}
		}

		Rectangle {
			id: previewStateBadge
			objectName: "previewStateBadge"
			Layout.alignment: Qt.AlignLeft
			Layout.preferredWidth: previewStateBadgeLabel.implicitWidth + Theme.space2 * 2
			Layout.preferredHeight: visible ? Theme.space5 : 0
			visible: root.previewStateLabel.length > 0
			radius: height / 2
			color: root.withAlpha(root.previewStateColor, 0.14)
			border.color: root.withAlpha(root.previewStateColor, 0.54)
			border.width: 1
			Accessible.ignored: true

			Label {
				id: previewStateBadgeLabel
				objectName: "previewStateBadgeLabel"
				anchors.fill: parent
				anchors.leftMargin: Theme.space2
				anchors.rightMargin: Theme.space2
				text: root.previewStateLabel
				textFormat: Text.PlainText
				color: Theme.textStrong
				font.pixelSize: Theme.fontCaption
				font.bold: true
				font.capitalization: Font.AllUppercase
				font.letterSpacing: 0.6
				horizontalAlignment: Text.AlignHCenter
				verticalAlignment: Text.AlignVCenter
			}
		}

		Item {
			id: expandedMediaSlot
			Layout.fillWidth: true
			Layout.preferredHeight: !root.inlineMediaStageVisible && !root.inlinePlaybackActive && root.expanded && (root.imageSource.length > 0
									|| root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
									? Math.min(420, Math.max(180, root.width * 9 / 16)) : 0
			visible: Layout.preferredHeight > 0

			Rectangle {
				id: expandedMediaPanel
				objectName: "previewExpandedMediaPanel"
				anchors.left: parent.left
				anchors.leftMargin: -Theme.space4
				width: root.width
				height: parent.height
				visible: expandedMediaSlot.visible
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
							objectName: "previewSteamPlayLabel_" + index
							anchors.centerIn: parent
							text: qsTr("PLAY")
							textFormat: Text.PlainText
							color: Theme.mediaOverlayTextStrong
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
			previewImageSource: root.mediaRequiresReveal ? "" : root.imageSource
			expanded: root.expanded
			onExternalOpenRequested: (url) => root.externalOpenRequested(url)
		}

		RowLayout {
			id: previewActionFlow
			objectName: "previewActionFlow"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			spacing: Theme.space2
			visible: root.previewState !== "loading" || root.hasFooterOriginalOpenAction
				|| root.hasPrimaryOpenAction

			ModernButton {
				objectName: "previewEmbedOriginalButton"
				visible: root.hasFooterOriginalOpenAction
				Layout.maximumWidth: Math.max(1, root.actionAvailableWidth
					- (previewOverflowButton.visible ? previewOverflowButton.implicitWidth + Theme.space2 : 0))
				Layout.fillWidth: root.narrowLayout
				text: root.openLabel
				dense: true
				Accessible.role: Accessible.Link
				Accessible.name: root.openLabel + ": " + root.displayTitle
				Accessible.description: qsTr("Open the original provider page for more details")
				onClicked: root.externalOpenRequested(root.originalProviderUrl)
			}

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
			}
            ModernButton {
				id: previewDirectMediaButton
                objectName: "previewDirectMediaButton"
				visible: root.hasPrimaryDirectAction
				Layout.maximumWidth: Math.max(1, root.actionAvailableWidth
					- (previewOverflowButton.visible ? previewOverflowButton.implicitWidth + Theme.space2 : 0))
				Layout.fillWidth: root.narrowLayout
				text: root.hasExternalImage ? qsTr("Open image") : root.hasExternalMedia ? qsTr("Open media")
					: root.currentMediaKind === "audio" ? qsTr("Play audio in chat") : qsTr("Play video in chat")
				dense: true
				onClicked: root.requestCurrentMediaWithFocus()
            }
			Item {
				Layout.fillWidth: true
				Layout.preferredWidth: 1
			}
			ModernIconButton {
				id: previewOverflowButton
				objectName: "previewOverflowButton"
				visible: root.previewState !== "loading" && root.hasOverflowActions
				dense: true
				iconName: "more"
				Accessible.name: qsTr("Preview actions")
				Accessible.description: root.inlineActionUsesPlaybackSemantics
					? qsTr("Open secondary playback and preview actions")
					: qsTr("Open secondary preview actions")
				onClicked: previewOverflowMenu.open()

				ModernMenu {
					id: previewOverflowMenu
					objectName: "previewOverflowMenu"
					x: Math.min(0, previewOverflowButton.width - width)
					y: previewOverflowButton.height
					implicitWidth: 210

					MenuItem {
						objectName: "previewPopoutButton"
						visible: root.hasPopoutAction
						text: root.popoutActionLabel
						Accessible.description: root.popoutActionDescription
						onTriggered: {
							if (root.hasEmbedPreview)
								root.popoutPlayRequested(root.safeEmbedUrl, root.safeEmbedProvider)
							else
								root.requestCurrentDirectMediaPopout()
						}
					}
					MenuItem {
						objectName: "previewWatchTogetherButton"
						visible: root.sharedPlaybackSupported
						enabled: root.watchTogetherAvailable
						text: root.watchTogetherAvailable ? qsTr("Watch together") : qsTr("Session active")
						Accessible.description: root.watchTogetherAvailable
							? qsTr("Start a synchronized media session")
							: qsTr("End or leave the active media session first")
						onTriggered: root.watchTogetherRequested(root.safeEmbedUrl,
							root.safeEmbedProvider, root.displayTitle)
					}
					MenuItem {
						objectName: "previewOpenOriginalButton"
						visible: root.hasSecondaryOriginalOpenAction
						text: root.preview && root.safeText(root.preview.openLabel, 128).length > 0
							? root.openLabel : qsTr("Open original")
						Accessible.name: text + ": " + root.displayTitle
						Accessible.description: qsTr("Open this item on the original provider")
						onTriggered: root.externalOpenRequested(root.originalProviderUrl)
					}
					MenuSeparator {
						visible: root.hasSizeActions
					}
					MenuItem {
						objectName: "previewSizeCompactButton"
						visible: root.hasSizeActions
						text: qsTr("Compact card")
						checkable: true
						checked: root.effectiveSizePreset === "compact"
						ButtonGroup.group: previewSizeGroup
						Accessible.role: Accessible.RadioButton
						Accessible.name: qsTr("Compact media card")
						Accessible.checked: checked
						onTriggered: root.setSizePreset("compact")
					}
					MenuItem {
						objectName: "previewSizeStandardButton"
						visible: root.hasSizeActions
						text: qsTr("Standard card")
						checkable: true
						checked: root.effectiveSizePreset === "default"
						ButtonGroup.group: previewSizeGroup
						Accessible.role: Accessible.RadioButton
						Accessible.name: qsTr("Standard media card")
						Accessible.checked: checked
						onTriggered: root.setSizePreset("default")
					}
					MenuItem {
						objectName: "previewSizeLargeButton"
						visible: root.hasSizeActions
						text: qsTr("Large card")
						checkable: true
						checked: root.effectiveSizePreset === "large"
						ButtonGroup.group: previewSizeGroup
						Accessible.role: Accessible.RadioButton
						Accessible.name: qsTr("Large media card")
						Accessible.checked: checked
						onTriggered: root.setSizePreset("large")
					}
					MenuItem {
						objectName: "previewExpandButton"
						visible: root.canExpand && !root.hasSizeActions
						text: root.userExpanded ? qsTr("Less") : qsTr("More")
						Accessible.name: root.userExpanded ? qsTr("Collapse preview") : qsTr("Expand preview")
						onTriggered: root.userExpanded = !root.userExpanded
					}
				}
			}
        }
    }
}
