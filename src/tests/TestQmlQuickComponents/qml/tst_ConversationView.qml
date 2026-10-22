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
