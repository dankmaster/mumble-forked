import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    required property var session
    required property var participantModel
	readonly property string surfaceId: "watchTogether.banner"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property string operationStatus: String(session.sharedOperationStatus ||
		(session.sharedJoined ? "ready" : "available")).trim().toLowerCase()
	readonly property string operationError: String(session.sharedOperationError || "").trim()
	readonly property bool operationBusy: operationStatus === "starting" || operationStatus === "reconnecting"
	readonly property bool operationFailed: operationStatus === "error"
	readonly property string operationLabel: operationStatus === "starting" ? qsTr("STARTING")
		: operationStatus === "reconnecting" ? qsTr("RECONNECTING")
		: operationFailed ? qsTr("FAILED")
		: session.sharedHost ? qsTr("HOSTING")
		: session.sharedJoined ? qsTr("SYNCED") : qsTr("AVAILABLE")
	readonly property color operationTone: operationFailed ? Theme.danger
		: operationBusy ? Theme.warning
		: (session.sharedHost || session.sharedJoined ? Theme.success : Theme.accent)
	readonly property string participantCountLabel: Number(session.sharedParticipantCount) === 1
		? qsTr("1 participant") : qsTr("%1 participants").arg(session.sharedParticipantCount)

    readonly property real naturalActionWidth: {
        const widths = []
        if (!session.sharedJoined && !operationFailed && joinButton)
            widths.push(joinButton.implicitWidth)
        if (session.sharedJoined && !session.active && !operationFailed && openButton)
            widths.push(openButton.implicitWidth)
        if (session.sharedHost && session.sharedParticipantCount > 1 && transferButton)
            widths.push(transferButton.implicitWidth)
		if (operationFailed && retryButton)
			widths.push(retryButton.implicitWidth)
        if (session.sharedJoined && !session.sharedHost && leaveButton)
            widths.push(leaveButton.implicitWidth)
        if (session.sharedHost && endButton)
            widths.push(endButton.implicitWidth)
		return Math.ceil(widths.reduce(function(total, width) { return total + width }, 0))
			+ Math.max(0, widths.length - 1) * Theme.space2 + Theme.space1
    }
    readonly property bool compactLayout: width < Math.max(620, naturalActionWidth + 300)
    readonly property real actionContentHeight: Math.max(Theme.controlHeight, actionFlow.childrenRect.height)
    readonly property bool actionsWrapped: actionContentHeight > Theme.controlHeight

    visible: session.sharedAvailable
    implicitHeight: visible ? content.implicitHeight + Theme.space4 : 0
    radius: Theme.innerRadius
	color: Theme.chatSurface
	border.color: operationFailed ? Theme.withAlpha(Theme.danger, 0.58)
		: operationBusy ? Theme.withAlpha(Theme.warning, 0.52)
		: session.sharedHost ? Theme.withAlpha(Theme.accent, 0.52) : Theme.chatIncomingBorder
	border.width: 1
	Accessible.role: operationFailed ? Accessible.AlertMessage : Accessible.Pane
    Accessible.name: qsTr("Watch Together: %1").arg(session.sharedTitle || qsTr("shared media"))
	Accessible.description: operationFailed ? (operationError || qsTr("Synchronized playback failed"))
		: operationBusy ? operationLabel
		: session.sharedHost
		? qsTr("You are hosting synchronized playback for %1").arg(participantCountLabel)
		: (session.sharedJoined ? qsTr("Synchronized with the room host")
			: qsTr("A synchronized media session is available in this voice room"))

	function focusInitialControl() {
		const candidates = [ retryButton, joinButton, openButton, transferButton, leaveButton, endButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const control = candidates[index]
			if (!control || !control.visible || !control.enabled)
				continue
			control.forceActiveFocus()
			return true
		}
		return false
	}

    function participantLabel(sessionId) {
        const wanted = String(sessionId)
        for (let row = 0; row < participantModel.count; ++row) {
            const participant = participantModel.get(row)
            if (String(participant.stableId || participant.id || "") === wanted)
                return participant.title || qsTr("Session %1").arg(wanted)
        }
        return qsTr("Session %1").arg(wanted)
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.space2
        implicitHeight: root.compactLayout
            ? summary.implicitHeight + Theme.space2 + root.actionContentHeight
            : Math.max(summary.implicitHeight, root.actionContentHeight)

        RowLayout {
            id: summary
            x: 0
            y: root.compactLayout ? 0 : Math.max(0, (content.height - height) / 2)
            width: root.compactLayout ? content.width
                : Math.max(0, content.width - actionFlow.width - Theme.space2)
            height: implicitHeight
            spacing: Theme.space2

            Rectangle {
                Layout.preferredWidth: Theme.controlHeight
                Layout.preferredHeight: Theme.controlHeight
                Layout.alignment: Qt.AlignVCenter
                radius: Theme.innerRadius
				color: Theme.accentSubtle
				border.color: Theme.withAlpha(Theme.accent, 0.28)
				ModernIcon {
                    anchors.centerIn: parent
					name: "play"
					size: 18
                    color: Theme.accent
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: session.sharedTitle || qsTr("Shared media session")
                    color: Theme.textStrong
					font.weight: Font.DemiBold
					font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }
				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						objectName: "watchTogetherStateBadge"
						Layout.preferredWidth: watchStateLabel.implicitWidth + Theme.space2
							+ (root.operationBusy ? 16 : 0)
						Layout.preferredHeight: 20
						radius: height / 2
						color: Theme.withAlpha(root.operationTone, 0.12)
						ModernBusyIndicator {
							anchors.left: parent.left
							anchors.leftMargin: Theme.space1
							anchors.verticalCenter: parent.verticalCenter
							width: 12
							height: 12
							visible: root.operationBusy
							running: visible
							Accessible.ignored: true
						}
						Label {
							id: watchStateLabel
							objectName: "watchTogetherStateLabel"
							anchors.centerIn: parent
							anchors.horizontalCenterOffset: root.operationBusy ? Theme.space1 : 0
							textFormat: Text.PlainText
							text: root.operationLabel
							color: root.operationTone
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
					}
					Label {
						objectName: "watchTogetherOperationDetail"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.operationFailed
							? (root.operationError || qsTr("Synchronized playback failed."))
							: root.participantCountLabel
						color: root.operationFailed ? Theme.danger : Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
						Accessible.name: text
					}
				}
            }
        }

        Flow {
            id: actionFlow
            objectName: "watchTogetherActionFlow"
            x: Math.max(0, content.width - width)
            y: root.compactLayout ? summary.height + Theme.space2
                : Math.max(0, (content.height - height) / 2)
            width: root.compactLayout ? content.width
                : Math.min(root.naturalActionWidth, content.width)
            height: root.actionContentHeight
            spacing: Theme.space2

        ModernButton {
            id: joinButton
            objectName: "watchTogetherJoinButton"
            visible: !session.sharedJoined && !root.operationFailed
			enabled: !root.operationBusy && !root.operationFailed
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("Join")
			tone: "accent"
			highlighted: true
            Accessible.description: qsTr("Open the isolated media player and join synchronized playback")
            onClicked: session.joinShared()
        }
        ModernButton {
            id: openButton
            objectName: "watchTogetherOpenButton"
            visible: session.sharedJoined && !session.active && !root.operationFailed
			enabled: !root.operationBusy && !root.operationFailed
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("Open player")
			tone: "accent"
			highlighted: true
			Accessible.description: qsTr("Reopen the isolated synchronized media player")
            onClicked: session.reopenSharedPlayer()
        }
		ModernButton {
            id: transferButton
            objectName: "watchTogetherTransferButton"
            visible: session.sharedHost && session.sharedParticipantCount > 1
			enabled: !root.operationBusy && !root.operationFailed
            dense: true
			text: qsTr("Transfer host")
			Accessible.description: qsTr("Choose another participant to control synchronized playback")
            onClicked: transferMenu.open()
            ModernMenu {
                id: transferMenu
                Repeater {
                    model: session.sharedParticipantSessions
                    delegate: MenuItem {
                        required property var modelData
                        visible: Number(modelData) !== Number(session.sharedHostSession)
                        height: visible ? implicitHeight : 0
                        text: qsTr("Transfer to %1").arg(root.participantLabel(modelData))
                        onTriggered: session.transferSharedHost(String(modelData))
                    }
                }
            }
        }
		ModernButton {
			id: retryButton
			objectName: "watchTogetherRetryButton"
			visible: root.operationFailed
			width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
			text: session.active ? qsTr("Retry player") : qsTr("Reconnect")
			tone: "accent"
			highlighted: true
			Accessible.description: qsTr("Retry synchronized playback without leaving the room session")
			onClicked: {
				if (session.active)
					session.retry()
				else if (session.sharedJoined)
					session.reopenSharedPlayer()
				else
					session.joinShared()
			}
		}
        ModernButton {
            id: leaveButton
            objectName: "watchTogetherLeaveButton"
            visible: session.sharedJoined && !session.sharedHost
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("Leave")
			Accessible.description: qsTr("Leave synchronized playback in this room")
            onClicked: session.leaveShared()
        }
        ModernButton {
            id: endButton
            objectName: "watchTogetherEndButton"
            visible: session.sharedHost
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("End")
			tone: "danger"
			Accessible.description: qsTr("End synchronized playback for every participant")
            onClicked: session.endShared()
        }
        }
    }
}
