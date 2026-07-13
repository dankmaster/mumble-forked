import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ScreenShare 1.0

Window {
    id: root
    required property var backend
	property bool hostClosing: false
	property bool closeRequestPending: false
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: qsTr("Mumble Screen Share - %1").arg(backend.detail)
    color: Theme.shellBackground
	function closeFromHost() {
		hostClosing = true
		closeRequestPending = false
		close()
	}
	onClosing: close => {
		if (hostClosing) {
			close.accepted = true
			return
		}
		close.accepted = false
		if (closeRequestPending)
			return
		closeRequestPending = true
		Qt.callLater(function() {
			if (!root.hostClosing && root.backend)
				root.backend.requestClose()
		})
	}

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: backend.title; color: Theme.textStrong; font.bold: true; font.pixelSize: 15 }
                Label { text: backend.detail; color: Theme.textMuted }
            }
            ModernButton { text: backend.paused ? qsTr("Resume live") : qsTr("Pause view"); onClicked: backend.setPaused(!backend.paused) }
            ModernButton { text: backend.audioMuted ? qsTr("Unmute audio") : qsTr("Mute audio"); enabled: backend.audioAvailable; onClicked: backend.setAudioMuted(!backend.audioMuted) }
            Slider { from: 0; to: 100; value: backend.audioVolume; enabled: backend.audioAvailable; onMoved: backend.setAudioVolume(value); Accessible.name: qsTr("Stream volume") }
            ModernButton { text: qsTr("Stop"); onClicked: backend.requestStop() }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#05070a"
            border.color: Theme.divider
            radius: Theme.innerRadius
            clip: true
            ScreenShareVideoItem {
                anchors.fill: parent
                backend: root.backend
                visible: backend.nativeFrameActive && !backend.paused
            }
            WindowContainer {
                anchors.fill: parent
                anchors.margins: 1
                window: backend.videoWindow
                visible: !backend.nativeFrameActive && window !== null && !backend.paused
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textMuted
                visible: (!backend.nativeFrameActive && backend.videoWindow === null) || backend.paused
                text: backend.paused ? qsTr("Paused locally\n\nResume returns to the live edge.") : backend.status
            }
        }
        Label { Layout.fillWidth: true; text: backend.status; color: Theme.textMuted; elide: Text.ElideRight }
    }
}
