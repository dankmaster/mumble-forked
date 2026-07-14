import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0

ApplicationWindow {
    id: mediaWindow
	readonly property string mediaAspect: inferMediaAspect()
	readonly property string surfaceId: "mediaSession.window"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property bool hasMediaSource: String(mediaSession.url || "").trim().length > 0
	readonly property bool webSurfaceActive: playerLoader.active
	readonly property string rendererState: !mediaSession.active ? "empty"
		: mediaSession.error.length > 0 ? "error"
		: !hasMediaSource ? "empty"
		: mediaSession.state === "loading" ? "loading" : "active"

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
    visible: mediaSession.active
    width: 1040
    height: 700
    minimumWidth: 640
    minimumHeight: 420
    title: mediaSession.sharedAvailable && mediaSession.sharedTitle.length > 0
           ? mediaSession.sharedTitle
           : qsTr("Media session")
    color: Theme.shellBackground
	Component.onCompleted: {
		applyInitialWindowSize()
		Qt.callLater(controls.focusInitialControl)
	}
    onClosing: function(close) {
		// Closing a shared player must never implicitly leave or end the session.
		// Keep the window alive while the user chooses the intended disposition;
		// closePlayer() will deactivate this lazy-loaded window afterwards.
		close.accepted = false
		controls.requestClose()
    }

	function setFullscreen(enabled) {
		if (enabled)
			mediaWindow.showFullScreen()
		else
			mediaWindow.showNormal()
	}

	function applyExitDisposition(disposition) {
		if (disposition === "end-shared")
			mediaSession.endShared()
		else if (disposition === "leave-shared")
			mediaSession.leaveShared()
		else {
			if (mediaSession.sharedHost && mediaSession.state === "playing")
				mediaSession.pause()
			mediaSession.closePlayer()
		}
	}

	function playerScript(command, value) {
		const provider = JSON.stringify(String(mediaSession.provider || ""))
		const numericValue = Number(value || 0)
		return "(function(){const provider=" + provider + ";"
			+ "const yt=document.getElementById('movie_player');"
			+ "const isYt=provider==='youtube'&&yt&&typeof yt.getPlayerState==='function';"
			+ "const media=document.querySelector('video,audio');"
			+ (command === "play"
				? "if(isYt&&typeof yt.playVideo==='function'){yt.playVideo();return true;}if(media){media.play();return true;}return false;"
				: command === "pause"
				? "if(isYt&&typeof yt.pauseVideo==='function'){yt.pauseVideo();return true;}if(media){media.pause();return true;}return false;"
				: command === "seek"
				? "const target=" + numericValue + ";if(isYt&&typeof yt.seekTo==='function'){yt.seekTo(target,true);return true;}if(media){media.currentTime=target;return true;}return false;"
				: command === "volume"
				? "const target=Math.max(0,Math.min(100," + numericValue + "));if(isYt&&typeof yt.setVolume==='function'){yt.setVolume(target);return true;}if(media){media.volume=target/100;return true;}return false;"
				: command === "mute"
				? "const muted=" + (numericValue > 0 ? "true" : "false") + ";if(isYt){if(muted&&typeof yt.mute==='function')yt.mute();else if(!muted&&typeof yt.unMute==='function')yt.unMute();return true;}if(media){media.muted=muted;return true;}return false;"
				: "if(isYt){const state=yt.getPlayerState();return {position:Number(yt.getCurrentTime()||0),duration:Number(yt.getDuration()||0),paused:state!==1};}"
				  + "if(media)return {position:Number(media.currentTime||0),duration:isFinite(media.duration)?Number(media.duration):0,paused:!!media.paused};return null;")
			+ "})()"
	}

    function runOnPlayers(script) {
        if (playerLoader.item)
            playerLoader.item.runJavaScript(script)
        if (audioPlayerLoader.item)
            audioPlayerLoader.item.runJavaScript(script)
    }

	function externalMediaUrl() {
		const value = String(mediaSession.url || "").trim()
		return /^https:\/\//i.test(value) ? value : ""
	}

	function inferMediaAspect() {
		const provider = String(mediaSession.provider || "").trim().toLowerCase()
		const url = String(mediaSession.url || "").trim().toLowerCase()
		if (provider.indexOf("tiktok") >= 0 || url.indexOf("tiktok.com/") >= 0)
			return "short"
		if (provider.indexOf("instagram") >= 0 || url.indexOf("instagram.com/") >= 0)
			return /\/reels?(?:\/|$)/.test(url) ? "short" : "square"
		if (provider.indexOf("spotify") >= 0 || url.indexOf("spotify.com/") >= 0)
			return /\/(?:track|episode)(?:\/|\?|$)/.test(url) ? "compact-audio" : "audio"
		if (provider.indexOf("soundcloud") >= 0 || url.indexOf("soundcloud.com/") >= 0)
			return "compact-audio"
		if (provider.indexOf("twitch") >= 0 || url.indexOf("twitch.tv/") >= 0)
			return "twitch"
		return "wide"
	}

	function applyInitialWindowSize() {
		if (mediaAspect === "short") {
			width = 640
			height = 820
		} else if (mediaAspect === "square") {
			width = 760
			height = 760
		} else if (mediaAspect === "audio") {
			width = 820
			height = 520
		} else if (mediaAspect === "compact-audio") {
			width = 820
			height = 420
		} else {
			width = 1040
			height = 700
		}
	}

	function fittedMediaWidth(availableWidth, availableHeight) {
		if (availableWidth <= 0 || availableHeight <= 0)
			return 0
		if (mediaAspect === "audio" || mediaAspect === "compact-audio")
			return availableWidth
		const ratio = mediaAspect === "short" ? 9 / 16
			: mediaAspect === "square" ? 1 : 16 / 9
		return Math.min(availableWidth, availableHeight * ratio)
	}

	function fittedMediaHeight(availableWidth, availableHeight) {
		if (availableWidth <= 0 || availableHeight <= 0)
			return 0
		if (mediaAspect === "audio")
			return Math.min(availableHeight, Math.min(352, Math.max(220, availableWidth * 0.61)))
		if (mediaAspect === "compact-audio")
			return Math.min(availableHeight, Math.min(166, Math.max(128, availableWidth * 0.29)))
		const ratio = mediaAspect === "short" ? 9 / 16
			: mediaAspect === "square" ? 1 : 16 / 9
		return fittedMediaWidth(availableWidth, availableHeight) / ratio
	}

	function withAlpha(color, alpha) {
		return Qt.rgba(color.r, color.g, color.b, alpha)
	}

	function applyDesiredPlaybackState() {
		if (!mediaSession.playbackControllable)
			return
		runOnPlayers(playerScript("volume", mediaSession.volume))
		runOnPlayers(playerScript("mute", mediaSession.muted ? 1 : 0))
		runOnPlayers(playerScript("seek", mediaSession.position))
		if (mediaSession.state === "playing")
			runOnPlayers(playerScript("play", 0))
		else if (mediaSession.state === "paused")
			runOnPlayers(playerScript("pause", 0))
	}

	Shortcut {
		sequence: "F11"
		context: Qt.ApplicationShortcut
		onActivated: mediaWindow.setFullscreen(mediaWindow.visibility !== Window.FullScreen)
	}

	Shortcut {
		sequence: "Escape"
		context: Qt.ApplicationShortcut
		enabled: mediaWindow.visibility === Window.FullScreen
		onActivated: mediaWindow.setFullscreen(false)
	}

	Rectangle {
		id: playerCanvas
		objectName: "mediaSessionCanvas"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: controls.top
		color: Theme.mediaCanvas
		border.color: mediaWindow.rendererState === "error"
			? Theme.withAlpha(Theme.danger, 0.55) : Theme.surfaceBorder
		border.width: 1
		clip: true
	}

    Loader {
        id: playerLoader
		objectName: "mediaSessionWebSurface"
		parent: playerCanvas
		width: mediaWindow.fittedMediaWidth(playerCanvas.width, playerCanvas.height)
		height: mediaWindow.fittedMediaHeight(playerCanvas.width, playerCanvas.height)
		x: Math.round((playerCanvas.width - width) / 2)
		y: Math.round((playerCanvas.height - height) / 2)
		// The main application creates this window only after explicit user
		// interaction. Tear the provider surface down on errors as well so a
		// crashed renderer is not kept alive behind the native failure state.
		active: mediaSession.active && mediaWindow.hasMediaSource
			&& mediaSession.error.length === 0
        sourceComponent: WebEngineView {
            id: player
			property int missingStatePolls: 0
			Accessible.name: qsTr("Media provider playback")
            profile: mediaProfiles.videoProfile
            url: mediaSession.url
            settings.playbackRequiresUserGesture: false
			onLoadProgressChanged: mediaSession.reportLoadProgress(loadProgress)
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    mediaSession.reportError(request.errorString)
				else if (request.status === WebEngineView.LoadSucceededStatus) {
					mediaSession.reportLoadProgress(100)
					missingStatePolls = 0
					Qt.callLater(mediaWindow.applyDesiredPlaybackState)
				}
            }
            onRenderProcessTerminated: function(status, exitCode) {
                mediaSession.reportError(qsTr("The media renderer stopped unexpectedly."))
            }
            // A new-window request fails closed unless openIn() is called.
            onNewWindowRequested: function(request) {}
            onFileDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onAuthenticationDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onPermissionRequested: function(permission) { permission.deny() }
            onCertificateError: function(error) { error.rejectCertificate() }
            onContextMenuRequested: function(request) { request.accepted = true }
            onNavigationRequested: function(request) {
                if (!mediaSession.isNavigationAllowed(request.url))
                    request.action = WebEngineNavigationRequest.IgnoreRequest
            }
			Connections {
				target: player.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
            Connections {
                target: mediaSession
                function onPlayRequested() {
					mediaWindow.runOnPlayers(mediaWindow.playerScript("play", 0))
                }
                function onPauseRequested() {
					mediaWindow.runOnPlayers(mediaWindow.playerScript("pause", 0))
                }
                function onSeekRequested(seconds) {
					mediaWindow.runOnPlayers(mediaWindow.playerScript("seek", seconds))
                }
				function onVolumeRequested(volume) {
					mediaWindow.runOnPlayers(mediaWindow.playerScript("volume", volume))
				}
				function onMutedRequested(muted) {
					mediaWindow.runOnPlayers(mediaWindow.playerScript("mute", muted ? 1 : 0))
				}
                function onRetryRequested() {
                    player.reload()
                    if (audioPlayerLoader.item)
                        audioPlayerLoader.item.reload()
                }
            }
            Timer {
                interval: 500
				running: mediaSession.active && mediaSession.playbackControllable
					&& mediaSession.state !== "error"
                repeat: true
                onTriggered: player.runJavaScript(mediaWindow.playerScript("state", 0),
                    function(value) {
						if (!value) {
							player.missingStatePolls += 1
							if (player.missingStatePolls === 20)
								mediaSession.reportError(qsTr("This provider did not expose playback controls. Open it externally instead."))
                            return
						}
						player.missingStatePolls = 0
                        mediaSession.reportPlaybackState(value.position, value.duration, value.paused)
                        if (audioPlayerLoader.item) {
                            audioPlayerLoader.item.runJavaScript(
                                "(function(){const m=document.querySelector('audio,video');if(!m)return;"
                                + "const target=" + Number(value.position) + ";"
                                + "if(Math.abs((m.currentTime||0)-target)>0.15)m.currentTime=target;"
                                + (value.paused ? "m.pause();" : "m.play();") + "})()")
                        }
                    })
            }
        }
    }

    Loader {
        id: audioPlayerLoader
		active: mediaSession.active && mediaSession.error.length === 0
			&& mediaSession.playbackControllable && mediaSession.audioUrl.toString().length > 0
        width: 1
        height: 1
        opacity: 0.01
        sourceComponent: WebEngineView {
			id: audioPlayer
			// The secondary track is transport-only. Keep its one-pixel surface out
			// of pointer, keyboard and assistive-technology traversal so it cannot
			// become an invisible focus trap.
			enabled: false
			focus: false
			activeFocusOnTab: false
			Accessible.ignored: true
			profile: mediaProfiles.audioProfile
            url: mediaSession.audioUrl
            settings.playbackRequiresUserGesture: false
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    mediaSession.reportError(request.errorString)
				else if (request.status === WebEngineView.LoadSucceededStatus)
					Qt.callLater(mediaWindow.applyDesiredPlaybackState)
            }
            onRenderProcessTerminated: function(status, exitCode) {
                mediaSession.reportError(qsTr("The direct-media audio renderer stopped unexpectedly."))
            }
            onNewWindowRequested: function(request) {}
            onFileDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onAuthenticationDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onPermissionRequested: function(permission) { permission.deny() }
            onCertificateError: function(error) { error.rejectCertificate() }
            onContextMenuRequested: function(request) { request.accepted = true }
            onNavigationRequested: function(request) {
                if (!mediaSession.isNavigationAllowed(request.url))
                    request.action = WebEngineNavigationRequest.IgnoreRequest
            }
			Connections {
				target: audioPlayer.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
        }
    }

	Rectangle {
		objectName: "mediaSessionProviderBadge"
		parent: playerCanvas
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.margins: Theme.space3
		width: providerBadgeRow.implicitWidth + Theme.space3
		height: 28
		radius: height / 2
		visible: mediaWindow.rendererState === "active"
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.82)
		border.color: Theme.withAlpha(Theme.accent, 0.42)
		z: 3
		Row {
			id: providerBadgeRow
			anchors.centerIn: parent
			spacing: Theme.space1
			ModernIcon {
				anchors.verticalCenter: parent.verticalCenter
				name: "play"
				size: 14
				color: Theme.accent
			}
			Label {
				textFormat: Text.PlainText
				text: String(mediaSession.provider || qsTr("media")).toUpperCase()
				color: Theme.textStrong
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}
			Label {
				visible: mediaSession.sharedAvailable && mediaSession.sharedJoined
				textFormat: Text.PlainText
				text: mediaSession.sharedHost ? qsTr("· HOSTING") : qsTr("· SYNCED")
				color: Theme.success
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}
		}
	}

	Rectangle {
		id: loadingSurface
		objectName: "mediaSessionLoadingSurface"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: mediaSession.active && mediaSession.state === "loading"
			&& mediaSession.error.length === 0
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.96)
		z: 4
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Loading media")
		Accessible.description: mediaSession.loadProgress > 0
			? qsTr("%1 percent loaded").arg(mediaSession.loadProgress) : qsTr("Contacting provider")

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 - Theme.space4, 420)
			spacing: Theme.space3

			ModernBusyIndicator {
				objectName: "mediaSessionBusyIndicator"
				Layout.alignment: Qt.AlignHCenter
				running: parent.parent.visible
				Accessible.name: qsTr("Loading media player")
			}
			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: 44
				Layout.preferredHeight: 24
				radius: height / 2
				color: Theme.accentSubtle
				Label {
					anchors.centerIn: parent
					textFormat: Text.PlainText
					text: String(mediaSession.provider || qsTr("media")).toUpperCase()
					color: Theme.accent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
				}
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Loading %1").arg(mediaSession.provider || qsTr("media"))
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: mediaSession.loadProgress > 0
					? qsTr("%1% loaded").arg(mediaSession.loadProgress)
					: qsTr("The isolated provider player is starting…")
				color: Theme.textMuted
				horizontalAlignment: Text.AlignHCenter
			}
			Rectangle {
				Layout.fillWidth: true
				Layout.preferredHeight: 3
				visible: mediaSession.loadProgress > 0
				radius: height / 2
				color: Theme.surfaceBorder
				Rectangle {
					width: parent.width * Math.max(0, Math.min(100, mediaSession.loadProgress)) / 100
					height: parent.height
					radius: height / 2
					color: Theme.accent
					Behavior on width { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }
				}
			}
		}
	}

	Rectangle {
		objectName: "mediaSessionEmptySurface"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: mediaSession.active && !mediaWindow.hasMediaSource
			&& mediaSession.error.length === 0
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.96)
		z: 4
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("No media source")
		Accessible.description: qsTr("Choose a supported preview to start playback")

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 * 2, 420)
			spacing: Theme.space3
			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: 52
				Layout.preferredHeight: 52
				radius: Theme.innerRadius
				color: Theme.accentSubtle
				ModernIcon {
					anchors.centerIn: parent
					name: "play"
					size: 24
					color: Theme.accent
				}
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Choose something to play")
				color: Theme.textStrong
				font.pixelSize: Theme.fontHeading
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Open a supported rich preview or rejoin the room session.")
				color: Theme.textMuted
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
			}
		}
	}

    Rectangle {
		id: failureSurface
		objectName: "mediaSessionFailureSurface"
		parent: playerCanvas
        anchors.fill: playerLoader
		visible: mediaSession.error.length > 0
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.96)
        z: 5
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Media playback failed")
		Accessible.description: mediaSession.error

        ColumnLayout {
            anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 - Theme.space4, 520)
			spacing: Theme.space3

			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: 52
				Layout.preferredHeight: 52
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
				textFormat: Text.PlainText
                Layout.fillWidth: true
                text: qsTr("Media playback failed")
                color: Theme.textStrong
				font.weight: Font.DemiBold
				font.pixelSize: Theme.fontHeading
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
				text: mediaSession.error
                color: Theme.textMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
				spacing: Theme.space2
				ModernButton {
					objectName: "mediaSessionRetryButton"
					text: qsTr("Retry")
					tone: "accent"
					highlighted: true
					Accessible.description: qsTr("Create a fresh isolated provider renderer")
					onClicked: mediaSession.retry()
				}
				ModernButton {
					objectName: "mediaSessionFailureExternalButton"
					visible: mediaWindow.externalMediaUrl().length > 0
					text: qsTr("Open externally")
					onClicked: Qt.openUrlExternally(mediaWindow.externalMediaUrl())
				}
            }
        }
    }

    MediaSessionControls {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
		session: mediaSession
		fullscreen: mediaWindow.visibility === Window.FullScreen
		externalAvailable: mediaWindow.externalMediaUrl().length > 0
		onFullscreenRequested: enabled => mediaWindow.setFullscreen(enabled)
		onExternalRequested: Qt.openUrlExternally(mediaWindow.externalMediaUrl())
		onExitConfirmed: disposition => mediaWindow.applyExitDisposition(disposition)
    }
}
