pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0

Rectangle {
	id: inlinePlayer

	required property var session
	readonly property bool ready: !!session && session.active && !session.detached
	color: Theme.shellBackground
	border.color: Theme.surfaceBorder
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Inline media player")

	function playerScript(command, value) {
		const provider = JSON.stringify(String(session ? session.provider : ""))
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

	function runPlayerScript(script) {
		if (playerLoader.item)
			playerLoader.item.runJavaScript(script)
	}

	function applyDesiredPlaybackState() {
		if (!ready || !session.playbackControllable)
			return
		runPlayerScript(playerScript("volume", session.volume))
		runPlayerScript(playerScript("mute", session.muted ? 1 : 0))
		runPlayerScript(playerScript("seek", session.position))
		runPlayerScript(playerScript(session.state === "playing" ? "play" : "pause", 0))
	}

	function externalMediaUrl() {
		const value = String(session ? session.url : "").trim()
		return /^https:\/\//i.test(value) ? value : ""
	}

	Rectangle {
		id: inlineToolbar
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		height: Theme.controlHeight + Theme.space2
		color: Theme.panel

		Label {
			anchors.left: parent.left
			anchors.right: toolbarActions.left
			anchors.leftMargin: Theme.space3
			anchors.rightMargin: Theme.space2
			anchors.verticalCenter: parent.verticalCenter
			textFormat: Text.PlainText
			text: qsTr("Playing in chat")
			color: Theme.textStrong
			font.pixelSize: Theme.fontLabel
			font.bold: true
			elide: Text.ElideRight
		}
		Row {
			id: toolbarActions
			anchors.right: parent.right
			anchors.rightMargin: Theme.space2
			anchors.verticalCenter: parent.verticalCenter
			spacing: Theme.space2
			ModernButton {
				objectName: "inlineMediaPopoutButton"
				dense: true
				text: qsTr("Pop out")
				Accessible.description: qsTr("Move playback to a separate movable window")
				onClicked: if (inlinePlayer.session) inlinePlayer.session.detach()
			}
			ModernButton {
				objectName: "inlineMediaExternalButton"
				dense: true
				visible: inlinePlayer.externalMediaUrl().length > 0
				text: qsTr("Browser")
				onClicked: Qt.openUrlExternally(inlinePlayer.externalMediaUrl())
			}
		}
	}

	Loader {
		id: playerLoader
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: inlineToolbar.bottom
		anchors.bottom: inlineControls.top
		active: inlinePlayer.ready
		sourceComponent: WebEngineView {
			id: webPlayer
			property int missingStatePolls: 0
			profile: mediaProfiles.videoProfile
			url: inlinePlayer.session ? inlinePlayer.session.url : ""
			settings.playbackRequiresUserGesture: false
			Accessible.name: qsTr("Embedded media provider playback")
			onLoadProgressChanged: if (inlinePlayer.session)
				inlinePlayer.session.reportLoadProgress(loadProgress)
			onLoadingChanged: function(request) {
				if (!inlinePlayer.session)
					return
				if (request.status === WebEngineView.LoadFailedStatus)
					inlinePlayer.session.reportError(request.errorString)
				else if (request.status === WebEngineView.LoadSucceededStatus) {
					inlinePlayer.session.reportLoadProgress(100)
					missingStatePolls = 0
					Qt.callLater(inlinePlayer.applyDesiredPlaybackState)
				}
			}
			onRenderProcessTerminated: if (inlinePlayer.session)
				inlinePlayer.session.reportError(qsTr("The inline media renderer stopped unexpectedly."))
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
				if (!inlinePlayer.session || !inlinePlayer.session.isNavigationAllowed(request.url))
					request.action = WebEngineNavigationRequest.IgnoreRequest
			}
			Connections {
				target: webPlayer.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
			Connections {
				target: inlinePlayer.session
				ignoreUnknownSignals: true
				function onPlayRequested() { inlinePlayer.runPlayerScript(inlinePlayer.playerScript("play", 0)) }
				function onPauseRequested() { inlinePlayer.runPlayerScript(inlinePlayer.playerScript("pause", 0)) }
				function onSeekRequested(seconds) { inlinePlayer.runPlayerScript(inlinePlayer.playerScript("seek", seconds)) }
				function onVolumeRequested(volume) { inlinePlayer.runPlayerScript(inlinePlayer.playerScript("volume", volume)) }
				function onMutedRequested(muted) { inlinePlayer.runPlayerScript(inlinePlayer.playerScript("mute", muted ? 1 : 0)) }
				function onRetryRequested() { webPlayer.reload() }
			}
			Timer {
				interval: 500
				running: inlinePlayer.ready && inlinePlayer.session.playbackControllable
					&& inlinePlayer.session.state !== "error"
				repeat: true
				onTriggered: webPlayer.runJavaScript(inlinePlayer.playerScript("state", 0), function(value) {
					if (!inlinePlayer.session)
						return
					if (!value) {
						webPlayer.missingStatePolls += 1
						if (webPlayer.missingStatePolls === 20)
							inlinePlayer.session.reportError(qsTr("This provider did not expose playback controls. Open it externally instead."))
						return
					}
					webPlayer.missingStatePolls = 0
					inlinePlayer.session.reportPlaybackState(value.position, value.duration, value.paused)
				})
			}
		}
	}

	Rectangle {
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && session.state === "loading" && session.error.length === 0
		color: Theme.shellBackground
		z: 4
		ColumnLayout {
			anchors.centerIn: parent
			spacing: Theme.space2
			ModernBusyIndicator {
				Layout.alignment: Qt.AlignHCenter
				running: parent.parent.visible
				Accessible.name: qsTr("Loading inline media")
			}
			Label {
				textFormat: Text.PlainText
				text: session && session.loadProgress > 0
					? qsTr("Loading · %1%").arg(session.loadProgress) : qsTr("Loading media…")
				color: Theme.textMuted
			}
		}
	}

	Rectangle {
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && session.error.length > 0
		color: Theme.shellBackground
		z: 5
		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space5 * 2, 480)
			spacing: Theme.space3
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Media playback failed")
				color: Theme.textStrong
				font.bold: true
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: session ? session.error : ""
				color: Theme.textMuted
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
				Accessible.role: Accessible.AlertMessage
			}
			RowLayout {
				Layout.alignment: Qt.AlignHCenter
				ModernButton { text: qsTr("Retry"); onClicked: session.retry() }
				ModernButton {
					text: qsTr("Open externally")
					onClicked: Qt.openUrlExternally(inlinePlayer.externalMediaUrl())
				}
			}
		}
	}

	MediaSessionControls {
		id: inlineControls
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		session: inlinePlayer.session
		fullscreenAvailable: false
		externalAvailable: inlinePlayer.externalMediaUrl().length > 0
		onExternalRequested: Qt.openUrlExternally(inlinePlayer.externalMediaUrl())
		onExitConfirmed: disposition => {
			if (disposition === "end-shared") session.endShared()
			else if (disposition === "leave-shared") session.leaveShared()
			else session.closePlayer()
		}
	}
}
