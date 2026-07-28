import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: root
	property var field: ({})
	property var controller: null
	readonly property bool captureActive: !!field.active
	readonly property bool captureFinalizing: !!field.finalizing
	readonly property bool captureBusy: captureActive || captureFinalizing

	implicitHeight: content.implicitHeight + Theme.space3 * 2
	radius: Theme.innerRadius
	color: Theme.surfaceRaised
	border.color: field.tone === "danger" ? Theme.danger
		: captureActive ? Theme.warning : Theme.surfaceBorder
	border.width: 1

	Timer {
		interval: 1000
		repeat: true
		running: root.captureBusy
		onTriggered: {
			const actionId = String(root.field.refreshActionId || "")
			if (root.controller && actionId.length > 0)
				root.controller.invokeAction(actionId, {})
		}
	}

	ColumnLayout {
		id: content
		anchors.fill: parent
		anchors.margins: Theme.space3
		spacing: Theme.space2

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2

			Rectangle {
				Layout.preferredWidth: 9
				Layout.preferredHeight: 9
				radius: 5
				color: root.field.tone === "danger" ? Theme.danger
					: root.captureActive ? Theme.warning
					: root.field.tone === "success" ? Theme.success : Theme.accent
				Accessible.ignored: true
			}

			Label {
				id: statusLabel
				objectName: "audioDebugStatus"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.field.statusText || qsTr("Ready")
				color: Theme.textStrong
				font.bold: true
				wrapMode: Text.Wrap
			}
		}

		Label {
			Layout.fillWidth: true
			textFormat: Text.PlainText
			text: root.field.privacyText || ""
			color: Theme.textMain
			wrapMode: Text.Wrap
		}

		ModernCheckBox {
			id: rawInputCheck
			objectName: "audioDebugRawInput"
			Layout.fillWidth: true
			text: qsTr("Include raw microphone audio")
			checked: root.captureBusy ? !!root.field.captureRawInput
				: root.field.defaultCaptureRawInput !== false
			enabled: !root.captureBusy
			Accessible.description: qsTr("Adds raw-input.wav to the private local capture.")
		}

		ModernCheckBox {
			id: serverMixCheck
			objectName: "audioDebugServerMix"
			Layout.fillWidth: true
			text: qsTr("Include voices received from the server")
			checked: root.captureBusy ? !!root.field.captureServerMix
				: !!root.field.defaultCaptureServerMix
			enabled: !root.captureBusy
			Accessible.description: qsTr("Adds incoming remote speech before device playback. Local interface sounds are excluded.")
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2

			ModernButton {
				id: captureButton
				objectName: "audioDebugCaptureButton"
				text: root.captureActive ? qsTr("Stop and save")
					: root.captureFinalizing ? qsTr("Finalizing…")
					: qsTr("Start 60-second capture")
				tone: root.captureActive ? "warning" : "accent"
				enabled: !root.captureFinalizing
				onClicked: {
					if (!root.controller)
						return
					if (root.captureActive) {
						root.controller.invokeAction(root.field.stopActionId || "", {})
					} else {
						root.controller.invokeAction(root.field.startActionId || "", {
							"durationSeconds": Number(root.field.defaultDurationSeconds || 60),
							"captureRawInput": rawInputCheck.checked,
							"captureServerMix": serverMixCheck.checked
						})
					}
				}
			}

			ModernButton {
				objectName: "audioDebugOpenFolder"
				text: qsTr("Open capture folder")
				dense: true
				enabled: !!root.field.hasCapture
				onClicked: {
					if (root.controller)
						root.controller.invokeAction(root.field.openFolderActionId || "", {})
				}
			}

			Item { Layout.fillWidth: true }
		}

		Label {
			objectName: "audioDebugDirectory"
			Layout.fillWidth: true
			visible: String(root.field.directory || "").length > 0
			textFormat: Text.PlainText
			text: root.field.directory || ""
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			elide: Text.ElideMiddle
			Accessible.name: qsTr("Capture folder")
			Accessible.description: text
		}

		Label {
			Layout.fillWidth: true
			textFormat: Text.PlainText
			text: root.field.guidanceText || ""
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			wrapMode: Text.Wrap
		}

		Label {
			Layout.fillWidth: true
			visible: Number(root.field.droppedItems || 0) > 0
			textFormat: Text.PlainText
			text: qsTr("%1 diagnostic frames were dropped to protect real-time audio.").arg(
				Number(root.field.droppedItems || 0))
			color: Theme.warning
			font.pixelSize: Theme.fontCaption
			wrapMode: Text.Wrap
		}
	}
}
