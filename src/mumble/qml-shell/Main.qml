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
                        Label { text: clientSession.serverName; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                        Label { text: "Qt Quick native shell preview"; color: Theme.textMuted; font.pixelSize: 12 }
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
                                placeholderText: clientSession.connected ? "Write a message" : "Connect to send messages"
                                enabled: clientSession.connected
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
                            enabled: clientSession.connected && composer.text.trim().length > 0
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
                            width: rooms.width - 20
                            height: subtitle.length > 0 ? 48 : 38
                            radius: 8
                            color: selected ? Theme.selected : roomMouse.containsMouse ? Theme.panel : "transparent"
                            Column {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 2
                                Label { width: parent.width; text: title; color: Theme.textStrong; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                                Label { width: parent.width; visible: subtitle.length > 0; text: subtitle; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
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
                                Label { text: clientSession.connected ? "Online" : "Offline"; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ModernButton { text: clientSession.selfMuted ? "Unmute" : "Mute"; onClicked: uiCommands.toggleSelfMute() }
                        }
                    }
                }
            }
        }
    }
}
