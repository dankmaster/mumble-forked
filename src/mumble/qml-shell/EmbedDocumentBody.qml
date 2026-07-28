pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var document: ({})
	property color accent: Theme.accent
	property bool expanded: false
	readonly property var provider: document && document.provider ? document.provider : ({})
	readonly property var content: document && document.content ? document.content : ({})
	readonly property var author: content && content.author ? content.author : ({})
	readonly property var facts: document && document.facts
		&& document.facts.length !== undefined ? document.facts : []
	readonly property string contentType: String(content.type || "")
	readonly property bool socialPresentation: document && document.presentation === "social"
	readonly property bool marketplacePresentation: document
		&& document.presentation === "marketplace"
	signal externalOpenRequested(string url)

	spacing: Theme.space3

	RowLayout {
		objectName: "embedDocumentAuthorRow"
		Layout.fillWidth: true
		visible: root.authorLine().length > 0
			|| String(root.content.publishedAt || "").length > 0
		spacing: Theme.space2

		Rectangle {
			Layout.preferredWidth: Theme.avatarSmall
			Layout.preferredHeight: Theme.avatarSmall
			visible: root.authorLine().length > 0
			radius: width / 2
			color: Theme.embedSurface
			border.color: Theme.embedBorder
			clip: true

			Image {
				id: authorAvatar
				anchors.fill: parent
				source: String(root.author.avatarUrl || "")
				asynchronous: true
				cache: false
				fillMode: Image.PreserveAspectCrop
			}

			Label {
				anchors.centerIn: parent
				visible: authorAvatar.status !== Image.Ready
				text: root.authorInitial()
				textFormat: Text.PlainText
				color: root.accent
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				Accessible.ignored: true
			}
		}

		ColumnLayout {
			Layout.fillWidth: true
			spacing: 0

			Label {
				Layout.fillWidth: true
				visible: root.authorLine().length > 0
				text: root.authorLine()
				textFormat: Text.PlainText
				color: Theme.textStrong
				font.pixelSize: Theme.fontLabel
				font.weight: Font.DemiBold
				wrapMode: Text.Wrap
			}

			Label {
				Layout.fillWidth: true
				visible: String(root.content.publishedAt || "").length > 0
				text: root.displayDate(root.content.publishedAt)
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
			}
		}
	}

	Label {
		objectName: "embedDocumentTitle"
		Layout.fillWidth: true
		visible: String(root.content.title || "").length > 0
		text: String(root.content.title || "")
		textFormat: Text.PlainText
		color: Theme.textStrong
		font.pixelSize: root.socialPresentation || root.marketplacePresentation
			? Theme.fontLabel : Theme.fontTitle
		font.weight: Font.DemiBold
		lineHeight: 1.15
		wrapMode: Text.Wrap
		maximumLineCount: root.expanded ? 8 : 3
		elide: root.expanded ? Text.ElideNone : Text.ElideRight
		Accessible.role: Accessible.Heading
	}

	Label {
		objectName: "embedDocumentDescription"
		Layout.fillWidth: true
		visible: String(root.content.description || "").length > 0
		text: String(root.content.description || "")
		textFormat: Text.PlainText
		color: Theme.textMain
		font.pixelSize: Theme.fontLabel
		lineHeight: 1.3
		wrapMode: Text.Wrap
		maximumLineCount: root.expanded ? 16 : (root.socialPresentation ? 8 : 4)
		elide: root.expanded ? Text.ElideNone : Text.ElideRight
	}

	EmbedThreadContext {
		Layout.fillWidth: true
		thread: root.document && root.document.thread ? root.document.thread : ({})
		accent: root.accent
		onOpenRequested: function(url) { root.externalOpenRequested(url) }
	}

	GridLayout {
		Layout.fillWidth: true
		visible: root.facts.length > 0
		columns: width >= 480 ? 3 : 2
		columnSpacing: Theme.space2
		rowSpacing: Theme.space2

		Repeater {
			model: root.facts

			delegate: Rectangle {
				id: factCard
				required property var modelData
				required property int index
				objectName: "embedDocumentFact_" + index

				Layout.fillWidth: true
				implicitHeight: factLayout.implicitHeight + Theme.space2 * 2
				radius: Theme.innerRadius
				color: Theme.embedSurface
				border.color: Theme.embedBorder

				ColumnLayout {
					id: factLayout
					anchors.fill: parent
					anchors.margins: Theme.space2
					spacing: 0

					Label {
						objectName: "embedDocumentFactLabel_" + factCard.index
						Layout.fillWidth: true
						text: String(factCard.modelData.label || "")
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
					}
					Label {
						objectName: "embedDocumentFactValue_" + factCard.index
						Layout.fillWidth: true
						text: String(factCard.modelData.value === undefined
							|| factCard.modelData.value === null ? "" : factCard.modelData.value)
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
						wrapMode: Text.Wrap
						maximumLineCount: 2
						elide: Text.ElideRight
					}
				}
			}
		}
	}

	function authorLine() {
		const name = String(author.name || "")
		const handle = String(author.handle || "")
		if (name.length > 0 && handle.length > 0 && name !== handle)
			return name + " · " + handle
		return name || handle
	}

	function authorInitial() {
		const line = authorLine().replace(/^@/, "")
		return line.length > 0 ? line.slice(0, 1).toUpperCase() : "?"
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
