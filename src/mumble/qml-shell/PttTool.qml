import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Popup {
    id: tool
    width: 280
    height: 180
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    onClosed: uiCommands.releasePtt()

    background: Rectangle {
        color: Theme.strip
        radius: Theme.shellRadius
        border.color: Theme.divider
    }

    contentItem: Rectangle {
        anchors.fill: parent
        anchors.margins: 12
        radius: Theme.shellRadius
        color: pressArea.pressed ? Qt.darker(Theme.accent, 1.35) : Theme.panel
        border.color: pressArea.pressed ? Theme.accent : Theme.divider
        border.width: pressArea.pressed ? 2 : 1

        Column {
            anchors.centerIn: parent
            spacing: 8
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Push to talk")
                color: Theme.textStrong
                font.pixelSize: 20
                font.bold: true
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Hold to transmit")
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }

        MouseArea {
            id: pressArea
            anchors.fill: parent
            hoverEnabled: true
            onPressed: uiCommands.setPttPressed(true)
            onReleased: uiCommands.releasePtt()
            onCanceled: uiCommands.releasePtt()
        }
    }
}
