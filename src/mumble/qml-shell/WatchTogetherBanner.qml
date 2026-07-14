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

    readonly property real naturalActionWidth: {
        const widths = []
        if (!session.sharedJoined && joinButton)
            widths.push(joinButton.implicitWidth)
        if (session.sharedJoined && !session.active && openButton)
            widths.push(openButton.implicitWidth)
        if (session.sharedHost && session.sharedParticipantCount > 1 && transferButton)
            widths.push(transferButton.implicitWidth)
        if (session.sharedJoined && !session.sharedHost && leaveButton)
            widths.push(leaveButton.implicitWidth)
        if (session.sharedHost && endButton)
            widths.push(endButton.implicitWidth)
        return widths.reduce(function(total, width) { return total + width }, 0)
            + Math.max(0, widths.length - 1) * Theme.space2
    }
    readonly property bool compactLayout: width < Math.max(620, naturalActionWidth + 300)
    readonly property real actionContentHeight: Math.max(Theme.controlHeight, actionFlow.childrenRect.height)
    readonly property bool actionsWrapped: actionContentHeight > Theme.controlHeight

    visible: session.sharedAvailable
    implicitHeight: visible ? content.implicitHeight + Theme.space4 : 0
    radius: Theme.innerRadius
	color: Theme.chatSurface
	border.color: session.sharedHost ? Theme.withAlpha(Theme.accent, 0.52) : Theme.chatIncomingBorder
	border.width: 1
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Watch Together: %1").arg(session.sharedTitle || qsTr("shared media"))
	Accessible.description: session.sharedHost
		? qsTr("You are hosting synchronized playback for %1 participant(s)").arg(session.sharedParticipantCount)
		: (session.sharedJoined ? qsTr("Synchronized with the room host")
			: qsTr("A synchronized media session is available in this voice room"))

	function focusInitialControl() {
		const candidates = [ joinButton, openButton, transferButton, leaveButton, endButton ]
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
						Layout.preferredHeight: 20
						radius: height / 2
						color: Theme.withAlpha(session.sharedHost || session.sharedJoined
							? Theme.success : Theme.accent, 0.12)
						Label {
							id: watchStateLabel
							anchors.centerIn: parent
							textFormat: Text.PlainText
							text: session.sharedHost ? qsTr("HOSTING")
								: (session.sharedJoined ? qsTr("SYNCED") : qsTr("AVAILABLE"))
							color: session.sharedHost || session.sharedJoined ? Theme.success : Theme.accent
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
					}
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("%1 participant(s)").arg(session.sharedParticipantCount)
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
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
            visible: !session.sharedJoined
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
            visible: session.sharedJoined && !session.active
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("Open player")
			tone: "accent"
			highlighted: true
			Accessible.description: qsTr("Reopen the isolated synchronized media player")
            onClicked: session.reopenSharedPlayer()
        }
		ModernIconButton {
            id: transferButton
            objectName: "watchTogetherTransferButton"
            visible: session.sharedHost && session.sharedParticipantCount > 1
            dense: true
			width: Theme.controlHeight
			height: Theme.controlHeight
			iconName: "move"
			text: qsTr("Transfer host")
            Accessible.name: qsTr("Transfer host")
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
