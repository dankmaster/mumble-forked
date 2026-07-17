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

	property string activePlaybackToken: ""
	readonly property string leftToken: String(field.inputEnhancementCalibrationLeftPlaybackToken || "")
	readonly property string rightToken: String(field.inputEnhancementCalibrationRightPlaybackToken || "")
	readonly property int calibrationState: Number(field.inputEnhancementCalibrationState || 0)
	readonly property bool comparisonReady: (calibrationState === 10 || calibrationState === 11)
		&& leftToken.length > 0 && rightToken.length > 0 && leftToken !== rightToken
	readonly property string playActionId: String(
		field.inputEnhancementCalibrationPlaybackActionId || "playInputEnhancementCalibration")
	readonly property string stopActionId: String(
		field.inputEnhancementCalibrationPlaybackStopActionId || "stopInputEnhancementCalibrationPlayback")
	readonly property string selectActionId: String(field.inputEnhancementCalibrationSelectActionId || "")

	function stopPlayback() {
		if (activePlaybackToken.length > 0 && stopActionId.length > 0)
			controller.invokeAction(stopActionId, {})
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
		controller.invokeAction(playActionId, { "playbackToken": normalized })
		activePlaybackToken = normalized
	}

	function select(token) {
		const normalized = String(token || "")
		if (!comparisonReady || normalized.length === 0 || selectActionId.length === 0)
			return
		stopPlayback()
		controller.invokeAction(selectActionId, { "playbackToken": normalized })
	}

	function validateActivePlayback() {
		if (!comparisonReady || (activePlaybackToken !== leftToken && activePlaybackToken !== rightToken))
			stopPlayback()
	}

	onFieldChanged: validateActivePlayback()
	onVisibleChanged: {
		if (!visible)
			stopPlayback()
	}

	visible: comparisonReady
	Layout.fillWidth: true
	spacing: Math.max(6, Theme.spacing - 3)

	Label {
		objectName: "inputEnhancementCalibrationHeading"
		Layout.fillWidth: true
		textFormat: Text.PlainText
		text: qsTr("Blind comparison")
		color: Theme.textStrong
		font.bold: true
	}
	Label {
		objectName: "inputEnhancementCalibrationPrivacy"
		Layout.fillWidth: true
		textFormat: Text.PlainText
		text: qsTr("A and B are played only on this device. Nothing is sent to the server.")
		color: Theme.textMuted
		font.pixelSize: 11
		wrapMode: Text.Wrap
	}
	Flow {
		Layout.fillWidth: true
		spacing: Math.max(6, Math.round(Theme.spacing / 2))
		ModernButton {
			objectName: "inputEnhancementCalibrationPlayA"
			text: root.activePlaybackToken === root.leftToken ? qsTr("Stop A") : qsTr("Play A")
			tone: root.activePlaybackToken === root.leftToken ? "warning" : ""
			onClicked: root.play(root.leftToken)
		}
		ModernButton {
			objectName: "inputEnhancementCalibrationPreferA"
			visible: root.selectActionId.length > 0
			text: qsTr("Prefer A")
			tone: "accent"
			onClicked: root.select(root.leftToken)
		}
		ModernButton {
			objectName: "inputEnhancementCalibrationPlayB"
			text: root.activePlaybackToken === root.rightToken ? qsTr("Stop B") : qsTr("Play B")
			tone: root.activePlaybackToken === root.rightToken ? "warning" : ""
			onClicked: root.play(root.rightToken)
		}
		ModernButton {
			objectName: "inputEnhancementCalibrationPreferB"
			visible: root.selectActionId.length > 0
			text: qsTr("Prefer B")
			tone: "accent"
			onClicked: root.select(root.rightToken)
		}
	}
}
