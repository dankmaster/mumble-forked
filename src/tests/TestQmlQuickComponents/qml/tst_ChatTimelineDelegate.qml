import QtQuick
import QtTest
import Mumble.Theme 1.0
import "qrc:/qml-shell" as Shell

TestCase {
	id: testCase
	name: "ChatTimelineDelegate"
	when: windowShown
	width: 420
	height: 320

	ListModel {
		id: directRows
		Component.onCompleted: append([
			{ "stableId": "dm:1", "startsGroup": true, "own": false, "bodyHeight": 32 },
			{ "stableId": "dm:2", "startsGroup": true, "own": true, "bodyHeight": 32 },
			{ "stableId": "dm:3", "startsGroup": true, "own": false, "bodyHeight": 32 }
		])
	}

	ListModel {
		id: deliveryRows
		Component.onCompleted: append([
			{ "stableId": "delivery:1", "startsGroup": true, "own": true, "bodyHeight": 30 },
			{ "stableId": "delivery:2", "startsGroup": false, "own": true, "bodyHeight": 20 },
			{ "stableId": "delivery:3", "startsGroup": false, "own": true, "bodyHeight": 38 }
		])
	}

	ListView {
		id: timeline
		anchors.fill: parent
		model: directRows
		clip: true
		reuseItems: true
		delegate: Shell.ChatMessageFrame {
			required property real bodyHeight
			laneAvailableWidth: timeline.width
			width: laneAvailableWidth
			bodyImplicitHeight: bodyHeight
			systemMessage: false
			objectName: "chat-frame:" + stableId
			Text {
				anchors.fill: parent
				text: stableId
			}
		}
	}

	function init() {
		testCase.width = 420
		timeline.model = directRows
		timeline.forceLayout()
		wait(20)
	}

	function test_scope_replacement_does_not_paint_pooled_rows() {
		const previousDelegates = []
		for (let index = 0; index < directRows.count; ++index) {
			const item = timeline.itemAtIndex(index)
			verify(item !== null)
			compare(item.stableId, "dm:" + (index + 1))
			previousDelegates.push(item)
		}
		timeline.model = deliveryRows
		timeline.forceLayout()
		wait(20)
		for (let index = 0; index < deliveryRows.count; ++index) {
			const item = timeline.itemAtIndex(index)
			verify(item !== null)
			compare(item.stableId, "delivery:" + (index + 1))
		}
		for (const oldItem of previousDelegates) {
			if (oldItem && String(oldItem.stableId || "").indexOf("dm:") === 0)
				verify(!oldItem.visible, "Removed direct-message delegate remained visible")
		}

		timeline.model = null
		timeline.forceLayout()
		wait(20)
		for (const oldItem of previousDelegates) {
			if (oldItem)
				verify(!oldItem.visible, "Pooled delegate remained visible without a model")
		}
	}

	function test_compact_rows_reserve_padding_and_minimum_height() {
		timeline.model = deliveryRows
		timeline.forceLayout()
		wait(20)
		const compact = timeline.itemAtIndex(1)
		const status = timeline.itemAtIndex(2)
		verify(compact !== null)
		verify(status !== null)
		compare(compact.height, 40)
		compare(compact.contentItem.height, 20)
		compare(status.height, 58)
		compare(status.contentItem.height, 38)
		verify(status.y >= compact.y + compact.height)
		verify(timeline.itemAtIndex(0).topGap > compact.topGap)
		compare(compact.topGap, Theme.space1)
		compare(timeline.itemAtIndex(0).topGap, Theme.space3)
	}

	function test_narrow_lane_is_bounded_and_outgoing_message_is_adaptive() {
		const incoming = timeline.itemAtIndex(0)
		const outgoing = timeline.itemAtIndex(1)
		verify(incoming !== null)
		verify(outgoing !== null)
		compare(incoming.laneWidth, 420)
		compare(incoming.messageWidth, 420)
		compare(incoming.surfaceX, 0)
		compare(outgoing.messageWidth, 260)
		compare(outgoing.surfaceX, 160)
		verify(outgoing.surfaceX + outgoing.surfaceWidth <= timeline.width)
		compare(incoming.surfaceBorderWidth, 0)
		compare(incoming.surfaceColor.a, 0)
		verify(outgoing.surfaceColor.a > 0)
	}

	function test_wide_lane_is_centered_and_rich_outgoing_content_keeps_width() {
		testCase.width = 1080
		timeline.forceLayout()
		wait(20)
		const incoming = timeline.itemAtIndex(0)
		const outgoing = timeline.itemAtIndex(1)
		verify(incoming !== null)
		verify(outgoing !== null)
		compare(incoming.laneWidth, 840)
		compare(incoming.messageWidth, 840)
		compare(incoming.surfaceX, 120)
		compare(outgoing.messageWidth, 260)
		compare(outgoing.surfaceX, 700)

		outgoing.wideContent = true
		compare(outgoing.messageWidth, 840)
		compare(outgoing.surfaceX, 120)
		outgoing.wideContent = false
	}
}
