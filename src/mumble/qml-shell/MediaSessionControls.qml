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
	property bool embedded: false
	property bool transportAvailable: true
	property Item focusBeforeClosePrompt: null
	property bool confirmedClosePending: false
	readonly property bool sharedPlayback: Boolean(session.sharedAvailable && session.sharedJoined)
	readonly property bool canControl: session.playbackControlAllowed !== undefined
		? Boolean(session.playbackControlAllowed)
		: Boolean(session.playbackControllable && (!session.sharedAvailable || session.sharedHost))
	readonly property bool providerControlled: !Boolean(session.playbackControllable)
	readonly property bool transportControlsVisible: transportAvailable && !providerControlled
	readonly property bool compactControls: embedded || width < 720
	readonly property bool embeddedNarrow: embedded && width < 420
	readonly property bool controlsWrapped: transportActions.childrenRect.height > Theme.controlHeight
	readonly property bool externalActionAvailable: externalAvailable
		&& (session.state === "error" || providerControlled || !transportAvailable)
	readonly property bool confirmationVisible: closeDialog.opened
	readonly property bool compactVolumeVisible: compactControls
	readonly property bool volumePopupVisible: volumePopup.opened
	readonly property real embeddedTransportReservedWidth: {
		const controls = [ playButton, embeddedTimeLabel, stateLabel, externalButton,
			muteButton, compactVolumeButton, fullscreenButton, closeButton ]
		let total = 0
		let count = 0
		for (let index = 0; index < controls.length; ++index) {
			const control = controls[index]
			if (!control || !control.visible)
				continue
			total += Number(control.width || 0)
			count += 1
		}
		return total + Math.max(0, count) * transportActions.spacing
	}
	readonly property string surfaceId: "mediaSession.controls"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property color stateTone: session.state === "error" ? Theme.danger
		: session.state === "loading" ? Theme.warning
		: session.sharedHost || session.sharedJoined ? Theme.accent
		: session.state === "playing" ? Theme.success : Theme.textMuted

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
		if (root.providerControlled || !root.transportAvailable) return ""
		if (session.sharedHost) return qsTr("Hosting for %1").arg(session.sharedParticipantCount)
		if (session.sharedJoined) return qsTr("Synchronized with host")
		return session.state === "playing" ? qsTr("Playing") : qsTr("Paused")
	}

	function requestClose() {
		if (sharedPlayback) {
			volumePopup.close()
			rememberClosePromptFocus()
			confirmedClosePending = false
			closeDialog.open()
			return false
		}
		exitConfirmed("close-player")
		return true
	}

	function confirmClosePlayer() {
		confirmedClosePending = true
		closeDialog.close()
		exitConfirmed("close-player")
	}

	function confirmSharedExit() {
		const disposition = session.sharedHost ? "end-shared" : "leave-shared"
		confirmedClosePending = true
		closeDialog.close()
		exitConfirmed(disposition)
	}

	function dismissClosePrompt() {
		confirmedClosePending = false
		closeDialog.close()
	}

	function rememberClosePromptFocus() {
		const window = root.Window.window
		const candidate = window ? window.activeFocusItem : null
		focusBeforeClosePrompt = candidate && candidate.forceActiveFocus ? candidate : null
	}

	function handleClosePromptClosed() {
		const shouldRestore = !confirmedClosePending
		const candidate = focusBeforeClosePrompt
		confirmedClosePending = false
		focusBeforeClosePrompt = null
		if (!shouldRestore)
			return
		Qt.callLater(function() {
			const target = candidate && candidate.forceActiveFocus && candidate.visible
				&& candidate.enabled ? candidate : closeButton
			if (target && target.visible && target.enabled)
				target.forceActiveFocus(Qt.PopupFocusReason)
		})
	}

	function focusInitialControl() {
		const candidates = [ playButton, externalButton, muteButton, compactVolumeButton,
			volumeSlider, fullscreenButton, closeButton ]
		for (let index = 0; index < candidates.length; ++index) {
			const candidate = candidates[index]
			if (!candidate || !candidate.enabled || !candidate.visible)
				continue
			candidate.forceActiveFocus()
			if (candidate.activeFocus)
				return true
		}
		return false
	}

	implicitHeight: embedded
		? Math.max(Theme.controlHeight, transportActions.childrenRect.height) + Theme.space2
		: controlsLayout.implicitHeight + Theme.space4
	color: embedded ? Theme.embedSurface : Theme.chatSurface
	border.color: embedded ? Theme.embedBorder : Theme.surfaceBorder
	Accessible.role: Accessible.ToolBar
	Accessible.name: qsTr("Media playback controls")
	Accessible.ignored: closeDialog.visible

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

	ModalAccessibilityBarrier {
		id: closePromptAccessibilityBarrier
		objectName: "mediaCloseAccessibilityBarrier"
		active: closeDialog.visible
		// Ignoring only the toolbar owner promotes its controls. Suppress the full
		// transport branch while the overlay-hosted close dialog owns semantics.
		targets: [ controlsLayout ]
	}

	ColumnLayout {
		id: controlsLayout
		anchors.fill: parent
		anchors.leftMargin: Theme.space3
		anchors.rightMargin: Theme.space3
		anchors.topMargin: embedded ? Theme.space1 : Theme.space2
		anchors.bottomMargin: embedded ? Theme.space1 : Theme.space2
		spacing: embedded ? 0 : Theme.space1

		RowLayout {
			id: seekRow
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			visible: !root.embedded && root.transportControlsVisible
			spacing: Theme.space2

			ModernSlider {
				id: seekSlider
				objectName: "mediaSeekSlider"
				Layout.fillWidth: true
				from: 0
				to: Math.max(1, Number(session.duration || 0))
				value: Number(session.position || 0)
				enabled: root.canControl && session.state !== "loading" && session.state !== "error"
				visible: root.transportControlsVisible
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
				visible: root.transportControlsVisible
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
				text: session.state === "playing" ? qsTr("Pause") : qsTr("Play")
				iconName: session.state === "playing" ? "pause" : "play"
				enabled: root.canControl && session.state !== "loading" && session.state !== "error"
				visible: root.transportControlsVisible
				Accessible.name: session.state === "playing" ? qsTr("Pause") : qsTr("Play")
				Accessible.description: session.sharedJoined
					? qsTr("Control synchronized playback for everyone in the session") : ""
				onClicked: session.state === "playing" ? session.pause() : session.play()
			}

			Label {
				id: embeddedTimeLabel
				objectName: "mediaEmbeddedTimeLabel"
				visible: root.embedded && root.transportControlsVisible && !root.embeddedNarrow
				width: session.duration >= 3600 ? 108 : 82
				height: Theme.controlHeight
				textFormat: Text.PlainText
				text: root.formatTime(session.position) + " / " + root.formatTime(session.duration)
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				horizontalAlignment: Text.AlignRight
				verticalAlignment: Text.AlignVCenter
			}

			ModernSlider {
				id: embeddedSeekSlider
				objectName: "mediaEmbeddedSeekSlider"
				visible: root.embedded && root.transportControlsVisible
				// Give seeking the space left by the visible transport cluster. This
				// keeps the card player on one row without relying on translated label
				// widths or a brittle per-breakpoint estimate.
				width: Math.max(72, transportActions.width - root.embeddedTransportReservedWidth)
				from: 0
				to: Math.max(1, Number(session.duration || 0))
				value: Number(session.position || 0)
				enabled: root.canControl && session.state !== "loading" && session.state !== "error"
				onMoved: session.seek(value)
				Accessible.name: qsTr("Playback position")
				Accessible.description: qsTr("%1 of %2").arg(root.formatTime(value)).arg(root.formatTime(session.duration))
			}

			Label {
				id: stateLabel
				objectName: "mediaStateLabel"
				width: root.embedded ? Math.min(130, Math.max(Theme.controlHeight, transportActions.width))
					: Math.min(190, Math.max(Theme.controlHeight, transportActions.width))
				height: Theme.controlHeight
				textFormat: Text.PlainText
				text: root.stateLabel()
				color: root.stateTone
				font.pixelSize: Theme.fontCaption
				font.weight: session.sharedHost ? Font.DemiBold : Font.Normal
				elide: Text.ElideRight
				verticalAlignment: Text.AlignVCenter
				visible: !root.embedded && root.transportControlsVisible
			}

			ModernButton {
				id: externalButton
				objectName: "mediaExternalButton"
				visible: root.externalActionAvailable
				width: Math.min(implicitWidth, Math.max(Theme.controlHeight, transportActions.width))
				dense: true
				text: qsTr("Open externally")
				tone: session.state === "error" ? "accent" : "neutral"
				highlighted: session.state === "error"
				Accessible.description: qsTr("Open the provider in your default browser")
				onClicked: root.externalRequested()
			}

			ModernIconButton {
				id: muteButton
				objectName: "mediaMuteButton"
				text: session.muted ? qsTr("Unmute media") : qsTr("Mute media")
				iconName: session.muted || session.volume === 0 ? "volume-off" : "volume"
				selected: !!session.muted
				visible: root.transportControlsVisible
				Accessible.name: session.muted ? qsTr("Unmute media") : qsTr("Mute media")
				onClicked: session.toggleMuted()
			}

			ModernSlider {
				id: volumeSlider
				objectName: "mediaVolumeSlider"
				width: 104
				visible: !root.compactControls && root.transportControlsVisible
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
				visible: root.compactControls && root.transportControlsVisible
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
				text: root.fullscreen ? qsTr("Exit full screen") : qsTr("Enter full screen")
				iconName: root.fullscreen ? "fullscreen-exit" : "fullscreen"
				selected: root.fullscreen
				Accessible.name: root.fullscreen ? qsTr("Exit full screen") : qsTr("Enter full screen")
				onClicked: root.fullscreenRequested(!root.fullscreen)
			}

			ModernButton {
				id: closeButton
				objectName: "mediaCloseButton"
				dense: true
				width: root.embedded ? Theme.controlHeight
					: Math.min(implicitWidth, Math.max(Theme.controlHeight, transportActions.width))
				tone: session.sharedHost ? "danger" : "neutral"
				text: session.sharedHost ? qsTr("End session")
					: (session.sharedJoined ? qsTr("Leave") : qsTr("Close"))
				Accessible.name: session.sharedHost ? qsTr("End session")
					: (session.sharedJoined ? qsTr("Leave") : qsTr("Close"))
				Accessible.description: session.sharedHost
					? qsTr("Choose whether to close only this player or end playback for everyone")
					: (session.sharedJoined
						? qsTr("Choose whether to close only this player or leave synchronized playback") : "")
				contentItem: Item {
					implicitWidth: root.embedded ? closeIcon.implicitWidth : closeLabel.implicitWidth
					implicitHeight: Math.max(closeIcon.implicitHeight, closeLabel.implicitHeight)
					readonly property color foreground: !closeButton.enabled ? Theme.textMuted
						: closeButton.emphasized ? Theme.contrastText(closeButton.toneColor) : Theme.textStrong

					ModernIcon {
						id: closeIcon
						objectName: "mediaCloseIcon"
						anchors.centerIn: parent
						visible: root.embedded
						name: "close"
						size: 18
						color: parent.foreground
					}
					Text {
						id: closeLabel
						anchors.fill: parent
						visible: !root.embedded
						text: closeButton.text
						font: closeButton.font
						color: parent.foreground
						horizontalAlignment: Text.AlignHCenter
						verticalAlignment: Text.AlignVCenter
						elide: Text.ElideRight
					}
				}
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
		onAboutToShow: if (!root.focusBeforeClosePrompt) root.rememberClosePromptFocus()
		onOpened: Qt.callLater(function() {
			mediaCloseCancelButton.forceActiveFocus(Qt.PopupFocusReason)
		})
		onClosed: root.handleClosePromptClosed()
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
					id: mediaCloseCancelButton
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
