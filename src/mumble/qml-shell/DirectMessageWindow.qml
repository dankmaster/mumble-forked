import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mumble.Theme 1.0

Window {
	id: root
	objectName: surfaceId

	required property var controller
	property var parentWindow: null
	property bool visualFixtureMode: false
	property var mediaSessionController: typeof mediaSession !== "undefined" ? mediaSession : null
	property var mediaProfileFactory: typeof mediaProfiles !== "undefined" ? mediaProfiles : null
	property var savedPreviewSizePresets: ({})
	readonly property string surfaceId: "directMessage.window"
	// A separate QQuickWindow is captured in its own window-local coordinate space.
	readonly property var captureRect: Qt.rect(0, 0, width, height)
	readonly property bool docked: controller && controller.windowDocked
	readonly property string activeConversationId: controller ? String(controller.activeSessionId || "") : ""
	readonly property var timelineModel: controller ? controller.timelineModel : null
	readonly property bool conversationSearchSupported: timelineModel
		&& typeof timelineModel.clearSearch === "function"
		&& typeof timelineModel.nextMatch === "function"
		&& typeof timelineModel.previousMatch === "function"
	readonly property int timelineCount: controller && controller.timelineModel
		? Number(controller.timelineModel.count || 0) : 0
	readonly property string historyErrorText: controller
		? String(controller.historyError || "").trim() : ""
	readonly property string windowPlacementLabel: docked
		? qsTr("Docked to the main window") : qsTr("Detached window")
	readonly property int resizeHandleThickness: 6
	readonly property int resizeCornerSize: 14
	readonly property bool windowResizeAvailable: visible
		&& controller && !controller.windowMinimized
		&& visibility === Window.Windowed
	property bool componentReady: false
	property bool composerFocusPending: false
	property bool conversationSearchOpen: false
	property bool followTimelineTail: true
	property bool timelinePositioning: false
	property string quickReactionMessageId: ""
	property bool hasPersistedPlacement: false
	property var geometryStore: typeof windowStateStore !== "undefined" ? windowStateStore : null
	signal managedImageOpenRequested(string source, string title, string messageId)
	signal managedAttachmentOpenRequested(var attachment, string messageId)
	signal watchTogetherRequested(string url, string provider, string title)
	signal previewSizePresetRequested(string preferenceKey, string preset)
	Component {
		id: directMessageCursorDelegate
		Item {
			width: 1
			Rectangle {
				objectName: "directMessageCursorPaint"
				anchors.fill: parent
				visible: !root.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}

	title: controller && String(controller.activeLabel || "").length > 0
		? qsTr("Direct message · %1").arg(controller.activeLabel) : qsTr("Direct message")
	width: 360
	height: controller && controller.windowMinimized ? 74 : 438
	minimumWidth: 340
	minimumHeight: controller && controller.windowMinimized ? 74 : 320
	maximumHeight: controller && controller.windowMinimized ? 74 : 10000
	visible: controller && controller.conversationOpen
	color: "transparent"
	flags: Qt.Tool | Qt.FramelessWindowHint
	modality: Qt.NonModal
	transientParent: parentWindow

	function safeAvatarSource(value) {
		const source = String(value || "").trim()
		return /^(image:\/\/mumble\/|data:image\/)/i.test(source) ? source : ""
	}

	function positionNearParent() {
		if (!parentWindow)
			return
		const margin = Theme.space4
		x = Math.max(parentWindow.x + margin, parentWindow.x + parentWindow.width - width - 335)
		y = Math.max(parentWindow.y + margin, parentWindow.y + parentWindow.height - height - 82)
	}

	function sendCurrentDraft() {
		const attachmentCount = normalizedSequence(controller ? controller.draftAttachments : []).length
		if (controller && controller.canSend
			&& (String(controller.draft || "").trim().length > 0 || attachmentCount > 0))
			controller.sendDraft()
	}

	function beginSystemResize(edges) {
		if (!windowResizeAvailable)
			return false
		return startSystemResize(edges)
	}

	component ResizeHandle: MouseArea {
		required property int resizeEdges
		hoverEnabled: true
		enabled: root.windowResizeAvailable
		acceptedButtons: Qt.LeftButton
		propagateComposedEvents: false
		Accessible.ignored: true
		onPressed: mouse => {
			if (!root.beginSystemResize(resizeEdges))
				mouse.accepted = false
		}
	}

	function positionTimelineAtEnd() {
		if (!timeline || timeline.count <= 0)
			return
		timelinePositioning = true
		timeline.positionViewAtEnd()
		Qt.callLater(function() { root.timelinePositioning = false })
	}

	function showQuickReactions(messageId, row) {
		const stableId = String(messageId || "")
		if (stableId.length === 0)
			return
		quickReactionMessageId = stableId
		let targetRow = Number(row)
		if (targetRow < 0 && timelineModel
				&& typeof timelineModel.rowForStableId === "function")
			targetRow = timelineModel.rowForStableId(stableId)
		if (targetRow < 0)
			return
		Qt.callLater(function() {
			timeline.positionViewAtIndex(targetRow, ListView.End)
			timeline.forceLayout()
		})
	}

	function toggleQuickReactions(messageId, row) {
		const stableId = String(messageId || "")
		if (quickReactionMessageId === stableId) {
			quickReactionMessageId = ""
			return
		}
		showQuickReactions(stableId, row)
	}

	function openConversationSearch() {
		if (!conversationSearchSupported || !controller || controller.windowMinimized)
			return
		composerFocusPending = false
		conversationSearchOpen = true
		requestActivate()
		Qt.callLater(function() { conversationSearchBar.activate() })
	}

	function closeConversationSearch(restoreFocus) {
		conversationSearchBar.reset()
		conversationSearchOpen = false
		if (restoreFocus && conversationSearchButton.visible) {
			Qt.callLater(function() {
				conversationSearchButton.forceActiveFocus(Qt.OtherFocusReason)
			})
		}
	}

	function revealConversationSearchMatch(row, stableId) {
		if (!conversationSearchOpen || row < 0 || String(stableId || "").length === 0)
			return
		followTimelineTail = false
		timeline.currentIndex = row
		timeline.positionViewAtIndex(row, ListView.Center)
	}

	function beginReply(messageId) {
		if (!controller)
			return
		controller.replyToMessage(String(messageId || ""))
		Qt.callLater(function() { root.activateComposer() })
	}

	function finishComposerAction() {
		Qt.callLater(function() { root.activateComposer() })
	}

	function draftAttachmentBusy(attachment) {
		const status = String(attachment && attachment.status || "").toLowerCase()
		return status === "queued" || status === "preparing" || status === "uploading"
	}

	function draftAttachmentStatusText(attachment) {
		if (!attachment)
			return ""
		const status = String(attachment.status || "").toLowerCase()
		const error = String(attachment.error || "").trim()
		if (status === "failed")
			return error.length > 0 ? error : qsTr("Attachment failed")
		if (status === "uploading") {
			const percentage = Math.max(0, Math.min(100,
				Math.round(Number(attachment.progress || 0) * 100)))
			return qsTr("Uploading · %1%").arg(percentage)
		}
		if (status === "queued" || status === "preparing")
			return qsTr("Preparing attachment…")
		return qsTr("Ready to send")
	}

	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}

	function previewPreferenceKey(messageId) {
		const conversation = controller ? String(controller.activeSessionId || "") : ""
		const stableId = String(messageId || "")
		return conversation.length > 0 && stableId.length > 0
			? "dm:" + conversation + ":" + stableId : stableId
	}

	function savedPreviewSizePreset(messageId) {
		const key = previewPreferenceKey(messageId)
		return key.length > 0 && savedPreviewSizePresets
			? String(savedPreviewSizePresets[key] || "") : ""
	}

	function requestAttachment(attachment, messageId) {
		if (!attachment)
			return
		const source = safeRenderImageSource(attachment.url)
			|| safeRenderImageSource(attachment.thumbnailUrl)
		if (source.length > 0) {
			managedAttachmentOpenRequested(attachment, String(messageId || ""))
			return
		}
		if (controller)
			controller.openAttachment(attachmentAssetId(attachment), attachmentFileName(attachment))
	}

	function focusComposerWhenActive() {
		if (!composerFocusPending || !visible || !active || !controller
				|| controller.windowMinimized || !composer.enabled)
			return false
		composer.forceActiveFocus(Qt.ActiveWindowFocusReason)
		composerFocusPending = false
		return true
	}

	function activateComposer() {
		if (!componentReady || !visible || !controller || controller.windowMinimized)
			return
		composerFocusPending = true
		requestActivate()
		Qt.callLater(function() { root.focusComposerWhenActive() })
	}

	function normalizedSequence(value) {
		if (Array.isArray(value))
			return value
		if (value && value.count !== undefined && typeof value.get === "function") {
			const result = []
			for (let index = 0; index < value.count; ++index)
				result.push(value.get(index))
			return result
		}
		if (value && typeof value !== "string") {
			try {
				return Array.from(value)
			} catch (error) {
				return []
			}
		}
		return []
	}

	function messageStartsGroup(index, title, own) {
		if (!controller || !controller.timelineModel || index <= 0
			|| typeof controller.timelineModel.get !== "function")
			return true
		const previous = controller.timelineModel.get(index - 1)
		return !previous || !!previous.own !== !!own || String(previous.title || "") !== String(title || "")
	}

	function attachmentAssetId(attachment) {
		return attachment ? String(attachment.assetId || attachment.assetID || "") : ""
	}

	function attachmentFileName(attachment) {
		return attachment ? String(attachment.fileName || attachment.name || "") : ""
	}

	function normalizedSegments(value) {
		if (Array.isArray(value))
			return value
		if (value && value.count !== undefined && typeof value.get === "function") {
			const result = []
			for (let index = 0; index < value.count; ++index)
				result.push(value.get(index))
			return result
		}
		if (value && typeof value !== "string") {
			try {
				const result = Array.from(value)
				if (result.length > 0 || Number(value.length) === 0)
					return result
			} catch (error) {
				// Some QML model wrappers intentionally reject generic sequence
				// conversion. The empty fallback below remains safe for those.
			}
		}
		return []
	}

	function estimatedMessageTextWidth(segments, replyActor, replySnippet) {
		let longestLine = 0
		for (const segment of normalizedSegments(segments)) {
			const text = String(segment && segment.text !== undefined ? segment.text : "")
			for (const line of text.split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		for (const replyText of [replyActor, replySnippet]) {
			for (const line of String(replyText || "").split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		return longestLine * Theme.fontBody * 0.58
	}

	function preferredMessageWidth(segments, replyActor, replySnippet, startsGroup, own,
			deliveryVisible) {
		const chromeWidth = own ? Theme.chatBubbleHorizontalPadding * 2
			: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
		const textEstimate = estimatedMessageTextWidth(segments, replyActor, replySnippet)
			+ chromeWidth
		const minimum = own ? (deliveryVisible ? 300 : 176)
			: (deliveryVisible ? 320 : startsGroup ? 260 : 220)
		return Math.max(minimum, Math.min(Theme.chatPlainMaximumWidth, textEstimate))
	}

	function messageContainsInlineImage(segments) {
		for (const segment of normalizedSegments(segments)) {
			if (segment && String(segment.kind || "").toLowerCase() === "image")
				return true
		}
		return false
	}

	function preferredAttachmentMessageWidth(attachments, own) {
		const values = normalizedSequence(attachments)
		if (values.length <= 0)
			return 0
		if (values.length > 1)
			return Theme.chatRichMaximumWidth
		const attachment = values[0] || ({})
		const kind = String(attachment.kind || "").toLowerCase()
		const mime = String(attachment.mime || "").toLowerCase()
		const image = kind === "image" || mime.indexOf("image/") === 0
		const requestedWidth = Math.max(0, Number(attachment.width || 0))
		const tileWidth = image ? Math.min(320, Math.max(180, requestedWidth || 240)) : 360
		const chromeWidth = own ? Theme.chatBubbleHorizontalPadding * 2
			: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
		return Math.min(Theme.chatRichMaximumWidth, tileWidth + chromeWidth)
	}

	onVisibleChanged: {
		if (visible) {
			if (!hasPersistedPlacement)
				positionNearParent()
			if (componentReady)
				activateComposer()
			followTimelineTail = true
			positionTimelineAtEnd()
			controller.markRead()
		} else if (componentReady && conversationSearchOpen) {
			closeConversationSearch(false)
		}
	}
	onActiveConversationIdChanged: {
		quickReactionMessageId = ""
		if (componentReady && conversationSearchOpen)
			closeConversationSearch(false)
		followTimelineTail = true
		positionTimelineAtEnd()
	}
	onTimelineCountChanged: {
		if (followTimelineTail)
			positionTimelineAtEnd()
	}
	onDockedChanged: if (docked && visible) positionNearParent()
	Component.onCompleted: {
		hasPersistedPlacement = geometryStore
			&& geometryStore.restoreWindow(root, "direct-message", minimumWidth, 320)
		componentReady = true
		if (visible) {
			activateComposer()
			positionTimelineAtEnd()
		}
	}
	onActiveChanged: {
		if (active && controller) {
			controller.markRead()
			focusComposerWhenActive()
		}
	}
	onVisibilityChanged: {
		if (root.visibility === Window.Windowed && controller && controller.windowMinimized)
			controller.setWindowMinimized(false)
	}
	onClosing: close => {
		close.accepted = false
		if (controller)
			controller.closeConversation()
	}

	Connections {
		target: controller
		function onWindowDockedChanged() {
			if (controller.windowDocked)
				root.positionNearParent()
		}
		function onWindowMinimizedChanged() {
			if (!controller.windowMinimized && root.visible) {
				root.showNormal()
				root.activateComposer()
			}
		}
		function onDraftChanged() {
			if (composer.text !== String(controller.draft || ""))
				composer.text = String(controller.draft || "")
		}
	}

	Connections {
		target: root.parentWindow
		enabled: root.docked
		ignoreUnknownSignals: true
		function onXChanged() { root.positionNearParent() }
		function onYChanged() { root.positionNearParent() }
		function onWidthChanged() { root.positionNearParent() }
		function onHeightChanged() { root.positionNearParent() }
	}

	Rectangle {
		anchors.fill: parent
		radius: Theme.innerRadius
		color: Theme.popupBackground
		border.color: windowFocusScope.activeFocus ? Theme.focus
			: controller && controller.mode === "private" ? Theme.withAlpha(Theme.accent, 0.46)
			: root.docked ? Theme.withAlpha(Theme.accent, 0.30) : Theme.popupBorder
		border.width: windowFocusScope.activeFocus ? Theme.focusRingWidth : 1

		Rectangle {
			anchors.fill: parent
			anchors.margins: 1
			radius: Math.max(0, parent.radius - 1)
			color: "transparent"
			border.color: Theme.elevationHighlight
			border.width: 1
		}
	}

	FocusScope {
		id: windowFocusScope
		anchors.fill: parent
		activeFocusOnTab: true
		focus: true
		Accessible.role: Accessible.Pane
		Accessible.name: root.title
		Accessible.description: (controller && controller.mode === "private"
			? qsTr("Private in-memory direct-message conversation")
			: qsTr("Direct-message conversation with persistent history"))
			+ ". " + root.windowPlacementLabel
	Keys.onEscapePressed: event => {
			if (root.conversationSearchOpen) {
				root.closeConversationSearch(true)
			} else if (controller && controller.hasPendingReply) {
				controller.cancelPendingReply()
				root.finishComposerAction()
			} else if (controller) {
				controller.closeConversation()
			}
			event.accepted = true
		}

		ColumnLayout {
			anchors.fill: parent
			spacing: 0

			Rectangle {
				Layout.fillWidth: true
				Layout.preferredHeight: 64
				color: Theme.panel
				radius: Theme.innerRadius

				Rectangle {
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.bottom: parent.bottom
					height: Theme.innerRadius
					color: parent.color
				}

				DragHandler {
					target: null
					onActiveChanged: if (active) {
						if (controller && controller.windowDocked)
							controller.setWindowDocked(false)
						root.startSystemMove()
					}
				}

				RowLayout {
					anchors.fill: parent
					anchors.leftMargin: Theme.space3
					anchors.rightMargin: Theme.space2
					spacing: Theme.space2

					Rectangle {
						Layout.preferredWidth: Theme.avatarMedium
						Layout.preferredHeight: Theme.avatarMedium
						radius: width / 2
						color: Theme.accent
						clip: true
						Accessible.ignored: true

						Image {
							id: peerAvatar
							anchors.fill: parent
							source: root.safeAvatarSource(controller ? controller.activeAvatarUrl : "")
							asynchronous: true
							cache: false
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
						}

						Label {
							anchors.centerIn: parent
							visible: peerAvatar.status !== Image.Ready
							textFormat: Text.PlainText
							text: controller ? String(controller.activeLabel || "?").slice(0, 2).toUpperCase() : "?"
							color: Theme.contrastText(Theme.accent)
							font.pixelSize: Theme.fontCaption
							font.weight: Font.Bold
							Accessible.ignored: true
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 1

						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: controller ? String(controller.activeLabel || qsTr("Direct message")) : qsTr("Direct message")
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.weight: Font.DemiBold
							elide: Text.ElideRight
						}

						Label {
							objectName: "directMessagePeerSubtitle"
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: controller && String(controller.activeSubtitle || "").trim().length > 0
								? String(controller.activeSubtitle).trim()
								: controller && controller.mode === "history"
									? qsTr("History · stored on server") : qsTr("Private · in-memory only")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}

					ModernIconButton {
						id: conversationSearchButton
						objectName: "directMessageSearchButton"
						visible: root.conversationSearchSupported
						dense: true
						iconName: "search"
						selected: root.conversationSearchOpen
						Accessible.name: root.conversationSearchOpen
							? qsTr("Focus direct-message search") : qsTr("Search this direct message")
						Accessible.description: qsTr("Search message text, senders, replies, and attachments")
						onClicked: root.openConversationSearch()
					}

					ModernIconButton {
						objectName: "directMessageModeToggle"
						dense: true
						iconName: controller && controller.mode === "history" ? "history" : "shield"
						selected: controller && controller.mode === "private"
						enabled: controller && (controller.mode === "history" || controller.persistentHistoryAvailable)
						Accessible.name: controller && controller.mode === "history"
							? qsTr("Switch to private mode") : qsTr("Use persistent history")
						onClicked: controller.setMode(controller.mode === "history" ? "private" : "history")
					}

					ModernIconButton {
						objectName: "directMessageDockToggle"
						dense: true
						iconName: "pin"
						selected: controller && controller.windowDocked
						Accessible.name: controller && controller.windowDocked
							? qsTr("Undock direct-message window") : qsTr("Dock direct-message window")
						Accessible.description: root.windowPlacementLabel
						onClicked: controller.setWindowDocked(!controller.windowDocked)
					}

					ModernIconButton {
						objectName: "directMessageWindowMinimize"
						dense: true
						iconName: "minimize"
						Accessible.name: qsTr("Minimize direct-message window")
						onClicked: {
							controller.setWindowMinimized(true)
							root.showMinimized()
						}
					}

					ModernIconButton {
						objectName: "directMessageWindowClose"
						dense: true
						iconName: "close"
						Accessible.name: qsTr("Close direct-message window")
						onClicked: controller.closeConversation()
					}
				}
			}

			ConversationSearchBar {
				id: conversationSearchBar
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? implicitHeight : 0
				visible: root.conversationSearchOpen && controller && !controller.windowMinimized
				timelineModel: root.timelineModel
				narrowLayout: true
				visualFixtureMode: root.visualFixtureMode
				onCloseRequested: root.closeConversationSearch(true)
				onCurrentMatchRequested: (row, stableId) =>
					root.revealConversationSearchMatch(row, stableId)
			}

			Rectangle {
				objectName: "directMessageModeBanner"
				Layout.fillWidth: true
				Layout.preferredHeight: controller && controller.windowMinimized ? 0
					: modeBannerRow.implicitHeight + Theme.space3 * 2
				visible: controller && !controller.windowMinimized
				color: controller && controller.mode === "private"
					? Theme.withAlpha(Theme.warning, 0.10) : Theme.withAlpha(Theme.success, 0.08)
				border.color: controller && controller.mode === "private"
					? Theme.withAlpha(Theme.warning, 0.42) : Theme.withAlpha(Theme.success, 0.32)
				border.width: 1
				Accessible.role: Accessible.StatusBar
				Accessible.name: modeBanner.text

				RowLayout {
					id: modeBannerRow
					anchors.fill: parent
					anchors.margins: Theme.space3
					spacing: Theme.space2

					ModernIcon {
						name: controller && controller.mode === "private" ? "shield" : "history"
						size: 16
						color: controller && controller.mode === "private" ? Theme.warning : Theme.success
						Accessible.ignored: true
					}

					Label {
						id: modeBanner
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: controller && controller.mode === "private"
							? qsTr("Private mode · Messages clear when this window closes.")
							: qsTr("History mode · Messages are stored by the server.")
						color: controller && controller.mode === "private" ? Theme.warning : Theme.success
						font.pixelSize: Theme.fontCaption
						font.weight: Font.Medium
						wrapMode: Text.Wrap
						Accessible.ignored: true
					}
				}
			}

			Item {
				Layout.fillWidth: true
				Layout.fillHeight: true
				visible: controller && !controller.windowMinimized

				ListView {
					id: timeline
					objectName: "directMessageTimeline"
					anchors.fill: parent
					anchors.margins: Theme.space3
					anchors.topMargin: Theme.space3
						+ (historyStatusOverlay.visible ? historyStatusOverlay.height + Theme.space2 : 0)
					visible: root.timelineCount > 0
					clip: true
					// ChatMessageFrame owns group and continuation rhythm. Keeping the
					// ListView spacing at zero prevents a second, DM-only vertical gap.
					spacing: 0
					model: controller ? controller.timelineModel : null
					reuseItems: true
					// Keep DM prefetch bounded just like the main timeline. Two full
					// viewports retained expensive rich-message and accessibility trees.
					cacheBuffer: Math.max(256, Math.min(480, height * 0.5))
					boundsBehavior: Flickable.StopAtBounds
					activeFocusOnTab: true
					Accessible.role: Accessible.List
					Accessible.name: qsTr("Messages with %1").arg(controller ? controller.activeLabel : "")
					Accessible.description: qsTr("%1 messages. Use Home or End to move through the conversation.")
						.arg(count)
					ScrollBar.vertical: ModernScrollBar { policy: ScrollBar.AsNeeded }
					MiddleDragScrollHandler {
						targetFlickable: timeline
						horizontalEnabled: false
						onScrollingStarted: root.followTimelineTail = false
						onScrollingEnded: root.followTimelineTail = timeline.atYEnd
					}
					onMovementStarted: root.followTimelineTail = false
					onMovementEnded: root.followTimelineTail = atYEnd
					onContentYChanged: {
						if (!root.timelinePositioning && !moving)
							root.followTimelineTail = atYEnd
					}
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Home) {
							positionViewAtBeginning()
							currentIndex = count > 0 ? 0 : -1
							root.followTimelineTail = false
							event.accepted = true
						} else if (event.key === Qt.Key_End) {
							root.followTimelineTail = true
							root.positionTimelineAtEnd()
							event.accepted = true
						}
					}

					delegate: ChatMessageFrame {
						id: messageDelegate
						objectName: "directMessageMessage_" + stableId
						required property int index
						required property string title
						required property string subtitle
						required property string status
						required property string timestamp
						required property string replyActor
						required property string replySnippet
						required property var reactions
						required property var bodySegments
						required property var preview
						required property var attachments
						required property var source
						required property bool deleted
						required property bool canReply
						required property bool canReact
						required property bool canDelete
						required property string avatarUrl
						startsGroup: root.messageStartsGroup(index, title, own)
						systemMessage: false
						bodyImplicitHeight: messageRow.implicitHeight
						laneAvailableWidth: ListView.view.width
						laneMaximumWidth: Theme.chatLaneMaximumWidth
						readonly property bool hasAttachmentContent:
							root.normalizedSequence(attachments).length > 0
						readonly property bool hasPreviewContent: !!preview && Object.keys(preview).length > 0
						readonly property bool hasInlineImageContent: root.messageContainsInlineImage(bodySegments)
						readonly property real previewTargetWidth: hasPreviewContent && messagePreviewLoader.item
							? messagePreviewLoader.item.targetCardWidth
							: preview && preview.previewSize === "compact" ? 460
							: preview && preview.previewSize === "large" ? 720 : 580
						readonly property real richContentChromeWidth: own
							? horizontalPadding * 2
							: Theme.avatarMedium + Theme.space2 + Theme.chatBubbleHorizontalPadding * 2
						preferredOwnWidth: root.preferredMessageWidth(bodySegments, replyActor,
							replySnippet, startsGroup, true, deliveryVisible)
						preferredIncomingWidth: root.preferredMessageWidth(bodySegments, replyActor,
							replySnippet, startsGroup, false, deliveryVisible)
						searchCurrent: root.conversationSearchOpen
							&& stableId === String(root.timelineModel.currentMatchStableId || "")
						wideContent: hasAttachmentContent || hasPreviewContent || hasInlineImageContent
						readonly property real attachmentTargetWidth: hasAttachmentContent
							? root.preferredAttachmentMessageWidth(attachments, own) : 0
						readonly property real previewMessageWidth: hasPreviewContent
							? previewTargetWidth + richContentChromeWidth : 0
						preferredWideContentWidth: hasInlineImageContent
							? Theme.chatRichMaximumWidth
							: Math.min(Theme.chatRichMaximumWidth, Math.max(attachmentTargetWidth,
								previewMessageWidth, own ? preferredOwnWidth : preferredIncomingWidth))
						readonly property string deliveryState: String(source.deliveryState || status || "")
							.trim().toLowerCase()
						readonly property string deliveryLabel: String(source.deliveryLabel || status || "").trim()
						readonly property bool deliveryVisible: deliveryState === "sending"
							|| deliveryState === "failed" || deliveryState === "cancelled"
						readonly property bool hasMessageActions: canReply || canReact || canDelete
							|| !!source.deliveryCanRetry
						readonly property bool contentNeedsHydration: (!!preview
							&& (preview.state === "loading" || preview.loading === true))
							|| root.normalizedSequence(attachments).some(function(attachment) {
								return String(attachment.state || "").toLowerCase() === "loading"
							})
						readonly property bool inHydrationWindow: !accessibilityPooled
							&& y + height >= timeline.contentY - timeline.height * 0.5
							&& y <= timeline.contentY + timeline.height * 1.5
						readonly property bool inVisibleViewport: !accessibilityPooled
							&& y + height >= timeline.contentY
							&& y <= timeline.contentY + timeline.height
						accessibilityViewportVisible: height > timeline.height
							? y + height > timeline.contentY + 0.5
								&& y < timeline.contentY + timeline.height - 0.5
							: y >= timeline.contentY - 0.5
								&& y + height <= timeline.contentY + timeline.height + 0.5
						Accessible.role: Accessible.ListItem
						Accessible.name: (own ? qsTr("You") : title) + ": "
							+ (deleted ? qsTr("Message deleted") : messageBody.plainText)
						Accessible.description: (replyActor.length > 0
							? qsTr("Replying to %1. ").arg(replyActor) : "")
							+ (root.normalizedSequence(attachments).length > 0
								? qsTr("%1 attachments. ").arg(root.normalizedSequence(attachments).length) : "")
							+ (deliveryVisible ? deliveryLabel : "")

						function openMessageActions() {
							messageActions.targetId = stableId
							messageActions.targetCanReply = canReply
							messageActions.targetCanReact = canReact
							messageActions.targetCanDelete = canDelete
							messageActions.targetCanRetry = !!source.deliveryCanRetry
							messageActions.open()
						}
						function hydrateIfNeeded(highPriority) {
							if (contentNeedsHydration && controller
								&& typeof controller.requestContentHydration === "function")
								controller.requestContentHydration(stableId, !!highPriority)
						}
						function hydrateWithinWindow() {
							if (inHydrationWindow)
								hydrateIfNeeded(inVisibleViewport)
						}
						function itemIntersectsViewport(item) {
							if (!item || accessibilityPooled)
								return false
							const scrollPosition = timeline.contentY
							const point = item.mapToItem(timeline, 0, 0)
							return point.y + item.height > 0.5
								&& point.y < timeline.height - 0.5 && isFinite(scrollPosition)
						}
						Component.onCompleted: hydrateWithinWindow()
						onContentNeedsHydrationChanged: hydrateWithinWindow()
						onInHydrationWindowChanged: hydrateWithinWindow()
						ListView.onPooled: {
							if (messageActions.visible)
								messageActions.close()
							accessibilityPooled = true
						}
						ListView.onReused: {
							accessibilityPooled = false
							if (messagePreviewLoader.item)
								messagePreviewLoader.item.resetForReuse()
							hydrateWithinWindow()
						}

						TapHandler {
							acceptedButtons: Qt.RightButton
							onTapped: messageDelegate.openMessageActions()
						}

						RowLayout {
							id: messageRow
							anchors.fill: parent
							spacing: Theme.space2

							Rectangle {
								Layout.preferredWidth: messageDelegate.own ? 0 : Theme.avatarMedium
								Layout.preferredHeight: Layout.preferredWidth
								Layout.alignment: Qt.AlignTop
								visible: !messageDelegate.own
								radius: width / 2
								color: Theme.strip
								clip: true
								opacity: messageDelegate.startsGroup ? 1 : 0

								Image {
									id: messageAvatar
									anchors.fill: parent
									source: !messageDelegate.accessibilityPooled
										? root.safeAvatarSource(messageDelegate.avatarUrl) : ""
									asynchronous: true
									cache: false
									fillMode: Image.PreserveAspectCrop
									visible: status === Image.Ready
								}

								Label {
									anchors.centerIn: parent
									visible: messageAvatar.status !== Image.Ready
									textFormat: Text.PlainText
									text: String(messageDelegate.title || "?").slice(0, 1).toUpperCase()
									color: Theme.textStrong
									font.weight: Font.Bold
									Accessible.ignored: true
								}
							}

							ColumnLayout {
								Layout.fillWidth: true
								spacing: Theme.chatMetadataSpacing

								RowLayout {
									Layout.fillWidth: true
									visible: messageDelegate.startsGroup
									spacing: Theme.chatMetadataSpacing

									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: messageDelegate.own ? qsTr("You") : messageDelegate.title
										color: messageDelegate.own ? Theme.textStrong : Theme.accent
										font.pixelSize: Theme.fontCaption
										font.weight: Font.DemiBold
										elide: Text.ElideRight
									}

									Label {
										textFormat: Text.PlainText
										text: messageDelegate.timestamp
										visible: text.length > 0
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
									}
								}

								Rectangle {
									id: messageBubble
									objectName: "directMessageBubble_" + messageDelegate.stableId
									readonly property real bubbleHorizontalInset: messageDelegate.own
										? 0 : Theme.chatBubbleHorizontalPadding
									readonly property real bubbleVerticalInset: messageDelegate.own
										? 0 : Theme.chatBubbleVerticalPadding
									Layout.fillWidth: true
									Layout.preferredHeight: bubbleColumn.implicitHeight + bubbleVerticalInset * 2
									radius: Theme.innerRadius
									color: messageDelegate.own ? "transparent" : Theme.chatIncomingSurface
									border.color: messageDelegate.own ? "transparent" : Theme.chatIncomingBorder
									border.width: messageDelegate.own ? 0 : 1

									ColumnLayout {
										id: bubbleColumn
										anchors.fill: parent
										anchors.leftMargin: messageBubble.bubbleHorizontalInset
										anchors.rightMargin: messageBubble.bubbleHorizontalInset
										anchors.topMargin: messageBubble.bubbleVerticalInset
										anchors.bottomMargin: messageBubble.bubbleVerticalInset
										spacing: Theme.chatContentSpacing

										Rectangle {
											objectName: "directMessageReplyContext_" + messageDelegate.stableId
											Layout.fillWidth: true
											Layout.preferredHeight: replyColumn.implicitHeight + Theme.space2 * 2
											visible: messageDelegate.replyActor.length > 0
												|| messageDelegate.replySnippet.length > 0
											radius: Theme.innerRadius
											color: Theme.chatReplySurface

											Rectangle {
												anchors.left: parent.left
												anchors.top: parent.top
												anchors.bottom: parent.bottom
												width: 2
												radius: 1
												color: Theme.accent
												Accessible.ignored: true
											}

											ColumnLayout {
												id: replyColumn
												anchors.fill: parent
												anchors.leftMargin: Theme.space3
												anchors.rightMargin: Theme.space2
												anchors.topMargin: Theme.space2
												anchors.bottomMargin: Theme.space2
												spacing: 1

												Label {
													Layout.fillWidth: true
													textFormat: Text.PlainText
													text: messageDelegate.replyActor
													color: Theme.accent
													font.pixelSize: Theme.fontCaption
													font.weight: Font.DemiBold
												}

												Label {
													Layout.fillWidth: true
													textFormat: Text.PlainText
													text: messageDelegate.replySnippet
													color: Theme.textMuted
													font.pixelSize: Theme.fontCaption
													elide: Text.ElideRight
												}
											}
										}

										RichMessageBody {
											id: messageBody
											Layout.fillWidth: true
											visible: !messageDelegate.deleted
											segments: root.normalizedSegments(messageDelegate.bodySegments)
											resourceActive: !messageDelegate.accessibilityPooled
											animationsEnabled: !root.visualFixtureMode
											hoverEffectsEnabled: !root.visualFixtureMode
											accessibilitySuppressed: !messageDelegate.itemIntersectsViewport(messageBody)
											textColor: Theme.textMain
											pixelSize: Theme.fontBody
											onLinkRequested: link => Qt.openUrlExternally(link)
										}

										Label {
											objectName: "directMessageDeleted_" + messageDelegate.stableId
											Layout.fillWidth: true
											visible: messageDelegate.deleted
											textFormat: Text.PlainText
											text: qsTr("Message deleted")
											color: Theme.textMuted
											font.pixelSize: Theme.fontBody
											font.italic: true
										}

										RowLayout {
											Layout.fillWidth: true
											visible: messageDelegate.deliveryVisible
											spacing: Theme.chatMetadataSpacing

											Label {
												Layout.fillWidth: true
												textFormat: Text.PlainText
												text: messageDelegate.deliveryLabel.length > 0
													? messageDelegate.deliveryLabel : messageDelegate.deliveryState
												color: messageDelegate.deliveryState === "failed" ? Theme.danger
													: messageDelegate.deliveryState === "cancelled" ? Theme.textMuted : Theme.warning
												font.pixelSize: Theme.fontCaption
												font.weight: Font.Medium
											}

											ModernButton {
												objectName: "directMessageDeliveryRetry_" + messageDelegate.stableId
												visible: !!messageDelegate.source.deliveryCanRetry
												dense: true
												tone: "retry"
												text: String(messageDelegate.source.deliveryRetryLabel || qsTr("Retry"))
												onClicked: controller.retryMessage(messageDelegate.stableId)
											}
										}

										Loader {
											id: messageAttachmentLoader
											active: messageDelegate.hasAttachmentContent
											Layout.fillWidth: true
											sourceComponent: Component {
										AttachmentGallery {
											objectName: "directMessageAttachments_" + messageDelegate.stableId
											attachments: root.normalizedSequence(messageDelegate.attachments)
											resourceActive: !messageDelegate.accessibilityPooled
											animationsEnabled: !root.visualFixtureMode
											onAttachmentRequested: attachment => root.requestAttachment(
												attachment, messageDelegate.stableId)
											onAttachmentDownloadRequested: attachment => controller.downloadAttachment(
												root.attachmentAssetId(attachment), root.attachmentFileName(attachment))
											onAttachmentRetryRequested: attachment => controller.retryAttachmentPreview(
												messageDelegate.stableId, root.attachmentAssetId(attachment))
											onAttachmentRefreshRequested: messageDelegate.hydrateIfNeeded(true)
										}
											}
										}

										Loader {
											id: messagePreviewLoader
											active: messageDelegate.hasPreviewContent
											Layout.fillWidth: false
											Layout.alignment: Qt.AlignLeft
											Layout.preferredWidth: Math.min(messageDelegate.previewTargetWidth,
												Math.max(1, parent.width))
											Layout.maximumWidth: Layout.preferredWidth
											sourceComponent: Component {
										RichPreviewCard {
											objectName: "directMessagePreview_" + messageDelegate.stableId
											preview: messageDelegate.preview || ({})
											mediaSessionController: root.mediaSessionController
											mediaProfileFactory: root.mediaProfileFactory
											mediaSessionId: messageDelegate.stableId
											savedSizePreset: root.savedPreviewSizePreset(messageDelegate.stableId)
											renderActive: messageDelegate.inHydrationWindow
											animationsEnabled: !root.visualFixtureMode
											hoverEffectsEnabled: !root.visualFixtureMode
											watchTogetherAvailable: root.mediaSessionController
												? !root.mediaSessionController.sharedAvailable : false
											onExternalOpenRequested: url => Qt.openUrlExternally(url)
											onImageOpenRequested: (source, title) => root.managedImageOpenRequested(
												source, title, messageDelegate.stableId)
											onImageRefreshRequested: messageDelegate.hydrateIfNeeded(true)
											onInlinePlayRequested: (url, provider) => {
												if (!root.mediaSessionController)
													return
												const sameMedia = root.mediaSessionController.active
													&& String(root.mediaSessionController.sessionId || "") === messageDelegate.stableId
													&& String(root.mediaSessionController.provider || "").toLowerCase()
														=== String(provider || "").toLowerCase()
													&& String(root.mediaSessionController.url || "") === String(url || "")
												if (sameMedia) {
													if (root.mediaSessionController.detached
															&& typeof root.mediaSessionController.attach === "function")
														root.mediaSessionController.attach()
													if (root.mediaSessionController.playbackControllable)
														root.mediaSessionController.play()
													return
												}
												if (root.mediaSessionController.openInline(url, provider,
														messageDelegate.stableId)
														&& root.mediaSessionController.playbackControllable)
													root.mediaSessionController.play()
											}
											onPopoutPlayRequested: (url, provider) => {
												if (!root.mediaSessionController
														|| root.mediaSessionController.detachedPlaybackSupported === false)
													return
												const sameMedia = root.mediaSessionController.active
													&& String(root.mediaSessionController.sessionId || "") === messageDelegate.stableId
													&& String(root.mediaSessionController.provider || "").toLowerCase()
														=== String(provider || "").toLowerCase()
													&& String(root.mediaSessionController.url || "") === String(url || "")
												if (sameMedia) {
													if (!root.mediaSessionController.detached
															&& typeof root.mediaSessionController.detach === "function")
														root.mediaSessionController.detach()
													return
												}
												if (root.mediaSessionController.open(url, provider,
														messageDelegate.stableId)
														&& root.mediaSessionController.playbackControllable)
													root.mediaSessionController.play()
											}
											onDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
												if (!root.mediaSessionController)
													return
												const sameMedia = root.mediaSessionController.active
													&& String(root.mediaSessionController.sessionId || "") === messageDelegate.stableId
													&& String(root.mediaSessionController.provider || "") === "direct"
													&& String(root.mediaSessionController.url || "") === String(url || "")
													&& String(root.mediaSessionController.audioUrl || "") === String(audioUrl || "")
													&& String(root.mediaSessionController.mediaMime || "").toLowerCase()
														=== String(mime || "").toLowerCase()
													&& String(root.mediaSessionController.audioMime || "").toLowerCase()
														=== String(audioMime || "").toLowerCase()
												if (sameMedia) {
													if (root.mediaSessionController.detached
															&& typeof root.mediaSessionController.attach === "function")
														root.mediaSessionController.attach()
													if (root.mediaSessionController.playbackControllable)
														root.mediaSessionController.play()
													return
												}
												if (root.mediaSessionController.openDirectInline(url, mime, audioUrl,
														audioMime, messageDelegate.stableId))
													root.mediaSessionController.play()
											}
											onPopoutDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
												if (!root.mediaSessionController
														|| root.mediaSessionController.detachedPlaybackSupported === false)
													return
												const sameMedia = root.mediaSessionController.active
													&& String(root.mediaSessionController.sessionId || "") === messageDelegate.stableId
													&& String(root.mediaSessionController.provider || "") === "direct"
													&& String(root.mediaSessionController.url || "") === String(url || "")
													&& String(root.mediaSessionController.audioUrl || "") === String(audioUrl || "")
													&& String(root.mediaSessionController.mediaMime || "").toLowerCase()
														=== String(mime || "").toLowerCase()
													&& String(root.mediaSessionController.audioMime || "").toLowerCase()
														=== String(audioMime || "").toLowerCase()
												if (sameMedia) {
													if (!root.mediaSessionController.detached
															&& typeof root.mediaSessionController.detach === "function")
														root.mediaSessionController.detach()
													return
												}
												if (root.mediaSessionController.openDirect(url, mime, audioUrl,
														audioMime, messageDelegate.stableId))
													root.mediaSessionController.play()
											}
											onWatchTogetherRequested: (url, provider, title) =>
												root.watchTogetherRequested(url, provider, title)
											onSizePresetRequested: preset => root.previewSizePresetRequested(
												root.previewPreferenceKey(messageDelegate.stableId), preset)
										}
											}
										}

										Flow {
											objectName: "directMessageReactions_" + messageDelegate.stableId
											Layout.fillWidth: true
											visible: root.normalizedSequence(messageDelegate.reactions).length > 0
											spacing: Theme.chatMetadataSpacing

											Repeater {
												model: root.normalizedSequence(messageDelegate.reactions)
								delegate: ModernButton {
									id: directReactionButton
									required property var modelData
									required property int index
													objectName: "directMessageReaction_" + messageDelegate.stableId
														+ "_" + String(modelData.emoji || index)
													dense: true
									checkable: true
									checked: !!modelData.selfReacted
									text: String(modelData.emoji || "")
									Accessible.name: qsTr("%1 reaction, %2").arg(String(modelData.emoji || ""))
										.arg(Number(modelData.count || 0))
									contentItem: Row {
										spacing: Theme.space1
										Label {
											anchors.verticalCenter: parent.verticalCenter
											textFormat: Text.PlainText
											text: String(modelData.emoji || "")
											color: !directReactionButton.enabled ? Theme.textMuted
												: directReactionButton.checked
													? Theme.contrastText(directReactionButton.toneColor) : Theme.textStrong
											font.family: Qt.platform.os === "windows" ? "Segoe UI Emoji" : ""
											font.pixelSize: 17
										}
										Label {
											anchors.verticalCenter: parent.verticalCenter
											visible: Number(modelData.count || 0) > 0
											textFormat: Text.PlainText
											text: String(Number(modelData.count || 0))
											color: !directReactionButton.enabled ? Theme.textMuted
												: directReactionButton.checked
													? Theme.contrastText(directReactionButton.toneColor) : Theme.textMain
											font.pixelSize: Theme.fontCaption
											font.weight: Font.DemiBold
										}
									}
													enabled: messageDelegate.canReact
													onClicked: controller.toggleMessageReaction(messageDelegate.stableId,
														String(modelData.emoji || ""))
												}
											}
										}

										RowLayout {
											id: directMessageActionRow
											readonly property bool quickReactionsExpanded: messageDelegate.canReact
												&& root.quickReactionMessageId === messageDelegate.stableId
											Layout.fillWidth: true
											visible: messageDelegate.hasMessageActions
											spacing: Theme.space1
											Item { Layout.fillWidth: true }

											QuickReactionBar {
												objectName: "directMessageQuickReactions_" + messageDelegate.stableId
												Layout.alignment: Qt.AlignVCenter
												Layout.preferredWidth: implicitWidth
												Layout.preferredHeight: implicitHeight
												expanded: directMessageActionRow.quickReactionsExpanded
												activeReactions: root.normalizedSequence(messageDelegate.reactions)
												onReactionRequested: emoji => {
													controller.toggleMessageReaction(messageDelegate.stableId, emoji)
													root.quickReactionMessageId = ""
												}
											}

											ModernIconButton {
												objectName: "directMessageReply_" + messageDelegate.stableId
												visible: messageDelegate.canReply
													&& !directMessageActionRow.quickReactionsExpanded
												dense: true
												iconName: "reply"
												text: qsTr("Reply")
												onClicked: root.beginReply(messageDelegate.stableId)
											}

											ModernIconButton {
												objectName: "directMessageReact_" + messageDelegate.stableId
												visible: messageDelegate.canReact
												dense: true
												iconName: "reaction"
												selected: directMessageActionRow.quickReactionsExpanded
												text: selected ? qsTr("Close reactions") : qsTr("Add reaction")
												ToolTip.visible: hovered
												ToolTip.text: text
												onClicked: root.toggleQuickReactions(messageDelegate.stableId,
													messageDelegate.index)
											}

											ModernIconButton {
												objectName: "directMessageDelete_" + messageDelegate.stableId
												visible: messageDelegate.canDelete
													&& !directMessageActionRow.quickReactionsExpanded
												dense: true
												iconName: "delete"
												tone: "danger"
												text: qsTr("Delete")
												onClicked: controller.deleteMessage(messageDelegate.stableId)
											}
										}
									}
								}
							}
						}

						ModernMenu {
							id: messageActions
							objectName: "directMessageActionsMenu_" + messageDelegate.stableId
							accessibleName: qsTr("Message actions")
							property string targetId: ""
							property bool targetCanReply: false
							property bool targetCanReact: false
							property bool targetCanDelete: false
							property bool targetCanRetry: false
							onClosed: targetId = ""

							PayloadMenuItem {
								payload: ({ "kind": "action", "id": "dm.message.reply",
									"label": qsTr("Reply"), "icon": "reply" })
								visible: messageActions.targetCanReply
								height: visible ? rowImplicitHeight : 0
									onActionRequested: root.beginReply(messageActions.targetId)
							}
							PayloadMenuItem {
								payload: ({ "kind": "action", "id": "dm.message.react",
									"label": qsTr("Add reaction"), "icon": "reaction" })
								visible: messageActions.targetCanReact
								height: visible ? rowImplicitHeight : 0
									onActionRequested: root.showQuickReactions(messageActions.targetId, -1)
							}
							PayloadMenuItem {
								payload: ({ "kind": "action", "id": "dm.message.retry",
									"label": qsTr("Retry"), "icon": "retry" })
								visible: messageActions.targetCanRetry
								height: visible ? rowImplicitHeight : 0
								onActionRequested: controller.retryMessage(messageActions.targetId)
							}
							PayloadMenuItem {
								payload: ({ "kind": "action", "id": "dm.message.delete",
									"label": qsTr("Delete"), "icon": "delete", "tone": "danger" })
								visible: messageActions.targetCanDelete
								height: visible ? rowImplicitHeight : 0
								onActionRequested: controller.deleteMessage(messageActions.targetId)
							}
						}
					}
				}

				Rectangle {
					id: historyStatusOverlay
					objectName: root.historyErrorText.length > 0
						? "directMessageHistoryErrorBanner" : "directMessageHistoryLoadingBanner"
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.margins: Theme.space3
					height: visible ? historyStatusRow.implicitHeight + Theme.space2 * 2 : 0
					visible: root.timelineCount > 0 && controller
						&& (controller.historyLoading || root.historyErrorText.length > 0)
					z: 2
					radius: Theme.innerRadius
					color: root.historyErrorText.length > 0
						? Theme.withAlpha(Theme.warning, 0.12) : Theme.surfaceRaised
					border.color: root.historyErrorText.length > 0
						? Theme.withAlpha(Theme.warning, 0.45) : Theme.divider
					border.width: 1
					Accessible.role: root.historyErrorText.length > 0
						? Accessible.AlertMessage : Accessible.StatusBar
					Accessible.name: historyStatusLabel.text

					RowLayout {
						id: historyStatusRow
						anchors.fill: parent
						anchors.margins: Theme.space2
						spacing: Theme.space2

						ModernBusyIndicator {
							Layout.preferredWidth: 18
							Layout.preferredHeight: 18
							visible: root.historyErrorText.length === 0
							running: visible
						}

						ModernIcon {
							visible: root.historyErrorText.length > 0
							name: "warning"
							size: 16
							color: Theme.warning
							Accessible.ignored: true
						}

						Label {
							id: historyStatusLabel
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: root.historyErrorText.length > 0
								? qsTr("Some message history could not be loaded: %1").arg(root.historyErrorText)
								: qsTr("Loading earlier messages…")
							color: root.historyErrorText.length > 0 ? Theme.warning : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
				}

				ColumnLayout {
					id: directMessageState
					objectName: root.historyErrorText.length > 0 ? "directMessageErrorState"
						: controller && controller.historyLoading ? "directMessageLoadingState"
						: "directMessageEmptyState"
					anchors.centerIn: parent
					width: Math.min(parent.width - Theme.space6, 280)
					visible: controller && root.timelineCount === 0
					spacing: Theme.space3
					Accessible.role: root.historyErrorText.length > 0
						? Accessible.AlertMessage : Accessible.Pane
					Accessible.name: stateTitle.text
					Accessible.description: stateBody.text

					ModernBusyIndicator {
						objectName: "directMessageHistoryBusy"
						Layout.alignment: Qt.AlignHCenter
						visible: controller && controller.historyLoading
							&& root.historyErrorText.length === 0
						running: visible
					}

					Rectangle {
						Layout.alignment: Qt.AlignHCenter
						Layout.preferredWidth: 42
						Layout.preferredHeight: 42
						visible: !controller.historyLoading || root.historyErrorText.length > 0
						radius: Theme.innerRadius
						color: root.historyErrorText.length > 0
							? Theme.withAlpha(Theme.warning, 0.12) : Theme.accentSubtle
						Accessible.ignored: true

						ModernIcon {
							anchors.centerIn: parent
							name: root.historyErrorText.length > 0 ? "warning" : "message"
							size: 20
							color: root.historyErrorText.length > 0 ? Theme.warning : Theme.accent
						}
					}

					Label {
						id: stateTitle
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.historyErrorText.length > 0 ? qsTr("Message history unavailable")
							: controller && controller.historyLoading ? qsTr("Loading messages")
							: controller && String(controller.activeLabel || "").length > 0
								? qsTr("Start a conversation with %1").arg(controller.activeLabel)
								: qsTr("No messages yet")
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
						Accessible.ignored: true
					}

					Label {
						id: stateBody
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.historyErrorText.length > 0 ? root.historyErrorText
							: controller && controller.historyLoading ? qsTr("Retrieving your conversation…")
							: controller ? controller.emptyCopy : qsTr("Messages will appear here.")
						color: root.historyErrorText.length > 0 ? Theme.warning : Theme.textMuted
						font.pixelSize: Theme.fontBody
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
						Accessible.ignored: true
					}

					ModernButton {
						objectName: "directMessageHistoryPrivateFallback"
						Layout.alignment: Qt.AlignHCenter
						visible: root.historyErrorText.length > 0 && controller
							&& controller.mode === "history"
						text: qsTr("Continue in private mode")
						tone: "primary"
						onClicked: {
							controller.setMode("private")
							root.finishComposerAction()
						}
					}
				}
			}

			Rectangle {
				Layout.fillWidth: true
				Layout.preferredHeight: controller && controller.windowMinimized ? 0 : composerRow.implicitHeight + Theme.space3 * 2
				visible: controller && !controller.windowMinimized
				color: Theme.panel
				border.color: Theme.divider
				border.width: 1
				Accessible.role: Accessible.Pane
				Accessible.name: qsTr("Message composer for %1").arg(controller
					? String(controller.activeLabel || "") : "")

				ColumnLayout {
					id: composerRow
					anchors.fill: parent
					anchors.margins: Theme.space3
					spacing: Theme.space2

					Rectangle {
						id: composerReplySurface
						objectName: "directMessageComposerReply"
						Layout.fillWidth: true
						Layout.preferredHeight: visible ? composerReplyRow.implicitHeight + Theme.space2 * 2 : 0
						visible: controller && !!controller.hasPendingReply
						radius: Theme.innerRadius
						color: Theme.chatReplySurface
						border.color: Theme.divider
						border.width: 1
						Accessible.role: Accessible.Grouping
						Accessible.name: qsTr("Replying to %1").arg(controller
							? String(controller.pendingReplyActor || "") : "")
						Accessible.description: controller
							? String(controller.pendingReplySnippet || "") : ""

						RowLayout {
							id: composerReplyRow
							anchors.fill: parent
							anchors.margins: Theme.space2
							spacing: Theme.space2

							ColumnLayout {
								Layout.fillWidth: true
								spacing: 1

								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: qsTr("Replying to %1").arg(controller
										? String(controller.pendingReplyActor || "") : "")
									color: Theme.accent
									font.pixelSize: Theme.fontCaption
									font.weight: Font.DemiBold
								}

								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: controller ? String(controller.pendingReplySnippet || "") : ""
									color: Theme.textMuted
									font.pixelSize: Theme.fontCaption
									elide: Text.ElideRight
								}
							}

							ModernIconButton {
								objectName: "directMessageComposerReplyCancel"
								dense: true
								iconName: "close"
								text: qsTr("Cancel reply")
								onClicked: {
									controller.cancelPendingReply()
									root.finishComposerAction()
								}
							}
						}
					}

					Flow {
						objectName: "directMessageDraftAttachments"
						Layout.fillWidth: true
						visible: root.normalizedSequence(controller ? controller.draftAttachments : []).length > 0
						spacing: Theme.space1

						Repeater {
							model: root.normalizedSequence(controller ? controller.draftAttachments : [])
							delegate: Rectangle {
								required property var modelData
								required property int index
								objectName: "directMessageDraftAttachment_" + String(modelData.id || index)
								width: Math.min(280, Math.max(196,
									draftAttachmentRow.implicitWidth + Theme.space2 * 2))
								height: 42
								radius: Theme.innerRadius
								color: Theme.surfaceRaised
								border.color: String(modelData.status || "") === "failed"
									? Theme.danger : Theme.divider
								border.width: 1
								Accessible.role: Accessible.Grouping
								Accessible.name: String(modelData.fileName || qsTr("Attachment"))
								Accessible.description: root.draftAttachmentStatusText(modelData)

								RowLayout {
									id: draftAttachmentRow
									anchors.fill: parent
									anchors.leftMargin: Theme.space2
									spacing: Theme.space1

									ModernIcon {
										name: "attach"
										size: 16
										color: Theme.textMuted
									}

									ColumnLayout {
										Layout.fillWidth: true
										Layout.maximumWidth: 154
										spacing: 0

										Label {
											Layout.fillWidth: true
											textFormat: Text.PlainText
											text: String(modelData.fileName || qsTr("Attachment"))
											color: Theme.textMain
											font.pixelSize: Theme.fontCaption
											elide: Text.ElideMiddle
											Accessible.ignored: true
										}

										Label {
											objectName: "directMessageDraftAttachmentStatus_"
												+ String(modelData.id || index)
											Layout.fillWidth: true
											textFormat: Text.PlainText
											text: root.draftAttachmentStatusText(modelData)
											color: String(modelData.status || "") === "failed"
												? Theme.danger : Theme.textMuted
											font.pixelSize: Math.max(9, Theme.fontCaption - 1)
											elide: Text.ElideRight
											Accessible.ignored: true
										}
									}

									ModernBusyIndicator {
										Layout.preferredWidth: 16
										Layout.preferredHeight: 16
										visible: root.draftAttachmentBusy(modelData)
										running: visible
									}

									ModernIconButton {
										objectName: "directMessageDraftAttachmentRetry_"
											+ String(modelData.id || index)
										visible: String(modelData.status || "") === "failed"
										dense: true
										iconName: "retry"
										text: qsTr("Retry attachment")
										onClicked: {
											controller.retryDraftAttachment(String(modelData.id || ""))
											root.finishComposerAction()
										}
									}

									ModernIconButton {
										objectName: "directMessageDraftAttachmentRemove_"
											+ String(modelData.id || index)
										dense: true
										iconName: "close"
										text: qsTr("Remove attachment")
										onClicked: {
											controller.removeDraftAttachment(String(modelData.id || ""))
											root.finishComposerAction()
										}
									}
								}
							}
						}
					}

					RowLayout {
						Layout.fillWidth: true
						spacing: Theme.space2

						ModernIconButton {
							objectName: "directMessageAttach"
							visible: controller && (controller.canAttachImages || controller.canAttachFiles)
							enabled: controller && controller.canSend
							dense: true
							iconName: "attach"
							text: qsTr("Add attachment")
							onClicked: {
								controller.chooseAttachment()
								root.finishComposerAction()
							}
						}

						ModernTextArea {
							id: composer
							objectName: "directMessageComposer"
							cursorDelegate: directMessageCursorDelegate
							Layout.fillWidth: true
							Layout.preferredHeight: Math.max(48, Math.min(100, contentHeight + topPadding + bottomPadding))
							enabled: controller && controller.canSend
							placeholderText: enabled
								? qsTr("Write to %1...").arg(controller.activeLabel)
								: qsTr("Messaging is unavailable")
							Accessible.name: placeholderText
							Accessible.description: qsTr("Press Enter to send. Press Shift+Enter for a new line.")
							Component.onCompleted: text = controller ? String(controller.draft || "") : ""
							onTextChanged: if (controller && text !== String(controller.draft || "")) controller.setDraft(text)
							Keys.onPressed: event => {
								if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
									&& !(event.modifiers & Qt.ShiftModifier)) {
									root.sendCurrentDraft()
									event.accepted = true
								}
							}
						}

						ModernButton {
							objectName: "directMessageSend"
							tone: "primary"
							text: qsTr("Send")
							enabled: controller && controller.canSend
								&& (String(controller.draft || "").trim().length > 0
									|| root.normalizedSequence(controller.draftAttachments).length > 0)
							onClicked: root.sendCurrentDraft()
						}
					}
				}
			}
		}
	}

	// Frameless windows do not receive native resize borders. Keep these zones
	// narrow so controls remain easy to hit, while using the platform resize
	// loop for correct snapping, DPI handling, and edge/corner cursors on Windows.
	ResizeHandle {
		objectName: "directMessageResizeTopLeft"
		z: 1000
		resizeEdges: Qt.TopEdge | Qt.LeftEdge
		cursorShape: Qt.SizeFDiagCursor
		width: root.resizeCornerSize
		height: root.resizeCornerSize
		anchors.left: parent.left
		anchors.top: parent.top
	}

	ResizeHandle {
		objectName: "directMessageResizeTopRight"
		z: 1000
		resizeEdges: Qt.TopEdge | Qt.RightEdge
		cursorShape: Qt.SizeBDiagCursor
		width: root.resizeCornerSize
		height: root.resizeCornerSize
		anchors.right: parent.right
		anchors.top: parent.top
	}

	ResizeHandle {
		objectName: "directMessageResizeBottomLeft"
		z: 1000
		resizeEdges: Qt.BottomEdge | Qt.LeftEdge
		cursorShape: Qt.SizeBDiagCursor
		width: root.resizeCornerSize
		height: root.resizeCornerSize
		anchors.left: parent.left
		anchors.bottom: parent.bottom
	}

	ResizeHandle {
		objectName: "directMessageResizeBottomRight"
		z: 1000
		resizeEdges: Qt.BottomEdge | Qt.RightEdge
		cursorShape: Qt.SizeFDiagCursor
		width: root.resizeCornerSize
		height: root.resizeCornerSize
		anchors.right: parent.right
		anchors.bottom: parent.bottom
	}

	ResizeHandle {
		objectName: "directMessageResizeTop"
		z: 999
		resizeEdges: Qt.TopEdge
		cursorShape: Qt.SizeVerCursor
		height: root.resizeHandleThickness
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.leftMargin: root.resizeCornerSize
		anchors.rightMargin: root.resizeCornerSize
		anchors.top: parent.top
	}

	ResizeHandle {
		objectName: "directMessageResizeBottom"
		z: 999
		resizeEdges: Qt.BottomEdge
		cursorShape: Qt.SizeVerCursor
		height: root.resizeHandleThickness
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.leftMargin: root.resizeCornerSize
		anchors.rightMargin: root.resizeCornerSize
		anchors.bottom: parent.bottom
	}

	ResizeHandle {
		objectName: "directMessageResizeLeft"
		z: 999
		resizeEdges: Qt.LeftEdge
		cursorShape: Qt.SizeHorCursor
		width: root.resizeHandleThickness
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.topMargin: root.resizeCornerSize
		anchors.bottomMargin: root.resizeCornerSize
	}

	ResizeHandle {
		objectName: "directMessageResizeRight"
		z: 999
		resizeEdges: Qt.RightEdge
		cursorShape: Qt.SizeHorCursor
		width: root.resizeHandleThickness
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.topMargin: root.resizeCornerSize
		anchors.bottomMargin: root.resizeCornerSize
	}

	Shortcut {
		sequence: StandardKey.Find
		context: Qt.WindowShortcut
		enabled: root.visible && root.conversationSearchSupported && controller
			&& !controller.windowMinimized
		onActivated: root.openConversationSearch()
	}
}
