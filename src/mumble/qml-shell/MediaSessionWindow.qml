import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0

ApplicationWindow {
    id: mediaWindow
    visible: mediaSession.active
    width: 1040
    height: 700
    minimumWidth: 640
    minimumHeight: 420
    title: mediaSession.sharedAvailable && mediaSession.sharedTitle.length > 0
           ? mediaSession.sharedTitle
           : qsTr("Media session")
    color: Theme.shellBackground
    onClosing: function(close) {
        close.accepted = true
        mediaSession.close()
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

	function applyDesiredPlaybackState() {
		if (!mediaSession.playbackControllable)
			return
		runOnPlayers(playerScript("seek", mediaSession.position))
		if (mediaSession.state === "playing")
			runOnPlayers(playerScript("play", 0))
		else if (mediaSession.state === "paused")
			runOnPlayers(playerScript("pause", 0))
	}

    Loader {
        id: playerLoader
        anchors.fill: parent
        anchors.bottomMargin: controls.height
        active: mediaSession.active && mediaSession.playbackControllable
        sourceComponent: WebEngineView {
            id: player
			property int missingStatePolls: 0
			Accessible.name: qsTr("Media provider playback")
			profile: mediaProfiles.videoProfile
            url: mediaSession.url
            settings.playbackRequiresUserGesture: false
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    mediaSession.reportError(request.errorString)
				else if (request.status === WebEngineView.LoadSucceededStatus) {
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
                function onRetryRequested() {
                    player.reload()
                    if (audioPlayerLoader.item)
                        audioPlayerLoader.item.reload()
                }
            }
            Timer {
                interval: 500
                running: true
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
        active: mediaSession.active && mediaSession.playbackControllable
			&& mediaSession.audioUrl.toString().length > 0
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
        anchors.fill: playerLoader
        visible: mediaSession.error.length > 0 || (mediaSession.active && !mediaSession.playbackControllable)
        color: "#d9161b26"
        z: 5
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Media playback failed")
		Accessible.description: mediaSession.error.length > 0 ? mediaSession.error
			: qsTr("This provider cannot be synchronized in-app. Open it in your browser instead.")

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 520)
            spacing: 14

            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
                text: qsTr("Media playback failed")
                color: Theme.textStrong
                font.bold: true
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
				text: mediaSession.error.length > 0 ? mediaSession.error
					: qsTr("This provider cannot be synchronized in-app. Open it in your browser instead.")
                color: Theme.textMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
				ModernButton { visible: mediaSession.playbackControllable; text: qsTr("Retry"); onClicked: mediaSession.retry() }
				ModernButton {
					visible: mediaWindow.externalMediaUrl().length > 0
					text: qsTr("Open externally")
					onClicked: Qt.openUrlExternally(mediaWindow.externalMediaUrl())
				}
            }
        }
    }

    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 64
        color: Theme.panel
        border.color: Theme.divider
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            ModernButton {
				enabled: mediaSession.playbackControllable
					&& (!mediaSession.sharedAvailable || mediaSession.sharedHost)
                text: mediaSession.state === "playing" ? qsTr("Pause") : qsTr("Play")
                onClicked: mediaSession.state === "playing" ? mediaSession.pause() : mediaSession.play()
            }
            Slider {
                Layout.fillWidth: true
				enabled: mediaSession.playbackControllable
					&& (!mediaSession.sharedAvailable || mediaSession.sharedHost)
                from: 0
                to: Math.max(1, mediaSession.duration)
                value: mediaSession.position
                onMoved: mediaSession.seek(value)
				Accessible.name: qsTr("Playback position")
				Accessible.description: qsTr("Seek within the current media")
            }
            Label { textFormat: Text.PlainText; text: Math.floor(mediaSession.position) + " / " + Math.floor(mediaSession.duration) + " s"; color: Theme.textMuted }
            Label {
				textFormat: Text.PlainText
                visible: mediaSession.sharedAvailable
                text: mediaSession.sharedHost ? qsTr("Hosting") : qsTr("Synchronized")
                color: mediaSession.sharedHost ? Theme.accent : Theme.textMuted
            }
            ModernButton {
                text: mediaSession.sharedHost ? qsTr("End") : (mediaSession.sharedJoined ? qsTr("Leave") : qsTr("Close"))
                onClicked: mediaSession.close()
            }
        }
    }
}
