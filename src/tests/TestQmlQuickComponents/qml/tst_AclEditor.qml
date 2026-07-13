import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "AclEditor"
	when: windowShown
	width: 1100
	height: 1100
	property int editorWidth: 920
	property var aclField: ({ "id": "acl", "value": ({}) })

	Loader {
		id: loader
		width: testCase.editorWidth
		height: 1060
		source: "qrc:/qml-shell/AclEditor.qml"
		onLoaded: item.field = testCase.aclField
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

	function setAcl(groups, rules) {
		testCase.aclField = {
			"id": "acl",
			"value": {
				"inheritAcls": true,
				"password": "",
				"groups": groups,
				"acls": rules,
				"permissions": [
					{ "id": "speak", "label": "Speak" },
					{ "id": "enter", "label": "Enter room" }
				]
			}
		}
		loader.item.field = testCase.aclField
	}

	function init() {
		verify(loader.item !== null)
		testCase.editorWidth = 920
		setAcl(makeGroups(2), makeRules(2))
	}

	function test_large_acl_collections_are_bounded_and_virtualized() {
		setAcl(makeGroups(1000), makeRules(1000))
		const groups = findChild(loader.item, "aclGroupList")
		const rules = findChild(loader.item, "aclRuleList")
		verify(groups !== null)
		verify(rules !== null)
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

		groups.forceActiveFocus()
		tryCompare(groups, "activeFocus", true)
		keyClick(Qt.Key_End)
		compare(groups.currentIndex, 999)
		tryVerify(function() { return groups.itemAtIndex(999) !== null })
		compare(groups.itemAtIndex(999).Accessible.name, "Group 999")
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
}
