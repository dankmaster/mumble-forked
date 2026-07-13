import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root
    property var state: ({})
    signal actionRequested(string actionId)

    readonly property string tone: String(state.tone || "accent")
    readonly property color toneColor: tone === "danger" || tone === "error" ? "#ef4444"
                                       : tone === "warning" ? "#f59e0b"
                                       : tone === "success" ? "#34d399"
                                       : Theme.accent
    readonly property var actions: state.actions || []

    visible: !!state.visible
    implicitHeight: visible ? content.implicitHeight + 24 : 0
    color: Theme.panel
    border.color: toneColor
    Accessible.role: Accessible.AlertMessage
    Accessible.name: String(state.title || qsTr("Update"))
    Accessible.description: String(state.detail || "")

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            radius: 2
            color: root.toneColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
                text: String(root.state.title || qsTr("Update"))
                color: Theme.textStrong
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
				textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: text.length > 0
                text: String(root.state.detail || "")
                color: Theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: !!root.state.progressVisible
                indeterminate: !!root.state.progressIndeterminate
                from: 0
                to: 100
                value: Number(root.state.progressPercent || 0)
                Accessible.name: String(root.state.progressLabel || qsTr("Update progress"))
            }
        }

        RowLayout {
            spacing: 8
            Repeater {
                model: root.actions
                delegate: ModernButton {
                    required property var modelData
                    text: String(modelData.label || modelData.title || qsTr("Open"))
                    enabled: modelData.enabled === undefined || !!modelData.enabled
                    Accessible.name: text
                    onClicked: {
                        const actionId = String(modelData.id || modelData.actionId || "")
                        if (actionId.length > 0)
                            root.actionRequested(actionId)
                    }
                }
            }
        }
    }
}
