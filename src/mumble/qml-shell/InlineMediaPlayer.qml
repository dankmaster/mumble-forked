pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0
import "MediaPlaybackProbe.js" as MediaPlaybackProbe

Rectangle {
	id: inlinePlayer

	required property var session
	// Keep isolated WebEngine profiles explicit. If a host forgets to provide
	// them, no provider surface is created with WebEngine's shared default.
	property var mediaProfileFactory: null
	// The playback transport can be "direct" while the card still represents a
	// provider such as Reddit. Keep presentation identity separate from transport.
	property string presentationProvider: ""
	property string aspect: "wide"
	property string visualFixtureMode: ""
	readonly property var providerPresentation: ProviderPresentation.resolve(
		String(presentationProvider || "").trim()
			|| (session ? session.provider : ""))
	readonly property string providerLabel: providerPresentation.label
		|| String(session ? session.provider || "" : "").trim() || qsTr("Media")
	readonly property string providerMark: providerPresentation.mark
		|| fallbackProviderMark(providerLabel)
	readonly property color providerAccent: providerPresentation.accent || Theme.accent
	readonly property color providerAccentSubtle: Theme.withAlpha(providerAccent, 0.16)
	readonly property color providerAccentBorder: Theme.withAlpha(providerAccent, 0.56)
	readonly property color providerOnAccent: Theme.contrastText(providerAccent)
	readonly property bool compactProviderChrome: width < 440
		|| normalizedAspect === "compact-audio"
	readonly property string normalizedVisualFixtureMode: String(visualFixtureMode || "").toLowerCase()
	readonly property bool visualFixtureRendererReady: normalizedVisualFixtureMode === "active"
		|| normalizedVisualFixtureMode === "controls"
	readonly property bool ready: !!session && session.active && !session.detached
	readonly property bool adaptiveManifest: {
		const mime = String(session ? session.mediaMime || "" : "").split(";", 1)[0].trim().toLowerCase()
		return String(session ? session.provider || "" : "").toLowerCase() === "direct"
			&& (mime === "application/vnd.apple.mpegurl" || mime === "application/dash+xml")
	}
	readonly property bool nativeDirectMedia: normalizedVisualFixtureMode.length === 0
		&& ready && String(session ? session.provider || "" : "").toLowerCase() === "direct"
		&& !adaptiveManifest && String(session ? session.url || "" : "").trim().length > 0
		&& String(session ? session.error || "" : "").length === 0
	readonly property string rendererDocumentUrl: playbackDocumentUrl()
	readonly property string normalizedAspect: normalizeAspect(aspect)
	readonly property int mediaGeneration: _mediaGeneration
	readonly property bool documentReady: visualFixtureRendererReady
		|| (nativeDirectMedia && nativePlayerLoader.item
			&& nativePlayerLoader.item.documentReady)
		|| (!nativeDirectMedia && _documentReadyGeneration === _mediaGeneration
			&& _mediaGeneration > 0 && _rendererHealthy)
	readonly property bool surfaceVerified: documentReady
	readonly property bool transportVerified: visualFixtureRendererReady
		|| (nativeDirectMedia && documentReady)
		|| (_transportVerifiedGeneration === _mediaGeneration && _mediaGeneration > 0)
	readonly property bool playbackVerified: documentReady
		&& ((session && String(session.state || "") === "playing")
			|| (_playbackVerifiedGeneration === _mediaGeneration && _mediaGeneration > 0))
	readonly property string surfaceVerificationState: visualFixtureRendererReady ? "verified"
		: nativeDirectMedia ? (documentReady ? "verified" : rendererHealthy ? "pending" : "idle")
		: String(session ? session.error || "" : "").length > 0
			&& _surfaceVerificationState === "idle" ? "failed" : _surfaceVerificationState
	readonly property string surfaceVerificationEvidence: visualFixtureRendererReady ? "fixture"
		: nativeDirectMedia && documentReady ? "native-media" : _surfaceVerificationEvidence
	readonly property string surfaceVerificationDetail: _surfaceVerificationDetail
	readonly property bool statePollInFlight: _statePollGeneration === _mediaGeneration
		&& _mediaGeneration > 0 && _statePollToken >= 0
	readonly property int statePollToken: _statePollToken
	readonly property bool secondaryAudioStatePollInFlight:
		_audioStatePollGeneration === _audioGeneration
		&& _audioGeneration > 0 && _audioStatePollToken >= 0
	readonly property int secondaryAudioStatePollToken: _audioStatePollToken
	readonly property bool rendererHealthy: visualFixtureRendererReady
		|| (nativeDirectMedia && nativePlayerLoader.item
			&& nativePlayerLoader.item.rendererHealthy)
		|| (!nativeDirectMedia && _rendererHealthy)
	readonly property string surfaceId: "mediaSession.inline"
	readonly property var captureRect: ({ "x": 0, "y": 0, "width": width, "height": height })
	readonly property bool webSurfaceActive: playerLoader.active
	readonly property bool nativeSurfaceActive: nativePlayerLoader.active
	readonly property string rendererBackend: visualFixtureRendererReady ? "fixture"
		: nativeSurfaceActive ? "native" : webSurfaceActive ? "webengine" : "none"
	readonly property bool secondaryAudioActive: nativeSurfaceActive && nativePlayerLoader.item
		? nativePlayerLoader.item.secondaryAudioActive : audioPlayerLoader.active
	readonly property bool sharedGuestPlaybackLocked: Boolean(session)
		&& Boolean(session.sharedAvailable) && Boolean(session.sharedJoined)
		&& !Boolean(session.sharedHost)
	readonly property bool providerInputEnabled: !sharedGuestPlaybackLocked
	property string _webSecondaryAudioWarning: ""
	readonly property string secondaryAudioWarning: nativeSurfaceActive && nativePlayerLoader.item
		? String(nativePlayerLoader.item.secondaryAudioWarning || "")
		: _webSecondaryAudioWarning
	readonly property bool secondaryAudioDegraded: secondaryAudioWarning.length > 0
	readonly property string secondaryAudioState: nativeSurfaceActive && nativePlayerLoader.item
		? String(nativePlayerLoader.item.secondaryAudioState || "idle")
		: secondaryAudioDegraded ? "degraded"
		: !session || String(session.audioUrl || "").length === 0 ? "idle"
		: secondaryAudioActive && audioPlayerLoader.documentReady ? "active"
		: secondaryAudioActive ? "loading" : "idle"
	readonly property string rendererState: normalizedVisualFixtureMode.length > 0
		? (visualFixtureRendererReady ? "active"
			: normalizedVisualFixtureMode === "loading" ? "loading" : "error")
		: !ready ? "empty"
		: String(session ? session.error || "" : "").length > 0 ? "error"
		: !documentReady ? "loading" : "active"
	readonly property real mediaViewportHeight: viewportHeightForWidth(width)
	property int _mediaGeneration: 0
	property int _documentReadyGeneration: -1
	property int _documentReadyProbeGeneration: -1
	property double _documentReadyProbeStartedAt: 0
	property int documentReadyProbeAttempts: 0
	property string documentReadyProbeState: "idle"
	property int documentReadyProbeMaxAttempts: adaptiveManifest ? 400 : 160
	property int _transportVerifiedGeneration: -1
	property int _playbackVerifiedGeneration: -1
	property string _surfaceVerificationState: "idle"
	property string _surfaceVerificationEvidence: ""
	property string _surfaceVerificationDetail: ""
	property int statePollTimeoutMs: 3000
	property int _statePollGeneration: -1
	property int _statePollToken: -1
	property int _nextStatePollToken: 0
	property int _missingStatePolls: 0
	property bool _rendererHealthy: false
	property string _documentUrl: ""
	property string _desiredPlaybackState: ""
	property int _transportRetryCount: 0
	property int _audioGeneration: 0
	property int _audioDocumentReadyGeneration: -1
	property int _audioStatePollGeneration: -1
	property int _audioStatePollToken: -1
	property int _nextAudioStatePollToken: 0
	property int _audioMissingStatePolls: 0
	// Controls only become meaningful once the provider transport is proven.
	// Keeping them hidden while a challenge/error document loads avoids the
	// misleading 0:00 / 0:00 bar that previously appeared under broken embeds.
	readonly property bool nativeControlsVisible: !!session
		&& Boolean(session.playbackControllable) && transportVerified
	implicitHeight: mediaViewportHeight
		+ (nativeControlsVisible ? inlineControls.implicitHeight : 0)
	color: Theme.mediaCanvas
	border.color: Theme.surfaceBorder
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("%1 inline media player").arg(providerLabel)

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

	function playerScript(command, value) {
		const provider = JSON.stringify(String(session ? session.provider : ""))
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

	function playbackDocumentUrl() {
		if (mediaProfileFactory && typeof mediaProfileFactory.videoDocumentUrl !== "undefined") {
			const documentUrl = String(mediaProfileFactory.videoDocumentUrl || "")
			if (documentUrl.length > 0)
				return documentUrl
		}
		return String(session ? session.url || "" : "")
	}

	function navigationRequestAllowed(requestUrl, firstPartyUrl) {
		if (mediaProfileFactory
				&& typeof mediaProfileFactory.isNavigationRequestAllowed === "function")
			return mediaProfileFactory.isNavigationRequestAllowed(requestUrl, firstPartyUrl)
		return session && typeof session.isNavigationAllowed === "function"
			? session.isNavigationAllowed(requestUrl) : false
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
		_transportVerifiedGeneration = -1
		_playbackVerifiedGeneration = -1
		_surfaceVerificationState = rendererIsHealthy ? "pending" : "idle"
		_surfaceVerificationEvidence = ""
		_surfaceVerificationDetail = ""
		resetStatePoll()
		_missingStatePolls = 0
		_rendererHealthy = !!rendererIsHealthy
		_transportRetryCount = 0
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
		_transportVerifiedGeneration = generation
		_surfaceVerificationState = "verified"
		_missingStatePolls = 0
		return true
	}

	function completeMediaDocumentLoad(generation, evaluation) {
		if (!markMediaDocumentReady(generation))
			return false
		_documentReadyProbeGeneration = -1
		const result = evaluation && typeof evaluation === "object" ? evaluation : ({})
		_surfaceVerificationEvidence = String(result.evidence || "manual")
		_surfaceVerificationDetail = ""
		if (result.playbackVerified === true)
			_playbackVerifiedGeneration = generation
		documentReadyProbeState = "verified:" + _surfaceVerificationEvidence
		if (session)
			session.reportLoadProgress(100)
		Qt.callLater(function() { inlinePlayer.applyDesiredPlaybackState(generation) })
		return true
	}

	function verificationFailureMessage(evaluation) {
		const kind = String(evaluation ? evaluation.kind || "" : "")
		if (kind === "verification" || kind === "sign-in")
			return qsTr("This provider requires verification or sign-in. Open it externally to continue.")
		if (kind === "unavailable")
			return qsTr("This provider says the media is unavailable here. Open the original page instead.")
		if (kind === "adaptive-renderer-failed")
			return String(evaluation.detail || "")
				|| qsTr("The stream could not be prepared for playback.")
		if (kind === "adaptive-renderer-timeout")
			return qsTr("The stream could not be prepared for playback.")
		return qsTr("The provider player did not expose a usable media surface. Open it externally instead.")
	}

	function applyMediaSurfaceProbeResult(generation, value, background) {
		if (generation !== _mediaGeneration)
			return false
		const evaluation = MediaPlaybackProbe.classify(value,
			session ? session.provider : "", adaptiveManifest,
			background ? 0 : documentReadyProbeAttempts,
			background ? 2147483647 : documentReadyProbeMaxAttempts)
		_surfaceVerificationEvidence = String(evaluation.evidence || "")
		documentReadyProbeState = String(evaluation.state || "pending")
			+ (_surfaceVerificationEvidence.length > 0
				? ":" + _surfaceVerificationEvidence : "")
		if (evaluation.state === "verified") {
			_transportVerifiedGeneration = generation
			if (evaluation.playbackVerified === true)
				_playbackVerifiedGeneration = generation
			if (background) {
				_surfaceVerificationState = "verified"
				return true
			}
			return completeMediaDocumentLoad(generation, evaluation)
		}
		if (evaluation.state === "pending")
			return false
		const failureState = evaluation.state === "blocked" ? "blocked" : "failed"
		const detail = verificationFailureMessage(evaluation)
		if (!failMediaDocument(generation, failureState, detail,
				String(evaluation.evidence || "")))
			return false
		if (session) {
			if (typeof session.reportTypedError === "function")
				session.reportTypedError(String(evaluation.kind || "provider-surface-failed"), detail)
			else
				session.reportError(detail)
		}
		return true
	}

	function probeMediaDocumentReady(generation, background) {
		if (generation !== _mediaGeneration || (!background && documentReady)
				|| _documentReadyProbeGeneration === generation || !playerLoader.item)
			return false
		const webPlayer = playerLoader.item
		_documentReadyProbeGeneration = generation
		_documentReadyProbeStartedAt = Date.now()
		if (!background)
			documentReadyProbeAttempts += 1
		documentReadyProbeState = "submitted"
		try {
			webPlayer.runJavaScript(
				MediaPlaybackProbe.probeScript(session ? session.provider : "",
					adaptiveManifest),
				function(value) {
					if (inlinePlayer._documentReadyProbeGeneration !== generation)
						return
					inlinePlayer._documentReadyProbeGeneration = -1
					inlinePlayer._documentReadyProbeStartedAt = 0
					inlinePlayer.applyMediaSurfaceProbeResult(generation, value, !!background)
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

	function failMediaDocument(generation, verificationState, detail, evidence) {
		if (!invalidateMediaDocument(generation))
			return false
		_surfaceVerificationState = String(verificationState || "failed")
		_surfaceVerificationDetail = String(detail || "")
		_surfaceVerificationEvidence = String(evidence || "")
		documentReadyProbeState = _surfaceVerificationState
		return true
	}

	function beginAudioDocumentLoad() {
		resetAudioStatePoll()
		_audioGeneration += 1
		_audioDocumentReadyGeneration = -1
		_audioMissingStatePolls = 0
		return _audioGeneration
	}

	function invalidateAudioDocument(expectedGeneration) {
		if (expectedGeneration !== undefined && expectedGeneration >= 0
				&& expectedGeneration !== _audioGeneration)
			return false
		resetAudioStatePoll()
		_audioGeneration += 1
		_audioDocumentReadyGeneration = -1
		_audioMissingStatePolls = 0
		return true
	}

	function markAudioDocumentReady(generation) {
		if (generation !== _audioGeneration || !ready || !session
				|| String(session.error || "").length > 0 || !documentReady)
			return false
		_audioDocumentReadyGeneration = generation
		return true
	}

	function reportSecondaryAudioError(generation, message) {
		if (generation !== _audioGeneration || !ready || !session
				|| String(session.error || "").length > 0 || !documentReady)
			return false
		invalidateAudioDocument(generation)
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

	function completeStatePoll(generation, value, pollToken) {
		const token = pollToken === undefined ? _statePollToken : pollToken
		if (generation !== _mediaGeneration || _statePollGeneration !== generation
				|| token !== _statePollToken)
			return false
		resetStatePoll()
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
		const playbackError = String(value.error || "").trim()
		if (playbackError.length > 0) {
			_desiredPlaybackState = ""
			failMediaDocument(generation)
			session.reportError(qsTr("Playback could not start: %1").arg(playbackError))
			return true
		}
		const position = Number(value.position)
		const duration = Number(value.duration)
		const paused = !!value.paused
		const desiredPaused = _desiredPlaybackState === "paused"
		const desiredPlaying = _desiredPlaybackState === "playing"
		if ((desiredPlaying && paused) || (desiredPaused && !paused)) {
			if (_transportRetryCount < 12) {
				_transportRetryCount += 1
				runPlayerScript(playerScript(desiredPlaying ? "play" : "pause", 0), generation)
				return true
			}
			_desiredPlaybackState = ""
			failMediaDocument(generation)
			session.reportError(desiredPlaying
				? qsTr("Playback was blocked by the media provider. Open it in your browser instead.")
				: qsTr("The media provider did not accept the pause command."))
			return true
		}
		_desiredPlaybackState = ""
		_transportRetryCount = 0
		session.reportPlaybackState(isFinite(position) ? position : 0,
			isFinite(duration) ? duration : 0, paused)
		syncSecondaryAudio(isFinite(position) ? position : 0, paused)
		return true
	}

	function claimAudioStatePoll(generation) {
		if (generation !== _audioGeneration || _audioStatePollGeneration >= 0)
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
		if (generation !== _audioGeneration || _audioStatePollGeneration !== generation
				|| token !== _audioStatePollToken)
			return false
		resetAudioStatePoll()
		return true
	}

	function recordMissingAudioStatePoll(generation) {
		if (generation !== _audioGeneration || !ready || !session)
			return false
		_audioMissingStatePolls += 1
		if (_audioMissingStatePolls === 20)
			reportSecondaryAudioError(generation,
				qsTr("The audio track did not provide playback controls."))
		return true
	}

	function expireAudioStatePoll(generation, pollToken) {
		if (!cancelAudioStatePoll(generation, pollToken))
			return false
		return recordMissingAudioStatePoll(generation)
	}

	function runPlayerScript(script, expectedGeneration) {
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !ready || !documentReady
				|| !_rendererHealthy || !playerLoader.item)
			return false
		try {
			playerLoader.item.runJavaScript(script)
		} catch (error) {
			if (failMediaDocument(generation) && session)
				session.reportError(qsTr("The inline player stopped responding."))
			return false
		}
		if (audioPlayerLoader.item && !secondaryAudioDegraded) {
			try {
				audioPlayerLoader.item.runJavaScript(script)
			} catch (error) {
				reportSecondaryAudioError(audioPlayerLoader.loadGeneration,
					qsTr("The inline player lost contact with the separate audio track."))
			}
		}
		return true
	}

	function syncSecondaryAudio(position, paused) {
		const audioPlayer = audioPlayerLoader.item
		if (!audioPlayer || !audioPlayerLoader.documentReady
				|| audioPlayerLoader.loadGeneration !== _audioGeneration
				|| secondaryAudioStatePollInFlight)
			return
		const generation = _audioGeneration
		const target = Math.max(0, Number(position || 0))
		if (!claimAudioStatePoll(generation))
			return
		const pollToken = secondaryAudioStatePollToken
		try {
			audioPlayer.runJavaScript(playerScript("state", 0), function(value) {
				const currentAudioPlayer = audioPlayerLoader.item
				if (generation !== inlinePlayer._audioGeneration || !inlinePlayer.ready
						|| !currentAudioPlayer || currentAudioPlayer !== audioPlayer
						|| audioPlayerLoader.loadGeneration !== generation)
					return
				if (!inlinePlayer.cancelAudioStatePoll(generation, pollToken))
					return
				if (!value) {
					inlinePlayer.recordMissingAudioStatePoll(generation)
					return
				}
				inlinePlayer._audioMissingStatePolls = 0
				const playbackError = String(value.error || "").trim()
				if (playbackError.length > 0) {
					inlinePlayer.reportSecondaryAudioError(generation, playbackError)
					return
				}
				audioPlayer.runJavaScript(
					"(function(){const m=document.querySelector('audio,video');if(!m)return;"
					+ "const target=" + target + ";"
					+ "if(Math.abs((m.currentTime||0)-target)>0.15)m.currentTime=target;"
					+ (paused ? "m.pause();"
						: "window.__mumbleMediaPlayError='';const p=m.play();if(p&&p.catch)p.catch(function(error){window.__mumbleMediaPlayError=String(error&&error.message||error||'Playback was blocked');});")
					+ "})()")
			})
		} catch (error) {
			cancelAudioStatePoll(generation, pollToken)
			reportSecondaryAudioError(generation,
				qsTr("The inline player lost contact with the audio track."))
		}
	}

	function applyDesiredPlaybackState(expectedGeneration) {
		if (nativeDirectMedia && nativePlayerLoader.item) {
			nativePlayerLoader.item.applySessionState()
			if (Number(session.position || 0) > 0.05)
				nativePlayerLoader.item.seek(session.position)
			return
		}
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		if (generation !== _mediaGeneration || !ready || !documentReady
				|| !session.playbackControllable)
			return
		runPlayerScript(playerScript("volume", session.volume), generation)
		runPlayerScript(playerScript("mute", session.muted ? 1 : 0), generation)
		if (Number(session.position || 0) > 0.05)
			runPlayerScript(playerScript("seek", session.position), generation)
		requestPlaybackState(session.state === "playing" ? "playing" : "paused", generation)
	}

	function requestPlaybackState(state, expectedGeneration) {
		if (nativeDirectMedia && nativePlayerLoader.item)
			return state === "playing" ? nativePlayerLoader.item.play()
				: nativePlayerLoader.item.pause()
		const generation = expectedGeneration === undefined ? _mediaGeneration : expectedGeneration
		_desiredPlaybackState = state === "playing" ? "playing" : "paused"
		_transportRetryCount = 0
		return runPlayerScript(playerScript(_desiredPlaybackState === "playing" ? "play" : "pause", 0), generation)
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
		if (providerCloseButton.visible && providerCloseButton.enabled) {
			providerCloseButton.forceActiveFocus()
			return providerCloseButton.activeFocus
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
			: normalizedAspect === "square" ? 1
			: normalizedAspect === "twitch" ? 4 / 3 : 16 / 9
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
			: normalizedAspect === "square" ? 1
			: normalizedAspect === "twitch" ? 4 / 3 : 16 / 9
		return fittedMediaWidth(availableWidth, availableHeight) / ratio
	}

	function viewportHeightForWidth(availableWidth) {
		if (availableWidth <= 0)
			return 0
		if (normalizedAspect === "audio")
			return Math.min(352, Math.max(220, availableWidth * 0.61))
		if (normalizedAspect === "compact-audio")
			return Math.min(166, Math.max(128, availableWidth * 0.29))
		if (normalizedAspect === "short")
			return availableWidth * 16 / 9
		if (normalizedAspect === "square")
			return availableWidth
		if (normalizedAspect === "twitch")
			return availableWidth * 3 / 4
		return availableWidth * 9 / 16
	}

	onSessionChanged: {
		invalidateMediaDocument()
		invalidateAudioDocument()
	}
	onReadyChanged: if (!ready) {
		invalidateMediaDocument()
		invalidateAudioDocument()
	}
	onSharedGuestPlaybackLockedChanged: if (sharedGuestPlaybackLocked)
		Qt.callLater(focusInitialControl)
	Component.onDestruction: {
		if (nativePlayerLoader.item)
			nativePlayerLoader.item.shutdown()
		invalidateMediaDocument()
		invalidateAudioDocument()
	}
	Connections {
		target: inlinePlayer.session
		ignoreUnknownSignals: true
		function onStateChanged() {
			if (inlinePlayer.session && (inlinePlayer.session.state === "error"
					|| String(inlinePlayer.session.error || "").length > 0)) {
				if (inlinePlayer.rendererHealthy || inlinePlayer.documentReady)
					inlinePlayer.invalidateMediaDocument()
				inlinePlayer.invalidateAudioDocument()
				Qt.callLater(inlinePlayer.focusFailureControl)
			}
		}
		function onSourceChanged() {
			inlinePlayer.invalidateMediaDocument()
			inlinePlayer.invalidateAudioDocument()
		}
		function onPlayRequested() { inlinePlayer.requestPlaybackState("playing") }
		function onPauseRequested() { inlinePlayer.requestPlaybackState("paused") }
		function onSeekRequested(seconds) {
			if (inlinePlayer.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.seek(seconds)
			else inlinePlayer.runPlayerScript(inlinePlayer.playerScript("seek", seconds))
		}
		function onVolumeRequested(volume) {
			if (inlinePlayer.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.setVolume(volume)
			else inlinePlayer.runPlayerScript(inlinePlayer.playerScript("volume", volume))
		}
		function onMutedRequested(muted) {
			if (inlinePlayer.nativeDirectMedia && nativePlayerLoader.item)
				nativePlayerLoader.item.setMuted(muted)
			else inlinePlayer.runPlayerScript(inlinePlayer.playerScript("mute", muted ? 1 : 0))
		}
		function onRetryRequested() {
			if (inlinePlayer.nativeDirectMedia && nativePlayerLoader.item) {
				nativePlayerLoader.item.retry()
				return
			}
			inlinePlayer.invalidateMediaDocument()
			inlinePlayer.invalidateAudioDocument()
			Qt.callLater(function() {
				if (playerLoader.item)
					playerLoader.item.reload()
				if (audioPlayerLoader.item)
					audioPlayerLoader.item.reload()
			})
		}
	}

	Rectangle {
		id: inlineToolbar
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		height: Theme.controlHeight + Theme.space2
		z: 6
		color: "transparent"
		border.width: 0
		gradient: Gradient {
			GradientStop { position: 0.0; color: Theme.withAlpha(Theme.embedOverlayBase, 0.82) }
			GradientStop { position: 1.0; color: "transparent" }
		}

		Rectangle {
			id: inlineProviderBadge
			objectName: "inlineMediaProviderBadge"
			anchors.left: parent.left
			anchors.leftMargin: Theme.space3
			anchors.verticalCenter: parent.verticalCenter
			width: inlineProviderRow.implicitWidth + Theme.space2
			height: 24
			radius: height / 2
			color: inlinePlayer.providerAccent
			border.color: Theme.withAlpha(inlinePlayer.providerOnAccent, 0.24)
			border.width: 1
			Accessible.role: Accessible.StaticText
			Accessible.name: qsTr("%1 media player").arg(inlinePlayer.providerLabel)
			Row {
				id: inlineProviderRow
				anchors.centerIn: parent
				spacing: Theme.space1
				Label {
					objectName: "inlineMediaProviderMark"
					anchors.verticalCenter: parent.verticalCenter
					textFormat: Text.PlainText
					text: inlinePlayer.providerMark
					color: inlinePlayer.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					Accessible.ignored: true
				}
				Rectangle {
					anchors.verticalCenter: parent.verticalCenter
					width: 1
					height: 12
					visible: inlineProviderLabel.visible
					color: Theme.withAlpha(inlinePlayer.providerOnAccent, 0.34)
				}
				Label {
					id: inlineProviderLabel
					objectName: "inlineMediaProviderLabel"
					visible: !inlinePlayer.compactProviderChrome
					textFormat: Text.PlainText
					text: inlinePlayer.providerLabel
					color: inlinePlayer.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
					Accessible.ignored: true
				}
			}
		}

		Label {
			objectName: "inlineMediaStatusLabel"
			visible: false
			anchors.left: inlineProviderBadge.right
			anchors.right: toolbarActions.left
			anchors.leftMargin: Theme.space2
			anchors.rightMargin: Theme.space2
			anchors.verticalCenter: parent.verticalCenter
			textFormat: Text.PlainText
			text: session && session.sharedAvailable && session.sharedJoined
				? qsTr("Watching together in chat") : qsTr("Playing in chat")
			color: Theme.mediaOverlayTextStrong
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
			ModernIconButton {
				objectName: "inlineMediaPopoutButton"
				overlay: true
				dense: true
				text: qsTr("Pop out")
				iconName: "external"
				Accessible.description: qsTr("Move playback to a separate movable window")
				onClicked: if (inlinePlayer.session) inlinePlayer.session.detach()
			}
			ModernIconButton {
				objectName: "inlineMediaExternalButton"
				overlay: true
				dense: true
				visible: false
				text: qsTr("Browser")
				iconName: "external"
				onClicked: Qt.openUrlExternally(inlinePlayer.externalMediaUrl())
			}
			ModernButton {
				id: providerCloseButton
				objectName: "inlineMediaProviderCloseButton"
				visible: !inlinePlayer.nativeControlsVisible
				dense: true
				text: qsTr("Close")
				Accessible.description: qsTr("Close the provider player and return to the preview")
				onClicked: inlineControls.requestClose()
			}
		}
	}

	Rectangle {
		id: playerCanvas
		objectName: "inlineMediaCanvas"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.bottomMargin: inlinePlayer.nativeControlsVisible
			? inlineControls.implicitHeight : 0
		color: Theme.mediaCanvas
		border.color: inlinePlayer.rendererState === "error"
			? Theme.withAlpha(Theme.danger, 0.55) : Theme.surfaceBorder
		border.width: 1
		clip: true
	}

	Rectangle {
		objectName: "inlineMediaVisualFixtureSurface"
		parent: playerCanvas
		x: playerLoader.x
		y: playerLoader.y
		width: playerLoader.width
		height: playerLoader.height
		visible: inlinePlayer.visualFixtureRendererReady
		z: 2
		color: Theme.mediaCanvas
		gradient: Gradient {
			GradientStop { position: 0.0; color: Theme.withAlpha(inlinePlayer.providerAccent, 0.34) }
			GradientStop { position: 0.55; color: Theme.withAlpha(Theme.panel, 0.96) }
			GradientStop { position: 1.0; color: Theme.mediaCanvas }
		}
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("Deterministic media playback preview")

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 * 2, 460)
			spacing: inlinePlayer.compactProviderChrome ? Theme.space1 : Theme.space3
			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: inlinePlayer.compactProviderChrome ? 44 : 72
				Layout.preferredHeight: Layout.preferredWidth
				radius: width / 2
				color: inlinePlayer.providerAccentSubtle
				border.color: inlinePlayer.providerAccentBorder
				Label {
					anchors.centerIn: parent
					text: inlinePlayer.providerMark
					textFormat: Text.PlainText
					color: inlinePlayer.providerAccent
					font.pixelSize: Theme.fontTitle
					font.weight: Font.Bold
				}
			}
			Label {
				objectName: "inlineMediaFixtureHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("%1 media session").arg(inlinePlayer.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontHeading
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "inlineMediaFixtureDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("%1 provider playback · deterministic visual fixture")
					.arg(inlinePlayer.providerMark)
				color: Theme.mediaOverlayTextMuted
				horizontalAlignment: Text.AlignHCenter
				visible: !inlinePlayer.compactProviderChrome
			}
		}
	}

	Loader {
		id: nativePlayerLoader
		objectName: "inlineMediaNativeSurface"
		parent: playerCanvas
		width: inlinePlayer.fittedMediaWidth(playerCanvas.width, playerCanvas.height)
		height: inlinePlayer.fittedMediaHeight(playerCanvas.width, playerCanvas.height)
		x: Math.round((playerCanvas.width - width) / 2)
		y: Math.round((playerCanvas.height - height) / 2)
		active: inlinePlayer.nativeDirectMedia
		clip: true
		sourceComponent: NativeDirectMediaPlayer {
			session: inlinePlayer.session
			playbackInputEnabled: inlinePlayer.providerInputEnabled
		}
	}

	Loader {
		id: playerLoader
		objectName: "inlineMediaWebSurface"
		parent: playerCanvas
		width: inlinePlayer.fittedMediaWidth(playerCanvas.width, playerCanvas.height)
		height: inlinePlayer.fittedMediaHeight(playerCanvas.width, playerCanvas.height)
		x: Math.round((playerCanvas.width - width) / 2)
		y: Math.round((playerCanvas.height - height) / 2)
		active: inlinePlayer.normalizedVisualFixtureMode.length === 0
			&& inlinePlayer.ready && !inlinePlayer.nativeDirectMedia
			&& !!inlinePlayer.mediaProfileFactory
			&& String(inlinePlayer.session ? inlinePlayer.session.error || "" : "").length === 0
		clip: true
		sourceComponent: WebEngineView {
			id: webPlayer
			property int loadGeneration: -1
			profile: inlinePlayer.mediaProfileFactory
				? inlinePlayer.mediaProfileFactory.videoProfile : null
			url: inlinePlayer.rendererDocumentUrl
			enabled: inlinePlayer.providerInputEnabled
			activeFocusOnTab: inlinePlayer.providerInputEnabled
			Accessible.ignored: !inlinePlayer.providerInputEnabled
			settings.playbackRequiresUserGesture: false
			settings.localContentCanAccessRemoteUrls: inlinePlayer.adaptiveManifest
			Accessible.name: qsTr("Embedded media provider playback")
			onLoadProgressChanged: {
				if (loadGeneration !== inlinePlayer.mediaGeneration
						|| !inlinePlayer.rendererHealthy || !inlinePlayer.session)
					return
				// 100 means a verified provider surface, not merely that WebEngine
				// finished receiving an HTML document.
				inlinePlayer.session.reportLoadProgress(Math.min(99, loadProgress))
				if (loadProgress === 100) {
					const generation = loadGeneration
					Qt.callLater(function() { inlinePlayer.probeMediaDocumentReady(generation) })
				}
			}
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
				} else if (request.status === WebEngineView.LoadSucceededStatus) {
					inlinePlayer.session.reportLoadProgress(99)
					Qt.callLater(function() { inlinePlayer.probeMediaDocumentReady(generation) })
				}
			}
			onRenderProcessTerminated: {
				const generation = loadGeneration
				if (generation === inlinePlayer.mediaGeneration)
					webPlayer.stop()
				if (inlinePlayer.failMediaDocument(generation) && inlinePlayer.session)
					inlinePlayer.session.reportError(qsTr("The inline player stopped unexpectedly."))
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
				if (!inlinePlayer.navigationRequestAllowed(request.url, inlinePlayer.rendererDocumentUrl))
					request.action = WebEngineNavigationRequest.IgnoreRequest
			}
			Connections {
				target: webPlayer.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
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
					const pollToken = inlinePlayer.statePollToken
					try {
						webPlayer.runJavaScript(inlinePlayer.playerScript("state", 0), function(value) {
							inlinePlayer.completeStatePoll(generation, value, pollToken)
						})
					} catch (error) {
						inlinePlayer.cancelStatePoll(generation, pollToken)
						if (inlinePlayer.failMediaDocument(generation) && inlinePlayer.session)
							inlinePlayer.session.reportError(qsTr("The inline player stopped responding."))
					}
				}
			}
			Component.onDestruction: inlinePlayer.invalidateMediaDocument(loadGeneration)
		}
	}

	Timer {
		interval: 50
		running: inlinePlayer.ready && !inlinePlayer.nativeDirectMedia
			&& inlinePlayer.rendererHealthy
			&& !inlinePlayer.documentReady && inlinePlayer.session
			&& inlinePlayer.session.loadProgress >= 99
		repeat: true
		onTriggered: {
			if (inlinePlayer._documentReadyProbeGeneration === inlinePlayer.mediaGeneration
					&& Date.now() - inlinePlayer._documentReadyProbeStartedAt >= 1000) {
				inlinePlayer._documentReadyProbeGeneration = -1
				inlinePlayer._documentReadyProbeStartedAt = 0
				inlinePlayer.documentReadyProbeState = "callback-timeout"
			}
			inlinePlayer.probeMediaDocumentReady(inlinePlayer.mediaGeneration)
		}
	}

	Timer {
		interval: 1500
		running: inlinePlayer.ready && !inlinePlayer.nativeDirectMedia
			&& inlinePlayer.documentReady && inlinePlayer.rendererHealthy
			&& inlinePlayer.session && inlinePlayer.session.error.length === 0
		repeat: true
		onTriggered: inlinePlayer.probeMediaDocumentReady(
			inlinePlayer.mediaGeneration, true)
	}

	Rectangle {
		id: guestPlaybackGuard
		objectName: "inlineMediaGuestPlaybackGuard"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: inlinePlayer.sharedGuestPlaybackLocked
			&& inlinePlayer.rendererState === "active"
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
			border.color: inlinePlayer.providerAccentBorder
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
		interval: Math.max(1, inlinePlayer.statePollTimeoutMs)
		repeat: false
		onTriggered: inlinePlayer.expireStatePoll(pollGeneration, pollToken)
	}

	Timer {
		id: audioStatePollWatchdog
		property int pollGeneration: -1
		property int pollToken: -1
		interval: Math.max(1, inlinePlayer.statePollTimeoutMs)
		repeat: false
		onTriggered: inlinePlayer.expireAudioStatePoll(pollGeneration, pollToken)
	}

	Loader {
		id: audioPlayerLoader
		objectName: "inlineMediaSecondaryAudioSurface"
		width: 1
		height: 1
		opacity: 0.01
		enabled: false
		active: inlinePlayer.normalizedVisualFixtureMode.length === 0
			&& !inlinePlayer.nativeDirectMedia
			&& inlinePlayer.ready && inlinePlayer.documentReady && inlinePlayer.session
			&& String(inlinePlayer.session.provider || "") === "direct"
			&& String(inlinePlayer.session.audioUrl || "").length > 0
			&& String(inlinePlayer.session.error || "").length === 0
			&& !inlinePlayer.secondaryAudioDegraded
			&& !!inlinePlayer.mediaProfileFactory
		property int loadGeneration: -1
		property bool documentReady: false
		sourceComponent: WebEngineView {
			id: audioPlayer
			profile: inlinePlayer.mediaProfileFactory
				? inlinePlayer.mediaProfileFactory.audioProfile : null
			url: inlinePlayer.session ? inlinePlayer.session.audioUrl : ""
			settings.playbackRequiresUserGesture: false
			onLoadingChanged: function(request) {
				if (request.status === WebEngineView.LoadStartedStatus) {
					audioPlayerLoader.loadGeneration = inlinePlayer.beginAudioDocumentLoad()
					audioPlayerLoader.documentReady = false
					return
				}
				const generation = audioPlayerLoader.loadGeneration
				if (!inlinePlayer.session || generation !== inlinePlayer._audioGeneration)
					return
				if (request.status === WebEngineView.LoadFailedStatus) {
					audioPlayerLoader.documentReady = false
					inlinePlayer.reportSecondaryAudioError(generation, request.errorString
						|| qsTr("The audio track could not be loaded."))
				} else if (request.status === WebEngineView.LoadSucceededStatus) {
					if (!inlinePlayer.markAudioDocumentReady(generation))
						return
					audioPlayerLoader.documentReady = true
					Qt.callLater(function() {
						inlinePlayer.applyDesiredPlaybackState(inlinePlayer.mediaGeneration)
					})
				}
			}
			onRenderProcessTerminated: {
				audioPlayerLoader.documentReady = false
				inlinePlayer.reportSecondaryAudioError(audioPlayerLoader.loadGeneration,
					qsTr("The audio track stopped unexpectedly."))
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
				if (!inlinePlayer.session || !inlinePlayer.session.isNavigationAllowed(request.url))
					request.action = WebEngineNavigationRequest.IgnoreRequest
			}
			Connections {
				target: audioPlayer.profile
				ignoreUnknownSignals: true
				function onDownloadRequested(download) { download.cancel() }
			}
			Component.onDestruction: inlinePlayer.invalidateAudioDocument(
				audioPlayerLoader.loadGeneration)
		}
	}

	Rectangle {
		objectName: "inlineMediaSecondaryAudioWarning"
		parent: playerCanvas
		anchors.left: playerLoader.left
		anchors.right: playerLoader.right
		anchors.bottom: playerLoader.bottom
		anchors.margins: Theme.space3
		height: secondaryAudioWarningRow.implicitHeight + Theme.space2
		visible: inlinePlayer.secondaryAudioDegraded && inlinePlayer.ready
		radius: Theme.innerRadius
		color: Theme.withAlpha(Theme.embedOverlayBase, 0.94)
		border.color: Theme.withAlpha(Theme.warning, 0.62)
		z: 7
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Video continues without the separate audio track")
		Accessible.description: inlinePlayer.secondaryAudioWarning

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
				objectName: "inlineMediaSecondaryAudioWarningText"
				Layout.fillWidth: true
				text: qsTr("Video remains available. %1").arg(inlinePlayer.secondaryAudioWarning)
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontCaption
				wrapMode: Text.Wrap
			}
		}
	}

	Rectangle {
		id: loadingOverlay
		objectName: "inlineMediaLoadingSurface"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && !inlinePlayer.documentReady && session.error.length === 0
		color: Theme.withAlpha(Theme.mediaCanvas, 0.96)
		z: 4
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("Loading %1 inline media").arg(inlinePlayer.providerLabel)
		Accessible.description: session && session.loadProgress > 0
			? qsTr("%1 percent loaded").arg(session.loadProgress) : qsTr("Contacting provider")
		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 * 2, 420)
			spacing: inlinePlayer.compactProviderChrome ? Theme.space1 : Theme.space3
			ModernBusyIndicator {
				objectName: "inlineMediaBusyIndicator"
				Layout.alignment: Qt.AlignHCenter
				running: parent.parent.visible
				animated: inlinePlayer.normalizedVisualFixtureMode.length === 0
				Accessible.name: qsTr("Loading inline media")
			}
			Rectangle {
				objectName: "inlineMediaLoadingProviderBadge"
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: Math.max(32, inlineLoadingProviderMark.implicitWidth + Theme.space3)
				Layout.preferredHeight: 24
				radius: height / 2
				color: inlinePlayer.providerAccent
				border.color: Theme.withAlpha(inlinePlayer.providerOnAccent, 0.24)
				Accessible.role: Accessible.StaticText
				Accessible.name: inlinePlayer.providerLabel
				Label {
					id: inlineLoadingProviderMark
					objectName: "inlineMediaLoadingProviderMark"
					anchors.centerIn: parent
					text: inlinePlayer.providerMark
					textFormat: Text.PlainText
					color: inlinePlayer.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					Accessible.ignored: true
				}
			}
			Label {
				objectName: "inlineMediaLoadingHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Loading %1").arg(inlinePlayer.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "inlineMediaLoadingDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				visible: !inlinePlayer.compactProviderChrome
				text: session && session.loadProgress > 0
					? qsTr("%1% loaded").arg(session.loadProgress) : qsTr("Starting provider playback…")
				color: Theme.mediaOverlayTextMuted
				horizontalAlignment: Text.AlignHCenter
			}
			Rectangle {
				objectName: "inlineMediaLoadingProgressTrack"
				Layout.fillWidth: true
				Layout.preferredHeight: 3
				visible: session && session.loadProgress > 0
				radius: height / 2
				color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.20)
				Rectangle {
					width: parent.width * Math.max(0, Math.min(100, session.loadProgress)) / 100
					height: parent.height
					radius: height / 2
					color: inlinePlayer.providerAccent
				}
			}
		}
	}

	Rectangle {
		id: failureOverlay
		objectName: "inlineMediaFailureOverlay"
		parent: playerCanvas
		anchors.fill: playerLoader
		visible: inlinePlayer.ready && session.error.length > 0
		color: Theme.withAlpha(Theme.mediaCanvas, 0.96)
		z: 5
		Accessible.role: Accessible.AlertMessage
		Accessible.name: qsTr("%1 playback failed").arg(inlinePlayer.providerLabel)
		Accessible.description: session ? session.error : ""
		onVisibleChanged: if (visible) Qt.callLater(inlinePlayer.focusFailureControl)
		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space5 * 2, 480)
			spacing: inlinePlayer.compactProviderChrome ? Theme.space2 : Theme.space3
			Rectangle {
				objectName: "inlineMediaFailureProviderBadge"
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: Math.max(32, inlineFailureProviderMark.implicitWidth + Theme.space3)
				Layout.preferredHeight: 24
				radius: height / 2
				color: inlinePlayer.providerAccent
				border.color: Theme.withAlpha(inlinePlayer.providerOnAccent, 0.24)
				Accessible.role: Accessible.StaticText
				Accessible.name: inlinePlayer.providerLabel
				Label {
					id: inlineFailureProviderMark
					objectName: "inlineMediaFailureProviderMark"
					anchors.centerIn: parent
					text: inlinePlayer.providerMark
					textFormat: Text.PlainText
					color: inlinePlayer.providerOnAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.Bold
					Accessible.ignored: true
				}
			}
			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: inlinePlayer.compactProviderChrome ? 0 : 52
				Layout.preferredHeight: inlinePlayer.compactProviderChrome ? 0 : 52
				visible: !inlinePlayer.compactProviderChrome
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
				objectName: "inlineMediaFailureHeading"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("%1 playback failed").arg(inlinePlayer.providerLabel)
				color: Theme.mediaOverlayTextStrong
				font.weight: Font.DemiBold
				font.pixelSize: Theme.fontTitle
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				objectName: "inlineMediaFailureDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: session ? session.error : ""
				color: Theme.mediaOverlayTextMuted
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
				Accessible.role: Accessible.AlertMessage
				visible: !inlinePlayer.compactProviderChrome || text.length < 120
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
		visible: inlinePlayer.nativeControlsVisible
		session: inlinePlayer.session
		embedded: true
		fullscreenAvailable: false
		externalAvailable: false
		onExternalRequested: Qt.openUrlExternally(inlinePlayer.externalMediaUrl())
		onExitConfirmed: disposition => {
			if (disposition === "end-shared") session.endShared()
			else if (disposition === "leave-shared") session.leaveShared()
			else {
				if (session.sharedHost && session.state === "playing")
					session.pause()
				session.closePlayer()
			}
		}
	}
}
