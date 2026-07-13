import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    property var session: null
    signal actionRequested(string actionId, var payload)

    readonly property string connectionState: session ? String(session.connectionState || "").toLowerCase() : ""
    readonly property string tone: session ? String(session.connectionTone || "muted").toLowerCase() : "muted"
	readonly property int reportedRetryRemainingMs: session
		? Math.max(0, Number(session.connectionRetryRemainingMs || 0)) : 0
	property double retryDeadlineEpochMs: 0
	property double retryClockEpochMs: Date.now()
	readonly property int retryRemainingMs: connectionState === "retrying" && retryDeadlineEpochMs > 0
		? Math.max(0, Math.ceil(retryDeadlineEpochMs - retryClockEpochMs)) : 0
    readonly property int retryRemainingSeconds: retryRemainingMs > 0 ? Math.max(1, Math.ceil(retryRemainingMs / 1000)) : 0
    readonly property string title: connectionState === "retrying" ? qsTr("Connection lost — reconnecting…")
                                            : connectionState === "connecting" ? qsTr("Connecting to server…")
                                            : connectionState === "disconnected" ? qsTr("You're disconnected")
                                            : ""
    readonly property string detail: connectionState === "retrying" && retryRemainingSeconds > 0
                                             ? qsTr("Automatic reconnect will retry in %1s.").arg(retryRemainingSeconds)
                                             : session && String(session.connectionDetail || "").length > 0
                                                 ? String(session.connectionDetail)
                                                 : session ? String(session.connectionLabel || "") : ""
    readonly property string primaryActionId: connectionState === "disconnected"
                                                       ? (session && session.canConnect ? "server.connect" : "")
                                                       : (connectionState === "connecting" || connectionState === "retrying")
                                                           && session && session.canCancel ? "server.disconnect" : ""
    readonly property string primaryActionLabel: connectionState === "disconnected" ? qsTr("Connect") : qsTr("Cancel")
    readonly property bool bannerVisible: connectionState.length > 0 && connectionState !== "connected"
    readonly property color toneColor: tone === "danger" || tone === "error" ? Theme.danger
                                              : tone === "warning" || tone === "retry" || tone === "orange" ? Theme.warning
                                              : tone === "success" ? Theme.success : Theme.textMuted

    objectName: "connectionBanner"
    visible: bannerVisible
    implicitHeight: visible ? Math.max(60, content.implicitHeight + 24) : 0
    color: Theme.panel
    radius: Theme.innerRadius
    border.width: 1
    border.color: toneColor
    activeFocusOnTab: visible && primaryActionId.length > 0

    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
    Accessible.description: detail

	function resetRetryDeadline() {
		retryClockEpochMs = Date.now()
		retryDeadlineEpochMs = connectionState === "retrying" && reportedRetryRemainingMs > 0
			? retryClockEpochMs + reportedRetryRemainingMs : 0
	}

	onReportedRetryRemainingMsChanged: resetRetryDeadline()
	onConnectionStateChanged: resetRetryDeadline()
	Component.onCompleted: resetRetryDeadline()

	Timer {
		interval: 1000
		repeat: true
		running: root.connectionState === "retrying" && root.retryDeadlineEpochMs > 0
		onTriggered: root.retryClockEpochMs = Date.now()
	}

    function requestPrimaryAction() {
        if (primaryActionId.length > 0)
            actionRequested(primaryActionId, {})
    }

    function focusPrimaryAction() {
        if (primaryActionId.length > 0) {
            primaryButton.forceActiveFocus()
            return true
        }
        forceActiveFocus()
        return false
    }

    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)
                && primaryActionId.length > 0) {
            requestPrimaryAction()
            event.accepted = true
        }
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            radius: 2
            color: root.toneColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Label {
                Layout.fillWidth: true
                text: root.title
                color: Theme.textStrong
                font.bold: true
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.detail
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }
        }

        ModernButton {
            id: primaryButton
            objectName: "connectionBannerPrimaryAction"
            visible: root.primaryActionId.length > 0
            text: root.primaryActionLabel
            Accessible.name: text
            Accessible.description: root.detail
            onClicked: root.requestPrimaryAction()
        }
    }
}
