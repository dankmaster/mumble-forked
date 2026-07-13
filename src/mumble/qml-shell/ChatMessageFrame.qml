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

	// The conversation lane is centered independently of the message surface.
	// Regular incoming messages occupy the lane without looking like bubbles,
	// while short outgoing messages can hug their content at the trailing edge.
	property real laneAvailableWidth: 0
	property real laneMaximumWidth: 840
	property real systemMaximumWidth: 680
	property real preferredOwnWidth: 260
	property real minimumOwnWidth: 176
	property bool wideContent: false
	readonly property real laneWidth: Math.max(1, Math.min(root.laneMaximumWidth,
		root.laneAvailableWidth > 0 ? root.laneAvailableWidth : root.laneMaximumWidth))
	readonly property real messageWidth: {
		if (root.systemMessage)
			return Math.min(root.laneWidth, root.systemMaximumWidth)
		if (root.own && !root.wideContent)
			return Math.min(root.laneWidth, Math.max(root.minimumOwnWidth, root.preferredOwnWidth))
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

	Rectangle {
		id: messageSurface
		objectName: "chatMessageSurface"
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
