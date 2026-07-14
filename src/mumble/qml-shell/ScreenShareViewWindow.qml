import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ScreenShare 1.0

ApplicationWindow {
    id: root
    required property var backend
	property bool hostClosing: false
	property bool closeRequestPending: false
	readonly property bool controlsWrapped: screenShareControls.controlsWrapped

	palette.window: Theme.shellBackground
	palette.active.base: Theme.surfaceRaised
	palette.inactive.base: Theme.surfaceRaised
	palette.alternateBase: Theme.panel
	palette.active.button: Theme.surfaceRaised
	palette.inactive.button: Theme.surfaceRaised
	palette.active.text: Theme.textMain
	palette.inactive.text: Theme.textMain
	palette.active.windowText: Theme.textMain
	palette.inactive.windowText: Theme.textMain
	palette.active.buttonText: Theme.textStrong
	palette.inactive.buttonText: Theme.textStrong
	palette.active.brightText: Theme.textStrong
	palette.inactive.brightText: Theme.textStrong
	palette.active.highlight: Theme.accent
	palette.inactive.highlight: Theme.accent
	palette.active.highlightedText: Theme.contrastText(Theme.accent)
	palette.inactive.highlightedText: Theme.contrastText(Theme.accent)
	palette.placeholderText: Theme.textMuted
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.surfaceRaised
	palette.inactive.toolTipBase: Theme.surfaceRaised
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.disabled.window: Theme.shellBackground
	palette.disabled.base: Theme.panel
	palette.disabled.alternateBase: Theme.panel
	palette.disabled.button: Theme.panel
	palette.disabled.text: Theme.textMuted
	palette.disabled.windowText: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
	palette.disabled.brightText: Theme.textMuted
	palette.disabled.highlight: Theme.surfaceBorder
	palette.disabled.highlightedText: Theme.textMuted
	palette.disabled.placeholderText: Theme.textMuted
	palette.disabled.light: Theme.surfaceBorder
	palette.disabled.midlight: Theme.panel
	palette.disabled.mid: Theme.divider
	palette.disabled.dark: Theme.rail
	palette.disabled.shadow: Theme.strip
	palette.disabled.link: Theme.textMuted
	palette.disabled.linkVisited: Theme.textMuted
	palette.disabled.toolTipBase: Theme.panel
	palette.disabled.toolTipText: Theme.textMuted
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
		ScreenShareControls {
			id: screenShareControls
			Layout.fillWidth: true
			backend: root.backend
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
				textFormat: Text.PlainText
                anchors.centerIn: parent
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textMuted
                visible: (!backend.nativeFrameActive && backend.videoWindow === null) || backend.paused
                text: backend.paused ? qsTr("Paused locally\n\nResume returns to the live edge.") : backend.status
            }
        }
        Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: backend.status; color: Theme.textMuted; elide: Text.ElideRight }
    }
}
