import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
	id: root
	required property var backend
	readonly property bool controlsWrapped: headerActions.childrenRect.height > Theme.controlHeight

	implicitWidth: controlsLayout.implicitWidth
	implicitHeight: controlsLayout.implicitHeight
	Accessible.role: Accessible.ToolBar
	Accessible.name: qsTr("Screen share controls")

	ColumnLayout {
		id: controlsLayout
		anchors.fill: parent
		spacing: Theme.space2

		ColumnLayout {
			Layout.fillWidth: true
			spacing: 2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.backend.title
				color: Theme.textStrong
				font.bold: true
				font.pixelSize: Theme.fontTitle
				elide: Text.ElideRight
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.backend.detail
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
			}
		}

		Flow {
			id: headerActions
			objectName: "screenShareHeaderActions"
			Layout.fillWidth: true
			Layout.minimumHeight: Theme.controlHeight
			Layout.preferredHeight: Math.max(Theme.controlHeight, childrenRect.height)
			spacing: Theme.space2

			ModernButton {
				id: pauseButton
				objectName: "screenSharePauseButton"
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, headerActions.width))
				text: root.backend.paused ? qsTr("Resume live") : qsTr("Pause view")
				onClicked: root.backend.setPaused(!root.backend.paused)
			}
			ModernButton {
				id: muteButton
				objectName: "screenShareMuteButton"
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, headerActions.width))
				text: root.backend.audioMuted ? qsTr("Unmute audio") : qsTr("Mute audio")
				enabled: root.backend.audioAvailable
				onClicked: root.backend.setAudioMuted(!root.backend.audioMuted)
			}
			ModernSlider {
				id: volumeSlider
				objectName: "screenShareVolumeSlider"
				width: Math.min(180, Math.max(120, headerActions.width))
				from: 0
				to: 100
				value: root.backend.audioVolume
				enabled: root.backend.audioAvailable
				onMoved: root.backend.setAudioVolume(value)
				Accessible.name: qsTr("Stream volume")
				Accessible.description: qsTr("%1 percent").arg(Math.round(value))
			}
			ModernButton {
				id: stopButton
				objectName: "screenShareStopButton"
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, headerActions.width))
				text: qsTr("Stop")
				tone: "danger"
				onClicked: root.backend.requestStop()
			}
		}
	}
}
