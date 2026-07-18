import QtQuick
import Mumble.Theme 1.0

Item {
    id: root
	objectName: "richMessageBody"

    required property var segments
    property color textColor: Theme.textMain
	property int pixelSize: 12
	property real lineHeight: Theme.chatBodyLineHeight
	property int textHorizontalAlignment: Text.AlignLeft
	property real maximumImageHeight: 320
	property real maximumImageWidth: 1000000
	property real imagePadding: Theme.space2
	property color imageSurfaceColor: Theme.embedSurface
	property color imageBorderColor: Theme.embedBorder
	property real imageBorderWidth: 1
	// Preserve metadata-derived image geometry while pooled, but release the
	// decoded image/texture until the delegate becomes active again.
	property bool resourceActive: true
	// Modal parents keep the product surface visible behind drawers/dialogs, but
	// their promoted semantic children must leave the accessibility tree.
	property bool accessibilitySuppressed: false
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
	readonly property var keyboardLinkEntries: root.linkEntriesForSegments(root.segments)
	readonly property var keyboardLinks: root.urlsForLinkEntries(root.keyboardLinkEntries)
	readonly property var currentKeyboardLinkEntry: keyboardLinkEntries.length > 0
		? keyboardLinkEntries[Math.max(0, Math.min(keyboardLinkIndex, keyboardLinkEntries.length - 1))]
		: ({})
	property int keyboardLinkIndex: 0

    signal linkRequested(string url)

	implicitWidth: body.implicitWidth
	implicitHeight: root.hasImages ? structuredBody.implicitHeight : body.implicitHeight
	Accessible.ignored: true
	onSegmentsChanged: {
		keyboardLinkIndex = Math.min(keyboardLinkIndex, Math.max(0, keyboardLinks.length - 1))
		if (keyboardLinks.length === 0 && plainLinkTarget.activeFocus)
			plainLinkTarget.focus = false
	}

	function linkEntriesForSegments(values) {
		const result = []
		for (const segment of (values || [])) {
			if (!segment || segment.text === undefined)
				continue
			const href = root.safeExternalUrl(segment ? segment.href : "")
			if (href.length === 0)
				continue
			let duplicate = false
			for (const entry of result) {
				if (entry.url === href) {
					duplicate = true
					break
				}
			}
			if (duplicate)
				continue
			const visibleLabel = String(segment.text || "").replace(/\s+/g, " ").trim().slice(0, 512)
			result.push({ "url": href, "label": visibleLabel.length > 0 ? visibleLabel : href })
		}
		return result
	}

	function urlsForLinkEntries(entries) {
		const result = []
		for (const entry of (entries || []))
			result.push(String(entry.url || ""))
		return result
	}

	function linksForSegments(values) {
		return urlsForLinkEntries(linkEntriesForSegments(values))
	}

	function activateKeyboardLink() {
		if (keyboardLinks.length === 0)
			return
		const index = Math.max(0, Math.min(keyboardLinkIndex, keyboardLinks.length - 1))
		root.linkRequested(keyboardLinks[index])
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

	function accessibleText(value) {
		return String(value === undefined || value === null ? "" : value)
			.replace(/\s+/g, " ").trim().slice(0, 4096)
	}

	function accessibleLinkDescription(entries, currentIndex) {
		if (!entries || entries.length === 0)
			return ""
		const index = Math.max(0, Math.min(Number(currentIndex || 0), entries.length - 1))
		const entry = entries[index] || ({})
		const label = accessibleText(entry.label || entry.url || "")
		const selection = entries.length > 1
			? qsTr("Link %1 of %2: %3.").arg(index + 1).arg(entries.length).arg(label)
			: qsTr("Link: %1.").arg(label)
		return entries.length > 1
			? selection + " " + qsTr("Press Enter to open it. Use Left and Right to choose a link.")
			: selection + " " + qsTr("Press Enter to open it.")
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
				"plainText": root.plainTextForSegments(textSegments),
				"links": root.linkEntriesForSegments(textSegments)
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
		lineHeightMode: Text.ProportionalHeight
		lineHeight: root.lineHeight
        linkColor: Theme.accent
		visible: !root.hasImages
		Accessible.ignored: true
        onLinkActivated: link => {
            const safeLink = root.safeExternalUrl(link)
            if (safeLink.length > 0)
                root.linkRequested(safeLink)
        }
    }

	Item {
		id: plainLinkTarget
		objectName: "richMessageBodyLinkTarget"
		anchors.fill: body
		visible: !root.hasImages
		// Keep the tab-stop property true while focus is being transferred away.
		// QQuickItem rejects changing activeFocusOnTab on the active focus item.
		activeFocusOnTab: activeFocus || (visible && !root.accessibilitySuppressed
			&& root.keyboardLinks.length > 0)
		onVisibleChanged: if (!visible && activeFocus) focus = false
		Accessible.ignored: root.accessibilitySuppressed
		Accessible.role: root.keyboardLinks.length > 0 ? Accessible.Link : Accessible.StaticText
		// A message containing a link is still a message. Announce the complete
		// visible text as its name and keep the currently selected link in the
		// action description so surrounding context is never lost.
		Accessible.name: root.accessibleText(root.plainText)
			|| String(root.currentKeyboardLinkEntry.label || root.currentKeyboardLinkEntry.url || "")
		Accessible.description: root.accessibleLinkDescription(
			root.keyboardLinkEntries, root.keyboardLinkIndex)
		Accessible.onPressAction: root.activateKeyboardLink()
		Keys.onPressed: event => {
			if (root.keyboardLinks.length === 0)
				return
			if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
				root.activateKeyboardLink()
				event.accepted = true
			} else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
				root.keyboardLinkIndex = (root.keyboardLinkIndex + root.keyboardLinks.length - 1)
					% root.keyboardLinks.length
				event.accepted = true
			} else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
				root.keyboardLinkIndex = (root.keyboardLinkIndex + 1) % root.keyboardLinks.length
				event.accepted = true
			}
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
					property int keyboardLinkIndex: 0
					readonly property var keyboardLinkEntries: parent.imageBlock
						? [] : (parent.modelData.links || [])
					readonly property var currentKeyboardLinkEntry: keyboardLinkEntries.length > 0
						? keyboardLinkEntries[Math.max(0,
							Math.min(keyboardLinkIndex, keyboardLinkEntries.length - 1))] : ({})
					visible: !parent.imageBlock
					text: parent.imageBlock ? "" : String(parent.modelData.html || "")
					textFormat: Text.RichText
					wrapMode: Text.Wrap
					horizontalAlignment: root.textHorizontalAlignment
					color: root.textColor
					font.pixelSize: root.pixelSize
					lineHeightMode: Text.ProportionalHeight
					lineHeight: root.lineHeight
					linkColor: Theme.accent
					activeFocusOnTab: activeFocus || (visible && !root.accessibilitySuppressed
						&& keyboardLinkEntries.length > 0)
					onVisibleChanged: if (!visible && activeFocus) focus = false
					Accessible.ignored: parent.imageBlock || root.accessibilitySuppressed
					Accessible.role: keyboardLinkEntries.length > 0
						? Accessible.Link : Accessible.StaticText
					Accessible.name: root.accessibleText(parent.modelData.plainText)
						|| String(currentKeyboardLinkEntry.label || currentKeyboardLinkEntry.url || "")
					Accessible.description: root.accessibleLinkDescription(
						keyboardLinkEntries, keyboardLinkIndex)
					Accessible.onPressAction: {
						if (keyboardLinkEntries.length > 0)
							root.linkRequested(String(currentKeyboardLinkEntry.url || ""))
					}
					onKeyboardLinkEntriesChanged: {
						keyboardLinkIndex = Math.min(keyboardLinkIndex,
							Math.max(0, keyboardLinkEntries.length - 1))
						if (keyboardLinkEntries.length === 0 && activeFocus)
							focus = false
					}
					onLinkActivated: link => {
						const safeLink = root.safeExternalUrl(link)
						if (safeLink.length > 0)
							root.linkRequested(safeLink)
					}
					Keys.onPressed: event => {
						if (keyboardLinkEntries.length === 0)
							return
						if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
								|| event.key === Qt.Key_Space) {
							root.linkRequested(String(currentKeyboardLinkEntry.url || ""))
							event.accepted = true
						} else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
							keyboardLinkIndex = (keyboardLinkIndex + keyboardLinkEntries.length - 1)
								% keyboardLinkEntries.length
							event.accepted = true
						} else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
							keyboardLinkIndex = (keyboardLinkIndex + 1) % keyboardLinkEntries.length
							event.accepted = true
						}
					}
				}

				Rectangle {
					objectName: "richMessageTextBlockFocus_" + parent.index
					anchors.fill: textBlock
					anchors.margins: -3
					visible: textBlock.visible && textBlock.activeFocus
					color: "transparent"
					border.color: Theme.focus
					border.width: Theme.focusRingWidth
					radius: Theme.innerRadius
					Accessible.ignored: true
				}

				Rectangle {
					id: imageCard
					objectName: "richMessageImageCard_" + index
					readonly property string safeHref: root.safeExternalUrl(parent.modelData.href)
					readonly property string accessibleLabel: String(
						parent.modelData.alt || qsTr("Server image")).trim().slice(0, 512)
					readonly property string imageStateDescription: inlineImage.status === Image.Loading
						? qsTr("Image loading") : inlineImage.status === Image.Error
							? qsTr("Image unavailable") : ""
					readonly property string interactionDescription: safeHref.length > 0
						? qsTr("Press Enter to open this image link.") : ""
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
					activeFocusOnTab: activeFocus || (!root.accessibilitySuppressed && safeHref.length > 0)
					onSafeHrefChanged: if (safeHref.length === 0 && activeFocus) focus = false
					Accessible.ignored: root.accessibilitySuppressed
					Accessible.role: safeHref.length > 0 ? Accessible.Link : Accessible.Graphic
					Accessible.name: accessibleLabel
					Accessible.focused: activeFocus
					Accessible.description: [imageStateDescription, interactionDescription]
						.filter(function(value) { return value.length > 0 }).join(". ")
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
						source: root.resourceActive && parent.parent.imageBlock
							? String(parent.parent.modelData.source || "") : ""
						asynchronous: true
						cache: false
						smooth: true
						fillMode: Image.PreserveAspectFit
						Accessible.ignored: true
					}

					ModernBusyIndicator {
						anchors.centerIn: parent
						visible: inlineImage.status === Image.Loading
						running: visible
						Accessible.ignored: root.accessibilitySuppressed
						Accessible.name: qsTr("Loading server image")
					}

					Text {
						objectName: "richMessageImageError_" + index
						anchors.centerIn: parent
						visible: inlineImage.status === Image.Error
						text: qsTr("Image unavailable")
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						Accessible.ignored: true
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
						Accessible.ignored: true
						onClicked: root.linkRequested(imageCard.safeHref)
					}
				}
			}
		}
	}

	Rectangle {
		objectName: "richMessageBodyFocusRing"
		anchors.fill: parent
		anchors.margins: -3
		visible: plainLinkTarget.visible && plainLinkTarget.activeFocus
		color: "transparent"
		border.color: Theme.focus
		border.width: Theme.focusRingWidth
		radius: Theme.innerRadius
	}
}
