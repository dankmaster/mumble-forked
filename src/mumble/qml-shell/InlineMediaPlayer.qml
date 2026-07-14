pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0

Rectangle {
	id: inlinePlayer

	required property var session
	property string aspect: "wide"
	readonly property bool ready: !!session && session.active && !session.detached
	readonly property string normalizedAspect: normalizeAspect(aspect)
	readonly property int mediaGeneration: _mediaGeneration
	readonly property bool documentReady: _documentReadyGeneration === _mediaGeneration
		&& _mediaGeneration > 0 && _rendererHealthy
	readonly property bool statePollInFlight: _statePollGeneration === _mediaGeneration
		&& _mediaGeneration > 0
	readonly property bool rendererHealthy: _rendererHealthy
	property int _mediaGeneration: 0
	property int _documentReadyGeneration: -1
	property int _statePollGeneration: -1
	property int _missingStatePolls: 0
	property bool _rendererHealthy: false
	property string _documentUrl: ""
	color: Theme.mediaCanvas
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

	function resetMediaLifecycle(documentUrl, rendererIsHealthy) {
		_mediaGeneration += 1
		_documentUrl = String(documentUrl || "")
		_documentReadyGeneration = -1
		_statePollGeneration = -1
		_missingStatePolls = 0
		_rendererHealthy = !!rendererIsHealthy
		return _mediaGeneration
	}

	function beginMediaDocumentLoad(documentUrl) {
		return resetMediaLifecycle(documentUrl, true)
	}

	function invalidateMediaDocument(expectedGeneration) {
		if (expectedGeneration !== undefined && expectedGeneration >= 0
				&& expectedGeneration !== _mediaGeneration)
			return false
		resetMediaLifecycle("", false)
		return true
	}

	function markMediaDocumentReady(generation) {
		if (generation !== _mediaGeneration || !_rendererHealthy)
			return false
		_documentReadyGeneration = generation
		_missingStatePolls = 0
		return true
	}

	function failMediaDocument(generation) {
		return invalidateMediaDocument(generation)
	}

	function claimStatePoll(generation) {
		if (generation !== _mediaGeneration || !documentReady
				|| _statePollGeneration >= 0)
			return false
		_statePollGeneration = generation
		return true
	}

	function cancelStatePoll(generation) {
		if (generation !== _mediaGeneration || _statePollGeneration !== generation)
			return false
		_statePollGeneration = -1
		return true
	}

	function completeStatePoll(generation, value) {
		if (generation !== _mediaGeneration || _statePollGeneration !== generation)
			return false
		_statePollGeneration = -1
		if (!ready || !documentReady || !session)
			return false
		if (!value) {
			_missingStatePolls += 1
			if (_missingStatePolls === 20) {
				failMediaDocument(generation)
				session.reportError(qsTr("This provider did not expose playback controls. Open it externally instead."))
			}
			return true
		}
		_missingStatePolls = 0
		const position = Number(value.position)
		const duration = Number(value.duration)
		session.reportPlaybackState(isFinite(position) ? position : 0,
			isFinite(duration) ? duration : 0, !!value.paused)
		return true
	}

	function runPlayerScript(script, expectedGeneration) {
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !ready || !documentReady
				|| !_rendererHealthy || !playerLoader.item)
			return false
		try {
			playerLoader.item.runJavaScript(script)
			return true
		} catch (error) {
			if (failMediaDocument(generation) && session)
				session.reportError(qsTr("The inline media player could not communicate with its renderer."))
			return false
		}
	}

	function applyDesiredPlaybackState(expectedGeneration) {
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !ready || !documentReady
				|| !session.playbackControllable)
			return
		runPlayerScript(playerScript("volume", session.volume), generation)
		runPlayerScript(playerScript("mute", session.muted ? 1 : 0), generation)
		runPlayerScript(playerScript("seek", session.position), generation)
		runPlayerScript(playerScript(session.state === "playing" ? "play" : "pause", 0), generation)
	}

	function externalMediaUrl() {
		const value = String(session ? session.url : "").trim()
		return /^https:\/\//i.test(value) ? value : ""
	}

	function focusInitialControl() {
		if (failureOverlay.visible && retryButton.visible && retryButton.enabled) {
			retryButton.forceActiveFocus()
			return retryButton.activeFocus
		}
		return inlineControls.focusInitialControl()
	}

	function focusFailureControl() {
		if (!failureOverlay.visible || !retryButton.visible || !retryButton.enabled)
			return false
		retryButton.forceActiveFocus()
		return retryButton.activeFocus
	}

	function normalizeAspect(value) {
		const normalized = String(value || "").trim().toLowerCase()
		return [ "wide", "twitch", "short", "square", "audio", "compact-audio" ]
			.indexOf(normalized) >= 0 ? normalized : "wide"
	}

	function fittedMediaWidth(availableWidth, availableHeight) {
		if (availableWidth <= 0 || availableHeight <= 0)
			return 0
		if (normalizedAspect === "audio" || normalizedAspect === "compact-audio")
			return availableWidth
		const ratio = normalizedAspect === "short" ? 9 / 16
			: normalizedAspect === "square" ? 1 : 16 / 9
		return Math.min(availableWidth, availableHeight * ratio)
	}

	function fittedMediaHeight(availableWidth, availableHeight) {
		if (availableWidth <= 0 || availableHeight <= 0)
			return 0
		if (normalizedAspect === "audio")
			return Math.min(availableHeight, Math.min(352, Math.max(220, availableWidth * 0.61)))
		if (normalizedAspect === "compact-audio")
			return Math.min(availableHeight, Math.min(166, Math.max(128, availableWidth * 0.29)))
		const ratio = normalizedAspect === "short" ? 9 / 16
			: normalizedAspect === "square" ? 1 : 16 / 9
		return fittedMediaWidth(availableWidth, availableHeight) / ratio
	}

	onSessionChanged: {
		invalidateMediaDocument()
		if (ready && playerLoader.item) {
			playerLoader.item.loadGeneration = beginMediaDocumentLoad(session.url)
			playerLoader.item.reload()
		}
	}
	onReadyChanged: if (!ready) invalidateMediaDocument()
	Component.onDestruction: invalidateMediaDocument()
	Connections {
		target: inlinePlayer.session
		ignoreUnknownSignals: true
		function onErrorChanged() {
			if (String(inlinePlayer.session ? inlinePlayer.session.error || "" : "").length > 0)
				Qt.callLater(inlinePlayer.focusFailureControl)
		}
		function onStateChanged() {
			if (inlinePlayer.session && inlinePlayer.session.state === "error")
				Qt.callLater(inlinePlayer.focusFailureControl)
		}
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

	Rectangle {
		id: playerCanvas
		objectName: "inlineMediaCanvas"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: inlineToolbar.bottom
		anchors.bottom: inlineControls.top
		color: Theme.mediaCanvas
		clip: true
	}

	Loader {
		id: playerLoader
		objectName: "inlineMediaWebSurface"
		parent: playerCanvas
		width: inlinePlayer.fittedMediaWidth(playerCanvas.width, playerCanvas.height)
		height: inlinePlayer.fittedMediaHeight(playerCanvas.width, playerCanvas.height)
		x: Math.round((playerCanvas.width - width) / 2)
		y: Math.round((playerCanvas.height - height) / 2)
		active: inlinePlayer.ready
			&& String(inlinePlayer.session ? inlinePlayer.session.error || "" : "").length === 0
		sourceComponent: WebEngineView {
			id: webPlayer
			property int loadGeneration: -1
			profile: mediaProfiles.videoProfile
			url: inlinePlayer.session ? inlinePlayer.session.url : ""
			settings.playbackRequiresUserGesture: false
			Accessible.name: qsTr("Embedded media provider playback")
			onLoadProgressChanged: if (loadGeneration === inlinePlayer.mediaGeneration
					&& inlinePlayer.rendererHealthy && inlinePlayer.session)
				inlinePlayer.session.reportLoadProgress(loadProgress)
			onLoadingChanged: function(request) {
				if (request.status === WebEngineView.LoadStartedStatus) {
					loadGeneration = inlinePlayer.beginMediaDocumentLoad(request.url)
					return
				}
				const generation = loadGeneration
				if (!inlinePlayer.session || generation !== inlinePlayer.mediaGeneration)
					return
				if (request.status === WebEngineView.LoadFailedStatus) {
					if (inlinePlayer.failMediaDocument(generation))
						inlinePlayer.session.reportError(request.errorString || qsTr("The media provider could not be loaded."))
				}
				else if (request.status === WebEngineView.LoadSucceededStatus) {
					if (!inlinePlayer.markMediaDocumentReady(generation))
						return
					inlinePlayer.session.reportLoadProgress(100)
					Qt.callLater(function() { inlinePlayer.applyDesiredPlaybackState(generation) })
				}
			}
			onRenderProcessTerminated: {
				const generation = loadGeneration
				if (generation === inlinePlayer.mediaGeneration)
					webPlayer.stop()
				if (inlinePlayer.failMediaDocument(generation) && inlinePlayer.session)
					inlinePlayer.session.reportError(qsTr("The inline media renderer stopped unexpectedly."))
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
				function onRetryRequested() {
					webPlayer.loadGeneration = inlinePlayer.beginMediaDocumentLoad(webPlayer.url)
					webPlayer.reload()
				}
				function onUrlChanged() {
					webPlayer.loadGeneration = inlinePlayer.beginMediaDocumentLoad(inlinePlayer.session.url)
				}
			}
			Timer {
				interval: 500
				running: inlinePlayer.ready && inlinePlayer.session.playbackControllable
					&& inlinePlayer.session.state !== "error" && inlinePlayer.documentReady
					&& inlinePlayer.rendererHealthy
				repeat: true
				onTriggered: {
					const generation = inlinePlayer.mediaGeneration
					if (!inlinePlayer.claimStatePoll(generation))
						return
					try {
						webPlayer.runJavaScript(inlinePlayer.playerScript("state", 0), function(value) {
							inlinePlayer.completeStatePoll(generation, value)
						})
					} catch (error) {
						inlinePlayer.cancelStatePoll(generation)
						if (inlinePlayer.failMediaDocument(generation) && inlinePlayer.session)
							inlinePlayer.session.reportError(qsTr("The inline media player could not communicate with its renderer."))
					}
				}
			}
			Component.onDestruction: inlinePlayer.invalidateMediaDocument(loadGeneration)
		}
	}

	Rectangle {
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && session.state === "loading" && session.error.length === 0
		color: Theme.mediaCanvas
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
		id: failureOverlay
		objectName: "inlineMediaFailureOverlay"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && session.error.length > 0
		color: Theme.mediaCanvas
		z: 5
		onVisibleChanged: if (visible) Qt.callLater(inlinePlayer.focusFailureControl)
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
				ModernButton {
					id: retryButton
					objectName: "inlineMediaRetryButton"
					text: qsTr("Retry")
					tone: "accent"
					Accessible.description: qsTr("Reload the current provider player")
					onClicked: session.retry()
				}
				ModernButton {
					objectName: "inlineMediaFailureExternalButton"
					visible: inlinePlayer.externalMediaUrl().length > 0
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
