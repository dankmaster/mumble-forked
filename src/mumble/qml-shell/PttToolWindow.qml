import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Mumble.Theme 1.0

Window {
    id: tool
	property bool hasBeenActive: false
	function beginHold() { uiCommands.setPttPressed(true) }
	function endHold() { uiCommands.releasePtt() }
    width: 280
    height: 180
    minimumWidth: 240
    minimumHeight: 140
    visible: false
    title: qsTr("Push to talk")
    color: Theme.strip
    flags: Qt.Tool | Qt.WindowStaysOnTopHint

    onClosing: function(close) {
        uiCommands.releasePtt()
        close.accepted = true
    }
    onActiveChanged: {
		if (active)
			hasBeenActive = true
		else if (hasBeenActive)
            uiCommands.releasePtt()
    }
	Component.onDestruction: uiCommands.releasePtt()
    onVisibleChanged: {
        if (!visible)
            uiCommands.releasePtt()
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: {
            uiCommands.releasePtt()
            tool.hide()
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 12
        radius: Theme.shellRadius
        color: pressButton.pressed ? Qt.darker(Theme.accent, 1.35) : Theme.panel
        border.color: pressButton.activeFocus || pressButton.pressed ? Theme.accent : Theme.divider
        border.width: pressButton.activeFocus || pressButton.pressed ? 2 : 1

        Column {
            anchors.centerIn: parent
            spacing: 8

            Label {
				textFormat: Text.PlainText
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Push to talk")
                color: Theme.textStrong
                font.pixelSize: 20
                font.bold: true
            }

            Label {
				textFormat: Text.PlainText
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Hold to transmit")
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }

        Button {
            id: pressButton
			objectName: "pttHoldButton"
            anchors.fill: parent
            hoverEnabled: true
			activeFocusOnTab: true
			focusPolicy: Qt.StrongFocus
			background: null
			contentItem: Item {}
            Accessible.name: qsTr("Push to talk")
			Accessible.description: qsTr("Hold with the pointer or Space key to transmit")
            Accessible.role: Accessible.Button
			Accessible.onPressAction: {
				uiCommands.setPttPressed(true)
				Qt.callLater(function() { uiCommands.releasePtt() })
			}
			onPressed: tool.beginHold()
			onReleased: tool.endHold()
			onCanceled: tool.endHold()
        }
    }
}
