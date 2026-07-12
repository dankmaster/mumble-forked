import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 820
    minimumWidth: 760
    minimumHeight: 520
    title: clientSession.serverName
    color: Theme.strip

    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        radius: Theme.shellRadius
        color: Theme.shellBackground
        border.color: Theme.divider
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: Theme.panel
                    border.color: Theme.divider
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Label {
                            text: activeScope.label.length > 0 ? activeScope.label : clientSession.serverName
                            color: Theme.textStrong
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Label {
                            text: activeScope.description.length > 0 ? activeScope.description : activeScope.kindLabel
                            color: Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: Math.max(0, root.width - 390)
                        }
                    }
                }

                ListView {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: chatModel
                    clip: true
                    spacing: 8
                    leftMargin: 28
                    rightMargin: 28
                    topMargin: 20
                    bottomMargin: 20
                    reuseItems: true
                    delegate: Rectangle {
                        required property string title
                        required property string subtitle
                        width: Math.min(timeline.width - 56, 680)
                        height: messageColumn.implicitHeight + 24
                        radius: Theme.innerRadius
                        color: Theme.panel
                        Column {
                            id: messageColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 5
                            Label { text: title || "System"; color: Theme.accent; font.bold: true; font.pixelSize: 11 }
                            Label { width: parent.width; text: subtitle; color: Theme.textMain; wrapMode: Text.Wrap; font.pixelSize: 12 }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: chatModel.count === 0
                        text: clientSession.connected ? "Select a room to start chatting" : "Connect to load rooms and messages"
                        color: Theme.textMuted
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: Theme.strip
                    border.color: Theme.divider
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.innerRadius
                            color: Theme.panel
                            border.color: Theme.divider
                            TextArea {
                                id: composer
                                anchors.fill: parent
                                anchors.margins: 5
                                placeholderText: activeScope.composerPlaceholder.length > 0
                                                 ? activeScope.composerPlaceholder
                                                 : qsTr("Connect to send messages")
                                enabled: activeScope.canSend
                                color: Theme.textMain
                                placeholderTextColor: Theme.textMuted
                                background: null
                                wrapMode: TextEdit.Wrap
                                Keys.onReturnPressed: event => {
                                    if (!(event.modifiers & Qt.ShiftModifier) && text.trim().length > 0) {
                                        uiCommands.sendMessage(text)
                                        text = ""
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                        ModernButton {
                            text: "Send"
                            enabled: activeScope.canSend && composer.text.trim().length > 0
                            onClicked: { uiCommands.sendMessage(composer.text); composer.text = "" }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 310
                Layout.fillHeight: true
                color: Theme.rail
                border.color: Theme.divider
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: Theme.panel
                        border.color: Theme.divider
                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Label { text: clientSession.serverName; color: Theme.textStrong; font.bold: true; font.pixelSize: 14 }
                            Label { text: clientSession.connectionLabel; color: clientSession.connected ? Theme.accent : Theme.textMuted; font.pixelSize: 11 }
                        }
                    }
                    Label { Layout.margins: 18; text: "VOICE ROOMS"; color: Theme.textMuted; font.pixelSize: 10; font.bold: true }
                    ListView {
                        id: rooms
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: roomModel
                        clip: true
                        reuseItems: true
                        spacing: 2
                        leftMargin: 10
                        rightMargin: 10
                        delegate: Rectangle {
                            required property string stableId
                            required property string title
                            required property string subtitle
                            required property string kind
                            required property bool selected
                            required property int depth
                            required property int unreadCount
                            width: rooms.width - 20
                            height: subtitle.length > 0 ? 48 : 38
                            radius: 8
                            color: selected ? Theme.selected : roomMouse.containsMouse ? Theme.panel : "transparent"
                            Column {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10 + Math.min(depth, 5) * 12
                                anchors.rightMargin: 10
                                spacing: 2
                                Label { width: parent.width; text: title; color: Theme.textStrong; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                                Label { width: parent.width; visible: subtitle.length > 0; text: subtitle; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                            Rectangle {
                                visible: unreadCount > 0
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(20, unreadLabel.implicitWidth + 10)
                                height: 20
                                radius: 10
                                color: Theme.accent
                                Label {
                                    id: unreadLabel
                                    anchors.centerIn: parent
                                    text: unreadCount > 99 ? "99+" : unreadCount
                                    color: Theme.strip
                                    font.pixelSize: 9
                                    font.bold: true
                                }
                            }
                            MouseArea {
                                id: roomMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: uiCommands.selectScope(stableId)
                                onDoubleClicked: if (kind === "voice") uiCommands.joinVoiceChannel(stableId)
                            }
                        }
                    }
                    Label {
                        Layout.leftMargin: 18
                        Layout.topMargin: 10
                        Layout.bottomMargin: 6
                        text: qsTr("PARTICIPANTS")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                        visible: participantModel.count > 0
                    }
                    ListView {
                        id: participants
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Math.min(contentHeight, 180) : 0
                        visible: participantModel.count > 0
                        model: participantModel
                        clip: true
                        reuseItems: true
                        leftMargin: 10
                        rightMargin: 10
                        delegate: Rectangle {
                            required property string title
                            required property string subtitle
                            required property string status
                            width: participants.width - 20
                            height: 42
                            color: "transparent"
                            Rectangle {
                                id: presenceDot
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: status.length > 0 && status !== "passive" ? Theme.accent : Theme.textMuted
                            }
                            Column {
                                anchors.left: presenceDot.right
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                Label { width: parent.width; text: title; color: Theme.textStrong; font.pixelSize: 11; elide: Text.ElideRight }
                                Label { width: parent.width; text: subtitle; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: subtitle.length > 0 }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: Theme.strip
                        border.color: Theme.divider
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: clientSession.selfName; color: Theme.textStrong; font.bold: true }
                                Label { text: clientSession.connectionLabel; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ModernButton { text: clientSession.selfMuted ? "Unmute" : "Mute"; onClicked: uiCommands.toggleSelfMute() }
                        }
                    }
                }
            }
        }
    }
}
