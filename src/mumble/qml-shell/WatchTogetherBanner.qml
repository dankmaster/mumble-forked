import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    required property var session
    required property var participantModel

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
    color: Theme.panel
    border.color: session.sharedHost ? Theme.accent : Theme.divider
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Watch Together: %1").arg(session.sharedTitle || qsTr("shared media"))

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
                radius: width / 2
                color: Theme.selected
                Label {
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    text: "▶"
                    color: Theme.accent
                    font.pixelSize: 15
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
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: session.sharedHost
                          ? qsTr("You are hosting · %1 participant(s)").arg(session.sharedParticipantCount)
                          : (session.sharedJoined
                             ? qsTr("Watching together · %1 participant(s)").arg(session.sharedParticipantCount)
                             : qsTr("Available in this voice room · %1 participant(s)").arg(session.sharedParticipantCount))
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideRight
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
            Accessible.description: qsTr("Open the isolated media player and join synchronized playback")
            onClicked: session.joinShared()
        }
        ModernButton {
            id: openButton
            objectName: "watchTogetherOpenButton"
            visible: session.sharedJoined && !session.active
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("Open player")
            onClicked: session.reopenSharedPlayer()
        }
        ModernButton {
            id: transferButton
            objectName: "watchTogetherTransferButton"
            visible: session.sharedHost && session.sharedParticipantCount > 1
            dense: true
            width: Theme.controlHeight
            text: "⇄"
            Accessible.name: qsTr("Transfer host")
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
            onClicked: session.leaveShared()
        }
        ModernButton {
            id: endButton
            objectName: "watchTogetherEndButton"
            visible: session.sharedHost
            width: Math.min(implicitWidth, Math.max(Theme.controlHeight, actionFlow.width))
            text: qsTr("End")
            onClicked: session.endShared()
        }
        }
    }
}
