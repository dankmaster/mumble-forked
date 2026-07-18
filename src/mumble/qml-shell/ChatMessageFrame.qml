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
	property string dateSeparatorLabel: ""
	property real bodyImplicitHeight: 0
	// Keep the accessibility hierarchy stable for the lifetime of a reused
	// delegate. Toggling Accessible.ignored on this technical frame causes Qt
	// Quick to first promote its children and then expose them below the frame as
	// well, leaving duplicate active nodes in the platform tree after reuse.
	property bool accessibilityPooled: false
	// ListView deliberately materializes a cache buffer beyond the clipped
	// viewport. Keep those rows laid out in the cache but absent from UIA until
	// they actually intersect the conversation viewport. The accessibility
	// barrier below performs that semantic-only suppression; hiding the visual
	// subtree here would collapse its implicit height and break prepend anchoring.
	property bool accessibilityViewportVisible: true
	property bool accessibilitySuppressed: false
	// Visual fixtures must remain stable while the automation layer traverses
	// accessibility bounds. That traversal can move the native pointer over a
	// message between captures, so allow fixture hosts to suppress hover-only
	// chrome without changing normal product interaction.
	property bool hoverEffectsEnabled: true
	readonly property bool accessibilityFrameIgnored: true
	readonly property bool accessibilitySubtreeActive: !accessibilityPooled && accessibilityViewportVisible
	readonly property bool hovered: hoverEffectsEnabled && messageHover.hovered

	// The conversation lane is centered independently of the message surface.
	// Regular incoming messages occupy the lane without looking like bubbles,
	// while short outgoing messages can hug their content at the trailing edge.
	property real laneAvailableWidth: 0
	property real laneMaximumWidth: Theme.chatLaneMaximumWidth
	property real systemMaximumWidth: 680
	property real preferredOwnWidth: 260
	// Plain incoming messages should stay visually connected to their author,
	// timestamp and actions. Rich content can still opt into the complete lane
	// through wideContent/preferredWideContentWidth.
	property real preferredIncomingWidth: 520
	property real preferredWideContentWidth: Theme.chatRichMaximumWidth
	property real minimumOwnWidth: 176
	property real minimumIncomingWidth: 220
	property bool wideContent: false
	// Conversation search highlights one stable message without changing the
	// delegate's layout or filtering unrelated rows out of the virtualized list.
	property bool searchCurrent: false
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
		const preferredWidth = root.wideContent
			? root.preferredWideContentWidth : root.preferredIncomingWidth
		return Math.min(root.laneWidth,
			Math.max(root.minimumIncomingWidth, preferredWidth))
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
	property real continuationGap: Theme.chatMetadataSpacing
	readonly property real topGap: root.startsGroup ? root.groupGap : root.continuationGap
	readonly property bool hasDateSeparator: root.dateSeparatorLabel.length > 0
	readonly property real dateSeparatorHeight: root.hasDateSeparator
		? Theme.space5 + Theme.fontCaption : 0
	property real horizontalPadding: root.systemMessage || root.own
		? Theme.chatBubbleHorizontalPadding : 0
	property real verticalPadding: root.systemMessage || root.own
		? Theme.chatBubbleVerticalPadding : 0
	property real compactMinimumHeight: root.own ? 32 : 28
	property real groupMinimumHeight: root.own ? 40 : Theme.avatarMedium
	readonly property real surfaceHeight: Math.max(
		root.startsGroup ? root.groupMinimumHeight : root.compactMinimumHeight,
		Math.ceil(root.bodyImplicitHeight) + root.verticalPadding * 2)
	readonly property color bubbleColor: root.systemMessage ? Theme.surfaceRaised
		: root.own ? Theme.chatOwnSurface : Theme.chatIncomingSurface
	readonly property color bubbleBorderColor: root.systemMessage ? Theme.surfaceBorder
		: root.own ? Theme.chatOwnBorder : Theme.chatIncomingBorder
	readonly property int bubbleBorderWidth: 1
	// The incoming avatar sits outside its bubble in Main.qml, so this technical
	// frame stays transparent for incoming rows while exposing the semantic
	// bubble roles to its content. Own/system rows can paint the complete frame.
	readonly property color surfaceColor: root.own || root.systemMessage ? root.bubbleColor : "transparent"
	readonly property color surfaceBorderColor: root.own || root.systemMessage
		? root.bubbleBorderColor : "transparent"
	readonly property int surfaceBorderWidth: root.own || root.systemMessage ? root.bubbleBorderWidth : 0
	readonly property alias dateSeparatorItem: dateSeparator
	readonly property alias surfaceX: messageSurface.x
	readonly property alias surfaceWidth: messageSurface.width
	default property alias contentData: contentHost.data
	readonly property alias contentItem: contentHost

	Timer {
		id: accessibilityReassertTimer
		interval: 0
		repeat: false
		onTriggered: {
			if (root.visible)
				root.reassertAccessibilitySuppression()
		}
	}

	function reassertAccessibilitySuppression() {
		if (!messageAccessibilityBarrier.active)
			return false
		return messageAccessibilityBarrier.reassertItem(root)
	}

	implicitWidth: laneAvailableWidth > 0 ? laneAvailableWidth : messageWidth
	implicitHeight: dateSeparatorHeight + topGap + surfaceHeight
	height: implicitHeight
	Accessible.ignored: accessibilityFrameIgnored
	// Qt Quick can make a pooled delegate visible again without emitting
	// ListView.onReused on every model replacement path. Visibility is the final
	// authority: an active delegate must always restore its semantic subtree.
	onVisibleChanged: {
		if (visible) {
			accessibilityPooled = false
			if (messageAccessibilityBarrier.active)
				accessibilityReassertTimer.restart()
		}
	}
	onAccessibilitySuppressedChanged: {
		if (messageAccessibilityBarrier.active)
			accessibilityReassertTimer.restart()
	}
	onAccessibilityViewportVisibleChanged: {
		if (messageAccessibilityBarrier.active)
			accessibilityReassertTimer.restart()
	}
	ListView.onAdd: accessibilityPooled = false

	Item {
		id: dateSeparator
		objectName: "chatDateSeparator"
		visible: root.hasDateSeparator
		x: Math.max(0, (root.laneAvailableWidth - root.laneWidth) / 2)
		y: 0
		width: root.laneWidth
		height: root.dateSeparatorHeight
		Accessible.ignored: true

		Row {
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.verticalCenter: parent.verticalCenter
			spacing: Theme.space3

			Rectangle {
				anchors.verticalCenter: parent.verticalCenter
				width: Math.max(1, (parent.width - separatorText.implicitWidth
					- parent.spacing * 2) / 2)
				height: 1
				color: Theme.divider
			}

			Text {
				id: separatorText
				objectName: "chatDateSeparatorText"
				text: root.dateSeparatorLabel
				textFormat: Text.PlainText
				color: Theme.textFaint
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				font.letterSpacing: 0.7
				Accessible.role: Accessible.StaticText
				Accessible.name: text
				// The ignored date-separator host promotes this leaf. Suppress the
				// semantic owner directly when another modal owns the application.
				Accessible.ignored: root.accessibilitySuppressed
			}

			Rectangle {
				anchors.verticalCenter: parent.verticalCenter
				width: Math.max(1, (parent.width - separatorText.implicitWidth
					- parent.spacing * 2) / 2)
				height: 1
				color: Theme.divider
			}
		}
	}

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
		visible: !root.accessibilityPooled
		x: root.messageX
		y: root.dateSeparatorHeight + root.topGap
		width: root.messageWidth
		height: root.surfaceHeight
		radius: root.systemMessage || root.own ? Theme.innerRadius : 0
		color: root.surfaceColor
		border.color: root.surfaceBorderColor
		border.width: root.surfaceBorderWidth

		HoverHandler {
			id: messageHover
			objectName: "chatMessageHoverArea"
		}

		Item {
			id: contentHost
			objectName: "chatMessageContentHost"
			anchors.fill: parent
			anchors.leftMargin: root.horizontalPadding
			anchors.rightMargin: root.horizontalPadding
			anchors.topMargin: root.verticalPadding
			anchors.bottomMargin: root.verticalPadding
			// messageSurface is intentionally ignored. Keep this layout-only host
			// ignored from creation as well so Windows UIA never materializes the
			// empty Client provider that otherwise survives later modal suppression.
			// Semantic descendants are promoted and remain governed by the barrier.
			Accessible.ignored: true
		}
	}

	Rectangle {
		id: searchHighlight
		objectName: "conversationSearchHighlight"
		x: messageSurface.x - 3
		y: messageSurface.y - 3
		width: messageSurface.width + 6
		height: messageSurface.height + 6
		visible: root.searchCurrent && !root.accessibilityPooled
		radius: Math.max(Theme.innerRadius, messageSurface.radius + 3)
		color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07)
		border.color: Theme.accent
		border.width: Theme.focusRingWidth
		z: 20
		Accessible.ignored: true
	}

	ModalAccessibilityBarrier {
		id: messageAccessibilityBarrier
		objectName: "chatMessageAccessibilityBarrier"
		active: root.accessibilitySuppressed || !root.accessibilitySubtreeActive
		// QQuickItemView may re-enable the delegate frame's private accessibility
		// bit while materializing or reusing a row, even though its public
		// Accessible.ignored binding remains true. Own the complete delegate while
		// a product modal is active or the cached row is outside the viewport so
		// neither that empty Client wrapper nor any promoted semantic descendant can
		// escape into UIA. This leaves visual geometry untouched.
		targets: [ root ]
	}
}
