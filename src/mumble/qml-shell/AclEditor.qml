import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root
	property var field: ({ "id": "", "value": ({}) })
	property var aclModel: field.value || ({})
	onFieldChanged: aclModel = field.value || ({})
	readonly property var groups: aclModel.groups || []
	readonly property var rules: aclModel.acls || []
	readonly property bool compactLayout: width < 700
	readonly property int groupListLimit: compactLayout ? 420 : 300
	readonly property int ruleListLimit: compactLayout ? 520 : 440
	width: parent ? parent.width : 0
	spacing: Theme.spacing

	function cloneModel() {
		return JSON.parse(JSON.stringify(aclModel || {}))
	}
	function publish(model) {
		aclModel = model
		dialogState.updateField(field.id, model)
	}
	function contains(list, value) {
		return (list || []).indexOf(value) >= 0
	}
	function togglePermission(ruleIndex, key, permissionId, checked) {
		const model = cloneModel()
		const rule = model.acls[ruleIndex]
		let values = rule[key] || []
		const valueIndex = values.indexOf(permissionId)
		if (checked && valueIndex < 0) values.push(permissionId)
		if (!checked && valueIndex >= 0) values.splice(valueIndex, 1)
		rule[key] = values
		publish(model)
	}
	function liveDelegateCount(list, marker) {
		let live = 0
		const objects = list && list.contentItem ? list.contentItem.children : []
		for (let index = 0; index < objects.length; ++index) {
			if (objects[index][marker]) ++live
		}
		return live
	}

	GridLayout {
		Layout.fillWidth: true
		Layout.minimumWidth: 0
		Layout.maximumWidth: root.width
		columns: root.compactLayout ? 1 : 2
		columnSpacing: Theme.spacing * 2
		rowSpacing: Math.max(6, Theme.spacing - 2)

		ModernCheckBox {
			objectName: "aclInheritFromParent"
			Layout.fillWidth: root.compactLayout
			text: qsTr("Inherit ACLs from parent room")
			checked: !!root.aclModel.inheritAcls
			onToggled: {
				const model = root.cloneModel()
				model.inheritAcls = checked
				root.publish(model)
			}
		}
		ColumnLayout {
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			Layout.maximumWidth: root.width
			spacing: 4
			Label {
				textFormat: Text.PlainText
				text: qsTr("Room password")
				color: Theme.textMuted
				font.pixelSize: 11
			}
			ModernTextField {
				objectName: "aclRoomPassword"
				Layout.fillWidth: true
				Layout.minimumWidth: 0
				Layout.maximumWidth: parent.width
				text: root.aclModel.password || ""
				echoMode: TextInput.Password
				Accessible.name: qsTr("Room password")
				onEditingFinished: {
					const model = root.cloneModel()
					model.password = text
					root.publish(model)
				}
			}
		}
	}

	RowLayout {
		Layout.fillWidth: true
		ColumnLayout {
			Layout.fillWidth: true
			spacing: 1
			Label {
				textFormat: Text.PlainText
				text: qsTr("Groups")
				color: Theme.textStrong
				font.pixelSize: 13
				font.bold: true
				Accessible.role: Accessible.Heading
			}
			Label {
				textFormat: Text.PlainText
				text: qsTr("%n group(s)", "", root.groups.length)
				color: Theme.textMuted
				font.pixelSize: 11
			}
		}
		ModernButton {
			objectName: "aclAddGroup"
			text: qsTr("Add group")
			dense: true
			onClicked: {
				const model = root.cloneModel()
				if (!model.groups) model.groups = []
				model.groups.push({ "name": "", "inherit": true, "inheritable": true,
					"inherited": false, "add": [], "remove": [], "inheritedMembers": [] })
				root.publish(model)
				Qt.callLater(function() { groupList.positionViewAtEnd() })
			}
		}
	}

	ListView {
		id: groupList
		objectName: "aclGroupList"
		Layout.fillWidth: true
		Layout.preferredHeight: visible
			? Math.min(root.groupListLimit, Math.max(68, contentHeight)) : 0
		Layout.maximumHeight: root.groupListLimit
		visible: count > 0
		model: root.groups
		clip: true
		spacing: Math.max(6, Theme.spacing - 2)
		cacheBuffer: 240
		reuseItems: true
		boundsBehavior: Flickable.StopAtBounds
		activeFocusOnTab: visible
		keyNavigationEnabled: true
		keyNavigationWraps: false
		currentIndex: count > 0 ? 0 : -1
		Accessible.role: Accessible.List
		Accessible.name: qsTr("Access-control groups")
		ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
		function liveDelegateCount() { return root.liveDelegateCount(groupList, "isAclGroupDelegate") }
		Keys.onPressed: event => {
			if (event.key === Qt.Key_Home && count > 0) {
				currentIndex = 0
				positionViewAtBeginning()
				event.accepted = true
			} else if (event.key === Qt.Key_End && count > 0) {
				currentIndex = count - 1
				positionViewAtEnd()
				event.accepted = true
			}
		}
		delegate: ItemDelegate {
			id: groupDelegate
			required property var modelData
			required property int index
			property bool isAclGroupDelegate: true
			width: ListView.view.width
			height: groupContent.implicitHeight + 20
			padding: 10
			hoverEnabled: true
			highlighted: ListView.isCurrentItem || hovered
			readonly property bool current: ListView.isCurrentItem
			readonly property bool keyboardFocused: activeFocus
				|| (ListView.view.activeFocus && current)
			Accessible.role: Accessible.ListItem
			Accessible.name: modelData.name || qsTr("New group")
			Accessible.description: modelData.inherited ? qsTr("Inherited group") : qsTr("Editable group")
			Accessible.selected: current
			background: Rectangle {
				color: groupDelegate.down ? Theme.accentSubtle
					: groupDelegate.current ? Theme.selected
					: groupDelegate.hovered ? Theme.surfaceHover : Theme.strip
				border.color: groupDelegate.keyboardFocused ? Theme.focus
					: groupDelegate.current ? Theme.accent : Theme.divider
				border.width: groupDelegate.keyboardFocused ? Theme.focusRingWidth : 1
				radius: Theme.innerRadius
			}
			contentItem: ColumnLayout {
				id: groupContent
				spacing: Math.max(6, Theme.spacing - 2)
				RowLayout {
					Layout.fillWidth: true
					ModernTextField {
						objectName: "aclGroupName_" + groupDelegate.index
						Layout.fillWidth: true
						text: groupDelegate.modelData.name || ""
						enabled: !groupDelegate.modelData.inherited
						placeholderText: qsTr("Group name")
						Accessible.name: qsTr("Group name")
						onEditingFinished: {
							const model = root.cloneModel()
							model.groups[groupDelegate.index].name = text
							root.publish(model)
						}
					}
					Label {
						textFormat: Text.PlainText
						visible: !!groupDelegate.modelData.inherited
						text: qsTr("Inherited")
						color: Theme.textMuted
						font.pixelSize: 11
					}
					ModernButton {
						objectName: "aclRemoveGroup_" + groupDelegate.index
						text: qsTr("Remove")
						dense: true
						tone: "danger"
						enabled: !groupDelegate.modelData.inherited
						onClicked: {
							const model = root.cloneModel()
							model.groups.splice(groupDelegate.index, 1)
							root.publish(model)
						}
					}
				}
				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Math.max(6, Theme.spacing - 2)
					ModernCheckBox {
						text: qsTr("Inherit members")
						checked: !!groupDelegate.modelData.inherit
						enabled: !groupDelegate.modelData.inherited
						onToggled: {
							const model = root.cloneModel()
							model.groups[groupDelegate.index].inherit = checked
							root.publish(model)
						}
					}
					ModernCheckBox {
						text: qsTr("Available to child rooms")
						checked: !!groupDelegate.modelData.inheritable
						enabled: !groupDelegate.modelData.inherited
						onToggled: {
							const model = root.cloneModel()
							model.groups[groupDelegate.index].inheritable = checked
							root.publish(model)
						}
					}
				}
				GridLayout {
					Layout.fillWidth: true
					columns: root.compactLayout ? 1 : 2
					columnSpacing: Theme.spacing
					rowSpacing: Math.max(6, Theme.spacing - 2)
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label { textFormat: Text.PlainText; text: qsTr("Added user IDs"); color: Theme.textMuted; font.pixelSize: 11 }
						ModernTextField {
							Layout.fillWidth: true
							text: (groupDelegate.modelData.add || []).join(", ")
							enabled: !groupDelegate.modelData.inherited
							placeholderText: qsTr("Comma-separated IDs")
							onEditingFinished: {
								const model = root.cloneModel()
								model.groups[groupDelegate.index].addText = text
								root.publish(model)
							}
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label { textFormat: Text.PlainText; text: qsTr("Removed user IDs"); color: Theme.textMuted; font.pixelSize: 11 }
						ModernTextField {
							Layout.fillWidth: true
							text: (groupDelegate.modelData.remove || []).join(", ")
							enabled: !groupDelegate.modelData.inherited
							placeholderText: qsTr("Comma-separated IDs")
							onEditingFinished: {
								const model = root.cloneModel()
								model.groups[groupDelegate.index].removeText = text
								root.publish(model)
							}
						}
					}
				}
			}
			onClicked: groupList.currentIndex = index
		}
	}
	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: visible ? 72 : 0
		visible: root.groups.length === 0
		color: Theme.strip
		border.color: Theme.divider
		radius: Theme.innerRadius
		Label {
			textFormat: Text.PlainText
			anchors.centerIn: parent
			text: qsTr("No explicit groups for this room.")
			color: Theme.textMuted
		}
	}

	RowLayout {
		Layout.fillWidth: true
		ColumnLayout {
			Layout.fillWidth: true
			spacing: 1
			Label {
				textFormat: Text.PlainText
				text: qsTr("Access rules")
				color: Theme.textStrong
				font.pixelSize: 13
				font.bold: true
				Accessible.role: Accessible.Heading
			}
			Label {
				textFormat: Text.PlainText
				text: qsTr("%n rule(s)", "", root.rules.length)
				color: Theme.textMuted
				font.pixelSize: 11
			}
		}
		ModernButton {
			objectName: "aclAddRule"
			text: qsTr("Add rule")
			dense: true
			tone: "accent"
			onClicked: {
				const model = root.cloneModel()
				if (!model.acls) model.acls = []
				model.acls.push({ "targetType": "group", "target": "all", "userId": -1,
					"applyHere": true, "applySubs": true, "inherited": false,
					"allow": [], "deny": [] })
				root.publish(model)
				Qt.callLater(function() { ruleList.positionViewAtEnd() })
			}
		}
	}

	ListView {
		id: ruleList
		objectName: "aclRuleList"
		Layout.fillWidth: true
		Layout.preferredHeight: visible
			? Math.min(root.ruleListLimit, Math.max(108, contentHeight)) : 0
		Layout.maximumHeight: root.ruleListLimit
		visible: count > 0
		model: root.rules
		clip: true
		spacing: Math.max(6, Theme.spacing - 2)
		cacheBuffer: 360
		reuseItems: true
		boundsBehavior: Flickable.StopAtBounds
		activeFocusOnTab: visible
		keyNavigationEnabled: true
		keyNavigationWraps: false
		currentIndex: count > 0 ? 0 : -1
		Accessible.role: Accessible.List
		Accessible.name: qsTr("Access rules")
		ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
		function liveDelegateCount() { return root.liveDelegateCount(ruleList, "isAclRuleDelegate") }
		Keys.onPressed: event => {
			if (event.key === Qt.Key_Home && count > 0) {
				currentIndex = 0
				positionViewAtBeginning()
				event.accepted = true
			} else if (event.key === Qt.Key_End && count > 0) {
				currentIndex = count - 1
				positionViewAtEnd()
				event.accepted = true
			}
		}
		delegate: ItemDelegate {
			id: ruleDelegate
			required property var modelData
			required property int index
			property bool isAclRuleDelegate: true
			width: ListView.view.width
			height: ruleContent.implicitHeight + 20
			padding: 10
			hoverEnabled: true
			highlighted: ListView.isCurrentItem || hovered
			readonly property bool current: ListView.isCurrentItem
			readonly property bool keyboardFocused: activeFocus
				|| (ListView.view.activeFocus && current)
			Accessible.role: Accessible.ListItem
			Accessible.name: qsTr("Rule for %1").arg(modelData.target || qsTr("all users"))
			Accessible.description: modelData.inherited ? qsTr("Inherited rule") : qsTr("Editable rule")
			Accessible.selected: current
			background: Rectangle {
				color: ruleDelegate.down ? Theme.accentSubtle
					: ruleDelegate.current ? Theme.selected
					: ruleDelegate.hovered ? Theme.surfaceHover : Theme.strip
				border.color: ruleDelegate.keyboardFocused ? Theme.focus
					: ruleDelegate.current ? Theme.accent
					: ruleDelegate.modelData.inherited ? Theme.divider : Theme.accent
				border.width: ruleDelegate.keyboardFocused ? Theme.focusRingWidth : 1
				radius: Theme.innerRadius
			}
			contentItem: ColumnLayout {
				id: ruleContent
				spacing: Math.max(7, Theme.spacing - 1)
				GridLayout {
					Layout.fillWidth: true
					columns: root.compactLayout ? 1 : 2
					columnSpacing: Theme.spacing
					rowSpacing: Math.max(6, Theme.spacing - 2)
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label { textFormat: Text.PlainText; text: qsTr("Applies to"); color: Theme.textMuted; font.pixelSize: 11 }
						ModernComboBox {
							Layout.fillWidth: true
							model: [ { label: qsTr("Group"), value: "group" }, { label: qsTr("User"), value: "user" } ]
							textRole: "label"
							currentIndex: ruleDelegate.modelData.targetType === "user" ? 1 : 0
							enabled: !ruleDelegate.modelData.inherited
							onActivated: {
								const nextModel = root.cloneModel()
								nextModel.acls[ruleDelegate.index].targetType = currentValue
								root.publish(nextModel)
							}
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label {
							textFormat: Text.PlainText
							text: ruleDelegate.modelData.targetType === "user" ? qsTr("User") : qsTr("Group")
							color: Theme.textMuted
							font.pixelSize: 11
						}
						ModernTextField {
							Layout.fillWidth: true
							text: ruleDelegate.modelData.target || ""
							enabled: !ruleDelegate.modelData.inherited
							placeholderText: ruleDelegate.modelData.targetType === "user"
								? qsTr("Username or ID") : qsTr("Group name")
							onEditingFinished: {
								const model = root.cloneModel()
								model.acls[ruleDelegate.index].target = text
								root.publish(model)
							}
						}
					}
				}
				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Math.max(6, Theme.spacing - 2)
					ModernCheckBox {
						text: qsTr("This room")
						checked: !!ruleDelegate.modelData.applyHere
						enabled: !ruleDelegate.modelData.inherited
						onToggled: {
							const model = root.cloneModel()
							model.acls[ruleDelegate.index].applyHere = checked
							root.publish(model)
						}
					}
					ModernCheckBox {
						text: qsTr("Child rooms")
						checked: !!ruleDelegate.modelData.applySubs
						enabled: !ruleDelegate.modelData.inherited
						onToggled: {
							const model = root.cloneModel()
							model.acls[ruleDelegate.index].applySubs = checked
							root.publish(model)
						}
					}
					Label {
						textFormat: Text.PlainText
						visible: !!ruleDelegate.modelData.inherited
						text: qsTr("Inherited from parent")
						color: Theme.textMuted
						font.pixelSize: 11
						verticalAlignment: Text.AlignVCenter
					}
					ModernButton {
						text: qsTr("Remove rule")
						dense: true
						tone: "danger"
						enabled: !ruleDelegate.modelData.inherited
						onClicked: {
							const model = root.cloneModel()
							model.acls.splice(ruleDelegate.index, 1)
							root.publish(model)
						}
					}
				}
				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: qsTr("Permissions")
					color: Theme.textStrong
					font.pixelSize: 11
					font.bold: true
				}
				Flow {
					id: permissionsFlow
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Math.max(6, Theme.spacing - 2)
					Repeater {
						model: root.aclModel.permissions || []
						delegate: Rectangle {
							required property var modelData
							width: root.compactLayout ? permissionsFlow.width
								: Math.max(210, Math.floor((permissionsFlow.width - permissionsFlow.spacing) / 2))
							height: permissionContent.implicitHeight + 12
							color: Theme.panel
							border.color: Theme.divider
							radius: 6
							ColumnLayout {
								id: permissionContent
								anchors.fill: parent
								anchors.margins: 6
								spacing: 3
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: modelData.label
									color: Theme.textMain
									font.pixelSize: 11
									font.bold: true
									elide: Text.ElideRight
								}
								RowLayout {
									Layout.fillWidth: true
									ModernCheckBox {
										text: qsTr("Allow")
										checked: root.contains(ruleDelegate.modelData.allow, modelData.id)
										enabled: !ruleDelegate.modelData.inherited
										onToggled: root.togglePermission(ruleDelegate.index, "allow", modelData.id, checked)
									}
									ModernCheckBox {
										text: qsTr("Deny")
										checked: root.contains(ruleDelegate.modelData.deny, modelData.id)
										enabled: !ruleDelegate.modelData.inherited
										onToggled: root.togglePermission(ruleDelegate.index, "deny", modelData.id, checked)
									}
									Item { Layout.fillWidth: true }
								}
							}
						}
					}
				}
			}
			onClicked: ruleList.currentIndex = index
		}
	}
	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: visible ? 72 : 0
		visible: root.rules.length === 0
		color: Theme.strip
		border.color: Theme.divider
		radius: Theme.innerRadius
		Label {
			textFormat: Text.PlainText
			anchors.centerIn: parent
			text: qsTr("No explicit access rules for this room.")
			color: Theme.textMuted
		}
	}
}
