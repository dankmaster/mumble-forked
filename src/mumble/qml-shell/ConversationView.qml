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
// - While reading history, a row at or below the reading point that changes
//   height moves the view by the same amount in the same frame.
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
	readonly property bool atLatest: followTail || tailY() - contentY < 4
	// Dragging the scrollbar sets contentY directly and starts no movement, so
	// it gets its own intent handling: it never follows, never pages and is
	// never compensated while held.
	readonly property bool scrollbarDragging: verticalScrollBar.pressed
	readonly property ListView rowView: view
	// A message the reader jumped to (search match or replied message) is
	// outlined briefly.
	property string flashStableId: ""
	property int pendingRevealThread: 0
	property int pendingRevealMessage: 0
	property int pendingRevealPages: 0
	property var previewHeights: ({})
	property int previewHeightCount: 0

	function tailY() {
		return originY + contentHeight + bottomMargin - height
	}
	function jumpToLatest() {
		followTail = true
	}
	function arrivalActive(rowIndex) {
		return rowIndex <= liveInsertLast && Date.now() - liveInsertAt < 400
	}
	function compensateRowResize(rowIndex, delta) {
		if (followTail || scrollbarDragging || delta === 0)
			return
		const anchorIndex = indexAt(width / 2, contentY + height / 2)
		if (anchorIndex < 0 || rowIndex > anchorIndex)
			return
		following = true
		contentY -= delta
		following = false
	}
	function loadOlderIfNeeded() {
		if (!visible || count === 0 || scrollbarDragging || !commands.canLoadOlder || commands.loadingOlder)
			return
		if (contentY - originY < height)
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
		positionViewAtIndex(row, ListView.Center)
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
			visibleRows.push({ "id": String(item.stableId || ""), "y": item.y - contentY, "height": item.height })
			y = item.y + item.height
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
		previewHeights = ({})
		previewHeightCount = 0
		messageActions.close()
	}

	model: rowModel
	verticalLayoutDirection: ListView.BottomToTop
	reuseItems: true
	cacheBuffer: Math.max(0, Math.round(height))
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
		accessibilityViewportVisible: y + height > rowView.contentY && y < rowView.contentY + rowView.height
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

	onContentYChanged: Qt.callLater(loadOlderIfNeeded)
	onCountChanged: {
		Qt.callLater(loadOlderIfNeeded)
		Qt.callLater(continuePendingReveal)
	}
	onMovementStarted: followTail = false
	onMovementEnded: followTail = tailY() - contentY < 4
	// Far jumps cut straight to the newest page; short distances ease in below.
	onFollowTailChanged: if (followTail && tailY() - contentY > height * 2) positionViewAtBeginning()
	onAtLatestChanged: markReadTimer.restart()
	onScrollbarDraggingChanged: {
		if (scrollbarDragging) {
			followTail = false
		} else {
			followTail = tailY() - contentY < 4
			Qt.callLater(loadOlderIfNeeded)
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

	TextEdit {
		id: clipboardHelper
		visible: false
		Accessible.ignored: true
	}
}
