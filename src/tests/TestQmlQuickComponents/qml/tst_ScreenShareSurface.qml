import QtQuick
import QtQuick.Window
import QtTest

TestCase {
	id: testCase
	name: "ScreenShareSurface"
	when: windowShown
	visible: true
	width: 720
	height: 480

	QtObject {
		id: shareBackend
		property string ownerLabel: "Alex"
		property string roomLabel: "Lobby"
		property string title: "Live screen share"
		property string detail: "Live viewer"
		property string status: "The live viewer is ready."
		property string operationStatus: "ready"
		property string operationError: ""
		property bool detached: false
		property bool paused: false
		property bool audioAvailable: true
		property bool audioMuted: false
		property int audioVolume: 72
		property string audioControlStatus: "ready"
		property string audioControlError: ""
		property bool nativeFrameActive: true
		property bool hasCurrentFrame: true
		property var videoWindow: null
		property int stopCalls: 0
		property int retryCalls: 0
		property int dockCalls: 0
		property int detachCalls: 0

		function setPaused(value) { paused = value }
		function setAudioMuted(value) { audioMuted = value }
		function setAudioVolume(value) { audioVolume = Math.round(value) }
		function requestStop() { stopCalls += 1 }
		function requestRetry() { retryCalls += 1 }
		function requestDock() { dockCalls += 1; detached = false }
		function requestDetach() { detachCalls += 1; detached = true }
	}

	Component {
		id: externalVideoComponent
		Window { width: 160; height: 90; visible: false }
	}

	SignalSpy { id: fullscreenSpy; signalName: "fullscreenRequested" }
	TextInput { id: composer; y: 400; width: 320; height: 32 }

	function init() {
		shareBackend.operationStatus = "ready"
		shareBackend.operationError = ""
		shareBackend.detached = false
		shareBackend.paused = false
		shareBackend.audioMuted = false
		shareBackend.audioAvailable = true
		shareBackend.nativeFrameActive = true
		shareBackend.hasCurrentFrame = true
		shareBackend.videoWindow = null
		shareBackend.stopCalls = 0
		shareBackend.retryCalls = 0
		shareBackend.dockCalls = 0
		shareBackend.detachCalls = 0
		fullscreenSpy.target = null
		fullscreenSpy.clear()
	}

	function createSurface(surfaceWidth, surfaceHeight) {
		const component = Qt.createComponent("qrc:/qml-shell/ScreenShareSurface.qml")
		compare(component.status, Component.Ready, component.errorString())
		const surface = createTemporaryObject(component, testCase, {
			"backend": shareBackend,
			"visualFixtureMode": true,
			"width": surfaceWidth,
			"height": surfaceHeight
		})
		verify(surface !== null, component.errorString())
		wait(0)
		return surface
	}

	function verifyInside(item, container) {
		verify(item !== null)
		verify(item.visible && item.enabled, item.objectName + " is available")
		verify(item.width > 0 && item.height > 0, item.objectName + " has a pointer target")
		const origin = item.mapToItem(container, 0, 0)
		verify(origin.x >= -0.5 && origin.y >= -0.5, item.objectName + " starts inside")
		verify(origin.x + item.width <= container.width + 0.5, item.objectName + " fits horizontally")
		verify(origin.y + item.height <= container.height + 0.5, item.objectName + " fits vertically")
	}

	function test_inline_controls_fit_data() {
		return [{ tag: "compact", width: 320 }, { tag: "medium", width: 420 }, { tag: "wide", width: 640 }]
	}

	function test_inline_controls_fit(data) {
		const surface = createSurface(data.width, 160)
		const header = findChild(surface, "screenShareViewerHeader")
		for (const name of ["screenSharePauseButton", "screenShareMuteButton", "screenSharePresentationButton",
				"screenShareFullscreenButton", "screenShareCloseButton"])
			verifyInside(findChild(surface, name), header)
		compare(findChild(surface, "screenShareVolumeSlider").visible, data.width >= 560)
		const mute = findChild(surface, "screenShareMuteButton")
		mouseClick(mute)
		compare(shareBackend.audioMuted, true)
		mouseClick(findChild(surface, "screenSharePauseButton"))
		compare(shareBackend.paused, true)
	}

	function test_dock_and_popout_transfer_the_renderer_without_receiver_actions() {
		const inlineSurface = createSurface(640, 320)
		const detachedSurface = createSurface(640, 320)
		inlineSurface.renderActive = Qt.binding(function() { return !shareBackend.detached })
		detachedSurface.renderActive = Qt.binding(function() { return shareBackend.detached })
		const inlineFrame = findChild(inlineSurface, "screenShareNativeVideoFrame")
		const detachedFrame = findChild(detachedSurface, "screenShareNativeVideoFrame")
		compare(inlineFrame.backend, shareBackend)
		compare(detachedFrame.backend, null)

		const popout = findChild(inlineSurface, "screenSharePresentationButton")
		compare(popout.Accessible.name, "Open in window")
		popout.clicked()
		compare(shareBackend.detachCalls, 1)
		compare(inlineFrame.backend, null)
		compare(detachedFrame.backend, shareBackend)
		verify(!inlineFrame.visible && detachedFrame.visible)

		const dock = findChild(detachedSurface, "screenSharePresentationButton")
		compare(dock.Accessible.name, "Return to channel")
		dock.clicked()
		compare(shareBackend.dockCalls, 1)
		compare(inlineFrame.backend, shareBackend)
		compare(detachedFrame.backend, null)
		compare(shareBackend.stopCalls, 0)
		compare(shareBackend.retryCalls, 0)
	}

	function test_inactive_surface_releases_the_external_window() {
		shareBackend.nativeFrameActive = false
		shareBackend.hasCurrentFrame = false
		const videoWindow = createTemporaryObject(externalVideoComponent, null)
		verify(videoWindow !== null)
		shareBackend.videoWindow = videoWindow
		const surface = createSurface(640, 320)
		const container = findChild(surface, "screenShareExternalVideoFrame")
		compare(container.window, videoWindow)
		surface.renderActive = false
		compare(container.window, null)
		verify(!container.visible)
		surface.renderActive = true
		compare(container.window, videoWindow)
		compare(shareBackend.stopCalls, 0)
		shareBackend.videoWindow = null
	}

	function test_inline_fullscreen_is_a_presentation_request() {
		const surface = createSurface(320, 160)
		fullscreenSpy.target = surface
		mouseClick(findChild(surface, "screenShareFullscreenButton"))
		compare(fullscreenSpy.count, 1)
		compare(fullscreenSpy.signalArguments[0][0], true)
		compare(shareBackend.stopCalls, 0)
		compare(shareBackend.retryCalls, 0)
	}

	function test_stalled_frame_exposes_recovery_and_resumes_with_the_same_surface() {
		const surface = createSurface(320, 160)
		const frame = findChild(surface, "screenShareNativeVideoFrame")
		shareBackend.operationError = "No video frames arrived in time."
		shareBackend.operationStatus = "stalled"
		compare(surface.displayState, "stalled")
		const state = findChild(surface, "screenShareStateSurface")
		verify(state.visible)
		compare(state.Accessible.description, shareBackend.operationError)
		const retry = findChild(surface, "screenShareFailureRetryButton")
		// Becoming visible schedules the nested RowLayout/ColumnLayout for polish.
		// Wait for actual layout completion before QtTest computes pointer coordinates.
		verify(waitForPolish(surface.Window.window), "Recovery layout finishes")
		verifyInside(retry, state)
		mousePress(retry)
		try {
			compare(retry.pressed, true, "The visible retry control receives the pointer press")
		} finally {
			mouseRelease(retry)
		}
		compare(shareBackend.retryCalls, 1)
		compare(shareBackend.stopCalls, 0)
		shareBackend.operationStatus = "ready"
		shareBackend.operationError = ""
		compare(surface.displayState, "active")
		verify(!state.visible)
		compare(findChild(surface, "screenShareNativeVideoFrame"), frame)
		compare(frame.backend, shareBackend)
	}

	function test_inline_recovery_does_not_take_focus_from_the_composer() {
		const surface = createSurface(320, 160)
		composer.forceActiveFocus()
		tryCompare(composer, "activeFocus", true)
		shareBackend.operationStatus = "stalled"
		wait(0)
		compare(surface.displayState, "stalled")
		verify(composer.activeFocus)
	}

	function test_loading_can_be_cancelled_from_the_compact_surface() {
		shareBackend.nativeFrameActive = false
		shareBackend.hasCurrentFrame = false
		shareBackend.operationStatus = "loading"
		const surface = createSurface(320, 160)
		const cancel = findChild(surface, "screenShareCloseButton")
		compare(cancel.Accessible.name, "Cancel viewer")
		verifyInside(cancel, findChild(surface, "screenShareViewerHeader"))
		mouseClick(cancel)
		compare(shareBackend.stopCalls, 1)
	}
}
