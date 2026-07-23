import QtQuick
import QtTest
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

TestCase {
	id: testCase
	name: "MediaSessionWindow"
	when: windowShown
	width: 320
	height: 240
	property alias mediaSession: session

	QtObject {
		id: mediaRuntime
		property bool runtimeReady: false
		property bool runtimePreparing: false
		property string runtimeError: ""
		property var videoProfile: null
		property var audioProfile: null
		property string videoDocumentUrl: ""
		property bool navigationAllowed: true
		property int retryCalls: 0
		function isNavigationRequestAllowed(requestUrl, firstPartyUrl) {
			return navigationAllowed
		}
		function retryRuntime() {
			retryCalls += 1
			runtimeError = ""
			runtimePreparing = true
		}
	}

	QtObject {
		id: session
		property bool active: false
		property bool detached: true
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property int sharedParticipantCount: 0
		property string sharedTitle: ""
		property string provider: "youtube"
		property string mediaMime: ""
		property string url: "https://www.youtube.com/embed/test"
		property string audioUrl: ""
		property string state: "paused"
		property string error: ""
		property real position: 0
		property real duration: 0
		property int loadProgress: 0
		property int volume: 100
		property bool muted: false
		property int playbackReports: 0
		property int errorReports: 0
		property int retryCalls: 0
		property int loadReports: 0
		property int lastLoadProgress: -1
		property real lastReportedPosition: -1
		property bool lastReportedPaused: false
		property int closePlayerCalls: 0
		signal sourceChanged()
		signal playRequested()
		signal pauseRequested()
		signal seekRequested(real seconds)
		signal volumeRequested(int value)
		signal mutedRequested(bool value)
		signal retryRequested()

		function play() {}
		function pause() {}
		function seek(value) {}
		function setVolume(value) {}
		function toggleMuted() {}
		function closePlayer() { closePlayerCalls += 1 }
		function endShared() {}
		function leaveShared() {}
		function reportLoadProgress(value) {
			loadReports += 1
			lastLoadProgress = value
		}
		function reportError(value) { errorReports += 1 }
		function reportTypedError(code, value) { errorReports += 1 }
		function retry() {
			retryCalls += 1
			error = ""
			state = "loading"
			retryRequested()
		}
		function reportPlaybackState(position, duration, paused) {
			playbackReports += 1
			lastReportedPosition = position
			lastReportedPaused = paused
		}
		function isNavigationAllowed(url) { return true }
	}

	Loader {
		id: windowLoader
		Component.onCompleted: source = Qt.resolvedUrl("../../../mumble/qml-shell/MediaSessionWindow.qml")
	}

	function init() {
		tryVerify(function() { return windowLoader.item !== null })
		session.provider = "youtube"
		session.mediaMime = ""
		session.detached = true
		session.playbackControllable = true
		session.playbackControlAllowed = true
		session.sharedAvailable = false
		session.sharedJoined = false
		session.sharedHost = false
		session.url = "https://www.youtube.com/embed/test"
		session.active = false
		session.error = ""
		session.state = "paused"
		session.audioUrl = ""
		session.playbackReports = 0
		session.errorReports = 0
		session.retryCalls = 0
		session.loadReports = 0
		session.lastLoadProgress = -1
		session.lastReportedPosition = -1
		session.lastReportedPaused = false
		session.closePlayerCalls = 0
		windowLoader.item.visualFixtureMode = ""
		windowLoader.item.statePollTimeoutMs = 3000
		windowLoader.item.mediaProfileFactory = mediaRuntime
		mediaRuntime.runtimeReady = false
		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeError = ""
		mediaRuntime.videoDocumentUrl = ""
		mediaRuntime.navigationAllowed = true
		mediaRuntime.retryCalls = 0
		windowLoader.item.invalidateMediaDocumentIfNeeded()
		wait(0)
	}

	function test_close_policy_accepts_inactive_loader_teardown_only() {
		const window = windowLoader.item
		const loaderClose = { "accepted": false }
		session.active = false
		window.handleWindowClosing(loaderClose)
		verify(loaderClose.accepted)
		compare(session.closePlayerCalls, 0)

		const userClose = { "accepted": true }
		session.active = true
		session.sharedAvailable = false
		session.sharedJoined = false
		window.handleWindowClosing(userClose)
		verify(!userClose.accepted)
		compare(session.closePlayerCalls, 1)
	}

	function test_adaptive_manifest_uses_local_profile_document() {
		const window = windowLoader.item
		window.visualFixtureMode = "active"
		session.active = true
		session.provider = "direct"
		session.mediaMime = "application/dash+xml"
		session.url = "https://video.akamai.steamstatic.com/store_trailers/test/manifest.mpd"
		mediaRuntime.videoDocumentUrl = "qrc:/media-player/AdaptiveMediaPlayer.html#manifest"
		wait(0)

		verify(window.adaptiveManifest)
		compare(window.rendererDocumentUrl, mediaRuntime.videoDocumentUrl)
		compare(window.externalMediaUrl(), session.url)
	}

	function test_non_adaptive_direct_media_uses_native_surface_without_webengine_runtime() {
		const window = windowLoader.item
		session.provider = "direct"
		session.mediaMime = "audio/wav"
		session.url = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="
		session.active = true
		wait(0)

		verify(window.nativeDirectMedia)
		verify(window.nativeSurfaceActive)
		verify(!window.webSurfaceActive)
		compare(window.rendererBackend, "native")
		verify(findChild(window.contentItem, "mediaSessionNativeSurface") !== null)

		session.active = false
		wait(0)
		verify(!window.nativeSurfaceActive)
	}

	function test_watch_together_guest_blocks_provider_input_but_keeps_local_audio_controls() {
		const window = windowLoader.item
		window.visualFixtureMode = "active"
		session.active = true
		session.sharedAvailable = true
		session.sharedJoined = true
		session.sharedHost = false
		session.playbackControlAllowed = false
		wait(0)

		verify(window.sharedGuestPlaybackLocked)
		verify(!window.providerInputEnabled)
		const guard = findChild(window.contentItem, "mediaSessionGuestPlaybackGuard")
		const playButton = findChild(window.contentItem, "mediaPlayButton")
		const muteButton = findChild(window.contentItem, "mediaMuteButton")
		const volumeSlider = findChild(window.contentItem, "mediaVolumeSlider")
		verify(guard !== null && guard.visible)
		compare(guard.Accessible.name, "Playback controlled by the host")
		verify(playButton !== null && !playButton.enabled)
		verify(muteButton !== null && muteButton.enabled)
		verify(volumeSlider !== null && volumeSlider.enabled)

		session.sharedHost = true
		session.playbackControlAllowed = true
		wait(0)
		verify(!window.sharedGuestPlaybackLocked)
		verify(window.providerInputEnabled)
		verify(!guard.visible)
	}

	function test_secondary_audio_failure_degrades_to_primary_video_warning() {
		const window = windowLoader.item
		window.visualFixtureMode = "active"
		session.active = true
		session.provider = "direct"
		session.audioUrl = "data:audio/mp4;base64,AAAA"
		const mediaGeneration = window.beginMediaDocumentLoad("about:blank")

		verify(window.documentReady)
		verify(window.reportSecondaryAudioError(mediaGeneration, "Audio decoder stopped"))
		verify(window.documentReady)
		compare(window.rendererState, "active")
		verify(window.secondaryAudioDegraded)
		compare(window.secondaryAudioState, "degraded")
		verify(window.secondaryAudioWarning.indexOf("Audio decoder stopped") >= 0)
		compare(session.errorReports, 0)
		compare(session.error, "")
		const warning = findChild(window.contentItem, "mediaSessionSecondaryAudioWarning")
		const failure = findChild(window.contentItem, "mediaSessionFailureSurface")
		verify(warning !== null && warning.visible)
		compare(warning.Accessible.name, "Video continues without the separate audio track")
		verify(failure !== null && !failure.visible)

		const replacementGeneration = window.beginMediaDocumentLoad("about:blank#retry")
		verify(replacementGeneration > mediaGeneration)
		verify(!window.secondaryAudioDegraded)
		compare(window.secondaryAudioWarning, "")
		verify(!warning.visible)
	}

	function test_first_detached_activation_waits_for_runtime_readiness() {
		const window = windowLoader.item
		session.url = "https://www.youtube.com/embed/first-popout"
		session.active = true
		mediaRuntime.runtimePreparing = true
		wait(0)

		verify(window.providerSurfaceRequested)
		verify(!window.providerSurfaceAllowed)
		verify(!window.webSurfaceActive)
		compare(window.rendererState, "loading")

		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeError = "The packaged media runtime is unavailable."
		wait(0)
		verify(window.providerSurfaceRequested)
		verify(!window.providerSurfaceAllowed)
		verify(!window.webSurfaceActive)
		compare(window.rendererState, "error")
		session.active = false
	}

	function test_runtime_error_surface_remains_actionable_across_retry() {
		const window = windowLoader.item
		session.url = "https://www.youtube.com/embed/runtime-retry"
		session.active = true
		mediaRuntime.runtimeError = "The packaged media runtime is unavailable."
		wait(0)

		compare(window.rendererState, "error")
		const failure = findChild(window.contentItem, "mediaSessionFailureSurface")
		const detail = findChild(window.contentItem, "mediaSessionFailureDetail")
		const retryButton = findChild(window.contentItem, "mediaSessionRetryButton")
		verify(failure !== null && failure.visible)
		compare(detail.text, mediaRuntime.runtimeError)
		verify(retryButton !== null && retryButton.enabled)

		retryButton.clicked()
		compare(mediaRuntime.retryCalls, 1)
		compare(session.retryCalls, 1)
		compare(window.rendererState, "loading")
		verify(!failure.visible)
		verify(findChild(window.contentItem, "mediaSessionLoadingSurface").visible)

		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeError = "The packaged media runtime is still unavailable."
		wait(0)
		compare(window.rendererState, "error")
		verify(failure.visible)
		compare(detail.text, mediaRuntime.runtimeError)
		verify(retryButton.enabled)
		session.active = false
	}

	function test_redirect_callbacks_require_current_surface_and_allowlist() {
		const window = windowLoader.item
		session.active = true
		session.url = "https://www.youtube-nocookie.com/embed/original"
		const redirected = "https://www.youtube.com/embed/original?canonical=1"
		const firstGeneration = window.beginMediaDocumentLoad(session.url)

		verify(window.requestUrlMatches(redirected, session.url,
			redirected, firstGeneration, firstGeneration))
		verify(window.requestUrlMatches("https://www.youtube.com/embed/normalized",
			"https://www.youtube.com/embed/source#fragment",
			"https://www.youtube.com/embed/normalized",
			firstGeneration, firstGeneration))
		mediaRuntime.navigationAllowed = false
		verify(!window.requestUrlMatches("https://evil.example/embed/original",
			session.url, "https://evil.example/embed/original",
			firstGeneration, firstGeneration))
		mediaRuntime.navigationAllowed = true
		verify(!window.requestUrlMatches(redirected, session.url,
			"https://www.youtube.com/embed/new-session",
			firstGeneration, firstGeneration))

		const secondGeneration = window.beginMediaDocumentLoad(
			"https://www.youtube.com/embed/new-session")
		verify(secondGeneration > firstGeneration)
		verify(!window.requestUrlMatches(redirected, session.url,
			redirected, firstGeneration, firstGeneration))
		verify(!window.completeMediaDocumentLoad(firstGeneration))
		session.active = false
	}

	function test_detached_polling_is_single_flight_and_generation_bound() {
		const window = windowLoader.item
		session.url = ""
		session.active = true

		const firstGeneration = window.beginMediaDocumentLoad("https://example.com/first")
		verify(window.markMediaDocumentReady(firstGeneration))
		verify(window.claimStatePoll(firstGeneration))
		verify(window.statePollInFlight)
		verify(!window.claimStatePoll(firstGeneration))

		const secondGeneration = window.beginMediaDocumentLoad("https://example.com/second")
		verify(secondGeneration > firstGeneration)
		verify(window.markMediaDocumentReady(secondGeneration))
		verify(window.claimStatePoll(secondGeneration))
		verify(!window.completeStatePoll(firstGeneration,
			{ "position": 11, "duration": 90, "paused": false }))
		compare(session.playbackReports, 0)
		verify(window.statePollInFlight)

		verify(window.completeStatePoll(secondGeneration,
			{ "position": 27, "duration": 90, "paused": true }))
		compare(session.playbackReports, 1)
		compare(session.lastReportedPosition, 27)
		compare(session.lastReportedPaused, true)
		verify(!window.statePollInFlight)
		session.active = false
	}

	function test_detached_state_poll_watchdog_releases_only_its_attempt() {
		const window = windowLoader.item
		session.url = ""
		session.active = true
		const generation = window.beginMediaDocumentLoad("about:blank")
		verify(window.markMediaDocumentReady(generation))
		window.statePollTimeoutMs = 25

		verify(window.claimStatePoll(generation))
		const expiredToken = window.statePollToken
		tryVerify(function() { return !window.statePollInFlight }, 1000)
		compare(window._missingStatePolls, 1)

		verify(window.claimStatePoll(generation))
		const currentToken = window.statePollToken
		verify(currentToken > expiredToken)
		verify(!window.expireStatePoll(generation, expiredToken))
		verify(window.statePollInFlight)
		verify(!window.completeStatePoll(generation,
			{ "position": 12, "duration": 45, "paused": true }, expiredToken))
		verify(window.statePollInFlight)
		verify(window.cancelStatePoll(generation, currentToken))
		session.active = false
	}

	function test_detached_audio_poll_watchdog_rejects_stale_attempts() {
		const window = windowLoader.item
		session.url = ""
		session.active = true
		const generation = window.beginMediaDocumentLoad("about:blank")
		window.statePollTimeoutMs = 25

		verify(window.claimAudioStatePoll(generation))
		const expiredToken = window.secondaryAudioStatePollToken
		tryVerify(function() { return !window.secondaryAudioStatePollInFlight }, 1000)
		compare(window._audioMissingStatePolls, 1)

		verify(window.claimAudioStatePoll(generation))
		const currentToken = window.secondaryAudioStatePollToken
		verify(currentToken > expiredToken)
		verify(!window.expireAudioStatePoll(generation, expiredToken))
		verify(!window.cancelAudioStatePoll(generation, expiredToken))
		verify(window.secondaryAudioStatePollInFlight)
		verify(window.cancelAudioStatePoll(generation, currentToken))
		session.active = false
	}

	function test_source_retry_and_close_invalidate_pending_callbacks() {
		const window = windowLoader.item

		let generation = window.beginMediaDocumentLoad("https://example.com/source")
		verify(window.markMediaDocumentReady(generation))
		verify(window.claimStatePoll(generation))
		session.sourceChanged()
		verify(window.mediaGeneration > generation)
		verify(!window.completeStatePoll(generation,
			{ "position": 1, "duration": 2, "paused": false }))

		generation = window.beginMediaDocumentLoad("https://example.com/retry")
		verify(window.markMediaDocumentReady(generation))
		verify(window.claimStatePoll(generation))
		session.retryRequested()
		verify(window.mediaGeneration > generation)
		verify(!window.completeStatePoll(generation,
			{ "position": 1, "duration": 2, "paused": false }))

		generation = window.beginMediaDocumentLoad("https://example.com/close")
		verify(window.markMediaDocumentReady(generation))
		verify(window.claimStatePoll(generation))
		session.active = false
		session.state = "idle"
		verify(window.mediaGeneration > generation)
		verify(!window.completeStatePoll(generation,
			{ "position": 1, "duration": 2, "paused": false }))
		compare(session.playbackReports, 0)
	}

	function test_verified_document_completion_is_generation_bound() {
		const window = windowLoader.item
		// Exercise the lifecycle helper without instantiating a real WebEngine
		// surface in the component-test process.
		session.url = ""
		session.active = true
		const firstGeneration = window.beginMediaDocumentLoad("about:blank")
		verify(window.completeMediaDocumentLoad(firstGeneration))
		verify(window.documentReady)
		compare(session.loadReports, 1)
		compare(session.lastLoadProgress, 100)

		const secondGeneration = window.beginMediaDocumentLoad("about:blank#second")
		verify(!window.completeMediaDocumentLoad(firstGeneration))
		verify(!window.documentReady)
		compare(session.loadReports, 1)
		verify(window.completeMediaDocumentLoad(secondGeneration))
		verify(window.documentReady)
		compare(session.loadReports, 2)
		session.active = false
	}

	function test_detached_surface_uses_the_same_provider_verification_contract() {
		const window = windowLoader.item
		session.url = ""
		session.active = true
		session.provider = "youtube"
		let generation = window.beginMediaDocumentLoad("about:blank#challenge")
		verify(window.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": false,
			"blockedKind": "verification",
			"detail": "Sign in to confirm you are not a bot"
		}, false))
		compare(window.surfaceVerificationState, "blocked")
		verify(!window.surfaceVerified)
		verify(!window.transportVerified)
		compare(session.errorReports, 1)

		session.provider = "soundcloud"
		generation = window.beginMediaDocumentLoad("about:blank#soundcloud")
		verify(window.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": true,
			"providerUi": true,
			"playbackEvidence": false
		}, false))
		verify(window.surfaceVerified)
		verify(window.transportVerified)
		verify(!window.playbackVerified)
		compare(window.surfaceVerificationState, "verified")

		verify(window.applyMediaSurfaceProbeResult(generation, {
			"readyState": "complete",
			"transport": true,
			"providerUi": true,
			"mediaPresent": true,
			"mediaReady": true,
			"playbackEvidence": true
		}, true))
		verify(window.playbackVerified)
		verify(window.surfaceVerificationEvidence.indexOf("playback") >= 0)
		session.active = false
	}

	function test_detached_direct_media_keeps_known_origin_identity() {
		const window = windowLoader.item
		session.provider = "direct"
		session.url = "https://v.redd.it/abc/DASH_720.mp4"
		session.mediaMime = "video/mp4"
		session.active = true
		window.visualFixtureMode = "active"
		wait(0)

		compare(window.presentationProvider, "reddit")
		compare(window.providerLabel, "Reddit")
		compare(window.providerMark, "R")
		compare(findChild(window.contentItem, "mediaSessionProviderLabel").text, "Reddit")

		window.visualFixtureMode = ""
		session.active = false
	}

	function test_provider_surface_stays_lazy_until_an_explicit_session_is_active() {
		const window = windowLoader.item
		compare(window.surfaceId, "mediaSession.window")
		verify(window.captureRect.width > 0)
		verify(!window.webSurfaceActive)

		session.url = ""
		session.active = true
		wait(0)
		compare(window.rendererState, "empty")
		verify(!window.webSurfaceActive)
		const empty = findChild(window.contentItem, "mediaSessionEmptySurface")
		verify(empty !== null)
		verify(empty.visible)

		session.error = "The renderer stopped."
		session.state = "error"
		session.url = "https://www.youtube.com/embed/test"
		wait(0)
		compare(window.rendererState, "error")
		verify(!window.webSurfaceActive)
		verify(!window.secondaryAudioActive)
		const failure = findChild(window.contentItem, "mediaSessionFailureSurface")
		verify(failure !== null)
		verify(failure.visible)
		verify(findChild(window.contentItem, "mediaSessionRetryButton") !== null)
		verify(findChild(window.contentItem, "mediaSessionFailureExternalButton").visible)
		verify(findChild(window.contentItem, "mediaSessionFailureCloseButton").visible)
		const controls = findChild(window.contentItem, "mediaSessionWindowControls")
		verify(controls !== null)
		verify(!controls.visible)
		compare(controls.height, 0)
	}

	function test_dark_media_surfaces_use_overlay_contrast_tokens() {
		const content = windowLoader.item.contentItem
		const strongLabels = [
			findChild(content, "mediaSessionFixtureHeading"),
			findChild(content, "mediaSessionLoadingHeading"),
			findChild(content, "mediaSessionEmptyHeading"),
			findChild(content, "mediaSessionFailureHeading")
		]
		const mutedLabels = [
			findChild(content, "mediaSessionFixtureDetail"),
			findChild(content, "mediaSessionLoadingDetail"),
			findChild(content, "mediaSessionEmptyDetail"),
			findChild(content, "mediaSessionFailureDetail")
		]
		for (const label of strongLabels) {
			verify(label !== null)
			compare(String(label.color), String(Theme.mediaOverlayTextStrong))
		}
		for (const label of mutedLabels) {
			verify(label !== null)
			compare(String(label.color), String(Theme.mediaOverlayTextMuted))
		}

		const progressTrack = findChild(content, "mediaSessionLoadingProgressTrack")
		verify(progressTrack !== null)
		compare(String(progressTrack.color),
			String(Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.20)))
	}

	function test_provider_presentation_styles_detached_loading_error_and_compact_states() {
		const window = windowLoader.item
		const presentation = ProviderPresentation.resolve("youtube")
		session.provider = "youtube"
		session.active = true
		window.visualFixtureMode = "active"
		wait(0)

		compare(window.providerLabel, presentation.label)
		compare(window.providerMark, presentation.mark)
		compare(String(window.providerAccent), String(presentation.accent))
		compare(String(window.providerOnAccent), String(Theme.contrastText(window.providerAccent)))
		compare(window.title, "YouTube player")
		const canvas = findChild(window.contentItem, "mediaSessionCanvas")
		verify(canvas !== null)
		compare(canvas.Accessible.name, "YouTube detached media player")

		const badge = findChild(window.contentItem, "mediaSessionProviderBadge")
		const badgeMark = findChild(window.contentItem, "mediaSessionProviderMark")
		const badgeLabel = findChild(window.contentItem, "mediaSessionProviderLabel")
		const stateLabel = findChild(window.contentItem, "mediaSessionSurfaceStateLabel")
		verify(badge !== null && badge.visible)
		compare(String(badge.color), String(window.providerAccent))
		compare(badge.Accessible.name, "YouTube media player, detached window")
		compare(badgeMark.text, "YT")
		compare(badgeLabel.text, "YouTube")
		compare(stateLabel.text, "· DETACHED")
		compare(String(badgeMark.color), String(window.providerOnAccent))
		compare(String(badgeLabel.color), String(window.providerOnAccent))

		session.sharedAvailable = true
		session.sharedJoined = true
		session.sharedHost = false
		wait(0)
		compare(stateLabel.text, "· SYNCED")
		compare(badge.Accessible.name, "YouTube media player, synchronized with host")
		session.sharedHost = true
		wait(0)
		compare(stateLabel.text, "· HOSTING")
		compare(badge.Accessible.name, "YouTube media player, hosting watch together")

		window.width = 680
		wait(0)
		verify(window.compactProviderChrome)
		verify(!badgeLabel.visible)
		verify(badgeMark.visible)

		session.sharedAvailable = false
		session.sharedJoined = false
		session.sharedHost = false
		window.visualFixtureMode = "loading"
		wait(0)
		const loadingBadge = findChild(window.contentItem, "mediaSessionLoadingProviderBadge")
		const loadingSurface = findChild(window.contentItem, "mediaSessionLoadingSurface")
		const loadingMark = findChild(window.contentItem, "mediaSessionLoadingProviderMark")
		const loadingHeading = findChild(window.contentItem, "mediaSessionLoadingHeading")
		verify(loadingSurface !== null && loadingBadge !== null && loadingBadge.visible)
		compare(loadingSurface.Accessible.name, "Loading YouTube media")
		compare(String(loadingBadge.color), String(window.providerAccent))
		compare(loadingBadge.Accessible.name, "YouTube")
		compare(loadingMark.text, "YT")
		compare(loadingHeading.text, "Loading YouTube")

		window.visualFixtureMode = "error"
		session.error = "Renderer stopped"
		session.state = "error"
		wait(0)
		const failureBadge = findChild(window.contentItem, "mediaSessionFailureProviderBadge")
		const failureSurface = findChild(window.contentItem, "mediaSessionFailureSurface")
		const failureMark = findChild(window.contentItem, "mediaSessionFailureProviderMark")
		const failureHeading = findChild(window.contentItem, "mediaSessionFailureHeading")
		const failureRetry = findChild(window.contentItem, "mediaSessionRetryButton")
		const failureExternal = findChild(window.contentItem, "mediaSessionFailureExternalButton")
		const failureClose = findChild(window.contentItem, "mediaSessionFailureCloseButton")
		verify(failureSurface !== null && failureBadge !== null && failureBadge.visible)
		compare(failureSurface.Accessible.name, "YouTube playback failed")
		compare(String(failureBadge.color), String(window.providerAccent))
		compare(failureMark.text, "YT")
		compare(failureHeading.text, "YouTube playback failed")
		verify(failureRetry !== null && !failureRetry.hoverEnabled)
		verify(failureExternal !== null && !failureExternal.hoverEnabled)
		verify(failureClose !== null && !failureClose.hoverEnabled)

		session.provider = "Acme Video"
		wait(0)
		compare(window.providerLabel, "Acme Video")
		compare(window.providerMark, "AV")
		compare(String(window.providerAccent), String(Theme.accent))
	}

	function test_visual_error_fixture_never_instantiates_the_provider_surface() {
		const window = windowLoader.item
		window.visualFixtureMode = "error"
		session.state = "loading"
		session.active = true
		session.error = "The deterministic renderer stopped."
		wait(0)

		compare(window.rendererState, "error")
		verify(!window.webSurfaceActive)
		verify(!window.secondaryAudioActive)
		const failure = findChild(window.contentItem, "mediaSessionFailureSurface")
		verify(failure !== null)
		verify(failure.visible)
		verify(findChild(window.contentItem, "mediaSessionFailureCloseButton").visible)
		verify(!findChild(window.contentItem, "mediaSessionWindowControls").visible)
		session.active = false
	}

	function test_visual_loading_fixture_freezes_only_the_busy_indicator_animation() {
		const window = windowLoader.item
		window.visualFixtureMode = "loading"
		session.state = "loading"
		session.active = true
		wait(0)

		compare(window.rendererState, "loading")
		verify(!window.webSurfaceActive)
		const busy = findChild(window.contentItem, "mediaSessionBusyIndicator")
		verify(busy !== null)
		verify(busy.visible)
		compare(busy.animated, false)

		session.active = false
		window.visualFixtureMode = ""
		compare(busy.animated, true)
	}

	function test_provider_and_url_inference_covers_every_supported_shape() {
		const window = windowLoader.item
		const cases = [
			[ "youtube", "https://www.youtube.com/embed/test", "wide" ],
			[ "twitch", "https://player.twitch.tv/?video=1", "twitch" ],
			[ "tiktok", "https://www.tiktok.com/player/v1/1", "short" ],
			[ "instagram", "https://www.instagram.com/reel/example/embed/", "short" ],
			[ "instagram", "https://www.instagram.com/p/example/embed/", "square" ],
			[ "spotify", "https://open.spotify.com/embed/track/example", "compact-audio" ],
			[ "spotify", "https://open.spotify.com/embed/episode/example", "compact-audio" ],
			[ "spotify", "https://open.spotify.com/embed/playlist/example", "audio" ],
			[ "soundcloud", "https://w.soundcloud.com/player/?url=test", "compact-audio" ]
		]
		for (let i = 0; i < cases.length; ++i) {
			session.provider = cases[i][0]
			session.url = cases[i][1]
			compare(window.inferMediaAspect(), cases[i][2], cases[i][0] + " aspect")
		}
	}

	function test_initial_size_is_aspect_aware_without_binding_future_resizes() {
		const window = windowLoader.item
		session.provider = "tiktok"
		session.url = "https://www.tiktok.com/player/v1/1"
		wait(0)
		window.applyInitialWindowSize()
		compare(window.width, 640)
		compare(window.height, 820)

		window.width = 713
		window.height = 677
		session.provider = "spotify"
		session.url = "https://open.spotify.com/embed/track/example"
		wait(0)
		compare(window.width, 713)
		compare(window.height, 677)
	}
}
