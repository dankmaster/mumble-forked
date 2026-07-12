import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var shareState: ({})
    property string selectedSourceId: String(shareState.selectedSourceId || "")

    function selectedValue(control, fallback) {
        return control.currentIndex >= 0 ? control.currentValue : fallback
    }

    function actionPayload() {
        return {
            "channelId": Number(shareState.channelId ?? -1),
            "sourceId": selectedSourceId,
            "resolution": String(selectedValue(resolution, shareState.resolutionDefault || "")),
            "frameRate": Number(selectedValue(frameRate, shareState.frameRateDefault || 0)),
            "audio": String(selectedValue(audio, shareState.audioDefault || ""))
        }
    }

    onShareStateChanged: Qt.callLater(function() {
        resolution.currentIndex = resolution.indexOfValue(shareState.resolutionDefault)
        frameRate.currentIndex = frameRate.indexOfValue(shareState.frameRateDefault)
        audio.currentIndex = audio.indexOfValue(shareState.audioDefault)
    })

    spacing: 16

    Label {
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
                        required property var modelData
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
                                    source: modelData.thumbnail || ""
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                }
                            }
                            Label { Layout.fillWidth: true; text: modelData.title || ""; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                            Label { Layout.fillWidth: true; text: modelData.detail || ""; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: text.length > 0 }
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.selectedSourceId = String(modelData.id) }
                        Keys.onReturnPressed: event => { root.selectedSourceId = String(modelData.id); event.accepted = true }
                        Keys.onSpacePressed: event => { root.selectedSourceId = String(modelData.id); event.accepted = true }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: (modelData.items || []).length === 0
                text: modelData.emptyText || qsTr("No sources available")
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
            Label { text: qsTr("Resolution"); color: Theme.textMuted; font.pixelSize: 10 }
            ComboBox { id: resolution; Layout.fillWidth: true; model: root.shareState.resolutionOptions || []; textRole: "label"; valueRole: "value"; Component.onCompleted: currentIndex = indexOfValue(root.shareState.resolutionDefault) }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Frame rate"); color: Theme.textMuted; font.pixelSize: 10 }
            ComboBox { id: frameRate; Layout.fillWidth: true; model: root.shareState.frameRateOptions || []; textRole: "label"; valueRole: "value"; Component.onCompleted: currentIndex = indexOfValue(root.shareState.frameRateDefault) }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Audio"); color: Theme.textMuted; font.pixelSize: 10 }
            ComboBox { id: audio; Layout.fillWidth: true; model: root.shareState.audioOptions || []; textRole: "label"; valueRole: "value"; Component.onCompleted: currentIndex = indexOfValue(root.shareState.audioDefault) }
        }
    }

    Label { Layout.fillWidth: true; visible: text.length > 0; text: root.shareState.qualityNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
    Label { Layout.fillWidth: true; visible: text.length > 0; text: root.shareState.audioNote || ""; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 10 }
}
