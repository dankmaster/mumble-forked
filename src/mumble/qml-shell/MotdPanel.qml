import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    property var session: null
    property real maximumBodyHeight: 260
    signal actionRequested(string actionId, var payload)
    signal linkRequested(url link)

    readonly property bool hasContent: !!session && !!session.hasMotd
    readonly property bool surfaceVisible: hasContent
    readonly property bool dismissed: !!session && !!session.motdDismissed
    readonly property bool expanded: !!session && !!session.motdExpanded
    readonly property bool contentVisible: hasContent && !dismissed
    readonly property string signature: session ? String(session.motdSignature || "") : ""
    readonly property var actions: session && session.motdActions ? session.motdActions : []
    readonly property bool compactLayout: width < 560
    readonly property bool actionsWrapped: actionFlow.implicitHeight > Theme.controlHeight + 1
	readonly property string summary: cleanSummary(session ? session.motdSummary : "")

    objectName: "motdPanel"
    visible: surfaceVisible
    implicitHeight: visible ? panelLayout.implicitHeight + Theme.space3 * 2 : 0
    color: Theme.panel
    radius: Theme.innerRadius
    border.width: 1
    border.color: session && session.motdChanged ? Theme.accent : Theme.divider
	// This is an informational container. Its real actions participate in the
	// tab chain; the panel itself must never become an inert stop.
    activeFocusOnTab: false

    Accessible.role: Accessible.Pane
    Accessible.name: dismissed ? qsTr("Welcome message hidden") : qsTr("Server message of the day")
    Accessible.description: summary

	function cleanSummary(value) {
		// QTextDocument uses U+FFFC as a placeholder for embedded objects. It is
		// useful while parsing rich content, but must never leak into visible or
		// accessible plain-text summaries.
		return String(value === undefined || value === null ? "" : value)
			.replace(/\uFFFC/g, " ").replace(/\s+/g, " ").trim()
	}

    function payloadFor(action) {
        const supplied = action && action.payload ? action.payload : null
        if (supplied)
            return supplied
        return signature.length > 0 ? { "signature": signature } : {}
    }

    function dispatch(actionId, payload) {
        const normalized = String(actionId || "").trim()
        if (normalized.length > 0)
            actionRequested(normalized, payload || {})
    }

    function safeExternalUrl(value) {
        const url = String(value === undefined || value === null ? "" : value).trim()
        return /^(https?:\/\/|mailto:|mumble:\/\/)/i.test(url) ? url : ""
    }

    function focusPrimaryAction() {
        if (actionRepeater.count > 0) {
            const button = actionRepeater.itemAt(0)
            if (button) {
                button.forceActiveFocus()
                return true
            }
        }
        return false
    }

    function runProbe(action, requestedSignature) {
        const normalized = String(action || "").trim().toLowerCase()
        const resolvedSignature = String(requestedSignature || signature || "").trim()
        if (!hasContent)
            return { "handled": false, "action": normalized, "expanded": false,
                     "visible": false, "dismissedSignature": "", "reason": "missing-motd" }

        let actionId = ""
        let targetExpanded = expanded
        let targetVisible = contentVisible
        let targetDismissedSignature = dismissed ? signature : ""
        if (normalized === "collapse") {
            actionId = "motd.hide"
            targetExpanded = false
            targetVisible = true
            targetDismissedSignature = ""
        } else if (normalized === "expand" || normalized === "show") {
            actionId = "motd.show"
            targetExpanded = true
            targetVisible = true
            targetDismissedSignature = ""
        } else if (normalized === "dismiss") {
            if (resolvedSignature.length === 0)
                return { "handled": false, "action": normalized, "expanded": expanded,
                         "visible": contentVisible, "dismissedSignature": "", "reason": "missing-signature" }
            actionId = "motd.dismiss"
            targetVisible = false
            targetDismissedSignature = resolvedSignature
        } else if (normalized === "restore") {
            actionId = "motd.restore"
            targetVisible = true
            targetDismissedSignature = ""
        } else {
            return { "handled": false, "action": normalized, "expanded": expanded,
                     "visible": contentVisible, "dismissedSignature": dismissed ? signature : "",
                     "reason": "unknown-action" }
        }

        dispatch(actionId, resolvedSignature.length > 0 ? { "signature": resolvedSignature } : {})
        return { "handled": true, "action": normalized, "actionId": actionId,
                 "expanded": targetExpanded, "visible": targetVisible,
                 "dismissedSignature": targetDismissedSignature }
    }

    GridLayout {
        id: panelLayout
        anchors.fill: parent
        anchors.margins: Theme.space3
		columns: root.compactLayout || root.expanded ? 1 : 2
		columnSpacing: Theme.space2
		rowSpacing: Theme.space2

        RowLayout {
			Layout.row: 0
			Layout.column: 0
			Layout.columnSpan: root.expanded ? panelLayout.columns : 1
            Layout.fillWidth: true
			Layout.minimumWidth: 0
            spacing: Theme.space2

            Label {
				textFormat: Text.PlainText
				text: root.dismissed ? qsTr("Welcome hidden")
					: root.expanded ? qsTr("Server message of the day") : qsTr("Welcome")
                color: Theme.textStrong
                font.bold: true
				font.pixelSize: Theme.fontLabel
                elide: Text.ElideRight
            }

			Label {
				objectName: "motdSummaryBody"
				Layout.fillWidth: true
				Layout.minimumWidth: 0
				visible: root.contentVisible && !root.expanded
				textFormat: Text.PlainText
				text: root.summary
				color: Theme.textMain
				font.pixelSize: Theme.fontBody
				maximumLineCount: 1
				elide: Text.ElideRight
				Accessible.role: Accessible.StaticText
				Accessible.name: text.length > 0 ? text : qsTr("Server message")
			}

            Label {
				textFormat: Text.PlainText
                visible: !!root.session && !!root.session.motdChanged
                text: qsTr("New")
                color: Theme.accent
                font.bold: true
                Accessible.name: qsTr("The welcome message has changed")
            }
        }

        ScrollView {
            id: motdScroll
			Layout.row: 1
			Layout.column: 0
			Layout.columnSpan: panelLayout.columns
            Layout.fillWidth: true
			Layout.preferredHeight: visible
				? Math.min(structuredBody.implicitHeight, root.maximumBodyHeight) : 0
			visible: root.contentVisible && root.expanded
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
			ScrollBar.vertical.policy: structuredBody.implicitHeight > root.maximumBodyHeight
                                         ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

			RichMessageBody {
				id: structuredBody
				objectName: "motdStructuredBody"
				width: Math.max(1, motdScroll.availableWidth)
				segments: root.session && root.session.motdSegments
					? root.session.motdSegments : []
				textColor: Theme.textMain
				onLinkRequested: function(link) {
					const safeLink = root.safeExternalUrl(link)
					if (safeLink.length > 0)
						root.linkRequested(safeLink)
				}
			}
        }

		// Keep actions after the summary/body in both visual and accessibility
		// order. On a normal desktop width the collapsed surface remains a single
		// compact row; narrow layouts wrap the actions below the summary.
        Flow {
            id: actionFlow
            objectName: "motdActionFlow"
			Layout.row: root.expanded ? 2 : root.compactLayout ? 1 : 0
			Layout.column: root.expanded || root.compactLayout ? 0 : 1
			Layout.columnSpan: root.expanded || root.compactLayout ? panelLayout.columns : 1
			Layout.fillWidth: root.expanded || root.compactLayout
            Layout.minimumWidth: 0
			Layout.preferredWidth: root.expanded || root.compactLayout
				? panelLayout.width : Math.floor(panelLayout.width * 0.42)
			Layout.maximumWidth: panelLayout.width
            Layout.preferredHeight: visible ? implicitHeight : 0
			Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            visible: root.actions.length > 0
            spacing: Theme.space2

            Repeater {
                id: actionRepeater
                model: root.actions

                delegate: ModernButton {
                    required property var modelData
                    readonly property string fullLabel: String(modelData.label
                        || modelData.title || qsTr("Open"))
                    objectName: "motdAction_" + String(modelData.id || "")
                    width: Math.max(1, Math.min(implicitWidth, actionFlow.width))
                    dense: !root.expanded || root.compactLayout
                    text: fullLabel
                    enabled: modelData.enabled === undefined || !!modelData.enabled
                    Accessible.name: fullLabel
                    onClicked: root.dispatch(String(modelData.id || ""), root.payloadFor(modelData))
                }
            }
        }
    }
}
