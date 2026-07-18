import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "RecorderEditor"
	when: windowShown
	visible: true
	width: 760
	height: 680

	QtObject {
		id: recorder
		property string state: "idle"
		property bool busy: false
		property bool canEdit: true
		property bool canStart: true
		property bool canPause: false
		property bool canResume: false
		property bool canStop: false
		property string elapsedText: "00:00:00"
		property string outputDirectory: "C:/Recordings"
		property string fileName: "%user"
		property string resolvedOutputPath: "C:/Recordings/%user.wav"
		property int format: 0
		property int mode: 0
		property var formatOptions: [
			{ "label": ".wav - Uncompressed", "value": 0, "enabled": true },
			{ "label": ".flac - Lossless", "value": 1, "enabled": true }
		]
		property var modeOptions: [
			{ "label": "Mixdown", "value": 0, "enabled": true },
			{ "label": "Multichannel", "value": 1, "enabled": true },
			{ "label": "Transport only", "value": 3, "enabled": false }
		]
		property var fieldErrors: ({})
		property string errorMessage: ""
		property string operationPhase: ""
		property int startCalls: 0
		property int pauseCalls: 0
		property int resumeCalls: 0
		property int stopCalls: 0
		property int clearErrorCalls: 0
		function start() { ++startCalls; return true }
		function pause() { ++pauseCalls; return true }
		function resume() { ++resumeCalls; return true }
		function stop() { ++stopCalls; return true }
		function clearError() { ++clearErrorCalls }
	}

	function createEditor() {
		const component = Qt.createComponent("qrc:/qml-shell/RecorderEditor.qml")
		compare(component.status, Component.Ready, component.errorString())
		const editor = component.createObject(testCase, {
			"recorderController": recorder,
			"width": testCase.width
		})
		verify(editor !== null, component.errorString())
		editor.height = editor.implicitHeight
		return editor
	}

	function init() {
		recorder.state = "idle"
		recorder.busy = false
		recorder.canEdit = true
		recorder.canStart = true
		recorder.canPause = false
		recorder.canResume = false
		recorder.canStop = false
		recorder.elapsedText = "00:00:00"
		recorder.outputDirectory = "C:/Recordings"
		recorder.fileName = "%user"
		recorder.resolvedOutputPath = "C:/Recordings/%user.wav"
		recorder.format = 0
		recorder.mode = 0
		recorder.fieldErrors = ({})
		recorder.errorMessage = ""
		recorder.operationPhase = ""
		recorder.startCalls = 0
		recorder.pauseCalls = 0
		recorder.resumeCalls = 0
		recorder.stopCalls = 0
		recorder.clearErrorCalls = 0
	}

	function test_idleSurfaceHasTypedOutputAndAccessibleStatus() {
		const editor = createEditor()
		const status = findChild(editor, "recorderStatusCard")
		const elapsed = findChild(editor, "recorderElapsed")
		const path = findChild(editor, "recording.path")
		const file = findChild(editor, "recording.file")
		const format = findChild(editor, "recording.format")
		const mode = findChild(editor, "recording.mode")
		const start = findChild(editor, "recorderStartButton")
		verify(status && elapsed && path && file && format && mode && start)
		compare(editor.Accessible.role, Accessible.Pane)
		compare(status.Accessible.role, Accessible.StatusBar)
		verify(status.Accessible.name.indexOf("Ready") >= 0)
		compare(elapsed.text, "00:00:00")
		compare(path.text, "C:/Recordings")
		compare(file.text, "%user")
		compare(format.currentIndex, 0)
		compare(mode.currentIndex, 0)
		verify(start.visible && start.enabled)
		start.clicked()
		compare(recorder.startCalls, 1)
		editor.destroy()
	}

	function test_elapsedAndAsyncStateNeverStealEditorFocus() {
		const editor = createEditor()
		const path = findChild(editor, "recording.path")
		verify(path !== null)
		path.forceActiveFocus()
		tryCompare(path, "activeFocus", true)

		recorder.elapsedText = "00:00:01"
		recorder.busy = true
		recorder.operationPhase = "starting"
		wait(0)
		compare(findChild(editor, "recorderElapsed").text, "00:00:01")
		compare(path.activeFocus, true)
		verify(findChild(editor, "recorderOperationBusy").visible)
		editor.destroy()
	}

	function test_pauseResumeStopActionsFollowLiveState() {
		const editor = createEditor()
		recorder.state = "recording"
		recorder.canEdit = false
		recorder.canStart = false
		recorder.canPause = true
		recorder.canStop = true
		wait(0)
		const pause = findChild(editor, "recorderPauseButton")
		const stop = findChild(editor, "recorderStopButton")
		verify(pause.visible && pause.enabled && stop.visible && stop.enabled)
		pause.clicked()
		compare(recorder.pauseCalls, 1)

		recorder.state = "paused"
		recorder.canPause = false
		recorder.canResume = true
		wait(0)
		const resume = findChild(editor, "recorderResumeButton")
		verify(resume.visible && resume.enabled)
		resume.clicked()
		stop.clicked()
		compare(recorder.resumeCalls, 1)
		compare(recorder.stopCalls, 1)
		editor.destroy()
	}

	function test_validationAndRuntimeErrorsAreInlineAndDismissible() {
		recorder.state = "error"
		recorder.errorMessage = "The disk is full."
		recorder.fieldErrors = { "recording.path": "Choose a target directory." }
		const editor = createEditor()
		const banner = findChild(editor, "recorderErrorBanner")
		const path = findChild(editor, "recording.path")
		const pathError = findChild(editor, "recording.path.error")
		const dismiss = findChild(editor, "recorderDismissError")
		verify(banner.visible && path.invalid && pathError.visible && dismiss.visible)
		compare(banner.Accessible.role, Accessible.AlertMessage)
		compare(path.Accessible.description, "Choose a target directory.")
		dismiss.clicked()
		compare(recorder.clearErrorCalls, 1)
		editor.destroy()
	}

	function test_browseIntentAndCompactLayoutStayInsideEditor() {
		const editor = createEditor()
		const requested = []
		editor.browseRequested.connect(function(path) { requested.push(path) })
		findChild(editor, "recorderBrowseButton").clicked()
		compare(requested, ["C:/Recordings"])
		editor.width = 480
		tryCompare(editor, "compactLayout", true)
		verify(editor.implicitHeight > 0)
		editor.destroy()
	}
}
