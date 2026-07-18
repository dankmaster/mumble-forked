import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    property var session: null
	// The disconnected empty-state already owns the prominent server-browser
	// action. Its host can suppress this secondary copy while retaining the
	// status banner and all retry/failure actions.
	property bool showDisconnectedAction: true
	property bool animationsEnabled: true
    signal actionRequested(string actionId, var payload)

    readonly property string connectionState: session ? String(session.connectionState || "").toLowerCase() : ""
    readonly property string tone: session ? String(session.connectionTone || "muted").toLowerCase() : "muted"
	readonly property bool failed: connectionState === "disconnected"
		&& (tone === "danger" || tone === "error")
	readonly property int reportedRetryRemainingMs: session
		? Math.max(0, Number(session.connectionRetryRemainingMs || 0)) : 0
	property double retryDeadlineEpochMs: 0
	property double retryClockEpochMs: Date.now()
	readonly property int retryRemainingMs: connectionState === "retrying" && retryDeadlineEpochMs > 0
		? Math.max(0, Math.ceil(retryDeadlineEpochMs - retryClockEpochMs)) : 0
    readonly property int retryRemainingSeconds: retryRemainingMs > 0 ? Math.max(1, Math.ceil(retryRemainingMs / 1000)) : 0
	readonly property string suppliedDetail: session
		? String(session.connectionDetail || session.connectionLabel || "").trim() : ""
	readonly property string suppliedLabel: session ? String(session.connectionLabel || "").trim() : ""
	readonly property bool detailEchoesDisconnectedStatus: connectionState === "disconnected" && !failed
		&& suppliedDetail.length > 0 && suppliedLabel.length > 0
		&& suppliedDetail.toLowerCase() === suppliedLabel.toLowerCase()
    readonly property string title: connectionState === "retrying" ? qsTr("Connection lost — reconnecting…")
                                            : connectionState === "connecting" ? qsTr("Connecting to server…")
                                            : connectionState === "disconnected"
											? (failed ? qsTr("Connection failed") : qsTr("You're disconnected"))
                                            : ""
    readonly property string detail: connectionState === "retrying" && retryRemainingSeconds > 0
                                             ? qsTr("Automatic reconnect will retry in %1s.").arg(retryRemainingSeconds)
											 : detailEchoesDisconnectedStatus ? "" : suppliedDetail
	readonly property string accessibleDetail: connectionState === "retrying"
		? (session && String(session.connectionDetail || "").length > 0
			? String(session.connectionDetail) : qsTr("Automatic reconnect is scheduled."))
		: (suppliedDetail.length > 0 ? suppliedDetail : detail)
    readonly property string primaryActionId: connectionState === "disconnected"
													   ? (showDisconnectedAction && session && session.canConnect ? "server.connect" : "")
                                                       : (connectionState === "connecting" || connectionState === "retrying")
                                                           && session && session.canCancel ? "server.disconnect" : ""
	readonly property string primaryActionLabel: connectionState === "disconnected"
		? (failed ? qsTr("Try again") : qsTr("Connect")) : qsTr("Cancel")
	readonly property bool progressVisible: connectionState === "connecting"
		|| connectionState === "retrying"
    readonly property bool bannerVisible: connectionState.length > 0 && connectionState !== "connected"
    readonly property bool compactLayout: width < 560
    readonly property color toneColor: tone === "danger" || tone === "error" ? Theme.danger
                                              : tone === "warning" || tone === "retry" || tone === "orange" ? Theme.warning
                                              : tone === "success" ? Theme.success : Theme.textMuted

    objectName: "connectionBanner"
    visible: bannerVisible
    implicitHeight: visible ? Math.max(60, messageGrid.implicitHeight + Theme.space3 * 2) : 0
    color: Theme.bannerBackground
    radius: Theme.innerRadius
    border.width: 1
    border.color: Theme.bannerBorder
    Behavior on border.color {
		ColorAnimation { duration: root.animationsEnabled ? Theme.motionFast : 0 }
	}
    // The alert describes the connection state, but only its real action is a
    // keyboard stop. Keeping the container out of the tab chain avoids two
    // consecutive controls that perform the same operation.
    activeFocusOnTab: false

    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
    Accessible.description: accessibleDetail
	Accessible.ignored: !visible

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
		return false
    }

    RowLayout {
        id: content
		objectName: "connectionBannerContent"
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space4
        anchors.topMargin: Theme.space3
        anchors.bottomMargin: Theme.space3
        spacing: Theme.space3

        Rectangle {
			objectName: "connectionBannerToneRail"
            Layout.preferredWidth: Theme.space1
            Layout.fillHeight: true
            radius: 2
            color: root.toneColor
        }

		ModernBusyIndicator {
			objectName: "connectionBannerBusyIndicator"
			Layout.preferredWidth: visible ? 20 : 0
			Layout.preferredHeight: 20
			visible: root.progressVisible
			running: visible
			animated: root.animationsEnabled
			tone: root.connectionState === "retrying" ? "warning" : "accent"
			Accessible.name: root.title
			Accessible.description: root.accessibleDetail
		}

        GridLayout {
			id: messageGrid
            Layout.fillWidth: true
            columns: root.compactLayout ? 1 : 2
            columnSpacing: Theme.space3
            rowSpacing: 3

            Label {
				objectName: "connectionBannerTitle"
				textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.row: 0
                Layout.column: 0
                text: root.title
                color: Theme.textStrong
                font.bold: true
                elide: Text.ElideRight
				Accessible.ignored: true
            }

            Label {
				objectName: "connectionBannerDetail"
				textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.row: 1
                Layout.column: 0
                visible: text.length > 0
                text: root.detail
                color: Theme.secondaryText
                wrapMode: Text.Wrap
				Accessible.ignored: true
            }

            ModernButton {
                id: primaryButton
                objectName: "connectionBannerPrimaryAction"
                Layout.row: root.compactLayout ? 2 : 0
                Layout.column: root.compactLayout ? 0 : 1
                Layout.rowSpan: root.compactLayout ? 1 : 2
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                visible: root.primaryActionId.length > 0
                text: root.primaryActionLabel
                Accessible.name: text
                Accessible.description: root.accessibleDetail
                onClicked: root.requestPrimaryAction()
            }
        }
    }
}
