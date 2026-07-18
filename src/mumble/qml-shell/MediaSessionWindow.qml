import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

ApplicationWindow {
    id: mediaWindow
	property string visualFixtureMode: ""
	property var mediaProfileFactory: typeof mediaProfiles !== "undefined" ? mediaProfiles : null
	readonly property var providerPresentation: ProviderPresentation.resolve(mediaSession.provider)
	readonly property string providerLabel: providerPresentation.label
		|| String(mediaSession.provider || "").trim() || qsTr("Media")
	readonly property string providerMark: providerPresentation.mark
		|| fallbackProviderMark(providerLabel)
	readonly property color providerAccent: providerPresentation.accent || Theme.accent
	readonly property color providerAccentSubtle: Theme.withAlpha(providerAccent, 0.16)
	readonly property color providerAccentBorder: Theme.withAlpha(providerAccent, 0.56)
	readonly property color providerOnAccent: Theme.contrastText(providerAccent)
	readonly property bool compactProviderChrome: width < 720
		|| mediaAspect === "compact-audio"
	readonly property string normalizedVisualFixtureMode: String(visualFixtureMode || "").toLowerCase()
	readonly property bool visualFixtureRendererReady: normalizedVisualFixtureMode === "active"
		|| normalizedVisualFixtureMode === "controls"
	readonly property bool mediaRuntimeContractAvailable: !!mediaProfileFactory
		&& typeof mediaProfileFactory.runtimeReady !== "undefined"
	readonly property bool mediaRuntimeReady: !mediaRuntimeContractAvailable
		|| !!mediaProfileFactory.runtimeReady
	readonly property string mediaRuntimeError: mediaRuntimeContractAvailable
		? String(mediaProfileFactory.runtimeError || "") : ""
	readonly property string effectiveMediaError: String(mediaSession.error || "").length > 0
		? String(mediaSession.error) : nativeDirectMedia ? "" : mediaRuntimeError
	readonly property string mediaAspect: inferMediaAspect()
	readonly property string surfaceId: "mediaSession.window"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property bool hasMediaSource: String(mediaSession.url || "").trim().length > 0
	readonly property bool adaptiveManifest: {
		const mime = String(mediaSession.mediaMime || "").split(";", 1)[0].trim().toLowerCase()
		return String(mediaSession.provider || "").toLowerCase() === "direct"
			&& (mime === "application/vnd.apple.mpegurl" || mime === "application/dash+xml")
	}
	readonly property bool nativeDirectMedia: providerSurfaceRequested
		&& String(mediaSession.provider || "").toLowerCase() === "direct"
		&& !adaptiveManifest
	readonly property string rendererDocumentUrl: playbackDocumentUrl()
	readonly property bool providerSurfaceRequested: normalizedVisualFixtureMode.length === 0
		&& mediaSession.active && hasMediaSource && mediaSession.error.length === 0
	readonly property bool providerSurfaceAllowed: providerSurfaceRequested
		&& !nativeDirectMedia && mediaRuntimeReady
	readonly property bool webSurfaceActive: playerLoader.active
	readonly property bool nativeSurfaceActive: nativePlayerLoader.active
	readonly property string rendererBackend: visualFixtureRendererReady ? "fixture"
		: nativeSurfaceActive ? "native" : webSurfaceActive ? "webengine" : "none"
	readonly property bool secondaryAudioActive: nativeSurfaceActive && nativePlayerLoader.item
		? nativePlayerLoader.item.secondaryAudioActive : audioPlayerLoader.active
	readonly property bool sharedGuestPlaybackLocked: mediaSession.sharedAvailable
		&& mediaSession.sharedJoined && !mediaSession.sharedHost
	readonly property bool providerInputEnabled: !sharedGuestPlaybackLocked
	property string _webSecondaryAudioWarning: ""
	readonly property string secondaryAudioWarning: nativeSurfaceActive && nativePlayerLoader.item
		? String(nativePlayerLoader.item.secondaryAudioWarning || "")
		: _webSecondaryAudioWarning
	readonly property bool secondaryAudioDegraded: secondaryAudioWarning.length > 0
	readonly property string secondaryAudioState: nativeSurfaceActive && nativePlayerLoader.item
		? String(nativePlayerLoader.item.secondaryAudioState || "idle")
		: secondaryAudioDegraded ? "degraded"
		: mediaSession.audioUrl.toString().length === 0 ? "idle"
		: secondaryAudioActive && audioPlayerLoader.item && audioPlayerLoader.item.documentReady
			? "active" : secondaryAudioActive ? "loading" : "idle"
	readonly property string rendererState: normalizedVisualFixtureMode.length > 0
		? (visualFixtureRendererReady ? "active"
			: normalizedVisualFixtureMode === "loading" ? "loading" : "error")
		: !mediaSession.active ? "empty"
		: mediaSession.error.length > 0 ? "error"
		: !nativeDirectMedia && mediaRuntimeError.length > 0 ? "error"
		: !hasMediaSource ? "empty"
		: !nativeDirectMedia && !mediaRuntimeReady ? "loading"
		: !documentReady ? "loading" : "active"
	readonly property int mediaGeneration: _mediaGeneration
	readonly property bool documentReady: visualFixtureRendererReady
		|| (nativeDirectMedia && nativePlayerLoader.item
			&& nativePlayerLoader.item.documentReady)
		|| (!nativeDirectMedia && _documentReadyGeneration === _mediaGeneration
			&& _mediaGeneration > 0 && _rendererHealthy)
	readonly property bool statePollInFlight: _statePollGeneration === _mediaGeneration
		&& _mediaGeneration > 0 && _statePollToken >= 0
	readonly property int statePollToken: _statePollToken
	readonly property bool secondaryAudioStatePollInFlight:
		_audioStatePollGeneration === _mediaGeneration
		&& _mediaGeneration > 0 && _audioStatePollToken >= 0
	readonly property int secondaryAudioStatePollToken: _audioStatePollToken
	readonly property bool rendererHealthy: visualFixtureRendererReady
		|| (nativeDirectMedia && nativePlayerLoader.item
			&& nativePlayerLoader.item.rendererHealthy)
		|| (!nativeDirectMedia && _rendererHealthy)
	readonly property string mediaDocumentUrl: _documentUrl
	property string desiredPlaybackState: ""
	property int transportRetryCount: 0
	property int _mediaGeneration: 0
	property int _documentReadyGeneration: -1
	property int _documentReadyProbeGeneration: -1
	property double _documentReadyProbeStartedAt: 0
	property int documentReadyProbeAttempts: 0
	property string documentReadyProbeState: "idle"
	property int statePollTimeoutMs: 3000
	property int _statePollGeneration: -1
	property int _statePollToken: -1
	property int _nextStatePollToken: 0
	property int _missingStatePolls: 0
	property int _audioStatePollGeneration: -1
	property int _audioStatePollToken: -1
	property int _nextAudioStatePollToken: 0
	property int _audioMissingStatePolls: 0
	property bool _rendererHealthy: false
	property string _documentUrl: ""

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
	palette.active.highlight: providerAccent
	palette.inactive.highlight: providerAccent
	palette.active.highlightedText: providerOnAccent
	palette.inactive.highlightedText: providerOnAccent
	palette.placeholderText: Theme.textMuted
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.active.link: providerAccent
	palette.inactive.link: providerAccent
	palette.active.linkVisited: Theme.mixColors(providerAccent, Theme.textStrong, 0.18)
	palette.inactive.linkVisited: Theme.mixColors(providerAccent, Theme.textStrong, 0.18)
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
	palette.disabled.highlight: Theme.withAlpha(providerAccent, 0.38)
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
	           ? qsTr("%1 · %2").arg(providerLabel).arg(mediaSession.sharedTitle)
	           : qsTr("%1 player").arg(providerLabel)
	    color: Theme.shellBackground
	Component.onCompleted: {
		applyInitialWindowSize()
		Qt.callLater(controls.focusInitialControl)
	}
	onSharedGuestPlaybackLockedChanged: if (sharedGuestPlaybackLocked)
		Qt.callLater(controls.focusInitialControl)
	Component.onDestruction: {
		if (nativePlayerLoader.item)
			nativePlayerLoader.item.shutdown()
		invalidateMediaDocument()
	}
	onClosing: function(close) { handleWindowClosing(close) }

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

	function resetMediaLifecycle(documentUrl, rendererIsHealthy) {
		_webSecondaryAudioWarning = ""
		_mediaGeneration += 1
		_documentUrl = String(documentUrl || "")
		_documentReadyGeneration = -1
		_documentReadyProbeGeneration = -1
		_documentReadyProbeStartedAt = 0
		documentReadyProbeAttempts = 0
		documentReadyProbeState = "idle"
		resetStatePoll()
		resetAudioStatePoll()
		_missingStatePolls = 0
		_audioMissingStatePolls = 0
		_rendererHealthy = !!rendererIsHealthy
		desiredPlaybackState = ""
		transportRetryCount = 0
		return _mediaGeneration
	}

	function beginMediaDocumentLoad(documentUrl) {
		return resetMediaLifecycle(documentUrl, true)
	}

	function prepareMediaDocumentLoad(generation, documentUrl) {
		if (generation !== _mediaGeneration)
			return false
		_documentUrl = String(documentUrl || "")
		_documentReadyGeneration = -1
		_documentReadyProbeGeneration = -1
		resetStatePoll()
		resetAudioStatePoll()
		_missingStatePolls = 0
		_audioMissingStatePolls = 0
		_rendererHealthy = true
		return true
	}

	function invalidateMediaDocument(expectedGeneration) {
		if (expectedGeneration !== undefined && expectedGeneration >= 0
				&& expectedGeneration !== _mediaGeneration)
			return false
		resetMediaLifecycle("", false)
		return true
	}

	function invalidateMediaDocumentIfNeeded() {
		if (_documentUrl.length === 0 && !_rendererHealthy
				&& _documentReadyGeneration < 0 && _statePollGeneration < 0)
			return false
		return invalidateMediaDocument()
	}

	function markMediaDocumentReady(generation) {
		if (generation !== _mediaGeneration || !_rendererHealthy)
			return false
		_documentReadyGeneration = generation
		_missingStatePolls = 0
		return true
	}

	function completeMediaDocumentLoad(generation) {
		if (!markMediaDocumentReady(generation))
			return false
		_documentReadyProbeGeneration = -1
		if (playerLoader.item && playerLoader.item.loadGeneration === generation)
			playerLoader.item.documentReady = true
		mediaSession.reportLoadProgress(100)
		Qt.callLater(function() { mediaWindow.applyDesiredPlaybackState(generation) })
		return true
	}

	function probeMediaDocumentReady(generation) {
		if (generation !== _mediaGeneration || documentReady
				|| _documentReadyProbeGeneration === generation || !playerLoader.item)
			return false
		const webPlayer = playerLoader.item
		_documentReadyProbeGeneration = generation
		_documentReadyProbeStartedAt = Date.now()
		documentReadyProbeAttempts += 1
		documentReadyProbeState = "submitted"
		try {
			webPlayer.runJavaScript(
				"(function(){const state=String(document.readyState||'');"
				+ "const media=document.querySelector('audio,video');"
				+ "const adaptiveExpected=" + (adaptiveManifest ? "true" : "false") + ";"
				+ "const adaptive=adaptiveExpected?(window.__mumbleAdaptiveState||null):null;"
				+ "const error=adaptive?String(adaptive.error||''):'';"
				+ "const ready=!!media&&(!adaptiveExpected||(adaptive&&adaptive.ready===true));"
				+ "return state+'|'+(ready?'media':'none')+'|'+error;})()",
				function(value) {
					if (mediaWindow._documentReadyProbeGeneration !== generation)
						return
					mediaWindow._documentReadyProbeGeneration = -1
					mediaWindow._documentReadyProbeStartedAt = 0
					const result = String(value || "")
					mediaWindow.documentReadyProbeState = "callback:" + result
					const parts = result.split("|")
					const documentIsReady = parts[0] === "interactive" || parts[0] === "complete"
					const mediaIsPresent = parts.length > 1 && parts[1] === "media"
					const adaptiveError = parts.length > 2 ? parts.slice(2).join("|").trim() : ""
					if (generation === mediaWindow.mediaGeneration && mediaWindow.adaptiveManifest
							&& adaptiveError.length > 0) {
						if (mediaWindow.failMediaDocument(generation))
							mediaSession.reportTypedError("adaptive-renderer-failed", adaptiveError)
						return
					}
					if (generation === mediaWindow.mediaGeneration && mediaWindow.adaptiveManifest
							&& !mediaIsPresent && mediaWindow.documentReadyProbeAttempts >= 400) {
						if (mediaWindow.failMediaDocument(generation))
							mediaSession.reportTypedError("adaptive-renderer-timeout",
								qsTr("The stream could not be prepared for playback."))
						return
					}
					if (generation !== mediaWindow.mediaGeneration || mediaWindow.documentReady
							|| !documentIsReady
							|| (String(mediaSession.provider || "") === "direct" && !mediaIsPresent))
						return
					mediaWindow.completeMediaDocumentLoad(generation)
				})
			return true
		} catch (error) {
			if (_documentReadyProbeGeneration === generation)
				_documentReadyProbeGeneration = -1
			_documentReadyProbeStartedAt = 0
			documentReadyProbeState = "error:" + String(error)
			return false
		}
	}

	function failMediaDocument(generation) {
		return invalidateMediaDocument(generation)
	}

	function handleWindowClosing(close) {
		// Loader teardown follows closePlayer(), which has already made the backend
		// inactive. Accept that lifecycle close so the isolated WebEngine window and
		// renderer can actually be destroyed. An active window close is still a user
		// intent: veto it and keep the explicit shared leave/end choice unchanged.
		if (!mediaSession.active) {
			close.accepted = true
			return
		}
		close.accepted = false
		controls.requestClose()
	}

	function fallbackProviderMark(label) {
		const words = String(label || "").trim().split(/\s+/).filter(function(word) {
			return word.length > 0
		})
		if (words.length === 0)
			return qsTr("Media")
		if (words.length === 1)
			return words[0].substring(0, Math.min(4, words[0].length)).toUpperCase()
		return words.slice(0, 2).map(function(word) { return word.charAt(0) })
			.join("").toUpperCase()
	}

	function detachedProviderAccessibleName() {
		if (mediaSession.sharedAvailable && mediaSession.sharedJoined)
			return mediaSession.sharedHost
				? qsTr("%1 media player, hosting watch together").arg(providerLabel)
				: qsTr("%1 media player, synchronized with host").arg(providerLabel)
		return qsTr("%1 media player, detached window").arg(providerLabel)
	}

	function reportSecondaryAudioError(generation, message) {
		if (generation !== _mediaGeneration || !mediaSession.active
				|| mediaSession.error.length > 0 || !documentReady)
			return false
		resetAudioStatePoll()
		_audioMissingStatePolls = 0
		if (audioPlayerLoader.item) {
			audioPlayerLoader.item.documentReady = false
			audioPlayerLoader.item.stop()
		}
		const detail = String(message || "").trim()
		_webSecondaryAudioWarning = detail.length > 0
			? qsTr("The separate audio track could not play: %1").arg(detail)
			: qsTr("The separate audio track could not be played.")
		return true
	}

	function claimStatePoll(generation) {
		if (generation !== _mediaGeneration || !documentReady
				|| _statePollGeneration >= 0)
			return false
		_nextStatePollToken += 1
		_statePollGeneration = generation
		_statePollToken = _nextStatePollToken
		statePollWatchdog.pollGeneration = generation
		statePollWatchdog.pollToken = _statePollToken
		statePollWatchdog.restart()
		return true
	}

	function resetStatePoll() {
		_statePollGeneration = -1
		_statePollToken = -1
		statePollWatchdog.stop()
		statePollWatchdog.pollGeneration = -1
		statePollWatchdog.pollToken = -1
	}

	function cancelStatePoll(generation, pollToken) {
		const token = pollToken === undefined ? _statePollToken : pollToken
		if (generation !== _mediaGeneration || _statePollGeneration !== generation
				|| token !== _statePollToken)
			return false
		resetStatePoll()
		return true
	}

	function expireStatePoll(generation, pollToken) {
		return completeStatePoll(generation, null, pollToken)
	}

	function playbackDocumentUrl() {
		if (mediaProfileFactory && typeof mediaProfileFactory.videoDocumentUrl !== "undefined") {
			const documentUrl = String(mediaProfileFactory.videoDocumentUrl || "")
			if (documentUrl.length > 0)
				return documentUrl
		}
		return String(mediaSession.url || "")
	}

	function navigationRequestAllowed(requestUrl, firstPartyUrl) {
		if (mediaProfileFactory
				&& typeof mediaProfileFactory.isNavigationRequestAllowed === "function")
			return mediaProfileFactory.isNavigationRequestAllowed(requestUrl, firstPartyUrl)
		return mediaSession && typeof mediaSession.isNavigationAllowed === "function"
			? mediaSession.isNavigationAllowed(requestUrl) : false
	}

	function requestUrlMatches(requestUrl, expectedUrl, acceptedUrl,
			acceptedGeneration, callbackGeneration) {
		const requestValue = String(requestUrl || "")
		if (requestValue.length === 0)
			return false
		if (callbackGeneration > 0 && callbackGeneration !== _mediaGeneration)
			return false
		if (requestValue === String(expectedUrl || ""))
			return true
		// Redirects and URL canonicalization are accepted only after the WebEngine
		// navigation signal admitted that exact URL for this document generation.
		// Resetting the generation also clears this capability, so a late callback
		// from the previous source cannot complete the replacement document.
		if (acceptedGeneration !== callbackGeneration
				|| requestValue !== String(acceptedUrl || ""))
			return false
		return navigationRequestAllowed(requestUrl, expectedUrl)
	}

	function retryMediaRenderer() {
		if (mediaRuntimeError.length > 0 && mediaProfileFactory
				&& typeof mediaProfileFactory.retryRuntime === "function")
			mediaProfileFactory.retryRuntime()
		mediaSession.retry()
	}

	function completeStatePoll(generation, value, pollToken) {
		const token = pollToken === undefined ? _statePollToken : pollToken
		if (generation !== _mediaGeneration || _statePollGeneration !== generation
				|| token !== _statePollToken)
			return false
		resetStatePoll()
		if (!mediaSession.active || !documentReady || mediaSession.error.length > 0)
			return false
		if (!value) {
			_missingStatePolls += 1
			if (_missingStatePolls === 20 && failMediaDocument(generation))
				mediaSession.reportError(qsTr("This provider did not expose playback controls. Open it externally instead."))
			return true
		}
		_missingStatePolls = 0
		const playbackError = String(value.error || "").trim()
		if (playbackError.length > 0) {
			desiredPlaybackState = ""
			if (failMediaDocument(generation))
				mediaSession.reportError(qsTr("Playback could not start: %1").arg(playbackError))
			return true
		}
		const position = Number(value.position)
		const duration = Number(value.duration)
		const paused = !!value.paused
		const wantsPlaying = desiredPlaybackState === "playing"
		const wantsPaused = desiredPlaybackState === "paused"
		if ((wantsPlaying && paused) || (wantsPaused && !paused)) {
			if (transportRetryCount < 12) {
				transportRetryCount += 1
				runOnPlayers(playerScript(wantsPlaying ? "play" : "pause", 0), generation)
				return true
			}
			desiredPlaybackState = ""
			if (failMediaDocument(generation)) {
				mediaSession.reportError(wantsPlaying
					? qsTr("Playback was blocked by the media provider. Open it in your browser instead.")
					: qsTr("The media provider did not accept the pause command."))
			}
			return true
		}
		desiredPlaybackState = ""
		transportRetryCount = 0
		mediaSession.reportPlaybackState(isFinite(position) ? position : 0,
			isFinite(duration) ? duration : 0, paused)
		syncSecondaryAudio(isFinite(position) ? position : 0, paused, generation)
		return true
	}

	function playerScript(command, value) {
		const provider = JSON.stringify(String(mediaSession.provider || ""))
		const numericValue = Number(value || 0)
		return "(function(){const provider=" + provider + ";"
			+ "const yt=document.getElementById('movie_player')||document.querySelector('.html5-video-player');"
			+ "const isYt=provider==='youtube'&&yt&&typeof yt.getPlayerState==='function';"
			+ "const media=document.querySelector('video,audio');"
			+ (command === "play"
				? "window.__mumbleMediaPlayError='';if(isYt&&typeof yt.playVideo==='function'){yt.playVideo();return true;}if(media){const result=media.play();if(result&&typeof result.catch==='function')result.catch(function(error){window.__mumbleMediaPlayError=String(error&&error.message||error||'Playback was blocked');});return true;}return false;"
				: command === "pause"
				? "if(isYt&&typeof yt.pauseVideo==='function'){yt.pauseVideo();return true;}if(media){media.pause();return true;}return false;"
				: command === "seek"
				? "const target=" + numericValue + ";if(isYt&&typeof yt.seekTo==='function'){yt.seekTo(target,true);return true;}if(media){media.currentTime=target;return true;}return false;"
				: command === "volume"
				? "const target=Math.max(0,Math.min(100," + numericValue + "));if(isYt&&typeof yt.setVolume==='function'){yt.setVolume(target);return true;}if(media){media.volume=target/100;return true;}return false;"
				: command === "mute"
				? "const muted=" + (numericValue > 0 ? "true" : "false") + ";if(isYt){if(muted&&typeof yt.mute==='function')yt.mute();else if(!muted&&typeof yt.unMute==='function')yt.unMute();return true;}if(media){media.muted=muted;return true;}return false;"
				: "const adaptiveError=String(window.__mumbleAdaptiveState&&window.__mumbleAdaptiveState.error||'');const playbackError=adaptiveError||String(window.__mumbleMediaPlayError||'');if(isYt){const state=yt.getPlayerState();return {position:Number(yt.getCurrentTime()||0),duration:Number(yt.getDuration()||0),paused:state!==1&&state!==3,error:playbackError};}"
				  + "if(media)return {position:Number(media.currentTime||0),duration:isFinite(media.duration)?Number(media.duration):0,paused:!!media.paused,error:playbackError};return null;")
			+ "})()"
	}

	function runOnPlayers(script, expectedGeneration) {
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !mediaSession.active || !documentReady
				|| !_rendererHealthy || mediaSession.error.length > 0 || !playerLoader.item
				|| playerLoader.item.loadGeneration !== generation)
			return false
		try {
			playerLoader.item.runJavaScript(script)
		} catch (error) {
			if (failMediaDocument(generation))
				mediaSession.reportError(qsTr("The media player stopped responding."))
			return false
		}
		if (audioPlayerLoader.item && !secondaryAudioDegraded
				&& audioPlayerLoader.item.loadGeneration === generation) {
			try {
				audioPlayerLoader.item.runJavaScript(script)
			} catch (error) {
				reportSecondaryAudioError(generation,
					qsTr("The media window lost contact with the separate audio track."))
			}
		}
		return true
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

	function applyDesiredPlaybackState(expectedGeneration) {
		if (nativeDirectMedia && nativePlayerLoader.item) {
			nativePlayerLoader.item.applySessionState()
			if (Number(mediaSession.position || 0) > 0.05)
				nativePlayerLoader.item.seek(mediaSession.position)
			return
		}
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !mediaSession.playbackControllable)
			return
		runOnPlayers(playerScript("volume", mediaSession.volume), generation)
		runOnPlayers(playerScript("mute", mediaSession.muted ? 1 : 0), generation)
		if (Number(mediaSession.position || 0) > 0.05)
			runOnPlayers(playerScript("seek", mediaSession.position), generation)
		requestPlaybackState(mediaSession.state === "playing" ? "playing" : "paused", generation)
	}

	function claimAudioStatePoll(generation) {
		if (generation !== _mediaGeneration || _audioStatePollGeneration >= 0)
			return false
		_nextAudioStatePollToken += 1
		_audioStatePollGeneration = generation
		_audioStatePollToken = _nextAudioStatePollToken
		audioStatePollWatchdog.pollGeneration = generation
		audioStatePollWatchdog.pollToken = _audioStatePollToken
		audioStatePollWatchdog.restart()
		return true
	}

	function resetAudioStatePoll() {
		_audioStatePollGeneration = -1
		_audioStatePollToken = -1
		audioStatePollWatchdog.stop()
		audioStatePollWatchdog.pollGeneration = -1
		audioStatePollWatchdog.pollToken = -1
	}

	function cancelAudioStatePoll(generation, pollToken) {
		const token = pollToken === undefined ? _audioStatePollToken : pollToken
		if (generation !== _mediaGeneration || _audioStatePollGeneration !== generation
				|| token !== _audioStatePollToken)
			return false
		resetAudioStatePoll()
		return true
	}

	function recordMissingAudioStatePoll(generation) {
		if (generation !== _mediaGeneration || !mediaSession.active)
			return false
		_audioMissingStatePolls += 1
		if (_audioMissingStatePolls === 20)
			reportSecondaryAudioError(generation,
				qsTr("The separate audio track did not provide playback controls."))
		return true
	}

	function expireAudioStatePoll(generation, pollToken) {
		if (!cancelAudioStatePoll(generation, pollToken))
			return false
		return recordMissingAudioStatePoll(generation)
	}

	function syncSecondaryAudio(position, paused, expectedGeneration) {
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		const audioPlayer = audioPlayerLoader.item
		if (generation !== _mediaGeneration || !audioPlayer || !audioPlayer.documentReady
				|| audioPlayer.loadGeneration !== generation || secondaryAudioStatePollInFlight)
			return false
		if (!claimAudioStatePoll(generation))
			return false
		const pollToken = secondaryAudioStatePollToken
		try {
			audioPlayer.runJavaScript(playerScript("state", 0), function(value) {
				const currentAudioPlayer = audioPlayerLoader.item
				if (generation !== mediaWindow.mediaGeneration || !mediaSession.active
						|| !currentAudioPlayer || currentAudioPlayer.loadGeneration !== generation)
					return
				if (!mediaWindow.cancelAudioStatePoll(generation, pollToken))
					return
				if (!value) {
					mediaWindow.recordMissingAudioStatePoll(generation)
					return
				}
				mediaWindow._audioMissingStatePolls = 0
				const playbackError = String(value.error || "").trim()
				if (playbackError.length > 0) {
					mediaWindow.reportSecondaryAudioError(generation, playbackError)
					return
				}
				const target = Math.max(0, Number(position || 0))
				audioPlayer.runJavaScript(
					"(function(){const m=document.querySelector('audio,video');if(!m)return;"
					+ "const target=" + target + ";"
					+ "if(Math.abs((m.currentTime||0)-target)>0.15)m.currentTime=target;"
					+ (paused ? "m.pause();"
						: "window.__mumbleMediaPlayError='';const p=m.play();if(p&&p.catch)p.catch(function(error){window.__mumbleMediaPlayError=String(error&&error.message||error||'Playback was blocked');});")
					+ "})()")
			})
			return true
		} catch (error) {
			cancelAudioStatePoll(generation, pollToken)
			reportSecondaryAudioError(generation,
				qsTr("The media window lost contact with the separate audio track."))
			return false
		}
	}

	function handleMediaSourceChanged() {
		if (!mediaSession.active || !mediaSession.detached || !hasMediaSource) {
			if (nativePlayerLoader.item)
				nativePlayerLoader.item.shutdown()
			invalidateMediaDocumentIfNeeded()
			return
		}
		if (nativeDirectMedia) {
			if (nativePlayerLoader.item)
				nativePlayerLoader.item.retry()
			return
		}
		const nextUrl = rendererDocumentUrl
		const sameUrl = mediaDocumentUrl === nextUrl
		const generation = resetMediaLifecycle(nextUrl, !!playerLoader.item)
		if (playerLoader.item) {
			playerLoader.item.loadGeneration = generation
			playerLoader.item.acceptedNavigationGeneration = generation
			playerLoader.item.acceptedNavigationUrl = nextUrl
			if (sameUrl) {
				playerLoader.item.stop()
				playerLoader.item.reload()
			}
		}
		if (audioPlayerLoader.item) {
			audioPlayerLoader.item.stop()
			audioPlayerLoader.item.documentReady = false
			audioPlayerLoader.item.loadGeneration = generation
			audioPlayerLoader.item.acceptedNavigationGeneration = generation
			audioPlayerLoader.item.acceptedNavigationUrl = String(mediaSession.audioUrl || "")
			audioPlayerLoader.item.reload()
		}
	}

	function handleMediaRetryRequested() {
		if (nativeDirectMedia && nativePlayerLoader.item) {
			nativePlayerLoader.item.retry()
			return
		}
		const generation = beginMediaDocumentLoad(rendererDocumentUrl)
		if (playerLoader.item) {
			playerLoader.item.loadGeneration = generation
			playerLoader.item.acceptedNavigationGeneration = generation
			playerLoader.item.acceptedNavigationUrl = rendererDocumentUrl
			playerLoader.item.reload()
		}
		if (audioPlayerLoader.item) {
			audioPlayerLoader.item.stop()
			audioPlayerLoader.item.documentReady = false
			audioPlayerLoader.item.loadGeneration = generation
			audioPlayerLoader.item.acceptedNavigationGeneration = generation
			audioPlayerLoader.item.acceptedNavigationUrl = String(mediaSession.audioUrl || "")
			audioPlayerLoader.item.reload()
		}
	}

	function handleMediaSessionStateChanged() {
		if (mediaSession.active && mediaSession.error.length === 0)
			return
		if (nativePlayerLoader.item)
			nativePlayerLoader.item.shutdown()
		if (playerLoader.item)
			playerLoader.item.stop()
		if (audioPlayerLoader.item)
			audioPlayerLoader.item.stop()
		invalidateMediaDocumentIfNeeded()
	}

	Connections {
		target: mediaSession
		ignoreUnknownSignals: true
		function onSourceChanged() { mediaWindow.handleMediaSourceChanged() }
		function onStateChanged() { mediaWindow.handleMediaSessionStateChanged() }
		function onPlayRequested() { mediaWindow.requestPlaybackState("playing") }
		function onPauseRequested() { mediaWindow.requestPlaybackState("paused") }
		function onSeekRequested(seconds) {
			if (mediaWindow.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.seek(seconds)
			else mediaWindow.runOnPlayers(mediaWindow.playerScript("seek", seconds))
		}
		function onVolumeRequested(volume) {
			if (mediaWindow.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.setVolume(volume)
			else mediaWindow.runOnPlayers(mediaWindow.playerScript("volume", volume))
		}
		function onMutedRequested(muted) {
			if (mediaWindow.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.setMuted(muted)
			else mediaWindow.runOnPlayers(mediaWindow.playerScript("mute", muted ? 1 : 0))
		}
		function onRetryRequested() { mediaWindow.handleMediaRetryRequested() }
	}

	function requestPlaybackState(state, expectedGeneration) {
		if (nativeDirectMedia && nativePlayerLoader.item)
			return state === "playing" ? nativePlayerLoader.item.play()
				: nativePlayerLoader.item.pause()
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration)
			return false
		desiredPlaybackState = state === "playing" ? "playing" : "paused"
		transportRetryCount = 0
		return runOnPlayers(playerScript(desiredPlaybackState === "playing" ? "play" : "pause", 0), generation)
	}

	Shortcut {
		sequence: "F11"
		context: Qt.WindowShortcut
		onActivated: mediaWindow.setFullscreen(mediaWindow.visibility !== Window.FullScreen)
	}

	Shortcut {
		sequence: "Escape"
		context: Qt.WindowShortcut
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
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("%1 detached media player").arg(mediaWindow.providerLabel)
	}

	Rectangle {
		objectName: "mediaSessionVisualFixtureSurface"
		parent: playerCanvas
		x: playerLoader.x
		y: playerLoader.y
		width: playerLoader.width
		height: playerLoader.height
		visible: mediaWindow.visualFixtureRendererReady
		z: 2
		color: Theme.mediaCanvas
		gradient: Gradient {
			GradientStop { position: 0.0; color: Theme.withAlpha(mediaWindow.providerAccent, 0.34) }
			GradientStop { position: 0.55; color: Theme.withAlpha(Theme.panel, 0.96) }
			GradientStop { position: 1.0; color: Theme.mediaCanvas }
		}
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("Deterministic media playback preview")

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 * 2, 520)
			spacing: Theme.space4
			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: 88
				Layout.preferredHeight: 88
				radius: width / 2
				color: mediaWindow.providerAccentSubtle
				border.color: mediaWindow.providerAccentBorder
				Label {
					anchors.centerIn: parent
					text: mediaWindow.providerMark
					textFormat: Text.PlainText
					color: mediaWindow.providerAccent
					font.pixelSize: Theme.fontHeading
					font.weight: Font.Bold
				}
			}
			Label {
				objectName: "mediaSessionFixtureHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("%1 media session").arg(mediaWindow.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontHeading
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "mediaSessionFixtureDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("%1 provider playback · deterministic visual fixture")
					.arg(mediaWindow.providerMark)
				color: Theme.mediaOverlayTextMuted
				horizontalAlignment: Text.AlignHCenter
			}
		}
	}

	Loader {
		id: nativePlayerLoader
		objectName: "mediaSessionNativeSurface"
		parent: playerCanvas
		width: mediaWindow.fittedMediaWidth(playerCanvas.width, playerCanvas.height)
		height: mediaWindow.fittedMediaHeight(playerCanvas.width, playerCanvas.height)
		x: Math.round((playerCanvas.width - width) / 2)
		y: Math.round((playerCanvas.height - height) / 2)
		active: mediaWindow.nativeDirectMedia
		clip: true
		sourceComponent: NativeDirectMediaPlayer {
			session: mediaSession
			playbackInputEnabled: mediaWindow.providerInputEnabled
		}
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
		active: mediaWindow.providerSurfaceAllowed
		sourceComponent: WebEngineView {
            id: player
			property int loadGeneration: -1
			property int acceptedNavigationGeneration: -1
			property string acceptedNavigationUrl: ""
			property bool documentReady: false
			Accessible.name: qsTr("Media provider playback")
			profile: mediaWindow.mediaProfileFactory
				? mediaWindow.mediaProfileFactory.videoProfile : null
			url: mediaWindow.rendererDocumentUrl
			enabled: mediaWindow.providerInputEnabled
			activeFocusOnTab: mediaWindow.providerInputEnabled
			Accessible.ignored: !mediaWindow.providerInputEnabled
			settings.playbackRequiresUserGesture: false
			settings.localContentCanAccessRemoteUrls: mediaWindow.adaptiveManifest
			onLoadProgressChanged: {
				if (loadGeneration !== mediaWindow.mediaGeneration
						|| !mediaWindow.rendererHealthy || !mediaSession.active)
					return
				mediaSession.reportLoadProgress(loadProgress)
				if (loadProgress === 100)
					Qt.callLater(function() { mediaWindow.probeMediaDocumentReady(loadGeneration) })
			}
            onLoadingChanged: function(request) {
				const callbackGeneration = loadGeneration > 0 ? loadGeneration : mediaWindow.mediaGeneration
				if (!mediaWindow.requestUrlMatches(request.url, mediaWindow.rendererDocumentUrl,
						acceptedNavigationUrl, acceptedNavigationGeneration, callbackGeneration))
					return
				if (request.status === WebEngineView.LoadStartedStatus) {
					let generation = callbackGeneration
					if (generation <= 0)
						generation = mediaWindow.beginMediaDocumentLoad(mediaWindow.rendererDocumentUrl)
					loadGeneration = generation
					acceptedNavigationGeneration = generation
					acceptedNavigationUrl = String(request.url || "")
					mediaWindow.prepareMediaDocumentLoad(generation, request.url)
					documentReady = false
					return
				}
				const generation = loadGeneration
				if (generation !== mediaWindow.mediaGeneration)
					return
				if (request.status === WebEngineView.LoadFailedStatus) {
					documentReady = false
					if (mediaWindow.failMediaDocument(generation))
						mediaSession.reportError(request.errorString)
				} else if (request.status === WebEngineView.LoadSucceededStatus)
					mediaWindow.completeMediaDocumentLoad(generation)
            }
            onRenderProcessTerminated: function(status, exitCode) {
				const generation = loadGeneration
				documentReady = false
				if (generation === mediaWindow.mediaGeneration)
					player.stop()
				if (mediaWindow.failMediaDocument(generation))
					mediaSession.reportError(qsTr("The media player stopped unexpectedly."))
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
			onJavaScriptDialogRequested: function(request) {
				request.accepted = true
				request.dialogReject()
			}
            onPermissionRequested: function(permission) { permission.deny() }
            onCertificateError: function(error) { error.rejectCertificate() }
            onContextMenuRequested: function(request) { request.accepted = true }
            onNavigationRequested: function(request) {
				if (!mediaWindow.navigationRequestAllowed(request.url, mediaWindow.rendererDocumentUrl)) {
                    request.action = WebEngineNavigationRequest.IgnoreRequest
					return
				}
				acceptedNavigationGeneration = loadGeneration > 0
					? loadGeneration : mediaWindow.mediaGeneration
				acceptedNavigationUrl = String(request.url || "")
            }
			Connections {
				target: player.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
            Timer {
                interval: 500
				running: mediaSession.active && mediaSession.playbackControllable
					&& mediaSession.state !== "error" && mediaWindow.documentReady
					&& mediaWindow.rendererHealthy
                repeat: true
				onTriggered: {
					const generation = mediaWindow.mediaGeneration
					if (!mediaWindow.claimStatePoll(generation))
						return
					const pollToken = mediaWindow.statePollToken
					try {
						player.runJavaScript(mediaWindow.playerScript("state", 0), function(value) {
							mediaWindow.completeStatePoll(generation, value, pollToken)
						})
					} catch (error) {
						mediaWindow.cancelStatePoll(generation, pollToken)
						if (mediaWindow.failMediaDocument(generation))
							mediaSession.reportError(qsTr("The media player stopped responding."))
					}
				}
            }
			Component.onDestruction: mediaWindow.invalidateMediaDocument(loadGeneration)
		}
	}

	Timer {
		interval: 50
		running: mediaSession.active && !mediaWindow.nativeDirectMedia
			&& mediaWindow.rendererHealthy
			&& !mediaWindow.documentReady && mediaSession.loadProgress >= 100
		repeat: true
		onTriggered: {
			if (mediaWindow._documentReadyProbeGeneration === mediaWindow.mediaGeneration
					&& Date.now() - mediaWindow._documentReadyProbeStartedAt >= 1000) {
				mediaWindow._documentReadyProbeGeneration = -1
				mediaWindow._documentReadyProbeStartedAt = 0
				mediaWindow.documentReadyProbeState = "callback-timeout"
			}
			mediaWindow.probeMediaDocumentReady(mediaWindow.mediaGeneration)
		}
	}

	Rectangle {
		id: guestPlaybackGuard
		objectName: "mediaSessionGuestPlaybackGuard"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: mediaWindow.sharedGuestPlaybackLocked
			&& mediaWindow.rendererState === "active"
		color: "transparent"
		z: 3
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("Playback controlled by the host")
		Accessible.description: qsTr("Provider playback controls are locked. Local mute and volume remain available.")

		MouseArea {
			anchors.fill: parent
			acceptedButtons: Qt.AllButtons
			hoverEnabled: true
			preventStealing: true
			propagateComposedEvents: false
			onWheel: function(wheel) { wheel.accepted = true }
		}

		Rectangle {
			anchors.horizontalCenter: parent.horizontalCenter
			anchors.bottom: parent.bottom
			anchors.bottomMargin: Theme.space3
			width: Math.min(parent.width - Theme.space4 * 2, guestPlaybackLabel.implicitWidth + Theme.space4)
			height: guestPlaybackLabel.implicitHeight + Theme.space2
			radius: height / 2
			color: Theme.withAlpha(Theme.embedOverlayBase, 0.88)
			border.color: mediaWindow.providerAccentBorder
			Label {
				id: guestPlaybackLabel
				anchors.centerIn: parent
				width: Math.min(implicitWidth, parent.width - Theme.space3)
				text: qsTr("Host controls playback · local audio controls remain available")
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				elide: Text.ElideRight
			}
		}
	}

	Timer {
		id: statePollWatchdog
		property int pollGeneration: -1
		property int pollToken: -1
		interval: Math.max(1, mediaWindow.statePollTimeoutMs)
		repeat: false
		onTriggered: mediaWindow.expireStatePoll(pollGeneration, pollToken)
	}

	Timer {
		id: audioStatePollWatchdog
		property int pollGeneration: -1
		property int pollToken: -1
		interval: Math.max(1, mediaWindow.statePollTimeoutMs)
		repeat: false
		onTriggered: mediaWindow.expireAudioStatePoll(pollGeneration, pollToken)
	}

	Loader {
        id: audioPlayerLoader
		objectName: "mediaSessionSecondaryAudioSurface"
		active: mediaWindow.normalizedVisualFixtureMode.length === 0
			&& !mediaWindow.nativeDirectMedia
			&& mediaWindow.mediaRuntimeReady && mediaSession.active
			&& mediaWindow.documentReady && mediaSession.error.length === 0
			&& mediaSession.playbackControllable && mediaSession.audioUrl.toString().length > 0
			&& !mediaWindow.secondaryAudioDegraded
        width: 1
        height: 1
        opacity: 0.01
		sourceComponent: WebEngineView {
			id: audioPlayer
			property int loadGeneration: -1
			property int acceptedNavigationGeneration: -1
			property string acceptedNavigationUrl: ""
			property bool documentReady: false
			// The secondary track is transport-only. Keep its one-pixel surface out
			// of pointer, keyboard and assistive-technology traversal so it cannot
			// become an invisible focus trap.
			enabled: false
			focus: false
			activeFocusOnTab: false
			Accessible.ignored: true
			profile: mediaWindow.mediaProfileFactory
				? mediaWindow.mediaProfileFactory.audioProfile : null
            url: mediaSession.audioUrl
            settings.playbackRequiresUserGesture: false
            onLoadingChanged: function(request) {
				const callbackGeneration = loadGeneration > 0 ? loadGeneration : mediaWindow.mediaGeneration
				if (!mediaWindow.requestUrlMatches(request.url, mediaSession.audioUrl,
						acceptedNavigationUrl, acceptedNavigationGeneration, callbackGeneration))
					return
				if (request.status === WebEngineView.LoadStartedStatus) {
					loadGeneration = callbackGeneration
					acceptedNavigationGeneration = callbackGeneration
					acceptedNavigationUrl = String(request.url || "")
					documentReady = false
					mediaWindow.resetAudioStatePoll()
					mediaWindow._audioMissingStatePolls = 0
					return
				}
				const generation = loadGeneration
				if (generation !== mediaWindow.mediaGeneration || !mediaWindow.documentReady)
					return
				if (request.status === WebEngineView.LoadFailedStatus) {
					documentReady = false
					mediaWindow.reportSecondaryAudioError(generation,
						request.errorString || qsTr("The separate audio track could not be loaded."))
				} else if (request.status === WebEngineView.LoadSucceededStatus) {
					documentReady = true
					Qt.callLater(function() { mediaWindow.applyDesiredPlaybackState(generation) })
				}
            }
            onRenderProcessTerminated: function(status, exitCode) {
				const generation = loadGeneration
				documentReady = false
				if (generation === mediaWindow.mediaGeneration)
					audioPlayer.stop()
				mediaWindow.reportSecondaryAudioError(generation,
					qsTr("The separate audio track stopped unexpectedly."))
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
			onJavaScriptDialogRequested: function(request) {
				request.accepted = true
				request.dialogReject()
			}
            onPermissionRequested: function(permission) { permission.deny() }
            onCertificateError: function(error) { error.rejectCertificate() }
            onContextMenuRequested: function(request) { request.accepted = true }
            onNavigationRequested: function(request) {
				if (!mediaWindow.navigationRequestAllowed(request.url, mediaSession.audioUrl)) {
                    request.action = WebEngineNavigationRequest.IgnoreRequest
					return
				}
				acceptedNavigationGeneration = loadGeneration > 0
					? loadGeneration : mediaWindow.mediaGeneration
				acceptedNavigationUrl = String(request.url || "")
            }
			Connections {
				target: audioPlayer.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
        }
    }

	Rectangle {
		id: mediaProviderBadge
		objectName: "mediaSessionProviderBadge"
		parent: playerCanvas
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.margins: Theme.space3
		width: providerBadgeRow.implicitWidth + Theme.space3
		height: 28
		radius: height / 2
		visible: mediaWindow.rendererState === "active"
		color: mediaWindow.providerAccent
		border.color: Theme.withAlpha(mediaWindow.providerOnAccent, 0.24)
		z: 3
		Accessible.role: Accessible.StaticText
		Accessible.name: mediaWindow.detachedProviderAccessibleName()
		Row {
			id: providerBadgeRow
			anchors.centerIn: parent
			spacing: Theme.space1
			Label {
				objectName: "mediaSessionProviderMark"
				anchors.verticalCenter: parent.verticalCenter
				text: mediaWindow.providerMark
				textFormat: Text.PlainText
				color: mediaWindow.providerOnAccent
				font.pixelSize: Theme.fontCaption
				font.weight: Font.Bold
				Accessible.ignored: true
			}
			Rectangle {
				anchors.verticalCenter: parent.verticalCenter
				width: 1
				height: 14
				visible: mediaProviderLabel.visible
				color: Theme.withAlpha(mediaWindow.providerOnAccent, 0.34)
			}
			Label {
				id: mediaProviderLabel
				objectName: "mediaSessionProviderLabel"
				visible: !mediaWindow.compactProviderChrome
				textFormat: Text.PlainText
				text: mediaWindow.providerLabel
				color: mediaWindow.providerOnAccent
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				Accessible.ignored: true
			}
			Label {
				objectName: "mediaSessionSurfaceStateLabel"
				textFormat: Text.PlainText
				text: mediaSession.sharedAvailable && mediaSession.sharedJoined
					? (mediaSession.sharedHost ? qsTr("· HOSTING") : qsTr("· SYNCED"))
					: qsTr("· DETACHED")
				color: Theme.withAlpha(mediaWindow.providerOnAccent, 0.82)
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				Accessible.ignored: true
			}
		}
	}

	Rectangle {
		objectName: "mediaSessionSecondaryAudioWarning"
		parent: playerCanvas
		anchors.left: playerLoader.left
		anchors.right: playerLoader.right
		anchors.bottom: playerLoader.bottom
		anchors.margins: Theme.space3
		height: secondaryAudioWarningRow.implicitHeight + Theme.space2
		visible: mediaWindow.secondaryAudioDegraded && mediaSession.active
		radius: Theme.innerRadius
		color: Theme.withAlpha(Theme.embedOverlayBase, 0.94)
		border.color: Theme.withAlpha(Theme.warning, 0.62)
		z: 7
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Video continues without the separate audio track")
		Accessible.description: mediaWindow.secondaryAudioWarning

		RowLayout {
			id: secondaryAudioWarningRow
			anchors.fill: parent
			anchors.margins: Theme.space1
			spacing: Theme.space2
			ModernIcon {
				name: "warning"
				size: 16
				color: Theme.warning
			}
			Label {
				objectName: "mediaSessionSecondaryAudioWarningText"
				Layout.fillWidth: true
				text: qsTr("Video remains available. %1").arg(mediaWindow.secondaryAudioWarning)
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontCaption
				wrapMode: Text.Wrap
			}
		}
	}

	Rectangle {
		id: loadingSurface
		objectName: "mediaSessionLoadingSurface"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: mediaWindow.rendererState === "loading"
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.96)
		z: 4
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Loading %1 media").arg(mediaWindow.providerLabel)
		Accessible.description: mediaSession.loadProgress > 0
			? qsTr("%1 percent loaded").arg(mediaSession.loadProgress) : qsTr("Contacting provider")

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 - Theme.space4, 420)
			spacing: mediaWindow.compactProviderChrome ? Theme.space2 : Theme.space3

			ModernBusyIndicator {
				objectName: "mediaSessionBusyIndicator"
				Layout.alignment: Qt.AlignHCenter
				running: parent.parent.visible
				animated: mediaWindow.normalizedVisualFixtureMode.length === 0
				Accessible.name: qsTr("Loading media player")
			}
			Rectangle {
				objectName: "mediaSessionLoadingProviderBadge"
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: Math.max(32, mediaLoadingProviderMark.implicitWidth + Theme.space3)
				Layout.preferredHeight: 24
				radius: height / 2
				color: mediaWindow.providerAccent
				border.color: Theme.withAlpha(mediaWindow.providerOnAccent, 0.24)
				Accessible.role: Accessible.StaticText
				Accessible.name: mediaWindow.providerLabel
				Label {
					id: mediaLoadingProviderMark
					objectName: "mediaSessionLoadingProviderMark"
					anchors.centerIn: parent
					textFormat: Text.PlainText
					text: mediaWindow.providerMark
					color: mediaWindow.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					Accessible.ignored: true
				}
			}
			Label {
				objectName: "mediaSessionLoadingHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Loading %1").arg(mediaWindow.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "mediaSessionLoadingDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: mediaSession.loadProgress > 0
					? qsTr("%1% loaded").arg(mediaSession.loadProgress)
					: qsTr("Starting provider playback…")
				color: Theme.mediaOverlayTextMuted
				horizontalAlignment: Text.AlignHCenter
			}
			Rectangle {
				objectName: "mediaSessionLoadingProgressTrack"
				Layout.fillWidth: true
				Layout.preferredHeight: 3
				visible: mediaSession.loadProgress > 0
				radius: height / 2
				color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.20)
				Rectangle {
					width: parent.width * Math.max(0, Math.min(100, mediaSession.loadProgress)) / 100
					height: parent.height
					radius: height / 2
					color: mediaWindow.providerAccent
					Behavior on width {
						enabled: mediaWindow.normalizedVisualFixtureMode.length === 0
						NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
					}
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
				objectName: "mediaSessionEmptyHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Choose something to play")
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontHeading
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "mediaSessionEmptyDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Open a supported rich preview or rejoin the room session.")
				color: Theme.mediaOverlayTextMuted
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
		visible: mediaWindow.rendererState === "error"
		color: mediaWindow.withAlpha(Theme.mediaCanvas, 0.96)
        z: 5
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("%1 playback failed").arg(mediaWindow.providerLabel)
		Accessible.description: mediaWindow.effectiveMediaError

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 - Theme.space4, 520)
			spacing: mediaWindow.compactProviderChrome ? Theme.space2 : Theme.space3

			Rectangle {
				objectName: "mediaSessionFailureProviderBadge"
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: Math.max(32, mediaFailureProviderMark.implicitWidth + Theme.space3)
				Layout.preferredHeight: 24
				radius: height / 2
				color: mediaWindow.providerAccent
				border.color: Theme.withAlpha(mediaWindow.providerOnAccent, 0.24)
				Accessible.role: Accessible.StaticText
				Accessible.name: mediaWindow.providerLabel
				Label {
					id: mediaFailureProviderMark
					objectName: "mediaSessionFailureProviderMark"
					anchors.centerIn: parent
					text: mediaWindow.providerMark
					textFormat: Text.PlainText
					color: mediaWindow.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					Accessible.ignored: true
				}
			}

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
				objectName: "mediaSessionFailureHeading"
				textFormat: Text.PlainText
                Layout.fillWidth: true
				text: qsTr("%1 playback failed").arg(mediaWindow.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.weight: Font.DemiBold
				font.pixelSize: Theme.fontHeading
                horizontalAlignment: Text.AlignHCenter
            }
			Label {
				objectName: "mediaSessionFailureDetail"
				textFormat: Text.PlainText
                Layout.fillWidth: true
				text: mediaWindow.effectiveMediaError
				color: Theme.mediaOverlayTextMuted
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
					hoverEnabled: mediaWindow.normalizedVisualFixtureMode.length === 0
					enabled: !mediaProfileFactory || !mediaProfileFactory.runtimePreparing
					Accessible.description: qsTr("Restart provider playback")
					onClicked: mediaWindow.retryMediaRenderer()
				}
				ModernButton {
					objectName: "mediaSessionFailureExternalButton"
					visible: mediaWindow.externalMediaUrl().length > 0
					text: qsTr("Open externally")
					hoverEnabled: mediaWindow.normalizedVisualFixtureMode.length === 0
					onClicked: Qt.openUrlExternally(mediaWindow.externalMediaUrl())
				}
				ModernButton {
					objectName: "mediaSessionFailureCloseButton"
					text: qsTr("Close")
					hoverEnabled: mediaWindow.normalizedVisualFixtureMode.length === 0
					onClicked: controls.requestClose()
				}
            }
        }
    }

    MediaSessionControls {
        id: controls
		objectName: "mediaSessionWindowControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
		visible: mediaWindow.rendererState !== "error"
		height: visible ? implicitHeight : 0
		session: mediaSession
		fullscreen: mediaWindow.visibility === Window.FullScreen
		externalAvailable: mediaWindow.externalMediaUrl().length > 0
		onFullscreenRequested: enabled => mediaWindow.setFullscreen(enabled)
		onExternalRequested: Qt.openUrlExternally(mediaWindow.externalMediaUrl())
		onExitConfirmed: disposition => mediaWindow.applyExitDisposition(disposition)
    }
}
