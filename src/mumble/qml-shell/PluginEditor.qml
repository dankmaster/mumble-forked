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
        Label {
            Layout.fillWidth: true
            text: field.label || qsTr("Installed plugins")
            color: Theme.textStrong
            font.bold: true
        }
        ModernButton { text: qsTr("Install…"); onClicked: dialogState.invokeAction("plugins.install", {}) }
        ModernButton { text: qsTr("Rescan"); onClicked: dialogState.invokeAction("plugins.rescan", {}) }
        ModernButton { text: qsTr("Check updates"); onClicked: dialogState.invokeAction("plugins.checkUpdates", {}) }
    }

    ListView {
        id: pluginList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 430)
        implicitHeight: Layout.preferredHeight
        model: field.rows || []
        clip: true
        spacing: 8
        delegate: Rectangle {
            required property var modelData
            width: pluginList.width
            height: pluginColumn.implicitHeight + 24
            radius: Theme.innerRadius
            color: Theme.strip
            border.color: Theme.divider
            ColumnLayout {
                id: pluginColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Layout.fillWidth: true; text: modelData.name || qsTr("Unnamed plugin"); color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: modelData.description || modelData.path || ""; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                    }
                    Label { text: modelData.version || ""; color: Theme.textMuted; font.pixelSize: 10 }
                    Label {
                        text: modelData.loaded ? qsTr("Loaded") : qsTr("Unloaded")
                        color: modelData.loaded ? Theme.accent : Theme.textMuted
                        font.pixelSize: 10
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox {
                        text: qsTr("Enabled")
                        checked: !!modelData.enabled
                        onToggled: dialogState.invokeAction("plugins.toggle",
                            { "pluginId": modelData.id, "property": "enabled", "value": checked })
                    }
                    CheckBox {
                        text: qsTr("Positional audio")
                        visible: !!modelData.positionalAvailable
                        checked: !!modelData.positionalEnabled
                        onToggled: dialogState.invokeAction("plugins.toggle",
                            { "pluginId": modelData.id, "property": "positional", "value": checked })
                    }
                    CheckBox {
                        text: qsTr("Keyboard monitoring")
                        checked: !!modelData.keyboardMonitoringAllowed
                        onToggled: dialogState.invokeAction("plugins.toggle",
                            { "pluginId": modelData.id, "property": "keyboard", "value": checked })
                    }
                    ModernButton {
                        text: qsTr("Configure")
                        visible: !!modelData.canConfigure
                        onClicked: dialogState.invokeAction("plugins.configure", { "pluginId": modelData.id })
                    }
                    ModernButton {
                        text: qsTr("About")
                        visible: !!modelData.canShowAbout
                        onClicked: dialogState.invokeAction("plugins.about", { "pluginId": modelData.id })
                    }
                    ModernButton {
                        text: qsTr("Unload")
                        visible: !!modelData.loaded && !modelData.builtIn
                        onClicked: dialogState.invokeAction("plugins.unload", { "pluginId": modelData.id })
                    }
                }
            }
        }
    }
}
