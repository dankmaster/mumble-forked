pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var thread: ({})
	property color accent: Theme.accent
	readonly property var items: thread && thread.items
		&& thread.items.length !== undefined ? thread.items : []
	signal openRequested(string url)

	visible: items.length > 0
	spacing: Theme.space2

	Label {
		Layout.fillWidth: true
		text: root.thread && root.thread.kind === "forum-thread"
			? qsTr("Thread context") : qsTr("Conversation context")
		textFormat: Text.PlainText
		color: Theme.textMuted
		font.pixelSize: Theme.fontCaption
		font.weight: Font.DemiBold
		Accessible.role: Accessible.Heading
	}

	Repeater {
		model: root.items

		delegate: Rectangle {
			id: contextCard
			required property int index
			required property var modelData
			objectName: "embedThreadContextItem_" + index

			Layout.fillWidth: true
			implicitHeight: contextLayout.implicitHeight + Theme.space3 * 2
			radius: Theme.innerRadius
			color: Theme.embedSurface
			border.color: Theme.withAlpha(root.accent, 0.34)
			border.width: 1
			Accessible.role: Accessible.StaticText
			Accessible.name: contextCard.roleLabel() + ": "
				+ String(modelData.authorName || modelData.authorHandle || "")
			Accessible.description: String(modelData.text || "")

			Rectangle {
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.bottom: parent.bottom
				width: 3
				radius: 1.5
				color: root.accent
			}

			ColumnLayout {
				id: contextLayout
				anchors.fill: parent
				anchors.leftMargin: Theme.space3
				anchors.rightMargin: Theme.space3
				anchors.topMargin: Theme.space3
				anchors.bottomMargin: Theme.space3
				spacing: Theme.space1

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space1

					Label {
						text: contextCard.roleLabel()
						textFormat: Text.PlainText
						color: root.accent
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
					}
					Label {
						Layout.fillWidth: true
						text: contextCard.authorLine()
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
					}
				}

				Label {
					objectName: "embedThreadContextText_" + contextCard.index
					Layout.fillWidth: true
					visible: String(contextCard.modelData.text || "").length > 0
					text: String(contextCard.modelData.text || "")
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontCaption
					lineHeight: 1.2
					wrapMode: Text.Wrap
					maximumLineCount: 5
					elide: Text.ElideRight
				}

				Button {
					id: contextUrlButton
					objectName: "embedThreadContextUrl_" + contextCard.index
					Layout.fillWidth: true
					visible: String(contextCard.modelData.url || "").length > 0
					text: String(contextCard.modelData.url || "")
					flat: true
					leftPadding: 0
					rightPadding: 0
					topPadding: 0
					bottomPadding: 0
					Accessible.role: Accessible.Link
					Accessible.name: qsTr("Open context source")
					Accessible.description: String(contextCard.modelData.url || "")
					background: Item {}
					contentItem: Label {
						text: contextUrlButton.text
						textFormat: Text.PlainText
						color: Theme.accentHover
						font.pixelSize: Theme.fontCaption
						wrapMode: Text.WrapAnywhere
					}
					onClicked: root.openRequested(String(contextCard.modelData.url || ""))
				}
			}

			function roleLabel() {
				switch (String(modelData.role || "")) {
				case "quote": return qsTr("Quoted post")
				case "reply-context": return qsTr("Replying to")
				default: return qsTr("Context")
				}
			}

			function authorLine() {
				const name = String(modelData.authorName || "")
				const handle = String(modelData.authorHandle || "")
				const published = root.displayDate(modelData.publishedAt)
				return [name, handle, published].filter(function(value) {
					return value.length > 0
				}).join(" · ")
			}
		}
	}

	function displayDate(value) {
		const raw = String(value || "")
		if (raw.length === 0)
			return ""
		const parsed = new Date(raw)
		if (isNaN(parsed.getTime()))
			return raw
		return Qt.formatDateTime(parsed, "d MMM yyyy · HH:mm")
	}
}
