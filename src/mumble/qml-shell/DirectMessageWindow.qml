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
	readonly property string surfaceId: "directMessage.window"
	// A separate QQuickWindow is captured in its own window-local coordinate space.
	readonly property var captureRect: Qt.rect(0, 0, width, height)
	readonly property bool docked: controller && controller.windowDocked

	title: controller && String(controller.activeLabel || "").length > 0
		? qsTr("Direct message · %1").arg(controller.activeLabel) : qsTr("Direct message")
	width: 360
	height: controller && controller.windowMinimized ? 74 : 438
	minimumWidth: 320
	minimumHeight: 74
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
		if (controller && controller.canSend && String(controller.draft || "").trim().length > 0)
			controller.sendDraft()
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
		return []
	}

	onVisibleChanged: {
		if (visible) {
			positionNearParent()
			requestActivate()
			controller.markRead()
		}
	}
	onActiveChanged: {
		if (active && controller)
			controller.markRead()
	}
	onClosing: close => {
		close.accepted = false
		if (controller)
			controller.closeConversation()
	}

	Connections {
		target: controller
		function onWindowDockedChanged() { root.positionNearParent() }
		function onWindowMinimizedChanged() {
			if (!controller.windowMinimized && root.visible) {
				root.showNormal()
				root.requestActivate()
			}
		}
		function onDraftChanged() {
			if (composer.text !== String(controller.draft || ""))
				composer.text = String(controller.draft || "")
		}
	}

	Rectangle {
		anchors.fill: parent
		radius: Theme.innerRadius
		color: Theme.popupBackground
		border.color: windowFocusScope.activeFocus ? Theme.focus : Theme.popupBorder
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
		Accessible.description: controller && controller.mode === "private"
			? qsTr("Private in-memory direct-message conversation")
			: qsTr("Direct-message conversation with persistent history")
		Keys.onEscapePressed: controller.closeConversation()

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
					onActiveChanged: if (active) root.startSystemMove()
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
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: controller && controller.mode === "history"
								? qsTr("History · stored on server") : qsTr("Private · in-memory only")
							color: controller && controller.mode === "history" ? Theme.success : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}

					ModernIconButton {
						objectName: "directMessageModeToggle"
						dense: true
						text: controller && controller.mode === "history" ? "H" : "P"
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
						onClicked: controller.setWindowDocked(!controller.windowDocked)
					}

					ModernIconButton {
						objectName: "directMessageWindowMinimize"
						dense: true
						text: "−"
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

			Rectangle {
				objectName: "directMessageModeBanner"
				Layout.fillWidth: true
				Layout.preferredHeight: controller && controller.windowMinimized ? 0 : modeBanner.implicitHeight + Theme.space3 * 2
				visible: controller && !controller.windowMinimized
				color: controller && controller.mode === "private"
					? Theme.withAlpha(Theme.warning, 0.10) : Theme.withAlpha(Theme.success, 0.08)
				border.color: controller && controller.mode === "private"
					? Theme.withAlpha(Theme.warning, 0.42) : Theme.withAlpha(Theme.success, 0.32)
				border.width: 1

				Label {
					id: modeBanner
					anchors.fill: parent
					anchors.margins: Theme.space3
					textFormat: Text.PlainText
					text: controller && controller.mode === "private"
						? qsTr("Private mode · Messages clear when this window closes.")
						: qsTr("History mode · Messages are stored by the server.")
					color: controller && controller.mode === "private" ? Theme.warning : Theme.success
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Medium
					wrapMode: Text.Wrap
					Accessible.name: text
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
					clip: true
					spacing: Theme.space2
					model: controller ? controller.timelineModel : null
					reuseItems: true
					cacheBuffer: Math.max(0, height * 2)
					boundsBehavior: Flickable.StopAtBounds
					activeFocusOnTab: true
					Accessible.role: Accessible.List
					Accessible.name: qsTr("Messages with %1").arg(controller ? controller.activeLabel : "")
					ScrollBar.vertical: ModernScrollBar { policy: ScrollBar.AsNeeded }

					delegate: Item {
						id: messageDelegate
						required property string stableId
						required property string title
						required property string timestamp
						required property var bodySegments
						required property bool own
						required property string avatarUrl

						width: ListView.view.width
						height: messageBubble.height + Theme.space1
						Accessible.role: Accessible.ListItem
						Accessible.name: (own ? qsTr("You") : title) + ": " + messageBody.plainText

						Rectangle {
							id: messageBubble
							width: Math.min(messageDelegate.width * 0.86,
								Math.max(116, bubbleColumn.implicitWidth + Theme.space3 * 2))
							height: bubbleColumn.implicitHeight + Theme.space3 * 2
							anchors.right: messageDelegate.own ? parent.right : undefined
							anchors.left: messageDelegate.own ? undefined : parent.left
							radius: Theme.innerRadius
							color: messageDelegate.own ? Theme.chatOwnSurface : Theme.surfaceRaised
							border.color: messageDelegate.own ? Theme.chatOwnBorder : Theme.divider
							border.width: 1

							ColumnLayout {
								id: bubbleColumn
								anchors.fill: parent
								anchors.margins: Theme.space3
								spacing: Theme.space1

								RowLayout {
									Layout.fillWidth: true
									spacing: Theme.space2

									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: messageDelegate.own ? qsTr("You") : messageDelegate.title
										color: Theme.textStrong
										font.pixelSize: Theme.fontCaption
										font.weight: Font.DemiBold
										elide: Text.ElideRight
									}

									Label {
										textFormat: Text.PlainText
										text: messageDelegate.timestamp
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
									}
								}

								RichMessageBody {
									id: messageBody
									Layout.fillWidth: true
									segments: root.normalizedSegments(messageDelegate.bodySegments)
									textColor: Theme.textMain
									pixelSize: Theme.fontBody
									onLinkRequested: link => Qt.openUrlExternally(link)
								}
							}
						}
					}
				}

				ColumnLayout {
					anchors.centerIn: parent
					width: Math.min(parent.width - Theme.space6, 280)
					visible: controller && Number(controller.timelineModel.count) === 0
					spacing: Theme.space2

					ModernBusyIndicator {
						Layout.alignment: Qt.AlignHCenter
						visible: controller && controller.historyLoading
						running: visible
					}

					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: controller && String(controller.historyError || "").length > 0
							? controller.historyError : controller ? controller.emptyCopy : qsTr("No messages yet")
						color: controller && String(controller.historyError || "").length > 0
							? Theme.warning : Theme.textMuted
						font.pixelSize: Theme.fontBody
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
						Accessible.name: text
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

				RowLayout {
					id: composerRow
					anchors.fill: parent
					anchors.margins: Theme.space3
					spacing: Theme.space2

					ModernTextArea {
						id: composer
						objectName: "directMessageComposer"
						Layout.fillWidth: true
						Layout.preferredHeight: Math.max(48, Math.min(100, contentHeight + topPadding + bottomPadding))
						enabled: controller && controller.canSend
						placeholderText: enabled
							? qsTr("Write to %1...").arg(controller.activeLabel) : qsTr("Unavailable")
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
							&& String(controller.draft || "").trim().length > 0
						onClicked: root.sendCurrentDraft()
					}
				}
			}
		}
	}
}
