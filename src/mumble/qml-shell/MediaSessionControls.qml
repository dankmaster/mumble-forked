import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: root

	required property var session
	property bool fullscreen: false
	property bool fullscreenAvailable: true
	property bool externalAvailable: false
	readonly property bool sharedPlayback: Boolean(session.sharedAvailable && session.sharedJoined)
	readonly property bool canControl: session.playbackControlAllowed !== undefined
		? Boolean(session.playbackControlAllowed)
		: Boolean(session.playbackControllable && (!session.sharedAvailable || session.sharedHost))
	readonly property bool providerControlled: !Boolean(session.playbackControllable)
	readonly property bool compactControls: width < 720
	readonly property bool controlsWrapped: transportActions.childrenRect.height > Theme.controlHeight
	readonly property bool externalActionAvailable: externalAvailable
		&& (session.state === "error" || providerControlled)
	readonly property bool confirmationVisible: closeDialog.opened
	readonly property bool compactVolumeVisible: compactControls
	readonly property bool volumePopupVisible: volumePopup.opened

	signal fullscreenRequested(bool fullscreen)
	signal externalRequested()
	signal exitConfirmed(string disposition)

	function formatTime(seconds) {
		const safe = Math.max(0, Math.floor(Number(seconds) || 0))
		const hours = Math.floor(safe / 3600)
		const minutes = Math.floor((safe % 3600) / 60)
		const remainder = safe % 60
		const paddedMinutes = hours > 0 && minutes < 10 ? "0" + minutes : String(minutes)
		const paddedSeconds = remainder < 10 ? "0" + remainder : String(remainder)
		return hours > 0 ? hours + ":" + paddedMinutes + ":" + paddedSeconds
			: minutes + ":" + paddedSeconds
	}

	function stateLabel() {
		if (session.state === "loading")
			return session.loadProgress > 0
				? qsTr("Loading · %1%").arg(session.loadProgress)
				: qsTr("Loading")
		if (session.state === "error") return qsTr("Playback unavailable")
		if (root.providerControlled) return qsTr("Provider controls")
		if (session.sharedHost) return qsTr("Hosting for %1").arg(session.sharedParticipantCount)
		if (session.sharedJoined) return qsTr("Synchronized with host")
		return session.state === "playing" ? qsTr("Playing") : qsTr("Paused")
	}

	function requestClose() {
		if (sharedPlayback) {
			closeDialog.open()
			return false
		}
		exitConfirmed("close-player")
		return true
	}

	function confirmClosePlayer() {
		closeDialog.close()
		exitConfirmed("close-player")
	}

	function confirmSharedExit() {
		const disposition = session.sharedHost ? "end-shared" : "leave-shared"
		closeDialog.close()
		exitConfirmed(disposition)
	}

	function dismissClosePrompt() {
		closeDialog.close()
		closeButton.forceActiveFocus()
	}

	function focusInitialControl() {
		const candidates = [ playButton, externalButton, muteButton, compactVolumeButton,
			volumeSlider, fullscreenButton, closeButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const candidate = candidates[index]
			if (!candidate || !candidate.enabled)
				continue
			candidate.forceActiveFocus()
			if (candidate.activeFocus)
				return true
		}
		return false
	}

	implicitHeight: controlsLayout.implicitHeight + Theme.space4
	color: Theme.panel
	border.color: Theme.surfaceBorder
	Accessible.role: Accessible.ToolBar
	Accessible.name: qsTr("Media playback controls")

	Connections {
		target: session
		function onStateChanged() {
			if (!root.sharedPlayback && closeDialog.opened)
				closeDialog.close()
		}
	}
	onCompactControlsChanged: {
		if (!compactControls)
			volumePopup.close()
	}

	ColumnLayout {
		id: controlsLayout
		anchors.fill: parent
		anchors.leftMargin: Theme.space3
		anchors.rightMargin: Theme.space3
		anchors.topMargin: Theme.space2
		anchors.bottomMargin: Theme.space2
		spacing: Theme.space1

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2

			ModernSlider {
				id: seekSlider
				objectName: "mediaSeekSlider"
				Layout.fillWidth: true
				from: 0
				to: Math.max(1, Number(session.duration || 0))
				value: Number(session.position || 0)
				enabled: root.canControl && session.state !== "loading" && session.state !== "error"
				visible: !root.providerControlled
				onMoved: session.seek(value)
				Accessible.name: qsTr("Playback position")
				Accessible.description: qsTr("%1 of %2").arg(root.formatTime(value)).arg(root.formatTime(session.duration))
			}

			Label {
				Layout.preferredWidth: session.duration >= 3600 ? 108 : 82
				textFormat: Text.PlainText
				text: root.formatTime(session.position) + " / " + root.formatTime(session.duration)
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				horizontalAlignment: Text.AlignRight
				visible: !root.providerControlled
			}
		}

		Flow {
			id: transportActions
			objectName: "mediaTransportActions"
			Layout.fillWidth: true
			Layout.minimumHeight: Theme.controlHeight
			Layout.preferredHeight: Math.max(Theme.controlHeight, childrenRect.height)
			spacing: Theme.space2

			ModernIconButton {
				id: playButton
				objectName: "mediaPlayButton"
				text: session.state === "playing" ? "Ⅱ" : "▶"
				enabled: root.canControl && session.state !== "loading" && session.state !== "error"
				visible: !root.providerControlled
				Accessible.name: session.state === "playing" ? qsTr("Pause") : qsTr("Play")
				Accessible.description: session.sharedJoined
					? qsTr("Control synchronized playback for everyone in the session") : ""
				onClicked: session.state === "playing" ? session.pause() : session.play()
			}

			Label {
				id: stateLabel
				objectName: "mediaStateLabel"
				width: Math.min(190, Math.max(Theme.controlHeight, transportActions.width))
				height: Theme.controlHeight
				textFormat: Text.PlainText
				text: root.stateLabel()
				color: session.sharedHost ? Theme.accent
					: session.state === "error" ? Theme.danger : Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.weight: session.sharedHost ? Font.DemiBold : Font.Normal
				elide: Text.ElideRight
				verticalAlignment: Text.AlignVCenter
			}

			ModernButton {
				id: externalButton
				objectName: "mediaExternalButton"
				visible: root.externalActionAvailable
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, transportActions.width))
				dense: true
				text: qsTr("Open externally")
				Accessible.description: qsTr("Open the provider in your default browser")
				onClicked: root.externalRequested()
			}

			ModernIconButton {
				id: muteButton
				objectName: "mediaMuteButton"
				text: session.muted || session.volume === 0 ? "M" : "A"
				selected: session.muted
				visible: !root.providerControlled
				Accessible.name: session.muted ? qsTr("Unmute media") : qsTr("Mute media")
				onClicked: session.toggleMuted()
			}

			ModernSlider {
				id: volumeSlider
				objectName: "mediaVolumeSlider"
				width: 104
				visible: !root.compactControls && !root.providerControlled
				from: 0
				to: 100
				stepSize: 1
				value: Number(session.volume)
				onMoved: session.setVolume(Math.round(value))
				Accessible.name: qsTr("Media volume")
				Accessible.description: qsTr("%1 percent").arg(Math.round(value))
			}

			ModernButton {
				id: compactVolumeButton
				objectName: "mediaCompactVolumeButton"
				visible: root.compactControls && !root.providerControlled
				dense: true
				highlighted: volumePopup.opened
				text: qsTr("%1%").arg(Math.round(session.volume))
				Accessible.name: qsTr("Adjust media volume")
				Accessible.description: qsTr("Current volume: %1 percent").arg(Math.round(session.volume))
				onClicked: volumePopup.opened ? volumePopup.close() : volumePopup.open()
			}

			ModernIconButton {
				id: fullscreenButton
				objectName: "mediaFullscreenButton"
				visible: root.fullscreenAvailable
				text: root.fullscreen ? "↙" : "⛶"
				selected: root.fullscreen
				Accessible.name: root.fullscreen ? qsTr("Exit full screen") : qsTr("Enter full screen")
				onClicked: root.fullscreenRequested(!root.fullscreen)
			}

			ModernButton {
				id: closeButton
				objectName: "mediaCloseButton"
				dense: true
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, transportActions.width))
				tone: session.sharedHost ? "danger" : "neutral"
				text: session.sharedHost ? qsTr("End session")
					: (session.sharedJoined ? qsTr("Leave") : qsTr("Close"))
				Accessible.description: session.sharedHost
					? qsTr("Choose whether to close only this player or end playback for everyone")
					: (session.sharedJoined
						? qsTr("Choose whether to close only this player or leave synchronized playback") : "")
				onClicked: root.requestClose()
			}
		}
	}

	Popup {
		id: volumePopup
		objectName: "mediaVolumePopup"
		parent: root
		modal: false
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		padding: Theme.space3
		width: Math.min(280, root.width - Theme.space4)
		palette.window: Theme.surfaceRaised
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
		palette.active.highlight: Theme.selected
		palette.inactive.highlight: Theme.selected
		palette.active.highlightedText: Theme.textStrong
		palette.inactive.highlightedText: Theme.textStrong
		palette.placeholderText: Theme.textMuted
		palette.active.link: Theme.accent
		palette.inactive.link: Theme.accent
		palette.active.linkVisited: Theme.accentHover
		palette.inactive.linkVisited: Theme.accentHover
		palette.active.toolTipBase: Theme.surfaceRaised
		palette.inactive.toolTipBase: Theme.surfaceRaised
		palette.active.toolTipText: Theme.textStrong
		palette.inactive.toolTipText: Theme.textStrong
		palette.active.light: Theme.surfaceHover
		palette.inactive.light: Theme.surfaceHover
		palette.active.midlight: Theme.surfaceRaised
		palette.inactive.midlight: Theme.surfaceRaised
		palette.active.mid: Theme.surfaceBorder
		palette.inactive.mid: Theme.surfaceBorder
		palette.dark: Theme.rail
		palette.shadow: Theme.strip
		palette.disabled.window: Theme.surfaceRaised
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
		x: {
			const point = compactVolumeButton.mapToItem(root, 0, 0)
			return Math.max(Theme.space2, Math.min(root.width - width - Theme.space2, point.x + compactVolumeButton.width - width))
		}
		y: -height - Theme.space2
		background: Rectangle {
			radius: Theme.innerRadius
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
			border.width: 1
		}

		contentItem: ColumnLayout {
			spacing: Theme.space2
			Accessible.role: Accessible.PopupMenu
			Accessible.name: qsTr("Media volume")
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Media volume · %1%").arg(Math.round(session.volume))
				color: Theme.textStrong
				font.pixelSize: Theme.fontLabel
				font.weight: Font.DemiBold
			}
			ModernSlider {
				id: compactVolumeSlider
				objectName: "mediaCompactVolumeSlider"
				Layout.fillWidth: true
				from: 0
				to: 100
				stepSize: 1
				value: Number(session.volume)
				onMoved: session.setVolume(Math.round(value))
				Accessible.name: qsTr("Media volume")
				Accessible.description: qsTr("%1 percent").arg(Math.round(value))
			}
		}
	}

	Dialog {
		id: closeDialog
		objectName: "mediaCloseConfirmation"
		parent: Overlay.overlay
		anchors.centerIn: parent
		modal: true
		focus: true
		closePolicy: Popup.NoAutoClose
		padding: Theme.space5
		width: Math.min(460, parent ? parent.width - Theme.space5 * 2 : 460)
		title: session.sharedHost ? qsTr("End this shared session?") : qsTr("Leave shared playback?")
		header: null
		footer: null
		palette.window: Theme.panel
		palette.active.base: Theme.panel
		palette.inactive.base: Theme.panel
		palette.alternateBase: Theme.surfaceRaised
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
		palette.active.highlight: Theme.selected
		palette.inactive.highlight: Theme.selected
		palette.active.highlightedText: Theme.textStrong
		palette.inactive.highlightedText: Theme.textStrong
		palette.placeholderText: Theme.textMuted
		palette.active.link: Theme.accent
		palette.inactive.link: Theme.accent
		palette.active.linkVisited: Theme.accentHover
		palette.inactive.linkVisited: Theme.accentHover
		palette.active.toolTipBase: Theme.surfaceRaised
		palette.inactive.toolTipBase: Theme.surfaceRaised
		palette.active.toolTipText: Theme.textStrong
		palette.inactive.toolTipText: Theme.textStrong
		palette.active.light: Theme.surfaceHover
		palette.inactive.light: Theme.surfaceHover
		palette.active.midlight: Theme.surfaceRaised
		palette.inactive.midlight: Theme.surfaceRaised
		palette.active.mid: Theme.surfaceBorder
		palette.inactive.mid: Theme.surfaceBorder
		palette.dark: Theme.rail
		palette.shadow: Theme.strip
		palette.disabled.window: Theme.panel
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

		background: Rectangle {
			radius: Theme.shellRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder
			border.width: 1
		}

		contentItem: ColumnLayout {
			spacing: Theme.space3
			Accessible.role: Accessible.Dialog
			Accessible.name: closeDialog.title
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: closeDialog.title
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				wrapMode: Text.Wrap
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: session.sharedHost
					? qsTr("Closing only the player pauses playback and keeps the session available. Ending it stops the session for every participant.")
					: qsTr("Closing only the player keeps you in the session so you can reopen it from the room banner.")
				color: Theme.textMain
				wrapMode: Text.Wrap
			}
			Flow {
				id: closeActionFlow
				objectName: "mediaCloseActionFlow"
				Layout.fillWidth: true
				Layout.topMargin: Theme.space2
				Layout.minimumHeight: Theme.controlHeight
				Layout.preferredHeight: Math.max(Theme.controlHeight, childrenRect.height)
				spacing: Theme.space2
				ModernButton {
					objectName: "mediaCloseCancelButton"
					width: Math.min(implicitWidth, Math.max(Theme.controlHeight, closeActionFlow.width))
					text: qsTr("Cancel")
					onClicked: root.dismissClosePrompt()
				}
				ModernButton {
					objectName: "mediaClosePlayerOnlyButton"
					width: Math.min(implicitWidth, Math.max(Theme.controlHeight, closeActionFlow.width))
					text: qsTr("Close player only")
					highlighted: true
					onClicked: root.confirmClosePlayer()
				}
				ModernButton {
					objectName: "mediaEndSharedButton"
					width: Math.min(implicitWidth, Math.max(Theme.controlHeight, closeActionFlow.width))
					text: session.sharedHost ? qsTr("End for everyone") : qsTr("Leave session")
					tone: "danger"
					onClicked: root.confirmSharedExit()
				}
			}
		}
	}
}
