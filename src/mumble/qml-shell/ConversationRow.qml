import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

// One row of the v2 timeline (docs/chat-timeline-v2-plan.md). Its height follows
// from the row data and the list width alone: text is laid out synchronously,
// attachments size themselves from their metadata, and a link card keeps a
// placeholder of its expected height until the card is ready. When a row still
// resizes later, the view keeps the row the reader is looking at in place.
ChatMessageFrame {
	id: row

	required property int index
	required property int threadId
	required property int replyToMessageId
	required property string kind
	required property string authorName
	required property string timeText
	required property string dateLabel
	required property bool groupStart
	required property bool edited
	required property var bodyBlocks
	required property string plainText
	required property var media
	required property var card
	required property var reactions
	required property string replyActor
	required property string replySnippet
	required property string deliveryState
	required property string deliveryLabel
	required property bool deliveryCanRetry
	required property string avatarUrl
	required property var attachments
	required property var preview
	required property bool canReply
	required property bool canReact
	required property bool canDelete

	// ConversationView sets the list and the lane geometry on the delegate. The
	// attached ListView.view is not there yet while the row is first created, and
	// a row laid out without its list takes its first height from a one pixel lane.
	property ListView view: null
	readonly property var host: view ? view.host : null
	readonly property bool logRow: kind === "log"
	readonly property bool systemRow: kind === "system" || logRow
	readonly property bool hasActions: canReply || canReact || canDelete || deliveryCanRetry
	property bool inlineReactionsOpen: false
	readonly property bool actionsOpen: inlineReactionsOpen
		|| (!!view && view.menuStableId === stableId)
		|| (!!view && view.hostReactionPopup && !!host && host.quickReactionMessageId === stableId)
	// Widths come from the frame's bindings, not from contentItem: its anchors
	// resolve only after creation, and the first layout must already be final.
	readonly property real innerWidth: Math.max(1, messageWidth - horizontalPadding * 2)
	readonly property real columnWidth: Math.max(1, innerWidth - (systemRow ? 0 : Theme.avatarMedium + Theme.space2))

	readonly property bool hasPreview: !!preview && Object.keys(preview).length > 0
	readonly property bool previewFailed: {
		if (!hasPreview)
			return false
		const state = String(preview.state || "").toLowerCase()
		const status = String(preview.status || "").toLowerCase()
		return state === "error" || state === "failed" || status === "error" || status === "failed"
			|| preview.failed === true
	}
	readonly property bool previewLoading: hasPreview && !previewFailed
		&& (preview.state === "loading" || preview.loading === true)
	readonly property bool cardExpected: !!card && String(card.status || "").length > 0
	readonly property bool showPreviewCard: hasPreview && !previewLoading && !previewFailed
	readonly property bool showPreviewPlaceholder: !previewFailed
		&& (previewLoading || (!hasPreview && cardExpected)
			|| (showPreviewCard && previewLoader.status !== Loader.Ready))
	readonly property var blockMedia: (media || []).filter(item => item.kind !== "inlineImage")
	readonly property bool attachmentsLoading: (attachments || []).some(attachment =>
		!!attachment && String(attachment.state || "").toLowerCase() === "loading")
	readonly property bool needsHydration: !logRow && (previewLoading || attachmentsLoading
		|| (!hasPreview && cardExpected)
		|| ((attachments || []).length === 0 && blockMedia.length > 0))

	// Arrival animation: paint only (opacity and a short rise), owned by the row so
	// it starts and stops with the delegate and never moves the list itself.
	function playArrival() {
		arrival.stop()
		if (view && view.arrivalActive(index)) {
			body.opacity = 0
			rise.y = Theme.space2
			arrival.start()
		} else {
			body.opacity = 1
			rise.y = 0
		}
	}
	function requestHydrationIfNeeded() {
		if (needsHydration && view)
			view.queueHydration(stableId, row)
	}
	function openActions(item, x, y) {
		if (view && hasActions)
			view.openMessageActions(row, item, x, y)
	}
	function reactionsForQuickBar() {
		return (reactions || []).map(reaction => ({
			"emoji": reaction.emoji,
			"count": reaction.count,
			"selfReacted": reaction.self
		}))
	}

	logEntry: logRow
	systemMessage: systemRow
	startsGroup: groupStart || systemRow
	dateSeparatorLabel: dateLabel
	bodyImplicitHeight: body.implicitHeight
	wideContent: true
	preferredWideContentWidth: laneMaximumWidth
	hoverEffectsEnabled: !!view && !view.moving

	Component.onCompleted: {
		playArrival()
		requestHydrationIfNeeded()
	}
	ListView.onReused: {
		accessibilityPooled = false
		playArrival()
		requestHydrationIfNeeded()
	}
	ListView.onPooled: {
		inlineReactionsOpen = false
		arrival.stop()
		body.opacity = 1
		rise.y = 0
		accessibilityPooled = true
	}
	onNeedsHydrationChanged: requestHydrationIfNeeded()

	TapHandler {
		acceptedButtons: Qt.RightButton
		enabled: row.hasActions
		onTapped: eventPoint => row.openActions(row, eventPoint.position.x, eventPoint.position.y)
	}

	ParallelAnimation {
		id: arrival
		NumberAnimation { target: body; property: "opacity"; to: 1; duration: 200; easing.type: Easing.OutCubic }
		NumberAnimation { target: rise; property: "y"; to: 0; duration: 240; easing.type: Easing.OutCubic }
	}

	Item {
		id: body

		width: row.innerWidth
		Accessible.role: Accessible.ListItem
		Accessible.name: [row.logRow ? "" : (row.own ? qsTr("You") : row.authorName), row.plainText, row.timeText]
			.filter(part => String(part).length > 0).join("  ·  ")
		implicitHeight: row.logRow ? logText.implicitHeight : Math.max(row.startsGroup && !row.systemRow ? Theme.avatarMedium : 0,
			column.implicitHeight)
		height: implicitHeight
		transform: Translate { id: rise }

		Text {
			id: logText
			visible: row.logRow
			width: parent.width
			text: row.timeText.length > 0 ? row.timeText + "  " + row.plainText : row.plainText
			textFormat: Text.PlainText
			wrapMode: Text.Wrap
			color: Theme.textMain
			font.pixelSize: Theme.fontLabel
		}

		Rectangle {
			id: avatar
			visible: !row.systemRow && row.startsGroup
			width: Theme.avatarMedium
			height: width
			radius: width / 2
			color: Theme.strip
			clip: true

			Image {
				id: avatarImage
				anchors.fill: parent
				source: row.host && !row.accessibilityPooled ? row.host.safeRenderImageSource(row.avatarUrl) : ""
				asynchronous: true
				cache: true
				sourceSize: Qt.size(Theme.avatarMedium * Screen.devicePixelRatio, Theme.avatarMedium * Screen.devicePixelRatio)
				fillMode: Image.PreserveAspectCrop
				visible: status === Image.Ready
			}
			Text {
				anchors.centerIn: parent
				visible: avatarImage.status !== Image.Ready
				text: (row.authorName || "?").slice(0, 1).toUpperCase()
				textFormat: Text.PlainText
				color: Theme.textStrong
				font.bold: true
				font.pixelSize: Theme.chatAuthorFontSize
			}
		}

		// Continuation rows show their time in the avatar gutter on hover.
		Text {
			visible: !row.systemRow && !row.startsGroup && row.hovered
			width: Theme.avatarMedium
			y: Theme.space1
			horizontalAlignment: Text.AlignHCenter
			text: row.timeText
			textFormat: Text.PlainText
			color: Theme.textFaint
			font.pixelSize: Theme.fontCaption - 1
		}

		Column {
			id: column

			visible: !row.logRow
			x: row.systemRow ? 0 : Theme.avatarMedium + Theme.space2
			width: row.columnWidth
			spacing: Theme.chatContentSpacing

			Row {
				visible: row.startsGroup && !row.systemRow
				spacing: Theme.chatMetadataSpacing + 2

				Text {
					id: authorLabel
					text: row.own ? qsTr("You") : row.authorName
					textFormat: Text.PlainText
					color: row.own ? Theme.accent : Theme.textStrong
					font.pixelSize: Theme.chatAuthorFontSize
					font.weight: Font.DemiBold
					elide: Text.ElideRight
					width: Math.min(implicitWidth, row.columnWidth * 0.6)
				}
				Text {
					anchors.baseline: authorLabel.baseline
					text: row.edited ? row.timeText + " · " + qsTr("edited") : row.timeText
					textFormat: Text.PlainText
					color: Theme.chatMetadata
					font.pixelSize: Theme.fontCaption
				}
			}

			Rectangle {
				visible: row.replyActor.length > 0 || row.replySnippet.length > 0
				width: Math.min(row.columnWidth, Math.max(160, replyText.implicitWidth + Theme.space3 * 2))
				height: replyText.implicitHeight + Theme.space1 * 2
				radius: Theme.innerRadius
				color: replyHover.hovered && row.replyToMessageId > 0 ? Theme.surfaceHover : Theme.chatReplySurface
				Accessible.role: Accessible.Button
				Accessible.name: qsTr("Replying to %1: %2").arg(row.replyActor).arg(row.replySnippet)

				HoverHandler {
					id: replyHover
					cursorShape: row.replyToMessageId > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
				}
				TapHandler {
					enabled: row.replyToMessageId > 0 && !!row.view
					onTapped: row.view.revealMessage(row.threadId, row.replyToMessageId)
				}

				Rectangle {
					width: 2
					height: parent.height
					color: Theme.accent
				}
				Text {
					id: replyText
					x: Theme.space3
					width: parent.width - Theme.space3 * 2
					anchors.verticalCenter: parent.verticalCenter
					text: "<b>" + row.replyActor.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>  "
						+ row.replySnippet.replace(/&/g, "&amp;").replace(/</g, "&lt;")
					textFormat: Text.StyledText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
				}
			}

			ChatBodyBlocks {
				visible: row.bodyBlocks.length > 0
				width: row.columnWidth
				blocks: row.bodyBlocks
				textColor: row.systemRow ? Theme.textMuted : Theme.textMain
				onLinkActivated: link => Qt.openUrlExternally(link)
			}

			Loader {
				id: galleryLoader
				active: (row.attachments || []).length > 0
				visible: active
				width: row.columnWidth
				height: active && item ? item.implicitHeight : 0
				sourceComponent: AttachmentGallery {
					attachments: row.host ? row.host.normalizedModelList(row.attachments) : row.attachments
					resourceActive: !row.accessibilityPooled
					accessibilitySuppressed: row.accessibilityPooled
					accessibilityViewportItem: row.view
					accessibilityViewportOffset: row.view ? row.view.contentY : 0
					animationsEnabled: !!row.view && !row.view.moving
					hoverEffectsEnabled: row.hoverEffectsEnabled
					onAttachmentRequested: attachment => row.host && row.host.requestAttachment(attachment, row.stableId)
					onAttachmentDownloadRequested: attachment => row.host && row.host.downloadAttachment(attachment, false)
					onAttachmentRetryRequested: attachment => row.view && row.view.commands.retryAttachmentPreview(
						row.stableId, String(attachment.assetId || attachment.assetID || ""))
					onAttachmentRefreshRequested: row.view && row.view.queueHydration(row.stableId, row, true)
				}
			}

			// Media the current pipeline has not described yet keeps its size from the
			// row's own metadata.
			Rectangle {
				visible: !galleryLoader.active && row.blockMedia.length > 0
				readonly property var first: row.blockMedia.length > 0 ? row.blockMedia[0] : ({})
				width: Math.min(row.columnWidth, 400)
				height: visible ? (first.geometryKnown && first.width > 0
					? Math.min(320, Math.round(width * first.height / first.width)) : 160) : 0
				radius: Theme.innerRadius
				color: Theme.strip
				border.color: Theme.divider
			}

			Item {
				id: previewSlot
				visible: row.showPreviewCard || row.showPreviewPlaceholder
				width: Math.min(row.columnWidth, Theme.chatRichMaximumWidth - Theme.avatarMedium - Theme.space2)
				// A card seen before keeps its last height while it loads again, so a
				// row scrolled back into view does not grow under the reader.
				readonly property real rememberedHeight: row.view ? row.view.previewHeightFor(row.stableId) : 0
				readonly property bool cardReady: previewLoader.status === Loader.Ready && !!previewLoader.item
				// Otherwise start from the card layout; rendered cards are about this tall.
				readonly property real expectedHeight: row.card.layout === "large" ? 300
					: row.card.layout === "small" ? 200 : 180
				height: !visible ? 0 : cardReady ? previewLoader.item.implicitHeight
					: rememberedHeight > 0 ? rememberedHeight : expectedHeight
				onHeightChanged: {
					if (cardReady && row.view)
						row.view.rememberPreviewHeight(row.stableId, height)
				}

				Rectangle {
					id: placeholder
					visible: row.showPreviewPlaceholder
					width: parent.width
					height: parent.height
					radius: Theme.innerRadius
					color: Theme.surfaceRaised
					border.color: Theme.divider

					Column {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						anchors.margins: Theme.space3
						spacing: 2

						Text {
							width: parent.width
							text: String(row.card.title || row.preview.title || qsTr("Loading link preview"))
							textFormat: Text.PlainText
							elide: Text.ElideRight
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.weight: Font.DemiBold
						}
						Text {
							width: parent.width
							text: String(row.card.url || row.preview.url || "")
							textFormat: Text.PlainText
							elide: Text.ElideRight
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
						}
					}
				}

				Loader {
					id: previewLoader
					width: parent.width
					active: row.showPreviewCard
					asynchronous: true
					opacity: status === Loader.Ready ? 1 : 0
					sourceComponent: RichPreviewCard {
						preview: row.preview
						width: previewSlot.width
						mediaSessionController: mediaSession
						mediaProfileFactory: mediaProfiles
						mediaSessionId: row.stableId
						visualMediaFixtureMode: row.host ? row.host.visualMediaFixtureMode : ""
						savedSizePreset: row.host ? row.host.richPreviewSizePreset(row.stableId) : ""
						animationsEnabled: !!row.view && !row.view.moving
						hoverEffectsEnabled: row.hoverEffectsEnabled
						previewIdentity: row.stableId + "|" + String(row.preview.url || row.preview.embedUrl
							|| row.preview.mediaUrl || row.preview.title || "")
						renderActive: !row.accessibilityPooled
						watchTogetherAvailable: !mediaSession.sharedAvailable
						onExternalOpenRequested: url => Qt.openUrlExternally(url)
						onImageOpenRequested: (source, title) => row.host && row.host.openManagedPreviewImage(
							source, title, row.stableId)
						onImageRefreshRequested: row.view && row.view.queueHydration(row.stableId, row, true)
						onDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
							if (mediaSession.openDirectInline(url, mime, audioUrl, audioMime, row.stableId))
								mediaSession.play()
						}
						onPopoutDirectMediaRequested: (url, mime, audioUrl, audioMime, title) => {
							if (mediaSession.detachedPlaybackSupported
									&& mediaSession.openDirect(url, mime, audioUrl, audioMime, row.stableId))
								mediaSession.play()
						}
						onInlinePlayRequested: (url, provider) => {
							if (mediaSession.openInline(url, provider, row.stableId) && mediaSession.playbackControllable)
								mediaSession.play()
						}
						onPopoutPlayRequested: (url, provider) => {
							if (mediaSession.detachedPlaybackSupported && mediaSession.open(url, provider, row.stableId)
									&& mediaSession.playbackControllable)
								mediaSession.play()
						}
						onWatchTogetherRequested: (url, provider, title, presentationAspect) =>
							row.host && row.host.startWatchTogether(url, provider, title, presentationAspect)
						onSizePresetRequested: preset => row.host && row.host.rememberRichPreviewSizePreset(row.stableId, preset)
					}
				}
			}

			Flow {
				visible: (row.reactions || []).length > 0
				width: row.columnWidth
				spacing: Theme.chatMetadataSpacing

				Repeater {
					model: row.reactions || []

					delegate: Button {
						id: reactionButton
						required property var modelData
						implicitWidth: reactionContent.implicitWidth + Theme.space2 * 2
						implicitHeight: 28
						enabled: row.canReact
						hoverEnabled: true
						Accessible.name: qsTr("%1 reaction, %2").arg(modelData.emoji || "").arg(modelData.count || 0)
						ToolTip.visible: hovered && (modelData.actorNames || []).length > 0
						ToolTip.text: (modelData.actorNames || []).join(", ")
						background: Rectangle {
							radius: height / 2
							color: reactionButton.modelData.self ? Theme.selected
								: reactionButton.hovered ? Theme.surfaceHover : Theme.strip
							border.color: reactionButton.modelData.self ? Theme.accent : Theme.divider
						}
						contentItem: Row {
							id: reactionContent
							spacing: Theme.space1
							Text {
								anchors.verticalCenter: parent.verticalCenter
								text: String(reactionButton.modelData.emoji || "")
								textFormat: Text.PlainText
								font.family: Qt.platform.os === "windows" ? "Segoe UI Emoji" : ""
								font.pixelSize: 15
							}
							Text {
								anchors.verticalCenter: parent.verticalCenter
								text: String(Number(reactionButton.modelData.count || 0))
								textFormat: Text.PlainText
								color: Theme.textMain
								font.pixelSize: Theme.fontCaption
								font.weight: Font.DemiBold
							}
						}
						onClicked: row.view && row.view.commands.toggleReaction(row.stableId, modelData.emoji)
					}
				}
			}

			Text {
				visible: row.deliveryLabel.length > 0 && row.deliveryState !== "sent"
				text: row.deliveryLabel
				textFormat: Text.PlainText
				color: row.deliveryState === "failed" ? Theme.danger
					: row.deliveryState === "sending" ? Theme.textMuted : Theme.warning
				font.pixelSize: Theme.fontCaption
			}
		}
	}

	// Hover actions float over the row and never take layout space. They are
	// created only while shown: rows are laid out a viewport ahead, and five
	// buttons per row made fast scrolling stall.
	Loader {
		id: actionTrayLoader
		active: row.hasActions && !row.systemRow && (row.hovered || row.actionsOpen)
		anchors.right: parent.right
		y: -Theme.space2
		z: 5
		sourceComponent: actionTrayComponent
	}

	Component {
		id: actionTrayComponent

		Rectangle {
			id: actionTray
			width: actionButtons.implicitWidth + Theme.space1
			height: 32
			radius: height / 2
			color: Theme.strip
			border.color: Theme.divider

			Row {
				id: actionButtons
				anchors.centerIn: parent

				QuickReactionBar {
					visible: row.inlineReactionsOpen
					anchors.verticalCenter: parent.verticalCenter
					expanded: row.inlineReactionsOpen
					activeReactions: row.reactionsForQuickBar()
					onReactionRequested: emoji => {
						row.view.commands.toggleReaction(row.stableId, emoji)
						row.inlineReactionsOpen = false
					}
				}
				ModernIconButton {
					visible: row.canReply
					dense: true
					iconName: "reply"
					text: qsTr("Reply")
					Accessible.name: text
					onClicked: row.view.commands.reply(row.stableId)
				}
				ModernIconButton {
					id: reactButton
					visible: row.canReact
					dense: true
					iconName: "reaction"
					selected: row.actionsOpen
					text: selected ? qsTr("Close reactions") : qsTr("Add reaction")
					Accessible.name: text
					onClicked: {
						if (!row.view.hostReactionPopup)
							row.inlineReactionsOpen = !row.inlineReactionsOpen
						else if (row.host)
							row.host.toggleQuickReactions(row.stableId, -1, reactButton, row.reactionsForQuickBar())
					}
				}
				ModernIconButton {
					visible: row.deliveryCanRetry
					dense: true
					iconName: "retry"
					text: qsTr("Retry")
					Accessible.name: text
					onClicked: row.view.commands.retry(row.stableId)
				}
				ModernIconButton {
					id: moreButton
					dense: true
					iconName: "more"
					text: qsTr("More actions")
					Accessible.name: text
					onClicked: row.openActions(moreButton, 0, moreButton.height)
				}
			}
		}
	}
}
