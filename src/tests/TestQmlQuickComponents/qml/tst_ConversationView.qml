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

	function test_reading_position_holds_when_cached_row_below_viewport_grows() {
		const list = view()
		const anchorIndex = readHistory()
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		// The newest row still created below the viewport (inside the cache buffer).
		let grownIndex = -1
		for (let index = 0; index < anchorIndex; ++index) {
			const item = list.itemAtIndex(index)
			if (item && item.y >= list.contentY + list.height) {
				grownIndex = index
				break
			}
		}
		verify(grownIndex >= 0, "a cached row below the viewport exists")
		const grown = list.itemAtIndex(grownIndex)
		const before = grown.height
		const longText = "A card below the viewport finishes loading. ".repeat(10)
		chatTimelineV2.updateRow(grownIndex, {
			"plainText": longText,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": longText }]
		})
		const contentYBefore = list.contentY, grownYBefore = grown.y, anchorYBefore = anchor.y
		tryVerify(() => grown.height > before + 10)
		wait(50)
		console.log("[cv] belowgrow index", grownIndex, "anchor", anchorIndex, "delta", grown.height - before,
			"contentY", contentYBefore, "->", list.contentY, "grownY", grownYBefore, "->", grown.y,
			"anchorY", anchorYBefore, "->", anchor.y, "viewBottom", contentYBefore + list.height)
		fuzzyCompare(anchor.y - list.contentY, screenY, 1)
	}

	function test_reading_position_holds_when_cached_row_above_viewport_grows() {
		const list = view()
		const anchorIndex = readHistory()
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		let grownIndex = -1
		for (let index = anchorIndex + 1; index < list.count; ++index) {
			const item = list.itemAtIndex(index)
			if (item && item.y + item.height <= list.contentY) {
				grownIndex = index
				break
			}
		}
		verify(grownIndex >= 0, "a cached row above the viewport exists")
		const grown = list.itemAtIndex(grownIndex)
		const before = grown.height
		const longText = "A card above the viewport finishes loading. ".repeat(10)
		chatTimelineV2.updateRow(grownIndex, {
			"plainText": longText,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": longText }]
		})
		tryVerify(() => grown.height > before + 10)
		wait(50)
		fuzzyCompare(anchor.y - list.contentY, screenY, 1)
	}

	function test_reading_position_holds_when_row_above_grows() {
		const list = view()
		const anchorIndex = readHistory()
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		const grownIndex = anchorIndex + 2
		const grown = list.itemAtIndex(grownIndex)
		verify(!!grown)
		const before = grown.height
		const longText = "This row above the reading point gains several lines. ".repeat(8)
		chatTimelineV2.updateRow(grownIndex, {
			"plainText": longText,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": longText }]
		})
		tryVerify(() => grown.height > before + 10)
		wait(50)
		fuzzyCompare(anchor.y - list.contentY, screenY, 1)
	}

	// Link-heavy history: rows grow shortly after they are created (cards
	// loading), and older pages arrive while the reader scrolls up with the wheel.
	QtObject {
		id: pagingCommands
		property int pages: 0
		property bool canLoadOlder: true
		property bool loadingOlder: false
		readonly property real unreadCount: 0
		readonly property bool sending: false
		readonly property bool hydrationAvailable: false
		function reply(stableId) {}
		function toggleReaction(stableId, emoji) {}
		function retry(stableId) {}
		function remove(stableId) {}
		function retryAttachmentPreview(stableId, assetId) {}
		function validHydrationId(stableId) { return false }
		function hydrate(stableIds, highPriority) {}
		function loadOlder() {
			if (loadingOlder || !canLoadOlder)
				return
			loadingOlder = true
			pageTimer.start()
		}
		function markRead() {}
	}
	Timer {
		id: pageTimer
		// About one server round trip and worker build for a history page.
		interval: 400
		onTriggered: {
			const first = 1000 - pagingCommands.pages * 40
			const rows = []
			for (let id = first; id > first - 40; --id)
				rows.push(message(id, "Older link " + id))
			chatTimelineV2.insertRows(chatTimelineV2.count, rows)
			pagingCommands.pages += 1
			pagingCommands.loadingOlder = false
			pagingCommands.canLoadOlder = pagingCommands.pages < 4
		}
	}
	property var grownRows: ({})
	Timer {
		id: cardLoader
		interval: 30
		repeat: true
		onTriggered: {
			const list = view()
			if (!list)
				return
			let grownNow = 0
			for (let index = 0; index < list.count && grownNow < 3; ++index) {
				const item = list.itemAtIndex(index)
				if (!item || testCase.grownRows[item.stableId])
					continue
				testCase.grownRows[item.stableId] = true
				const card = "Card " + item.stableId + ": a preview title and a description that takes several lines. "
					.repeat(1 + index % 4)
				chatTimelineV2.updateRow(index, {
					"plainText": card,
					"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": card }]
				})
				++grownNow
			}
		}
	}

	// Scrolls up through link-heavy history with the wheel until the top and
	// reports how the reader's view behaved.
	function scrollUpThroughHistory(prefetchDistance, prefetchRows) {
		const list = view()
		pagingCommands.pages = 0
		pagingCommands.canLoadOlder = true
		pagingCommands.loadingOlder = false
		testCase.grownRows = ({})
		const rows = []
		for (let id = 1100; id > 1040; --id)
			rows.push(message(id, "Link " + id))
		chatTimelineV2.resetRows("channel:links", rows)
		list.commands = pagingCommands
		list.olderPrefetchDistance = prefetchDistance
		list.olderPrefetchRows = prefetchRows
		cardLoader.start()
		settleAtNewest()
		wait(300)
		const result = { "followed": 0, "backward": 0, "stalls": 0, "steps": 0 }
		for (let step = 0; step < 800 && !(list.contentY - list.originY < 1 && !pagingCommands.canLoadOlder
				&& !pagingCommands.loadingOlder); ++step) {
			const index = list.indexAt(list.width / 2, list.contentY + list.height / 2)
			const item = index >= 0 ? list.itemAtIndex(index) : null
			const before = item ? item.y - list.contentY : 0
			const contentYBefore = list.contentY
			mouseWheel(list, list.width / 2, list.height / 2, 0, 360)
			wait(40)
			result.steps = step + 1
			if (list.followTail)
				++result.followed
			// Scrolling up moves what the reader looks at down the screen.
			if (item && list.itemAtIndex(index) === item && item.y - list.contentY < before - 2)
				++result.backward
			// The wheel moved nothing although older history still exists.
			if (Math.abs(list.contentY - contentYBefore) < 1
					&& (pagingCommands.canLoadOlder || pagingCommands.loadingOlder))
				++result.stalls
		}
		cardLoader.stop()
		result.toTop = list.contentY - list.originY
		result.pages = pagingCommands.pages
		return result
	}

	function test_wheel_scrolls_up_through_growing_cards_and_pages() {
		// The next page is requested early enough that the reader never waits
		// against the top edge (requesting it one viewport from the top did).
		const early = scrollUpThroughHistory(view().olderPrefetchDistance, view().olderPrefetchRows)
		compare(early.stalls, 0, "the reader hit the top edge while older history was loading")
		compare(early.followed, 0, "the view started following while the reader scrolled up")
		compare(early.backward, 0, "what the reader looked at jumped against the scroll direction")
		verify(early.toTop < 1, "reached the top: " + early.toTop)
		compare(early.pages, 4)
	}

	function test_wheel_notch_scrolls_a_full_step_and_releases_following() {
		const list = view()
		settleAtNewest()
		const start = list.contentY
		mouseWheel(list, list.width / 2, list.height / 2, 0, 120)
		tryVerify(() => start - list.contentY >= 79, 2000, "moved: " + (start - list.contentY))
		wait(500)
		verify(!list.followTail)
		verify(!list.atLatest)
		mouseWheel(list, list.width / 2, list.height / 2, 0, -120)
		tryVerify(() => list.followTail, 2000, "following again at the bottom")
		settleAtNewest()
	}

	function test_rows_above_exist_before_they_scroll_in() {
		const list = view()
		settleAtNewest()
		for (let notch = 0; notch < 12; ++notch) {
			mouseWheel(list, list.width / 2, list.height / 2, 0, 120)
			wait(60)
			// Rows a viewport above what the reader sees are already laid out.
			const above = list.indexAt(list.width / 2, list.contentY - list.height * 0.9)
			verify(above >= 0 && !!list.itemAtIndex(above),
				"no row laid out above the viewport at notch " + notch)
		}
	}

	// Grows one row while the reader is in history and returns how far the row at
	// the reading point moved on screen.
	function anchorShiftAfterGrowing(pick) {
		const list = view()
		const anchorIndex = readHistory()
		wait(100)
		const anchor = list.itemAtIndex(anchorIndex)
		const screenY = anchor.y - list.contentY
		const index = pick(list, anchorIndex)
		const grown = index >= 0 ? list.itemAtIndex(index) : null
		verify(!!grown, "row to grow exists")
		const before = grown.height
		const text = "Growth text that wraps over several lines of the lane. ".repeat(6)
		chatTimelineV2.updateRow(index, { "plainText": text,
			"bodyBlocks": [{ "kind": "p", "level": 0, "format": "plain", "text": text }] })
		tryVerify(() => grown.height > before + 10)
		wait(80)
		return (anchor.y - list.contentY) - screenY
	}

	function test_reading_position_holds_wherever_a_row_grows_data() {
		return [
			{ "tag": "newest row in the display margin", "pick": (list, anchor) => 0 },
			{ "tag": "row below the reading point", "pick": (list, anchor) => anchor - 2 },
			{ "tag": "row above the reading point", "pick": (list, anchor) => anchor + 2 },
			{ "tag": "oldest laid-out row", "pick": (list, anchor) => {
				for (let index = list.count - 1; index > anchor; --index)
					if (list.itemAtIndex(index))
						return index
				return -1
			} }
		]
	}
	function test_reading_position_holds_wherever_a_row_grows(data) {
		fuzzyCompare(anchorShiftAfterGrowing(data.pick), 0, 1)
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
		// Reused rows lay out their columns in the next polish, before that frame
		// is drawn; compare the drawn heights.
		waitForRendering(list)
		for (let index = 0; index < 6; ++index)
			compare(list.itemAtIndex(index).height, heights[index], "row " + index)
	}
}
