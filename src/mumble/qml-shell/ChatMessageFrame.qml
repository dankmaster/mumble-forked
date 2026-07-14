import QtQuick
import Mumble.Theme 1.0

// The ListView owns this item's visibility while it is removed, pooled, and
// reused. Do not bind `visible` here: doing so keeps stale delegates in the
// scene after a scope switch when reuseItems is enabled.
Item {
	id: root

	required property string stableId
	required property bool startsGroup
	required property bool own
	required property bool systemMessage
	property real bodyImplicitHeight: 0
	// Keep the accessibility hierarchy stable for the lifetime of a reused
	// delegate. Toggling Accessible.ignored on this technical frame causes Qt
	// Quick to first promote its children and then expose them below the frame as
	// well, leaving duplicate active nodes in the platform tree after reuse.
	property bool accessibilityPooled: false
	readonly property bool accessibilityFrameIgnored: true
	readonly property bool accessibilitySubtreeActive: !accessibilityPooled

	// The conversation lane is centered independently of the message surface.
	// Regular incoming messages occupy the lane without looking like bubbles,
	// while short outgoing messages can hug their content at the trailing edge.
	property real laneAvailableWidth: 0
	property real laneMaximumWidth: 840
	property real systemMaximumWidth: 680
	property real preferredOwnWidth: 260
	property real preferredWideContentWidth: laneMaximumWidth
	property real minimumOwnWidth: 176
	property bool wideContent: false
	readonly property real laneWidth: Math.max(1, Math.min(root.laneMaximumWidth,
		root.laneAvailableWidth > 0 ? root.laneAvailableWidth : root.laneMaximumWidth))
	readonly property real messageWidth: {
		if (root.systemMessage)
			return Math.min(root.laneWidth, root.systemMaximumWidth)
		if (root.own) {
			const preferredWidth = root.wideContent
				? root.preferredWideContentWidth : root.preferredOwnWidth
			return Math.min(root.laneWidth, Math.max(root.minimumOwnWidth, preferredWidth))
		}
		return root.laneWidth
	}
	readonly property real messageX: {
		const laneStart = Math.max(0, (root.laneAvailableWidth - root.laneWidth) / 2)
		if (root.own)
			return laneStart + root.laneWidth - root.messageWidth
		if (root.systemMessage)
			return laneStart + (root.laneWidth - root.messageWidth) / 2
		return laneStart
	}

	property real groupGap: Theme.space3
	property real continuationGap: Theme.space1
	readonly property real topGap: root.startsGroup ? root.groupGap : root.continuationGap
	property real horizontalPadding: root.systemMessage ? Theme.space3
		: root.own ? Theme.space2 : 0
	property real verticalPadding: root.systemMessage ? Theme.space2
		: root.own ? Theme.space2 : 0
	property real compactMinimumHeight: root.own ? 32 : 28
	property real groupMinimumHeight: root.own ? 40 : Theme.avatarMedium
	readonly property real surfaceHeight: Math.max(
		root.startsGroup ? root.groupMinimumHeight : root.compactMinimumHeight,
		Math.ceil(root.bodyImplicitHeight) + root.verticalPadding * 2)
	readonly property color surfaceColor: root.systemMessage ? Theme.surfaceRaised
		: root.own ? Theme.accentSubtle : "transparent"
	readonly property color surfaceBorderColor: root.systemMessage ? Theme.surfaceBorder : "transparent"
	readonly property int surfaceBorderWidth: root.systemMessage ? 1 : 0
	readonly property alias surfaceX: messageSurface.x
	readonly property alias surfaceWidth: messageSurface.width
	default property alias contentData: contentHost.data
	readonly property alias contentItem: contentHost

	implicitWidth: laneAvailableWidth > 0 ? laneAvailableWidth : messageWidth
	implicitHeight: topGap + surfaceHeight
	height: implicitHeight
	Accessible.ignored: accessibilityFrameIgnored
	// Qt Quick can make a pooled delegate visible again without emitting
	// ListView.onReused on every model replacement path. Visibility is the final
	// authority: an active delegate must always restore its semantic subtree.
	onVisibleChanged: {
		if (visible)
			accessibilityPooled = false
	}
	ListView.onAdd: accessibilityPooled = false

	Rectangle {
		id: messageSurface
		objectName: "chatMessageSurface"
		// This rectangle is visual layout only. Leaving it in the accessible tree
		// makes Qt expose both a Client subtree and the same promoted children as
		// siblings, so screen readers announce every message action twice.
		Accessible.ignored: true
		// ListView retains released delegates in its reuse pool. Explicitly hide the
		// complete semantic subtree while pooled; relying on the delegate's effective
		// visibility leaves stale QAccessible children active after a model reset.
		visible: root.accessibilitySubtreeActive
		x: root.messageX
		y: root.topGap
		width: root.messageWidth
		height: root.surfaceHeight
		radius: root.systemMessage || root.own ? Theme.innerRadius : 0
		color: root.surfaceColor
		border.color: root.surfaceBorderColor
		border.width: root.surfaceBorderWidth

		Item {
			id: contentHost
			anchors.fill: parent
			anchors.leftMargin: root.horizontalPadding
			anchors.rightMargin: root.horizontalPadding
			anchors.topMargin: root.verticalPadding
			anchors.bottomMargin: root.verticalPadding
		}
	}
}
