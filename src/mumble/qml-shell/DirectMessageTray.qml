import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

FocusScope {
	id: root
	objectName: surfaceId

	required property var controller
	readonly property string surfaceId: "directMessage.tray"
	readonly property var captureRect: Qt.rect(x, y, width, height)
	readonly property bool hasRows: controller && controller.summaryModel
		&& Number(controller.summaryModel.count) > 0

	implicitWidth: 310
	implicitHeight: Math.min(410, Math.max(118,
		headerColumn.implicitHeight + Theme.space4 * 2
		+ (hasRows ? Math.min(Number(controller.summaryModel.count), 5) * 58 : 56)))
	activeFocusOnTab: true
	Accessible.role: Accessible.Pane
	Accessible.name: controller ? String(controller.title || qsTr("Direct messages")) : qsTr("Direct messages")
	Accessible.description: controller ? String(controller.description || "") : ""
	Keys.onEscapePressed: controller.setTrayOpen(false)

	function safeAvatarSource(value) {
		const source = String(value || "").trim()
		return /^(image:\/\/mumble\/|data:image\/)/i.test(source) ? source : ""
	}

	function markAllRead() {
		if (!controller || !controller.summaryModel)
			return
		for (let row = 0; row < controller.summaryModel.count; ++row) {
			const conversation = controller.summaryModel.get(row)
			if (conversation && Number(conversation.unreadCount || 0) > 0)
				controller.markRead(String(conversation.id || ""))
		}
	}

	Rectangle {
		anchors.fill: parent
		radius: Theme.innerRadius
		color: Theme.popupBackground
		border.color: root.activeFocus ? Theme.focus : Theme.popupBorder
		border.width: root.activeFocus ? Theme.focusRingWidth : 1

		Rectangle {
			anchors.fill: parent
			anchors.margins: 1
			radius: Math.max(0, parent.radius - 1)
			color: "transparent"
			border.color: Theme.elevationHighlight
			border.width: 1
		}
	}

	ColumnLayout {
		anchors.fill: parent
		anchors.margins: Theme.space3
		spacing: Theme.space2

		ColumnLayout {
			id: headerColumn
			Layout.fillWidth: true
			spacing: Theme.space1

			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: controller ? String(controller.title || qsTr("Direct messages")) : qsTr("Direct messages")
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
					elide: Text.ElideRight
				}

				ModernButton {
					objectName: "directMessageTrayMarkAllRead"
					visible: controller && controller.hasUnread
					dense: true
					text: qsTr("Mark read")
					onClicked: root.markAllRead()
				}

				ModernIconButton {
					objectName: "directMessageTrayClose"
					dense: true
					iconName: "close"
					Accessible.name: qsTr("Close direct messages")
					onClicked: controller.setTrayOpen(false)
				}
			}

			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: controller ? String(controller.description || "") : ""
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				wrapMode: Text.Wrap
				maximumLineCount: 2
				elide: Text.ElideRight
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: 1
			color: Theme.divider
		}

		Label {
			Layout.fillWidth: true
			Layout.fillHeight: true
			visible: !root.hasRows
			textFormat: Text.PlainText
			text: controller && controller.available
				? qsTr("No direct messages yet") : qsTr("Connect to view direct messages")
			color: Theme.textMuted
			font.pixelSize: Theme.fontBody
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignVCenter
			wrapMode: Text.Wrap
			Accessible.name: text
		}

		ListView {
			id: conversationList
			objectName: "directMessageTrayList"
			Layout.fillWidth: true
			Layout.fillHeight: true
			visible: root.hasRows
			clip: true
			spacing: Theme.space1
			model: controller ? controller.summaryModel : null
			reuseItems: true
			cacheBuffer: height
			boundsBehavior: Flickable.StopAtBounds
			activeFocusOnTab: true
			Accessible.role: Accessible.List
			Accessible.name: qsTr("Direct-message conversations")
			ScrollBar.vertical: ModernScrollBar { policy: ScrollBar.AsNeeded }

			delegate: Item {
				id: conversationRow
				objectName: "directMessageTrayRow_" + stableId
				required property int index
				required property string stableId
				required property string title
				required property string subtitle
				required property int unreadCount
				required property string avatarUrl
				required property bool selected

				width: ListView.view.width
				height: 56
				activeFocusOnTab: true
				Accessible.role: Accessible.ListItem
				Accessible.name: title + (subtitle.length > 0 ? ": " + subtitle : "")
				Accessible.description: unreadCount > 0
					? qsTr("%1 unread messages").arg(unreadCount) : qsTr("No unread messages")
				Accessible.onPressAction: activate()
				Keys.onReturnPressed: activate()
				Keys.onEnterPressed: activate()

				function activate() {
					controller.openConversation(stableId)
				}

				Rectangle {
					anchors.fill: parent
					radius: Theme.innerRadius
					color: conversationRow.activeFocus ? Theme.popupSelected
						: rowPointer.hovered ? Theme.popupHover
						: conversationRow.selected ? Theme.selected : Theme.panel
					border.color: conversationRow.activeFocus ? Theme.focus
						: conversationRow.selected ? Theme.accent : Theme.divider
					border.width: conversationRow.activeFocus ? Theme.focusRingWidth : 1
				}

				HoverHandler { id: rowPointer }
				TapHandler { onTapped: conversationRow.activate() }

				RowLayout {
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space2
					spacing: Theme.space2

					Rectangle {
						Layout.preferredWidth: Theme.avatarMedium
						Layout.preferredHeight: Theme.avatarMedium
						radius: width / 2
						color: Theme.accent
						clip: true

						Image {
							id: avatarImage
							anchors.fill: parent
							source: root.safeAvatarSource(conversationRow.avatarUrl)
							asynchronous: true
							cache: false
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
						}

						Label {
							anchors.centerIn: parent
							visible: avatarImage.status !== Image.Ready
							textFormat: Text.PlainText
							text: conversationRow.title.slice(0, 2).toUpperCase()
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
							text: conversationRow.title
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.weight: conversationRow.unreadCount > 0 ? Font.DemiBold : Font.Medium
							elide: Text.ElideRight
						}

						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: conversationRow.subtitle
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}

					Rectangle {
						visible: conversationRow.unreadCount > 0
						Layout.preferredWidth: Math.max(22, unreadLabel.implicitWidth + Theme.space2)
						Layout.preferredHeight: 22
						radius: height / 2
						color: Theme.accentSubtle
						border.color: Theme.accent
						Label {
							id: unreadLabel
							anchors.centerIn: parent
							textFormat: Text.PlainText
							text: conversationRow.unreadCount > 99 ? "99+" : String(conversationRow.unreadCount)
							color: Theme.textStrong
							font.pixelSize: Theme.fontCaption
							font.weight: Font.Bold
							Accessible.ignored: true
						}
					}

					ModernIconButton {
						visible: conversationRow.unreadCount > 0
						dense: true
						iconName: "check"
						Accessible.name: qsTr("Mark messages from %1 as read").arg(conversationRow.title)
						onClicked: controller.markRead(conversationRow.stableId)
					}
				}
			}
		}
	}
}
