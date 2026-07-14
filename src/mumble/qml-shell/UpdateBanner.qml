import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
    id: root
    property var state: ({})
    signal actionRequested(string actionId)

    readonly property string tone: String(state.tone || "accent")
    readonly property color toneColor: tone === "danger" || tone === "error" ? Theme.danger
                                       : tone === "warning" ? Theme.warning
                                       : tone === "success" ? Theme.success
                                       : Theme.accent
    readonly property var actions: state.actions || []
    readonly property bool compactLayout: width < 560
    readonly property bool actionsWrapped: actionFlow.implicitHeight > Theme.controlHeight + 1
    readonly property real contentAvailableWidth: Math.max(1,
        width - Theme.space4 * 2 - Theme.space1 - Theme.space3)

    objectName: "updateBanner"
    visible: !!state.visible
    implicitHeight: visible ? content.implicitHeight + Theme.space3 * 2 : 0
    color: Theme.panel
    border.color: toneColor
    Accessible.role: Accessible.AlertMessage
    Accessible.name: String(state.title || qsTr("Update"))
    Accessible.description: String(state.detail || "")

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space4
        anchors.topMargin: Theme.space3
        anchors.bottomMargin: Theme.space3
        spacing: Theme.space3

        Rectangle {
            Layout.preferredWidth: Theme.space1
            Layout.fillHeight: true
            radius: 2
            color: root.toneColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.contentAvailableWidth
            spacing: Theme.space2
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
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            ModernProgressBar {
                Layout.fillWidth: true
                visible: !!root.state.progressVisible
                indeterminate: !!root.state.progressIndeterminate
                from: 0
                to: 100
                value: Number(root.state.progressPercent || 0)
                Accessible.name: String(root.state.progressLabel || qsTr("Update progress"))
            }
            Flow {
                id: actionFlow
                objectName: "updateBannerActionFlow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: root.contentAvailableWidth
                Layout.preferredHeight: visible ? implicitHeight : 0
                visible: root.actions.length > 0
                spacing: Theme.space2

                Repeater {
                    id: actionRepeater
                    model: root.actions
                    delegate: ModernButton {
                        required property var modelData
                        required property int index
                        readonly property string fullLabel: String(modelData.label
                            || modelData.title || qsTr("Open"))
                        objectName: "updateAction_" + String(modelData.id || modelData.actionId || index)
                        width: Math.max(1, Math.min(implicitWidth, actionFlow.width))
                        dense: root.compactLayout
                        text: fullLabel
                        enabled: modelData.enabled === undefined || !!modelData.enabled
                        Accessible.name: fullLabel
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
}
