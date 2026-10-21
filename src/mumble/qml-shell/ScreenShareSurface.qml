import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ScreenShare 1.0

Item {
	id: root
	required property var backend
	property bool renderActive: true
	property bool fullscreen: false
	property bool visualFixtureMode: false
	property bool focusRecoveryAutomatically: false
	readonly property var share: backend || ({})
	signal fullscreenRequested(bool enabled)
	implicitWidth: 640
	implicitHeight: 360
	enabled: Boolean(backend)
	clip: true
	property bool hasShownLiveFrame: false
	readonly property bool narrowHeader: width < 560
	readonly property bool controlsWrapped: false
	readonly property string surfaceId: "screenShare.viewer"
	readonly property string sharerLabel: String(share.ownerLabel || "").trim()
	readonly property string roomLabel: String(share.roomLabel || "").trim()
	readonly property string identityDetail: {
		const parts = []
		if (sharerLabel.length > 0)
			parts.push(qsTr("Shared by %1").arg(sharerLabel))
		if (roomLabel.length > 0)
			parts.push(qsTr("in %1").arg(roomLabel))
		return parts.length > 0 ? parts.join(" · ") : String(share.detail || qsTr("Live viewer"))
	}
	readonly property string compactIdentityLabel: {
		const parts = []
		if (sharerLabel.length > 0)
			parts.push(sharerLabel)
		if (roomLabel.length > 0)
			parts.push(roomLabel)
		return parts.length > 0
			? parts.join(" · ") : String(share.title || qsTr("Live screen share"))
	}
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property string operationStatus: String(share.operationStatus || "idle")
	readonly property bool operationBusy: operationStatus === "loading"
	readonly property bool operationFailed: operationStatus === "error"
	readonly property bool operationStalled: operationStatus === "stalled"
	readonly property bool recoveryAvailable: displayState === "error" || displayState === "stalled"
	readonly property string audioControlStatus: String(share.audioControlStatus || "idle")
	readonly property string audioControlError: String(share.audioControlError || "")
	readonly property bool audioControlNeedsAttention: audioControlStatus === "retrying"
		|| audioControlStatus === "error"
	readonly property bool transportControlsEnabled: !operationBusy && !operationFailed
	readonly property bool nativeSurfaceReady: Boolean(share.nativeFrameActive && share.hasCurrentFrame)
	readonly property bool externalSurfaceReady: Boolean(share.videoWindow)
	readonly property bool playbackSurfaceReady: nativeSurfaceReady || externalSurfaceReady
	readonly property string displayState: operationFailed ? "error"
		: share.paused ? "paused"
		: operationStalled ? "stalled"
		: operationBusy ? (hasShownLiveFrame ? "reconnecting" : "loading")
		: playbackSurfaceReady ? "active" : "empty"
	readonly property string stateHeading: displayState === "error" ? qsTr("Screen share unavailable")
		: displayState === "stalled" ? qsTr("The screen share stopped updating")
		: displayState === "paused" ? qsTr("Paused locally")
		: displayState === "reconnecting" ? qsTr("Reconnecting to the live share")
		: displayState === "loading" ? qsTr("Connecting to the live share")
		: qsTr("Waiting for the first frame")
	readonly property string stateDetail: recoveryAvailable
		? String(share.operationError || share.status || qsTr("The viewer could not be started."))
		: displayState === "paused" ? qsTr("The receiver stays connected while this frame is frozen locally.")
		: String(share.status || qsTr("The viewer is starting."))
	readonly property string headerStateLabel: operationFailed ? qsTr("Viewer unavailable")
		: operationStalled ? qsTr("Waiting for video")
		: operationBusy ? (hasShownLiveFrame ? qsTr("Reconnecting") : qsTr("Connecting"))
		: share.paused ? qsTr("Paused locally")
		: playbackSurfaceReady ? qsTr("Live") : qsTr("Waiting for first frame")

	onPlaybackSurfaceReadyChanged: if (playbackSurfaceReady) hasShownLiveFrame = true
	onRecoveryAvailableChanged: if (focusRecoveryAutomatically && recoveryAvailable) Qt.callLater(function() {
		if (root.renderActive && screenShareFailureRetry.visible)
			screenShareFailureRetry.forceActiveFocus()
	})
	function focusInitialControl() {
		const candidates = recoveryAvailable
			? [ screenShareFailureRetry, screenShareFailureClose ]
			: operationBusy ? [ screenShareCloseButton, screenSharePresentationButton, screenShareFullscreenButton ]
			: [ screenSharePauseButton, screenShareMuteButton, screenShareVolumeSlider,
				screenSharePresentationButton, screenShareFullscreenButton, screenShareCloseButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const control = candidates[index]
			if (!control || !control.visible || !control.enabled)
				continue
			control.forceActiveFocus()
			return true
		}
		return false
	}

    GridLayout {
        anchors.fill: parent
		columns: 1
		rowSpacing: 0
		columnSpacing: 0

		Rectangle {
			id: viewerHeader
			objectName: "screenShareViewerHeader"
			Layout.row: 1
			Layout.column: 0
			Layout.fillWidth: true
			Layout.preferredHeight: visible
				? Math.max(headerMetadata.implicitHeight, headerActions.implicitHeight) + Theme.space2 * 2
				: 0
			Layout.minimumHeight: 0
			Layout.maximumHeight: visible ? Number.POSITIVE_INFINITY : 0
			visible: true
			radius: 0
			color: Theme.withAlpha(Theme.chatSurface, 0.98)
			border.width: 0
			Accessible.role: Accessible.ToolBar
			Accessible.name: qsTr("Screen share viewer controls")
			Accessible.description: qsTr("%1, %2, %3")
				.arg(String(root.share.title || qsTr("Live screen share")))
				.arg(root.identityDetail).arg(root.headerStateLabel)

			Item {
				id: viewerHeaderContent
				anchors.fill: parent
				anchors.leftMargin: Theme.space3
				anchors.rightMargin: Theme.space3
				anchors.topMargin: Theme.space2
				anchors.bottomMargin: Theme.space2

				Rectangle {
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.topMargin: -Theme.space2
					height: 1
					color: Theme.divider
					Accessible.ignored: true
				}

				RowLayout {
					anchors.fill: parent
					spacing: Theme.space3

					Label {
						id: headerMetadata
						objectName: "screenShareViewerMetadata"
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						Layout.alignment: Qt.AlignVCenter
						textFormat: Text.PlainText
						visible: root.width >= 360
						text: root.compactIdentityLabel
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
						Accessible.ignored: true
					}

					Item {
						id: headerActions
						objectName: "screenShareViewerHeaderActions"
						Layout.alignment: Qt.AlignVCenter
						Layout.preferredWidth: headerActionRow.implicitWidth
						Layout.preferredHeight: headerActionRow.implicitHeight
						implicitWidth: headerActionRow.implicitWidth
						implicitHeight: headerActionRow.implicitHeight

						RowLayout {
							id: headerActionRow
							anchors.fill: parent
							spacing: Theme.space1

							ModernIconButton {
								id: screenSharePauseButton
								objectName: "screenSharePauseButton"
								visible: !root.operationBusy && !root.operationFailed
								enabled: root.transportControlsEnabled
								dense: true
								text: root.backend && root.share.paused ? qsTr("Resume") : qsTr("Pause")
								iconName: root.backend && root.share.paused ? "play" : "pause"
								selected: Boolean(root.backend && root.share.paused)
								tone: root.backend && root.share.paused ? "accent" : "neutral"
								Accessible.description: root.backend && root.share.paused
									? qsTr("Resume immediately without reconnecting the receiver")
									: qsTr("Pause this stream locally without closing the viewer")
								onClicked: if (root.backend) root.share.setPaused(!root.share.paused)
							}

							ModernIconButton {
								id: screenShareMuteButton
								objectName: "screenShareMuteButton"
								visible: Boolean(root.share.audioAvailable)
									&& !root.operationBusy && !root.operationFailed
								enabled: root.transportControlsEnabled
								dense: true
								text: root.share.audioMuted ? qsTr("Unmute") : qsTr("Mute")
								iconName: root.share.audioMuted ? "volume-off" : "volume"
								selected: Boolean(root.share.audioMuted)
								Accessible.description: root.audioControlNeedsAttention
									&& root.audioControlError.length > 0
									? root.audioControlError
									: qsTr("Change local audio playback for this share")
								onClicked: root.share.setAudioMuted(!root.share.audioMuted)
							}

							ModernSlider {
								id: screenShareVolumeSlider
								objectName: "screenShareVolumeSlider"
								Layout.preferredWidth: 104
								visible: !root.narrowHeader && Boolean(root.share.audioAvailable)
									&& !root.operationBusy && !root.operationFailed
								from: 0
								to: 100
								value: Number(root.share.audioVolume || 0)
								enabled: root.transportControlsEnabled
								onMoved: root.share.setAudioVolume(value)
								Accessible.name: qsTr("Stream volume")
								Accessible.description: qsTr("%1 percent").arg(Math.round(value))
							}

							ModernIconButton {
								id: screenSharePresentationButton
								objectName: "screenSharePresentationButton"
								dense: true
								text: root.share.detached ? qsTr("Return to channel") : qsTr("Open in window")
								iconName: root.share.detached ? "message" : "external"
								Accessible.description: qsTr("Move the viewer without reconnecting the share")
								onClicked: {
									if (root.share.detached) root.backend.requestDock()
									else root.backend.requestDetach()
								}
							}

							ModernIconButton {
								id: screenShareFullscreenButton
								objectName: "screenShareFullscreenButton"
								visible: true
								text: root.fullscreen
									? qsTr("Exit full screen") : qsTr("Enter full screen")
								iconName: root.fullscreen ? "fullscreen-exit" : "fullscreen"
								dense: true
								selected: root.fullscreen
								Accessible.name: root.fullscreen
									? qsTr("Exit full screen") : qsTr("Enter full screen")
								onClicked: root.fullscreenRequested(!root.fullscreen)
							}

							ModernIconButton {
								id: screenShareCloseButton
								objectName: "screenShareCloseButton"
								visible: true
								dense: true
								text: root.operationBusy ? qsTr("Cancel viewer") : qsTr("Stop viewing")
								iconName: "close"
								selected: activeFocus
								tone: "danger"
								Accessible.description: qsTr("Stop receiving this screen share")
								onClicked: root.share.requestStop()
							}
						}
					}
				}
			}
		}

		Rectangle {
			id: viewerCanvas
			objectName: "screenShareCanvas"
			Layout.row: 0
			Layout.column: 0
			Layout.fillWidth: true
			Layout.fillHeight: true
			color: Theme.mediaCanvas
			border.color: root.displayState === "error" ? Theme.withAlpha(Theme.danger, 0.55) : "transparent"
			border.width: root.displayState === "error" ? 1 : 0
			radius: 0
			clip: true

			ScreenShareVideoItem {
				id: nativeVideoFrame
				objectName: "screenShareNativeVideoFrame"
				anchors.fill: parent
				backend: root.renderActive ? root.backend : null
				visible: root.renderActive
					&& (root.displayState === "active" || root.displayState === "paused" || root.operationStalled)
					&& root.nativeSurfaceReady
				Accessible.ignored: !visible
				Accessible.role: Accessible.Graphic
				Accessible.name: qsTr("Live shared screen frame")
				Accessible.description: qsTr("Decoded video from the active screen share")
			}
			WindowContainer {
				anchors.fill: parent
				anchors.margins: 1
				objectName: "screenShareExternalVideoFrame"
				window: root.renderActive ? (root.share.videoWindow || null) : null
				visible: root.renderActive && root.displayState === "active" && !root.nativeSurfaceReady && window !== null
			}

			Rectangle {
				id: stateSurface
				objectName: "screenShareStateSurface"
				anchors.fill: parent
				visible: root.displayState !== "active"
				color: Theme.withAlpha(Theme.mediaCanvas, root.displayState === "paused" ? 0.38 : 0.96)
				Accessible.role: root.recoveryAvailable ? Accessible.AlertMessage : Accessible.Pane
				Accessible.name: root.stateHeading
				Accessible.description: root.stateDetail
				z: 4

				Rectangle {
					id: pausedStateCard
					objectName: "screenSharePausedStateCard"
					anchors.centerIn: parent
					width: stateContent ? Math.min(parent.width - Theme.space2 * 2, stateContent.width + Theme.space6 * 2) : 0
					height: stateContent ? stateContent.implicitHeight + Theme.space5 * 2 : 0
					visible: root.displayState === "paused"
					radius: Theme.innerRadius
					color: Theme.withAlpha(Theme.mediaCanvas, 0.88)
					border.color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.18)
					border.width: 1
				}

				ColumnLayout {
					id: stateContent
					anchors.centerIn: parent
					width: Math.max(0, Math.min(parent.width - Theme.space3 * 2, 520))
					spacing: viewerCanvas.height < 230 ? Theme.space1 : Theme.space3

					ModernBusyIndicator {
						id: screenShareBusy
						objectName: "screenShareBusyIndicator"
						Layout.alignment: Qt.AlignHCenter
						visible: (root.displayState === "loading" || root.displayState === "reconnecting")
							&& viewerCanvas.height >= 180
						running: visible
						animated: !root.visualFixtureMode
						Accessible.name: root.stateHeading
					}
					Rectangle {
						Layout.alignment: Qt.AlignHCenter
						Layout.preferredWidth: 52
						Layout.preferredHeight: 52
						visible: root.recoveryAvailable && viewerCanvas.height >= 230
						radius: Theme.innerRadius
						color: Theme.withAlpha(Theme.danger, 0.15)
						ModernIcon {
							anchors.centerIn: parent
							name: "warning"
							size: 24
							color: Theme.danger
						}
					}
					Label {
						objectName: "screenShareStateHeading"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateHeading
						color: Theme.mediaOverlayTextStrong
						font.pixelSize: viewerCanvas.height < 230 ? Theme.fontBody : Theme.fontHeading
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					Label {
						objectName: "screenShareStateDetail"
						visible: viewerCanvas.height >= 150
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateDetail
						maximumLineCount: viewerCanvas.height < 230 ? 2 : 4
						elide: Text.ElideRight
						color: Theme.mediaOverlayTextMuted
						font.pixelSize: Theme.fontBody
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					RowLayout {
						Layout.alignment: Qt.AlignHCenter
						visible: root.recoveryAvailable || root.displayState === "paused"
						spacing: Theme.space2
						ModernButton {
							id: screenSharePausedResume
							objectName: "screenSharePausedResumeButton"
							visible: root.displayState === "paused"
							text: qsTr("Resume live share")
							tone: "accent"
							highlighted: true
							Accessible.description: qsTr("Resume immediately; the receiver remained connected")
							onClicked: root.share.setPaused(false)
						}
						ModernButton {
							id: screenShareFailureRetry
							objectName: "screenShareFailureRetryButton"
							visible: root.recoveryAvailable
							text: qsTr("Retry viewer")
							tone: "accent"
							highlighted: true
							Accessible.description: qsTr("Clean up the failed helper and start a fresh screen-share viewer")
							onClicked: root.share.requestRetry()
						}
						ModernButton {
							id: screenShareFailureClose
							objectName: "screenShareFailureCloseButton"
							visible: root.recoveryAvailable
							text: qsTr("Stop viewing")
							tone: "danger"
							Accessible.description: qsTr("Stop receiving this unavailable screen share")
							onClicked: root.share.requestStop()
						}
					}
				}
			}
		}

		Label {
			objectName: "screenShareAudioControlStatus"
			Layout.row: 2
			Layout.column: 0
			Layout.fillWidth: true
			Layout.leftMargin: Theme.space3
			Layout.rightMargin: Theme.space3
			Layout.topMargin: Theme.space1
			Layout.bottomMargin: Theme.space1
			visible: !root.fullscreen && root.playbackSurfaceReady
				&& !root.operationBusy && !root.operationFailed
				&& root.audioControlNeedsAttention && root.audioControlError.length > 0
			textFormat: Text.PlainText
			text: root.audioControlError
			color: root.audioControlStatus === "error" ? Theme.danger : Theme.warning
			font.pixelSize: Theme.fontCaption
			elide: Text.ElideRight
			Accessible.role: root.audioControlStatus === "error"
				? Accessible.AlertMessage : Accessible.StaticText
			Accessible.name: root.audioControlStatus === "error"
				? qsTr("Viewer audio control failed") : qsTr("Connecting viewer audio controls")
			Accessible.description: text
		}
    }
}
