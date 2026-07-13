import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root
	property var field
	readonly property bool editorEnabled: !field || field.enabled !== false
	readonly property var rows: field && field.rows ? field.rows : []
	readonly property int assignedCount: {
		let count = 0
		for (let index = 0; index < rows.length; ++index) {
			if (rows[index] && rows[index].assigned) ++count
		}
		return count
	}

	function optionIndex(options, value) {
		if (!options) return -1
		for (let index = 0; index < options.length; ++index) {
			if (String(options[index].value) === String(value)) return index
		}
		return -1
	}

	function selectedUser(target, hash) {
		const users = target && target.users ? target.users : []
		for (let index = 0; index < users.length; ++index) {
			if (String(users[index].value) === String(hash)) return true
		}
		return false
	}

	function targetPatch(rowIndex, patch) {
		const payload = { "index": rowIndex }
		for (const key in patch) payload[key] = patch[key]
		dialogState.invokeAction("keys.shortcutTarget", payload)
	}

	width: parent ? parent.width : 0
	spacing: Theme.space3

	RowLayout {
		Layout.fillWidth: true
		spacing: Theme.space3

		ColumnLayout {
			Layout.fillWidth: true
			spacing: 2
			Label {
				textFormat: Text.PlainText
				text: root.field && root.field.label ? root.field.label : qsTr("Shortcuts")
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
			}
			Label {
				textFormat: Text.PlainText
				text: qsTr("%1 assigned · %2 total").arg(root.assignedCount).arg(root.rows.length)
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
			}
		}

		ModernButton {
			objectName: "shortcutAddButton"
			text: qsTr("Add shortcut")
			tone: "primary"
			enabled: root.editorEnabled
			onClicked: dialogState.invokeAction("keys.addShortcut", {})
		}
	}

	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: 72
		visible: root.rows.length === 0
		radius: Theme.innerRadius
		color: Theme.panel
		border.color: Theme.divider

		Column {
			anchors.centerIn: parent
			spacing: Theme.space1
			Label {
				anchors.horizontalCenter: parent.horizontalCenter
				textFormat: Text.PlainText
				text: qsTr("No shortcuts configured")
				color: Theme.textMain
				font.weight: Font.Medium
			}
			Label {
				anchors.horizontalCenter: parent.horizontalCenter
				textFormat: Text.PlainText
				text: qsTr("Add one to bind an action to a keyboard or device input.")
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
			}
		}
	}

	ListView {
		id: shortcutList
		objectName: "shortcutList"
		Layout.fillWidth: true
		Layout.preferredHeight: Math.min(contentHeight, 520)
		implicitHeight: Layout.preferredHeight
		visible: root.rows.length > 0
		model: root.rows
		clip: true
		spacing: Theme.space2
		boundsBehavior: Flickable.StopAtBounds
		Accessible.role: Accessible.List
		Accessible.name: qsTr("Configured shortcuts")
		ScrollIndicator.vertical: ScrollIndicator { }

		delegate: Rectangle {
			id: shortcutCard
			required property var modelData
			readonly property int rowIndex: Number(modelData.index)
			readonly property string dataType: String(modelData.dataType || "none")
			readonly property var target: modelData.target || ({})
			readonly property string targetMode: String(target.mode || "channel")
			readonly property bool dataEnabled: root.editorEnabled && modelData.dataEditable !== false
			objectName: "shortcutRow_" + rowIndex
			Accessible.role: Accessible.ListItem
			Accessible.name: qsTr("%1 shortcut").arg(modelData.actionLabel || qsTr("Unassigned"))
			width: shortcutList.width
			height: shortcutContent.implicitHeight + Theme.space4 * 2
			radius: Theme.innerRadius
			color: modelData.capturing ? Theme.accentSubtle : Theme.panel
			border.color: modelData.capturing ? Theme.accent : modelData.assigned ? Theme.surfaceBorder : Theme.divider
			border.width: modelData.capturing ? 2 : 1

			ColumnLayout {
				id: shortcutContent
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space4
				spacing: Theme.space3

				GridLayout {
					Layout.fillWidth: true
					columns: width >= 690 ? 3 : 1
					columnSpacing: Theme.space3
					rowSpacing: Theme.space2

					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 190
						spacing: Theme.space1
						Label {
							textFormat: Text.PlainText
							text: qsTr("Function")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
						ModernComboBox {
							id: actionPicker
							objectName: "shortcutAction_" + shortcutCard.rowIndex
							Layout.fillWidth: true
							model: root.field && root.field.actionOptions ? root.field.actionOptions : []
							textRole: "label"
							valueRole: "value"
							Accessible.name: qsTr("Function")
							currentIndex: root.optionIndex(model, shortcutCard.modelData.actionIndex)
							enabled: root.editorEnabled
							onActivated: dialogState.invokeAction("keys.shortcutAction", {
								"index": shortcutCard.rowIndex,
								"actionIndex": currentValue
							})
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 190
						spacing: Theme.space1
						Label {
							textFormat: Text.PlainText
							text: qsTr("Data")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}

						ModernComboBox {
							objectName: "shortcutToggleData_" + shortcutCard.rowIndex
							Layout.fillWidth: true
							visible: shortcutCard.dataType === "toggle"
							model: root.field && root.field.toggleOptions ? root.field.toggleOptions : []
							textRole: "label"
							valueRole: "value"
							Accessible.name: qsTr("Shortcut data")
							currentIndex: root.optionIndex(model, shortcutCard.modelData.dataValue)
							enabled: shortcutCard.dataEnabled
							onActivated: dialogState.invokeAction("keys.shortcutData", {
								"index": shortcutCard.rowIndex,
								"value": currentValue
							})
						}

						ModernComboBox {
							objectName: "shortcutChannelData_" + shortcutCard.rowIndex
							Layout.fillWidth: true
							visible: shortcutCard.dataType === "channel"
							model: root.field && root.field.channelOptions ? root.field.channelOptions : []
							textRole: "label"
							valueRole: "value"
							Accessible.name: qsTr("Shortcut channel")
							currentIndex: root.optionIndex(model, shortcutCard.modelData.dataValue)
							enabled: shortcutCard.dataEnabled
							onActivated: dialogState.invokeAction("keys.shortcutData", {
								"index": shortcutCard.rowIndex,
								"value": currentValue
							})
						}

						ModernTextField {
							objectName: "shortcutTextData_" + shortcutCard.rowIndex
							Layout.fillWidth: true
							visible: shortcutCard.dataType === "text"
							text: shortcutCard.modelData.dataValue == null ? "" : String(shortcutCard.modelData.dataValue)
							placeholderText: qsTr("Shortcut data")
							Accessible.name: qsTr("Shortcut data")
							enabled: shortcutCard.dataEnabled
							onEditingFinished: dialogState.invokeAction("keys.shortcutData", {
								"index": shortcutCard.rowIndex,
								"value": text
							})
						}

						Rectangle {
							Layout.fillWidth: true
							Layout.preferredHeight: Theme.controlHeight
							visible: ["toggle", "channel", "text", "target"].indexOf(shortcutCard.dataType) < 0
							radius: Theme.innerRadius
							color: Theme.strip
							Label {
								anchors.fill: parent
								anchors.leftMargin: Theme.space3
								anchors.rightMargin: Theme.space3
								verticalAlignment: Text.AlignVCenter
								textFormat: Text.PlainText
								text: shortcutCard.modelData.dataLabel || qsTr("No data")
								color: Theme.textMuted
								elide: Text.ElideRight
							}
						}

						Label {
							Layout.fillWidth: true
							visible: shortcutCard.dataType === "target"
							textFormat: Text.PlainText
							text: shortcutCard.target.summary || shortcutCard.modelData.dataLabel || qsTr("Current")
							color: Theme.accentHover
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
							elide: Text.ElideRight
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 180
						spacing: Theme.space1
						Label {
							textFormat: Text.PlainText
							text: qsTr("Shortcut")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
						Rectangle {
							Layout.fillWidth: true
							Layout.preferredHeight: Theme.controlHeight
							radius: Theme.innerRadius
							color: shortcutCard.modelData.capturing ? Theme.accentSubtle : Theme.strip
							border.color: shortcutCard.modelData.capturing ? Theme.accent : "transparent"
							Label {
								anchors.fill: parent
								anchors.leftMargin: Theme.space3
								anchors.rightMargin: Theme.space3
								verticalAlignment: Text.AlignVCenter
								textFormat: Text.PlainText
								text: shortcutCard.modelData.capturing
									? qsTr("Press the shortcut now…")
									: (shortcutCard.modelData.inputLabel || qsTr("Not assigned"))
								color: shortcutCard.modelData.assigned || shortcutCard.modelData.capturing
									? Theme.textStrong : Theme.textMuted
								font.weight: shortcutCard.modelData.capturing ? Font.DemiBold : Font.Normal
								elide: Text.ElideRight
							}
						}
					}
				}

				Rectangle {
					Layout.fillWidth: true
					implicitHeight: targetLayout.implicitHeight + Theme.space3 * 2
					visible: shortcutCard.dataType === "target"
					radius: Theme.innerRadius
					color: Theme.strip
					border.color: Theme.divider

					ColumnLayout {
						id: targetLayout
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.top: parent.top
						anchors.margins: Theme.space3
						spacing: Theme.space2

						GridLayout {
							Layout.fillWidth: true
							columns: width >= 600 && shortcutCard.targetMode === "channel" ? 3 : 2
							columnSpacing: Theme.space3
							rowSpacing: Theme.space2

							ColumnLayout {
								Layout.fillWidth: true
								Label { textFormat: Text.PlainText; text: qsTr("Target"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
								ModernComboBox {
									objectName: "shortcutTargetMode_" + shortcutCard.rowIndex
									Layout.fillWidth: true
									model: root.field && root.field.targetModeOptions ? root.field.targetModeOptions : []
									textRole: "label"
									valueRole: "value"
									Accessible.name: qsTr("Target type")
									currentIndex: root.optionIndex(model, shortcutCard.targetMode)
									enabled: shortcutCard.dataEnabled
									onActivated: root.targetPatch(shortcutCard.rowIndex, {
										"targetAction": "mode", "mode": currentValue
									})
								}
							}

							ColumnLayout {
								Layout.fillWidth: true
								visible: shortcutCard.targetMode === "channel"
								Label { textFormat: Text.PlainText; text: qsTr("Channel"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
								ModernComboBox {
									objectName: "shortcutTargetChannel_" + shortcutCard.rowIndex
									Layout.fillWidth: true
									model: root.field && root.field.targetChannelOptions ? root.field.targetChannelOptions : []
									textRole: "label"
									valueRole: "value"
									Accessible.name: qsTr("Target channel")
									currentIndex: root.optionIndex(model, shortcutCard.target.channelId)
									enabled: shortcutCard.dataEnabled
									onActivated: root.targetPatch(shortcutCard.rowIndex, {
										"targetAction": "channel", "channelId": currentValue
									})
								}
							}

							ColumnLayout {
								Layout.fillWidth: true
								visible: shortcutCard.targetMode === "channel"
								Label { textFormat: Text.PlainText; text: qsTr("Restrict to group"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
								ModernTextField {
									objectName: "shortcutTargetGroup_" + shortcutCard.rowIndex
									Layout.fillWidth: true
									text: shortcutCard.target.group == null ? "" : String(shortcutCard.target.group)
									placeholderText: qsTr("Optional group")
									Accessible.name: qsTr("Target group")
									enabled: shortcutCard.dataEnabled
									onEditingFinished: root.targetPatch(shortcutCard.rowIndex, {
										"targetAction": "group", "group": text
									})
								}
							}
						}

						ColumnLayout {
							Layout.fillWidth: true
							visible: shortcutCard.targetMode === "users"
							spacing: Theme.space2

							Flow {
								id: selectedUsersFlow
								Layout.fillWidth: true
								spacing: Theme.space2

								Label {
									visible: !shortcutCard.target.users || shortcutCard.target.users.length === 0
									textFormat: Text.PlainText
									text: qsTr("No users selected")
									color: Theme.textMuted
									font.pixelSize: Theme.fontCaption
								}

								Repeater {
									model: shortcutCard.target.users || []
									delegate: Rectangle {
										required property var modelData
										implicitWidth: selectedUserRow.implicitWidth + Theme.space2 * 2
										implicitHeight: 30
										radius: 15
										color: Theme.accentSubtle
										border.color: Theme.accent
										RowLayout {
											id: selectedUserRow
											anchors.centerIn: parent
											spacing: Theme.space1
										Label {
											textFormat: Text.PlainText
											text: modelData.label || modelData.value || qsTr("Unknown")
												color: Theme.textStrong
												font.pixelSize: Theme.fontCaption
											}
										ModernIconButton {
											objectName: "shortcutTargetRemoveUser_" + shortcutCard.rowIndex + "_" + String(modelData.value)
											iconName: "close"
												tone: "danger"
												dense: true
												enabled: shortcutCard.dataEnabled
												Accessible.name: qsTr("Remove %1").arg(modelData.label || modelData.value)
												onClicked: root.targetPatch(shortcutCard.rowIndex, {
													"targetAction": "removeUser", "hash": modelData.value
												})
											}
										}
									}
								}
							}

							RowLayout {
								Layout.fillWidth: true
								ModernComboBox {
									id: targetUserPicker
									objectName: "shortcutTargetUser_" + shortcutCard.rowIndex
									Layout.fillWidth: true
									model: root.field && root.field.targetUserOptions ? root.field.targetUserOptions : []
									textRole: "label"
									valueRole: "value"
									Accessible.name: qsTr("User to add")
									enabled: shortcutCard.dataEnabled && count > 0
								}
								ModernButton {
									objectName: "shortcutTargetAddUser_" + shortcutCard.rowIndex
									text: qsTr("Add")
									enabled: shortcutCard.dataEnabled && targetUserPicker.currentIndex >= 0
									onClicked: root.targetPatch(shortcutCard.rowIndex, {
										"targetAction": "addUser",
										"hash": targetUserPicker.currentValue
									})
								}
							}
						}

						Flow {
							Layout.fillWidth: true
							spacing: Theme.space3
							ModernCheckBox {
								objectName: "shortcutTargetLinks_" + shortcutCard.rowIndex
								text: qsTr("Linked channels")
								checked: !!shortcutCard.target.links
								enabled: shortcutCard.dataEnabled
								onToggled: root.targetPatch(shortcutCard.rowIndex, {
									"targetAction": "links", "enabled": checked
								})
							}
							ModernCheckBox {
								objectName: "shortcutTargetChildren_" + shortcutCard.rowIndex
								text: qsTr("Subchannels")
								checked: !!shortcutCard.target.children
								enabled: shortcutCard.dataEnabled
								onToggled: root.targetPatch(shortcutCard.rowIndex, {
									"targetAction": "children", "enabled": checked
								})
							}
							ModernCheckBox {
								objectName: "shortcutTargetForceCenter_" + shortcutCard.rowIndex
								text: qsTr("Ignore positional audio")
								checked: !!shortcutCard.target.forceCenter
								enabled: shortcutCard.dataEnabled
								onToggled: root.targetPatch(shortcutCard.rowIndex, {
									"targetAction": "forceCenter", "enabled": checked
								})
							}
						}
					}
				}

				Flow {
					Layout.fillWidth: true
					spacing: Theme.space2

					ModernCheckBox {
						objectName: "shortcutSuppress_" + shortcutCard.rowIndex
						text: qsTr("Suppress other applications")
						visible: !!root.field && !!root.field.canSuppress
						checked: !!shortcutCard.modelData.suppress
						enabled: root.editorEnabled
						onToggled: dialogState.invokeAction("keys.shortcutSuppress", {
							"index": shortcutCard.rowIndex, "suppress": checked
						})
					}
					ModernButton {
						objectName: "shortcutCapture_" + shortcutCard.rowIndex
						text: shortcutCard.modelData.capturing ? qsTr("Cancel capture") : qsTr("Capture")
						tone: shortcutCard.modelData.capturing ? "warning" : "neutral"
						enabled: root.editorEnabled && !!root.field && !!root.field.canCapture
						onClicked: dialogState.invokeAction(shortcutCard.modelData.capturing
							? "keys.cancelShortcutCapture" : "keys.beginShortcutCapture",
							{ "index": shortcutCard.rowIndex })
					}
					ModernButton {
						objectName: "shortcutClear_" + shortcutCard.rowIndex
						text: qsTr("Clear")
						enabled: root.editorEnabled && !!shortcutCard.modelData.assigned
						onClicked: dialogState.invokeAction("keys.clearShortcut", { "index": shortcutCard.rowIndex })
					}
					ModernButton {
						objectName: "shortcutRemove_" + shortcutCard.rowIndex
						text: qsTr("Remove")
						tone: "danger"
						enabled: root.editorEnabled
						onClicked: dialogState.invokeAction("keys.removeShortcut", { "index": shortcutCard.rowIndex })
					}
				}
			}
		}
	}
}
