import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var field: ({ "id": "", "value": ({}) })
	property var aclModel: field.value || ({})
	property bool visualFixtureMode: false
	property Item accessibilityViewport: null
	property int selectedGroupIndex: -1
	property int selectedRuleIndex: -1
	property int editorFocusGeneration: 0
	property bool explicitRowFocusPending: false

	readonly property var groups: aclModel.groups || []
	readonly property var rules: aclModel.acls || []
	readonly property var selectedGroup: selectedGroupIndex >= 0
		&& selectedGroupIndex < groups.length ? groups[selectedGroupIndex] : null
	readonly property var selectedRule: selectedRuleIndex >= 0
		&& selectedRuleIndex < rules.length ? rules[selectedRuleIndex] : null
	readonly property bool compactLayout: width < 700
	readonly property int navigatorRowHeight: compactLayout ? 46 : 48
	readonly property int navigatorSpacing: Math.max(4, Theme.space1)
	readonly property int navigatorVisibleRows: compactLayout ? 3 : 4
	readonly property int groupListLimit: navigatorRowHeight * navigatorVisibleRows
		+ navigatorSpacing * (navigatorVisibleRows - 1)
	readonly property int ruleListLimit: groupListLimit

	function itemFullyInsideAccessibilityViewport(item) {
		if (!accessibilityViewport)
			return true
		if (!item || !item.visible || item.width <= 0 || item.height <= 0
				|| accessibilityViewport.width <= 0 || accessibilityViewport.height <= 0)
			return false
		try {
			// mapToItem() is not a notifying dependency. Track both the owning dialog
			// scroll offset and local geometry so permission semantics follow the
			// sticky footer instead of remaining promoted behind it.
			const viewportFlickable = accessibilityViewport.contentItem || null
			const dependency = Number(viewportFlickable ? viewportFlickable.contentY || 0 : 0)
				+ Number(item.x || 0) + Number(item.y || 0)
				+ Number(accessibilityViewport.width || 0) + Number(accessibilityViewport.height || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(accessibilityViewport, 0, 0)
			const tolerance = 0.5
			return point.x >= -tolerance && point.y >= -tolerance
				&& point.x + item.width <= accessibilityViewport.width + tolerance
				&& point.y + item.height <= accessibilityViewport.height + tolerance
		} catch (error) {
			return false
		}
	}

	objectName: "aclEditor_" + String((field || {}).id || "")
	width: parent ? parent.width : 0
	spacing: Theme.spacing
	Component {
		id: aclTextCursorDelegate
		Item {
			width: 1
			Rectangle {
				anchors.fill: parent
				visible: !root.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}

	onFieldChanged: {
		// Adding a row owns the next focus hand-off. A controller republish can
		// otherwise restore the editor that was active before the Add action over
		// the newly-created row one event-loop turn later.
		const focusObjectName = explicitRowFocusPending ? "" : activeEditorObjectName()
		const preservedGroupSelection = selectedGroupIndex
		const preservedRuleSelection = selectedRuleIndex
		aclModel = field.value || ({})
		// ListView may normalize currentIndex to zero synchronously when its model
		// is replaced. Restore the actual product selection before applying our
		// default-selection policy so an inherited first row cannot win by timing.
		selectedGroupIndex = preservedGroupSelection
		selectedRuleIndex = preservedRuleSelection
		// Keep the selected-detail contract synchronous for callers that replace a
		// complete DTO and immediately query the active editor. The deferred pass
		// still catches ListView polish/model publication one turn later.
		restoreSelections()
		scheduleSelectionRestore()
		if (focusObjectName.length > 0)
			scheduleEditorFocusRestore(focusObjectName)
	}

	function cloneModel() {
		return JSON.parse(JSON.stringify(aclModel || {}))
	}

	function clampedSelection(index, count) {
		if (count <= 0)
			return -1
		return Math.max(0, Math.min(count - 1, index < 0 ? 0 : index))
	}

	function preferredRuleSelection(index) {
		if (rules.length <= 0)
			return -1
		if (index >= 0 && index < rules.length)
			return index
		for (let ruleIndex = 0; ruleIndex < rules.length; ++ruleIndex) {
			if (!(rules[ruleIndex] || {}).inherited)
				return ruleIndex
		}
		return 0
	}

	function restoreSelections() {
		selectedGroupIndex = clampedSelection(selectedGroupIndex, groups.length)
		selectedRuleIndex = preferredRuleSelection(selectedRuleIndex)
		groupList.currentIndex = selectedGroupIndex
		ruleList.currentIndex = selectedRuleIndex
	}

	function scheduleSelectionRestore() {
		Qt.callLater(root.restoreSelections)
	}

	function isDescendantOf(target, ancestor) {
		let current = target
		while (current) {
			if (current === ancestor)
				return true
			current = current.parent
		}
		return false
	}

	function activeEditorObjectName() {
		const window = root.Window.window
		let current = window ? window.activeFocusItem : null
		while (current && isDescendantOf(current, root)) {
			const name = String(current.objectName || "")
			if (name.length > 0 && name !== root.objectName)
				return name
			if (current === root)
				break
			current = current.parent
		}
		return ""
	}

	function focusObjectInTree(target, objectName) {
		if (!target)
			return null
		if (String(target.objectName || "") === objectName)
			return target
		const children = target.children || []
		for (let index = 0; index < children.length; ++index) {
			const match = focusObjectInTree(children[index], objectName)
			if (match)
				return match
		}
		return null
	}

	function scheduleEditorFocusRestore(objectName) {
		const requested = String(objectName || "")
		if (requested.length === 0)
			return
		const generation = ++editorFocusGeneration
		const restore = function(retriesRemaining) {
			if (generation !== root.editorFocusGeneration)
				return
			const target = root.focusObjectInTree(root, requested)
			if (target && target.visible !== false && target.enabled !== false && target.forceActiveFocus) {
				target.forceActiveFocus(Qt.TabFocusReason)
				return
			}
			if (retriesRemaining > 0)
				Qt.callLater(function() { restore(retriesRemaining - 1) })
		}
		Qt.callLater(function() { restore(4) })
	}

	function primaryEditorObjectName(list, index) {
		return list === groupList ? "aclGroupName_" + index
			: list === ruleList ? "aclRuleTargetType_" + index : ""
	}

	function focusListRow(list, index) {
		if (!list || index < 0 || index >= list.count) {
			explicitRowFocusPending = false
			return
		}
		const generation = ++editorFocusGeneration
		const requestedObjectName = primaryEditorObjectName(list, index)
		list.currentIndex = index
		list.positionViewAtIndex(index, ListView.Contain)
		list.forceActiveFocus(Qt.TabFocusReason)
		const focusEditor = function(retriesRemaining) {
			if (generation !== root.editorFocusGeneration)
				return
			const window = root.Window.window
			const activeItem = window ? window.activeFocusItem : null
			if (activeItem && !root.isDescendantOf(activeItem, root)) {
				root.explicitRowFocusPending = false
				return
			}
			// If the user already moved to another editor control, that newer intent
			// wins over the deferred Add/remove hand-off.
			if (activeItem && activeItem !== list && !root.isDescendantOf(activeItem, list)) {
				root.explicitRowFocusPending = false
				return
			}
			const target = root.focusObjectInTree(root, requestedObjectName)
			if (target && target.visible !== false && target.enabled !== false
					&& target.forceActiveFocus) {
				target.forceActiveFocus(Qt.TabFocusReason)
				root.explicitRowFocusPending = false
			} else if (retriesRemaining > 0) {
				Qt.callLater(function() { focusEditor(retriesRemaining - 1) })
			} else {
				root.explicitRowFocusPending = false
			}
		}
		Qt.callLater(function() { focusEditor(8) })
	}

	function publish(model, preserveEditorFocus) {
		const focusObjectName = preserveEditorFocus === false || explicitRowFocusPending
			? "" : activeEditorObjectName()
		if (groupList.currentIndex >= 0)
			selectedGroupIndex = groupList.currentIndex
		if (ruleList.currentIndex >= 0)
			selectedRuleIndex = ruleList.currentIndex
		aclModel = model
		dialogState.updateField(field.id, model)
		scheduleSelectionRestore()
		if (focusObjectName.length > 0)
			scheduleEditorFocusRestore(focusObjectName)
	}

	function contains(list, value) {
		return (list || []).indexOf(value) >= 0
	}

	function normalizedMemberIds(values) {
		const result = []
		const seen = ({})
		const source = values || []
		for (let index = 0; index < source.length; ++index) {
			const id = Number(source[index])
			const key = String(id)
			if (isFinite(id) && id >= 0 && Math.floor(id) === id && !seen[key]) {
				seen[key] = true
				result.push(id)
			}
		}
		return result
	}

	function updateGroupMembers(groupIndex, key, values, requestedFocusObjectName) {
		const model = cloneModel()
		const group = model.groups[groupIndex]
		const oppositeKey = key === "add" ? "remove" : "add"
		const next = normalizedMemberIds(values)
		const selected = ({})
		for (let index = 0; index < next.length; ++index)
			selected[String(next[index])] = true
		group[key] = next
		group[oppositeKey] = normalizedMemberIds(group[oppositeKey]).filter(function(userId) {
			return !selected[String(userId)]
		})
		delete group.addText
		delete group.removeText
		explicitRowFocusPending = true
		publish(model, false)
		explicitRowFocusPending = false
		if (String(requestedFocusObjectName || "").length > 0)
			scheduleEditorFocusRestore(requestedFocusObjectName)
	}

	function userOptionIndex(userId, label) {
		const options = aclModel.userOptions || []
		for (let index = 0; index < options.length; ++index) {
			const option = options[index] || ({})
			if (Number(option.value) === Number(userId) && Number(userId) >= 0)
				return index
			if (String(option.label || "").toLocaleLowerCase()
					=== String(label || "").toLocaleLowerCase())
				return index
		}
		return -1
	}

	function togglePermission(ruleIndex, key, permissionId, checked) {
		const model = cloneModel()
		const rule = model.acls[ruleIndex]
		let values = rule[key] || []
		const valueIndex = values.indexOf(permissionId)
		if (checked && valueIndex < 0)
			values.push(permissionId)
		if (!checked && valueIndex >= 0)
			values.splice(valueIndex, 1)
		rule[key] = values
		publish(model)
	}

	function liveDelegateCount(list, marker) {
		let live = 0
		const objects = list && list.contentItem ? list.contentItem.children : []
		for (let index = 0; index < objects.length; ++index) {
			if (objects[index][marker])
				++live
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
			objectName: "dialogField_" + String((root.field || {}).id || "")
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
				cursorDelegate: aclTextCursorDelegate
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
				objectName: "aclGroupCountLabel"
				textFormat: Text.PlainText
				text: root.groups.length === 1 ? qsTr("1 group")
					: qsTr("%1 groups").arg(root.groups.length)
				color: Theme.textMuted
				font.pixelSize: 11
			}
		}

		ModernButton {
			id: addGroupButton
			objectName: "aclAddGroup"
			text: qsTr("Add group")
			dense: true
			onClicked: {
				const model = root.cloneModel()
				if (!model.groups)
					model.groups = []
				model.groups.push({ "name": "", "inherit": true, "inheritable": true,
					"inherited": false, "add": [], "remove": [], "inheritedMembers": [] })
				root.explicitRowFocusPending = true
				root.publish(model, false)
				root.selectedGroupIndex = model.groups.length - 1
				Qt.callLater(function() {
					groupList.positionViewAtEnd()
					root.focusListRow(groupList, root.selectedGroupIndex)
				})
			}
		}
	}

	GridLayout {
		Layout.fillWidth: true
		Layout.minimumWidth: 0
		columns: root.compactLayout ? 1 : 2
		columnSpacing: Theme.spacing
		rowSpacing: Theme.spacing

		ColumnLayout {
			Layout.fillWidth: root.compactLayout
			Layout.preferredWidth: root.compactLayout ? -1 : Math.min(280, root.width * 0.32)
			Layout.alignment: Qt.AlignTop
			spacing: 0

			ListView {
				id: groupList
				objectName: "aclGroupList"
				Layout.fillWidth: true
				Layout.preferredHeight: visible
					? Math.min(root.groupListLimit,
						root.groups.length * root.navigatorRowHeight
						+ Math.max(0, root.groups.length - 1) * root.navigatorSpacing) : 0
				Layout.maximumHeight: root.groupListLimit
				visible: count > 0
				model: root.groups
				clip: true
				spacing: root.navigatorSpacing
				cacheBuffer: 0
				reuseItems: true
				boundsBehavior: Flickable.StopAtBounds
				activeFocusOnTab: activeFocus || visible
				keyNavigationEnabled: true
				keyNavigationWraps: false
				currentIndex: -1
				onCountChanged: root.scheduleSelectionRestore()
				onCurrentIndexChanged: if (currentIndex >= 0)
					root.selectedGroupIndex = currentIndex
				Accessible.role: Accessible.List
				Accessible.name: qsTr("Access-control groups")
				ScrollBar.vertical: ModernScrollBar {
					objectName: "aclGroupListScrollBar"
					parent: groupList
					anchors.top: groupList.top
					anchors.right: groupList.right
					anchors.bottom: groupList.bottom
				}
				function liveDelegateCount() {
					return root.liveDelegateCount(groupList, "isAclGroupDelegate")
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Home && count > 0) {
						currentIndex = 0
						positionViewAtBeginning()
						event.accepted = true
					} else if (event.key === Qt.Key_End && count > 0) {
						currentIndex = count - 1
						positionViewAtEnd()
						event.accepted = true
					} else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
							|| event.key === Qt.Key_Space) && currentIndex >= 0) {
						root.focusListRow(groupList, currentIndex)
						event.accepted = true
					}
				}

				delegate: ItemDelegate {
					id: groupDelegate
					required property var modelData
					required property int index
					objectName: "aclGroupRow_" + index
					property bool isAclGroupDelegate: true
					width: ListView.view.width
					height: root.navigatorRowHeight
					padding: Theme.space2
					hoverEnabled: true
					highlighted: ListView.isCurrentItem || hovered
					readonly property bool current: ListView.isCurrentItem
					readonly property bool keyboardFocused: activeFocus
						|| (ListView.view.activeFocus && current)
					Accessible.role: Accessible.ListItem
					Accessible.name: modelData.name || qsTr("New group")
					Accessible.description: modelData.inherited
						? qsTr("Inherited group")
						: qsTr("Editable group, %1 added and %2 removed members")
							.arg((modelData.add || []).length).arg((modelData.remove || []).length)
					Accessible.selected: current
					function focusPrimaryEditor() {
						root.focusListRow(groupList, index)
					}
					background: Rectangle {
						color: groupDelegate.down ? Theme.accentSubtle
							: groupDelegate.current ? Theme.selected
							: groupDelegate.hovered ? Theme.surfaceHover : Theme.strip
						border.color: groupDelegate.keyboardFocused ? Theme.focus
							: groupDelegate.current ? Theme.accent : Theme.divider
						border.width: groupDelegate.keyboardFocused ? Theme.focusRingWidth : 1
						radius: Theme.innerRadius
					}
					contentItem: RowLayout {
						spacing: Theme.space2
						Accessible.ignored: true
						ColumnLayout {
							Layout.fillWidth: true
							Layout.minimumWidth: 0
							spacing: 0
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: groupDelegate.modelData.name || qsTr("New group")
								color: Theme.textStrong
								font.weight: Font.DemiBold
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: groupDelegate.modelData.inherited ? qsTr("Inherited")
									: qsTr("%1 added · %2 removed")
										.arg((groupDelegate.modelData.add || []).length)
										.arg((groupDelegate.modelData.remove || []).length)
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
						ModernIcon {
							name: "next"
							color: groupDelegate.current ? Theme.accent : Theme.textMuted
							size: 16
							Accessible.ignored: true
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
				Accessible.role: Accessible.StaticText
				Accessible.name: qsTr("No explicit groups for this room.")
				Label {
					anchors.centerIn: parent
					textFormat: Text.PlainText
					text: qsTr("No explicit groups for this room.")
					color: Theme.textMuted
					Accessible.ignored: true
				}
			}
		}

		Rectangle {
			id: groupEditorCard
			objectName: "aclSelectedGroupEditor"
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			Layout.alignment: Qt.AlignTop
			Layout.preferredHeight: visible ? groupEditorContent.implicitHeight + Theme.space4 : 0
			visible: root.selectedGroup !== null
			color: Theme.panel
			border.color: Theme.divider
			border.width: 1
			radius: Theme.innerRadius
			Accessible.role: Accessible.Pane
			Accessible.name: root.selectedGroup
				? qsTr("Edit group %1").arg(root.selectedGroup.name || qsTr("New group")) : ""

			ColumnLayout {
				id: groupEditorContent
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space2
				spacing: Math.max(6, Theme.spacing - 2)

				RowLayout {
					Layout.fillWidth: true
					ModernTextField {
						id: groupNameEditor
						objectName: "aclGroupName_" + root.selectedGroupIndex
						Layout.fillWidth: true
						cursorDelegate: aclTextCursorDelegate
						text: root.selectedGroup ? root.selectedGroup.name || "" : ""
						enabled: !!root.selectedGroup && !root.selectedGroup.inherited
						placeholderText: qsTr("Group name")
						Accessible.name: qsTr("Group name")
						onEditingFinished: {
							if (!root.selectedGroup)
								return
							const model = root.cloneModel()
							model.groups[root.selectedGroupIndex].name = text
							root.publish(model)
						}
					}
					Label {
						visible: !!root.selectedGroup && !!root.selectedGroup.inherited
						textFormat: Text.PlainText
						text: qsTr("Inherited")
						color: Theme.textMuted
						font.pixelSize: 11
					}
					ModernButton {
						objectName: "aclRemoveGroup_" + root.selectedGroupIndex
						text: qsTr("Remove")
						dense: true
						tone: "danger"
						enabled: !!root.selectedGroup && !root.selectedGroup.inherited
						onClicked: {
							const removedIndex = root.selectedGroupIndex
							const model = root.cloneModel()
							model.groups.splice(removedIndex, 1)
							const nextIndex = root.clampedSelection(removedIndex, model.groups.length)
							root.explicitRowFocusPending = true
							root.publish(model, false)
							root.selectedGroupIndex = nextIndex
							Qt.callLater(function() {
								if (model.groups.length > 0)
									root.focusListRow(groupList, nextIndex)
								else {
									root.explicitRowFocusPending = false
									addGroupButton.forceActiveFocus(Qt.TabFocusReason)
								}
							})
						}
					}
				}

				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Math.max(6, Theme.spacing - 2)
					ModernCheckBox {
						objectName: "aclGroupInherit_" + root.selectedGroupIndex
						text: qsTr("Inherit members")
						checked: !!root.selectedGroup && !!root.selectedGroup.inherit
						enabled: !!root.selectedGroup && !root.selectedGroup.inherited
						onToggled: {
							const model = root.cloneModel()
							model.groups[root.selectedGroupIndex].inherit = checked
							root.publish(model)
						}
					}
					ModernCheckBox {
						objectName: "aclGroupInheritable_" + root.selectedGroupIndex
						text: qsTr("Available to child rooms")
						checked: !!root.selectedGroup && !!root.selectedGroup.inheritable
						enabled: !!root.selectedGroup && !root.selectedGroup.inherited
						onToggled: {
							const model = root.cloneModel()
							model.groups[root.selectedGroupIndex].inheritable = checked
							root.publish(model)
						}
					}
				}

				GridLayout {
					Layout.fillWidth: true
					columns: root.compactLayout ? 1 : 2
					columnSpacing: Theme.spacing
					rowSpacing: Math.max(6, Theme.spacing - 2)
					AclMemberEditor {
						objectName: "aclGroupAddedUsers_" + root.selectedGroupIndex
						objectNamePrefix: objectName
						title: qsTr("Added members")
						pickerLabel: qsTr("Add to added members")
						excludedListName: qsTr("Removed members")
						memberIds: root.selectedGroup ? root.selectedGroup.add || [] : []
						excludedIds: root.selectedGroup ? root.selectedGroup.remove || [] : []
						userOptions: root.aclModel.userOptions || []
						editable: !!root.selectedGroup && !root.selectedGroup.inherited
						compact: root.compactLayout
						Layout.fillWidth: true
						onMembersEdited: (memberIds, requestedFocusObjectName) =>
							root.updateGroupMembers(root.selectedGroupIndex, "add", memberIds,
								requestedFocusObjectName)
					}
					AclMemberEditor {
						objectName: "aclGroupRemovedUsers_" + root.selectedGroupIndex
						objectNamePrefix: objectName
						title: qsTr("Removed members")
						pickerLabel: qsTr("Add to removed members")
						excludedListName: qsTr("Added members")
						memberIds: root.selectedGroup ? root.selectedGroup.remove || [] : []
						excludedIds: root.selectedGroup ? root.selectedGroup.add || [] : []
						userOptions: root.aclModel.userOptions || []
						editable: !!root.selectedGroup && !root.selectedGroup.inherited
						compact: root.compactLayout
						Layout.fillWidth: true
						onMembersEdited: (memberIds, requestedFocusObjectName) =>
							root.updateGroupMembers(root.selectedGroupIndex, "remove", memberIds,
								requestedFocusObjectName)
					}
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
				text: qsTr("Access rules")
				color: Theme.textStrong
				font.pixelSize: 13
				font.bold: true
				Accessible.role: Accessible.Heading
			}
			Label {
				objectName: "aclRuleCountLabel"
				textFormat: Text.PlainText
				text: root.rules.length === 1 ? qsTr("1 rule")
					: qsTr("%1 rules").arg(root.rules.length)
				color: Theme.textMuted
				font.pixelSize: 11
			}
		}

		ModernButton {
			id: addRuleButton
			objectName: "aclAddRule"
			text: qsTr("Add rule")
			dense: true
			tone: "accent"
			onClicked: {
				const model = root.cloneModel()
				if (!model.acls)
					model.acls = []
				model.acls.push({ "targetType": "group", "target": "all", "userId": -1,
					"applyHere": true, "applySubs": true, "inherited": false,
					"allow": [], "deny": [] })
				root.explicitRowFocusPending = true
				root.publish(model, false)
				root.selectedRuleIndex = model.acls.length - 1
				Qt.callLater(function() {
					ruleList.positionViewAtEnd()
					root.focusListRow(ruleList, root.selectedRuleIndex)
				})
			}
		}
	}

	GridLayout {
		Layout.fillWidth: true
		Layout.minimumWidth: 0
		columns: root.compactLayout ? 1 : 2
		columnSpacing: Theme.spacing
		rowSpacing: Theme.spacing

		ColumnLayout {
			Layout.fillWidth: root.compactLayout
			Layout.preferredWidth: root.compactLayout ? -1 : Math.min(280, root.width * 0.32)
			Layout.alignment: Qt.AlignTop
			spacing: 0

			ListView {
				id: ruleList
				objectName: "aclRuleList"
				Layout.fillWidth: true
				Layout.preferredHeight: visible
					? Math.min(root.ruleListLimit,
						root.rules.length * root.navigatorRowHeight
						+ Math.max(0, root.rules.length - 1) * root.navigatorSpacing) : 0
				Layout.maximumHeight: root.ruleListLimit
				visible: count > 0
				model: root.rules
				clip: true
				spacing: root.navigatorSpacing
				cacheBuffer: 0
				reuseItems: true
				boundsBehavior: Flickable.StopAtBounds
				activeFocusOnTab: activeFocus || visible
				keyNavigationEnabled: true
				keyNavigationWraps: false
				currentIndex: -1
				onCountChanged: root.scheduleSelectionRestore()
				onCurrentIndexChanged: if (currentIndex >= 0)
					root.selectedRuleIndex = currentIndex
				Accessible.role: Accessible.List
				Accessible.name: qsTr("Access rules")
				ScrollBar.vertical: ModernScrollBar {
					objectName: "aclRuleListScrollBar"
					parent: ruleList
					anchors.top: ruleList.top
					anchors.right: ruleList.right
					anchors.bottom: ruleList.bottom
				}
				function liveDelegateCount() {
					return root.liveDelegateCount(ruleList, "isAclRuleDelegate")
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Home && count > 0) {
						currentIndex = 0
						positionViewAtBeginning()
						event.accepted = true
					} else if (event.key === Qt.Key_End && count > 0) {
						currentIndex = count - 1
						positionViewAtEnd()
						event.accepted = true
					} else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
							|| event.key === Qt.Key_Space) && currentIndex >= 0) {
						root.focusListRow(ruleList, currentIndex)
						event.accepted = true
					}
				}

				delegate: ItemDelegate {
					id: ruleDelegate
					required property var modelData
					required property int index
					objectName: "aclRuleRow_" + index
					property bool isAclRuleDelegate: true
					width: ListView.view.width
					height: root.navigatorRowHeight
					padding: Theme.space2
					hoverEnabled: true
					highlighted: ListView.isCurrentItem || hovered
					readonly property bool current: ListView.isCurrentItem
					readonly property bool keyboardFocused: activeFocus
						|| (ListView.view.activeFocus && current)
					Accessible.role: Accessible.ListItem
					Accessible.name: qsTr("Rule for %1").arg(modelData.target || qsTr("all users"))
					Accessible.description: modelData.inherited ? qsTr("Inherited rule")
						: qsTr("Editable %1 rule").arg(modelData.targetType === "user"
							? qsTr("user") : qsTr("group"))
					Accessible.selected: current
					function focusPrimaryEditor() {
						root.focusListRow(ruleList, index)
					}
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
					contentItem: RowLayout {
						spacing: Theme.space2
						Accessible.ignored: true
						ColumnLayout {
							Layout.fillWidth: true
							Layout.minimumWidth: 0
							spacing: 0
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: ruleDelegate.modelData.target || qsTr("All users")
								color: Theme.textStrong
								font.weight: Font.DemiBold
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: ruleDelegate.modelData.inherited ? qsTr("Inherited")
									: ruleDelegate.modelData.targetType === "user" ? qsTr("User rule") : qsTr("Group rule")
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
						ModernIcon {
							name: "next"
							color: ruleDelegate.current ? Theme.accent : Theme.textMuted
							size: 16
							Accessible.ignored: true
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
				Accessible.role: Accessible.StaticText
				Accessible.name: qsTr("No explicit access rules for this room.")
				Label {
					anchors.centerIn: parent
					textFormat: Text.PlainText
					text: qsTr("No explicit access rules for this room.")
					color: Theme.textMuted
					Accessible.ignored: true
				}
			}
		}

		Rectangle {
			id: ruleEditorCard
			objectName: "aclSelectedRuleEditor"
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			Layout.alignment: Qt.AlignTop
			Layout.preferredHeight: visible ? ruleEditorContent.implicitHeight + Theme.space4 : 0
			visible: root.selectedRule !== null
			color: Theme.panel
			border.color: Theme.divider
			border.width: 1
			radius: Theme.innerRadius
			Accessible.role: Accessible.Pane
			Accessible.name: root.selectedRule
				? qsTr("Edit rule for %1").arg(root.selectedRule.target || qsTr("all users")) : ""

			ColumnLayout {
				id: ruleEditorContent
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space2
				spacing: Math.max(7, Theme.spacing - 1)

				GridLayout {
					Layout.fillWidth: true
					columns: root.compactLayout ? 1 : 2
					columnSpacing: Theme.spacing
					rowSpacing: Math.max(6, Theme.spacing - 2)
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label {
							textFormat: Text.PlainText
							text: qsTr("Applies to")
							color: Theme.textMuted
							font.pixelSize: 11
						}
						ModernComboBox {
							id: targetTypeEditor
							objectName: "aclRuleTargetType_" + root.selectedRuleIndex
							Layout.fillWidth: true
							model: [ { label: qsTr("Group"), value: "group" },
								{ label: qsTr("User"), value: "user" } ]
							textRole: "label"
							valueRole: "value"
							currentIndex: root.selectedRule && root.selectedRule.targetType === "user" ? 1 : 0
							enabled: !!root.selectedRule && !root.selectedRule.inherited
							onActivated: {
								const model = root.cloneModel()
								model.acls[root.selectedRuleIndex].targetType = currentValue
								root.publish(model)
							}
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label {
							textFormat: Text.PlainText
							text: root.selectedRule && root.selectedRule.targetType === "user"
								? qsTr("User") : qsTr("Group")
							color: Theme.textMuted
							font.pixelSize: 11
						}
						ModernComboBox {
							id: userTargetEditor
							objectName: "aclRuleUserTarget_" + root.selectedRuleIndex
							Layout.fillWidth: true
							visible: !!root.selectedRule && root.selectedRule.targetType === "user"
							enabled: !!root.selectedRule && !root.selectedRule.inherited && count > 0
							model: root.aclModel.userOptions || []
							textRole: "label"
							valueRole: "value"
							currentIndex: root.selectedRule
								? root.userOptionIndex(root.selectedRule.userId, root.selectedRule.target) : -1
							displayText: currentIndex >= 0 ? currentText : qsTr("Choose a registered user")
							Accessible.name: qsTr("ACL user target")
							onActivated: {
								const option = optionAt(currentIndex)
								const model = root.cloneModel()
								model.acls[root.selectedRuleIndex].userId = Number(currentValue)
								model.acls[root.selectedRuleIndex].target = String(option.label || "")
								root.publish(model)
							}
						}
						ModernTextField {
							objectName: "aclRuleTarget_" + root.selectedRuleIndex
							Layout.fillWidth: true
							cursorDelegate: aclTextCursorDelegate
							visible: !!root.selectedRule && root.selectedRule.targetType !== "user"
							text: root.selectedRule ? root.selectedRule.target || "" : ""
							enabled: !!root.selectedRule && !root.selectedRule.inherited
							placeholderText: qsTr("Group name")
							onEditingFinished: {
								const model = root.cloneModel()
								model.acls[root.selectedRuleIndex].target = text
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
						objectName: "aclRuleApplyHere_" + root.selectedRuleIndex
						text: qsTr("This room")
						checked: !!root.selectedRule && !!root.selectedRule.applyHere
						enabled: !!root.selectedRule && !root.selectedRule.inherited
						onToggled: {
							const model = root.cloneModel()
							model.acls[root.selectedRuleIndex].applyHere = checked
							root.publish(model)
						}
					}
					ModernCheckBox {
						objectName: "aclRuleApplySubs_" + root.selectedRuleIndex
						text: qsTr("Child rooms")
						checked: !!root.selectedRule && !!root.selectedRule.applySubs
						enabled: !!root.selectedRule && !root.selectedRule.inherited
						onToggled: {
							const model = root.cloneModel()
							model.acls[root.selectedRuleIndex].applySubs = checked
							root.publish(model)
						}
					}
					Label {
						visible: !!root.selectedRule && !!root.selectedRule.inherited
						textFormat: Text.PlainText
						text: qsTr("Inherited from parent")
						color: Theme.textMuted
						font.pixelSize: 11
						verticalAlignment: Text.AlignVCenter
					}
					ModernButton {
						objectName: "aclRemoveRule_" + root.selectedRuleIndex
						text: qsTr("Remove rule")
						dense: true
						tone: "danger"
						enabled: !!root.selectedRule && !root.selectedRule.inherited
						onClicked: {
							const removedIndex = root.selectedRuleIndex
							const model = root.cloneModel()
							model.acls.splice(removedIndex, 1)
							const nextIndex = root.clampedSelection(removedIndex, model.acls.length)
							root.explicitRowFocusPending = true
							root.publish(model, false)
							root.selectedRuleIndex = nextIndex
							Qt.callLater(function() {
								if (model.acls.length > 0)
									root.focusListRow(ruleList, nextIndex)
								else {
									root.explicitRowFocusPending = false
									addRuleButton.forceActiveFocus(Qt.TabFocusReason)
								}
							})
						}
					}
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
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
							id: permissionCard
							required property var modelData
							required property int index
							objectName: "aclPermissionCard_" + String(modelData.id || index)
							readonly property bool accessibilityExposed:
								root.itemFullyInsideAccessibilityViewport(permissionCard)
							width: root.compactLayout ? permissionsFlow.width
								: Math.max(210, Math.floor((permissionsFlow.width - permissionsFlow.spacing) / 2))
							height: permissionContent.implicitHeight + 12
							color: Theme.surfaceRaised
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
										objectName: "aclRulePermissionAllow_" + root.selectedRuleIndex
											+ "_" + String(modelData.id || index)
										text: qsTr("Allow")
										checked: root.selectedRule
											? root.contains(root.selectedRule.allow, modelData.id) : false
										enabled: !!root.selectedRule && !root.selectedRule.inherited
										onToggled: root.togglePermission(root.selectedRuleIndex,
											"allow", modelData.id, checked)
									}
									ModernCheckBox {
										objectName: "aclRulePermissionDeny_" + root.selectedRuleIndex
											+ "_" + String(modelData.id || index)
										text: qsTr("Deny")
										checked: root.selectedRule
											? root.contains(root.selectedRule.deny, modelData.id) : false
										enabled: !!root.selectedRule && !root.selectedRule.inherited
										onToggled: root.togglePermission(root.selectedRuleIndex,
											"deny", modelData.id, checked)
									}
									Item { Layout.fillWidth: true }
								}
							}
							ModalAccessibilityBarrier {
								id: permissionAccessibilityBarrier
								objectName: "aclPermissionAccessibilityBarrier_"
									+ String(permissionCard.modelData.id || permissionCard.index)
								active: !permissionCard.accessibilityExposed
								targets: [ permissionCard ]
							}
						}
					}
				}
			}
		}
	}
}
