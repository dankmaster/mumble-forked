import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var field
    property var aclModel: field.value || ({})
    width: parent ? parent.width : 0
    spacing: 12

    function cloneModel() { return JSON.parse(JSON.stringify(aclModel || {})) }
    function publish(model) { aclModel = model; dialogState.updateField(field.id, model) }
    function contains(list, value) { return (list || []).indexOf(value) >= 0 }
    function togglePermission(ruleIndex, key, permissionId, checked) {
        const model = cloneModel(); const rule = model.acls[ruleIndex]; let values = rule[key] || []
        const index = values.indexOf(permissionId)
        if (checked && index < 0) values.push(permissionId)
        if (!checked && index >= 0) values.splice(index, 1)
        rule[key] = values; publish(model)
    }

    RowLayout {
        Layout.fillWidth: true
        CheckBox {
            text: qsTr("Inherit ACLs from parent room")
            checked: !!root.aclModel.inheritAcls
            onToggled: { const model = root.cloneModel(); model.inheritAcls = checked; root.publish(model) }
        }
        Item { Layout.fillWidth: true }
            Label { textFormat: Text.PlainText; text: qsTr("Room password"); color: Theme.textMuted; font.pixelSize: 10 }
        TextField {
            text: root.aclModel.password || ""
            echoMode: TextInput.Password
            onEditingFinished: { const model = root.cloneModel(); model.password = text; root.publish(model) }
        }
    }

            Label { textFormat: Text.PlainText; text: qsTr("Groups"); color: Theme.textStrong; font.bold: true }
    Repeater {
        model: root.aclModel.groups || []
        delegate: Rectangle {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            height: groupRow.implicitHeight + 16
            color: Theme.strip
            radius: 6
            RowLayout {
                id: groupRow
                anchors.fill: parent
                anchors.margins: 8
                TextField {
                    Layout.preferredWidth: 150
                    text: modelData.name || ""
                    enabled: !modelData.inherited
                    placeholderText: qsTr("Group name")
                    onEditingFinished: { const model = root.cloneModel(); model.groups[index].name = text; root.publish(model) }
                }
                CheckBox {
                    text: qsTr("Inherit")
                    checked: !!modelData.inherit
                    enabled: !modelData.inherited
                    onToggled: { const model = root.cloneModel(); model.groups[index].inherit = checked; root.publish(model) }
                }
                CheckBox {
                    text: qsTr("Inheritable")
                    checked: !!modelData.inheritable
                    enabled: !modelData.inherited
                    onToggled: { const model = root.cloneModel(); model.groups[index].inheritable = checked; root.publish(model) }
                }
                TextField {
                    Layout.fillWidth: true
                    text: (modelData.add || []).join(", ")
                    enabled: !modelData.inherited
                    placeholderText: qsTr("Added user IDs")
                    onEditingFinished: { const model = root.cloneModel(); model.groups[index].addText = text; root.publish(model) }
                }
                TextField {
                    Layout.fillWidth: true
                    text: (modelData.remove || []).join(", ")
                    enabled: !modelData.inherited
                    placeholderText: qsTr("Removed user IDs")
                    onEditingFinished: { const model = root.cloneModel(); model.groups[index].removeText = text; root.publish(model) }
                }
                ModernButton {
                    text: qsTr("Remove")
                    enabled: !modelData.inherited
                    onClicked: { const model = root.cloneModel(); model.groups.splice(index, 1); root.publish(model) }
                }
            }
        }
    }
    ModernButton {
        text: qsTr("Add group")
        onClicked: {
            const model = root.cloneModel(); if (!model.groups) model.groups = []
            model.groups.push({ "name": "", "inherit": true, "inheritable": true, "inherited": false,
                                "add": [], "remove": [], "inheritedMembers": [] }); root.publish(model)
        }
    }

            Label { textFormat: Text.PlainText; text: qsTr("Access rules"); color: Theme.textStrong; font.bold: true }
    Repeater {
        model: root.aclModel.acls || []
        delegate: Rectangle {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            height: ruleColumn.implicitHeight + 20
            color: Theme.strip
            border.color: modelData.inherited ? Theme.divider : Theme.accent
            radius: Theme.innerRadius
            ColumnLayout {
                id: ruleColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        model: [ { label: qsTr("Group"), value: "group" }, { label: qsTr("User"), value: "user" } ]
                        textRole: "label"
                        currentIndex: modelData.targetType === "user" ? 1 : 0
                        enabled: !modelData.inherited
                        onActivated: { const model = root.cloneModel(); model.acls[index].targetType = model[currentIndex].value; root.publish(model) }
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: modelData.target || ""
                        enabled: !modelData.inherited
                        placeholderText: modelData.targetType === "user" ? qsTr("Username or ID") : qsTr("Group")
                        onEditingFinished: { const model = root.cloneModel(); model.acls[index].target = text; root.publish(model) }
                    }
                    CheckBox {
                        text: qsTr("Here")
                        checked: !!modelData.applyHere
                        enabled: !modelData.inherited
                        onToggled: { const model = root.cloneModel(); model.acls[index].applyHere = checked; root.publish(model) }
                    }
                    CheckBox {
                        text: qsTr("Sub-rooms")
                        checked: !!modelData.applySubs
                        enabled: !modelData.inherited
                        onToggled: { const model = root.cloneModel(); model.acls[index].applySubs = checked; root.publish(model) }
                    }
                    ModernButton {
                        text: qsTr("Remove")
                        enabled: !modelData.inherited
                        onClicked: { const model = root.cloneModel(); model.acls.splice(index, 1); root.publish(model) }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: root.aclModel.permissions || []
                        delegate: Row {
                            required property var modelData
                            spacing: 2
                        Label { textFormat: Text.PlainText; text: modelData.label; color: Theme.textMuted; font.pixelSize: 9; anchors.verticalCenter: parent.verticalCenter }
                            CheckBox {
                                text: qsTr("Allow")
                                checked: root.contains(ruleColumn.parent.modelData.allow, modelData.id)
                                enabled: !ruleColumn.parent.modelData.inherited
                                onToggled: root.togglePermission(ruleColumn.parent.index, "allow", modelData.id, checked)
                            }
                            CheckBox {
                                text: qsTr("Deny")
                                checked: root.contains(ruleColumn.parent.modelData.deny, modelData.id)
                                enabled: !ruleColumn.parent.modelData.inherited
                                onToggled: root.togglePermission(ruleColumn.parent.index, "deny", modelData.id, checked)
                            }
                        }
                    }
                }
            }
        }
    }
    ModernButton {
        text: qsTr("Add rule")
        onClicked: {
            const model = root.cloneModel(); if (!model.acls) model.acls = []
            model.acls.push({ "targetType": "group", "target": "all", "userId": -1,
                              "applyHere": true, "applySubs": true, "inherited": false,
                              "allow": [], "deny": [] }); root.publish(model)
        }
    }
}
