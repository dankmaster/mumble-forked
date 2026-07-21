import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
	id: root
	property var activeReactions: []
	property bool expanded: true
	readonly property var options: [
		{ "emoji": "👍", "label": qsTr("Thumbs up") },
		{ "emoji": "❤️", "label": qsTr("Love") },
		{ "emoji": "😂", "label": qsTr("Laugh") },
		{ "emoji": "😮", "label": qsTr("Surprised") },
		{ "emoji": "😢", "label": qsTr("Sad") },
		{ "emoji": "🎉", "label": qsTr("Celebrate") },
		{ "emoji": "👀", "label": qsTr("Eyes") },
		{ "emoji": "🔥", "label": qsTr("Fire") }
	]
	readonly property int optionCount: reactionRepeater.count
	signal reactionRequested(string emoji)

	implicitWidth: reactionRow.implicitWidth
	implicitHeight: reactionRow.implicitHeight
	visible: expanded
	Accessible.role: Accessible.Grouping
	Accessible.name: qsTr("Quick reactions")

	function selfReacted(emoji) {
		const wanted = String(emoji || "")
		for (let index = 0; index < activeReactions.length; ++index) {
			const reaction = activeReactions[index] || ({})
			if (String(reaction.emoji || "") === wanted)
				return !!reaction.selfReacted
		}
		return false
	}

	function optionAt(index) {
		return reactionRepeater.itemAt(index)
	}

	Row {
		id: reactionRow
		spacing: Theme.space1

		Repeater {
			id: reactionRepeater
			model: root.options
			delegate: Button {
				id: reactionOption
				required property var modelData
				required property int index
				readonly property bool reacted: root.selfReacted(modelData.emoji)
				objectName: "quickReactionOption-" + index
				implicitWidth: 38
				implicitHeight: 32
				hoverEnabled: true
				activeFocusOnTab: true
				focusPolicy: Qt.StrongFocus
				Accessible.name: modelData.label
				Accessible.description: reacted
					? qsTr("Remove %1 reaction").arg(modelData.label)
					: qsTr("Add %1 reaction").arg(modelData.label)
				ToolTip.visible: hovered
				ToolTip.text: Accessible.description

				background: Rectangle {
					radius: reactionOption.implicitHeight / 2
					color: reactionOption.reacted ? Theme.selected
						: reactionOption.down ? Theme.accentSubtle
						: reactionOption.hovered ? Theme.surfaceHover : Theme.strip
					border.color: reactionOption.activeFocus ? Theme.focus
						: reactionOption.reacted ? Theme.accent : Theme.divider
					border.width: reactionOption.activeFocus ? Theme.focusRingWidth : 1
				}

				contentItem: Label {
					textFormat: Text.PlainText
					text: reactionOption.modelData.emoji
					color: Theme.textStrong
					font.family: Qt.platform.os === "windows" ? "Segoe UI Emoji" : ""
					font.pixelSize: 19
					horizontalAlignment: Text.AlignHCenter
					verticalAlignment: Text.AlignVCenter
					Accessible.ignored: true
				}

				onClicked: root.reactionRequested(String(modelData.emoji || ""))
			}
		}
	}
}
