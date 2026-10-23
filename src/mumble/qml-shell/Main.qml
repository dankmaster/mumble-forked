import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels
import Mumble.Theme 1.0

ApplicationWindow {
	id: root
	onClosing: close => close.accepted = false
	onFrameSwapped: {
		timeline.frameSwappedSerial += 1
		timeline.observeScopePresentationFrame()
		timeline.observePrependSettlementFrame()
	}
	onActiveChanged: {
		if (active)
			timeline.markVisibleScopeReadIfEligible()
	}
    property var attachmentViewerPayload: null
	property var attachmentViewerHydrationModel: null
    property var pendingPreviewHydrationIds: ({})
	property var richPreviewSizePresets: ({})
    property bool pendingPreviewHydrationHighPriority: false
	// A cold scope can expose several cached provider previews at once. Keep their
	// dispatch paced until the complete initial queue drains, even if the hidden
	// presentation gate finishes before the last per-frame request.
	property bool coldPreviewHydrationPacingActive: false
    visible: false
    width: 1280
    height: 820
    minimumWidth: 420
    minimumHeight: 520
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
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.surfaceRaised
	palette.inactive.toolTipBase: Theme.surfaceRaised
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
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
	// Product dialogs expose their own accessible heading inside the scene. Keep
	// the native title tied to the server so an in-shell modal is not repeated in
	// the Windows title bar.
	title: clientSession.connected && clientSession.serverName.length > 0
		? qsTr("%1 — Mumble").arg(clientSession.serverName) : qsTr("Mumble")
    color: Theme.strip
	property real performanceChatScrollStartY: 0
	property real performanceChatScrollTargetY: 0
	property bool performanceChatScrollOwnershipActive: false
	property bool performanceChatScrollRunning: false
	property bool performanceChatScrollFullRangeToStart: false
	property int performanceChatScrollCompletedSteps: 0
	property int performanceChatScrollTargetSteps: 0
	property int performanceChatDelegateCreatedCount: 0
	property int performanceChatDelegatePooledCount: 0
	property int performanceChatDelegateReusedCount: 0
	property int performanceChatPreviewLoadedCount: 0
	property int performanceChatAttachmentLoadedCount: 0
	property var performanceChatScrollDiagnosticsBefore: ({})
	property var performanceChatScrollRetainedRichIds: ({})
	property int performanceChatScrollRetainedRichResourceDropCount: 0
	// Cumulative coarse-wheel telemetry is observational only. Keep it on the
	// persistent shell so automation can distinguish OS delivery from the bounded
	// handler's admitted and deliberately consumed overflow without resetting a
	// live gesture or exposing ownership state.
	property int timelineWheelInputObservedCount: 0
	property int timelineWheelObservedNotchUnits: 0
	property int timelineWheelObservedSignedNotchBalance: 0
	property int timelineWheelInputAdmittedNotchUnits: 0
	property int timelineWheelInputDroppedNotchUnits: 0
	property int timelineWheelInputBlockedCount: 0
	property int timelineWheelBlockedNotchUnits: 0
	property int timelineWheelBlockedSignedNotchBalance: 0
	property int timelineWheelBlockedPrecisionCount: 0
	property int timelineScrollbarReleaseWheelTakeoverCount: 0
	property real timelineWheelExpiredDistance: 0
	readonly property bool compactNavigation: width < 900
	readonly property bool narrowShell: width < 600
	readonly property string normalizedConnectionState: String(clientSession.connectionState || "").toLowerCase()
	readonly property string normalizedConnectionTone: String(clientSession.connectionTone || "").toLowerCase()
	readonly property bool connectionTransitionActive: normalizedConnectionState === "connecting"
		|| normalizedConnectionState === "retrying"
	readonly property bool connectionFailureActive: !clientSession.connected
		&& (normalizedConnectionTone === "danger" || normalizedConnectionTone === "error")
	readonly property int timelineHorizontalMargin: narrowShell ? Theme.space3
		: compactNavigation ? Theme.space5 : 28
	readonly property int timelineVerticalMargin: narrowShell ? Theme.space3 : Theme.space5
	readonly property int conversationLaneMaximumWidth: Theme.chatLaneMaximumWidth
	readonly property int activityLogTimeColumnWidth: Theme.densityMetric(58, 68, 76)
	readonly property int activityLogGutterWidth: Theme.densityMetric(12, 14, 16)
	// Drawer.visible can stay true while the popup item is resident. Position is
	// the reliable modal-state signal: zero is fully closed, one fully open.
	readonly property bool navigationModalActive: navigationDrawer.position > 0.001
	readonly property bool productDialogTransitionActive: productDialog.opened || productDialog.visible
	readonly property bool modalUiActive: (dialogState.open && dialogState.kind !== "settings")
		|| navigationModalActive
		|| mediaSessionWindowUnavailable
	// Detached product dialogs are native QQuickWindows. Windows owns their
	// modal accessibility relationship, so recursively suppressing the complete
	// main scene duplicates native work and stalls the GUI thread. Keep the deep
	// barrier only for modal surfaces hosted inside this window.
	readonly property bool backgroundAccessibilitySuppressed: navigationModalActive
		|| mediaSessionWindowUnavailable
	readonly property bool automationNavigationOpen: navigationModalActive
	readonly property real automationNavigationPosition: navigationDrawer.position
	readonly property var navigationRoomModel: roomModel
	readonly property var navigationParticipantModel: participantModel
	readonly property var navigationRailModel: navigationModel
	readonly property var navigationSelectionState: selectionState
	readonly property var navigationCommands: uiCommands
	readonly property var navigationSession: clientSession
	readonly property var settingsActionEntry: resolvedSettingsAction()
	readonly property bool settingsActionEnabled: settingsActionEntry.enabled === undefined
		|| !!settingsActionEntry.enabled
	readonly property var stonksTickerState: clientSession.stonks || ({})
	readonly property bool stonksShortcutEnabled: !!clientSession.connected
		&& stonksTickerState.supported === true
		&& stonksTickerState.accessSupported === true
		&& stonksTickerState.allowed === true
		&& stonksTickerState.profileShortcutVisible !== false
	readonly property bool stonksTickerEnabled: stonksTickerState.tickerBannerEnabled === true
	readonly property string stonksTickerPlacement: String(stonksTickerState.tickerPlacement || "bottom")
	readonly property string stonksTickerDirection: String(stonksTickerState.tickerDirection || "left")
	readonly property string stonksTickerSpeed: String(stonksTickerState.tickerSpeed || "normal")
	readonly property bool stonksRoomPanelActive: {
		if (stonksTickerState.automationHeaderVisible === true)
			return true
		const label = String(activeScope.label || "").trim().toLowerCase().replace(/^#/, "")
		if (label === "stonks")
			return true
		const channelId = Number(stonksTickerState.textChannelId || 0)
		if (channelId <= 0)
			return false
		const token = String(activeScope.scopeToken || "").trim().toLowerCase()
		return token === "text:" + channelId || token === "3:" + channelId
	}
	property string contextScopeToken: ""
	property string contextScopeKind: ""
	property var contextScopeActions: []
	property string contextParticipantId: ""
	property string contextParticipantRowKey: ""
	property var contextParticipantActions: []
	property string contextParticipantEntryKind: "user"
	property string contextParticipantScopeToken: ""
	property bool conversationSearchOpen: false
	property string quickReactionMessageId: ""
	property var quickReactionAnchorItem: null
	property var quickReactionActiveReactions: []
	property string automationMenuVariant: ""
	property var automationMenuSurface: null
	property var automationMenuFocusItem: null
	property bool visualFixtureOverrideActive: false
	property string visualFixtureSurfaceVariant: "none"
	// The first ChatMessageFrame type construction otherwise lands after the
	// connected window is already interactive and can consume an entire GUI
	// heartbeat. Retain one invisible geometry-only frame for the shell lifetime
	// so the first real row does not have to rebuild the platform accessibility
	// and binding metadata after the startup object has already been collected.
	Loader {
		id: chatMessageFrameWarmupLoader
		objectName: "chatMessageFrameWarmupLoader"
		active: true
		asynchronous: false
		visible: false
		width: 1
		height: 1
		sourceComponent: Component {
			ChatMessageFrame {
				stableId: "__shell_warmup__"
				startsGroup: true
				own: false
				systemMessage: false
				laneAvailableWidth: 1
				accessibilityPooled: true
				accessibilityViewportVisible: false
				accessibilityBarrierEnabled: false
				hoverEffectsEnabled: false
			}
		}
	}
	Component {
		id: composerTextCursorDelegate
		// Preserve real keyboard focus in visual fixtures while keeping the
		// screenshot frame deterministic across the platform cursor blink timer.
		Item {
			width: 1
			Rectangle {
				objectName: "composerTextCursorPaint"
				anchors.fill: parent
				visible: !root.visualFixtureOverrideActive
				color: Theme.textStrong
			}
		}
	}
	// Non-empty only while the deterministic Windows visual gate is presenting
	// a media surface. The value is forwarded to the isolated media components
	// so they can exercise loading/playback/error chrome without creating a
	// Chromium renderer or touching the network.
	property string visualMediaFixtureMode: ""
	property string motdSeenRequestSignature: ""
	property url mediaSessionWindowComponentUrl: Qt.resolvedUrl("MediaSessionWindow.qml")
	property Item mediaWindowFailureFocusReturnItem: null
	readonly property bool mediaSessionWindowRequested: mediaSession.detachedPlaybackSupported
		&& mediaSession.active && mediaSession.detached
	readonly property bool mediaRuntimeContractAvailable: !!mediaProfiles
		&& typeof mediaProfiles.runtimeReady !== "undefined"
	readonly property bool mediaRuntimeReady: !mediaRuntimeContractAvailable
		|| !!mediaProfiles.runtimeReady
	readonly property bool mediaRuntimePreparing: mediaRuntimeContractAvailable
		&& !!mediaProfiles.runtimePreparing
	readonly property string mediaRuntimeError: mediaRuntimeContractAvailable
		? String(mediaProfiles.runtimeError || "") : ""
	readonly property bool mediaSessionWindowComponentFailed:
		mediaSessionWindowLoader.active && mediaSessionWindowLoader.status === Loader.Error
	readonly property bool mediaSessionWindowRuntimeFailed: mediaSessionWindowRequested
		&& mediaRuntimeError.length > 0
	// Runtime preparation and runtime failures are presented inside the detached
	// media window. The shell-level modal is reserved for a QML component load
	// failure, where no detached product surface exists to host recovery.
	readonly property bool mediaSessionWindowUnavailable: mediaSessionWindowComponentFailed
	onMediaSessionWindowComponentUrlChanged: mediaSessionWindowLoader.updateSource()
	onMediaSessionWindowRequestedChanged: mediaSessionWindowLoader.syncRuntimePresentation()
	onMediaRuntimeReadyChanged: mediaSessionWindowLoader.syncRuntimePresentation()
	onMediaRuntimePreparingChanged: mediaSessionWindowLoader.syncRuntimePresentation()
	onMediaRuntimeErrorChanged: mediaSessionWindowLoader.syncRuntimePresentation()
	readonly property bool activeScopeHasScreenShare:
		String((activeScope.screenShare || {}).streamId || "").length > 0

	function maybeMarkMotdSeen() {
		const signature = String(clientSession.motdSignature || "").trim()
		if (root.visualFixtureOverrideActive || !clientSession.hasMotd
				|| clientSession.motdDismissed
				|| clientSession.motdChanged || signature.length === 0
				|| String(clientSession.motdLastSeenSignature || "").length > 0
				|| String(clientSession.motdDismissedSignature || "").length > 0
				|| root.motdSeenRequestSignature === signature)
			return

		root.motdSeenRequestSignature = signature
		Qt.callLater(function() {
			if (root.motdSeenRequestSignature !== signature
					|| String(clientSession.motdSignature || "").trim() !== signature
					|| !clientSession.hasMotd || clientSession.motdDismissed
					|| clientSession.motdChanged)
				return
			uiCommands.invokeAppAction("motd.markSeen", { "signature": signature })
		})
	}

	function mediaSessionExternalFallbackUrl() {
		const candidate = String(mediaSession ? mediaSession.url || "" : "").trim().slice(0, 2048)
		return /^https:\/\//i.test(candidate) ? candidate : ""
	}

	function refreshTimelineVirtualizedAccessibility() {
		if (!root.backgroundAccessibilitySuppressed || !timeline || !timeline.contentItem)
			return
		// QQuickItemView may write a materialized delegate's private accessible
		// bit after the product-level modal barrier has traversed the scene. Keep
		// this bounded to the live timeline rows; never scan the model or the full
		// product tree on a frame tick.
		const delegates = timeline.contentItem.children || []
		for (let index = 0; index < delegates.length; ++index) {
			const item = delegates[index]
			if (item && typeof item.reassertAccessibilitySuppression === "function")
				item.reassertAccessibilitySuppression()
		}
	}

	onModalUiActiveChanged: {
		if (modalUiActive) {
			refreshTimelineVirtualizedAccessibility()
			Qt.callLater(refreshTimelineVirtualizedAccessibility)
		}
	}

	function mediaSessionWindowFailureMessage() {
		if (mediaSessionWindowRuntimeFailed)
			return mediaRuntimeError
		const sessionMessage = mediaSession ? String(mediaSession.error || "") : ""
		return sessionMessage.length > 0 ? sessionMessage
			: qsTr("The isolated media window is unavailable. Open this provider in your browser instead.")
	}

	function rememberMediaWindowFailureFocus() {
		const candidate = root.activeFocusItem
		root.mediaWindowFailureFocusReturnItem = candidate && candidate.forceActiveFocus
			? candidate : null
	}

	function restoreMediaWindowFailureFocus() {
		const candidate = root.mediaWindowFailureFocusReturnItem
		root.mediaWindowFailureFocusReturnItem = null
		Qt.callLater(function() {
			if (!root.mediaSessionWindowUnavailable && candidate && candidate.forceActiveFocus
					&& candidate.visible && candidate.enabled)
				candidate.forceActiveFocus(Qt.PopupFocusReason)
		})
	}

	function richPreviewSizePreset(messageId) {
		const key = String(messageId || "")
		return key.length > 0 ? String(root.richPreviewSizePresets[key] || "") : ""
	}

	function rememberRichPreviewSizePreset(messageId, preset) {
		const key = String(messageId || "")
		const normalized = String(preset || "").trim().toLowerCase()
		if (key.length === 0 || [ "compact", "default", "large" ].indexOf(normalized) < 0)
			return
		const next = Object.assign({}, root.richPreviewSizePresets)
		next[key] = normalized
		root.richPreviewSizePresets = next
	}

	function openManagedPreviewImage(source, title, messageId) {
		return root.openAttachment({
			"url": source, "thumbnailUrl": source, "kind": "image", "alt": title
		}, title, messageId)
	}

	function startWatchTogether(url, provider, title, presentationAspect) {
		if (mediaSession && !mediaSession.sharedAvailable)
			return mediaSession.startShared(url, provider, title, presentationAspect)
		return false
	}

	function reportMediaSessionWindowComponentFailure() {
		if (!mediaSession || !mediaSession.active || !mediaSession.detached)
			return
		const message = qsTr("The isolated media window is unavailable. Open this provider in your browser instead.")
		if (typeof mediaSession.reportTypedError === "function")
			mediaSession.reportTypedError("renderer-component-unavailable", message)
		else if (typeof mediaSession.reportError === "function")
			mediaSession.reportError(message)
		Qt.callLater(function() {
			if (root.mediaSessionWindowComponentFailed)
				mediaWindowComponentRetryButton.forceActiveFocus()
		})
	}

	function retryMediaSessionWindowComponent() {
		if (!mediaSession || !mediaSession.active)
			return
		if (mediaSessionWindowRuntimeFailed && mediaProfiles
				&& typeof mediaProfiles.retryRuntime === "function")
			mediaProfiles.retryRuntime()
		if (typeof mediaSession.retry === "function")
			mediaSession.retry()
		mediaSessionWindowLoader.source = ""
		Qt.callLater(function() {
			if (mediaSessionWindowLoader.active)
				mediaSessionWindowLoader.source = root.mediaSessionWindowComponentUrl
		})
	}

	function handleMotdAction(actionId, payload) {
		const normalized = String(actionId || "").trim()
		uiCommands.invokeAppAction(normalized, payload || {})
	}

	Connections {
		target: clientSession
		function onMotdSignatureChanged() {
			if (root.motdSeenRequestSignature !== String(clientSession.motdSignature || ""))
				root.motdSeenRequestSignature = ""
			Qt.callLater(function() { root.maybeMarkMotdSeen() })
		}
		function onHasMotdChanged() { Qt.callLater(function() { root.maybeMarkMotdSeen() }) }
		function onMotdDismissedChanged() { Qt.callLater(function() { root.maybeMarkMotdSeen() }) }
		function onMotdChangedChanged() { Qt.callLater(function() { root.maybeMarkMotdSeen() }) }
		function onMotdLastSeenSignatureChanged() {
			if (String(clientSession.motdLastSeenSignature || "") === root.motdSeenRequestSignature)
				root.motdSeenRequestSignature = ""
		}
	}

	Connections {
		target: root.attachmentViewerHydrationModel || chatModel
		function onDataChanged() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
		function onModelReset() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
		function onRowsInserted() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
	}

	Component.onCompleted: Qt.callLater(function() { root.maybeMarkMotdSeen() })

	function messageDateLabel(source) {
		const createdAt = Number(source && source.createdAtMs ? source.createdAtMs : 0)
		if (createdAt <= 0)
			return ""
		const current = new Date(createdAt)
		if (isNaN(current.getTime()))
			return ""
		const today = new Date()
		if (current.getFullYear() === today.getFullYear()
				&& current.getMonth() === today.getMonth()
				&& current.getDate() === today.getDate())
			return qsTr("TODAY")
		const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1)
		if (current.getFullYear() === yesterday.getFullYear()
				&& current.getMonth() === yesterday.getMonth()
				&& current.getDate() === yesterday.getDate())
			return qsTr("YESTERDAY")
		return Qt.formatDate(current, Locale.LongFormat).toUpperCase()
	}

	function stableAlternatingRow(stableId) {
		const value = String(stableId || "")
		let parity = 0
		for (let index = 0; index < value.length; ++index)
			parity ^= value.charCodeAt(index) & 1
		return parity === 1
	}

	function normalizedModelList(value) {
		if (Array.isArray(value))
			return value
		if (value && value.count !== undefined && typeof value.get === "function") {
			const result = []
			for (let index = 0; index < value.count; ++index)
				result.push(value.get(index))
			return result
		}
		if (value && typeof value !== "string") {
			try {
				const result = Array.from(value)
				if (result.length > 0 || Number(value.length) === 0)
					return result
			} catch (error) {
				// QML model wrappers are not required to implement generic
				// JavaScript sequence conversion. The empty fallback is safe.
			}
		}
		return []
	}

	function richPreviewPresentationIdentity(messageId, previewValue) {
		const value = previewValue || ({})
		const metadata = value.metadata || ({})
		const mediaItems = value.mediaItems || []
		const mediaItemCount = Number(mediaItems.length !== undefined
			? mediaItems.length : (mediaItems.count || 0))
		return [
			String(messageId || ""),
			String(value.url || value.sourceUrl || ""),
			String(value.embedUrl || value.mediaUrl || ""),
			String(value.state || ""),
			String(value.failed === true ? "failed" : ""),
			String(value.title || ""),
			String(value.thumbnailUrl || value.posterUrl || ""),
			String(metadata.provider || metadata.previewProvider || ""),
			mediaItemCount,
			String(value.revision || value.updatedAt || value.cacheKey
				|| metadata.revision || "")
		].join("|")
	}

	function estimatedMessageTextWidth(segments, replyActor, replySnippet) {
		let longestLine = 0
		for (const segment of normalizedModelList(segments)) {
			const text = String(segment && segment.text !== undefined ? segment.text : "")
			for (const line of text.split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		for (const replyText of [replyActor, replySnippet]) {
			for (const line of String(replyText || "").split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		return longestLine * Theme.chatBodyFontSize * 0.58
	}

	function messageContainsInlineImage(segments) {
		for (const segment of normalizedModelList(segments)) {
			if (segment && String(segment.kind || "").toLowerCase() === "image")
				return true
		}
		return false
	}

	function normalizedComparableSourceUrl(value) {
		let url = String(value === undefined || value === null ? "" : value).trim()
		while (url.length > 1 && /\/$/.test(url))
			url = url.slice(0, -1)
		return url
	}

	function messageBodyIsPreviewSourceOnly(segments, preview) {
		const document = preview && preview.document ? preview.document : ({})
		const typedSourceUrl = document && document.commonPresentation === true && document.source
			? document.source.url || "" : ""
		const value = preview || ({})
		const sourceUrls = [typedSourceUrl, value.url, value.sourceUrl,
			value.embedUrl, value.mediaUrl].map(normalizedComparableSourceUrl)

		let text = ""
		let linkUrl = ""
		for (const segment of normalizedModelList(segments)) {
			if (!segment || segment.text === undefined)
				return false
			text += String(segment.text)
			const href = normalizedComparableSourceUrl(segment.href || "")
			if (href.length > 0) {
				if (linkUrl.length > 0 && linkUrl !== href)
					return false
				linkUrl = href
			}
		}
		const bodyUrl = normalizedComparableSourceUrl(text)
		if (!/^https?:\/\/\S+$/i.test(bodyUrl) || sourceUrls.indexOf(bodyUrl) < 0)
			return false
		return linkUrl.length === 0 || linkUrl === bodyUrl
	}

	function preferredAttachmentMessageWidth(attachments, ownOrSystem) {
		const values = normalizedModelList(attachments)
		if (values.length <= 0)
			return 0
		if (values.length > 1)
			return Theme.chatRichMaximumWidth
		const attachment = values[0] || ({})
		const kind = String(attachment.kind || "").toLowerCase()
		const mime = String(attachment.mime || "").toLowerCase()
		const image = kind === "image" || mime.indexOf("image/") === 0
		const requestedWidth = Math.max(0, Number(attachment.width || 0))
		const tileWidth = image ? Math.min(440, Math.max(220, requestedWidth || 320)) : 360
		const chromeWidth = ownOrSystem ? Theme.chatBubbleHorizontalPadding * 2
			: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
		return Math.min(Theme.chatRichMaximumWidth, tileWidth + chromeWidth)
	}

	function preferredOutgoingMessageWidth(segments, startsGroup, replyActor, replySnippet,
			deliveryVisible, actionTextInset) {
		// This only chooses a comfortable bubble width. RichMessageBody remains
		// responsible for exact text measurement and wrapping.
		const textEstimate = estimatedMessageTextWidth(segments, replyActor, replySnippet)
			+ Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
			+ Math.max(0, Number(actionTextInset || 0))
		const metadataFloor = deliveryVisible ? 320 : startsGroup ? 260 : 220
		return Math.max(metadataFloor, Math.min(Theme.chatPlainMaximumWidth, textEstimate))
	}

	function preferredIncomingMessageWidth(segments, startsGroup, replyActor, replySnippet,
			deliveryVisible) {
		// Include the avatar lane and inner bubble padding. Author metadata has a
		// slightly wider floor only on the first row of a group.
		const textEstimate = estimatedMessageTextWidth(segments, replyActor, replySnippet)
			+ Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
		const metadataFloor = deliveryVisible ? 320 : startsGroup ? 260 : 220
		return Math.max(metadataFloor, Math.min(Theme.chatPlainMaximumWidth, textEstimate))
	}

	function isSystemChatMessage(message, avatar, replyAllowed, reactionAllowed, ownMessage) {
		return !!message.system || (!message.actorKey && !message.actor
			&& avatar.length === 0 && !replyAllowed && !reactionAllowed && !ownMessage)
	}

	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		if (/^(image:\/\/mumble\/|qrc:\/)/i.test(source))
			return source
		return /^file:\/\//i.test(source)
			&& /\/mumble-qml-images-[A-Za-z0-9]+\/[0-9a-f]{64}-[0-9a-f-]{36}\.gif$/i.test(source)
			? source : ""
	}

	function attachmentIdentity(attachment) {
		if (!attachment)
			return ""
		const inlineToken = String(attachment.inlineToken || "").trim()
		if (inlineToken.length > 0)
			return "inline:" + inlineToken
		let assetId = attachment.assetId
		if (assetId === undefined || assetId === null || String(assetId).length === 0)
			assetId = attachment.assetID
		if (assetId !== undefined && assetId !== null && String(assetId).trim().length > 0)
			return "asset:" + String(assetId).trim()
		return "id:" + String(attachment.id || "").trim()
	}

	function openAttachment(attachment, titleOverride, hydrationMessageId, hydrationModel) {
		if (!attachment)
			return false
		const fullSource = safeRenderImageSource(attachment.url)
		const thumbnailSource = safeRenderImageSource(attachment.thumbnailUrl)
		const source = fullSource || thumbnailSource
		if (source.length === 0)
			return false
		let rawAssetId = attachment.assetId
		if (rawAssetId === undefined || rawAssetId === null || String(rawAssetId).length === 0)
			rawAssetId = attachment.assetID
		const assetId = String(rawAssetId === undefined || rawAssetId === null ? "" : rawAssetId).trim()
		const inlineToken = String(attachment.inlineToken || "").trim()
		const messageId = String(hydrationMessageId || attachment.hydrationMessageId || "").trim()
		const reportedOriginalState = String(attachment.originalState || "").trim().toLowerCase()
		const requestOriginal = (assetId.length > 0 || inlineToken.length > 0)
			&& messageId.length > 0 && fullSource.length === 0
			&& reportedOriginalState !== "error"
		attachmentViewerHydrationModel = hydrationModel || chatModel
		attachmentViewerPayload = {
			"id": attachment.id,
			"url": fullSource,
			"thumbnailUrl": thumbnailSource || source,
			"name": String(titleOverride || attachment.name || ""),
			"fileName": String(attachment.fileName || attachment.name || ""),
			"alt": String(titleOverride || attachment.alt || attachment.name || qsTr("Image attachment")),
			"kind": String(attachment.kind || "image"),
			"mime": String(attachment.mime || ""),
			"assetId": attachment.assetId,
			"assetID": attachment.assetID,
			"inlineToken": inlineToken,
			"byteSize": attachment.byteSize,
			"size": attachment.size,
			"width": attachment.width,
			"height": attachment.height,
			"originalState": requestOriginal ? "loading"
				: String(attachment.originalState || (fullSource.length > 0 ? "ready" : "idle")),
			"originalError": String(attachment.originalError || ""),
			"hydrationMessageId": messageId
		}
		if (requestOriginal) {
			if (assetId.length > 0)
				uiCommands.requestChatAttachmentImage(assetId, messageId)
			else
				uiCommands.requestChatInlineImage(inlineToken, messageId)
		}
		return true
	}

	function retryAttachmentOriginal(attachment) {
		if (!attachment)
			return false
		let rawAssetId = attachment.assetId
		if (rawAssetId === undefined || rawAssetId === null || String(rawAssetId).length === 0)
			rawAssetId = attachment.assetID
		const assetId = String(rawAssetId === undefined || rawAssetId === null ? "" : rawAssetId).trim()
		const inlineToken = String(attachment.inlineToken || "").trim()
		const messageId = String(attachment.hydrationMessageId || "").trim()
		if (messageId.length === 0 || (assetId.length === 0 && inlineToken.length === 0))
			return false

		const nextPayload = ({})
		for (const key in attachment)
			nextPayload[key] = attachment[key]
		nextPayload.originalState = "loading"
		nextPayload.originalError = ""
		attachmentViewerPayload = nextPayload
		if (assetId.length > 0)
			uiCommands.requestChatAttachmentImage(assetId, messageId)
		else
			uiCommands.requestChatInlineImage(inlineToken, messageId)
		return true
	}

	function refreshOpenAttachmentFromModel() {
		const current = attachmentViewerPayload
		if (!current)
			return false
		const messageId = String(current.hydrationMessageId || "").trim()
		const identity = attachmentIdentity(current)
		if (messageId.length === 0 || identity.length === 0)
			return false
		const sourceModel = attachmentViewerHydrationModel || chatModel
		if (!sourceModel || typeof sourceModel.rowForStableId !== "function"
				|| typeof sourceModel.get !== "function")
			return false
		const rowIndex = sourceModel.rowForStableId(messageId)
		if (rowIndex < 0)
			return false
		const row = sourceModel.get(rowIndex)
		const candidates = row && row.attachments ? row.attachments : []
		for (let index = 0; index < candidates.length; ++index) {
			const candidate = candidates[index]
			if (attachmentIdentity(candidate) !== identity)
				continue
			const nextFullSource = safeRenderImageSource(candidate.url)
			const nextThumbnailSource = safeRenderImageSource(candidate.thumbnailUrl)
			const nextSource = nextFullSource || nextThumbnailSource
			const sameSources = nextFullSource === String(current.url || "")
				&& nextThumbnailSource === String(current.thumbnailUrl || "")
			const sameOriginalState = String(candidate.originalState || "")
				=== String(current.originalState || "")
			if (nextSource.length === 0 || (sameSources && sameOriginalState))
				return false
			return openAttachment(candidate, current.alt || current.name, messageId, sourceModel)
		}
		return false
	}

	function resetQuickReactionState() {
		quickReactionMessageId = ""
		quickReactionAnchorItem = null
		quickReactionActiveReactions = []
	}

	function closeQuickReactions() {
		resetQuickReactionState()
		if (globalQuickReactionPopup.opened)
			globalQuickReactionPopup.close()
	}

	function positionQuickReactionPopup() {
		const anchor = quickReactionAnchorItem
		const overlay = globalQuickReactionPopup.parent
		if (!anchor || !overlay)
			return false
		const point = anchor.mapToItem(overlay, 0, 0)
		const margin = Theme.space2
		const gap = Theme.space1
		const desiredX = point.x + anchor.width - globalQuickReactionPopup.width
		globalQuickReactionPopup.x = Math.max(margin,
			Math.min(overlay.width - globalQuickReactionPopup.width - margin, desiredX))
		const belowY = point.y + anchor.height + gap
		const aboveY = point.y - globalQuickReactionPopup.height - gap
		globalQuickReactionPopup.y = belowY + globalQuickReactionPopup.height + margin <= overlay.height
			? belowY : Math.max(margin, aboveY)
		return true
	}

	function showQuickReactions(messageId, row, anchorItem, activeReactions) {
		const stableId = String(messageId || "")
		if (stableId.length === 0 || !anchorItem)
			return
		quickReactionMessageId = stableId
		quickReactionAnchorItem = anchorItem
		quickReactionActiveReactions = activeReactions || []
		Qt.callLater(function() {
			if (root.quickReactionMessageId !== stableId || !root.positionQuickReactionPopup())
				return
			if (!globalQuickReactionPopup.opened)
				globalQuickReactionPopup.open()
			Qt.callLater(function() { root.positionQuickReactionPopup() })
		})
	}

	function toggleQuickReactions(messageId, row, anchorItem, activeReactions) {
		const stableId = String(messageId || "")
		if (quickReactionMessageId === stableId) {
			closeQuickReactions()
			return
		}
		showQuickReactions(stableId, row, anchorItem, activeReactions)
	}

	function isImageAttachment(attachment) {
		if (!attachment)
			return false
		const kind = String(attachment.kind || "").trim().toLowerCase()
		const mime = String(attachment.mime || "").trim().toLowerCase()
		if (kind === "image" || mime.indexOf("image/") === 0)
			return true
		if (kind.length > 0 || mime.length > 0)
			return false
		// Older message rows only carried a sanitized image-provider URL.
		return safeRenderImageSource(attachment.url).length > 0
			|| safeRenderImageSource(attachment.thumbnailUrl).length > 0
	}

	function canOpenAttachmentExternally(attachment) {
		if (!attachment || isImageAttachment(attachment))
			return false
		const mime = String(attachment.mime || "").trim().toLowerCase()
		const supportedMimes = [
			"application/pdf", "text/plain", "text/markdown",
			"audio/mpeg", "audio/mp3", "audio/wav", "audio/x-wav", "audio/ogg",
			"audio/flac", "audio/x-flac", "audio/aac", "audio/mp4", "audio/webm",
			"video/mp4", "video/webm", "video/quicktime"
		]
		if (supportedMimes.indexOf(mime) >= 0)
			return true
		if (mime.length > 0 && mime !== "application/octet-stream")
			return false
		const kind = String(attachment.kind || "").trim().toLowerCase()
		const fileName = String(attachment.fileName || attachment.name || "").trim()
		if (kind === "document")
			return /\.(pdf|txt|md)$/i.test(fileName)
		if (kind === "audio")
			return /\.(mp3|m4a|mp4|wav|ogg|flac|aac|webm|weba)$/i.test(fileName)
		if (kind === "video")
			return /\.(mp4|webm|mov)$/i.test(fileName)
		return false
	}

	function requestAttachment(attachment, hydrationMessageId) {
		if (!attachment)
			return false
		const imageAttachment = isImageAttachment(attachment)
		if (imageAttachment && openAttachment(attachment, "", hydrationMessageId))
			return true
		if (!imageAttachment && canOpenAttachmentExternally(attachment))
			return openExternalAttachment(attachment)
		return downloadAttachment(attachment, !imageAttachment)
	}

	function openExternalAttachment(attachment) {
		if (!attachment)
			return false
		let rawAssetId = attachment.assetId
		if (rawAssetId === undefined || rawAssetId === null || String(rawAssetId).length === 0)
			rawAssetId = attachment.assetID
		const assetId = String(rawAssetId === undefined || rawAssetId === null ? "" : rawAssetId).trim()
		if (assetId.length === 0)
			return false
		uiCommands.openChatAttachment(assetId, String(attachment.fileName || attachment.name || ""))
		return true
	}

	function downloadAttachment(attachment, allowLegacyId) {
		if (!attachment)
			return false
		const inlineToken = String(attachment.inlineToken || "").trim()
		if (inlineToken.length > 0) {
			uiCommands.saveChatInlineImage(inlineToken,
				String(attachment.fileName || attachment.name || ""))
			return true
		}
		let rawAssetId = attachment.assetId
		if (rawAssetId === undefined || rawAssetId === null || String(rawAssetId).length === 0)
			rawAssetId = attachment.assetID
		if (allowLegacyId && (rawAssetId === undefined || rawAssetId === null
				|| String(rawAssetId).length === 0))
			rawAssetId = attachment.id
		const assetId = String(rawAssetId === undefined || rawAssetId === null ? "" : rawAssetId).trim()
		if (assetId.length === 0)
			return false
		uiCommands.downloadChatAttachment(assetId,
			String(attachment.fileName || attachment.name || ""))
		return true
	}

	function attachmentKindLabel(kind, mime) {
		const normalizedKind = String(kind || "").trim().toLowerCase()
		const normalizedMime = String(mime || "").trim().toLowerCase()
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

	function formatAttachmentByteSize(value) {
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
		const normalizedMime = String(mime || "").trim()
		if (normalizedMime.length > 0 && normalizedMime.toLowerCase() !== "application/octet-stream")
			parts.push(normalizedMime)
		const size = formatAttachmentByteSize(byteSize)
		if (size.length > 0)
			parts.push(size)
		return parts.join(" · ")
	}

	function localFileUrls(urls) {
		const result = []
		const source = urls || []
		for (let index = 0; index < source.length; ++index) {
			const candidate = source[index]
			if (/^file:/i.test(String(candidate || "").trim()))
				result.push(candidate)
		}
		return result
	}

	function localAttachmentUrls(urls, allowFiles) {
		const localUrls = localFileUrls(urls)
		if (allowFiles)
			return localUrls
		return localUrls.filter(url => /\.(png|jpe?g|gif|webp|bmp)$/i.test(String(url)))
	}

    function clearPreviewHydrationQueue() {
        pendingPreviewHydrationIds = ({})
        pendingPreviewHydrationHighPriority = false
		coldPreviewHydrationPacingActive = false
        previewHydrationTimer.stop()
    }

	function queuePreviewHydration(messageId, highPriority, ownerScopeToken, ownerGeneration) {
		if (root.visualFixtureOverrideActive)
			return false
		const normalized = String(messageId === undefined || messageId === null ? "" : messageId).trim()
		const currentScopeToken = String(activeScope.scopeToken || "")
		const currentGeneration = Number(timeline.scopePresentationGeneration || 0)
		const requestedScopeToken = ownerScopeToken === undefined
			? currentScopeToken : String(ownerScopeToken || "")
		const requestedGeneration = ownerGeneration === undefined
			? currentGeneration : Number(ownerGeneration || 0)
		if (!/^[1-9][0-9]*$/.test(normalized) || currentScopeToken.length === 0
				|| requestedScopeToken !== currentScopeToken
				|| requestedGeneration !== currentGeneration)
			return false
        // Keep protocol uint64 IDs as decimal strings. JavaScript numbers lose
        // precision above 2^53 and would hydrate a different message.
        if (normalized.length > 20)
            return false
		const key = requestedScopeToken + "|" + requestedGeneration + "|" + normalized
		const previousEntry = pendingPreviewHydrationIds[key] || ({})
		pendingPreviewHydrationIds[key] = {
			"id": normalized,
			"scopeToken": requestedScopeToken,
			"generation": requestedGeneration,
			"highPriority": previousEntry.highPriority === true || !!highPriority
		}
        pendingPreviewHydrationHighPriority = pendingPreviewHydrationHighPriority || !!highPriority
        if (!previewHydrationTimer.running)
            previewHydrationTimer.start()
        return true
    }

    function flushPreviewHydrationQueue() {
		if (timeline && (timeline.prependAnchorActive || chatModel.appendPending
				|| chatModel.prependPending)) {
			previewHydrationTimer.restart()
			return
		}
		if (coldPreviewHydrationPacingActive && timeline
				&& timeline.richLoaderResumeBusy) {
			// Do not let two independent per-frame queues converge on the same GUI
			// turn. Finish the lightweight row shells before cached preview results
			// begin instantiating their provider trees.
			previewHydrationTimer.restart()
			return
		}
		const keys = Object.keys(pendingPreviewHydrationIds)
        if (keys.length === 0) {
			coldPreviewHydrationPacingActive = false
            return
		}
		const batch = []
		let highPriority = pendingPreviewHydrationHighPriority
		const currentScopeToken = String(activeScope.scopeToken || "")
		const currentGeneration = Number(timeline.scopePresentationGeneration || 0)
		// Cold scope reveal is especially sensitive to several cached provider
		// results instantiating rich trees in the same GUI turn. Admit one visible
		// warm-up request per frame until the complete initial queue is drained;
		// steady-state hydration keeps the wider throughput-oriented batch.
		const maxBatch = coldPreviewHydrationPacingActive ? 1 : 32
		for (let index = 0; index < keys.length && batch.length < maxBatch; ++index) {
			const key = keys[index]
			const entry = pendingPreviewHydrationIds[key] || ({})
			delete pendingPreviewHydrationIds[key]
			if (String(entry.scopeToken || "") !== currentScopeToken
					|| Number(entry.generation || 0) !== currentGeneration)
				continue
			batch.push(String(entry.id || ""))
			highPriority = highPriority || entry.highPriority === true
		}
		pendingPreviewHydrationHighPriority = false
		if (batch.length > 0)
			uiCommands.requestPreviewHydration(currentScopeToken, batch, highPriority)
        if (Object.keys(pendingPreviewHydrationIds).length > 0)
            previewHydrationTimer.start()
		else
			coldPreviewHydrationPacingActive = false
    }

	function normalizedMenuVariant(value) {
		return String(value === undefined || value === null ? "" : value).trim()
	}

	function menuActionById(items, actionId) {
		const source = items || []
		for (let index = 0; index < source.length; ++index) {
			const entry = source[index] || ({})
			if (String(entry.id || "") === actionId)
				return entry
			const nestedItems = entry.items || (entry.submenu ? entry.submenu.items : null)
			const nested = menuActionById(nestedItems, actionId)
			if (nested)
				return nested
		}
		return null
	}

	function menuActionInGroups(groups, actionId) {
		const source = groups || []
		for (let index = 0; index < source.length; ++index) {
			const action = menuActionById((source[index] || ({})).items, actionId)
			if (action)
				return action
		}
		return null
	}

	function resolvedSettingsAction() {
		const fromApplicationMenu = menuActionInGroups(clientSession.appMenus || [],
			"configure.settings")
		if (fromApplicationMenu)
			return fromApplicationMenu
		const fromProfileMenu = menuActionById((clientSession.selfMenu || ({})).actions || [],
			"configure.settings")
		return fromProfileMenu || ({
			"kind": "action", "id": "configure.settings",
			"label": qsTr("Settings"), "enabled": true, "icon": "settings"
		})
	}

	function applicationMenuGroups() {
		const source = clientSession.appMenus || []
		const groups = []
		let helpGroup = null
		for (let index = 0; index < source.length; ++index) {
			const group = source[index] || ({})
			const groupId = String(group.id || "")
			// The server identity card owns this menu. App preferences have one
			// discoverable home in the footer Settings button, while identity lives
			// under the self card and conversation actions live in the room header.
			if (groupId === "configure")
				continue
			if (groupId === "help") {
				helpGroup = group
				continue
			}
			if (groupId === "server") {
				// Server actions are the primary content, so render them directly instead
				// of making the user open a redundant Server submenu first.
				groups.push(Object.assign({}, group, { "label": "" }))
			} else {
				groups.push(group)
			}
		}
		const audioStats = menuActionById(
			((clientSession.selfMenu || ({})).actions || []), "self.audioStats")
		if (audioStats) {
			groups.push({
				"id": "diagnostics", "label": qsTr("Diagnostics"), "icon": "activity",
				"items": [ audioStats ]
			})
		}
		if (helpGroup)
			groups.push(helpGroup)
		return groups
	}

	function requestSettings() {
		if (!root.settingsActionEnabled)
			return false
		const action = root.settingsActionEntry || ({})
		const actionPayload = action.payload ? action.payload : ({})
		closeProductMenus()
		if (root.navigationModalActive)
			navigationDrawer.close()
		uiCommands.invokeAppAction("configure.settings", actionPayload)
		return true
	}

	function profileMenuGroups() {
		const state = clientSession.selfMenu || ({})
		const groups = []
		const presence = state.presence || []
		const actions = []
		const profileActionIds = [
			"self.comment", "self.avatarChange", "self.avatarRemove",
			"self.register", "configure.certificate"
		]
		for (let index = 0; index < profileActionIds.length; ++index) {
			const action = menuActionById(state.actions || [], profileActionIds[index])
			if (action)
				actions.push(action)
		}
		if (presence.length > 0) {
			groups.push({
				"id": "presence",
				"label": qsTr("Presence"),
				"icon": "activity",
				"items": presence
			})
		}
		if (actions.length > 0) {
			groups.push({
				"id": "profile-actions",
				"label": qsTr("Profile"),
				"icon": "user",
				"items": actions
			})
		}
		return groups
	}

	function closeProductMenus() {
		appMenuPopup.close()
		profileMenuPopup.close()
		roomMenuPopup.close()
		textRoomMenuPopup.close()
		participantMenuPopup.close()
		chatBackgroundMenuPopup.close()
	}

	function roomRowForKind(kind) {
		for (let index = 0; index < roomModel.count; ++index) {
			const row = roomModel.get(index)
			if (row && row.kind === kind)
				return row
		}
		return null
	}

	function selectedRoomRow() {
		for (let index = 0; index < roomModel.count; ++index) {
			const row = roomModel.get(index)
			if (row && row.selected)
				return row
		}
		return roomModel.count > 0 ? roomModel.get(0) : null
	}

	function openMenuAt(menu, anchorPoint, focusReturnTarget) {
		const point = anchorPoint && anchorPoint.x !== undefined
			? anchorPoint : Qt.point(Math.round(root.width / 2), Math.round(root.height / 3))
		// Menu declarations live in several different visual branches, so Qt's
		// implicit popup-parent focus restoration is not reliable after reparenting.
		// Preserve the concrete row/button that launched the top-level menu instead.
		menu.openerItem = focusReturnTarget && focusReturnTarget.forceActiveFocus
			? focusReturnTarget : null
		// The ModernMenu helper owns all logical-pixel sizing and edge clamping.
		// Keeping this path devicePixelRatio-free is essential on mixed-DPI Windows.
		return menu.openAtLogicalPoint(root.contentItem, point, Theme.space2,
			Theme.space2 + Theme.elevationMenuOffset)
	}

	function openScopeMenu(scopeToken, kind, actions, anchorPoint) {
		const focusReturnTarget = root.activeFocusItem
		closeProductMenus()
		contextScopeToken = String(scopeToken || "")
		contextScopeKind = String(kind || "")
		let resolvedActions = actions || []
		if (resolvedActions.length === 0 && contextScopeToken.length > 0)
			resolvedActions = uiCommands.requestScopeActions(contextScopeToken, contextScopeKind) || []
		contextScopeActions = resolvedActions
		const menu = contextScopeKind === "text" ? textRoomMenuPopup : roomMenuPopup
		if (!menu.hasActionableEntries(contextScopeActions))
			return false
		return openMenuAt(menu, anchorPoint, focusReturnTarget)
	}

	function openConversationSearch() {
		if (root.modalUiActive)
			return
		root.conversationSearchOpen = true
		Qt.callLater(function() { conversationSearchBar.activate() })
	}

	function closeConversationSearch(restoreFocus) {
		const searchOwnedFocus = root.itemOwnsActiveFocus(conversationSearchBar)
		conversationSearchBar.reset()
		root.conversationSearchOpen = false
		if (restoreFocus || searchOwnedFocus) {
			Qt.callLater(function() {
				// A pointer-driven scope change has already established a better focus
				// target. Only recover when focus remained in the now-hidden search bar.
				const current = root.activeFocusItem
				if (!restoreFocus && current && current.visible
						&& !root.itemOwnsActiveFocus(conversationSearchBar))
					return
				if (conversationSearchButton.visible && conversationSearchButton.enabled)
					conversationSearchButton.forceActiveFocus(Qt.OtherFocusReason)
				else if (conversationViewLoader.item)
					conversationViewLoader.item.forceActiveFocus(Qt.OtherFocusReason)
				else
					timeline.forceActiveFocus(Qt.OtherFocusReason)
			})
		}
	}

	function revealConversationSearchMatch(row, stableId) {
		if (!root.conversationSearchOpen || row < 0 || String(stableId || "").length === 0)
			return
		if (conversationViewLoader.item) {
			conversationViewLoader.item.revealRow(row, stableId)
			return
		}
		const presentationRow = chatModel.presentationModel.rowForStableId(String(stableId || ""))
		if (presentationRow < 0) {
			if (chatModel.materializeDeferredMessage(String(stableId || "")))
				Qt.callLater(function() {
					root.revealConversationSearchMatch(row, stableId)
				})
			return
		}
		timeline.releasePrependAnchor()
		timeline.releaseViewportAnchor()
		timeline.releaseTopAnchor()
		timeline.invalidateBottomFollow()
		timeline.stickToBottom = false
		timeline.currentIndex = presentationRow
		timeline.positionViewAtIndex(presentationRow, ListView.Center)
	}

	// Automation: shows the conversation's row at an index (0 is the newest).
	function positionConversationRowAt(index) {
		const view = conversationViewLoader.item
		if (!view || index < 0 || index >= view.count)
			return false
		view.followTail = false
		view.positionAtIndex(index, ListView.Beginning)
		return true
	}
	// Automation: plays the card of the conversation's row at an index.
	function playConversationRowAt(index) {
		const view = conversationViewLoader.item
		if (!positionConversationRowAt(index))
			return false
		view.forceLayout()
		const row = view.itemAtIndex(index)
		return !!row && view.startPlayback(row)
	}
	// Automation: opens the detail sheet of the conversation's row at an index.
	function openConversationDetailsAt(index, mediaIndex) {
		const view = conversationViewLoader.item
		// A negative index closes the sheet.
		if (view && index < 0) {
			view.closeDetails()
			return true
		}
		if (!positionConversationRowAt(index))
			return false
		view.forceLayout()
		const row = view.itemAtIndex(index)
		if (!row || !row.usesEmbedCard)
			return false
		view.openDetails(row, mediaIndex)
		return true
	}
	function positionVisualFixtureTimelineAt(stableId) {
		if (!root.visualFixtureOverrideActive)
			return false
		if (conversationViewLoader.item) {
			const view = conversationViewLoader.item
			const index = chatTimelineV2.rowForStableId(String(stableId || ""))
			if (index < 0)
				return false
			view.followTail = false
			view.positionAtIndex(index, ListView.Beginning)
			return true
		}
		const row = chatModel.presentationModel.rowForStableId(String(stableId || ""))
		if (row < 0)
			return false
		timeline.releasePrependAnchor()
		timeline.releaseViewportAnchor()
		timeline.releaseTopAnchor()
		timeline.invalidateBottomFollow()
		timeline.followTailAfterInsert = false
		timeline.pendingTailInsertCount = 0
		timeline.pendingTailMessageCount = 0
		timeline.stickToBottom = false
		timeline.currentIndex = row
		timeline.positionViewAtIndex(row, ListView.Beginning)
		return true
	}

	function visualFixtureTimelineState() {
		if (!root.visualFixtureOverrideActive)
			return ({})
		if (conversationViewLoader.item) {
			const view = conversationViewLoader.item
			const first = view.firstVisibleRow()
			return {
				"firstVisibleStableId": first ? String(first.stableId || "") : "",
				"firstVisibleOffset": first ? Number(first.y - view.contentY) : 0,
				"contentY": Number(view.contentY),
				"count": Number(view.count)
			}
		}
		const item = timeline.firstVisibleMessageDelegate()
		return {
			"firstVisibleStableId": item ? String(item.stableId || "") : "",
			"firstVisibleOffset": item ? Number(item.y - timeline.contentY) : 0,
			"contentY": Number(timeline.contentY),
			"count": Number(timeline.count)
		}
	}

	function openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken, rowKey) {
		const focusReturnTarget = root.activeFocusItem
		closeProductMenus()
		contextParticipantId = String(sessionId || "")
		contextParticipantRowKey = String(rowKey || "")
		contextParticipantActions = actions || []
		contextParticipantEntryKind = String(entryKind || "user").toLowerCase()
		contextParticipantScopeToken = String(scopeToken || "")
		if (!participantMenuPopup.hasActionableEntries(contextParticipantActions))
			return false
		return openMenuAt(participantMenuPopup, anchorPoint, focusReturnTarget)
	}

	function openProfileMenu(anchorPoint) {
		const focusReturnTarget = root.activeFocusItem
		closeProductMenus()
		return openMenuAt(profileMenuPopup, anchorPoint, focusReturnTarget)
	}

	function openApplicationMenu(anchorPoint, focusReturnTarget) {
		const opener = focusReturnTarget && focusReturnTarget.forceActiveFocus
			? focusReturnTarget : root.activeFocusItem
		const openMenu = function() {
			root.closeProductMenus()
			root.openMenuAt(appMenuPopup, anchorPoint, opener)
		}
		if (root.navigationModalActive) {
			navigationDrawer.close()
			Qt.callLater(openMenu)
		} else {
			openMenu()
		}
		return true
	}

	function chatBackgroundMenuEntries() {
		const result = []
		const selfActions = (clientSession.selfMenu || ({})).actions || []
		const conversationActionIds = [
			"self.recording", "self.prioritySpeaker"
		]
		for (let index = 0; index < conversationActionIds.length; ++index) {
			const action = menuActionById(selfActions, conversationActionIds[index])
			if (action)
				result.push(action)
		}
		if (activeScope.canLoadOlder) {
			if (result.length > 0)
				result.push({ "kind": "separator" })
			result.push({
				"kind": "action", "id": "activeScope.loadOlder",
				"label": qsTr("Load older messages"),
				"enabled": activeScope.loadingState !== "older"
			})
		}
		if (result.length > 0 && root.contextScopeActions.length > 0
				&& String((result[result.length - 1] || ({})).kind || "") !== "separator")
			result.push({ "kind": "separator" })
		for (let index = 0; index < root.contextScopeActions.length; ++index)
			result.push(root.contextScopeActions[index])
		return result
	}

	function openChatBackgroundMenu(anchorPoint) {
		const focusReturnTarget = root.activeFocusItem
		closeProductMenus()
		const row = selectedRoomRow()
		contextScopeToken = row ? String(row.scopeToken || "") : String(activeScope.scopeToken || "")
		contextScopeKind = row ? String(row.kind || "") : ""
		contextScopeActions = row && row.source ? (row.source.actions || []) : []
		if (contextScopeToken.length > 0 && contextScopeActions.length === 0)
			contextScopeActions = uiCommands.requestScopeActions(contextScopeToken, contextScopeKind) || []
		if (!chatBackgroundMenuPopup.hasActionableEntries(chatBackgroundMenuEntries()))
			return false
		return openMenuAt(chatBackgroundMenuPopup, anchorPoint, focusReturnTarget)
	}

	function visibleMenuLabels(menu) {
		const labels = []
		if (!menu || menu.count === undefined || !menu.itemAt)
			return labels
		for (let index = 0; index < menu.count; ++index) {
			const item = menu.itemAt(index)
			if (!item || !item.visible || item.height <= 0)
				continue
			const label = String(item.text || "").trim()
			if (label.length > 0)
				labels.push(label)
		}
		return labels
	}

	function openAutomationMenuProbe(variant) {
		const inputVariant = normalizedMenuVariant(variant)
		const alias = inputVariant.toLowerCase()
		const legacyAppServerAlias = alias === "appserver" || alias === "app-server"
			|| alias === "server-submenu"
		let normalized = inputVariant
		if (alias === "self" || alias === "profile")
			normalized = "profile"
		else if (alias === "member" || alias === "participant")
			normalized = "participant"
		else if (alias === "chat" || alias === "background" || alias === "chatbackground")
			normalized = "chatBackground"
		else if (alias === "textroom" || alias === "textroomreal")
			normalized = "textRoom"
		else if (legacyAppServerAlias)
			// Server actions now live directly in the application menu. Keep the
			// legacy visual-case alias as a dark-theme capture of that real
			// surface instead of trying to open a submenu the product removed.
			normalized = "app"
		else if (alias === "app" || alias === "room" || alias === "message")
			normalized = alias
		// The legacy dark app-server case immediately follows the light app-menu
		// case. Reuse that already-open popup while its theme updates; closing and
		// reopening the same QQuickPopup in one scene turn races its exit
		// transition. Isolated runs still take the normal fresh-open path below.
		const preserveOpenAppMenu = legacyAppServerAlias && appMenuPopup.visible
		if (!preserveOpenAppMenu)
			closeProductMenus()
		automationMenuVariant = normalized
		automationMenuSurface = null
		automationMenuFocusItem = null
		let handled = true
		let menu = null
		if (normalized === "app") {
			menu = appMenuPopup
			if (!menu.visible)
				openMenuAt(menu, Qt.point(root.width - menu.width - 24, 72))
		} else if (normalized === "profile") {
			menu = profileMenuPopup
			openProfileMenu(Qt.point(root.width - menu.width - 24, root.height - 90))
		} else if (normalized === "room" || normalized === "textRoom") {
			const row = roomRowForKind(normalized === "textRoom" ? "text" : "voice")
			if (!row) {
				handled = false
			} else {
				menu = normalized === "textRoom" ? textRoomMenuPopup : roomMenuPopup
				handled = openScopeMenu(row.scopeToken, row.kind,
					row.source ? (row.source.actions || []) : [],
					Qt.point(root.width - menu.width - 24, 150))
			}
		} else if (normalized === "participant") {
			let row = null
			for (let index = 0; index < root.navigationRailModel.count; ++index) {
				const candidate = root.navigationRailModel.get(index)
				const candidateIsSelf = candidate && (!!candidate.isSelf
					|| !!(candidate.source && candidate.source.isSelf))
				if (candidate && String(candidate.kind || "") === "participant"
						&& !candidateIsSelf) {
					row = candidate
					break
				}
			}

			if (!row) {
				handled = false
			} else {
				const participantId = String(row.participantSession
					|| (row.source && row.source.session) || row.stableId || row.id || "")
				const participantRowKey = String(row.stableId || row.id || "")
				if (participantId.length === 0 || participantRowKey.length === 0) {
					handled = false
				} else {
					// Exercise the same model -> NavigationRail -> UiCommandController ->
					// product-menu path as a real participant-row context request. This
					// keeps automation from inventing actions that the live UI cannot expose.
					const rail = desktopNavigationRail.visible
						? desktopNavigationRail : navigationDrawerRail
					menu = participantMenuPopup
					rail.requestParticipantMenu(participantId, participantRowKey, row,
						Qt.point(root.width - menu.width - 24, 250))
					handled = menu.visible
				}
			}
		} else if (normalized === "chatBackground") {
			menu = chatBackgroundMenuPopup
			handled = openChatBackgroundMenu(
				Qt.point(Math.round(root.width / 2), Math.round(root.height / 2)))
		} else if (normalized === "message") {
			closeProductMenus()
			// A single-case automation run may arrive before ListView has completed
			// its first delegate pass. Materialize the visible rows, then select a
			// genuinely actionable message instead of relying on an incidental
			// currentIndex left behind by an earlier visual case.
			timeline.forceLayout()
			let message = timeline.currentItem && timeline.currentItem.hasMessageActions
				? timeline.currentItem : null
			for (let index = 0; !message && index < timeline.count; ++index) {
				const candidate = timeline.itemAtIndex(index)
				if (candidate && candidate.openAutomationActions && candidate.hasMessageActions)
					message = candidate
			}
			if (!message || !message.openAutomationActions) {
				handled = false
			} else {
				menu = message.openAutomationActions()
			}
		} else {
			handled = false
		}
		automationMenuSurface = handled ? menu : null
		const menuVisible = handled && menu !== null && menu.visible
		const surfaceId = handled && menu !== null
			? String(menu.objectName || (normalized + "Menu")) : ""
		let captureRect = ({})
		if (menuVisible) {
			const surface = menu.contentItem || menu
			let origin = Qt.point(Number(menu.x || 0), Number(menu.y || 0))
			if (surface && surface.mapToItem) {
				const mapped = surface.mapToItem(root.contentItem || null, 0, 0)
				if (mapped)
					origin = mapped
			}
			captureRect = {
				"x": Math.round(Number(origin.x || 0)),
				"y": Math.round(Number(origin.y || 0)),
				"width": Math.round(Number((surface && surface.width) || menu.width || 0)),
				"height": Math.round(Number((surface && surface.height) || menu.height || 0))
			}
		}
		return {
			"handled": handled,
			"variant": normalized,
			"inputVariant": inputVariant,
			"open": handled && menu !== null && (menu.opened || menu.visible),
			"visible": menuVisible,
			"surfaceId": surfaceId,
			"objectName": surfaceId,
			"captureRect": captureRect,
			"viewportWidth": Math.round(root.width),
			"viewportHeight": Math.round(root.height),
			"fixtureUsed": false,
			"labels": handled ? visibleMenuLabels(menu) : [],
			"menuCount": handled && menu && menu.count !== undefined ? Number(menu.count) : 0,
			"menuHeight": handled && menu ? Number(menu.height || 0) : 0,
			"menuImplicitHeight": handled && menu ? Number(menu.implicitHeight || 0) : 0,
			"targetCanReply": handled && menu && menu.targetCanReply !== undefined
				? !!menu.targetCanReply : false,
			"targetCanReact": handled && menu && menu.targetCanReact !== undefined
				? !!menu.targetCanReact : false
		}
	}

	function directMessageAutomationSurfaceState(variant) {
		const inputVariant = String(variant || "main").trim().toLowerCase()
		const normalized = inputVariant === "private" ? "window" : inputVariant
		let surface = null
		let visible = false
		let surfaceId = ""
		let objectName = ""
		let captureRect = ({})
		let viewportWidth = Math.round(root.width)
		let viewportHeight = Math.round(root.height)
		let windowId = "main"

		if (normalized === "main") {
			surface = timeline
			visible = root.visible && timeline.visible
				&& String(activeScope.scopeToken || "").indexOf("-2:") === 0
			surfaceId = "directMessage.main"
		} else if (normalized === "tray") {
			surface = directMessageTrayLoader.item
			visible = !!surface && directMessageTrayLoader.visible && surface.visible
			surfaceId = "directMessage.tray"
		} else if (normalized === "window") {
			surface = directMessageWindowLoader.item
			visible = !!surface && surface.visible
			surfaceId = "directMessage.window"
			windowId = "direct-message"
			if (surface) {
				viewportWidth = Math.round(Number(surface.width || 0))
				viewportHeight = Math.round(Number(surface.height || 0))
				captureRect = {
					"x": 0,
					"y": 0,
					"width": viewportWidth,
					"height": viewportHeight
				}
			}
		}

		if (surface) {
			objectName = String(surface.objectName || surfaceId)
			if (normalized !== "window" && surface.mapToItem) {
				const mapped = surface.mapToItem(root.contentItem || null, 0, 0)
				captureRect = {
					"x": Math.round(Number(mapped ? mapped.x : 0)),
					"y": Math.round(Number(mapped ? mapped.y : 0)),
					"width": Math.round(Number(surface.width || 0)),
					"height": Math.round(Number(surface.height || 0))
				}
			}
		}

		return {
			"handled": !!surface,
			"variant": normalized,
			"inputVariant": inputVariant,
			"visible": visible,
			"surfaceId": surfaceId,
			"objectName": objectName,
			"windowId": windowId,
			"captureRect": captureRect,
			"viewportWidth": viewportWidth,
			"viewportHeight": viewportHeight
		}
	}

	function runMotdUiProbe(action, signature) {
		if (!motdPanel || !motdPanel.runProbe) {
			return {
				"handled": false,
				"action": String(action || "").trim().toLowerCase(),
				"expanded": false,
				"visible": false,
				"dismissedSignature": "",
				"reason": "missing-motd-surface"
			}
		}
		return motdPanel.runProbe(action, signature)
	}

	function setAutomationNavigationOpen(open) {
		if (!compactNavigation)
			return false
		if (open)
			navigationDrawer.open()
		else
			navigationDrawer.close()
		return true
	}

	function itemIsWithin(item, ancestor) {
		let cursor = item
		while (cursor) {
			if (cursor === ancestor)
				return true
			cursor = cursor.parent
		}
		return false
	}

	onCompactNavigationChanged: {
		// Visual-gate state injection changes the viewport before it establishes
		// its explicit focus target. Do not queue a responsive-layout focus
		// handoff that can race and steal that deterministic target one frame
		// later; the fixture controller owns focus for the duration of the case.
		if (visualFixtureOverrideActive) {
			if (!compactNavigation)
				navigationDrawer.close()
			return
		}
		const focusedItem = root.activeFocusItem
		const focusWasInDesktopRail = root.itemIsWithin(focusedItem, desktopNavigationRail)
		const focusWasInDrawer = root.itemIsWithin(focusedItem, navigationDrawerRail)
			|| focusedItem === navigationToggle
		if (!compactNavigation)
			navigationDrawer.close()
		Qt.callLater(function() {
			if (root.compactNavigation && focusWasInDesktopRail && navigationToggle.visible)
				navigationToggle.forceActiveFocus(Qt.OtherFocusReason)
			else if (!root.compactNavigation && focusWasInDrawer)
				desktopNavigationRail.focusInitialItem()
		})
	}

	function visualFixtureItemByObjectName(item, objectName) {
		if (!item || !objectName)
			return null
		if (String(item.objectName || "") === objectName)
			return item
		const children = item.children || []
		for (let index = 0; index < children.length; ++index) {
			const match = visualFixtureItemByObjectName(children[index], objectName)
			if (match)
				return match
		}
		return null
	}

	function focusVisualFixture(state, surfaceVariant) {
		const surface = String(surfaceVariant || "none")
		if (surface.indexOf("conversation-search-") === 0) {
			const searchTarget = visualFixtureItemByObjectName(root.contentItem,
				"conversationSearchField")
			if (searchTarget && searchTarget.visible && searchTarget.enabled) {
				searchTarget.forceActiveFocus(Qt.OtherFocusReason)
				return searchTarget.objectName
			}
		}
		if (dialogState.open && productDialog.visible) {
			return String(productDialog.applyInitialFocus() || "")
		}
		if (surface.indexOf("menu-") === 0) {
			if (automationMenuFocusItem && automationMenuFocusItem.visible
					&& automationMenuFocusItem.enabled
					&& appMenuPopup.focusMenuItem(automationMenuFocusItem))
				return String(automationMenuFocusItem.objectName || "semanticMenuItem")
			const menuFocus = automationMenuSurface && automationMenuSurface.visible
				&& automationMenuSurface.focusInitialItem
				? automationMenuSurface.focusInitialItem() : root.activeFocusItem
			if (menuFocus)
				return String(menuFocus.objectName || "semanticMenuItem")
		}
		if (surface === "direct-message-main" || surface === "chat-composer-states") {
			const dmComposer = visualFixtureItemByObjectName(root.contentItem, "visualFixtureComposer")
			if (dmComposer && dmComposer.visible && dmComposer.enabled) {
				dmComposer.forceActiveFocus(Qt.OtherFocusReason)
				return dmComposer.objectName
			}
		}
		if (surface.indexOf("async-") === 0) {
			const operationTarget = operationOverlay.visualFixtureFocusTarget
				|| visualFixtureItemByObjectName(root.contentItem,
					surface === "async-running" ? "operationCancelButton" : "visualFixtureDismissOperation")
			if (operationTarget && operationTarget.visible && operationTarget.enabled) {
				operationTarget.forceActiveFocus(Qt.OtherFocusReason)
				return operationTarget.objectName
			}
			// The ListView delegate can arrive one presented frame after its
			// controller row. Report a pending target so the bounded fixture loop
			// retries it instead of silently focusing the app-menu fallback.
			return ""
		}
		if (surface.indexOf("toast-") === 0) {
			const toastTarget = visualFixtureItemByObjectName(root.contentItem, "toastDismissButton")
			if (toastTarget && toastTarget.visible && toastTarget.enabled) {
				toastTarget.forceActiveFocus(Qt.OtherFocusReason)
				return toastTarget.objectName
			}
		}
		if (surface === "update-banner") {
			const updateAction = visualFixtureItemByObjectName(root.contentItem, "updateAction_update.restart")
			if (updateAction && updateAction.visible && updateAction.enabled) {
				updateAction.forceActiveFocus(Qt.OtherFocusReason)
				return updateAction.objectName
			}
		}
		if (surface.indexOf("media-inline-") === 0) {
			let mediaControlName = "inlineMediaPopoutButton"
			if (surface === "media-inline-active" || surface === "media-inline-controls")
				mediaControlName = "mediaPlayButton"
			else if (surface === "media-inline-error" || surface === "media-inline-retry")
				mediaControlName = "inlineMediaRetryButton"
			else if (surface === "media-inline-external")
				mediaControlName = "inlineMediaFailureExternalButton"
			const mediaControl = visualFixtureItemByObjectName(root.contentItem, mediaControlName)
			if (mediaControl && mediaControl.visible && mediaControl.enabled) {
				mediaControl.forceActiveFocus(Qt.OtherFocusReason)
				return mediaControl.objectName
			}
		}
		if (root.compactNavigation && navigationDrawer.opened) {
			// A compact drawer owns the visible navigation surface. Focusing the
			// application-menu button behind that modal drawer leaves assistive
			// technology without a focused node after a theme/viewport transition.
			// Focus the selected room and return the containing ListView as the
			// stable fixture target; delegate reuse may replace the concrete row.
			navigationDrawerRail.focusInitialItem()
			return navigationDrawerRail.roomListObjectName
		}
		if (state === "connected") {
			// The deterministic fixture intentionally has no writable live scope,
			// so its composer is disabled and cannot own accessibility focus.
			appMenuButton.forceActiveFocus(Qt.OtherFocusReason)
			return appMenuButton.objectName
		}
		if (state === "error" || state === "loading") {
			if (connectionBanner.focusPrimaryAction())
				return "connectionBannerPrimaryAction"
		}
		if (state === "empty" && emptyConversationConnectButton.visible
				&& emptyConversationConnectButton.enabled) {
			emptyConversationConnectButton.forceActiveFocus(Qt.OtherFocusReason)
			return emptyConversationConnectButton.objectName
		}
		appMenuButton.forceActiveFocus(Qt.OtherFocusReason)
		return appMenuButton.objectName
	}

	Instantiator {
		model: actionModel
		delegate: Shortcut {
			required property string stableId
			required property var payload
			sequence: payload.shortcutPortableText || ""
			enabled: payload.enabled && payload.visible && sequence.length > 0
			context: Qt.ApplicationShortcut
			onActivated: actionModel.trigger(stableId)
		}
	}

	function itemOwnsActiveFocus(item) {
		if (!item)
			return false
		for (let current = root.activeFocusItem; current; current = current.parent) {
			if (current === item)
				return true
		}
		return false
	}

	function focusNavigationLandmark() {
		if (desktopNavigationRail.visible) {
			desktopNavigationRail.focusInitialItem()
			return true
		}
		return false
	}

	function cycleMainLandmarkFocus() {
		if (itemOwnsActiveFocus(desktopNavigationRail)) {
			appMenuButton.forceActiveFocus(Qt.TabFocusReason)
			return
		}
		if (itemOwnsActiveFocus(shellHeader)) {
			timeline.forceActiveFocus(Qt.TabFocusReason)
			return
		}
		if (itemOwnsActiveFocus(timeline)) {
			if (composerInput.visible && composerInput.enabled)
				composerInput.forceActiveFocus(Qt.TabFocusReason)
			else if (!focusNavigationLandmark())
				appMenuButton.forceActiveFocus(Qt.TabFocusReason)
			return
		}
		if (itemOwnsActiveFocus(composerSurface)) {
			if (!focusNavigationLandmark())
				appMenuButton.forceActiveFocus(Qt.TabFocusReason)
			return
		}
		if (!focusNavigationLandmark())
			appMenuButton.forceActiveFocus(Qt.TabFocusReason)
	}

	Shortcut {
		sequence: "F6"
		context: Qt.WindowShortcut
		enabled: !root.modalUiActive
		onActivated: root.cycleMainLandmarkFocus()
	}

    function createScreenShareView(backend) {
		return screenShareViewComponent.createObject(null, {
			"backend": backend,
			"visualFixtureMode": root.visualFixtureOverrideActive
		})
    }

	function preparePerformanceChatScrollWorkload(stepCount, fullRangeToStart) {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		const requestedSteps = Math.max(1, Math.floor(Number(stepCount) || 0))
		// Each step must move the real ListView by at least two logical pixels so
		// the scene-graph cannot legitimately coalesce it into a no-op frame.
		const loadedTravel = maximumY - minimumY
		const availableUp = Math.max(0, timeline.contentY - minimumY)
		const availableDown = Math.max(0, maximumY - timeline.contentY)
		const maximumDirectionalTravel = Math.max(availableUp, availableDown)
		const desiredGestureTravel = requestedSteps * 16
		const minimumGestureTravel = Math.min(80, desiredGestureTravel)
		const liveRangeUnavailable = fullRangeToStart === true
			&& (timeline.count < 2 || loadedTravel < 8)
		const fixtureRangeUnavailable = fullRangeToStart !== true
			&& (timeline.count < 2
				|| maximumDirectionalTravel
					< Math.max(requestedSteps * 2, minimumGestureTravel))
		if (liveRangeUnavailable || fixtureRangeUnavailable)
			return { "started": false, "reason": qsTr("The chat timeline is not scrollable."),
				"count": timeline.count, "contentHeight": timeline.contentHeight, "viewportHeight": timeline.height }
		// The deterministic workload owns contentY from this point onward. Retire
		// native Flickable kinetics and the custom runway before capturing its start
		// position. Publish the guard first because cancelFlick() may synchronously
		// emit movementEnded.
		performanceChatScrollOwnershipActive = true
		timelineScrollHandler.cancelNavigationForOwnershipLoss()
		performanceChatScrollStartY = timeline.contentY
		if (fullRangeToStart === true) {
			// The release gate starts from the real tail and traverses every loaded
			// message before it measures the first older-page request.
			performanceChatScrollTargetY = minimumY
		} else {
			// Model a sustained wheel/trackpad gesture, not a teleport through the
			// entire history. Sixteen logical pixels per presented input mirrors a
			// high-resolution wheel or trackpad gesture for the synthetic fixture.
			const travelDistance = Math.min(maximumDirectionalTravel,
				desiredGestureTravel)
			const scrollUp = availableUp >= travelDistance
				&& (availableDown < travelDistance || availableUp >= availableDown)
			performanceChatScrollTargetY = scrollUp
				? performanceChatScrollStartY - travelDistance
				: performanceChatScrollStartY + travelDistance
		}
		timeline.invalidateBottomFollow()
		timeline.stickToBottom = false
		timeline.releaseViewportAnchor()
		timeline.releaseTopAnchor()
		performanceChatScrollDiagnosticsBefore = timelineDelegateDiagnostics()
		performanceChatScrollRetainedRichIds = ({})
		performanceChatScrollRetainedRichResourceDropCount = 0
		const retainedRichIds = ({})
		const scrollDelta = performanceChatScrollTargetY - performanceChatScrollStartY
		const children = timeline.contentItem ? timeline.contentItem.children : []
		for (let childIndex = 0; childIndex < children.length; ++childIndex) {
			const item = children[childIndex]
			if (!item || item.performanceTimelineDelegate !== true
					|| item.accessibilityPooled === true
					|| item.richResourcesRequired !== true
					|| item.richResourcesActive !== true)
				continue
			const point = item.mapToItem(timeline, 0, 0)
			const finalY = point.y - scrollDelta
			const visibleNow = point.y + item.height > 0.5
				&& point.y < timeline.height - 0.5
			const visibleAtTarget = finalY + item.height > 0.5
				&& finalY < timeline.height - 0.5
			const retainedId = String(item.stableId || "")
			if (visibleNow && visibleAtTarget && retainedId.length > 0)
				retainedRichIds[retainedId] = true
		}
		performanceChatScrollRetainedRichIds = retainedRichIds
		performanceChatScrollCompletedSteps = 0
		performanceChatScrollTargetSteps = requestedSteps
		performanceChatScrollFullRangeToStart = fullRangeToStart === true
		performanceChatScrollRunning = true
		return { "started": true, "beforeY": performanceChatScrollStartY,
			"targetY": performanceChatScrollTargetY, "count": timeline.count,
			"targetSteps": performanceChatScrollTargetSteps }
	}

	function advancePerformanceChatScrollWorkload(step, totalSteps) {
		const nextStep = Math.floor(Number(step) || 0)
		const expectedSteps = Math.floor(Number(totalSteps) || 0)
		if (!performanceChatScrollRunning || expectedSteps !== performanceChatScrollTargetSteps
				|| nextStep !== performanceChatScrollCompletedSteps + 1
				|| nextStep > performanceChatScrollTargetSteps) {
			return { "advanced": false, "currentY": timeline.contentY,
				"completedSteps": performanceChatScrollCompletedSteps }
		}
		if (performanceChatScrollFullRangeToStart) {
			const previousY = timeline.contentY
			const currentOriginY = timeline.originY
			performanceChatScrollTargetY = currentOriginY
			// ListView can refine originY by sub-pixels while variable-height
			// delegates are pooled. Treat a device-pixel-sized remainder as the
			// real start, snap it exactly, and let the post-scroll top anchor own
			// any later geometry refinement.
			if (Math.abs(previousY - currentOriginY) <= 2.0) {
				timeline.contentY = currentOriginY
				return { "advanced": false, "reachedTarget": true,
					"currentY": timeline.contentY,
					"completedSteps": performanceChatScrollCompletedSteps }
			}
			// Advance one presented high-resolution wheel step at a time. Re-read
			// originY every frame because delegate estimates can refine the ListView
			// origin while the complete real history is traversed.
			// Match the real smooth-wheel handler while retaining one mutation per
			// presented frame. Re-reading originY above keeps the target adaptive
			// as variable-height rich delegates refine the live ListView geometry;
			// the old 8-16 px probe could spend its complete safety budget merely
			// chasing those refinements in an embed-heavy room.
			const wheelStep = Math.max(80, Math.min(112, timeline.height * 0.14))
			timeline.contentY = Math.max(currentOriginY, previousY - wheelStep)
			performanceChatScrollCompletedSteps = nextStep
			const firstVisible = timeline.firstVisibleMessageDelegate()
			return { "advanced": Math.abs(timeline.contentY - previousY) > 0.01,
				"reachedTarget": Math.abs(timeline.contentY - timeline.originY) <= 0.5,
				"currentY": timeline.contentY,
				"firstVisibleStableId": firstVisible ? String(firstVisible.stableId || "") : "",
				"firstVisibleHasPreview": firstVisible ? firstVisible.hasPreviewContent === true : false,
				"firstVisibleHasAttachment": firstVisible
					? firstVisible.hasAttachmentContent === true : false,
				"completedSteps": performanceChatScrollCompletedSteps }
		}
		const progress = nextStep / performanceChatScrollTargetSteps
		// Smoothstep preserves the old fluent acceleration curve while C++ owns
		// the cadence. Assigning contentY keeps this a genuine virtualized
		// ListView scroll, including delegate reuse and dynamic-height layout.
		const easedProgress = progress * progress * (3 - 2 * progress)
		const previousY = timeline.contentY
		timeline.contentY = performanceChatScrollStartY
			+ (performanceChatScrollTargetY - performanceChatScrollStartY) * easedProgress
		performanceChatScrollCompletedSteps = nextStep
		return { "advanced": Math.abs(timeline.contentY - previousY) > 0.01,
			"reachedTarget": Math.abs(timeline.contentY
				- performanceChatScrollTargetY) <= 0.5,
			"currentY": timeline.contentY, "completedSteps": performanceChatScrollCompletedSteps }
	}

	function completePerformanceChatScrollWorkload() {
		// Capture the exact visible row while lightweight scroll placeholders are
		// still active. Rich cards may refine their estimated height once the
		// scroll ends; the anchor keeps that refinement visually stationary.
		// Finalization can call this a second time after the frame-driven runner
		// already stopped. Never replace a live top anchor with a transient
		// pooled-delegate anchor during that idempotent cleanup.
		if (!timeline.topAnchorActive) {
			if (performanceChatScrollFullRangeToStart
					&& Math.abs(timeline.contentY - timeline.originY) <= 2.0) {
				timeline.contentY = timeline.originY
				timeline.beginTopAnchor(true)
			}
			else
				timeline.captureViewportAnchor()
		}
		performanceChatScrollRunning = false
		performanceChatScrollOwnershipActive = false
		performanceChatScrollFullRangeToStart = false
		// A programmatic workload has no pointer/flick movementEnded signal.
		// Wake only after the scrolling bindings have observed the stopped
		// state, then restore visible rich rows through the paced queues.
		Qt.callLater(function() {
			timeline.scheduleRichContentResume()
		})
	}

	function performanceChatFixtureState() {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		const firstVisible = timeline.firstVisibleMessageDelegate()
		const visiblePresentation = timeline.visiblePresentationDiagnostics()
		const visiblePresentationReady = timeline.visiblePresentationReady(
			visiblePresentation) || (timeline.scopePresentationForcedByDeadline
				&& timeline.visiblePresentationDeadlineReady(visiblePresentation))
		const visibleFrameCommitted = timeline.scopePresentationStartedAt <= 0
			|| timeline.scopePresentationVisibleFrameCommittedAt > 0
		const visibleHeightSettled = timeline.scopePresentationForcedByDeadline
			|| !timeline.visiblePresentationHeightConvergencePending()
		return { "count": timeline.count, "contentHeight": timeline.contentHeight,
			"viewportHeight": timeline.height, "originY": minimumY, "maximumY": maximumY,
			"contentY": timeline.contentY,
			"firstVisibleId": firstVisible ? String(firstVisible.stableId || "") : "",
			"delegateDiagnostics": timelineDelegateDiagnostics(),
			"settled": visibleFrameCommitted && visiblePresentationReady
				&& visibleHeightSettled
				&& !bottomFollowTimer.running && !timeline.scopeResetPending
				&& !timeline.scopePresentationPending && !timeline.scopePresentationFinalizing
				&& !timeline.restoringBottom && !timeline.prependAnchorActive
				&& !viewportMembershipTimer.running
				&& !timeline.viewportMembershipUpdatePending
				&& !performanceChatScrollRunning,
			"scrollable": timeline.count === 96 && maximumY - minimumY >= 8 }
	}

	function nearViewportPrefetchDiagnostics() {
		const stableId = String(timeline.nearViewportPrefetchActiveId
			|| timeline.nearViewportPrefetchReadyId || timeline.nearViewportWarmId || "")
		const item = stableId.length > 0
			? timeline.timelineDelegateForStableId(stableId) : null
		if (!item)
			return { "id": stableId, "delegateAvailable": false }
		const point = item.mapToItem(timeline, 0, 0)
		return {
			"id": stableId,
			"delegateAvailable": true,
			"viewportY": point.y,
			"height": item.height,
			"viewportProbeHeight": Number(item.viewportProbeHeight || 0),
			"inVisibleViewport": item.inVisibleViewport === true,
			"richContentViewportEligible":
				item.richContentViewportEligible === true,
			"contentNeedsHydration": item.contentNeedsHydration === true,
			"previewNeedsHydration": item.previewNeedsHydration === true,
			"messageRowReady": item.messageRowReady === true,
			"previewPresentationReady":
				item.previewPresentationReady === true,
			"richResourcesRequired": item.richResourcesRequired === true,
			"richResourcesActive": item.richResourcesActive === true,
			"richResourceActivationPending":
				item.richResourceActivationPending === true,
			"richLoaderSuspended": item.richLoaderSuspended === true,
			"previewMaterializationAdmitted":
				item.previewMaterializationAdmitted === true,
			"previewLoaderStatus": Number(item.previewLoaderStatus),
								"nativeThumbnailReady": item.nativeThumbnailReady === true,
			"previewLoaderSurfaceState":
				String(item.previewLoaderSurfaceState || "blank"),
			"previewLoaderIncubationWatchdogRunning":
				item.previewLoaderIncubationWatchdogRunning === true,
			"previewLoaderIncubationWatchdogStartedAt":
				Number(item.previewLoaderIncubationWatchdogStartedAt || 0),
			"previewLoaderIncubationSuppressed":
				item.previewLoaderIncubationSuppressed === true,
			"previewLoaderFailed": item.previewLoaderFailed === true,
			"nearViewportPrefetchTerminal":
				item.nearViewportPrefetchTerminal === true
		}
	}

	function timelinePresentationState() {
		if (conversationViewLoader.item) {
			const state = conversationViewLoader.item.presentationState()
			state.roomHeaderHeight = stonksRoomPanelLoader.height
			state.emptyConversationVisible = emptyConversationState.visible
			return state
		}
		try {
			const maximumY = timeline.tailContentYTarget()
			const firstVisible = timeline.firstVisibleMessageDelegate()
			const visiblePresentation = timeline.visiblePresentationDiagnostics()
			const pendingVisibleHydrationCount =
				timeline.pendingVisibleHydrationCount()
			const pendingVisibleRichRecoveryCount =
				timeline.pendingVisibleRichRecoveryCount()
			const completedAt = timeline.scopePresentationCompletedAt > 0
				? timeline.scopePresentationCompletedAt : Date.now()
			const dataReadyAt = timeline.scopePresentationDataReadyAt
			const stableReadyAt = timeline.scopePresentationStableReadyAt
			const firstStableFrameCommittedAt =
				timeline.scopePresentationFirstStableFrameCommittedAt
			const secondMatchingStableFrameCommittedAt =
				timeline.scopePresentationSecondMatchingStableFrameCommittedAt
			const firstVisibleStableRowsCommittedAt =
				timeline.scopePresentationFirstVisibleStableRowsCommittedAt
			const stableFrameCommittedAt =
				timeline.scopePresentationStableFrameCommittedAt
			const visibleFrameCommittedAt =
				timeline.scopePresentationVisibleFrameCommittedAt
			const probeAnchor = timeline.historyLoadProbeAnchorState()
			const prependSettlementGeometry =
				timeline.prependSettlementGeometrySnapshot()
			const timelineViewportOrigin = timeline.mapToItem(root.contentItem, 0, 0)
			const roomHeaderOrigin = stonksRoomPanelLoader.mapToItem(
				root.contentItem, 0, 0)
			const composerOrigin = composerSurface.mapToItem(root.contentItem, 0, 0)
			const jumpToLatestOrigin = jumpToLatestButton.mapToItem(
				root.contentItem, 0, 0)
			const aboveComposerTickerOrigin = aboveComposerStonksTickerLoader.mapToItem(
				root.contentItem, 0, 0)
			const currentTimelineHeader = timeline.headerItem
			const historyLoader = currentTimelineHeader
				? currentTimelineHeader.olderHistoryControl : null
			const historyLoaderOrigin = historyLoader
				? historyLoader.mapToItem(root.contentItem, 0, 0)
				: ({ "x": 0, "y": 0 })
			let visibleTailBottom = 0
			for (let rowIndex = 0; rowIndex < visiblePresentation.rows.length; ++rowIndex) {
				const row = visiblePresentation.rows[rowIndex] || ({})
				visibleTailBottom = Math.max(visibleTailBottom,
					Number(row.viewportY || 0) + Number(row.height || 0))
			}
			const expectedTailGap = timeline.footerItem
				? Number(timeline.footerItem.height || 0) : 0
			const visibleTailGap = visibleTailBottom > 0
				? Math.max(0, timeline.height - visibleTailBottom) : 0
			const sparseBottomAlignmentError = chatModel.count > 0
				&& chatModel.count <= 10
				? Math.abs(visibleTailBottom
					- (timeline.height - expectedTailGap)) : 0
			const sparseTailContentYError = timeline.sparseBottomAlignmentActive
				? Math.abs(timeline.contentY - maximumY) : 0
			const visiblePresentationComplete =
				timeline.visiblePresentationReady(visiblePresentation)
				|| (timeline.scopePresentationForcedByDeadline
					&& timeline.visiblePresentationDeadlineReady(visiblePresentation))
			const stablePresentationReady = stableReadyAt > 0
			const richHydrated = timeline.scopePresentationRichHydratedAt > 0
			const degradedReady = timeline.scopePresentationDegradedReadyAt > 0
			return {
				"scopeToken": String(activeScope.scopeToken || ""),
				"scopeLabel": String(activeScope.label || ""),
				"count": chatModel.count,
				"richPresentationCount": timeline.count,
				"contentHeight": timeline.contentHeight,
				"viewportHeight": timeline.height,
				"viewportX": Number(timelineViewportOrigin.x || 0),
				"viewportY": Number(timelineViewportOrigin.y || 0),
				"viewportWidth": timeline.width,
				"viewportBottom": Number(timelineViewportOrigin.y || 0)
					+ Number(timeline.height || 0),
				"roomHeaderHeight": stonksRoomPanelLoader.height,
				"roomHeaderBottom": Number(roomHeaderOrigin.y || 0)
					+ Number(stonksRoomPanelLoader.height || 0),
				"composerHeight": composerSurface.height,
				"composerTop": Number(composerOrigin.y || 0),
				"jumpToLatestVisible": jumpToLatestButton.visible,
				"jumpToLatestTop": Number(jumpToLatestOrigin.y || 0),
				"jumpToLatestHeight": jumpToLatestButton.height,
				"jumpToLatestBottom": Number(jumpToLatestOrigin.y || 0)
					+ Number(jumpToLatestButton.height || 0),
				"jumpToLatestTopMargin": jumpToLatestButton.timelineTopMargin,
				"jumpToLatestBottomMargin": jumpToLatestButton.timelineBottomMargin,
				"jumpToLatestRowHeight": jumpToLatestRow.height,
				"aboveComposerTickerVisible": aboveComposerStonksTickerLoader.height > 1,
				"aboveComposerTickerTop": Number(aboveComposerTickerOrigin.y || 0),
				"aboveComposerTickerHeight": aboveComposerStonksTickerLoader.height,
				"aboveComposerTickerBottom": Number(aboveComposerTickerOrigin.y || 0)
					+ Number(aboveComposerStonksTickerLoader.height || 0),
				"historyLoaderVisible": !!historyLoader && historyLoader.visible,
				"historyLoaderTop": Number(historyLoaderOrigin.y || 0),
				"historyLoaderHeight": historyLoader ? historyLoader.height : 0,
				"historyLoaderBottom": Number(historyLoaderOrigin.y || 0)
					+ Number(historyLoader ? historyLoader.height : 0),
				"timelineHeaderHeight": currentTimelineHeader
					? currentTimelineHeader.height : 0,
				"baseTopMargin": timeline.baseTopMargin,
				"topMargin": timeline.topMargin,
				"footerHeight": expectedTailGap,
				"sparseBottomAlignmentActive":
					timeline.sparseBottomAlignmentActive,
				"visibleTailBottom": visibleTailBottom,
				"visibleTailGap": visibleTailGap,
				"sparseBottomAlignmentError": sparseBottomAlignmentError,
				"sparseTailContentYTarget": maximumY,
				"sparseTailContentYError": sparseTailContentYError,
				"emptyConversationVisible": emptyConversationState.visible,
				"originY": timeline.originY,
				"contentY": timeline.contentY,
				"maximumY": maximumY,
				"firstVisibleId": firstVisible ? String(firstVisible.stableId || "") : "",
				"firstVisibleOffset": firstVisible
					? Number(firstVisible.y - timeline.contentY) : 0,
				"probeAnchorId": probeAnchor.id,
				"probeAnchorOffset": probeAnchor.offset,
				"probeAnchorVisible": probeAnchor.visible,
				"viewportAnchorActive": timeline.viewportAnchorActive,
				"viewportAnchorId": timeline.viewportAnchorId,
				"viewportAnchorOffset": timeline.viewportAnchorOffset,
				"viewportAnchorReleasePending": timeline.viewportAnchorReleasePending,
				"viewportAnchorQuietReleaseActive":
					timeline.viewportAnchorQuietReleaseActive,
				"viewportAnchorQuietReleaseGeneration":
					timeline.viewportAnchorQuietReleaseGeneration,
				"restoringViewportAnchor": timeline.restoringViewportAnchor,
				"presentationPending": timeline.scopePresentationPending,
				"presentationFinalizing": timeline.scopePresentationFinalizing,
				"scopeResetPending": timeline.scopeResetPending,
				"presentationFastPath": timeline.scopePresentationFastPath,
				"presentationContentVisible":
					timeline.scopePresentationContentVisible,
				"fastPathAttemptCount":
					timeline.scopePresentationFastPathAttemptCount,
				"firstFastPathBlocker":
					timeline.scopePresentationFirstFastPathBlocker,
				"lastFastPathBlocker":
					timeline.scopePresentationLastFastPathBlocker,
				"observationActive": timeline.scopePresentationObservationActive,
				"forcedByDeadline": timeline.scopePresentationForcedByDeadline,
				"dataReady": dataReadyAt > 0,
				"stablePresentationReady": stablePresentationReady,
				"firstStableFrameCommitted": firstStableFrameCommittedAt > 0,
				"secondMatchingStableFrameCommitted":
					secondMatchingStableFrameCommittedAt > 0,
				"firstVisibleStableRowsCommitted":
					firstVisibleStableRowsCommittedAt > 0,
				"stableFrameCommitted": stableFrameCommittedAt > 0,
				"visibleFrameCommitted": visibleFrameCommittedAt > 0,
				"stableFrameConsecutiveCount":
					timeline.scopePresentationStableFrameCount,
				"richHydrated": richHydrated,
				"degradedReady": degradedReady,
				"loadingFrameCommitted":
					timeline.scopePresentationLoadingFrameCommittedAt > 0,
				"composerInteractive":
					timeline.scopePresentationComposerInteractiveAt > 0,
				"startedAt": timeline.scopePresentationStartedAt,
				"dataReadyAt": dataReadyAt,
				"stableReadyAt": stableReadyAt,
				"firstStableFrameCommittedAt": firstStableFrameCommittedAt,
				"secondMatchingStableFrameCommittedAt":
					secondMatchingStableFrameCommittedAt,
				"firstVisibleStableRowsCommittedAt":
					firstVisibleStableRowsCommittedAt,
				"stableFrameCommittedAt": stableFrameCommittedAt,
				"visibleFrameCommittedAt": visibleFrameCommittedAt,
				"loadingFrameCommittedAt":
					timeline.scopePresentationLoadingFrameCommittedAt,
				"composerInteractiveAt":
					timeline.scopePresentationComposerInteractiveAt,
				"richHydratedAt": timeline.scopePresentationRichHydratedAt,
				"degradedReadyAt": timeline.scopePresentationDegradedReadyAt,
				"providerResultAppliedAt":
					timeline.scopePresentationProviderResultAppliedAt,
				"providerResultVisiblePaintAt":
					timeline.scopePresentationProviderResultVisiblePaintAt,
				"providerResultSerial":
					timeline.scopePresentationProviderResultSerial,
				"dataReadyDurationMs": timeline.scopePresentationStartedAt > 0
					&& dataReadyAt > 0
					? Math.max(0, dataReadyAt - timeline.scopePresentationStartedAt) : 0,
				"stableReadyDurationMs": timeline.scopePresentationStartedAt > 0
					&& stableReadyAt > 0
					? Math.max(0, stableReadyAt - timeline.scopePresentationStartedAt) : 0,
				"stableFrameDurationMs": timeline.scopePresentationStartedAt > 0
					&& stableFrameCommittedAt > 0
					? Math.max(0, stableFrameCommittedAt
						- timeline.scopePresentationStartedAt) : 0,
				"visibleFrameDurationMs": timeline.scopePresentationStartedAt > 0
					&& visibleFrameCommittedAt > 0
					? Math.max(0, visibleFrameCommittedAt
						- timeline.scopePresentationStartedAt) : 0,
				"firstStableFrameDurationMs": timeline.scopePresentationStartedAt > 0
					&& firstStableFrameCommittedAt > 0
					? Math.max(0, firstStableFrameCommittedAt
						- timeline.scopePresentationStartedAt) : 0,
				"secondMatchingStableFrameDurationMs":
					timeline.scopePresentationStartedAt > 0
						&& secondMatchingStableFrameCommittedAt > 0
						? Math.max(0, secondMatchingStableFrameCommittedAt
							- timeline.scopePresentationStartedAt) : 0,
				"firstVisibleStableRowsDurationMs":
					timeline.scopePresentationStartedAt > 0
						&& firstVisibleStableRowsCommittedAt > 0
						? Math.max(0, firstVisibleStableRowsCommittedAt
							- timeline.scopePresentationStartedAt) : 0,
				"loadingFrameDurationMs": timeline.scopePresentationStartedAt > 0
					&& timeline.scopePresentationLoadingFrameCommittedAt > 0
					? Math.max(0, timeline.scopePresentationLoadingFrameCommittedAt
						- timeline.scopePresentationStartedAt) : 0,
				"composerInteractiveDurationMs": timeline.scopePresentationStartedAt > 0
					&& timeline.scopePresentationComposerInteractiveAt > 0
					? Math.max(0, timeline.scopePresentationComposerInteractiveAt
						- timeline.scopePresentationStartedAt) : 0,
				"richHydratedDurationMs": timeline.scopePresentationStartedAt > 0
					&& timeline.scopePresentationRichHydratedAt > 0
					? Math.max(0, timeline.scopePresentationRichHydratedAt
						- timeline.scopePresentationStartedAt) : 0,
				"degradedReadyDurationMs": timeline.scopePresentationStartedAt > 0
					&& timeline.scopePresentationDegradedReadyAt > 0
					? Math.max(0, timeline.scopePresentationDegradedReadyAt
						- timeline.scopePresentationStartedAt) : 0,
				"providerResultToVisiblePaintMs":
					timeline.scopePresentationProviderResultAppliedAt > 0
						&& timeline.scopePresentationProviderResultVisiblePaintAt > 0
						? timeline.scopePresentationProviderResultVisiblePaintAt
							- timeline.scopePresentationProviderResultAppliedAt : 0,
				"outerLoaderLoadedCount":
					timeline.scopePresentationOuterLoaderLoadedCount,
				"firstOuterLoaderLoadedDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationFirstOuterLoaderLoadedAt),
				"lastOuterLoaderLoadedDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationLastOuterLoaderLoadedAt),
				"plainRowAdmissionCount":
					timeline.scopePresentationPlainRowAdmissionCount,
				"plainLoaderEvents": timeline.scopePresentationPlainLoaderEvents,
				"firstPlainRowAdmissionDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationFirstPlainRowAdmissionAt),
				"lastPlainRowAdmissionDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationLastPlainRowAdmissionAt),
				"rowLoaderLoadedCount":
					timeline.scopePresentationRowLoaderLoadedCount,
				"firstRowLoaderLoadedDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationFirstRowLoaderLoadedAt),
				"lastRowLoaderLoadedDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationLastRowLoaderLoadedAt),
				"heightCommitCount": timeline.scopePresentationHeightCommitCount,
				"firstHeightCommitDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationFirstHeightCommitAt),
				"lastHeightCommitDurationMs":
					timeline.scopePresentationPhaseDuration(
						timeline.scopePresentationLastHeightCommitAt),
				"generation": timeline.scopePresentationGeneration,
				"frameSwappedSerial": timeline.frameSwappedSerial,
				"mutationCount": timeline.scopePresentationMutationCount,
				"tailCorrectionCount": timeline.scopePresentationTailCorrectionCount,
				"exposedHeightChangeCount": timeline.scopePresentationExposedHeightChangeCount,
				"exposedTailCorrectionCount": timeline.scopePresentationExposedTailCorrectionCount,
				"exposedTailTravel": timeline.scopePresentationExposedTailTravel,
				"pendingHydrationCount": timeline.pendingScopeHydrationCount(),
				"pendingVisibleHydrationCount": pendingVisibleHydrationCount,
				"pendingVisibleRichRecoveryCount":
					pendingVisibleRichRecoveryCount,
				"prependAnchorActive": timeline.prependAnchorActive,
				"prependAnchorId": timeline.prependAnchorId,
				"prependAnchorOffset": timeline.prependAnchorOffset,
				"prependAnchorTargetY": timeline.prependAnchorTargetY,
				"prependAnchorInsertedExtent": timeline.prependAnchorInsertedExtent,
				"prependQueuePending": chatModel.prependPending,
				"prependSettlementState": timeline.prependSettlementState,
				"prependSettlementTransactionId":
					timeline.prependSettlementTransactionId,
				"prependSettlementScopeToken":
					timeline.prependSettlementScopeToken,
				"prependSettlementPresentationGeneration":
					timeline.prependSettlementPresentationGeneration,
				"prependSettlementExpectedOrderedIdHash":
					timeline.prependSettlementExpectedOrderedIdHash,
				"prependSettlementExpectedMessageCount":
					timeline.prependSettlementExpectedMessageCount,
				"prependSettlementAnchorId": timeline.prependSettlementAnchorId,
				"prependSettlementExpectedAnchorOffset":
					timeline.prependSettlementExpectedAnchorOffset,
				"prependSettlementReceiptSequence":
					timeline.prependSettlementReceiptSequence,
				"prependSettlementReceipt": timeline.prependSettlementReceipt,
				"prependSettlementCurrentGeometryFingerprint":
					prependSettlementGeometry.fingerprint,
				"appendQueuePending": chatModel.appendPending,
				"chatModelPresentationBusy": chatModel.presentationBusy,
				"deferredPresentationUpdatesPending":
					chatModel.deferredPresentationUpdatesPending,
				"deferredPrependRichCount":
					Object.keys(timeline.deferredPrependRichIds || ({})).length,
				"richLoaderResumeQueueCount": timeline.richLoaderResumeQueue.length,
				"richLoaderResumeActiveId": timeline.richLoaderResumeActiveId,
				"richContentResumeRunning": richContentResumeTimer.running,
				"richLoaderResumeRunning": richLoaderResumeTimer.running,
				"nearViewportPrefetchActiveId": timeline.nearViewportPrefetchActiveId,
				"nearViewportWarmId": timeline.nearViewportWarmId,
				"nearViewportPrefetchReadyId": timeline.nearViewportPrefetchReadyId,
				"nearViewportPrefetchAllowed": timeline.nearViewportPrefetchAllowed(),
				"nearViewportPrefetchCandidate": timeline.nearestDeferredPrefetchCandidate(),
				"coldPreviewHydrationPacingActive": root.coldPreviewHydrationPacingActive,
				"nearViewportPrefetch": nearViewportPrefetchDiagnostics(),
				"staticPreviewPreheatActiveId": timeline.staticPreviewPreheatActiveId,
				"staticPreviewPreheatQueueCount": timeline.staticPreviewPreheatQueue.length,
				"staticPreviewPreheatStatus": Number(staticPreviewPreheater.status),
				"topAnchorActive": timeline.topAnchorActive,
				"topAnchorProgrammaticMovementGuard":
					timeline.topAnchorProgrammaticMovementGuard,
				"stickToBottom": timeline.stickToBottom,
				"moving": timeline.moving,
				"dragging": timeline.dragging,
				"flicking": timeline.flicking,
				"scrollHandlerScrolling": timelineScrollHandler.scrolling,
				"scrollHandlerGestureActive": timelineScrollHandler.gestureActive,
				"scrollHandlerNavigationTransitionActive":
					timelineScrollHandler.navigationTransitionActive,
				"scrollbarPressed": timelineScrollBar.pressed,
				"scrollbarDragActive": timeline.scrollbarDragActive,
				"scrollbarReleasePending": timeline.scrollbarReleasePending,
				"scrollbarReleaseGuardRunning": scrollbarReleaseGuardTimer.running,
				"wheelInputObservedCount": root.timelineWheelInputObservedCount,
				"wheelObservedNotchUnits": root.timelineWheelObservedNotchUnits,
				"wheelObservedSignedNotchBalance":
					root.timelineWheelObservedSignedNotchBalance,
				"wheelInputAdmittedNotchUnits":
					root.timelineWheelInputAdmittedNotchUnits,
				"wheelInputDroppedNotchUnits":
					root.timelineWheelInputDroppedNotchUnits,
				"wheelInputBlockedCount": root.timelineWheelInputBlockedCount,
				"wheelBlockedNotchUnits": root.timelineWheelBlockedNotchUnits,
				"wheelBlockedSignedNotchBalance":
					root.timelineWheelBlockedSignedNotchBalance,
				"wheelBlockedPrecisionCount": root.timelineWheelBlockedPrecisionCount,
				"scrollbarReleaseWheelTakeoverCount":
					root.timelineScrollbarReleaseWheelTakeoverCount,
				"wheelExpiredDistance": root.timelineWheelExpiredDistance,
				"explicitWheelInputPending": timeline.explicitWheelInputPending,
				"explicitNavigationMovement": timeline.explicitNavigationMovement,
				"lastMovementClassification": timeline.lastMovementClassification,
				"explicitNavigationMovementCount":
					timeline.explicitNavigationMovementCount,
				"automaticLayoutMovementCount":
					timeline.automaticLayoutMovementCount,
				"deferredMaterializationQueueCount":
					timeline.deferredMaterializationQueue.length,
				"deferredMaterializationActiveId":
					timeline.deferredMaterializationActiveId,
				"deferredMaterializationActiveGeneration":
					timeline.deferredMaterializationActiveGeneration,
				"plainDeferredMaterializationActiveCount":
					timeline.plainDeferredMaterializationActiveCount(),
				"plainDeferredMaterializationActiveIds": Object.keys(
					timeline.plainDeferredMaterializationActive).sort(),
				"plainDeferredSourceAdmissionCount":
					timeline.plainDeferredSourceAdmissionCount,
				"serializedDeferredSourceAdmissionCount":
					timeline.serializedDeferredSourceAdmissionCount,
				"deferredMaterializationRunning": deferredMaterializationTimer.running,
				"coldMaterializationDelayRunning":
					coldScopeMaterializationDelayTimer.running,
				"viewportMembershipUpdateRunning":
					viewportMembershipTimer.running,
				"viewportMembershipUpdatePending":
					timeline.viewportMembershipUpdatePending,
				"presentationFinalizeQuietRunning":
					scopePresentationFinalizeQuietTimer.running,
				"presentationFinalizeDeadlineRunning":
					scopePresentationFinalizeDeadlineTimer.running,
				"visiblePresentationCount": visiblePresentation.visibleCount,
				"visibleReadyPresentationCount": visiblePresentation.readyCount,
				"visibleFullMessageCount": visiblePresentation.fullMessageCount,
				"visibleDeadlineFallbackCount":
					visiblePresentation.deadlineFallbackCount,
				"visibleDeferredSummaryCount": visiblePresentation.deferredSummaryCount,
				"visibleBlankPresentationCount": visiblePresentation.blankCount,
				"visibleOuterLoaderReadyCount": visiblePresentation.outerLoaderReadyCount,
				"visibleRowMaterializedCount": visiblePresentation.rowMaterializedCount,
				"visibleRowLoaderReadyCount": visiblePresentation.rowLoaderReadyCount,
				"visiblePreviewReadyCount": visiblePresentation.previewReadyCount,
				"visibleBlankPreviewSurfaceCount":
					visiblePresentation.blankPreviewSurfaceCount,
				"visiblePaintedSummaryCount":
					visiblePresentation.paintedSummaryCount,
				"visiblePaintedSurfaceCount":
					visiblePresentation.paintedSurfaceCount,
				"visiblePaintedCompleteCount":
					visiblePresentation.paintedCompleteCount,
				"visiblePaintedLoadingCount":
					visiblePresentation.paintedLoadingCount,
				"visiblePaintedTerminalCount":
					visiblePresentation.paintedTerminalCount,
				"visibleDuplicateHandoffSummaryCount":
					visiblePresentation.duplicateHandoffSummaryCount,
				"stationaryThumbWarmPressEpoch": stationaryThumbWarmGate.pressEpoch,
				"stationaryThumbWarmDwellGeneration":
					stationaryThumbWarmGate.dwellGeneration,
				"stationaryThumbWarmAdmittedCount":
					stationaryThumbWarmGate.admittedCount,
				"stationaryThumbWarmActiveId": stationaryThumbWarmGate.activeStableId,
				"stationaryThumbWarmPinnedIds":
					Object.keys(timeline.stationaryThumbWarmPins),
				"stationaryThumbWarmPins": timeline.stationaryThumbWarmPins,
				"stationaryThumbWarmReconcilePending":
					timeline.stationaryThumbWarmReconcilePending,
				"stationaryThumbWarmReconcileBlocked":
					timeline.stationaryThumbWarmReconcileBlockerActive,
				"visibleInactiveRichResourceCount":
					visiblePresentation.inactiveRichResourceCount,
				"visiblePresentationHeightPendingCount":
					visiblePresentation.presentationHeightPendingCount,
				"visiblePresentationHeightMismatchCount":
					visiblePresentation.presentationHeightMismatchCount,
				"maximumVisiblePresentationHeightError":
					visiblePresentation.maximumPresentationHeightError,
				"visibleRows": visiblePresentation.rows,
				"durationMs": timeline.scopePresentationStartedAt > 0
					? Math.max(0, completedAt - timeline.scopePresentationStartedAt) : 0,
				// The top anchor is a transparent geometry guard while suspended rich
				// rows wake incrementally. Do not report the visible timeline as busy
				// for that background work; callers still verify exact originY and
				// stable visible-row geometry independently.
				// Observation only records late exposed corrections. It must not
				// make an already visible and interactive scope report unready.
				"settled": visibleFrameCommittedAt > 0
					&& visiblePresentationComplete
					&& !timeline.scopePresentationPending && !timeline.scopePresentationFinalizing
					&& !timeline.scopeResetPending && !bottomFollowTimer.running
					&& !timeline.restoringBottom && !timeline.prependAnchorActive
					&& !timeline.viewportAnchorReleasePending
					&& !timeline.viewportAnchorQuietReleaseActive
					&& !timeline.explicitNavigationMovement
					&& !timeline.scrollbarDragActive && !timeline.scrollbarReleasePending
					&& !timelineScrollBar.pressed && !timelineScrollHandler.scrolling
					&& !timelineScrollHandler.gestureActive
					&& !timelineScrollHandler.navigationTransitionActive
					&& !viewportMembershipTimer.running
					&& !timeline.viewportMembershipUpdatePending
					&& !chatModel.appendPending && !chatModel.prependPending
					&& timeline.stationaryThumbWarmPinCount === 0
					&& (timeline.scopePresentationForcedByDeadline
						|| (visiblePresentation.presentationHeightPendingCount === 0
							&& visiblePresentation.presentationHeightMismatchCount === 0))
			}
		} catch (error) {
			return { "error": String(error), "settled": false }
		}
	}

	function prepareHistoryLoadPerformanceProbe() {
		if (timeline.count <= 0)
			return { "prepared": false, "reason": "The active timeline is empty." }
		if (Math.abs(timeline.contentY - timeline.originY) > 0.5)
			return { "prepared": false,
				"reason": "The active timeline has not been scrolled completely to the start." }
		timeline.releasePrependAnchor()
		timeline.releaseViewportAnchor()
		timeline.invalidateBottomFollow()
		timeline.followTailAfterInsert = false
		timeline.pendingTailInsertCount = 0
		timeline.pendingTailMessageCount = 0
		timeline.stickToBottom = false
		timeline.forceLayout()
		const firstVisible = timeline.firstVisibleMessageDelegate()
		timeline.performanceHistoryProbeAnchorId = firstVisible
			? String(firstVisible.stableId || "") : ""
		return {
			"prepared": firstVisible !== null,
			"count": chatModel.count,
			"contentY": timeline.contentY,
			"firstVisibleId": timeline.performanceHistoryProbeAnchorId,
			"firstVisibleOffset": firstVisible
				? Number(firstVisible.mapToItem(timeline, 0, 0).y) : 0
		}
	}

	function requestOlderMessagesPreservingAnchor() {
		if (!timeline.scopeResetPending && !timeline.stickToBottom
				&& !timeline.prependAnchorActive) {
			timeline.prependMutationInProgress = timeline.capturePrependAnchor()
			timeline.deferredPrependAnchorActive = timeline.prependMutationInProgress
		}
		uiCommands.requestOlderMessages()
	}

	function performanceChatScrollState() {
		const currentDiagnostics = timelineDelegateDiagnostics()
		const beforeDiagnostics = performanceChatScrollDiagnosticsBefore || ({})
		return { "beforeY": performanceChatScrollStartY, "currentY": timeline.contentY,
			"targetY": performanceChatScrollTargetY,
			"moved": Math.abs(timeline.contentY - performanceChatScrollStartY) > 1,
			"running": performanceChatScrollRunning,
			"completedSteps": performanceChatScrollCompletedSteps,
			"targetSteps": performanceChatScrollTargetSteps,
			"retainedRichResourceIds":
				Object.keys(performanceChatScrollRetainedRichIds || ({})),
			"retainedRichResourceDropCount":
				performanceChatScrollRetainedRichResourceDropCount,
			"delegateDiagnosticsBefore": beforeDiagnostics,
			"delegateDiagnostics": currentDiagnostics,
			"delegateDiagnosticsDelta": {
				"created": currentDiagnostics.created - Number(beforeDiagnostics.created || 0),
				"pooled": currentDiagnostics.pooledEvents - Number(beforeDiagnostics.pooledEvents || 0),
				"reused": currentDiagnostics.reusedEvents - Number(beforeDiagnostics.reusedEvents || 0),
				"previewsLoaded": currentDiagnostics.previewsLoaded - Number(beforeDiagnostics.previewsLoaded || 0),
				"attachmentsLoaded": currentDiagnostics.attachmentsLoaded - Number(beforeDiagnostics.attachmentsLoaded || 0)
			} }
	}

	function timelineDelegateDiagnostics() {
		const children = timeline && timeline.contentItem ? timeline.contentItem.children : []
		const visiblePresentation = timeline.visiblePresentationDiagnostics()
		let materialized = 0
		let pooled = 0
		let active = 0
		let previewRows = 0
		let previewItems = 0
		let attachmentRows = 0
		let attachmentItems = 0
		for (let index = 0; index < children.length; ++index) {
			const child = children[index]
			if (!child || child.performanceTimelineDelegate !== true)
				continue
			++materialized
			if (child.accessibilityPooled)
				++pooled
			else
				++active
			if (child.hasPreviewContent)
				++previewRows
			if (child.performancePreviewMaterialized)
				++previewItems
			if (child.hasAttachmentContent)
				++attachmentRows
			if (child.performanceAttachmentMaterialized)
				++attachmentItems
		}
		return {
			"materialized": materialized, "active": active, "pooled": pooled,
			"previewRows": previewRows, "previewItems": previewItems,
			"attachmentRows": attachmentRows, "attachmentItems": attachmentItems,
			"contentItemChildren": children.length,
			"cacheBuffer": timeline ? timeline.cacheBuffer : 0,
			"created": performanceChatDelegateCreatedCount,
			"pooledEvents": performanceChatDelegatePooledCount,
			"reusedEvents": performanceChatDelegateReusedCount,
			"previewsLoaded": performanceChatPreviewLoadedCount,
			"attachmentsLoaded": performanceChatAttachmentLoadedCount,
			"visiblePresentationCount": visiblePresentation.visibleCount,
			"visibleReadyPresentationCount": visiblePresentation.readyCount,
			"visibleFullMessageCount": visiblePresentation.fullMessageCount,
			"visibleDeferredSummaryCount": visiblePresentation.deferredSummaryCount,
			"visibleBlankPresentationCount": visiblePresentation.blankCount
		}
	}

    Timer {
        id: previewHydrationTimer
        interval: 16
        repeat: false
        onTriggered: root.flushPreviewHydrationQueue()
    }

	Popup {
		id: globalQuickReactionPopup
		objectName: "globalMessageQuickReactionPopup"
		parent: Overlay.overlay
		modal: false
		focus: true
		padding: Theme.space1
		margins: Theme.space2
		width: globalQuickReactionBar.implicitWidth + leftPadding + rightPadding
		height: globalQuickReactionBar.implicitHeight + topPadding + bottomPadding
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		background: Rectangle {
			radius: height / 2
			color: Theme.surfaceRaised
			border.color: Theme.divider
			border.width: 1
		}
		contentItem: QuickReactionBar {
			id: globalQuickReactionBar
			objectName: "globalMessageQuickReactionBar"
			expanded: globalQuickReactionPopup.opened
			activeReactions: root.quickReactionActiveReactions
			onReactionRequested: emoji => {
				const targetId = root.quickReactionMessageId
				root.closeQuickReactions()
				if (targetId.length > 0)
					uiCommands.toggleMessageReaction(targetId, emoji)
			}
		}
		onClosed: root.resetQuickReactionState()
	}

    Connections {
        target: activeScope
		function onScopeTokenChanged() {
			timeline.beginScopeChange()
			root.closeQuickReactions()
			root.clearPreviewHydrationQueue()
			if (root.conversationSearchOpen)
				root.closeConversationSearch(false)
		}
		function onLoadingChanged() {
			const generation = timeline.scopePresentationGeneration
			const scopeToken = String(activeScope.scopeToken || "")
			// Scope loading is published immediately before the matching model DTOs.
			// Defer one GUI turn so the readiness probe observes the target queue or
			// completed direct replacement, then fence rapid A -> B navigation.
			Qt.callLater(function() {
				if (generation !== timeline.scopePresentationGeneration
						|| scopeToken !== String(activeScope.scopeToken || ""))
					return
				timeline.noteScopePresentationDataReady()
				timeline.scheduleScopePresentationFastPath()
			})
		}
		function onUnreadCountChanged() {
			if (Number(activeScope.unreadCount || 0) > 0)
				Qt.callLater(function() { timeline.markVisibleScopeReadIfEligible() })
		}
    }

	Shortcut {
		sequence: StandardKey.Find
		context: Qt.WindowShortcut
		enabled: !root.modalUiActive
		onActivated: root.openConversationSearch()
	}

    Component {
        id: screenShareViewComponent
        ScreenShareViewWindow { }
    }

	ProductDialogWindow {
		id: productDialog
		controller: dialogState
		parentWindow: root
		visualFixtureMode: root.visualFixtureOverrideActive
		beforeOpen: function() {
			root.closeProductMenus()
			if (root.navigationModalActive) {
				navigationDrawer.close()
				return false
			}
			return true
		}
	}
	Connections {
		target: productDialog
		function onOpenedWindow() { root.closeProductMenus() }
	}
	SettingsWindow {
		id: settingsWindow
		controller: dialogState
		parentWindow: root
		visualFixtureMode: root.visualFixtureOverrideActive
		beforeOpen: function() {
			root.closeProductMenus()
			if (root.navigationModalActive)
				navigationDrawer.close()
			return true
		}
	}

	ModalAccessibilityBarrier {
		id: modalAccessibilityBarrier
		objectName: "modalAccessibilityBarrier"
		active: root.backgroundAccessibilitySuppressed
		// Popup content is reparented to Overlay.overlay. The active dialog or
		// drawer therefore stays outside these background targets and owns the
		// complete semantic tree while the product scene remains visible.
		targets: [ productSurface, toastPill ]
	}
	Timer {
		id: timelineAccessibilityReassertionTimer
		objectName: "timelineAccessibilityReassertionTimer"
		interval: 16
		repeat: true
		triggeredOnStart: true
		running: root.backgroundAccessibilitySuppressed
		onTriggered: root.refreshTimelineVirtualizedAccessibility()
	}

	onVisualFixtureOverrideActiveChanged: {
		if (visualFixtureOverrideActive)
			clearPreviewHydrationQueue()
	}
    Component {
        id: imageViewerComponent
        ImageViewer {
			controller: dialogState
			transientParent: root
		}
    }
    Loader {
        active: dialogState.open && dialogState.kind === "imageViewer"
        sourceComponent: imageViewerComponent
    }
    Component {
        id: attachmentViewerComponent
		AttachmentViewer {
			attachment: root.attachmentViewerPayload || ({})
			transientParent: root
			onSaveRequested: attachment => root.downloadAttachment(attachment, false)
			onOriginalRetryRequested: attachment => root.retryAttachmentOriginal(attachment)
			onRefreshRequested: attachment => root.queuePreviewHydration(
				String(attachment.hydrationMessageId || ""), true)
			onClosing: {
				root.attachmentViewerPayload = null
				root.attachmentViewerHydrationModel = null
			}
        }
    }
    Loader {
        active: root.attachmentViewerPayload !== null
        sourceComponent: attachmentViewerComponent
    }
    // Keep the isolated media QML plugin out of the main-shell import graph. The
    // media window (and therefore Chromium) is resolved only after an explicit
    // media-session action makes the backend active, and is destroyed again
    // when that session closes.
    Loader {
        id: mediaSessionWindowLoader
		// Start the detached native window once the Windows delay-load worker has
		// entered a concrete preparing/ready/error state. Keep that presentation
		// alive through retry so runtime failures remain actionable. The provider
		// The isolated provider renderer inside MediaSessionWindow stays independently gated by
		// mediaRuntimeReady and is therefore never instantiated during preparation
		// or while the runtime is in its retryable error state.
		property bool runtimePresentationStarted: false
		active: root.mediaSessionWindowRequested && runtimePresentationStarted
        asynchronous: true
		function syncRuntimePresentation() {
			if (!root.mediaSessionWindowRequested) {
				runtimePresentationStarted = false
				return
			}
			if (!root.mediaRuntimeContractAvailable || root.mediaRuntimeReady
					|| root.mediaRuntimePreparing || root.mediaRuntimeError.length > 0)
				runtimePresentationStarted = true
		}
		function updateSource() {
			source = active ? root.mediaSessionWindowComponentUrl : ""
		}
		onActiveChanged: updateSource()
		onLoaded: {
			item.transientParent = root
			item.visualFixtureMode = root.visualMediaFixtureMode
		}
		onStatusChanged: if (status === Loader.Error && active)
			root.reportMediaSessionWindowComponentFailure()
		Component.onCompleted: {
			syncRuntimePresentation()
			updateSource()
		}
		Binding {
			target: mediaSessionWindowLoader.item
			property: "visualFixtureMode"
			value: root.visualMediaFixtureMode
			when: mediaSessionWindowLoader.item !== null
		}
    }

	Popup {
		id: mediaWindowComponentFailurePopup
		objectName: "mediaSessionWindowComponentFailure"
		parent: Overlay.overlay
		x: Math.round((parent.width - width) / 2)
		y: Math.round((parent.height - height) / 2)
		width: Math.min(parent.width - Theme.space6 * 2, 520)
		height: Math.min(parent.height - Theme.space6 * 2, mediaWindowFailureContent.implicitHeight + Theme.space6 * 2)
		visible: root.mediaSessionWindowUnavailable
		modal: true
		dim: true
		focus: true
		closePolicy: Popup.NoAutoClose
		onAboutToShow: root.rememberMediaWindowFailureFocus()
		onOpened: Qt.callLater(function() {
			const candidates = [ mediaWindowComponentRetryButton,
				mediaWindowComponentExternalButton, mediaWindowComponentCloseButton ]
			for (let index = 0; index < candidates.length; ++index) {
				const candidate = candidates[index]
				if (candidate && candidate.visible && candidate.enabled) {
					candidate.forceActiveFocus(Qt.PopupFocusReason)
					return
				}
			}
		})
		onClosed: root.restoreMediaWindowFailureFocus()
		background: Rectangle {
			radius: Theme.shellRadius
			color: Theme.surfaceRaised
			border.color: Theme.withAlpha(Theme.danger, 0.55)
			border.width: 1
		}
		contentItem: ColumnLayout {
			id: mediaWindowFailureContent
			spacing: Theme.space3
			Accessible.role: Accessible.AlertMessage
			Accessible.name: qsTr("Media window unavailable")
			Accessible.description: root.mediaSessionWindowFailureMessage()
			ModernIcon {
				Layout.alignment: Qt.AlignHCenter
				name: "warning"
				size: Theme.avatarMedium
				color: Theme.danger
			}
			Label {
				Layout.fillWidth: true
				text: qsTr("Media window unavailable")
				textFormat: Text.PlainText
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				Layout.fillWidth: true
				text: root.mediaSessionWindowFailureMessage()
				textFormat: Text.PlainText
				color: Theme.textMuted
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
			}
			RowLayout {
				Layout.alignment: Qt.AlignHCenter
				ModernButton {
					id: mediaWindowComponentRetryButton
					objectName: "mediaSessionWindowComponentRetryButton"
					visible: root.mediaSessionWindowComponentFailed
					text: qsTr("Retry")
					tone: "accent"
					onClicked: root.retryMediaSessionWindowComponent()
				}
				ModernButton {
					id: mediaWindowComponentExternalButton
					objectName: "mediaSessionWindowComponentExternalButton"
					visible: root.mediaSessionExternalFallbackUrl().length > 0
					text: qsTr("Open in browser")
					onClicked: Qt.openUrlExternally(root.mediaSessionExternalFallbackUrl())
				}
				ModernButton {
					id: mediaWindowComponentCloseButton
					objectName: "mediaSessionWindowComponentCloseButton"
					text: qsTr("Close")
					onClicked: if (mediaSession && typeof mediaSession.closePlayer === "function")
						mediaSession.closePlayer()
				}
			}
		}
	}

	Drawer {
		id: navigationDrawer
		edge: Theme.railSide === "left" ? Qt.LeftEdge : Qt.RightEdge
		width: Math.min(340, root.width * 0.86)
		height: root.height
		enabled: root.compactNavigation
		modal: true
		dim: true
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		enter: Transition {
			NumberAnimation {
				property: "position"
				from: 0
				to: 1
				duration: root.visualFixtureOverrideActive ? 0 : Theme.motionNormal
				easing.type: Easing.OutCubic
			}
		}
		exit: Transition {
			NumberAnimation {
				property: "position"
				from: 1
				to: 0
				duration: root.visualFixtureOverrideActive ? 0 : Theme.motionNormal
				easing.type: Easing.InCubic
			}
		}
		onOpened: navigationDrawerRail.focusInitialItem()
		onClosed: {
			productDialog.syncVisibility()
			if (navigationToggle.visible)
				navigationToggle.forceActiveFocus()
			else
				desktopNavigationRail.focusInitialItem()
		}
		Overlay.modal: Rectangle {
			objectName: "navigationDrawerScrim"
			color: Theme.modalScrim
			Accessible.ignored: true
		}
		background: Rectangle { color: Theme.rail; border.color: Theme.divider }
		NavigationRail {
			id: navigationDrawerRail
			roomListObjectName: "navigationDrawerRooms"
			anchors.fill: parent
			Accessible.role: Accessible.Dialog
			Accessible.name: qsTr("Rooms and participants")
			navigationModel: root.navigationRailModel
			selectionState: root.navigationSelectionState
			uiCommands: root.navigationCommands
			clientSession: root.navigationSession
			visualFixtureMode: root.visualFixtureOverrideActive
			commitOnSelection: true
			settingsEnabled: root.settingsActionEnabled
			stonksEnabled: root.stonksShortcutEnabled
			serverMenuOpen: appMenuPopup.visible
			// The drawer is the semantic owner while navigationModalActive is true;
			// suppressing it through the global modal flag leaves screen readers with
			// only an empty Window node. Suppress it only for a different product
			// modal that actually owns the application at the same time.
			accessibilitySuppressed: root.productDialogTransitionActive
				|| root.mediaSessionWindowUnavailable
			activeScopeMenuToken: (roomMenuPopup.visible || textRoomMenuPopup.visible)
				? root.contextScopeToken : ""
			activeParticipantMenuKey: participantMenuPopup.visible
				? root.contextParticipantRowKey : ""
			onSelectionCommitted: navigationDrawer.close()
			onScopeMenuRequested: (scopeToken, kind, actions, anchorPoint) =>
				root.openScopeMenu(scopeToken, kind, actions, anchorPoint)
			onParticipantMenuRequested: (sessionId, actions, anchorPoint, entryKind, scopeToken, rowKey) =>
				root.openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken, rowKey)
			onSettingsRequested: root.requestSettings()
			onStonksRequested: uiCommands.invokeAppAction("server.stonksPortfolio", {})
			onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
			onServerMenuRequested: anchorPoint => root.openApplicationMenu(anchorPoint)
		}
	}

	SemanticMenu {
		id: profileMenuPopup
		objectName: "profileMenu"
		accessibleName: qsTr("Profile menu")
		preferredWidth: 292
		maximumHeight: Math.max(260, root.height - 48)
		headerTitle: String((clientSession.selfMenu || ({})).name || clientSession.selfName || qsTr("You"))
		headerSubtitle: String((clientSession.selfMenu || ({})).statusLabel
			|| clientSession.selfStatusLabel || "")
		headerTone: String((clientSession.selfMenu || ({})).statusTone || "")
		groups: root.profileMenuGroups()
		onActionRequested: (actionId, payload) =>
			uiCommands.invokeAppAction(actionId, payload && payload.payload ? payload.payload : ({}))
	}

	PayloadMenu {
		id: roomMenuPopup
		objectName: "voiceRoomMenu"
		accessibleName: qsTr("Voice room menu")
		preferredWidth: 280
		maximumHeight: Math.max(160, root.height - 32)
		entries: root.contextScopeActions.length > 0 ? root.contextScopeActions : [{
			"kind": "label", "id": "voice-room-empty",
			"label": qsTr("No room actions available"), "enabled": false
		}]
		onActionRequested: (actionId, payload) =>
			uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
		onValueRequested: (actionId, value, finalValue, payload) =>
			uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
	}

	PayloadMenu {
		id: textRoomMenuPopup
		objectName: "textRoomMenu"
		accessibleName: qsTr("Text room menu")
		preferredWidth: 280
		maximumHeight: Math.max(160, root.height - 32)
		entries: root.contextScopeActions.length > 0 ? root.contextScopeActions : [{
			"kind": "label", "id": "text-room-empty",
			"label": qsTr("No text-room actions available"), "enabled": false
		}]
		onActionRequested: (actionId, payload) =>
			uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
		onValueRequested: (actionId, value, finalValue, payload) =>
			uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
	}

	PayloadMenu {
		id: participantMenuPopup
		objectName: "participantMenu"
		accessibleName: qsTr("Participant menu")
		preferredWidth: 300
		maximumHeight: Math.max(160, root.height - 32)
		entries: root.contextParticipantActions.length > 0 ? root.contextParticipantActions : [{
			"kind": "label", "id": "participant-empty",
			"label": qsTr("No participant actions available"), "enabled": false
		}]
		onActionRequested: (actionId, payload) => {
			if (root.contextParticipantEntryKind === "listener")
				uiCommands.invokeScopeAction(root.contextParticipantScopeToken, actionId)
			else
				uiCommands.invokeParticipantAction(root.contextParticipantId, actionId)
		}
		onValueRequested: (actionId, value, finalValue, payload) => {
			if (root.contextParticipantEntryKind === "listener")
				uiCommands.invokeScopeActionValue(root.contextParticipantScopeToken,
					actionId, value, finalValue)
			else
				uiCommands.invokeParticipantActionValue(root.contextParticipantId,
					actionId, value, finalValue)
		}
	}

	PayloadMenu {
		id: chatBackgroundMenuPopup
		objectName: "chatBackgroundMenu"
		accessibleName: qsTr("Conversation options")
		preferredWidth: 280
		maximumHeight: Math.max(160, root.height - 32)
		entries: root.chatBackgroundMenuEntries()
		onActionRequested: (actionId, payload) => {
			if (actionId === "activeScope.loadOlder")
				root.requestOlderMessagesPreservingAnchor()
			else if (String(actionId || "").substring(0, 5) === "self.")
				uiCommands.invokeAppAction(actionId,
					payload && payload.payload ? payload.payload : ({}))
			else if (root.contextScopeToken.length > 0)
				uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
		}
		onValueRequested: (actionId, value, finalValue, payload) => {
			if (root.contextScopeToken.length > 0)
				uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
		}
	}

	Item {
		id: operationOverlay
		objectName: "asyncOperationOverlay"
		parent: timeline
		x: Math.max(Theme.space3, timeline.width - width - Theme.space3)
		y: Theme.space3
		width: Math.min(380, Math.max(1, timeline.width - Theme.space3 * 2))
		height: operationList.count > 0
			? Math.min(Math.max(56, operationList.contentHeight), maximumHeight) : 0
		readonly property int maximumHeight: Math.max(56, timeline.height - Theme.space3 * 2)
		readonly property bool productMenuOpen: appMenuPopup.visible || profileMenuPopup.visible
			|| roomMenuPopup.visible || textRoomMenuPopup.visible
			|| participantMenuPopup.visible || chatBackgroundMenuPopup.visible
		readonly property Item firstOperationItem: operationList.count > 0
			? operationList.itemAtIndex(0) : null
		readonly property Item visualFixtureFocusTarget: firstOperationItem
			? firstOperationItem.visualFixtureFocusTarget : null
		visible: operationList.count > 0 && !productMenuOpen
		enabled: visible
		z: 24
		clip: true

		Behavior on height {
			enabled: !root.visualFixtureOverrideActive
			NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
		}

		ListView {
			id: operationList
			objectName: "asyncOperationList"
			anchors.fill: parent
			model: operationOverlayModel
			spacing: Theme.space2
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
			delegate: AsyncOperationCard {
				// A height-dependent width made wrapped card content feed back into
				// contentHeight and the overlay height. Reserve a stable scrollbar gutter.
				width: Math.max(1, operationList.width - Theme.space2)
				maximumHeight: operationOverlay.maximumHeight
				narrowLayout: root.width < 640
				animationsEnabled: !root.visualFixtureOverrideActive
				itemResultPageProvider: function(operationId, offset, limit, unsuccessfulOnly) {
					return operationModel.itemResultPage(operationId, offset, limit, unsuccessfulOnly)
				}
				onCancelRequested: operationId => operationModel.cancel(operationId)
				onDismissRequested: operationId => operationModel.dismiss(operationId)
				onActionRequested: (operationId, actionId, actionPayload) => {
					uiCommands.invokeAppAction(actionId, actionPayload || ({}))
					operationModel.dismiss(operationId)
				}
			}
		}
	}

	ToastPill {
		id: toastPill
		objectName: "productToastPill"
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.bottom: parent.bottom
		// Keep transient feedback clear of the composer even while replies,
		// attachments, autocomplete, or multiline input expand it.
		anchors.bottomMargin: composerSurface.height + bottomStonksTickerLoader.height + 22
		width: Math.min(implicitWidth, Math.max(1, parent.width - Theme.space4 * 2))
		maximumWidth: Math.min(680, Math.max(280, parent.width - Theme.space4 * 2))
		controller: toastState
		animationsEnabled: !root.visualFixtureOverrideActive
		onActionRequested: actionId => uiCommands.invokeAppAction(actionId, {})
	}

	Rectangle {
		id: productSurface
		objectName: "productSurface"
        anchors.fill: parent
        anchors.margins: 8
		// Modal popups own pointer/focus blocking. Keeping the live scene in the
		// window avoids a full-window texture copy and remains capturable by
		// PrintWindow/off-screen automation. Do not toggle enabled on the entire
		// tree: that would propagate disabled state and repaint every control for
		// each dialog. ModalAccessibilityBarrier prunes every semantic descendant
		// without hiding or disabling the visual background.
		visible: true
		Accessible.ignored: false
        radius: Theme.shellRadius
        color: Theme.shellBackground
        border.color: Theme.divider
        clip: true

        RowLayout {
            anchors.fill: parent
			anchors.topMargin: windowTopStonksTickerLoader.height
			anchors.bottomMargin: bottomStonksTickerLoader.height
            spacing: 0
			layoutDirection: Theme.railSide === "left" ? Qt.RightToLeft : Qt.LeftToRight

			ColumnLayout {
				id: mainContentColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
				// Compact windows must be allowed to shrink below the combined implicit
				// width of the header and message metadata. Without an explicit zero
				// minimum the RowLayout grows past productSurface and the rightmost
				// timestamp/message controls are clipped by the shell radius.
				Layout.minimumWidth: 0
                spacing: 0

                Rectangle {
                    id: shellHeader
                    Layout.fillWidth: true
					// On desktop this is also the navigation header height. Keeping
					// one source of geometry makes the divider continuous for both
					// left- and right-side rail placement.
					Layout.preferredHeight: root.compactNavigation
						? (root.narrowShell ? 64 : 72) : Theme.railHeaderHeight
                    color: Theme.panel
					border.width: 0
					DragHandler {
						target: null
						onActiveChanged: if (active) root.startSystemMove()
					}
					TapHandler {
						acceptedButtons: Qt.LeftButton
						onDoubleTapped: root.visibility === Window.Maximized
							? root.showNormal() : root.showMaximized()
					}
					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						height: 1
						color: Theme.divider
					}
                    Column {
                        anchors.left: parent.left
						anchors.leftMargin: root.narrowShell ? Theme.space3 : Theme.space5
						anchors.right: headerActions.left
						anchors.rightMargin: Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Label {
							textFormat: Text.PlainText
							width: parent.width
                            text: activeScope.label.length > 0 ? activeScope.label : clientSession.serverName
                            color: Theme.textStrong
                            font.pixelSize: Theme.fontHeading
                            font.bold: true
							elide: Text.ElideRight
                        }
                        Label {
							textFormat: Text.PlainText
                            text: activeScope.description.length > 0 ? activeScope.description : activeScope.kindLabel
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontLabel
                            elide: Text.ElideRight
							width: parent.width
                        }
                    }
					Row {
						id: headerActions
						anchors.right: parent.right
						anchors.rightMargin: root.narrowShell ? Theme.space2 : Theme.space4
						anchors.verticalCenter: parent.verticalCenter
						spacing: 6
						ModernIconButton {
							id: navigationToggle
							objectName: "compactNavigationToggle"
							visible: root.compactNavigation
							iconName: "menu"
							Accessible.ignored: root.backgroundAccessibilitySuppressed
							Accessible.name: qsTr("Open rooms and participants")
							onClicked: navigationDrawer.open()
						}
						ModernButton {
							readonly property var share: activeScope.screenShare || ({})
							visible: !root.activeScopeHasScreenShare && !!share.visible
								&& String(share.primaryActionId || "").length > 0
							dense: true
							iconName: "screen-share"
							text: root.narrowShell ? qsTr("Share")
								: String(share.primaryLabel || qsTr("Screen share"))
							tone: String(share.primaryTone || "neutral")
							enabled: share.primaryEnabled === undefined || !!share.primaryEnabled
							Accessible.ignored: root.backgroundAccessibilitySuppressed
							Accessible.name: String(share.primaryLabel || qsTr("Screen share"))
							Accessible.description: String(share.primaryHint || "")
							ToolTip.visible: hovered
							ToolTip.text: String(share.primaryHint || Accessible.name)
							ToolTip.delay: 500
							ToolTip.timeout: 15000
							onClicked: uiCommands.invokeScopeAction(activeScope.scopeToken,
								String(share.primaryActionId || ""))
						}
						ModernButton {
							id: motdToggleButton
							objectName: "motdToggleButton"
							visible: !!clientSession.hasMotd
							dense: true
							iconName: "info"
							checked: visible && !clientSession.motdDismissed
							readonly property bool revealsWelcome: clientSession.motdDismissed
							text: qsTr("MOTD")
							Accessible.name: revealsWelcome
								? qsTr("Show welcome message") : qsTr("Hide welcome message")
							Accessible.description: clientSession.motdChanged
								? (revealsWelcome
									? qsTr("A new server welcome message is available. Activate to show the welcome message.")
									: qsTr("A new server welcome message is available. Activate to hide the welcome message."))
								: (revealsWelcome
									? qsTr("Activate to show the server welcome message.")
									: qsTr("Activate to hide the server welcome message."))
							Accessible.checked: checked
							ToolTip.visible: hovered
							ToolTip.text: Accessible.name
							ToolTip.delay: 500
							ToolTip.timeout: 15000
							contentItem: RowLayout {
								spacing: 6
								ModernIcon {
									name: motdToggleButton.iconName
									size: 16
									color: motdToggleButton.enabled ? Theme.textStrong : Theme.textMuted
								}
								Label {
									textFormat: Text.PlainText
									text: motdToggleButton.text
									color: motdToggleButton.enabled ? Theme.textStrong : Theme.textMuted
									font.pixelSize: Theme.fontLabel
									font.weight: motdToggleButton.checked ? Font.DemiBold : Font.Medium
									Accessible.ignored: true
								}
								Rectangle {
									visible: clientSession.motdChanged && !root.narrowShell
									Layout.preferredWidth: motdNewLabel.implicitWidth + 10
									Layout.preferredHeight: 18
									radius: 9
									color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
									border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.72)
									border.width: 1
									Label {
										id: motdNewLabel
										anchors.centerIn: parent
										textFormat: Text.PlainText
										text: qsTr("New")
										color: Theme.warning
										font.pixelSize: 9
										font.bold: true
										Accessible.ignored: true
									}
								}
							}
							background: Rectangle {
								radius: Math.min(Theme.innerRadius,
									Math.round(motdToggleButton.implicitHeight / 3))
								color: !motdToggleButton.enabled ? Theme.panel
									: motdToggleButton.down ? Theme.surfaceHover
									: motdToggleButton.checked ? Theme.selected
									: motdToggleButton.hovered ? Theme.surfaceHover : Theme.surfaceRaised
								border.color: motdToggleButton.activeFocus ? Theme.focus
									: clientSession.motdChanged ? Theme.warning
									: motdToggleButton.checked ? Theme.accent : Theme.surfaceBorder
								border.width: motdToggleButton.activeFocus ? Theme.focusRingWidth : 1
								Behavior on color { ColorAnimation { duration: Theme.motionFast } }
								Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
							}
							onClicked: {
								const reveal = clientSession.motdDismissed
								root.handleMotdAction(reveal ? "motd.restore" : "motd.dismiss",
									{ "signature": String(clientSession.motdSignature || "") })
							}
						}
						ModernIconButton {
							id: conversationSearchButton
							objectName: "conversationSearchButton"
							visible: clientSession.connected
							iconName: "search"
							Accessible.ignored: root.backgroundAccessibilitySuppressed
							Accessible.name: root.conversationSearchOpen
								? qsTr("Focus conversation search") : qsTr("Search this conversation")
							Accessible.description: qsTr("Search message text, senders, replies, and attachments")
							onClicked: root.openConversationSearch()
						}
						ModernIconButton {
							objectName: "activeScopeMarkRead"
							visible: activeScope.canMarkRead && Number(activeScope.unreadCount || 0) > 0
							iconName: "check"
							Accessible.name: qsTr("Mark this conversation as read")
							Accessible.description: qsTr("%1 unread messages").arg(activeScope.unreadCount)
							onClicked: uiCommands.markActiveScopeRead()
						}
						ModernIconButton {
							id: directMessageTrayButton
							objectName: "directMessageTrayButton"
							visible: directMessages && (directMessages.available
								|| Number(directMessages.summaryModel.count) > 0)
							iconName: "direct"
							selected: directMessages && directMessages.trayOpen
							Accessible.name: directMessages && directMessages.hasUnread
								? qsTr("Direct messages, %1 unread").arg(directMessages.unreadTotal)
								: qsTr("Direct messages")
							Accessible.checked: selected
							onClicked: directMessages.setTrayOpen(!directMessages.trayOpen)

							Rectangle {
								visible: directMessages && directMessages.hasUnread
								anchors.right: parent.right
								anchors.top: parent.top
								anchors.margins: 2
								width: Math.max(16, unreadBadgeLabel.implicitWidth + 6)
								height: 16
								radius: height / 2
								color: Theme.accent
								z: 2
								Label {
									id: unreadBadgeLabel
									anchors.centerIn: parent
									textFormat: Text.PlainText
									text: directMessages && directMessages.unreadTotal > 99
										? "99+" : String(directMessages ? directMessages.unreadTotal : 0)
									color: Theme.contrastText(Theme.accent)
									font.pixelSize: 9
									font.bold: true
									Accessible.ignored: true
								}
							}
						}
					ModernIconButton {
						id: appMenuButton
						objectName: "conversationOptionsButton"
						iconName: "more"
						Accessible.ignored: root.backgroundAccessibilitySuppressed
						Accessible.name: qsTr("Conversation options")
						Accessible.description: qsTr("Actions for %1").arg(
							activeScope.label || qsTr("this conversation"))
						Accessible.focusable: true
						Accessible.focused: activeFocus
						ToolTip.visible: hovered
						ToolTip.text: Accessible.name
							onClicked: root.openChatBackgroundMenu(
								appMenuButton.mapToItem(null, appMenuButton.width, appMenuButton.height))
					}
					}
					SemanticMenu {
						id: appMenuPopup
						objectName: "applicationMenu"
						accessibleName: qsTr("Server menu")
						parent: root.contentItem
						preferredWidth: 292
						maximumHeight: Math.max(280, root.height - 104)
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
						headerTitle: clientSession.connected
							? qsTr("Connected to %1").arg(clientSession.serverName) : qsTr("Mumble")
						headerSubtitle: clientSession.connected
							? qsTr("%1 · %2").arg(clientSession.selfName).arg(clientSession.selfStatusLabel)
							: qsTr("Choose a server to get started")
						headerTone: clientSession.connected ? "success" : ""
						groups: root.applicationMenuGroups()
						onActionRequested: (actionId, payload) =>
							uiCommands.invokeAppAction(actionId,
								payload && payload.payload ? payload.payload : ({}))
                    }
                }

				Loader {
					id: topStonksTickerLoader
					Layout.fillWidth: true
					Layout.preferredHeight: active && item && item.visible ? item.implicitHeight : 0
					active: root.stonksTickerEnabled && root.stonksTickerPlacement === "top"
					asynchronous: true
					sourceComponent: Component {
						StonksHeader {
							objectName: "stonksTickerTop"
							width: parent ? parent.width : implicitWidth
							stonks: root.stonksTickerState
							tickerBannerEnabled: true
							tickerDirection: root.stonksTickerDirection
							tickerSpeed: root.stonksTickerSpeed
							scopeToken: activeScope.scopeToken
							scopeLabel: activeScope.label
							narrowLayout: root.narrowShell
							docked: true
							onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
						}
					}
				}

				ConversationSearchBar {
					id: conversationSearchBar
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					visible: root.conversationSearchOpen
					timelineModel: chatTimelineV2Enabled ? chatTimelineV2 : chatModel
					narrowLayout: root.narrowShell
					accessibilitySuppressed: root.backgroundAccessibilitySuppressed
					visualFixtureMode: root.visualFixtureOverrideActive
					onCloseRequested: root.closeConversationSearch(true)
					onCurrentMatchRequested: (row, stableId) =>
						root.revealConversationSearchMatch(row, stableId)
				}

                UpdateBanner {
                    Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Theme.spacing : 0
                    state: clientSession.updateBanner
					animationsEnabled: !root.visualFixtureOverrideActive
                    onActionRequested: actionId => uiCommands.invokeAppAction(actionId, {})
                }

				ConnectionBanner {
					id: connectionBanner
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Theme.spacing : 0
					session: clientSession
					showDisconnectedAction: !emptyConversationState.visible
					animationsEnabled: !root.visualFixtureOverrideActive
					onActionRequested: (actionId, payload) => uiCommands.invokeAppAction(actionId, payload)
				}

				MotdPanel {
					id: motdPanel
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					maximumBodyHeight: root.height <= 560
						? (root.activeScopeHasScreenShare ? 60 : root.compactNavigation ? 80 : 88)
						: root.compactNavigation ? Math.max(112, Math.min(160, root.height * 0.18))
						: Math.max(132, Math.min(220, root.height * 0.28))
					maximumImageHeight: root.height <= 560
						? (root.activeScopeHasScreenShare ? 32 : root.compactNavigation ? 34 : 40)
						: root.compactNavigation ? 56 : 68
					visualFixtureMode: root.visualFixtureOverrideActive
					session: clientSession
					onActionRequested: (actionId, payload) => root.handleMotdAction(actionId, payload)
					onLinkRequested: link => Qt.openUrlExternally(link)
				}

				ScreenShareScopeCard {
					id: activeScopeScreenShareCard
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					share: activeScope.screenShare || ({})
					scopeLabel: activeScope.label
					narrowLayout: root.narrowShell
					accessibilitySuppressed: root.backgroundAccessibilitySuppressed
					onActionRequested: actionId => uiCommands.invokeScopeAction(
						activeScope.scopeToken, actionId)
				}

				Item {
					id: dockedScreenShare
					objectName: "dockedScreenShare"
					property var backend: (activeScope.screenShare || ({})).backend || null
					readonly property bool presentationActive: backend !== null && !backend.detached
					readonly property real desiredHeight: presentationActive
						? Math.max(160, Math.min(320, root.height * 0.32)) : 0
					property real committedHeight: 0
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.preferredHeight: committedHeight
					visible: committedHeight > 0
					onDesiredHeightChanged: {
						// Capture before changing the layout budget. The existing timeline
						// owner restores the anchor; user input and prepend keep priority.
						const generation = timeline.scopePresentationGeneration
						timeline.captureViewportAnchor()
						committedHeight = desiredHeight
						Qt.callLater(function() {
							if (generation !== timeline.scopePresentationGeneration)
								return
							if (timeline.viewportAnchorActive)
								timeline.scheduleViewportAnchorQuietRelease()
						})
					}
					Loader {
						anchors.fill: parent
						active: dockedScreenShare.presentationActive
						sourceComponent: Component {
							ScreenShareSurface {
								backend: dockedScreenShare.backend
								renderActive: dockedScreenShare.presentationActive
								visualFixtureMode: root.visualFixtureOverrideActive
								onFullscreenRequested: enabled => {
									if (enabled && backend) backend.requestFullscreen()
								}
							}
						}
					}
				}

				WatchTogetherBanner {
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					session: mediaSession
					participantModel: root.navigationParticipantModel
				}

				WatchTogetherStage {
					id: watchTogetherStage
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Theme.space2 : 0
					Layout.preferredHeight: visible ? implicitHeight : 0
					session: mediaSession
					mediaProfileFactory: mediaProfiles
					visualMediaFixtureMode: root.visualMediaFixtureMode
					maximumStageHeight: Math.max(240, Math.min(520, root.height * 0.56))
					renderActive: visible
				}

				Loader {
					id: stonksRoomPanelLoader
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: active && item && item.visible
						? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					Layout.preferredHeight: active && item && item.visible ? item.implicitHeight : 0
					active: root.stonksRoomPanelActive
					asynchronous: true
					sourceComponent: Component {
						StonksRoomPanel {
							objectName: "stonksRoomPanel"
							width: parent ? parent.width : implicitWidth
							stonks: root.stonksTickerState
							scopeToken: activeScope.scopeToken
							scopeLabel: activeScope.label
							narrowLayout: root.narrowShell
							accessibilitySuppressed: root.backgroundAccessibilitySuppressed
							onActionRequested: (actionId, payload) =>
								uiCommands.invokeAppAction(actionId, payload)
						}
					}
				}

				Item {
					id: activityLogColumnHeader
					objectName: "activityLogColumnHeader"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? Theme.densityMetric(30, 34, 40) : 0
					visible: activeScope.activity
					Accessible.role: Accessible.Grouping
					Accessible.name: qsTr("Activity log columns: time and event")
					Accessible.ignored: root.backgroundAccessibilitySuppressed

					Rectangle {
						id: activityLogColumnSurface
						objectName: "activityLogColumnSurface"
						anchors.horizontalCenter: parent.horizontalCenter
						anchors.verticalCenter: parent.verticalCenter
						width: Math.max(1, Math.min(root.conversationLaneMaximumWidth,
							parent.width - root.timelineHorizontalMargin * 2 - Theme.space4))
						height: parent.height
						radius: Math.min(Theme.innerRadius, 8)
						color: Theme.withAlpha(Theme.surfaceRaised, 0.76)
						border.color: Theme.quietBorder
						border.width: 1
						Accessible.ignored: true

						RowLayout {
							anchors.fill: parent
							anchors.leftMargin: Theme.space3
							anchors.rightMargin: Theme.space3
							spacing: Theme.space2

							Label {
								Layout.preferredWidth: root.activityLogTimeColumnWidth
								textFormat: Text.PlainText
								text: qsTr("TIME")
								color: Theme.textFaint
								font.pixelSize: Theme.fontCaption
								font.weight: Font.DemiBold
								font.letterSpacing: 0.7
								horizontalAlignment: Text.AlignRight
								Accessible.ignored: true
							}
							Item {
								Layout.preferredWidth: root.activityLogGutterWidth
								Layout.fillHeight: true
								Accessible.ignored: true
							}
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: qsTr("EVENT")
								color: Theme.textFaint
								font.pixelSize: Theme.fontCaption
								font.weight: Font.DemiBold
								font.letterSpacing: 0.7
								Accessible.ignored: true
							}
							RowLayout {
								spacing: Theme.space1
								Accessible.ignored: true
								Rectangle {
									Layout.preferredWidth: 6
									Layout.preferredHeight: 6
									radius: 3
									color: clientSession.connected ? Theme.success : Theme.textMuted
									Accessible.ignored: true
								}
								Label {
									textFormat: Text.PlainText
									text: clientSession.connected ? qsTr("LIVE") : qsTr("OFFLINE")
									color: clientSession.connected ? Theme.success : Theme.textMuted
									font.pixelSize: Theme.fontCaption
									font.weight: Font.DemiBold
									font.letterSpacing: 0.6
									Accessible.ignored: true
								}
							}
						}
					}
				}

                ListView {
                    id: timeline
					objectName: "chatTimeline"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
					// The v2 timeline replaces this view behind its development switch.
					visible: !chatTimelineV2Enabled
                    model: chatTimelineV2Enabled ? null : chatModel.presentationModel
					// The auto-connected scope can already be populated before the
					// activeScope signal connection exists. Start the same bounded
					// presentation transaction for that initial scope explicitly.
					Component.onCompleted: beginScopeChange()
                    clip: true
					interactive: !scopePresentationPending
					Accessible.role: Accessible.List
					Accessible.name: activeScope.activity ? qsTr("Activity log")
						: qsTr("Conversation messages")
					// QQuickListView's scrolling contentItem reports its untransformed
					// content bounds to Windows UIA. With tall rich cards that wrapper can
					// appear hundreds of pixels away from its visible children. Remove only
					// that implementation detail so delegates are parented semantically by
					// this stable viewport instead.
					Binding {
						target: timeline.contentItem
						property: "Accessible.ignored"
						value: true
						when: !!timeline.contentItem
						restoreMode: Binding.RestoreBindingOrValue
					}
					spacing: 0
					leftMargin: root.timelineHorizontalMargin
					rightMargin: root.timelineHorizontalMargin
					readonly property real baseTopMargin: activeScope.activity
						? Theme.space1 : root.timelineVerticalMargin
					readonly property bool sparseBottomAlignmentActive:
						!activeScope.activity && count > 0 && count <= 10
						&& contentHeight + baseTopMargin < height - 0.5
					// Flickable margins stay outside contentHeight. Use that independent
					// space to place an underfilled conversation against its inline footer,
					// which keeps the newest sparse row directly above the composer without
					// inventing a model row or changing scrollbar/content-height geometry.
					topMargin: sparseBottomAlignmentActive
						? Math.max(baseTopMargin, height - contentHeight) : baseTopMargin
					reuseItems: true
					boundsBehavior: Flickable.StopAtBounds
					ScrollBar.vertical: ModernScrollBar {
						id: timelineScrollBar
						objectName: "chatTimelineScrollBar"
						// This fixture verifies production history anchoring, not
						// subpixel scrollbar rounding. Keep that unrelated Qt
						// implementation detail out of its deterministic evidence.
						visible: root.visualFixtureSurfaceVariant !== "chat-history-prepend-anchor"
						Accessible.ignored: !visible
						enabled: !timeline.scopePresentationPending
						onPressedChanged: {
							if (pressed)
								timeline.beginScrollbarDrag()
							else {
								timeline.finishScrollbarDrag()
								timeline.scheduleStationaryThumbWarmReconcile()
							}
						}
					}
					MiddleDragScrollHandler {
						id: timelineScrollHandler
						targetFlickable: timeline
						horizontalEnabled: false
						navigationEnabled: timeline.interactive
							&& !timeline.scopePresentationPending
							&& !timeline.scrollbarDragActive
							&& !timeline.scrollbarReleasePending
							&& !timelineScrollBar.pressed
							&& !root.performanceChatScrollOwnershipActive
							&& !root.performanceChatScrollRunning
						smoothWheelEnabled: true
						wheelStep: Math.max(80, Math.min(112, timeline.height * 0.14))
						onScrollingStarted: timeline.scheduleViewportMembershipUpdate()
						onScrollingEnded: {
							if (!timelineScrollHandler.navigationAvailable)
								return
							if (timeline.scrollbarDragActive
									|| timeline.scrollbarReleasePending
									|| timelineScrollBar.pressed) {
								timeline.scheduleViewportMembershipUpdate()
								return
							}
							if (!timeline.explicitNavigationMovement) {
								timeline.scheduleViewportMembershipUpdate()
								return
							}
							// Direct custom-wheel and middle-drag contentY writes do not
							// guarantee a matching ListView movementEnded signal. Clear only
							// after rejecting a newer thumb owner, so a stale end cannot erase
							// the newer owner's explicit-navigation latch.
							timeline.explicitNavigationMovement = false
							timeline.stickToBottom = timeline.isAtBottom()
							timeline.requestBottomFollow()
							if (!timeline.stickToBottom)
								timeline.captureViewportAnchor()
							timeline.scheduleRichContentResume()
						}
					onWheelInputObserved: {
						++root.timelineWheelInputObservedCount
						timeline.releaseScrollbarGuardForWheelInput()
					}
					onWheelInputPassedThrough: if (timelineScrollHandler.navigationAvailable)
						timeline.noteExplicitWheelInput()
					onWheelAngleUnitsObserved: (notchUnits, signedNotchUnits) => {
						root.timelineWheelObservedNotchUnits += notchUnits
						root.timelineWheelObservedSignedNotchBalance += signedNotchUnits
					}
					onWheelInputAdmitted: notchUnits =>
						root.timelineWheelInputAdmittedNotchUnits += notchUnits
					onWheelInputDropped: notchUnits =>
						root.timelineWheelInputDroppedNotchUnits += notchUnits
					onWheelInputBlocked: (notchUnits, signedNotchUnits, precision) => {
						++root.timelineWheelInputBlockedCount
						root.timelineWheelBlockedNotchUnits += notchUnits
						root.timelineWheelBlockedSignedNotchBalance += signedNotchUnits
						if (precision)
							++root.timelineWheelBlockedPrecisionCount
					}
					onWheelInputExpired: distance =>
						root.timelineWheelExpiredDistance += distance
					}
					StationaryScrollWarmGate {
						id: stationaryThumbWarmGate
						objectName: "stationaryScrollWarmGate"
						pressed: timelineScrollBar.pressed
						dragActive: timeline.scrollbarDragActive
						releasePending: timeline.scrollbarReleasePending
						moving: timeline.moving
						dragging: timeline.dragging
						flicking: timeline.flicking
						customScrolling: timelineScrollHandler.scrolling
						performanceScrolling: root.performanceChatScrollRunning
						scopeBlocked: timeline.scopePresentationPending
							|| timeline.scopePresentationFinalizing
							|| timeline.scopeResetPending
							|| timeline.scopePresentationObservationActive
						prependBlocked: timeline.prependAnchorActive
							|| timeline.prependMutationInProgress
							|| timeline.deferredPrependAnchorActive
							|| chatModel.prependPending
						membershipBlocked: timeline.viewportMembershipUpdatePending
							|| viewportMembershipTimer.running
						position: timeline.contentY
						visibleStableIdProvider: function() {
							return timeline.stationaryThumbVisibleStableIds()
						}
						canAdmitStableId: function(stableId) {
							return timeline.stationaryThumbWarmCanAdmit(stableId)
						}
						revalidateActiveStableId: function(stableId, pressEpoch,
								dwellGeneration) {
							return timeline.revalidateStationaryThumbWarmAdmission(
								stableId, pressEpoch, dwellGeneration)
						}
						onAdmissionCancelled: timeline.scheduleStationaryThumbWarmReconcile()
						onAdmissionFinished: timeline.scheduleStationaryThumbWarmReconcile()
						onRetentionsCleared: timeline.scheduleStationaryThumbWarmReconcile()
						onMembershipRevisionDrained:
							timeline.scheduleStationaryThumbWarmReconcile()
						onAdmissionRequested: (stableId, pressEpoch, dwellGeneration) => {
							if (!timeline.installStationaryThumbWarmPin(stableId,
									pressEpoch, dwellGeneration))
								stationaryThumbWarmGate.finishAdmission(stableId,
									pressEpoch, dwellGeneration, "terminal")
						}
					}
					Timer {
						id: stationaryThumbWarmReceiptTimer
						interval: 0
						repeat: false
						onTriggered: timeline.drainStationaryThumbWarmReceipts()
					}
					Timer {
						id: stationaryThumbWarmReconcileTimer
						interval: 16
						repeat: false
						onTriggered: timeline.reconcileStationaryThumbWarmPins()
					}
					// Keep a bounded two-viewport lookahead. Cold entries remain lightweight;
					// the idle queue prepares one nearby card at a time before it is exposed.
					readonly property real baseCacheBuffer:
						Math.max(640, Math.min(1600, height * 2))
					// Once a real gesture begins, retain the bounded loaded page behind
					// it. New delegates remain lightweight scroll placeholders, while the
					// already visible rich trees never enter the reuse pool mid-gesture.
					// Reset this scope-local retention on navigation rather than growing
					// it with every older-history page.
					property real richScrollRetentionBuffer: 0
					property real richScrollRetentionTarget: 0
					// Stay narrowly ahead of the 112 px performance/wheel step. A wider
					// lead can make ListView construct two variable-height message frames
					// in one GUI turn, while 128 px still prevents rows behind the gesture
					// from falling out of the retained range.
					readonly property real richScrollRetentionRampStep: 128
					// Full card preparation remains single-flight and yields to user input.
					// Completed stills remain in the existing delegate cache for reversal.
					readonly property real nearViewportPrefetchDistance: baseCacheBuffer
					readonly property real staticPreviewLookaheadDistance:
						Math.max(320, Math.min(baseCacheBuffer, height * 1.35))
					readonly property real maximumRichScrollRetentionBuffer: 18000
					// During cold entry every rich source row is still a childless
					// provider-height summary. Let ListView instantiate that inexpensive
					// geometry for the complete bounded page so its contentHeight is an
					// exact sum, not an average extrapolated from whichever three rows
					// happen to intersect the tail.
					cacheBuffer: scopePresentationPending
						? maximumRichScrollRetentionBuffer
						: Math.max(baseCacheBuffer, richScrollRetentionBuffer)
					readonly property bool richScrollInProgress: moving || dragging || flicking
						|| timelineScrollHandler.scrolling || scrollbarDragActive
						|| timelineScrollBar.pressed
						|| scrollbarReleasePending
						|| root.performanceChatScrollRunning
					// Stationary thumb warming pins geometry at the timeline level so a
					// delegate reuse or preview-identity update cannot transfer or discard
					// the selected viewport reservation.
					property var stationaryThumbWarmPins: ({})
					property int stationaryThumbWarmPinSerial: 0
					property int stationaryThumbWarmReceiptEpoch: 0
					property var stationaryThumbWarmReceipts: []
					readonly property int stationaryThumbWarmPinCount:
						Object.keys(stationaryThumbWarmPins).length
					readonly property int stationaryThumbWarmCurrentDwellPinCount: {
						let count = 0
						const keys = Object.keys(stationaryThumbWarmPins)
						for (let index = 0; index < keys.length; ++index) {
							const pin = stationaryThumbWarmPins[keys[index]]
							if (pin
									&& Number(pin.pressEpoch)
										=== Number(stationaryThumbWarmGate.pressEpoch)
									&& Number(pin.dwellGeneration)
										=== Number(stationaryThumbWarmGate.dwellGeneration))
								++count
						}
						return count
					}
					function stationaryThumbWarmPin(stableId) {
						return stationaryThumbWarmPins[String(stableId || "")] || null
					}
					function stationaryThumbWarmPinMatchesCurrentScope(pin, stableId) {
						return !!pin && String(pin.id || "") === String(stableId || "")
							&& String(pin.scopeToken || "")
								=== String(activeScope.scopeToken || "")
							&& Number(pin.scopeGeneration)
								=== Number(scopePresentationGeneration)
					}
					function stationaryThumbWarmPinMatchesReceipt(pin, receipt) {
						return stationaryThumbWarmPinMatchesCurrentScope(pin, receipt.stableId)
							&& String(pin.scopeToken || "")
								=== String(receipt.scopeToken || "")
							&& Number(pin.scopeGeneration)
								=== Number(receipt.scopeGeneration)
							&& Number(pin.pressEpoch) === Number(receipt.pressEpoch)
							&& Number(pin.dwellGeneration)
								=== Number(receipt.dwellGeneration)
							&& Number(pin.serial) === Number(receipt.pinSerial)
					}
					function invalidateStationaryThumbWarmReceipts() {
						++stationaryThumbWarmReceiptEpoch
						stationaryThumbWarmReceipts = []
						stationaryThumbWarmReceiptTimer.stop()
					}
					function queueStationaryThumbWarmReceipt(stableId, scopeToken,
							scopeGeneration, pressEpoch, dwellGeneration, pinSerial,
							outcome, rowVisible, outerHeight, probeHeight) {
						const completion = String(outcome || "")
						const pin = stationaryThumbWarmPin(stableId)
						const receipt = {
							"receiptEpoch": stationaryThumbWarmReceiptEpoch,
							"stableId": String(stableId || ""),
							"scopeToken": String(scopeToken || ""),
							"scopeGeneration": Number(scopeGeneration),
							"pressEpoch": Number(pressEpoch),
							"dwellGeneration": Number(dwellGeneration),
							"pinSerial": Number(pinSerial),
							"outcome": completion,
							"rowVisible": rowVisible === true,
							"outerHeight": Number(outerHeight || 0),
							"probeHeight": Number(probeHeight || 0)
						}
						if ((completion !== "painted" && completion !== "terminal")
								|| !stationaryThumbWarmPinMatchesReceipt(pin, receipt))
							return false
						const nextReceipts = stationaryThumbWarmReceipts.slice()
						for (let index = 0; index < nextReceipts.length; ++index) {
							const queued = nextReceipts[index]
							if (Number(queued.pinSerial) === receipt.pinSerial
									&& String(queued.outcome) === completion)
								return true
						}
						nextReceipts.push(receipt)
						stationaryThumbWarmReceipts = nextReceipts
						stationaryThumbWarmReceiptTimer.restart()
						return true
					}
					function drainStationaryThumbWarmReceipts() {
						const receipts = stationaryThumbWarmReceipts
						stationaryThumbWarmReceipts = []
						for (let index = 0; index < receipts.length; ++index) {
							const receipt = receipts[index]
							if (Number(receipt.receiptEpoch)
									!== Number(stationaryThumbWarmReceiptEpoch))
								continue
							const pin = stationaryThumbWarmPin(receipt.stableId)
							if (!stationaryThumbWarmPinMatchesReceipt(pin, receipt))
								continue
							const owns = stationaryThumbWarmGate.owns(receipt.stableId,
								Number(receipt.pressEpoch), Number(receipt.dwellGeneration))
							const retains = stationaryThumbWarmGate.retains(receipt.stableId,
								Number(receipt.pressEpoch), Number(receipt.dwellGeneration))
							if (receipt.outcome === "painted") {
								if (owns)
									stationaryThumbWarmGate.finishAdmission(receipt.stableId,
										Number(receipt.pressEpoch),
										Number(receipt.dwellGeneration), "painted")
								continue
							}
							if (owns)
								stationaryThumbWarmGate.finishAdmission(receipt.stableId,
									Number(receipt.pressEpoch),
									Number(receipt.dwellGeneration), "terminal")
							else if (retains)
								stationaryThumbWarmGate.retireAdmission(receipt.stableId,
									Number(receipt.pressEpoch),
									Number(receipt.dwellGeneration), "terminal")
							if (!receipt.rowVisible)
								clearEqualStationaryThumbWarmTerminal(receipt.stableId,
									receipt.outerHeight, receipt.probeHeight, false, true)
						}
						if (stationaryThumbWarmReceipts.length > 0)
							stationaryThumbWarmReceiptTimer.restart()
					}
					function stationaryThumbDelegateActuallyVisible(item) {
						if (!item || item.performanceTimelineDelegate !== true
								|| item.accessibilityPooled === true || !item.visible)
							return false
						const point = item.mapToItem(timeline, 0, 0)
						return point.y + Number(item.height || 0) > 0.5
							&& point.y < height - 0.5
					}
					function stationaryThumbVisibleStableIds() {
						const candidates = []
						const children = contentItem ? contentItem.children : []
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!stationaryThumbDelegateActuallyVisible(item))
								continue
							const point = item.mapToItem(timeline, 0, 0)
							if (point.y + item.height <= 0.5 || point.y >= height - 0.5)
								continue
							const key = String(item.stableId || "")
							if (key.length > 0)
								candidates.push({ "id": key, "viewportY": point.y })
						}
						candidates.sort(function(left, right) {
							return left.viewportY - right.viewportY
						})
						return candidates.map(function(candidate) { return candidate.id })
					}
					function stationaryThumbWarmCanAdmit(stableId) {
						const key = String(stableId || "")
						const item = timelineDelegateForStableId(key)
						if (key.length === 0 || !stationaryThumbDelegateActuallyVisible(item))
							return false
						const deferred = item.lightweightDeferredPresentation === true
						if (deferred && item.richPresentationReserved !== true)
							return false
						if (!deferred && item.stationaryThumbWarmEligible !== true)
							return false
						// Pins from earlier stops retain only their captured geometry. They
						// must not consume the warming budget for the viewport currently
						// parked under the physical thumb.
						return !!stationaryThumbWarmPin(key)
							|| stationaryThumbWarmCurrentDwellPinCount
								< stationaryThumbWarmGate.maximumAdmissionsPerDwell
					}
					function installStationaryThumbWarmPin(stableId, pressEpoch,
							dwellGeneration) {
						const key = String(stableId || "")
						const item = timelineDelegateForStableId(key)
						if (!stationaryThumbWarmCanAdmit(key) || !item)
							return false
						const existing = stationaryThumbWarmPin(key)
						const deferred = item.lightweightDeferredPresentation === true
						const nextPins = Object.assign({}, stationaryThumbWarmPins)
						const serial = ++stationaryThumbWarmPinSerial
						nextPins[key] = {
							"id": key,
							"scopeToken": String(activeScope.scopeToken || ""),
							"scopeGeneration": Number(scopePresentationGeneration),
							"serial": serial,
							"outerHeight": existing
								? Number(existing.outerHeight || item.height)
								: Number(item.height || deferredMessageRowHeight),
							"probeHeight": existing
								? Number(existing.probeHeight || item.viewportProbeHeight)
								: Number(item.viewportProbeHeight || item.height
									|| deferredMessageRowHeight),
							"pressEpoch": Number(pressEpoch),
							"dwellGeneration": Number(dwellGeneration)
						}
						stationaryThumbWarmPins = nextPins
						if (deferred && chatModel.materializeDeferredMessage(key)) {
							stationaryThumbWarmGate.pauseActiveForMembershipRevision()
							scheduleStationaryThumbWarmReconcile()
							return true
						}
						if (!deferred && item.admitStationaryThumbWarm(
								pressEpoch, dwellGeneration))
							return true
						const rolledBackPins = Object.assign({}, stationaryThumbWarmPins)
						if (existing)
							rolledBackPins[key] = existing
						else
							delete rolledBackPins[key]
						stationaryThumbWarmPins = rolledBackPins
						return false
					}
					function revalidateStationaryThumbWarmAdmission(stableId, pressEpoch,
							dwellGeneration) {
						const key = String(stableId || "")
						const pin = stationaryThumbWarmPin(key)
						const item = timelineDelegateForStableId(key)
						if (!stationaryThumbWarmPinMatchesCurrentScope(pin, key)
								|| Number(pin.pressEpoch) !== Number(pressEpoch)
								|| Number(pin.dwellGeneration) !== Number(dwellGeneration)
								|| !stationaryThumbDelegateActuallyVisible(item)
								|| item.lightweightDeferredPresentation === true
								|| item.stationaryThumbWarmEligible !== true)
							return false
						return true
					}
					function clearStationaryThumbWarmPin(stableId) {
						const key = String(stableId || "")
						if (key.length === 0 || !stationaryThumbWarmPin(key))
							return false
						const nextPins = Object.assign({}, stationaryThumbWarmPins)
						delete nextPins[key]
						stationaryThumbWarmPins = nextPins
						return true
					}
					function retireStationaryThumbWarmPin(stableId, reason) {
						const pin = stationaryThumbWarmPin(stableId)
						if (!pin)
							return false
						stationaryThumbWarmGate.retireAdmission(stableId,
							Number(pin.pressEpoch), Number(pin.dwellGeneration), reason)
						return clearStationaryThumbWarmPin(stableId)
					}
					function clearStationaryThumbWarmPins(reason) {
						invalidateStationaryThumbWarmReceipts()
						stationaryThumbWarmGate.invalidateIdentity(
							String(reason || "pins-cleared"))
						stationaryThumbWarmPins = ({})
					}
					function clearStationaryThumbWarmPinForCommit(stableId,
							outerHeight, probeHeight) {
						const pin = stationaryThumbWarmPin(stableId)
						if (!pin || !isFinite(outerHeight) || !isFinite(probeHeight)
								|| outerHeight <= 0 || probeHeight <= 0)
							return false
						return clearStationaryThumbWarmPin(stableId)
					}
					function clearEqualStationaryThumbWarmTerminal(stableId,
							outerHeight, probeHeight, rowVisible, terminal) {
						const pin = stationaryThumbWarmPin(stableId)
						if (!pin || rowVisible === true || terminal !== true)
							return false
						if (Math.abs(Number(pin.outerHeight) - Number(outerHeight)) > 0.5
								|| Math.abs(Number(pin.probeHeight) - Number(probeHeight)) > 0.5)
							return false
						return clearStationaryThumbWarmPin(stableId)
					}
					property int stationaryThumbWarmReconcilePass: 0
					readonly property int maximumStationaryThumbWarmReconcilePasses: 32
					property bool stationaryThumbWarmReconcilePending: false
					readonly property bool stationaryThumbWarmReconcileBlockerActive:
						timelineScrollBar.pressed || scrollbarDragActive
						|| scrollbarReleasePending || moving || dragging || flicking
						|| timelineScrollHandler.scrolling
						|| root.performanceChatScrollRunning
						|| scopePresentationPending || scopePresentationFinalizing
						|| scopeResetPending || prependAnchorActive
						|| prependMutationInProgress || viewportMembershipUpdatePending
						|| viewportMembershipTimer.running
					onStationaryThumbWarmReconcileBlockerActiveChanged: {
						if (!stationaryThumbWarmReconcileBlockerActive
								&& stationaryThumbWarmReconcilePending
								&& stationaryThumbWarmPinCount > 0)
							scheduleStationaryThumbWarmReconcile()
					}
					onStationaryThumbWarmPinCountChanged: {
						if (stationaryThumbWarmPinCount > 0)
							return
						stationaryThumbWarmReconcilePending = false
						stationaryThumbWarmReconcilePass = 0
						stationaryThumbWarmReconcileTimer.stop()
					}
					function stationaryThumbWarmModelGeometry(stableId) {
						const key = String(stableId || "")
						const rowIndex = key.length > 0
							? chatModel.rowForStableId(key) : -1
						if (rowIndex < 0)
							return { "available": false, "outerHeight": 0, "probeHeight": 0 }
						const row = chatModel.get(rowIndex) || ({})
						const estimatedHeight = coldMessageRowHeight(row.deferredPresentationHeight)
						return {
							"available": isFinite(estimatedHeight) && estimatedHeight > 0,
							"outerHeight": estimatedHeight,
							"probeHeight": estimatedHeight
						}
					}
					function stationaryThumbWarmReconcileBlocked() {
						return stationaryThumbWarmReconcileBlockerActive
					}
					function scheduleStationaryThumbWarmReconcile() {
						if (stationaryThumbWarmPinCount === 0) {
							stationaryThumbWarmReconcilePending = false
							return
						}
						stationaryThumbWarmReconcilePending = true
						if (stationaryThumbWarmReconcileBlocked())
							return
						stationaryThumbWarmReconcilePending = false
						stationaryThumbWarmReconcilePass = 0
						stationaryThumbWarmReconcileTimer.restart()
					}
					function rearmStationaryThumbWarmReconcile() {
						if (stationaryThumbWarmPinCount > 0
								&& stationaryThumbWarmReconcilePass
									< maximumStationaryThumbWarmReconcilePasses) {
							stationaryThumbWarmReconcilePending = true
							if (stationaryThumbWarmReconcileBlocked())
								return
							stationaryThumbWarmReconcilePending = false
							stationaryThumbWarmReconcileTimer.restart()
						}
					}
					function reconcileStationaryThumbWarmPins() {
						if (stationaryThumbWarmPinCount === 0) {
							stationaryThumbWarmReconcilePending = false
							return
						}
						if (stationaryThumbWarmReconcileBlocked()) {
							stationaryThumbWarmReconcilePending = true
							return
						}
						stationaryThumbWarmReconcilePending = false
						++stationaryThumbWarmReconcilePass
						let retryNeeded = false
						const pinnedIds = Object.keys(stationaryThumbWarmPins)
						for (let index = 0; index < pinnedIds.length; ++index) {
							const key = pinnedIds[index]
							const pin = stationaryThumbWarmPin(key)
							if (!pin)
								continue
							const item = timelineDelegateForStableId(key)
							if (item && item.lightweightDeferredPresentation !== true) {
								const wouldBeOuterHeight =
									item.stationaryThumbWouldBeOuterHeight()
								const wouldBeProbeHeight =
									item.stationaryThumbWouldBeProbeHeight()
								const retained = stationaryThumbWarmGate.retains(key,
									Number(pin.pressEpoch), Number(pin.dwellGeneration))
								if (!stationaryThumbDelegateActuallyVisible(item) && !retained
										&& Math.abs(Number(pin.outerHeight)
											- wouldBeOuterHeight) <= 0.5
										&& Math.abs(Number(pin.probeHeight)
											- wouldBeProbeHeight) <= 0.5) {
									clearStationaryThumbWarmPin(key)
									continue
								}
								if (typeof item.schedulePresentationHeightObservation === "function")
									item.schedulePresentationHeightObservation()
								if (!stationaryThumbDelegateActuallyVisible(item)
										&& item.paintedTerminal === true
										&& clearEqualStationaryThumbWarmTerminal(key,
											wouldBeOuterHeight,
											wouldBeProbeHeight, false, true))
									continue
								retryNeeded = true
								continue
							}
							const modelGeometry = stationaryThumbWarmModelGeometry(key)
							if (!modelGeometry.available) {
								clearStationaryThumbWarmPin(key)
								continue
							}
							if (modelGeometry.available
									&& Math.abs(Number(pin.outerHeight)
										- modelGeometry.outerHeight) <= 0.5
									&& Math.abs(Number(pin.probeHeight)
										- modelGeometry.probeHeight) <= 0.5) {
								clearStationaryThumbWarmPin(key)
								continue
							}
							if (modelGeometry.available)
								chatModel.materializeDeferredMessage(key)
							retryNeeded = true
						}
						if (retryNeeded)
							rearmStationaryThumbWarmReconcile()
					}
					property var richLoaderResumeQueue: []
					// Plain visible rows do not own provider/image resources. Admit the
					// same bounded two-row cohort as the model's cold append drain while
					// the replacement scope is still hidden, without claiming the
					// serialized rich-content token.
					readonly property int plainScopeResumeAdmissionsPerFrame: 2
					// Asynchronous Loader incubation can span several polish turns. Do
					// not activate another complete message tree until the previous one
					// reports Ready (or Error), otherwise their final polish work can
					// converge into one long GUI heartbeat gap.
					property string richLoaderResumeActiveId: ""
					readonly property bool richLoaderResumeBusy:
						richLoaderResumeTimer.running || richLoaderResumeQueue.length > 0
						|| richLoaderResumeActiveId.length > 0
					// Once the visible viewport is fully quiet, warm exactly one row no
					// farther away than a native 112 px wheel step. The stable-id grants
					// that row narrowly scoped access to the existing serial hydration and
					// Loader token; it never broadens visible readiness diagnostics.
					property string nearViewportPrefetchActiveId: ""
					property string nearViewportWarmId: ""
					property string nearViewportPrefetchReadyId: ""
					property string nearViewportPrefetchViewportSignature: ""
					property bool nearViewportPrefetchOwnsViewportAnchor: false
					// One global static decode plus one queued source is enough to hide
					// wheel lookahead without retaining video, animation or player state.
					property string staticPreviewPreheatActiveId: ""
					property string staticPreviewPreheatActiveSource: ""
					property string staticPreviewPreheatActiveOrigin: ""
					property int staticPreviewPreheatActiveSweepIndex: -1
					property var staticPreviewPreheatQueue: []
					property var staticPreviewPreheatCompletedSources: ({})
					property var staticPreviewPreheatRetryAfter: ({})
					property var staticPreviewPreheatRetryOrder: []
					// Six decoded stills cover a long desktop viewport plus the next wheel
					// page without retaining provider trees. At 1280x960 this remains a
					// deliberately small, bounded cache; players and animations never enter it.
					readonly property int maximumRetainedStaticPreviews: 6
					readonly property int maximumStaticPreviewRetryEntries: 32
					property int staticPreviewPreheatDirection: -1
					property real staticPreviewPreheatLastContentY: 0
					property bool staticPreviewPreheatTeleportPending: false
					readonly property bool staticPreviewThumbBlocked:
						timelineScrollBar.pressed || scrollbarDragActive
						|| scrollbarReleasePending
					property int staticPreviewSweepIndex: -1
					property bool staticPreviewSweepStarted: false
					property bool staticPreviewSweepCompleted: false
					property int staticPreviewSweepRemainingAttempts: 0
					property var staticPreviewSweepAttemptedSources: ({})
					property var staticPreviewScopeRetainedSources: ({})
					property int staticPreviewScopeRetainedCount: 0
					onRichScrollInProgressChanged: {
						if (richScrollInProgress) {
							staticPreviewSweepTimer.stop()
							cancelActiveNearViewportPrefetchForScroll()
							richScrollRetentionTarget = Math.min(maximumRichScrollRetentionBuffer,
								Math.max(richScrollRetentionTarget, contentHeight + height))
							if (richScrollRetentionBuffer < richScrollRetentionTarget
									&& !richScrollRetentionRampTimer.running)
								richScrollRetentionRampTimer.start()
							chatModel.presentationBusy = true
							previewHydrationTimer.stop()
							richContentResumeTimer.stop()
							richLoaderResumeTimer.stop()
							richLoaderResumeActiveId = ""
							deferredMaterializationTimer.stop()
							deferredMaterializationQueue = []
							clearDeferredMaterializationOwnership()
							suspendRichContentForScroll()
							return
						}
					scheduleStaticPreviewLookahead()
					if (Object.keys(root.pendingPreviewHydrationIds).length > 0
								&& !previewHydrationTimer.running)
							previewHydrationTimer.start()
						scheduleRichContentResume()
					}
					property string prependAnchorId: ""
					property real prependAnchorOffset: 0
					property real prependAnchorContentY: 0
					property real prependAnchorTargetY: 0
					property real prependAnchorInsertedExtent: 0
					property bool prependAnchorActive: false
					property bool deferredPrependAnchorActive: false
					property string performanceHistoryProbeAnchorId: ""
					property bool prependMutationInProgress: false
					property bool restoringPrependAnchor: false
					property int prependAnchorRetries: 0
					property int prependAnchorCorrectionFrames: 0
					property int prependSettlementTransactionSequence: 0
					property string prependSettlementState: "SETTLED"
					property string prependSettlementTransactionId: ""
					property string prependSettlementScopeToken: ""
					property int prependSettlementPresentationGeneration: 0
					property string prependSettlementExpectedOrderedIdHash: ""
					property int prependSettlementExpectedMessageCount: 0
					property string prependSettlementAnchorId: ""
					property real prependSettlementExpectedAnchorOffset: 0
					property int prependSettlementStartFrameSerial: 0
					property double prependSettlementStartedAt: 0
					property bool prependSettlementPendingObserved: false
					property string prependSettlementFirstGeometryFingerprint: ""
					property int prependSettlementFirstFrameSerial: 0
					property string prependSettlementSecondObservationUpdateTransactionId: ""
					property string prependSettlementRetirementUpdateTransactionId: ""
					property int prependSettlementReceiptSequence: 0
					property var prependSettlementReceipt: ({})
					readonly property bool prependSettlementHeightCommitOwned:
						prependAnchorActive && !chatModel.prependPending
						&& (prependSettlementState === "MODEL_APPLIED"
							|| prependSettlementState === "ANCHOR_CORRECTION_ACTIVE"
							|| prependSettlementState === "FIRST_STABLE_GEOMETRY_OBSERVED"
							|| prependSettlementState === "SECOND_MATCHING_GEOMETRY_OBSERVED")
					property var deferredPrependRichIds: ({})
					property var deferredMaterializationQueue: []
					// Rich source promotion and asynchronous row construction are one
					// singleton transaction. Proven plain rows use a separate bounded
					// cohort whose complete identity set is published before mutation.
					property string deferredMaterializationActiveId: ""
					property int deferredMaterializationActiveGeneration: -1
					property var plainDeferredMaterializationActive: ({})
					readonly property int plainDeferredMaterializationsPerFrame: 2
					property int plainDeferredSourceAdmissionCount: 0
					property int serializedDeferredSourceAdmissionCount: 0
					readonly property real deferredMessageRowHeight: 58
					function coldMessageRowHeight(estimate) {
						// Reserve provider space before the card enters the viewport. A
						// compact summary that later becomes a portrait card changes the
						// scroll range by hundreds of pixels after the gesture has ended.
						const height = Number(estimate || deferredMessageRowHeight)
						return isFinite(height) ? Math.max(deferredMessageRowHeight, height)
							: deferredMessageRowHeight
					}
					// The queued tail itself is the supersede window: no full row is
					// admitted until the final append has completed. Yield one event turn
					// here, but do not add a fixed delay to every cold room entry.
					readonly property int coldScopeMaterializationDelayMs: 0
					// Cold finalization keeps rich and unknown rows one-at-a-time so
					// provider trees cannot converge into a startup hitch. After a real scroll, admit the
					// two rows that can straddle the viewport together: their outer Loaders
					// incubate asynchronously and the measured frame budget has ample
					// headroom, while serial admission otherwise leaves a visible handoff
					// gap at the oldest loaded boundary.
					readonly property int deferredMaterializationsPerFrame:
						scopePresentationFinalizing ? 1 : 2
					property string viewportAnchorId: ""
					property real viewportAnchorOffset: 0
					property bool viewportAnchorActive: false
					property bool restoringViewportAnchor: false
					property int viewportAnchorRetries: 0
					property bool viewportAnchorReleasePending: false
					// A committed rich-row height must let ListView polish before the
					// existing anchor is released. Keep this as timeline ownership state;
					// restoreViewportAnchor() remains the only contentY writer.
					property bool viewportAnchorQuietReleaseActive: false
					property int viewportAnchorQuietReleaseGeneration: 0
					property real viewportAnchorQuietReleaseObservedContentHeight: 0
					property int viewportAnchorQuietReleaseQuietObservations: 0
					property double viewportAnchorQuietReleaseStartedAt: 0
					readonly property int viewportAnchorQuietReleaseDeadlineMs: 192
					// Scrollbar input owns the viewport until delayed ListView movement
					// signals have drained. No tail-follow or anchor writer may reinterpret
					// the thumb-selected position during that window.
					property bool scrollbarDragActive: false
					property bool scrollbarReleasePending: false
					property int bottomFollowGeneration: 0
					property int pendingBottomFollowGeneration: 0
					property bool pendingBottomFollowOwnsScopeReset: false
					property bool topAnchorActive: false
					property bool restoringTopAnchor: false
					property bool topAnchorProgrammaticMovementGuard: false
					property bool explicitWheelInputPending: false
					property int explicitWheelInputGeneration: 0
					property real explicitWheelInputStartY: 0
					property bool explicitNavigationMovement: false
					property string lastMovementClassification: ""
					property int explicitNavigationMovementCount: 0
					property int automaticLayoutMovementCount: 0
					property bool stickToBottom: true
					property bool followTailAfterInsert: false
					property int pendingTailInsertCount: 0
					property int pendingTailMessageCount: 0
					property bool restoringBottom: false
					property bool scopeResetPending: false
					property bool viewportMembershipUpdatePending: false
					onScopeResetPendingChanged: scheduleViewportMembershipUpdate()
					// A replacement remains private until its real visible rows own stable,
					// measured geometry. Finalization is construction, not presentation.
					property bool scopePresentationPending: false
					property bool scopePresentationFinalizing: false
					property bool scopePresentationFrameCommitPending: false
					readonly property bool scopePresentationContentVisible:
						scopePresentationStartedAt <= 0
							|| !scopePresentationPending
							|| (scopePresentationStableFrameCommittedAt > 0
								&& scopePresentationGeometryMatches())
					property bool scopePresentationFastPath: false
					property int scopePresentationFastPathAttemptCount: 0
					property string scopePresentationFirstFastPathBlocker: ""
					property string scopePresentationLastFastPathBlocker: ""
					property bool scopePresentationObservationActive: false
					property bool scopePresentationForcedByDeadline: false
					property int scopePresentationGeneration: 0
					property bool scopePresentationMilestonesDirty: true
					property int orderedMessageRevision: 0
					property int orderedIdentityRevision: -1
					property int orderedIdentityGeneration: -1
					property var orderedIdentityCache: ({ "count": 0, "hash": "" })
					readonly property bool expectedHistoryLoaderVisible:
						!!activeScope.canLoadOlder || activeScope.loadingState === "older"
					readonly property int expectedHistoryLoaderHeight: expectedHistoryLoaderVisible ? 48 : 0
					onScopePresentationGenerationChanged: scopePresentationMilestonesDirty = true
					onScopePresentationFirstVisibleStableRowsCommittedAtChanged:
						scopePresentationMilestonesDirty = true

					property int frameSwappedSerial: 0
					property int scopePresentationMutationCount: 0
					property int scopePresentationTailContainedMutationCount: -1
					property int scopePresentationTailCorrectionCount: 0
					property int scopePresentationExposedHeightChangeCount: 0
					property int scopePresentationExposedTailCorrectionCount: 0
					property real scopePresentationExposedTailTravel: 0
					property double scopePresentationStartedAt: 0
					property double scopePresentationDeadlineAt: 0
					property double scopePresentationDataReadyAt: 0
					property double scopePresentationStableReadyAt: 0
					property double scopePresentationFirstStableFrameCommittedAt: 0
					property double scopePresentationSecondMatchingStableFrameCommittedAt: 0
					property double scopePresentationFirstVisibleStableRowsCommittedAt: 0
					property double scopePresentationStableFrameCommittedAt: 0
					property double scopePresentationVisibleFrameCommittedAt: 0
					property double scopePresentationLoadingFrameCommittedAt: 0
					property double scopePresentationComposerInteractiveAt: 0
					property double scopePresentationRichHydratedAt: 0
					property double scopePresentationDegradedReadyAt: 0
					property double scopePresentationProviderResultAppliedAt: 0
					property double scopePresentationProviderResultVisiblePaintAt: 0
					property string scopePresentationProviderResultSerial: ""
					property double scopePresentationCompletedAt: 0
					property int scopePresentationOuterLoaderLoadedCount: 0
					property double scopePresentationFirstOuterLoaderLoadedAt: 0
					property double scopePresentationLastOuterLoaderLoadedAt: 0
					property int scopePresentationPlainRowAdmissionCount: 0
					property var scopePresentationPlainLoaderEvents: []
					property double scopePresentationFirstPlainRowAdmissionAt: 0
					property double scopePresentationLastPlainRowAdmissionAt: 0
					property int scopePresentationRowLoaderLoadedCount: 0
					property double scopePresentationFirstRowLoaderLoadedAt: 0
					property double scopePresentationLastRowLoaderLoadedAt: 0
					property int scopePresentationHeightCommitCount: 0
					property double scopePresentationFirstHeightCommitAt: 0
					property double scopePresentationLastHeightCommitAt: 0
					property int scopePresentationStableFrameCount: 0
					property string scopePresentationStableFrameFirstVisibleId: ""
					property real scopePresentationStableFrameFirstVisibleOffset: 0
					property real scopePresentationStableFrameContentY: 0
					property real scopePresentationStableFrameContentHeight: 0
					property real scopePresentationStableFrameViewportHeight: 0
					property real scopePresentationStableFrameTopMargin: 0
					property real bottomFollowThreshold: 48
					onStickToBottomChanged: {
						if (stickToBottom)
							pendingTailMessageCount = 0
					}

					function isNearBottom() {
						if (count === 0 || atYEnd)
							return true
						const maximumY = tailContentYTarget()
						return maximumY - contentY <= bottomFollowThreshold
					}

					function isAtBottom() {
						if (count === 0 || atYEnd)
							return true
						const maximumY = tailContentYTarget()
						return maximumY - contentY <= 0.5
					}

					function tailContentYTarget() {
						const naturalTailY = originY + contentHeight - height
						// ListView margins extend the legal Flickable bounds but do not
						// translate delegates. An underfilled sparse scope therefore owns
						// the negative margin-backed tail coordinate explicitly.
						return sparseBottomAlignmentActive
							? naturalTailY : Math.max(originY, naturalTailY)
					}

					function markVisibleScopeReadIfEligible() {
						if (!root.active || scopePresentationPending || activeScope.activity
								|| Number(activeScope.unreadCount || 0) <= 0 || !isNearBottom())
							return false
						uiCommands.markActiveScopeRead()
						return true
					}

					function invalidateBottomFollow() {
						++bottomFollowGeneration
						bottomFollowTimer.stop()
					}

					function beginScrollbarDrag() {
						if (scrollbarDragActive)
							return true
						// Timer ids are lexical QML ids, not properties on this ListView.
						// Keep the complete press transition here so a failed lookup cannot
						// leave the previous viewport anchor alive under the thumb.
						scrollbarReleaseGuardTimer.stop()
						scrollbarReleasePending = false
						scrollbarDragActive = true
						pauseStaticPreviewPreheatForThumb()
						// Publish thumb ownership before cancelFlick(), which can emit
						// movementEnded synchronously for native precision-wheel kinetics.
						timelineScrollHandler.cancelNavigationForOwnershipLoss()
						takeExplicitNavigationOwnership("scrollbar")
						return true
					}

					function finishScrollbarDrag() {
						if (!scrollbarDragActive)
							return
						scrollbarReleasePending = true
						scrollbarDragActive = false
						// Attached ScrollBar writes do not guarantee a ListView
						// movementEnded pair. Release the explicit-navigation latch here
						// so delegate-height convergence cannot remain blocked forever.
						explicitNavigationMovement = false
						invalidateBottomFollow()
						const atBottom = isAtBottom()
						stickToBottom = atBottom
						if (atBottom) {
							// Exact-tail ownership supersedes every message anchor. This is
							// normally already true after beginScrollbarDrag(), but keeping it
							// explicit makes release robust against queued recovery callbacks.
							releaseViewportAnchor()
							releaseTopAnchor()
							markVisibleScopeReadIfEligible()
						} else {
							// Capture only the selected viewport identity/offset. Any later
							// rich-height recovery can preserve it without a tail jump.
							captureViewportAnchor(true)
						}
						// Release does not request a positional correction. These are the
						// bounded, non-positional wakeups needed after a fixed RangeValue
						// jump or thumb drag.
						scheduleViewportMembershipUpdate()
						scrollbarReleaseGuardTimer.restart()
					}

					function requestBottomFollow() {
						if (scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return
						if (stickToBottom && !prependAnchorActive && !restoringBottom
								&& !scopePresentationPending && !bottomFollowTimer.running
								&& !root.performanceChatScrollRunning) {
							const maximumY = tailContentYTarget()
							// Metadata-only preview updates emit dataChanged as their cache
							// state advances. If geometry is already exactly at the tail,
							// starting a 16 ms correction timer only makes the otherwise
							// stable first paint report busy and delays input readiness.
							if (!scopeResetPending
									&& Math.abs(maximumY - contentY) <= 0.5) {
								markVisibleScopeReadIfEligible()
								return
							}
							pendingBottomFollowGeneration = bottomFollowGeneration
							pendingBottomFollowOwnsScopeReset = scopeResetPending
							bottomFollowTimer.start()
						}
					}

					function beginScopeChange() {
						scopePresentationMilestonesDirty = true
						scopePresentationStartedAt = Date.now()
						// Publish the scope owner before cancelFlick(): Qt may synchronously
						// emit movementEnded while retiring a native precision-wheel flick.
						scopePresentationPending = true
						clearStationaryThumbWarmPins("scope-change")
						timelineScrollHandler.cancelNavigationForOwnershipLoss()
						richScrollRetentionRampTimer.stop()
						viewportMembershipTimer.stop()
						viewportMembershipUpdatePending = false
						// Block retiring-scope DTO updates before stopping the optional
						// preheaters below. Their final Image callbacks can otherwise land
						// in the same turn as the replacement snapshot.
						chatModel.presentationBusy = true
						staticPreviewLookaheadTimer.stop()
						staticPreviewSweepTimer.stop()
						staticPreviewPreheatActiveSource = ""
						staticPreviewPreheatActiveId = ""
						staticPreviewPreheatActiveOrigin = ""
						staticPreviewPreheatActiveSweepIndex = -1
						staticPreviewPreheatQueue = []
						staticPreviewPreheatDirection = -1
						staticPreviewPreheatLastContentY = contentY
						staticPreviewPreheatTeleportPending = false
						staticPreviewSweepIndex = -1
						staticPreviewSweepStarted = false
						staticPreviewSweepCompleted = false
						staticPreviewSweepRemainingAttempts = maximumRetainedStaticPreviews
						staticPreviewSweepAttemptedSources = ({})
						staticPreviewScopeRetainedSources = ({})
						staticPreviewScopeRetainedCount = 0
						staticPreviewRetryTimer.stop()
						nearViewportPrefetchTimer.stop()
						nearViewportPrefetchDeadlineTimer.stop()
						nearViewportPrefetchActiveId = ""
						nearViewportWarmId = ""
						nearViewportPrefetchReadyId = ""
						nearViewportPrefetchViewportSignature = ""
						nearViewportPrefetchOwnsViewportAnchor = false
						richScrollRetentionBuffer = 0
						richScrollRetentionTarget = 0
						// A provider/image result from the retiring scope may arrive
						// between the selection change and the replacement snapshot.
						// Keep those DTO updates in the existing bounded source queue
						// until the new scope is stable; otherwise QML can polish a rich
						// card that is immediately removed in the same cold-entry window.
						releasePrependAnchor()
						clearDeferredPrependRichRows()
						releaseViewportAnchor()
						releaseTopAnchor()
						invalidateBottomFollow()
						richContentResumeTimer.stop()
						richLoaderResumeTimer.stop()
						richLoaderResumeQueue = []
						richLoaderResumeActiveId = ""
						deferredMaterializationTimer.stop()
						deferredMaterializationQueue = []
						clearDeferredMaterializationOwnership()
						plainDeferredSourceAdmissionCount = 0
						serializedDeferredSourceAdmissionCount = 0
						coldScopeMaterializationDelayTimer.restart()
						scopePresentationFastPathTimer.stop()
						scopePresentationQuietTimer.stop()
						scopePresentationDeadlineTimer.stop()
						scopePresentationFinalizeStartTimer.stop()
						scopePresentationFinalizeQuietTimer.stop()
						scopePresentationFinalizeDeadlineTimer.stop()
						scopePresentationObservationTimer.stop()
						++scopePresentationGeneration
						scopePresentationFinalizing = false
						scopePresentationFastPath = false
						scopePresentationObservationActive = false
						scopePresentationForcedByDeadline = false
						scopePresentationFastPathAttemptCount = 0
						scopePresentationFirstFastPathBlocker = ""
						scopePresentationLastFastPathBlocker = ""
						scopePresentationMutationCount = 0
						scopePresentationTailContainedMutationCount = -1
						scopePresentationTailCorrectionCount = 0
						scopePresentationExposedHeightChangeCount = 0
						scopePresentationExposedTailCorrectionCount = 0
						scopePresentationExposedTailTravel = 0
						scopePresentationDeadlineAt = scopePresentationStartedAt
							+ scopePresentationDeadlineTimer.defaultInterval
						scopePresentationDataReadyAt = 0
						scopePresentationStableReadyAt = 0
						scopePresentationFirstStableFrameCommittedAt = 0
						scopePresentationSecondMatchingStableFrameCommittedAt = 0
						scopePresentationFirstVisibleStableRowsCommittedAt = 0
						scopePresentationStableFrameCommittedAt = 0
						scopePresentationVisibleFrameCommittedAt = 0
						scopePresentationLoadingFrameCommittedAt = 0
						scopePresentationComposerInteractiveAt = 0
						scopePresentationRichHydratedAt = 0
						scopePresentationDegradedReadyAt = 0
						scopePresentationProviderResultAppliedAt = 0
						scopePresentationProviderResultVisiblePaintAt = 0
						scopePresentationProviderResultSerial = ""
						scopePresentationCompletedAt = 0
						scopePresentationOuterLoaderLoadedCount = 0
						scopePresentationFirstOuterLoaderLoadedAt = 0
						scopePresentationLastOuterLoaderLoadedAt = 0
						scopePresentationPlainRowAdmissionCount = 0
						scopePresentationPlainLoaderEvents = []
						scopePresentationFirstPlainRowAdmissionAt = 0
						scopePresentationLastPlainRowAdmissionAt = 0
						scopePresentationRowLoaderLoadedCount = 0
						scopePresentationFirstRowLoaderLoadedAt = 0
						scopePresentationLastRowLoaderLoadedAt = 0
						scopePresentationHeightCommitCount = 0
						scopePresentationFirstHeightCommitAt = 0
						scopePresentationLastHeightCommitAt = 0
						scopePresentationFrameCommitPending = false
						scopePresentationStableFrameCount = 0
						scopePresentationStableFrameFirstVisibleId = ""
						scopePresentationStableFrameFirstVisibleOffset = 0
						scopePresentationStableFrameContentY = 0
						scopePresentationStableFrameContentHeight = 0
						scopePresentationStableFrameViewportHeight = 0
						scopePresentationStableFrameTopMargin = 0
						root.clearPreviewHydrationQueue()
						explicitWheelInputPending = false
						++explicitWheelInputGeneration
						explicitWheelInputStartY = contentY
						explicitNavigationMovement = false
						lastMovementClassification = ""
						explicitNavigationMovementCount = 0
						automaticLayoutMovementCount = 0
						performanceHistoryProbeAnchorId = ""
						followTailAfterInsert = false
						pendingTailInsertCount = 0
						pendingTailMessageCount = 0
						stickToBottom = true
						scopeResetPending = true
						scheduleViewportMembershipUpdate()
						scopePresentationQuietTimer.start()
						armScopePresentationDeadline()
						scheduleScopePresentationFastPath()
					}

					function noteScopePresentationDataReady() {
						scopePresentationMilestonesDirty = true
						if (!scopePresentationPending || activeScope.loading
								|| chatModel.appendPending || chatModel.prependPending)
							return false
						if (scopePresentationDataReadyAt <= 0)
							scopePresentationDataReadyAt = Date.now()
						return true
					}

					function scopePresentationDeadlineExpired() {
						return scopePresentationDeadlineAt > 0
							&& Date.now() >= scopePresentationDeadlineAt
					}

					function armScopePresentationDeadline() {
						const remaining = scopePresentationDeadlineAt - Date.now()
						if (scopePresentationDeadlineAt <= 0 || remaining <= 0) {
							scopePresentationDeadlineTimer.stop()
							scopePresentationForcedByDeadline = true
							return false
						}
						scopePresentationDeadlineTimer.interval = Math.max(1,
							Math.ceil(remaining))
						scopePresentationDeadlineTimer.restart()
						return true
					}

					function armScopePresentationStableFrameCommit() {
						scopePresentationStableReadyAt = Date.now()
						scopePresentationFirstStableFrameCommittedAt = 0
						scopePresentationSecondMatchingStableFrameCommittedAt = 0
						scopePresentationFirstVisibleStableRowsCommittedAt = 0
						scopePresentationStableFrameCommittedAt = 0
						scopePresentationVisibleFrameCommittedAt = 0
						scopePresentationFrameCommitPending = true
						scopePresentationStableFrameCount = 0
						scopePresentationStableFrameFirstVisibleId = ""
						scopePresentationStableFrameFirstVisibleOffset = 0
						scopePresentationStableFrameContentY = contentY
						scopePresentationStableFrameContentHeight = contentHeight
						scopePresentationStableFrameViewportHeight = height
						scopePresentationStableFrameTopMargin = topMargin
						root.update()
					}

					function completeScopePresentationVisibleFrameCommit() {
						if (!scopePresentationPending || !scopePresentationFrameCommitPending
								|| scopePresentationStableFrameCommittedAt <= 0)
							return
						const generation = scopePresentationGeneration
						scopePresentationFirstVisibleStableRowsCommittedAt = Date.now()
						scopePresentationVisibleFrameCommittedAt =
							scopePresentationFirstVisibleStableRowsCommittedAt
						scopePresentationCompletedAt = scopePresentationVisibleFrameCommittedAt
						scopePresentationFrameCommitPending = false
						scopePresentationPending = false
						scopePresentationFinalizing = false
						scopePresentationObservationActive = count > 0
						if (scopePresentationObservationActive)
							scopePresentationObservationTimer.restart()
						chatModel.presentationBusy = false
						root.coldPreviewHydrationPacingActive = true
						scheduleRichContentResume()
						Qt.callLater(function() {
							if (generation === timeline.scopePresentationGeneration)
								timeline.queueVisiblePreviewHydration()
						})
						markVisibleScopeReadIfEligible()
					}

					function recoverScopePresentationFrameRegression() {
						if (!scopePresentationPending || !scopePresentationFrameCommitPending)
							return false
						// Finalization proved a complete hidden frame, but readiness or geometry
						// changed before the visible-frame transaction could commit. Retire that
						// snapshot as one cohort so the next frame cannot inherit stale evidence.
						scopePresentationFrameCommitPending = false
						scopePresentationStableReadyAt = 0
						scopePresentationFirstStableFrameCommittedAt = 0
						scopePresentationSecondMatchingStableFrameCommittedAt = 0
						scopePresentationFirstVisibleStableRowsCommittedAt = 0
						scopePresentationStableFrameCommittedAt = 0
						scopePresentationVisibleFrameCommittedAt = 0
						scopePresentationStableFrameCount = 0
						scopePresentationStableFrameFirstVisibleId = ""
						scopePresentationStableFrameFirstVisibleOffset = 0
						scopePresentationForcedByDeadline = scopePresentationForcedByDeadline
							|| scopePresentationDeadlineExpired()
						if (scopePresentationForcedByDeadline) {
							// The absolute scope deadline has already elapsed. Re-enter bounded
							// fallback finalization immediately; a readiness-only regression need
							// not emit another model/content-height mutation that could wake us.
							scopePresentationQuietTimer.stop()
							scopePresentationDeadlineTimer.stop()
							finishScopePresentation(true)
						} else {
							scopePresentationQuietTimer.restart()
							if (!armScopePresentationDeadline()) {
								// The deadline can cross between the expired probe above and
								// timer rearming. Do not leave that boundary case waiting for
								// another mutation that may never arrive.
								scopePresentationQuietTimer.stop()
								finishScopePresentation(true)
							}
						}
						root.update()
						return true
					}

					function recordScopePresentationFrameMilestones() {
						if (!scopePresentationMilestonesDirty && !scopePresentationPending)
							return
						if (scopePresentationStartedAt <= 0)
							return
						const committedAt = Date.now()
						if (scopePresentationLoadingFrameCommittedAt <= 0
								&& emptyConversationState.visible
								&& emptyConversationState.visualLoading)
							scopePresentationLoadingFrameCommittedAt = committedAt
						if (scopePresentationComposerInteractiveAt <= 0
								&& activeScope.canSend && composerSurface.visible
								&& composerSurface.height > 1 && composerInput.enabled)
							scopePresentationComposerInteractiveAt = committedAt
						if (scopePresentationFirstVisibleStableRowsCommittedAt <= 0)
							return

						scopePresentationMilestonesDirty = false
						const diagnostics = visiblePresentationDiagnostics()
						const providerAppliedAt = Number(
							diagnostics.providerResultAppliedAt || 0)
						const providerSerial = String(
							diagnostics.providerResultSerial || "")
						if (providerAppliedAt > 0 && providerSerial.length > 0
								&& (scopePresentationProviderResultSerial !== providerSerial
									|| scopePresentationProviderResultAppliedAt
										!== providerAppliedAt)) {
							scopePresentationProviderResultSerial = providerSerial
							scopePresentationProviderResultAppliedAt = providerAppliedAt
							scopePresentationProviderResultVisiblePaintAt = 0
							scopePresentationRichHydratedAt = 0
							scopePresentationDegradedReadyAt = 0
						}
						if (scopePresentationProviderResultVisiblePaintAt <= 0
								&& diagnostics.providerResultPainted === true
								&& providerAppliedAt > 0 && providerSerial.length > 0)
							scopePresentationProviderResultVisiblePaintAt = committedAt

						const terminalGeometryReady = pendingVisibleHydrationCount() === 0
							&& pendingVisibleRichRecoveryCount() === 0
							&& diagnostics.blankCount === 0
							&& diagnostics.blankPreviewSurfaceCount === 0
							&& diagnostics.presentationHeightPendingCount === 0
							&& diagnostics.presentationHeightMismatchCount === 0
							&& diagnostics.paintedLoadingCount === 0
						if (!terminalGeometryReady)
							return
						if (diagnostics.paintedTerminalCount > 0
								|| diagnostics.deadlineFallbackCount > 0) {
							scopePresentationRichHydratedAt = 0
							if (scopePresentationDegradedReadyAt <= 0)
								scopePresentationDegradedReadyAt = committedAt
							return
						}
						if (diagnostics.inactiveRichResourceCount === 0
								&& scopePresentationRichHydratedAt <= 0) {
							scopePresentationDegradedReadyAt = 0
							scopePresentationRichHydratedAt = committedAt
						}
					}

					function scopePresentationGeometryMatches() {
						// Read every scalar before comparing. This binding also runs between
						// the certified hidden frame and scene-graph synchronization, when a
						// late viewport mutation must hide the rows before they can be painted.
						const currentY = contentY
						const currentHeight = contentHeight
						const viewportHeight = height
						const margin = topMargin
						const contentOffset = contentItem ? contentItem.y : 0
						const headerOffset = headerItem ? headerItem.y : 0
						const firstVisible = firstVisibleMessageDelegate()
						const firstId = firstVisible ? String(firstVisible.stableId || "") : ""
						const firstOffset = firstVisible
							? Number(firstVisible.mapToItem(timeline, 0, 0).y) : 0
						return scopePresentationStableFrameCount > 0
							&& isFinite(contentOffset) && isFinite(headerOffset)
							&& firstId === scopePresentationStableFrameFirstVisibleId
							&& Math.abs(firstOffset - scopePresentationStableFrameFirstVisibleOffset) <= 0.5
							&& Math.abs(currentY - scopePresentationStableFrameContentY) <= 0.5
							&& Math.abs(currentHeight - scopePresentationStableFrameContentHeight) <= 0.5
							&& Math.abs(viewportHeight - scopePresentationStableFrameViewportHeight) <= 0.5
							&& Math.abs(margin - scopePresentationStableFrameTopMargin) <= 0.5
					}

					function observeScopePresentationFrame() {
						recordScopePresentationFrameMilestones()
						if (scopePresentationStableReadyAt <= 0
								|| !scopePresentationFrameCommitPending
								|| !scopePresentationPending || scopePresentationFinalizing
								|| scopeResetPending || chatModel.appendPending
								|| chatModel.prependPending) {
							return
						}
						const diagnostics = visiblePresentationDiagnostics()
						const presentationReady = visiblePresentationReady(diagnostics)
							|| (scopePresentationForcedByDeadline
								&& visiblePresentationDeadlineReady(diagnostics))
						if (!presentationReady
								|| (!scopePresentationForcedByDeadline
									&& visiblePresentationHeightConvergencePending())) {
							recoverScopePresentationFrameRegression()
							return
						}
						// This callback follows the first frame that contained visible rows
						// with the loading pane removed. Only now may input, UIA, and
						// presentationBusy ownership be released.
						if (scopePresentationStableFrameCommittedAt > 0) {
							if (!scopePresentationContentVisible || !scopePresentationGeometryMatches()) {
								recoverScopePresentationFrameRegression()
								return
							}
							completeScopePresentationVisibleFrameCommit()
							return
						}
						const firstVisible = firstVisibleMessageDelegate()
						const firstVisibleId = firstVisible
							? String(firstVisible.stableId || "") : ""
						const firstVisibleOffset = firstVisible
							? Number(firstVisible.mapToItem(timeline, 0, 0).y) : 0
						const stableWithPrevious = scopePresentationStableFrameCount > 0
							&& firstVisibleId === scopePresentationStableFrameFirstVisibleId
							&& Math.abs(firstVisibleOffset
								- scopePresentationStableFrameFirstVisibleOffset) <= 0.5
							&& Math.abs(contentY - scopePresentationStableFrameContentY) <= 0.5
							&& Math.abs(contentHeight
								- scopePresentationStableFrameContentHeight) <= 0.5
							&& Math.abs(height
								- scopePresentationStableFrameViewportHeight) <= 0.5
							&& Math.abs(topMargin
								- scopePresentationStableFrameTopMargin) <= 0.5
						scopePresentationStableFrameCount = stableWithPrevious
							? scopePresentationStableFrameCount + 1 : 1
						if (!stableWithPrevious) {
							scopePresentationFirstStableFrameCommittedAt = Date.now()
							scopePresentationSecondMatchingStableFrameCommittedAt = 0
							scopePresentationStableFrameCommittedAt = 0
						}
						scopePresentationStableFrameFirstVisibleId = firstVisibleId
						scopePresentationStableFrameFirstVisibleOffset = firstVisibleOffset
						scopePresentationStableFrameContentY = contentY
						scopePresentationStableFrameContentHeight = contentHeight
						scopePresentationStableFrameViewportHeight = height
						scopePresentationStableFrameTopMargin = topMargin
						if (scopePresentationStableFrameCount >= 2) {
							scopePresentationSecondMatchingStableFrameCommittedAt = Date.now()
							scopePresentationStableFrameCommittedAt =
								scopePresentationSecondMatchingStableFrameCommittedAt
							root.update()
							return
						}
						root.update()
					}

					function scheduleScopePresentationFastPath() {
						if (scopePresentationPending && !scopePresentationFinalizing)
							scopePresentationFastPathTimer.restart()
					}

					function rowNeedsDeferredPresentation(row) {
						if (!row)
							return false
						if (row.bodyHydrationPending === true)
							return true
						const preview = row.preview || ({})
						if (Object.keys(preview).length > 0)
							return true
						const attachments = row.attachments || []
						return attachments.length > 0
					}

					function rejectScopePresentationFastPath(reason) {
						const blocker = String(reason || "unknown")
						if (scopePresentationFirstFastPathBlocker.length === 0)
							scopePresentationFirstFastPathBlocker = blocker
						scopePresentationLastFastPathBlocker = blocker
						return false
					}

					function canCompleteScopePresentationFastPath() {
						++scopePresentationFastPathAttemptCount
						scopePresentationLastFastPathBlocker = ""
						if (!scopePresentationPending)
							return rejectScopePresentationFastPath("not-pending")
						if (scopePresentationFinalizing)
							return rejectScopePresentationFastPath("finalizing")
						if (activeScope.loading)
							return rejectScopePresentationFastPath("scope-loading")
						if (chatModel.appendPending)
							return rejectScopePresentationFastPath("append-pending")
						// Activity rows are already materialized client log entries. They do
						// not need the rich-message hydration gate used by conversations.
						if (activeScope.activity)
							return true
						if (chatModel.count === 0)
							return true
						// Large histories must reject before touching the ListView or
						// probing individual model rows. The normal bounded presentation
						// path owns those scopes.
						if (chatModel.count > 10)
							return rejectScopePresentationFastPath("model-count")
						if (count === 0)
							return rejectScopePresentationFastPath("timeline-empty")
						if (height <= 1)
							return rejectScopePresentationFastPath("viewport-height")
						// Reject rich, hydrating rows from the lightweight path before
						// forceLayout(). A cold attachment-heavy room can otherwise make
						// that synchronous probe construct and polish the entire visible
						// delegate tree on one GUI heartbeat merely to discover that the
						// fast path was never eligible.
						for (let index = 0; index < count; ++index) {
							if (rowNeedsDeferredPresentation(chatModel.get(index)))
								return rejectScopePresentationFastPath(
									"deferred-row:" + String(index))
						}
						forceLayout()
						const pendingHydration = pendingScopeHydrationCount()
						if (pendingHydration > 0)
							return rejectScopePresentationFastPath(
								"pending-hydration:" + String(pendingHydration))
						return true
					}

					function completeScopePresentationFastPath() {
						if (!canCompleteScopePresentationFastPath())
							return false
						scopePresentationFastPathTimer.stop()
						scopePresentationQuietTimer.stop()
						scopePresentationDeadlineTimer.stop()
						scopePresentationFinalizeQuietTimer.stop()
						scopePresentationFinalizeDeadlineTimer.stop()
						forceLayout()
						if (count > 0) {
							positionTailImmediately()
							restoringBottom = true
							containTailMessageWhenPossible()
							restoringBottom = false
						}
						scopeResetPending = false
						noteScopePresentationDataReady()
						scopePresentationFastPath = true
						scopePresentationFinalizing = false
						armScopePresentationStableFrameCommit()
						return true
					}

					function noteScopePresentationMutation() {
						scopePresentationMilestonesDirty = true
						if (!scopePresentationPending)
							return
						++scopePresentationMutationCount
						if (scopePresentationFrameCommitPending) {
							// A mutation between the hidden stability observations and the
							// first visible swap invalidates that reveal transaction. Re-enter
							// bounded finalization instead of exposing a stale height map.
							recoverScopePresentationFrameRegression()
							return
						}
						if (scopePresentationFinalizing)
							scopePresentationFinalizeQuietTimer.restart()
						else
							scopePresentationQuietTimer.restart()
					}

					function pendingScopeHydrationCount() {
						const children = contentItem ? contentItem.children : []
						let pending = 0
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (item && item.contentNeedsHydration === true
									&& item.inHydrationWindow === true
									&& item.accessibilityPooled !== true)
								++pending
						}
						return pending
					}

					function pendingVisibleHydrationCount() {
						const children = contentItem ? contentItem.children : []
						let pending = 0
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (item && item.contentNeedsHydration === true
									&& item.inVisibleViewport === true
									&& item.accessibilityPooled !== true)
								++pending
						}
						return pending
					}

					function richResourceRecoveryPending(item) {
						return !!item
							&& item.richResourcesRequired === true
							&& item.contentNeedsHydration !== true
							&& item.richResourcesActive !== true
					}

					function pendingVisibleRichRecoveryCount() {
						const children = contentItem ? contentItem.children : []
						let pending = 0
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true
									|| item.lightweightDeferredPresentation === true
									|| item.inVisibleViewport !== true
									|| item.richResourcesRequired !== true)
								continue
							if (item.messageRowReady !== true
									|| item.previewPresentationReady !== true
									|| item.richResourceActivationPending === true
									|| richResourceRecoveryPending(item))
								++pending
						}
						return pending
					}

					function finishScopePresentation(forcedByDeadline) {
						if (!scopePresentationPending || scopePresentationFinalizing)
							return
						if (!noteScopePresentationDataReady()) {
							scopePresentationQuietTimer.restart()
							armScopePresentationDeadline()
							return
						}
						const deadlineMode = !!forcedByDeadline
							|| scopePresentationForcedByDeadline
							|| scopePresentationDeadlineExpired()
						scopePresentationFinalizing = true
						scopePresentationForcedByDeadline =
							scopePresentationForcedByDeadline || deadlineMode
						scopePresentationFastPathTimer.stop()
						scopePresentationQuietTimer.stop()
						scopePresentationDeadlineTimer.stop()
						// Keep tail containment, rich-loader admission, and preview
						// hydration in a separate turn from the final model insert.
						scopePresentationFinalizeStartTimer.restart()
					}

					function continueScopePresentationFinalization() {
						if (!scopePresentationPending || !scopePresentationFinalizing)
							return
						positionTailImmediately()
						restoringBottom = true
						containTailMessageWhenPossible()
						restoringBottom = false
						// Admit only the rows intersecting the real viewport, one per
						// frame. Keep the presentation surface pending until those rows
						// have real delegate content, so "ready" can never expose a blank
						// timeline or a fixed-height summary that immediately jumps.
						queueVisibleDeferredMaterialization(true)
						scopePresentationFinalizeQuietTimer.start()
						scopePresentationFinalizeDeadlineTimer.start()
					}

					function completeScopePresentationFinalization(forcedByDeadline) {
						if (!scopePresentationPending || !scopePresentationFinalizing)
							return
						const deadlineMode = !!forcedByDeadline
							|| scopePresentationForcedByDeadline
						if (deadlineMode) {
							scopePresentationForcedByDeadline = true
							coldScopeMaterializationDelayTimer.stop()
							deferredMaterializationTimer.stop()
							deferredMaterializationQueue = []
							clearDeferredMaterializationOwnership()
							richLoaderResumeTimer.stop()
							richLoaderResumeQueue = []
							richLoaderResumeActiveId = ""
							forceVisibleDeadlineFallbacks()
						}
						if (!deadlineMode && coldScopeMaterializationDelayTimer.running) {
								scopePresentationFinalizeQuietTimer.restart()
								return
						}
							forceLayout()
							// Re-evaluate the tail after each paced source-row promotion.
							// Replacing a 58 px summary with a rich card can move the
							// maximum by thousands of pixels; without this correction the
							// hidden ListView keeps materializing the obsolete top viewport
							// until the deadline releases a visibly blank conversation.
							positionTailImmediately()
							restoringBottom = true
							containTailMessageWhenPossible()
							restoringBottom = false
							if (!deadlineMode && (viewportMembershipTimer.running
									|| viewportMembershipUpdatePending)) {
								scopePresentationFinalizeQuietTimer.restart()
								return
							}
							if (!deadlineMode)
								queueVisibleDeferredMaterialization(true)
							// Source materialization activates the asynchronous outer
							// delegate, but its message tree still enters as a geometry
							// shell. Promote only the newly visible shells through the
							// existing one-at-a-time recovery queue before declaring ready.
							if (!deadlineMode)
								queueRichLoaderResumes()
							const diagnostics = visiblePresentationDiagnostics()
							// Offscreen/stale source rows may remain in the paced queue after
							// the real viewport is complete. They are background work and must
							// not keep the hidden loading surface up until its deadline.
							const presentationReady = visiblePresentationReady(diagnostics)
								|| (deadlineMode && visiblePresentationDeadlineReady(diagnostics))
							if (!presentationReady
									|| (!deadlineMode
										&& visiblePresentationHeightConvergencePending())) {
								scopePresentationFinalizeQuietTimer.restart()
								return
							}
						const generation = scopePresentationGeneration
						const mutationCount = scopePresentationMutationCount
						scopePresentationFinalizeQuietTimer.stop()
						positionTailImmediately()
						restoringBottom = true
						containTailMessageWhenPossible()
						restoringBottom = false
						Qt.callLater(function() {
							if (generation !== timeline.scopePresentationGeneration
									|| !timeline.scopePresentationPending
									|| !timeline.scopePresentationFinalizing)
								return
							timeline.positionTailImmediately()
							timeline.restoringBottom = true
							timeline.containTailMessageWhenPossible()
							timeline.restoringBottom = false
							// An ordinary quiet pass waits for membership reconciliation. The
							// hard deadline instead owns bounded real-row fallbacks, so stale
							// membership work cannot extend the liveness bound.
							if (deadlineMode)
								timeline.forceVisibleDeadlineFallbacks()
							if (!deadlineMode && (viewportMembershipTimer.running
									|| timeline.viewportMembershipUpdatePending)) {
								scopePresentationFinalizeQuietTimer.restart()
								return
							}
							timeline.forceLayout()
							const settledDiagnostics =
								timeline.visiblePresentationDiagnostics()
							const settledReady = timeline.visiblePresentationReady(
								settledDiagnostics) || (deadlineMode
									&& timeline.visiblePresentationDeadlineReady(settledDiagnostics))
							if ((!deadlineMode
									&& mutationCount !== timeline.scopePresentationMutationCount)
									|| !settledReady
									|| (!deadlineMode
										&& timeline.visiblePresentationHeightConvergencePending())) {
									if (!deadlineMode) {
										timeline.queueVisibleDeferredMaterialization(true)
										timeline.queueRichLoaderResumes()
									}
									scopePresentationFinalizeQuietTimer.restart()
									return
							}
							scopePresentationFinalizeDeadlineTimer.stop()
							// The visible viewport has passed the settled full-row gate.
							// Retire the last frame-paced source candidate before revealing
							// it: that candidate was selected against an older height map and
							// can now be completely offscreen. Let the next real scroll build
							// a fresh viewport queue instead of mutating hidden geometry after
							// the loading surface has gone away.
							deferredMaterializationTimer.stop()
							timeline.deferredMaterializationQueue = []
							// Keep the now-known initial page geometry alive through the
							// first real scroll. Offscreen rich rows remain lightweight and
							// provider resources are still admitted only inside their
							// hydration window, so this retains positions without eagerly
							// constructing every embed.
							timeline.richScrollRetentionTarget = Math.min(
								timeline.maximumRichScrollRetentionBuffer,
								Math.max(timeline.baseCacheBuffer,
									timeline.contentHeight + timeline.height))
							timeline.richScrollRetentionBuffer =
								timeline.richScrollRetentionTarget
							timeline.scopePresentationForcedByDeadline =
								timeline.scopePresentationForcedByDeadline || !!forcedByDeadline
							timeline.scopeResetPending = false
							timeline.armScopePresentationStableFrameCommit()
							timeline.scopePresentationFinalizing = false
						})
					}

					function recordTailCorrection(previousY, nextY) {
						const distance = Math.abs(nextY - previousY)
						if (distance <= 0.5)
							return
						++scopePresentationTailCorrectionCount
						if (!scopePresentationPending && scopePresentationObservationActive) {
							++scopePresentationExposedTailCorrectionCount
							scopePresentationExposedTailTravel += distance
						}
					}

					function setTailContentY(nextY) {
						if (scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return
						const minimumY = sparseBottomAlignmentActive
							? tailContentYTarget() : originY
						const maximumY = tailContentYTarget()
						const boundedY = Math.max(minimumY, Math.min(maximumY, nextY))
						if (Math.abs(contentY - boundedY) <= 0.5)
							return
						const previousY = contentY
						contentY = boundedY
						recordTailCorrection(previousY, contentY)
					}

					function positionTailImmediately() {
						if (!stickToBottom || prependAnchorActive || scrollbarDragActive
								|| scrollbarReleasePending || timelineScrollBar.pressed)
							return
						// A fully contained rich tail can sit above the absolute Flickable
						// maximum because its inline footer is intentionally traded for the
						// complete card. Reapplying the maximum on every quiet pass would
						// undo containment, schedule another viewport-membership scan, and
						// starve finalization until its hard deadline. A real row/height
						// mutation increments the generation below and reopens positioning.
						if (scopePresentationFinalizing
								&& scopePresentationTailContainedMutationCount
									=== scopePresentationMutationCount)
							return
						const maximumY = tailContentYTarget()
						restoringBottom = true
						setTailContentY(maximumY)
						restoringBottom = false
					}

					function firstVisibleMessageDelegate() {
						const children = contentItem ? contentItem.children : []
						let candidate = null
						let candidateViewportY = Number.POSITIVE_INFINITY
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.stableId === undefined || !item.visible
									|| item.accessibilityPooled === true)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							// Explicit y reads make the reveal binding observe layout changes;
							// mapToItem's native transform reads are not QML dependencies.
							if (!isFinite(item.y) || point.y + item.height <= 0.5)
								continue
							if (!candidate || point.y < candidateViewportY) {
								candidate = item
								candidateViewportY = point.y
							}
						}
						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let index = 0;
								deferredRepeater && index < deferredRepeater.count; ++index) {
							const item = deferredRepeater.itemAt(index)
							if (!item || !item.visible)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							if (!isFinite(item.y) || point.y + item.height <= 0.5)
								continue
							if (!candidate || point.y < candidateViewportY) {
								candidate = item
								candidateViewportY = point.y
							}
						}
						return candidate
					}

					function visiblePresentationDiagnostics() {
						const diagnostics = {
							"visibleCount": 0,
							"readyCount": 0,
							"fullMessageCount": 0,
							"deadlineFallbackCount": 0,
							"deferredSummaryCount": 0,
							"blankCount": 0,
							"outerLoaderReadyCount": 0,
							"rowMaterializedCount": 0,
							"rowLoaderReadyCount": 0,
							"previewReadyCount": 0,
							"blankPreviewSurfaceCount": 0,
							"paintedSummaryCount": 0,
							"paintedSurfaceCount": 0,
							"paintedCompleteCount": 0,
							"paintedLoadingCount": 0,
							"paintedTerminalCount": 0,
							"duplicateHandoffSummaryCount": 0,
							"pinnedCount": 0,
							"inactiveRichResourceCount": 0,
							"presentationHeightPendingCount": 0,
							"presentationHeightMismatchCount": 0,
							"maximumPresentationHeightError": 0,
							"providerResultAppliedAt": 0,
							"providerResultSerial": "",
							"providerResultPainted": false,
							"rows": []
						}

						function record(item) {
							if (!item || item.performanceTimelineDelegate !== true
									|| !item.visible || item.accessibilityPooled === true)
								return
							const point = item.mapToItem(timeline, 0, 0)
							if (point.y + item.height <= 0.5 || point.y >= height - 0.5)
								return
							++diagnostics.visibleCount
							const preview = item.preview || ({})
							const previewKeyCount = Object.keys(preview).length
							const paintedSummary = item.paintedSummary === true
							const paintedSurface = item.paintedSurface === true
						const paintedLoading = item.paintedLoading === true
						const paintedTerminal = item.paintedTerminal === true
						const duplicateHandoffSummary =
							item.duplicateHandoffSummaryPainted === true
							const deadlineFallback =
								item.deadlinePresentationFallback === true
						const paintedComplete = paintedSurface || paintedTerminal
								|| (paintedLoading && paintedSummary)
							const providerResultAppliedAt = Number(item.source
								? item.source.providerResultAppliedAt || 0 : 0)
							const providerResultSerial = String(item.source
								? item.source.providerResultSerial || "" : "")
							if (providerResultAppliedAt > diagnostics.providerResultAppliedAt
									&& providerResultSerial.length > 0) {
								diagnostics.providerResultAppliedAt = providerResultAppliedAt
								diagnostics.providerResultSerial = providerResultSerial
								diagnostics.providerResultPainted =
									paintedSurface || paintedTerminal
							}
							const warmPin = stationaryThumbWarmPin(item.stableId)
							if (paintedSummary)
								++diagnostics.paintedSummaryCount
							if (paintedSurface)
								++diagnostics.paintedSurfaceCount
							if (paintedComplete)
								++diagnostics.paintedCompleteCount
							if (paintedLoading)
								++diagnostics.paintedLoadingCount
						if (paintedTerminal)
							++diagnostics.paintedTerminalCount
						if (duplicateHandoffSummary)
							++diagnostics.duplicateHandoffSummaryCount
							if (deadlineFallback)
								++diagnostics.deadlineFallbackCount
							if (warmPin)
								++diagnostics.pinnedCount
							const previewSurfaceState = previewKeyCount === 0
								? "not-required"
								: item.item
									? String(item.item.performancePreviewSurfaceState
										|| "blank").toLowerCase()
									: "blank"
							const blankPreviewSurface =
								item.lightweightDeferredPresentation !== true
								&& item.messageRowReady === true
								&& previewKeyCount > 0
								&& !paintedLoading
								&& previewSurfaceState === "blank"
							if (blankPreviewSurface)
								++diagnostics.blankPreviewSurfaceCount
							const inactiveRichResources =
								timeline.richResourceRecoveryPending(item)
							if (inactiveRichResources)
								++diagnostics.inactiveRichResourceCount
							const loadedImplicitHeight = item.item
								? Number(item.item.implicitHeight || 0) : 0
							const hasLoadedImplicitHeight = isFinite(loadedImplicitHeight)
								&& loadedImplicitHeight > 0
							const expectedPresentationHeight = hasLoadedImplicitHeight
								? Math.max(timeline.deferredMessageRowHeight,
									loadedImplicitHeight) : 0
							const presentationHeightError = hasLoadedImplicitHeight
								? Math.abs(Number(item.height || 0)
									- expectedPresentationHeight) : 0
							const presentationHeightPending = hasLoadedImplicitHeight
								&& item.messageRowReady === true
								&& (item.presentationHeightReleased !== true
									|| presentationHeightError > 1)
							if (presentationHeightPending)
								++diagnostics.presentationHeightPendingCount
							if (presentationHeightError > 1)
								++diagnostics.presentationHeightMismatchCount
							diagnostics.maximumPresentationHeightError = Math.max(
								diagnostics.maximumPresentationHeightError,
								presentationHeightError)
							const presentationRow = Number(
								chatModel.presentationModel.rowForStableId(
									String(item.stableId || "")))
							diagnostics.rows.push({
								"id": String(item.stableId || ""),
								"modelIndex": isFinite(presentationRow) ? presentationRow : -1,
								"title": String(item.title || ""),
								"subtitle": String(item.subtitle || ""),
								"height": Number(item.height || 0),
								"estimatedHeight": Number(item.deferredPresentationHeight || 0),
								"loadedImplicitHeight": hasLoadedImplicitHeight
									? loadedImplicitHeight : 0,
								"viewportY": Number(point.y || 0),
								"viewportProbeHeight": Number(item.viewportProbeHeight || 0),
								"effectiveViewportProbeHeight": Number(
									item.effectiveViewportProbeHeight
										|| item.viewportProbeHeight || 0),
								"paintedSummary": paintedSummary,
								"paintedSurface": paintedSurface,
								"paintedComplete": paintedComplete,
							"paintedLoading": paintedLoading,
							"paintedTerminal": paintedTerminal,
							"duplicateHandoffSummary": duplicateHandoffSummary,
							"deadlineFallback": deadlineFallback,
								"stationaryThumbPinned": !!warmPin,
								"stationaryThumbPinId": warmPin
									? String(warmPin.id || "") : "",
								"stationaryThumbPinOuterHeight": warmPin
									? Number(warmPin.outerHeight || 0) : 0,
								"stationaryThumbPinProbeHeight": warmPin
									? Number(warmPin.probeHeight || 0) : 0,
								"stationaryThumbPinPressEpoch": warmPin
									? Number(warmPin.pressEpoch || 0) : 0,
								"stationaryThumbPinDwellGeneration": warmPin
									? Number(warmPin.dwellGeneration || 0) : 0,
								"richGeometrySettling": item.richGeometrySettling === true,
								"reservedExcess": hasLoadedImplicitHeight
									? Math.max(0, Number(item.height || 0)
										- Math.max(58, loadedImplicitHeight)) : 0,
								"presentationHeightError": presentationHeightError,
								"presentationHeightPending": presentationHeightPending,
								"presentationHeightEligible":
									item.presentationHeightQuietObservationEligible === true,
								"presentationHeightReleasePending":
									item.presentationHeightReleasePending === true,
								"presentationHeightIdentity":
									String(item.currentPresentationHeightIdentity
										? item.currentPresentationHeightIdentity() : ""),
								"presentationHeightSettledIdentity":
									String(item.presentationHeightSettledIdentity || ""),
								"presentationHeightReleased":
									item.presentationHeightReleased === true,
								"presentationHeightSettledHeight": Number(
									item.presentationHeightSettledHeight || 0),
								"lightweight": item.lightweightDeferredPresentation === true,
								"outerReady": item.outerLoaderReady === true,
								"messageReady": item.messageRowReady === true,
								"previewAspect": String(preview.embedAspect || ""),
								"previewSize": String(preview.previewSize || ""),
								"previewProvider": String(preview.provider || preview.previewProvider
									|| preview.providerName || preview.siteName || ""),
								"previewKind": String(preview.previewKind || preview.embedKind || ""),
								"previewState": String(preview.state || ""),
								"previewTitle": String(preview.title || ""),
								"previewUrl": String(preview.url || preview.sourceUrl || ""),
								"previewKeyCount": previewKeyCount,
								"previewSurfaceState": previewSurfaceState,
								"previewMaterializationAdmitted":
									item.previewMaterializationAdmitted === true,
								"previewLoaderStatus": Number(item.previewLoaderStatus),
								"nativeThumbnailReady": item.nativeThumbnailReady === true,
								"previewLoaderIncubationSuppressed":
									item.previewLoaderIncubationSuppressed === true,
								"previewLoaderIncubationWatchdogRunning":
									item.previewLoaderIncubationWatchdogRunning === true,
								"previewLoaderIncubationWatchdogElapsedMs":
									item.previewLoaderIncubationWatchdogStartedAt > 0
										? Math.max(0, Date.now()
											- item.previewLoaderIncubationWatchdogStartedAt) : 0,
								"richResourcesRequired": item.richResourcesRequired === true,
								"richResourcesActive": item.richResourcesActive === true,
								"richResourceActivationPending":
									item.richResourceActivationPending === true,
								"inactiveRichResources": inactiveRichResources,
								"blankPreviewSurface": blankPreviewSurface
							})
							if (item.lightweightDeferredPresentation === true) {
								if (paintedSummary) {
									++diagnostics.deferredSummaryCount
								} else {
									++diagnostics.blankCount
								}
								return
							}
							if (item.outerLoaderReady === true)
								++diagnostics.outerLoaderReadyCount
							if (item.messageRowMaterialized === true)
								++diagnostics.rowMaterializedCount
							if (item.messageContentLoaderReady === true)
								++diagnostics.rowLoaderReadyCount
							if (item.previewPresentationReady === true)
								++diagnostics.previewReadyCount
							if (item.messageRowReady === true && paintedComplete) {
								++diagnostics.fullMessageCount
								++diagnostics.readyCount
							} else if (!paintedSummary) {
								++diagnostics.blankCount
							}
						}

						const children = contentItem ? contentItem.children : []
						for (let index = 0; index < children.length; ++index)
							record(children[index])

						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let index = 0;
								deferredRepeater && index < deferredRepeater.count; ++index)
							record(deferredRepeater.itemAt(index))
						return diagnostics
					}

					function visiblePresentationReady(diagnostics) {
						if (count === 0)
							return true
						return diagnostics.visibleCount > 0
							&& diagnostics.readyCount === diagnostics.visibleCount
							&& diagnostics.fullMessageCount === diagnostics.visibleCount
							&& diagnostics.paintedCompleteCount === diagnostics.visibleCount
							&& diagnostics.deferredSummaryCount === 0
							&& diagnostics.blankCount === 0
							&& diagnostics.blankPreviewSurfaceCount === 0
							&& diagnostics.presentationHeightPendingCount === 0
							&& diagnostics.presentationHeightMismatchCount === 0
							&& diagnostics.maximumPresentationHeightError <= 1
					}

					function visiblePresentationHeightConvergencePending() {
						const diagnostics = visiblePresentationDiagnostics()
						if (diagnostics.presentationHeightPendingCount > 0
								|| diagnostics.presentationHeightMismatchCount > 0)
							return true
						for (let index = 0; index < diagnostics.rows.length; ++index) {
							const row = diagnostics.rows[index]
							if (row.lightweight === true) {
								if (row.deadlineFallback !== true)
									return true
								continue
							}
							if (row.deadlineFallback !== true
									&& (row.outerReady !== true || row.messageReady !== true))
								return true
							if (row.presentationHeightReleased !== true
									&& (row.richGeometrySettling === true
										|| row.inactiveRichResources === true))
								return true
						}
						return false
					}

					function scheduleNextVisiblePresentationHeightSettlement() {
						const diagnostics = visiblePresentationDiagnostics()
						for (let index = 0; index < diagnostics.rows.length; ++index) {
							const row = diagnostics.rows[index] || ({})
							if (row.presentationHeightPending !== true)
								continue
							const stableId = String(row.id || "")
							const item = stableId.length > 0
								? timelineDelegateForStableId(stableId) : null
							if (!item || typeof item.schedulePresentationHeightObservation
									!== "function"
									|| item.presentationHeightQuietObservationEligible !== true)
								continue
							item.schedulePresentationHeightObservation()
							root.update()
							return true
						}
						return false
					}

					function prependSettlementTransactionActive() {
						return prependSettlementTransactionId.length > 0
							&& prependSettlementState !== "SETTLED"
							&& prependSettlementState !== "CANCELLED"
					}

					function prependSettlementFixed(value) {
						const number = Number(value)
						return isFinite(number) ? number.toFixed(3) : "nan"
					}

					function prependOrderedMessageIdentity() {
						if (orderedIdentityRevision === orderedMessageRevision
								&& orderedIdentityGeneration === scopePresentationGeneration)
							return orderedIdentityCache
						const ids = []
						for (let index = 0; index < chatModel.count; ++index) {
							const row = chatModel.get(index) || ({})
							ids.push(String(row.id || ""))
						}
						orderedIdentityCache = {
							"count": ids.length,
							"hash": Qt.md5(ids.join("\u001f"))
						}
						orderedIdentityRevision = orderedMessageRevision
						orderedIdentityGeneration = scopePresentationGeneration
						return orderedIdentityCache
					}

					function prependSettlementGeometrySnapshot() {
						const anchorId = prependSettlementAnchorId
						const anchorItem = anchorId.length > 0
							? timelineDelegateForStableId(anchorId) : null
						const anchorOffset = anchorItem
							? Number(anchorItem.mapToItem(timeline, 0, 0).y || 0) : 0
						const diagnostics = visiblePresentationDiagnostics()
						const timelineOrigin = timeline.mapToItem(root.contentItem, 0, 0)
						const composerOrigin = composerSurface.mapToItem(root.contentItem, 0, 0)
						const jumpOrigin = jumpToLatestButton.mapToItem(root.contentItem, 0, 0)
						const currentTimelineHeader = timeline.headerItem
						const historyLoader = currentTimelineHeader
							? currentTimelineHeader.olderHistoryControl : null
						const jumpBottom = Number(jumpOrigin.y || 0)
							+ Number(jumpToLatestButton.height || 0)
						const composerTop = Number(composerOrigin.y || 0)
						const composerContiguous = composerSurface.height > 1
							&& Math.abs(composerTop - (jumpBottom
								+ Number(jumpToLatestButton.timelineBottomMargin || 0))) <= 1
						const historyLoaderVisible = !!historyLoader && historyLoader.visible
						const historyLoaderHeight = historyLoader
							? Number(historyLoader.height || 0) : 0
						const fingerprint = "count=" + chatModel.count
							+ "|anchor=" + anchorId
							+ "|offset=" + prependSettlementFixed(anchorOffset)
							+ "|contentY=" + prependSettlementFixed(contentY)
							+ "|contentHeight=" + prependSettlementFixed(contentHeight)
							+ "|viewportY=" + prependSettlementFixed(timelineOrigin.y)
							+ "|viewportBottom=" + prependSettlementFixed(
								Number(timelineOrigin.y || 0) + Number(height || 0))
							+ "|loader=" + (historyLoaderVisible ? "1" : "0")
							+ "/" + prependSettlementFixed(historyLoaderHeight)
							+ "|jump=" + (jumpToLatestButton.visible ? "1" : "0")
							+ "/" + prependSettlementFixed(jumpBottom)
							+ "|composer=" + prependSettlementFixed(composerTop)
							+ "/" + prependSettlementFixed(composerSurface.height)
							+ "|blank=" + diagnostics.blankCount
							+ "|height=" + diagnostics.presentationHeightPendingCount
							+ "/" + diagnostics.presentationHeightMismatchCount
						return {
							"fingerprint": fingerprint,
							"anchorAvailable": !!anchorItem,
							"anchorOffset": anchorOffset,
							"blankCount": diagnostics.blankCount,
							"heightPendingCount": diagnostics.presentationHeightPendingCount,
							"heightMismatchCount": diagnostics.presentationHeightMismatchCount,
							"historyLoaderVisible": historyLoaderVisible,
							"historyLoaderHeight": historyLoaderHeight,
							"jumpToLatestVisible": jumpToLatestButton.visible,
							"composerContiguous": composerContiguous
						}
					}

					function beginPrependSettlementTransaction(insertedCount) {
						if (!prependAnchorActive || prependAnchorId.length === 0
								|| insertedCount <= 0)
							return false
						++prependSettlementTransactionSequence
						prependSettlementScopeToken = String(activeScope.scopeToken || "")
						prependSettlementPresentationGeneration = scopePresentationGeneration
						prependSettlementTransactionId = prependSettlementScopeToken + "/"
							+ prependSettlementPresentationGeneration + "/"
							+ prependSettlementTransactionSequence
						prependSettlementExpectedMessageCount = chatModel.count
							+ Math.max(0, Number(insertedCount || 0))
						prependSettlementExpectedOrderedIdHash = ""
						prependSettlementAnchorId = prependAnchorId
						prependSettlementExpectedAnchorOffset = prependAnchorOffset
						prependSettlementStartFrameSerial = frameSwappedSerial
						prependSettlementStartedAt = Date.now()
						prependSettlementPendingObserved = chatModel.prependPending
						prependSettlementFirstGeometryFingerprint = ""
						prependSettlementFirstFrameSerial = 0
						prependSettlementSecondObservationUpdateTransactionId = ""
						prependSettlementState = "PREPEND_STARTED"
						prependSettlementDeadlineTimer.restart()
						return true
					}

					function markPrependSettlementModelApplied() {
						if (!prependSettlementTransactionActive()
								|| prependSettlementState !== "PREPEND_STARTED"
								|| !prependSettlementPendingObserved || chatModel.prependPending)
							return false
						const identity = prependOrderedMessageIdentity()
						if (identity.count !== prependSettlementExpectedMessageCount)
							return false
						prependSettlementExpectedOrderedIdHash = identity.hash
						prependSettlementState = "MODEL_APPLIED"
						root.update()
						return true
					}

					function cancelPrependSettlementTransaction() {
						if (!prependSettlementTransactionActive())
							return
						prependSettlementDeadlineTimer.stop()
						prependSettlementFirstGeometryFingerprint = ""
						prependSettlementFirstFrameSerial = 0
						prependSettlementState = "CANCELLED"
					}

					function publishPrependSettlementReceipt(snapshot, identity) {
						if (!prependSettlementTransactionActive()
								|| prependSettlementState !== "SECOND_MATCHING_GEOMETRY_OBSERVED")
							return false
						const settlementTimestamp = Date.now()
						const settlementDuration = Math.max(0,
							settlementTimestamp - prependSettlementStartedAt)
						if (settlementDuration > 300)
							return false
						const transactionId = prependSettlementTransactionId
						++prependSettlementReceiptSequence
						prependSettlementReceipt = {
							"sequence": prependSettlementReceiptSequence,
							"transaction_id": transactionId,
							"scope_token": prependSettlementScopeToken,
							"presentation_generation": prependSettlementPresentationGeneration,
							"ordered_message_count": identity.count,
							"ordered_message_id_hash": identity.hash,
							"qualifying_frame_count": 2,
							"first_geometry_fingerprint":
								prependSettlementFirstGeometryFingerprint,
							"second_geometry_fingerprint": snapshot.fingerprint,
							"first_frame_serial": prependSettlementFirstFrameSerial,
							"second_frame_serial": frameSwappedSerial,
							"anchor_id": prependSettlementAnchorId,
							"anchor_offset": snapshot.anchorOffset,
							"settlement_timestamp_ms": settlementTimestamp,
							"settlement_duration_ms": settlementDuration
						}
						prependSettlementState = "SETTLEMENT_RECEIPT_PUBLISHED"
						prependSettlementDeadlineTimer.stop()
						releasePrependAnchor(true, transactionId)
						return true
					}

					function observePrependSettlementFrame() {
						if (!prependSettlementTransactionActive())
							return
						if (prependSettlementState === "PREPEND_STARTED") {
							if (!markPrependSettlementModelApplied())
								return
						}
						if (prependSettlementState === "MODEL_APPLIED")
							prependSettlementState = "ANCHOR_CORRECTION_ACTIVE"
						if (prependSettlementState !== "ANCHOR_CORRECTION_ACTIVE"
								&& prependSettlementState !== "FIRST_STABLE_GEOMETRY_OBSERVED")
							return
						// Reject unsettled ownership before walking delegates or hashing rows.
						if (!prependSettlementPendingObserved || chatModel.prependPending
								|| !prependAnchorActive || prependMutationInProgress
								|| prependAnchorCorrectionFrames !== 0
								|| String(activeScope.scopeToken || "") !== prependSettlementScopeToken
								|| scopePresentationGeneration !== prependSettlementPresentationGeneration) {
							prependSettlementFirstGeometryFingerprint = ""
							prependSettlementFirstFrameSerial = 0
							prependSettlementState = "ANCHOR_CORRECTION_ACTIVE"
							return
						}
						const identity = prependOrderedMessageIdentity()
						const snapshot = prependSettlementGeometrySnapshot()
						const identityMatches = prependSettlementPendingObserved
							&& !chatModel.prependPending && prependAnchorActive
							&& !prependMutationInProgress && prependAnchorCorrectionFrames === 0
							&& String(activeScope.scopeToken || "") === prependSettlementScopeToken
							&& scopePresentationGeneration === prependSettlementPresentationGeneration
							&& identity.count === prependSettlementExpectedMessageCount
							&& identity.hash === prependSettlementExpectedOrderedIdHash
							&& snapshot.anchorAvailable
							&& Math.abs(snapshot.anchorOffset
								- prependSettlementExpectedAnchorOffset) <= 0.5
						const surfaceMatches = snapshot.blankCount === 0
							&& snapshot.heightPendingCount === 0
							&& snapshot.heightMismatchCount === 0
							&& snapshot.historyLoaderVisible === expectedHistoryLoaderVisible
							&& Math.abs(snapshot.historyLoaderHeight - expectedHistoryLoaderHeight) <= 1
							&& snapshot.jumpToLatestVisible && snapshot.composerContiguous
						if (!identityMatches || !surfaceMatches
								|| frameSwappedSerial <= prependSettlementStartFrameSerial) {
							prependSettlementFirstGeometryFingerprint = ""
							prependSettlementFirstFrameSerial = 0
							prependSettlementState = "ANCHOR_CORRECTION_ACTIVE"
							return
						}
						if (prependSettlementFirstGeometryFingerprint.length === 0) {
							prependSettlementFirstGeometryFingerprint = snapshot.fingerprint
							prependSettlementFirstFrameSerial = frameSwappedSerial
							prependSettlementState = "FIRST_STABLE_GEOMETRY_OBSERVED"
							if (prependSettlementSecondObservationUpdateTransactionId
									!== prependSettlementTransactionId) {
								prependSettlementSecondObservationUpdateTransactionId =
									prependSettlementTransactionId
								root.update()
							}
							return
						}
						if (frameSwappedSerial <= prependSettlementFirstFrameSerial)
							return
						if (snapshot.fingerprint !== prependSettlementFirstGeometryFingerprint) {
							prependSettlementFirstGeometryFingerprint = snapshot.fingerprint
							prependSettlementFirstFrameSerial = frameSwappedSerial
							return
						}
						prependSettlementState = "SECOND_MATCHING_GEOMETRY_OBSERVED"
						publishPrependSettlementReceipt(snapshot, identity)
					}

					function historyLoadProbeAnchorState() {
						const stableId = performanceHistoryProbeAnchorId
						if (stableId.length === 0)
							return { "id": "", "offset": 0, "visible": false }
						const row = chatModel.presentationModel.rowForStableId(stableId)
						if (row < 0)
							return { "id": stableId, "offset": 0, "visible": false }
						const item = itemAtIndex(row)
						if (!item)
							return { "id": stableId, "offset": 0, "visible": false }
						const point = item.mapToItem(timeline, 0, 0)
						return { "id": stableId, "offset": point.y,
							"visible": point.y + item.height > 0.5 && point.y < height - 0.5 }
					}

					function capturePrependAnchor() {
						if (scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return false
						const item = firstVisibleMessageDelegate()
						if (!item)
							return false
						const nextPrependAnchorId = String(item.stableId || "")
						if (nextPrependAnchorId.length === 0)
							return false
						// A successful structural prepend supersedes any ordinary viewport
						// correction, including a missing-row final-release retry. Failed or
						// input-blocked capture leaves that existing transaction untouched.
						releaseViewportAnchor()
						prependAnchorId = nextPrependAnchorId
						// Holding the absolute top is correct while the existing loaded
						// range recovers after a scroll. Once older rows are requested,
						// however, originY moves to the newly inserted prefix. Hand off
						// exclusively to the stable-message prepend anchor so that the
						// row the user was reading remains at the same viewport offset.
						releaseTopAnchor()
						prependAnchorOffset = item.mapToItem(timeline, 0, 0).y
						prependAnchorContentY = contentY
						prependAnchorTargetY = contentY
						prependAnchorInsertedExtent = 0
						prependAnchorRetries = 0
						prependAnchorCorrectionFrames = 0
						deferredPrependAnchorActive = false
						prependAnchorActive = true
						stickToBottom = false
						// A non-structural data change can preserve an anchor without
						// changing contentHeight. Arm the quiet release here as well as
						// from restorePrependAnchor() so that such an anchor cannot stay
						// active forever and block deferred hydration.
						prependAnchorSettleTimer.restart()
						return true
					}

					function captureViewportAnchor(allowDuringInput) {
						if (stickToBottom || prependAnchorActive || scopePresentationPending
								|| (allowDuringInput !== true && (moving || dragging || flicking
									|| scrollbarDragActive || scrollbarReleasePending
									|| timelineScrollBar.pressed)))
							return false
						if (Math.abs(contentY - originY) <= 0.5) {
							beginTopAnchor()
							return true
						}
						const item = readingAnchorDelegate()
						if (!item)
							return false
						const nextViewportAnchorId = String(item.stableId || "")
						if (nextViewportAnchorId.length === 0)
							return false
						// Validate the replacement first so a transient empty viewport cannot
						// orphan an existing quiet release. A successful recapture explicitly
						// invalidates old prefetch/release ownership before installing the ID.
						releaseViewportAnchor()
						releaseTopAnchor()
						viewportAnchorId = nextViewportAnchorId
						viewportAnchorOffset = item.mapToItem(timeline, 0, 0).y
						viewportAnchorRetries = 0
						viewportAnchorReleasePending = false
						viewportAnchorActive = true
						return true
					}

					function readingAnchorDelegate() {
						const first = firstVisibleMessageDelegate()
						if (!first || first.mapToItem(timeline, 0, 0).y >= 0
								|| (first.messageRowReady === true
									&& first.presentationHeightCommittedHeight > 0))
							return first
						// The visible tail of an unbuilt card can be empty reservation
						// space. Anchor the next row's visible header instead, so resolving
						// that reservation cannot push the text the reader actually sees.
						let candidate = first
						let candidateY = height
						const children = contentItem ? contentItem.children : []
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true || !item.visible)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							if (point.y >= 0 && point.y < candidateY) {
								candidate = item
								candidateY = point.y
							}
						}
						return candidate
					}

					function clearViewportAnchorQuietRelease() {
						++viewportAnchorQuietReleaseGeneration
						if (typeof viewportAnchorQuietReleaseTimer !== "undefined")
							viewportAnchorQuietReleaseTimer.stop()
						viewportAnchorQuietReleaseActive = false
						viewportAnchorQuietReleaseObservedContentHeight = 0
						viewportAnchorQuietReleaseQuietObservations = 0
						viewportAnchorQuietReleaseStartedAt = 0
					}

					function scheduleViewportAnchorQuietRelease() {
						if (!viewportAnchorActive) {
							clearViewportAnchorQuietRelease()
							return false
						}
						if (viewportAnchorQuietReleaseActive) {
							++viewportAnchorQuietReleaseGeneration
							viewportAnchorQuietReleaseObservedContentHeight =
								Number(contentHeight || 0)
							viewportAnchorQuietReleaseQuietObservations = 0
							viewportAnchorQuietReleaseStartedAt = Date.now()
							viewportAnchorQuietReleaseTimer.restart()
							return true
						}
						++viewportAnchorQuietReleaseGeneration
						viewportAnchorQuietReleaseActive = true
						viewportAnchorQuietReleaseObservedContentHeight =
							Number(contentHeight || 0)
						viewportAnchorQuietReleaseQuietObservations = 0
						viewportAnchorQuietReleaseStartedAt = Date.now()
						viewportAnchorQuietReleaseTimer.restart()
						return true
					}

					function viewportAnchorQuietReleaseGeometryBlocked() {
						return stickToBottom || richScrollInProgress || moving || dragging || flicking
							|| timelineScrollHandler.scrolling
							|| scrollbarDragActive || scrollbarReleasePending
							|| timelineScrollBar.pressed
							|| scopePresentationPending || scopePresentationFinalizing
							|| scopePresentationObservationActive || scopeResetPending
							|| prependAnchorActive || deferredPrependAnchorActive
							|| prependMutationInProgress || chatModel.prependPending
							|| restoringPrependAnchor || topAnchorActive
							|| restoringTopAnchor || restoringViewportAnchor
							|| restoringBottom || explicitWheelInputPending
							|| explicitNavigationMovement
							|| root.performanceChatScrollRunning
					}

					function observeViewportAnchorQuietRelease(generation) {
						if (!viewportAnchorQuietReleaseActive
							|| generation !== viewportAnchorQuietReleaseGeneration)
							return
						if (!viewportAnchorActive) {
							viewportAnchorReleasePending = false
							clearViewportAnchorQuietRelease()
							return
						}
						if (viewportAnchorQuietReleaseGeometryBlocked()) {
							viewportAnchorQuietReleaseTimer.restart()
							return
						}
						restoreViewportAnchor()
						if (!viewportAnchorActive) {
							clearViewportAnchorQuietRelease()
							return
						}
						// A release transaction belongs to the whole visible height cohort,
						// not only the first rich row that commits. Keep the same anchor,
						// clear only the quiet phase, and let the next eligible row commit
						// serially under that anchor. A later commit rearms this timer.
						if (visiblePresentationHeightConvergencePending()) {
							clearViewportAnchorQuietRelease()
							Qt.callLater(function() {
								timeline.scheduleNextVisiblePresentationHeightSettlement()
							})
							root.update()
							return
						}
						const currentContentHeight = Number(contentHeight || 0)
						if (!isFinite(currentContentHeight)) {
							viewportAnchorQuietReleaseTimer.restart()
							return
						}
						if (Math.abs(currentContentHeight
							- viewportAnchorQuietReleaseObservedContentHeight) <= 0.5)
							++viewportAnchorQuietReleaseQuietObservations
						else {
							viewportAnchorQuietReleaseObservedContentHeight =
								currentContentHeight
							viewportAnchorQuietReleaseQuietObservations = 1
						}
						const deadlineReached = Date.now()
							- viewportAnchorQuietReleaseStartedAt
							>= viewportAnchorQuietReleaseDeadlineMs
						if (viewportAnchorQuietReleaseQuietObservations >= 2
							|| deadlineReached) {
							// The existing anchor writer performs the final correction and
							// releases only after the new ListView geometry is observable.
							const releaseRestored = restoreViewportAnchor(true)
							if (viewportAnchorActive) {
								if (!releaseRestored && !viewportAnchorReleasePending) {
									// StopAtBounds can make the requested offset temporarily
									// infeasible near the tail. Retain non-tail anchor intent
									// without leaving the timeline in a busy release state.
									stickToBottom = false
									clearViewportAnchorQuietRelease()
									return
								}
								// Missing-row retries or a just-drained ownership blocker
								// still own the release; keep the bounded timer alive until
								// restoreViewportAnchor() can finish it.
								viewportAnchorQuietReleaseTimer.restart()
								return
							}
							clearViewportAnchorQuietRelease()
							return
						}
						viewportAnchorQuietReleaseTimer.restart()
					}

					function releaseViewportAnchor() {
						clearViewportAnchorQuietRelease()
						viewportAnchorRestoreTimer.stop()
						// An explicit release invalidates any speculative prefetch
						// ownership. Its late completion must not release a newer anchor
						// captured by scrollbar or navigation input.
						nearViewportPrefetchOwnsViewportAnchor = false
						viewportAnchorActive = false
						viewportAnchorId = ""
						viewportAnchorRetries = 0
						viewportAnchorReleasePending = false
						Qt.callLater(function() { timeline.scheduleStaticPreviewSweep() })
					}

					function safeStaticPreviewSource(value) {
						const source = String(value === undefined || value === null ? "" : value).trim()
						return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
					}

					function safeStaticPreviewExternalUrl(value) {
						const source = String(value === undefined || value === null ? "" : value)
							.trim().slice(0, 2048)
						return /^(https?:\/\/|mailto:|mumble:\/\/)/i.test(source) ? source : ""
					}

					function safeStaticPreviewDirectMediaUrl(value, kind) {
						const source = String(value === undefined || value === null ? "" : value).trim()
						if (/^https:\/\//i.test(source))
							return source.slice(0, 2048)
						const expectedKind = String(kind || "").toLowerCase()
						if ((expectedKind === "audio" || expectedKind === "video")
								&& new RegExp("^data:" + expectedKind + "\\/[a-z0-9.+-]+;base64,", "i")
									.test(source))
							return source
						return ""
					}

					function staticPreviewMediaSource(item) {
						if (!item)
							return { "eligible": false, "source": "", "topLevelThumbnail": false }
						const kind = String(item.kind || "").toLowerCase()
						const mime = String(item.mime || "").toLowerCase()
						const presentation = String(item.presentation
							|| item.mediaPresentation || "").toLowerCase()
						const branch = String(item.contentBranch || "").toLowerCase()
						const imageItem = kind === "image" || kind === "gif"
							|| kind === "animated-image" || mime.indexOf("image/") === 0
						if (imageItem) {
							const renderSource = safeStaticPreviewSource(item.url)
							const externalSource = safeStaticPreviewExternalUrl(item.externalUrl
								|| (renderSource.length === 0 ? item.url : ""))
							if (renderSource.length === 0 && externalSource.length === 0)
								return { "eligible": false, "source": "", "topLevelThumbnail": false }
							const animated = item.managedAnimated === true || kind === "gif"
								|| kind === "animated-image" || mime === "image/gif"
								|| presentation === "animated-image" || branch.indexOf("animated") >= 0
							return { "eligible": true, "source": animated ? "" : renderSource,
								"topLevelThumbnail": false }
						}
						const playableKind = kind === "audio" || mime.indexOf("audio/") === 0
							? "audio" : kind === "video" || mime.indexOf("video/") === 0
								? "video" : ""
						if (playableKind.length === 0)
							return { "eligible": false, "source": "", "topLevelThumbnail": false }
						const playback = safeStaticPreviewDirectMediaUrl(item.url, playableKind)
						const external = safeStaticPreviewExternalUrl(item.externalUrl || item.url)
						const target = item.directPlayable !== false && playback.length > 0
							? playback : external
						const thumbnail = safeStaticPreviewSource(item.thumbnailUrl)
							|| safeStaticPreviewSource(item.thumbnail)
						const poster = safeStaticPreviewSource(item.posterUrl)
							|| safeStaticPreviewSource(item.poster)
						if (target.length === 0 && thumbnail.length === 0 && poster.length === 0)
							return { "eligible": false, "source": "", "topLevelThumbnail": false }
						return { "eligible": true, "source": poster || thumbnail,
							"topLevelThumbnail": poster.length === 0 && thumbnail.length === 0 }
					}

					function staticPreviewSourceFor(preview) {
						if (!preview || typeof preview !== "object")
							return ""
						const state = String(preview.state
							|| (preview.failed ? "error" : preview.loading ? "loading" : "ready"))
							.toLowerCase()
						const metadata = preview.metadata || ({})
						const thumbnailBlurValue = metadata.thumbnailBlur
						const thumbnailBlur = thumbnailBlurValue === true
							|| thumbnailBlurValue === 1
							|| String(thumbnailBlurValue || "").toLowerCase() === "true"
						// RichPreviewCard requires an explicit reveal for either marker. Apply
						// the same policy before preheating so a retained still can never bypass
						// that surface for even one scroll frame.
						if (String(metadata.contentWarning || "").trim().length > 0
								|| thumbnailBlur)
							return ""
						if (state !== "ready" || preview.failed === true
								|| preview.mediaAnimated === true
								|| String(metadata.mediaPresentation || "").toLowerCase()
									=== "animated-image")
							return ""
						const document = preview.document || ({})
						const typedDocument = Number(document.schemaVersion || 0) === 1
							&& document.commonPresentation === true
						const embeddedProviderPoster = String(preview.embedUrl || "").length > 0
							&& String(preview.embedKind || "").toLowerCase() !== "direct"
						if (embeddedProviderPoster && !typedDocument) {
							const embedPosters = [preview.thumbnailUrl, preview.posterUrl]
							for (let index = 0; index < embedPosters.length; ++index) {
								const source = safeStaticPreviewSource(embedPosters[index])
								if (source.length > 0)
									return source
							}
						}
						const mediaCollection = typedDocument ? document.media : preview.mediaItems
						const mediaCount = mediaCollection && mediaCollection.length !== undefined
							? Number(mediaCollection.length) : 0
						for (let itemIndex = 0; itemIndex < mediaCount; ++itemIndex) {
							const candidate = staticPreviewMediaSource(mediaCollection[itemIndex])
							if (candidate.eligible === true) {
								if (candidate.source.length > 0)
									return candidate.source
								return candidate.topLevelThumbnail && !typedDocument
									? safeStaticPreviewSource(preview.thumbnailUrl) : ""
							}
						}
						if (embeddedProviderPoster && typedDocument) {
							const embedPosters = [preview.thumbnailUrl, preview.posterUrl]
							for (let index = 0; index < embedPosters.length; ++index) {
								const source = safeStaticPreviewSource(embedPosters[index])
								if (source.length > 0)
									return source
							}
						}
						if (!typedDocument && (String(preview.mediaUrl || "").length > 0
								|| safeStaticPreviewExternalUrl(preview.mediaExternalUrl).length > 0)) {
							const fallbackKind = String(preview.mediaKind || preview.kind || "").toLowerCase()
							const fallbackMime = String(preview.mediaMime || "").toLowerCase()
							const fallbackImage = fallbackKind === "image"
								|| fallbackMime.indexOf("image/") === 0
							const fallbackPlayable = fallbackKind === "audio" || fallbackKind === "video"
								|| fallbackMime.indexOf("audio/") === 0
								|| fallbackMime.indexOf("video/") === 0
							if (fallbackImage && fallbackKind !== "gif"
									&& fallbackKind !== "animated-image" && fallbackMime !== "image/gif") {
								const source = safeStaticPreviewSource(preview.mediaUrl)
								const external = safeStaticPreviewExternalUrl(preview.mediaExternalUrl)
									|| (source.length === 0
										? safeStaticPreviewExternalUrl(preview.mediaUrl) : "")
								if (source.length > 0 || external.length > 0)
									return source
							} else if (fallbackPlayable) {
								const playback = safeStaticPreviewDirectMediaUrl(preview.mediaUrl,
									fallbackKind === "audio" ? "audio" : "video")
								const external = safeStaticPreviewExternalUrl(preview.mediaExternalUrl)
									|| (playback.length === 0
										? safeStaticPreviewExternalUrl(preview.mediaUrl) : "")
								if (playback.length > 0 || external.length > 0)
									return safeStaticPreviewSource(preview.posterUrl)
										|| safeStaticPreviewSource(preview.thumbnailUrl)
							}
						}
						return !typedDocument ? safeStaticPreviewSource(preview.thumbnailUrl) : ""
					}

					function staticPreviewHandoffMediaItem(preview) {
						if (!preview || typeof preview !== "object")
							return ({})
						const document = preview.document || ({})
						const typedDocument = Number(document.schemaVersion || 0) === 1
							&& document.commonPresentation === true
						const media = typedDocument ? document.media : preview.mediaItems
						const count = media && media.length !== undefined ? Number(media.length) : 0
						for (let index = 0; index < count; ++index) {
							const item = media[index] || ({})
							if (staticPreviewMediaSource(item).eligible === true)
								return item
						}
						return ({})
					}

					function staticPreviewHandoffMediaKind(item) {
						const value = item || ({})
						const kind = String(value.kind || value.mediaKind || "").toLowerCase()
						const mime = String(value.mime || value.mediaMime || "").toLowerCase()
						if (kind === "image" || kind === "gif" || kind === "animated-image"
								|| mime.indexOf("image/") === 0)
							return "image"
						if (kind === "audio" || mime.indexOf("audio/") === 0)
							return "audio"
						if (kind === "video" || mime.indexOf("video/") === 0)
							return "video"
						return ""
					}

					function staticPreviewHandoffMediaExpanded(item) {
						const value = item || ({})
						const kind = staticPreviewHandoffMediaKind(value)
						if (kind !== "audio" && kind !== "video")
							return false
						const playback = safeStaticPreviewDirectMediaUrl(value.url, kind)
						const external = safeStaticPreviewExternalUrl(value.externalUrl || value.url)
						return (value.directPlayable !== false && playback.length > 0)
							|| external.length > 0
					}

					function staticPreviewHandoffEmbedGeometry(preview) {
						const value = preview || ({})
						const kind = String(value.embedKind || "").trim().toLowerCase()
						const safeEmbed = String(value.embedUrl || "").trim().slice(0, 2048)
						return kind.length > 0 && kind !== "direct"
							&& (/^https:\/\//i.test(safeEmbed)
								|| value.reserveEmbedGeometry === true)
					}

					function staticPreviewHandoffUsesCompactHeader(preview) {
						if (!preview || typeof preview !== "object")
							return true
						const document = preview.document || ({})
						const typedDocument = Number(document.schemaVersion || 0) === 1
							&& document.commonPresentation === true
						const currentMedia = staticPreviewHandoffMediaItem(preview)
						const currentKind = staticPreviewHandoffMediaKind(currentMedia)
						if (typedDocument && currentKind.length > 0)
							return false
						if (staticPreviewHandoffEmbedGeometry(preview))
							return false
						if (currentKind === "audio" || currentKind === "video")
							return !staticPreviewHandoffMediaExpanded(currentMedia)
						if (currentKind.length > 0)
							return true
						const fallbackMedia = {
							"kind": preview.mediaKind || preview.kind || "",
							"mime": preview.mediaMime || "",
							"url": preview.mediaUrl || "",
							"externalUrl": preview.mediaExternalUrl || "",
							"directPlayable": preview.directPlayable
						}
						if (staticPreviewHandoffMediaExpanded(fallbackMedia))
							return false
						return true
					}

					function staticPreviewHandoffAspect(preview) {
						const value = preview || ({})
						const token = String(value.embedAspect || "").trim().toLowerCase()
						if (token === "short" || token === "portrait" || token === "9:16")
							return "short"
						if (token === "square" || token === "1:1")
							return "square"
						if (token === "audio" || token === "compact-audio" || token === "twitch")
							return token
						const provider = String(value.embedKind || "").trim().toLowerCase()
						if (provider === "tiktok")
							return "short"
						if (provider === "instagram") {
							const metadata = value.metadata || ({})
							let mediaKind = String(metadata.instagramMediaKind || "").toLowerCase()
							mediaKind = mediaKind === "p" ? "post"
								: mediaKind === "reels" ? "reel" : mediaKind
							const match = String(value.embedUrl || "").match(
								/^https:\/\/(?:www\.)?(?:instagram\.com|instagr\.am)\/(p|reel|reels|tv)\//i)
							if (match) {
								const pathKind = String(match[1] || "").toLowerCase()
								if (pathKind === "reel" || pathKind === "reels" || pathKind === "tv")
									mediaKind = pathKind === "reels" ? "reel" : pathKind
							}
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

					function staticPreviewHandoffSize(preview, savedSizePreset) {
						const saved = String(savedSizePreset || "").trim()
						const token = String(saved.length > 0 ? saved
							: preview ? (preview.previewSize || "") : "").toLowerCase()
						if (token === "compact" || token === "small")
							return "compact"
						if (token === "large" || token === "expanded")
							return "large"
						return "default"
					}

					function staticPreviewSourceRetainedOrCooling(source) {
						const key = String(source || "")
						if (key.length === 0 || staticPreviewPreheatCompletedSources[key] === true)
							return true
						const retryAfter = Number(staticPreviewPreheatRetryAfter[key] || 0)
						if (retryAfter > Date.now())
							return true
						if (retryAfter > 0) {
							const retryMap = Object.assign({}, staticPreviewPreheatRetryAfter)
							delete retryMap[key]
							staticPreviewPreheatRetryAfter = retryMap
							staticPreviewPreheatRetryOrder = staticPreviewPreheatRetryOrder.filter(
								function(value) { return String(value || "") !== key })
						}
						return false
					}

					function staticPreviewSourceAlreadyOwned(source) {
						const key = String(source || "")
						if (staticPreviewSourceRetainedOrCooling(key)
								|| key === staticPreviewPreheatActiveSource)
							return true
						for (let index = 0; index < staticPreviewPreheatQueue.length; ++index) {
							if (String(staticPreviewPreheatQueue[index].source || "") === key)
								return true
						}
						return false
					}

					function noteStaticPreviewSourceInScope(source) {
						const key = String(source || "")
						if (key.length === 0 || staticPreviewScopeRetainedSources[key] === true)
							return
						const retained = Object.assign({}, staticPreviewScopeRetainedSources)
						retained[key] = true
						staticPreviewScopeRetainedSources = retained
						++staticPreviewScopeRetainedCount
						if (staticPreviewScopeRetainedCount >= maximumRetainedStaticPreviews)
							staticPreviewSweepCompleted = true
					}

					function touchRetainedStaticPreviewSource(source) {
						const key = String(source || "")
						for (let index = 0; index < staticPreviewRetainedModel.count; ++index) {
							if (String(staticPreviewRetainedModel.get(index).source || "") !== key)
								continue
							if (index !== staticPreviewRetainedModel.count - 1)
								staticPreviewRetainedModel.move(index,
									staticPreviewRetainedModel.count - 1, 1)
							noteStaticPreviewSourceInScope(key)
							return true
						}
						return false
					}

					function retainStaticPreviewSource(source) {
						const key = String(source || "")
						if (key.length === 0)
							return
						for (let index = 0; index < staticPreviewRetainedModel.count; ++index) {
							if (String(staticPreviewRetainedModel.get(index).source || "") !== key)
								continue
							touchRetainedStaticPreviewSource(key)
							return
						}
						const completed = Object.assign({}, staticPreviewPreheatCompletedSources)
						while (staticPreviewRetainedModel.count >= maximumRetainedStaticPreviews) {
							const evicted = String(staticPreviewRetainedModel.get(0).source || "")
							staticPreviewRetainedModel.remove(0)
							delete completed[evicted]
						}
						staticPreviewRetainedModel.append({ "source": key })
						completed[key] = true
						staticPreviewPreheatCompletedSources = completed
						noteStaticPreviewSourceInScope(key)
						const retryMap = Object.assign({}, staticPreviewPreheatRetryAfter)
						delete retryMap[key]
						staticPreviewPreheatRetryAfter = retryMap
						staticPreviewPreheatRetryOrder = staticPreviewPreheatRetryOrder.filter(
							function(value) { return String(value || "") !== key })
					}

					function coolDownStaticPreviewSource(source) {
						const key = String(source || "")
						if (key.length === 0)
							return
						const retryMap = Object.assign({}, staticPreviewPreheatRetryAfter)
						let order = staticPreviewPreheatRetryOrder.filter(
							function(value) { return String(value || "") !== key })
						retryMap[key] = Date.now() + 10000
						order.push(key)
						while (order.length > maximumStaticPreviewRetryEntries) {
							const evicted = String(order.shift() || "")
							delete retryMap[evicted]
						}
						staticPreviewPreheatRetryAfter = retryMap
						staticPreviewPreheatRetryOrder = order
						scheduleStaticPreviewRetryWake()
					}

					function scheduleStaticPreviewRetryWake() {
						let earliest = 0
						const now = Date.now()
						for (const key in staticPreviewPreheatRetryAfter) {
							const retryAt = Number(staticPreviewPreheatRetryAfter[key] || 0)
							if (retryAt > now && (earliest <= 0 || retryAt < earliest))
								earliest = retryAt
						}
						if (earliest <= 0) {
							staticPreviewRetryTimer.stop()
							return
						}
						staticPreviewRetryTimer.interval = Math.max(1, earliest - now)
						staticPreviewRetryTimer.restart()
					}

					function retryCooledStaticPreviews() {
						const now = Date.now()
						const attempted = Object.assign({}, staticPreviewSweepAttemptedSources)
						let openedRetry = false
						for (const key in staticPreviewPreheatRetryAfter) {
							if (Number(staticPreviewPreheatRetryAfter[key] || 0) > now)
								continue
							delete attempted[key]
							openedRetry = true
						}
						staticPreviewSweepAttemptedSources = attempted
						if (openedRetry && staticPreviewScopeRetainedCount
								< maximumRetainedStaticPreviews) {
							staticPreviewSweepIndex = -1
							staticPreviewSweepStarted = false
							staticPreviewSweepCompleted = false
							staticPreviewSweepRemainingAttempts = Math.max(1,
								maximumRetainedStaticPreviews - staticPreviewScopeRetainedCount)
							scheduleStaticPreviewSweep()
						}
						scheduleStaticPreviewRetryWake()
					}

					function activateStaticPreviewPreheat(entry) {
						if (!entry || staticPreviewThumbBlocked
								|| staticPreviewPreheatActiveSource.length > 0)
							return false
						const source = String(entry.source || "")
						if (source.length === 0 || staticPreviewSourceRetainedOrCooling(source))
							return false
						const origin = String(entry.origin || "lookahead")
						if (origin === "sweep" && (staticPreviewSweepCompleted
								|| staticPreviewScopeRetainedCount >= maximumRetainedStaticPreviews))
							return false
						staticPreviewPreheatActiveId = String(entry.id || "")
						staticPreviewPreheatActiveOrigin = origin
						staticPreviewPreheatActiveSweepIndex = Number(entry.sweepIndex === undefined
							? -1 : entry.sweepIndex)
						// Image.source is the request trigger and may synchronously reach Ready
						// on a Qt cache hit. Publish all bookkeeping before changing it.
						staticPreviewPreheatActiveSource = source
						return true
					}

					function reopenStaticPreviewSweepForCanceledIndex(index) {
						const canceledIndex = Number(index)
						if (canceledIndex < 0 || staticPreviewScopeRetainedCount
								>= maximumRetainedStaticPreviews
								|| staticPreviewSweepRemainingAttempts <= 0)
							return
						staticPreviewSweepIndex = Math.max(staticPreviewSweepIndex,
							Math.min(chatModel.count - 1, canceledIndex))
						staticPreviewSweepStarted = true
						staticPreviewSweepCompleted = false
					}

					function commitStaticPreviewSweepAttempt(source) {
						const key = String(source || "")
						if (key.length === 0 || staticPreviewSweepAttemptedSources[key] === true)
							return
						const attempted = Object.assign({}, staticPreviewSweepAttemptedSources)
						attempted[key] = true
						staticPreviewSweepAttemptedSources = attempted
						staticPreviewSweepRemainingAttempts = Math.max(0,
							staticPreviewSweepRemainingAttempts - 1)
						if (staticPreviewSweepRemainingAttempts <= 0
								|| staticPreviewScopeRetainedCount >= maximumRetainedStaticPreviews)
							staticPreviewSweepCompleted = true
					}

					function activateQueuedStaticPreviewPreheat() {
						if (staticPreviewThumbBlocked
								|| staticPreviewPreheatActiveSource.length > 0
								|| staticPreviewPreheatQueue.length === 0)
							return false
						const entry = staticPreviewPreheatQueue[0]
						staticPreviewPreheatQueue = staticPreviewPreheatQueue.slice(1)
						return activateStaticPreviewPreheat(entry)
					}

					function completeStaticPreviewPreheat(status) {
						if (staticPreviewPreheatActiveSource.length === 0
								|| (status !== Image.Ready && status !== Image.Error))
							return false
						const completedSource = staticPreviewPreheatActiveSource
						const completedOrigin = staticPreviewPreheatActiveOrigin
						const sweepCapacityReached = completedOrigin === "sweep"
							&& staticPreviewScopeRetainedCount >= maximumRetainedStaticPreviews
						if (!sweepCapacityReached) {
							if (status === Image.Ready)
								retainStaticPreviewSource(completedSource)
							else
								coolDownStaticPreviewSource(completedSource)
							if (completedOrigin === "sweep")
								commitStaticPreviewSweepAttempt(completedSource)
						}
						staticPreviewPreheatActiveSource = ""
						staticPreviewPreheatActiveId = ""
						staticPreviewPreheatActiveOrigin = ""
						staticPreviewPreheatActiveSweepIndex = -1
						Qt.callLater(function() {
							if (!timeline.activateQueuedStaticPreviewPreheat()) {
								timeline.scheduleStaticPreviewLookahead()
								timeline.scheduleStaticPreviewSweep()
							}
						})
						return true
					}

					function staticPreviewLookaheadCandidates() {
						const candidates = []
						const seen = ({})
						let retainedTouchCandidate = null
						const children = contentItem ? contentItem.children : []
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true
									|| typeof item.preview === "undefined")
								continue
							const source = staticPreviewSourceFor(item.preview)
							if (source.length === 0 || seen[source])
								continue
							const point = item.mapToItem(timeline, 0, 0)
							const visible = point.y + item.height > 0.5 && point.y < height - 0.5
							const ahead = staticPreviewPreheatDirection < 0
								? point.y + item.height <= 0.5 : point.y >= height - 0.5
							if (!visible && !ahead)
								continue
							const distance = visible ? 0 : staticPreviewPreheatDirection < 0
								? Math.max(0, -(point.y + item.height))
								: Math.max(0, point.y - height)
							if (distance > staticPreviewLookaheadDistance + 0.5)
								continue
							seen[source] = true
							if (staticPreviewPreheatCompletedSources[source] === true) {
								// Touch at most one nearest retained still per coalesced refill.
								// Reordering every visible retained delegate would otherwise
								// turn the max-four ListModel into scroll-time model churn.
								if (!retainedTouchCandidate
										|| (visible && retainedTouchCandidate.visible !== true)
										|| (visible === retainedTouchCandidate.visible
											&& distance < retainedTouchCandidate.distance))
									retainedTouchCandidate = { "source": source,
										"visible": visible, "distance": distance }
								continue
							}
							if (staticPreviewSourceRetainedOrCooling(source))
								continue
							candidates.push({ "id": String(item.stableId || ""),
								"source": source, "visible": visible, "distance": distance,
								"origin": "lookahead" })
						}
						if (retainedTouchCandidate)
							touchRetainedStaticPreviewSource(retainedTouchCandidate.source)
						candidates.sort(function(left, right) {
							if (left.visible !== right.visible)
								return left.visible ? -1 : 1
							return left.distance - right.distance
						})
						return candidates
					}

					function refillStaticPreviewLookahead() {
						if (staticPreviewThumbBlocked)
							return
						const candidates = staticPreviewLookaheadCandidates()
						if (staticPreviewPreheatTeleportPending && candidates.length > 0
								&& candidates[0].visible === true
								&& staticPreviewPreheatActiveSource.length > 0
								&& candidates[0].source !== staticPreviewPreheatActiveSource) {
							// A viewport-scale scrollbar teleport is the one case where stale
							// decode work loses priority. Ordinary wheel reversals keep it alive.
							const canceledOrigin = staticPreviewPreheatActiveOrigin
							const canceledSweepIndex = staticPreviewPreheatActiveSweepIndex
							staticPreviewPreheatActiveSource = ""
							staticPreviewPreheatActiveId = ""
							staticPreviewPreheatActiveOrigin = ""
							staticPreviewPreheatActiveSweepIndex = -1
							if (canceledOrigin === "sweep")
								reopenStaticPreviewSweepForCanceledIndex(canceledSweepIndex)
						}
						if (staticPreviewPreheatActiveSource.length === 0) {
							for (let index = 0; index < candidates.length; ++index) {
								if (activateStaticPreviewPreheat(candidates[index]))
									break
							}
						}
						const nextQueue = []
						for (let index = 0; index < candidates.length; ++index) {
							const candidate = candidates[index]
							if (candidate.source === staticPreviewPreheatActiveSource)
								continue
							nextQueue.push(candidate)
							break
						}
						let queuedSweep = null
						for (let index = 0; index < staticPreviewPreheatQueue.length; ++index) {
							if (String(staticPreviewPreheatQueue[index].origin || "") === "sweep") {
								queuedSweep = staticPreviewPreheatQueue[index]
								break
							}
						}
						if (queuedSweep && nextQueue.length > 0
								&& String(nextQueue[0].source || "")
									=== String(queuedSweep.source || ""))
							nextQueue[0] = queuedSweep
						else if (queuedSweep)
							reopenStaticPreviewSweepForCanceledIndex(queuedSweep.sweepIndex)
						staticPreviewPreheatQueue = nextQueue
						staticPreviewPreheatTeleportPending = false
					}

					function scheduleStaticPreviewLookahead() {
						if (!staticPreviewThumbBlocked
								&& !staticPreviewLookaheadTimer.running)
							staticPreviewLookaheadTimer.start()
					}

					function pauseStaticPreviewPreheatForThumb() {
						staticPreviewLookaheadTimer.stop()
						staticPreviewSweepTimer.stop()
						staticPreviewPreheatActiveSource = ""
						staticPreviewPreheatActiveId = ""
						staticPreviewPreheatActiveOrigin = ""
						staticPreviewPreheatActiveSweepIndex = -1
						staticPreviewPreheatQueue = []
						staticPreviewPreheatTeleportPending = false
					}

					function staticPreviewSweepAllowed() {
						return !richScrollInProgress && !scopePresentationPending
							&& !scopePresentationFinalizing && !scopeResetPending
							&& !scopePresentationObservationActive && !activeScope.loading
							&& !chatModel.presentationBusy && !chatModel.appendPending
							&& !chatModel.prependPending && !root.coldPreviewHydrationPacingActive
							&& !prependAnchorActive && !topAnchorActive && !restoringViewportAnchor
							&& pendingVisibleHydrationCount() === 0
							&& pendingVisibleRichRecoveryCount() === 0
							&& richContentResumeTimer.running !== true
							&& richLoaderResumeTimer.running !== true
							&& deferredMaterializationTimer.running !== true
							&& viewportMembershipTimer.running !== true
							&& viewportMembershipUpdatePending !== true
							&& richLoaderResumeActiveId.length === 0
							&& deferredMaterializationActiveId.length === 0
							&& plainDeferredMaterializationActiveCount() === 0
							&& richLoaderResumeQueue.length === 0
							&& deferredMaterializationQueue.length === 0
							&& nearViewportPrefetchActiveId.length === 0
							&& nearViewportPrefetchTimer.running !== true
					}

					function scheduleStaticPreviewSweep() {
						if (staticPreviewThumbBlocked || !staticPreviewSweepAllowed()
								|| chatModel.count <= 0
								|| staticPreviewSweepCompleted
								|| staticPreviewSweepRemainingAttempts <= 0
								|| staticPreviewSweepTimer.running)
							return
						staticPreviewSweepTimer.start()
					}

					function reopenStaticPreviewSweepForNewData() {
						if (staticPreviewScopeRetainedCount >= maximumRetainedStaticPreviews)
							return
						staticPreviewSweepIndex = -1
						staticPreviewSweepStarted = false
						staticPreviewSweepCompleted = false
						staticPreviewSweepRemainingAttempts = Math.max(
							staticPreviewSweepRemainingAttempts,
							maximumRetainedStaticPreviews - staticPreviewScopeRetainedCount)
						scheduleStaticPreviewSweep()
					}

					function advanceStaticPreviewSweep() {
						if (!staticPreviewSweepAllowed())
							return
						if (staticPreviewSweepCompleted || staticPreviewSweepRemainingAttempts <= 0)
							return
						if (staticPreviewPreheatActiveSource.length > 0
								&& staticPreviewPreheatQueue.length > 0)
							return
						if (!staticPreviewSweepStarted) {
							staticPreviewSweepIndex = chatModel.count - 1
							staticPreviewSweepStarted = true
						}
						while (staticPreviewSweepIndex >= 0) {
							const sweepIndex = staticPreviewSweepIndex--
							const row = chatModel.get(sweepIndex)
							const source = staticPreviewSourceFor(row ? row.preview : null)
							if (source.length === 0
									|| staticPreviewSweepAttemptedSources[source] === true)
								continue
							if (staticPreviewPreheatCompletedSources[source] === true) {
								touchRetainedStaticPreviewSource(source)
								if (staticPreviewSweepCompleted)
									return
								continue
							}
							if (staticPreviewSourceAlreadyOwned(source))
								continue
							const entry = { "id": String(row ? row.stableId || "" : ""),
								"source": source, "origin": "sweep",
								"sweepIndex": sweepIndex }
							if (staticPreviewPreheatActiveSource.length === 0)
								activateStaticPreviewPreheat(entry)
							else
								staticPreviewPreheatQueue = [entry]
							return
						}
						staticPreviewSweepCompleted = true
					}

					function releaseScrollbarGuardForWheelInput() {
						if (!scrollbarReleasePending || scrollbarDragActive
								|| timelineScrollBar.pressed || scopePresentationPending
								|| !interactive || root.performanceChatScrollOwnershipActive
								|| root.performanceChatScrollRunning)
							return false
						// Keep one bounded fallback wakeup. Actual coarse/native movement
						// transfers ownership and stops this timer; an edge-clamped precision
						// packet has no movement signal, so the fallback must still resume
						// membership and rich-content recovery after the input turn.
						scrollbarReleaseGuardTimer.restart()
						scrollbarReleasePending = false
						++root.timelineScrollbarReleaseWheelTakeoverCount
						return true
					}

					function noteExplicitWheelInput() {
						releaseScrollbarGuardForWheelInput()
						const generation = ++explicitWheelInputGeneration
						explicitWheelInputStartY = contentY
						explicitWheelInputPending = true
						// Native precision deltas are handled by Flickable after the
						// overlay passes the current wheel event through. The matching
						// movementStarted consumes this one-turn token synchronously;
						// an edge-clamped event must not classify a later layout change.
						Qt.callLater(function() {
							if (generation === timeline.explicitWheelInputGeneration)
								timeline.explicitWheelInputPending = false
						})
					}

					function takeExplicitNavigationOwnership(classification) {
						const navigationClass = String(classification || "")
						const retiresThumbWarm = navigationClass !== "scrollbar"
							&& !scrollbarDragActive && !timelineScrollBar.pressed
						if (retiresThumbWarm) {
							scrollbarReleaseGuardTimer.stop()
							scrollbarReleasePending = false
						}
						if (!explicitNavigationMovement)
							++explicitNavigationMovementCount
						explicitNavigationMovement = true
						explicitWheelInputPending = false
						lastMovementClassification = navigationClass || "explicit"
						root.closeQuickReactions()
						stickToBottom = false
						releasePrependAnchor()
						releaseViewportAnchor()
						releaseTopAnchor()
						invalidateBottomFollow()
						// A wheel/middle-drag movement has now proved new user ownership.
						// Retire the thumb-era receipt only after that ownership and the
						// selected viewport anchors are published; observation-only edge
						// packets keep the bounded release fallback instead.
						if (retiresThumbWarm && (stationaryThumbWarmPinCount > 0
								|| stationaryThumbWarmReceipts.length > 0))
							clearStationaryThumbWarmPins("explicit-navigation")
						richContentResumeTimer.stop()
					}

					function beginTopAnchor(guardProgrammaticMovement) {
						releaseViewportAnchor()
						topAnchorActive = true
						topAnchorProgrammaticMovementGuard =
							guardProgrammaticMovement === true
						if (topAnchorProgrammaticMovementGuard)
							topAnchorProgrammaticMovementGuardTimer.restart()
						topAnchorFrameTimer.restart()
						topAnchorSettleTimer.restart()
					}

					function releaseTopAnchor() {
						clearViewportAnchorQuietRelease()
						topAnchorActive = false
						topAnchorProgrammaticMovementGuard = false
						topAnchorProgrammaticMovementGuardTimer.stop()
						topAnchorSettleTimer.stop()
					}

					function restoreTopAnchor() {
						if (!topAnchorActive || restoringTopAnchor || moving || dragging || flicking
								|| timelineScrollHandler.scrolling || scrollbarDragActive
								|| scrollbarReleasePending || timelineScrollBar.pressed)
							return
						restoringTopAnchor = true
						contentY = originY
						restoringTopAnchor = false
					}

					function scheduleRichContentResume() {
						if (richScrollInProgress
								|| coldScopeMaterializationDelayTimer.running)
							return
						richContentResumeTimer.restart()
					}

					function timelineDelegateForStableId(stableId) {
						const key = String(stableId || "")
						if (key.length === 0)
							return null
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (item && item.performanceTimelineDelegate === true
									&& item.accessibilityPooled !== true
									&& String(item.stableId || "") === key)
								return item
						}
						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let childIndex = 0;
								deferredRepeater && childIndex < deferredRepeater.count;
								++childIndex) {
							const item = deferredRepeater.itemAt(childIndex)
							if (item && item.performanceTimelineDelegate === true
									&& item.accessibilityPooled !== true
									&& String(item.stableId || "") === key)
								return item
						}
						return null
					}

					function updateViewportMemberships() {
						if (!contentItem)
							return
						const children = contentItem.children
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (item && item.performanceTimelineDelegate === true
									&& item.updateViewportMembership)
								item.updateViewportMembership()
						}
					}

					function scheduleViewportMembershipUpdate() {
						viewportMembershipUpdatePending = true
						if (!viewportMembershipTimer.running)
							viewportMembershipTimer.start()
					}

					function nearViewportPrefetchSignature() {
						const first = firstVisibleMessageDelegate()
						if (!first)
							return ""
						const point = first.mapToItem(timeline, 0, 0)
						return String(first.stableId || "") + "|"
							+ String(Math.round(point.y * 2) / 2)
					}

					function nearViewportPrefetchCandidateFromItem(item, candidate) {
						if (!item || item.accessibilityPooled === true || !item.visible)
							return candidate
						if (item.nearViewportPrefetchTerminal === true)
							return candidate
						const deferred = item.lightweightDeferredPresentation === true
						// Plain summaries are at most 180 px. Spending the single
						// adjacent warm slot on one would leave the next media card cold.
						const deferredRich = deferred
							&& Number(item.deferredPresentationHeight || 0) > 180
						const needsWarmRecovery = !deferred
							&& item.staticPreviewPresentationReady !== true
							&& item.richResourcesRequired === true
							&& item.previewLoaderIncubationSuppressed !== true
							&& item.previewLoaderFailed !== true
							&& (item.richResourcesActive !== true
								|| item.richLoaderSuspended === true
								|| item.messageRowReady !== true
								|| item.previewPresentationReady !== true)
						if (!deferredRich && !needsWarmRecovery)
							return candidate
						const point = item.mapToItem(timeline, 0, 0)
						const above = point.y + item.height <= 0.5
						const below = point.y >= height - 0.5
						if (!above && !below)
							return candidate
						// At the absolute beginning there is no stable row above which
						// can own an expansion anchor. The lower candidate remains safe.
						if (above && Math.abs(contentY - originY) <= 0.5)
							return candidate
						const distance = above
							? Math.max(0, -(point.y + item.height))
							: Math.max(0, point.y - height)
						if (distance > nearViewportPrefetchDistance + 0.5)
							return candidate
						const stableId = String(item.stableId || "")
						if (stableId.length === 0
								|| (candidate && candidate.distance <= distance))
							return candidate
						return { "id": stableId, "distance": distance,
							"above": above, "deferred": deferredRich }
					}

					function nearestDeferredPrefetchCandidate() {
						let candidate = null
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex)
							candidate = nearViewportPrefetchCandidateFromItem(
								children[childIndex], candidate)
						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let childIndex = 0;
								deferredRepeater && childIndex < deferredRepeater.count;
								++childIndex)
							candidate = nearViewportPrefetchCandidateFromItem(
								deferredRepeater.itemAt(childIndex), candidate)
						return candidate
					}

					function nearViewportPrefetchAllowed() {
						return !richScrollInProgress
							&& !scopePresentationPending && !scopePresentationFinalizing
							&& !scopeResetPending && !scopePresentationObservationActive
							&& !prependAnchorActive && !topAnchorActive && !activeScope.loading
							&& !chatModel.appendPending && !chatModel.prependPending
							&& !root.coldPreviewHydrationPacingActive
							&& pendingVisibleHydrationCount() === 0
							&& pendingVisibleRichRecoveryCount() === 0
							&& richContentResumeTimer.running !== true
							&& richLoaderResumeTimer.running !== true
							&& deferredMaterializationTimer.running !== true
							&& viewportMembershipTimer.running !== true
							&& viewportMembershipUpdatePending !== true
							&& richLoaderResumeActiveId.length === 0
							&& deferredMaterializationActiveId.length === 0
							&& plainDeferredMaterializationActiveCount() === 0
							&& richLoaderResumeQueue.length === 0
							&& deferredMaterializationQueue.length === 0
					}

					function scheduleNearViewportPrefetch() {
						if (nearViewportPrefetchActiveId.length > 0
								|| nearViewportPrefetchTimer.running
								|| !nearViewportPrefetchAllowed())
							return
						nearViewportPrefetchTimer.start()
					}

					function startNearViewportPrefetch() {
						if (!nearViewportPrefetchAllowed()
								|| nearViewportPrefetchActiveId.length > 0)
							return
						const signature = nearViewportPrefetchSignature()
						if (signature.length === 0
								|| signature === nearViewportPrefetchViewportSignature) {
							scheduleStaticPreviewSweep()
							return
						}
						if (nearViewportPrefetchReadyId.length > 0) {
							nearViewportPrefetchReadyId = ""
							nearViewportWarmId = ""
						}
						const candidate = nearestDeferredPrefetchCandidate()
						if (!candidate) {
							scheduleStaticPreviewSweep()
							return
						}
						nearViewportPrefetchViewportSignature = signature
						nearViewportPrefetchActiveId = candidate.id
						nearViewportWarmId = candidate.id
						nearViewportPrefetchReadyId = ""
						nearViewportPrefetchOwnsViewportAnchor = false
						nearViewportPrefetchDeadlineTimer.restart()
						if (candidate.above && !stickToBottom && !viewportAnchorActive) {
							if (!captureViewportAnchor()) {
								nearViewportPrefetchDeadlineTimer.stop()
								nearViewportPrefetchActiveId = ""
								nearViewportWarmId = ""
								return
							}
							nearViewportPrefetchOwnsViewportAnchor = true
						}
						if (candidate.deferred) {
							deferredMaterializationActiveId = candidate.id
							deferredMaterializationActiveGeneration =
								scopePresentationGeneration
							if (!chatModel.materializeDeferredMessage(candidate.id)) {
								nearViewportPrefetchDeadlineTimer.stop()
								deferredMaterializationActiveId = ""
								deferredMaterializationActiveGeneration = -1
								nearViewportPrefetchActiveId = ""
								nearViewportWarmId = ""
								if (nearViewportPrefetchOwnsViewportAnchor
										&& viewportAnchorActive)
									releaseViewportAnchor()
								nearViewportPrefetchOwnsViewportAnchor = false
								return
							}
						}
						// A cached delegate can finish its provider polish between
						// candidate discovery and ownership publication. Reconcile that
						// already-ready state before waiting for a transition that will
						// never fire again.
						if (completeNearViewportPrefetchIfReady(candidate.id))
							return
						Qt.callLater(function() {
							if (timeline.nearViewportPrefetchActiveId !== candidate.id
									|| timeline.richScrollInProgress)
								return
							if (timeline.completeNearViewportPrefetchIfReady(candidate.id))
								return
							timeline.queueRichLoaderResumes()
							timeline.queueVisiblePreviewHydration()
							timeline.completeNearViewportPrefetchIfReady(candidate.id)
						})
					}

					function cancelActiveNearViewportPrefetchForScroll() {
						const activeId = nearViewportPrefetchActiveId
						if (activeId.length === 0)
							return
						const item = timelineDelegateForStableId(activeId)
						if (item && item.cancelNearViewportPrefetchForScroll)
							item.cancelNearViewportPrefetchForScroll()
						abandonNearViewportPrefetch(activeId, false)
					}

					function expireNearViewportPrefetch() {
						const activeId = String(nearViewportPrefetchActiveId || "")
						if (activeId.length === 0)
							return false
						// A completion and this timer can land on adjacent event-loop
						// turns. Give the fully settled card one final chance to publish
						// ready before retiring the exact preview generation.
						if (completeNearViewportPrefetchIfReady(activeId))
							return true
						if (nearViewportPrefetchActiveId !== activeId)
							return true
						const item = timelineDelegateForStableId(activeId)
						if (item && item.cancelNearViewportPrefetchForScroll)
							item.cancelNearViewportPrefetchForScroll()
						if (nearViewportPrefetchActiveId !== activeId)
							return true
						return retireTerminalNearViewportPrefetch(activeId)
					}

					function abandonNearViewportPrefetch(stableId, restoreOwnedAnchor) {
						const key = String(stableId || "")
						if (key.length === 0
								|| (key !== nearViewportPrefetchActiveId
									&& key !== nearViewportWarmId
									&& key !== nearViewportPrefetchReadyId))
							return false
						nearViewportPrefetchDeadlineTimer.stop()
						richLoaderResumeQueue = richLoaderResumeQueue.filter(function(stableId) {
							return String(stableId || "") !== key
						})
						const releasedToken = richLoaderResumeActiveId === key
						if (releasedToken)
							richLoaderResumeActiveId = ""
						if (deferredMaterializationActiveId === key) {
							deferredMaterializationActiveId = ""
							deferredMaterializationActiveGeneration = -1
						}
						if (nearViewportPrefetchActiveId === key)
							nearViewportPrefetchActiveId = ""
						if (nearViewportWarmId === key)
							nearViewportWarmId = ""
						if (nearViewportPrefetchReadyId === key)
							nearViewportPrefetchReadyId = ""
						if (nearViewportPrefetchOwnsViewportAnchor
								&& viewportAnchorActive) {
							if (restoreOwnedAnchor === true)
								scheduleViewportAnchorQuietRelease()
							else
								releaseViewportAnchor()
						}
						nearViewportPrefetchOwnsViewportAnchor = false
						if (releasedToken && !richScrollInProgress) {
							if (richLoaderResumeQueue.length > 0)
								richLoaderResumeTimer.start()
							else
								Qt.callLater(function() {
									timeline.queueRichLoaderResumes()
								})
						}
						return true
					}

					function invalidateNearViewportPrefetchReady(stableId) {
						const key = String(stableId || "")
						if (key.length === 0 || key !== nearViewportPrefetchReadyId)
							return false
						if (!abandonNearViewportPrefetch(key, true))
							return false
						// Hydration invalidation is recoverable for this generation.
						// Rearm the same viewport so the row can preheat again once
						// its body/attachment/preview data settles.
						nearViewportPrefetchViewportSignature = ""
						return true
					}

					function retireTerminalNearViewportPrefetch(stableId) {
						const key = String(stableId || "")
						const item = timelineDelegateForStableId(key)
						if (item && item.markNearViewportPrefetchTerminal)
							item.markNearViewportPrefetchTerminal()
						if (!abandonNearViewportPrefetch(key, true))
							return false
						// This generation cannot ever publish active preview media.
						// Allow the same viewport to select a distinct eligible neighbor;
						// the terminal row is excluded by candidate filtering above.
						nearViewportPrefetchViewportSignature = ""
						Qt.callLater(function() {
							if (!timeline.richScrollInProgress)
								timeline.scheduleNearViewportPrefetch()
						})
						return true
					}

					function completeNearViewportPrefetchIfReady(stableId) {
						const key = String(stableId || "")
						if (key.length === 0 || key !== nearViewportPrefetchActiveId)
							return false
						const item = timelineDelegateForStableId(key)
						if (!item || item.messageRowReady !== true
								|| item.previewPresentationReady !== true
								|| item.contentNeedsHydration === true)
							return false
						// A provider watchdog/error fallback is a valid non-blank
						// presentation, but it is not preheated media. Retire that
						// candidate instead of publishing a false ready grant that
						// would expose an inactive card on the next wheel step.
						if (item.previewLoaderIncubationSuppressed === true
								|| item.previewLoaderFailed === true
								|| item.richResourcesRequired !== true) {
							retireTerminalNearViewportPrefetch(key)
							return false
						}
						if (item.richResourcesActive !== true
								|| item.richResourceActivationPending === true)
							return false
						// Tree construction and provider-detail polish can finish before the
						// selected still reaches Image.Ready. Keep the speculative grant until
						// that cheap visual is ready or its bounded native fallback is final.
						if (item.previewPrewarmVisualReady !== true)
							return false
						// Keep the warm slot until its outer height is committed. Only then
						// can the completed static card survive losing offscreen admission.
						if (!(item.presentationHeightCommittedHeight > 0)
								|| item.richGeometrySettling === true)
							return false
						// Animated/interactive media cannot hold a static offscreen lease.
						// Do not repeatedly select it as the sequential warm slot advances.
						if (item.staticPreviewPresentationReady !== true) {
							retireTerminalNearViewportPrefetch(key)
							return false
						}
						nearViewportPrefetchDeadlineTimer.stop()
						nearViewportPrefetchActiveId = ""
						nearViewportPrefetchReadyId = key
						if (nearViewportPrefetchOwnsViewportAnchor
								&& viewportAnchorActive) {
							scheduleViewportAnchorQuietRelease()
						}
						nearViewportPrefetchOwnsViewportAnchor = false
						nearViewportPrefetchViewportSignature = ""
						Qt.callLater(function() {
							timeline.scheduleNearViewportPrefetch()
							timeline.scheduleStaticPreviewSweep()
						})
						return true
					}

					function suspendRichContentForScroll() {
						richRecoveryIdleTimer.stop()
						richLoaderResumeQueue = []
						richLoaderResumeActiveId = ""
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true
									|| item.lightweightDeferredPresentation === true)
								continue
							// Keep the outer card tree and its measured geometry alive. Only
							// release posters, animations and provider players here; tearing
							// down every Loader synchronously creates its own scroll-start
							// hitch. Pooled/reused delegates still retire the complete tree.
							// Already-painted visible media remains active through a shallow
							// wheel gesture. Clearing it here paints a black card and makes
							// renderActive=false close or detach an active inline player.
							// A retained row suspends only when it actually leaves the viewport.
							if (item.inVisibleViewport === true
									|| String(item.stableId || "")
										=== nearViewportPrefetchReadyId)
								continue
							item.suspendRichResourcesForScroll()
						}
					}

					function queueRichLoaderResumes() {
						if (richScrollInProgress)
							return
						const candidates = []
						const plainSourceCohortActive =
							plainDeferredMaterializationActiveCount() > 0
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true
									|| item.lightweightDeferredPresentation === true
									|| item.previewLoaderIncubationSuppressed === true
									|| (item.previewNeedsHydration === true
										&& item.messageRowMaterialized === true)
									|| (item.richLoaderSuspended !== true
										&& item.messageRowReady === true
										&& item.previewPresentationReady === true
										&& !richResourceRecoveryPending(item)))
								continue
							const stableId = String(item.stableId || "")
							if (stableId.length === 0)
								continue
							const plainSourceOwned = item.plainDeferredSourceOwned === true
							if (plainSourceCohortActive && !plainSourceOwned)
								continue
							if (stableId === richLoaderResumeActiveId)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							const visible = point.y + item.height > 0.5
								&& point.y < height - 0.5
							const prefetchEligible = String(item.stableId || "")
								=== nearViewportWarmId
							// Cache-buffer delegates retain their bounded placeholder
							// geometry. Rebuilding provider trees that are still outside
							// the viewport consumes UI frames without drawing anything.
							// Plain cached rows may also prepare while idle without provider work.
							if (!visible && !prefetchEligible && !plainSourceOwned
									&& !cachedPlainPresentationEligible(item))
								continue
							// A finalizing plain row that already owns its nested Loader is
							// observed by visiblePresentationReady(). Re-enqueuing it while
							// asynchronous incubation is pending would consume another bounded
							// admission slot without making progress.
							if ((plainSourceOwned || (scopePresentationPending
									&& scopePresentationFinalizing && visible))
									&& item.plainScopeResumeEligible === true
									&& item.messageRowMaterialized === true)
								continue
							const distance = visible ? 0
								: Math.min(Math.abs(point.y), Math.abs(point.y - height))
							candidates.push({ "id": stableId, "visible": visible, "distance": distance })
						}
						candidates.sort(function(left, right) {
							if (left.visible !== right.visible)
								return left.visible ? -1 : 1
							return left.distance - right.distance
						})
						richLoaderResumeQueue = candidates.map(function(candidate) {
							return candidate.id
						})
						if (richLoaderResumeQueue.length > 0 && !richLoaderResumeTimer.running)
							richLoaderResumeTimer.start()
						else
							finishRichContentRecoveryIfIdle()
					}

					function resumeNextRichLoader() {
						if (richScrollInProgress) {
							scheduleRichContentResume()
							return
						}
						if (richLoaderResumeActiveId.length > 0)
							return
						if (richLoaderResumeQueue.length === 0) {
							finishRichContentRecoveryIfIdle()
							return
						}
						const queued = richLoaderResumeQueue.slice()
						const children = contentItem ? contentItem.children : []
						let resumed = false
						let plainAdmissions = 0
						while (queued.length > 0 && !resumed) {
							const stableId = String(queued.shift() || "")
							richLoaderResumeQueue = queued
							let item = null
							for (let childIndex = 0; childIndex < children.length; ++childIndex) {
								const candidate = children[childIndex]
								if (candidate && candidate.performanceTimelineDelegate === true
										&& candidate.accessibilityPooled !== true
										&& String(candidate.stableId || "") === stableId) {
									item = candidate
									break
								}
							}
							if (!item)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							const stillVisible = point.y + item.height > 0.5
								&& point.y < height - 0.5
							const prefetchEligible = String(item.stableId || "")
								=== nearViewportWarmId
							const plainSourceOwned = item.plainDeferredSourceOwned === true
							if (!stillVisible && !prefetchEligible && !plainSourceOwned
									&& !cachedPlainPresentationEligible(item))
								continue
							const plainScopeResume = (plainSourceOwned || (scopePresentationPending
								&& scopePresentationFinalizing && stillVisible))
								&& item.plainScopeResumeEligible === true
							if (plainScopeResume) {
								if (item.messageRowMaterialized !== true
										&& item.resumeRichContentAfterScroll(true, false) !== false) {
									++plainAdmissions
									noteScopePresentationPlainRowAdmission()
								}
								if (plainAdmissions >= plainScopeResumeAdmissionsPerFrame)
									break
								continue
							}
							if (plainAdmissions > 0) {
								queued.unshift(stableId)
								richLoaderResumeQueue = queued
								break
							}
							richLoaderResumeActiveId = stableId
							resumed = item.resumeRichContentAfterScroll(
								timeline.scopePresentationFinalizing, true) !== false
							if (!resumed)
								richLoaderResumeActiveId = ""
							else if (item.messageRowReady === true)
								{
									const resumeGeneration = scopePresentationGeneration
									Qt.callLater(function() {
										if (resumeGeneration === timeline.scopePresentationGeneration
												&& String(item.stableId || "") === stableId
												&& item.accessibilityPooled !== true
												&& timeline.richLoaderResumeActiveId === stableId)
											item.completeRichLoaderResumeWhenReady()
									})
								}
						}
						if (!resumed && queued.length > 0)
							richLoaderResumeTimer.start()
						else if (!resumed)
							Qt.callLater(function() {
								timeline.queueRichLoaderResumes()
							})
					}

					function completeRichLoaderResume(stableId) {
						if (String(stableId || "") !== richLoaderResumeActiveId)
							return false
						richLoaderResumeActiveId = ""
						if (richScrollInProgress) {
							scheduleRichContentResume()
							return true
						}
						if (richLoaderResumeQueue.length > 0)
							richLoaderResumeTimer.start()
						else
							// Rich placeholder heights can move a different row into the
							// viewport. Re-scan after polish instead of declaring recovery
							// complete from a stale candidate snapshot.
							Qt.callLater(function() {
								timeline.queueRichLoaderResumes()
							})
						return true
					}

					function cancelRichLoaderResume(stableId) {
						return completeRichLoaderResume(stableId)
					}

					function plainDeferredMaterializationActiveCount() {
						return Object.keys(plainDeferredMaterializationActive || ({})).length
					}

					function plainDeferredMaterializationOwns(stableId, generation) {
						const key = String(stableId || "")
						const expectedGeneration = Number(generation)
						const ownedGeneration = key.length > 0
							? plainDeferredMaterializationActive[key] : undefined
						return ownedGeneration !== undefined
							&& Number(ownedGeneration) === expectedGeneration
					}

					function deferredMaterializationGenerationFor(stableId) {
						const key = String(stableId || "")
						if (key.length === 0)
							return -1
						if (key === deferredMaterializationActiveId)
							return deferredMaterializationActiveGeneration
						const plainGeneration = plainDeferredMaterializationActive[key]
						return plainGeneration === undefined ? -1 : Number(plainGeneration)
					}

					function deferredMaterializationOwnershipActive() {
						return deferredMaterializationActiveId.length > 0
							|| plainDeferredMaterializationActiveCount() > 0
					}

					function clearDeferredMaterializationOwnership() {
						deferredMaterializationActiveId = ""
						deferredMaterializationActiveGeneration = -1
						plainDeferredMaterializationActive = ({})
					}

					function finishDeferredMaterializationCohort(generation) {
						const ownedGeneration = Number(generation)
						if (ownedGeneration !== scopePresentationGeneration
								|| deferredMaterializationOwnershipActive())
							return
						if (scopePresentationFinalizing && stickToBottom) {
							positionTailImmediately()
							restoringBottom = true
							containTailMessageWhenPossible()
							restoringBottom = false
						}
						Qt.callLater(function() {
							if (ownedGeneration !== timeline.scopePresentationGeneration)
								return
							timeline.queueVisibleDeferredMaterialization(
								timeline.scopePresentationFinalizing)
						})
					}

					function retirePlainDeferredMaterialization(stableId, generation) {
						const key = String(stableId || "")
						const ownedGeneration = Number(generation)
						if (!plainDeferredMaterializationOwns(key, ownedGeneration))
							return false
						const nextActive = Object.assign({}, plainDeferredMaterializationActive)
						delete nextActive[key]
						plainDeferredMaterializationActive = nextActive
						if (plainDeferredMaterializationActiveCount() === 0)
							finishDeferredMaterializationCohort(ownedGeneration)
						return true
					}

					function scopePresentationPhaseDuration(timestamp) {
						return scopePresentationStartedAt > 0 && Number(timestamp) > 0
							? Math.max(0, Number(timestamp) - scopePresentationStartedAt) : 0
					}

					function noteScopePresentationOuterLoaderLoaded() {
						if (!scopePresentationPending)
							return
						const timestamp = Date.now()
						if (scopePresentationFirstOuterLoaderLoadedAt <= 0)
							scopePresentationFirstOuterLoaderLoadedAt = timestamp
						scopePresentationLastOuterLoaderLoadedAt = timestamp
						++scopePresentationOuterLoaderLoadedCount
					}

					function notePlainLoaderEvent(stableId, generation, phase) {
						if (!scopePresentationPending || generation !== scopePresentationGeneration
								|| scopePresentationPlainLoaderEvents.length >= 128)
							return
						scopePresentationPlainLoaderEvents.push({
							"id": String(stableId), "generation": generation, "phase": phase,
							"ms": scopePresentationPhaseDuration(Date.now())
						})
					}

					function noteScopePresentationPlainRowAdmission() {
						if (!scopePresentationPending || !scopePresentationFinalizing)
							return
						const timestamp = Date.now()
						if (scopePresentationFirstPlainRowAdmissionAt <= 0)
							scopePresentationFirstPlainRowAdmissionAt = timestamp
						scopePresentationLastPlainRowAdmissionAt = timestamp
						++scopePresentationPlainRowAdmissionCount
					}

					function noteScopePresentationRowLoaderLoaded() {
						if (!scopePresentationPending)
							return
						const timestamp = Date.now()
						if (scopePresentationFirstRowLoaderLoadedAt <= 0)
							scopePresentationFirstRowLoaderLoadedAt = timestamp
						scopePresentationLastRowLoaderLoadedAt = timestamp
						++scopePresentationRowLoaderLoadedCount
					}

					function noteScopePresentationHeightCommit() {
						scopePresentationMilestonesDirty = true
						if (!scopePresentationPending)
							return
						const timestamp = Date.now()
						if (scopePresentationFirstHeightCommitAt <= 0)
							scopePresentationFirstHeightCommitAt = timestamp
						scopePresentationLastHeightCommitAt = timestamp
						++scopePresentationHeightCommitCount
					}

					function cachedPlainPresentationEligible(item) {
						// Prepare only already-instantiated cache delegates, after the scope
						// is visible and input has released layout ownership. Never hydrate
						// providers or scan the history model to populate this window.
						if (activeScope.activity || scopePresentationPending || richScrollInProgress
								|| !item || item.performanceTimelineDelegate !== true
								|| item.accessibilityPooled === true)
							return false
						const point = item.mapToItem(timeline, 0, 0)
						if (point.y + item.height <= -cacheBuffer
								|| point.y >= height + cacheBuffer)
							return false
						return item.lightweightDeferredPresentation === true
							? chatModel.deferredMessagePlainForMaterialization(String(item.stableId || ""))
							: item.plainScopeResumeEligible === true
					}

					function queueVisibleDeferredMaterialization(allowDuringFinalization) {
						const pendingAllowed = allowDuringFinalization === true
							&& scopePresentationFinalizing
						if (richScrollInProgress
								|| coldScopeMaterializationDelayTimer.running
								|| prependAnchorActive
								|| deferredMaterializationOwnershipActive()
								|| (scopePresentationPending && !pendingAllowed))
							return
						// Rebuild from the current viewport on every pass. A rich row can
						// expand enough to move a previously queued summary completely
						// offscreen before its 16 ms turn; retaining that stale id would
						// mutate hidden geometry after the conversation has been revealed.
						const candidates = []
						const queuedSet = ({})
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (!item || item.lightweightDeferredPresentation !== true
									|| item.accessibilityPooled === true
									|| item.contentNeedsHydration !== true)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							const visible = item.visible && point.y + item.height > 0.5
								&& point.y < height - 0.5
							if (!visible && !cachedPlainPresentationEligible(item))
								continue
							const stableId = String(item.stableId || "")
							if (stableId.length > 0 && queuedSet[stableId] !== true) {
								candidates.push({ "id": stableId, "viewportY": point.y, "visible": visible })
								queuedSet[stableId] = true
							}
						}
						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let childIndex = 0;
								deferredRepeater && childIndex < deferredRepeater.count; ++childIndex) {
							const item = deferredRepeater.itemAt(childIndex)
							if (!item || !item.visible)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							if (point.y + item.height <= 0.5 || point.y >= height - 0.5)
								continue
							const stableId = item ? String(item.stableId || "") : ""
							if (stableId.length > 0 && queuedSet[stableId] !== true) {
								candidates.push({ "id": stableId, "viewportY": point.y, "visible": true })
								queuedSet[stableId] = true
							}
						}
						// Only promote inline-header rows that themselves intersect the
						// viewport. A visible sibling does not make the rest of the fixed
						// 58 px prefix eligible for hidden geometry work.
						// Cold scopes and prepend headers are both anchored from their
						// newest/bottom edge. Promote that edge first, then rebuild after
						// ListView polishes the resulting real height. This prevents the
						// initial all-summary viewport from admitting an entire rich page
						// that becomes offscreen as soon as its first cards expand.
						candidates.sort(function(left, right) {
							if (left.visible !== right.visible)
								return left.visible ? -1 : 1
							return right.viewportY - left.viewportY
						})
						const queued = candidates.map(function(candidate) {
							return candidate.id
						})
						deferredMaterializationQueue = queued
						if (queued.length > 0 && !deferredMaterializationTimer.running)
							deferredMaterializationTimer.start()
						else
							finishRichContentRecoveryIfIdle()
					}

					function deferredPresentationStillEligible(stableId) {
						const key = String(stableId || "")
						if (key.length === 0)
							return false
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (!item || item.lightweightDeferredPresentation !== true
									|| item.accessibilityPooled === true
									|| String(item.stableId || "") !== key)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							return (item.visible && point.y + item.height > 0.5
								&& point.y < height - 0.5) || cachedPlainPresentationEligible(item)
						}
						const deferredRepeater = headerItem
							? headerItem.deferredRepeater : null
						for (let childIndex = 0;
								deferredRepeater && childIndex < deferredRepeater.count; ++childIndex) {
							const item = deferredRepeater.itemAt(childIndex)
							if (!item || !item.visible
									|| String(item.stableId || "") !== key)
								continue
							const point = item.mapToItem(timeline, 0, 0)
							return point.y + item.height > 0.5
								&& point.y < height - 0.5
						}
						return false
					}

					function queueVisiblePreviewHydration(allowDuringFinalization) {
						const pendingAllowed = allowDuringFinalization === true
							&& scopePresentationFinalizing
						if (richScrollInProgress || prependAnchorActive
								|| (scopePresentationPending && !pendingAllowed))
							return
						const children = contentItem ? contentItem.children : []
						for (let childIndex = 0; childIndex < children.length; ++childIndex) {
							const item = children[childIndex]
							if (item && item.performanceTimelineDelegate === true
									&& item.accessibilityPooled !== true
									&& item.lightweightDeferredPresentation !== true
									&& (item.inVisibleViewport === true
										|| String(item.stableId || "")
											=== nearViewportWarmId))
								item.requestPreviewHydrationIfNeeded(pendingAllowed)
						}
					}

					function materializeNextDeferredRow() {
						const pendingAllowed = scopePresentationPending
							&& scopePresentationFinalizing
						const serializeSourcePromotion = scopePresentationFinalizing || !activeScope.activity
						if (richScrollInProgress
								|| coldScopeMaterializationDelayTimer.running
								|| prependAnchorActive
								|| deferredMaterializationOwnershipActive()
								|| (scopePresentationPending && !pendingAllowed)) {
							scheduleRichContentResume()
							return
						}
						if (deferredMaterializationQueue.length === 0) {
							finishRichContentRecoveryIfIdle()
							return
						}
						const queued = deferredMaterializationQueue.slice()
						let materialized = 0
						const materializationGeneration = scopePresentationGeneration
						if (!serializeSourcePromotion) {
							while (queued.length > 0
									&& materialized < deferredMaterializationsPerFrame) {
								const stableId = String(queued.shift() || "")
								if (deferredPresentationStillEligible(stableId)
										&& chatModel.materializeDeferredMessage(stableId))
									++materialized
							}
						} else {
							const plainCohortIds = []
							let serializedCandidateId = ""
							while (queued.length > 0) {
								const stableId = String(queued.shift() || "")
								if (!deferredPresentationStillEligible(stableId))
									continue
								const knownPlain = !activeScope.activity
									&& chatModel.deferredMessagePlainForMaterialization(stableId)
								if (!knownPlain) {
									if (plainCohortIds.length === 0)
										serializedCandidateId = stableId
									break
								}
								plainCohortIds.push(stableId)
								if (plainCohortIds.length
										>= plainDeferredMaterializationsPerFrame)
									break
							}
							// The model mutation synchronously changes proxy membership. Publish
							// every accepted owner and retire the stale queue before the first
							// mutation so a reentrant delegate callback cannot finish a partial
							// cohort and admit another source row in the same turn.
							deferredMaterializationQueue = []
							if (plainCohortIds.length > 0) {
								const nextPlainActive = ({})
								for (let plainIndex = 0;
										plainIndex < plainCohortIds.length; ++plainIndex)
									nextPlainActive[plainCohortIds[plainIndex]] =
										materializationGeneration
								plainDeferredMaterializationActive = nextPlainActive
								for (let plainIndex = 0;
										plainIndex < plainCohortIds.length; ++plainIndex) {
									const stableId = plainCohortIds[plainIndex]
									if (materializationGeneration !== scopePresentationGeneration
											|| richScrollInProgress
											|| !deferredPresentationStillEligible(stableId)
											|| activeScope.activity
											|| !chatModel.deferredMessagePlainForMaterialization(
												stableId)) {
										retirePlainDeferredMaterialization(stableId,
											materializationGeneration)
										continue
									}
									if (chatModel.materializeDeferredMessage(stableId)) {
										++materialized
										++plainDeferredSourceAdmissionCount
									} else {
										retirePlainDeferredMaterialization(stableId,
											materializationGeneration)
									}
								}
							} else if (serializedCandidateId.length > 0) {
								deferredMaterializationActiveId = serializedCandidateId
								deferredMaterializationActiveGeneration =
									materializationGeneration
								if (chatModel.materializeDeferredMessage(serializedCandidateId)) {
									++materialized
									++serializedDeferredSourceAdmissionCount
								} else {
									deferredMaterializationActiveId = ""
									deferredMaterializationActiveGeneration = -1
									finishDeferredMaterializationCohort(
										materializationGeneration)
								}
							}
						}
						// Never carry candidates across a geometry-changing promotion.
						// The post-polish pass below computes the next viewport from scratch.
						deferredMaterializationQueue = []
						if (topAnchorActive) {
							topAnchorSettleTimer.restart()
							topAnchorFrameTimer.restart()
						}
						if (materialized === 0) {
							// A pooled or already-promoted candidate changed no geometry.
							// Finish this pass instead of immediately rebuilding the same
							// stale one-item queue forever at 16 ms intervals.
							finishRichContentRecoveryIfIdle()
							return
						}
						const finalizationGeneration = scopePresentationFinalizing
							? scopePresentationGeneration : -1
						// Materializing a fixed-height row changes which deferred row
						// intersects the viewport. Wake its asynchronous outer/message
						// loaders now, but keep source admission locked until that row
						// reports its real geometry through completeDeferredMaterialization().
						Qt.callLater(function() {
							if (materializationGeneration !== timeline.scopePresentationGeneration)
								return
							if (finalizationGeneration >= 0
									&& (timeline.scopePresentationGeneration
										!== finalizationGeneration
										|| !timeline.scopePresentationPending
										|| !timeline.scopePresentationFinalizing))
								return
							if (timeline.scopePresentationFinalizing)
								timeline.positionTailImmediately()
							timeline.queueRichLoaderResumes()
							if (!timeline.scopePresentationFinalizing)
								timeline.queueVisibleDeferredMaterialization(false)
						})
					}

					function completeDeferredMaterialization(stableId, generation) {
						const key = String(stableId || "")
						const ownedGeneration = Number(generation)
						if (retirePlainDeferredMaterialization(key, ownedGeneration))
							return true
						if (key.length === 0 || key !== deferredMaterializationActiveId
								|| ownedGeneration !== deferredMaterializationActiveGeneration)
							return false
						deferredMaterializationActiveId = ""
						deferredMaterializationActiveGeneration = -1
						finishDeferredMaterializationCohort(ownedGeneration)
						return true
					}

					function finishRichContentRecoveryIfIdle() {
						if (richScrollInProgress)
							return
						if (scopePresentationPending)
							return
						if (richContentResumeTimer.running || richLoaderResumeTimer.running
								|| deferredMaterializationTimer.running
								|| richLoaderResumeActiveId.length > 0
								|| deferredMaterializationOwnershipActive()) {
							// One paced wakeup leaves incubation time for the pending
							// loaders. Self-queuing callLater callbacks spin between frames.
							if (!richRecoveryIdleTimer.running)
								richRecoveryIdleTimer.start()
							return
						}
						if (richLoaderResumeQueue.length > 0
								|| deferredMaterializationQueue.length > 0)
							return
						richRecoveryIdleTimer.stop()
						if (topAnchorActive) {
							restoreTopAnchor()
							topAnchorSettleTimer.restart()
						}
						chatModel.presentationBusy = false
						// A fully cached room may never enqueue hydration after reveal.
						// Its cold pacing flag must not indefinitely block idle prefetch.
						if (Object.keys(root.pendingPreviewHydrationIds).length === 0)
							root.coldPreviewHydrationPacingActive = false
						scheduleNearViewportPrefetch()
						scheduleStaticPreviewSweep()
					}

					function restoreViewportAnchor(releaseWhenRestored) {
						// A stale coalesced callback must be idempotent after input or
						// identity cancellation. Only an active anchor may record a pending
						// release for a blocked or temporarily missing-row retry.
						if (!viewportAnchorActive) {
							viewportAnchorReleasePending = false
							return false
						}
						if (releaseWhenRestored === true)
							viewportAnchorReleasePending = true
						const restoreBlocked = restoringViewportAnchor || stickToBottom
							|| prependAnchorActive || scopePresentationPending
							|| moving || dragging || flicking || scrollbarDragActive
							|| scrollbarReleasePending || timelineScrollBar.pressed
						if (restoreBlocked)
							return false
						const item = timelineDelegateForStableId(viewportAnchorId)
						if (!item) {
							if (++viewportAnchorRetries <= 8)
								// DelegateChooser replacement needs a polish/animation turn.
								// callLater can spend every retry before that turn occurs.
								viewportAnchorRestoreTimer.restart()
							else
								releaseViewportAnchor()
							return false
						}
						restoringViewportAnchor = true
						const currentOffset = item.mapToItem(timeline, 0, 0).y
						const minimumY = originY
						const maximumY = Math.max(minimumY, originY + contentHeight - height)
						const requestedY = contentY + currentOffset - viewportAnchorOffset
						const desiredY = Math.max(minimumY,
							Math.min(maximumY,
								requestedY))
						if (Math.abs(contentY - desiredY) > 0.5)
							contentY = desiredY
						const restoredOffset = item.mapToItem(timeline, 0, 0).y
						const offsetRestored = Math.abs(restoredOffset
							- viewportAnchorOffset) <= 1
						restoringViewportAnchor = false
						viewportAnchorRetries = 0
						if (viewportAnchorReleasePending) {
							if (offsetRestored)
								releaseViewportAnchor()
							else {
								viewportAnchorReleasePending = false
								stickToBottom = false
							}
						}
						return offsetRestored
					}

					function releasePrependAnchor(preserveViewportAnchor, settledTransactionId) {
						clearViewportAnchorQuietRelease()
						const settlementRetiresCurrent = String(settledTransactionId || "").length > 0
							&& String(settledTransactionId || "") === prependSettlementTransactionId
							&& prependSettlementState === "SETTLEMENT_RECEIPT_PUBLISHED"
						if (!settlementRetiresCurrent)
							cancelPrependSettlementTransaction()
						// The structural prepend settles before its deferred rich rows are
						// allowed to replace their fixed-height summaries. Hand the exact
						// message/offset to the ordinary viewport anchor on the quiet path
						// so that those later card expansions cannot displace what the user
						// was reading. Cancellation paths (input, search, scope reset and
						// missing rows) omit the flag and do not create this handoff.
						const handoffToViewport = preserveViewportAnchor === true
							&& prependAnchorActive && !stickToBottom
							&& !scopePresentationPending && prependAnchorId.length > 0
						const preservedAnchorId = prependAnchorId
						const preservedAnchorOffset = prependAnchorOffset
						prependAnchorSettleTimer.stop()
						prependMutationInProgress = false
						prependAnchorActive = false
						deferredPrependAnchorActive = false
						prependAnchorId = ""
						prependAnchorRetries = 0
						prependAnchorCorrectionFrames = 0
						prependAnchorInsertedExtent = 0
						if (settlementRetiresCurrent)
							prependSettlementState = "ANCHOR_AND_PENDING_OWNERSHIP_RETIRED"
						if (handoffToViewport) {
							viewportAnchorId = preservedAnchorId
							viewportAnchorOffset = preservedAnchorOffset
							viewportAnchorRetries = 0
							viewportAnchorReleasePending = false
							viewportAnchorActive = true
						}
						scheduleRichContentResume()
						if (settlementRetiresCurrent) {
							prependSettlementState = "SETTLED"
							if (prependSettlementRetirementUpdateTransactionId
									!== prependSettlementTransactionId) {
								prependSettlementRetirementUpdateTransactionId =
									prependSettlementTransactionId
								root.update()
							}
						} else {
							Qt.callLater(function() {
								timeline.scheduleNextVisiblePresentationHeightSettlement()
							})
							root.update()
						}
					}

					function extendDeferredPrependAnchorTarget(insertedCount) {
						prependAnchorInsertedExtent +=
							Math.max(0, insertedCount) * deferredMessageRowHeight
						// InlineHeader grows into negative content coordinates. The existing
						// rich rows keep their coordinates, so preserving the user's exact
						// viewport means retaining contentY rather than adding the prefix height.
						prependAnchorTargetY = prependAnchorContentY
					}

					function restoreDeferredPrependAnchor() {
						if (!prependAnchorActive || !deferredPrependAnchorActive
								|| restoringPrependAnchor || scrollbarDragActive
								|| scrollbarReleasePending || timelineScrollBar.pressed)
							return
						// The technical prefix starts with a fixed estimate, but each row
						// can become a differently sized text, gallery, or media card as
						// it approaches the viewport. Preserve the stable message itself,
						// not the temporary prefix height, through every refinement.
						restorePrependAnchor()
					}

					function deferPrependedRichRows(first, last) {
						const deferred = Object.assign({}, deferredPrependRichIds || ({}))
						for (let rowIndex = Math.max(0, first);
								rowIndex <= Math.min(last, chatModel.count - 1); ++rowIndex) {
							const row = chatModel.get(rowIndex) || ({})
							const preview = row.preview || ({})
							const attachments = row.attachments || []
							if (row.deferredPresentation !== true
									|| (Object.keys(preview).length === 0
										&& attachments.length === 0))
								continue
							const stableId = String(row.id || "")
							if (stableId.length > 0)
								deferred[stableId] = true
						}
						deferredPrependRichIds = deferred
					}

					function clearDeferredPrependRichRows() {
						deferredPrependRichIds = ({})
					}

					function prependRichRowDeferred(stableId) {
						return deferredPrependRichIds[String(stableId || "")] === true
					}

					function prependRichContentActive(stableId, rowVisible) {
						return !prependRichRowDeferred(stableId)
							|| (rowVisible && !richScrollInProgress)
					}

					function markPrependRichRowMaterialized(stableId) {
						const key = String(stableId || "")
						if (key.length === 0 || !prependRichRowDeferred(key))
							return
						const deferred = Object.assign({}, deferredPrependRichIds || ({}))
						delete deferred[key]
						deferredPrependRichIds = deferred
					}

					function restorePrependAnchor() {
						if (!prependAnchorActive || restoringPrependAnchor
								|| scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return
						const row = chatModel.presentationModel.rowForStableId(prependAnchorId)
						if (row < 0) {
							releasePrependAnchor()
							return
						}
						let item = itemAtIndex(row)
						if (!item) {
							if (++prependAnchorRetries > 8) {
								releasePrependAnchor()
								return
							}
							positionViewAtIndex(row, ListView.Beginning)
							Qt.callLater(function() { timeline.restorePrependAnchor() })
							return
						}
						restoringPrependAnchor = true
						const minimumY = originY
						const maximumY = Math.max(minimumY, originY + contentHeight - height)
						const desiredY = Math.max(minimumY, Math.min(maximumY, item.y - prependAnchorOffset))
						if (Math.abs(contentY - desiredY) > 0.5)
							contentY = desiredY
						restoringPrependAnchor = false
						prependAnchorSettleTimer.restart()
					}

					function containTailMessageWhenPossible() {
						if (count <= 0 || scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return false
						const item = itemAtIndex(count - 1)
						if (!item || !item.visible)
							return false
						// An oversized latest message must remain identifiable. Showing an
						// attachment/card fragment without its actor and body makes the first
						// frame incoherent, so prefer the message start and let the user scroll
						// through the remaining rich content.
						if (item.height > height + 1.0) {
							if (item.y < contentY - 1.0) {
								const previousY = contentY
								setTailContentY(item.y)
								if (scopePresentationFinalizing
										&& Math.abs(contentY - previousY) > 0.5)
									scopePresentationTailContainedMutationCount =
										scopePresentationMutationCount
							}
							return Math.abs(contentY - item.y) <= 1.0
								&& Math.abs(contentY - tailContentYTarget()) > 0.5
						}
						// A rich row can be taller than the viewport because its actor/body chrome
						// surrounds an otherwise fully containable preview. Tail-following must keep
						// that primary card intact instead of clipping its first device-independent
						// pixels at fractional DPR.
						const richBounds = item.primaryRichContentViewportBounds
							? item.primaryRichContentViewportBounds() : null
						if (richBounds && richBounds.height > 0 && richBounds.height <= height + 1.0) {
							let correction = 0
							if (richBounds.top < -0.5)
								correction = richBounds.top
							else if (richBounds.bottom > height + 0.5)
								correction = richBounds.bottom - height
							if (Math.abs(correction) > 0.5) {
								const previousY = contentY
								setTailContentY(contentY + correction)
								if (scopePresentationFinalizing
										&& Math.abs(contentY - previousY) > 0.5)
									scopePresentationTailContainedMutationCount =
											scopePresentationMutationCount
							}
							return Math.abs(contentY - tailContentYTarget()) > 0.5
						}
						// The inline footer normally gives the latest message breathing room.
						// If that footer pushes an otherwise fitting rich message above the
						// viewport, trade only the required footer pixels for complete content.
						// This also keeps the visible row's semantic subtree available at
						// fractional DPR instead of exposing a visually clipped message.
						if (item.y < contentY - 1.0) {
							const previousY = contentY
							setTailContentY(item.y)
							if (scopePresentationFinalizing
									&& Math.abs(contentY - previousY) > 0.5)
									scopePresentationTailContainedMutationCount =
										scopePresentationMutationCount
						}
						return Math.abs(contentY - item.y) <= 1.0
							&& Math.abs(contentY - tailContentYTarget()) > 0.5
					}

					// Delegate y-coordinates settle over several scene-graph frames after a
					// structural insert. Correct only during that bounded window; this is
					// dormant during steady-state chat and ordinary scrolling.
					FrameAnimation {
						id: prependAnchorFrameCorrection
						running: timeline.prependAnchorActive
							&& timeline.prependAnchorCorrectionFrames > 0
						onTriggered: {
							if (timeline.deferredPrependAnchorActive)
								timeline.restoreDeferredPrependAnchor()
							else
								timeline.restorePrependAnchor()
							timeline.prependAnchorCorrectionFrames = Math.max(0,
								timeline.prependAnchorCorrectionFrames - 1)
						}
					}

					onContentYChanged: {
						scheduleViewportMembershipUpdate()
						// The custom handler emits a balanced start/end pair even for a
						// coarse wheel packet clamped at a physical edge. Transfer ownership
						// only from an observed viewport mutation, preserving thumb-era pins
						// for a zero-distance packet.
						if (timelineScrollHandler.scrolling && !explicitNavigationMovement)
							takeExplicitNavigationOwnership("wheel-or-middle-drag")
						const staticPreheatDelta = contentY - staticPreviewPreheatLastContentY
						if (Math.abs(staticPreheatDelta) > 0.5
								&& (richScrollInProgress || explicitWheelInputPending
									|| explicitNavigationMovement)) {
							if (Math.abs(staticPreheatDelta) >= Math.max(96, height * 0.75))
								staticPreviewPreheatTeleportPending = true
							const nextDirection = staticPreheatDelta < 0 ? -1 : 1
							if (nextDirection !== staticPreviewPreheatDirection) {
								for (let index = 0; index < staticPreviewPreheatQueue.length; ++index) {
									const entry = staticPreviewPreheatQueue[index]
									if (String(entry.origin || "") === "sweep")
										reopenStaticPreviewSweepForCanceledIndex(entry.sweepIndex)
								}
								staticPreviewPreheatQueue = []
							}
							staticPreviewPreheatDirection = nextDirection
						}
						staticPreviewPreheatLastContentY = contentY
						scheduleStaticPreviewLookahead()
						// A native wheel event can arrive while a geometry-driven
						// movement epoch is already active, in which case ListView does
						// not emit a second movementStarted. A real content delta inside
						// the one-turn wheel token still transfers ownership to the user;
						// an edge-clamped wheel event changes nothing and expires quietly.
						if (explicitWheelInputPending
								&& Math.abs(contentY - explicitWheelInputStartY) > 0.01)
							takeExplicitNavigationOwnership("native-wheel")
					}
					onOriginYChanged: scheduleViewportMembershipUpdate()
					onHeightChanged: {
						scheduleViewportMembershipUpdate()
						if (!scopePresentationPending && stickToBottom
								&& !dragging && !flicking && !scrollbarDragActive
								&& !scrollbarReleasePending && !timelineScrollBar.pressed) {
							const generation = scopePresentationGeneration
							Qt.callLater(function() {
								if (generation === timeline.scopePresentationGeneration
										&& timeline.stickToBottom
										&& !timeline.scopePresentationPending)
									timeline.requestBottomFollow()
							})
						}
					}

					function visiblePresentationDeadlineReady(diagnostics) {
						if (count === 0)
							return true
						// The hard deadline may publish only a real, bounded message
						// summary or a completed row. A transparent reservation or blank
						// provider shell remains ineligible even at the deadline.
						return diagnostics.visibleCount > 0
							&& diagnostics.paintedSummaryCount === diagnostics.visibleCount
							&& diagnostics.blankCount === 0
							&& diagnostics.deadlineFallbackCount
								+ diagnostics.fullMessageCount >= diagnostics.visibleCount
					}

					function forceVisibleDeadlineFallbacks() {
						function forceFallback(item) {
							if (!item || item.performanceTimelineDelegate !== true
									|| item.accessibilityPooled === true)
								return
							const point = item.mapToItem(timeline, 0, 0)
							if (point.y + item.height <= 0.5 || point.y >= height - 0.5)
								return
							const paintedComplete = item.paintedSurface === true
								|| item.paintedTerminal === true
							// One stalled row must not collapse ready neighbors at the scope
							// deadline. Preserve every already-painted complete presentation.
							if (item.deadlinePresentationFallback !== true
									&& item.messageRowReady === true && paintedComplete)
								return
							if (typeof item.forceDeadlinePresentationFallback === "function")
								item.forceDeadlinePresentationFallback()
						}
						const children = contentItem ? contentItem.children : []
						for (let index = 0; index < children.length; ++index)
							forceFallback(children[index])
						const deferredRepeater = headerItem ? headerItem.deferredRepeater : null
						for (let index = 0;
								deferredRepeater && index < deferredRepeater.count; ++index)
							forceFallback(deferredRepeater.itemAt(index))
						forceLayout()
						positionTailImmediately()
						forceLayout()
						// Shrinking one oversized reservation can bring the next row into
						// view. Bound that newly visible row in the same deadline turn.
						for (let index = 0; index < children.length; ++index)
							forceFallback(children[index])
					}
					onContentHeightChanged: {
						scheduleViewportMembershipUpdate()
						if (scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed)
							return
						if (scopePresentationPending) {
							noteScopePresentationMutation()
							// Keep the hidden cold transaction anchored to its real tail
							// while the single admitted row replaces its 58 px estimate.
							// Otherwise the obsolete top viewport admits the whole page
							// before the first rich row has finished polishing.
							if (scopePresentationFinalizing && stickToBottom
									&& !restoringBottom)
								positionTailImmediately()
							return
						}
						if (scopePresentationObservationActive)
							++scopePresentationExposedHeightChangeCount
						if (scopePresentationObservationActive && stickToBottom
								&& !dragging && !flicking && !scrollbarDragActive
								&& !scrollbarReleasePending && !timelineScrollBar.pressed) {
							// A late provider/card refinement is layout movement, not user
							// navigation. Keep the freshly opened conversation pinned to its
							// current tail in the same turn so ListView cannot reinterpret the
							// automatic displacement as an upward scroll.
							restoringBottom = true
							const maximumY = tailContentYTarget()
							setTailContentY(maximumY)
							Qt.callLater(function() {
								timeline.containTailMessageWhenPossible()
								timeline.restoringBottom = false
								timeline.markVisibleScopeReadIfEligible()
							})
							return
						}
						if (topAnchorActive) {
							// Every late rich-card/body refinement opens a new quiet
							// window. A single callLater correction is insufficient when
							// image and provider geometry arrive over several frames.
							topAnchorSettleTimer.restart()
							topAnchorFrameTimer.restart()
							Qt.callLater(function() { timeline.restoreTopAnchor() })
							return
						}
						if (prependAnchorActive) {
							// Pure older-history rows have a fixed technical height until
							// they approach the viewport. Preserve their exact displacement
							// numerically; itemAtIndex() would synchronously materialize the
							// newly prepended page.
							if (deferredPrependAnchorActive)
								Qt.callLater(function() {
									timeline.restoreDeferredPrependAnchor()
								})
							else if (!restoringPrependAnchor)
								Qt.callLater(function() { timeline.restorePrependAnchor() })
							return
						}
						if (scopeResetPending && !restoringBottom) {
							positionTailImmediately()
							requestBottomFollow()
						} else if (viewportAnchorActive && !restoringViewportAnchor
								&& !restoringBottom && !moving)
							Qt.callLater(function() { timeline.restoreViewportAnchor() })
						else if (!restoringBottom)
							requestBottomFollow()
					}
					onMovementStarted: {
						const pendingNativeWheel = explicitWheelInputPending
						const scrollbarRelease = scrollbarReleasePending
							&& !scrollbarDragActive && !timelineScrollBar.pressed
						const explicitNavigation = dragging
							|| timelineScrollHandler.scrolling
							|| timelineScrollBar.pressed
							|| scrollbarDragActive || scrollbarRelease
							|| root.performanceChatScrollRunning
							|| pendingNativeWheel
						explicitWheelInputPending = false
						if (scrollbarRelease) {
							explicitNavigationMovement = true
							lastMovementClassification = "scrollbar-release"
							return
						}
						if (!explicitNavigation) {
							explicitNavigationMovement = false
							++automaticLayoutMovementCount
							lastMovementClassification = "layout"
						} else
							takeExplicitNavigationOwnership(dragging ? "drag"
								: timelineScrollHandler.scrolling ? "wheel-or-middle-drag"
								: timelineScrollBar.pressed ? "scrollbar"
								: root.performanceChatScrollRunning ? "performance"
								: "native-wheel")
						// QQuickListView also emits movementStarted when an asynchronous
						// delegate changes its measured geometry. Preserve whichever stable
						// content anchor owns that automatic movement; mouse, touch,
						// scrollbar and performance-runner input are classified above.
						if (!explicitNavigation) {
							if (stickToBottom)
								requestBottomFollow()
							else if (topAnchorActive) {
								topAnchorFrameTimer.restart()
								topAnchorSettleTimer.restart()
							} else if (prependAnchorActive) {
								prependAnchorSettleTimer.restart()
							} else if (viewportAnchorActive) {
								Qt.callLater(function() {
									timeline.restoreViewportAnchor()
								})
							}
							return
						}
					}
					onMovementEnded: {
						scheduleStationaryThumbWarmReconcile()
						const explicitNavigation = explicitNavigationMovement
						explicitNavigationMovement = false
						if (timelineScrollHandler.navigationTransitionActive
								|| timelineScrollHandler.gestureActive
								|| timelineScrollHandler.scrolling || scopePresentationPending
								|| root.performanceChatScrollOwnershipActive) {
							scheduleViewportMembershipUpdate()
							return
						}
						if (scrollbarDragActive || scrollbarReleasePending
								|| timelineScrollBar.pressed) {
							scheduleViewportMembershipUpdate()
							return
						}
						if (!restoringBottom) {
							if (!explicitNavigation) {
								if (stickToBottom)
									requestBottomFollow()
								else if (topAnchorActive) {
									topAnchorFrameTimer.restart()
									topAnchorSettleTimer.restart()
									restoreTopAnchor()
								} else if (prependAnchorActive) {
									if (deferredPrependAnchorActive)
										restoreDeferredPrependAnchor()
									else
										restorePrependAnchor()
								} else if (viewportAnchorActive) {
									restoreViewportAnchor()
								} else {
									// Geometry can temporarily clamp a reading viewport to the
									// end. Only user navigation may establish follow-latest
									// intent; an automatic layout must preserve reading mode.
									if (stickToBottom)
										requestBottomFollow()
									else
										captureViewportAnchor()
								}
								scheduleRichContentResume()
								if (stickToBottom)
									Qt.callLater(function() {
										timeline.markVisibleScopeReadIfEligible()
									})
								return
							}
							// An automatic ListView geometry movement may finish before the
							// coalesced bottom-follow timer gets its next frame. Preserve an
							// existing tail intent here; explicit input paths clear it before
							// movement starts and still recompute whether the gesture ended
							// back at the exact tail.
							if (!stickToBottom)
								stickToBottom = isAtBottom()
							requestBottomFollow()
							if (!stickToBottom)
								captureViewportAnchor()
							scheduleRichContentResume()
							if (stickToBottom)
								Qt.callLater(function() {
									timeline.markVisibleScopeReadIfEligible()
								})
						}
					}

					Timer {
						id: coldScopeMaterializationDelayTimer
						interval: timeline.coldScopeMaterializationDelayMs
						repeat: false
						onTriggered: {
							if (timeline.scopePresentationPending
									&& timeline.scopePresentationFinalizing) {
								timeline.queueVisibleDeferredMaterialization(true)
								scopePresentationFinalizeQuietTimer.restart()
								return
							}
							timeline.scheduleRichContentResume()
						}
					}

					Timer {
						id: richRecoveryIdleTimer
						interval: 16
						repeat: false
						onTriggered: timeline.finishRichContentRecoveryIfIdle()
					}

					Timer {
						id: richContentResumeTimer
						interval: 0
						repeat: false
						onTriggered: {
							if (timeline.richScrollInProgress)
								return
							if (!timeline.stickToBottom && !timeline.viewportAnchorActive)
								timeline.captureViewportAnchor()
							timeline.queueRichLoaderResumes()
							timeline.queueVisibleDeferredMaterialization()
							// Loading previews count as structurally ready rows, so they are
							// intentionally absent from queueRichLoaderResumes(). Rescan the
							// settled viewport explicitly to admit any request that crossed
							// the visibility boundary while scrolling.
							timeline.queueVisiblePreviewHydration()
						}
					}

					Timer {
						id: viewportMembershipTimer
						interval: 0
						repeat: false
						onTriggered: {
							timeline.viewportMembershipUpdatePending = false
							timeline.updateViewportMemberships()
							// A membership transition can retire the previous offscreen
							// resource while this single-shot still reports running. Flush
							// any geometry dirtied by the scan before readiness or prefetch
							// can observe the new snapshot.
							Qt.callLater(function() {
								if (timeline.viewportMembershipUpdatePending) {
									if (!viewportMembershipTimer.running)
										viewportMembershipTimer.start()
									return
								}
								if (!timeline.richScrollInProgress)
									timeline.scheduleRichContentResume()
								timeline.scheduleNearViewportPrefetch()
							})
						}
					}

					Timer {
						id: richLoaderResumeTimer
						interval: 16
						repeat: false
						onTriggered: timeline.resumeNextRichLoader()
					}

					Image {
						id: staticPreviewPreheater
						objectName: "staticPreviewPreheater"
						visible: false
						width: 1
						height: 1
						source: timeline.staticPreviewPreheatActiveSource
						asynchronous: true
						cache: true
						sourceSize: Qt.size(1280, 960)
						onStatusChanged: {
							const observedSource = String(source || "")
							const observedStatus = status
							// Completing mutates staticPreviewPreheatActiveSource, which owns
							// this Image's source binding. Leave the binding evaluation first
							// and ignore a stale completion after a teleport/reprioritization.
							Qt.callLater(function() {
								if (observedSource.length > 0
										&& String(staticPreviewPreheater.source || "") === observedSource
										&& timeline.staticPreviewPreheatActiveSource === observedSource)
									timeline.completeStaticPreviewPreheat(observedStatus)
							})
						}
						onSourceChanged: Qt.callLater(function() {
							if (staticPreviewPreheater.status === Image.Ready
									|| staticPreviewPreheater.status === Image.Error)
								timeline.completeStaticPreviewPreheat(staticPreviewPreheater.status)
						})
						Accessible.ignored: true
					}

					ListModel {
						id: staticPreviewRetainedModel
					}

					Repeater {
						model: staticPreviewRetainedModel
						delegate: Image {
							visible: false
							width: 1
							height: 1
							source: String(model.source || "")
							asynchronous: true
							cache: true
							sourceSize: Qt.size(1280, 960)
							Accessible.ignored: true
						}
					}

					Timer {
						id: staticPreviewLookaheadTimer
						interval: 16
						repeat: false
						onTriggered: timeline.refillStaticPreviewLookahead()
					}

					Timer {
						id: staticPreviewSweepTimer
						// Idle room sweep is intentionally slower than one frame and shares
						// the same single static decoder used by wheel lookahead.
						interval: 96
						repeat: false
						onTriggered: timeline.advanceStaticPreviewSweep()
					}

					Timer {
						id: staticPreviewRetryTimer
						interval: 10000
						repeat: false
						onTriggered: timeline.retryCooledStaticPreviews()
					}

					Timer {
						id: nearViewportPrefetchTimer
						interval: 16
						repeat: false
						onTriggered: timeline.startNearViewportPrefetch()
					}

					Timer {
						id: nearViewportPrefetchDeadlineTimer
						// Adjacent preheating is speculative and must not monopolize the
						// warm slot or outlive the strict populated-room readiness gate.
						// Healthy cached provider trees settle well inside this bound;
						// slower work remains retryable when the row becomes visible.
						interval: 2400
						repeat: false
						onTriggered: timeline.expireNearViewportPrefetch()
					}

					Timer {
						id: richScrollRetentionRampTimer
						interval: 16
						repeat: true
						onTriggered: {
							timeline.richScrollRetentionBuffer = Math.min(
								timeline.richScrollRetentionTarget,
								timeline.richScrollRetentionBuffer
									+ timeline.richScrollRetentionRampStep)
							if (timeline.richScrollRetentionBuffer
									>= timeline.richScrollRetentionTarget)
								stop()
						}
					}

					Timer {
						id: topAnchorFrameTimer
						interval: 16
						repeat: false
						onTriggered: timeline.restoreTopAnchor()
					}

					Timer {
						id: topAnchorProgrammaticMovementGuardTimer
						// QQuickListView can emit its delayed movementStarted after the
						// frame-driven scroll runner has already installed the top anchor.
						// Ignore only that bounded programmatic tail; real drag/flick input
						// still cancels the anchor immediately.
						interval: 96
						repeat: false
						onTriggered:
							timeline.topAnchorProgrammaticMovementGuard = false
					}

					Timer {
						id: topAnchorSettleTimer
						interval: 180
						repeat: false
						onTriggered: {
							const recoveryPending = chatModel.presentationBusy
								|| coldScopeMaterializationDelayTimer.running
								|| richContentResumeTimer.running
								|| richLoaderResumeTimer.running
								|| deferredMaterializationTimer.running
								|| timeline.richLoaderResumeQueue.length > 0
								|| timeline.richLoaderResumeActiveId.length > 0
								|| timeline.deferredMaterializationQueue.length > 0
								|| timeline.pendingScopeHydrationCount() > 0
								|| timeline.pendingVisibleRichRecoveryCount() > 0
							if (recoveryPending) {
								timeline.restoreTopAnchor()
								restart()
							} else {
								timeline.releaseTopAnchor()
								timeline.scheduleNearViewportPrefetch()
							}
						}
					}

					Timer {
						id: viewportAnchorRestoreTimer
						interval: 16
						repeat: false
						onTriggered: timeline.restoreViewportAnchor()
					}

					Timer {
						id: viewportAnchorQuietReleaseTimer
						interval: 16
						repeat: false
						onTriggered:
							timeline.observeViewportAnchorQuietRelease(
								timeline.viewportAnchorQuietReleaseGeneration)
					}

					Timer {
						id: deferredMaterializationTimer
						interval: 16
						repeat: false
						onTriggered: timeline.materializeNextDeferredRow()
					}

					Timer {
						id: prependAnchorSettleTimer
						// Four scene-graph frame corrections already cover the structural
						// insert. Keep a short quiet window after the last correction without
						// turning a static, responsive viewport into a >60 ms frame interval.
						interval: 48
						repeat: false
						onTriggered: {
							// The anchor owns only the structural prepend/correction
							// window. Rich recovery and preview hydration are deliberately
							// gated while it is active, so waiting for those queues here
							// creates a circular wait after a data-only row change.
							const correctionPending = chatModel.prependPending
								|| timeline.prependMutationInProgress
								|| timeline.prependAnchorCorrectionFrames > 0
							if (correctionPending) {
								if (timeline.deferredPrependAnchorActive)
									timeline.restoreDeferredPrependAnchor()
								else
									timeline.restorePrependAnchor()
								restart()
							} else if (!timeline.prependSettlementTransactionActive()) {
								timeline.releasePrependAnchor(true)
							}
						}
					}

					Timer {
						id: prependSettlementDeadlineTimer
						interval: 300
						repeat: false
						onTriggered: {
							if (timeline.prependSettlementTransactionActive())
								timeline.releasePrependAnchor(true)
						}
					}

					Timer {
						id: scopePresentationFastPathTimer
						interval: 0
						repeat: false
						onTriggered: timeline.completeScopePresentationFastPath()
					}

					Timer {
						id: scopePresentationQuietTimer
						// Model append-idle is the authoritative DataReady boundary. Yield
						// one frame for its final synchronous bindings, then let the
						// mutation-aware 48 ms finalization loop prove geometry stability;
						// duplicating that quiet window here breaks the 300 ms cached gate.
						interval: 16
						repeat: false
						onTriggered: timeline.finishScopePresentation(false)
					}

					Timer {
						id: scopePresentationDeadlineTimer
						readonly property int defaultInterval: 2000
						interval: defaultInterval
						repeat: false
						onTriggered: {
							timeline.scopePresentationForcedByDeadline = true
							timeline.finishScopePresentation(true)
						}
					}

					Timer {
						id: scopePresentationFinalizeStartTimer
						// The append-idle gate already yielded one full frame. This timer
						// only separates source promotion from the final model insertion.
						interval: 0
						repeat: false
						onTriggered: timeline.continueScopePresentationFinalization()
					}

					Timer {
						id: scopePresentationFinalizeQuietTimer
						// Delegate height release and viewport membership both settle on
						// frame-paced observations. Hidden finalization now preserves the
						// height latch across its own tail movement, so one frame is enough
						// between retries without paying three duplicate quiet windows.
						interval: 16
						repeat: false
						onTriggered: timeline.completeScopePresentationFinalization(false)
					}

					Timer {
						id: scopePresentationFinalizeDeadlineTimer
						// Tail materialization can briefly collapse ListView's height estimate
						// while rich delegates are rebuilt. The ordinary 120 ms quiet window
						// still reveals warm scopes quickly; this deadline only bounds a cold,
						// continuously mutating scope before it can hold the loading surface.
						interval: 1500
						repeat: false
						onTriggered: timeline.completeScopePresentationFinalization(true)
					}

					Timer {
						id: scopePresentationObservationTimer
						interval: 400
						repeat: false
						onTriggered: {
							timeline.scopePresentationObservationActive = false
							// Room changes cancel timer callbacks, not their bounded cooldown
							// state. Re-open expired still-image retries (or re-arm the next
							// deadline) as soon as the replacement scope is genuinely quiet.
							timeline.retryCooledStaticPreviews()
							timeline.scheduleNearViewportPrefetch()
							timeline.scheduleStaticPreviewLookahead()
						}
					}

					Timer {
						id: scrollbarReleaseGuardTimer
						interval: 128
						repeat: false
						onTriggered: {
							timeline.scrollbarReleasePending = false
							timeline.clearStationaryThumbWarmPins("thumb-release")
							timeline.scheduleViewportMembershipUpdate()
							timeline.scheduleRichContentResume()
							timeline.requestBottomFollow()
							timeline.scheduleStaticPreviewLookahead()
							timeline.scheduleStaticPreviewSweep()
							timeline.scheduleStationaryThumbWarmReconcile()
						}
					}

					Timer {
						id: bottomFollowTimer
						// Coalesce the height churn produced by delegate creation, rich-text
						// layout and asynchronous media into at most one correction per frame.
						interval: 16
						repeat: false
							onTriggered: {
								const generation = timeline.pendingBottomFollowGeneration
								const ownsScopeReset = timeline.pendingBottomFollowOwnsScopeReset
								if (generation !== timeline.bottomFollowGeneration
										|| !timeline.stickToBottom || timeline.prependAnchorActive
										|| timeline.scrollbarDragActive
										|| timeline.scrollbarReleasePending
										|| timelineScrollBar.pressed) {
								timeline.restoringBottom = false
								return
							}
							const maximumY = timeline.tailContentYTarget()
							if (!timeline.scopeResetPending && Math.abs(maximumY - timeline.contentY) <= 0.5) {
								timeline.restoringBottom = false
								return
							}
							timeline.restoringBottom = true
							timeline.setTailContentY(maximumY)
							Qt.callLater(function() {
								if (generation !== timeline.bottomFollowGeneration
										|| timeline.scrollbarDragActive
										|| timeline.scrollbarReleasePending
										|| timelineScrollBar.pressed) {
									timeline.restoringBottom = false
									return
								}
								const tailContained = timeline.containTailMessageWhenPossible()
								timeline.restoringBottom = false
								if (ownsScopeReset)
									timeline.scopeResetPending = false
								if (timeline.stickToBottom && !timeline.isNearBottom()
										&& !tailContained)
									timeline.requestBottomFollow()
								else
									timeline.markVisibleScopeReadIfEligible()
							})
						}
					}

					Connections {
						target: chatModel
						function invalidate() {
							++timeline.orderedMessageRevision
							timeline.scopePresentationMilestonesDirty = true
						}
						function onRowsInserted() { invalidate() }
						function onRowsRemoved() { invalidate() }
						function onRowsMoved() { invalidate() }
						function onModelReset() { invalidate() }
						function onLayoutChanged() { invalidate() }
						function onDataChanged() { invalidate() }
					}

					Connections {
						target: chatModel
						function onScopeReplacementPresentationRequested() {
							// Rapid A -> B -> A navigation can restore the already
							// selected token after A's rows were precleared. Re-open
							// the same presentation transaction explicitly because the
							// typed selection correctly emits no token-change signal.
							timeline.beginScopeChange()
						}
						function onRowsAboutToBePrepended(count) {
							if (count <= 0)
								return
							if (!timeline.scopeResetPending && !timeline.stickToBottom
									&& !timeline.prependAnchorActive) {
								timeline.prependMutationInProgress = timeline.capturePrependAnchor()
								timeline.deferredPrependAnchorActive =
									timeline.prependMutationInProgress
								if (timeline.prependMutationInProgress)
									timeline.beginPrependSettlementTransaction(count)
							}
						}
						function onPrependPendingChanged() {
							if (chatModel.prependPending) {
								if (timeline.prependSettlementTransactionActive())
									timeline.prependSettlementPendingObserved = true
								// A page insert and late source-preview results can otherwise
								// compete for the same GUI turn. Keep preview DTO changes in the
								// model's bounded queue until the prepend anchor is stable.
								chatModel.presentationBusy = true
								return
							}
							timeline.markPrependSettlementModelApplied()
							timeline.noteScopePresentationDataReady()
							if (!timeline.deferredPrependAnchorActive) {
								timeline.scheduleRichContentResume()
								return
							}
							// Deferred rows live in the lightweight inline header and never
							// enter the warm rich ListView model. Preserve the original item by
							// the exact fixed prefix extent, then release after one quiet turn.
							timeline.restoreDeferredPrependAnchor()
						}
						function onAppendPendingChanged() {
							if (!chatModel.appendPending) {
								timeline.noteScopePresentationDataReady()
								// replaceMessages() publishes the complete model before its
								// append-idle signal. Probe an eligible plain/sparse scope once
								// on the zero-delay timer; never scan the model synchronously and
								// then schedule the same failed probe again.
								timeline.scheduleScopePresentationFastPath()
								// A plain row can still own a short body-parse mutation at this
								// boundary. Keep the existing quiet timer as the bounded rich
								// fallback so that mutation can re-enter the sparse fast path.
							}
						}
						function onRowsAboutToChange(first, last) {
							if (!chatModel.presentationBusy
									&& !timeline.topAnchorActive
									&& !timeline.scopeResetPending && !timeline.stickToBottom
									&& !timeline.prependAnchorActive
									&& !timeline.viewportAnchorActive)
								timeline.capturePrependAnchor()
						}
						function onDataChanged(topLeft, bottomRight, roles) {
							if (timeline.prependAnchorActive)
								return
							if (timeline.scopePresentationPending) {
								timeline.noteScopePresentationMutation()
								timeline.scheduleScopePresentationFastPath()
							} else
								timeline.requestBottomFollow()
							timeline.reopenStaticPreviewSweepForNewData()
						}
						function onRowsAboutToBeInserted(parentIndex, first, last) {
							timeline.followTailAfterInsert = false
							timeline.pendingTailInsertCount = 0
							if (timeline.scopeResetPending) {
								timeline.followTailAfterInsert = true
							} else if (first === 0 && timeline.count > 0
									&& !timeline.prependAnchorActive) {
								// ChatTimelineModel announces the mutation before Qt begins the
								// row insertion. Keep that first geometry snapshot; recapturing
								// here can observe ListView's transitional one-row offset.
								timeline.prependMutationInProgress = timeline.capturePrependAnchor()
								timeline.deferredPrependAnchorActive =
									timeline.prependMutationInProgress
								if (timeline.prependMutationInProgress)
									timeline.beginPrependSettlementTransaction(last - first + 1)
							} else if (first >= timeline.count) {
								timeline.followTailAfterInsert = timeline.count === 0
									|| (timeline.stickToBottom && timeline.isNearBottom())
								if (!timeline.followTailAfterInsert)
									timeline.pendingTailInsertCount = Math.max(0, last - first + 1)
							}
						}
						function onRowsInserted(parentIndex, first, last) {
							if (first === 0)
								timeline.deferPrependedRichRows(first, last)
							if (first === 0 && timeline.prependAnchorActive) {
								timeline.extendDeferredPrependAnchorTarget(last - first + 1)
								Qt.callLater(function() {
									// ListView may apply its automatic insert displacement one or
									// two scene-graph frames after a chunk arrives. Keep correcting
									// to the original numerical target across that bounded window.
									timeline.prependAnchorCorrectionFrames = Math.max(
										timeline.prependAnchorCorrectionFrames, 4)
									timeline.restoreDeferredPrependAnchor()
									timeline.prependMutationInProgress = false
									if (!chatModel.prependPending)
										timeline.markPrependSettlementModelApplied()
								})
							} else if (timeline.followTailAfterInsert) {
								timeline.releaseViewportAnchor()
								timeline.releaseTopAnchor()
								timeline.stickToBottom = true
								// Scope replacement happens before the next scene render. Position
								// the newly inserted tail synchronously so an old scroll offset is
								// never presented, then retain the coalesced follow-up for any later
								// rich-text or preview height refinement.
								if (timeline.scopeResetPending)
									timeline.positionTailImmediately()
								timeline.requestBottomFollow()
							} else if (timeline.pendingTailInsertCount > 0)
								timeline.pendingTailMessageCount += timeline.pendingTailInsertCount
							if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							timeline.followTailAfterInsert = false
							timeline.pendingTailInsertCount = 0
						}
						function onModelReset() {
							timeline.scheduleViewportMembershipUpdate()
							timeline.clearStationaryThumbWarmPins("model-reset")
							timeline.releasePrependAnchor()
							timeline.releaseViewportAnchor()
							timeline.releaseTopAnchor()
							timeline.clearDeferredPrependRichRows()
							timeline.pendingTailInsertCount = 0
							timeline.pendingTailMessageCount = 0
							timeline.stickToBottom = true
							if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							else
								timeline.requestBottomFollow()
						}
						function onCountChanged() {
							timeline.scheduleViewportMembershipUpdate()
							if (timeline.scopePresentationPending) {
								timeline.noteScopePresentationMutation()
								timeline.scheduleScopePresentationFastPath()
							}
							if (timeline.count === 0) {
								timeline.clearStationaryThumbWarmPins("model-empty")
								timeline.releasePrependAnchor()
								timeline.releaseViewportAnchor()
								timeline.releaseTopAnchor()
								timeline.stickToBottom = true
							}
						}
					}
					TapHandler {
						enabled: !timeline.scopePresentationPending
						acceptedButtons: Qt.RightButton
						onTapped: point => {
							const row = timeline.itemAt(point.position.x + timeline.contentX,
								point.position.y + timeline.contentY)
							if (!row || row.stableId === undefined)
								root.openChatBackgroundMenu(
									timeline.mapToItem(null, point.position.x, point.position.y))
						}
                    }
                    headerPositioning: ListView.InlineHeader
					header: Column {
						id: timelineHeader
						property alias deferredRepeater: deferredHistoryRepeater
						property alias olderHistoryControl: olderHistoryControlRow
                        width: timeline.width
						height: implicitHeight
						spacing: 0
						visible: height > 0
						opacity: timeline.scopePresentationContentVisible ? 1 : 0
						Item {
							id: olderHistoryControlRow
							width: timelineHeader.width
						height: timeline.expectedHistoryLoaderHeight
							visible: height > 0
							ModernButton {
								anchors.horizontalCenter: parent.horizontalCenter
								anchors.top: parent.top
								anchors.topMargin: 4
								enabled: activeScope.canLoadOlder
									&& activeScope.loadingState !== "older"
								text: activeScope.loadingState === "older"
									  ? qsTr("Loading older messages…")
									  : qsTr("Load older messages")
								Accessible.name: text
								Accessible.description: qsTr(
									"Load messages sent before the currently visible history")
								onClicked: root.requestOlderMessagesPreservingAnchor()
							}
						}
						Repeater {
							id: deferredHistoryRepeater
							model: chatModel.deferredPresentationModel
							delegate: Rectangle {
								id: deferredHistoryRow
								required property string stableId
								required property string title
								required property string subtitle
								required property string timestamp
								required property int deferredPresentationHeight
								readonly property bool deferredPrefixItem: true
								readonly property bool performanceTimelineDelegate: true
								readonly property bool lightweightDeferredPresentation: true
								readonly property bool contentNeedsHydration: true
								// Do not bind the full source payload here. Older rows can
								// contain large inline-image or provider maps; copying those
								// into every lightweight Repeater delegate defeats the cold
								// first-paint path before the rich row is even visible.
								readonly property string summaryText: [title, subtitle, timestamp]
									.filter(function(part) {
										return String(part || "").length > 0
									}).join("  ·  ")
								readonly property string presentationSummary: summaryText
								readonly property bool messageRowReady: false
								readonly property bool paintedSummary: summaryText.length > 0
								readonly property bool paintedSurface: false
								readonly property bool paintedLoading: richPresentationReserved
								readonly property bool paintedTerminal: false
								readonly property bool richPresentationReserved:
									deferredPresentationHeight > 180
								property int deadlinePresentationFallbackGeneration: -1
								readonly property bool deadlinePresentationFallback:
									deadlinePresentationFallbackGeneration
										=== timeline.scopePresentationGeneration
								property bool accessibilityPooled: false
								function forceDeadlinePresentationFallback() {
									deadlinePresentationFallbackGeneration =
										timeline.scopePresentationGeneration
								}
								onStableIdChanged: deadlinePresentationFallbackGeneration = -1
								width: timelineHeader.width
								height: timeline.deferredMessageRowHeight
								radius: richPresentationReserved ? 0 : Theme.innerRadius
								color: richPresentationReserved ? Theme.withAlpha(
									Theme.surfaceRaised, 0.64) : Theme.surfaceRaised
								border.width: 1
								border.color: Theme.divider
								Accessible.role: Accessible.ListItem
								Accessible.name: summaryText
								Accessible.ignored: richPresentationReserved
									&& !deadlinePresentationFallback

								Label {
									visible: true
									anchors.fill: parent
									anchors.leftMargin: Theme.space3
									anchors.rightMargin: Theme.space3
									verticalAlignment: Text.AlignVCenter
									textFormat: Text.PlainText
									text: deferredHistoryRow.summaryText
									color: Theme.textMain
									font.pixelSize: Theme.chatBodyFontSize
									elide: Text.ElideRight
									maximumLineCount: 1
								}
							}
						}
                    }
					// Flickable margins live outside contentHeight, and ListView's
					// positionViewAtEnd() does not include bottomMargin. Keep the
					// visual breathing room inside the list so tail-following exposes
					// the complete final message above the composer.
					footerPositioning: ListView.InlineFooter
					footer: Item {
						width: timeline.width
						// The reserved jump-to-latest row already separates the tail
						// from the composer; only the remainder of the margin is needed.
						height: activeScope.activity ? Theme.space3
							: Math.max(0, root.timelineVerticalMargin - jumpToLatestRow.height)
					}
					delegate: DelegateChooser {
						role: "deferredPresentation"
						DelegateChoice {
							roleValue: true
							Rectangle {
								id: deferredInlineMessageDelegate
								required property string stableId
								required property string title
								required property string subtitle
								required property string timestamp
								required property int deferredPresentationHeight
								required property bool deferredPresentationCompact
								property bool accessibilityPooled: false
								readonly property bool performanceTimelineDelegate: true
								readonly property bool lightweightDeferredPresentation: true
								readonly property bool contentNeedsHydration: true
								readonly property bool messageRowReady: false
								readonly property bool paintedSummary:
									presentationSummary.length > 0
								readonly property bool paintedSurface: false
								readonly property bool paintedLoading: richPresentationReserved
								readonly property bool paintedTerminal: false
								readonly property bool richPresentationReserved:
									deferredPresentationHeight > 180
								property int deadlinePresentationFallbackGeneration: -1
								readonly property bool deadlinePresentationFallback:
									deadlinePresentationFallbackGeneration
										=== timeline.scopePresentationGeneration
								readonly property string presentationSummary: [title, subtitle, timestamp]
									.filter(function(part) {
										return String(part || "").trim().length > 0
									}).join("  ·  ")
								readonly property bool inVisibleViewport: !accessibilityPooled
									&& y + height >= timeline.contentY
									&& y <= timeline.contentY + timeline.height
								width: timeline.width
								height: deadlinePresentationFallback || deferredPresentationCompact
									? timeline.deferredMessageRowHeight
									: timeline.coldMessageRowHeight(deferredPresentationHeight)
								radius: 0
								color: "transparent"
								border.width: 0
								opacity: timeline.scopePresentationContentVisible ? 1 : 0
								Accessible.role: Accessible.ListItem
								Accessible.name: presentationSummary
								Accessible.ignored: richPresentationReserved
									&& !deadlinePresentationFallback
								function forceDeadlinePresentationFallback() {
									deadlinePresentationFallbackGeneration =
										timeline.scopePresentationGeneration
								}

								ListView.onPooled: accessibilityPooled = true
								ListView.onReused: {
									deadlinePresentationFallbackGeneration = -1
									accessibilityPooled = false
								}
								onStableIdChanged: deadlinePresentationFallbackGeneration = -1

								RowLayout {
									visible: true
									anchors.left: parent.left
									anchors.right: parent.right
									anchors.top: parent.top
									anchors.topMargin: Theme.space2
									height: Math.min(48, Math.max(1, parent.height
										- Theme.space2 * 2))
									anchors.leftMargin: Theme.space3
									anchors.rightMargin: Theme.space3
									spacing: Theme.space2

									Label {
										Layout.maximumWidth: Math.max(80,
											deferredInlineMessageDelegate.width * 0.24)
										textFormat: Text.PlainText
										text: deferredInlineMessageDelegate.title
										color: Theme.textStrong
										font.pixelSize: Theme.fontLabel
										font.weight: Font.DemiBold
										elide: Text.ElideRight
									}
									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: deferredInlineMessageDelegate.subtitle
										color: Theme.textMain
										font.pixelSize: Theme.chatBodyFontSize
										elide: Text.ElideRight
										maximumLineCount: 1
									}
									Label {
										visible: text.length > 0
										textFormat: Text.PlainText
										text: deferredInlineMessageDelegate.timestamp
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
									}
								}
							}
						}
						DelegateChoice {
							Loader {
								id: asyncMessageDelegate
								Connections {
									target: asyncMessageDelegate
									function onSourceChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPaintedSummaryChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPaintedSurfaceChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPaintedLoadingChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPaintedTerminalChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onContentNeedsHydrationChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onRichResourcesActiveChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onRichResourcesRequiredChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onRichResourceActivationPendingChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onMessageRowReadyChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPreviewPresentationReadyChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onPresentationHeightReleasedChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onHeightChanged() { timeline.scopePresentationMilestonesDirty = true }
									function onAccessibilityPooledChanged() { timeline.scopePresentationMilestonesDirty = true }
								}
								required property string stableId
								required property bool deferredPresentation
								required property bool own
								required property string title
								required property string subtitle
								required property string status
								required property string avatarUrl
								required property string timestamp
								required property int deferredPresentationHeight
								required property string replyActor
								required property string replySnippet
								required property var reactions
								required property var bodySegments
								required property bool bodyHydrationPending
								required property var preview
								required property var attachments
								required property var source
								required property bool modelStartsGroup
								required property bool dateBoundary
								required property bool deleted
								required property bool canReply
								required property bool canReact
								required property bool canDelete
								property bool accessibilityPooled: false
								property int deadlinePresentationFallbackGeneration: -1
								property string deadlinePresentationFallbackStableId: ""
								property string deadlinePresentationFallbackPreviewIdentity: ""
								property bool deadlinePresentationRecoveryActive: false
								readonly property string deadlinePresentationSourceIdentity:
									root.richPreviewPresentationIdentity(stableId, preview)
								readonly property bool deadlinePresentationFallback:
									deadlinePresentationFallbackGeneration
										=== timeline.scopePresentationGeneration
									&& deadlinePresentationFallbackStableId === stableId
								property bool resumeRequested: false
								property bool resumeDuringFinalization: false
								property bool resumeAdmitsPreview: false
								property string resumeOwnedStableId: ""
								property string deferredMaterializationCleanupId: ""
								property int deferredMaterializationCleanupGeneration: -1
								readonly property bool plainDeferredSourceOwned:
									!activeScope.activity
									&& timeline.plainDeferredMaterializationOwns(
										deferredMaterializationCleanupId,
										deferredMaterializationCleanupGeneration)
								function captureDeferredMaterializationOwnership() {
									deferredMaterializationCleanupId = String(stableId || "")
									deferredMaterializationCleanupGeneration =
										timeline.deferredMaterializationGenerationFor(
											deferredMaterializationCleanupId)
								}
								function releaseDeferredMaterialization(stableIdOverride,
										generationOverride) {
									if (typeof timeline === "undefined" || !timeline)
										return
									const key = String(stableIdOverride || stableId || "")
									const generation = generationOverride === undefined
										? deferredMaterializationCleanupGeneration
										: Number(generationOverride)
									if (key.length > 0)
										timeline.completeDeferredMaterialization(key, generation)
								}
								function forceDeadlinePresentationFallback() {
									if (deadlinePresentationFallback)
										return
									deadlinePresentationFallbackGeneration =
										timeline.scopePresentationGeneration
									deadlinePresentationFallbackStableId = String(stableId || "")
									deadlinePresentationFallbackPreviewIdentity =
										deadlinePresentationSourceIdentity
									deadlinePresentationRecoveryActive = false
									stopPresentationHeightObservation()
									resetOuterLoaderIncubationWatchdog(true)
									releaseDeferredMaterialization()
								}
								function clearDeadlinePresentationFallback() {
									deadlinePresentationFallbackGeneration = -1
									deadlinePresentationFallbackStableId = ""
									deadlinePresentationFallbackPreviewIdentity = ""
									deadlinePresentationRecoveryActive = false
								}
								function beginDeadlinePresentationFallbackRecovery() {
									if (!deadlinePresentationFallback)
										return true
									// The bounded real-row summary owns the deadline reveal. Re-enter
									// the normal one-row recovery queue only after that reveal has
									// committed and while no user gesture owns timeline geometry.
									if (accessibilityPooled || timeline.scopePresentationPending
											|| timeline.scopePresentationFinalizing
											|| timeline.richScrollInProgress)
										return false
									deadlinePresentationFallbackPreviewIdentity =
										deadlinePresentationSourceIdentity
									deadlinePresentationRecoveryActive = true
									timeline.scheduleViewportMembershipUpdate()
									return true
								}
								onDeadlinePresentationSourceIdentityChanged: {
									if (!deadlinePresentationFallback)
										return
									deadlinePresentationRecoveryActive = false
									stopPresentationHeightObservation()
									resetOuterLoaderIncubationWatchdog(true)
									if (!timeline.scopePresentationPending)
										timeline.scheduleRichContentResume()
								}
								property bool outerLoaderReady: false
								property bool outerLoaderIncubationSuppressed: false
								property double outerLoaderIncubationWatchdogStartedAt: 0
								// A released rich row owns a committed height for its stable
								// presentation identity. Pooling may remove the nested item, but it
								// must not put the old estimate back into ListView geometry.
								property string presentationHeightStableId: ""
								property string presentationHeightPreviewIdentity: ""
								property string presentationHeightSettledIdentity: ""
								property real presentationHeightSettledHeight: 0
								property real retainedPresentationHeight: 0
								property int retainedPresentationGeneration: -1
								property real presentationHeightObservedHeight: 0
								property int presentationHeightQuietPolishObservations: 0
								property bool presentationHeightReleasePending: false
								property bool presentationHeightReleased: false
								property bool presentationHeightTailIntent: false
								function clearPresentationHeightReleaseState() {
									// Identity/reuse state is local to this delegate. Never cancel
									// the timeline-owned settle for another visible row here.
									presentationHeightSettledIdentity = ""
									presentationHeightSettledHeight = 0
									presentationHeightObservedHeight = 0
									presentationHeightQuietPolishObservations = 0
									presentationHeightReleasePending = false
									presentationHeightReleased = false
									presentationHeightTailIntent = false
									if (typeof presentationHeightReleaseTimer !== "undefined")
										presentationHeightReleaseTimer.stop()
								}
								function resetPresentationHeightIdentity() {
									clearPresentationHeightReleaseState()
									retainedPresentationHeight = 0
									retainedPresentationGeneration = -1
									presentationHeightStableId = String(stableId || "")
									presentationHeightPreviewIdentity = ""
								}
								function presentationHeightIdentityFor(stable, preview) {
									const stableKey = String(stable || "")
									const previewKey = String(preview || "")
									return stableKey.length > 0 && previewKey.length > 0
										? stableKey + "|" + previewKey : ""
								}
								function currentPresentationHeightIdentity() {
									const currentStableId = String(stableId || "")
									if (currentStableId.length === 0)
										return ""
									const currentPreviewIdentity = item
										? String(item.previewLoaderIncubationIdentity || "")
										: (presentationHeightStableId === currentStableId
											? presentationHeightPreviewIdentity : "")
									return presentationHeightIdentityFor(currentStableId,
										currentPreviewIdentity)
								}
								function reevaluatePresentationHeightIdentity() {
									const currentStableId = String(stableId || "")
									if (currentStableId !== presentationHeightStableId) {
										resetPresentationHeightIdentity()
										return true
									}
									if (!item)
										return false
									const currentPreviewIdentity =
										String(item.previewLoaderIncubationIdentity || "")
									const identityChanged =
										presentationHeightPreviewIdentity !== currentPreviewIdentity
									const hadIdentity =
										presentationHeightPreviewIdentity.length > 0
										|| currentPreviewIdentity.length > 0
									if (identityChanged && hadIdentity)
										clearPresentationHeightReleaseState()
									presentationHeightPreviewIdentity = currentPreviewIdentity
									return identityChanged && hadIdentity
								}
								function stopPresentationHeightObservation() {
									if (typeof presentationHeightReleaseTimer !== "undefined")
										presentationHeightReleaseTimer.stop()
									presentationHeightReleasePending = false
									presentationHeightObservedHeight = 0
									presentationHeightQuietPolishObservations = 0
								}
								function schedulePresentationHeightObservation() {
									if (!presentationHeightQuietObservationEligible
										|| presentationHeightReleaseTimer.running)
									return
									presentationHeightReleasePending = true
									presentationHeightReleaseTimer.restart()
								}
				function preparePresentationHeightCommit() {
					// The existing tail path owns bottom correction. It must not
					// acquire a viewport anchor merely because a card settled.
					presentationHeightTailIntent = false
					if (timeline.stickToBottom) {
						presentationHeightTailIntent = true
						return true
					}
					if (timeline.prependSettlementHeightCommitOwned)
						return true
					if (timeline.restoringViewportAnchor
						|| timeline.viewportAnchorReleasePending
						|| timeline.topAnchorActive
						|| timeline.restoringTopAnchor)
						return false
					// Reuse an existing stable anchor. Capturing again here would overwrite
					// the selected message/offset and can leave the latch waiting forever.
					if (timeline.viewportAnchorActive)
						return true
					return timeline.captureViewportAnchor()
				}
								function presentationHeightCommitValue(measuredHeight) {
									return Math.max(timeline.deferredMessageRowHeight, measuredHeight)
								}
				function commitPresentationHeight(identity, measuredHeight) {
					const currentIdentity = currentPresentationHeightIdentity()
					if (identity.length === 0 || identity !== currentIdentity
						|| !isFinite(measuredHeight) || measuredHeight <= 0)
						return false
					if (!preparePresentationHeightCommit())
						return false
					if (presentationHeightTailIntent) {
						// An exact-tail commit supersedes any stale viewport anchor.
						// Releasing it does not write contentY; the existing bottom-follow
						// path owns the correction after the new height is assigned.
						if (timeline.viewportAnchorActive)
							timeline.releaseViewportAnchor()
						timeline.stickToBottom = true
					}
					presentationHeightSettledIdentity = identity
					const commitsDeadlineRecovery = deadlinePresentationFallback
						&& deadlinePresentationRecoveryActive
						&& deadlinePresentationFallbackPreviewIdentity
							=== deadlinePresentationSourceIdentity
					const committedOuterHeight =
						presentationHeightCommitValue(measuredHeight)
					const committedProbeHeight = Math.max(
						timeline.deferredMessageRowHeight, committedOuterHeight)
					presentationHeightSettledHeight = committedOuterHeight
					retainedPresentationHeight = committedOuterHeight
					retainedPresentationGeneration = timeline.scopePresentationGeneration
					presentationHeightReleased = true
					presentationHeightReleasePending = false
					timeline.noteScopePresentationHeightCommit()
					presentationHeightObservedHeight = measuredHeight
									presentationHeightQuietPolishObservations = 0
									viewportProbeStableId = String(stableId || "")
					viewportProbeHeight = committedProbeHeight
					if (commitsDeadlineRecovery)
						clearDeadlinePresentationFallback()
					// Clear the pin in the same measured outer+probe commit while the
					// non-tail anchor or exact-tail intent still owns correction. Only
					// the existing restore/bottom-follow path runs after this cohort.
					timeline.clearStationaryThumbWarmPinForCommit(stableId,
						committedOuterHeight, committedProbeHeight)
					if (presentationHeightTailIntent)
						timeline.requestBottomFollow()
					else if (timeline.viewportAnchorActive)
						timeline.scheduleViewportAnchorQuietRelease()
						timeline.scheduleViewportMembershipUpdate()
					root.update()
					if (item)
						item.completeRichLoaderResumeWhenReady()
					return true
								}
								function observePresentationHeight() {
									presentationHeightReleasePending = false
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									if (!presentationHeightQuietObservationEligible) {
										stopPresentationHeightObservation()
										return
									}
									const identity = currentPresentationHeightIdentity()
									const measuredHeight = presentationHeightMeasuredHeight
									if (!isFinite(measuredHeight) || measuredHeight <= 0) {
										stopPresentationHeightObservation()
										return
									}
									if (presentationHeightObservedHeight > 0
										&& Math.abs(presentationHeightObservedHeight
											- measuredHeight) <= 0.5)
										++presentationHeightQuietPolishObservations
									else {
										presentationHeightObservedHeight = measuredHeight
										presentationHeightQuietPolishObservations = 1
									}
									if (presentationHeightQuietPolishObservations < 2) {
										schedulePresentationHeightObservation()
										return
									}
									const committedHeight =
										presentationHeightCommitValue(measuredHeight)
									if (presentationHeightReleased
										&& presentationHeightSettledIdentity === identity
										&& Math.abs(presentationHeightSettledHeight - committedHeight)
											<= 0.5
										&& !stationaryThumbWarmPinned) {
										presentationHeightQuietPolishObservations = 0
										return
									}
									if (!commitPresentationHeight(identity, measuredHeight)) {
										presentationHeightReleasePending = true
										presentationHeightReleaseTimer.restart()
									}
								}
								readonly property bool performanceTimelineDelegate: true
								readonly property bool lightweightDeferredPresentation: false
								readonly property string presentationSummary:
									[title, subtitle, timestamp]
										.filter(function(part) {
											return String(part || "").trim().length > 0
										}).join("  ·  ")
								readonly property bool previewOnlyAtomicHandoff:
									root.messageBodyIsPreviewSourceOnly(bodySegments || [], preview || ({}))
									&& root.normalizedModelList(attachments).length === 0
									&& String(replyActor || "").length === 0
									&& String(replySnippet || "").length === 0
								// The retained still cache already paid the decode cost while this
								// row was near the viewport. Reuse only that completed, safe local
								// source during the scroll handoff; never start provider/network or
								// animated/player work from this lightweight outer delegate.
								readonly property string cachedStaticHandoffSource:
									!accessibilityPooled && cachedStaticHandoffEligible
										? timeline.staticPreviewSourceFor(preview || ({})) : ""
								readonly property bool cachedStaticHandoffEligible:
									root.normalizedModelList(attachments).length === 0
									&& String(replyActor || "").length === 0
									&& String(replySnippet || "").length === 0
									&& !bodyHydrationPending && !deleted && !activeScope.activity
									&& !root.messageContainsInlineImage(bodySegments)
								readonly property bool cachedStaticHandoffReady:
									cachedStaticHandoffSource.length > 0
									&& timeline.staticPreviewPreheatCompletedSources[
										cachedStaticHandoffSource] === true
								readonly property bool cachedStaticHandoffCompact:
									timeline.staticPreviewHandoffUsesCompactHeader(preview || ({}))
								readonly property bool cachedStaticHandoffSystemMessage:
									root.isSystemChatMessage(source, avatarUrl, canReply, canReact, own)
								readonly property string cachedStaticHandoffProvider: String(preview
									? (preview.host || preview.providerName || preview.provider
										|| preview.embedKind || "") : "").trim()
								readonly property string cachedStaticHandoffTitle: String(preview
									? (preview.title || preview.loadingLabel
										|| cachedStaticHandoffProvider || "") : "").trim()
								readonly property string cachedStaticHandoffDescription: String(preview
									? (preview.description || preview.subtitle || "") : "").trim()
								readonly property real cachedStaticHandoffLaneAvailableWidth: Math.max(1,
									timeline.width - timeline.leftMargin - timeline.rightMargin - Theme.space4)
								readonly property real cachedStaticHandoffLaneWidth: Math.max(1, Math.min(
									root.conversationLaneMaximumWidth, cachedStaticHandoffLaneAvailableWidth))
								readonly property real cachedStaticHandoffRichChromeWidth:
									cachedStaticHandoffSystemMessage
										? Theme.chatBubbleHorizontalPadding * 2
										: Theme.avatarMedium + Theme.space2
											+ Theme.chatBubbleHorizontalPadding * 2
								readonly property string cachedStaticHandoffAspect:
									timeline.staticPreviewHandoffAspect(preview || ({}))
								readonly property string cachedStaticHandoffSize:
									timeline.staticPreviewHandoffSize(preview || ({}),
										root.richPreviewSizePreset(stableId))
								readonly property bool cachedStaticHandoffFullBleed:
									cachedStaticHandoffEmbedGeometry
									&& cachedStaticHandoffAspect !== "short"
									&& cachedStaticHandoffAspect !== "square"
								readonly property bool cachedStaticHandoffEmbedGeometry: {
									return timeline.staticPreviewHandoffEmbedGeometry(preview || ({}))
								}
								readonly property var cachedStaticHandoffMediaItem:
									timeline.staticPreviewHandoffMediaItem(preview || ({}))
								readonly property bool cachedStaticHandoffTypedGallery: {
									const value = preview || ({})
									const document = value.document || ({})
									return Number(document.schemaVersion || 0) === 1
										&& document.commonPresentation === true
										&& document.media && document.media.length !== undefined
										&& document.media.length > 0
								}
								readonly property string cachedStaticHandoffItemKind:
									timeline.staticPreviewHandoffMediaKind(cachedStaticHandoffMediaItem)
								readonly property string cachedStaticHandoffMediaKind:
									cachedStaticHandoffItemKind
									|| timeline.staticPreviewHandoffMediaKind({
										"kind": preview ? (preview.mediaKind || preview.kind || "") : "",
										"mime": preview ? (preview.mediaMime || "") : ""
									})
								readonly property string cachedStaticHandoffMediaMime: String(
									cachedStaticHandoffMediaItem.mime
										|| cachedStaticHandoffMediaItem.mediaMime
										|| (preview ? (preview.mediaMime || "") : "")).toLowerCase()
								readonly property bool cachedStaticHandoffMediaExpanded: {
									if (cachedStaticHandoffItemKind.length > 0)
										return timeline.staticPreviewHandoffMediaExpanded(
											cachedStaticHandoffMediaItem)
									return timeline.staticPreviewHandoffMediaExpanded({
										"kind": preview ? (preview.mediaKind || preview.kind || "") : "",
										"mime": preview ? (preview.mediaMime || "") : "",
										"url": preview ? (preview.mediaUrl || "") : "",
										"externalUrl": preview ? (preview.mediaExternalUrl || "") : "",
										"directPlayable": preview ? preview.directPlayable : undefined
									})
								}
								readonly property bool cachedStaticHandoffDirectAudio:
									cachedStaticHandoffMediaExpanded
									&& (cachedStaticHandoffMediaKind === "audio"
										|| cachedStaticHandoffMediaMime.indexOf("audio/") === 0)
								readonly property bool cachedStaticHandoffDirectVideo:
									cachedStaticHandoffMediaExpanded
									&& (cachedStaticHandoffMediaKind === "video"
										|| cachedStaticHandoffMediaMime.indexOf("video/") === 0)
								readonly property real cachedStaticHandoffTargetWidth:
									cachedStaticHandoffAspect === "short"
										? (cachedStaticHandoffSize === "large" ? 420
											: cachedStaticHandoffSize === "compact" ? 320 : 360)
									: cachedStaticHandoffAspect === "square"
										? (cachedStaticHandoffSize === "large" ? 520
											: cachedStaticHandoffSize === "compact" ? 340 : 460)
									: cachedStaticHandoffAspect === "twitch"
										? (cachedStaticHandoffSize === "large" ? 620
											: cachedStaticHandoffSize === "compact" ? 420 : 520)
									: cachedStaticHandoffSize === "compact"
										? Math.round(Math.max(300, Math.min(400, root.width * 0.48)))
									: cachedStaticHandoffSize === "large"
										? Math.round(Math.max(400, Math.min(720, root.width * 0.74)))
										: Math.round(Math.max(340, Math.min(480, root.width * 0.62)))
								readonly property real cachedStaticHandoffCardWidth: Math.min(
									cachedStaticHandoffTargetWidth, Math.max(220,
										cachedStaticHandoffLaneAvailableWidth
											- cachedStaticHandoffRichChromeWidth))
								readonly property real cachedStaticHandoffMessageWidth:
									cachedStaticHandoffSystemMessage
										? Math.min(cachedStaticHandoffLaneWidth, 680)
										: Math.min(cachedStaticHandoffLaneWidth, Math.max(own ? 176 : 220,
											cachedStaticHandoffCardWidth + cachedStaticHandoffRichChromeWidth))
								readonly property real cachedStaticHandoffLaneStart: Math.max(0,
									(cachedStaticHandoffLaneAvailableWidth - cachedStaticHandoffLaneWidth) / 2)
								readonly property real cachedStaticHandoffMessageX:
									cachedStaticHandoffSystemMessage
										? cachedStaticHandoffLaneStart + (cachedStaticHandoffLaneWidth
											- cachedStaticHandoffMessageWidth) / 2
									: cachedStaticHandoffLaneStart
								readonly property real cachedStaticHandoffCardX:
									cachedStaticHandoffMessageX + (cachedStaticHandoffSystemMessage
										? Theme.chatBubbleHorizontalPadding
										: Theme.avatarMedium + Theme.space2
											+ Theme.chatBubbleHorizontalPadding)
								readonly property int cachedStaticHandoffBodyLineCount: {
									if (previewOnlyAtomicHandoff)
										return 0
									const parts = root.normalizedModelList(bodySegments)
									let body = ""
									for (let index = 0; index < parts.length; ++index)
										body += String(parts[index].text || "")
									const explicitLines = body.length > 0 ? body.split(/\r?\n/).length : 1
									return Math.max(1, Math.min(4,
										Math.max(explicitLines, Math.ceil(body.length / 88))))
								}
								readonly property real cachedStaticHandoffTopInset:
									(dateBoundary ? Theme.space5 + Theme.fontCaption : 0)
									+ (modelStartsGroup ? Theme.space3 : Theme.chatMetadataSpacing)
									+ (modelStartsGroup || cachedStaticHandoffSystemMessage
										? Theme.fontCaption + Theme.chatMetadataSpacing : 0)
									+ cachedStaticHandoffBodyLineCount
										* Math.ceil(Theme.chatBodyFontSize * Theme.chatBodyLineHeight)
									+ (cachedStaticHandoffBodyLineCount > 0 ? Theme.chatContentSpacing : 0)
								readonly property real cachedStaticHandoffActionWidth: Math.max(1,
									cachedStaticHandoffCardWidth - Theme.space4 * 2)
								readonly property real cachedStaticHandoffEmbedMediaWidth:
									cachedStaticHandoffAspect === "short"
										? Math.min(cachedStaticHandoffActionWidth,
											cachedStaticHandoffSize === "large" ? 340
												: cachedStaticHandoffSize === "compact" ? 240 : 280)
									: cachedStaticHandoffAspect === "square"
										? cachedStaticHandoffActionWidth
										: cachedStaticHandoffFullBleed
											? cachedStaticHandoffCardWidth : cachedStaticHandoffActionWidth
								readonly property real cachedStaticHandoffEmbedMediaHeight:
									cachedStaticHandoffAspect === "short"
										? cachedStaticHandoffEmbedMediaWidth * 16 / 9
									: cachedStaticHandoffAspect === "square"
										? cachedStaticHandoffEmbedMediaWidth
									: cachedStaticHandoffAspect === "twitch"
										? cachedStaticHandoffEmbedMediaWidth * 3 / 4
									: cachedStaticHandoffAspect === "compact-audio"
										? Math.min(166, Math.max(128,
											cachedStaticHandoffEmbedMediaWidth * 0.29))
									: cachedStaticHandoffAspect === "audio"
										? Math.min(352, Math.max(220,
											cachedStaticHandoffEmbedMediaWidth * 0.61))
										: cachedStaticHandoffEmbedMediaWidth * 9 / 16
								readonly property real cachedStaticHandoffMediaHeight:
									cachedStaticHandoffTypedGallery && !cachedStaticHandoffEmbedGeometry
										? (cachedStaticHandoffSize === "large"
											? Math.min(420, Math.max(240,
												cachedStaticHandoffActionWidth * 9 / 16))
											: Math.min(320, Math.max(180,
												cachedStaticHandoffActionWidth * 9 / 16)))
									: cachedStaticHandoffEmbedGeometry
										? cachedStaticHandoffEmbedMediaHeight
									: cachedStaticHandoffDirectAudio
										? Math.min(166, Math.max(128,
											cachedStaticHandoffActionWidth * 0.29))
									: cachedStaticHandoffDirectVideo
										? cachedStaticHandoffCardWidth * 9 / 16
										: cachedStaticHandoffEmbedMediaHeight
								readonly property real cachedStaticHandoffCardHeight:
									cachedStaticHandoffCompact ? 176 : cachedStaticHandoffMediaHeight + 132
								readonly property real cachedStaticHandoffY: Math.min(Math.max(0,
									cachedStaticHandoffTopInset), Math.max(0, height - 1))
								readonly property real cachedStaticHandoffVisibleHeight: Math.min(
									cachedStaticHandoffCardHeight, Math.max(1,
										height - cachedStaticHandoffY))
								// Viewport membership controls provider activation. Before the
								// release latch commits a real height, use the compact cold row.
								// Afterward, the probe is the same cached
								// committed height as the outer delegate, including while the
								// nested Loader is temporarily absent.
								property string viewportProbeStableId: ""
								property real viewportProbeHeight:
									timeline.coldMessageRowHeight(deferredPresentationHeight)
								readonly property var stationaryThumbWarmPin:
									timeline.stationaryThumbWarmPin(stableId)
								readonly property bool stationaryThumbWarmPinned:
									timeline.stationaryThumbWarmPinMatchesCurrentScope(
										stationaryThumbWarmPin, stableId)
								readonly property real stationaryThumbPinnedOuterHeight:
									stationaryThumbWarmPinned
										? Number(stationaryThumbWarmPin.outerHeight || 0) : 0
								readonly property real stationaryThumbPinnedProbeHeight:
									stationaryThumbWarmPinned
										? Number(stationaryThumbWarmPin.probeHeight || 0) : 0
								readonly property real effectiveViewportProbeHeight:
									stationaryThumbWarmPinned
										? stationaryThumbPinnedProbeHeight : viewportProbeHeight
								readonly property bool stationaryThumbWarmOwned:
									stationaryThumbWarmPinned
									&& (stationaryThumbWarmGate.owns(stableId,
											Number(stationaryThumbWarmPin.pressEpoch),
											Number(stationaryThumbWarmPin.dwellGeneration))
										|| stationaryThumbWarmGate.retains(stableId,
											Number(stationaryThumbWarmPin.pressEpoch),
											Number(stationaryThumbWarmPin.dwellGeneration)))
								property string stationaryThumbWarmObservationStableId: ""
								property string stationaryThumbWarmObservationScopeToken: ""
								property int stationaryThumbWarmObservationScopeGeneration: -1
								property int stationaryThumbWarmObservationPressEpoch: 0
								property int stationaryThumbWarmObservationDwellGeneration: 0
								property int stationaryThumbWarmObservationPinSerial: 0
								property string stationaryThumbDelegateIdentityStableId: ""
								readonly property bool stationaryThumbWarmEligible: {
									const value = preview || ({})
									const state = String(value.state || "").toLowerCase()
									const statusValue = String(value.status || "").toLowerCase()
									return active && !deferredPresentation && inVisibleViewport
										&& Object.keys(value).length > 0
										&& state !== "error" && state !== "failed"
										&& statusValue !== "error" && statusValue !== "failed"
										&& value.failed !== true && !previewLoaderFailed
								}
						readonly property bool paintedSummary: !accessibilityPooled
							&& (handoffSummaryPainted || deadlinePresentationFallback
								|| (active && (!item || item.paintedSummary === true)))
						readonly property bool handoffSummaryPainted: !accessibilityPooled
							&& asyncRichHandoff.visible && asyncRichHandoffSummary.visible
						readonly property bool duplicateHandoffSummaryPainted:
							handoffSummaryPainted && !!item && item.paintedSummary === true
								readonly property bool paintedSurface: !!item
									&& item.paintedSurface === true
								readonly property bool paintedLoading: !!item
									&& item.paintedLoading === true
								readonly property bool paintedTerminal: !!item
									&& item.paintedTerminal === true
								property bool viewportMembershipUpdating: false
								readonly property real presentationHeightCommittedHeight:
									presentationHeightReleased
									&& presentationHeightSettledIdentity.length > 0
									&& presentationHeightSettledIdentity
										=== currentPresentationHeightIdentity()
									&& isFinite(Number(presentationHeightSettledHeight))
									&& Number(presentationHeightSettledHeight) > 0
									? Math.max(timeline.deferredMessageRowHeight,
										Number(presentationHeightSettledHeight)) : 0
								function viewportProbeBaseHeight() {
									if (presentationHeightCommittedHeight > 0)
										return presentationHeightCommittedHeight
									return retainedPresentationGeneration === timeline.scopePresentationGeneration
										&& presentationHeightStableId === String(stableId || "")
										&& retainedPresentationHeight > 0
										? retainedPresentationHeight
										: timeline.coldMessageRowHeight(deferredPresentationHeight)
								}
								function resetViewportProbeHeight() {
									viewportProbeStableId = String(stableId || "")
									viewportProbeHeight = viewportProbeBaseHeight()
								}
								function updateViewportProbeHeight() {
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									const committedHeight = presentationHeightCommittedHeight
									if (committedHeight > 0) {
										if (Math.abs(viewportProbeHeight - committedHeight) > 0.5)
											viewportProbeHeight = committedHeight
										return
									}
									if (accessibilityPooled || richGeometrySettling || !item)
										return
									const settledHeight = viewportProbeBaseHeight()
									if (Math.abs(settledHeight - viewportProbeHeight) > 0.5)
										viewportProbeHeight = settledHeight
								}
								function updateViewportMembership() {
									const rowTop = Number(y || 0)
									const extent = Math.max(0,
										Number(effectiveViewportProbeHeight || 0))
									const viewportTop = Number(timeline.contentY || 0)
									const viewportBottom = viewportTop
										+ Number(timeline.height || 0)
									const warmDistance = timeline.baseCacheBuffer
									const nextVisible = !accessibilityPooled
										&& rowTop <= viewportBottom
										&& rowTop + extent >= viewportTop
									const nextHydration = !accessibilityPooled
										&& rowTop <= viewportBottom + warmDistance
										&& rowTop + extent >= viewportTop - warmDistance
									const visibleChanged =
										inVisibleViewport !== nextVisible
									const hydrationChanged =
										inHydrationWindow !== nextHydration
									if (!visibleChanged && !hydrationChanged)
										return
									// Publish the pair from one geometry snapshot. Suppress
									// nested provider handlers until neither value can observe
									// the other value from the previous viewport.
									viewportMembershipUpdating = true
									inHydrationWindow = nextHydration
									inVisibleViewport = nextVisible
									viewportMembershipUpdating = false
									reconcileViewportMembership()
									if (item && item.reconcileViewportMembership)
										item.reconcileViewportMembership()
								}
								function scheduleViewportProbeHeightUpdate() {
									viewportProbeUpdateTimer.restart()
								}
								function scheduleViewportProbeReset() {
									const identity = String(stableId || "")
									Qt.callLater(function() {
										if (String(asyncMessageDelegate.stableId || "")
												!== identity)
											return
										if (asyncMessageDelegate.viewportProbeStableId
												!== identity)
											asyncMessageDelegate.resetViewportProbeHeight()
										asyncMessageDelegate.scheduleViewportProbeHeightUpdate()
									})
								}
								onDeferredPresentationHeightChanged: {
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									else if (!presentationHeightReleased)
										viewportProbeHeight = Math.max(
											viewportProbeHeight,
											viewportProbeBaseHeight())
									schedulePresentationHeightObservation()
								}
								onViewportProbeHeightChanged:
									timeline.scheduleViewportMembershipUpdate()
								onEffectiveViewportProbeHeightChanged:
									timeline.scheduleViewportMembershipUpdate()
								onYChanged: timeline.scheduleViewportMembershipUpdate()
								readonly property bool contentNeedsHydration: item
									? item.contentNeedsHydration : true
								readonly property bool previewNeedsHydration: item
									? item.previewNeedsHydration : true
								readonly property bool richLoaderSuspended: item
									? item.richLoaderSuspended : true
								readonly property bool richResourcesActive: item
									? item.richResourcesActive : false
								readonly property bool richResourceActivationQueued: item
									? item.richResourceActivationQueued : false
								readonly property bool richResourceActivationPending: item
									? item.richResourceActivationPending : false
								readonly property bool previewMaterializationAdmitted: item
									? item.previewMaterializationAdmitted : false
								readonly property bool nativeThumbnailReady: item ? item.nativeThumbnailReady : false
								readonly property bool previewLoaderIncubationSuppressed: item
									? outerLoaderIncubationSuppressed
										|| item.previewLoaderIncubationSuppressed
									: outerLoaderIncubationSuppressed
								readonly property bool previewLoaderFailed: item
									? item.previewLoaderFailed : false
								readonly property bool nearViewportPrefetchTerminal: item
									? item.nearViewportPrefetchTerminal : false
								readonly property string previewLoaderSurfaceState: item
									? item.previewLoaderSurfaceState : "blank"
								readonly property bool previewLoaderIncubationWatchdogRunning:
									outerLoaderIncubationWatchdog.running
									|| (item ? item.previewLoaderIncubationWatchdogRunning : false)
								readonly property double previewLoaderIncubationWatchdogStartedAt:
									outerLoaderIncubationWatchdog.running
										? outerLoaderIncubationWatchdogStartedAt
										: item ? item.previewLoaderIncubationWatchdogStartedAt : 0
								readonly property int previewLoaderStatus: item
									? item.previewLoaderStatus : Loader.Null
								readonly property bool messageRowReady: item
									? item.messageRowReady : false
								readonly property bool messageRowMaterialized: item
									? item.messageRowMaterialized : false
								readonly property bool messageContentLoaderReady: item
									? item.messageContentLoaderReady : false
								readonly property bool previewPresentationReady: item
									? item.previewPresentationReady : false
								readonly property bool previewPrewarmVisualReady: item
									? item.previewPrewarmVisualReady : false
								readonly property bool staticPreviewPresentationReady: item
									? item.staticPreviewPresentationReady : false
								readonly property int previewPrewarmProgressRevision: item
									? Number(item.previewPrewarmProgressRevision || 0) : 0
								readonly property bool richResourcesRequired: item
									? item.richResourcesRequired : false
								readonly property bool plainScopeResumeEligible: item
									? item.plainScopeResumeEligible && !item.messageRowFailed : !activeScope.activity
										&& !deferredPresentation && !bodyHydrationPending
										&& (!preview || Object.keys(preview).length === 0)
										&& (!attachments || attachments.length === 0)
										&& !root.messageContainsInlineImage(bodySegments)
								property bool inVisibleViewport: false
								property bool inHydrationWindow: false
								readonly property bool richContentViewportEligible:
									inVisibleViewport || stableId === timeline.nearViewportWarmId
								function reconcileViewportMembership() {
									if (richContentViewportEligible || item || plainDeferredSourceOwned
											|| timeline.cachedPlainPresentationEligible(asyncMessageDelegate))
										return
									const ownedStableId = resumeOwnedStableId
									resumeRequested = false
									resumeDuringFinalization = false
									resumeAdmitsPreview = false
									resumeOwnedStableId = ""
									resetOuterLoaderIncubationWatchdog(true)
									if (ownedStableId.length > 0)
										timeline.cancelRichLoaderResume(ownedStableId)
								}
								onRichContentViewportEligibleChanged: {
									if (viewportMembershipUpdating)
										return
									reconcileViewportMembership()
								}
								width: timeline.width
								readonly property bool richGeometrySettling: item
									&& item.richResourcesRequired
									&& (!item.messageRowReady
										|| item.richResourceActivationPending
										|| (!item.richResourcesActive
											&& !item.boundedRichGeometryReady))
								readonly property real presentationHeightMeasuredHeight: item
									? Number(item.implicitHeight || 0) : 0
								readonly property bool presentationHeightFinalizationMovementOwned:
									timeline.scopePresentationPending
									&& timeline.scopePresentationFinalizing
								readonly property bool presentationHeightQuietObservationEligible:
									!accessibilityPooled && !!item
									&& currentPresentationHeightIdentity().length > 0
									&& messageRowReady
									// A loading summary is not the card's measured height. Keep
									// the reservation until hydration resolves or fails; otherwise
									// portrait cards collapse to a link row and then grow again.
									&& !contentNeedsHydration
									// Hidden finalization may latch its bounded fallback surface. Once
									// presented, wait for provider activation to settle so a temporary
									// rich height cannot rewrite the steady-state ListView coordinate map.
									&& (!richGeometrySettling || timeline.scopePresentationFinalizing)
									&& (!richResourceActivationPending
										|| timeline.scopePresentationFinalizing)
									&& !previewLoaderIncubationWatchdogRunning
									&& isFinite(presentationHeightMeasuredHeight)
									&& presentationHeightMeasuredHeight > 0
									&& (!timeline.richScrollInProgress
										|| presentationHeightFinalizationMovementOwned)
									&& ((!timeline.moving && !timeline.dragging && !timeline.flicking)
										|| presentationHeightFinalizationMovementOwned)
									&& !timeline.scrollbarDragActive
									&& !timeline.scrollbarReleasePending
									&& !timelineScrollBar.pressed
									&& (!timeline.prependAnchorActive
										|| timeline.prependSettlementHeightCommitOwned)
									&& (!timeline.prependMutationInProgress
										|| timeline.prependSettlementHeightCommitOwned)
									&& !chatModel.prependPending
									&& !timeline.restoringPrependAnchor
									&& (!timeline.deferredPrependAnchorActive
										|| timeline.prependSettlementHeightCommitOwned)
									&& (!timeline.restoringBottom
										|| presentationHeightFinalizationMovementOwned)
									&& !timeline.explicitWheelInputPending
									&& !timeline.explicitNavigationMovement
									&& !timelineScrollHandler.scrolling
					&& !timeline.topAnchorActive
					&& !timeline.restoringTopAnchor
					&& !timeline.viewportAnchorQuietReleaseActive
					&& !timeline.viewportAnchorReleasePending
					&& !timeline.restoringViewportAnchor
									&& (!bottomFollowTimer.running
										|| presentationHeightFinalizationMovementOwned)
									&& !root.performanceChatScrollRunning
								onPresentationHeightQuietObservationEligibleChanged: {
									if (presentationHeightQuietObservationEligible) {
										schedulePresentationHeightObservation()
									} else if (!presentationHeightFinalizationMovementOwned) {
										// Hidden finalization owns its programmatic tail movement.
										// Preserve the first quiet height sample across that movement;
										// real wheel, drag, scrollbar, and performance ownership still
										// cancel the latch through their independent guards above.
										stopPresentationHeightObservation()
									}
								}
								onPresentationHeightMeasuredHeightChanged: {
									scheduleViewportProbeHeightUpdate()
									schedulePresentationHeightObservation()
								}
								onRichGeometrySettlingChanged: {
									if (!richGeometrySettling)
										scheduleViewportProbeHeightUpdate()
									schedulePresentationHeightObservation()
								}
								// Incubation cannot expand the coordinate map. Only the quiet
								// two-observation commit replaces compact or retained geometry.
								height: deadlinePresentationFallback
									? timeline.deferredMessageRowHeight
									: stationaryThumbWarmPinned
									? stationaryThumbPinnedOuterHeight
									: presentationHeightCommittedHeight > 0
									? presentationHeightCommittedHeight
									: Math.max(viewportProbeBaseHeight(), activeScope.activity && item
										? item.lightweightMessageRowHeight : 0)
								onHeightChanged: {
									if (item) {
										scheduleViewportProbeHeightUpdate()
										schedulePresentationHeightObservation()
									}
								}
								clip: stationaryThumbWarmPinned || richGeometrySettling || (item
									&& presentationHeightMeasuredHeight > height + 0.5)
								opacity: timeline.scopePresentationContentVisible ? 1 : 0
								asynchronous: true
								// A cold row stays as a fixed-height placeholder until the
								// frame-paced admission queue selects it. Keeping the Loader
								// inactive is important: asynchronous incubation still lets a
								// burst of visible delegates finish in one GUI heartbeat.
								active: !deferredPresentation
									&& (!deadlinePresentationFallback
										|| deadlinePresentationRecoveryActive)
								onActiveChanged: {
									if (!active) {
										stopPresentationHeightObservation()
										const ownedStableId = resumeOwnedStableId
										outerLoaderReady = false
										resumeRequested = false
										resumeDuringFinalization = false
										resumeAdmitsPreview = false
										resumeOwnedStableId = ""
										resetOuterLoaderIncubationWatchdog(true)
										if (ownedStableId.length > 0)
											timeline.cancelRichLoaderResume(ownedStableId)
									} else
										schedulePresentationHeightObservation()
								}

								// DelegateChooser replaces the lightweight source delegate
								// before this asynchronous Loader has completed incubation.
								// Keep the same real message summary visible in that bounded
								// handoff instead of exposing an empty reserved rectangle.
								Rectangle {
									id: asyncRichHandoff
									anchors.fill: parent
									z: 1
									visible: asyncMessageDelegate.deadlinePresentationFallback
										|| (asyncMessageDelegate.active
											&& (!asyncMessageDelegate.item
											|| (!activeScope.activity
												&& asyncMessageDelegate.presentationHeightCommittedHeight <= 0)
											|| !asyncMessageDelegate.item.currentMessagePresentationReady
											|| (asyncMessageDelegate.item.richResourcesRequired
												&& !asyncMessageDelegate.item.richResourcesActive
												&& !asyncMessageDelegate.item.boundedRichGeometryReady
												&& !asyncMessageDelegate.item.staticPreviewPresentationReady)
											|| asyncMessageDelegate.item
												.terminalPreviewFallbackUnavailable
											|| (asyncMessageDelegate.item.richLoaderSuspended
												&& !asyncMessageDelegate.item.boundedRichGeometryReady
												&& (asyncMessageDelegate.item.richResourcesRequired
													|| asyncMessageDelegate.item.previewNeedsHydration
													|| asyncMessageDelegate.item.previewLoaderIncubationSuppressed))))
									// This item retains the outer delegate's reserved geometry while
									// its asynchronous handoff is in flight. Do not paint that
									// complete height as a card: a cold rich row can be several
									// hundred pixels tall before the real delegate is ready.
									radius: 0
									color: "transparent"
									border.width: 0
									Accessible.role: Accessible.ListItem
									Accessible.name: asyncMessageDelegate.presentationSummary
									Accessible.ignored:
										!visible
										|| asyncMessageDelegate.accessibilityPooled
										|| !asyncMessageDelegate.inVisibleViewport

									RowLayout {
										id: asyncRichHandoffSummary
										objectName: "asyncRichHandoffSummary"
										// Preview-only rows must still paint their real fixed-height
										// loading summary instead of a transparent reserved gap. Once
										// the loaded row owns bounded loading/error geometry, it is the
										// coherent handoff and this summary must not paint beside it.
										visible: !asyncMessageDelegate.accessibilityPooled
											&& (asyncMessageDelegate.deadlinePresentationFallback
												|| !asyncMessageDelegate.item
												|| !asyncMessageDelegate.item.boundedRichGeometryReady
												|| asyncMessageDelegate.presentationHeightCommittedHeight <= 0)
										anchors.left: parent.left
										anchors.right: parent.right
										anchors.top: parent.top
										anchors.topMargin: Theme.space2
										height: Math.min(48, Math.max(1, parent.height
											- Theme.space2 * 2))
										anchors.leftMargin: Theme.space3
										anchors.rightMargin: Theme.space3
										spacing: Theme.space2

										Label {
											Layout.maximumWidth: Math.max(80,
												asyncMessageDelegate.width * 0.24)
											textFormat: Text.PlainText
											text: asyncMessageDelegate.title
											color: Theme.textStrong
											font.pixelSize: Theme.fontLabel
											font.weight: Font.DemiBold
											elide: Text.ElideRight
											Accessible.ignored: true
										}
										Label {
											Layout.fillWidth: true
											textFormat: Text.PlainText
											text: asyncMessageDelegate.subtitle
											color: Theme.textMain
											font.pixelSize: Theme.chatBodyFontSize
											elide: Text.ElideRight
											maximumLineCount: 1
											Accessible.ignored: true
										}
										Label {
											visible: text.length > 0
											textFormat: Text.PlainText
											text: asyncMessageDelegate.timestamp
											color: Theme.textMuted
											font.pixelSize: Theme.fontCaption
											Accessible.ignored: true
										}
									}

									Rectangle {
										id: coldMessageLinkHandoff
										objectName: "coldMessageLinkHandoff"
										x: asyncMessageDelegate.cachedStaticHandoffCardX
										y: 58
										width: Math.min(400, Math.max(1, parent.width - x - Theme.space4))
										height: Math.min(108, Math.max(0, parent.height - y - Theme.space2))
										visible: height > 0 && !asyncMessageDelegate.accessibilityPooled
											&& asyncMessageDelegate.presentationHeightCommittedHeight <= 0
											&& Object.keys(asyncMessageDelegate.preview || ({})).length > 0
										radius: Theme.innerRadius
										color: Theme.embedSurface
										border.color: Theme.embedBorder
										clip: true
										Accessible.ignored: true
										readonly property bool compact: height < 76
										readonly property real contentInset: compact ? Theme.space2 : Theme.space3
										Image {
											id: coldHandoffStill
											x: parent.contentInset
											anchors.verticalCenter: parent.verticalCenter
											width: parent.compact ? height : 96
											height: Math.max(1, Math.min(64, parent.height - parent.contentInset * 2))
											source: parent.visible && asyncRichHandoff.visible
												&& asyncMessageDelegate.cachedStaticHandoffReady
												? asyncMessageDelegate.cachedStaticHandoffSource : ""
											visible: source.toString().length > 0
											asynchronous: true
											cache: true
											sourceSize: Qt.size(1280, 960)
											fillMode: Image.PreserveAspectCrop
										}
										Column {
											anchors.left: parent.left
											anchors.right: parent.right
											anchors.verticalCenter: parent.verticalCenter
											anchors.margins: parent.contentInset
											anchors.leftMargin: coldHandoffStill.visible
												? coldHandoffStill.x + coldHandoffStill.width + parent.contentInset : parent.contentInset
											spacing: Theme.space1
											Label {
												visible: !coldMessageLinkHandoff.compact
												width: parent.width
												text: asyncMessageDelegate.cachedStaticHandoffProvider || qsTr("Link")
												textFormat: Text.PlainText
												font.pixelSize: Theme.fontCaption
												color: Theme.textMuted
												elide: Text.ElideRight
											}
											Label {
												width: parent.width
												text: asyncMessageDelegate.cachedStaticHandoffTitle
													|| String(asyncMessageDelegate.preview.url || "")
												maximumLineCount: 1
												textFormat: Text.PlainText
												font.pixelSize: Theme.fontLabel
												font.weight: Font.DemiBold
												color: Theme.textStrong
												elide: Text.ElideRight
											}
											Label {
												width: parent.width
												text: String(asyncMessageDelegate.preview.url || "")
												visible: coldMessageLinkHandoff.height >= 100
												textFormat: Text.PlainText
												font.pixelSize: Theme.fontCaption
												color: Theme.textMuted
												elide: Text.ElideRight
											}
										}
									}

									// Long wheel interpolation deliberately suspends the heavy card.
									// Paint its already-retained still until the real presentation is
									// ready so the optimization never leaks through as an empty row.
									Rectangle {
										objectName: "asyncCachedStaticPreviewHandoff"
										x: asyncMessageDelegate.cachedStaticHandoffCardX
										y: asyncMessageDelegate.cachedStaticHandoffY
										width: Math.min(asyncMessageDelegate.cachedStaticHandoffCardWidth,
											Math.max(1, parent.width - x - Theme.space2))
										height: asyncMessageDelegate.cachedStaticHandoffVisibleHeight
										visible: !asyncMessageDelegate.accessibilityPooled
											&& asyncMessageDelegate.cachedStaticHandoffReady
											&& asyncMessageDelegate.presentationHeightCommittedHeight > 0
										radius: Theme.innerRadius
										color: Theme.embedSurface
										border.width: 1
										border.color: Theme.embedBorder
										clip: true
										Accessible.ignored: true

										Rectangle {
											anchors.left: parent.left
											anchors.right: parent.right
											anchors.top: parent.top
											height: asyncMessageDelegate.cachedStaticHandoffCompact ? 42 : 44
											color: Theme.withAlpha(Theme.surfaceRaised, 0.72)
											border.width: 0

											Column {
												anchors.left: parent.left
												anchors.right: parent.right
												anchors.verticalCenter: parent.verticalCenter
												anchors.leftMargin: Theme.space3
												anchors.rightMargin: Theme.space3
												spacing: 1

												Label {
													width: parent.width
													visible: text.length > 0
													textFormat: Text.PlainText
													text: asyncMessageDelegate.cachedStaticHandoffProvider
													color: Theme.textMuted
													font.pixelSize: Theme.fontCaption
													elide: Text.ElideRight
												}
											}
										}

										Image {
											id: cachedStaticHandoffImage
											x: asyncMessageDelegate.cachedStaticHandoffCompact
												? Theme.space3 : 1
											y: asyncMessageDelegate.cachedStaticHandoffCompact
												? 50 : 44
											width: asyncMessageDelegate.cachedStaticHandoffCompact
												? Math.min(92, Math.max(1, parent.width * 0.22))
												: Math.max(1, parent.width - 2)
											height: asyncMessageDelegate.cachedStaticHandoffCompact
												? Math.min(64, Math.max(1, parent.height - y - Theme.space3))
												: Math.min(asyncMessageDelegate.cachedStaticHandoffMediaHeight,
													Math.max(1, parent.height - y - 56))
											source: asyncRichHandoff.visible
												&& !asyncMessageDelegate.accessibilityPooled
												&& asyncMessageDelegate.cachedStaticHandoffReady
												? asyncMessageDelegate.cachedStaticHandoffSource : ""
											asynchronous: true
											cache: true
											sourceSize: Qt.size(1280, 960)
											fillMode: Image.PreserveAspectCrop
											smooth: true
											mipmap: false
											Accessible.ignored: true
										}

										Column {
											x: asyncMessageDelegate.cachedStaticHandoffCompact
												? cachedStaticHandoffImage.x + cachedStaticHandoffImage.width
													+ Theme.space2 : Theme.space3
											y: asyncMessageDelegate.cachedStaticHandoffCompact
												? cachedStaticHandoffImage.y
												: cachedStaticHandoffImage.y + cachedStaticHandoffImage.height
													+ Theme.space2
											width: Math.max(1, parent.width - x - Theme.space3)
											spacing: 3

											Label {
												width: parent.width
												textFormat: Text.PlainText
												text: asyncMessageDelegate.cachedStaticHandoffTitle
												color: Theme.textStrong
												font.pixelSize: Theme.fontLabel
												font.weight: Font.DemiBold
												elide: Text.ElideRight
												maximumLineCount: 1
											}

											Label {
												width: parent.width
												visible: text.length > 0
												textFormat: Text.PlainText
												text: asyncMessageDelegate.cachedStaticHandoffDescription
												color: Theme.textMuted
												font.pixelSize: Theme.fontCaption
												elide: Text.ElideRight
												maximumLineCount: 1
											}
										}
									}
								}

								function openAutomationActions() {
									return item ? item.openAutomationActions() : null
								}
								function openMessageActions() {
									if (item)
										item.openMessageActions()
								}
								function closeMessageActionsForReuse() {
									if (item)
										item.closeMessageActionsForReuse()
								}
								function primaryRichContentViewportBounds() {
									return item ? item.primaryRichContentViewportBounds() : null
								}
								function requestPreviewHydrationIfNeeded(allowDuringFinalization) {
									if (item)
										item.requestPreviewHydrationIfNeeded(allowDuringFinalization)
								}
								function resumeRichContentAfterScroll(allowDuringFinalization,
										admitPreviewMaterialization) {
									if (!beginDeadlinePresentationFallbackRecovery())
										return false
									if (admitPreviewMaterialization === true)
										resumeOwnedStableId = stableId
									if (item) {
										return item.resumeRichContentAfterScroll(
											allowDuringFinalization,
											admitPreviewMaterialization)
									}
									if (outerLoaderIncubationSuppressed)
										return false
									resumeRequested = true
									resumeDuringFinalization = allowDuringFinalization === true
									resumeAdmitsPreview =
										admitPreviewMaterialization === true
									armOuterLoaderIncubationWatchdog()
									return true
								}
								function completeRichLoaderResumeWhenReady() {
									if (item)
										item.completeRichLoaderResumeWhenReady()
								}
								function markNearViewportPrefetchTerminal() {
									if (item)
										item.markNearViewportPrefetchTerminal()
								}
								function suspendRichResourcesForScroll() {
									if (item)
										item.suspendRichResourcesForScroll()
								}
								function clearStationaryThumbWarmObservation() {
									stationaryThumbWarmObservationStableId = ""
									stationaryThumbWarmObservationScopeToken = ""
									stationaryThumbWarmObservationScopeGeneration = -1
									stationaryThumbWarmObservationPressEpoch = 0
									stationaryThumbWarmObservationDwellGeneration = 0
									stationaryThumbWarmObservationPinSerial = 0
								}
								function stationaryThumbWarmObservationMatchesPin(pin) {
									return timeline.stationaryThumbWarmPinMatchesCurrentScope(
											pin, stableId)
										&& stationaryThumbWarmObservationStableId
											=== String(stableId || "")
										&& stationaryThumbWarmObservationScopeToken
											=== String(pin.scopeToken || "")
										&& stationaryThumbWarmObservationScopeGeneration
											=== Number(pin.scopeGeneration)
										&& stationaryThumbWarmObservationPressEpoch
											=== Number(pin.pressEpoch)
										&& stationaryThumbWarmObservationDwellGeneration
											=== Number(pin.dwellGeneration)
										&& stationaryThumbWarmObservationPinSerial
											=== Number(pin.serial)
								}
								function recordStationaryThumbWarmObservation(pin) {
									if (!timeline.stationaryThumbWarmPinMatchesCurrentScope(
											pin, stableId))
										return false
									stationaryThumbWarmObservationStableId = String(stableId || "")
									stationaryThumbWarmObservationScopeToken = String(pin.scopeToken || "")
									stationaryThumbWarmObservationScopeGeneration =
										Number(pin.scopeGeneration)
									stationaryThumbWarmObservationPressEpoch = Number(pin.pressEpoch)
									stationaryThumbWarmObservationDwellGeneration =
										Number(pin.dwellGeneration)
									stationaryThumbWarmObservationPinSerial = Number(pin.serial)
									return true
								}
								function retireStationaryThumbWarmForDelegate(reason) {
									const retiredId = stationaryThumbWarmObservationStableId.length > 0
										? stationaryThumbWarmObservationStableId
										: String(stableId || "")
									clearStationaryThumbWarmObservation()
									return retiredId.length > 0
										&& timeline.retireStationaryThumbWarmPin(retiredId, reason)
								}
								function admitStationaryThumbWarm(pressEpoch,
										dwellGeneration) {
									if (!stationaryThumbWarmEligible
											|| !stationaryThumbWarmPinned
											|| Number(stationaryThumbWarmPin.pressEpoch)
												!== Number(pressEpoch)
											|| Number(stationaryThumbWarmPin.dwellGeneration)
												!== Number(dwellGeneration)
											|| !stationaryThumbWarmOwned)
										return false
									const admitted = item
										? item.admitStationaryThumbWarm(pressEpoch,
											dwellGeneration) : active
									if (admitted)
										recordStationaryThumbWarmObservation(
											stationaryThumbWarmPin)
									return admitted
								}
								function stationaryThumbWouldBeOuterHeight() {
									if (presentationHeightCommittedHeight > 0)
										return presentationHeightCommittedHeight
									return viewportProbeBaseHeight()
								}
								function stationaryThumbWouldBeProbeHeight() {
									return presentationHeightCommittedHeight > 0
										? presentationHeightCommittedHeight
										: Number(viewportProbeHeight || 0)
								}
								function observeStationaryThumbWarmPaintedState() {
									const pin = stationaryThumbWarmPin
									if (!stationaryThumbWarmPinned || !stationaryThumbWarmOwned
											|| !stationaryThumbWarmObservationMatchesPin(pin))
										return false
									let outcome = ""
									if (paintedTerminal)
										outcome = "terminal"
									else if (paintedSurface && item
											&& item.previewPrewarmVisualReady === true)
										outcome = "painted"
									if (outcome.length === 0)
										return false
									return timeline.queueStationaryThumbWarmReceipt(stableId,
										stationaryThumbWarmObservationScopeToken,
										stationaryThumbWarmObservationScopeGeneration,
										stationaryThumbWarmObservationPressEpoch,
										stationaryThumbWarmObservationDwellGeneration,
										stationaryThumbWarmObservationPinSerial, outcome,
										inVisibleViewport,
										stationaryThumbWouldBeOuterHeight(),
										stationaryThumbWouldBeProbeHeight())
								}
								function noteStationaryThumbWarmProgress() {
									const pin = stationaryThumbWarmPin
									if (!stationaryThumbWarmOwned
											|| !stationaryThumbWarmObservationMatchesPin(pin))
										return false
									return stationaryThumbWarmGate.noteAdmissionProgress(stableId,
										Number(pin.pressEpoch), Number(pin.dwellGeneration))
								}
								onStationaryThumbWarmOwnedChanged: {
									if (stationaryThumbWarmOwned) {
										if (!stationaryThumbWarmObservationMatchesPin(
												stationaryThumbWarmPin))
											admitStationaryThumbWarm(
											Number(stationaryThumbWarmPin.pressEpoch),
											Number(stationaryThumbWarmPin.dwellGeneration))
										observeStationaryThumbWarmPaintedState()
									} else
										clearStationaryThumbWarmObservation()
								}
								onStationaryThumbWarmPinnedChanged: {
									if (!stationaryThumbWarmPinned) {
										schedulePresentationHeightObservation()
										clearStationaryThumbWarmObservation()
									} else
										observeStationaryThumbWarmPaintedState()
								}
								onPaintedSummaryChanged: observeStationaryThumbWarmPaintedState()
								onPaintedSurfaceChanged: {
									noteStationaryThumbWarmProgress()
									observeStationaryThumbWarmPaintedState()
								}
								onPaintedLoadingChanged: observeStationaryThumbWarmPaintedState()
								onPaintedTerminalChanged: observeStationaryThumbWarmPaintedState()
								onPreviewPrewarmVisualReadyChanged:
									observeStationaryThumbWarmPaintedState()
								onPreviewPrewarmProgressRevisionChanged: {
									noteStationaryThumbWarmProgress()
									observeStationaryThumbWarmPaintedState()
								}
								onInVisibleViewportChanged: observeStationaryThumbWarmPaintedState()
								function cancelNearViewportPrefetchForScroll() {
									const ownedStableId = resumeOwnedStableId
									resumeRequested = false
									resumeDuringFinalization = false
									resumeAdmitsPreview = false
									resumeOwnedStableId = ""
									resetOuterLoaderIncubationWatchdog()
									if (item)
										item.cancelNearViewportPrefetchForScroll()
									if (ownedStableId.length > 0)
										timeline.cancelRichLoaderResume(ownedStableId)
								}
								function resetOuterLoaderIncubationWatchdog(clearSuppression) {
									outerLoaderIncubationWatchdog.stop()
									outerLoaderIncubationWatchdogStartedAt = 0
									if (clearSuppression === true)
										outerLoaderIncubationSuppressed = false
								}
								function armOuterLoaderIncubationWatchdog() {
									if (!active || item || outerLoaderIncubationSuppressed
											|| !resumeRequested || !resumeAdmitsPreview
											|| resumeOwnedStableId.length === 0
											|| timeline.richScrollInProgress
											|| timeline.richLoaderResumeActiveId
												!== resumeOwnedStableId) {
										outerLoaderIncubationWatchdog.stop()
										outerLoaderIncubationWatchdogStartedAt = 0
										return
									}
									if (!outerLoaderIncubationWatchdog.running) {
										outerLoaderIncubationWatchdogStartedAt = Date.now()
										outerLoaderIncubationWatchdog.start()
									}
								}
								function recoverOuterLoaderIncubationTimeout() {
									if (!active || item || outerLoaderIncubationSuppressed
											|| !resumeRequested || !resumeAdmitsPreview
											|| resumeOwnedStableId.length === 0
											|| timeline.richScrollInProgress
											|| timeline.richLoaderResumeActiveId
												!== resumeOwnedStableId)
										return
									const ownedStableId = resumeOwnedStableId
									outerLoaderIncubationWatchdogStartedAt = 0
									// The single adjacent prefetch is bounded by the
									// timeline's 2.4 s deadline and cancelled by scroll.
									// Let its asynchronous outer delegate finish instead
									// of converting it to a terminal summary after 192 ms.
									if (!inVisibleViewport
											&& timeline.nearViewportPrefetchActiveId
												=== ownedStableId)
										return
									outerLoaderIncubationSuppressed = true
									resumeRequested = false
									resumeDuringFinalization = false
									resumeAdmitsPreview = false
									resumeOwnedStableId = ""
									if (!timeline.abandonNearViewportPrefetch(
											ownedStableId, true))
										timeline.cancelRichLoaderResume(ownedStableId)
									releaseDeferredMaterialization(ownedStableId)
								}
								Timer {
									id: presentationHeightReleaseTimer
									interval: 16
									repeat: false
									onTriggered:
										asyncMessageDelegate.observePresentationHeight()
								}
								Timer {
									id: viewportProbeUpdateTimer
									interval: 0
									repeat: false
									onTriggered:
										asyncMessageDelegate.updateViewportProbeHeight()
								}
								Timer {
									id: outerLoaderIncubationWatchdog
									interval: 192
									repeat: false
									onTriggered:
										asyncMessageDelegate.recoverOuterLoaderIncubationTimeout()
								}

								ListView.onPooled: {
									retireStationaryThumbWarmForDelegate("delegate-pooled")
									deadlinePresentationRecoveryActive = false
									accessibilityPooled = true
									updateViewportMembership()
									// Pooling the same stable presentation must not restore
									// the conservative estimate. Only abandon the in-flight
									// quiet observation; retain the committed cache.
									stopPresentationHeightObservation()
									viewportProbeUpdateTimer.stop()
									resetOuterLoaderIncubationWatchdog(true)
									const retiredStableId = resumeOwnedStableId.length > 0
										? resumeOwnedStableId : stableId
									if (!timeline.abandonNearViewportPrefetch(
											retiredStableId, true))
										timeline.cancelRichLoaderResume(retiredStableId)
									releaseDeferredMaterialization(retiredStableId)
									resumeRequested = false
									resumeDuringFinalization = false
									resumeAdmitsPreview = false
									resumeOwnedStableId = ""
									if (item)
										item.handleListViewPooled()
								}
								ListView.onReused: {
									releaseDeferredMaterialization(
										deferredMaterializationCleanupId,
										deferredMaterializationCleanupGeneration)
									retireStationaryThumbWarmForDelegate("delegate-reused")
									stationaryThumbDelegateIdentityStableId =
										String(stableId || "")
									clearDeadlinePresentationFallback()
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									stopPresentationHeightObservation()
									resetOuterLoaderIncubationWatchdog(true)
									viewportProbeUpdateTimer.stop()
									resetViewportProbeHeight()
									if (item)
										item.handleListViewReused()
									accessibilityPooled = false
									updateViewportMembership()
									scheduleViewportProbeHeightUpdate()
									if (identityChanged || presentationHeightReleased)
										schedulePresentationHeightObservation()
									captureDeferredMaterializationOwnership()
								}
								onStableIdChanged: {
									const retiredStationaryThumbId =
										stationaryThumbDelegateIdentityStableId
									const currentStationaryThumbId = String(stableId || "")
									clearStationaryThumbWarmObservation()
									if (retiredStationaryThumbId.length > 0
											&& retiredStationaryThumbId
												!== currentStationaryThumbId)
										timeline.retireStationaryThumbWarmPin(
											retiredStationaryThumbId, "delegate-identity-changed")
									stationaryThumbDelegateIdentityStableId =
										currentStationaryThumbId
									clearDeadlinePresentationFallback()
									resetPresentationHeightIdentity()
									resetViewportProbeHeight()
									scheduleViewportProbeReset()
									timeline.scheduleViewportMembershipUpdate()
									const retiredMaterializationId =
										deferredMaterializationCleanupId
									const retiredMaterializationGeneration =
										deferredMaterializationCleanupGeneration
									const currentMaterializationId = String(stableId || "")
									if (retiredMaterializationId.length > 0
											&& retiredMaterializationId !== currentMaterializationId)
										releaseDeferredMaterialization(retiredMaterializationId,
											retiredMaterializationGeneration)
									deferredMaterializationCleanupId = currentMaterializationId
									deferredMaterializationCleanupGeneration =
										timeline.deferredMaterializationGenerationFor(
											currentMaterializationId)
									const ownedStableId = resumeOwnedStableId
									if (ownedStableId.length > 0
											&& ownedStableId !== stableId
											&& !timeline.abandonNearViewportPrefetch(
												ownedStableId, true))
										timeline.cancelRichLoaderResume(ownedStableId)
									resumeRequested = false
									resumeDuringFinalization = false
									resumeAdmitsPreview = false
									resumeOwnedStableId = ""
									resetOuterLoaderIncubationWatchdog(true)
								}
								Component.onCompleted: {
									stationaryThumbDelegateIdentityStableId =
										String(stableId || "")
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									captureDeferredMaterializationOwnership()
									scheduleViewportProbeReset()
									updateViewportMembership()
									schedulePresentationHeightObservation()
								}
								Component.onDestruction: {
									stopPresentationHeightObservation()
									if (typeof timeline !== "undefined" && timeline) {
										retireStationaryThumbWarmForDelegate(
											"delegate-destroyed")
										const retiredStableId = resumeOwnedStableId.length > 0
											? resumeOwnedStableId : stableId
										const abandoned = timeline.abandonNearViewportPrefetch(
											retiredStableId, true)
										if (resumeOwnedStableId.length > 0 && !abandoned)
											timeline.cancelRichLoaderResume(resumeOwnedStableId)
										releaseDeferredMaterialization(
											deferredMaterializationCleanupId,
											deferredMaterializationCleanupGeneration)
									}
								}
								onLoaded: {
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									const outerIncubationRecovered =
										outerLoaderIncubationSuppressed
									resetOuterLoaderIncubationWatchdog(true)
									outerLoaderReady = true
									timeline.noteScopePresentationOuterLoaderLoaded()
									item.accessibilityPooled = accessibilityPooled
									scheduleViewportProbeHeightUpdate()
									schedulePresentationHeightObservation()
									if (resumeRequested) {
										const allowDuringFinalization = resumeDuringFinalization
										const admitPreviewMaterialization =
											resumeAdmitsPreview
										resumeRequested = false
										resumeDuringFinalization = false
										resumeAdmitsPreview = false
										if (item.resumeRichContentAfterScroll(
												allowDuringFinalization,
												admitPreviewMaterialization) === false)
											timeline.completeRichLoaderResume(stableId)
									}
									if (outerIncubationRecovered)
										timeline.scheduleRichContentResume()
								}
								onItemChanged: {
									const identityChanged =
										reevaluatePresentationHeightIdentity()
									if (identityChanged)
										resetViewportProbeHeight()
									if (item) {
										schedulePresentationHeightObservation()
										timeline.scheduleScopePresentationFastPath()
										if (stationaryThumbWarmOwned)
											admitStationaryThumbWarm(
												Number(stationaryThumbWarmPin.pressEpoch),
												Number(stationaryThumbWarmPin.dwellGeneration))
									} else
										stopPresentationHeightObservation()
									observeStationaryThumbWarmPaintedState()
								}

								sourceComponent: Component {
									ChatMessageFrame {
					id: messageDelegate
					// The handoff and the authored row must never paint simultaneously.
					opacity: logEntry || !asyncRichHandoff.visible ? 1 : 0
					enabled: logEntry || !asyncRichHandoff.visible
					stableId: asyncMessageDelegate.stableId
					own: asyncMessageDelegate.own
					readonly property bool performanceTimelineDelegate: true
					logEntry: activeScope.activity
						logAlternating: logEntry && root.stableAlternatingRow(stableId)
				// Ordinary rows enter UIA only when fully contained so cached actor/header
				// nodes cannot leak across the clip. A legitimate rich card may be taller
				// than the viewport; it can never satisfy full containment, so expose that
				// row while it intersects the viewport and let its visible provider surface
				// remain navigable.
				accessibilityViewportVisible: asyncMessageDelegate.height > timeline.height
					? asyncMessageDelegate.y + asyncMessageDelegate.height > timeline.contentY + 0.5
						&& asyncMessageDelegate.y < timeline.contentY + timeline.height - 0.5
					: asyncMessageDelegate.y >= timeline.contentY - 0.5
						&& asyncMessageDelegate.y + asyncMessageDelegate.height
							<= timeline.contentY + timeline.height + 0.5
				accessibilitySuppressed: root.backgroundAccessibilitySuppressed
					|| timeline.scopePresentationPending
					|| (!logEntry && asyncRichHandoff.visible)
				accessibilityBarrierEnabled: logEntry || messageRowReady
				hoverEffectsEnabled: !root.visualFixtureOverrideActive
					&& !timeline.richScrollInProgress && !previewOnlyPaintPending
						function openAutomationActions() {
							openMessageActions()
							return messageRowLoader.item
								? messageRowLoader.item.actionsMenu : null
						}
						function openMessageActions() {
							if (previewOnlyPaintPending)
								return
							if (messageRowLoader.item)
								messageRowLoader.item.openActions()
						}
						function closeMessageActionsForReuse() {
							if (messageRowLoader.item)
								messageRowLoader.item.closeActionsForReuse()
						}
						readonly property bool previewNeedsHydration: !!preview
							&& Object.keys(preview).length > 0
							&& !previewBackendError
							&& (preview.state === "loading" || preview.loading === true)
						readonly property bool previewBackendError: {
							const state = String(preview ? preview.state || "" : "")
								.toLowerCase()
							const status = String(preview ? preview.status || "" : "")
								.toLowerCase()
							return state === "error" || state === "failed"
								|| status === "error" || status === "failed"
								|| !!(preview && preview.failed === true)
						}
						readonly property bool attachmentNeedsHydration: (attachments || []).some(function(attachment) {
							return !!attachment && String(attachment.state || "").toLowerCase() === "loading"
						})
						readonly property bool bodyNeedsHydration: bodyHydrationPending
						readonly property bool backendContentNeedsHydration: previewNeedsHydration
							|| attachmentNeedsHydration
						readonly property bool contentNeedsHydration: backendContentNeedsHydration
							|| bodyNeedsHydration
						// One backend request starts the complete async preview/attachment
						// pipeline. Model updates can keep the row in "loading" for several
						// provider stages, so do not enqueue the same message again while
						// that pipeline is already in flight.
						property bool backendHydrationRequested: false
						property string hydrationOwnerScopeToken: ""
						property int hydrationOwnerGeneration: -1
						function adoptHydrationOwner() {
							hydrationOwnerScopeToken = String(activeScope.scopeToken || "")
							hydrationOwnerGeneration = timeline.scopePresentationGeneration
						}
						property string stationaryThumbHydrationStableId: ""
						property string stationaryThumbHydrationScopeToken: ""
						property int stationaryThumbHydrationScopeGeneration: -1
						property int stationaryThumbHydrationPressEpoch: 0
						property int stationaryThumbHydrationDwellGeneration: 0
						property int stationaryThumbHydrationPinSerial: 0
						property bool previewBackendErrorEpochActive: false
						property string previewBackendErrorStableId: ""
						// A pooled rich-card tree is expensive to retarget while a
						// variable-height ListView is moving. Retire it outside the
						// viewport, keep a provider-aware geometry estimate, and wake
						// only the settled visible rows after scrolling ends.
						property bool richLoaderSuspended: false
						// Existing visible rows keep their complete presentation through a
						// gesture. A delegate first created by the expanding retention
						// window stays a lightweight geometry shell until scrolling stops,
						// avoiding full message-tree allocation and GC on the input frame.
						property bool messageRowMaterialized: false
						property int diagnosticLoadGeneration: -1
						property string diagnosticLoadId: ""
						function notePlainLoaderEvent(phase) {
							if (diagnosticLoadId === String(stableId))
								timeline.notePlainLoaderEvent(stableId, diagnosticLoadGeneration, phase)
						}
						// A cached preview refresh can briefly replace a ready provider
						// tree with its loading surface. Once this stable-id has shown a
						// complete message, retain that presentation contract and keep the
						// real title/subtitle/timestamp summary above the rebuilding tree.
						// Pooling or rebinding resets the latch for the next message.
						property bool messagePresentationReadyLatched: false
						// Backend hydration may finish for several visible messages in
						// one network turn. Keep each ready preview on the stable branded
						// surface until the timeline's single global loader token admits
						// its RichPreviewCard tree.
						property bool previewMaterializationAdmitted: false
						// A QML Loader can remain in Null/Loading without ever emitting
						// Ready or Error. Bound that single-flight state: after the
						// branded placeholder has owned a short incubation window, stop
						// retrying the same stable-id until its preview identity changes
						// or the delegate leaves the viewport.
						property bool previewLoaderIncubationSuppressed: false
						property string previewLoaderIncubationSuppressedIdentity: ""
						property string previewLoaderTerminalErrorIdentity: ""
						property string nearViewportPrefetchTerminalIdentity: ""
						property double previewLoaderIncubationWatchdogStartedAt: 0
						readonly property bool previewLoaderIncubationWatchdogRunning:
							previewLoaderIncubationWatchdogTimer.running
						readonly property int previewLoaderStatus: {
							const row = messageRowLoader.item
							return row && row.previewLoader
								? row.previewLoader.status : Loader.Null
						}
						readonly property string previewLoaderIncubationIdentity: {
							return root.richPreviewPresentationIdentity(stableId, preview)
						}
						readonly property bool nearViewportPrefetchTerminal:
							nearViewportPrefetchTerminalIdentity.length > 0
							&& nearViewportPrefetchTerminalIdentity
								=== previewLoaderIncubationIdentity
						property bool richResourceActivationQueued: false
						readonly property bool richResourceActivationPending:
							richResourceActivationQueued
							|| richResourceActivationTimer.running
							|| richResourceActivationSettleTimer.running
						readonly property string previewLoaderSurfaceState: {
							const row = messageRowLoader.item
							const loader = row ? row.previewLoader : null
							if (!loader || loader.status !== Loader.Ready || !loader.item
									|| loader.item.materializationSettled !== true)
								return "blank"
							return String(loader.item.performanceEmbedSurfaceState || "blank")
								.toLowerCase()
						}
						readonly property bool previewLoaderSurfaceNonBlank:
							previewLoaderSurfaceState !== "blank"
						readonly property bool staticPreviewPresentationReady: {
							const row = messageRowLoader.item
							const loader = row ? row.previewLoader : null
							const card = loader && loader.status === Loader.Ready ? loader.item : null
							// Keep completed current-identity stills and native link cards
							// in the existing ListView cache. A card without artwork has no
							// image cache lease, but is still a complete static presentation.
							// This never admits a new tree or retains a player.
							return !accessibilityPooled && !hasAttachmentContent && !hasInlineImageContent
								&& hydrationOwnerGeneration === timeline.scopePresentationGeneration
								&& hydrationOwnerScopeToken === String(activeScope.scopeToken || "")
								&& asyncMessageDelegate.presentationHeightCommittedHeight > 0
								&& previewMaterializationAdmitted && !previewNeedsHydration
								&& !previewTerminalUnavailable && !!card
								&& card.materializationSettled
								&& (card.staticPreviewCacheActive
									|| card.performanceEmbedSurfaceState === "not-required")
								&& card.paintedSurface && !card.inlinePlaybackActive
								&& !card.currentMediaManagedAnimated
						}
						onStaticPreviewPresentationReadyChanged: {
							// A same-message artwork/identity update can occur without a
							// viewport-membership change. Retire its old offscreen admission
							// immediately rather than decoding a new card under that lease.
							if (!staticPreviewPresentationReady && !richContentViewportEligible
									&& previewMaterializationAdmitted) {
								previewMaterializationAdmitted = false
								timeline.cancelRichLoaderResume(stableId)
								suspendRichResourcesForScroll()
							}
						}
						readonly property string performancePreviewSurfaceState:
							!hasPreviewContent ? "not-required"
							: previewLoaderIncubationSuppressed ? "loading"
							: messageRowLoader.status === Loader.Ready && messageRowLoader.item
								? String(messageRowLoader.item.previewSurfaceState || "blank")
									.toLowerCase()
								: "blank"
						readonly property bool previewLoaderFailed:
							(previewLoaderTerminalErrorIdentity.length > 0
								&& previewLoaderTerminalErrorIdentity
									=== previewLoaderIncubationIdentity)
							|| (messageRowLoader.item && messageRowLoader.item.previewLoader
								&& messageRowLoader.item.previewLoader.status === Loader.Error)
						readonly property bool previewTerminalUnavailable:
							previewBackendError || previewLoaderFailed
							|| previewLoaderIncubationSuppressed
						readonly property bool terminalPreviewFallbackUnavailable:
							hasPreviewContent
							&& previewTerminalUnavailable
							&& (!messageRowLoader.item || richLoaderSuspended
								|| !timeline.prependRichContentActive(
									stableId, richContentViewportEligible))
						readonly property bool previewPresentationReady: !hasPreviewContent
							|| previewNeedsHydration
							|| previewTerminalUnavailable
							|| previewLoaderFailed
							|| previewLoaderIncubationSuppressed
							|| (previewMaterializationAdmitted
								&& messageRowLoader.item && messageRowLoader.item.previewLoader
								&& (messageRowLoader.item.previewLoader.status === Loader.Error
									|| (messageRowLoader.item.previewLoader.status === Loader.Ready
										&& messageRowLoader.item.previewLoader.item
										&& previewLoaderSurfaceNonBlank
										&& messageRowLoader.item.previewLoader.item
											.materializationSettled)))
						readonly property bool currentMessagePresentationReady:
							messageRowMaterialized
							&& (messageRowLoader.status === Loader.Error
								|| (messageRowLoader.status === Loader.Ready
									&& previewPresentationReady))
						readonly property bool messageRowReady: messageRowMaterialized
							&& (currentMessagePresentationReady
								|| messagePresentationReadyLatched)
						readonly property bool messageContentLoaderReady:
							messageRowLoader.status === Loader.Ready
						// A component-level preview error is a terminal branded
						// "unavailable" surface, not a resource that can ever be
						// reactivated. Other rich content in the same message still
						// owns the normal resource lifecycle.
						readonly property bool richResourcesRequired:
							(hasPreviewContent && !previewTerminalUnavailable)
							|| hasAttachmentContent || hasInlineImageContent
						readonly property bool previewStagingPresentationReady:
							!hasPreviewContent
							|| (!previewNeedsHydration && !previewTerminalUnavailable)
							|| (messageRowLoader.item
								&& messageRowLoader.item.previewHydrationPlaceholderLoader
								&& messageRowLoader.item.previewHydrationPlaceholderLoader.status
									=== Loader.Ready
								&& !!messageRowLoader.item.previewHydrationPlaceholderLoader.item)
						readonly property bool attachmentPresentationReady:
							!hasAttachmentContent
							|| (messageRowLoader.item && messageRowLoader.item.attachmentLoader
								&& messageRowLoader.item.attachmentLoader.status === Loader.Ready
								&& !!messageRowLoader.item.attachmentLoader.item)
						// A loading or terminal native preview shell has bounded geometry.
						// Let an independently ready attachment gallery settle beside it
						// without waiting for the optional provider pipeline to finish.
						readonly property bool boundedRichGeometryReady: messageRowReady
							&& hasPreviewContent
							&& (previewNeedsHydration || previewTerminalUnavailable)
							&& previewStagingPresentationReady
							&& attachmentPresentationReady
							&& !hasInlineImageContent
						readonly property bool richResourceActivationReady:
							!previewNeedsHydration
							&& (!hasPreviewContent || previewTerminalUnavailable
								|| (previewMaterializationAdmitted
									&& messageRowLoader.item
									&& messageRowLoader.item.previewLoader
									&& messageRowLoader.item.previewLoader.status
										=== Loader.Ready
									&& messageRowLoader.item.previewLoader.item
									&& previewLoaderSurfaceNonBlank))
						onRichResourcesRequiredChanged: {
							const terminalId = String(stableId || "")
							if (!richResourcesRequired
									&& (timeline.nearViewportPrefetchReadyId === terminalId
										|| (timeline.nearViewportPrefetchActiveId === terminalId
											&& messageRowReady && previewPresentationReady
											&& !contentNeedsHydration)))
								timeline.retireTerminalNearViewportPrefetch(terminalId)
							updateRichResourceActivity()
						}
						onPreviewLoaderIncubationSuppressedChanged: {
							if (!previewLoaderIncubationSuppressed)
								return
							retireOwnedTerminalPreviewPrefetch()
						}
						onPreviewBackendErrorChanged: {
							if (previewBackendError) {
								previewBackendErrorEpochActive = true
								previewBackendErrorStableId = String(stableId || "")
								// A backend error is terminal for the current preview
								// generation, but not for this stable message. Clear any
								// in-flight latch and release only preview ownership so a
								// mixed attachment/inline row keeps its shared resource token.
								backendHydrationRequested = false
								stationaryThumbHydrationStableId = ""
								stationaryThumbHydrationScopeToken = ""
								stationaryThumbHydrationScopeGeneration = -1
								stationaryThumbHydrationPressEpoch = 0
								stationaryThumbHydrationDwellGeneration = 0
								stationaryThumbHydrationPinSerial = 0
								previewMaterializationAdmitted = false
								previewLoaderTerminalErrorIdentity = ""
								nearViewportPrefetchTerminalIdentity = ""
								resetPreviewLoaderIncubationWatchdog(true)
								retireOwnedTerminalPreviewPrefetch()
								return
							}
							const previousBackendError = previewBackendErrorEpochActive
							const previousStableId = previewBackendErrorStableId
							previewBackendErrorEpochActive = false
							previewBackendErrorStableId = ""
							recoverPreviewBackendGeneration(
								previewBackendRecoveryAction(previousStableId,
									String(stableId || ""), previousBackendError,
									previewBackendError, previewNeedsHydration))
						}
						onPreviewLoaderFailedChanged: {
							const row = messageRowLoader.item
							const loader = row ? row.previewLoader : null
							if (!loader || loader.status !== Loader.Error)
								return
							previewLoaderTerminalErrorIdentity =
								previewLoaderIncubationIdentity
							retireOwnedTerminalPreviewPrefetch()
							messagePresentationReadyLatched = true
							completeRichLoaderResumeWhenReady()
						}
						onPreviewLoaderSurfaceStateChanged: {
							if (previewLoaderSurfaceNonBlank) {
								// A successful settled retry supersedes any terminal
								// marker from an earlier Error/watchdog generation.
								resetPreviewLoaderIncubationWatchdog(true)
								previewLoaderTerminalErrorIdentity = ""
								nearViewportPrefetchTerminalIdentity = ""
							} else if (previewMaterializationAdmitted) {
								armPreviewLoaderIncubationWatchdog()
								// A loaded provider tree can begin a new asynchronous
								// presentation after its original token was released.
								// Reacquire single-flight ownership so the same bounded
								// placeholder contract covers that generation too.
								if (timeline.richLoaderResumeActiveId !== stableId)
									timeline.scheduleRichContentResume()
							}
							completeRichLoaderResumeWhenReady()
						}
						onCurrentMessagePresentationReadyChanged: {
							if (currentMessagePresentationReady) {
								messagePresentationReadyLatched = true
								completeRichLoaderResumeWhenReady()
							}
						}
						function resetPreviewLoaderIncubationWatchdog(clearSuppression) {
							previewLoaderIncubationWatchdogTimer.stop()
							previewLoaderIncubationWatchdogStartedAt = 0
							if (clearSuppression === true) {
								previewLoaderIncubationSuppressed = false
								previewLoaderIncubationSuppressedIdentity = ""
							}
						}
						function previewBackendRecoveryAction(previousStableId, currentStableId,
								previousBackendError, currentBackendError, currentNeedsHydration) {
							if (currentNeedsHydration)
								return "hydrate"
							if (previousBackendError && !currentBackendError
									&& String(previousStableId || "")
										=== String(currentStableId || "")
									&& String(currentStableId || "").length > 0)
								return "resume"
							return "none"
						}
						function recoverPreviewBackendGeneration(recoveryAction) {
							if (recoveryAction !== "hydrate") {
								backendHydrationRequested = false
								stationaryThumbHydrationStableId = ""
								stationaryThumbHydrationScopeToken = ""
								stationaryThumbHydrationScopeGeneration = -1
								stationaryThumbHydrationPressEpoch = 0
								stationaryThumbHydrationDwellGeneration = 0
								stationaryThumbHydrationPinSerial = 0
							}
							previewMaterializationAdmitted = false
							previewLoaderTerminalErrorIdentity = ""
							nearViewportPrefetchTerminalIdentity = ""
							resetPreviewLoaderIncubationWatchdog(true)
							richResourceActivationTimer.stop()
							richResourceActivationSettleTimer.stop()
							richResourceActivationQueued = false
							updateRichResourceActivity()
							if (recoveryAction === "hydrate") {
								requestPreviewHydrationIfNeeded()
								return
							}
							if (recoveryAction === "resume" && hasPreviewContent
									&& richContentViewportEligible
									&& !timeline.richScrollInProgress)
								timeline.scheduleRichContentResume()
						}
						function markNearViewportPrefetchTerminal() {
							nearViewportPrefetchTerminalIdentity =
								previewLoaderIncubationIdentity
						}
						function retireOwnedTerminalPreviewPrefetch() {
							const terminalId = String(stableId || "")
							if (timeline.nearViewportPrefetchActiveId === terminalId
									|| timeline.nearViewportPrefetchReadyId
										=== terminalId)
								timeline.retireTerminalNearViewportPrefetch(terminalId)
							updateRichResourceActivity()
						}
						function armPreviewLoaderIncubationWatchdog() {
							const row = messageRowLoader.item
							const loader = row ? row.previewLoader : null
							const loaderFinished = loader
								&& (loader.status === Loader.Error
									|| (loader.status === Loader.Ready
										&& previewLoaderSurfaceNonBlank))
							if (!previewMaterializationAdmitted
									|| previewLoaderIncubationSuppressed
									|| accessibilityPooled || !richContentViewportEligible
									|| timeline.richScrollInProgress
									|| timeline.richLoaderResumeActiveId !== stableId
									|| loaderFinished) {
								previewLoaderIncubationWatchdogTimer.stop()
								previewLoaderIncubationWatchdogStartedAt = 0
								return
							}
							if (!previewLoaderIncubationWatchdogTimer.running) {
								previewLoaderIncubationWatchdogStartedAt = Date.now()
								previewLoaderIncubationWatchdogTimer.start()
							}
						}
						function recoverPreviewLoaderIncubationTimeout() {
							const row = messageRowLoader.item
							const loader = row ? row.previewLoader : null
							if (!previewMaterializationAdmitted
									|| previewLoaderIncubationSuppressed
									|| accessibilityPooled || !richContentViewportEligible
									|| timeline.richScrollInProgress
									|| timeline.richLoaderResumeActiveId !== stableId
									|| (loader && (loader.status === Loader.Error
										|| (loader.status === Loader.Ready
											&& previewLoaderSurfaceNonBlank))))
								return
							// The single offscreen prefetch already has its own 2.4 s
							// deadline and is cancelled synchronously by user scroll.
							// Do not convert its slower asynchronous provider tree into
							// the visible path's 192 ms terminal summary fallback.
							if (!inVisibleViewport
									&& timeline.nearViewportPrefetchActiveId === stableId) {
								previewLoaderIncubationWatchdogStartedAt = 0
								return
							}
							previewLoaderIncubationSuppressed = true
							previewLoaderIncubationSuppressedIdentity =
								previewLoaderIncubationIdentity
							previewLoaderIncubationWatchdogStartedAt = 0
							previewMaterializationAdmitted = false
							richResourceActivationTimer.stop()
							richResourceActivationSettleTimer.stop()
							richResourceActivationQueued = false
							richResourcesActive = false
							// The non-blank branded loading surface is the terminal
							// presentation for this preview generation. It must not keep
							// the room's global token or block later visible rich rows.
							messagePresentationReadyLatched = true
							updateRichResourceActivity()
							completeRichLoaderResumeWhenReady()
						}
						function suspendRichResourcesForScroll() {
							resetPreviewLoaderIncubationWatchdog()
							richResourceActivationTimer.stop()
							richResourceActivationSettleTimer.stop()
							richResourceActivationQueued = false
							richResourcesActive = false
						}
						function cancelNearViewportPrefetchForScroll() {
							timeline.cancelRichLoaderResume(stableId)
							previewMaterializationAdmitted = false
							richLoaderSuspended = true
							resetPreviewLoaderIncubationWatchdog()
							suspendRichResourcesForScroll()
						}
						function completeRichLoaderResumeWhenReady() {
							if (!messageRowReady || !previewPresentationReady)
								return
							if (richResourcesRequired && richResourceActivationReady
									&& !richResourcesActive) {
								scheduleRichResourceActivation()
							}
							// Keep the single global materialization token through the
							// first provider-resource activation and polish window.
							// Releasing it now would let the next RichPreviewCard incubate
							// on the same frame as this card's poster/gallery.
							if (richResourceActivationPending)
								return
							// Keep cached plain ownership until its current outer geometry is committed.
							if (!asyncMessageDelegate.plainDeferredSourceOwned
									|| timeline.scopePresentationPending
									|| asyncMessageDelegate.presentationHeightCommittedHeight > 0)
								timeline.completeDeferredMaterialization(stableId,
									asyncMessageDelegate.deferredMaterializationCleanupGeneration)
							timeline.completeNearViewportPrefetchIfReady(stableId)
							// The complete message/card tree now owns its final layout
							// geometry and its first bounded resource window has settled.
							const rowId = stableId
							const resumeGeneration = timeline.scopePresentationGeneration
							const previewGeneration = previewLoaderIncubationIdentity
							Qt.callLater(function() {
								if (rowId === messageDelegate.stableId
										&& resumeGeneration
											=== timeline.scopePresentationGeneration
										&& !messageDelegate.accessibilityPooled
										&& previewGeneration
											=== messageDelegate.previewLoaderIncubationIdentity
										&& timeline.richLoaderResumeActiveId === rowId
										&& (messageDelegate.currentMessagePresentationReady
											|| messageDelegate.previewLoaderIncubationSuppressed)
										&& messageDelegate.previewPresentationReady
										&& !asyncMessageDelegate.deadlinePresentationFallback)
									timeline.completeRichLoaderResume(rowId)
							})
						}
						readonly property bool inHydrationWindow:
							asyncMessageDelegate.inHydrationWindow
						// ChatMessageFrame is nested inside the asynchronous delegate
						// Loader, so its own y is always zero. The ListView-space outer
						// delegate owns viewport membership.
						readonly property bool inVisibleViewport:
							asyncMessageDelegate.inVisibleViewport
						readonly property bool richContentViewportEligible:
							asyncMessageDelegate.richContentViewportEligible
						readonly property bool stationaryThumbWarmOwned:
							asyncMessageDelegate.stationaryThumbWarmOwned
						property bool richResourcesActive: false
						onRichResourcesActiveChanged: {
							const retainedId = String(stableId || "")
							if (!richResourcesActive && richResourcesRequired
									&& timeline.nearViewportPrefetchReadyId === retainedId)
								timeline.abandonNearViewportPrefetch(retainedId, true)
							if (!richResourcesActive && timeline.richScrollInProgress
									&& root.performanceChatScrollRetainedRichIds[
										retainedId] === true)
								++root.performanceChatScrollRetainedRichResourceDropCount
							if (!richResourcesActive && !inVisibleViewport
									&& !timeline.richScrollInProgress)
								timeline.scheduleNearViewportPrefetch()
						}
						function scheduleRichResourceActivation() {
							if (richResourcesActive || richResourceActivationQueued
									|| !richResourcesRequired
									|| !richResourceActivationReady
									|| accessibilityPooled || !inHydrationWindow
									|| timeline.richScrollInProgress
									|| (timeline.scopePresentationPending
										&& !timeline.scopePresentationFinalizing)
									|| !messageRowReady)
								return
							richResourceActivationQueued = true
							richResourceActivationTimer.restart()
						}
						function updateRichResourceActivity() {
							if (accessibilityPooled || !richResourcesRequired) {
								richResourceActivationTimer.stop()
								richResourceActivationSettleTimer.stop()
								richResourceActivationQueued = false
								richResourceReleaseTimer.stop()
								richResourcesActive = false
								return
							}
							if (inHydrationWindow) {
								richResourceReleaseTimer.stop()
								if (!timeline.richScrollInProgress
										&& (!timeline.scopePresentationPending
											|| (timeline.scopePresentationFinalizing
												&& !richLoaderSuspended)))
									scheduleRichResourceActivation()
							} else if (!timeline.richScrollInProgress) {
								richResourceReleaseTimer.restart()
							}
						}
						function reconcileViewportMembership() {
							updateRichResourceActivity()
							requestPreviewHydrationIfNeeded()
							if (!richContentViewportEligible) {
								const retainStaticPresentation = staticPreviewPresentationReady
								timeline.cancelRichLoaderResume(stableId)
								// Cancel unfinished incubation with its token. A completed
								// static card stays warm for immediate reverse scrolling;
								// pooling/reuse and preview identity changes still retire it.
								if (!retainStaticPresentation)
									previewMaterializationAdmitted = false
								resetPreviewLoaderIncubationWatchdog(true)
								// Scroll-start preserves already-painted visible media. Once
								// the row actually leaves the viewport it is safe to release
								// the poster/player even when it did not own the global token.
								suspendRichResourcesForScroll()
							} else {
								timeline.scheduleRichContentResume()
							}
						}
						Timer {
							id: richResourceActivationTimer
							interval: 16
							repeat: false
							onTriggered: {
								messageDelegate.richResourceActivationQueued = false
								if (messageDelegate.accessibilityPooled
										|| timeline.richScrollInProgress
										|| !messageDelegate.inHydrationWindow
										|| (timeline.scopePresentationPending
											&& !timeline.scopePresentationFinalizing)
										|| !messageDelegate.messageRowReady)
									return
								messageDelegate.richResourcesActive = true
								// A provider tree gets its own polish window before the next
								// complete card is admitted. Attachment/inline-only rows have
								// no nested provider tree to polish, so release the token via
								// completeRichLoaderResumeWhenReady's queued turn; the global
								// resume timer still prevents a same-frame activation burst.
								if (messageDelegate.hasPreviewContent
										&& !messageDelegate.previewLoaderFailed)
									richResourceActivationSettleTimer.restart()
								else {
									const activationStableId = String(messageDelegate.stableId || "")
									const activationGeneration = timeline.scopePresentationGeneration
									Qt.callLater(function() {
										if (activationGeneration === timeline.scopePresentationGeneration
												&& activationStableId === String(messageDelegate.stableId || "")
												&& messageDelegate.accessibilityPooled !== true)
											messageDelegate.completeRichLoaderResumeWhenReady()
									})
								}
							}
						}
						Timer {
							id: richResourceActivationSettleTimer
							interval: 48
							repeat: false
							// Timer.running may remain true while onTriggered executes.
							// Reconcile on the following event-loop turn so the prefetch
							// ready gate observes a genuinely completed polish window.
							onTriggered: {
								const activationStableId = String(messageDelegate.stableId || "")
								const activationGeneration = timeline.scopePresentationGeneration
								Qt.callLater(function() {
									if (activationGeneration === timeline.scopePresentationGeneration
											&& activationStableId === String(messageDelegate.stableId || "")
											&& messageDelegate.accessibilityPooled !== true)
										messageDelegate.completeRichLoaderResumeWhenReady()
								})
							}
						}
						function requestPreviewHydrationIfNeeded(allowDuringFinalization) {
							const pendingAllowed = allowDuringFinalization === true
								&& timeline.scopePresentationFinalizing
							if (backendContentNeedsHydration && !backendHydrationRequested
									&& richContentViewportEligible
									&& (!timeline.scopePresentationPending || pendingAllowed)
									&& !timeline.richScrollInProgress
									&& !timeline.prependAnchorActive
									&& !chatModel.prependPending
									&& root.queuePreviewHydration(stableId, inVisibleViewport,
										hydrationOwnerScopeToken, hydrationOwnerGeneration))
								backendHydrationRequested = true
							if (bodyNeedsHydration && inHydrationWindow
									&& !timeline.richScrollInProgress
									&& !timeline.prependAnchorActive
									&& timeline.prependRichContentActive(stableId,
										richContentViewportEligible))
								chatModel.requestBodyHydration(stableId)
						}
						function requestStationaryThumbHydration(pressEpoch,
								dwellGeneration) {
							const pin = asyncMessageDelegate.stationaryThumbWarmPin
							if (!stationaryThumbWarmOwned || !pin
									|| !timeline.stationaryThumbWarmPinMatchesCurrentScope(
										pin, stableId)
									|| Number(pin.pressEpoch) !== Number(pressEpoch)
									|| Number(pin.dwellGeneration) !== Number(dwellGeneration)
									|| !stationaryThumbWarmGate.owns(stableId,
										Number(pressEpoch), Number(dwellGeneration))
									|| !previewNeedsHydration || accessibilityPooled
									|| !inVisibleViewport || timeline.scopePresentationPending
									|| timeline.prependAnchorActive || chatModel.prependPending)
								return false
							// Attach this stationary generation to an existing single-flight
							// request, or enqueue exactly one backend hydration. A late result may
							// update the model, but only the still-owned generation may reveal it.
							if (!backendHydrationRequested
									&& !root.queuePreviewHydration(stableId, true,
										hydrationOwnerScopeToken, hydrationOwnerGeneration))
								return false
							backendHydrationRequested = true
							stationaryThumbHydrationStableId = String(stableId || "")
							stationaryThumbHydrationScopeToken =
								String(pin.scopeToken || "")
							stationaryThumbHydrationScopeGeneration =
								Number(pin.scopeGeneration)
							stationaryThumbHydrationPressEpoch = Number(pressEpoch)
							stationaryThumbHydrationDwellGeneration = Number(dwellGeneration)
							stationaryThumbHydrationPinSerial = Number(pin.serial)
							return true
						}
						function resumeStationaryThumbWarmAfterHydration() {
							const pin = asyncMessageDelegate.stationaryThumbWarmPin
							if (!timeline.stationaryThumbWarmPinMatchesCurrentScope(
									pin, stableId) || stationaryThumbHydrationStableId
									!== String(stableId || "")
									|| stationaryThumbHydrationScopeToken
										!== String(pin.scopeToken || "")
									|| stationaryThumbHydrationScopeGeneration
										!== Number(pin.scopeGeneration)
									|| Number(pin.pressEpoch)
										!== stationaryThumbHydrationPressEpoch
									|| Number(pin.dwellGeneration)
										!== stationaryThumbHydrationDwellGeneration
									|| Number(pin.serial)
										!== stationaryThumbHydrationPinSerial)
								return false
							let resumed = false
							if (stationaryThumbWarmOwned
									&& stationaryThumbWarmGate.owns(stableId,
										Number(pin.pressEpoch), Number(pin.dwellGeneration))) {
								resumed = admitStationaryThumbWarm(
									Number(pin.pressEpoch), Number(pin.dwellGeneration))
								if (resumed)
									stationaryThumbWarmGate.noteAdmissionProgress(stableId,
										Number(pin.pressEpoch), Number(pin.dwellGeneration))
							} else
								resumed = stationaryThumbWarmGate.reopenAdmission(stableId,
									Number(pin.pressEpoch), Number(pin.dwellGeneration))
							if (resumed) {
								stationaryThumbHydrationStableId = ""
								stationaryThumbHydrationScopeToken = ""
								stationaryThumbHydrationScopeGeneration = -1
								stationaryThumbHydrationPressEpoch = 0
								stationaryThumbHydrationDwellGeneration = 0
								stationaryThumbHydrationPinSerial = 0
							}
							return resumed
						}
						function resumeRichContentAfterScroll(allowDuringFinalization,
								admitPreviewMaterialization) {
							const pendingAllowed = allowDuringFinalization === true
								&& timeline.scopePresentationFinalizing
							let previewAdmissionAccepted = true
							if (!timeline.richScrollInProgress) {
								if (timeline.scopePresentationPending && !pendingAllowed) {
									updateRichResourceActivity()
									return false
								}
								richLoaderSuspended = false
								// Delegates discovered by the retention window stay as
								// geometry-only shells while the gesture is active. Let the
								// ordered resume queue promote one visible message tree per
								// frame instead of allowing every visible Loader binding to
								// activate together on the first idle polish pass.
								if (!logEntry && (richContentViewportEligible
										|| asyncMessageDelegate.plainDeferredSourceOwned
										|| timeline.cachedPlainPresentationEligible(asyncMessageDelegate))) {
									if (!messageRowMaterialized && plainScopeResumeEligible
											&& timeline.scopePresentationPending) {
										diagnosticLoadGeneration = timeline.scopePresentationGeneration
										diagnosticLoadId = String(stableId)
										notePlainLoaderEvent("admitted")
									}
									messageRowMaterialized = true
								}
								if (admitPreviewMaterialization === true
										&& hasPreviewContent) {
									previewAdmissionAccepted = !previewNeedsHydration
										&& !previewLoaderIncubationSuppressed
										&& !previewLoaderFailed
										&& richContentViewportEligible
									if (previewAdmissionAccepted) {
										previewMaterializationAdmitted = true
										armPreviewLoaderIncubationWatchdog()
									}
								}
							}
							updateRichResourceActivity()
							requestPreviewHydrationIfNeeded()
							if (hasPreviewContent && !previewNeedsHydration
									&& !previewMaterializationAdmitted
									&& !previewLoaderFailed
									&& richContentViewportEligible)
								timeline.scheduleRichContentResume()
							return (logEntry || messageRowMaterialized)
								&& previewAdmissionAccepted
						}
						function admitStationaryThumbWarm(pressEpoch, dwellGeneration) {
							const pin = asyncMessageDelegate.stationaryThumbWarmPin
							if (!stationaryThumbWarmOwned || !pin
									|| Number(pin.pressEpoch) !== Number(pressEpoch)
									|| Number(pin.dwellGeneration) !== Number(dwellGeneration)
									|| accessibilityPooled || !inVisibleViewport
									|| !hasPreviewContent
									|| previewTerminalUnavailable)
								return false
							// This path intentionally bypasses only the row/static-still
							// suspension. Provider resources, animation and playback remain
							// owned by richResourcesActive, which stays false during scroll.
							messageRowMaterialized = true
							if (previewNeedsHydration)
								return requestStationaryThumbHydration(
									pressEpoch, dwellGeneration)
							previewMaterializationAdmitted = true
							return true
						}
						Timer {
							id: previewLoaderIncubationWatchdogTimer
							// Twelve 60 Hz frames are ample for the outer RichPreviewCard
							// Loader while remaining inside the 250 ms interactive-tail gate.
							interval: 192
							repeat: false
							onTriggered:
								messageDelegate.recoverPreviewLoaderIncubationTimeout()
						}
						Timer {
							id: richResourceReleaseTimer
							interval: 900
							repeat: false
							onTriggered: {
								if (!messageDelegate.inHydrationWindow
										&& !timeline.richScrollInProgress)
									messageDelegate.richResourcesActive = false
							}
						}
						function primaryRichContentViewportBounds() {
							let content = null
							const row = messageRowLoader.item
							const previewLoader = row ? row.previewLoader : null
							const attachmentLoader = row ? row.attachmentLoader : null
							if (previewLoader && previewLoader.status === Loader.Ready
									&& previewLoader.item)
								content = previewLoader.item
							else if (attachmentLoader && attachmentLoader.status === Loader.Ready
									&& attachmentLoader.item)
								content = attachmentLoader.item
							if (!content || !content.mapToItem || content.height <= 0)
								return null
							const point = content.mapToItem(timeline, 0, 0)
							return { "top": point.y, "bottom": point.y + content.height,
								"height": content.height }
						}
						function itemIntersectsViewport(item) {
							if (!item || accessibilityPooled)
								return false
							// Keep contentY as an explicit dependency; mapToItem supplies the
							// transformed viewport coordinate but does not reliably invalidate
							// bindings for every Flickable movement on all Qt backends.
							const scrollPosition = timeline.contentY
							const point = item.mapToItem(timeline, 0, 0)
							return point.y + item.height > 0.5
								&& point.y < timeline.height - 0.5 && isFinite(scrollPosition)
						}
						function itemContainedInViewport(item) {
							if (!item || accessibilityPooled)
								return false
							const scrollPosition = timeline.contentY
							const point = item.mapToItem(timeline, 0, 0)
							return point.y >= -0.5 && point.y + item.height <= timeline.height + 0.5
								&& isFinite(scrollPosition)
						}
						function handleListViewPooled() {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegatePooledCount
							timeline.cancelRichLoaderResume(stableId)
							closeMessageActionsForReuse()
							accessibilityPooled = true
							richLoaderSuspended = true
							messageRowMaterialized = false
							messagePresentationReadyLatched = false
							resetPreviewLoaderIncubationWatchdog(true)
							previewLoaderTerminalErrorIdentity = ""
							nearViewportPrefetchTerminalIdentity = ""
							richResourceActivationTimer.stop()
							richResourceActivationSettleTimer.stop()
							richResourceActivationQueued = false
							previewMaterializationAdmitted = false
							backendHydrationRequested = false
							stationaryThumbHydrationStableId = ""
							stationaryThumbHydrationScopeToken = ""
							stationaryThumbHydrationScopeGeneration = -1
							stationaryThumbHydrationPressEpoch = 0
							stationaryThumbHydrationDwellGeneration = 0
							stationaryThumbHydrationPinSerial = 0
							previewBackendErrorEpochActive = false
							previewBackendErrorStableId = ""
							updateRichResourceActivity()
						}
						function handleListViewReused() {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegateReusedCount
							closeMessageActionsForReuse()
							resetPreviewLoaderIncubationWatchdog(true)
							previewLoaderTerminalErrorIdentity = ""
							nearViewportPrefetchTerminalIdentity = ""
							previewBackendErrorEpochActive = false
							previewBackendErrorStableId = ""
							accessibilityPooled = false
							richLoaderSuspended = timeline.richScrollInProgress
								|| timeline.scopePresentationPending
							const row = messageRowLoader.item
							if (row && row.previewLoader && row.previewLoader.item)
								row.previewLoader.item.resetForReuse()
							adoptHydrationOwner()
							const reuseStableId = String(stableId || "")
							const reuseGeneration = hydrationOwnerGeneration
							Qt.callLater(function() {
								if (reuseGeneration === timeline.scopePresentationGeneration
										&& reuseStableId === String(messageDelegate.stableId || "")
										&& messageDelegate.accessibilityPooled !== true)
									messageDelegate.resumeRichContentAfterScroll()
							})
						}
						onHasPreviewContentChanged: {
							resetPreviewLoaderIncubationWatchdog(true)
							if (!hasPreviewContent) {
								timeline.cancelRichLoaderResume(stableId)
								suspendRichResourcesForScroll()
								previewMaterializationAdmitted = false
							} else {
								suspendRichResourcesForScroll()
								timeline.scheduleRichContentResume()
							}
						}
						onPreviewNeedsHydrationChanged: {
							if (previewNeedsHydration) {
								// Loading is a new backend epoch after a terminal error;
								// clear the old single-flight latch before enqueueing once.
								backendHydrationRequested = false
								timeline.cancelRichLoaderResume(stableId)
								suspendRichResourcesForScroll()
								resetPreviewLoaderIncubationWatchdog(true)
								previewMaterializationAdmitted = false
								const pin = asyncMessageDelegate.stationaryThumbWarmPin
								if (stationaryThumbWarmOwned && pin)
									admitStationaryThumbWarm(Number(pin.pressEpoch),
										Number(pin.dwellGeneration))
								else
									requestPreviewHydrationIfNeeded()
							} else {
								if (!resumeStationaryThumbWarmAfterHydration())
									timeline.scheduleRichContentResume()
							}
						}
						onPreviewLoaderIncubationIdentityChanged: {
							const presentationIdentityChanged =
								asyncMessageDelegate.reevaluatePresentationHeightIdentity()
							if (presentationIdentityChanged) {
								asyncMessageDelegate.resetViewportProbeHeight()
								asyncMessageDelegate.updateViewportMembership()
							}
							asyncMessageDelegate.schedulePresentationHeightObservation()
							const suppressionChanged =
								previewLoaderIncubationSuppressed
								&& previewLoaderIncubationIdentity
									!== previewLoaderIncubationSuppressedIdentity
							const terminalErrorChanged =
								previewLoaderTerminalErrorIdentity.length > 0
								&& previewLoaderIncubationIdentity
									!== previewLoaderTerminalErrorIdentity
							const prefetchTerminalChanged =
								nearViewportPrefetchTerminalIdentity.length > 0
								&& previewLoaderIncubationIdentity
									!== nearViewportPrefetchTerminalIdentity
							if (!suppressionChanged && !terminalErrorChanged
									&& !prefetchTerminalChanged)
								return
							resetPreviewLoaderIncubationWatchdog(true)
							if (terminalErrorChanged)
								previewLoaderTerminalErrorIdentity = ""
							if (prefetchTerminalChanged)
								nearViewportPrefetchTerminalIdentity = ""
							previewMaterializationAdmitted = false
							timeline.cancelRichLoaderResume(stableId)
							timeline.scheduleRichContentResume()
						}
						onContentNeedsHydrationChanged: {
							const hydrationId = String(stableId || "")
							if (contentNeedsHydration)
								timeline.invalidateNearViewportPrefetchReady(
									hydrationId)
							requestPreviewHydrationIfNeeded()
							if (!contentNeedsHydration) {
								timeline.scheduleScopePresentationFastPath()
								completeRichLoaderResumeWhenReady()
								timeline.scheduleRichContentResume()
								timeline.scheduleNearViewportPrefetch()
							}
						}
						onInHydrationWindowChanged: {
							if (!asyncMessageDelegate.viewportMembershipUpdating)
								reconcileViewportMembership()
						}
						// Visible rows and the single named bounded prefetch row share the
						// serial admission path. Every other cache delegate remains a
						// geometry-only shell.
						onRichContentViewportEligibleChanged: {
							if (!asyncMessageDelegate.viewportMembershipUpdating)
								reconcileViewportMembership()
						}
						onStableIdChanged: {
							adoptHydrationOwner()
							asyncMessageDelegate.resetPresentationHeightIdentity()
							asyncMessageDelegate.resetViewportProbeHeight()
							closeMessageActionsForReuse()
							richResourcesActive = false
							resetPreviewLoaderIncubationWatchdog(true)
							previewLoaderTerminalErrorIdentity = ""
							nearViewportPrefetchTerminalIdentity = ""
							richResourceActivationTimer.stop()
							richResourceActivationSettleTimer.stop()
							richResourceActivationQueued = false
							previewMaterializationAdmitted = false
							backendHydrationRequested = false
							stationaryThumbHydrationStableId = ""
							stationaryThumbHydrationScopeToken = ""
							stationaryThumbHydrationScopeGeneration = -1
							stationaryThumbHydrationPressEpoch = 0
							stationaryThumbHydrationDwellGeneration = 0
							stationaryThumbHydrationPinSerial = 0
							previewBackendErrorEpochActive = false
							previewBackendErrorStableId = ""
							messagePresentationReadyLatched = false
							resumeRichContentAfterScroll()
						}
						Component.onCompleted: {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegateCreatedCount
							richLoaderSuspended = timeline.richScrollInProgress
								|| timeline.scopePresentationPending
							adoptHydrationOwner()
							resumeRichContentAfterScroll()
							timeline.scheduleRichContentResume()
						}
						Connections {
							target: timeline
							function onScopePresentationPendingChanged() {
								if (timeline.scopePresentationPending)
									return
								messageDelegate.adoptHydrationOwner()
								messageDelegate.requestPreviewHydrationIfNeeded()
							}
						}
						// This technical delegate frame must remain ignored for its entire
						// lifetime. Switching it between ignored and exposed while ListView
						// reuses it leaves both promoted and nested children in the Windows
						// accessibility tree.
						Accessible.ignored: accessibilityFrameIgnored
						property string title: asyncMessageDelegate.title
						property string subtitle: asyncMessageDelegate.subtitle
						property string status: asyncMessageDelegate.status
						property string avatarUrl: asyncMessageDelegate.avatarUrl
						property string timestamp: asyncMessageDelegate.timestamp
						property string replyActor: asyncMessageDelegate.replyActor
						property string replySnippet: asyncMessageDelegate.replySnippet
						// QAbstractItemModel list roles arrive in QML as
						// QQmlListModel wrappers, not JavaScript arrays. Normalize
						// once at the message boundary so every downstream
						// component can safely iterate and use Array helpers.
						property var reactions: root.normalizedModelList(asyncMessageDelegate.reactions)
						property var bodySegments: root.normalizedModelList(asyncMessageDelegate.bodySegments)
						property bool bodyHydrationPending: asyncMessageDelegate.bodyHydrationPending
						property var preview: asyncMessageDelegate.preview
						property var attachments: root.normalizedModelList(asyncMessageDelegate.attachments)
						property var source: asyncMessageDelegate.source
						property bool modelStartsGroup: asyncMessageDelegate.modelStartsGroup
						property bool dateBoundary: asyncMessageDelegate.dateBoundary
						property bool deleted: asyncMessageDelegate.deleted
						property bool canReply: asyncMessageDelegate.canReply
						property bool canReact: asyncMessageDelegate.canReact
						property bool canDelete: asyncMessageDelegate.canDelete
						searchCurrent: root.conversationSearchOpen
							&& stableId === String(chatModel.currentMatchStableId || "")
						systemMessage: logEntry || root.isSystemChatMessage(source, avatarUrl, canReply, canReact, own)
						startsGroup: modelStartsGroup
						dateSeparatorLabel: dateBoundary ? root.messageDateLabel(source) : ""
						bodyImplicitHeight: logEntry
							? (activityLogRowLoader.item
								? activityLogRowLoader.item.implicitHeight : 0)
							: messageRowLoader.item
								? messageRowLoader.item.implicitHeight
								: lightweightMessageRowHeight
						// Flickable margins do not constrain a vertical ListView delegate's
						// horizontal scene rect. Reserve the shell-safe trailing inset here as
						// well, otherwise compact metadata extends beneath productSurface's
						// rounded clip even though the timeline itself is correctly sized.
						laneAvailableWidth: Math.max(1, timeline.width
							- timeline.leftMargin - timeline.rightMargin - Theme.space4)
						laneMaximumWidth: root.conversationLaneMaximumWidth
						readonly property bool hasPreviewContent: !!preview && Object.keys(preview).length > 0
						// messagePreviewLoader lives inside messageRowLoader's source Component.
						// Reach it through the row's public alias so these outer readiness bindings
						// never resolve an id outside their component scope during reuse/scroll.
						readonly property var previewPresentationLoader: messageRowLoader.item
							? messageRowLoader.item.previewLoader : null
						readonly property bool previewVisualPresentationPending:
							hasPreviewContent && !!previewPresentationLoader
							&& previewPresentationLoader.status === Loader.Ready
							&& !!previewPresentationLoader.item
							&& previewPresentationLoader.item.visualPresentationPending === true
						readonly property bool previewPrewarmVisualReady: !hasPreviewContent
							|| (!!previewPresentationLoader
								&& previewPresentationLoader.status === Loader.Ready
								&& !!previewPresentationLoader.item
								&& previewPresentationLoader.item.prewarmVisualReady === true)
						readonly property int previewPrewarmProgressRevision:
							previewPresentationLoader
								&& previewPresentationLoader.status === Loader.Ready
								&& previewPresentationLoader.item
							? Number(previewPresentationLoader.item.prewarmProgressRevision || 0) : 0
						readonly property bool previewPaintPending: hasPreviewContent
							&& !previewTerminalUnavailable
							&& (previewNeedsHydration || !previewMaterializationAdmitted
								|| !previewPresentationLoader
								|| previewPresentationLoader.status !== Loader.Ready
								|| previewVisualPresentationPending)
						readonly property bool previewSourceOnly: root.messageBodyIsPreviewSourceOnly(
							bodySegments || [], preview || ({}))
						readonly property bool previewOnlyPaintPending: previewPaintPending
							&& previewSourceOnly && !hasAttachmentContent && !hasReplyContent
						readonly property bool paintedSummary: !accessibilityPooled && opacity > 0
							&& (messageRowMaterialized || asyncMessageDelegate.active)
						readonly property bool paintedLoading: opacity > 0 && hasPreviewContent
							&& !previewTerminalUnavailable
							&& (!previewPresentationLoader
								|| previewPresentationLoader.status !== Loader.Ready
								|| !previewPresentationLoader.item
								|| previewPresentationLoader.item.paintedLoading === true)
						readonly property bool paintedTerminal: opacity > 0 && hasPreviewContent
							&& (previewTerminalUnavailable
								|| (!!previewPresentationLoader
									&& previewPresentationLoader.status === Loader.Ready
									&& !!previewPresentationLoader.item
									&& previewPresentationLoader.item.paintedTerminal === true))
						readonly property bool paintedSurface: opacity > 0 && (!hasPreviewContent
							? messageRowReady
							: !!previewPresentationLoader
								&& previewPresentationLoader.status === Loader.Ready
								&& !!previewPresentationLoader.item
								&& previewPresentationLoader.item.paintedSurface === true)
						readonly property string previewOpenUrl: {
							const value = preview || ({})
							const candidates = [value.url, value.sourceUrl, value.embedUrl,
								value.mediaUrl]
							for (let index = 0; index < candidates.length; ++index) {
								const candidate = String(candidates[index] || "").trim()
								if (/^https?:\/\/\S+$/i.test(candidate))
									return candidate.slice(0, 2048)
							}
							return ""
						}
						readonly property string previewProviderLabel: String(preview
							? (preview.host || preview.providerName || preview.provider || preview.embedKind || "") : "")
							.trim() || qsTr("Link preview")
						readonly property string previewTitleLabel: String(preview
							? (preview.title || previewProviderLabel || previewOpenUrl) : "")
							.trim() || previewProviderLabel
						readonly property bool hasAttachmentContent: !!attachments && attachments.length > 0
						readonly property bool hasReplyContent: replyActor.length > 0 || replySnippet.length > 0
						readonly property bool hasInlineImageContent: root.messageContainsInlineImage(bodySegments)
						readonly property bool messageRowFailed: messageRowLoader.status === Loader.Error
						readonly property bool plainScopeResumeEligible: !logEntry
							&& !contentNeedsHydration && !hasPreviewContent
							&& !hasAttachmentContent && !hasInlineImageContent
						readonly property bool hasMessageActions: canReply || canReact || canDelete
							|| !!source.deliveryCanRetry
						readonly property bool hasReactions: !!reactions && reactions.length > 0
						readonly property bool quickReactionsExpanded: canReact
							&& root.quickReactionMessageId === stableId
						// Message actions are floating chrome. Keeping rich content in this
						// footer solely for its action tray makes attachment cards grow when
						// hover reveals the controls.
						readonly property bool hasEmbeddedFooterContent: hasReactions
							|| hasReplyContent || hasDeliveryStatus
						readonly property real richPresentationMetadataHeight:
							(hasReplyContent ? 44 : 0)
							+ (hasEmbeddedFooterContent ? 34 : 0)
						readonly property bool usesCompactActionOverlay: hasMessageActions
						readonly property int compactActionButtonCount:
							(canReply ? 1 : 0) + (canReact ? 1 : 0)
							+ (!!source.deliveryCanRetry ? 1 : 0) + (canDelete ? 1 : 0)
						readonly property real compactActionTrayWidth: usesCompactActionOverlay
							? compactActionButtonCount * 30 + Theme.space1 : 0
						readonly property real compactActionTextInset: usesCompactActionOverlay
							? compactActionTrayWidth + Theme.space2 : 0
						readonly property bool performancePreviewMaterialized:
							!!messageRowLoader.item
							&& messageRowLoader.item.previewLoader.status === Loader.Ready
							&& !!messageRowLoader.item.previewLoader.item
						readonly property bool performanceAttachmentMaterialized:
							!!messageRowLoader.item
							&& messageRowLoader.item.attachmentLoader.status === Loader.Ready
							&& !!messageRowLoader.item.attachmentLoader.item
						readonly property real previewTargetWidth: hasPreviewContent
							&& messageRowLoader.item
							&& messageRowLoader.item.previewLoader.item
							? messageRowLoader.item.previewLoader.item.targetCardWidth
							: preview && preview.previewSize === "compact" ? 400
							: preview && preview.previewSize === "large" ? 720 : 480
						readonly property real previewPlaceholderWidth: Math.min(previewTargetWidth,
							Math.max(220, laneAvailableWidth - richContentChromeWidth))
						readonly property string previewPlaceholderAspect:
							String(preview ? (preview.embedAspect || "") : "").toLowerCase()
						readonly property string previewPlaceholderKind:
							String(preview ? ((preview.metadata || {}).previewKind
								|| preview.previewKind || "") : "").toLowerCase()
						readonly property bool previewPlaceholderHasMedia: !!preview
							&& ((preview.mediaItems || []).length > 0
								|| String(preview.embedKind || "").length > 0
								|| String(preview.mediaUrl || "").length > 0
								|| previewPlaceholderKind.length > 0)
						readonly property real previewPlaceholderMediaHeight:
							previewPlaceholderAspect === "short"
								? Math.min(520, previewPlaceholderWidth * 16 / 9)
							: previewPlaceholderAspect === "square" ? previewPlaceholderWidth
							: previewPlaceholderAspect === "compact-audio" ? 166
							: previewPlaceholderAspect === "audio" ? 300
							: previewPlaceholderHasMedia
								? Math.min(420, previewPlaceholderWidth * 9 / 16) : 0
						readonly property real previewPlaceholderHeight:
							previewPlaceholderHasMedia
								? previewPlaceholderMediaHeight + 132 : 176
						// Terminal provider/Loader failure is a native link row, not a
						// media-sized error card. Loading still keeps its reserved estimate
						// so admission and scroll anchors remain stable without painting it.
						// Metadata and a local poster remain useful even when the optional
						// provider tree exhausts its incubation budget.
						readonly property bool previewNativeSummary: previewTerminalUnavailable
							&& !previewNeedsHydration && !previewBackendError
						readonly property string previewFallbackThumbnail: timeline.staticPreviewSourceFor(preview)
						readonly property real previewStagingHeight: previewFallbackThumbnail.length > 0 ? 96 : 64
						readonly property bool nativeThumbnailReady: !!messageRowLoader.item
							&& !!messageRowLoader.item.previewHydrationPlaceholderLoader.item
							&& messageRowLoader.item.previewHydrationPlaceholderLoader.item.nativeThumbnailReady
						readonly property real previewReservedHeight:
							previewNeedsHydration || previewTerminalUnavailable
								? previewStagingHeight
								: previewPlaceholderHeight
						readonly property real attachmentPlaceholderHeight: 320
						readonly property real richPresentationContentHeight:
							(hasPreviewContent ? previewReservedHeight : 0)
							+ (hasAttachmentContent ? attachmentPlaceholderHeight : 0)
							+ (hasPreviewContent && hasAttachmentContent
								? Theme.chatContentSpacing : 0)
						readonly property real lightweightMessageRowHeight:
							hasPreviewContent || hasAttachmentContent
								? richPresentationContentHeight + 72
								+ richPresentationMetadataHeight
							: hasInlineImageContent ? 360 + richPresentationMetadataHeight
							: Math.max(48, Math.min(180,
								48 + (bodySegments || []).length * 22
									+ (hasReplyContent ? 44 : 0)
									+ (hasEmbeddedFooterContent ? 34 : 0)))
						// Reactions are compact metadata and wrap naturally in their Flow. Treating
						// one reaction like a media card makes an otherwise short message expand
						// across the complete conversation lane.
						readonly property real richContentChromeWidth: systemMessage
							? horizontalPadding * 2
							: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
						// Replies and delivery metadata are compact text. Only media opts into
						// the wider lane, so sending/failed messages never become broad panels.
						wideContent: hasPreviewContent || hasAttachmentContent || hasInlineImageContent
						readonly property real attachmentTargetWidth: hasAttachmentContent
							? root.preferredAttachmentMessageWidth(attachments, systemMessage) : 0
						readonly property real previewMessageWidth: hasPreviewContent
							? previewTargetWidth + richContentChromeWidth : 0
						preferredWideContentWidth: hasInlineImageContent
							? Theme.chatRichMaximumWidth
							: Math.min(Theme.chatRichMaximumWidth, Math.max(attachmentTargetWidth,
								previewMessageWidth, own ? preferredOwnWidth : preferredIncomingWidth))
						preferredOwnWidth: root.preferredOutgoingMessageWidth(bodySegments, startsGroup,
							replyActor, replySnippet, hasDeliveryStatus, compactActionTextInset)
						preferredIncomingWidth: root.preferredIncomingMessageWidth(bodySegments, startsGroup,
							replyActor, replySnippet, hasDeliveryStatus)
						readonly property string deliveryState: String(source.deliveryState || status || "").trim().toLowerCase()
						readonly property string deliveryLabel: String(source.deliveryLabel || status || "").trim()
						readonly property bool hasDeliveryStatus: deliveryState === "sending"
							|| deliveryState === "failed" || deliveryState === "cancelled"
						width: laneAvailableWidth
						TapHandler {
							enabled: !messageDelegate.logEntry
								&& !messageDelegate.previewOnlyPaintPending
							acceptedButtons: Qt.RightButton
							onTapped: messageDelegate.openMessageActions()
						}
						Loader {
							id: activityLogRowLoader
							anchors.fill: parent
							active: messageDelegate.logEntry
							asynchronous: true
							sourceComponent: Component {
								RowLayout {
									id: activityLogRow
									anchors.fill: parent
									spacing: Theme.space2

									Label {
										id: activityLogTimestamp
										objectName: "activityLogTimestamp"
										Layout.preferredWidth: root.activityLogTimeColumnWidth
										Layout.alignment: Qt.AlignTop
										Layout.topMargin: 1
										textFormat: Text.PlainText
										text: timestamp
										color: Theme.textMuted
										font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
										font.pixelSize: Theme.fontCaption
										font.weight: Font.Medium
										horizontalAlignment: Text.AlignRight
										Accessible.name: qsTr("Time %1").arg(text)
										Accessible.ignored: root.backgroundAccessibilitySuppressed
											|| !messageDelegate.itemContainedInViewport(activityLogTimestamp)
									}
									Item {
										id: activityLogGutter
										objectName: "activityLogGutter"
										Layout.preferredWidth: root.activityLogGutterWidth
										Layout.fillHeight: true
										Accessible.ignored: true

										Rectangle {
											anchors.horizontalCenter: parent.horizontalCenter
											anchors.top: parent.top
											anchors.bottom: parent.bottom
											width: 1
											color: Theme.divider
											Accessible.ignored: true
										}
										Rectangle {
											objectName: "activityLogMarker"
											anchors.horizontalCenter: parent.horizontalCenter
											anchors.top: parent.top
											anchors.topMargin: 5
											width: 7
											height: 7
											radius: width / 2
											color: Theme.accent
											border.color: Theme.withAlpha(Theme.textStrong, 0.18)
											border.width: 1
											Accessible.ignored: true
										}
									}
									RichMessageBody {
										id: activityLogBody
										objectName: "activityLogEvent"
										Layout.fillWidth: true
										Layout.alignment: Qt.AlignTop
										accessibilitySuppressed: root.backgroundAccessibilitySuppressed
											|| !messageDelegate.itemContainedInViewport(activityLogBody)
										segments: messageDelegate.bodySegments || []
										contentIdentity: messageDelegate.stableId
										resourceActive: !messageDelegate.accessibilityPooled
										animationsEnabled: !root.visualFixtureOverrideActive
										hoverEffectsEnabled: !root.visualFixtureOverrideActive
										textColor: Theme.textMain
										pixelSize: Theme.chatBodyFontSize
										onLinkRequested: link => Qt.openUrlExternally(link)
									}
								}
							}
						}
						Loader {
							id: messageRowLoader
							anchors.fill: parent
							active: !messageDelegate.logEntry
								&& messageDelegate.messageRowMaterialized
							asynchronous: true
							visible: active
							onLoaded: {
								messageDelegate.messageRowMaterialized = true
								messageDelegate.notePlainLoaderEvent("loaded")
								timeline.noteScopePresentationRowLoaderLoaded()
								messageDelegate.completeRichLoaderResumeWhenReady()
								if (timeline.topAnchorActive) {
									topAnchorSettleTimer.restart()
									topAnchorFrameTimer.restart()
									Qt.callLater(function() { timeline.restoreTopAnchor() })
								}
							}
							onStatusChanged: {
								if (status === Loader.Loading || status === Loader.Error
										|| status === Loader.Null)
									messageDelegate.notePlainLoaderEvent(status === Loader.Loading
										? "loading" : status === Loader.Error ? "error" : "inactive")
								if (status === Loader.Error) {
									asyncMessageDelegate.releaseDeferredMaterialization()
									if (!timeline.abandonNearViewportPrefetch(
											messageDelegate.stableId, true))
										timeline.completeRichLoaderResume(messageDelegate.stableId)
								}
							}
								sourceComponent: Component {
									RowLayout {
										id: messageRow
										enabled: true
										Accessible.ignored: false
									property alias previewLoader: messagePreviewLoader
									property alias attachmentLoader: messageAttachmentLoader
									property alias previewHydrationPlaceholderLoader:
										messagePreviewHydrationPlaceholderLoader
									property alias actionsMenu: messageActions
									readonly property string previewSurfaceState:
										!messageDelegate.hasPreviewContent ? "not-required"
										: messagePreviewLoader.status === Loader.Error ? "error"
										: messageDelegate.previewNeedsHydration
											|| messageDelegate.previewTerminalUnavailable
											|| !messageDelegate.previewMaterializationAdmitted
											|| messagePreviewLoader.status !== Loader.Ready
											? messagePreviewHydrationPlaceholderLoader.visible
												&& messagePreviewHydrationPlaceholderLoader.status
													=== Loader.Ready
												&& !!messagePreviewHydrationPlaceholderLoader.item
													? "loading" : "blank"
											: messagePreviewLoader.visible
												&& messagePreviewLoader.status === Loader.Ready
												&& !!messagePreviewLoader.item
													// RichPreviewCard paints an opaque themed
													// frame as soon as its Loader is Ready. Its
													// deeper provider tree can still report blank
													// until materialization settles; describe that
													// painted handoff as loading while the separate
													// readiness gate remains strict.
													? messageDelegate.previewLoaderSurfaceNonBlank
														? String(messagePreviewLoader.item
															.performanceEmbedSurfaceState || "blank")
															.toLowerCase()
														: "loading"
													: "blank"
									function openActions() {
										messageActions.targetId = messageDelegate.stableId
										messageActions.targetCanReply = messageDelegate.canReply
										messageActions.targetCanReact = messageDelegate.canReact
										messageActions.targetCanDelete = messageDelegate.canDelete
										messageActions.targetCanRetry =
											!!messageDelegate.source.deliveryCanRetry
										messageActions.open()
									}
									function closeActionsForReuse() {
										if (messageActions.visible)
											messageActions.close()
										messageActions.targetId = ""
									}
									anchors.fill: parent
									spacing: Theme.space2
                            Rectangle {
								Layout.preferredWidth: messageDelegate.systemMessage ? 0 : Theme.avatarMedium
								Layout.preferredHeight: Layout.preferredWidth
                                Layout.alignment: Qt.AlignTop
								visible: !messageDelegate.systemMessage
								radius: width / 2
                                color: Theme.strip
                                clip: true
									opacity: messageDelegate.previewOnlyPaintPending
										? 0 : messageDelegate.startsGroup ? 1 : 0
                                Image {
                                    id: avatarImage
                                    anchors.fill: parent
								source: !messageDelegate.accessibilityPooled
									? root.safeRenderImageSource(avatarUrl) : ""
                                    asynchronous: true
                                    cache: false
                                    sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                                    fillMode: Image.PreserveAspectCrop
                                    visible: avatarImage.status === Image.Ready
                                }
                                Label {
									textFormat: Text.PlainText
                                    anchors.centerIn: parent
                                    visible: avatarUrl.length === 0
                                    text: (title || "S").slice(0, 1).toUpperCase()
                                    color: Theme.textStrong
                                    font.bold: true
									Accessible.ignored: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
								spacing: Theme.chatMetadataSpacing
                                RowLayout {
                                    Layout.fillWidth: true
									spacing: Theme.chatMetadataSpacing
									Layout.leftMargin: messageDelegate.systemMessage ? 0 : Theme.chatBubbleHorizontalPadding
									visible: messageDelegate.startsGroup
										|| messageDelegate.systemMessage
									opacity: messageDelegate.previewOnlyPaintPending ? 0 : 1
									Label {
										id: messageSenderLabel
										Layout.maximumWidth: Math.max(80, parent.width * 0.62)
										textFormat: Text.PlainText
										text: messageDelegate.own ? qsTr("You") : title || qsTr("System")
										color: messageDelegate.systemMessage ? Theme.textMuted
											: messageDelegate.own ? Theme.accent : Theme.textStrong
										font.bold: true
										font.pixelSize: Theme.chatAuthorFontSize
										elide: Text.ElideRight
										Accessible.ignored: root.backgroundAccessibilitySuppressed
											|| messageDelegate.previewOnlyPaintPending
											|| !messageDelegate.itemContainedInViewport(messageSenderLabel)
									}
									Label {
										id: messageTimestampLabel
										textFormat: Text.PlainText
										text: timestamp
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
										visible: timestamp.length > 0
										Accessible.ignored: root.backgroundAccessibilitySuppressed
											|| messageDelegate.previewOnlyPaintPending
											|| !messageDelegate.itemContainedInViewport(messageTimestampLabel)
									}
									Label {
										id: messageStatusLabel
										Layout.maximumWidth: root.narrowShell ? 72 : 120
										textFormat: Text.PlainText
										text: status
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
										visible: status.length > 0
										elide: Text.ElideRight
										Accessible.ignored: root.backgroundAccessibilitySuppressed
											|| messageDelegate.previewOnlyPaintPending
											|| !messageDelegate.itemContainedInViewport(messageStatusLabel)
									}
									Item { Layout.fillWidth: true }
                                }
								Rectangle {
									id: messageBubble
									objectName: "chatMessageBubble"
									readonly property real bubbleHorizontalInset: messageDelegate.systemMessage ? 0 : Theme.chatBubbleHorizontalPadding
									readonly property real bubbleVerticalInset: messageDelegate.systemMessage ? 0 : Theme.chatBubbleVerticalPadding
									Layout.fillWidth: true
									Layout.preferredHeight: bubbleContent.implicitHeight + bubbleVerticalInset * 2
									radius: Theme.innerRadius
									color: "transparent"
									border.width: 0

									ColumnLayout {
										id: bubbleContent
										anchors.fill: parent
										anchors.leftMargin: messageBubble.bubbleHorizontalInset
										anchors.rightMargin: messageBubble.bubbleHorizontalInset
										anchors.topMargin: messageBubble.bubbleVerticalInset
										anchors.bottomMargin: messageBubble.bubbleVerticalInset
										spacing: Theme.chatContentSpacing
                                Rectangle {
                                    id: previewCard
                                    Layout.fillWidth: true
                                    // The accessible text children must stay inside the reply card.
                                    // Reserve the same top and bottom margins that replyColumn uses;
                                    // space3 is smaller than two space2 margins at regular density.
                                    Layout.preferredHeight: replyColumn.implicitHeight + Theme.space2 * 2
                                    visible: replyActor.length > 0 || replySnippet.length > 0
                                    radius: Theme.innerRadius
									color: Theme.chatReplySurface
									Rectangle {
										anchors.left: parent.left
										anchors.top: parent.top
										anchors.bottom: parent.bottom
										width: 2
										radius: 1
										color: Theme.accent
										Accessible.ignored: true
									}
                                    Column {
                                        id: replyColumn
                                        anchors.fill: parent
										anchors.leftMargin: Theme.space3
										anchors.rightMargin: Theme.space2
										anchors.topMargin: Theme.space2
										anchors.bottomMargin: Theme.space2
                                        Label { textFormat: Text.PlainText; text: replyActor; color: Theme.accent; font.pixelSize: Theme.fontCaption; font.bold: true }
                                        Label { width: parent.width; textFormat: Text.PlainText; text: replySnippet; color: Theme.textMuted; font.pixelSize: Theme.fontLabel; elide: Text.ElideRight }
                                    }
                                }
									RichMessageBody {
									id: messageBody
									Layout.fillWidth: true
									visible: !messageDelegate.deleted
										&& !messageDelegate.previewSourceOnly
									accessibilitySuppressed: root.backgroundAccessibilitySuppressed
										|| !messageDelegate.itemContainedInViewport(messageBody)
										segments: messageDelegate.bodySegments || []
										contentIdentity: messageDelegate.stableId
									Layout.rightMargin: messageDelegate.compactActionTextInset
									resourceActive: messageDelegate.richResourcesActive
									animationsEnabled: !root.visualFixtureOverrideActive
									hoverEffectsEnabled: !root.visualFixtureOverrideActive
                                    textColor: Theme.textMain
                                    pixelSize: Theme.chatBodyFontSize
									onLinkRequested: link => Qt.openUrlExternally(link)
								}
								RowLayout {
									Layout.fillWidth: true
									visible: messageDelegate.hasDeliveryStatus
									spacing: Theme.space2
									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: messageDelegate.deliveryLabel.length > 0
											? messageDelegate.deliveryLabel
											: messageDelegate.deliveryState
										color: messageDelegate.deliveryState === "failed" ? Theme.danger
											: messageDelegate.deliveryState === "cancelled" ? Theme.textMuted : Theme.warning
										font.pixelSize: Theme.fontCaption
										font.weight: Font.Medium
										Accessible.name: text
									}
									ModernButton {
										visible: !!messageDelegate.source.deliveryCanRetry
										dense: true
										tone: "retry"
										text: String(messageDelegate.source.deliveryRetryLabel || qsTr("Retry"))
										onClicked: uiCommands.retryMessage(messageDelegate.stableId)
									}
								}
								Label {
									textFormat: Text.PlainText
                                    Layout.fillWidth: true
									visible: messageDelegate.deleted
                                    text: qsTr("Message deleted")
                                    color: Theme.textMuted
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Theme.chatBodyFontSize
                                    font.italic: true
                                }
								Item {
									id: messageAttachmentPresentation
									visible: messageDelegate.hasAttachmentContent
									Layout.fillWidth: true
									Layout.preferredHeight: visible
										? messageAttachmentLoader.status === Loader.Ready
											&& messageAttachmentLoader.item
												? messageAttachmentLoader.item.implicitHeight
												: messageDelegate.attachmentPlaceholderHeight
										: 0

									Loader {
										id: messageAttachmentLoader
										anchors.fill: parent
										active: messageDelegate.hasAttachmentContent
											&& !messageDelegate.richLoaderSuspended
											&& timeline.prependRichContentActive(
												messageDelegate.stableId,
												messageDelegate.richContentViewportEligible)
										// Mixed galleries own ready, loading, and failed tiles independently.
										// Incubate only the gallery tree; never replace all siblings because
										// one attachment is still hydrating.
										asynchronous: true
										visible: active && status === Loader.Ready && !!item
										onLoaded: {
											timeline.markPrependRichRowMaterialized(messageDelegate.stableId)
											if (timeline.topAnchorActive) {
												topAnchorSettleTimer.restart()
												topAnchorFrameTimer.restart()
												Qt.callLater(function() { timeline.restoreTopAnchor() })
											}
											if (root.visualFixtureOverrideActive)
												++root.performanceChatAttachmentLoadedCount
										}
										sourceComponent: Component {
										AttachmentGallery {
											attachments: messageDelegate.attachments || []
											resourceActive: messageDelegate.richResourcesActive
												|| (messageDelegate.previewNeedsHydration
													&& messageDelegate.richContentViewportEligible
													&& !messageDelegate.richLoaderSuspended)
											accessibilitySuppressed: messageDelegate.accessibilitySuppressed
												|| messageDelegate.accessibilityPooled
												|| !messageDelegate.accessibilityViewportVisible
											accessibilityViewportItem: timeline
											accessibilityViewportOffset: timeline.contentY
											animationsEnabled: !root.visualFixtureOverrideActive
												&& !timeline.richScrollInProgress
											hoverEffectsEnabled: !root.visualFixtureOverrideActive
												&& !timeline.richScrollInProgress
												onAttachmentRequested: attachment => root.requestAttachment(
													attachment, messageDelegate.stableId)
												onAttachmentDownloadRequested: attachment => root.downloadAttachment(
													attachment, false)
												onAttachmentRetryRequested: attachment =>
													uiCommands.retryChatAttachmentPreview(
														activeScope.scopeToken, messageDelegate.stableId,
														String(attachment.assetId || attachment.assetID || ""))
												onAttachmentRefreshRequested: root.queuePreviewHydration(
													messageDelegate.stableId, true,
													messageDelegate.hydrationOwnerScopeToken,
													messageDelegate.hydrationOwnerGeneration)
											}
										}
									}

									Rectangle {
										id: messageAttachmentHydrationPlaceholder
										objectName: "messageAttachmentHydrationPlaceholder"
										anchors.fill: parent
										visible: messageAttachmentLoader.active
											&& messageAttachmentLoader.status !== Loader.Ready
										radius: Theme.innerRadius
										color: Theme.strip
										border.color: Theme.divider
										border.width: 1
										clip: true
										Accessible.role: Accessible.StaticText
										Accessible.name: qsTr("Preparing message attachments")
										Accessible.ignored: messageDelegate.accessibilitySuppressed
											|| messageDelegate.accessibilityPooled
											|| !messageDelegate.itemContainedInViewport(
												messageAttachmentHydrationPlaceholder)

										Column {
											anchors.centerIn: parent
											spacing: Theme.space2

											Rectangle {
												anchors.horizontalCenter: parent.horizontalCenter
												width: 34
												height: 34
												radius: Theme.innerRadius
												color: Theme.panel

												ModernIcon {
													anchors.centerIn: parent
													name: "attach"
													size: 18
													color: Theme.textMuted
												}
											}

											Label {
												anchors.horizontalCenter: parent.horizontalCenter
												textFormat: Text.PlainText
												text: qsTr("Preparing attachment gallery")
												color: Theme.textMuted
												font.pixelSize: Theme.fontLabel
												Accessible.ignored: true
											}
										}
									}
								}
								Loader {
									id: messagePreviewHydrationPlaceholderLoader
									// Backend hydration keeps the provider tree unmounted. A compact,
									// bounded native link row still owns the visible loading/error state.
									active: messageDelegate.hasPreviewContent
										&& (messageDelegate.previewNeedsHydration
											|| messageDelegate.previewTerminalUnavailable)
										&& !messageDelegate.richLoaderSuspended
										&& timeline.prependRichContentActive(
											messageDelegate.stableId,
											messageDelegate.richContentViewportEligible)
									visible: messageDelegate.hasPreviewContent
										&& (messageDelegate.previewNeedsHydration
											|| messageDelegate.previewTerminalUnavailable)
									Layout.fillWidth: false
									Layout.preferredHeight: visible
										? messageDelegate.previewStagingHeight : 0
									Layout.alignment: Qt.AlignLeft
									Layout.preferredWidth: Math.min(messageDelegate.previewTargetWidth,
										Math.max(1, messageDelegate.previewPlaceholderWidth))
									Layout.maximumWidth: Layout.preferredWidth
									opacity: active ? 1 : 0
									enabled: active
									Accessible.ignored: !active
									sourceComponent: Component {
										Rectangle {
											objectName: "previewHydrationPlaceholder"
											readonly property bool nativeThumbnailReady: fallbackThumbnail.status === Image.Ready
											implicitWidth: messagePreviewHydrationPlaceholderLoader.Layout.preferredWidth
											implicitHeight: messagePreviewHydrationPlaceholderLoader.Layout.preferredHeight
											radius: Theme.innerRadius
										color: Theme.surfaceRaised
										border.color: Theme.divider
										border.width: 1
										Accessible.role: Accessible.Grouping
										Accessible.name: messageDelegate.previewTitleLabel
										Accessible.description: messageDelegate.previewNativeSummary
											? messageDelegate.previewProviderLabel
											: messageDelegate.previewTerminalUnavailable
											? qsTr("Link preview unavailable")
											: qsTr("Loading link preview")

											RowLayout {
												id: previewHydrationCompactRow
												objectName: "previewHydrationCompactRow"
												anchors.left: parent.left
												anchors.right: parent.right
											anchors.verticalCenter: parent.verticalCenter
												anchors.margins: Theme.space2
												height: Math.max(1, messageDelegate.previewStagingHeight
													- Theme.space2 * 2)
												spacing: Theme.space2

												Image {
													id: fallbackThumbnail
													objectName: "previewNativeThumbnail"
													Layout.preferredWidth: Math.min(128, previewHydrationCompactRow.width * 0.32)
													Layout.preferredHeight: 72
													visible: messageDelegate.previewFallbackThumbnail.length > 0
													source: messageDelegate.previewFallbackThumbnail
													asynchronous: true
													cache: true
													fillMode: Image.PreserveAspectFit
													sourceSize: Qt.size(256, 144)
													Accessible.ignored: true
												}

												ProviderIdentityBadge {
													id: previewHydrationProviderBadge
													visible: messageDelegate.previewFallbackThumbnail.length === 0 && !messageDelegate.previewNativeSummary
													objectName: "previewHydrationProviderBadge"
													Layout.maximumWidth: Math.max(1,
														previewHydrationCompactRow.width * 0.30)
													providerToken: String(messageDelegate.preview.embedKind
														|| messageDelegate.preview.provider
														|| messageDelegate.preview.providerName
														|| messageDelegate.preview.host || "")
													badgeText: messageDelegate.previewProviderLabel
													presentation: "inline"
													labelObjectName: "previewHydrationProviderLabel"
												}

												ColumnLayout {
													objectName: "previewHydrationTextColumn"
													Layout.fillWidth: true
													Layout.minimumWidth: 0
													spacing: 0

													Label {
														objectName: "previewHydrationStateLabel"
														Layout.fillWidth: true
														textFormat: Text.PlainText
														text: messageDelegate.previewNativeSummary
															? messageDelegate.previewProviderLabel
															: messageDelegate.previewTerminalUnavailable
															? qsTr("Preview unavailable")
															: qsTr("Loading link preview")
														color: Theme.textMuted
														font.pixelSize: Theme.fontCaption
														elide: Text.ElideRight
													}

													Label {
														objectName: "previewHydrationTitleLabel"
														Layout.fillWidth: true
														textFormat: Text.PlainText
														text: messageDelegate.previewTitleLabel
														color: Theme.textStrong
														font.pixelSize: Theme.fontLabel
														font.weight: Font.DemiBold
														elide: Text.ElideRight
														maximumLineCount: 1
													}

													Label {
														objectName: "previewHydrationUrlLabel"
														Layout.fillWidth: true
														textFormat: Text.PlainText
														text: messageDelegate.previewOpenUrl
														color: Theme.textMuted
														font.pixelSize: Theme.fontCaption
														elide: Text.ElideRight
														maximumLineCount: 1
													}
												}

												ModernIcon {
													objectName: "previewHydrationStateIcon"
													visible: !messageDelegate.previewNativeSummary
													name: messageDelegate.previewTerminalUnavailable
														? "warning" : "link"
													size: 18
													color: messageDelegate.previewTerminalUnavailable
														? Theme.warning : Theme.textMuted
													Accessible.ignored: true
												}

												ModernIconButton {
													objectName: "previewHydrationOpenButton"
													Layout.alignment: Qt.AlignVCenter
													iconName: "external"
													dense: true
													visible: messageDelegate.previewOpenUrl.length > 0
													enabled: visible
													toolTipText: qsTr("Open link")
													Accessible.role: Accessible.Link
													Accessible.name: qsTr("%1: %2").arg(
														String(messageDelegate.preview.openLabel
															|| qsTr("Open link"))).arg(
															messageDelegate.previewTitleLabel)
													onClicked: Qt.openUrlExternally(
														messageDelegate.previewOpenUrl)
												}
											}
										}
									}
								}
								Loader {
									id: messagePreviewLoader
									active: messageDelegate.hasPreviewContent
										&& !messageDelegate.previewNeedsHydration
									&& !messageDelegate.previewTerminalUnavailable
									&& messageDelegate.previewMaterializationAdmitted
									&& (!messageDelegate.richLoaderSuspended
										|| messageDelegate.stationaryThumbWarmOwned)
										&& timeline.prependRichContentActive(
											messageDelegate.stableId,
											messageDelegate.richContentViewportEligible)
									// The large ListView cache keeps rich-card geometry warm for
									// smooth scrolling. Asynchronous incubation preserves that
									// benefit without letting a page of newly prepended embeds
									// monopolize one UI frame.
									asynchronous: true
									// Only the provider tree is inactive during scroll. Preserve
									// the lightweight row estimate in Layout so pooled delegates
									// cannot move the viewport when rich content resumes.
									// Do not gate effective visibility on the child's reported
									// surface state: RichPreviewCard owns a non-blank loading frame,
									// while previewPresentationReady separately requires its final
									// surface and materialization state.
									visible: active && status === Loader.Ready && !!item
										&& messageDelegate.hasPreviewContent
										&& !messageDelegate.previewNeedsHydration
										&& !messageDelegate.previewTerminalUnavailable
									Layout.fillWidth: false
									// Loader retains its previous implicitHeight briefly while a
									// pooled delegate drops rich content. Bind the layout height to
									// active content explicitly so a plain reused message cannot keep
									// a stale card-sized gap.
									Layout.preferredHeight: visible && item
										? item.implicitHeight : 0
									Layout.alignment: Qt.AlignLeft
									Layout.preferredWidth: Math.min(messageDelegate.previewTargetWidth,
										Math.max(1, messageDelegate.previewPlaceholderWidth))
									Layout.maximumWidth: Layout.preferredWidth
									onActiveChanged: {
										if (!active) {
											messageDelegate.resetPreviewLoaderIncubationWatchdog()
										} else {
											messageDelegate.armPreviewLoaderIncubationWatchdog()
										}
									}
									onLoaded: {
										if (messageDelegate.previewLoaderSurfaceNonBlank)
											messageDelegate.resetPreviewLoaderIncubationWatchdog()
										else
											messageDelegate.armPreviewLoaderIncubationWatchdog()
										timeline.markPrependRichRowMaterialized(messageDelegate.stableId)
										messageDelegate.completeRichLoaderResumeWhenReady()
										if (timeline.topAnchorActive) {
											topAnchorSettleTimer.restart()
											topAnchorFrameTimer.restart()
											Qt.callLater(function() { timeline.restoreTopAnchor() })
										}
										if (root.visualFixtureOverrideActive)
											++root.performanceChatPreviewLoadedCount
									}
									onStatusChanged: {
										if (status === Loader.Error) {
											messageDelegate.resetPreviewLoaderIncubationWatchdog()
											messageDelegate.completeRichLoaderResumeWhenReady()
										} else if (status === Loader.Ready) {
											if (messageDelegate.previewLoaderSurfaceNonBlank)
												messageDelegate.resetPreviewLoaderIncubationWatchdog()
											else
												messageDelegate.armPreviewLoaderIncubationWatchdog()
										} else {
											if (active)
												messageDelegate.armPreviewLoaderIncubationWatchdog()
										}
									}
									sourceComponent: Component {
										RichPreviewCard {
                                    preview: messageDelegate.preview || ({})
									mediaSessionController: mediaSession
									mediaProfileFactory: mediaProfiles
									mediaSessionId: messageDelegate.stableId
									visualMediaFixtureMode: root.visualMediaFixtureMode
									savedSizePreset: root.richPreviewSizePreset(messageDelegate.stableId)
									animationsEnabled: !root.visualFixtureOverrideActive
										&& !timeline.richScrollInProgress
									hoverEffectsEnabled: !root.visualFixtureOverrideActive
										&& !timeline.richScrollInProgress
									previewIdentity: messageDelegate.stableId + "|" + String(messageDelegate.preview
										? (messageDelegate.preview.url || messageDelegate.preview.embedUrl
											|| messageDelegate.preview.mediaUrl || messageDelegate.preview.title || "") : "")
									renderActive: messageDelegate.richResourcesActive
									warmStaticActive:
										messageDelegate.stationaryThumbWarmOwned
									watchTogetherAvailable: !mediaSession.sharedAvailable
                                    onExternalOpenRequested: url => Qt.openUrlExternally(url)
									onImageOpenRequested: (source, title) => root.openManagedPreviewImage(
										source, title, messageDelegate.stableId)
									onImageRefreshRequested: root.queuePreviewHydration(
										messageDelegate.stableId, true,
										messageDelegate.hydrationOwnerScopeToken,
										messageDelegate.hydrationOwnerGeneration)
									onDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
										const sameMedia = mediaSession.active
											&& String(mediaSession.sessionId || "") === messageDelegate.stableId
											&& String(mediaSession.provider || "") === "direct"
											&& String(mediaSession.url || "") === String(url || "")
											&& String(mediaSession.audioUrl || "") === String(audioUrl || "")
											&& String(mediaSession.mediaMime || "").toLowerCase() === String(mime || "").toLowerCase()
											&& String(mediaSession.audioMime || "").toLowerCase() === String(audioMime || "").toLowerCase()
										if (sameMedia) {
											if (mediaSession.detached && typeof mediaSession.attach === "function")
												mediaSession.attach()
											if (mediaSession.playbackControllable)
												mediaSession.play()
											return
										}
										if (mediaSession.openDirectInline(url, mime, audioUrl, audioMime,
												messageDelegate.stableId))
											mediaSession.play()
									}
									onPopoutDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
										if (!mediaSession.detachedPlaybackSupported)
											return
										const sameMedia = mediaSession.active
											&& String(mediaSession.sessionId || "") === messageDelegate.stableId
											&& String(mediaSession.provider || "") === "direct"
											&& String(mediaSession.url || "") === String(url || "")
											&& String(mediaSession.audioUrl || "") === String(audioUrl || "")
											&& String(mediaSession.mediaMime || "").toLowerCase() === String(mime || "").toLowerCase()
											&& String(mediaSession.audioMime || "").toLowerCase() === String(audioMime || "").toLowerCase()
										if (sameMedia) {
											if (!mediaSession.detached)
												mediaSession.detach()
											return
										}
										if (mediaSession.openDirect(url, mime, audioUrl, audioMime,
												messageDelegate.stableId))
											mediaSession.play()
									}
									onInlinePlayRequested: (url, provider) => {
										const sameMedia = mediaSession.active
											&& String(mediaSession.sessionId || "") === messageDelegate.stableId
											&& String(mediaSession.provider || "").toLowerCase() === String(provider || "").toLowerCase()
											&& String(mediaSession.url || "") === String(url || "")
										if (sameMedia) {
											if (mediaSession.detached && typeof mediaSession.attach === "function")
												mediaSession.attach()
											if (mediaSession.playbackControllable)
												mediaSession.play()
											return
										}
										if (mediaSession.openInline(url, provider, messageDelegate.stableId)
												&& mediaSession.playbackControllable)
											mediaSession.play()
									}
									onPopoutPlayRequested: (url, provider) => {
										if (!mediaSession.detachedPlaybackSupported)
											return
										const sameMedia = mediaSession.active
											&& String(mediaSession.sessionId || "") === messageDelegate.stableId
											&& String(mediaSession.provider || "").toLowerCase() === String(provider || "").toLowerCase()
											&& String(mediaSession.url || "") === String(url || "")
										if (sameMedia) {
											if (!mediaSession.detached)
												mediaSession.detach()
											return
										}
										if (mediaSession.open(url, provider, messageDelegate.stableId)
												&& mediaSession.playbackControllable)
											mediaSession.play()
									}
									onWatchTogetherRequested: (url, provider, title, presentationAspect) =>
										root.startWatchTogether(url, provider, title, presentationAspect)
									onSizePresetRequested: preset =>
										root.rememberRichPreviewSizePreset(messageDelegate.stableId, preset)
										}
									}
                                }
							Connections {
								target: messagePreviewLoader.item
								enabled: !!target
								ignoreUnknownSignals: true
								function onMaterializationSettledChanged() {
									messageDelegate.completeRichLoaderResumeWhenReady()
								}
								function onPrewarmVisualReadyChanged() {
									messageDelegate.completeRichLoaderResumeWhenReady()
								}
							}
								RowLayout {
									id: messageFooter
									objectName: "chatMessageFooter"
									readonly property bool quickReactionsExpanded: messageDelegate.canReact
										&& root.quickReactionMessageId === messageDelegate.stableId
									Layout.fillWidth: true
									Layout.rightMargin: messageDelegate.compactActionTextInset
									visible: messageDelegate.hasEmbeddedFooterContent
										&& !messageDelegate.previewOnlyPaintPending
									spacing: Theme.space2

									Flow {
										id: messageReactionFlow
										Layout.fillWidth: true
										Layout.alignment: Qt.AlignVCenter
										spacing: Theme.chatMetadataSpacing
										visible: messageDelegate.hasReactions
										Repeater {
											model: reactions || []
											delegate: Button {
												id: reactionButton
												required property var modelData
												implicitWidth: contentItem.implicitWidth + Theme.space2 * 2
												implicitHeight: Math.max(30, Theme.avatarSmall)
												enabled: messageDelegate.canReact && (modelData.emoji || "").length > 0
												hoverEnabled: true
												activeFocusOnTab: true
												focusPolicy: Qt.StrongFocus
												Accessible.name: qsTr("%1 reaction, %2").arg(modelData.emoji || "")
													.arg(modelData.count || 0)
												background: Rectangle {
													radius: reactionButton.implicitHeight / 2
													color: !reactionButton.enabled ? Theme.panel
														: reactionButton.modelData.selfReacted ? Theme.selected
														: reactionButton.down ? Theme.accentSubtle
														: reactionButton.hovered ? Theme.surfaceHover : Theme.strip
													border.color: reactionButton.activeFocus ? Theme.focus : Theme.divider
													border.width: reactionButton.activeFocus ? Theme.focusRingWidth : 1
												}
												contentItem: Row {
													spacing: Theme.space1
													Label {
														objectName: "messageReactionEmoji"
														anchors.verticalCenter: parent.verticalCenter
														textFormat: Text.PlainText
														text: String(reactionButton.modelData.emoji || "")
														color: reactionButton.enabled ? Theme.textStrong : Theme.textMuted
														font.family: Qt.platform.os === "windows" ? "Segoe UI Emoji" : ""
														font.pixelSize: 17
														verticalAlignment: Text.AlignVCenter
													}
													Label {
														objectName: "messageReactionCount"
														anchors.verticalCenter: parent.verticalCenter
														textFormat: Text.PlainText
														text: String(Number(reactionButton.modelData.count || 0))
														color: reactionButton.enabled ? Theme.textMain : Theme.textMuted
														font.pixelSize: Theme.fontCaption
														font.weight: Font.DemiBold
														verticalAlignment: Text.AlignVCenter
													}
												}
												onClicked: uiCommands.toggleMessageReaction(messageDelegate.stableId,
													modelData.emoji)
											}
										}
									}

									Item {
										Layout.fillWidth: true
										visible: !messageReactionFlow.visible
									}
											Label {
												textFormat: Text.PlainText
												text: [timestamp, status].filter(value => String(value).length > 0).join(" · ")
												visible: text.length > 0 && ((messageDelegate.own
													&& !messageDelegate.systemMessage)
													|| (!messageDelegate.startsGroup && messageDelegate.hovered))
												color: Theme.chatMetadata
												font.pixelSize: Theme.fontCaption
												Accessible.name: text
											}
									Rectangle {
										id: messageActionTray
										objectName: "chatMessageActionTray"
										Layout.alignment: Qt.AlignVCenter
										Layout.preferredWidth: messageActionButtons.implicitWidth + Theme.space1
										Layout.preferredHeight: 34
										visible: messageDelegate.hasMessageActions
											&& (messageDelegate.hovered || messageFooter.quickReactionsExpanded)
											&& !messageDelegate.usesCompactActionOverlay
										radius: height / 2
										color: Theme.strip
										border.color: Theme.divider
										border.width: 1

										Row {
											id: messageActionButtons
											anchors.centerIn: parent
											spacing: 0
											ModernIconButton {
												objectName: "messageReplyButton"
												visible: messageDelegate.canReply
													&& (messageDelegate.hovered || messageFooter.quickReactionsExpanded)
												dense: true
												iconName: "reply"
												text: qsTr("Reply")
												Accessible.name: text
												onClicked: uiCommands.replyToMessage(messageDelegate.stableId)
											}
											ModernIconButton {
												id: messageReactButton
												objectName: "messageReactButton"
												visible: messageDelegate.canReact
													&& (messageDelegate.hovered
														|| root.quickReactionMessageId === messageDelegate.stableId)
												dense: true
												iconName: "reaction"
												selected: messageFooter.quickReactionsExpanded
												text: selected ? qsTr("Close reactions") : qsTr("Add reaction")
												Accessible.name: text
												ToolTip.visible: hovered
												ToolTip.text: text
												onClicked: root.toggleQuickReactions(messageDelegate.stableId,
													chatModel.presentationModel.rowForStableId(
														messageDelegate.stableId),
													messageReactButton,
													messageDelegate.reactions || [])
											}
											ModernIconButton {
												objectName: "messageRetryButton"
												visible: !!messageDelegate.source.deliveryCanRetry
													&& (messageDelegate.hovered || messageFooter.quickReactionsExpanded)
												dense: true
												iconName: "retry"
												text: qsTr("Retry")
												Accessible.name: text
												onClicked: uiCommands.retryMessage(messageDelegate.stableId)
											}
											ModernIconButton {
												objectName: "messageDeleteButton"
												visible: messageDelegate.canDelete
													&& (messageDelegate.hovered || messageFooter.quickReactionsExpanded)
												dense: true
												iconName: "delete"
												text: qsTr("Delete")
												tone: "danger"
												Accessible.name: text
												onClicked: uiCommands.deleteMessage(messageDelegate.stableId)
											}
											ModernMenu {
												id: messageActions
												objectName: "messageActionsMenu-" + messageDelegate.stableId
												accessibleName: qsTr("Message actions")
												property string targetId: ""
												property bool targetCanReply: false
												property bool targetCanReact: false
												property bool targetCanDelete: false
												property bool targetCanRetry: false
												onClosed: targetId = ""
												PayloadMenuItem {
													payload: ({ "kind": "action", "id": "message.reply",
														"label": qsTr("Reply"), "icon": "reply" })
													visible: messageActions.targetCanReply
													height: visible ? rowImplicitHeight : 0
													onActionRequested: uiCommands.replyToMessage(messageActions.targetId)
												}
												PayloadMenuItem {
													payload: ({ "kind": "action", "id": "message.react",
													"label": qsTr("Add reaction"), "icon": "reaction" })
													visible: messageActions.targetCanReact
													height: visible ? rowImplicitHeight : 0
												onActionRequested: root.showQuickReactions(messageActions.targetId, -1,
													messageDelegate.usesCompactActionOverlay
														? compactMessageReactButton : messageReactButton,
													messageDelegate.reactions || [])
												}
												PayloadMenuItem {
													payload: ({ "kind": "action", "id": "message.retry",
														"label": qsTr("Retry"), "icon": "retry" })
													visible: messageActions.targetCanRetry
													height: visible ? rowImplicitHeight : 0
													onActionRequested: uiCommands.retryMessage(messageActions.targetId)
												}
												PayloadMenuItem {
													payload: ({ "kind": "action", "id": "message.delete",
														"label": qsTr("Delete"), "icon": "delete", "tone": "danger" })
													visible: messageActions.targetCanDelete
													height: visible ? rowImplicitHeight : 0
													onActionRequested: uiCommands.deleteMessage(messageActions.targetId)
													}
												}
											}
										}
									}
									}
								Rectangle {
									id: compactMessageActionTray
									objectName: "chatCompactMessageActionTray"
									anchors.bottom: parent.bottom
									anchors.right: parent.right
									anchors.bottomMargin: Theme.space1
									anchors.rightMargin: Theme.space1
									width: messageDelegate.compactActionTrayWidth
									height: 34
								visible: messageDelegate.usesCompactActionOverlay
									&& !messageDelegate.previewOnlyPaintPending
									&& (messageDelegate.hovered || messageDelegate.quickReactionsExpanded)
									radius: height / 2
									color: Theme.strip
									border.color: Theme.divider
									border.width: 1
									z: 2

									Row {
										id: compactMessageActionButtons
										anchors.centerIn: parent
										spacing: 0

										ModernIconButton {
											objectName: "compactMessageReplyButton"
											visible: messageDelegate.canReply
											dense: true
											iconName: "reply"
											text: qsTr("Reply")
											Accessible.name: text
											onClicked: uiCommands.replyToMessage(messageDelegate.stableId)
										}
										ModernIconButton {
											id: compactMessageReactButton
											objectName: "compactMessageReactButton"
											visible: messageDelegate.canReact
											dense: true
											iconName: "reaction"
											selected: messageDelegate.quickReactionsExpanded
											text: selected ? qsTr("Close reactions") : qsTr("Add reaction")
											Accessible.name: text
											ToolTip.visible: hovered
											ToolTip.text: text
										onClicked: root.toggleQuickReactions(messageDelegate.stableId,
											chatModel.presentationModel.rowForStableId(
												messageDelegate.stableId),
												compactMessageReactButton,
												messageDelegate.reactions || [])
										}
										ModernIconButton {
											objectName: "compactMessageRetryButton"
											visible: !!messageDelegate.source.deliveryCanRetry
											dense: true
											iconName: "retry"
											text: qsTr("Retry")
											Accessible.name: text
											onClicked: uiCommands.retryMessage(messageDelegate.stableId)
										}
										ModernIconButton {
											objectName: "compactMessageDeleteButton"
											visible: messageDelegate.canDelete
											dense: true
											iconName: "delete"
											text: qsTr("Delete")
											tone: "danger"
											Accessible.name: text
											onClicked: uiCommands.deleteMessage(messageDelegate.stableId)
										}
									}
								}
								}
								}
							}
						}
							}
						}
									}
								}
                    }
					}
					Rectangle {
						id: emptyConversationState
						objectName: "emptyConversationState"
						// The v2 timeline hides this view, so the state moves over to it.
						parent: chatTimelineV2Enabled ? conversationViewLoader : timeline
						anchors.centerIn: parent
						width: Math.max(1, Math.min(380, parent.width
							- root.timelineHorizontalMargin * 2 - Theme.space4 * 2))
						height: emptyConversationContent.implicitHeight + Theme.space5 * 2
						readonly property int conversationRowCount: chatTimelineV2Enabled
							? chatTimelineV2.count : chatModel.count
						readonly property bool presentationContentVisible: chatTimelineV2Enabled
							|| timeline.scopePresentationContentVisible
						// A bounded branded shell owns every replacement frame. Internal
						// materialization/finalization phases are never named or exposed.
						readonly property bool visualLoading: !activeScope.activity
							&& (activeScope.loading
								|| !presentationContentVisible)
						visible: (activeScope.activity ? conversationRowCount === 0
							: conversationRowCount === 0
								|| !presentationContentVisible)
							&& !root.connectionTransitionActive
							&& !root.connectionFailureActive
						z: 5
						radius: Theme.innerRadius
						color: Theme.surfaceRaised
						border.color: Theme.surfaceBorder
						Accessible.role: Accessible.Pane
						Accessible.name: emptyConversationTitle.text
						Accessible.description: emptyConversationDetail.text

						Column {
							id: emptyConversationContent
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.verticalCenter: parent.verticalCenter
							anchors.leftMargin: Theme.space5
							anchors.rightMargin: Theme.space5
							spacing: Theme.space2
							ModernBusyIndicator {
								objectName: "emptyConversationBusyIndicator"
								anchors.horizontalCenter: parent.horizontalCenter
								visible: emptyConversationState.visualLoading
								running: visible
								animated: !root.visualFixtureOverrideActive
								Accessible.name: qsTr("Loading conversation")
							}
							Label {
								id: emptyConversationTitle
								width: parent.width
								textFormat: Text.PlainText
								text: emptyConversationState.visualLoading
									? (String(activeScope.label || "").trim().length > 0
										? String(activeScope.label) : qsTr("Loading conversation"))
									: activeScope.activity ? qsTr("No server log entries yet")
									: !clientSession.connected ? qsTr("Connect to Mumble")
									: activeScope.canSend ? qsTr("This conversation is quiet")
									: qsTr("Choose a conversation")
								color: Theme.textStrong
								font.pixelSize: Theme.fontTitle
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								wrapMode: Text.Wrap
							}
							Label {
								id: emptyConversationDetail
								width: parent.width
								textFormat: Text.PlainText
								text: emptyConversationState.visualLoading
									? qsTr("Loading recent messages…")
									: activeScope.activity
										? qsTr("New Murmur server-log entries will appear here live.")
									: !clientSession.connected ? qsTr("Open the server browser to load rooms and messages.")
									: activeScope.canSend ? qsTr("Be the first to write in %1.").arg(activeScope.label)
									: qsTr("Select a text room, voice room, or direct message to get started.")
								color: Theme.textMuted
								font.pixelSize: Theme.chatBodyFontSize
								horizontalAlignment: Text.AlignHCenter
								wrapMode: Text.Wrap
							}
							ModernButton {
								id: emptyConversationConnectButton
								objectName: "emptyConversationConnectButton"
								anchors.horizontalCenter: parent.horizontalCenter
								visible: !clientSession.connected && clientSession.canConnect
								text: qsTr("Choose a server")
								tone: "accent"
								Accessible.description: qsTr("Open the server browser")
								onClicked: uiCommands.invokeAppAction("server.connect", {})
							}
						}
					}
                }

				Loader {
					id: conversationViewLoader
					objectName: "conversationViewLoader"
					Layout.fillWidth: true
					Layout.fillHeight: true
					active: chatTimelineV2Enabled
					visible: active
					sourceComponent: ConversationView {
						host: root
						leftMargin: root.timelineHorizontalMargin
						rightMargin: root.timelineHorizontalMargin
						topMargin: activeScope.activity ? Theme.space1 : root.timelineVerticalMargin
						bottomMargin: Math.max(Theme.space2, root.timelineVerticalMargin - jumpToLatestRow.height)
					}
				}

				// Keep this navigation action in its own layout row. A floating pill on
				// top of the viewport obscured the newest visible message, especially in
				// compact layouts and immediately after history prepends. The row keeps
				// its height while the button is hidden: resizing the timeline each time
				// the reader left or reached the bottom shifted the conversation.
				Item {
					id: jumpToLatestRow
					objectName: "jumpToLatestRow"
					readonly property var conversationView: conversationViewLoader.item
					readonly property bool reserved: conversationView ? conversationView.count > 0
						: timeline.count > 0
					Layout.fillWidth: true
					Layout.preferredHeight: reserved
						? jumpToLatestButton.implicitHeight + jumpToLatestButton.timelineTopMargin
							+ jumpToLatestButton.timelineBottomMargin
						: 0

					ModernButton {
						id: jumpToLatestButton
						objectName: "jumpToLatestButton"
						readonly property real timelineTopMargin: Theme.space1
						readonly property real timelineBottomMargin: Theme.space1
						anchors.right: parent.right
						anchors.rightMargin: Theme.space4
						y: timelineTopMargin
						visible: jumpToLatestRow.reserved && (jumpToLatestRow.conversationView
							? !jumpToLatestRow.conversationView.atLatest
							: !timeline.stickToBottom && !timeline.scopePresentationPending)
						dense: true
						tone: timeline.pendingTailMessageCount > 0 ? "accent" : "neutral"
						text: timeline.pendingTailMessageCount > 0
							? qsTr("%n new message(s)", "", timeline.pendingTailMessageCount)
							: qsTr("Jump to latest")
						Accessible.description: timeline.pendingTailMessageCount > 0
							? qsTr("Scroll to %n new message(s)", "", timeline.pendingTailMessageCount)
							: qsTr("Scroll to the newest message")
						onClicked: {
							if (jumpToLatestRow.conversationView) {
								jumpToLatestRow.conversationView.jumpToLatest()
								return
							}
							timeline.releasePrependAnchor()
							timeline.releaseViewportAnchor()
							timeline.releaseTopAnchor()
							timeline.invalidateBottomFollow()
							timeline.stickToBottom = true
							timeline.requestBottomFollow()
						}
					}
				}

				Loader {
					id: aboveComposerStonksTickerLoader
					Layout.fillWidth: true
					Layout.preferredHeight: active && item && item.visible ? item.implicitHeight : 0
					active: root.stonksTickerEnabled
						&& root.stonksTickerPlacement === "aboveComposer"
					asynchronous: true
					sourceComponent: Component {
						StonksHeader {
							objectName: "stonksTickerAboveComposer"
							width: parent ? parent.width : implicitWidth
							stonks: root.stonksTickerState
							tickerBannerEnabled: true
							tickerDirection: root.stonksTickerDirection
							tickerSpeed: root.stonksTickerSpeed
							scopeToken: activeScope.scopeToken
							scopeLabel: activeScope.label
							narrowLayout: root.narrowShell
							docked: true
							onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
						}
					}
				}

				Rectangle {
					id: composerSurface
                    Layout.fillWidth: true
					visible: !activeScope.activity
					enabled: visible
					Accessible.ignored: !visible
					// Reply copy, attachments, autocomplete, and the input row all keep
					// their own minimum height. Reserve the complete reply row plus the
					// intervening layout gap so compact combinations never overflow the
					// composer's semantic or visual bounds.
					readonly property int outerMargin: root.narrowShell ? Theme.space2 : 14
					readonly property bool showTargetContext: String(activeScope.scopeToken || "").indexOf("dm:") === 0
					readonly property int targetRowHeight: showTargetContext ? 16 : 0
					readonly property int inputRowMinimumHeight: 30
					readonly property int baseHeight: Math.max(
						outerMargin * 2 + Theme.space2 * 2 + targetRowHeight
							+ Theme.chatMetadataSpacing + inputRowMinimumHeight,
						Theme.railFooterHeight)
					Layout.preferredHeight: visible ? (activeScope.hasPendingReply ? baseHeight + 44 : baseHeight)
						+ Math.min(3, Math.max(0, composerInput.lineCount - 1)) * Theme.composerLineHeight
						+ (composer.attachments.count > 0 ? 58 : 0)
						+ (composer.autocompleteItems.length > 0 ? 34 : 0) : 0
					color: Theme.chatCanvas
					border.width: 0
					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.top: parent.top
						height: 1
						color: Theme.divider
					}
					DropArea {
						id: composerFileDropArea
						objectName: "composerFileDropArea"
						anchors.fill: parent
						z: 100
						enabled: activeScope.canAttachImages && composer.canSend && !composer.sending
						onEntered: drag => {
							if (drag.hasUrls
								&& root.localAttachmentUrls(drag.urls, !!activeScope.canAttachFiles).length > 0)
								drag.accept(Qt.CopyAction)
							else
								drag.accepted = false
						}
						onDropped: drop => {
							const localUrls = drop.hasUrls
								? root.localAttachmentUrls(drop.urls, !!activeScope.canAttachFiles) : []
							if (localUrls.length === 0) {
								drop.accepted = false
								return
							}
							composer.addUrls(localUrls)
							drop.accept(Qt.CopyAction)
						}
						Rectangle {
							objectName: "composerFileDropOverlay"
							anchors.fill: parent
							anchors.margins: Theme.space2
							visible: composerFileDropArea.containsDrag
							radius: Theme.innerRadius
							color: Theme.panel
							border.color: Theme.accent
							border.width: 2
							Accessible.ignored: true
							Row {
								anchors.centerIn: parent
								spacing: Theme.space2
								ModernIcon {
									anchors.verticalCenter: parent.verticalCenter
									name: "attach"
									size: 20
									color: Theme.accent
								}
								Label {
									anchors.verticalCenter: parent.verticalCenter
									textFormat: Text.PlainText
									text: activeScope.canAttachFiles
										? qsTr("Drop files to attach") : qsTr("Drop images to attach")
									color: Theme.textStrong
									font.pixelSize: Theme.chatBodyFontSize
									font.weight: Font.DemiBold
					}
				}
            }
					}
                    RowLayout {
						anchors.top: parent.top
						anchors.bottom: parent.bottom
						anchors.horizontalCenter: parent.horizontalCenter
						anchors.topMargin: composerSurface.outerMargin
						anchors.bottomMargin: anchors.topMargin
						width: Math.max(1, Math.min(root.conversationLaneMaximumWidth,
							parent.width - (root.narrowShell ? Theme.space2 : 14) * 2))
						spacing: 0
                        Rectangle {
							id: composerInputSurface
							objectName: "composerInputSurface"
							readonly property bool focusWithin: composerInput.activeFocus
								|| composerAttachButton.activeFocus || composerSendButton.activeFocus
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.innerRadius
							color: "transparent"
							border.width: 0
							Rectangle {
								x: 0
								y: Theme.elevationLowOffset
								width: parent.width
								height: parent.height
								radius: parent.radius
								color: Theme.composerShadow
								Accessible.ignored: true
							}
							Rectangle {
								anchors.fill: parent
								radius: parent.radius
								color: Theme.composerBackground
								border.color: composerInputSurface.focusWithin
									? Theme.composerFocusBorder : Theme.composerBorder
								border.width: composerInputSurface.focusWithin ? Theme.focusRingWidth : 1
								Accessible.ignored: true
								Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
							}
                            ColumnLayout {
                                anchors.fill: parent
								anchors.margins: Theme.space2
								spacing: Theme.chatMetadataSpacing
								RowLayout {
									id: composerTargetRow
									objectName: "composerTargetRow"
									visible: composerSurface.showTargetContext
									Layout.fillWidth: true
									Layout.preferredHeight: composerSurface.targetRowHeight
									spacing: Theme.chatMetadataSpacing
									ModernIcon {
										Layout.alignment: Qt.AlignVCenter
										name: "send"
										size: 12
										color: Theme.accent
									}
									Label {
										objectName: "composerTargetLabel"
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: qsTr("To: %1").arg(activeScope.label)
										color: Theme.textStrong
										font.pixelSize: Theme.fontCaption
										font.weight: Font.DemiBold
										elide: Text.ElideRight
									}
									Label {
										objectName: "composerTargetKindLabel"
										Layout.alignment: Qt.AlignVCenter
										textFormat: Text.PlainText
										text: activeScope.kindLabel
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
									}
								}
								Rectangle {
									id: composerReplySurface
									objectName: "composerReplySurface"
                                    Layout.fillWidth: true
									Layout.preferredHeight: 42
                                    visible: activeScope.hasPendingReply
									radius: Theme.innerRadius
									color: Theme.chatReplySurface
									Rectangle {
										anchors.left: parent.left
										anchors.top: parent.top
										anchors.bottom: parent.bottom
										width: 3
										radius: 2
										color: Theme.accent
									}
									RowLayout {
										anchors.fill: parent
										anchors.leftMargin: Theme.space2
									spacing: Theme.chatMetadataSpacing
										ModernIcon {
											Layout.alignment: Qt.AlignVCenter
											name: "reply"
											size: 16
											color: Theme.accent
										}
										ColumnLayout {
											Layout.fillWidth: true
											spacing: 0
											Label {
												Layout.fillWidth: true
												textFormat: Text.PlainText
												text: qsTr("Replying to %1").arg(activeScope.replyActor)
												color: Theme.accent
												font.pixelSize: Theme.fontCaption
												font.weight: Font.DemiBold
												elide: Text.ElideRight
											}
											Label {
												Layout.fillWidth: true
												textFormat: Text.PlainText
												text: activeScope.replySnippet
												color: Theme.chatMetadata
												font.pixelSize: Theme.fontLabel
												elide: Text.ElideRight
											}
										}
										ModernIconButton {
											id: composerReplyCloseButton
											objectName: "composerReplyCloseButton"
											dense: true
											iconName: "close"
											text: qsTr("Cancel reply")
											Accessible.name: text
											onClicked: uiCommands.cancelPendingReply()
										}
									}
                                }
                                ListView {
									id: attachmentStrip
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: composer.attachments.count > 0 ? 52 : 0
                                    visible: composer.attachments.count > 0
                                    orientation: ListView.Horizontal
                                    spacing: 6
                                    model: composer.attachments
                                    delegate: Rectangle {
										id: draftAttachment
                                        required property string stableId
                                        required property string thumbnailUrl
                                        required property string fileName
										required property string kind
										required property string mime
										required property var byteSize
										required property string status
										required property real progress
										required property string error
										readonly property string normalizedStatus: String(status || "").trim().toLowerCase()
										readonly property real boundedProgress: Math.max(0, Math.min(1,
											isFinite(Number(progress)) ? Number(progress) : 0))
										readonly property int progressPercent: Math.round(boundedProgress * 100)
										readonly property bool preparing: normalizedStatus === "loading"
											|| normalizedStatus === "queued" || normalizedStatus === "preparing"
										readonly property bool uploading: normalizedStatus === "uploading"
										readonly property bool failed: normalizedStatus === "failed"
										readonly property bool imageDraft: String(kind).toLowerCase() === "image"
											|| String(mime).toLowerCase().indexOf("image/") === 0
										readonly property bool mediaDraft: String(kind).toLowerCase() === "video"
											|| String(kind).toLowerCase() === "audio"
											|| /^(video|audio)\//i.test(String(mime))
										readonly property string metadataText: root.attachmentMetadata(kind, mime, byteSize)
										readonly property string statusText: failed
											? (String(error || "").trim().slice(0, 512) || qsTr("Attachment failed"))
											: uploading ? qsTr("Uploading · %1%").arg(progressPercent)
											: preparing ? qsTr("Preparing attachment…") : qsTr("Ready to send")
										readonly property string visibleStatusText: normalizedStatus === "ready"
											&& metadataText.length > 0 ? metadataText : statusText
										readonly property string accessibleStatusText: normalizedStatus === "ready"
											&& metadataText.length > 0 ? statusText + " · " + metadataText : statusText
										objectName: "composerDraftAttachment_" + stableId
										width: Math.min(210, Math.max(156, attachmentStrip.width - 4))
										height: 48; radius: 7; color: Theme.strip; border.color: Theme.divider
										Accessible.role: Accessible.Grouping
										Accessible.name: fileName || qsTr("Attachment")
										Accessible.description: accessibleStatusText
                                        RowLayout { anchors.fill: parent; anchors.margins: 4
											Rectangle {
												Layout.preferredWidth: 38
												Layout.preferredHeight: 38
												radius: 5
												color: Theme.panel
												clip: true
												ModernIcon {
													anchors.centerIn: parent
													name: draftAttachment.mediaDraft ? "play" : "attach"
													size: 18
													color: Theme.textMuted
												}
												Image {
													anchors.fill: parent
													source: draftAttachment.imageDraft
														? root.safeRenderImageSource(draftAttachment.thumbnailUrl) : ""
													asynchronous: true
													cache: false
													fillMode: Image.PreserveAspectCrop
												}
											}
											ColumnLayout {
												Layout.fillWidth: true
												spacing: 1
												Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: fileName; color: Theme.textMain; elide: Text.ElideMiddle; font.pixelSize: 9; Accessible.ignored: true }
												Label {
													id: draftAttachmentMetadata
													objectName: "composerDraftAttachmentStatus_" + draftAttachment.stableId
													Layout.fillWidth: true
													textFormat: Text.PlainText
													visible: text.length > 0
													text: draftAttachment.visibleStatusText
													color: draftAttachment.failed ? Theme.danger
														: draftAttachment.uploading ? Theme.accent : Theme.textMuted
													elide: Text.ElideRight
													font.pixelSize: 8
													Accessible.ignored: true
												}
												ModernProgressBar {
													objectName: "composerDraftAttachmentProgress_" + draftAttachment.stableId
													Layout.fillWidth: true
													Layout.preferredHeight: 4
													visible: draftAttachment.uploading
													from: 0
													to: 1
													value: draftAttachment.boundedProgress
													trackHeight: 3
													animated: !root.visualFixtureOverrideActive
													Accessible.name: qsTr("Uploading %1").arg(draftAttachment.fileName
														|| qsTr("attachment"))
													Accessible.description: draftAttachment.statusText
												}
											}
											ModernBusyIndicator {
												objectName: "composerDraftAttachmentBusy_" + draftAttachment.stableId
												Layout.preferredWidth: 16
												Layout.preferredHeight: 16
												visible: draftAttachment.preparing
												running: visible
												Accessible.name: draftAttachment.statusText
											}
											ModernIconButton {
												objectName: "composerDraftAttachmentRetry_" + draftAttachment.stableId
												dense: true
												visible: draftAttachment.failed
												enabled: !composer.sending
												iconName: "retry"
												onClicked: composer.retryAttachment(draftAttachment.stableId)
												Accessible.name: qsTr("Retry %1").arg(draftAttachment.fileName
													|| qsTr("attachment"))
											}
											ModernIconButton {
												objectName: "composerDraftAttachmentRemove_" + draftAttachment.stableId
												dense: true
												enabled: !composer.sending
												iconName: "close"
												onClicked: composer.removeAttachment(draftAttachment.stableId)
												Accessible.name: qsTr("Remove %1").arg(draftAttachment.fileName
													|| qsTr("attachment"))
											}
                                        }
                                    }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: composer.autocompleteItems.length > 0 ? 30 : 0
                                    visible: composer.autocompleteItems.length > 0
                                    orientation: ListView.Horizontal
                                    spacing: 4
                                    model: composer.autocompleteItems
									delegate: ModernButton {
										required property var modelData
										dense: true
                                        text: modelData.label || ""
                                        onClicked: composer.complete(modelData.value || "")
                                    }
                                }
								RowLayout {
									id: composerInputRow
									Layout.fillWidth: true
									Layout.fillHeight: true
									Layout.minimumHeight: composerSurface.inputRowMinimumHeight
									spacing: Theme.space1
									ModernIconButton {
										id: composerAttachButton
										objectName: "composerAttachButton"
										Layout.alignment: Qt.AlignBottom
										visible: activeScope.canAttachImages
										enabled: activeScope.canSend && !composer.sending
										dense: true
										iconName: "attach"
										text: activeScope.canAttachFiles ? qsTr("Attach files") : qsTr("Attach images")
										Accessible.name: text
										ToolTip.visible: hovered
										ToolTip.text: text
										onClicked: uiCommands.chooseAttachment()
									}
							TextArea {
								id: composerInput
								objectName: "visualFixtureComposer"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
								text: composer.text
								cursorDelegate: composerTextCursorDelegate
                                onTextChanged: if (composer.text !== text) composer.text = text
                                placeholderText: activeScope.composerPlaceholder.length > 0
                                                 ? activeScope.composerPlaceholder
                                                 : qsTr("Connect to send messages")
                                enabled: composer.canSend && !composer.sending
								color: composerInput.enabled ? Theme.textMain : Theme.textMuted
								placeholderTextColor: Theme.textMuted
								Accessible.ignored: root.backgroundAccessibilitySuppressed
								Accessible.name: activeScope.composerPlaceholder.length > 0
												 ? activeScope.composerPlaceholder
												 : qsTr("Message composer")
								Accessible.description: qsTr("Sending to %1. %2")
									.arg(activeScope.label).arg(activeScope.composerHint)
								Accessible.focusable: true
								Accessible.focused: activeFocus
                                background: null
                                wrapMode: TextEdit.Wrap
								Keys.priority: Keys.BeforeItem
								Keys.onShortcutOverride: event => {
									if (event.matches(StandardKey.Paste))
										event.accepted = true
								}
								Keys.onPressed: event => {
									if (event.matches(StandardKey.Paste)) {
										const attached = activeScope.canAttachImages && composer.pasteFromClipboard()
										if (!attached)
											composerInput.paste()
										event.accepted = true
										return
									}
									const returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
									if (!returnKey || (event.modifiers & Qt.ShiftModifier)
											|| composerInput.inputMethodComposing)
										return
									if (text.trim().length > 0 || composer.attachments.count > 0)
										composer.send()
									// Empty Return is deliberately a no-op. Shift+Return is the
									// only newline gesture, regardless of whether the draft came
									// from typing, paste, drop, or an attachment.
									event.accepted = true
								}
                            }
									ModernIconButton {
										id: composerSendButton
										objectName: "composerSendButton"
										Layout.alignment: Qt.AlignBottom
										dense: true
										iconName: "send"
										text: qsTr("Send")
										tone: "success"
										selected: enabled
										enabled: composer.canSend && !composer.sending
											&& (composer.text.trim().length > 0 || composer.attachments.count > 0)
										Accessible.name: text
										ToolTip.visible: hovered
										ToolTip.text: text
										onClicked: composer.send()
									}
								}
                            }
                        }
                    }
                }

            }

		NavigationRail {
				id: desktopNavigationRail
                Layout.preferredWidth: 310
                Layout.fillHeight: true
			visible: !root.compactNavigation
			alignedHeaderHeight: shellHeader.height
			alignedFooterHeight: Math.max(Theme.railFooterHeight, composerSurface.height)
			accessibilitySuppressed: root.backgroundAccessibilitySuppressed
				navigationModel: root.navigationRailModel
				selectionState: root.navigationSelectionState
				uiCommands: root.navigationCommands
				clientSession: root.navigationSession
				visualFixtureMode: root.visualFixtureOverrideActive
				settingsEnabled: root.settingsActionEnabled
				stonksEnabled: root.stonksShortcutEnabled
				serverMenuOpen: appMenuPopup.visible
				activeScopeMenuToken: (roomMenuPopup.visible || textRoomMenuPopup.visible)
					? root.contextScopeToken : ""
				activeParticipantMenuKey: participantMenuPopup.visible
					? root.contextParticipantRowKey : ""
				onSelectionCommitted: navigationDrawer.close()
				onScopeMenuRequested: (scopeToken, kind, actions, anchorPoint) =>
					root.openScopeMenu(scopeToken, kind, actions, anchorPoint)
				onParticipantMenuRequested: (sessionId, actions, anchorPoint, entryKind, scopeToken, rowKey) =>
					root.openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken, rowKey)
				onSettingsRequested: root.requestSettings()
				onStonksRequested: uiCommands.invokeAppAction("server.stonksPortfolio", {})
				onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
				onServerMenuRequested: anchorPoint => root.openApplicationMenu(anchorPoint)
			}
		}

		// Exercise the exact production delegate graph once while the shell is
		// still hidden. ChatMessageFrame alone does not construct the authored-row
		// layout inside timeline.delegate, leaving its first real incubation able
		// to block a connected GUI heartbeat. This one local row has no preview,
		// attachment, hydration, or accessible surface and performs no I/O.
		ListModel {
			id: chatMessageDelegateWarmupModel
			dynamicRoles: true
			Component.onCompleted: append({
				"stableId": "__delegate_warmup__",
				"deferredPresentation": false,
				"deferredPresentationCompact": false,
				"deferredPresentationHeight": 58,
				"own": false,
				"title": "Warmup",
				"subtitle": "",
				"status": "",
				"avatarUrl": "",
				"timestamp": "",
				"replyActor": "",
				"replySnippet": "",
				"reactions": [],
				"bodySegments": [],
				"bodyHydrationPending": false,
				"preview": ({}),
				"attachments": [],
				"source": ({}),
				"modelStartsGroup": true,
				"dateBoundary": false,
				"deleted": false,
				"canReply": false,
				"canReact": false,
				"canDelete": false
			})
		}
		ListView {
			id: chatMessageDelegateWarmupList
			objectName: "chatMessageDelegateWarmupList"
			width: Math.max(1, timeline.width)
			height: timeline.deferredMessageRowHeight
			visible: false
			interactive: false
			reuseItems: false
			cacheBuffer: height
			model: chatMessageDelegateWarmupModel
			delegate: timeline.delegate
			Accessible.ignored: true
		}

		Loader {
			id: windowTopStonksTickerLoader
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			height: active && item && item.visible ? item.implicitHeight : 0
			active: root.stonksTickerEnabled && root.stonksTickerPlacement === "windowTop"
			asynchronous: true
			z: 2
			sourceComponent: Component {
				StonksHeader {
					objectName: "stonksTickerWindowTop"
					width: parent ? parent.width : implicitWidth
					stonks: root.stonksTickerState
					tickerBannerEnabled: true
					tickerDirection: root.stonksTickerDirection
					tickerSpeed: root.stonksTickerSpeed
					scopeToken: activeScope.scopeToken
					scopeLabel: activeScope.label
					narrowLayout: root.narrowShell
					docked: true
					dockedDividerEdge: "bottom"
					onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
				}
			}
		}

		Loader {
			id: bottomStonksTickerLoader
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			height: active && item && item.visible ? item.implicitHeight : 0
			active: root.stonksTickerEnabled && root.stonksTickerPlacement === "bottom"
			asynchronous: true
			z: 2
			sourceComponent: Component {
				StonksHeader {
					objectName: "stonksTickerBottom"
					width: parent ? parent.width : implicitWidth
					stonks: root.stonksTickerState
					tickerBannerEnabled: true
					tickerDirection: root.stonksTickerDirection
					tickerSpeed: root.stonksTickerSpeed
					scopeToken: activeScope.scopeToken
					scopeLabel: activeScope.label
					narrowLayout: root.narrowShell
					docked: true
					dockedDividerEdge: "top"
					onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
				}
			}
		}

		MouseArea {
			id: directMessageTrayDismissLayer
			objectName: "directMessageTrayDismissLayer"
			anchors.fill: parent
			visible: directMessageTrayLoader.active
			enabled: visible
			z: 39
			Accessible.ignored: true
			onClicked: directMessages.setTrayOpen(false)
		}

		Loader {
			id: directMessageTrayLoader
			objectName: "directMessageTrayLoader"
			anchors.right: parent.right
			// Keep this overlay outside RowLayout/ColumnLayout ownership. On a
			// right-hand desktop rail, reserve its width so the tray remains over
			// the chat surface; a left rail or compact drawer needs no reservation.
			anchors.rightMargin: Theme.space4 + (desktopNavigationRail.visible
				&& Theme.railSide !== "left" ? desktopNavigationRail.width : 0)
			anchors.bottom: parent.bottom
			anchors.bottomMargin: Theme.space4 + bottomStonksTickerLoader.height
			active: directMessages && directMessages.trayOpen
			visible: active && status === Loader.Ready
			z: 40
			sourceComponent: Component {
				DirectMessageTray { controller: directMessages }
			}
		}
	}

	Loader {
		id: directMessageWindowLoader
		objectName: "directMessageWindowLoader"
		active: directMessages && directMessages.conversationOpen
		sourceComponent: Component {
			DirectMessageWindow {
				controller: directMessages
				parentWindow: root
				visualFixtureMode: root.visualFixtureOverrideActive
				savedPreviewSizePresets: root.richPreviewSizePresets
				onManagedImageOpenRequested: (source, title, messageId) =>
					root.openManagedPreviewImage(source, title, messageId)
				onManagedAttachmentOpenRequested: (attachment, messageId) =>
					root.openAttachment(attachment, "", messageId,
						directMessages ? directMessages.timelineModel : null)
				onWatchTogetherRequested: (url, provider, title, presentationAspect) =>
					root.startWatchTogether(url, provider, title, presentationAspect)
				onPreviewSizePresetRequested: (preferenceKey, preset) =>
					root.rememberRichPreviewSizePreset(preferenceKey, preset)
			}
		}
	}
}
