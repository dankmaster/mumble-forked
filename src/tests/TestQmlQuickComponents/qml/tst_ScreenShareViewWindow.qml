import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ScreenShareViewWindow"
	when: windowShown
	visible: true
	width: 320
	height: 240

	QtObject {
		id: shareBackend
		property string ownerLabel: "Alex"
		property string roomLabel: "Lobby"
		property string title: "Live screen share"
		property string detail: "Session 102"
		property string status: "The live viewer is ready."
		property string operationStatus: "ready"
		property string operationError: ""
		property bool paused: false
		property bool audioAvailable: true
		property bool audioMuted: false
		property int audioVolume: 72
		property string audioControlStatus: "ready"
		property string audioControlError: ""
		property bool nativeFrameActive: false
		property bool hasCurrentFrame: false
		property var videoWindow: null
		property int pauseCalls: 0
		property int muteCalls: 0
		property int volumeCalls: 0
		property int stopCalls: 0
		property int retryCalls: 0
		property int closeCalls: 0

		function setPaused(value) { pauseCalls += 1; paused = value }
		function setAudioMuted(value) { muteCalls += 1; audioMuted = value }
		function setAudioVolume(value) { volumeCalls += 1; audioVolume = Math.round(value) }
		function requestStop() { stopCalls += 1 }
		function requestRetry() { retryCalls += 1 }
		function requestClose() { closeCalls += 1 }
	}

	function init() {
		shareBackend.ownerLabel = "Alex"
		shareBackend.roomLabel = "Lobby"
		shareBackend.title = "Live screen share"
		shareBackend.detail = "Session 102"
		shareBackend.status = "The live viewer is ready."
		shareBackend.operationStatus = "ready"
		shareBackend.operationError = ""
		shareBackend.paused = false
		shareBackend.audioAvailable = true
		shareBackend.audioMuted = false
		shareBackend.audioVolume = 72
		shareBackend.audioControlStatus = "ready"
		shareBackend.audioControlError = ""
		shareBackend.nativeFrameActive = false
		shareBackend.hasCurrentFrame = false
		shareBackend.videoWindow = null
		shareBackend.pauseCalls = 0
		shareBackend.muteCalls = 0
		shareBackend.volumeCalls = 0
		shareBackend.stopCalls = 0
		shareBackend.retryCalls = 0
		shareBackend.closeCalls = 0
	}

	function test_window_and_header_keep_sharer_and_room_identity() {
		const viewer = createViewer(1120, 720)
		const metadata = findChild(viewer.contentItem, "screenShareViewerMetadata")
		verify(metadata !== null)
		compare(viewer.sharerLabel, "Alex")
		compare(viewer.roomLabel, "Lobby")
		compare(viewer.identityDetail, "Shared by Alex · in Lobby")
		verify(viewer.title.indexOf("Alex") >= 0)
		verify(viewer.title.indexOf("Lobby") >= 0)
		const header = findChild(viewer.contentItem, "screenShareViewerHeader")
		verify(header.Accessible.description.indexOf("Alex") >= 0)
		verify(header.Accessible.description.indexOf("Lobby") >= 0)
		viewer.destroy()
	}

	function createViewer(viewWidth, viewHeight) {
		const component = Qt.createComponent("qrc:/qml-shell/ScreenShareViewWindow.qml")
		compare(component.status, Component.Ready, component.errorString())
		const viewer = component.createObject(null, {
			"backend": shareBackend,
			"visualFixtureMode": true,
			"width": viewWidth,
			"height": viewHeight,
			"visible": true
		})
		verify(viewer !== null, component.errorString())
		viewer.requestActivate()
		wait(0)
		return viewer
	}

	function verifyInside(item, container, label) {
		verify(item !== null, label + " exists")
		const origin = item.mapToItem(container, 0, 0)
		verify(origin.x >= -0.5, label + " starts inside the header")
		verify(origin.y >= -0.5, label + " starts inside the header")
		verify(origin.x + item.width <= container.width + 0.5,
			label + " ends inside the header")
		verify(origin.y + item.height <= container.height + 0.5,
			label + " ends inside the header")
	}

	function verifyActionRow(viewer, header) {
		const pause = findChild(viewer.contentItem, "screenSharePauseButton")
		const mute = findChild(viewer.contentItem, "screenShareMuteButton")
		const volume = findChild(viewer.contentItem, "screenShareVolumeSlider")
		const fullscreen = findChild(viewer.contentItem, "screenShareFullscreenButton")
		const close = findChild(viewer.contentItem, "screenShareCloseButton")
		verifyInside(pause, header, "pause")
		verifyInside(mute, header, "mute")
		verifyInside(volume, header, "volume")
		verifyInside(fullscreen, header, "full screen")
		verifyInside(close, header, "close")
		const pauseOrigin = pause.mapToItem(header, 0, 0)
		const closeOrigin = close.mapToItem(header, 0, 0)
		verify(Math.abs(pauseOrigin.y - closeOrigin.y) <= 0.5,
			"Close viewer stays in the same deliberate action row as Pause")
		compare(close.Accessible.name, "Close viewer")
		compare(pause.iconName, shareBackend.paused ? "play" : "pause")
		compare(mute.iconName, shareBackend.audioMuted ? "volume-off" : "volume")
		compare(fullscreen.iconName, "fullscreen")
		compare(close.iconName, "close")
		return close
	}

	function test_wide_media_chrome_keeps_metadata_and_actions_on_one_compact_row() {
		const viewer = createViewer(1120, 720)
		verify(!viewer.narrowHeader)
		verify(!viewer.controlsWrapped)
		const header = findChild(viewer.contentItem, "screenShareViewerHeader")
		const metadata = findChild(viewer.contentItem, "screenShareViewerMetadata")
		const actions = findChild(viewer.contentItem, "screenShareViewerHeaderActions")
		verify(header !== null && metadata !== null && actions !== null)
		compare(header.Accessible.role, Accessible.ToolBar)
		compare(header.Accessible.name, "Screen share viewer controls")
		verifyActionRow(viewer, header)
		const metadataOrigin = metadata.mapToItem(header, 0, 0)
		const actionsOrigin = actions.mapToItem(header, 0, 0)
		verify(Math.abs(metadataOrigin.y - actionsOrigin.y) <= Theme.space2,
			"wide metadata and actions share one compact header row")
		verify(metadataOrigin.x + metadata.width <= actionsOrigin.x + 0.5,
			"metadata does not collide with right-aligned actions")
		verify(actionsOrigin.x + actions.width <= header.width - Theme.space3 + 0.5,
			"actions remain right aligned inside the header padding")
		verify(header.height < 64, "the 1120 px media chrome remains compact")
		const canvas = findChild(viewer.contentItem, "screenShareCanvas")
		const canvasOrigin = canvas.mapToItem(viewer.contentItem, 0, 0)
		const headerOrigin = header.mapToItem(viewer.contentItem, 0, 0)
		verify(canvasOrigin.y <= 0.5, "video owns the first row of the viewer")
		verify(headerOrigin.y >= canvasOrigin.y + canvas.height - 0.5,
			"controls stay outside the native/external video surface")
		viewer.destroy()
	}

	function test_narrow_media_chrome_moves_the_complete_action_group_to_a_clean_second_row() {
		const viewer = createViewer(640, 400)
		verify(viewer.narrowHeader)
		verify(viewer.controlsWrapped)
		const header = findChild(viewer.contentItem, "screenShareViewerHeader")
		const metadata = findChild(viewer.contentItem, "screenShareViewerMetadata")
		const actions = findChild(viewer.contentItem, "screenShareViewerHeaderActions")
		const canvas = findChild(viewer.contentItem, "screenShareCanvas")
		verify(header !== null && metadata !== null && actions !== null && canvas !== null)
		verifyActionRow(viewer, header)
		const metadataOrigin = metadata.mapToItem(header, 0, 0)
		const actionsOrigin = actions.mapToItem(header, 0, 0)
		verify(actionsOrigin.y >= metadataOrigin.y + metadata.height + Theme.space2 - 0.5,
			"the complete action group follows metadata at narrow width")
		verify(actionsOrigin.x >= Theme.space3 - 0.5)
		verify(actionsOrigin.x + actions.width <= header.width - Theme.space3 + 0.5)
		verify(canvas.height > 180, "responsive header does not collapse the video surface")
		viewer.destroy()
	}

	function test_loading_and_error_states_preserve_clear_primary_focus() {
		shareBackend.operationStatus = "loading"
		const viewer = createViewer(1120, 720)
		tryVerify(function() { return viewer.active })
		const close = findChild(viewer.contentItem, "screenShareCloseButton")
		const pause = findChild(viewer.contentItem, "screenSharePauseButton")
		verify(close.visible)
		verify(!pause.visible)
		verify(viewer.focusInitialControl())
		tryCompare(close, "activeFocus", true)

		shareBackend.operationError = "The helper stopped."
		shareBackend.operationStatus = "error"
		const retry = findChild(viewer.contentItem, "screenShareFailureRetryButton")
		tryCompare(retry, "visible", true)
		tryCompare(retry, "activeFocus", true)
		compare(retry.Accessible.name, "Retry viewer")
		viewer.destroy()
	}

	function test_media_canvas_state_text_uses_overlay_contrast_tokens() {
		shareBackend.operationStatus = "error"
		shareBackend.operationError = "The helper stopped."
		const viewer = createViewer(1120, 720)
		const heading = findChild(viewer.contentItem, "screenShareStateHeading")
		const detail = findChild(viewer.contentItem, "screenShareStateDetail")
		verify(heading !== null && detail !== null)
		compare(String(heading.color), String(Theme.mediaOverlayTextStrong))
		compare(String(detail.color), String(Theme.mediaOverlayTextMuted))
		viewer.destroy()
	}

	function test_paused_state_promotes_resume_without_hiding_viewer_controls() {
		shareBackend.nativeFrameActive = true
		shareBackend.hasCurrentFrame = true
		shareBackend.paused = true
		const viewer = createViewer(1120, 720)
		const headerResume = findChild(viewer.contentItem, "screenSharePauseButton")
		const canvasResume = findChild(viewer.contentItem, "screenSharePausedResumeButton")
		const stateSurface = findChild(viewer.contentItem, "screenShareStateSurface")
		const pausedCard = findChild(viewer.contentItem, "screenSharePausedStateCard")
		const nativeFrame = findChild(viewer.contentItem, "screenShareNativeVideoFrame")
		verify(headerResume !== null && canvasResume !== null && stateSurface !== null)
		verify(pausedCard !== null && nativeFrame !== null)
		compare(headerResume.text, "Resume")
		verify(headerResume.selected)
		compare(headerResume.iconName, "play")
		verify(canvasResume.visible)
		verify(pausedCard.visible)
		verify(nativeFrame.visible,
			"the frozen native frame remains visible underneath the paused scrim")
		verify(stateSurface.color.a < 0.6,
			"the paused scrim remains translucent enough to preserve the frozen frame")
		compare(canvasResume.Accessible.name, "Resume live share")
		compare(stateSurface.Accessible.name, "Paused locally")
		verify(viewer.playbackSurfaceReady,
			"pause keeps the receiver surface identity and last native frame alive")
		verify(viewer.stateDetail.indexOf("receiver stays connected") >= 0)
		canvasResume.clicked()
		compare(shareBackend.pauseCalls, 1)
		verify(!shareBackend.paused)
		tryCompare(viewer, "displayState", "active")
		verify(viewer.playbackSurfaceReady)
		verify(findChild(viewer.contentItem, "screenShareNativeVideoFrame") === nativeFrame,
			"resume reuses the exact same native video item and surface")
		verify(nativeFrame.visible)
		verify(!pausedCard.visible)
		viewer.destroy()
	}

	function test_responsive_header_bindings_are_safe_during_window_destruction() {
		for (const width of [1120, 640, 920, 520]) {
			const viewer = createViewer(width, 420)
			viewer.width = width < 900 ? 1040 : 620
			wait(0)
			viewer.destroy()
			wait(0)
		}
	}

	function test_audio_control_retry_and_error_are_exposed_without_replacing_video() {
		shareBackend.nativeFrameActive = true
		shareBackend.hasCurrentFrame = true
		shareBackend.audioControlStatus = "retrying"
		shareBackend.audioControlError = "Waiting for the viewer's Windows audio session."
		const viewer = createViewer(1120, 720)
		const status = findChild(viewer.contentItem, "screenShareAudioControlStatus")
		verify(status !== null)
		verify(status.visible)
		compare(status.text, shareBackend.audioControlError)
		compare(viewer.displayState, "active")
		verify(viewer.playbackSurfaceReady)

		shareBackend.audioControlStatus = "error"
		shareBackend.audioControlError = "Windows audio controls are unavailable."
		tryCompare(status, "text", shareBackend.audioControlError)
		compare(status.Accessible.role, Accessible.AlertMessage)

		shareBackend.audioControlStatus = "ready"
		shareBackend.audioControlError = ""
		tryCompare(status, "visible", false)
		compare(viewer.displayState, "active")
		viewer.destroy()
	}

	function test_active_native_frame_is_visible_and_keeps_pause_as_primary_focus() {
		shareBackend.nativeFrameActive = true
		shareBackend.hasCurrentFrame = true
		const viewer = createViewer(1120, 720)
		tryVerify(function() { return viewer.active })
		const videoFrame = findChild(viewer.contentItem, "screenShareNativeVideoFrame")
		const pause = findChild(viewer.contentItem, "screenSharePauseButton")
		verify(videoFrame !== null)
		verify(videoFrame.visible)
		compare(videoFrame.Accessible.role, Accessible.Graphic)
		compare(videoFrame.Accessible.name, "Live shared screen frame")
		verify(viewer.focusInitialControl())
		tryCompare(pause, "activeFocus", true)
		viewer.destroy()
	}

	function test_standard_close_shortcut_binds_every_platform_sequence() {
		const viewer = createViewer(1120, 720)
		const shortcut = findChild(viewer, "screenShareCloseShortcut")
		verify(shortcut !== null)
		verify(shortcut.sequences.length > 0)
		for (let index = 0; index < shortcut.sequences.length; ++index) {
			keySequence(shortcut.sequences[index])
			compare(shareBackend.stopCalls, index + 1)
		}
		viewer.destroy()
	}
}
