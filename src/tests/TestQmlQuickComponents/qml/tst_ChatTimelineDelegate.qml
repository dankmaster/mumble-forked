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
			{ "stableId": "dm:1", "startsGroup": true, "own": false, "bodyHeight": 32, "separatorLabel": "" },
			{ "stableId": "dm:2", "startsGroup": true, "own": true, "bodyHeight": 32, "separatorLabel": "" },
			{ "stableId": "dm:3", "startsGroup": true, "own": false, "bodyHeight": 32, "separatorLabel": "" }
		])
	}

	ListModel {
		id: deliveryRows
		Component.onCompleted: append([
			{ "stableId": "delivery:1", "startsGroup": true, "own": true, "bodyHeight": 30, "separatorLabel": "" },
			{ "stableId": "delivery:2", "startsGroup": false, "own": true, "bodyHeight": 20, "separatorLabel": "" },
			{ "stableId": "delivery:3", "startsGroup": false, "own": true, "bodyHeight": 38, "separatorLabel": "" }
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
			required property string separatorLabel
			ListView.onPooled: accessibilityPooled = true
			ListView.onReused: accessibilityPooled = false
			Accessible.ignored: accessibilityFrameIgnored
			laneAvailableWidth: timeline.width
			width: laneAvailableWidth
			bodyImplicitHeight: bodyHeight
			dateSeparatorLabel: separatorLabel
			systemMessage: false
			objectName: "chat-frame:" + stableId
			Text {
				objectName: "chatTestSemanticText"
				anchors.fill: parent
				text: stableId
				Accessible.role: Accessible.StaticText
				Accessible.name: text
			}
		}
	}

	function init() {
		testCase.width = 420
		directRows.setProperty(0, "separatorLabel", "")
		timeline.model = directRows
		timeline.forceLayout()
		wait(20)
		for (let index = 0; index < timeline.count; ++index) {
			const item = timeline.itemAtIndex(index)
			if (item)
				item.accessibilitySuppressed = false
		}
	}

	function test_modal_suppression_prunes_promoted_message_content_and_restores_it() {
		const item = timeline.itemAtIndex(0)
		verify(item !== null)
		const semanticText = findChild(item, "chatTestSemanticText")
		const contentHost = findChild(item, "chatMessageContentHost")
		const barrier = findChild(item, "chatMessageAccessibilityBarrier")
		verify(semanticText !== null && contentHost !== null && barrier !== null)
		verify(!semanticText.Accessible.ignored)
		verify(contentHost.Accessible.ignored)

		item.accessibilitySuppressed = true
		tryCompare(barrier, "active", true)
		verify(barrier.bindingFor(item) !== null)
		verify(item.Accessible.ignored)
		tryCompare(contentHost.Accessible, "ignored", true)
		tryCompare(semanticText.Accessible, "ignored", true)

		item.accessibilitySuppressed = false
		tryCompare(barrier, "active", false)
		tryCompare(contentHost.Accessible, "ignored", true)
		tryCompare(semanticText.Accessible, "ignored", false)
	}

	function test_promoted_message_owners_bind_modal_suppression_directly() {
		directRows.setProperty(0, "separatorLabel", "Today")
		timeline.forceLayout()
		const item = timeline.itemAtIndex(0)
		verify(item !== null)
		const contentHost = findChild(item, "chatMessageContentHost")
		const separatorText = findChild(item, "chatDateSeparatorText")
		const barrier = findChild(item, "chatMessageAccessibilityBarrier")
		verify(contentHost !== null && separatorText !== null && barrier !== null)
		verify(contentHost.Accessible.ignored)
		const originalTargets = barrier.targets
		try {
			// Disable traversal to prove the layout-only Client host is never
			// materialized, while the date semantic owner follows the modal.
			barrier.targets = []
			item.accessibilitySuppressed = true
			tryCompare(contentHost.Accessible, "ignored", true)
			tryCompare(separatorText.Accessible, "ignored", true)

			item.accessibilitySuppressed = false
			tryCompare(contentHost.Accessible, "ignored", true)
			tryCompare(separatorText.Accessible, "ignored", false)
		} finally {
			item.accessibilitySuppressed = false
			barrier.targets = originalTargets
		}
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

	function test_accessibility_parentage_stays_stable_across_pooling() {
		const item = timeline.itemAtIndex(0)
		verify(item !== null)
		const surface = findChild(item, "chatMessageSurface")
		const barrier = findChild(item, "chatMessageAccessibilityBarrier")
		verify(surface !== null)
		verify(barrier !== null)
		verify(item.accessibilityFrameIgnored)
		verify(item.accessibilitySubtreeActive)

		item.accessibilityPooled = true
		verify(item.accessibilityFrameIgnored)
		verify(!item.accessibilitySubtreeActive)

		item.accessibilityPooled = false
		verify(item.accessibilityFrameIgnored)
		verify(item.accessibilitySubtreeActive)

		const laidOutHeight = item.height
		const surfaceHeight = surface.height
		item.accessibilityViewportVisible = false
		verify(item.accessibilityFrameIgnored)
		verify(!item.accessibilitySubtreeActive)
		tryCompare(barrier, "active", true)
		compare(surface.height, surfaceHeight)
		compare(item.height, laidOutHeight)
		item.accessibilityViewportVisible = true
		verify(item.accessibilitySubtreeActive)
		tryCompare(barrier, "active", false)
		compare(item.height, laidOutHeight)
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

	function test_date_separator_reserves_space_without_changing_message_geometry() {
		const incoming = timeline.itemAtIndex(0)
		const following = timeline.itemAtIndex(1)
		verify(incoming !== null)
		verify(following !== null)
		const originalHeight = incoming.height
		const originalSurfaceX = incoming.surfaceX
		const originalFollowingY = following.y
		directRows.setProperty(0, "separatorLabel", "TODAY")
		tryCompare(incoming, "dateSeparatorLabel", "TODAY")
		compare(incoming.hasDateSeparator, true)
		verify(incoming.dateSeparatorHeight > 0)
		tryCompare(incoming, "height", originalHeight + incoming.dateSeparatorHeight)
		timeline.forceLayout()
		tryCompare(following, "y", originalFollowingY + incoming.dateSeparatorHeight)
		compare(incoming.surfaceX, originalSurfaceX)
		const separator = incoming.dateSeparatorItem
		verify(separator !== null)
		compare(separator.height, incoming.dateSeparatorHeight)
		compare(separator.width, incoming.laneWidth)
		directRows.setProperty(0, "separatorLabel", "")
		tryCompare(incoming, "hasDateSeparator", false)
		tryCompare(incoming, "dateSeparatorHeight", 0)
		tryCompare(incoming, "height", originalHeight)
		timeline.forceLayout()
		tryCompare(following, "y", originalFollowingY)
	}

	function test_semantic_bubble_roles_keep_incoming_frame_transparent() {
		const incoming = timeline.itemAtIndex(0)
		const outgoing = timeline.itemAtIndex(1)
		verify(incoming !== null)
		verify(outgoing !== null)

		compare(incoming.bubbleColor, Theme.chatIncomingSurface)
		compare(incoming.bubbleBorderColor, Theme.chatIncomingBorder)
		compare(incoming.bubbleBorderWidth, 1)
		compare(incoming.surfaceColor.a, 0)
		compare(incoming.surfaceBorderWidth, 0)
		compare(outgoing.bubbleColor, Theme.chatOwnSurface)
		compare(outgoing.bubbleBorderColor, Theme.chatOwnBorder)
		compare(outgoing.surfaceColor, Theme.chatOwnSurface)
		compare(outgoing.surfaceBorderWidth, 1)
		compare(incoming.horizontalPadding, 0)
		compare(incoming.verticalPadding, 0)
		compare(outgoing.horizontalPadding, Theme.chatBubbleHorizontalPadding)
		compare(outgoing.verticalPadding, Theme.chatBubbleVerticalPadding)

		incoming.preferredIncomingWidth = 300
		compare(incoming.messageWidth, 300)
		outgoing.preferredOwnWidth = 190
		compare(outgoing.messageWidth, 190)
		incoming.preferredIncomingWidth = 520
		outgoing.preferredOwnWidth = 260
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

	function test_wide_lane_keeps_plain_messages_compact_and_rich_content_adapts() {
		testCase.width = 1080
		timeline.forceLayout()
		wait(20)
		const incoming = timeline.itemAtIndex(0)
		const outgoing = timeline.itemAtIndex(1)
		verify(incoming !== null)
		verify(outgoing !== null)
		compare(incoming.laneWidth, Theme.chatLaneMaximumWidth)
		compare(incoming.messageWidth, 520)
		compare(incoming.surfaceX, 120)
		compare(outgoing.messageWidth, 260)
		compare(outgoing.surfaceX, 700)

		incoming.wideContent = true
		compare(incoming.messageWidth, Theme.chatRichMaximumWidth)
		compare(incoming.surfaceX, 120)
		incoming.preferredWideContentWidth = 596
		compare(incoming.messageWidth, 596)
		compare(incoming.surfaceX, 120)
		incoming.wideContent = false

		outgoing.wideContent = true
		compare(outgoing.messageWidth, Theme.chatRichMaximumWidth)
		compare(outgoing.surfaceX, 200)
		outgoing.preferredWideContentWidth = 596
		compare(outgoing.messageWidth, 596)
		compare(outgoing.surfaceX, 364)
		verify(outgoing.surfaceX + outgoing.surfaceWidth <= 960)
		outgoing.wideContent = false
	}

	function test_hover_area_matches_the_message_surface_not_the_complete_row() {
		const outgoing = timeline.itemAtIndex(1)
		verify(outgoing !== null)
		verify(outgoing.surfaceX > 0)
		const hoverArea = findChild(outgoing, "chatMessageHoverArea")
		verify(hoverArea !== null)
		compare(hoverArea.parent.width, outgoing.surfaceWidth)
		compare(hoverArea.parent.height, outgoing.contentItem.height
			+ outgoing.verticalPadding * 2)
		verify(hoverArea.parent.width < outgoing.width)
		const outsidePoint = outgoing.mapToItem(hoverArea.parent, 1, outgoing.height / 2)
		verify(outsidePoint.x < 0)
	}
}
