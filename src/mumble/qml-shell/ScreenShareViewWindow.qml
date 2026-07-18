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
	property bool hasShownLiveFrame: false
	readonly property bool narrowHeader: width < 760
	readonly property bool controlsWrapped: narrowHeader
	readonly property string surfaceId: "screenShare.viewer"
	readonly property string sharerLabel: String(backend.ownerLabel || "").trim()
	readonly property string roomLabel: String(backend.roomLabel || "").trim()
	readonly property string identityDetail: {
		const parts = []
		if (sharerLabel.length > 0)
			parts.push(qsTr("Shared by %1").arg(sharerLabel))
		if (roomLabel.length > 0)
			parts.push(qsTr("in %1").arg(roomLabel))
		return parts.length > 0 ? parts.join(" · ") : String(backend.detail || qsTr("Live viewer"))
	}
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property string operationStatus: String(backend.operationStatus || "idle")
	readonly property bool operationBusy: operationStatus === "loading"
	readonly property bool operationFailed: operationStatus === "error"
	readonly property string audioControlStatus: String(backend.audioControlStatus || "idle")
	readonly property string audioControlError: String(backend.audioControlError || "")
	readonly property bool audioControlNeedsAttention: audioControlStatus === "retrying"
		|| audioControlStatus === "error"
	readonly property bool transportControlsEnabled: !operationBusy && !operationFailed
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
		: displayState === "paused" ? qsTr("The receiver stays connected while this frame is frozen locally.")
		: String(backend.status || qsTr("The viewer is starting."))
	readonly property string headerStateLabel: operationFailed ? qsTr("Viewer unavailable")
		: operationBusy ? (hasShownLiveFrame ? qsTr("Reconnecting") : qsTr("Connecting"))
		: backend.paused ? qsTr("Paused locally") : qsTr("Live")
	readonly property color headerStateTone: operationFailed ? Theme.danger
		: operationBusy ? Theme.warning
		: backend.paused ? Theme.textMuted : Theme.success

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
	onPlaybackSurfaceReadyChanged: if (playbackSurfaceReady) hasShownLiveFrame = true
	onOperationFailedChanged: if (operationFailed) Qt.callLater(function() {
		if (screenShareFailureRetry.visible)
			screenShareFailureRetry.forceActiveFocus()
	})
	function focusInitialControl() {
		const candidates = operationFailed
			? [ screenShareFailureRetry, screenShareFailureClose ]
			: [ screenSharePauseButton, screenShareMuteButton, screenShareVolumeSlider,
				screenShareFullscreenButton, screenShareCloseButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const control = candidates[index]
			if (!control || !control.visible || !control.enabled)
				continue
			control.forceActiveFocus()
			return true
		}
		return false
	}
	function setFullscreen(enabled) {
		if (enabled)
			root.showFullScreen()
		else
			root.showNormal()
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
		onActivated: root.backend.requestStop()
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
			Layout.preferredHeight: {
				const metadataHeight = headerMetadata ? headerMetadata.implicitHeight : 0
				const actionsHeight = headerActions ? headerActions.implicitHeight : 0
				return Boolean(root && root.narrowHeader)
					? metadataHeight + actionsHeight + Theme.space2 + Theme.space2 * 2
					: Math.max(metadataHeight, actionsHeight) + Theme.space2 * 2
			}
			radius: 0
			color: Theme.withAlpha(Theme.chatSurface, 0.98)
			border.width: 0
			Accessible.role: Accessible.ToolBar
			Accessible.name: qsTr("Screen share viewer controls")
			Accessible.description: qsTr("%1, %2, %3")
				.arg(String(backend.title || qsTr("Live screen share")))
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
					id: headerMetadata
					objectName: "screenShareViewerMetadata"
					anchors.left: parent.left
					anchors.right: !parent ? undefined
						: (root && root.narrowHeader) ? parent.right
						: headerActions ? headerActions.left : parent.right
					anchors.rightMargin: root && root.narrowHeader ? 0 : Theme.space4
					anchors.top: parent.top
					height: implicitHeight
					spacing: Theme.space2
					Accessible.ignored: true

					Rectangle {
						Layout.preferredWidth: 30
						Layout.preferredHeight: 30
						Layout.alignment: Qt.AlignVCenter
						radius: height / 2
						color: Theme.withAlpha(Theme.accent, 0.12)
						border.width: 0
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
							text: String(backend.title || qsTr("Live screen share"))
							color: Theme.textStrong
							font.weight: Font.DemiBold
							font.pixelSize: Theme.fontLabel
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: root.identityDetail
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}

					Rectangle {
						id: headerStateBadge
						objectName: "screenShareStateBadge"
						Layout.alignment: Qt.AlignVCenter
						Layout.preferredWidth: headerStateBadgeLabel.implicitWidth + Theme.space3
						Layout.preferredHeight: 24
						radius: height / 2
						color: Theme.withAlpha(root.headerStateTone, 0.14)
						border.color: Theme.withAlpha(root.headerStateTone, 0.34)
						Accessible.role: Accessible.StaticText
						Accessible.name: root.headerStateLabel
						Label {
							id: headerStateBadgeLabel
							anchors.centerIn: parent
							textFormat: Text.PlainText
							text: root.headerStateLabel
							color: root.headerStateTone
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
							Accessible.ignored: true
						}
					}
				}

				Item {
					id: headerActions
					objectName: "screenShareViewerHeaderActions"
					anchors.left: root && root.narrowHeader && parent ? parent.left : undefined
					anchors.right: parent ? parent.right : undefined
					anchors.top: !parent ? undefined
						: (root && root.narrowHeader) && headerMetadata ? headerMetadata.bottom : parent.top
					anchors.topMargin: root && root.narrowHeader ? Theme.space2 : 0
					anchors.verticalCenter: root && root.narrowHeader || !parent
						? undefined : parent.verticalCenter
					width: !parent ? 0
						: root && root.narrowHeader ? parent.width
						: headerActionRow ? headerActionRow.implicitWidth : 0
					height: headerActionRow ? headerActionRow.implicitHeight : 0
					implicitWidth: headerActionRow ? headerActionRow.implicitWidth : 0
					implicitHeight: headerActionRow ? headerActionRow.implicitHeight : 0

					RowLayout {
						id: headerActionRow
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						spacing: Theme.space1

						ModernIconButton {
							id: screenSharePauseButton
							objectName: "screenSharePauseButton"
							visible: !root.operationBusy && !root.operationFailed
							enabled: root.transportControlsEnabled
							dense: true
							text: root.backend && root.backend.paused ? qsTr("Resume") : qsTr("Pause")
							iconName: root.backend && root.backend.paused ? "play" : "pause"
							overlay: true
							selected: Boolean(root.backend && root.backend.paused)
							tone: root.backend && root.backend.paused ? "accent" : "neutral"
							Accessible.description: root.backend && root.backend.paused
								? qsTr("Resume immediately without reconnecting the receiver")
								: qsTr("Pause this stream locally without closing the viewer")
							onClicked: if (root.backend) root.backend.setPaused(!root.backend.paused)
						}

						ModernIconButton {
							id: screenShareMuteButton
							objectName: "screenShareMuteButton"
							visible: !root.operationBusy && !root.operationFailed
							enabled: backend.audioAvailable && root.transportControlsEnabled
							dense: true
							text: backend.audioMuted ? qsTr("Unmute") : qsTr("Mute")
							iconName: backend.audioMuted ? "volume-off" : "volume"
							overlay: true
							selected: backend.audioMuted
							Accessible.description: backend.audioAvailable
								? (root.audioControlNeedsAttention && root.audioControlError.length > 0
									? root.audioControlError : qsTr("Change local audio playback for this share"))
								: qsTr("This share has no audio track")
							onClicked: backend.setAudioMuted(!backend.audioMuted)
						}

						ModernSlider {
							id: screenShareVolumeSlider
							objectName: "screenShareVolumeSlider"
							Layout.preferredWidth: root && root.narrowHeader ? 96 : 112
							visible: !root.operationBusy && !root.operationFailed
							from: 0
							to: 100
							value: backend.audioVolume
							enabled: backend.audioAvailable && root.transportControlsEnabled
							onMoved: backend.setAudioVolume(value)
							Accessible.name: qsTr("Stream volume")
							Accessible.description: qsTr("%1 percent").arg(Math.round(value))
						}

						ModernIconButton {
							id: screenShareFullscreenButton
							objectName: "screenShareFullscreenButton"
							visible: !root.operationBusy && !root.operationFailed
							text: root.visibility === Window.FullScreen
								? qsTr("Exit full screen") : qsTr("Enter full screen")
							iconName: root.visibility === Window.FullScreen ? "fullscreen-exit" : "fullscreen"
							overlay: true
							dense: true
							selected: root.visibility === Window.FullScreen
							Accessible.name: root.visibility === Window.FullScreen
								? qsTr("Exit full screen") : qsTr("Enter full screen")
							onClicked: root.setFullscreen(root.visibility !== Window.FullScreen)
						}

						ModernIconButton {
							id: screenShareCloseButton
							objectName: "screenShareCloseButton"
							visible: !root.operationFailed
							dense: true
							text: qsTr("Close viewer")
							iconName: "close"
							overlay: true
							selected: activeFocus
							tone: "danger"
							Accessible.description: qsTr("Stop receiving this screen share")
							onClicked: backend.requestStop()
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
				backend: root.backend
				visible: (root.displayState === "active" || root.displayState === "paused")
					&& root.nativeSurfaceReady
				Accessible.ignored: !visible
				Accessible.role: Accessible.Graphic
				Accessible.name: qsTr("Live shared screen frame")
				Accessible.description: qsTr("Decoded video from the active screen share")
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
						objectName: "screenShareLiveBadgeLabel"
						textFormat: Text.PlainText
						text: qsTr("LIVE")
						color: Theme.mediaOverlayTextStrong
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
				color: Theme.withAlpha(Theme.mediaCanvas, root.displayState === "paused" ? 0.38 : 0.96)
				Accessible.role: root.displayState === "error" ? Accessible.AlertMessage : Accessible.Pane
				Accessible.name: root.stateHeading
				Accessible.description: root.stateDetail
				z: 4

				Rectangle {
					id: pausedStateCard
					objectName: "screenSharePausedStateCard"
					anchors.centerIn: parent
					width: stateContent ? stateContent.width + Theme.space6 * 2 : 0
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
					width: Math.min(parent.width - Theme.space6 * 2, 520)
					spacing: Theme.space3

					ModernBusyIndicator {
						id: screenShareBusy
						objectName: "screenShareBusyIndicator"
						Layout.alignment: Qt.AlignHCenter
						visible: root.displayState === "loading" || root.displayState === "reconnecting"
						running: visible
						animated: !root.visualFixtureMode
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
						objectName: "screenShareStateHeading"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateHeading
						color: Theme.mediaOverlayTextStrong
						font.pixelSize: Theme.fontHeading
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					Label {
						objectName: "screenShareStateDetail"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: root.stateDetail
						color: Theme.mediaOverlayTextMuted
						font.pixelSize: Theme.fontBody
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					RowLayout {
						Layout.alignment: Qt.AlignHCenter
						visible: root.displayState === "error" || root.displayState === "paused"
						spacing: Theme.space2
						ModernButton {
							id: screenSharePausedResume
							objectName: "screenSharePausedResumeButton"
							visible: root.displayState === "paused"
							text: qsTr("Resume live share")
							tone: "accent"
							highlighted: true
							Accessible.description: qsTr("Resume immediately; the receiver remained connected")
							onClicked: root.backend.setPaused(false)
						}
						ModernButton {
							id: screenShareFailureRetry
							objectName: "screenShareFailureRetryButton"
							visible: root.displayState === "error"
							text: qsTr("Retry viewer")
							tone: "accent"
							highlighted: true
							Accessible.description: qsTr("Clean up the failed helper and start a fresh screen-share viewer")
							onClicked: root.backend.requestRetry()
						}
						ModernButton {
							id: screenShareFailureClose
							objectName: "screenShareFailureCloseButton"
							visible: root.displayState === "error"
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
			Layout.row: 2
			Layout.column: 0
			Layout.fillWidth: true
			Layout.leftMargin: Theme.space3
			Layout.rightMargin: Theme.space3
			Layout.topMargin: Theme.space1
			Layout.bottomMargin: Theme.space1
			visible: root.playbackSurfaceReady && !root.operationBusy && !root.operationFailed
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
				objectName: "screenShareAudioControlStatus"
				Layout.maximumWidth: Math.max(180, root.width * 0.34)
				visible: root.audioControlNeedsAttention && root.audioControlError.length > 0
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
			Label {
				textFormat: Text.PlainText
				text: root.nativeSurfaceReady ? qsTr("In-app video") : qsTr("System video")
				color: Theme.textFaint
				font.pixelSize: Theme.fontCaption
				visible: root.playbackSurfaceReady
			}
		}
    }
}
