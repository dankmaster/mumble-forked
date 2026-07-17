// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root
	objectName: "inputEnhancementCalibration"
	required property var field
	required property var controller

	property bool captureOptionalLocalNoise: false
	property string activePlaybackToken: ""
	readonly property int playbackExpiryMs: 12000
	readonly property string leftToken: String(field.inputEnhancementCalibrationLeftPlaybackToken || "")
	readonly property string rightToken: String(field.inputEnhancementCalibrationRightPlaybackToken || "")
	readonly property int calibrationState: Number(field.inputEnhancementCalibrationState || 0)
	readonly property string workerState: String(field.inputEnhancementCalibrationWorkerState || "idle")
	readonly property int workerProgress: Math.max(0,
		Math.min(100, Number(field.inputEnhancementCalibrationProgress || 0)))
	readonly property string errorText: String(field.inputEnhancementCalibrationErrorText || "")
	readonly property bool probationRunning: !!field.inputEnhancementProbationRunning
	readonly property bool probationUndoAvailable: !!field.inputEnhancementProbationUndoAvailable
	readonly property bool workerActive: workerState === "running" || workerState === "cancelling"
	readonly property bool captureActive: calibrationState >= 1 && calibrationState <= 9
	readonly property bool comparisonReady: (calibrationState === 10 || calibrationState === 11)
		&& leftToken.length > 0 && rightToken.length > 0 && leftToken !== rightToken
	readonly property bool refreshTimerRunning: refreshTimer.running

	readonly property string startActionId: String(field.inputEnhancementCalibrationStartActionId || "")
	readonly property string advanceActionId: String(field.inputEnhancementCalibrationAdvanceActionId || "")
	readonly property string cancelActionId: String(field.inputEnhancementCalibrationCancelActionId || "")
	readonly property string skipNoiseActionId: String(field.inputEnhancementCalibrationSkipNoiseActionId || "")
	readonly property string evaluateActionId: String(field.inputEnhancementCalibrationEvaluateActionId || "")
	readonly property string refreshActionId: String(field.inputEnhancementCalibrationRefreshActionId || "")
	readonly property string selectActionId: String(field.inputEnhancementCalibrationSelectActionId || "")
	readonly property string applyActionId: String(field.inputEnhancementCalibrationApplyActionId || "")
	readonly property string undoActionId: String(field.inputEnhancementProbationUndoActionId || "")
	readonly property string playActionId: String(
		field.inputEnhancementCalibrationPlaybackActionId || "playInputEnhancementCalibration")
	readonly property string stopActionId: String(
		field.inputEnhancementCalibrationPlaybackStopActionId || "stopInputEnhancementCalibrationPlayback")

	function invoke(actionId, payload) {
		if (String(actionId || "").length > 0)
			controller.invokeAction(actionId, payload || {})
	}

	function startCalibration() {
		invoke(startActionId, { "captureOptionalLocalNoise": captureOptionalLocalNoise })
	}

	function expirePlaybackState() {
		playbackExpiryTimer.stop()
		activePlaybackToken = ""
	}

	function stopPlayback() {
		playbackExpiryTimer.stop()
		if (activePlaybackToken.length > 0)
			invoke(stopActionId, {})
		activePlaybackToken = ""
	}

	function play(token) {
		const normalized = String(token || "")
		if (!comparisonReady || normalized.length === 0 || playActionId.length === 0)
			return
		if (activePlaybackToken === normalized) {
			stopPlayback()
			return
		}
		if (activePlaybackToken.length > 0)
			stopPlayback()
		invoke(playActionId, { "playbackToken": normalized })
		activePlaybackToken = normalized
		playbackExpiryTimer.restart()
	}

	function select(token) {
		const normalized = String(token || "")
		if (calibrationState !== 10 || normalized.length === 0 || selectActionId.length === 0)
			return
		stopPlayback()
		invoke(selectActionId, { "playbackToken": normalized })
	}

	function requestRefresh() {
		invoke(refreshActionId, {})
	}

	function validateActivePlayback() {
		if (!comparisonReady || (activePlaybackToken !== leftToken && activePlaybackToken !== rightToken))
			stopPlayback()
	}

	function stateHeading() {
		switch (calibrationState) {
		case 0: return qsTr("Tune input enhancement")
		case 1:
		case 2: return qsTr("Check microphone level")
		case 3:
		case 4: return qsTr("Capture room sound")
		case 5:
		case 6: return qsTr("Capture your voice")
		case 7:
		case 8: return qsTr("Capture local noise")
		case 9: return qsTr("Compare safe candidates")
		case 10: return qsTr("Blind comparison")
		case 11: return qsTr("Selection ready")
		case 12: return qsTr("Calibration applied")
		case 13: return qsTr("Calibration cancelled")
		case 14: return qsTr("Calibration stopped")
		case 15: return qsTr("Calibration could not finish")
		default: return qsTr("Tune input enhancement")
		}
	}

	function levelStatusText() {
		switch (Number(field.inputEnhancementCalibrationLevelStatus || 0)) {
		case 1: return qsTr("Level looks good. Continue when you are ready.")
		case 2: return qsTr("The microphone is too quiet. Move closer or raise its input level, then retry.")
		case 3: return qsTr("The microphone is clipping. Lower its input level, then retry.")
		default: return qsTr("Speak at your normal call volume while the level is checked.")
		}
	}

	function stateDescription() {
		switch (calibrationState) {
		case 0:
			return qsTr("A short local recording is used to compare profiles for this microphone.")
		case 1:
		case 2:
			return levelStatusText()
		case 3:
			return qsTr("Stay quiet for 8 seconds while Mumble learns the room sound.")
		case 4:
			return qsTr("Room sound captured. Continue to the voice sample.")
		case 5:
			return qsTr("Speak naturally for 12 seconds. Include these neutral sounds: s, sh, f — p, t, k.")
		case 6:
			return qsTr("Voice sample captured. Continue when you are ready.")
		case 7:
			return qsTr("For 8 seconds, make the nearby noise you want reduced, such as typing or a fan. You can skip this step.")
		case 8:
			return qsTr("Local noise captured. Continue to local analysis, or discard this optional sample.")
		case 9:
			if (workerState === "running")
				return qsTr("Testing approved profiles through the product pipeline and Opus on this computer.")
			if (workerState === "cancelling")
				return qsTr("Stopping local analysis safely…")
			if (workerState === "failed")
				return errorText.length > 0 ? errorText : qsTr("Local analysis failed. Cancel and try again.")
			return qsTr("The samples are ready. Start local analysis to prepare a loudness-matched blind pair.")
		case 10:
			return qsTr("Listen to A and B, then choose the version you prefer. Profile names stay hidden.")
		case 11:
			return qsTr("Your choice is staged only. Apply it to save this microphone's calibration.")
		case 12:
			return probationRunning
				? qsTr("The chosen profile is active and completing a short safety check.")
				: qsTr("The chosen profile was saved for this microphone.")
		case 13:
			return qsTr("No calibration choice was saved and captured audio was erased.")
		case 14:
			return qsTr("Calibration stopped safely. The previous input profile remains active.")
		case 15:
			return errorText.length > 0 ? errorText
				: qsTr("Calibration failed safely. The previous input profile remains active.")
		default:
			return qsTr("The previous input profile remains active.")
		}
	}

	onFieldChanged: validateActivePlayback()
	onCalibrationStateChanged: validateActivePlayback()
	onVisibleChanged: {
		if (!visible)
			stopPlayback()
	}
	Component.onDestruction: stopPlayback()

	visible: startActionId.length > 0 || calibrationState !== 0 || errorText.length > 0
	Layout.fillWidth: true
	spacing: Math.max(6, Theme.spacing - 3)

	Timer {
		id: refreshTimer
		objectName: "inputEnhancementCalibrationRefreshTimer"
		interval: 400
		repeat: true
		running: root.visible && root.refreshActionId.length > 0
			&& (root.captureActive || root.workerActive || root.probationRunning)
		onTriggered: root.requestRefresh()
	}

	Timer {
		id: playbackExpiryTimer
		objectName: "inputEnhancementCalibrationPlaybackExpiryTimer"
		interval: root.playbackExpiryMs
		repeat: false
		onTriggered: root.expirePlaybackState()
	}

	Label {
		objectName: "inputEnhancementCalibrationHeading"
		Layout.fillWidth: true
		textFormat: Text.PlainText
		text: root.stateHeading()
		color: Theme.textStrong
		font.bold: true
	}

	Label {
		objectName: "inputEnhancementCalibrationStatus"
		Layout.fillWidth: true
		textFormat: Text.PlainText
		text: root.stateDescription()
		color: root.calibrationState === 15 || root.workerState === "failed" ? Theme.danger : Theme.textMuted
		font.pixelSize: 11
		wrapMode: Text.Wrap
	}

	Label {
		objectName: "inputEnhancementCalibrationPrivacy"
		Layout.fillWidth: true
		textFormat: Text.PlainText
		text: qsTr("Calibration audio stays in memory on this device, is never sent to the server, and is erased when calibration ends.")
		color: Theme.textMuted
		font.pixelSize: 10
		wrapMode: Text.Wrap
	}

	ModernCheckBox {
		id: optionalNoise
		objectName: "inputEnhancementCalibrationOptionalNoise"
		Layout.fillWidth: true
		visible: root.calibrationState === 0
		text: qsTr("Also capture 8 seconds of keyboard, fan, or other nearby noise")
		checked: root.captureOptionalLocalNoise
		onToggled: root.captureOptionalLocalNoise = checked
	}

	ModernProgressBar {
		objectName: "inputEnhancementCalibrationLevel"
		Layout.fillWidth: true
		visible: root.calibrationState === 1 || root.calibrationState === 2
		from: 0
		to: 100
		value: Math.max(Number(root.field.inputEnhancementCalibrationLevelPeakPercent || 0),
			Number(root.field.inputEnhancementCalibrationLevelRmsPercent || 0))
		tone: Number(root.field.inputEnhancementCalibrationLevelStatus || 0) === 1 ? "success"
			: Number(root.field.inputEnhancementCalibrationLevelStatus || 0) >= 2 ? "warning" : "accent"
		Accessible.name: qsTr("Microphone level")
		Accessible.description: qsTr("%1 percent").arg(Math.round(value))
	}

	ModernProgressBar {
		objectName: "inputEnhancementCalibrationCaptureProgress"
		Layout.fillWidth: true
		visible: root.calibrationState === 3 || root.calibrationState === 5 || root.calibrationState === 7
		indeterminate: true
		Accessible.name: root.calibrationState === 3 ? qsTr("Capturing room sound")
			: root.calibrationState === 5 ? qsTr("Capturing voice") : qsTr("Capturing local noise")
	}

	ModernProgressBar {
		objectName: "inputEnhancementCalibrationEvaluationProgress"
		Layout.fillWidth: true
		visible: root.calibrationState === 9 && root.workerActive
		from: 0
		to: 100
		value: root.workerProgress
		indeterminate: root.workerState === "cancelling"
		tone: root.workerState === "cancelling" ? "warning" : "accent"
		Accessible.name: qsTr("Local profile analysis")
		Accessible.description: qsTr("%1 percent").arg(root.workerProgress)
	}

	Flow {
		Layout.fillWidth: true
		spacing: Math.max(6, Math.round(Theme.spacing / 2))

		ModernButton {
			objectName: "inputEnhancementCalibrationStart"
			visible: root.calibrationState === 0 || root.calibrationState >= 12
			enabled: root.startActionId.length > 0 && !root.probationRunning
			text: root.calibrationState === 0 ? qsTr("Start calibration") : qsTr("Calibrate again")
			tone: "accent"
			onClicked: root.startCalibration()
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationAdvance"
			visible: root.calibrationState === 2 || root.calibrationState === 4
				|| root.calibrationState === 6 || root.calibrationState === 8
			enabled: root.advanceActionId.length > 0
			text: root.calibrationState === 2
				&& Number(root.field.inputEnhancementCalibrationLevelStatus || 0) !== 1
				? qsTr("Retry level check") : qsTr("Continue")
			tone: "accent"
			onClicked: root.invoke(root.advanceActionId, {})
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationSkipNoise"
			visible: root.calibrationState === 7 || root.calibrationState === 8
			enabled: root.skipNoiseActionId.length > 0
			text: root.calibrationState === 8 ? qsTr("Discard optional sample") : qsTr("Skip")
			tone: "secondary"
			onClicked: root.invoke(root.skipNoiseActionId, {})
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationEvaluate"
			visible: root.calibrationState === 9 && !root.workerActive
			enabled: root.evaluateActionId.length > 0 && root.workerState !== "succeeded"
			text: root.workerState === "failed" ? qsTr("Retry analysis") : qsTr("Start local analysis")
			tone: "accent"
			onClicked: root.invoke(root.evaluateActionId, {})
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationPlayA"
			visible: root.comparisonReady
			text: root.activePlaybackToken === root.leftToken ? qsTr("Stop A") : qsTr("Play A")
			tone: root.activePlaybackToken === root.leftToken ? "warning" : "secondary"
			onClicked: root.play(root.leftToken)
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationPreferA"
			visible: root.calibrationState === 10 && root.comparisonReady && root.selectActionId.length > 0
			text: qsTr("Prefer A")
			tone: "accent"
			onClicked: root.select(root.leftToken)
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationPlayB"
			visible: root.comparisonReady
			text: root.activePlaybackToken === root.rightToken ? qsTr("Stop B") : qsTr("Play B")
			tone: root.activePlaybackToken === root.rightToken ? "warning" : "secondary"
			onClicked: root.play(root.rightToken)
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationPreferB"
			visible: root.calibrationState === 10 && root.comparisonReady && root.selectActionId.length > 0
			text: qsTr("Prefer B")
			tone: "accent"
			onClicked: root.select(root.rightToken)
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationApply"
			visible: root.calibrationState === 11
			enabled: root.applyActionId.length > 0
			text: qsTr("Apply selection")
			tone: "accent"
			onClicked: {
				root.stopPlayback()
				root.invoke(root.applyActionId, {})
			}
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationUndo"
			visible: root.calibrationState === 12 && root.probationUndoAvailable
			enabled: root.undoActionId.length > 0
			text: qsTr("Undo")
			tone: "warning"
			onClicked: root.invoke(root.undoActionId, {})
		}

		ModernButton {
			objectName: "inputEnhancementCalibrationCancel"
			visible: root.calibrationState >= 1 && root.calibrationState <= 11
			enabled: root.cancelActionId.length > 0 && root.workerState !== "cancelling"
			text: root.workerActive ? qsTr("Stop analysis") : qsTr("Cancel")
			tone: "warning"
			onClicked: {
				root.stopPlayback()
				root.invoke(root.cancelActionId, {})
			}
		}
	}
}
