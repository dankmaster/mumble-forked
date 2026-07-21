import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root
	objectName: "motdEditor"

	property var field: ({})
	property var dialogHost: null
	property var dialogStateHost: null
	property string draftHtml: ""
	property string synchronizedHtml: ""
	property bool synchronizingEditor: false
	property string mode: canEdit ? "edit" : "preview"
	readonly property bool canEdit: field && field.canEdit !== undefined
		? Boolean(field.canEdit) : field && field.enabled !== undefined ? Boolean(field.enabled) : false
	readonly property bool showStandaloneSave: Boolean(field && field.showSaveAction)
	readonly property int maximumLength: Math.max(1024,
		Math.min(1000000, Number((field || {}).maximumLength || 100000)))
	readonly property bool overLimit: draftHtml.length > maximumLength
	readonly property bool dirty: draftHtml !== String((field || {}).originalValue ?? synchronizedHtml)
	readonly property string previewSourceHtml: String((field || {}).previewSourceHtml || "")
	readonly property var previewBlocks: (field || {}).previewBlocks || []
	readonly property string previewSummary: String((field || {}).previewSummary || "")
	readonly property bool previewReady: previewSourceHtml === draftHtml

	width: parent ? parent.width : 0
	spacing: Theme.space3
	Accessible.role: Accessible.Pane
	Accessible.name: field.label || qsTr("Message of the day")
	Accessible.description: canEdit
		? qsTr("Edit and preview the server message of the day")
		: qsTr("Read-only server message of the day")

	function boundedHtml(value) {
		return String(value === undefined || value === null ? "" : value).slice(0, maximumLength + 1)
	}

	function syncFromField() {
		const incoming = boundedHtml((field || {}).value)
		const changedExternally = incoming !== synchronizedHtml
		if (sourceEditor.activeFocus && !changedExternally)
			return
		synchronizedHtml = incoming
		if (changedExternally || !sourceEditor.activeFocus || draftHtml.length === 0) {
			draftHtml = incoming
			if (sourceEditor.text !== incoming) {
				synchronizingEditor = true
				sourceEditor.text = incoming
				synchronizingEditor = false
			}
		}
	}

	function publishDraft() {
		if (!canEdit || !dialogHost || overLimit)
			return
		synchronizedHtml = draftHtml
		dialogHost.updateFieldValue(String(field.id || "motd.html"), draftHtml)
	}

	function wrapSelection(openTag, closeTag, placeholder) {
		if (!canEdit)
			return
		const start = Math.max(0, sourceEditor.selectionStart)
		const end = Math.max(start, sourceEditor.selectionEnd)
		const selected = sourceEditor.text.slice(start, end)
		const body = selected.length > 0 ? selected : placeholder
		const insertion = openTag + body + closeTag
		sourceEditor.remove(start, end)
		sourceEditor.insert(start, insertion)
		sourceEditor.select(start + openTag.length,
			start + openTag.length + body.length)
		sourceEditor.forceActiveFocus(Qt.ShortcutFocusReason)
	}

	function insertImage() {
		if (!canEdit || !dialogHost)
			return
		publishTimer.stop()
		dialogHost.invokeNativePickerAction("motd.insertImage", {
			"fieldId": String(field.id || "motd.html"),
			"html": sourceEditor.text,
			"selectionStart": sourceEditor.selectionStart,
			"selectionEnd": sourceEditor.selectionEnd,
			"maximumLength": root.maximumLength
		}, "motdSourceEditor")
	}

	function requestStructuredPreview() {
		if (!dialogStateHost || overLimit || previewReady)
			return
		dialogStateHost.invokeAction("motd.preview", { "html": draftHtml })
	}

	onFieldChanged: {
		syncFromField()
		if (mode === "preview" && !previewReady)
			Qt.callLater(function() { root.requestStructuredPreview() })
	}
	Component.onCompleted: {
		syncFromField()
		if (mode === "preview")
			Qt.callLater(function() { root.requestStructuredPreview() })
	}

	Timer {
		id: publishTimer
		interval: 160
		repeat: false
		onTriggered: root.publishDraft()
	}

	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: accessRow.implicitHeight + Theme.space3 * 2
		radius: Theme.innerRadius
		color: root.canEdit ? Theme.accentSubtle : Theme.strip
		border.color: root.canEdit ? Theme.accent : Theme.divider

		RowLayout {
			id: accessRow
			anchors.fill: parent
			anchors.margins: Theme.space3
			spacing: Theme.space3

			ModernIcon {
				name: root.canEdit ? "edit" : "eye"
				color: root.canEdit ? Theme.accent : Theme.textMuted
				size: 20
			}
			ColumnLayout {
				Layout.fillWidth: true
				spacing: 1
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.canEdit ? qsTr("You can edit this server MOTD")
						: qsTr("Server MOTD — read only")
					color: Theme.textStrong
					font.weight: Font.DemiBold
				}
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.canEdit
						? qsTr("Changes are written through the standard Mumble server configuration API.")
						: qsTr("Root Write permission is required to publish changes.")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
				}
			}
		}
	}

	RowLayout {
		Layout.fillWidth: true
		spacing: Theme.space3

		ModernSegmentedControl {
			id: modeControl
			Layout.preferredWidth: 250
			model: root.canEdit
				? [{ "label": qsTr("Editor"), "value": "edit" },
				   { "label": qsTr("Preview"), "value": "preview" }]
				: [{ "label": qsTr("Preview"), "value": "preview" },
				   { "label": qsTr("Source"), "value": "edit" }]
			currentValue: root.mode
			accessibleName: qsTr("MOTD view")
			onActivated: function(index, value) {
				root.mode = String(value)
				if (root.mode === "preview") {
					publishTimer.stop()
					root.publishDraft()
					root.requestStructuredPreview()
				}
			}
		}
		Item { Layout.fillWidth: true }
		Label {
			textFormat: Text.PlainText
			text: qsTr("%1 / %2 characters").arg(root.draftHtml.length).arg(root.maximumLength)
			color: root.overLimit ? Theme.danger : Theme.textMuted
			font.pixelSize: Theme.fontCaption
		}
	}

	Flow {
		Layout.fillWidth: true
		visible: root.canEdit && root.mode === "edit"
		spacing: Theme.space2

		ModernButton { dense: true; text: qsTr("Bold"); onClicked: root.wrapSelection("<strong>", "</strong>", qsTr("bold text")) }
		ModernButton { dense: true; text: qsTr("Italic"); onClicked: root.wrapSelection("<em>", "</em>", qsTr("italic text")) }
		ModernButton { dense: true; text: qsTr("Heading"); onClicked: root.wrapSelection("<h2>", "</h2>", qsTr("Heading")) }
		ModernButton { dense: true; text: qsTr("Center"); onClicked: root.wrapSelection("<div style='text-align:center;'>", "</div>", qsTr("Centered content")) }
		ModernButton { dense: true; text: qsTr("List"); onClicked: root.wrapSelection("<ul><li>", "</li></ul>", qsTr("List item")) }
		ModernButton { dense: true; text: qsTr("Link"); onClicked: root.wrapSelection("<a href='https://example.com'>", "</a>", qsTr("link text")) }
		ModernButton { dense: true; text: qsTr("Image…"); onClicked: root.insertImage() }
	}

	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: 320
		Layout.minimumHeight: 240
		radius: Theme.innerRadius
		color: Theme.strip
		border.color: root.overLimit ? Theme.danger : Theme.divider
		clip: true

		ScrollView {
			anchors.fill: parent
			anchors.margins: 1
			visible: root.mode === "edit"
			clip: true

			TextArea {
				id: sourceEditor
				objectName: "motdSourceEditor"
				text: ""
				readOnly: !root.canEdit
				textFormat: TextEdit.PlainText
				wrapMode: TextEdit.WrapAnywhere
				selectByMouse: true
				color: root.canEdit ? Theme.textMain : Theme.textMuted
				selectionColor: Theme.accent
				selectedTextColor: Theme.contrastText(Theme.accent)
				font.family: "Consolas"
				font.pixelSize: 12
				background: Rectangle { color: Theme.strip }
				Accessible.name: root.canEdit ? qsTr("MOTD HTML editor") : qsTr("MOTD HTML source")
				onTextChanged: {
					if (root.synchronizingEditor)
						return
					if (root.draftHtml === text)
						return
					root.draftHtml = text
					if (root.canEdit)
						publishTimer.restart()
				}
			}
		}

		ScrollView {
			id: previewScroll
			objectName: "motdLivePreview"
			anchors.fill: parent
			visible: root.mode === "preview"
			clip: true

			Item {
				width: Math.max(1, previewScroll.availableWidth)
				implicitHeight: previewDocument.implicitHeight + Theme.space4 * 2

				MotdDocumentBody {
					id: previewDocument
					objectName: "motdPreviewDocumentBody"
					x: Theme.space4
					y: Theme.space4
					width: Math.max(1, parent.width - Theme.space4 * 2)
					blocks: root.previewReady ? root.previewBlocks : []
					fallbackText: root.previewReady ? root.previewSummary : qsTr("Preparing preview…")
					maximumImageWidth: 640
					maximumImageHeight: 180
					animationsEnabled: false
					hoverEffectsEnabled: false
					onLinkRequested: function(link) { Qt.openUrlExternally(link) }
					Accessible.role: Accessible.StaticText
					Accessible.name: qsTr("MOTD preview")
				}
			}
		}
	}

	Label {
		Layout.fillWidth: true
		visible: root.overLimit || String((root.field || {}).hint || "").length > 0
		textFormat: Text.PlainText
		text: root.overLimit
			? qsTr("The MOTD is too long to save.")
			: String((root.field || {}).hint || "")
		color: root.overLimit ? Theme.danger : Theme.textMuted
		font.pixelSize: Theme.fontCaption
		wrapMode: Text.Wrap
	}

	RowLayout {
		Layout.fillWidth: true
		visible: root.canEdit && root.showStandaloneSave
		Item { Layout.fillWidth: true }
		ModernButton {
			objectName: "motdSaveButton"
			text: qsTr("Save MOTD to server")
			tone: "accent"
			enabled: !root.overLimit && root.dirty
			onClicked: {
				publishTimer.stop()
				root.publishDraft()
				if (root.dialogStateHost)
					root.dialogStateHost.invokeAction("motd.save", { "html": root.draftHtml })
			}
		}
	}
}
