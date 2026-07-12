import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Dialog {
    id: dialog
    parent: Overlay.overlay
    visible: dialogState.open
    modal: true
    focus: true
    width: Math.min(parent ? parent.width - 48 : 920, 1040)
    height: Math.min(parent ? parent.height - 48 : 700, 760)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        color: Theme.shellBackground
        border.color: Theme.divider
        radius: Theme.shellRadius
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: dialogState.title; color: Theme.textStrong; font.pixelSize: 19; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: dialogState.subtitle
                        visible: text.length > 0
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                ToolButton {
                    text: "×"
                    font.pixelSize: 20
                    Accessible.name: qsTr("Close dialog")
                    onClicked: dialogState.requestClose()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: visible ? 190 : 0
                Layout.fillHeight: true
                visible: dialogState.pages.length > 0
                color: Theme.rail
                border.color: Theme.divider
                ListView {
                    anchors.fill: parent
                    anchors.margins: 10
                    model: dialogState.pages
                    clip: true
                    spacing: 3
                    delegate: ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        height: 40
                        text: modelData.label || modelData.title || modelData.id
                        highlighted: modelData.selected || modelData.id === dialogState.activePage
                        onClicked: dialogState.invokeAction("selectPage", { "pageId": modelData.id })
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                Column {
                    width: parent.width
                    spacing: 16
                    Repeater {
                        model: dialogState.sections
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width - 36
                            x: 18
                            height: sectionColumn.implicitHeight + 28
                            color: Theme.panel
                            border.color: Theme.divider
                            radius: Theme.innerRadius
                            Column {
                                id: sectionColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 14
                                spacing: 10
                                Label {
                                    width: parent.width
                                    text: modelData.title || ""
                                    visible: text.length > 0
                                    color: Theme.textStrong
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                Repeater {
                                    model: modelData.fields || []
                                    delegate: Loader {
                                        id: fieldLoader
                                        required property var modelData
                                        width: sectionColumn.width
                                        property var field: modelData
                                        property bool conditionVisible: !field.visibleWhen
                                            || (field.visibleWhen.values || []).indexOf(
                                                String(dialogState.fieldValue(field.visibleWhen.fieldId))) >= 0
                                        visible: conditionVisible
                                        active: conditionVisible
                                        height: conditionVisible ? ((item ? item.implicitHeight : 0)
                                                + (fieldErrorLabel.visible ? fieldErrorLabel.implicitHeight + 4 : 0)) : 0
                                        onLoaded: item.field = field
                                        onFieldChanged: if (item) item.field = field
                                        sourceComponent: {
                                            const type = field.type || "text"
                                            if (type === "hidden") return hiddenField
                                            if (type === "note") return noteField
                                            if (type === "readonly" || type === "status" || type === "voiceMeter") return readonlyField
                                            if (type === "checkbox" || type === "toggle") return checkboxField
                                            if (type === "select" || type === "combo" || type === "dropdown") return selectField
                                            if (type === "slider" || type === "range" || type === "number" || type === "integer") return numberField
                                            if (type === "action" || type === "button") return actionField
                                            if (type === "pluginEditor") return pluginEditorField
                                            if (type === "messageEventEditor") return messageEventEditorField
                                            if (type === "shortcutEditor") return shortcutEditorField
                                            if (type === "aclEditor") return aclEditorField
                                            if (type === "textarea") return textareaField
                                            if (type === "resultList") return resultListField
                                            if (type === "profile") return profileField
                                            if (type === "imagePicker") return pathField
                                            if (type === "manualPositionPreview") return manualPreviewField
                                            if (type === "pathPicker" || type === "filePicker" || type === "folderPicker") return pathField
                                            return textField
                                        }
                                        Label {
                                            id: fieldErrorLabel
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: fieldLoader.item ? fieldLoader.item.bottom : parent.top
                                            anchors.topMargin: 4
                                            text: dialogState.fieldError(fieldLoader.field.id)
                                            visible: text.length > 0
                                            color: "#f87171"
                                            font.pixelSize: 10
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.strip
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                Item { Layout.fillWidth: true }
                Repeater {
                    model: dialogState.actions
                    delegate: ModernButton {
                        required property var modelData
                        text: modelData.label || modelData.text || modelData.id
                        enabled: modelData.enabled === undefined || modelData.enabled
                        onClicked: dialogState.invokeAction(modelData.id, {})
                    }
                }
            }
        }
    }

    Component { id: hiddenField; Item { property var field; width: parent ? parent.width : 0; height: 0 } }
    Component {
        id: noteField
        Label {
            property var field
            width: parent ? parent.width : 0
            text: field.text || field.label || ""
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
    Component {
        id: readonlyField
        ColumnLayout {
            id: readonlyRoot
            property var field
            width: parent ? parent.width : 0
            Label { text: readonlyRoot.field.label || ""; visible: text.length > 0; color: Theme.textMuted; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; text: String(readonlyRoot.field.value ?? ""); color: Theme.textMain; wrapMode: Text.Wrap }
        }
    }
    Component {
        id: checkboxField
        CheckBox {
            property var field
            width: parent ? parent.width : 0
            text: field.label || ""
            checked: !!field.value
            enabled: field.enabled === undefined || field.enabled
            contentItem: Label {
                text: parent.text
                color: parent.enabled ? Theme.textMain : Theme.textMuted
                leftPadding: parent.indicator.width + parent.spacing
                verticalAlignment: Text.AlignVCenter
            }
            onToggled: dialogState.updateField(field.id, checked)
        }
    }
    Component {
        id: selectField
        ColumnLayout {
            id: selectRoot
            property var field
            function syncCurrentIndex() {
                Qt.callLater(function() {
                    const index = selectControl.indexOfValue(field.value)
                    selectControl.currentIndex = index
                })
            }
            onFieldChanged: syncCurrentIndex()
            width: parent ? parent.width : 0
            Label { text: selectRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
            ComboBox {
                id: selectControl
                Layout.fillWidth: true
                model: selectRoot.field.options || []
                textRole: "label"
                valueRole: "value"
                Component.onCompleted: selectRoot.syncCurrentIndex()
                onModelChanged: selectRoot.syncCurrentIndex()
                onActivated: if (currentIndex >= 0) dialogState.updateField(selectRoot.field.id, currentValue)
            }
        }
    }
    Component {
        id: numberField
        ColumnLayout {
            id: numberRoot
            property var field
            width: parent ? parent.width : 0
            Label { text: numberRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
            SpinBox {
                from: numberRoot.field.minimum ?? numberRoot.field.min ?? -100000
                to: numberRoot.field.maximum ?? numberRoot.field.max ?? 100000
                value: Number(numberRoot.field.value || 0)
                editable: true
                onValueModified: dialogState.updateField(numberRoot.field.id, value)
            }
        }
    }
    Component {
        id: textField
        ColumnLayout {
            id: textRoot
            property var field
            width: parent ? parent.width : 0
            Label { text: textRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
            TextField {
                Layout.fillWidth: true
                text: String(textRoot.field.value ?? "")
                enabled: textRoot.field.enabled === undefined || textRoot.field.enabled
                echoMode: textRoot.field.type === "password" ? TextInput.Password : TextInput.Normal
                onEditingFinished: dialogState.updateField(textRoot.field.id, text)
            }
        }
    }
    Component {
        id: pathField
        ColumnLayout {
            id: pathRoot
            property var field
            width: parent ? parent.width : 0
            Label { text: pathRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
            RowLayout {
                TextField {
                    Layout.fillWidth: true
                    text: String(pathRoot.field.value ?? "")
                    onEditingFinished: dialogState.updateField(pathRoot.field.id, text)
                }
                ModernButton {
                    text: pathRoot.field.browseLabel || qsTr("Browse…")
                    onClicked: dialogState.invokeAction(pathRoot.field.browseActionId, { "fieldId": pathRoot.field.id })
                }
            }
        }
    }
    Component {
        id: manualPreviewField
        Rectangle {
            property var field
            width: parent ? parent.width : 0
            height: 170
            implicitHeight: 170
            color: Theme.strip
            radius: Theme.innerRadius
            Canvas {
                anchors.fill: parent
                anchors.margins: 12
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset();
                    ctx.strokeStyle = Theme.divider; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(width / 2, 0); ctx.lineTo(width / 2, height)
                    ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                    const x = Number(dialogState.fieldValue("manual.x") || 0)
                    const z = Number(dialogState.fieldValue("manual.z") || 0)
                    const scale = Math.min(width, height) / 20
                    ctx.fillStyle = Theme.accent; ctx.beginPath()
                    ctx.arc(width / 2 + x * scale, height / 2 - z * scale, 7, 0, Math.PI * 2); ctx.fill()
                }
                Connections { target: dialogState; function onStateChanged() { parent.requestPaint() } }
            }
            Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; text: qsTr("Top view · X / Z"); color: Theme.textMuted; font.pixelSize: 9 }
        }
    }
    Component {
        id: profileField
        RowLayout {
            property var field
            width: parent ? parent.width : 0
            property var profile: field.value || ({})
            Rectangle {
                Layout.preferredWidth: 56; Layout.preferredHeight: 56; radius: 28; color: Theme.strip; clip: true
                Image { anchors.fill: parent; source: parent.parent.profile.avatarUrl || ""; asynchronous: true; fillMode: Image.PreserveAspectCrop }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: parent.parent.profile.name || ""; color: Theme.textStrong; font.bold: true }
                Label { Layout.fillWidth: true; text: parent.parent.profile.subtitle || ""; color: Theme.textMuted; elide: Text.ElideRight }
            }
            ModernButton {
                visible: (parent.profile.avatarActionId || "").length > 0
                text: parent.profile.avatarActionLabel || qsTr("Change avatar")
                onClicked: dialogState.invokeAction(parent.profile.avatarActionId, {})
            }
        }
    }
    Component {
        id: resultListField
        ColumnLayout {
            property var field
            width: parent ? parent.width : 0
            Label { text: parent.field.label || ""; color: Theme.textStrong; font.bold: true; visible: text.length > 0 }
            Repeater {
                model: parent.field.items || parent.field.value || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true; height: 46; radius: 6; color: Theme.strip
                    Column { anchors.fill: parent; anchors.margins: 7
                        Label { width: parent.width; text: modelData.label || modelData.title || modelData.name || ""; color: Theme.textMain; elide: Text.ElideRight }
                        Label { width: parent.width; text: modelData.subtitle || modelData.description || ""; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight }
                    }
                }
            }
            Label { text: parent.field.emptyText || ""; visible: (parent.field.items || []).length === 0; color: Theme.textMuted; wrapMode: Text.Wrap }
        }
    }
    Component {
        id: textareaField
        ColumnLayout {
            id: textareaRoot
            property var field
            width: parent ? parent.width : 0
            Label { text: textareaRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(90, (textareaRoot.field.rows || 4) * 22)
                text: String(textareaRoot.field.value ?? "")
                enabled: textareaRoot.field.enabled === undefined || textareaRoot.field.enabled
                wrapMode: TextEdit.Wrap
                onActiveFocusChanged: if (!activeFocus) dialogState.updateField(textareaRoot.field.id, text)
            }
        }
    }
    Component {
        id: aclEditorField
        AclEditor { }
    }
    Component {
        id: shortcutEditorField
        ShortcutEditor { }
    }
    Component {
        id: messageEventEditorField
        MessageEventEditor { }
    }
    Component {
        id: pluginEditorField
        PluginEditor { }
    }
    Component {
        id: actionField
        ModernButton {
            property var field
            width: parent ? parent.width : implicitWidth
            text: field.buttonLabel || field.label || field.text || field.id
            enabled: field.enabled === undefined || field.enabled
            onClicked: dialogState.invokeAction(field.actionId || field.id, field.payload || {})
        }
    }
}
