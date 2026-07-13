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

    objectName: "motdPanel"
    visible: surfaceVisible
    implicitHeight: visible ? panelLayout.implicitHeight + 24 : 0
    color: Theme.panel
    radius: Theme.innerRadius
    border.width: 1
    border.color: session && session.motdChanged ? Theme.accent : Theme.divider
    activeFocusOnTab: visible

    Accessible.role: Accessible.Pane
    Accessible.name: dismissed ? qsTr("Welcome message hidden") : qsTr("Server message of the day")
    Accessible.description: session ? String(session.motdSummary || "") : ""

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
        forceActiveFocus()
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

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            if (focusPrimaryAction())
                event.accepted = true
        }
    }

    ColumnLayout {
        id: panelLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
                text: root.dismissed ? qsTr("Welcome message hidden") : qsTr("Server message of the day")
                color: Theme.textStrong
                font.bold: true
                elide: Text.ElideRight
            }

            Label {
				textFormat: Text.PlainText
                visible: !!root.session && !!root.session.motdChanged
                text: qsTr("New")
                color: Theme.accent
                font.bold: true
                Accessible.name: qsTr("The welcome message has changed")
            }

            Repeater {
                id: actionRepeater
                model: root.actions

                delegate: ModernButton {
                    required property var modelData
                    objectName: "motdAction_" + String(modelData.id || "")
                    text: String(modelData.label || modelData.title || qsTr("Open"))
                    enabled: modelData.enabled === undefined || !!modelData.enabled
                    Accessible.name: text
                    onClicked: root.dispatch(String(modelData.id || ""), root.payloadFor(modelData))
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(body.implicitHeight, root.maximumBodyHeight)
            visible: root.contentVisible
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: body.implicitHeight > root.maximumBodyHeight
                                         ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

            Loader {
                id: body
                width: parent.width
                sourceComponent: root.expanded ? expandedBody : summaryBody
            }

			Component {
				id: expandedBody
				RichMessageBody {
					objectName: "motdStructuredBody"
					width: body.width
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

			Component {
				id: summaryBody
				Label {
					textFormat: Text.PlainText
					objectName: "motdSummaryBody"
					width: body.width
					text: root.session ? String(root.session.motdSummary || "") : ""
					color: Theme.textMain
					wrapMode: Text.Wrap
					maximumLineCount: 4
					elide: Text.ElideRight
					Accessible.role: Accessible.StaticText
					Accessible.name: text.length > 0 ? text : qsTr("Server message")
				}
			}
        }
    }
}
