import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

// The v2 chat timeline (docs/chat-timeline-v2-plan.md). Rows come newest first
// from a ChatRowModel (chatTimelineV2 for rooms) and are laid out bottom to top,
// so the newest message sits at the bottom without scrolling and older pages
// grow upwards. `commands` performs actions, hydration, paging and read state:
// RoomConversationCommands for rooms, the direct message window's own object
// for a conversation.
//
// Scrolling rules:
// - The view follows the newest message only while the reader is there. That
//   intent changes through the reader's own scrolling or an explicit jump, never
//   through layout the list does itself.
// - While following, growth below eases the view down and ends with a snap.
// - While reading history, the row at the reading point stays where it is on
//   screen: whenever layout moves it or the view (a row resizing or arriving
//   anywhere, Qt keeping the view near a growing top), the view moves back by
//   the same amount in the same layout pass. Only the reader's own scrolling
//   and the view's explicit jumps choose a new reading row. Qt moves rows
//   differently depending on where the change is (a growing newest row inside
//   the display margin shifts the rest by twice its growth), so the view
//   measures the anchor instead of predicting the shift.
ListView {
	id: view
	objectName: "conversationView"

	// The shell's root item: shared helpers for avatars, attachments, previews
	// and the quick reaction popup.
	property var host: null
	property var rowModel: chatTimelineV2
	property QtObject commands: RoomConversationCommands {}
	// Rooms use the shell's quick reaction popup; a separate window shows the
	// reaction bar inside the row instead.
	property bool hostReactionPopup: true
	property bool followTail: true
	property bool following: false
	property double liveInsertAt: 0
	property int liveInsertLast: -1
	property var hydrationRequested: ({})
	property var hydrationQueue: []
	property bool hydrationHighPriority: false
	readonly property bool atLatest: followTail || nearTail()
	// Dragging the scrollbar sets contentY directly and starts no movement, so
	// it gets its own intent handling: it never follows, never pages and is
	// never compensated while held.
	readonly property bool scrollbarDragging: verticalScrollBar.pressed
	readonly property ListView rowView: view
	// The row whose action menu is open keeps its action buttons.
	readonly property string menuStableId: messageActions.targetId
	// A message the reader jumped to (search match or replied message) is
	// outlined briefly.
	property string flashStableId: ""
	property int pendingRevealThread: 0
	property int pendingRevealMessage: 0
	property int pendingRevealPages: 0
	property var previewHeights: ({})
	// One player for the conversation's link cards: it covers the playing card's
	// media box while that is on screen and docks in the corner while it is not.
	readonly property var session: typeof mediaSession !== "undefined" ? mediaSession : null
	// The session this view started; another card's session is not its own.
	property string ownedSessionId: ""
	property var playbackInfo: ({})
	readonly property string playingStableId: ownedSessionId.length > 0 && !!session && session.active
		&& !session.detached && String(session.sessionId || "") === ownedSessionId ? ownedSessionId : ""
	property bool playerDocked: false
	onPlayingStableIdChanged: {
		if (playingStableId.length === 0 && !(session && session.active
				&& String(session.sessionId || "") === ownedSessionId))
			ownedSessionId = ""
	}
	// The next older page is requested this close to the top (in pixels or in
	// rows), so a reader scrolling up does not stop against the edge while the
	// page is on its way.
	property real olderPrefetchDistance: Math.max(height * 3, 1500)
	property int olderPrefetchRows: 25
	// The row at the centre of the viewport while the reader is not following.
	property Item anchorItem: null
	property string anchorStableId: ""
	property real anchorScreenY: 0
	property bool holdingAnchor: false
	// Set while the view itself jumps (search, reply, fixtures).
	property bool repositioning: false
	readonly property bool readerScrolling: moving || scrollbarDragging || scrollHandler.scrolling || repositioning
	// Corrections left in this frame, so the lock can never keep layout from
	// settling; when they run out it continues in the next frame.
	readonly property int anchorHoldsPerFrame: 8
	property int anchorHoldBudget: anchorHoldsPerFrame
	property int previewHeightCount: 0

	function tailY() {
		return originY + contentHeight + bottomMargin - height
	}
	// The wheel stops at the content edge, inside the bottom margin.
	function nearTail() {
		return tailY() - contentY < bottomMargin + 4
	}
	function jumpToLatest() {
		followTail = true
	}
	function arrivalActive(rowIndex) {
		return rowIndex <= liveInsertLast && Date.now() - liveInsertAt < 400
	}
	// Explicit jumps that choose a new reading position.
	function positionAt(y) {
		repositioning = true
		contentY = y
		repositioning = false
		pickAnchor()
	}
	function positionAtIndex(index, mode) {
		repositioning = true
		positionViewAtIndex(index, mode)
		repositioning = false
		pickAnchor()
	}
	function pickAnchor() {
		if (holdingAnchor)
			return
		const index = followTail ? -1 : indexAt(width / 2, contentY + height / 2)
		const item = index >= 0 ? itemAtIndex(index) : null
		anchorItem = item
		anchorStableId = item ? String(item.stableId || "") : ""
		anchorScreenY = item ? item.y - contentY : 0
	}
	function holdAnchor() {
		if (!anchorItem || followTail || holdingAnchor)
			return
		if (String(anchorItem.stableId || "") !== anchorStableId) {
			// The delegate was pooled and reused for another row.
			pickAnchor()
			return
		}
		const shift = anchorItem.y - contentY - anchorScreenY
		if (Math.abs(shift) < 1)
			return
		if (anchorHoldBudget <= 0)
			return
		if (anchorHoldBudget === anchorHoldsPerFrame)
			Qt.callLater(resetAnchorHoldBudget)
		--anchorHoldBudget
		holdingAnchor = true
		contentY += shift
		holdingAnchor = false
		// pixelAligned rounds contentY, so up to half a pixel may remain.
		if (Math.abs(anchorItem.y - contentY - anchorScreenY) < 1.5)
			return
		// Layout has already moved the row, but Qt updates the content's edges
		// (originY, contentHeight) a pass later, so the view stops at a stale
		// edge. Keep the reading row and finish when the edges move.
		if (atYBeginning || atYEnd)
			return
		// The rows moved with the view: take where the row is now.
		pickAnchor()
	}
	// Rows within three quarters of a screen of the view finish their link cards
	// before they scroll in, so they arrive at their real height. Cards on screen
	// finish at once; of the others, only the nearest per call, so a burst of
	// large cards spreads over several frames.
	function finishCardsNearView() {
		const margin = height * 0.75
		const rows = contentItem.children
		let nearest = null
		let nearestGap = margin
		for (let i = 0; i < rows.length; ++i) {
			const row = rows[i]
			if (!row.previewPending || !row.previewPending())
				continue
			const gap = Math.max(contentY - (row.y + row.height), row.y - (contentY + height), 0)
			if (gap === 0) {
				row.finishPreview()
			} else if (gap < nearestGap) {
				nearest = row
				nearestGap = gap
			}
		}
		if (nearest)
			nearest.finishPreview()
	}
	function resetAnchorHoldBudget() {
		const exhausted = anchorHoldBudget <= 0
		anchorHoldBudget = anchorHoldsPerFrame
		if (exhausted)
			holdAnchor()
	}
	function loadOlderIfNeeded() {
		if (!visible || count === 0 || scrollbarDragging || !commands.canLoadOlder || commands.loadingOlder)
			return
		const nearTop = contentY - originY < olderPrefetchDistance
			|| indexAt(width / 2, contentY + 1) >= count - olderPrefetchRows
		if (nearTop)
			commands.loadOlder()
	}
	function queueHydration(stableId, item, force) {
		const id = String(stableId || "")
		if (!commands.hydrationAvailable || !commands.validHydrationId(id) || (hydrationRequested[id] && !force))
			return
		hydrationRequested[id] = true
		hydrationQueue.push(id)
		if (item && item.y + item.height > contentY && item.y < contentY + height)
			hydrationHighPriority = true
		if (!hydrationTimer.running)
			hydrationTimer.start()
	}
	function openMessageActions(rowItem, anchorItem, x, y) {
		messageActions.targetId = rowItem.stableId
		messageActions.targetRow = rowItem
		messageActions.targetText = rowItem.plainText
		messageActions.targetReactions = rowItem.reactionsForQuickBar()
		messageActions.targetAnchor = anchorItem
		messageActions.targetCanReply = rowItem.canReply
		messageActions.targetCanReact = rowItem.canReact
		messageActions.targetCanDelete = rowItem.canDelete
		messageActions.targetCanRetry = rowItem.deliveryCanRetry
		messageActions.popup(anchorItem, x, y)
	}
	// Opens the detail sheet of a row's card at one of its pictures.
	function openDetails(rowItem, mediaIndex) {
		if (!rowItem || !rowItem.preview)
			return
		detailLoader.active = true
		detailLoader.item.show(rowItem.preview, rowItem.stableId, mediaIndex)
	}
	// Plays a medium chosen in the detail sheet in its card, bringing the row
	// into view first if it has scrolled away.
	function playFromDetails(stableId, mediaIndex) {
		const index = rowModel ? rowModel.rowForStableId(stableId) : -1
		if (index < 0)
			return
		let rowItem = itemAtIndex(index)
		if (!rowItem || rowItem.y + rowItem.height < contentY || rowItem.y > contentY + height) {
			followTail = false
			positionAtIndex(index, ListView.Center)
			forceLayout()
			rowItem = itemAtIndex(index)
		}
		if (rowItem)
			startPlayback(rowItem, mediaIndex)
	}
	// Starts the card's media in the view's player.
	function startPlayback(rowItem, mediaIndex) {
		const card = rowItem ? rowItem.embedCard() : null
		if (!card || !session)
			return false
		const preview = card.preview || ({})
		const id = String(rowItem.stableId || "")
		const mode = String(card.playback.mode || "none")
		const metadata = preview.metadata || ({})
		let started = false
		let provider = String(card.playback.provider || card.providerId || "")
		if (mode === "native") {
			const playable = item => !!item && item.directPlayable
				&& (item.kind === "video" || item.kind === "audio" || item.kind === "animated-image")
			const chosen = card.media[Number(mediaIndex) || 0]
			const direct = playable(chosen) ? chosen : (card.media.find(playable) || ({}))
			const url = String(direct.url || preview.mediaUrl || "")
			if (!/^https?:\/\//i.test(url) && !/^file:\/\//i.test(url))
				return false
			const pairedAudio = url === String(preview.mediaUrl || "") ? String(preview.mediaAudioUrl || "") : ""
			started = session.openDirectInline(url, String(direct.mime || preview.mediaMime || ""), pairedAudio,
				pairedAudio.length > 0 ? String(preview.mediaAudioMime || "") : "", id)
			provider = "direct"
		} else if (mode === "provider") {
			const url = String(card.playback.url || preview.embedUrl || "")
			if (!/^https:\/\//i.test(url))
				return false
			provider = String(preview.embedKind || provider)
			started = session.openInline(url, provider, id)
		}
		if (!started)
			return false
		playbackInfo = {
			"stableId": id,
			"aspect": card.aspect,
			"provider": provider,
			"title": card.title || card.description || card.providerLabel,
			"presentationMode": String(metadata.mediaPresentation || "") === "provider-post-card"
				? "provider-post-card" : String(metadata.mediaPresentation || "")
		}
		// A new player for each media, set up for its shape and provider.
		playerLoader.setSource(Qt.resolvedUrl("InlineMediaPlayer.qml"), {
			"session": session,
			"aspect": String(playbackInfo.aspect || "wide"),
			"presentationProvider": String(playbackInfo.provider || ""),
			"presentationMode": String(playbackInfo.presentationMode || ""),
			"animationAutoPlayEnabled": true,
			"mediaProfileFactory": typeof mediaProfiles !== "undefined" ? mediaProfiles : null,
			"visualFixtureMode": host ? String(host.visualMediaFixtureMode || "") : ""
		})
		ownedSessionId = id
		if (session.playbackControllable)
			session.play()
		return true
	}
	// The player's height at a width: its picture plus its controls.
	function playerHeightFor(width) {
		const player = playerLoader.item
		if (!player)
			return Math.round(width * 9 / 16)
		return player.viewportHeightForWidth(width) + Math.max(0, player.implicitHeight - player.mediaViewportHeight)
	}
	function playingRow() {
		const rows = contentItem.children
		for (let i = 0; i < rows.length; ++i) {
			if (rows[i].embedCard && String(rows[i].stableId) === playingStableId && rows[i].visible)
				return rows[i]
		}
		return null
	}
	// Over the card while its box is on screen; docked in the corner otherwise.
	function placePlayer() {
		if (playingStableId.length === 0)
			return
		const row = playingRow()
		const card = row ? row.embedCard() : null
		const box = card ? card.mediaBox : null
		if (box && box.height > 0) {
			const at = box.mapToItem(view, 0, 0)
			if (at.y + box.height > 0 && at.y < height) {
				playerDocked = false
				playerHost.x = at.x
				playerHost.y = at.y
				playerHost.width = box.width
				return
			}
		}
		playerDocked = true
		playerHost.width = Math.min(360, Math.max(240, width * 0.4))
		playerHost.x = width - playerHost.width - Theme.space4
		playerHost.y = height - playerHost.height - Theme.space4
	}
	// Ends playback this view started once its message has left the view.
	function closePlaybackIfGone() {
		if (playingStableId.length > 0 && rowModel && rowModel.rowForStableId(playingStableId) < 0) {
			session.close()
			ownedSessionId = ""
		}
	}
	function openLinkActions(rowItem, anchorItem, url) {
		linkActions.targetUrl = String(url || "")
		if (linkActions.targetUrl.length > 0)
			linkActions.popup(anchorItem, 0, anchorItem.height)
	}
	function previewHeightFor(stableId) {
		return Number(previewHeights[String(stableId)] || 0)
	}
	function rememberPreviewHeight(stableId, height) {
		const key = String(stableId || "")
		if (key.length === 0 || height <= 0)
			return
		if (!(key in previewHeights) && ++previewHeightCount > 2000) {
			previewHeights = ({})
			previewHeightCount = 1
		}
		previewHeights[key] = height
	}
	function revealRow(row, stableId) {
		if (row < 0 || row >= count)
			return false
		followTail = false
		positionAtIndex(row, ListView.Center)
		flashStableId = String(stableId || "")
		flashTimer.restart()
		return true
	}
	// Scrolls to a message, paging older history in until it is loaded or the
	// history ends.
	function revealMessage(threadId, messageId) {
		pendingRevealThread = 0
		const row = rowModel.rowForMessage(threadId, messageId)
		if (revealRow(row, String(messageId)))
			return true
		if (!commands.canLoadOlder)
			return false
		pendingRevealThread = threadId
		pendingRevealMessage = messageId
		pendingRevealPages = 20
		followTail = false
		commands.loadOlder()
		return false
	}
	function continuePendingReveal() {
		if (pendingRevealThread === 0)
			return
		const row = rowModel.rowForMessage(pendingRevealThread, pendingRevealMessage)
		if (row >= 0) {
			pendingRevealThread = 0
			revealRow(row, String(pendingRevealMessage))
		} else if (--pendingRevealPages > 0 && commands.canLoadOlder && !commands.loadingOlder) {
			commands.loadOlder()
		} else if (!commands.canLoadOlder || pendingRevealPages <= 0) {
			pendingRevealThread = 0
		}
	}
	function firstVisibleRow() {
		let best = null
		for (let y = contentY + 1; y < contentY + height; y += 8) {
			const index = indexAt(width / 2, y)
			if (index >= 0) {
				best = itemAtIndex(index)
				break
			}
		}
		return best
	}
	// Automation and visual-fixture readiness, in the shape the shell's
	// timelinePresentationState() reports for the current timeline.
	function presentationState() {
		const first = firstVisibleRow()
		const visibleRows = []
		for (let y = contentY; y < contentY + height; ) {
			const index = indexAt(width / 2, y + 1)
			const item = index >= 0 ? itemAtIndex(index) : null
			if (!item) {
				y += 8
				continue
			}
			const row = { "id": String(item.stableId || ""), "y": item.y - contentY, "height": item.height }
			if (item.previewGeometry)
				row.preview = item.previewGeometry()
			visibleRows.push(row)
			y = item.y + item.height
		}
		// Rows laid over each other, or a row drawn taller than the list placed it.
		const placed = contentItem.children.filter(child => child.visible && child.stableId !== undefined
			&& child.y + child.height > contentY && child.y < contentY + height)
			.sort((a, b) => a.y - b.y)
		let overlappingRows = 0
		for (let i = 1; i < placed.length; ++i) {
			if (placed[i - 1].y + placed[i - 1].height > placed[i].y + 1)
				++overlappingRows
		}
		const settled = !moving && !flicking && !tailFollower.running && hydrationQueue.length === 0
		return {
			"scopeToken": String(rowModel.scopeToken || ""),
			"generation": 0,
			"count": count,
			"presentationPending": false,
			"presentationFinalizing": false,
			"scopeResetPending": false,
			"stablePresentationReady": true,
			"stableFrameCommitted": true,
			"stableFrameConsecutiveCount": 2,
			"stableFrameCommittedAt": Date.now(),
			"visibleFrameCommitted": true,
			"settled": settled,
			"firstVisibleId": first ? String(first.stableId || "") : "",
			"firstVisibleOffset": first ? first.y - contentY : 0,
			"contentY": contentY,
			"followTail": followTail,
			"visibleRows": visibleRows,
			"overlappingRows": overlappingRows,
			"visibleDeferredSummaryCount": 0,
			"visibleBlankPresentationCount": 0,
			"visibleBlankPreviewSurfaceCount": 0,
			"visiblePresentationHeightPendingCount": 0,
			"visiblePresentationHeightMismatchCount": 0,
			"maximumVisiblePresentationHeightError": 0,
			"presentationFastPath": false,
			"sparseBottomAlignmentActive": false,
			"sparseBottomAlignmentError": 0,
			"sparseTailContentYError": 0,
			"pendingHydrationCount": hydrationQueue.length,
			"historyLoaderVisible": !!footerItem && footerItem.height > 0
		}
	}
	function resetForScope() {
		followTail = true
		liveInsertLast = -1
		hydrationRequested = ({})
		hydrationQueue = []
		hydrationHighPriority = false
		pendingRevealThread = 0
		flashStableId = ""
		anchorItem = null
		anchorStableId = ""
		previewHeights = ({})
		previewHeightCount = 0
		messageActions.close()
	}

	model: rowModel
	verticalLayoutDirection: ListView.BottomToTop
	reuseItems: true
	// Rows (and their cards) must exist before they scroll into view. With the
	// list laid out bottom to top the cache buffer is only built behind the last
	// movement, so a reader scrolling up met every row as it was created. The
	// display margins lay rows out above (end) and below (beginning) the
	// viewport as part of every layout, in both directions.
	cacheBuffer: Math.max(1200, Math.round(height * 2))
	displayMarginEnd: Math.max(600, Math.round(height))
	displayMarginBeginning: Math.max(300, Math.round(height * 0.5))
	boundsBehavior: Flickable.StopAtBounds
	maximumFlickVelocity: 7000
	flickDeceleration: 3500
	pixelAligned: true
	clip: true
	activeFocusOnTab: true
	Accessible.role: Accessible.List
	Accessible.name: qsTr("Messages")

	// Geometry binds to rowView, never to the row's own view property, so the
	// first layout of a new row already uses the real lane width.
	delegate: ConversationRow {
		view: rowView
		width: rowView.width
		laneAvailableWidth: Math.max(1, rowView.width - rowView.leftMargin - rowView.rightMargin - Theme.space4)
		laneMaximumWidth: rowView.host ? rowView.host.conversationLaneMaximumWidth : Theme.chatLaneMaximumWidth
		searchCurrent: stableId.length > 0 && (stableId === rowView.flashStableId
			|| (!!rowView.host && rowView.host.conversationSearchOpen
				&& stableId === String(rowView.rowModel.currentMatchStableId || "")))
		accessibilitySuppressed: !!rowView.host && rowView.host.backgroundAccessibilitySuppressed === true
	}

	// With the list laid out bottom to top the footer sits above the oldest row.
	footer: Item {
		width: rowView.width
		height: rowView.commands.canLoadOlder || rowView.commands.loadingOlder ? 40 : 0
		visible: height > 0

		Row {
			anchors.centerIn: parent
			spacing: Theme.space2
			visible: rowView.commands.loadingOlder

			ModernBusyIndicator {
				anchors.verticalCenter: parent.verticalCenter
				running: parent.visible
				Accessible.name: qsTr("Loading older messages")
			}
			Text {
				anchors.verticalCenter: parent.verticalCenter
				text: qsTr("Loading older messages…")
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
			}
		}
	}

	onContentYChanged: {
		finishCardsNearView()
		// The reader's scrolling picks a new reading row; anything else moving
		// the view is layout, which the lock undoes.
		if (readerScrolling || followTail)
			pickAnchor()
		else
			holdAnchor()
		Qt.callLater(loadOlderIfNeeded)
	}
	onHeightChanged: pickAnchor()
	onOriginYChanged: if (!readerScrolling) holdAnchor()
	onContentHeightChanged: if (!readerScrolling) holdAnchor()
	onCountChanged: {
		Qt.callLater(loadOlderIfNeeded)
		Qt.callLater(continuePendingReveal)
		Qt.callLater(closePlaybackIfGone)
	}
	onMovementStarted: followTail = false
	onMovementEnded: {
		followTail = nearTail()
		pickAnchor()
	}
	// Far jumps cut straight to the newest page; short distances ease in below.
	onFollowTailChanged: {
		if (followTail && tailY() - contentY > height * 2)
			positionViewAtBeginning()
		pickAnchor()
	}
	onAtLatestChanged: markReadTimer.restart()
	onScrollbarDraggingChanged: {
		if (scrollbarDragging) {
			followTail = false
		} else {
			followTail = nearTail()
			pickAnchor()
			Qt.callLater(loadOlderIfNeeded)
		}
	}

	Connections {
		target: view.anchorItem
		function onYChanged() {
			view.holdAnchor()
		}
	}
	Connections {
		target: view.rowModel
		function onModelReset() {
			view.resetForScope()
		}
		function onRowsInserted(parent, first, last) {
			if (first === 0 && view.count > last + 1) {
				view.liveInsertAt = Date.now()
				view.liveInsertLast = last
				markReadTimer.restart()
			}
		}
	}
	Connections {
		target: view.commands
		function onSendingChanged() {
			if (view.commands.sending)
				view.followTail = true
		}
		function onUnreadCountChanged() {
			markReadTimer.restart()
		}
	}
	Connections {
		target: Qt.application
		function onStateChanged() {
			markReadTimer.restart()
		}
	}

	FrameAnimation {
		id: tailFollower
		running: view.visible && view.followTail && view.tailY() - view.contentY > 0.5
		onTriggered: {
			const gap = view.tailY() - view.contentY
			const step = gap * (1 - Math.exp(-frameTime * 16))
			view.following = true
			// pixelAligned rounds sub-pixel steps away, so finish with a snap.
			view.contentY = step < 1 ? view.tailY() : view.contentY + step
			view.following = false
		}
	}

	Timer {
		id: hydrationTimer
		interval: 60
		onTriggered: {
			const batch = view.hydrationQueue.splice(0, 32)
			const highPriority = view.hydrationHighPriority
			view.hydrationHighPriority = false
			if (batch.length > 0)
				view.commands.hydrate(batch, highPriority)
			if (view.hydrationQueue.length > 0)
				start()
		}
	}

	Timer {
		id: flashTimer
		interval: 1600
		onTriggered: view.flashStableId = ""
	}

	Timer {
		id: markReadTimer
		interval: 400
		onTriggered: {
			if (view.visible && view.atLatest && Qt.application.state === Qt.ApplicationActive
					&& view.commands.unreadCount > 0)
				view.commands.markRead()
		}
	}

	ScrollBar.vertical: ModernScrollBar {
		id: verticalScrollBar
	}

	// The shell's wheel and middle-button scrolling: a full step per wheel notch,
	// eased. Like the scrollbar it writes contentY directly, so it carries the
	// reader's intent itself.
	MiddleDragScrollHandler {
		id: scrollHandler
		parent: view
		targetFlickable: view
		horizontalEnabled: false
		navigationEnabled: view.interactive && !view.scrollbarDragging
		smoothWheelEnabled: true
		// A quarter of the view per notch at Windows' default of three lines,
		// scaled by the system's lines-per-notch setting.
		wheelStep: Math.round(Math.max(120, Math.min(280, view.height * 0.25))
			* Math.max(1, Qt.styleHints.wheelScrollLines) / 3)
		onScrollingStarted: view.followTail = false
		onScrollingEnded: {
			view.followTail = view.nearTail()
			view.pickAnchor()
			Qt.callLater(view.loadOlderIfNeeded)
		}
	}

	ModernMenu {
		id: messageActions
		objectName: "conversationMessageActions"
		accessibleName: qsTr("Message actions")
		property string targetId: ""
		property var targetRow: null
		property string targetText: ""
		property var targetReactions: []
		property var targetAnchor: null
		property bool targetCanReply: false
		property bool targetCanReact: false
		property bool targetCanDelete: false
		property bool targetCanRetry: false
		onClosed: {
			targetId = ""
			targetRow = null
		}

		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "message.reply", "label": qsTr("Reply"), "icon": "reply" })
			visible: messageActions.targetCanReply
			height: visible ? rowImplicitHeight : 0
			onActionRequested: view.commands.reply(messageActions.targetId)
		}
		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "message.react", "label": qsTr("Add reaction"), "icon": "reaction" })
			visible: messageActions.targetCanReact
			height: visible ? rowImplicitHeight : 0
			onActionRequested: {
				if (!view.hostReactionPopup) {
					if (messageActions.targetRow)
						messageActions.targetRow.inlineReactionsOpen = true
				} else if (view.host && messageActions.targetAnchor) {
					view.host.showQuickReactions(messageActions.targetId, -1, messageActions.targetAnchor,
						messageActions.targetReactions)
				}
			}
		}
		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "message.copy", "label": qsTr("Copy text"), "icon": "copy" })
			visible: messageActions.targetText.length > 0
			height: visible ? rowImplicitHeight : 0
			onActionRequested: {
				clipboardHelper.text = messageActions.targetText
				clipboardHelper.selectAll()
				clipboardHelper.copy()
				clipboardHelper.text = ""
			}
		}
		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "message.retry", "label": qsTr("Retry"), "icon": "retry" })
			visible: messageActions.targetCanRetry
			height: visible ? rowImplicitHeight : 0
			onActionRequested: view.commands.retry(messageActions.targetId)
		}
		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "message.delete", "label": qsTr("Delete"), "icon": "delete",
				"tone": "danger" })
			visible: messageActions.targetCanDelete
			height: visible ? rowImplicitHeight : 0
			onActionRequested: view.commands.remove(messageActions.targetId)
		}
	}

	Loader {
		id: detailLoader
		parent: view
		active: false
		sourceComponent: EmbedDetailSheet {
			onOpenRequested: url => Qt.openUrlExternally(url)
			onPlayRequested: (stableId, mediaIndex) => view.playFromDetails(stableId, mediaIndex)
			onImageOpenRequested: (source, title, stableId) => view.host
				&& view.host.openManagedPreviewImage(source, title, stableId)
		}
	}

	ModernMenu {
		id: linkActions
		objectName: "conversationLinkActions"
		accessibleName: qsTr("Link actions")
		property string targetUrl: ""

		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "link.open", "label": qsTr("Open link"), "icon": "external" })
			onActionRequested: Qt.openUrlExternally(linkActions.targetUrl)
		}
		PayloadMenuItem {
			payload: ({ "kind": "action", "id": "link.copy", "label": qsTr("Copy link"), "icon": "copy" })
			onActionRequested: {
				clipboardHelper.text = linkActions.targetUrl
				clipboardHelper.selectAll()
				clipboardHelper.copy()
				clipboardHelper.text = ""
			}
		}
	}

	// The conversation's player. It follows the playing card every frame while
	// it plays, and docks in the corner when the card is off screen.
	Item {
		id: playerHost
		objectName: "conversationPlayer"
		parent: view
		z: 20
		visible: playerLoader.active
		height: (view.playerDocked ? dockBar.height : 0) + (playerLoader.item ? playerLoader.item.implicitHeight : 0)

		Rectangle {
			anchors.fill: parent
			anchors.margins: -1
			visible: view.playerDocked
			radius: Theme.innerRadius
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
		}
		Item {
			id: dockBar
			visible: view.playerDocked
			width: parent.width
			height: visible ? 32 : 0

			Text {
				anchors.left: parent.left
				anchors.leftMargin: Theme.space2
				anchors.right: dockReturn.left
				anchors.verticalCenter: parent.verticalCenter
				text: String(view.playbackInfo.title || "")
				textFormat: Text.PlainText
				elide: Text.ElideRight
				color: Theme.textMain
				font.pixelSize: Theme.fontCaption
			}
			ModernIconButton {
				id: dockReturn
				objectName: "conversationPlayerReturn"
				anchors.right: dockClose.left
				anchors.verticalCenter: parent.verticalCenter
				dense: true
				iconName: "chevron-up"
				Accessible.name: qsTr("Show the message")
				onClicked: {
					const index = view.rowModel ? view.rowModel.rowForStableId(view.playingStableId) : -1
					if (index >= 0)
						view.positionAtIndex(index, ListView.Center)
				}
			}
			ModernIconButton {
				id: dockClose
				objectName: "conversationPlayerClose"
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				dense: true
				iconName: "close"
				Accessible.name: qsTr("Close the player")
				onClicked: {
					if (view.session)
						view.session.close()
					view.ownedSessionId = ""
				}
			}
		}
		Loader {
			id: playerLoader
			y: dockBar.height
			width: parent.width
			active: view.playingStableId.length > 0
			// Incubated like the card's own player: a WebEngineView created in one
			// synchronous pass gets its profile after the default one and crashes
			// in QQuickWebEngineView::setProfile.
			asynchronous: true
			onLoaded: view.placePlayer()
		}
		FrameAnimation {
			running: playerLoader.active
			onTriggered: view.placePlayer()
		}
	}

	TextEdit {
		id: clipboardHelper
		visible: false
		Accessible.ignored: true
	}
}
