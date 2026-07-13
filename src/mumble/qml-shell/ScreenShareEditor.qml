import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var shareState: ({})
    property string selectedSourceId: String(shareState.selectedSourceId || "")
	property bool controlsInitialized: false
	property string selectedResolutionValue: ""
	property string selectedFrameRateValue: ""
	property string selectedAudioValue: ""
	signal thumbnailRequested(string sourceId)

	function optionIndex(options, value) {
		const target = String(value ?? "")
		for (let index = 0; index < (options || []).length; ++index) {
			if (String(options[index].value ?? "") === target)
				return index
		}
		return -1
	}

    function safeThumbnailSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
        return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
    }

    function actionPayload() {
        return {
            "channelId": String(shareState.channelId ?? ""),
            "sourceId": selectedSourceId,
			"resolution": selectedResolutionValue,
			"frameRate": Number(selectedFrameRateValue || 0),
			"audio": selectedAudioValue
        }
    }

	function synchronizeControls() {
		const resolutions = shareState.resolutionOptions || []
		const frameRates = shareState.frameRateOptions || []
		const audioOptions = shareState.audioOptions || []
		if (!controlsInitialized) {
			selectedResolutionValue = String(shareState.resolutionDefault || "")
			selectedFrameRateValue = String(shareState.frameRateDefault || "")
			selectedAudioValue = String(shareState.audioDefault || "")
			controlsInitialized = true
		}
		if (optionIndex(resolutions, selectedResolutionValue) < 0)
			selectedResolutionValue = String(shareState.resolutionDefault || "")
		if (optionIndex(frameRates, selectedFrameRateValue) < 0)
			selectedFrameRateValue = String(shareState.frameRateDefault || "")
		if (optionIndex(audioOptions, selectedAudioValue) < 0)
			selectedAudioValue = String(shareState.audioDefault || "")
		resolution.currentIndex = optionIndex(resolutions, selectedResolutionValue)
		frameRate.currentIndex = optionIndex(frameRates, selectedFrameRateValue)
		audio.currentIndex = optionIndex(audioOptions, selectedAudioValue)
	}

	onShareStateChanged: Qt.callLater(synchronizeControls)

    spacing: 16

    Label {
		textFormat: Text.PlainText
        Layout.fillWidth: true
        text: qsTr("Choose what to share")
        color: Theme.textStrong
        font.bold: true
        font.pixelSize: 14
    }

    Repeater {
        model: root.shareState.sources || []
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Label {
				textFormat: Text.PlainText
                text: modelData.section || ""
                color: Theme.textMuted
                font.pixelSize: 10
                font.bold: true
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 680 ? 3 : 2
                columnSpacing: 10
                rowSpacing: 10
                Repeater {
                    model: modelData.items || []
                    delegate: Rectangle {
						id: sourceTile
                        required property var modelData
						objectName: "screenShareSource_" + String(modelData.id || "")
						readonly property string sourceId: String(modelData.id || "")
						property bool thumbnailRequestSent: false
						property int thumbnailRequestAttempts: 0
						function requestThumbnailIfNeeded() {
							if (!thumbnailRequestSent && thumbnailRequestAttempts < 4 && sourceId.length > 0
									&& String(modelData.thumbnail || "").length === 0) {
								thumbnailRequestSent = true
								++thumbnailRequestAttempts
								root.thumbnailRequested(sourceId)
								thumbnailRetry.restart()
							}
						}
						function selectSource() {
							root.selectedSourceId = sourceId
							requestThumbnailIfNeeded()
						}
                        Layout.fillWidth: true
                        Layout.preferredHeight: 132
                        radius: Theme.innerRadius
                        color: root.selectedSourceId === String(modelData.id) ? Theme.selected : Theme.strip
                        border.color: root.selectedSourceId === String(modelData.id) ? Theme.accent : Theme.divider
                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: String(modelData.title || qsTr("Share source"))
                        Accessible.description: String(modelData.detail || "")
                        Accessible.checked: root.selectedSourceId === String(modelData.id)
						Accessible.onPressAction: selectSource()
						onActiveFocusChanged: if (activeFocus) requestThumbnailIfNeeded()
						onSourceIdChanged: {
							thumbnailRequestSent = false
							thumbnailRequestAttempts = 0
							thumbnailRetry.stop()
							if (root.selectedSourceId === sourceId) Qt.callLater(requestThumbnailIfNeeded)
						}
						onModelDataChanged: {
							if (String(modelData.thumbnail || "").length > 0)
								thumbnailRetry.stop()
						}
						Component.onCompleted: if (root.selectedSourceId === sourceId)
							Qt.callLater(requestThumbnailIfNeeded)
						Timer {
							id: thumbnailRetry
							interval: 600 * Math.max(1, sourceTile.thumbnailRequestAttempts)
							repeat: false
							onTriggered: {
								if (String(sourceTile.modelData.thumbnail || "").length > 0)
									return
								// Only the selected/focused source retries a bounded request.
								// Hover-only rows get one request, preventing large grids from
								// turning rejected work into a recurring request storm.
								if (root.selectedSourceId === sourceTile.sourceId || sourceTile.activeFocus) {
									sourceTile.thumbnailRequestSent = false
									sourceTile.requestThumbnailIfNeeded()
								}
							}
						}

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 5
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#05070a"
                                radius: 5
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: root.safeThumbnailSource(modelData.thumbnail)
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
                                }
                            }
                            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.title || ""; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.detail || ""; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: text.length > 0 }
                        }
						MouseArea {
							anchors.fill: parent
							hoverEnabled: true
							onEntered: parent.requestThumbnailIfNeeded()
							onClicked: {
								parent.selectSource()
							}
						}
						Keys.onReturnPressed: event => { selectSource(); event.accepted = true }
						Keys.onSpacePressed: event => { selectSource(); event.accepted = true }
                    }
                }
            }
            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: (modelData.items || []).length === 0
				text: modelData.loading ? qsTr("Finding sources…")
					: (modelData.emptyText || qsTr("No sources available"))
                color: Theme.textMuted
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width >= 620 ? 3 : 1
        columnSpacing: 12
        rowSpacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            Label { textFormat: Text.PlainText; text: qsTr("Resolution"); color: Theme.textMuted; font.pixelSize: 10 }
			ModernComboBox {
				id: resolution
				objectName: "screenShareResolution"
				Layout.fillWidth: true
				model: root.shareState.resolutionOptions || []
				textRole: "label"
				valueRole: "value"
				onActivated: root.selectedResolutionValue = String(currentValue)
			}
        }
        ColumnLayout {
            Layout.fillWidth: true
            Label { textFormat: Text.PlainText; text: qsTr("Frame rate"); color: Theme.textMuted; font.pixelSize: 10 }
			ModernComboBox {
				id: frameRate
				objectName: "screenShareFrameRate"
				Layout.fillWidth: true
				model: root.shareState.frameRateOptions || []
				textRole: "label"
				valueRole: "value"
				onActivated: root.selectedFrameRateValue = String(currentValue)
			}
        }
        ColumnLayout {
            Layout.fillWidth: true
            Label { textFormat: Text.PlainText; text: qsTr("Audio"); color: Theme.textMuted; font.pixelSize: 10 }
			ModernComboBox {
				id: audio
				objectName: "screenShareAudio"
				Layout.fillWidth: true
				model: root.shareState.audioOptions || []
				textRole: "label"
				valueRole: "value"
				onActivated: root.selectedAudioValue = String(currentValue)
			}
        }
    }

    Label { Layout.fillWidth: true; textFormat: Text.PlainText; visible: text.length > 0; text: root.shareState.qualityNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
    Label { Layout.fillWidth: true; textFormat: Text.PlainText; visible: text.length > 0; text: root.shareState.audioNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
}
