import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var field
    width: parent ? parent.width : 0
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: field.label || qsTr("Event behavior"); color: Theme.textStrong; font.bold: true }
        Label { text: qsTr("Log"); color: Theme.textMuted; font.pixelSize: 9 }
        Label { text: qsTr("Notify"); color: Theme.textMuted; font.pixelSize: 9 }
        Label { text: qsTr("Highlight"); color: Theme.textMuted; font.pixelSize: 9 }
        Label { text: qsTr("TTS"); color: Theme.textMuted; font.pixelSize: 9 }
        Label { text: qsTr("Sound"); color: Theme.textMuted; font.pixelSize: 9 }
    }

    ListView {
        id: eventList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 460)
        implicitHeight: Layout.preferredHeight
        model: field.rows || []
        clip: true
        spacing: 2
        delegate: Rectangle {
            required property var modelData
            width: eventList.width
            height: 38
            radius: 6
            color: index % 2 === 0 ? Theme.strip : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 4
                Label { Layout.fillWidth: true; text: modelData.name || qsTr("Event"); color: Theme.textMain; elide: Text.ElideRight; font.pixelSize: 10 }
                CheckBox { checked: !!modelData.console; onToggled: root.toggle(modelData.type, "console", checked) }
                CheckBox { checked: !!modelData.notification; onToggled: root.toggle(modelData.type, "notification", checked) }
                CheckBox { checked: !!modelData.highlight; onToggled: root.toggle(modelData.type, "highlight", checked) }
                CheckBox { checked: !!modelData.tts; onToggled: root.toggle(modelData.type, "tts", checked) }
                CheckBox { checked: !!modelData.sound; onToggled: root.toggle(modelData.type, "sound", checked) }
            }
        }
    }

    function toggle(messageType, propertyName, value) {
        dialogState.invokeAction("messages.toggleEvent",
                                 { "messageType": messageType, "property": propertyName, "value": value })
    }
}
