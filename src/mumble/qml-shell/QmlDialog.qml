import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Mumble.Theme 1.0

Dialog {
    id: dialog
	readonly property int densityInset: Theme.spacing + 6
	readonly property int sectionPadding: Theme.spacing + 2
	readonly property bool compactDialogLayout: width < 640
	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}
    parent: Overlay.overlay
    visible: dialogState.open
	modal: true
	focus: true
	title: dialogState.title
    width: Math.min(parent ? parent.width - 48 : 920, 1040)
    height: Math.min(parent ? parent.height - 48 : 700, 760)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.NoAutoClose
	Shortcut {
		sequence: StandardKey.Cancel
		onActivated: dialogState.requestClose()
	}

    background: Rectangle {
        color: Theme.shellBackground
        border.color: Theme.divider
        radius: Theme.shellRadius
    }

	contentItem: ColumnLayout {
		Accessible.role: Accessible.Dialog
		Accessible.name: dialogState.title
		Accessible.description: dialogState.subtitle
		spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.densityId === "compact" ? 68
                                    : Theme.densityId === "spacious" ? 84 : 76
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dialog.densityInset + 2
                anchors.rightMargin: Theme.spacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { textFormat: Text.PlainText; text: dialogState.title; color: Theme.textStrong; font.pixelSize: 19; font.bold: true }
                    Label {
						textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: dialogState.subtitle
                        visible: text.length > 0
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                ToolButton {
                    objectName: "dialogCloseButton"
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
				objectName: "dialogPageRail"
                Layout.preferredWidth: visible ? 190 : 0
                Layout.fillHeight: true
				visible: dialogState.pages.length > 0 && !dialog.compactDialogLayout
                color: Theme.rail
                border.color: Theme.divider
                ListView {
                    anchors.fill: parent
                    anchors.margins: Math.max(8, Theme.spacing - 2)
                    model: dialogState.pages
                    clip: true
                    spacing: Math.max(2, Math.round(Theme.spacing / 4))
                    delegate: ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        height: Theme.densityId === "compact" ? 36
                                : Theme.densityId === "spacious" ? 46 : 40
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
                    spacing: Theme.spacing + 4
					ComboBox {
						id: compactPageSelector
						objectName: "dialogCompactPageSelector"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						visible: dialog.compactDialogLayout && dialogState.pages.length > 0
						height: visible ? implicitHeight : 0
						model: dialogState.pages
						textRole: "label"
						displayText: {
							dialogState.revision
							const page = currentIndex >= 0 && currentIndex < dialogState.pages.length
								? dialogState.pages[currentIndex] : null
							return page ? (page.label || page.title || page.id || "") : ""
						}
						currentIndex: {
							dialogState.revision
							for (let index = 0; index < dialogState.pages.length; ++index) {
								if (String(dialogState.pages[index].id || "") === String(dialogState.activePage || ""))
									return index
							}
							return dialogState.pages.length > 0 ? 0 : -1
						}
						delegate: ItemDelegate {
							required property var modelData
							required property int index
							width: compactPageSelector.popup ? compactPageSelector.popup.width : compactPageSelector.width
							text: modelData.label || modelData.title || modelData.id || ""
							highlighted: compactPageSelector.highlightedIndex === index
						}
						onActivated: index => {
							const page = index >= 0 && index < dialogState.pages.length ? dialogState.pages[index] : null
							if (page)
								dialogState.invokeAction("selectPage", { "pageId": page.id })
						}
						Accessible.name: qsTr("Settings page")
					}
                    Loader {
                        id: screenShareLoader
						objectName: "screenShareEditorLoader"
                        width: parent.width - (dialog.densityInset * 2)
                        x: dialog.densityInset
                        active: dialogState.kind === "screenShare"
                        visible: active
                        sourceComponent: screenShareEditorComponent
                    }
					Binding {
						target: screenShareLoader.item
						property: "shareState"
						value: dialogState.state.screenShare || ({})
						when: screenShareLoader.status === Loader.Ready && screenShareLoader.item !== null
						restoreMode: Binding.RestoreNone
					}
					Connections {
						target: screenShareLoader.item
						enabled: screenShareLoader.status === Loader.Ready && screenShareLoader.item !== null
						function onThumbnailRequested(sourceId) {
							dialogState.invokeAction("screenShare.thumbnail", { "sourceId": sourceId })
						}
					}
                    Loader {
						id: stonksLoader
						objectName: "stonksEditorLoader"
                        width: parent.width - (dialog.densityInset * 2)
                        x: dialog.densityInset
                        active: dialogState.kind === "stonks"
                        visible: active
                        sourceComponent: stonksEditorComponent
                    }
					Binding {
						target: stonksLoader.item
						property: "stonks"
						value: dialogState.state.stonks || ({})
						when: stonksLoader.status === Loader.Ready && stonksLoader.item !== null
						restoreMode: Binding.RestoreNone
					}
                    Repeater {
                        visible: dialogState.kind !== "screenShare" && dialogState.kind !== "stonks"
                        model: dialogState.sections
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width - (dialog.densityInset * 2)
                            x: dialog.densityInset
                            height: sectionColumn.implicitHeight + (dialog.sectionPadding * 2)
                            color: Theme.panel
                            border.color: Theme.divider
                            radius: Theme.innerRadius
                            Column {
                                id: sectionColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: dialog.sectionPadding
                                spacing: Math.max(6, Theme.spacing - 2)
                                Label {
									textFormat: Text.PlainText
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
                                        property bool conditionVisible: {
                                            dialogState.revision
                                            return !field.visibleWhen
                                                || (field.visibleWhen.values || []).indexOf(
                                                    String(dialogState.fieldValue(field.visibleWhen.fieldId))) >= 0
                                        }
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
                                            if (type === "color") return colorField
                                            if (type === "profile") return profileField
                                            if (type === "imagePicker") return pathField
                                            if (type === "manualPositionPreview") return manualPreviewField
                                            if (type === "pathPicker" || type === "filePicker" || type === "folderPicker") return pathField
                                            return textField
                                        }
                                        Label {
											textFormat: Text.PlainText
                                            id: fieldErrorLabel
                                            objectName: "dialogFieldError_" + fieldLoader.field.id
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: fieldLoader.item ? fieldLoader.item.bottom : parent.top
                                            anchors.topMargin: 4
                                            text: {
                                                dialogState.revision
                                                return dialogState.fieldError(fieldLoader.field.id)
                                            }
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
			objectName: "dialogFooter"
            Layout.fillWidth: true
			Layout.preferredHeight: Math.max(Theme.densityId === "compact" ? 56
									 : Theme.densityId === "spacious" ? 72 : 64,
				footerActions.childrenRect.height + (Theme.spacing * 2))
            color: Theme.strip
            border.color: Theme.divider
			Flow {
				id: footerActions
				x: Theme.spacing
				y: Theme.spacing
				width: parent.width - (Theme.spacing * 2)
				spacing: Math.max(6, Math.round(Theme.spacing / 2))
				layoutDirection: Qt.RightToLeft
                Repeater {
                    model: dialogState.actions
                    delegate: ModernButton {
                        required property var modelData
                        objectName: "dialogAction_" + modelData.id
                        text: modelData.label || modelData.text || modelData.id
                        enabled: modelData.enabled === undefined || modelData.enabled
                        onClicked: {
                            const payload = dialogState.kind === "screenShare"
                                    && screenShareLoader.item
                                    && modelData.id === "screenShare.start"
                                ? screenShareLoader.item.actionPayload() : ({})
                            dialogState.invokeAction(modelData.id, payload)
                        }
                    }
                }
            }
        }
    }

    Component { id: hiddenField; Item { property var field; width: parent ? parent.width : 0; height: 0 } }
    Component { id: screenShareEditorComponent; ScreenShareEditor { } }
    Component { id: stonksEditorComponent; StonksEditor { } }
    Component {
        id: noteField
        Label {
			textFormat: Text.PlainText
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
            Label { textFormat: Text.PlainText; text: readonlyRoot.field.label || ""; visible: text.length > 0; color: Theme.textMuted; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: String(readonlyRoot.field.value ?? ""); color: Theme.textMain; wrapMode: Text.Wrap }
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
				textFormat: Text.PlainText
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
            Label { textFormat: Text.PlainText; text: selectRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
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
            Label { textFormat: Text.PlainText; text: numberRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
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
            Label { textFormat: Text.PlainText; text: textRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
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
            Label { textFormat: Text.PlainText; text: pathRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
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
				id: manualPositionCanvas
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
				Connections {
					target: dialogState
					function onStateChanged() { manualPositionCanvas.requestPaint() }
				}
			}
            Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; textFormat: Text.PlainText; text: qsTr("Top view · X / Z"); color: Theme.textMuted; font.pixelSize: 9 }
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
                Image { anchors.fill: parent; source: dialog.safeRenderImageSource(parent.parent.profile.avatarUrl || ""); asynchronous: true; cache: false; sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio); fillMode: Image.PreserveAspectCrop }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { textFormat: Text.PlainText; text: parent.parent.profile.name || ""; color: Theme.textStrong; font.bold: true }
                Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: parent.parent.profile.subtitle || ""; color: Theme.textMuted; elide: Text.ElideRight }
            }
            ModernButton {
                visible: (parent.profile.avatarActionId || "").length > 0
                text: parent.profile.avatarActionLabel || qsTr("Change avatar")
                onClicked: dialogState.invokeAction(parent.profile.avatarActionId, {})
            }
        }
    }
    Component {
        id: colorField
		RowLayout {
			id: colorRoot
			property var field: ({})
            width: parent ? parent.width : 0
            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: colorRoot.field.label || ""; color: Theme.textMain }
			Button {
				objectName: "dialogColorButton_" + String(colorRoot.field.id || "")
				Layout.preferredWidth: 38; Layout.preferredHeight: 28
				text: ""
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Choose %1").arg(colorRoot.field.label || qsTr("color"))
				Accessible.description: String(colorRoot.field.value || "")
				background: Rectangle {
					radius: 6
					color: String(colorRoot.field.value || "#000000")
					border.color: parent.activeFocus ? Theme.focus : Theme.divider
					border.width: parent.activeFocus ? 2 : 1
				}
				onClicked: colorDialog.open()
            }
            Label { textFormat: Text.PlainText; text: String(colorRoot.field.value || ""); color: Theme.textMuted; font.pixelSize: 10 }
            ColorDialog {
                id: colorDialog
                title: colorRoot.field.label || qsTr("Choose color")
                selectedColor: String(colorRoot.field.value || "#000000")
                onAccepted: dialogState.updateField(colorRoot.field.id, selectedColor.toString())
            }
        }
    }
    Component {
        id: resultListField
        ColumnLayout {
            property var field
            width: parent ? parent.width : 0
            Label { textFormat: Text.PlainText; text: parent.field.label || ""; color: Theme.textStrong; font.bold: true; visible: text.length > 0 }
            Repeater {
                model: parent.field.items || parent.field.value || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true; height: 46; radius: 6; color: Theme.strip
                    RowLayout { anchors.fill: parent; anchors.margins: 7
                      ColumnLayout { Layout.fillWidth: true
                        Label { width: parent.width; textFormat: Text.PlainText; text: modelData.label || modelData.title || modelData.name || ""; color: Theme.textMain; elide: Text.ElideRight }
                        Label { width: parent.width; textFormat: Text.PlainText; text: modelData.subtitle || modelData.description || ""; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight }
                      }
                      ModernButton {
                        visible: (modelData.primaryActionId || modelData.primaryAction || "").length > 0
                        text: modelData.primaryActionLabel || modelData.primaryAction || qsTr("Open")
                        onClicked: {
                            const inferred = modelData.type === "user" ? "messageSearchResult" : "selectSearchResult"
                            dialogState.invokeAction(modelData.primaryActionId || inferred,
                                                     modelData.payload || { "id": modelData.id, "type": modelData.type })
                        }
                      }
                      ModernButton {
                        visible: (modelData.secondaryActionId || modelData.secondaryAction || "").length > 0
                        text: modelData.secondaryActionLabel || modelData.secondaryAction
                        onClicked: {
                            const inferred = modelData.type === "channel" ? "joinSearchResult" : "selectSearchResult"
                            dialogState.invokeAction(modelData.secondaryActionId || inferred,
                                                     modelData.payload || { "id": modelData.id, "type": modelData.type })
                        }
                      }
                    }
                }
            }
            Label { textFormat: Text.PlainText; text: parent.field.emptyText || ""; visible: (parent.field.items || []).length === 0; color: Theme.textMuted; wrapMode: Text.Wrap }
        }
    }
    Component {
        id: textareaField
        ColumnLayout {
            id: textareaRoot
            property var field
            width: parent ? parent.width : 0
            Label { textFormat: Text.PlainText; text: textareaRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 10 }
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
