// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: root
	objectName: "voiceActivationSetup"
	required property var field
	required property var meter
	required property var controller

	property int setupState: 0
	property int ticksRemaining: 0
	property int totalTicks: 1
	property var ambientPeaks: [0, 0, 0]
	property var voiceSums: [0, 0, 0]
	property var voicePeaks: [0, 0, 0]
	property int voiceSamples: 0
	property int suggestedMethod: 1
	property int selectedMethod: 1
	property int suggestedStopThreshold: 20
	property int suggestedStartThreshold: 45
	property string recommendationReason: ""

	readonly property bool captureRunning: setupState === 1 || setupState === 3
	readonly property bool resultReady: setupState === 4 || setupState === 5
	readonly property bool inputAvailable: meter && meter.available !== false
	readonly property real progress: totalTicks > 0
		? Math.max(0, Math.min(1, 1 - ticksRemaining / totalTicks)) : 0

	function boundedMetric(value) {
		return Math.max(0, Math.min(100, Number(value || 0)))
	}

	function metrics() {
		return [boundedMetric(meter.amplitude), boundedMetric(meter.signalToNoise),
			boundedMetric(meter.hybrid)]
	}

	function resetSamples() {
		ambientPeaks = [0, 0, 0]
		voiceSums = [0, 0, 0]
		voicePeaks = [0, 0, 0]
		voiceSamples = 0
		recommendationReason = ""
	}

	function startQuietCapture() {
		resetSamples()
		setupState = 1
		totalTicks = 30
		ticksRemaining = totalTicks
		sampleTimer.restart()
	}

	function startVoiceCapture() {
		setupState = 3
		totalTicks = 50
		ticksRemaining = totalTicks
		sampleTimer.restart()
	}

	function cancelSetup() {
		sampleTimer.stop()
		setupState = 0
		resetSamples()
	}

	function sampleCurrentFrame() {
		const values = metrics()
		if (setupState === 1) {
			for (let index = 0; index < 3; ++index)
				ambientPeaks[index] = Math.max(Number(ambientPeaks[index] || 0), values[index])
		} else if (setupState === 3) {
			for (let index = 0; index < 3; ++index) {
				voiceSums[index] = Number(voiceSums[index] || 0) + values[index]
				voicePeaks[index] = Math.max(Number(voicePeaks[index] || 0), values[index])
			}
			voiceSamples += 1
		}
	}

	function voiceAverage(method) {
		return voiceSamples > 0 ? Number(voiceSums[method] || 0) / voiceSamples : 0
	}

	function marginFor(method) {
		return voiceAverage(method) - Number(ambientPeaks[method] || 0)
	}

	function thresholdsFor(method) {
		const ambient = Number(ambientPeaks[method] || 0)
		const average = voiceAverage(method)
		const peak = Number(voicePeaks[method] || 0)
		const usableVoice = Math.max(average, peak * 0.72)
		const gap = Math.max(6, usableVoice - ambient)
		const stop = Math.max(3, Math.min(85, Math.round(ambient + gap * 0.22)))
		const start = Math.max(stop + 5, Math.min(96, Math.round(ambient + gap * 0.58)))
		return [stop, start]
	}

	function chooseMethod() {
		const speechMargin = marginFor(1)
		const hybridMargin = marginFor(2)
		const volumeMargin = marginFor(0)
		const speechAverage = voiceAverage(1)
		const ambientSpeech = Number(ambientPeaks[1] || 0)

		let method = 1
		if (speechAverage < 18 && volumeMargin >= 10) {
			method = 0
		} else if (ambientSpeech >= 28 && hybridMargin >= 8
				&& hybridMargin >= speechMargin - 3) {
			method = 2
		} else if (speechMargin < 6) {
			const margins = [volumeMargin, speechMargin, hybridMargin]
			method = margins.indexOf(Math.max.apply(Math, margins))
		}

		suggestedMethod = method
		selectMethod(method)
		if (method === 0) {
			recommendationReason = qsTr("Volume separated your voice from the room more reliably than speech probability.")
		} else if (method === 2) {
			recommendationReason = qsTr("Speech probability reacted in the quiet sample, so adding a volume check should reject more non-voice sounds.")
		} else {
			recommendationReason = qsTr("Speech probability provided the clearest separation and should preserve soft words better than an extra gate.")
		}
	}

	function selectMethod(method) {
		selectedMethod = Math.max(0, Math.min(2, Number(method)))
		const thresholds = thresholdsFor(selectedMethod)
		suggestedStopThreshold = thresholds[0]
		suggestedStartThreshold = thresholds[1]
	}

	function finishVoiceCapture() {
		sampleTimer.stop()
		chooseMethod()
		setupState = 4
	}

	function applySuggestion() {
		const actionId = String(field.calibrationActionId || "")
		if (actionId.length === 0)
			return
		controller.invokeAction(actionId, {
			"silenceThreshold": suggestedStopThreshold,
			"speechThreshold": suggestedStartThreshold,
			"vadSource": selectedMethod,
			"voiceHold": Number(field.voiceHold || 20),
			"inputGateMode": 0
		})
		setupState = 5
	}

	function methodName(method) {
		if (method === 0) return qsTr("Volume level")
		if (method === 2) return qsTr("Speech + volume")
		return qsTr("Speech probability")
	}

	function methodDescription(method) {
		if (method === 0)
			return qsTr("Use when the speech detector rarely reacts but your microphone has a clear level difference.")
		if (method === 2)
			return qsTr("Use when keyboard or room sounds are sometimes classified as speech.")
		return qsTr("Best default for most microphones and the safest choice for quiet speech.")
	}

	implicitHeight: setupContent.implicitHeight + Theme.space3 * 2
	Layout.fillWidth: true
	radius: Theme.innerRadius
	color: Theme.panel
	border.color: resultReady ? Theme.accent : Theme.surfaceBorder
	border.width: 1

	Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }

	Timer {
		id: sampleTimer
		interval: 100
		repeat: true
		onTriggered: {
			root.sampleCurrentFrame()
			root.ticksRemaining -= 1
			if (root.ticksRemaining > 0)
				return
			stop()
			if (root.setupState === 1)
				root.setupState = 2
			else if (root.setupState === 3)
				root.finishVoiceCapture()
		}
	}

	ColumnLayout {
		id: setupContent
		anchors.fill: parent
		anchors.margins: Theme.space3
		spacing: Theme.space2

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2
			ModernIcon {
				Layout.preferredWidth: 18
				Layout.preferredHeight: 18
				name: "settings"
				color: Theme.accent
			}
			ColumnLayout {
				Layout.fillWidth: true
				spacing: 1
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.resultReady ? qsTr("Detection recommendation") : qsTr("Detection guide")
					color: Theme.textStrong
					font.weight: Font.DemiBold
				}
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: qsTr("Compare room sound with your normal voice; no audio is recorded or sent.")
					color: Theme.textMuted
					font.pixelSize: 10
					wrapMode: Text.Wrap
				}
			}
		}

		Label {
			Layout.fillWidth: true
			visible: root.setupState === 1
			textFormat: Text.PlainText
			text: qsTr("Detection 1 of 2 · Stay quiet while Mumble measures the room for 3 seconds.")
			color: Theme.textMain
			wrapMode: Text.Wrap
		}
		Label {
			Layout.fillWidth: true
			visible: root.setupState === 2
			textFormat: Text.PlainText
			text: qsTr("Room sample complete. In the next step, speak naturally for 5 seconds.")
			color: Theme.textMain
			wrapMode: Text.Wrap
		}
		Label {
			Layout.fillWidth: true
			visible: root.setupState === 3
			textFormat: Text.PlainText
			text: qsTr("Detection 2 of 2 · Speak at your normal call volume until the meter completes.")
			color: Theme.textMain
			wrapMode: Text.Wrap
		}

		ModernProgressBar {
			Layout.fillWidth: true
			visible: root.captureRunning
			from: 0
			to: 1
			value: root.progress
			animated: true
			Accessible.name: root.setupState === 1 ? qsTr("Room measurement") : qsTr("Voice measurement")
		}

		ColumnLayout {
			Layout.fillWidth: true
			visible: root.resultReady
			spacing: Theme.space2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Recommended: %1").arg(root.methodName(root.suggestedMethod))
				color: Theme.textStrong
				font.weight: Font.DemiBold
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.recommendationReason
				color: Theme.textMuted
				font.pixelSize: 11
				wrapMode: Text.Wrap
			}
			Flow {
				Layout.fillWidth: true
				spacing: Theme.space2
				Repeater {
					model: [1, 2, 0]
					delegate: ModernButton {
						required property int modelData
						objectName: "voiceActivationMethod_" + modelData
						text: root.methodName(modelData)
						checkable: true
						checked: root.selectedMethod === modelData
						dense: true
						onClicked: root.selectMethod(modelData)
						ToolTip.visible: hovered
						ToolTip.text: root.methodDescription(modelData)
					}
				}
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: qsTr("Stop %1% · Start %2% · Extra input gate off")
					.arg(root.suggestedStopThreshold).arg(root.suggestedStartThreshold)
				color: Theme.textMain
				font.pixelSize: 11
				wrapMode: Text.Wrap
			}
			Label {
				Layout.fillWidth: true
				visible: root.setupState === 5
				textFormat: Text.PlainText
				text: qsTr("Detection is ready. Continue with processing calibration below, then use Apply or Done.")
				color: Theme.success
				font.pixelSize: 11
				wrapMode: Text.Wrap
			}
		}

		Flow {
			Layout.fillWidth: true
			spacing: Theme.space2
			ModernButton {
				id: startButton
				objectName: "voiceMeterCalibration_" + String(root.field.id || "")
				visible: root.setupState === 0
				enabled: root.inputAvailable && String(root.field.calibrationActionId || "").length > 0
				text: root.field.calibrationLabel || qsTr("Set up voice activation")
				tone: "accent"
				onClicked: root.startQuietCapture()
			}
			ModernButton {
				visible: root.setupState === 2
				text: qsTr("Start voice sample")
				tone: "accent"
				onClicked: root.startVoiceCapture()
			}
			ModernButton {
				objectName: "voiceActivationUseSuggestion"
				visible: root.setupState === 4
				text: qsTr("Use this setup")
				tone: "accent"
				onClicked: root.applySuggestion()
			}
			ModernButton {
				visible: root.resultReady
				text: qsTr("Measure again")
				onClicked: root.startQuietCapture()
			}
			ModernButton {
				visible: root.setupState >= 1 && root.setupState <= 3
				text: qsTr("Cancel")
				tone: "warning"
				onClicked: root.cancelSetup()
			}
		}
	}
}
