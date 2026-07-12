import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var field
    width: parent ? parent.width : 0
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: field.label || qsTr("Shortcuts"); color: Theme.textStrong; font.bold: true }
        ModernButton { text: qsTr("Add shortcut"); enabled: !!field.canCapture; onClicked: dialogState.invokeAction("keys.addShortcut", {}) }
    }

    ListView {
        id: shortcutList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 460)
        implicitHeight: Layout.preferredHeight
        model: field.rows || []
        clip: true
        spacing: 8
        delegate: Rectangle {
            required property var modelData
            width: shortcutList.width
            height: shortcutRow.implicitHeight + 22
            radius: Theme.innerRadius
            color: Theme.strip
            border.color: modelData.capturing ? Theme.accent : Theme.divider
            ColumnLayout {
                id: shortcutRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 11
                spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        Layout.fillWidth: true
                        model: root.field.actionOptions || []
                        textRole: "label"
                        Component.onCompleted: {
                            for (let i = 0; i < count; ++i) {
                                if (model[i].value === modelData.actionIndex) { currentIndex = i; break }
                            }
                        }
                        onActivated: dialogState.invokeAction("keys.shortcutAction",
                            { "index": modelData.index, "actionIndex": model[currentIndex].value })
                    }
                    Label { text: modelData.dataLabel || ""; color: Theme.textMuted; font.pixelSize: 10; visible: text.length > 0 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: modelData.capturing ? qsTr("Press the shortcut now…")
                                                  : (modelData.inputLabel || qsTr("Not assigned"))
                        color: modelData.assigned ? Theme.textMain : Theme.textMuted
                    }
                    CheckBox {
                        text: qsTr("Suppress")
                        visible: !!root.field.canSuppress
                        checked: !!modelData.suppress
                        onToggled: dialogState.invokeAction("keys.shortcutSuppress",
                            { "index": modelData.index, "value": checked })
                    }
                    ModernButton {
                        text: modelData.capturing ? qsTr("Cancel") : qsTr("Capture")
                        enabled: !!root.field.canCapture
                        onClicked: dialogState.invokeAction(modelData.capturing
                            ? "keys.cancelShortcutCapture" : "keys.beginShortcutCapture",
                            { "index": modelData.index })
                    }
                    ModernButton {
                        text: qsTr("Clear")
                        enabled: !!modelData.assigned
                        onClicked: dialogState.invokeAction("keys.clearShortcut", { "index": modelData.index })
                    }
                    ModernButton {
                        text: qsTr("Remove")
                        onClicked: dialogState.invokeAction("keys.removeShortcut", { "index": modelData.index })
                    }
                }
            }
        }
    }
}
