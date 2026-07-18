import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	required property var recorderController
	property bool compactLayout: width < 560
	property bool visualFixtureMode: false
	readonly property Item initialFocusTarget: outputDirectoryField
	readonly property string stateLabel: {
		switch (String(recorderController.state || "idle")) {
		case "recording": return qsTr("Recording")
		case "paused": return qsTr("Paused")
		case "stopping": return qsTr("Stopping…")
		case "error": return qsTr("Needs attention")
		default: return qsTr("Ready")
		}
	}
	readonly property color stateTone: recorderController.state === "recording" ? Theme.danger
		: recorderController.state === "paused" ? Theme.warning
		: recorderController.state === "error" ? Theme.danger : Theme.success

	signal browseRequested(string currentDirectory)
	Component {
		id: recorderTextCursorDelegate
		Item {
			width: 1
			Rectangle {
				anchors.fill: parent
				visible: !root.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}

	function fieldError(fieldId) {
		const errors = recorderController.fieldErrors || ({})
		return String(errors[fieldId] || "")
	}

	function optionIndex(options, value) {
		const rows = options || []
		for (let index = 0; index < rows.length; ++index) {
			if (Number(rows[index].value) === Number(value))
				return index
		}
		return -1
	}

	spacing: Theme.space4
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Voice recorder")

	Rectangle {
		objectName: "recorderStatusCard"
		Layout.fillWidth: true
		implicitHeight: statusLayout.implicitHeight + Theme.space4 * 2
		radius: Theme.innerRadius
		color: Theme.panel
		border.color: root.stateTone
		border.width: recorderController.state === "recording" || recorderController.state === "error" ? 2 : 1
		Accessible.role: Accessible.StatusBar
		Accessible.name: root.stateLabel + ", " + recorderController.elapsedText

		RowLayout {
			id: statusLayout
			anchors.fill: parent
			anchors.margins: Theme.space4
			spacing: Theme.space3

			Rectangle {
				Layout.preferredWidth: 12
				Layout.preferredHeight: 12
				radius: 6
				color: root.stateTone
				Accessible.ignored: true
			}

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2

				Label {
					text: root.stateLabel
					textFormat: Text.PlainText
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
				}

				Label {
					visible: String(recorderController.resolvedOutputPath || "").length > 0
					Layout.fillWidth: true
					text: recorderController.resolvedOutputPath
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideMiddle
				}
			}

			Label {
				objectName: "recorderElapsed"
				text: recorderController.elapsedText
				textFormat: Text.PlainText
				color: Theme.textStrong
				font.family: "monospace"
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				Accessible.name: qsTr("Elapsed %1").arg(text)
			}
		}
	}

	Rectangle {
		objectName: "recorderErrorBanner"
		Layout.fillWidth: true
		visible: String(recorderController.errorMessage || "").length > 0
		implicitHeight: errorLayout.implicitHeight + Theme.space3 * 2
		radius: Theme.innerRadius
		color: Theme.withAlpha(Theme.danger, 0.12)
		border.color: Theme.danger
		Accessible.role: Accessible.AlertMessage
		Accessible.name: recorderController.errorMessage

		RowLayout {
			id: errorLayout
			anchors.fill: parent
			anchors.margins: Theme.space3
			spacing: Theme.space2

			ModernIcon {
				Layout.preferredWidth: 18
				Layout.preferredHeight: 18
				name: "warning"
				color: Theme.danger
				Accessible.ignored: true
			}

			Label {
				Layout.fillWidth: true
				text: recorderController.errorMessage
				textFormat: Text.PlainText
				color: Theme.textStrong
				wrapMode: Text.Wrap
			}

			ModernButton {
				objectName: "recorderDismissError"
				dense: true
				text: qsTr("Dismiss")
				onClicked: recorderController.clearError()
			}
		}
	}

	ColumnLayout {
		Layout.fillWidth: true
		spacing: Theme.space2

		Label {
			text: qsTr("Output")
			textFormat: Text.PlainText
			color: Theme.textStrong
			font.pixelSize: Theme.fontTitle
			font.weight: Font.DemiBold
		}

		Label {
			text: qsTr("Target directory")
			textFormat: Text.PlainText
			color: Theme.textMain
			font.pixelSize: Theme.fontLabel
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space2

			ModernTextField {
				id: outputDirectoryField
				objectName: "recording.path"
				Layout.fillWidth: true
				enabled: recorderController.canEdit
				text: recorderController.outputDirectory
				invalid: root.fieldError("recording.path").length > 0
				placeholderText: qsTr("Choose where recordings are stored")
				cursorDelegate: recorderTextCursorDelegate
				Accessible.name: qsTr("Target directory")
				Accessible.description: root.fieldError("recording.path")
				onEditingFinished: recorderController.outputDirectory = text
			}

			ModernButton {
				objectName: "recorderBrowseButton"
				enabled: recorderController.canEdit
				text: qsTr("Browse")
				onClicked: root.browseRequested(outputDirectoryField.text)
			}
		}

		Label {
			objectName: "recording.path.error"
			Layout.fillWidth: true
			visible: root.fieldError("recording.path").length > 0
			text: root.fieldError("recording.path")
			textFormat: Text.PlainText
			color: Theme.danger
			font.pixelSize: Theme.fontCaption
			wrapMode: Text.Wrap
		}

		GridLayout {
			Layout.fillWidth: true
			columns: root.compactLayout ? 1 : 2
			columnSpacing: Theme.space3
			rowSpacing: Theme.space2

			ColumnLayout {
				Layout.fillWidth: true
				spacing: Theme.space1
				Label {
					text: qsTr("Filename")
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
				}
				ModernTextField {
					id: fileNameField
					objectName: "recording.file"
					Layout.fillWidth: true
					enabled: recorderController.canEdit
					text: recorderController.fileName
					placeholderText: "%user"
					cursorDelegate: recorderTextCursorDelegate
					Accessible.name: qsTr("Filename")
					onEditingFinished: recorderController.fileName = text
				}
			}

			ColumnLayout {
				Layout.fillWidth: true
				spacing: Theme.space1
				Label {
					text: qsTr("Format")
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
				}
				ModernComboBox {
					id: formatCombo
					objectName: "recording.format"
					Layout.fillWidth: true
					enabled: recorderController.canEdit
					model: recorderController.formatOptions
					textRole: "label"
					valueRole: "value"
					currentIndex: root.optionIndex(model, recorderController.format)
					invalid: root.fieldError("recording.format").length > 0
					Accessible.name: qsTr("Recording format")
					Accessible.description: root.fieldError("recording.format")
					onActivated: recorderController.format = Number(currentValue)
				}
			}
		}

		Label {
			text: qsTr("Recording mode")
			textFormat: Text.PlainText
			color: Theme.textMain
			font.pixelSize: Theme.fontLabel
		}

		ModernComboBox {
			id: modeCombo
			objectName: "recording.mode"
			Layout.fillWidth: true
			enabled: recorderController.canEdit
			model: recorderController.modeOptions
			textRole: "label"
			valueRole: "value"
			currentIndex: root.optionIndex(model, recorderController.mode)
			invalid: root.fieldError("recording.mode").length > 0
			Accessible.name: qsTr("Recording mode")
			Accessible.description: root.fieldError("recording.mode")
			onActivated: recorderController.mode = Number(currentValue)
		}
	}

	RowLayout {
		objectName: "recorderActions"
		Layout.fillWidth: true
		spacing: Theme.space2

		ModernBusyIndicator {
			objectName: "recorderOperationBusy"
			Layout.preferredWidth: 22
			Layout.preferredHeight: 22
			visible: recorderController.busy
			running: visible
		}

		Label {
			Layout.fillWidth: true
			visible: recorderController.busy
			text: recorderController.operationPhase === "stopping" ? qsTr("Finishing the recording…")
				: recorderController.operationPhase === "starting" ? qsTr("Starting the recorder…")
				: qsTr("Updating recorder…")
			textFormat: Text.PlainText
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			Accessible.role: Accessible.StatusBar
			Accessible.name: text
		}

		Item { Layout.fillWidth: !recorderController.busy }

		ModernButton {
			objectName: "recorderStartButton"
			visible: recorderController.state === "idle" || recorderController.state === "error"
			enabled: recorderController.canStart
			text: qsTr("Start recording")
			tone: "accent"
			onClicked: recorderController.start()
		}

		ModernButton {
			objectName: "recorderPauseButton"
			visible: recorderController.state === "recording"
			enabled: recorderController.canPause
			text: qsTr("Pause")
			onClicked: recorderController.pause()
		}

		ModernButton {
			objectName: "recorderResumeButton"
			visible: recorderController.state === "paused"
			enabled: recorderController.canResume
			text: qsTr("Resume")
			tone: "accent"
			onClicked: recorderController.resume()
		}

		ModernButton {
			objectName: "recorderStopButton"
			visible: recorderController.state === "recording" || recorderController.state === "paused"
				|| recorderController.state === "stopping"
			enabled: recorderController.canStop
			text: qsTr("Stop")
			tone: "danger"
			onClicked: recorderController.stop()
		}
	}
}
