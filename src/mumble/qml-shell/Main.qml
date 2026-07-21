import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
	id: root
	onClosing: close => close.accepted = false
    property var attachmentViewerPayload: null
    property var pendingPreviewHydrationIds: ({})
	property var richPreviewSizePresets: ({})
    property bool pendingPreviewHydrationHighPriority: false
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
	property bool performanceChatScrollRunning: false
	property int performanceChatScrollCompletedSteps: 0
	property int performanceChatScrollTargetSteps: 0
	property int performanceChatDelegateCreatedCount: 0
	property int performanceChatDelegatePooledCount: 0
	property int performanceChatDelegateReusedCount: 0
	property int performanceChatPreviewLoadedCount: 0
	property int performanceChatAttachmentLoadedCount: 0
	property var performanceChatScrollDiagnosticsBefore: ({})
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
		&& stonksTickerState.profileShortcutVisible !== false
	readonly property bool stonksTickerEnabled: stonksTickerState.tickerBannerEnabled === true
	readonly property string stonksTickerPlacement: String(stonksTickerState.tickerPlacement || "bottom")
	readonly property string stonksTickerDirection: String(stonksTickerState.tickerDirection || "left")
	readonly property string stonksTickerSpeed: String(stonksTickerState.tickerSpeed || "normal")
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
	readonly property bool mediaSessionWindowRequested: mediaSession.active && mediaSession.detached
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

	function startWatchTogether(url, provider, title) {
		if (mediaSession && !mediaSession.sharedAvailable)
			return mediaSession.startShared(url, provider, title)
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
		target: chatModel
		function onDataChanged() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
		function onModelReset() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
		function onRowsInserted() { Qt.callLater(function() { root.refreshOpenAttachmentFromModel() }) }
	}

	Component.onCompleted: Qt.callLater(function() { root.maybeMarkMotdSeen() })

	function messageStartsGroup(row, source, title) {
		if (row <= 0 || !source || source.system)
			return true
		const previous = chatModel.get(row - 1)
		const previousSource = previous && previous.source ? previous.source : ({})
		if (previousSource.system)
			return true
		const actorKey = String(source.actorKey || title || "")
		const previousActorKey = String(previousSource.actorKey || (previous ? previous.title : "") || "")
		if (actorKey !== previousActorKey || !!source.own !== !!previousSource.own)
			return true
		const createdAt = Number(source.createdAtMs || 0)
		const previousCreatedAt = Number(previousSource.createdAtMs || 0)
		return createdAt > 0 && previousCreatedAt > 0 && createdAt - previousCreatedAt > 300000
	}

	function messageDateSeparator(row, source) {
		const createdAt = Number(source && source.createdAtMs ? source.createdAtMs : 0)
		if (createdAt <= 0)
			return ""
		const current = new Date(createdAt)
		if (isNaN(current.getTime()))
			return ""
		if (row > 0) {
			const previous = chatModel.get(row - 1)
			const previousSource = previous && previous.source ? previous.source : ({})
			const previousAt = Number(previousSource.createdAtMs || 0)
			if (previousAt > 0) {
				const previousDate = new Date(previousAt)
				if (current.getFullYear() === previousDate.getFullYear()
						&& current.getMonth() === previousDate.getMonth()
						&& current.getDate() === previousDate.getDate())
					return ""
			}
		}
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

	function estimatedMessageTextWidth(segments, replyActor, replySnippet) {
		let longestLine = 0
		for (const segment of (segments || [])) {
			const text = String(segment && segment.text !== undefined ? segment.text : "")
			for (const line of text.split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		for (const replyText of [replyActor, replySnippet]) {
			for (const line of String(replyText || "").split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		return longestLine * Theme.fontBody * 0.58
	}

	function messageContainsInlineImage(segments) {
		for (const segment of (segments || [])) {
			if (segment && String(segment.kind || "").toLowerCase() === "image")
				return true
		}
		return false
	}

	function preferredAttachmentMessageWidth(attachments, ownOrSystem) {
		const values = attachments || []
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
			deliveryVisible) {
		// This only chooses a comfortable bubble width. RichMessageBody remains
		// responsible for exact text measurement and wrapping.
		const textEstimate = estimatedMessageTextWidth(segments, replyActor, replySnippet)
			+ Theme.chatBubbleHorizontalPadding * 2
		const metadataFloor = deliveryVisible ? 300 : 176
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

	function openAttachment(attachment, titleOverride, hydrationMessageId) {
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
		const rowIndex = chatModel.rowForStableId(messageId)
		if (rowIndex < 0)
			return false
		const row = chatModel.get(rowIndex)
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
			return openAttachment(candidate, current.alt || current.name, messageId)
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
        previewHydrationTimer.stop()
    }

	function queuePreviewHydration(messageId, highPriority) {
		if (root.visualFixtureOverrideActive)
			return false
		const normalized = String(messageId === undefined || messageId === null ? "" : messageId).trim()
        if (!/^[1-9][0-9]*$/.test(normalized) || String(activeScope.scopeToken || "").length === 0)
            return false
        // Keep protocol uint64 IDs as decimal strings. JavaScript numbers lose
        // precision above 2^53 and would hydrate a different message.
        if (normalized.length > 20)
            return false
        pendingPreviewHydrationIds[normalized] = normalized
        pendingPreviewHydrationHighPriority = pendingPreviewHydrationHighPriority || !!highPriority
        if (!previewHydrationTimer.running)
            previewHydrationTimer.start()
        return true
    }

    function flushPreviewHydrationQueue() {
        const keys = Object.keys(pendingPreviewHydrationIds)
        if (keys.length === 0)
            return
        const batch = []
        for (let index = 0; index < keys.length && batch.length < 32; ++index) {
            const key = keys[index]
            batch.push(pendingPreviewHydrationIds[key])
            delete pendingPreviewHydrationIds[key]
        }
        const highPriority = pendingPreviewHydrationHighPriority
        pendingPreviewHydrationHighPriority = false
        uiCommands.requestPreviewHydration(String(activeScope.scopeToken || ""), batch, highPriority)
        if (Object.keys(pendingPreviewHydrationIds).length > 0)
            previewHydrationTimer.start()
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
				else
					timeline.forceActiveFocus(Qt.OtherFocusReason)
			})
		}
	}

	function revealConversationSearchMatch(row, stableId) {
		if (!root.conversationSearchOpen || row < 0 || String(stableId || "").length === 0)
			return
		timeline.releasePrependAnchor()
		timeline.stickToBottom = false
		timeline.currentIndex = row
		timeline.positionViewAtIndex(row, ListView.Center)
	}

	function positionVisualFixtureTimelineAt(stableId) {
		if (!root.visualFixtureOverrideActive)
			return false
		const row = chatModel.rowForStableId(String(stableId || ""))
		if (row < 0)
			return false
		timeline.releasePrependAnchor()
		bottomFollowTimer.stop()
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
		let normalized = inputVariant
		if (alias === "self" || alias === "profile")
			normalized = "profile"
		else if (alias === "member" || alias === "participant")
			normalized = "participant"
		else if (alias === "chat" || alias === "background" || alias === "chatbackground")
			normalized = "chatBackground"
		else if (alias === "textroom" || alias === "textroomreal")
			normalized = "textRoom"
		else if (alias === "appserver" || alias === "app-server" || alias === "server-submenu")
			normalized = "appServer"
		else if (alias === "app" || alias === "room" || alias === "message")
			normalized = alias
		closeProductMenus()
		automationMenuVariant = normalized
		automationMenuSurface = null
		automationMenuFocusItem = null
		let handled = true
		let menu = null
		if (normalized === "app" || normalized === "appServer") {
			menu = appMenuPopup
			openMenuAt(menu, Qt.point(root.width - menu.width - 24, 72))
			if (normalized === "appServer") {
				let firstServerAction = null
				for (let index = 0; index < appMenuPopup.count; ++index) {
					const candidate = appMenuPopup.itemAt(index)
					if (candidate && candidate.payload
							&& String(candidate.payload.id || "").substring(0, 7) === "server.") {
						firstServerAction = candidate
						break
					}
				}
				handled = !!firstServerAction
				if (handled)
					automationMenuFocusItem = firstServerAction
			}
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

	function preparePerformanceChatScrollWorkload(stepCount) {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		const requestedSteps = Math.max(1, Math.floor(Number(stepCount) || 0))
		// Each step must move the real ListView by at least two logical pixels so
		// the scene-graph cannot legitimately coalesce it into a no-op frame.
		if (timeline.count < 20 || maximumY - minimumY < requestedSteps * 2)
			return { "started": false, "reason": qsTr("The chat timeline is not scrollable."),
				"count": timeline.count, "contentHeight": timeline.contentHeight, "viewportHeight": timeline.height }
		performanceChatScrollStartY = timeline.contentY
		// Model a sustained wheel/trackpad gesture, not a teleport through the
		// entire history. Traversing thousands of pixels per frame measures cold
		// delegate construction rather than steady-state chat scrolling and can
		// leave Windows presentation-throttled between mutations. Sixteen logical
		// pixels per presented input mirrors a sustained high-resolution wheel or
		// trackpad gesture; the 40-step release workload still crosses enough rows
		// to exercise the bounded delegate cache and reuse path.
		const travelDistance = Math.min(maximumY - minimumY, requestedSteps * 16)
		performanceChatScrollTargetY = Math.abs(performanceChatScrollStartY - minimumY) > 8
			? Math.max(minimumY, performanceChatScrollStartY - travelDistance)
			: Math.min(maximumY, performanceChatScrollStartY + travelDistance)
		bottomFollowTimer.stop()
		performanceChatScrollDiagnosticsBefore = timelineDelegateDiagnostics()
		performanceChatScrollCompletedSteps = 0
		performanceChatScrollTargetSteps = requestedSteps
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
			"currentY": timeline.contentY, "completedSteps": performanceChatScrollCompletedSteps }
	}

	function completePerformanceChatScrollWorkload() {
		performanceChatScrollRunning = false
	}

	function performanceChatFixtureState() {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		const firstVisible = timeline.firstVisibleMessageDelegate()
		return { "count": timeline.count, "contentHeight": timeline.contentHeight,
			"viewportHeight": timeline.height, "originY": minimumY, "maximumY": maximumY,
			"contentY": timeline.contentY,
			"firstVisibleId": firstVisible ? String(firstVisible.stableId || "") : "",
			"delegateDiagnostics": timelineDelegateDiagnostics(),
			"settled": !bottomFollowTimer.running && !timeline.scopeResetPending
				&& !timeline.scopePresentationPending && !timeline.scopePresentationFinalizing
				&& !timeline.restoringBottom && !timeline.prependAnchorActive
				&& !performanceChatScrollRunning,
			"scrollable": timeline.count === 96 && maximumY - minimumY >= 8 }
	}

	function timelinePresentationState() {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		const firstVisible = timeline.firstVisibleMessageDelegate()
		const completedAt = timeline.scopePresentationCompletedAt > 0
			? timeline.scopePresentationCompletedAt : Date.now()
		return {
			"scopeToken": String(activeScope.scopeToken || ""),
			"scopeLabel": String(activeScope.label || ""),
			"count": timeline.count,
			"contentHeight": timeline.contentHeight,
			"viewportHeight": timeline.height,
			"contentY": timeline.contentY,
			"maximumY": maximumY,
			"firstVisibleId": firstVisible ? String(firstVisible.stableId || "") : "",
			"presentationPending": timeline.scopePresentationPending,
			"presentationFinalizing": timeline.scopePresentationFinalizing,
			"observationActive": timeline.scopePresentationObservationActive,
			"forcedByDeadline": timeline.scopePresentationForcedByDeadline,
			"generation": timeline.scopePresentationGeneration,
			"mutationCount": timeline.scopePresentationMutationCount,
			"tailCorrectionCount": timeline.scopePresentationTailCorrectionCount,
			"exposedHeightChangeCount": timeline.scopePresentationExposedHeightChangeCount,
			"exposedTailCorrectionCount": timeline.scopePresentationExposedTailCorrectionCount,
			"exposedTailTravel": timeline.scopePresentationExposedTailTravel,
			"pendingHydrationCount": timeline.pendingScopeHydrationCount(),
			"durationMs": timeline.scopePresentationStartedAt > 0
				? Math.max(0, completedAt - timeline.scopePresentationStartedAt) : 0,
			"settled": !timeline.scopePresentationPending && !timeline.scopePresentationFinalizing
				&& !timeline.scopePresentationObservationActive && !bottomFollowTimer.running
				&& !timeline.restoringBottom && !timeline.prependAnchorActive
		}
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
			"attachmentsLoaded": performanceChatAttachmentLoadedCount
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
			root.closeQuickReactions()
			root.clearPreviewHydrationQueue()
			timeline.beginScopeChange()
			if (root.conversationSearchOpen)
				root.closeConversationSearch(false)
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
			onClosing: root.attachmentViewerPayload = null
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
				uiCommands.requestOlderMessages()
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
			model: operationModel
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
		anchors.bottomMargin: composerSurface.height + bottomStonksTicker.height + 22
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
			anchors.topMargin: windowTopStonksTicker.height
			anchors.bottomMargin: bottomStonksTicker.height
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
							text: root.narrowShell ? qsTr("Share")
								: String(share.primaryLabel || qsTr("Screen share"))
							tone: String(share.primaryTone || "neutral")
							enabled: share.primaryEnabled === undefined || !!share.primaryEnabled
							Accessible.ignored: root.backgroundAccessibilitySuppressed
							Accessible.name: String(share.primaryLabel || qsTr("Screen share"))
							Accessible.description: String(share.primaryHint || "")
							onClicked: uiCommands.invokeScopeAction(activeScope.scopeToken,
								String(share.primaryActionId || ""))
						}
						ModernButton {
							id: motdToggleButton
							objectName: "motdToggleButton"
							visible: !!clientSession.hasMotd
							dense: true
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
							contentItem: RowLayout {
								spacing: 6
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

				StonksHeader {
					id: topStonksTicker
					objectName: "stonksTickerTop"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					stonks: root.stonksTickerState
					tickerBannerEnabled: root.stonksTickerEnabled && root.stonksTickerPlacement === "top"
					tickerDirection: root.stonksTickerDirection
					tickerSpeed: root.stonksTickerSpeed
					scopeToken: activeScope.scopeToken
					scopeLabel: activeScope.label
					narrowLayout: root.narrowShell
					docked: true
					onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
				}

				ConversationSearchBar {
					id: conversationSearchBar
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					visible: root.conversationSearchOpen
					timelineModel: chatModel
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
						? (root.activeScopeHasScreenShare ? 60 : root.compactNavigation ? 68 : 88)
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

				WatchTogetherBanner {
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					session: mediaSession
					participantModel: root.navigationParticipantModel
				}

                ListView {
                    id: timeline
					objectName: "chatTimeline"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: chatModel
                    clip: true
					interactive: !scopePresentationPending
					Accessible.role: Accessible.List
					Accessible.name: qsTr("Conversation messages")
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
					topMargin: root.timelineVerticalMargin
					reuseItems: !scopeReuseResetActive
					boundsBehavior: Flickable.StopAtBounds
					ScrollBar.vertical: ModernScrollBar {
						objectName: "chatTimelineScrollBar"
						enabled: !timeline.scopePresentationPending
						onPressedChanged: {
							if (pressed) {
								bottomFollowTimer.stop()
								timeline.stickToBottom = false
								timeline.releasePrependAnchor()
							} else {
								timeline.stickToBottom = timeline.isNearBottom()
								timeline.requestBottomFollow()
							}
						}
					}
					MiddleDragScrollHandler {
						targetFlickable: timeline
						horizontalEnabled: false
						onScrollingStarted: {
							bottomFollowTimer.stop()
							timeline.stickToBottom = false
							timeline.releasePrependAnchor()
						}
						onScrollingEnded: {
							timeline.stickToBottom = timeline.isNearBottom()
							timeline.requestBottomFollow()
						}
					}
					// Build a small amount of chat content outside the viewport while
					// the chat surface is otherwise idle. Complex rich-message delegates
					// should not have to be constructed on the first frame they scroll in.
					// Keep one bounded viewport of delegates warm. This is enough to
					// construct occasional rich rows outside the presentation-critical
					// scroll frame without scaling cache memory with history length.
					cacheBuffer: Math.max(256, Math.min(720, height))
					property string prependAnchorId: ""
					property real prependAnchorOffset: 0
					property bool prependAnchorActive: false
					property bool prependMutationInProgress: false
					property bool restoringPrependAnchor: false
					property int prependAnchorRetries: 0
					property int prependAnchorCorrectionFrames: 0
					property bool stickToBottom: true
					property bool followTailAfterInsert: false
					property int pendingTailInsertCount: 0
					property int pendingTailMessageCount: 0
					property bool restoringBottom: false
					property bool scopeResetPending: false
					// Rich text, preview cards, and attachments refine delegate heights after
					// a scope arrives. Keep that churn behind one stable loading surface and
					// expose the fully anchored tail in a single frame.
					property bool scopePresentationPending: false
					property bool scopePresentationFinalizing: false
					property bool scopePresentationObservationActive: false
					property bool scopePresentationForcedByDeadline: false
					property int scopePresentationGeneration: 0
					property int scopePresentationMutationCount: 0
					property int scopePresentationTailCorrectionCount: 0
					property int scopePresentationExposedHeightChangeCount: 0
					property int scopePresentationExposedTailCorrectionCount: 0
					property real scopePresentationExposedTailTravel: 0
					property double scopePresentationStartedAt: 0
					property double scopePresentationCompletedAt: 0
					// Scope replacement removes one conversation and inserts another in the
					// same event turn. Temporarily retire the reuse pool so Qt Quick cannot
					// present a delegate with geometry/content retained from the old scope.
					// Reuse resumes immediately after the new scope has laid out and remains
					// enabled for all steady-state chat scrolling.
					property bool scopeReuseResetActive: false
					property real bottomFollowThreshold: 48
					onStickToBottomChanged: {
						if (stickToBottom)
							pendingTailMessageCount = 0
					}

					function isNearBottom() {
						if (count === 0 || atYEnd)
							return true
						const maximumY = Math.max(originY, originY + contentHeight - height)
						return maximumY - contentY <= bottomFollowThreshold
					}

					function requestBottomFollow() {
						if (stickToBottom && !prependAnchorActive && !restoringBottom
								&& !scopePresentationPending && !bottomFollowTimer.running
								&& !root.performanceChatScrollRunning)
							bottomFollowTimer.start()
					}

					function beginScopeChange() {
						releasePrependAnchor()
						bottomFollowTimer.stop()
						scopePresentationQuietTimer.stop()
						scopePresentationDeadlineTimer.stop()
						scopePresentationFinalizeQuietTimer.stop()
						scopePresentationFinalizeDeadlineTimer.stop()
						scopePresentationObservationTimer.stop()
						++scopePresentationGeneration
						scopePresentationPending = true
						scopePresentationFinalizing = false
						scopePresentationObservationActive = false
						scopePresentationForcedByDeadline = false
						scopePresentationMutationCount = 0
						scopePresentationTailCorrectionCount = 0
						scopePresentationExposedHeightChangeCount = 0
						scopePresentationExposedTailCorrectionCount = 0
						scopePresentationExposedTailTravel = 0
						scopePresentationStartedAt = Date.now()
						scopePresentationCompletedAt = 0
						scopeReuseResetActive = true
						followTailAfterInsert = false
						pendingTailInsertCount = 0
						pendingTailMessageCount = 0
						stickToBottom = true
						scopeResetPending = true
						scopePresentationQuietTimer.start()
						scopePresentationDeadlineTimer.start()
					}

					function noteScopePresentationMutation() {
						if (!scopePresentationPending)
							return
						++scopePresentationMutationCount
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

					function finishScopePresentation(forcedByDeadline) {
						if (!scopePresentationPending || scopePresentationFinalizing)
							return
						if (!forcedByDeadline && pendingScopeHydrationCount() > 0) {
							scopePresentationQuietTimer.restart()
							return
						}
						scopePresentationFinalizing = true
						scopePresentationForcedByDeadline = !!forcedByDeadline
						scopePresentationQuietTimer.stop()
						scopePresentationDeadlineTimer.stop()
						// Re-enable steady-state delegate reuse while the conversation is still
						// hidden. Creating the replacement pool can refine rich-row geometry, so
						// give it its own bounded quiet window before exposing the timeline.
						scopeReuseResetActive = false
						forceLayout()
						positionTailImmediately()
						restoringBottom = true
						containTailMessageWhenPossible()
						restoringBottom = false
						scopePresentationFinalizeQuietTimer.start()
						scopePresentationFinalizeDeadlineTimer.start()
					}

					function completeScopePresentationFinalization(forcedByDeadline) {
						if (!scopePresentationPending || !scopePresentationFinalizing)
							return
						const generation = scopePresentationGeneration
						const mutationCount = scopePresentationMutationCount
						scopePresentationFinalizeQuietTimer.stop()
						forceLayout()
						positionTailImmediately()
						restoringBottom = true
						containTailMessageWhenPossible()
						restoringBottom = false
						Qt.callLater(function() {
							if (generation !== timeline.scopePresentationGeneration
									|| !timeline.scopePresentationPending
									|| !timeline.scopePresentationFinalizing)
								return
							timeline.forceLayout()
							timeline.positionTailImmediately()
							timeline.restoringBottom = true
							timeline.containTailMessageWhenPossible()
							timeline.restoringBottom = false
							if (!forcedByDeadline
									&& mutationCount !== timeline.scopePresentationMutationCount) {
								scopePresentationFinalizeQuietTimer.restart()
								return
							}
							scopePresentationFinalizeDeadlineTimer.stop()
							timeline.scopePresentationForcedByDeadline =
								timeline.scopePresentationForcedByDeadline || !!forcedByDeadline
							timeline.scopeResetPending = false
							timeline.scopePresentationCompletedAt = Date.now()
							timeline.scopePresentationPending = false
							timeline.scopePresentationFinalizing = false
							timeline.scopePresentationObservationActive = true
							scopePresentationObservationTimer.restart()
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
						const minimumY = originY
						const maximumY = Math.max(minimumY, originY + contentHeight - height)
						const boundedY = Math.max(minimumY, Math.min(maximumY, nextY))
						if (Math.abs(contentY - boundedY) <= 0.5)
							return
						const previousY = contentY
						contentY = boundedY
						recordTailCorrection(previousY, contentY)
					}

					function positionTailImmediately() {
						if (!stickToBottom || prependAnchorActive)
							return
						const maximumY = Math.max(originY, originY + contentHeight - height)
						restoringBottom = true
						setTailContentY(maximumY)
						restoringBottom = false
					}

					function firstVisibleMessageDelegate() {
						const children = contentItem ? contentItem.children : []
						let candidate = null
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.stableId === undefined || !item.visible
									|| item.accessibilityPooled === true
									|| item.y + item.height <= contentY + 0.5)
								continue
							if (!candidate || item.y < candidate.y)
								candidate = item
						}
						return candidate
					}

					function capturePrependAnchor() {
						const item = firstVisibleMessageDelegate()
						if (!item)
							return false
						prependAnchorId = String(item.stableId || "")
						if (prependAnchorId.length === 0)
							return false
						prependAnchorOffset = item.y - contentY
						prependAnchorRetries = 0
						prependAnchorCorrectionFrames = 0
						prependAnchorActive = true
						stickToBottom = false
						return true
					}

					function releasePrependAnchor() {
						prependAnchorSettleTimer.stop()
						prependMutationInProgress = false
						prependAnchorActive = false
						prependAnchorId = ""
						prependAnchorRetries = 0
						prependAnchorCorrectionFrames = 0
					}

					function restorePrependAnchor() {
						if (!prependAnchorActive || restoringPrependAnchor)
							return
						const row = chatModel.rowForStableId(prependAnchorId)
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
						if (count <= 0)
							return
						const item = itemAtIndex(count - 1)
						if (!item || !item.visible)
							return
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
								setTailContentY(contentY + correction)
							}
							return
						}
						if (item.height > height + 1.0)
							return
						// The inline footer normally gives the latest message breathing room.
						// If that footer pushes an otherwise fitting rich message above the
						// viewport, trade only the required footer pixels for complete content.
						// This also keeps the visible row's semantic subtree available at
						// fractional DPR instead of exposing a visually clipped message.
						if (item.y < contentY - 1.0) {
							setTailContentY(item.y)
						}
					}

					// Delegate y-coordinates settle over several scene-graph frames after a
					// structural insert. Correct only during that bounded window; this is
					// dormant during steady-state chat and ordinary scrolling.
					FrameAnimation {
						id: prependAnchorFrameCorrection
						running: timeline.prependAnchorActive
							&& timeline.prependAnchorCorrectionFrames > 0
						onTriggered: {
							timeline.restorePrependAnchor()
							timeline.prependAnchorCorrectionFrames = Math.max(0,
								timeline.prependAnchorCorrectionFrames - 1)
						}
					}

					onContentHeightChanged: {
						if (scopePresentationPending) {
							noteScopePresentationMutation()
							return
						}
						if (scopePresentationObservationActive)
							++scopePresentationExposedHeightChangeCount
						if (prependAnchorActive && !restoringPrependAnchor)
							Qt.callLater(function() { timeline.restorePrependAnchor() })
						else if (scopeResetPending && !restoringBottom) {
							positionTailImmediately()
							requestBottomFollow()
						} else if (!restoringBottom)
							requestBottomFollow()
					}
					onMovementStarted: {
						root.closeQuickReactions()
						// QQuickListView reports its own insert displacement as movement.
						// Preserve the pre-structural anchor through that transition, while
						// ordinary wheel, drag and flick movement still cancels restoration.
						if (!restoringPrependAnchor && !restoringBottom
								&& !prependMutationInProgress) {
							stickToBottom = false
							releasePrependAnchor()
						}
					}
					onMovementEnded: {
						if (!restoringBottom) {
							stickToBottom = isNearBottom()
							requestBottomFollow()
						}
					}

					Timer {
						id: prependAnchorSettleTimer
						interval: 1500
						repeat: false
						onTriggered: timeline.releasePrependAnchor()
					}

					Timer {
						id: scopePresentationQuietTimer
						interval: 120
						repeat: false
						onTriggered: timeline.finishScopePresentation(false)
					}

					Timer {
						id: scopePresentationDeadlineTimer
						interval: 2000
						repeat: false
						onTriggered: timeline.finishScopePresentation(true)
					}

					Timer {
						id: scopePresentationFinalizeQuietTimer
						interval: 120
						repeat: false
						onTriggered: timeline.completeScopePresentationFinalization(false)
					}

					Timer {
						id: scopePresentationFinalizeDeadlineTimer
						interval: 500
						repeat: false
						onTriggered: timeline.completeScopePresentationFinalization(true)
					}

					Timer {
						id: scopePresentationObservationTimer
						interval: 400
						repeat: false
						onTriggered: timeline.scopePresentationObservationActive = false
					}

					Timer {
						id: bottomFollowTimer
						// Coalesce the height churn produced by delegate creation, rich-text
						// layout and asynchronous media into at most one correction per frame.
						interval: 16
						repeat: false
						onTriggered: {
							if (!timeline.stickToBottom || timeline.prependAnchorActive)
								return
							const maximumY = Math.max(timeline.originY,
								timeline.originY + timeline.contentHeight - timeline.height)
							if (!timeline.scopeResetPending && Math.abs(maximumY - timeline.contentY) <= 0.5)
								return
							timeline.restoringBottom = true
							timeline.setTailContentY(maximumY)
							Qt.callLater(function() {
								timeline.containTailMessageWhenPossible()
								timeline.restoringBottom = false
								timeline.scopeResetPending = false
								if (timeline.stickToBottom && !timeline.isNearBottom())
									timeline.requestBottomFollow()
							})
						}
					}

					Connections {
						target: chatModel
						function onRowsAboutToBePrepended(count) {
							if (count > 0 && !timeline.scopeResetPending && !timeline.stickToBottom
									&& !timeline.prependAnchorActive)
								timeline.prependMutationInProgress = timeline.capturePrependAnchor()
						}
						function onRowsAboutToChange(first, last) {
							if (!timeline.scopeResetPending && !timeline.stickToBottom
									&& !timeline.prependAnchorActive)
								timeline.capturePrependAnchor()
						}
						function onDataChanged(topLeft, bottomRight, roles) {
							if (timeline.prependAnchorActive)
								Qt.callLater(function() { timeline.restorePrependAnchor() })
							else if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							else
								timeline.requestBottomFollow()
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
							} else if (first >= timeline.count) {
								timeline.followTailAfterInsert = timeline.count === 0
									|| (timeline.stickToBottom && timeline.isNearBottom())
								if (!timeline.followTailAfterInsert)
									timeline.pendingTailInsertCount = Math.max(0, last - first + 1)
							}
						}
						function onRowsInserted(parentIndex, first, last) {
							if (first === 0 && timeline.prependAnchorActive)
								Qt.callLater(function() {
									timeline.prependMutationInProgress = false
									timeline.prependAnchorCorrectionFrames = 12
									timeline.restorePrependAnchor()
								})
							else if (timeline.followTailAfterInsert) {
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
							if (timeline.scopeReuseResetActive)
								Qt.callLater(function() {
									timeline.forceLayout()
									timeline.scopeReuseResetActive = false
								})
							if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							timeline.followTailAfterInsert = false
							timeline.pendingTailInsertCount = 0
						}
						function onModelReset() {
							timeline.releasePrependAnchor()
							timeline.pendingTailInsertCount = 0
							timeline.pendingTailMessageCount = 0
							timeline.stickToBottom = true
							if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							else
								timeline.requestBottomFollow()
						}
						function onCountChanged() {
							if (timeline.scopePresentationPending)
								timeline.noteScopePresentationMutation()
							if (timeline.count === 0) {
								timeline.releasePrependAnchor()
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
                    header: Item {
                        width: timeline.width
                        height: (activeScope.canLoadOlder || activeScope.loadingState === "older") ? 48 : 0
                        visible: height > 0
						opacity: timeline.scopePresentationPending ? 0 : 1
                        ModernButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 4
                            enabled: activeScope.canLoadOlder && activeScope.loadingState !== "older"
                            text: activeScope.loadingState === "older"
                                  ? qsTr("Loading older messages…")
                                  : qsTr("Load older messages")
                            Accessible.name: text
                            Accessible.description: qsTr("Load messages sent before the currently visible history")
                            onClicked: uiCommands.requestOlderMessages()
                        }
                    }
					// Flickable margins live outside contentHeight, and ListView's
					// positionViewAtEnd() does not include bottomMargin. Keep the
					// visual breathing room inside the list so tail-following exposes
					// the complete final message above the composer.
					footerPositioning: ListView.InlineFooter
					footer: Item {
						width: timeline.width
						height: root.timelineVerticalMargin
					}
			delegate: ChatMessageFrame {
						id: messageDelegate
						opacity: timeline.scopePresentationPending ? 0 : 1
						readonly property bool performanceTimelineDelegate: true
				required property int index
				// Ordinary rows enter UIA only when fully contained so cached actor/header
				// nodes cannot leak across the clip. A legitimate rich card may be taller
				// than the viewport; it can never satisfy full containment, so expose that
				// row while it intersects the viewport and let its visible provider surface
				// remain navigable.
				accessibilityViewportVisible: height > timeline.height
					? y + height > timeline.contentY + 0.5
						&& y < timeline.contentY + timeline.height - 0.5
					: y >= timeline.contentY - 0.5
						&& y + height <= timeline.contentY + timeline.height + 0.5
				accessibilitySuppressed: root.backgroundAccessibilitySuppressed
					|| timeline.scopePresentationPending
				hoverEffectsEnabled: !root.visualFixtureOverrideActive
						function openAutomationActions() {
							openMessageActions()
							return messageActions
						}
						function openMessageActions() {
							messageActions.targetId = stableId
							messageActions.targetCanReply = canReply
							messageActions.targetCanReact = canReact
							messageActions.targetCanDelete = canDelete
							messageActions.targetCanRetry = !!source.deliveryCanRetry
							messageActions.open()
						}
						function closeMessageActionsForReuse() {
							if (!messageActions)
								return
							if (messageActions.visible)
								messageActions.close()
							messageActions.targetId = ""
						}
						readonly property bool previewNeedsHydration: !!preview
							&& Object.keys(preview).length > 0
							&& (preview.state === "loading" || preview.loading === true)
						readonly property bool attachmentNeedsHydration: (attachments || []).some(function(attachment) {
							return !!attachment && String(attachment.state || "").toLowerCase() === "loading"
						})
						readonly property bool bodyNeedsHydration: !!source.bodyHydrationPending
						readonly property bool backendContentNeedsHydration: previewNeedsHydration
							|| attachmentNeedsHydration
						readonly property bool contentNeedsHydration: backendContentNeedsHydration
							|| bodyNeedsHydration
						readonly property bool inHydrationWindow: !accessibilityPooled
							&& y + height >= timeline.contentY - timeline.height * 0.5
							&& y <= timeline.contentY + timeline.height * 1.5
						readonly property bool inVisibleViewport: !accessibilityPooled
							&& y + height >= timeline.contentY
							&& y <= timeline.contentY + timeline.height
						function requestPreviewHydrationIfNeeded() {
							if (backendContentNeedsHydration && inHydrationWindow)
								root.queuePreviewHydration(stableId, inVisibleViewport)
						}
						function primaryRichContentViewportBounds() {
							let content = null
							if (messagePreviewLoader.status === Loader.Ready && messagePreviewLoader.item)
								content = messagePreviewLoader.item
							else if (messageAttachmentLoader.status === Loader.Ready && messageAttachmentLoader.item)
								content = messageAttachmentLoader.item
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
						ListView.onPooled: {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegatePooledCount
							closeMessageActionsForReuse()
							accessibilityPooled = true
						}
						ListView.onReused: {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegateReusedCount
							closeMessageActionsForReuse()
							accessibilityPooled = false
							if (messagePreviewLoader.item)
								messagePreviewLoader.item.resetForReuse()
							Qt.callLater(function() { messageDelegate.requestPreviewHydrationIfNeeded() })
						}
						onContentNeedsHydrationChanged: requestPreviewHydrationIfNeeded()
						onInHydrationWindowChanged: requestPreviewHydrationIfNeeded()
						onStableIdChanged: {
							closeMessageActionsForReuse()
							requestPreviewHydrationIfNeeded()
						}
						Component.onCompleted: {
							if (root.visualFixtureOverrideActive)
								++root.performanceChatDelegateCreatedCount
							requestPreviewHydrationIfNeeded()
						}
						// This technical delegate frame must remain ignored for its entire
						// lifetime. Switching it between ignored and exposed while ListView
						// reuses it leaves both promoted and nested children in the Windows
						// accessibility tree.
						Accessible.ignored: accessibilityFrameIgnored
						required property string title
                        required property string subtitle
                        required property string status
                        required property string avatarUrl
                        required property string timestamp
                        required property string replyActor
                        required property string replySnippet
                        required property var reactions
                        required property var bodySegments
                        required property var preview
                        required property var attachments
                        required property var source
                        required property bool deleted
                        required property bool canReply
						required property bool canReact
						required property bool canDelete
						searchCurrent: root.conversationSearchOpen
							&& stableId === String(chatModel.currentMatchStableId || "")
						systemMessage: !!source.system
							|| (!source.actorKey && avatarUrl.length === 0 && !canReply && !canReact && !own)
						startsGroup: root.messageStartsGroup(index, source, title)
						dateSeparatorLabel: root.messageDateSeparator(index, source)
						bodyImplicitHeight: messageRow.implicitHeight
						// Flickable margins do not constrain a vertical ListView delegate's
						// horizontal scene rect. Reserve the shell-safe trailing inset here as
						// well, otherwise compact metadata extends beneath productSurface's
						// rounded clip even though the timeline itself is correctly sized.
						laneAvailableWidth: Math.max(1, timeline.width
							- timeline.leftMargin - timeline.rightMargin - Theme.space4)
						laneMaximumWidth: root.conversationLaneMaximumWidth
						readonly property bool hasPreviewContent: !!preview && Object.keys(preview).length > 0
						readonly property bool hasAttachmentContent: !!attachments && attachments.length > 0
						readonly property bool hasReplyContent: replyActor.length > 0 || replySnippet.length > 0
						readonly property bool hasInlineImageContent: root.messageContainsInlineImage(bodySegments)
						readonly property bool hasMessageActions: canReply || canReact || canDelete
							|| !!source.deliveryCanRetry
						readonly property bool hasReactions: !!reactions && reactions.length > 0
						readonly property bool quickReactionsExpanded: canReact
							&& root.quickReactionMessageId === stableId
						readonly property bool hasEmbeddedFooterContent: hasReactions || wideContent
							|| hasReplyContent || hasDeliveryStatus
							|| (own && !systemMessage && timestamp.length > 0)
						readonly property bool usesCompactActionOverlay: hasMessageActions
							&& !wideContent
						readonly property bool performancePreviewMaterialized: messagePreviewLoader.status === Loader.Ready
							&& !!messagePreviewLoader.item
						readonly property bool performanceAttachmentMaterialized: messageAttachmentLoader.status === Loader.Ready
							&& !!messageAttachmentLoader.item
						readonly property real previewTargetWidth: hasPreviewContent && messagePreviewLoader.item
							? messagePreviewLoader.item.targetCardWidth
							: preview && preview.previewSize === "compact" ? 460
							: preview && preview.previewSize === "large" ? 720 : 580
						// Reactions are compact metadata and wrap naturally in their Flow. Treating
						// one reaction like a media card makes an otherwise short message expand
						// across the complete conversation lane.
						readonly property real richContentChromeWidth: own || systemMessage
							? horizontalPadding * 2
							: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
						// Replies and delivery metadata are compact text. Only media opts into
						// the wider lane, so sending/failed messages never become broad panels.
						wideContent: hasPreviewContent || hasAttachmentContent || hasInlineImageContent
						readonly property real attachmentTargetWidth: hasAttachmentContent
							? root.preferredAttachmentMessageWidth(attachments, own || systemMessage) : 0
						readonly property real previewMessageWidth: hasPreviewContent
							? previewTargetWidth + richContentChromeWidth : 0
						preferredWideContentWidth: hasInlineImageContent
							? Theme.chatRichMaximumWidth
							: Math.min(Theme.chatRichMaximumWidth, Math.max(attachmentTargetWidth,
								previewMessageWidth, own ? preferredOwnWidth : preferredIncomingWidth))
						preferredOwnWidth: root.preferredOutgoingMessageWidth(bodySegments, startsGroup,
							replyActor, replySnippet, hasDeliveryStatus)
						preferredIncomingWidth: root.preferredIncomingMessageWidth(bodySegments, startsGroup,
							replyActor, replySnippet, hasDeliveryStatus)
						readonly property string deliveryState: String(source.deliveryState || status || "").trim().toLowerCase()
						readonly property string deliveryLabel: String(source.deliveryLabel || status || "").trim()
						readonly property bool hasDeliveryStatus: deliveryState === "sending"
							|| deliveryState === "failed" || deliveryState === "cancelled"
						width: laneAvailableWidth
						TapHandler {
							acceptedButtons: Qt.RightButton
							onTapped: messageDelegate.openMessageActions()
						}
                        RowLayout {
							id: messageRow
							anchors.fill: parent
							spacing: Theme.space2
                            Rectangle {
								Layout.preferredWidth: messageDelegate.own || messageDelegate.systemMessage ? 0 : Theme.avatarMedium
								Layout.preferredHeight: Layout.preferredWidth
                                Layout.alignment: Qt.AlignTop
								visible: !messageDelegate.own && !messageDelegate.systemMessage
								radius: width / 2
                                color: Theme.strip
                                clip: true
								opacity: messageDelegate.startsGroup ? 1 : 0
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
									visible: (messageDelegate.startsGroup && !messageDelegate.own)
										|| messageDelegate.systemMessage
									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: title || qsTr("System")
										color: messageDelegate.systemMessage ? Theme.textMuted : Theme.accent
										font.bold: true
										font.pixelSize: Theme.fontCaption
										elide: Text.ElideRight
									}
                                    Label { textFormat: Text.PlainText; text: timestamp; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; visible: timestamp.length > 0 }
									Label {
										Layout.maximumWidth: root.narrowShell ? 72 : 120
										textFormat: Text.PlainText
										text: status
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
										visible: status.length > 0
										elide: Text.ElideRight
									}
                                }
								Rectangle {
									id: messageBubble
									objectName: "chatMessageBubble"
									readonly property real bubbleHorizontalInset: messageDelegate.own
										|| messageDelegate.systemMessage ? 0 : Theme.chatBubbleHorizontalPadding
									readonly property real bubbleVerticalInset: messageDelegate.own
										|| messageDelegate.systemMessage ? 0 : Theme.chatBubbleVerticalPadding
									Layout.fillWidth: true
									Layout.preferredHeight: bubbleContent.implicitHeight + bubbleVerticalInset * 2
									radius: Theme.innerRadius
									color: messageDelegate.own || messageDelegate.systemMessage
										? "transparent" : messageDelegate.bubbleColor
									border.color: messageDelegate.own || messageDelegate.systemMessage
										? "transparent" : messageDelegate.bubbleBorderColor
									border.width: messageDelegate.own || messageDelegate.systemMessage
										? 0 : messageDelegate.bubbleBorderWidth

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
									accessibilitySuppressed: root.backgroundAccessibilitySuppressed
										|| !messageDelegate.itemContainedInViewport(messageBody)
									segments: messageDelegate.bodySegments || []
									Layout.rightMargin: messageDelegate.usesCompactActionOverlay
										? compactMessageActionTray.width + Theme.space2 : 0
									resourceActive: !messageDelegate.accessibilityPooled
									animationsEnabled: !root.visualFixtureOverrideActive
									hoverEffectsEnabled: !root.visualFixtureOverrideActive
                                    textColor: Theme.textMain
                                    pixelSize: Theme.fontBody
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
                                    font.pixelSize: Theme.fontBody
                                    font.italic: true
                                }
								Loader {
									id: messageAttachmentLoader
									active: messageDelegate.hasAttachmentContent
									visible: active
									Layout.fillWidth: true
									Layout.preferredHeight: active && item ? item.implicitHeight : 0
									onLoaded: if (root.visualFixtureOverrideActive)
										++root.performanceChatAttachmentLoadedCount
									sourceComponent: Component {
										AttachmentGallery {
											attachments: messageDelegate.attachments || []
											resourceActive: !messageDelegate.accessibilityPooled
											animationsEnabled: !root.visualFixtureOverrideActive
											onAttachmentRequested: attachment => root.requestAttachment(
												attachment, messageDelegate.stableId)
											onAttachmentDownloadRequested: attachment => root.downloadAttachment(attachment, false)
											onAttachmentRetryRequested: attachment => uiCommands.retryChatAttachmentPreview(
												activeScope.scopeToken, messageDelegate.stableId,
												String(attachment.assetId || attachment.assetID || ""))
											onAttachmentRefreshRequested: root.queuePreviewHydration(messageDelegate.stableId, true)
										}
									}
                                }
								Loader {
									id: messagePreviewLoader
									active: messageDelegate.hasPreviewContent
									visible: active
									Layout.fillWidth: false
									// Loader retains its previous implicitHeight briefly while a
									// pooled delegate drops rich content. Bind the layout height to
									// active content explicitly so a plain reused message cannot keep
									// a stale card-sized gap.
									Layout.preferredHeight: active && item ? item.implicitHeight : 0
									Layout.alignment: Qt.AlignLeft
									Layout.preferredWidth: Math.min(messageDelegate.previewTargetWidth,
										Math.max(1, parent.width))
									Layout.maximumWidth: Layout.preferredWidth
									onLoaded: if (root.visualFixtureOverrideActive)
										++root.performanceChatPreviewLoadedCount
									sourceComponent: Component {
										RichPreviewCard {
                                    preview: messageDelegate.preview || ({})
									mediaSessionController: mediaSession
									mediaProfileFactory: mediaProfiles
									mediaSessionId: messageDelegate.stableId
									visualMediaFixtureMode: root.visualMediaFixtureMode
									savedSizePreset: root.richPreviewSizePreset(messageDelegate.stableId)
									animationsEnabled: !root.visualFixtureOverrideActive
									hoverEffectsEnabled: !root.visualFixtureOverrideActive
									previewIdentity: messageDelegate.stableId + "|" + String(messageDelegate.preview
										? (messageDelegate.preview.url || messageDelegate.preview.embedUrl
											|| messageDelegate.preview.mediaUrl || messageDelegate.preview.title || "") : "")
									renderActive: messageDelegate.inHydrationWindow && !messageDelegate.accessibilityPooled
									watchTogetherAvailable: !mediaSession.sharedAvailable
                                    onExternalOpenRequested: url => Qt.openUrlExternally(url)
									onImageOpenRequested: (source, title) => root.openManagedPreviewImage(
										source, title, messageDelegate.stableId)
									onImageRefreshRequested: root.queuePreviewHydration(messageDelegate.stableId, true)
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
									onWatchTogetherRequested: (url, provider, title) =>
										root.startWatchTogether(url, provider, title)
									onSizePresetRequested: preset =>
										root.rememberRichPreviewSizePreset(messageDelegate.stableId, preset)
										}
									}
                                }
								RowLayout {
									id: messageFooter
									objectName: "chatMessageFooter"
									readonly property bool quickReactionsExpanded: messageDelegate.canReact
										&& root.quickReactionMessageId === messageDelegate.stableId
									Layout.fillWidth: true
									Layout.rightMargin: messageDelegate.usesCompactActionOverlay
										? compactMessageActionTray.width + Theme.space2 : 0
									visible: messageDelegate.hasEmbeddedFooterContent
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
													messageDelegate.index, messageReactButton,
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
									width: compactMessageActionButtons.implicitWidth + Theme.space1
									height: 34
									visible: messageDelegate.usesCompactActionOverlay
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
												messageDelegate.index, compactMessageReactButton,
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
					Rectangle {
						id: emptyConversationState
						objectName: "emptyConversationState"
						anchors.centerIn: parent
						width: Math.max(1, Math.min(380, timeline.width
							- root.timelineHorizontalMargin * 2 - Theme.space4 * 2))
						height: emptyConversationContent.implicitHeight + Theme.space5 * 2
						readonly property bool visualLoading: activeScope.loading
							|| timeline.scopePresentationPending
						visible: (timeline.scopePresentationPending || chatModel.count === 0)
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
									? (activeScope.loading ? qsTr("Loading conversation")
										: qsTr("Preparing conversation"))
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
									? (activeScope.loading
										? qsTr("Messages will appear here when history is ready.")
										: qsTr("Recent messages are being laid out."))
									: !clientSession.connected ? qsTr("Open the server browser to load rooms and messages.")
									: activeScope.canSend ? qsTr("Be the first to write in %1.").arg(activeScope.label)
									: qsTr("Select a text room, voice room, or direct message to get started.")
								color: Theme.textMuted
								font.pixelSize: Theme.fontBody
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

				// Keep this navigation action in its own layout row. A floating pill on
				// top of the viewport obscured the newest visible message, especially in
				// compact layouts and immediately after history prepends.
				ModernButton {
					id: jumpToLatestButton
					objectName: "jumpToLatestButton"
					Layout.alignment: Qt.AlignRight
					Layout.rightMargin: Theme.space4
					Layout.topMargin: visible ? Theme.space1 : 0
					Layout.bottomMargin: visible ? Theme.space1 : 0
					Layout.preferredHeight: visible ? implicitHeight : 0
					visible: timeline.count > 0 && !timeline.stickToBottom
						&& !timeline.scopePresentationPending
					dense: true
					tone: timeline.pendingTailMessageCount > 0 ? "accent" : "neutral"
					text: timeline.pendingTailMessageCount > 0
						? qsTr("%n new message(s)", "", timeline.pendingTailMessageCount)
						: qsTr("Jump to latest")
					Accessible.description: timeline.pendingTailMessageCount > 0
						? qsTr("Scroll to %n new message(s)", "", timeline.pendingTailMessageCount)
						: qsTr("Scroll to the newest message")
					onClicked: {
						timeline.stickToBottom = true
						timeline.requestBottomFollow()
					}
				}

				StonksHeader {
					id: aboveComposerStonksTicker
					objectName: "stonksTickerAboveComposer"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					stonks: root.stonksTickerState
					tickerBannerEnabled: root.stonksTickerEnabled
						&& root.stonksTickerPlacement === "aboveComposer"
					tickerDirection: root.stonksTickerDirection
					tickerSpeed: root.stonksTickerSpeed
					scopeToken: activeScope.scopeToken
					scopeLabel: activeScope.label
					narrowLayout: root.narrowShell
					docked: true
					onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
				}

				Rectangle {
					id: composerSurface
                    Layout.fillWidth: true
					// Reply copy, attachments, autocomplete, and the input row all keep
					// their own minimum height. Reserve the complete reply row plus the
					// intervening layout gap so compact combinations never overflow the
					// composer's semantic or visual bounds.
					readonly property int baseHeight: Math.max(72, Theme.railFooterHeight)
					Layout.preferredHeight: (activeScope.hasPendingReply ? baseHeight + 44 : baseHeight)
						+ Math.min(3, Math.max(0, composerInput.lineCount - 1)) * Theme.composerLineHeight
						+ (composer.attachments.count > 0 ? 58 : 0)
						+ (composer.autocompleteItems.length > 0 ? 34 : 0)
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
									font.pixelSize: Theme.fontBody
									font.weight: Font.DemiBold
					}
				}
            }
					}
                    RowLayout {
						anchors.top: parent.top
						anchors.bottom: parent.bottom
						anchors.horizontalCenter: parent.horizontalCenter
						anchors.topMargin: root.narrowShell ? Theme.space2 : 14
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
								Accessible.description: activeScope.composerHint
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
									if (!event.matches(StandardKey.Paste))
										return
									const attached = activeScope.canAttachImages && composer.pasteFromClipboard()
									if (!attached)
										composerInput.paste()
									event.accepted = true
								}
                                Keys.onReturnPressed: event => {
                                    if (!(event.modifiers & Qt.ShiftModifier) && (text.trim().length > 0 || composer.attachments.count > 0)) {
                                        composer.send()
                                        event.accepted = true
                                    }
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
			alignedFooterHeight: composerSurface.height
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

		StonksHeader {
			id: windowTopStonksTicker
			objectName: "stonksTickerWindowTop"
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			height: visible ? implicitHeight : 0
			stonks: root.stonksTickerState
			tickerBannerEnabled: root.stonksTickerEnabled
				&& root.stonksTickerPlacement === "windowTop"
			tickerDirection: root.stonksTickerDirection
			tickerSpeed: root.stonksTickerSpeed
			scopeToken: activeScope.scopeToken
			scopeLabel: activeScope.label
			narrowLayout: root.narrowShell
			docked: true
			z: 2
			onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
		}

		StonksHeader {
			id: bottomStonksTicker
			objectName: "stonksTickerBottom"
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			height: visible ? implicitHeight : 0
			stonks: root.stonksTickerState
			tickerBannerEnabled: root.stonksTickerEnabled && root.stonksTickerPlacement === "bottom"
			tickerDirection: root.stonksTickerDirection
			tickerSpeed: root.stonksTickerSpeed
			scopeToken: activeScope.scopeToken
			scopeLabel: activeScope.label
			narrowLayout: root.narrowShell
			docked: true
			z: 2
			onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
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
			anchors.bottomMargin: Theme.space4 + bottomStonksTicker.height
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
					root.openAttachment(attachment, "", messageId)
				onWatchTogetherRequested: (url, provider, title) =>
					root.startWatchTogether(url, provider, title)
				onPreviewSizePresetRequested: (preferenceKey, preset) =>
					root.rememberRichPreviewSizePreset(preferenceKey, preset)
			}
		}
	}
}
