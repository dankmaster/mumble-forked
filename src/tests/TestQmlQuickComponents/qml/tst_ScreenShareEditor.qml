import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
    id: testCase
    name: "ScreenShareEditor"
    when: windowShown
	visible: true
    width: 640
    height: 480

    function createEditor() {
        const component = Qt.createComponent("qrc:/qml-shell/ScreenShareEditor.qml")
        compare(component.status, Component.Ready, component.errorString())
        const editor = component.createObject(testCase)
        verify(editor !== null, component.errorString())
        return editor
    }

    function test_thumbnailSourcesStayInsideManagedPipeline() {
        const editor = createEditor()

        compare(editor.safeThumbnailSource("image://mumble/source?g=1"),
                "image://mumble/source?g=1")
        compare(editor.safeThumbnailSource("qrc:/qml-shell/fallback.svg"),
                "qrc:/qml-shell/fallback.svg")
        compare(editor.safeThumbnailSource("data:image/jpeg;base64,AAAA"), "")
        compare(editor.safeThumbnailSource("https://example.test/window.jpg"), "")
        compare(editor.safeThumbnailSource("file:///C:/Users/test/Desktop/private.png"), "")
        compare(editor.safeThumbnailSource("image://other/source"), "")
        compare(editor.safeThumbnailSource(null), "")

        editor.destroy()
    }

	function screenShareState(thumbnail) {
		return {
			"channelId": "4294967295",
			"selectedSourceId": "monitor:0",
			"sources": [{
				"id": "screens",
				"section": "Screens",
				"items": [{ "id": "monitor:0", "title": "Screen 1",
					"detail": "Primary display", "thumbnail": thumbnail || "",
					"processId": 0, "audioAuto": false },
					{ "id": "monitor:1", "title": "Screen 2",
						"detail": "Secondary display", "thumbnail": "",
						"processId": 0, "audioAuto": false },
					{ "id": "window:42", "title": "Browser",
						"detail": "Community stream", "thumbnail": "",
						"processId": 4242, "audioAuto": true }]
			}],
			"resolutionOptions": [
				{ "value": "1920x1080", "label": "1080p" },
				{ "value": "2560x1440", "label": "1440p" }
			],
			"resolutionDefault": "1920x1080",
			"frameRateOptions": [
				{ "value": 30, "label": "30 FPS" },
				{ "value": 60, "label": "60 FPS" }
			],
			"frameRateDefault": 30,
			"audioOptions": [
				{ "value": "", "label": "No audio" },
				{ "value": "default-loopback", "label": "System audio" },
				{ "value": "process:4242", "label": "App: Browser" }
			],
			"audioDefault": ""
		}
	}

	function test_selection_and_uint32_channel_survive_hydration() {
		const editor = createEditor()
		editor.width = testCase.width
		const requested = []
		editor.thumbnailRequested.connect(function(sourceId) { requested.push(sourceId) })
		editor.shareState = screenShareState("")
		tryVerify(function() { return editor.controlsInitialized })
		const source = findChild(editor, "screenShareSource_monitor:0")
		verify(source !== null)
		source.forceActiveFocus()
		tryCompare(source, "activeFocus", true)
		compare(source.border.color, Theme.focus)
		compare(source.border.width, Theme.focusRingWidth)
		source.requestThumbnailIfNeeded()
		compare(requested.length, 1)
		editor.selectedResolutionValue = "2560x1440"
		editor.selectedFrameRateValue = "60"
		editor.selectedAudioValue = "default-loopback"

		editor.shareState = screenShareState("image://mumble/screen?g=2")
		wait(0)
		const payload = editor.actionPayload()
		compare(payload.channelId, "4294967295")
		compare(payload.sourceId, "monitor:0")
		compare(payload.resolution, "2560x1440")
		compare(payload.frameRate, 60)
		compare(payload.audio, "default-loopback")
		editor.destroy()
	}

	function test_window_source_uses_discovered_app_audio_until_user_overrides_it() {
		const editor = createEditor()
		editor.width = testCase.width
		editor.shareState = screenShareState("")
		tryVerify(function() { return editor.controlsInitialized })
		const appWindow = findChild(editor, "screenShareSource_window:42")
		const monitor = findChild(editor, "screenShareSource_monitor:1")
		verify(appWindow !== null && monitor !== null)

		appWindow.selectSource()
		compare(editor.selectedSourceId, "window:42")
		compare(editor.selectedAudioValue, "process:4242")
		compare(editor.actionPayload().audio, "process:4242")

		editor.selectedAudioValue = "default-loopback"
		editor.audioSelectionExplicit = true
		monitor.selectSource()
		appWindow.selectSource()
		compare(editor.selectedAudioValue, "default-loopback")
		editor.destroy()
	}

	function test_start_eligibility_and_stale_source_error_follow_current_state() {
		const editor = createEditor()
		editor.width = testCase.width
		editor.shareState = screenShareState("")
		tryVerify(function() { return editor.controlsInitialized })
		verify(editor.hasValidSelectedSource)

		editor.selectedSourceId = "window:missing"
		verify(!editor.hasValidSelectedSource)

		const staleState = screenShareState("")
		staleState.selectedSourceId = ""
		staleState.sourceError = "That screen is no longer available."
		editor.shareState = staleState
		wait(0)
		compare(editor.selectedSourceId, "")
		verify(!editor.hasValidSelectedSource)
		const error = findChild(editor, "screenShareSourceError")
		verify(error !== null && error.visible)
		compare(error.text, "That screen is no longer available.")
		compare(error.Accessible.role, Accessible.AlertMessage)
		compare(error.Accessible.name, error.text)
		editor.destroy()
	}

	function test_runtime_probe_and_failure_are_visible_in_the_picker() {
		const editor = createEditor()
		editor.width = testCase.width
		const probing = screenShareState("")
		probing.runtimeProbePending = true
		editor.shareState = probing
		wait(0)
		const status = findChild(editor, "screenShareRuntimeStatus")
		const label = findChild(editor, "screenShareRuntimeStatusLabel")
		const busy = findChild(editor, "screenShareRuntimeBusy")
		verify(status !== null && status.visible)
		verify(label !== null && label.text.indexOf("Checking") >= 0)
		verify(busy !== null && busy.visible && busy.running)
		compare(status.Accessible.role, Accessible.ProgressBar)

		const failed = screenShareState("")
		failed.runtimeError = "The bundled runtime is unavailable."
		editor.shareState = failed
		wait(0)
		verify(status.visible)
		compare(label.text, failed.runtimeError)
		verify(!busy.visible)
		compare(status.Accessible.role, Accessible.AlertMessage)
		compare(status.Accessible.name, failed.runtimeError)
		editor.destroy()
	}

	function test_thumbnail_retry_is_bounded_to_selected_or_focused_source() {
		const editor = createEditor()
		editor.width = testCase.width
		const requested = []
		editor.thumbnailRequested.connect(function(sourceId) { requested.push(sourceId) })
		editor.shareState = screenShareState("")
		tryVerify(function() { return editor.controlsInitialized })
		const selected = findChild(editor, "screenShareSource_monitor:0")
		const background = findChild(editor, "screenShareSource_monitor:1")
		verify(selected !== null)
		verify(background !== null)

		background.requestThumbnailIfNeeded()
		compare(background.thumbnailRequestAttempts, 1)
		wait(700)
		compare(background.thumbnailRequestAttempts, 1)

		selected.requestThumbnailIfNeeded()
		const attemptsBeforeRetry = selected.thumbnailRequestAttempts
		tryVerify(function() {
			return selected.thumbnailRequestAttempts > attemptsBeforeRetry
		}, 1200)
		verify(selected.thumbnailRequestAttempts <= 4)
		editor.destroy()
	}

	function test_source_card_uses_theme_pressed_state() {
		const editor = createEditor()
		editor.width = testCase.width
		editor.shareState = screenShareState("")
		tryVerify(function() { return editor.controlsInitialized })
		editor.height = editor.implicitHeight
		const source = findChild(editor, "screenShareSource_monitor:1")
		const hitArea = findChild(editor, "screenShareSourceHitArea_monitor:1")
		const thumbnail = findChild(editor, "screenShareSourceThumbnail_monitor:1")
		const title = findChild(editor, "screenShareSourceTitle_monitor:1")
		const detail = findChild(editor, "screenShareSourceDetail_monitor:1")
		const selected = []
		editor.sourceSelected.connect(function(sourceId) { selected.push(sourceId) })
		verify(source !== null)
		verify(hitArea !== null)
		verify(thumbnail !== null && title !== null && detail !== null)
		compare(String(thumbnail.color), String(Theme.mediaCanvas))
		compare(thumbnail.radius, Theme.space1)
		compare(title.font.pixelSize, Theme.fontLabel)
		compare(detail.font.pixelSize, Theme.fontCaption)
		compare(source.Accessible.role, Accessible.RadioButton)
		compare(source.Accessible.name, "Screen 2")
		compare(source.Accessible.description, "Secondary display")
		compare(title.Accessible.ignored, true)
		compare(detail.Accessible.ignored, true)

		mousePress(hitArea, hitArea.width / 2, hitArea.height / 2, Qt.LeftButton)
		tryCompare(hitArea, "pressed", true)
		compare(source.color, Theme.accentSubtle)
		mouseRelease(hitArea, hitArea.width / 2, hitArea.height / 2, Qt.LeftButton)
		compare(editor.selectedSourceId, "monitor:1")
		compare(selected.length, 1)
		compare(selected[0], "monitor:1")
		editor.destroy()
	}
}
