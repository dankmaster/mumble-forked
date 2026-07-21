import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    property var session: null
    property real maximumBodyHeight: compactLayout ? 280 : 360
	property real maximumImageHeight: compactLayout ? 120 : 180
	property bool visualFixtureMode: false
    signal actionRequested(string actionId, var payload)
    signal linkRequested(url link)

    readonly property bool hasContent: !!session && !!session.hasMotd
    readonly property bool dismissed: !!session && !!session.motdDismissed
	readonly property bool surfaceVisible: hasContent && !dismissed
    readonly property bool expanded: !!session && !!session.motdExpanded
	readonly property bool contentVisible: surfaceVisible
    readonly property string signature: session ? String(session.motdSignature || "") : ""
    readonly property var actions: session && session.motdActions ? session.motdActions : []
    readonly property bool compactLayout: width < 560
	readonly property bool actionsWrapped: false
	readonly property int headerHeight: 42
	readonly property string summary: cleanSummary(session ? session.motdSummary : "")
	readonly property real expandedBodyHeight: expanded
		? Math.min(structuredBody.implicitHeight, maximumBodyHeight) : 0

    objectName: "motdPanel"
    visible: surfaceVisible
	implicitHeight: surfaceVisible ? headerHeight
		+ (expanded ? expandedBodyHeight + Theme.space2 : 0) : 0
	color: session && session.motdChanged ? Theme.accentSubtle : Theme.panel
    radius: Theme.space2
    border.width: 1
	border.color: session && session.motdChanged ? Theme.warning : Theme.divider
	// This is an informational container. Its real actions participate in the
	// tab chain; the panel itself must never become an inert stop.
    activeFocusOnTab: false

    Accessible.role: Accessible.Pane
    Accessible.name: dismissed ? qsTr("Welcome message hidden") : qsTr("Server message of the day")
    Accessible.description: expanded ? "" : summary

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
		if (!surfaceVisible)
			return false
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

	Item {
			id: headerBar
			objectName: "motdHeaderBar"
			anchors.top: parent.top
			anchors.left: parent.left
			anchors.right: parent.right
			height: root.headerHeight

			RowLayout {
				anchors.fill: parent
				anchors.leftMargin: 10
				anchors.rightMargin: 6
				spacing: Theme.space2

				Rectangle {
					id: infoBadge
					objectName: "motdInfoBadge"
					Layout.preferredWidth: 24
					Layout.preferredHeight: 24
					radius: Theme.space2
					color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
					Accessible.ignored: true

					ModernIcon {
						anchors.centerIn: parent
						name: "direct"
						size: 15
						color: Theme.accent
					}
				}

				Label {
					id: heading
					objectName: "motdHeading"
					Layout.preferredWidth: implicitWidth
					textFormat: Text.PlainText
					text: qsTr("Welcome")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					font.capitalization: Font.AllUppercase
					font.letterSpacing: 0.8
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				Rectangle {
					visible: summaryBody.visible
					Layout.preferredWidth: 3
					Layout.preferredHeight: 3
					radius: 2
					color: Theme.textMuted
					Accessible.ignored: true
				}

				Label {
					id: summaryBody
					objectName: "motdSummaryBody"
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					visible: !root.expanded && root.summary.length > 0
					textFormat: Text.PlainText
					text: root.summary
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					elide: Text.ElideRight
					verticalAlignment: Text.AlignVCenter
					Accessible.ignored: true
				}

				Item {
					visible: !summaryBody.visible
					Layout.fillWidth: true
					Layout.minimumWidth: 0
				}

				RowLayout {
					id: actionFlow
					objectName: "motdActionFlow"
					Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
					spacing: Theme.space1
					visible: root.actions.length > 0

					Repeater {
						id: actionRepeater
						model: root.actions

						delegate: ModernIconButton {
							required property var modelData
							readonly property string actionId: String(modelData.id || "")
							readonly property string fullLabel: String(modelData.label
								|| modelData.title || qsTr("Open"))
							objectName: "motdAction_" + actionId
							Layout.minimumWidth: 26
							Layout.preferredWidth: 26
							Layout.maximumWidth: 26
							Layout.minimumHeight: 26
							Layout.preferredHeight: 26
							Layout.maximumHeight: 26
							implicitWidth: 26
							implicitHeight: 26
							dense: true
							iconName: actionId === "motd.dismiss" ? "close"
								: root.expanded ? "chevron-up" : "chevron-down"
							text: fullLabel
							enabled: modelData.enabled === undefined || !!modelData.enabled
							Accessible.name: fullLabel
							ToolTip.visible: hovered
							ToolTip.text: fullLabel
							onClicked: root.dispatch(actionId, root.payloadFor(modelData))
						}
					}
				}
			}
	}

	Rectangle {
		objectName: "motdBodyDivider"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: headerBar.bottom
		height: root.expanded ? 1 : 0
		visible: root.expanded
		color: Theme.divider
		Accessible.ignored: true
	}

	ScrollView {
			id: motdScroll
			objectName: "motdBodyScroll"
			anchors.top: headerBar.bottom
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.topMargin: root.expanded ? Theme.space1 : 0
			anchors.leftMargin: root.compactLayout ? Theme.space3 : 42
			anchors.rightMargin: Theme.space3
			height: root.contentVisible && root.expanded ? root.expandedBodyHeight : 0
			// ScrollView otherwise derives its internal content width from the
			// unwrapped text's implicit width. The body is visually stretched, but
			// Windows UIA then reports a narrow ancestor around a full-width text
			// leaf. Keep layout and semantic bounds on the same fixed viewport width.
			contentWidth: Math.max(1, availableWidth)
			contentHeight: structuredBody.implicitHeight
			visible: root.contentVisible && root.expanded
			clip: true
			ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
			ScrollBar.vertical.policy: structuredBody.implicitHeight > root.maximumBodyHeight
				? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

			MotdDocumentBody {
				id: structuredBody
				objectName: "motdStructuredBody"
				width: Math.max(1, motdScroll.availableWidth)
				maximumImageWidth: 640
				maximumImageHeight: root.maximumImageHeight
				blocks: root.session && root.session.motdBlocks
					? root.session.motdBlocks : []
				fallbackText: root.summary
				animationsEnabled: !root.visualFixtureMode
				hoverEffectsEnabled: !root.visualFixtureMode
				onLinkRequested: function(link) {
					const safeLink = root.safeExternalUrl(link)
					if (safeLink.length > 0)
						root.linkRequested(safeLink)
				}
			}
	}
}
