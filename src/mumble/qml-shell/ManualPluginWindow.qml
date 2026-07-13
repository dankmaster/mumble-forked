import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mumble.Theme 1.0

Window {
    id: tool
    width: 720
    height: 680
    minimumWidth: 620
    minimumHeight: 560
    visible: false
    title: qsTr("Manual placement")
    color: Theme.strip
    flags: Qt.Window
    property string statusMessage: ""

    onVisibleChanged: {
        if (visible)
            manualPlugin.refresh()
		manualPlugin.setSpeakerUpdatesEnabled(visible)
    }

    function updateNumber(propertyName, text, fallbackValue) {
        const parsed = Number.fromLocaleString(Qt.locale(), text)
        manualPlugin[propertyName] = Number.isFinite(parsed) ? parsed : fallbackValue
    }

    function syncFields() {
        if (!xField.activeFocus)
            xField.text = Number(manualPlugin.x).toLocaleString(Qt.locale(), "f", 2)
        if (!yField.activeFocus)
            yField.text = Number(manualPlugin.y).toLocaleString(Qt.locale(), "f", 2)
        if (!zField.activeFocus)
            zField.text = Number(manualPlugin.z).toLocaleString(Qt.locale(), "f", 2)
        if (!azimuthField.activeFocus)
            azimuthField.value = manualPlugin.azimuth
        if (!elevationField.activeFocus)
            elevationField.value = manualPlugin.elevation
        if (!contextField.activeFocus)
            contextField.text = manualPlugin.context
        if (!identityField.activeFocus)
            identityField.text = manualPlugin.identity
        if (!staleField.activeFocus)
            staleField.value = manualPlugin.staleSeconds
        activeField.checked = manualPlugin.active
        linkedField.checked = manualPlugin.linked
    }

    Connections {
        target: manualPlugin
        function onStateChanged() { tool.syncFields() }
        function onApplied() {
            tool.statusMessage = qsTr("Position updated")
            statusTimer.restart()
        }
        function onResetCompleted() {
            tool.statusMessage = qsTr("Position reset")
            statusTimer.restart()
        }
    }

    Timer {
        id: statusTimer
        interval: 3000
        onTriggered: tool.statusMessage = ""
    }

    component FieldLabel: Label {
		textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: 12
        Layout.alignment: Qt.AlignVCenter
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 18
        clip: true

        ColumnLayout {
            width: Math.max(560, tool.width - 52)
            spacing: 16

            Label {
				textFormat: Text.PlainText
                text: qsTr("Manual positional audio")
                color: Theme.textStrong
                font.pixelSize: 24
                font.bold: true
            }

            Label {
				textFormat: Text.PlainText
                text: qsTr("Place your positional-audio identity and inspect linked speaker positions.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Frame {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.shellRadius
                    border.color: Theme.divider
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label {
						textFormat: Text.PlainText
                        text: qsTr("Position and orientation")
                        color: Theme.textStrong
                        font.bold: true
                    }

                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 8
                        Layout.fillWidth: true

                        FieldLabel { text: qsTr("X") }
                        TextField {
                            id: xField
                            text: Number(manualPlugin.x).toLocaleString(Qt.locale(), "f", 2)
                            validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                            onEditingFinished: tool.updateNumber("x", text, manualPlugin.x)
                            Accessible.name: qsTr("X position")
                            Layout.fillWidth: true
                        }
                        FieldLabel { text: qsTr("Y") }
                        TextField {
                            id: yField
                            text: Number(manualPlugin.y).toLocaleString(Qt.locale(), "f", 2)
                            validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                            onEditingFinished: tool.updateNumber("y", text, manualPlugin.y)
                            Accessible.name: qsTr("Y position")
                            Layout.fillWidth: true
                        }
                        FieldLabel { text: qsTr("Z") }
                        TextField {
                            id: zField
                            text: Number(manualPlugin.z).toLocaleString(Qt.locale(), "f", 2)
                            validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                            onEditingFinished: tool.updateNumber("z", text, manualPlugin.z)
                            Accessible.name: qsTr("Z position")
                            Layout.fillWidth: true
                        }
                        FieldLabel { text: qsTr("Azimuth") }
                        SpinBox {
                            id: azimuthField
                            from: 0
                            to: 360
                            value: manualPlugin.azimuth
                            editable: true
                            onValueModified: manualPlugin.azimuth = value
                            Accessible.name: qsTr("Azimuth")
                            Layout.fillWidth: true
                        }
                        FieldLabel { text: qsTr("Elevation") }
                        SpinBox {
                            id: elevationField
                            from: -90
                            to: 90
                            value: manualPlugin.elevation
                            editable: true
                            onValueModified: manualPlugin.elevation = value
                            Accessible.name: qsTr("Elevation")
                            Layout.fillWidth: true
                        }
                    }

                    Rectangle {
                        id: preview
                        Layout.fillWidth: true
                        Layout.preferredHeight: 210
                        color: Theme.strip
                        radius: Theme.shellRadius
                        border.color: Theme.divider
                        clip: true

                        property real scaleFactor: Math.max(1, Math.min(width, height) / 120)
                        function displayX(value) { return width / 2 + Math.max(-50, Math.min(50, value)) * scaleFactor }
                        function displayY(value) { return height / 2 + Math.max(-50, Math.min(50, value)) * scaleFactor }

                        Rectangle {
                            x: 12
                            y: parent.height / 2
                            width: parent.width - 24
                            height: 1
                            color: Theme.divider
                        }
                        Rectangle {
                            x: parent.width / 2
                            y: 12
                            width: 1
                            height: parent.height - 24
                            color: Theme.divider
                        }

                        Repeater {
                            model: manualPlugin.speakers.slice(0, 64)
                            delegate: Rectangle {
                                required property var modelData
                                width: 8
                                height: 8
                                radius: 4
                                x: preview.displayX(Number(modelData.x)) - width / 2
                                y: preview.displayY(Number(modelData.z)) - height / 2
                                color: Theme.textMuted
                            }
                        }

                        Rectangle {
                            id: avatar
                            width: 14
                            height: 14
                            radius: 7
                            x: preview.displayX(manualPlugin.x) - width / 2
                            y: preview.displayY(manualPlugin.z) - height / 2
                            color: Theme.accent
                            border.color: Theme.textStrong

                            Rectangle {
                                width: 3
                                height: 24
                                radius: 2
                                color: Theme.accent
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.verticalCenter
                                transformOrigin: Item.Bottom
                                rotation: manualPlugin.azimuth
                            }
                        }

                        Label {
							textFormat: Text.PlainText
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            text: qsTr("Linked speakers: %1").arg(manualPlugin.speakers.length)
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 12
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.shellRadius
                    border.color: Theme.divider
                }

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    FieldLabel { text: qsTr("Context") }
                    TextField {
                        id: contextField
                        text: manualPlugin.context
                        onEditingFinished: manualPlugin.context = text
                        Accessible.name: qsTr("Context")
                        Layout.fillWidth: true
                    }
                    FieldLabel { text: qsTr("Identity") }
                    TextField {
                        id: identityField
                        text: manualPlugin.identity
                        onEditingFinished: manualPlugin.identity = text
                        Accessible.name: qsTr("Identity")
                        Layout.fillWidth: true
                    }
                    FieldLabel { text: qsTr("Stale user time") }
                    SpinBox {
                        id: staleField
                        from: 0
                        to: 3600
                        value: manualPlugin.staleSeconds
                        editable: true
                        onValueModified: manualPlugin.staleSeconds = value
                        Accessible.name: qsTr("Stale user display time in seconds")
                        Layout.fillWidth: true
                    }
                    CheckBox {
                        id: activeField
                        text: qsTr("Active")
                        checked: manualPlugin.active
                        onToggled: manualPlugin.active = checked
                    }
                    CheckBox {
                        id: linkedField
                        text: qsTr("Linked")
                        checked: manualPlugin.linked
                        onToggled: manualPlugin.linked = checked
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
					textFormat: Text.PlainText
                    text: tool.statusMessage
                    color: Theme.textMuted
                    visible: text.length > 0
                    Accessible.role: Accessible.StaticText
                }
                Item { Layout.fillWidth: true }
                ModernButton {
                    text: qsTr("Reset")
                    onClicked: manualPlugin.reset()
                }
                ModernButton {
                    text: qsTr("Close")
                    onClicked: tool.hide()
                }
                ModernButton {
                    text: qsTr("Apply")
                    onClicked: manualPlugin.apply()
                }
            }
        }
    }
}
