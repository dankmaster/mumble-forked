import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ScreenShare 1.0

ApplicationWindow {
    id: root
	required property var backend
	property bool visualFixtureMode: false
	property bool hostClosing: false
	property bool closeRequestPending: false
	property alias hasShownLiveFrame: viewerSurface.hasShownLiveFrame
	readonly property bool fullscreen: visibility === Window.FullScreen
	readonly property string surfaceId: "screenShare.viewer"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property bool narrowHeader: viewerSurface.narrowHeader
	readonly property bool controlsWrapped: viewerSurface.controlsWrapped
	readonly property string sharerLabel: viewerSurface.sharerLabel
	readonly property string roomLabel: viewerSurface.roomLabel
	readonly property string identityDetail: viewerSurface.identityDetail
	readonly property string compactIdentityLabel: viewerSurface.compactIdentityLabel
	readonly property string operationStatus: viewerSurface.operationStatus
	readonly property bool operationBusy: viewerSurface.operationBusy
	readonly property bool operationFailed: viewerSurface.operationFailed
	readonly property string audioControlStatus: viewerSurface.audioControlStatus
	readonly property string audioControlError: viewerSurface.audioControlError
	readonly property bool audioControlNeedsAttention: viewerSurface.audioControlNeedsAttention
	readonly property bool transportControlsEnabled: viewerSurface.transportControlsEnabled
	readonly property bool nativeSurfaceReady: viewerSurface.nativeSurfaceReady
	readonly property bool externalSurfaceReady: viewerSurface.externalSurfaceReady
	readonly property bool playbackSurfaceReady: viewerSurface.playbackSurfaceReady
	readonly property string displayState: viewerSurface.displayState
	readonly property string stateHeading: viewerSurface.stateHeading
	readonly property string stateDetail: viewerSurface.stateDetail
	readonly property string headerStateLabel: viewerSurface.headerStateLabel

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
	title: qsTr("Mumble Screen Share — %1").arg(root.identityDetail)
    color: Theme.shellBackground
	Component.onCompleted: Qt.callLater(focusInitialControl)
	function focusInitialControl() {
		return viewerSurface.focusInitialControl()
	}
	function setFullscreen(enabled) {
		if (enabled)
			root.showFullScreen()
		else {
			root.showNormal()
			Qt.callLater(focusInitialControl)
		}
	}
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

	Shortcut {
		sequence: "F11"
		context: Qt.WindowShortcut
		onActivated: root.setFullscreen(root.visibility !== Window.FullScreen)
	}

	Shortcut {
		sequence: "Escape"
		context: Qt.WindowShortcut
		enabled: root.visibility === Window.FullScreen
		onActivated: root.setFullscreen(false)
	}

	Shortcut {
		objectName: "screenShareCloseShortcut"
		sequences: [ StandardKey.Close ]
		context: Qt.WindowShortcut
		onActivated: root.backend.requestClose()
	}


	ScreenShareSurface {
		id: viewerSurface
		objectName: "screenShareViewerSurface"
		anchors.fill: parent
		backend: root.backend
		renderActive: !root.hostClosing && Boolean(root.backend && root.backend.detached)
		fullscreen: root.fullscreen
		visualFixtureMode: root.visualFixtureMode
		focusRecoveryAutomatically: true
		onFullscreenRequested: enabled => root.setFullscreen(enabled)
	}
}
