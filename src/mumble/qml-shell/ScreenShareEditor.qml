import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var shareState: ({})
    property string selectedSourceId: String(shareState.selectedSourceId || "")
	readonly property string sourceError: String(shareState.sourceError || "").trim()
	readonly property bool runtimeProbePending: !!shareState.runtimeProbePending
	readonly property string runtimeError: String(shareState.runtimeError || "").trim()
	readonly property bool hasValidSelectedSource: sourceExists(selectedSourceId)
	property bool controlsInitialized: false
	property string selectedResolutionValue: ""
	property string selectedFrameRateValue: ""
	property string selectedAudioValue: ""
	// App-window discovery publishes audioAuto/processId when the helper can
	// capture that application's audio directly. Keep the convenience automatic
	// until the user deliberately chooses a different audio source.
	property bool audioSelectionExplicit: false
	signal thumbnailRequested(string sourceId)
	signal sourceSelected(string sourceId)

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

	function sourceExists(sourceId) {
		const normalized = String(sourceId || "").trim()
		if (normalized.length === 0)
			return false
		const sections = shareState.sources || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const items = sections[sectionIndex].items || []
			for (let itemIndex = 0; itemIndex < items.length; ++itemIndex) {
				if (String(items[itemIndex].id || "").trim() === normalized)
					return true
			}
		}
		return false
	}

	function sourceState(sourceId) {
		const normalized = String(sourceId || "").trim()
		const sections = shareState.sources || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const items = sections[sectionIndex].items || []
			for (let itemIndex = 0; itemIndex < items.length; ++itemIndex) {
				if (String(items[itemIndex].id || "").trim() === normalized)
					return items[itemIndex]
			}
		}
		return ({})
	}

	function applyAutomaticAudioForSource(sourceId) {
		if (audioSelectionExplicit)
			return false
		const source = sourceState(sourceId)
		const processId = Number(source.processId || 0)
		if (!source.audioAuto || !Number.isFinite(processId) || processId <= 0)
			return false
		const preferred = "process:" + String(Math.floor(processId))
		if (optionIndex(shareState.audioOptions || [], preferred) < 0)
			return false
		selectedAudioValue = preferred
		audio.currentIndex = optionIndex(shareState.audioOptions || [], preferred)
		return true
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

	onShareStateChanged: {
		if (!sourceExists(selectedSourceId))
			selectedSourceId = String(shareState.selectedSourceId || "")
		Qt.callLater(synchronizeControls)
	}

	spacing: Theme.space4

    Label {
		textFormat: Text.PlainText
        Layout.fillWidth: true
        text: qsTr("Choose what to share")
        color: Theme.textStrong
        font.bold: true
		font.pixelSize: Theme.fontTitle
    }

	Rectangle {
		id: runtimeStatus
		objectName: "screenShareRuntimeStatus"
		Layout.fillWidth: true
		Layout.preferredHeight: runtimeStatusRow.implicitHeight + Theme.space4
		visible: root.runtimeProbePending || root.runtimeError.length > 0
		radius: Theme.innerRadius
		color: root.runtimeError.length > 0
			? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12) : Theme.accentSubtle
		border.color: root.runtimeError.length > 0 ? Theme.danger : Theme.accent
		border.width: 1
		Accessible.role: root.runtimeError.length > 0 ? Accessible.AlertMessage : Accessible.ProgressBar
		Accessible.name: runtimeStatusLabel.text

		RowLayout {
			id: runtimeStatusRow
			anchors.fill: parent
			anchors.margins: Theme.space2
			spacing: Theme.space2

			ModernBusyIndicator {
				objectName: "screenShareRuntimeBusy"
				Layout.preferredWidth: Theme.rowHeight - Theme.space2
				Layout.preferredHeight: Layout.preferredWidth
				visible: root.runtimeProbePending
				running: visible
				Accessible.ignored: true
			}

			ModernIcon {
				Layout.preferredWidth: Theme.iconSize
				Layout.preferredHeight: Theme.iconSize
				visible: root.runtimeError.length > 0
				name: "warning"
				color: Theme.danger
				Accessible.ignored: true
			}

			Label {
				id: runtimeStatusLabel
				objectName: "screenShareRuntimeStatusLabel"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.runtimeProbePending
					? qsTr("Checking the local screen-share runtime…") : root.runtimeError
				color: root.runtimeError.length > 0 ? Theme.danger : Theme.textStrong
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}
	}

	Label {
		objectName: "screenShareSourceError"
		Layout.fillWidth: true
		visible: root.sourceError.length > 0
		textFormat: Text.PlainText
		text: root.sourceError
		color: Theme.danger
		wrapMode: Text.Wrap
		Accessible.role: Accessible.AlertMessage
		Accessible.name: text
	}

    Repeater {
        model: root.shareState.sources || []
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
			spacing: Theme.space2
            Label {
				textFormat: Text.PlainText
                text: modelData.section || ""
                color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
                font.bold: true
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 680 ? 3 : 2
				columnSpacing: Theme.space2
				rowSpacing: Theme.space2
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
							root.applyAutomaticAudioForSource(sourceId)
							requestThumbnailIfNeeded()
							// Keep native source validation as the final semantic result of
							// the user's selection. Thumbnail hydration is opportunistic and
							// must not obscure the selection intent for automation or focus
							// restoration when both signals are delivered synchronously.
							root.sourceSelected(sourceId)
						}
                        Layout.fillWidth: true
						Layout.preferredHeight: Theme.rowHeight * 3
                        radius: Theme.innerRadius
						color: sourceHover.pressed ? Theme.accentSubtle
							: root.selectedSourceId === sourceId ? Theme.selected
							: sourceHover.containsMouse ? Theme.surfaceHover : Theme.strip
						border.color: activeFocus ? Theme.focus
							: root.selectedSourceId === sourceId ? Theme.accent : Theme.divider
						border.width: activeFocus ? Theme.focusRingWidth : 1
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
							anchors.margins: Theme.space2
							spacing: Theme.space1
							Rectangle {
								objectName: "screenShareSourceThumbnail_" + sourceTile.sourceId
								Layout.fillWidth: true
								Layout.fillHeight: true
								color: Theme.mediaCanvas
								radius: Theme.space1
								clip: true
								Accessible.ignored: true
                                Image {
                                    anchors.fill: parent
                                    source: root.safeThumbnailSource(modelData.thumbnail)
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
									Accessible.ignored: true
                                }
                            }
							Label {
								objectName: "screenShareSourceTitle_" + sourceTile.sourceId
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: modelData.title || ""
								color: Theme.textStrong
								font.pixelSize: Theme.fontLabel
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "screenShareSourceDetail_" + sourceTile.sourceId
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: modelData.detail || ""
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								visible: text.length > 0
								Accessible.ignored: true
							}
                        }
						MouseArea {
							id: sourceHover
							objectName: "screenShareSourceHitArea_" + sourceTile.sourceId
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
				onActivated: {
					root.selectedAudioValue = String(currentValue)
					root.audioSelectionExplicit = true
				}
			}
        }
    }

    Label { Layout.fillWidth: true; textFormat: Text.PlainText; visible: text.length > 0; text: root.shareState.qualityNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
    Label { Layout.fillWidth: true; textFormat: Text.PlainText; visible: text.length > 0; text: root.shareState.audioNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
}
