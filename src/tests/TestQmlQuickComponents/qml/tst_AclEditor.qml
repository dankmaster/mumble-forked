import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "AclEditor"
	when: windowShown
	visible: true
	width: 1100
	height: 1100
	property int editorWidth: 920
	property int accessibilityViewportHeight: 1060
	property var aclField: ({ "id": "acl", "value": ({}) })
	Item {
		id: editorAccessibilityViewport
		width: loader.width
		height: testCase.accessibilityViewportHeight
	}

	Loader {
		id: loader
		width: testCase.editorWidth
		height: 1060
		source: "qrc:/qml-shell/AclEditor.qml"
		onLoaded: {
			item.accessibilityViewport = editorAccessibilityViewport
			item.field = testCase.aclField
		}
	}

	function makeGroups(count) {
		const groups = []
		for (let index = 0; index < count; ++index) {
			groups.push({ "name": "Group " + index, "inherit": index % 2 === 0,
				"inheritable": true, "inherited": false, "add": [index], "remove": [] })
		}
		return groups
	}

	function makeRules(count) {
		const rules = []
		for (let index = 0; index < count; ++index) {
			rules.push({ "targetType": "group", "target": "Group " + index,
				"applyHere": true, "applySubs": index % 2 === 0, "inherited": false,
				"allow": ["speak"], "deny": [] })
		}
		return rules
	}

	function editableGroup(addMembers, removedMembers) {
		return {
			"name": "Moderators",
			"inherit": true,
			"inheritable": true,
			"inherited": false,
			"add": addMembers || [],
			"remove": removedMembers || [],
			"inheritedMembers": []
		}
	}

	function optionIndex(combo, userId) {
		for (let index = 0; index < combo.count; ++index) {
			if (Number(combo.optionAt(index).value) === Number(userId))
				return index
		}
		return -1
	}

	function setAcl(groups, rules) {
		testCase.aclField = {
			"id": "acl",
			"value": {
				"inheritAcls": true,
				"password": "",
				"groups": groups,
				"acls": rules,
				"userOptions": [
					{ "id": 101, "value": 101, "label": "Alice", "subtitle": "ID 101" },
					{ "id": 202, "value": 202, "label": "Bob", "subtitle": "ID 202" }
				],
				"permissions": [
					{ "id": "speak", "label": "Speak" },
					{ "id": "enter", "label": "Enter room" },
					{ "id": 2097152, "label": "Use tools" }
				]
			}
		}
		loader.item.field = testCase.aclField
	}

	function init() {
		verify(loader.item !== null)
		testCase.editorWidth = 920
		testCase.accessibilityViewportHeight = 1060
		loader.item.selectedGroupIndex = -1
		loader.item.selectedRuleIndex = -1
		++loader.item.editorFocusGeneration
		loader.item.forceActiveFocus()
		setAcl(makeGroups(2), makeRules(2))
	}

	function test_partially_clipped_permission_card_leaves_accessibility_viewport() {
		const editor = loader.item
		let permissionCard = null
		let allow = null
		let barrier = null
		tryVerify(function() {
			permissionCard = findChild(editor, "aclPermissionCard_speak")
			allow = findChild(editor, "aclRulePermissionAllow_0_speak")
			barrier = findChild(editor, "aclPermissionAccessibilityBarrier_speak")
			return permissionCard !== null && allow !== null && barrier !== null
		})
		const point = permissionCard.mapToItem(testCase, 0, 0)
		testCase.accessibilityViewportHeight = point.y + permissionCard.height - 2
		tryCompare(permissionCard, "accessibilityExposed", false)
		tryCompare(barrier, "active", true)
		tryCompare(allow.Accessible, "ignored", true)

		testCase.accessibilityViewportHeight = point.y + permissionCard.height + 2
		tryCompare(permissionCard, "accessibilityExposed", true)
		tryCompare(barrier, "active", false)
		tryCompare(allow.Accessible, "ignored", false)
	}

	function test_tools_acl_permission_card_round_trips_numeric_bit() {
		const editor = loader.item
		let permissionCard = null
		let allow = null
		let deny = null
		tryVerify(function() {
			permissionCard = findChild(editor, "aclPermissionCard_2097152")
			allow = findChild(editor, "aclRulePermissionAllow_0_2097152")
			deny = findChild(editor, "aclRulePermissionDeny_0_2097152")
			return permissionCard !== null && allow !== null && deny !== null
		})
		verify(!allow.checked && !deny.checked)
		mouseClick(allow, allow.width / 2, allow.height / 2, Qt.LeftButton)
		tryVerify(function() {
			return editor.aclModel.acls[0].allow.indexOf(2097152) >= 0
		})
		verify(allow.checked)
		mouseClick(allow, allow.width / 2, allow.height / 2, Qt.LeftButton)
		tryVerify(function() {
			return editor.aclModel.acls[0].allow.indexOf(2097152) < 0
		})
	}

	function test_large_acl_collections_are_bounded_and_virtualized() {
		setAcl(makeGroups(1000), makeRules(1000))
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		const groupScrollBar = findChild(loader.item, "aclGroupListScrollBar")
		const ruleScrollBar = findChild(loader.item, "aclRuleListScrollBar")
		verify(groups !== null)
		verify(rules !== null)
		verify(groupScrollBar !== null && ruleScrollBar !== null)
		tryCompare(groups, "count", 1000)
		tryCompare(rules, "count", 1000)
		verify(groups.height <= 300)
		verify(rules.height <= 440)
		verify(groups.contentHeight > groups.height)
		verify(rules.contentHeight > rules.height)
		tryVerify(function() { return groups.liveDelegateCount() > 0 })
		tryVerify(function() { return rules.liveDelegateCount() > 0 })
		verify(groups.liveDelegateCount() < 50)
		verify(rules.liveDelegateCount() < 50)
		compare(groupScrollBar.parent, groups)
		compare(ruleScrollBar.parent, rules)
		tryVerify(function() {
			const groupPoint = groupScrollBar.mapToItem(groups, 0, 0)
			const rulePoint = ruleScrollBar.mapToItem(rules, 0, 0)
			return groupPoint.x >= groups.width - groupScrollBar.width - 1
				&& rulePoint.x >= rules.width - ruleScrollBar.width - 1
				&& Math.abs(groupPoint.y) <= 1 && Math.abs(rulePoint.y) <= 1
		})

		groups.forceActiveFocus()
		tryCompare(groups, "activeFocus", true)
		keyClick(Qt.Key_End)
		compare(groups.currentIndex, 999)
		tryVerify(function() { return groups.itemAtIndex(999) !== null })
		compare(groups.itemAtIndex(999).Accessible.name, "Group 999")
		compare(groups.itemAtIndex(999).Accessible.selected, true)
		verify(String(groups.itemAtIndex(999).background.color).toLowerCase() !== "#ffffff")
		verify(groups.itemAtIndex(999).background.border.width > 1)
	}

	function test_narrow_layout_stacks_fields_without_horizontal_overflow() {
		testCase.editorWidth = 420
		tryVerify(function() { return loader.item.compactLayout })
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		tryVerify(function() { return groups.width > 0 && rules.width > 0 })
		groups.positionViewAtBeginning()
		rules.positionViewAtBeginning()
		tryVerify(function() { return groups.itemAtIndex(0) !== null && rules.itemAtIndex(0) !== null })
		verify(groups.itemAtIndex(0).width <= groups.width)
		verify(rules.itemAtIndex(0).width <= rules.width)
		verify(groups.height <= 420)
		verify(rules.height <= 520)
		const password = findChild(loader.item, "aclRoomPassword")
		verify(password !== null)
		tryVerify(function() { return password.width <= testCase.editorWidth })
	}

	function test_count_copy_uses_correct_singular_and_plural_forms() {
		setAcl(makeGroups(1), makeRules(1))
		const groupCount = findChild(loader.item, "aclGroupCountLabel")
		const ruleCount = findChild(loader.item, "aclRuleCountLabel")
		verify(groupCount !== null && ruleCount !== null)
		compare(groupCount.text, "1 group")
		compare(ruleCount.text, "1 rule")

		setAcl(makeGroups(2), makeRules(3))
		tryCompare(groupCount, "text", "2 groups")
		tryCompare(ruleCount, "text", "3 rules")
	}

	function test_initial_rule_selection_prefers_first_editable_rule_without_mutating_dto() {
		const inherited = { "targetType": "group", "target": "all",
			"applyHere": true, "applySubs": true, "inherited": true,
			"allow": [], "deny": [] }
		const editable = { "targetType": "group", "target": "Moderators",
			"applyHere": true, "applySubs": false, "inherited": false,
			"allow": ["speak"], "deny": [] }
		loader.item.selectedRuleIndex = -1
		setAcl(makeGroups(1), [inherited, editable])
		const rules = findChild(loader.item, "aclRuleList")
		tryCompare(rules, "currentIndex", 1)
		compare(loader.item.aclModel.acls.length, 2)
		compare(loader.item.aclModel.acls[0].target, "all")
		compare(loader.item.aclModel.acls[0].inherited, true)
		compare(loader.item.aclModel.acls[1].target, "Moderators")
		compare(loader.item.aclModel.acls[1].inherited, false)

		// Once the user has selected an inherited row, republishing the same DTO
		// preserves that explicit choice instead of applying the default again.
		rules.currentIndex = 0
		loader.item.publish(loader.item.cloneModel())
		tryCompare(rules, "currentIndex", 0)

		loader.item.selectedRuleIndex = -1
		setAcl(makeGroups(1), [inherited])
		tryCompare(rules, "currentIndex", 0)
	}

	function test_selected_editors_are_materialized_outside_virtualized_navigators() {
		setAcl(makeGroups(8), makeRules(8))
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		tryVerify(function() {
			return groups !== null && rules !== null && groups.currentIndex === 0
				&& rules.currentIndex === 0 && groups.itemAtIndex(0) !== null
				&& rules.itemAtIndex(0) !== null
		})

		let groupEditor = findChild(loader.item, "aclGroupName_0")
		let ruleEditor = findChild(loader.item, "aclRuleTargetType_0")
		verify(groupEditor !== null && ruleEditor !== null)
		verify(!loader.item.isDescendantOf(groupEditor, groups))
		verify(!loader.item.isDescendantOf(ruleEditor, rules))
		compare(findChild(groups.itemAtIndex(0), "aclGroupName_0"), null)
		compare(findChild(rules.itemAtIndex(0), "aclRuleTargetType_0"), null)
		compare(groups.itemAtIndex(0).height, loader.item.navigatorRowHeight)
		compare(rules.itemAtIndex(0).height, loader.item.navigatorRowHeight)

		groups.currentIndex = 5
		rules.currentIndex = 6
		tryVerify(function() {
			groupEditor = findChild(loader.item, "aclGroupName_5")
			ruleEditor = findChild(loader.item, "aclRuleTargetType_6")
			return groupEditor !== null && ruleEditor !== null
		})
		compare(findChild(loader.item, "aclGroupName_0"), null)
		compare(findChild(loader.item, "aclRuleTargetType_0"), null)
		verify(!loader.item.isDescendantOf(groupEditor, groups))
		verify(!loader.item.isDescendantOf(ruleEditor, rules))
	}

	function test_group_and_rule_cards_use_theme_pressed_state() {
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		verify(groups !== null && rules !== null)
		tryVerify(function() {
			return groups.itemAtIndex(0) !== null && rules.itemAtIndex(0) !== null
		})
		const group = groups.itemAtIndex(0)
		const rule = rules.itemAtIndex(0)

		mousePress(group, 4, 4, Qt.LeftButton)
		tryCompare(group, "down", true)
		compare(group.background.color, Theme.accentSubtle)
		mouseRelease(group, 4, 4, Qt.LeftButton)

		mousePress(rule, 4, 4, Qt.LeftButton)
		tryCompare(rule, "down", true)
		compare(rule.background.color, Theme.accentSubtle)
		mouseRelease(rule, 4, 4, Qt.LeftButton)
	}

	function test_empty_acl_exposes_clear_add_actions() {
		setAcl([], [])
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		const addGroup = findChild(loader.item, "aclAddGroup")
		const addRule = findChild(loader.item, "aclAddRule")
		verify(groups !== null && !groups.visible)
		verify(rules !== null && !rules.visible)
		verify(addGroup !== null && addGroup.enabled && addGroup.width > 0 && addGroup.height > 0)
		verify(addRule !== null && addRule.enabled && addRule.width > 0 && addRule.height > 0)
	}

	function test_user_rule_uses_registered_user_picker_with_typed_id() {
		setAcl(makeGroups(1), makeRules(1))
		const targetType = findChild(loader.item, "aclRuleTargetType_0")
		tryVerify(function() { return targetType !== null })
		targetType.currentIndex = 1
		targetType.activated(1)
		tryVerify(function() { return loader.item.aclModel.acls[0].targetType === "user" })

		const userTarget = findChild(loader.item, "aclRuleUserTarget_0")
		tryVerify(function() { return userTarget !== null && userTarget.visible && userTarget.count === 2 })
		userTarget.currentIndex = 1
		userTarget.activated(1)
		tryVerify(function() {
			return Number(loader.item.aclModel.acls[0].userId) === 202
				&& loader.item.aclModel.acls[0].target === "Bob"
		})
	}

	function test_group_members_use_typed_tokens_and_mutually_exclusive_arrays() {
		setAcl([editableGroup([], [])], [])
		let addedPicker = findChild(loader.item, "aclGroupAddedUsers_0_picker")
		let removedPicker = findChild(loader.item, "aclGroupRemovedUsers_0_picker")
		const addedPickerLabel = findChild(loader.item, "aclGroupAddedUsers_0_pickerLabel")
		const removedPickerLabel = findChild(loader.item, "aclGroupRemovedUsers_0_pickerLabel")
		tryVerify(function() {
			return addedPicker !== null && removedPicker !== null
				&& addedPickerLabel !== null && removedPickerLabel !== null
				&& addedPicker.count === 2 && removedPicker.count === 2
		})
		compare(addedPickerLabel.text, "Add to added members")
		compare(removedPickerLabel.text, "Add to removed members")

		let aliceIndex = optionIndex(addedPicker, 101)
		verify(aliceIndex >= 0)
		addedPicker.currentIndex = aliceIndex
		addedPicker.activated(aliceIndex)
		tryVerify(function() {
			const group = loader.item.aclModel.groups[0]
			return group.add.length === 1 && Number(group.add[0]) === 101
				&& group.remove.length === 0
		})
		verify(loader.item.aclModel.groups[0].addText === undefined)
		verify(loader.item.aclModel.groups[0].removeText === undefined)

		const aliceToken = findChild(loader.item, "aclGroupAddedUsers_0_token_101")
		verify(aliceToken !== null)
		compare(aliceToken.Accessible.name, "Alice, ID 101")
		compare(aliceToken.Accessible.description, "Registered user")

		removedPicker = findChild(loader.item, "aclGroupRemovedUsers_0_picker")
		aliceIndex = optionIndex(removedPicker, 101)
		verify(aliceIndex >= 0)
		compare(removedPicker.optionAt(aliceIndex).enabled, false)
		removedPicker.currentIndex = aliceIndex
		removedPicker.activated(aliceIndex)
		compare(loader.item.aclModel.groups[0].remove.length, 0)

		const bobIndex = optionIndex(removedPicker, 202)
		verify(bobIndex >= 0)
		removedPicker.currentIndex = bobIndex
		removedPicker.activated(bobIndex)
		tryVerify(function() {
			const group = loader.item.aclModel.groups[0]
			return group.add.length === 1 && Number(group.add[0]) === 101
				&& group.remove.length === 1 && Number(group.remove[0]) === 202
		})
		addedPicker = findChild(loader.item, "aclGroupAddedUsers_0_picker")
		compare(addedPicker.optionAt(optionIndex(addedPicker, 202)).enabled, false)

		// The canonical update path also resolves an opposite-list conflict so
		// stale callers cannot persist the same ID in both protocol arrays.
		loader.item.updateGroupMembers(0, "add", [101, 202], "")
		tryVerify(function() {
			const group = loader.item.aclModel.groups[0]
			return group.add.length === 2 && group.remove.length === 0
		})
	}

	function test_unknown_member_id_is_named_retained_and_survives_other_edits() {
		setAcl([editableGroup([999], [])], [])
		const unknownToken = findChild(loader.item, "aclGroupAddedUsers_0_token_999")
		tryVerify(function() { return unknownToken !== null })
		compare(unknownToken.Accessible.name, "Unknown user, ID 999")
		compare(unknownToken.Accessible.description, "Unknown user retained from the server")

		let addedPicker = findChild(loader.item, "aclGroupAddedUsers_0_picker")
		const bobIndex = optionIndex(addedPicker, 202)
		verify(bobIndex >= 0)
		addedPicker.currentIndex = bobIndex
		addedPicker.activated(bobIndex)
		tryVerify(function() {
			const values = loader.item.aclModel.groups[0].add
			return values.length === 2 && Number(values[0]) === 999 && Number(values[1]) === 202
		})
		verify(findChild(loader.item, "aclGroupAddedUsers_0_token_999") !== null)
	}

	function test_member_remove_keyboard_focus_moves_to_neighbor_then_picker() {
		setAcl([editableGroup([101, 999], [])], [])
		let removeAlice = findChild(loader.item, "aclGroupAddedUsers_0_remove_101")
		tryVerify(function() { return removeAlice !== null })
		compare(removeAlice.Accessible.name, "Remove Alice (ID 101) from Added members")
		removeAlice.forceActiveFocus()
		tryCompare(removeAlice, "activeFocus", true)
		keyClick(Qt.Key_Return)
		tryVerify(function() {
			const values = loader.item.aclModel.groups[0].add
			return values.length === 1 && Number(values[0]) === 999
		})

		let removeUnknown = null
		tryVerify(function() {
			removeUnknown = findChild(loader.item, "aclGroupAddedUsers_0_remove_999")
			return removeUnknown !== null && removeUnknown.activeFocus
		})
		keyClick(Qt.Key_Return)
		tryCompare(loader.item.aclModel.groups[0].add, "length", 0)
		let picker = null
		tryVerify(function() {
			picker = findChild(loader.item, "aclGroupAddedUsers_0_picker")
			return picker !== null && picker.activeFocus
		})
		compare(picker.Accessible.name, "Add user to Added members")
	}

	function test_compact_member_editors_stack_and_tokens_stay_in_bounds() {
		testCase.editorWidth = 420
		setAcl([editableGroup([101, 999], [202])], [])
		const added = findChild(loader.item, "aclGroupAddedUsers_0")
		const removed = findChild(loader.item, "aclGroupRemovedUsers_0")
		const addedToken = findChild(loader.item, "aclGroupAddedUsers_0_token_101")
		const unknownToken = findChild(loader.item, "aclGroupAddedUsers_0_token_999")
		const addedPicker = findChild(loader.item, "aclGroupAddedUsers_0_picker")
		tryVerify(function() {
			return loader.item.compactLayout && added !== null && removed !== null
				&& addedToken !== null && unknownToken !== null && addedPicker !== null
		})
		tryVerify(function() {
			const addedPoint = added.mapToItem(loader.item, 0, 0)
			const removedPoint = removed.mapToItem(loader.item, 0, 0)
			return removedPoint.y >= addedPoint.y + added.height
		})
		verify(added.width <= testCase.editorWidth)
		verify(removed.width <= testCase.editorWidth)
		verify(addedToken.width <= added.width)
		verify(unknownToken.width <= added.width)
		verify(addedPicker.width <= added.width)
	}

	function test_selection_survives_publish_and_add_focuses_the_new_row() {
		setAcl(makeGroups(4), makeRules(4))
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		const addGroup = findChild(loader.item, "aclAddGroup")
		const addRule = findChild(loader.item, "aclAddRule")
		tryVerify(function() {
			return groups !== null && rules !== null && addGroup !== null && addRule !== null
				&& groups.count === 4 && rules.count === 4
		})

		groups.currentIndex = 2
		rules.currentIndex = 3
		let groupName = findChild(loader.item, "aclGroupName_2")
		tryVerify(function() { return groupName !== null })
		groupName.forceActiveFocus()
		tryCompare(groupName, "activeFocus", true)
		loader.item.publish(loader.item.cloneModel())
		tryCompare(groups, "currentIndex", 2)
		tryCompare(rules, "currentIndex", 3)
		tryVerify(function() {
			groupName = findChild(loader.item, "aclGroupName_2")
			return groupName !== null && groupName.activeFocus
		})
		setAcl(makeGroups(4), makeRules(4))
		tryVerify(function() {
			groupName = findChild(loader.item, "aclGroupName_2")
			return groups.currentIndex === 2 && groupName !== null && groupName.activeFocus
		})

		let ruleTarget = findChild(loader.item, "aclRuleTargetType_3")
		tryVerify(function() { return ruleTarget !== null })
		ruleTarget.forceActiveFocus()
		tryCompare(ruleTarget, "activeFocus", true)
		loader.item.publish(loader.item.cloneModel())
		tryVerify(function() {
			ruleTarget = findChild(loader.item, "aclRuleTargetType_3")
			return ruleTarget !== null && ruleTarget.activeFocus
		})

		addGroup.clicked()
		tryCompare(groups, "count", 5)
		tryCompare(groups, "currentIndex", 4)
		let newGroupName = null
		tryVerify(function() {
			newGroupName = findChild(loader.item, "aclGroupName_4")
			return newGroupName !== null && newGroupName.activeFocus
		})
		compare(groups.itemAtIndex(4).Accessible.selected, true)

		addRule.clicked()
		tryCompare(rules, "count", 5)
		tryCompare(rules, "currentIndex", 4)
		let newRuleTarget = null
		tryVerify(function() {
			newRuleTarget = findChild(loader.item, "aclRuleTargetType_4")
			return newRuleTarget !== null && newRuleTarget.activeFocus
		})
		compare(rules.itemAtIndex(4).Accessible.selected, true)

		// A queued Add-row handoff must yield if the user has already chosen a
		// different control before the delegate materializes.
		loader.item.focusListRow(groups, 3)
		addGroup.forceActiveFocus()
		tryCompare(addGroup, "activeFocus", true)
		wait(0)
		compare(addGroup.activeFocus, true)
	}

	function test_remove_hands_focus_to_neighbor_or_empty_add_action() {
		setAcl(makeGroups(2), makeRules(2))
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		const addGroup = findChild(loader.item, "aclAddGroup")
		const addRule = findChild(loader.item, "aclAddRule")
		tryVerify(function() {
			return groups !== null && rules !== null && addGroup !== null && addRule !== null
				&& groups.count === 2 && rules.count === 2
		})

		let removeGroup = findChild(loader.item, "aclRemoveGroup_0")
		tryVerify(function() { return removeGroup !== null })
		removeGroup.forceActiveFocus()
		removeGroup.clicked()
		tryCompare(groups, "count", 1)
		let remainingGroupName = null
		tryVerify(function() {
			remainingGroupName = findChild(loader.item, "aclGroupName_0")
			return groups.currentIndex === 0 && remainingGroupName !== null
				&& remainingGroupName.activeFocus
		})

		removeGroup = findChild(loader.item, "aclRemoveGroup_0")
		tryVerify(function() { return removeGroup !== null })
		removeGroup.forceActiveFocus()
		removeGroup.clicked()
		tryCompare(groups, "count", 0)
		tryCompare(addGroup, "activeFocus", true)

		let removeRule = findChild(loader.item, "aclRemoveRule_0")
		tryVerify(function() { return removeRule !== null })
		removeRule.forceActiveFocus()
		removeRule.clicked()
		tryCompare(rules, "count", 1)
		let remainingRuleTarget = null
		tryVerify(function() {
			remainingRuleTarget = findChild(loader.item, "aclRuleTargetType_0")
			return rules.currentIndex === 0 && remainingRuleTarget !== null
				&& remainingRuleTarget.activeFocus
		})

		removeRule = findChild(loader.item, "aclRemoveRule_0")
		tryVerify(function() { return removeRule !== null })
		removeRule.forceActiveFocus()
		removeRule.clicked()
		tryCompare(rules, "count", 0)
		tryCompare(addRule, "activeFocus", true)
	}
}
