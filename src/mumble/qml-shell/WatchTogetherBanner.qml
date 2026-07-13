import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root

    required property var session
    required property var participantModel

    visible: session.sharedAvailable
    implicitHeight: visible ? content.implicitHeight + 20 : 0
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

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 17
            color: Theme.selected
            Label {
                anchors.centerIn: parent
                text: "▶"
                color: Theme.accent
                font.pixelSize: 15
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: session.sharedTitle || qsTr("Shared media session")
                color: Theme.textStrong
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
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

        ModernButton {
            visible: !session.sharedJoined
            text: qsTr("Join")
            Accessible.description: qsTr("Open the isolated media player and join synchronized playback")
            onClicked: session.joinShared()
        }
        ModernButton {
            visible: session.sharedJoined && !session.active
            text: qsTr("Open player")
            onClicked: session.reopenSharedPlayer()
        }
        ToolButton {
            id: transferButton
            visible: session.sharedHost && session.sharedParticipantCount > 1
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
            visible: session.sharedJoined && !session.sharedHost
            text: qsTr("Leave")
            onClicked: session.leaveShared()
        }
        ModernButton {
            visible: session.sharedHost
            text: qsTr("End")
            onClicked: session.endShared()
        }
    }
}
