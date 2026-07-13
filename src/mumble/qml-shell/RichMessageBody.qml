import QtQuick
import Mumble.Theme 1.0

Item {
    id: root

    required property var segments
    property color textColor: Theme.textMain
    property int pixelSize: 12
    readonly property string plainText: root.plainTextForSegments(root.segments)
    readonly property string renderedHtml: root.htmlForSegments(root.segments)
	readonly property var keyboardLinks: root.linksForSegments(root.segments)
	property int keyboardLinkIndex: 0

    signal linkRequested(string url)

    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight
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

    function plainTextForSegments(values) {
        let result = ""
        for (const segment of (values || []))
            result += String(segment && segment.text !== undefined ? segment.text : "")
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

    Text {
        id: body
        objectName: "richMessageBodyText"
        width: root.width
        text: root.renderedHtml
        textFormat: Text.RichText
        wrapMode: Text.Wrap
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
		anchors.fill: body
		anchors.margins: -3
		visible: root.activeFocus && root.keyboardLinks.length > 0
		color: "transparent"
		border.color: Theme.focus
		border.width: 2
		radius: 4
	}
}
