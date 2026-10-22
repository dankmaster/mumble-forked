import QtQuick
import Mumble.Theme 1.0

// Message body of the v2 timeline. Blocks arrive prepared from the row worker
// (paragraph, quote, heading, code); prose is plain text or StyledText unless a
// paragraph carries inline code, so laying a row out never parses HTML.
Column {
	id: root

	property var blocks: []
	property color textColor: Theme.textMain
	property int pixelSize: Theme.chatBodyFontSize

	signal linkActivated(string link)

	spacing: Theme.chatContentSpacing

	Repeater {
		model: root.blocks

		delegate: Item {
			id: block

			required property var modelData
			readonly property bool code: modelData.kind === "code"
			readonly property bool quote: modelData.kind === "quote"
			readonly property bool heading: modelData.kind === "h"
			readonly property real inset: code ? Theme.space2 : 0

			width: root.width
			height: Math.ceil(label.implicitHeight) + inset * 2

			Rectangle {
				visible: block.code || block.quote
				width: block.code ? parent.width : 3
				height: parent.height
				radius: block.code ? Theme.innerRadius : 1.5
				color: block.code ? Theme.strip : Theme.divider
				border.color: block.code ? Theme.divider : "transparent"
				border.width: block.code ? 1 : 0
			}

			Text {
				id: label

				x: block.code ? block.inset : block.quote ? Theme.space3 : 0
				y: block.inset
				width: parent.width - x - block.inset
				text: String(block.modelData.text || "")
				textFormat: block.modelData.format === "plain" ? Text.PlainText
					: block.modelData.format === "rich" ? Text.RichText : Text.StyledText
				wrapMode: block.code ? Text.WrapAnywhere : Text.Wrap
				color: block.quote ? Theme.textMuted : root.textColor
				linkColor: Theme.accent
				font.family: block.code ? (Qt.platform.os === "windows" ? "Consolas" : "monospace")
					: Qt.application.font.family
				font.pixelSize: block.code ? root.pixelSize - 1
					: block.heading ? root.pixelSize + Math.max(0, 8 - Number(block.modelData.level || 1) * 2)
					: root.pixelSize
				font.weight: block.heading ? Font.DemiBold : Font.Normal
				lineHeight: block.code ? 1.1 : Theme.chatBodyLineHeight
				onLinkActivated: link => root.linkActivated(link)

				HoverHandler {
					cursorShape: label.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
				}
			}
		}
	}
}
