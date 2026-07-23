import QtQuick
import QtTest
import QtMultimedia
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

TestCase {
	id: testCase
	name: "InlineMediaPlayer"
	when: windowShown
	visible: true
	width: 800
	height: 700

	QtObject {
		id: session
		property bool active: false
		property bool detached: false
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property int sharedParticipantCount: 0
		property string provider: "youtube"
		property string mediaMime: ""
		property string url: "https://www.youtube.com/embed/test"
		property string audioUrl: ""
		property var playbackUrl: undefined
		property var playbackAudioUrl: undefined
		property var playbackAudioWarning: undefined
		property bool playbackSourcePreparing: false
		property string state: "paused"
		property string error: ""
		property real position: 0
		property real duration: 0
		property int loadProgress: 0
		property int volume: 100
		property bool muted: false
		property int retryCalls: 0
		property int playCalls: 0
		property int pauseCalls: 0
		property int closeCalls: 0
		property int errorReports: 0
		property int loadReports: 0
		property int lastLoadProgress: -1
		signal sourceChanged()
		signal playRequested()
		signal pauseRequested()
		signal seekRequested(real seconds)
		signal volumeRequested(int value)
		signal mutedRequested(bool value)
		signal retryRequested()

		function play() {
			playCalls += 1
			state = "playing"
		}
		function pause() {
			pauseCalls += 1
			state = "paused"
		}
		function seek(value) {}
		function setVolume(value) {}
		function toggleMuted() {}
		function retry() {
			retryCalls += 1
			state = "loading"
			error = ""
			retryRequested()
		}
		function reportLoadProgress(value) {
			loadReports += 1
			lastLoadProgress = value
		}
		function reportError(value) { errorReports += 1 }
		function reportTypedError(code, value) { errorReports += 1 }
		function reportPlaybackState(position, duration, paused) {}
		function isNavigationAllowed(url) { return true }
		function detach() {}
		function closePlayer() { closeCalls += 1 }
	}

	QtObject {
		id: mediaRuntime
		property var videoProfile: null
		property string videoDocumentUrl: ""
		property bool providerStatePersistent: true
		function isNavigationRequestAllowed(requestUrl, firstPartyUrl) { return true }
		function isVerificationNavigationAllowed(requestUrl, firstPartyUrl) { return true }
	}

	Loader {
		id: playerLoader
		anchors.fill: parent
		Component.onCompleted: setSource(
			Qt.resolvedUrl("../../../mumble/qml-shell/InlineMediaPlayer.qml"),
			{ "session": session, "aspect": "wide" })
	}

	function init() {
		tryVerify(function() { return playerLoader.item !== null })
		testCase.width = 800
		testCase.height = 700
		playerLoader.item.invalidateMediaDocument()
		playerLoader.item.aspect = "wide"
		playerLoader.item.presentationProvider = ""
		playerLoader.item.presentationMode = ""
		playerLoader.item.animationAutoPlayEnabled = true
		playerLoader.item.mediaProfileFactory = null
		playerLoader.item.providerVerificationStabilityMs = 0
		playerLoader.item.providerVerificationProbeCount = 1
		mediaRuntime.videoDocumentUrl = ""
		playerLoader.item.statePollTimeoutMs = 3000
		session.active = false
		playerLoader.item.visualFixtureMode = ""
		session.detached = false
		session.playbackControllable = true
		session.playbackControlAllowed = true
		session.sharedAvailable = false
		session.sharedJoined = false
		session.sharedHost = false
		session.state = "paused"
		session.error = ""
		session.provider = "youtube"
		session.mediaMime = ""
		session.url = "https://www.youtube.com/embed/test"
		session.audioUrl = ""
		session.playbackUrl = undefined
		session.playbackAudioUrl = undefined
		session.playbackAudioWarning = undefined
		session.playbackSourcePreparing = false
		session.retryCalls = 0
		session.playCalls = 0
		session.pauseCalls = 0
		session.closeCalls = 0
		session.errorReports = 0
		session.loadReports = 0
		session.lastLoadProgress = -1
		wait(0)
	}

	function test_adaptive_manifest_uses_local_profile_document() {
		const player = playerLoader.item
		player.visualFixtureMode = "active"
		player.mediaProfileFactory = mediaRuntime
		session.active = true
		session.provider = "direct"
		session.mediaMime = "application/vnd.apple.mpegurl"
		session.url = "https://video.akamai.steamstatic.com/store_trailers/test/master.m3u8"
		mediaRuntime.videoDocumentUrl = "qrc:/media-player/AdaptiveMediaPlayer.html#manifest"
		wait(0)

		verify(player.adaptiveManifest)
		compare(player.rendererDocumentUrl, mediaRuntime.videoDocumentUrl)
		compare(player.externalMediaUrl(), session.url)
	}

	function test_non_adaptive_direct_media_uses_native_surface_without_webengine() {
		const player = playerLoader.item
		session.provider = "direct"
		session.mediaMime = "audio/wav"
		session.url = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		session.active = true
		wait(0)

		verify(player.nativeDirectMedia)
		verify(player.nativeSurfaceActive)
		verify(!player.webSurfaceActive)
		compare(player.rendererBackend, "native")
		verify(findChild(player, "inlineMediaNativeSurface") !== null)

		session.active = false
		wait(0)
		verify(!player.nativeSurfaceActive)
	}

	function test_video_backed_animation_is_silent_looping_and_exposes_only_pause_resume() {
		const player = playerLoader.item
		player.presentationProvider = "reddit"
		player.presentationMode = "animated-image"
		player.animationAutoPlayEnabled = false
		player.visualFixtureMode = "active"
		session.provider = "direct"
		session.mediaMime = "video/mp4"
		session.url = "https://v.redd.it/p91cxpzry9v41/DASH_1080?source=fallback"
		session.audioUrl = "https://v.redd.it/p91cxpzry9v41/DASH_AUDIO_128.mp4"
		session.state = "playing"
		session.active = true
		wait(0)

		const animationToggle = findChild(player, "inlineMediaAnimationToggleButton")
		const popoutButton = findChild(player, "inlineMediaPopoutButton")
		const controls = findChild(player, "mediaTransportActions")
		verify(animationToggle === null && popoutButton === null && controls !== null)
		verify(player.animationPresentation)
		verify(!player.nativeControlsVisible)
		verify(!controls.parent.parent.visible)
		compare(player.Accessible.name, "Reddit inline animated image")
		compare(player.Accessible.description, "")

		player.visualFixtureMode = ""
		session.mediaMime = "audio/wav"
		session.url = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		session.playbackUrl = undefined
		session.playbackAudioUrl = undefined
		wait(0)

		const nativeLoader = findChild(player, "inlineMediaNativeSurface")
		verify(nativeLoader !== null)
		tryVerify(function() { return nativeLoader.item !== null })
		const nativePlayer = nativeLoader.item
		verify(nativePlayer.animationPresentation)
		compare(nativePlayer.animationAutoPlayEnabled, false)
		compare(nativePlayer.secondaryAudioUrl, "")
		verify(nativePlayer.primaryAudioMuted)
		tryVerify(function() { return nativePlayer.primaryPlayer !== null })
		compare(nativePlayer.primaryPlayer.loops, MediaPlayer.Infinite)

		session.active = false
	}

	function test_native_direct_media_waits_for_prepared_playback_source() {
		const player = playerLoader.item
		const originalSource = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		const preparedSource = "file:///C:/private-cache/native-a.wav"
		session.provider = "direct"
		session.mediaMime = "audio/wav"
		session.url = originalSource
		session.playbackUrl = ""
		session.playbackSourcePreparing = true
		session.active = true
		wait(0)

		const nativeLoader = findChild(player, "inlineMediaNativeSurface")
		verify(nativeLoader !== null)
		tryVerify(function() { return nativeLoader.item !== null })
		const nativePlayer = nativeLoader.item
		compare(nativePlayer.sourceUrl, "")
		compare(nativePlayer.rendererState, "loading")
		verify(nativePlayer.primaryPlayer === null)

		const preparingGeneration = nativePlayer.mediaGeneration
		session.playbackUrl = preparedSource
		session.playbackSourcePreparing = false
		tryCompare(nativePlayer, "sourceUrl", preparedSource)
		tryVerify(function() { return nativePlayer.mediaGeneration > preparingGeneration })
		tryVerify(function() { return nativePlayer.primaryPlayer !== null })
		compare(session.url, originalSource)

		session.active = false
	}

	function test_native_direct_media_ignores_late_callbacks_from_replaced_source() {
		const player = playerLoader.item
		session.provider = "direct"
		session.mediaMime = "audio/wav"
		session.url = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		session.active = true
		wait(0)

		const nativeLoader = findChild(player, "inlineMediaNativeSurface")
		verify(nativeLoader !== null)
		tryVerify(function() { return nativeLoader.item !== null })
		const nativePlayer = nativeLoader.item
		const replacedGeneration = nativePlayer.mediaGeneration
		session.errorReports = 0
		nativePlayer.sourceUrl = session.url + "#replacement"

		verify(nativePlayer.mediaGeneration > replacedGeneration)
		verify(!nativePlayer.reportMainError("late source A error", replacedGeneration))
		verify(!nativePlayer.acceptMainReady(replacedGeneration, 100))
		verify(nativePlayer.rendererHealthy)
		compare(session.errorReports, 0)

		session.active = false
	}

	function test_secondary_audio_hang_degrades_and_restores_primary_audio() {
		const player = playerLoader.item
		session.provider = "direct"
		session.mediaMime = "audio/wav"
		session.url = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		session.active = true
		wait(0)

		const nativeLoader = findChild(player, "inlineMediaNativeSurface")
		verify(nativeLoader !== null)
		tryVerify(function() { return nativeLoader.item !== null })
		const nativePlayer = nativeLoader.item
		nativePlayer.secondaryAudioUrl = "data:audio/mp4;base64,AAAA"
		nativePlayer.setMuted(false)
		verify(!nativePlayer.primaryAudioMuted)
		const readyGeneration = nativePlayer.secondaryAudioGeneration
		verify(nativePlayer.acceptSecondaryReady(readyGeneration))
		verify(nativePlayer.primaryAudioMuted)

		verify(nativePlayer.beginSecondaryPending(readyGeneration))
		const audioGeneration = readyGeneration
		verify(!nativePlayer.primaryAudioMuted)

		verify(nativePlayer.expireSecondaryAudioLoad(audioGeneration))
		verify(nativePlayer.secondaryAudioDegraded)
		compare(nativePlayer.secondaryAudioState, "degraded")
		verify(nativePlayer.secondaryAudioWarning.indexOf("timed out") >= 0)
		verify(!nativePlayer.primaryAudioMuted)
		verify(!nativePlayer.acceptSecondaryReady(audioGeneration))

		session.active = false
	}

	function test_secondary_audio_preparation_failure_keeps_primary_playback_available() {
		const player = playerLoader.item
		session.provider = "direct"
		session.mediaMime = "video/mp4"
		session.url = "data:video/mp4;base64,AAAA"
		session.audioUrl = "data:audio/mp4;base64,INVALID"
		session.playbackUrl = "file:///C:/private-cache/native-video.mp4"
		session.playbackAudioUrl = ""
		session.playbackAudioWarning = "The separate audio track could not be prepared."
		session.muted = false
		session.active = true
		wait(0)

		const nativeLoader = findChild(player, "inlineMediaNativeSurface")
		verify(nativeLoader !== null)
		tryVerify(function() { return nativeLoader.item !== null })
		const nativePlayer = nativeLoader.item
		tryVerify(function() { return nativePlayer.secondaryAudioDegraded })
		compare(nativePlayer.secondaryAudioState, "degraded")
		compare(nativePlayer.secondaryAudioWarning,
			"The separate audio track could not be prepared.")
		verify(!nativePlayer.primaryAudioMuted)
		verify(nativePlayer.secondaryPlayer === null)

		session.active = false
	}

	function test_watch_together_guest_blocks_provider_input_but_keeps_local_audio_controls() {
		const player = playerLoader.item
		player.visualFixtureMode = "active"
		session.active = true
		session.sharedAvailable = true
		session.sharedJoined = true
		session.sharedHost = false
		session.playbackControlAllowed = false
		wait(0)

		verify(player.sharedGuestPlaybackLocked)
		verify(!player.providerInputEnabled)
		const guard = findChild(player, "inlineMediaGuestPlaybackGuard")
		const playButton = findChild(player, "mediaPlayButton")
		const muteButton = findChild(player, "mediaMuteButton")
		const volumeButton = findChild(player, "mediaCompactVolumeButton")
		verify(guard !== null && guard.visible)
		compare(guard.Accessible.name, "Playback controlled by the host")
		verify(playButton !== null && !playButton.enabled)
		verify(muteButton !== null && muteButton.enabled)
		verify(volumeButton !== null && volumeButton.enabled)

		session.sharedHost = true
		session.playbackControlAllowed = true
		wait(0)
		verify(!player.sharedGuestPlaybackLocked)
		verify(player.providerInputEnabled)
		verify(!guard.visible)
	}

	function test_secondary_audio_failure_degrades_to_primary_video_warning() {
		const player = playerLoader.item
		player.visualFixtureMode = "active"
		session.active = true
		session.provider = "direct"
		session.audioUrl = "data:audio/mp4;base64,AAAA"
		const mediaGeneration = player.beginMediaDocumentLoad("about:blank")
		verify(player.markMediaDocumentReady(mediaGeneration))
		const audioGeneration = player.beginAudioDocumentLoad()
		verify(player.markAudioDocumentReady(audioGeneration))

		verify(player.reportSecondaryAudioError(audioGeneration, "Audio decoder stopped"))
		verify(player.documentReady)
		compare(player.rendererState, "active")
		verify(player.secondaryAudioDegraded)
		compare(player.secondaryAudioState, "degraded")
		verify(player.secondaryAudioWarning.indexOf("Audio decoder stopped") >= 0)
		compare(session.errorReports, 0)
		compare(session.error, "")
		const warning = findChild(player, "inlineMediaSecondaryAudioWarning")
		const canvas = findChild(player, "inlineMediaCanvas")
		const failure = findChild(player, "inlineMediaFailureOverlay")
		verify(warning !== null && warning.visible && canvas !== null)
		compare(warning.Accessible.name, "Video continues without the separate audio track")
		const warningOrigin = warning.mapToItem(player, 0, 0)
		const canvasOrigin = canvas.mapToItem(player, 0, 0)
		verify(warningOrigin.y >= canvasOrigin.y + canvas.height - 0.5,
			"degraded-audio status must remain below, not over, the media canvas")
		verify(failure !== null && !failure.visible)

		const replacementGeneration = player.beginMediaDocumentLoad("about:blank#retry")
		verify(replacementGeneration > mediaGeneration)
		verify(!player.secondaryAudioDegraded)
		compare(player.secondaryAudioWarning, "")
		verify(!warning.visible)
	}

	function test_document_readiness_gates_state_polling() {
		const player = playerLoader.item
		const generation = player.beginMediaDocumentLoad("https://www.youtube.com/embed/first")
		compare(player.mediaGeneration, generation)
		verify(player.rendererHealthy)
		verify(!player.documentReady)
		verify(!player.claimStatePoll(generation))

		verify(player.markMediaDocumentReady(generation))
		verify(player.documentReady)
		verify(player.claimStatePoll(generation))
		verify(player.statePollInFlight)
		verify(!player.claimStatePoll(generation))
		verify(player.cancelStatePoll(generation))
		verify(!player.statePollInFlight)
	}

	function test_playing_session_stays_in_loading_state_until_document_is_ready() {
		const player = playerLoader.item
		session.state = "playing"
		session.active = true
		const generation = player.beginMediaDocumentLoad("about:blank")
		compare(player.rendererState, "loading")
		verify(player.markMediaDocumentReady(generation))
		compare(player.rendererState, "active")
		session.active = false
	}

	function test_verified_document_completion_is_generation_bound() {
		const player = playerLoader.item
		session.active = true
		const firstGeneration = player.beginMediaDocumentLoad("about:blank")
		verify(player.completeMediaDocumentLoad(firstGeneration))
		verify(player.documentReady)
		compare(session.loadReports, 1)
		compare(session.lastLoadProgress, 100)

		const secondGeneration = player.beginMediaDocumentLoad("about:blank#second")
		verify(!player.completeMediaDocumentLoad(firstGeneration))
		verify(!player.documentReady)
		compare(session.loadReports, 1)
		verify(player.completeMediaDocumentLoad(secondGeneration))
		verify(player.documentReady)
		compare(session.loadReports, 2)
		session.active = false
	}

	function test_provider_surface_probe_rejects_challenges_and_verifies_every_allowlisted_transport() {
		const player = playerLoader.item
		session.active = true
		session.provider = "youtube"
		let generation = player.beginMediaDocumentLoad("about:blank#challenge")
		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": false,
			"blockedKind": "verification",
			"detail": "Logga in för att bekräfta att du inte är en bot"
		}, false))
		verify(!player.documentReady)
		compare(player.surfaceVerificationState, "blocked")
		verify(player.surfaceVerificationDetail.indexOf("Complete verification") >= 0)
		verify(player.surfaceVerificationDetail.indexOf("reload the media") >= 0)
		compare(session.errorReports, 0)
		verify(player.providerVerificationRequired)
		verify(!player.transportVerified)
		verify(!player.nativeControlsVisible)
		const loadingSurface = findChild(player, "inlineMediaLoadingSurface")
		verify(findChild(player, "inlineMediaVerificationStrip") === null)
		verify(player.Accessible.description.indexOf("Complete verification") >= 0)
		verify(loadingSurface !== null && !loadingSurface.visible)

		generation = player.beginMediaDocumentLoad("about:blank#consent")
		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": true,
			"mediaPresent": true,
			"blockedKind": "consent",
			"detail": "Allow all cookies"
		}, false))
		compare(player.surfaceVerificationState, "blocked")
		verify(player.surfaceVerificationDetail.indexOf("cookie preferences") >= 0)
		verify(!player.playbackVerified)

		const providers = [ "youtube", "twitch", "streamable", "vimeo",
			"dailymotion", "spotify", "facebook", "tiktok", "instagram",
			"soundcloud" ]
		for (let index = 0; index < providers.length; ++index) {
			session.provider = providers[index]
			generation = player.beginMediaDocumentLoad("about:blank#" + providers[index])
			player.documentReadyProbeAttempts = 1
			verify(player.applyMediaSurfaceProbeResult(generation, {
				"readyState": "complete",
				"transport": true,
				"providerUi": true,
				"mediaPresent": providers[index] === "youtube",
				"mediaReady": providers[index] === "youtube",
				"playbackEvidence": false
			}, false))
			verify(player.documentReady)
			verify(player.surfaceVerified)
			verify(player.transportVerified)
			compare(player.surfaceVerificationState, "verified")
			verify(player.surfaceVerificationEvidence.indexOf("transport") >= 0)
		}
		session.active = false
	}

	function test_provider_verification_requires_stability_and_revokes_late_false_positive() {
		const player = playerLoader.item
		player.providerVerificationStabilityMs = 1200
		player.providerVerificationProbeCount = 3
		session.active = true
		session.provider = "youtube"
		const generation = player.beginMediaDocumentLoad("about:blank#late-challenge")
		const verifiedProbe = {
			"readyState": "complete",
			"transport": true,
			"providerUi": true,
			"mediaPresent": true,
			"mediaReady": true,
			"playbackEvidence": true
		}

		verify(!player.applyMediaSurfaceProbeResult(generation, verifiedProbe, false))
		verify(!player.documentReady)
		compare(player.surfaceVerificationState, "pending")
		player._verifiedProbeCount = 2
		player._verifiedProbeStartedAt = Date.now() - 1300
		verify(player.applyMediaSurfaceProbeResult(generation, verifiedProbe, false))
		verify(player.documentReady)
		verify(player.playbackVerified)

		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": false,
			"blockedKind": "verification",
			"detail": "Logga in för att bekräfta att du inte är en bot"
		}, true))
		verify(!player.documentReady)
		verify(!player.transportVerified)
		verify(!player.playbackVerified)
		verify(player.providerVerificationRequired)
		compare(session.errorReports, 0)
		compare(session.error, "")
		verify(findChild(player, "inlineMediaVerificationStrip") === null)
		verify(player.Accessible.description.indexOf("Complete verification") >= 0)
		session.active = false
	}

	function test_provider_verification_navigation_survives_auth_redirects_until_reload() {
		const player = playerLoader.item
		session.active = true
		const generation = player.beginMediaDocumentLoad("about:blank#challenge-navigation")
		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": false,
			"blockedKind": "verification",
			"detail": "Sign in to confirm you are not a bot"
		}, false))
		verify(player.providerVerificationRequired)

		player.verificationNavigationActive = true
		player.beginMediaDocumentLoad("https://accounts.google.com/v3/signin/identifier")
		verify(player.verificationNavigationActive)
		verify(player.navigationRequestAllowed(
			"https://accounts.google.com/v3/signin/identifier",
			player.rendererDocumentUrl,
			player.verificationNavigationActive))

		session.retry()
		compare(player.verificationNavigationActive, false)
		session.active = false
	}

	function test_background_probe_persists_real_playback_evidence() {
		const player = playerLoader.item
		session.active = true
		session.provider = "vimeo"
		const generation = player.beginMediaDocumentLoad("about:blank#vimeo")
		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete", "transport": true, "providerUi": true,
			"playbackEvidence": false
		}, false))
		verify(!player.playbackVerified)
		verify(player.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete", "transport": true, "providerUi": true,
			"mediaPresent": true, "mediaReady": true, "playbackEvidence": true
		}, true))
		verify(player.playbackVerified)
		verify(player.surfaceVerificationEvidence.indexOf("playback") >= 0)
		session.active = false
	}

	function test_late_poll_callback_cannot_mutate_new_document_generation() {
		const player = playerLoader.item
		const firstGeneration = player.beginMediaDocumentLoad("https://www.youtube.com/embed/first")
		verify(player.markMediaDocumentReady(firstGeneration))
		verify(player.claimStatePoll(firstGeneration))

		const secondGeneration = player.beginMediaDocumentLoad("https://www.youtube.com/embed/second")
		verify(secondGeneration > firstGeneration)
		verify(player.markMediaDocumentReady(secondGeneration))
		verify(player.claimStatePoll(secondGeneration))
		verify(player.statePollInFlight)

		verify(!player.completeStatePoll(firstGeneration,
			{ "position": 15, "duration": 90, "paused": false }))
		compare(player.mediaGeneration, secondGeneration)
		verify(player.documentReady)
		verify(player.statePollInFlight)
		verify(player.cancelStatePoll(secondGeneration))
	}

	function test_state_poll_watchdog_releases_only_its_own_poll_attempt() {
		const player = playerLoader.item
		session.active = true
		const generation = player.beginMediaDocumentLoad("about:blank")
		verify(player.markMediaDocumentReady(generation))
		player.statePollTimeoutMs = 25

		verify(player.claimStatePoll(generation))
		const expiredToken = player.statePollToken
		tryVerify(function() { return !player.statePollInFlight }, 1000)
		compare(player._missingStatePolls, 1)

		verify(player.claimStatePoll(generation))
		const currentToken = player.statePollToken
		verify(currentToken > expiredToken)
		verify(!player.expireStatePoll(generation, expiredToken))
		verify(player.statePollInFlight)
		verify(!player.completeStatePoll(generation,
			{ "position": 9, "duration": 30, "paused": false }, expiredToken))
		verify(player.statePollInFlight)
		verify(player.cancelStatePoll(generation, currentToken))
		session.active = false
	}

	function test_secondary_audio_poll_watchdog_is_generation_and_attempt_bound() {
		const player = playerLoader.item
		session.active = true
		player.statePollTimeoutMs = 25
		const generation = player.beginAudioDocumentLoad()

		verify(player.claimAudioStatePoll(generation))
		const expiredToken = player.secondaryAudioStatePollToken
		tryVerify(function() { return !player.secondaryAudioStatePollInFlight }, 1000)
		compare(player._audioMissingStatePolls, 1)

		verify(player.claimAudioStatePoll(generation))
		const currentToken = player.secondaryAudioStatePollToken
		verify(currentToken > expiredToken)
		verify(!player.expireAudioStatePoll(generation, expiredToken))
		verify(!player.cancelAudioStatePoll(generation, expiredToken))
		verify(player.secondaryAudioStatePollInFlight)
		verify(player.cancelAudioStatePoll(generation, currentToken))
		session.active = false
	}

	function test_renderer_failure_invalidates_document_and_pending_poll() {
		const player = playerLoader.item
		const generation = player.beginMediaDocumentLoad("https://www.youtube.com/embed/failure")
		verify(player.markMediaDocumentReady(generation))
		verify(player.claimStatePoll(generation))

		verify(player.failMediaDocument(generation))
		verify(player.mediaGeneration > generation)
		verify(!player.rendererHealthy)
		verify(!player.documentReady)
		verify(!player.statePollInFlight)
		verify(!player.markMediaDocumentReady(generation))
		verify(!player.completeStatePoll(generation,
			{ "position": 20, "duration": 100, "paused": true }))
	}

	function test_aspect_values_are_normalized_and_unknown_values_fail_safe() {
		const player = playerLoader.item
		compare(player.normalizeAspect("TWITCH"), "twitch")
		compare(player.normalizeAspect(" compact-audio "), "compact-audio")
		compare(player.normalizeAspect("unsupported"), "wide")
		player.aspect = "SHORT"
		tryCompare(player, "normalizedAspect", "short")
	}

	function test_visual_surface_preserves_wide_short_square_and_twitch_geometry() {
		const player = playerLoader.item
		const canvas = findChild(player, "inlineMediaCanvas")
		const surface = findChild(player, "inlineMediaWebSurface")
		verify(canvas !== null && surface !== null)

		player.aspect = "wide"
		wait(0)
		verify(Math.abs(surface.width / surface.height - 16 / 9) < 0.01)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))

		player.aspect = "short"
		wait(0)
		verify(Math.abs(surface.width / surface.height - 9 / 16) < 0.01)
		verify(surface.width < canvas.width)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))

		player.aspect = "square"
		wait(0)
		verify(Math.abs(surface.width - surface.height) < 0.01)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))

		player.aspect = "twitch"
		wait(0)
		verify(Math.abs(surface.width / surface.height - 4 / 3) < 0.01)
		compare(surface.x, Math.round((canvas.width - surface.width) / 2))
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))
	}

	function test_source_change_invalidates_both_renderer_generations() {
		const player = playerLoader.item
		session.active = true
		const mediaGeneration = player.beginMediaDocumentLoad("https://www.youtube.com/embed/old")
		const audioGeneration = player.beginAudioDocumentLoad()
		verify(player.markMediaDocumentReady(mediaGeneration))
		verify(player.markAudioDocumentReady(audioGeneration))
		verify(player.documentReady)
		compare(player._audioDocumentReadyGeneration, audioGeneration)

		session.sourceChanged()
		verify(player.mediaGeneration > mediaGeneration)
		verify(player._audioGeneration > audioGeneration)
		verify(!player.documentReady)
		compare(player._audioDocumentReadyGeneration, -1)
	}

	function test_error_state_invalidates_primary_and_secondary_audio_lifecycles() {
		const player = playerLoader.item
		const mediaGeneration = player.beginMediaDocumentLoad("https://example.com/video")
		verify(player.markMediaDocumentReady(mediaGeneration))
		const audioGeneration = player.beginAudioDocumentLoad()

		session.error = "The renderer stopped."
		session.state = "error"
		verify(player.mediaGeneration > mediaGeneration)
		verify(player._audioGeneration > audioGeneration)
		verify(!player.documentReady)
		compare(player._audioDocumentReadyGeneration, -1)
	}

	function test_secondary_audio_surface_cannot_activate_behind_error_ui() {
		const player = playerLoader.item
		session.error = "The primary renderer failed."
		session.provider = "direct"
		session.audioUrl = "about:blank"
		session.active = true
		const generation = player.beginMediaDocumentLoad("about:blank")
		verify(player.markMediaDocumentReady(generation))
		verify(player.ready)
		verify(player.documentReady)
		wait(0)
		verify(!player.secondaryAudioActive)
		session.active = false
	}

	function test_card_sized_player_keeps_a_full_width_viewport_above_one_control_row() {
		const player = playerLoader.item
		const canvas = findChild(player, "inlineMediaCanvas")
		const surface = findChild(player, "inlineMediaWebSurface")
		const controls = findChild(player, "mediaTransportActions")
		verify(canvas !== null && surface !== null && controls !== null)
		session.active = true
		player.visualFixtureMode = "active"
		wait(0)

		for (const width of [ 460, 580, 720 ]) {
			testCase.width = width
			wait(0)
			testCase.height = Math.ceil(player.implicitHeight)
			wait(0)
			verify(Math.abs(canvas.height - player.mediaViewportHeight) < 1)
			verify(surface.width >= canvas.width * 0.99)
			verify(Math.abs(surface.width / surface.height - 16 / 9) < 0.01)
			verify(controls.height <= 64)
		}

		testCase.width = 340
		wait(0)
		testCase.height = Math.ceil(player.implicitHeight)
		wait(0)
		verify(canvas.height >= 190)
		verify(surface.width >= canvas.width * 0.99)
		verify(player.height >= canvas.height + 40)
		session.active = false
		player.visualFixtureMode = ""
	}

	function test_provider_controlled_embed_keeps_the_media_viewport_free_of_mumble_chrome() {
		const player = playerLoader.item
		session.provider = "vimeo"
		session.playbackControllable = false
		session.active = true
		player.visualFixtureMode = "active"
		wait(0)

		const canvas = findChild(player, "inlineMediaCanvas")
		const controls = findChild(player, "mediaTransportActions")
		const closeButton = findChild(player, "inlineMediaProviderCloseButton")
		verify(canvas !== null && controls !== null && closeButton === null)
		verify(!player.nativeControlsVisible)
		verify(!controls.parent.parent.visible)
		verify(Math.abs(player.implicitHeight - player.mediaViewportHeight) < 1)
		testCase.height = Math.ceil(player.implicitHeight)
		wait(0)
		verify(Math.abs(canvas.height - player.mediaViewportHeight) < 1)
		verify(!player.focusInitialControl())

		session.playbackControllable = true
		session.active = false
	}

	function test_direct_transport_can_keep_the_originating_provider_identity() {
		const player = playerLoader.item
		session.provider = "direct"
		player.presentationProvider = "reddit"
		player.visualFixtureMode = "active"
		session.active = true
		wait(0)

		compare(player.providerLabel, "Reddit")
		compare(player.providerMark, "R")
		compare(player.Accessible.name, "Reddit inline media player")
		verify(findChild(player, "inlineMediaProviderLabel") === null)
		session.active = false
	}

	function test_audio_surfaces_stay_low_and_controls_keep_full_width() {
		const player = playerLoader.item
		const canvas = findChild(player, "inlineMediaCanvas")
		const surface = findChild(player, "inlineMediaWebSurface")

		player.aspect = "audio"
		wait(0)
		compare(surface.width, canvas.width)
		verify(surface.height <= 352)
		const audioHeight = surface.height

		player.aspect = "compact-audio"
		wait(0)
		compare(surface.width, canvas.width)
		verify(surface.height <= 166)
		verify(surface.height < audioHeight)
		compare(surface.y, Math.round((canvas.height - surface.height) / 2))
	}

	function test_accessibility_exposes_one_named_player_without_overlay_toolbar_actions() {
		const player = playerLoader.item
		compare(player.Accessible.role, Accessible.Pane)
		compare(player.Accessible.name, "YouTube inline media player")
		compare(player.surfaceId, "mediaSession.inline")
		verify(player.captureRect.width > 0)
		verify(!player.webSurfaceActive)
		compare(player.rendererState, "empty")

		verify(findChild(player, "inlineMediaPopoutButton") === null)
		verify(findChild(player, "inlineMediaExternalButton") === null)
		verify(findChild(player, "inlineMediaProviderBadge") === null)
	}

	function test_dark_media_surfaces_use_overlay_contrast_tokens() {
		const player = playerLoader.item
		const strongLabels = [
			findChild(player, "inlineMediaFixtureHeading"),
			findChild(player, "inlineMediaLoadingHeading"),
			findChild(player, "inlineMediaFailureHeading")
		]
		const mutedLabels = [
			findChild(player, "inlineMediaFixtureDetail"),
			findChild(player, "inlineMediaLoadingDetail"),
			findChild(player, "inlineMediaFailureDetail")
		]
		for (const label of strongLabels) {
			verify(label !== null)
			compare(String(label.color), String(Theme.mediaOverlayTextStrong))
		}
		for (const label of mutedLabels) {
			verify(label !== null)
			compare(String(label.color), String(Theme.mediaOverlayTextMuted))
		}

		const progressTrack = findChild(player, "inlineMediaLoadingProgressTrack")
		verify(progressTrack !== null)
		compare(String(progressTrack.color),
			String(Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.20)))
	}

	function test_provider_presentation_styles_active_loading_error_and_compact_states() {
		const player = playerLoader.item
		const presentation = ProviderPresentation.resolve("youtube")
		session.provider = "youtube"
		session.active = true
		player.visualFixtureMode = "active"
		wait(0)

		compare(player.providerLabel, presentation.label)
		compare(player.providerMark, presentation.mark)
		compare(String(player.providerAccent), String(presentation.accent))
		compare(String(player.providerOnAccent), String(Theme.contrastText(player.providerAccent)))
		compare(player.Accessible.name, "YouTube inline media player")

		verify(findChild(player, "inlineMediaProviderBadge") === null)
		verify(findChild(player, "inlineMediaProviderMark") === null)
		verify(findChild(player, "inlineMediaProviderLabel") === null)

		testCase.width = 420
		wait(0)
		verify(player.compactProviderChrome)

		player.visualFixtureMode = "loading"
		wait(0)
		const loadingBadge = findChild(player, "inlineMediaLoadingProviderBadge")
		const loadingSurface = findChild(player, "inlineMediaLoadingSurface")
		const loadingBusy = findChild(player, "inlineMediaBusyIndicator")
		const loadingMark = findChild(player, "inlineMediaLoadingProviderMark")
		const loadingHeading = findChild(player, "inlineMediaLoadingHeading")
		verify(loadingSurface !== null && loadingBadge !== null && loadingBadge.visible
			&& loadingBusy !== null && loadingBusy.visible)
		compare(loadingBusy.animated, false)
		compare(loadingBusy.rotation, 0)
		compare(loadingSurface.Accessible.name, "Loading YouTube inline media")
		compare(String(loadingBadge.color), String(player.providerAccent))
		compare(loadingBadge.Accessible.name, "YouTube")
		compare(loadingMark.text, "YT")
		compare(loadingHeading.text, "Loading YouTube")

		player.visualFixtureMode = "error"
		session.error = "Renderer stopped"
		session.state = "error"
		wait(0)
		const failureBadge = findChild(player, "inlineMediaFailureProviderBadge")
		const failureSurface = findChild(player, "inlineMediaFailureOverlay")
		const failureMark = findChild(player, "inlineMediaFailureProviderMark")
		const failureHeading = findChild(player, "inlineMediaFailureHeading")
		verify(failureSurface !== null && failureBadge !== null && failureBadge.visible)
		compare(failureSurface.Accessible.name, "YouTube playback failed")
		compare(String(failureBadge.color), String(player.providerAccent))
		compare(failureMark.text, "YT")
		compare(failureHeading.text, "YouTube playback failed")

		session.provider = "Acme Video"
		wait(0)
		compare(player.providerLabel, "Acme Video")
		compare(player.providerMark, "AV")
		compare(String(player.providerAccent), String(Theme.accent))
	}

	function test_focus_handoff_targets_first_transport_control() {
		const player = playerLoader.item
		const playButton = findChild(player, "mediaPlayButton")
		verify(playButton !== null)
		session.active = true
		player.visualFixtureMode = "active"
		wait(0)
		verify(player.focusInitialControl())
		compare(playButton.activeFocus, true)
		session.active = false
		player.visualFixtureMode = ""
	}

	function test_failure_moves_focus_to_retry_and_retry_is_the_initial_control() {
		const player = playerLoader.item
		const failureOverlay = findChild(player, "inlineMediaFailureOverlay")
		const retryButton = findChild(player, "inlineMediaRetryButton")
		verify(failureOverlay !== null && retryButton !== null)

		session.state = "error"
		session.error = "The provider renderer stopped."
		session.active = true
		tryCompare(failureOverlay, "visible", true)
		compare(player.rendererState, "error")
		verify(!player.webSurfaceActive)
		tryCompare(retryButton, "activeFocus", true)
		verify(player.focusInitialControl())
		compare(retryButton.activeFocus, true)
		compare(retryButton.Accessible.name, "Retry")

		retryButton.clicked()
		compare(session.retryCalls, 1)
	}
}
