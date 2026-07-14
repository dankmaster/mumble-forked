import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
	id: root
	required property var backend
	readonly property bool controlsWrapped: headerActions.childrenRect.height > Theme.controlHeight
	readonly property string operationStatus: String(backend.operationStatus || "idle")
	readonly property bool operationBusy: operationStatus === "loading"
	readonly property bool operationFailed: operationStatus === "error"
	readonly property bool transportControlsEnabled: !operationBusy && !operationFailed
	readonly property real maximumActionWidth: Math.max(Theme.controlHeight,
		Math.floor((headerActions.width - Theme.space2) / 2))
	readonly property string stateLabel: operationFailed ? qsTr("Viewer unavailable")
		: operationBusy ? qsTr("Reconnecting")
		: backend.paused ? qsTr("Paused locally") : qsTr("Live")
	readonly property color stateTone: operationFailed ? Theme.danger
		: operationBusy ? Theme.warning
		: backend.paused ? Theme.textMuted : Theme.success

	implicitWidth: controlsLayout.implicitWidth
	implicitHeight: controlsLayout.implicitHeight
	Accessible.role: Accessible.ToolBar
	Accessible.name: qsTr("Screen share controls")
	Accessible.description: stateLabel

	function focusInitialControl() {
		const candidates = [ pauseButton, muteButton, volumeSlider, stopButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const control = candidates[index]
			if (!control || !control.visible || !control.enabled)
				continue
			control.forceActiveFocus()
			return true
		}
		return false
	}

	ColumnLayout {
		id: controlsLayout
		anchors.fill: parent
		spacing: Theme.space2

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space3

			Rectangle {
				Layout.preferredWidth: Theme.controlHeight
				Layout.preferredHeight: Theme.controlHeight
				Layout.alignment: Qt.AlignVCenter
				radius: Theme.innerRadius
				color: Theme.accentSubtle
				border.color: Theme.withAlpha(Theme.accent, 0.28)
				ModernIcon {
					anchors.centerIn: parent
					name: "screen-share"
					size: 18
					color: Theme.accent
				}
			}

			ColumnLayout {
				Layout.fillWidth: true
				Layout.minimumWidth: 0
				spacing: 2
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.backend.title
					color: Theme.textStrong
					font.weight: Font.DemiBold
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

			Rectangle {
				objectName: "screenShareStateBadge"
				Layout.alignment: Qt.AlignVCenter
				Layout.preferredWidth: stateBadgeLabel.implicitWidth + Theme.space3
				Layout.preferredHeight: 24
				radius: height / 2
				color: Theme.withAlpha(root.stateTone, 0.14)
				border.color: Theme.withAlpha(root.stateTone, 0.34)
				Label {
					id: stateBadgeLabel
					anchors.centerIn: parent
					textFormat: Text.PlainText
					text: root.stateLabel
					color: root.stateTone
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
				}
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
				width: Math.min(implicitWidth, root.maximumActionWidth)
				text: root.backend.paused ? qsTr("Resume live") : qsTr("Pause view")
				enabled: root.transportControlsEnabled
				highlighted: root.backend.paused
				tone: root.backend.paused ? "accent" : "neutral"
				Accessible.description: root.backend.paused
					? qsTr("Resume at the current live edge") : qsTr("Pause this view locally")
				onClicked: root.backend.setPaused(!root.backend.paused)
			}
			ModernButton {
				id: muteButton
				objectName: "screenShareMuteButton"
				width: Math.min(implicitWidth, root.maximumActionWidth)
				text: root.backend.audioMuted ? qsTr("Unmute audio") : qsTr("Mute audio")
				enabled: root.backend.audioAvailable && root.transportControlsEnabled
				highlighted: root.backend.audioMuted
				Accessible.description: root.backend.audioAvailable
					? qsTr("Change local audio playback for this share") : qsTr("This share has no audio track")
				onClicked: root.backend.setAudioMuted(!root.backend.audioMuted)
			}
			ModernSlider {
				id: volumeSlider
				objectName: "screenShareVolumeSlider"
				width: Math.min(180, Math.max(120, headerActions.width))
				from: 0
				to: 100
				value: root.backend.audioVolume
				enabled: root.backend.audioAvailable && root.transportControlsEnabled
				onMoved: root.backend.setAudioVolume(value)
				Accessible.name: qsTr("Stream volume")
				Accessible.description: qsTr("%1 percent").arg(Math.round(value))
			}
			ModernButton {
				id: stopButton
				objectName: "screenShareStopButton"
				width: Math.min(implicitWidth, root.maximumActionWidth)
				text: qsTr("Close viewer")
				tone: "danger"
				Accessible.description: qsTr("Stop receiving this screen share")
				onClicked: root.backend.requestStop()
			}
		}
	}
}
