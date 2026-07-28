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
	readonly property bool inlineProviderLoadTimeoutRunning:
		inlineMediaLoader.item
			? Boolean(inlineMediaLoader.item.providerDocumentLoadTimeoutRunning) : false
	readonly property double inlineProviderLoadElapsedMs:
		inlineMediaLoader.item
			? Number(inlineMediaLoader.item.providerDocumentLoadElapsedMs || 0) : 0
	readonly property int inlineProviderLoadTimeoutMs:
		inlineMediaLoader.item
			? Number(inlineMediaLoader.item.providerDocumentLoadTimeoutMs || 0) : 0
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
	property int embedPosterRetryAttempt: 0
	property int embedPosterRetryNonce: 0
	property int embedPosterRecoveryIntervalMs: 2500
	property int embedPosterRetryLimit: 2
	property bool embedPosterFallbackActive: false
	property int embedPosterHydrationRetryCount: 0
	property int embedPosterHydrationRetryLimit: 1
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
	readonly property var embedDocument: preview && preview.document ? preview.document : ({})
	readonly property bool typedDocumentActive: Number(embedDocument.schemaVersion || 0) === 1
		&& embedDocument.commonPresentation === true
	readonly property var typedDocumentContent: typedDocumentActive && embedDocument.content
		? embedDocument.content : ({})
	readonly property var typedDocumentMedia: typedDocumentActive && embedDocument.media
		&& embedDocument.media.length !== undefined ? embedDocument.media : []
	readonly property var typedDocumentThread: typedDocumentActive && embedDocument.thread
		? embedDocument.thread : ({})
	readonly property var typedDocumentFacts: typedDocumentActive
		&& embedDocument.facts && embedDocument.facts.length !== undefined
		? embedDocument.facts : []
	readonly property var typedDocumentPlayback: typedDocumentActive
		&& embedDocument.playback ? embedDocument.playback : ({})
	readonly property string typedPlaybackMode:
		safeText(typedDocumentPlayback.mode, 32).toLowerCase()
	readonly property bool typedDocumentCanExpand: typedDocumentActive
		&& (typedDocumentFacts.length > 0
			|| (typedDocumentThread.items && typedDocumentThread.items.length > 0)
			|| String(typedDocumentContent.description || "").length > 240
			|| mediaItems.length > 1)
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
		: hasEmbedGeometry ? fallbackEmbedTitle(safeEmbedProvider)
		: previewHost.length > 0 ? previewHost : qsTr("Link preview")
	readonly property string metadataLine: rawMetadataLine.length > 0
		&& !equivalentPreviewText(rawMetadataLine, displayTitle) ? rawMetadataLine
		: previewHost.length > 0 && !equivalentPreviewText(previewHost, displayTitle) ? previewHost : ""
	readonly property string displayDescription: descriptionRepeatsVisibleSummary(sanitizedDescription)
		? "" : sanitizedDescription
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
	readonly property string contentBranch: safeText(preview && preview.metadata
		? preview.metadata.contentBranch : "", 64).toLowerCase()
	readonly property string mediaPresentation: safeText(preview && preview.metadata
		? preview.metadata.mediaPresentation : "", 64).toLowerCase()
	readonly property bool animatedImagePresentation: mediaPresentation === "animated-image"
	// Direct media is represented by typed media fields and never by a generic
	// sender-controlled WebEngine embed.
	readonly property bool hasEmbedPreview: safeEmbedUrl.length > 0 && safeEmbedProvider.length > 0
		&& normalizedEmbedProvider !== "direct"
	readonly property bool reserveEmbedGeometry: !!preview
		&& preview.reserveEmbedGeometry === true && safeEmbedProvider.length > 0
	readonly property bool hasEmbedGeometry: hasEmbedPreview || reserveEmbedGeometry
	readonly property string instagramEmbedMediaKind: normalizedInstagramEmbedMediaKind()
	readonly property bool instagramStaticPost: normalizedEmbedProvider === "instagram"
		&& instagramEmbedMediaKind === "post"
	readonly property bool providerPostPresentation: mediaPresentation === "provider-post-card"
		|| instagramStaticPost
	// Local rendering and synchronized room playback are separate capabilities.
	// A provider can be viewed or played in this card without implementing the
	// deterministic state contract required by Watch Together.
	readonly property bool localPlaybackSupported: hasDirectMedia || hasEmbedPreview
	readonly property bool sharedPlaybackSupported: hasEmbedPreview
		&& normalizedEmbedProvider === "youtube" && isSupportedYouTubeSharedPlaybackUrl(safeEmbedUrl)
	// Compatibility alias for callers and automation that already consume this
	// property. Its meaning is now deliberately limited to shared playback.
	readonly property bool watchTogetherSupported: sharedPlaybackSupported
	readonly property string openLabel: safeText(preview ? preview.openLabel : "", 128) || qsTr("Open")
	readonly property bool inlineActionUsesPlaybackSemantics: !instagramStaticPost
		&& !animatedImagePresentation && !providerPostPresentation
	readonly property string inlineActionLabel: inlineActionUsesPlaybackSemantics
		? qsTr("Play here") : qsTr("View here")
	readonly property string inlineActionIconName: inlineActionUsesPlaybackSemantics ? "play" : "eye"
	readonly property string inlineActionAccessibilityName: inlineActionUsesPlaybackSemantics
		? qsTr("Play %1 here").arg(displayTitle) : qsTr("View %1 here").arg(displayTitle)
	readonly property string inlineActionAccessibilityDescription: inlineActionUsesPlaybackSemantics
		? qsTr("Loads the provider player in this preview")
		: animatedImagePresentation ? qsTr("Loads the animation in this preview")
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
	readonly property bool typedDocumentUsesGallery: typedDocumentActive
		&& mediaItems.length > 0
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
	readonly property string effectiveEmbedPosterSource: embedPosterFallbackActive
		? "" : posterSourceWithRetry(embedPosterSource, embedPosterRetryNonce)
	readonly property bool fullBleedEmbed: hasEmbedGeometry
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
	readonly property bool inlineNativeControlsExpected: inlinePlaybackActive
		&& !!mediaSessionController && Boolean(mediaSessionController.playbackControllable)
	readonly property real inlineControlsEstimate: inlineNativeControlsExpected
		? Theme.controlHeight + Theme.space2 * 2 : 0
	readonly property string inlineMediaAspect: hasEmbedPreview ? normalizedEmbedAspect
		: currentMediaKind === "audio" ? "compact-audio" : "wide"
	readonly property real directInlineMediaHeight: currentMediaKind === "audio"
		? Math.min(166, Math.max(128, actionAvailableWidth * 0.29)) : width * 9 / 16
	readonly property real typedNativeMediaHeight: expanded
		? Math.min(420, Math.max(240, actionAvailableWidth * 9 / 16))
		: Math.min(320, Math.max(180, actionAvailableWidth * 9 / 16))
	readonly property real inlineMediaViewportHeight: typedDocumentUsesGallery
		? (hasEmbedGeometry ? embedMediaHeight : typedNativeMediaHeight)
		: hasEmbedGeometry ? embedMediaHeight : directInlineMediaHeight
	readonly property real inlineMediaStageWidth: typedDocumentUsesGallery
		? (hasEmbedGeometry ? embedMediaWidth : actionAvailableWidth)
		: hasEmbedGeometry ? embedMediaWidth
		: currentMediaKind === "audio" ? actionAvailableWidth : width
	readonly property real reservedInlineControlsHeight:
		localPlaybackSupported && inlineActionUsesPlaybackSemantics && !providerPostPresentation
			? Theme.controlHeight + Theme.space2 * 2 : 0
	readonly property real stableInlinePanelHeight: inlineMediaViewportHeight
		+ reservedInlineControlsHeight
	readonly property real embedPanelHeight: !inlineMediaStageVisible ? 0
		: typedDocumentUsesGallery ? stableInlinePanelHeight
		: inlinePlaybackActive
			? providerPostPresentation ? inlineMediaViewportHeight
				: Math.max(inlineMediaViewportHeight + inlineControlsEstimate,
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
		&& displayDescription.length > 0
		&& !providerDetails.ownsDescription && !typedDocumentActive
	readonly property bool canExpand: !!preview && preview.previewSize !== "large"
		&& (providerDetails.canExpand || hasExpandedDescription || hasRevealableMedia
			|| hasEmbedPreview || mediaItems.length > 1 || typedDocumentCanExpand)
	readonly property bool hasDetails: canExpand
	readonly property bool hasPrimaryDirectAction: !hasEmbedPreview && !mediaRequiresReveal
		&& !directInlinePlaybackActive && (hasExternalMedia || hasExternalImage)
	readonly property string originalProviderUrl: safeExternalUrl(preview ? preview.url : "")
	readonly property bool hasPrimaryOpenAction: !typedDocumentActive
		&& !inlineMediaStageVisible && !hasPrimaryDirectAction
		&& originalProviderUrl.length > 0
	readonly property bool hasFooterOriginalOpenAction: !typedDocumentActive
		&& inlineMediaStageVisible && hasEmbedPreview
		&& originalProviderUrl.length > 0
	readonly property bool hasSecondaryOriginalOpenAction: !typedDocumentActive
		&& (hasPrimaryDirectAction || hasDirectMedia)
		&& originalProviderUrl.length > 0
	readonly property bool hasSizeActions: inlineMediaStageVisible && localPlaybackSupported
	readonly property bool hasPopoutAction: localPlaybackSupported && !animatedImagePresentation
		&& !providerPostPresentation
		&& (!mediaSessionController
			|| mediaSessionController.detachedPlaybackSupported !== false)
	readonly property bool hasOverflowActions: hasPopoutAction || canExpand
		|| hasSecondaryOriginalOpenAction || hasSizeActions
	readonly property bool inlinePlaybackActive: !!mediaSessionController
		&& mediaSessionController.active && !mediaSessionController.detached
		&& String(mediaSessionController.sessionId || "") === mediaSessionId
	readonly property bool inlineAdapterReady: inlinePlaybackActive
		&& inlineMediaLoader.status === Loader.Ready && !!inlineMediaLoader.item
		&& inlineMediaLoader.item.documentReady === true
	readonly property bool inlineAdapterPending: inlinePlaybackActive
		&& providerPostPresentation && !inlineAdapterReady
	readonly property bool directInlinePlaybackActive: inlinePlaybackActive && !hasEmbedPreview
		&& hasDirectMedia && String(mediaSessionController ? mediaSessionController.provider || "" : "") === "direct"
	readonly property bool embedPosterUnavailable: hasEmbedPreview && embedPosterFallbackActive
		&& !inlinePlaybackActive
	readonly property bool inlineMediaStageVisible:
		(reserveEmbedGeometry
			|| (hasEmbedPreview && localPlaybackSupported && !embedPosterUnavailable)
			|| hasDirectMedia)
		&& (!typedDocumentUsesGallery || inlinePlaybackActive)
	readonly property string inlinePresentationProvider: hasEmbedPreview
		? normalizedEmbedProvider
		: String(providerDetails.providerToken || providerLabel || "").trim()
	// Provider players are adapters inside the Mumble-owned card. Keep the
	// native identity/details surface mounted while an adapter activates so
	// transient player state cannot collapse the delegate, move the scroll
	// anchor, or make the card alternate between two ownership models.
	readonly property bool inlineProviderOwnsDetails: inlinePlaybackActive
		&& hasEmbedPreview
		&& ["spotify", "soundcloud"].indexOf(providerDetails.providerToken) >= 0
	readonly property bool providerOwnsDetails: inlineProviderOwnsDetails
		|| (hasEmbedPreview
			&& ["spotify", "soundcloud"].indexOf(providerDetails.providerToken) >= 0)
	readonly property bool fullBleedMediaStage: !typedDocumentUsesGallery
		&& ((hasDirectMedia && currentMediaKind !== "audio") || fullBleedEmbed)
	readonly property string genericProviderMark: buildGenericProviderMark()
	readonly property bool genericHeaderShowsIdentityMark: previewState === "ready"
		&& !inlineMediaStageVisible && imageSource.length === 0 && !hasExternalImage
		&& !hasDirectMedia && !hasExternalMedia && genericProviderMark.length > 0
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
	signal watchTogetherRequested(string url, string provider, string title,
		string presentationAspect)

	implicitHeight: content.implicitHeight + contentTopInset + contentBottomInset
	clip: true
	radius: Theme.innerRadius
	color: effectiveHovered
		? Theme.mixColors(Theme.previewCardHover, providerDetails.providerAccent, 0.055)
		: Theme.mixColors(Theme.previewCardBackground, providerDetails.providerAccent, 0.03)
	border.color: root.previewState === "error" ? root.withAlpha(Theme.danger, 0.65)
		: root.mediaRequiresReveal ? root.withAlpha(Theme.warning, 0.65)
		: effectiveHovered ? root.withAlpha(providerDetails.providerAccent, 0.46)
		: root.withAlpha(providerDetails.providerAccent, 0.24)
	border.width: 1
	Behavior on color { ColorAnimation { duration: root.animationDuration } }
	Behavior on border.color { ColorAnimation { duration: root.animationDuration } }
	Accessible.role: Accessible.Grouping
	Accessible.name: displayTitle + (metadataLine.length > 0 ? ": " + metadataLine : "")
	Accessible.description: previewAccessibilityDescription()

	onMediaRuntimeErrorChanged: focusInlineRuntimeRetry()
    onPreviewIdentityChanged: resetForReuse()
	onEmbedPosterSourceChanged: {
		embedPosterHydrationRetryCount = 0
		resetEmbedPosterRecovery()
	}
    onPreviewChanged: {
		// Required-property assignment can notify before the dependent mediaItems
		// binding has completed its first evaluation in a newly reused delegate.
		const itemCount = mediaItems && mediaItems.length !== undefined ? mediaItems.length : 0
		selectedMediaIndex = Math.max(0, Math.min(selectedMediaIndex, Math.max(0, itemCount - 1)))
		const retryHydratedPoster = embedPosterFallbackActive && renderActive
			&& embedPosterHydrationRetryCount < embedPosterHydrationRetryLimit
        imageRefreshQueued = false
		if (retryHydratedPoster) {
			embedPosterHydrationRetryCount += 1
			Qt.callLater(function() {
				if (root.embedPosterFallbackActive && root.renderActive)
					root.resetEmbedPosterRecovery()
			})
		}
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

	Timer {
		id: inlineFocusRestoreTimer
		interval: 0
		repeat: false
		property int generation: -1
		property int attempt: 0
		onTriggered: root.restoreInlinePlaybackTriggerFocus(generation, attempt)
	}

	Timer {
		id: embedPosterRecoveryTimer
		interval: root.embedPosterRecoveryIntervalMs
		repeat: false
		onTriggered: root.recoverStalledEmbedPoster()
	}

	Timer {
		id: inlineFocusHandoffTimer
		interval: 0
		repeat: false
		onTriggered: root.handOffInlinePlaybackFocus()
	}

	Timer {
		id: inlineFailureFocusTimer
		interval: 0
		repeat: false
		onTriggered: {
			if (root.inlinePlayerComponentFailed && inlineComponentRetryButton.visible)
				inlineComponentRetryButton.forceActiveFocus()
		}
	}

	Timer {
		id: inlineComponentRetryTimer
		interval: 0
		repeat: false
		onTriggered: {
			if (inlineMediaLoader.active)
				inlineMediaLoader.updateSource()
		}
	}

	Timer {
		id: inlineRuntimeRetryFocusTimer
		interval: 0
		repeat: false
		onTriggered: root.focusInlineRuntimeRetryNow()
	}

	Timer {
		id: currentMediaRequestTimer
		interval: 0
		repeat: false
		onTriggered: root.requestCurrentMedia()
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
		return displayDescription
	}

	function resetForReuse() {
        userExpanded = false
		sizePresetOverride = ""
        selectedMediaIndex = 0
        imageRefreshQueued = false
		embedPosterHydrationRetryCount = 0
		resetEmbedPosterRecovery()
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

	function resetEmbedPosterRecovery() {
		embedPosterRecoveryTimer.stop()
		embedPosterRetryAttempt = 0
		embedPosterRetryNonce = 0
		embedPosterFallbackActive = false
	}

	function posterSourceWithRetry(source, nonce) {
		const normalized = String(source || "")
		if (normalized.length === 0 || Number(nonce || 0) <= 0)
			return normalized
		return normalized + (normalized.indexOf("?") >= 0 ? "&" : "?")
			+ "qmlRetry=" + Number(nonce)
	}

	function recoverStalledEmbedPoster(statusOverride) {
		const status = statusOverride === undefined ? embedPoster.status : statusOverride
		if ((status !== Image.Loading && status !== Image.Error) || !renderActive
				|| embedPosterSource.length === 0 || mediaRequiresReveal)
			return false
		if (embedPosterRetryAttempt < embedPosterRetryLimit) {
			embedPosterRetryAttempt += 1
			embedPosterRetryNonce += 1
			return true
		}
		// The provider metadata remains real and actionable even when its image
		// transport stalls. Stop presenting an endless busy state and fall back
		// to the provider action surface until hydration supplies a new source.
		embedPosterFallbackActive = true
		requestImageRefresh()
		return false
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
		const metadataKind = safeText(preview && preview.metadata
			? preview.metadata.instagramMediaKind : "", 32).toLowerCase()
		const normalizedMetadataKind = metadataKind === "p" ? "post"
			: metadataKind === "reels" ? "reel" : metadataKind
		const urlMatch = safeEmbedUrl.match(
			/^https:\/\/(?:www\.)?(?:instagram\.com|instagr\.am)\/(p|reel|reels|tv)\//i)
		if (urlMatch) {
			const pathKind = String(urlMatch[1] || "").toLowerCase()
			if (pathKind === "reel" || pathKind === "reels" || pathKind === "tv")
				return pathKind === "reels" ? "reel" : pathKind
			// Historic Instagram videos and reels often use /p/. Prefer typed
			// metadata when it proves this is playable media; otherwise keep a
			// static post native instead of loading the whole provider document.
			if (normalizedMetadataKind === "reel" || normalizedMetadataKind === "tv")
				return normalizedMetadataKind
			return "post"
		}
		return normalizedMetadataKind
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

	function descriptionRepeatsVisibleSummary(value) {
		const description = safeText(value, 4096)
		if (description.length === 0)
			return false
		for (const summary of [displayTitle, metadataLine, providerLabel, previewHost]) {
			if (equivalentPreviewText(description, summary))
				return true
		}
		return false
	}

	function buildGenericProviderMark() {
		const catalogMark = safeText(providerDetails.providerMark, 12)
		if (catalogMark.length > 0)
			return catalogMark

		const identity = firstSafeText([previewHost, providerLabel], 256)
			.toLowerCase().replace(/^https?:\/\//, "").replace(/^www\./, "")
			.replace(/:\d+$/, "").replace(/[/?#].*$/, "")
		if (identity.length === 0)
			return ""

		if (identity.indexOf(".") < 0 && /\s/.test(identity)) {
			const words = identity.split(/\s+/).filter(function(word) { return word.length > 0 })
			return words.slice(0, 2).map(function(word) {
				return word.charAt(0)
			}).join("").toUpperCase()
		}

		const genericLabels = ["app", "blog", "docs", "help", "m", "mobile",
			"news", "open", "social", "store", "support", "www"]
		const labels = identity.split(".").filter(function(label) { return label.length > 0 })
		let label = labels.length > 0 ? labels[0] : identity
		for (let index = 0; index < labels.length; ++index) {
			if (genericLabels.indexOf(labels[index]) < 0) {
				label = labels[index]
				break
			}
		}
		const compactLabel = label.replace(/[^a-z0-9]/g, "")
		if (compactLabel.length === 0)
			return ""
		return (compactLabel.length <= 4 ? compactLabel : compactLabel.slice(0, 2)).toUpperCase()
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
		case "tiktok": return qsTr("TikTok video")
		case "instagram": return instagramStaticPost
			? qsTr("Instagram post") : qsTr("Instagram video")
		case "facebook": return qsTr("Facebook video")
		default:
			const label = displayProviderLabel(provider)
			return label.length > 0 ? qsTr("%1 media").arg(label) : qsTr("Embedded media")
		}
	}

	function requestInlinePlayback() {
		if (!localPlaybackSupported)
			return
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
		inlineFailureFocusTimer.restart()
	}

	function retryInlinePlayerComponent() {
		if (!mediaSessionController)
			return
		if (typeof mediaSessionController.retry === "function")
			mediaSessionController.retry()
		inlineMediaLoader.source = ""
		inlineComponentRetryTimer.restart()
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
		inlineRuntimeRetryFocusTimer.restart()
	}

	function focusInlineRuntimeRetryNow() {
		if (!mediaRuntimeCanRetry || !inlineRuntimeLoadingSurface.visible
				|| !inlineRuntimeRetryButton.visible
				|| !inlineFocusRequestMatchesCurrentSession())
			return
		const activeItem = root.Window ? root.Window.activeFocusItem : null
		// Runtime preparation is asynchronous. Do not pull focus back from a
		// different product surface when the user has moved on in the meantime.
		if (activeItem && !itemIsWithin(activeItem, root))
			return
		inlineRuntimeRetryButton.forceActiveFocus(Qt.OtherFocusReason)
		retainInlinePlaybackFocusForRestore(true)
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
		if (!inlineFocusRequestMatchesCurrentSession())
			return false
		if (inlineMediaLoader.item && inlineMediaLoader.item.focusInitialControl
				&& inlineMediaLoader.item.focusInitialControl())
			return true
		const target = previewInlineAnimationToggleButton.visible
			? previewInlineAnimationToggleButton : previewInlineCloseButton
		if (!target.visible || !target.enabled)
			return false
		target.forceActiveFocus(Qt.OtherFocusReason)
		retainInlinePlaybackFocusForRestore(target.activeFocus)
		return target.activeFocus
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
		inlineFocusRestoreTimer.generation = generation
		inlineFocusRestoreTimer.attempt = attempt + 1
		inlineFocusRestoreTimer.restart()
	}

	function scheduleInlinePlaybackFocusRestore() {
		const generation = ++inlineFocusRestoreGeneration
		inlineFocusRestoreTimer.generation = generation
		inlineFocusRestoreTimer.attempt = 0
		inlineFocusRestoreTimer.restart()
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
				&& mediaSessionController.detachedPlaybackSupported !== false
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
		if (!renderActive) {
			embedPosterRecoveryTimer.stop()
			preserveInlinePlaybackWhenHidden()
		} else {
			if (embedPosterFallbackActive) {
				embedPosterHydrationRetryCount = 0
				resetEmbedPosterRecovery()
			} else if (embedPoster.status === Image.Loading) {
				embedPosterRecoveryTimer.restart()
			}
		}
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
	Component.onDestruction: {
		inlineFocusRestoreGeneration += 1
		inlineFocusRestoreTimer.stop()
		inlineFocusHandoffTimer.stop()
		inlineFailureFocusTimer.stop()
		inlineComponentRetryTimer.stop()
		inlineRuntimeRetryFocusTimer.stop()
		currentMediaRequestTimer.stop()
		embedPosterRecoveryTimer.stop()
		preserveInlinePlaybackWhenHidden()
	}

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
        if (kind === "image" || kind === "gif" || kind === "animated-image"
				|| mime.indexOf("image/") === 0)
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
		const useTypedDocument = typedDocumentActive
		// C++ exposes these values as QVariantList. They are array-like in QML,
		// but Array.isArray() returns false for them and used to silently discard
		// every gallery item in the real client while JavaScript-only tests
		// continued to pass.
        const sourceItems = useTypedDocument ? typedDocumentMedia
			: preview.mediaItems && preview.mediaItems.length !== undefined
				? preview.mediaItems : []
        const result = []
        for (let index = 0; index < sourceItems.length && result.length < 16; ++index) {
            const item = sourceItems[index] || {}
            if (mediaKind(item) === "image") {
				const managedAnimated = !!item.managedAnimated
					|| String(item.kind || "").toLowerCase() === "animated-image"
                const renderSource = safeRenderImageSource(item.url || "", managedAnimated)
                const externalSource = safeExternalUrl(item.externalUrl
                                                       || (renderSource.length === 0 ? item.url : ""))
                if (renderSource.length > 0 || externalSource.length > 0) {
                    result.push({
                        "kind": "image",
						"mime": safeText(item.mime, 128),
                        "url": renderSource,
                        "externalUrl": externalSource,
                        "title": safeText(item.title, 512),
						"description": safeText(item.description, 2048),
                        "directPlayable": renderSource.length > 0,
						"managedAnimated": managedAnimated && /^file:\/\//i.test(renderSource),
						"contentBranch": safeText(item.contentBranch, 64),
						"presentation": safeText(item.presentation, 64),
                        "thumbnail": safeRenderImageSource(item.thumbnailUrl || item.thumbnail || ""),
                        "poster": safeRenderImageSource(item.posterUrl || item.poster || "")
                    })
                }
		} else {
				const kind = mediaKind(item)
				const playbackUrl = safeDirectMediaUrl(item.url || "", kind)
				const externalUrl = safeExternalUrl(item.externalUrl || item.url)
				const directPlayable = item.directPlayable !== false && playbackUrl.length > 0
				const targetUrl = directPlayable ? playbackUrl : externalUrl
				const thumbnail = safeRenderImageSource(item.thumbnailUrl || item.thumbnail || "")
				const poster = safeRenderImageSource(item.posterUrl || item.poster || "")
				if (kind.length > 0
						&& (targetUrl.length > 0 || thumbnail.length > 0 || poster.length > 0)) {
					result.push({
						"kind": kind,
						"mime": safeText(item.mime, 128),
						"url": targetUrl,
						"externalUrl": externalUrl,
						"title": safeText(item.title, 512),
						"description": safeText(item.description, 2048),
						"directPlayable": directPlayable,
						"managedAnimated": !!item.managedAnimated,
						"contentBranch": safeText(item.contentBranch, 64),
						"presentation": safeText(item.presentation, 64),
						"thumbnail": thumbnail,
						"poster": poster
					})
				}
            }
        }
		if (!useTypedDocument && result.length === 0 && (String(preview.mediaUrl || "").length > 0
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
		const pairedAudioUrl = !animatedImagePresentation
			&& currentMediaUrl === safeDirectMediaUrl(preview.mediaUrl || "", currentMediaKind)
			? safeDirectMediaUrl(preview.mediaAudioUrl || "", "audio") : ""
		const pairedAudioMime = pairedAudioUrl.length > 0 ? safeText(preview.mediaAudioMime, 128) : ""
		directMediaRequested(currentMediaUrl, String(currentMedia.mime || preview.mediaMime || ""),
							 pairedAudioUrl, pairedAudioMime,
							 safeText(currentMedia.title || displayTitle, 512))
    }

	function requestTypedMedia() {
		if (mediaRequiresReveal) {
			sensitiveMediaRevealed = true
			return
		}
		if (currentMediaKind === "image") {
			requestCurrentMedia()
			return
		}
		if (typedPlaybackMode === "provider" && hasEmbedPreview) {
			requestInlinePlaybackWithFocus()
			return
		}
		requestCurrentMediaWithFocus()
	}

	function requestCurrentMediaWithFocus() {
		if (hasDirectMedia)
			prepareInlinePlaybackFocus()
		requestCurrentMedia()
	}

	function requestCurrentDirectMediaPopout() {
		if (!hasDirectMedia || !hasPopoutAction)
			return
		const pairedAudioUrl = !animatedImagePresentation
			&& currentMediaUrl === safeDirectMediaUrl(preview.mediaUrl || "", currentMediaKind)
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

		EmbedSourceLink {
			objectName: "previewSourceLink"
			Layout.fillWidth: true
			sourceUrl: root.originalProviderUrl
			providerLabel: root.typedDocumentActive && root.embedDocument.provider
				? String(root.embedDocument.provider.label || root.providerLabel)
				: root.providerLabel
			accent: providerDetails.providerAccent
			onOpenRequested: function(url) { root.externalOpenRequested(url) }
		}

		ColumnLayout {
			id: embedMediaComposition
			objectName: "previewEmbedMediaComposition"
			Accessible.ignored: true
			Layout.fillWidth: true
			spacing: Theme.space2
			visible: root.inlineMediaStageVisible
				|| (root.typedDocumentActive && root.previewState === "ready"
					&& root.mediaItems.length > 0)

			Item {
				id: embedMediaSlot
				objectName: "previewEmbedMediaSlot"
				Accessible.ignored: true
				Layout.fillWidth: true
				Layout.preferredHeight: root.inlineMediaStageVisible ? root.embedPanelHeight : 0
				Layout.minimumHeight: Layout.preferredHeight
				Layout.maximumHeight: Layout.preferredHeight
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
				source: root.renderActive && (!root.inlinePlaybackActive || root.inlineAdapterPending)
					&& !root.mediaRequiresReveal
					? root.effectiveEmbedPosterSource : ""
				asynchronous: true
				cache: false
				sourceSize: Qt.size(Math.min(1280, root.inlineMediaStageWidth * Screen.devicePixelRatio),
					Math.min(1280, root.inlineMediaViewportHeight * Screen.devicePixelRatio))
				// Match the production preview treatment: the poster is a backdrop for
				// the provider action, including audio embeds. PreserveAspectFit left
				// square album art floating in a wide empty strip for Spotify.
				fillMode: Image.PreserveAspectCrop
				visible: status === Image.Ready
					&& (!root.inlinePlaybackActive || root.inlineAdapterPending)
				onStatusChanged: {
					if (status === Image.Loading && root.renderActive)
						embedPosterRecoveryTimer.restart()
					else
						embedPosterRecoveryTimer.stop()
					if (status === Image.Ready)
						root.embedPosterRetryAttempt = 0
					else if (status === Image.Error && root.embedPosterSource.length > 0)
						root.recoverStalledEmbedPoster(status)
				}
			}

			Rectangle {
				anchors.fill: parent
				visible: !root.inlinePlaybackActive || root.inlineAdapterPending
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
				running: !root.mediaRequiresReveal
					&& ((!root.inlinePlaybackActive
							&& (root.previewState === "loading" || embedPoster.status === Image.Loading))
						|| root.inlineAdapterPending)
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
				opacity: root.providerPostPresentation && !root.inlineAdapterReady ? 0 : 1
				enabled: opacity > 0
				Accessible.ignored: opacity <= 0
				asynchronous: true
				Behavior on opacity {
					NumberAnimation { duration: root.animationDuration }
				}
				function updateSource() {
					if (active) {
						setSource(root.inlinePlayerComponentUrl, {
							"session": root.mediaSessionController,
							"aspect": root.inlineMediaAspect,
							"presentationProvider": root.inlinePresentationProvider,
							"presentationMode": root.providerPostPresentation
								? "provider-post-card" : root.mediaPresentation,
							"animationAutoPlayEnabled": root.animationsEnabled,
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
					inlineFocusHandoffTimer.restart()
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

			EmbedMediaGallery {
				objectName: "previewDocumentMediaGallery"
				Layout.fillWidth: true
				visible: root.typedDocumentActive && root.previewState === "ready"
					&& root.mediaItems.length > 0
					&& (!root.inlinePlaybackActive || root.mediaItems.length > 1)
				mediaItems: root.mediaItems
				selectedIndex: root.selectedMediaIndex
				expanded: root.expanded
				renderActive: root.renderActive
				animationsEnabled: root.animationsEnabled
				mediaRequiresReveal: root.mediaRequiresReveal
				viewportVisible: !root.inlinePlaybackActive
				viewportPreferredWidth: root.hasEmbedPreview
					? root.embedMediaWidth : root.actionAvailableWidth
				viewportPreferredHeight: root.typedDocumentUsesGallery
					? root.stableInlinePanelHeight : 0
				accent: providerDetails.providerAccent
				accessibleTitle: root.displayTitle
				onSelectionRequested: function(index) { root.selectedMediaIndex = index }
				onMediaRequested: function(index) {
					root.selectedMediaIndex = index
					root.requestTypedMedia()
				}
				onRevealRequested: root.sensitiveMediaRevealed = true
			}
		}

		EmbedDocumentBody {
			objectName: "previewDocumentBody"
			Layout.fillWidth: true
			visible: root.typedDocumentActive && root.previewState === "ready"
			document: root.embedDocument
			accent: providerDetails.providerAccent
			expanded: root.expanded
			onExternalOpenRequested: function(url) { root.externalOpenRequested(url) }
		}

		RowLayout {
			id: genericHeader
			objectName: "previewGenericHeader"
			Layout.fillWidth: true
			visible: (!root.typedDocumentActive || root.previewState !== "ready")
				&& (!providerDetails.ownsHeader || root.previewState !== "ready")
			spacing: Theme.space3

			Rectangle {
				id: compactPreviewVisual
				objectName: "previewCompactVisual"
				Layout.preferredWidth: root.genericHeaderVisualWidth
				Layout.preferredHeight: root.genericHeaderVisualHeight
				visible: !root.inlineMediaStageVisible && (root.imageSource.length > 0
					|| root.previewState !== "ready" || root.hasExternalImage
					|| root.hasDirectMedia || root.hasExternalMedia
					|| root.genericHeaderShowsIdentityMark)
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
				ProviderIdentityBadge {
					objectName: "previewGenericProviderMark"
					anchors.centerIn: parent
					visible: root.genericHeaderShowsIdentityMark
					providerToken: providerDetails.providerToken
					badgeText: root.genericProviderMark
					presentation: "mark"
					markExtent: root.compact ? 40 : 44
					accent: providerDetails.providerAccent
					foreground: providerDetails.providerForeground
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
					text: root.displayDescription
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
			objectName: "previewExpandedMediaSlot"
			Layout.fillWidth: true
			Layout.preferredHeight: !root.typedDocumentActive && !providerDetails.ownsMediaGallery
				&& !root.inlineMediaStageVisible && !root.inlinePlaybackActive && root.expanded && (root.imageSource.length > 0
									|| root.hasDirectMedia || root.hasExternalMedia || root.hasExternalImage)
									? Math.min(420, Math.max(180, root.width * 9 / 16)) : 0
			Layout.minimumHeight: Layout.preferredHeight
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
                    : root.currentMediaKind === "audio" ? qsTr("Play audio")
					: root.animatedImagePresentation ? qsTr("View animation") : qsTr("Play video")
				Accessible.name: root.animatedImagePresentation
					? qsTr("View %1 animation here").arg(root.displayTitle)
					: root.playAccessibilityName
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

		ProviderDetails {
			id: providerDetails
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			height: visible ? implicitHeight : 0
			visible: !root.providerOwnsDetails && !root.typedDocumentActive
			metadata: root.providerMetadata
			previewKind: root.preview ? String(root.preview.kind || "") : ""
			providerHint: root.preview ? String(root.preview.embedKind || root.providerLabel || "") : ""
			previewTitle: root.displayTitle
			previewSubtitle: root.metadataLine
			previewDescription: root.sanitizedDescription
			previewImageSource: root.mediaRequiresReveal ? "" : root.imageSource
			expanded: root.expanded
			steamMediaItems: root.mediaItems
			steamMediaIndex: root.selectedMediaIndex
			onExternalOpenRequested: (url) => root.externalOpenRequested(url)
			onSteamMediaSelectionRequested: (index) => root.selectedMediaIndex = index
			onSteamMediaOpenRequested: (index) => {
				root.selectedMediaIndex = index
				currentMediaRequestTimer.restart()
			}
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

			ModernIconButton {
				id: previewInlineAnimationToggleButton
				objectName: "previewInlineAnimationToggleButton"
				visible: root.inlinePlaybackActive && root.animatedImagePresentation
				enabled: !!root.mediaSessionController
				dense: true
				iconName: root.mediaSessionController
					&& String(root.mediaSessionController.state || "") === "playing"
					? "pause" : "play"
				text: root.mediaSessionController
					&& String(root.mediaSessionController.state || "") === "playing"
					? qsTr("Pause animation") : qsTr("Resume animation")
				Accessible.description: qsTr("Pause or resume this silent looping animation")
				onClicked: {
					if (!root.mediaSessionController)
						return
					if (String(root.mediaSessionController.state || "") === "playing")
						root.mediaSessionController.pause()
					else
						root.mediaSessionController.play()
				}
				onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
			}

			ModernButton {
				id: previewInlineCloseButton
				objectName: "previewInlineCloseButton"
				visible: root.inlinePlaybackActive
				dense: true
				text: qsTr("Close player")
				Accessible.description: qsTr("Close inline media and return to the preview")
				onClicked: {
					root.retainInlinePlaybackFocusForRestore(activeFocus)
					if (root.mediaSessionController
							&& typeof root.mediaSessionController.closePlayer === "function")
						root.mediaSessionController.closePlayer()
					else if (root.mediaSessionController
							&& typeof root.mediaSessionController.close === "function")
						root.mediaSessionController.close()
					if (!root.inlinePlaybackActive && root.restoreInlinePlaybackFocus)
						root.scheduleInlinePlaybackFocusRestore()
				}
				onActiveFocusChanged: root.retainInlinePlaybackFocusForRestore(activeFocus)
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
					: root.currentMediaKind === "audio" ? qsTr("Play audio in chat")
					: root.animatedImagePresentation ? qsTr("View animation in chat") : qsTr("Play video in chat")
				dense: true
				Accessible.name: root.animatedImagePresentation
					? qsTr("View %1 animation in chat").arg(root.displayTitle)
					: root.playAccessibilityName
				Accessible.description: root.inlineActionAccessibilityDescription
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
							root.safeEmbedProvider, root.displayTitle, root.normalizedEmbedAspect)
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
