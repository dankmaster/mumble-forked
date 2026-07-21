import QtQuick
import Mumble.Theme 1.0

Item {
	id: root

	required property var block
	property bool compactLayout: false
	property real maximumImageWidth: 640
	property real maximumImageHeight: compactLayout ? 120 : 180
	property bool animationsEnabled: true
	property bool hoverEffectsEnabled: true
	signal linkRequested(string url)

	readonly property string kind: String(block.kind || "paragraph")
	readonly property bool heading: kind === "heading"
	readonly property bool listItem: kind === "list-item"
	readonly property bool quote: kind === "quote"
	readonly property bool image: kind === "image"
	readonly property int headingLevel: Math.max(1, Math.min(6, Number(block.headingLevel || 2)))
	readonly property real leadingWidth: listItem ? 30 : quote ? 16 : 0
	readonly property int horizontalAlignment: String(block.alignment || "left") === "center"
		? Text.AlignHCenter : String(block.alignment || "left") === "right"
			? Text.AlignRight : Text.AlignLeft

	objectName: "motdDocumentBlock_" + kind
	implicitHeight: Math.max(content.implicitHeight, listMarker.implicitHeight)
	// RichMessageBody owns links and normal text semantics. Headings are promoted
	// here so assistive technology receives the document structure instead of a
	// bold-looking static text node.
	Accessible.ignored: !heading
	Accessible.role: Accessible.Heading
	Accessible.name: heading ? String(block.plainText || "") : ""

	Rectangle {
		anchors.fill: parent
		visible: root.quote
		color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07)
		radius: Theme.innerRadius
		Accessible.ignored: true
	}

	Rectangle {
		x: 0
		y: 1
		width: 3
		height: Math.max(0, parent.height - 2)
		visible: root.quote
		color: Theme.accent
		radius: 2
		Accessible.ignored: true
	}

	Text {
		id: listMarker
		objectName: root.listItem ? "motdListMarker" : ""
		x: 0
		width: 22
		visible: root.listItem
		text: String(root.block.marker || "\u2022")
		textFormat: Text.PlainText
		horizontalAlignment: Text.AlignRight
		color: Theme.accent
		font.pixelSize: Theme.fontBody
		font.weight: Font.Bold
		Accessible.ignored: true
	}

	RichMessageBody {
		id: content
		objectName: "motdBlockBody"
		x: root.leadingWidth
		y: root.quote ? Theme.space1 : 0
		width: Math.max(1, root.width - root.leadingWidth - (root.quote ? Theme.space2 : 0))
		segments: root.block.segments || []
		pixelSize: root.heading ? (root.headingLevel === 1 ? 22
			: root.headingLevel === 2 ? 19 : root.headingLevel === 3 ? 16 : 14)
			: Theme.fontBody
		lineHeight: root.heading ? 1.16 : Theme.chatBodyLineHeight
		textHorizontalAlignment: root.horizontalAlignment
		maximumImageWidth: root.maximumImageWidth
		maximumImageHeight: root.maximumImageHeight
		imagePadding: Theme.space2
		imageSurfaceColor: Theme.surfaceHover
		imageBorderColor: Theme.embedBorder
		imageBorderWidth: 1
		accessibilitySuppressed: root.heading
		animationsEnabled: root.animationsEnabled
		hoverEffectsEnabled: root.hoverEffectsEnabled
		onLinkRequested: function(url) { root.linkRequested(url) }
	}
}
