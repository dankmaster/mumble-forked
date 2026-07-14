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
	property bool hasShownLiveFrame: false
	readonly property bool controlsWrapped: screenShareControls.controlsWrapped
	readonly property string surfaceId: "screenShare.viewer"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property string operationStatus: String(backend.operationStatus || "idle")
	readonly property bool operationBusy: operationStatus === "loading"
	readonly property bool operationFailed: operationStatus === "error"
	readonly property bool nativeSurfaceReady: Boolean(backend.nativeFrameActive && backend.hasCurrentFrame)
	readonly property bool externalSurfaceReady: backend.videoWindow !== null
	readonly property bool playbackSurfaceReady: nativeSurfaceReady || externalSurfaceReady
	readonly property string displayState: operationFailed ? "error"
		: backend.paused ? "paused"
		: operationBusy ? (hasShownLiveFrame ? "reconnecting" : "loading")
		: playbackSurfaceReady ? "active" : "empty"
	readonly property string stateHeading: displayState === "error" ? qsTr("Screen share unavailable")
		: displayState === "paused" ? qsTr("Paused locally")
		: displayState === "reconnecting" ? qsTr("Reconnecting to the live share")
		: displayState === "loading" ? qsTr("Connecting to the live share")
		: qsTr("Waiting for the first frame")
	readonly property string stateDetail: displayState === "error"
		? String(backend.operationError || backend.status || qsTr("The viewer could not be started."))
		: displayState === "paused" ? qsTr("Resume returns to the current live edge.")
		: String(backend.status || qsTr("The viewer is starting."))

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
	Component.onCompleted: Qt.callLater(screenShareControls.focusInitialControl)
	onPlaybackSurfaceReadyChanged: if (playbackSurfaceReady) hasShownLiveFrame = true
	onOperationFailedChanged: if (operationFailed) Qt.callLater(function() {
		if (screenShareFailureRetry.visible)
			screenShareFailureRetry.forceActiveFocus()
	})
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
		anchors.margins: Theme.space3
		spacing: Theme.space3

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: screenShareControls.implicitHeight + Theme.space4
			radius: Theme.innerRadius
			color: Theme.chatSurface
			border.color: Theme.surfaceBorder
			border.width: 1
			ScreenShareControls {
				id: screenShareControls
				anchors.fill: parent
				anchors.margins: Theme.space3
				backend: root.backend
			}
		}

		Rectangle {
			id: viewerCanvas
			objectName: "screenShareCanvas"
			Layout.fillWidth: true
			Layout.fillHeight: true
			color: Theme.mediaCanvas
			border.color: root.displayState === "error" ? Theme.withAlpha(Theme.danger, 0.55) : Theme.surfaceBorder
			radius: Theme.innerRadius
			clip: true

			ScreenShareVideoItem {
				anchors.fill: parent
				backend: root.backend
				visible: root.displayState === "active" && root.nativeSurfaceReady
			}
			WindowContainer {
				anchors.fill: parent
				anchors.margins: 1
				window: backend.videoWindow
				visible: root.displayState === "active" && !root.nativeSurfaceReady && window !== null
			}

			Rectangle {
				objectName: "screenShareLiveBadge"
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.margins: Theme.space3
				width: liveBadgeRow.implicitWidth + Theme.space3
				height: 26
				radius: height / 2
				visible: root.displayState === "active"
				color: Theme.withAlpha(Theme.mediaCanvas, 0.78)
				border.color: Theme.withAlpha(Theme.success, 0.55)
				z: 3
				Row {
					id: liveBadgeRow
					anchors.centerIn: parent
					spacing: Theme.space1
					Rectangle {
						anchors.verticalCenter: parent.verticalCenter
						width: 7
						height: 7
						radius: width / 2
						color: Theme.success
					}
					Label {
						textFormat: Text.PlainText
						text: qsTr("LIVE")
						color: Theme.textStrong
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
					}
				}
			}

			Rectangle {
				id: stateSurface
				objectName: "screenShareStateSurface"
				anchors.fill: parent
				visible: root.displayState !== "active"
				color: Theme.withAlpha(Theme.mediaCanvas, 0.96)
				Accessible.role: root.displayState === "error" ? Accessible.AlertMessage : Accessible.Pane
				Accessible.name: root.stateHeading
				Accessible.description: root.stateDetail
				z: 4

				ColumnLayout {
					anchors.centerIn: parent
					width: Math.min(parent.width - Theme.space6 * 2, 520)
					spacing: Theme.space3

					ModernBusyIndicator {
						id: screenShareBusy
						objectName: "screenShareBusyIndicator"
						Layout.alignment: Qt.AlignHCenter
						visible: root.displayState === "loading" || root.displayState === "reconnecting"
						running: visible
						Accessible.name: root.stateHeading
					}
					Rectangle {
						Layout.alignment: Qt.AlignHCenter
						Layout.preferredWidth: 52
						Layout.preferredHeight: 52
						visible: !screenShareBusy.visible
						radius: Theme.innerRadius
						color: root.displayState === "error"
							? Theme.withAlpha(Theme.danger, 0.15) : Theme.accentSubtle
						ModernIcon {
							anchors.centerIn: parent
							name: root.displayState === "error" ? "warning" : "screen-share"
							size: 24
							color: root.displayState === "error" ? Theme.danger : Theme.accent
						}
					}
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateHeading
						color: Theme.textStrong
						font.pixelSize: Theme.fontHeading
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateDetail
						color: Theme.textMuted
						font.pixelSize: Theme.fontBody
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					RowLayout {
						Layout.alignment: Qt.AlignHCenter
						visible: root.displayState === "error"
						spacing: Theme.space2
						ModernButton {
							id: screenShareFailureRetry
							objectName: "screenShareFailureRetryButton"
							text: qsTr("Retry viewer")
							tone: "accent"
							highlighted: true
							Accessible.description: qsTr("Clean up the failed helper and start a fresh screen-share viewer")
							onClicked: root.backend.requestRetry()
						}
						ModernButton {
							id: screenShareFailureClose
							objectName: "screenShareFailureCloseButton"
							text: qsTr("Close viewer")
							tone: "danger"
							Accessible.description: qsTr("Stop receiving this unavailable screen share")
							onClicked: root.backend.requestStop()
						}
					}
				}
			}
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: backend.status
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
			}
			Label {
				textFormat: Text.PlainText
				text: root.nativeSurfaceReady ? qsTr("Native Qt video") : qsTr("Windows viewer")
				color: Theme.textFaint
				font.pixelSize: Theme.fontCaption
				visible: root.playbackSurfaceReady
			}
		}
    }
}
