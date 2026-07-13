import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "ScreenShareEditor"
    when: windowShown
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
					"thumbnail": thumbnail || "" },
					{ "id": "monitor:1", "title": "Screen 2", "thumbnail": "" }]
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
				{ "value": "default-loopback", "label": "System audio" }
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
		source.requestThumbnailIfNeeded()
		compare(requested.length, 1)
		editor.selectedResolutionValue = "2560x1440"
		editor.selectedFrameRateValue = "60"
		editor.selectedAudioValue = "default-loopback"

		editor.shareState = screenShareState("image://mumble/screen?g=2")
		wait(0)
		const payload = editor.actionPayload()
		compare(payload.channelId, "4294967295")
		compare(payload.resolution, "2560x1440")
		compare(payload.frameRate, 60)
		compare(payload.audio, "default-loopback")
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
}
