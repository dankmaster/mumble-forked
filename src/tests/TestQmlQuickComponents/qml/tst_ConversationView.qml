import QtQuick
import QtTest

// Scrolling contract of the v2 timeline (docs/chat-timeline-v2-plan.md): the view
// opens at the newest message, follows new messages only while the reader is at
// the bottom, and never moves what the reader is looking at while they read
// history, whether a message arrives or a row below them changes height.
TestCase {
	id: testCase
	name: "ConversationView"
	when: windowShown
	visible: true
	width: 640
	height: 480

	Loader {
		id: viewLoader
		anchors.fill: parent
	}

	function message(id, text) {
		return {
			"messageId": id,
			"threadId": 1,
			"stableId": String(id),
			"authorName": "Author " + (id % 3),
			"timeText": "12:00",
			"groupStart": id % 4 === 0,
			"plainText": text,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": text }]
		}
	}
	function history(count) {
		const rows = []
		for (let id = count; id >= 1; --id)
			rows.push(message(id, "Message " + id + (id % 5 === 0 ? " with a second sentence that wraps onto the next line of the lane." : "")))
		return rows
	}
	function view() {
		return viewLoader.item
	}
	function settleAtNewest() {
		tryVerify(() => Math.abs(view().tailY() - view().contentY) < 1, 3000, "view settles at the newest row")
	}
	function readHistory() {
		const list = view()
		settleAtNewest()
		list.followTail = false
		list.contentY = list.tailY() - list.height * 3
		list.forceLayout()
		wait(50)
		const anchorIndex = list.indexAt(list.width / 2, list.contentY + list.height / 2)
		verify(anchorIndex > 4, "reading point is in history: " + anchorIndex)
		return anchorIndex
	}

	function init() {
		chatTimelineV2.resetRows("channel:1", history(240))
		viewLoader.setSource("qrc:/qml-shell/ConversationView.qml", { "host": null })
		tryVerify(() => viewLoader.status === Loader.Ready && !!viewLoader.item)
	}
	function cleanup() {
		viewLoader.source = ""
		chatTimelineV2.resetRows("channel:1", [])
	}

	function test_opens_at_newest_message() {
		settleAtNewest()
		verify(view().atLatest)
		const newest = view().itemAtIndex(0)
		verify(!!newest)
		fuzzyCompare(newest.y + newest.height, view().contentY + view().height - view().bottomMargin, 1)
	}

	function test_new_message_eases_in_while_following() {
		settleAtNewest()
		chatTimelineV2.insertRows(0, [message(241, "A live message")])
		tryVerify(() => !!view().itemAtIndex(0) && view().itemAtIndex(0).stableId === "241")
		settleAtNewest()
		verify(view().followTail)
	}

	function test_reading_position_holds_when_message_arrives() {
		const list = view()
		const anchorIndex = readHistory()
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		chatTimelineV2.insertRows(0, [message(241, "A live message"), message(242, "Another one")])
		wait(100)
		verify(!list.followTail)
		fuzzyCompare(anchor.y - list.contentY, screenY, 1)
	}

	function test_reading_position_holds_when_row_below_grows() {
		const list = view()
		const anchorIndex = readHistory()
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		const grownIndex = anchorIndex - 2
		const grown = list.itemAtIndex(grownIndex)
		verify(!!grown)
		const before = grown.height
		const longText = "This row gains several lines of text while the reader looks at an older message. "
			.repeat(6)
		chatTimelineV2.updateRow(grownIndex, {
			"plainText": longText,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": longText }]
		})
		tryVerify(() => grown.height > before + 10)
		wait(50)
		fuzzyCompare(anchor.y - list.contentY, screenY, 1)
	}

	function test_scrollbar_drag_reaches_the_top_and_stays_there() {
		const list = view()
		settleAtNewest()
		const bar = list.ScrollBar.vertical
		verify(!!bar)
		const x = bar.width / 2
		const thumbY = (bar.position + bar.size / 2) * bar.height
		mousePress(bar, x, thumbY)
		for (let step = 1; step <= 10; ++step)
			mouseMove(bar, x, thumbY - (thumbY - 1) * step / 10)
		// Hold at the top: neither following, paging nor compensation may pull it away.
		wait(300)
		verify(!list.followTail)
		verify(list.contentY - list.originY < list.height, "top reached: " + (list.contentY - list.originY))
		const heldY = list.contentY
		wait(200)
		fuzzyCompare(list.contentY, heldY, 1)
		mouseRelease(bar, x, 1)
		verify(!list.followTail)
	}

	function test_reveal_message_centers_and_outlines_it() {
		const list = view()
		settleAtNewest()
		verify(list.revealMessage(1, 150))
		verify(!list.followTail)
		compare(list.flashStableId, "150")
		const index = 240 - 150
		const item = list.itemAtIndex(index)
		verify(!!item)
		verify(item.searchCurrent)
		const center = item.y + item.height / 2 - list.contentY
		verify(Math.abs(center - list.height / 2) < item.height + 2, "centered: " + center)
		// A message that is not loaded and cannot be paged in is not found.
		verify(!list.revealMessage(1, 999))
	}

	QtObject {
		id: recordingCommands
		property var calls: []
		property bool canLoadOlder: true
		readonly property bool loadingOlder: false
		readonly property real unreadCount: 0
		readonly property bool sending: false
		readonly property bool hydrationAvailable: true
		function record(call) { calls = calls.concat([call]) }
		function reply(stableId) { record("reply:" + stableId) }
		function toggleReaction(stableId, emoji) { record("react:" + stableId + ":" + emoji) }
		function retry(stableId) { record("retry:" + stableId) }
		function remove(stableId) { record("remove:" + stableId) }
		function retryAttachmentPreview(stableId, assetId) { record("retryPreview:" + stableId) }
		function validHydrationId(stableId) { return String(stableId || "").length > 0 }
		function hydrate(stableIds, highPriority) { record("hydrate:" + stableIds.join(",")) }
		function loadOlder() { record("older") }
		function markRead() { record("read") }
	}

	function test_custom_commands_drive_paging_hydration_and_inline_reactions() {
		recordingCommands.calls = []
		const list = view()
		list.commands = recordingCommands
		list.hostReactionPopup = false
		settleAtNewest()
		// A row whose attachment is still loading asks the commands for it by its own id.
		const row = message(241, "With a loading attachment")
		row.stableId = "dm:7:241"
		row.attachments = [{ "id": "a", "kind": "file", "state": "loading", "name": "notes.txt" }]
		chatTimelineV2.insertRows(0, [row])
		tryVerify(() => recordingCommands.calls.indexOf("hydrate:dm:7:241") >= 0, 2000,
			"calls: " + recordingCommands.calls.join(" "))
		// Reaching the top pages older history through the commands.
		list.followTail = false
		list.positionViewAtEnd()
		list.contentY = list.originY
		tryVerify(() => recordingCommands.calls.indexOf("older") >= 0, 2000)
		// Without the shell popup the reaction bar opens inside the row.
		list.followTail = true
		settleAtNewest()
		const first = list.itemAtIndex(0)
		first.inlineReactionsOpen = true
		verify(first.actionsOpen)
	}

	function test_row_height_is_stable_across_reuse() {
		const list = view()
		settleAtNewest()
		const heights = []
		for (let index = 0; index < 6; ++index)
			heights.push(list.itemAtIndex(index).height)
		list.followTail = false
		list.positionViewAtIndex(200, ListView.Center)
		wait(50)
		list.followTail = true
		settleAtNewest()
		for (let index = 0; index < 6; ++index)
			compare(list.itemAtIndex(index).height, heights[index], "row " + index)
	}
}
