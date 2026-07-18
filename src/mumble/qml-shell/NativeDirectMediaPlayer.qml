pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Mumble.Theme 1.0

Item {
	id: root

	required property var session
	property bool playbackInputEnabled: true
	function preparedSource(propertyName, fallbackName) {
		if (!session)
			return ""
		const prepared = session[propertyName]
		return String(prepared === undefined ? session[fallbackName] || "" : prepared || "")
	}
	property string sourceUrl: preparedSource("playbackUrl", "url")
	property string secondaryAudioUrl: preparedSource("playbackAudioUrl", "audioUrl")
	readonly property string preparedSecondaryAudioWarning: String(session
		&& session.playbackAudioWarning !== undefined ? session.playbackAudioWarning || "" : "")
	readonly property bool sourcePreparing: Boolean(session
		&& session.playbackSourcePreparing !== undefined
		&& session.playbackSourcePreparing)
	readonly property string rendererBackend: "native"
	readonly property bool documentReady: _mainReady
	readonly property bool rendererHealthy: !_mainFailed
	readonly property string rendererState: sourceUrl.length === 0 ? (sourcePreparing ? "loading" : "empty")
		: _mainFailed ? "error" : _mainReady ? "active" : "loading"
	readonly property bool secondaryAudioActive: secondaryAudioUrl.length > 0
		&& !_secondaryFailed
	readonly property bool secondaryAudioDegraded: secondaryAudioWarning.length > 0
	readonly property string secondaryAudioState: secondaryAudioDegraded ? "degraded"
		: secondaryAudioUrl.length === 0 ? "idle"
		: _secondaryReady ? "active" : "loading"
	property string secondaryAudioWarning: ""
	readonly property int mediaGeneration: _generation
	readonly property int secondaryAudioGeneration: _secondaryGeneration
	readonly property bool primaryAudioMuted: primaryAudio.muted
	readonly property bool secondaryLoadWatchdogActive: secondaryLoadWatchdog.running
	readonly property var primaryPlayer: primaryPlayerLoader.item
	readonly property var secondaryPlayer: secondaryPlayerLoader.item
	property bool _enabled: true
	property bool _componentReady: false
	property bool _mainAttemptActive: false
	property bool _secondaryAttemptActive: false
	property bool _mainReady: false
	property bool _mainFailed: false
	property bool _secondaryReady: false
	property bool _secondaryFailed: false
	property string _desiredState: "paused"
	property int _generation: 0
	property int _secondaryGeneration: 0
	property int _reportedErrorGeneration: -1
	property int _reportedSecondaryErrorGeneration: -1
	property double _lastReportedPosition: -1
	property double _lastReportedDuration: -1
	property bool _lastReportedPaused: true
	property int syncToleranceMs: 150
	property int secondaryLoadTimeoutMs: 8000
	clip: true

	function boundedVolume() {
		return Math.max(0, Math.min(100, Number(session ? session.volume : 100)))
	}

	function sessionMuted() {
		return Boolean(session && session.muted)
	}

	function reportLoadProgress(progress) {
		if (session && typeof session.reportLoadProgress === "function")
			session.reportLoadProgress(Math.max(0, Math.min(100, Math.round(Number(progress) || 0))))
	}

	function reportMainError(message, expectedGeneration) {
		const generation = expectedGeneration === undefined ? _generation : expectedGeneration
		if (generation !== _generation)
			return false
		if (_reportedErrorGeneration === _generation)
			return false
		_reportedErrorGeneration = _generation
		_mainFailed = true
		_mainReady = false
		const detail = String(message || "").trim()
		const userText = detail.length > 0
			? qsTr("The native media player could not play this source: %1").arg(detail)
			: qsTr("The native media player could not play this source.")
		if (!session)
			return
		if (typeof session.reportTypedError === "function")
			session.reportTypedError("native-renderer-failed", userText)
		else if (typeof session.reportError === "function")
			session.reportError(userText)
		return true
	}

	function reportSecondaryError(message, expectedGeneration) {
		const generation = expectedGeneration === undefined
			? _secondaryGeneration : expectedGeneration
		if (generation !== _secondaryGeneration)
			return false
		if (_reportedSecondaryErrorGeneration === _secondaryGeneration)
			return false
		_reportedSecondaryErrorGeneration = _secondaryGeneration
		secondaryLoadWatchdog.stop()
		_secondaryFailed = true
		_secondaryReady = false
		const detail = String(message || "").trim()
		secondaryAudioWarning = detail.length > 0
			? qsTr("The separate audio track could not play: %1").arg(detail)
			: qsTr("The separate audio track could not be played.")
		setMuted(sessionMuted())
		return true
	}

	function applyPreparedSecondaryAudioWarning() {
		const warning = preparedSecondaryAudioWarning.trim()
		if (warning.length === 0 || _secondaryReady)
			return false
		secondaryLoadWatchdog.stop()
		_secondaryFailed = true
		_secondaryReady = false
		secondaryAudioWarning = warning
		setMuted(sessionMuted())
		return true
	}

	function acceptMainReady(expectedGeneration, progress) {
		if (expectedGeneration !== _generation || _mainFailed)
			return false
		const wasReady = _mainReady
		_mainReady = true
		_mainFailed = false
		reportLoadProgress(progress === undefined ? 100 : progress)
		if (!wasReady)
			Qt.callLater(function() {
				if (root._generation === expectedGeneration)
					root.applySessionState()
			})
		return true
	}

	function acceptSecondaryReady(expectedGeneration) {
		if (expectedGeneration !== _secondaryGeneration || _secondaryFailed)
			return false
		secondaryLoadWatchdog.stop()
		_secondaryReady = true
		secondaryAudioWarning = ""
		setMuted(sessionMuted())
		synchronizeSecondaryAudio()
		return true
	}

	function beginSecondaryPending(expectedGeneration) {
		if (expectedGeneration !== _secondaryGeneration || _secondaryFailed
				|| secondaryAudioUrl.length === 0)
			return false
		_secondaryReady = false
		setMuted(sessionMuted())
		secondaryLoadWatchdog.watchedGeneration = expectedGeneration
		secondaryLoadWatchdog.restart()
		return true
	}

	function expireSecondaryAudioLoad(expectedGeneration) {
		if (expectedGeneration !== _secondaryGeneration || _secondaryReady
				|| _secondaryFailed || secondaryAudioUrl.length === 0)
			return false
		return reportSecondaryError(
			qsTr("Loading the separate audio track timed out."), expectedGeneration)
	}

	function reportPlaybackState(force) {
		if (!session || !_mainReady || typeof session.reportPlaybackState !== "function")
			return
		const player = primaryPlayer
		if (!player)
			return
		const positionSeconds = Math.max(0, Number(player.position || 0) / 1000)
		const durationSeconds = Math.max(0, Number(player.duration || 0) / 1000)
		const paused = player.playbackState !== MediaPlayer.PlayingState
		if (!force
				&& Math.abs(positionSeconds - _lastReportedPosition) < 0.12
				&& Math.abs(durationSeconds - _lastReportedDuration) < 0.01
				&& paused === _lastReportedPaused)
			return
		_lastReportedPosition = positionSeconds
		_lastReportedDuration = durationSeconds
		_lastReportedPaused = paused
		session.reportPlaybackState(positionSeconds, durationSeconds, paused)
	}

	function resetLifecycle() {
		_generation += 1
		_reportedErrorGeneration = -1
		_mainReady = false
		_mainFailed = false
		_lastReportedPosition = -1
		_lastReportedDuration = -1
		_lastReportedPaused = true
		_desiredState = String(session ? session.state || "paused" : "paused") === "playing"
			? "playing" : "paused"
	}

	function restartMainAttempt() {
		resetLifecycle()
		const generation = _generation
		_mainAttemptActive = false
		Qt.callLater(function() {
			if (root._componentReady && root._enabled && root._generation === generation
					&& root.sourceUrl.length > 0)
				root._mainAttemptActive = true
		})
		return generation
	}

	function resetSecondaryLifecycle() {
		secondaryLoadWatchdog.stop()
		_secondaryGeneration += 1
		_reportedSecondaryErrorGeneration = -1
		_secondaryReady = false
		_secondaryFailed = false
		secondaryAudioWarning = ""
		setMuted(sessionMuted())
	}

	function restartSecondaryAttempt() {
		resetSecondaryLifecycle()
		const generation = _secondaryGeneration
		_secondaryAttemptActive = false
		Qt.callLater(function() {
			if (root._componentReady && root._enabled
					&& root._secondaryGeneration === generation
					&& root.secondaryAudioUrl.length > 0)
				root._secondaryAttemptActive = true
		})
		return generation
	}

	function applySessionState() {
		setVolume(boundedVolume())
		setMuted(sessionMuted())
		if (_desiredState === "playing")
			play()
		else
			pause()
	}

	function play() {
		_desiredState = "playing"
		if (!_enabled || !_mainReady)
			return false
		const player = primaryPlayer
		if (!player)
			return false
		player.play()
		if (_secondaryReady && secondaryPlayer)
			secondaryPlayer.play()
		return true
	}

	function pause() {
		_desiredState = "paused"
		if (!_enabled)
			return false
		if (primaryPlayer)
			primaryPlayer.pause()
		if (secondaryPlayer)
			secondaryPlayer.pause()
		reportPlaybackState(true)
		return true
	}

	function seek(seconds) {
		const player = primaryPlayer
		if (!_enabled || !_mainReady || !player || !player.seekable)
			return false
		const target = Math.max(0, Number(seconds || 0) * 1000)
		player.position = target
		if (_secondaryReady && secondaryPlayer && secondaryPlayer.seekable)
			secondaryPlayer.position = target
		reportPlaybackState(true)
		return true
	}

	function setVolume(volume) {
		const normalized = Math.max(0, Math.min(100, Number(volume || 0))) / 100
		primaryAudio.volume = normalized
		secondaryAudio.volume = normalized
		return true
	}

	function setMuted(muted) {
		const value = Boolean(muted)
		// Keep the primary track audible while the optional separate track loads.
		// Switch atomically only after that track is actually ready; timeout/error
		// therefore degrades without an audible multi-second gap.
		primaryAudio.muted = value || _secondaryReady
		secondaryAudio.muted = value
		return true
	}

	function synchronizeSecondaryAudio() {
		const mainPlayer = primaryPlayer
		const audioPlayer = secondaryPlayer
		if (!_secondaryReady || !_mainReady || !mainPlayer || !audioPlayer)
			return
		const target = Number(mainPlayer.position || 0)
		if (audioPlayer.seekable
				&& Math.abs(Number(audioPlayer.position || 0) - target) > syncToleranceMs)
			audioPlayer.position = target
		if (mainPlayer.playbackState === MediaPlayer.PlayingState) {
			if (audioPlayer.playbackState !== MediaPlayer.PlayingState)
				audioPlayer.play()
		} else if (audioPlayer.playbackState === MediaPlayer.PlayingState) {
			audioPlayer.pause()
		}
	}

	function retry() {
		_enabled = false
		_mainAttemptActive = false
		_secondaryAttemptActive = false
		secondaryLoadWatchdog.stop()
		Qt.callLater(function() {
			root._enabled = true
			root.restartMainAttempt()
			root.restartSecondaryAttempt()
		})
		return true
	}

	function shutdown() {
		_enabled = false
		if (primaryPlayer)
			primaryPlayer.stop()
		if (secondaryPlayer)
			secondaryPlayer.stop()
		_mainAttemptActive = false
		_secondaryAttemptActive = false
		secondaryLoadWatchdog.stop()
		_mainReady = false
		_secondaryReady = false
	}

	onSourceUrlChanged: {
		if (_componentReady) {
			restartMainAttempt()
			restartSecondaryAttempt()
		}
	}
	onSecondaryAudioUrlChanged: {
		if (_componentReady) {
			restartSecondaryAttempt()
			Qt.callLater(applyPreparedSecondaryAudioWarning)
		}
	}
	onPreparedSecondaryAudioWarningChanged: Qt.callLater(applyPreparedSecondaryAudioWarning)
	Component.onCompleted: {
		_componentReady = true
		restartMainAttempt()
		restartSecondaryAttempt()
		Qt.callLater(applyPreparedSecondaryAudioWarning)
	}
	Component.onDestruction: shutdown()

	AudioOutput {
		id: primaryAudio
		volume: root.boundedVolume() / 100
		muted: root.sessionMuted()
	}

	AudioOutput {
		id: secondaryAudio
		volume: root.boundedVolume() / 100
		muted: root.sessionMuted()
	}

	Loader {
		id: primaryPlayerLoader
		active: root._enabled && root._mainAttemptActive && root.sourceUrl.length > 0
		sourceComponent: MediaPlayer {
			property int generation: -1
			property string attemptSource: ""
			audioOutput: primaryAudio
			videoOutput: videoOutput
			Component.onCompleted: {
				generation = root._generation
				attemptSource = root.sourceUrl
				source = attemptSource
			}
			onMediaStatusChanged: {
				if (generation !== root._generation)
					return
				if (mediaStatus === MediaPlayer.LoadingMedia) {
					root._mainReady = false
					root.reportLoadProgress(Math.round(bufferProgress * 100))
				} else if (mediaStatus === MediaPlayer.LoadedMedia
						|| mediaStatus === MediaPlayer.BufferingMedia
						|| mediaStatus === MediaPlayer.BufferedMedia
						|| mediaStatus === MediaPlayer.StalledMedia
						|| mediaStatus === MediaPlayer.EndOfMedia) {
					root.acceptMainReady(generation, mediaStatus === MediaPlayer.BufferingMedia
						? Math.max(1, Math.round(bufferProgress * 100)) : 100)
				} else if (mediaStatus === MediaPlayer.InvalidMedia) {
					root.reportMainError(errorString, generation)
				}
			}
			onBufferProgressChanged: {
				if (generation === root._generation && !root._mainReady)
					root.reportLoadProgress(Math.round(bufferProgress * 100))
			}
			onErrorOccurred: function(error, errorString) {
				if (error !== MediaPlayer.NoError)
					root.reportMainError(errorString, generation)
			}
			onPlaybackStateChanged: {
				if (generation === root._generation)
					root.reportPlaybackState(true)
			}
			onDurationChanged: {
				if (generation === root._generation)
					root.reportPlaybackState(true)
			}
		}
	}

	Loader {
		id: secondaryPlayerLoader
		active: root._enabled && root._secondaryAttemptActive
			&& root.secondaryAudioUrl.length > 0
		onLoaded: {
			secondaryLoadWatchdog.watchedGeneration = root._secondaryGeneration
			secondaryLoadWatchdog.restart()
		}
		sourceComponent: MediaPlayer {
			property int generation: -1
			property string attemptSource: ""
			audioOutput: secondaryAudio
			Component.onCompleted: {
				generation = root._secondaryGeneration
				attemptSource = root.secondaryAudioUrl
				source = attemptSource
			}
			onMediaStatusChanged: {
				if (generation !== root._secondaryGeneration || root._secondaryFailed)
					return
				if (mediaStatus === MediaPlayer.LoadingMedia
						|| mediaStatus === MediaPlayer.StalledMedia) {
					root.beginSecondaryPending(generation)
				} else if (mediaStatus === MediaPlayer.LoadedMedia
						|| mediaStatus === MediaPlayer.BufferingMedia
						|| mediaStatus === MediaPlayer.BufferedMedia
						|| mediaStatus === MediaPlayer.EndOfMedia) {
					root.acceptSecondaryReady(generation)
				} else if (mediaStatus === MediaPlayer.InvalidMedia) {
					root.reportSecondaryError(errorString, generation)
				}
			}
			onErrorOccurred: function(error, errorString) {
				if (error !== MediaPlayer.NoError)
					root.reportSecondaryError(errorString, generation)
			}
		}
	}

	VideoOutput {
		id: videoOutput
		objectName: "nativeDirectMediaVideoOutput"
		anchors.fill: parent
		fillMode: VideoOutput.PreserveAspectFit
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("Direct media playback")
		Accessible.description: root.playbackInputEnabled
			? qsTr("Native media playback surface")
			: qsTr("Playback is controlled by the session host")
	}

	Rectangle {
		anchors.fill: parent
		visible: root.documentReady && root.primaryPlayer && !root.primaryPlayer.hasVideo
		color: Theme.mediaCanvas
		gradient: Gradient {
			GradientStop { position: 0.0; color: Theme.withAlpha(Theme.accent, 0.22) }
			GradientStop { position: 0.58; color: Theme.withAlpha(Theme.panel, 0.96) }
			GradientStop { position: 1.0; color: Theme.mediaCanvas }
		}
		Accessible.ignored: true

		ColumnLayout {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6 * 2, 420)
			spacing: Theme.space2

			Rectangle {
				Layout.alignment: Qt.AlignHCenter
				Layout.preferredWidth: 64
				Layout.preferredHeight: 64
				radius: width / 2
				color: Theme.accentSubtle
				border.color: Theme.withAlpha(Theme.accent, 0.55)
				ModernIcon {
					anchors.centerIn: parent
					name: "volume"
					size: 26
					color: Theme.accent
				}
			}
			Label {
				Layout.fillWidth: true
				text: qsTr("Audio playback")
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontHeading
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				Layout.fillWidth: true
				text: qsTr("Playing with the native media engine")
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextMuted
				font.pixelSize: Theme.fontLabel
				horizontalAlignment: Text.AlignHCenter
			}
		}
	}

	Timer {
		id: secondaryLoadWatchdog
		property int watchedGeneration: -1
		interval: Math.max(1, root.secondaryLoadTimeoutMs)
		repeat: false
		onTriggered: root.expireSecondaryAudioLoad(watchedGeneration)
	}

	Timer {
		interval: 250
		repeat: true
		running: root._enabled && root._mainReady
		onTriggered: {
			root.reportPlaybackState(false)
			root.synchronizeSecondaryAudio()
		}
	}
}
