import QtQuick
import Mumble.Theme 1.0

Item {
    id: root

    required property var segments
    property color textColor: Theme.textMain
    property int pixelSize: 12
	property int textHorizontalAlignment: Text.AlignLeft
	property real maximumImageHeight: 320
	property real maximumImageWidth: 1000000
	property real imagePadding: Theme.space2
	property color imageSurfaceColor: Theme.strip
	property color imageBorderColor: Theme.divider
	property real imageBorderWidth: 1
    readonly property string plainText: root.plainTextForSegments(root.segments)
    readonly property string renderedHtml: root.htmlForSegments(root.segments)
	readonly property var renderBlocks: root.blocksForSegments(root.segments)
	readonly property bool hasImages: root.renderBlocks.some(function(block) {
		return block.kind === "image"
	})
	readonly property bool plainTextOnly: !root.hasImages && !(root.segments || []).some(function(segment) {
		return !!segment && (!!segment.bold || !!segment.italic || !!segment.strike
			|| !!segment.code || root.safeExternalUrl(segment.href).length > 0)
	})
	readonly property var keyboardLinks: root.linksForSegments(root.segments)
	property int keyboardLinkIndex: 0

    signal linkRequested(string url)

	implicitWidth: body.implicitWidth
	implicitHeight: root.hasImages ? structuredBody.implicitHeight : body.implicitHeight
	activeFocusOnTab: keyboardLinks.length > 0
	Accessible.role: keyboardLinks.length > 0 ? Accessible.Link : Accessible.StaticText
	Accessible.name: plainText
	Accessible.description: keyboardLinks.length > 1
		? qsTr("Press Enter to open the selected link. Use Left and Right to choose a link.")
		: (keyboardLinks.length > 0 ? qsTr("Press Enter to open the link.") : "")
	Accessible.onPressAction: root.activateKeyboardLink()
	onSegmentsChanged: keyboardLinkIndex = Math.min(keyboardLinkIndex, Math.max(0, keyboardLinks.length - 1))

	function linksForSegments(values) {
		const result = []
		for (const segment of (values || [])) {
			const href = root.safeExternalUrl(segment ? segment.href : "")
			if (href.length > 0 && result.indexOf(href) < 0)
				result.push(href)
		}
		return result
	}

	function activateKeyboardLink() {
		if (keyboardLinks.length === 0)
			return
		const index = Math.max(0, Math.min(keyboardLinkIndex, keyboardLinks.length - 1))
		root.linkRequested(keyboardLinks[index])
	}

	Keys.onPressed: event => {
		if (keyboardLinks.length === 0)
			return
		if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
			activateKeyboardLink()
			event.accepted = true
		} else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
			keyboardLinkIndex = (keyboardLinkIndex + keyboardLinks.length - 1) % keyboardLinks.length
			event.accepted = true
		} else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
			keyboardLinkIndex = (keyboardLinkIndex + 1) % keyboardLinks.length
			event.accepted = true
		}
	}

    function escapeHtml(value) {
        return String(value === undefined || value === null ? "" : value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/\"/g, "&quot;")
            .replace(/'/g, "&#39;")
    }

    function safeExternalUrl(value) {
        const url = String(value === undefined || value === null ? "" : value).trim()
        return /^(https?:\/\/|mailto:|mumble:\/\/)/i.test(url) ? url : ""
    }

	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}

	function plainTextForSegments(values) {
		let result = ""
		for (const segment of (values || [])) {
			if (segment && segment.text !== undefined) {
				result += String(segment.text)
			} else if (segment && segment.kind === "image") {
				const alt = String(segment.alt || qsTr("Server image")).trim().slice(0, 512)
				if (alt.length > 0)
					result += (result.length > 0 && !/[\r\n]$/.test(result) ? "\n" : "") + alt
			}
		}
		return result
	}

    function htmlForSegments(values) {
        let result = ""
        for (const segment of (values || [])) {
            if (!segment || segment.text === undefined)
                continue
            let part = root.escapeHtml(segment.text).replace(/\r\n|\r|\n/g, "<br/>")
            if (segment.code)
                part = "<code>" + part + "</code>"
            if (segment.bold)
                part = "<b>" + part + "</b>"
            if (segment.italic)
                part = "<i>" + part + "</i>"
            if (segment.strike)
                part = "<s>" + part + "</s>"
            const href = root.safeExternalUrl(segment.href)
            if (href.length > 0)
                part = "<a href=\"" + root.escapeHtml(href) + "\">" + part + "</a>"
            result += part
        }
        return result
    }

	function blocksForSegments(values) {
		const blocks = []
		let textSegments = []
		function flushText() {
			if (textSegments.length === 0)
				return
			blocks.push({
				"kind": "text",
				"html": root.htmlForSegments(textSegments),
				"plainText": root.plainTextForSegments(textSegments)
			})
			textSegments = []
		}

		for (const segment of (values || [])) {
			const source = segment && segment.kind === "image"
				? root.safeRenderImageSource(segment.source) : ""
			if (source.length > 0) {
				flushText()
				blocks.push({
					"kind": "image",
					"source": source,
					"alt": String(segment.alt || qsTr("Server image")),
					"width": Math.max(0, Number(segment.width || 0)),
					"height": Math.max(0, Number(segment.height || 0)),
					"href": root.safeExternalUrl(segment.href)
				})
			} else if (segment && segment.text !== undefined) {
				textSegments.push(segment)
			}
		}
		flushText()
		return blocks
	}

    Text {
        id: body
        objectName: "richMessageBodyText"
        width: root.width
		// Most chat lines are unformatted. Keeping them on the plain-text path
		// avoids reparsing generated HTML whenever a pooled delegate is reused.
		text: root.hasImages ? "" : (root.plainTextOnly ? root.plainText : root.renderedHtml)
		textFormat: root.plainTextOnly ? Text.PlainText : Text.RichText
        wrapMode: Text.Wrap
		horizontalAlignment: root.textHorizontalAlignment
        color: root.textColor
        font.pixelSize: root.pixelSize
        linkColor: Theme.accent
		visible: !root.hasImages
        onLinkActivated: link => {
            const safeLink = root.safeExternalUrl(link)
            if (safeLink.length > 0)
                root.linkRequested(safeLink)
        }
    }

	Column {
		id: structuredBody
		objectName: "richMessageStructuredBody"
		width: root.width
		visible: root.hasImages
		spacing: Theme.space2

		Repeater {
			model: root.renderBlocks

			delegate: Item {
				required property var modelData
				required property int index
				readonly property bool imageBlock: modelData.kind === "image"
				width: structuredBody.width
				height: imageBlock ? imageCard.implicitHeight : textBlock.implicitHeight

				Text {
					id: textBlock
					objectName: "richMessageTextBlock_" + index
					width: parent.width
					visible: !parent.imageBlock
					text: parent.imageBlock ? "" : String(parent.modelData.html || "")
					textFormat: Text.RichText
					wrapMode: Text.Wrap
					horizontalAlignment: root.textHorizontalAlignment
					color: root.textColor
					font.pixelSize: root.pixelSize
					linkColor: Theme.accent
					onLinkActivated: link => {
						const safeLink = root.safeExternalUrl(link)
						if (safeLink.length > 0)
							root.linkRequested(safeLink)
					}
				}

				Rectangle {
					id: imageCard
					objectName: "richMessageImageCard_" + index
					readonly property string safeHref: root.safeExternalUrl(parent.modelData.href)
					readonly property string accessibleLabel: String(
						parent.modelData.alt || qsTr("Server image")).trim().slice(0, 512)
					readonly property real imageMargin: Math.max(0, root.imagePadding)
					readonly property real availableImageWidth: Math.max(1,
						Math.min(parent.width - imageMargin * 2, root.maximumImageWidth))
					readonly property real naturalWidth: inlineImage.sourceSize.width > 0
						? inlineImage.sourceSize.width : Number(parent.modelData.width || availableImageWidth)
					readonly property real naturalHeight: inlineImage.sourceSize.height > 0
						? inlineImage.sourceSize.height : Number(parent.modelData.height || naturalWidth * 9 / 16)
					readonly property real fitScale: Math.max(0.001, Math.min(1,
						availableImageWidth / Math.max(1, naturalWidth),
						root.maximumImageHeight / Math.max(1, naturalHeight)))
					readonly property real displayWidth: Math.max(1, naturalWidth * fitScale)
					readonly property real displayHeight: Math.max(1, naturalHeight * fitScale)
					width: Math.min(parent.width, displayWidth + imageMargin * 2)
					anchors.horizontalCenter: parent.horizontalCenter
					implicitHeight: parent.imageBlock ? displayHeight + imageMargin * 2 : 0
					visible: parent.imageBlock
					color: root.imageSurfaceColor
					radius: Theme.innerRadius
					border.color: root.imageBorderColor
					border.width: root.imageBorderWidth
					clip: true
					activeFocusOnTab: safeHref.length > 0
					Accessible.role: safeHref.length > 0 ? Accessible.Link : Accessible.Graphic
					Accessible.name: accessibleLabel
					Accessible.focused: activeFocus
					Accessible.description: safeHref.length > 0
						? qsTr("Press Enter to open this image link.") : ""
					Accessible.onPressAction: {
						if (safeHref.length > 0)
							root.linkRequested(safeHref)
					}
					Keys.onPressed: event => {
						if (safeHref.length > 0 && (event.key === Qt.Key_Return
								|| event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
							root.linkRequested(safeHref)
							event.accepted = true
						}
					}
					Image {
						id: inlineImage
						objectName: "richMessageInlineImage_" + index
						anchors.centerIn: parent
						width: imageCard.displayWidth
						height: imageCard.displayHeight
						source: parent.parent.imageBlock ? String(parent.parent.modelData.source || "") : ""
						asynchronous: true
						cache: false
						smooth: true
						fillMode: Image.PreserveAspectFit
						Accessible.name: imageCard.accessibleLabel
					}

					ModernBusyIndicator {
						anchors.centerIn: parent
						visible: inlineImage.status === Image.Loading
						running: visible
						Accessible.name: qsTr("Loading server image")
					}

					Text {
						anchors.centerIn: parent
						visible: inlineImage.status === Image.Error
						text: qsTr("Image unavailable")
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
					}

					Rectangle {
						objectName: "richMessageImageState_" + index
						anchors.fill: parent
						anchors.margins: root.imageBorderWidth
						radius: Math.max(0, imageCard.radius - root.imageBorderWidth)
						color: imagePointer.pressed ? Theme.accentSubtle
							: imagePointer.containsMouse
								? Qt.rgba(Theme.surfaceHover.r, Theme.surfaceHover.g, Theme.surfaceHover.b, 0.32)
								: "transparent"
						border.color: imageCard.activeFocus ? Theme.focus : "transparent"
						border.width: imageCard.activeFocus ? Theme.focusRingWidth : 0
						Behavior on color { ColorAnimation { duration: Theme.motionFast } }
					}

					MouseArea {
						id: imagePointer
						anchors.fill: parent
						enabled: imageCard.safeHref.length > 0
						hoverEnabled: true
						acceptedButtons: Qt.LeftButton
						cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
						onClicked: root.linkRequested(imageCard.safeHref)
					}
				}
			}
		}
	}

	Rectangle {
		anchors.fill: parent
		anchors.margins: -3
		visible: root.activeFocus && root.keyboardLinks.length > 0
		color: "transparent"
		border.color: Theme.focus
		border.width: Theme.focusRingWidth
		radius: Theme.innerRadius
	}
}
