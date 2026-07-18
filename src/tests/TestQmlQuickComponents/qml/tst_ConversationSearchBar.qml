import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "ConversationSearchBar"
	when: windowShown
	visible: true
	width: 760
	height: 180
	property var searchModel: null
	property var searchBar: null

	Component {
		id: modelComponent
		QtObject {
			signal currentMatchChanged()
			property string query: ""
			property int matchCount: 0
			property int currentMatchIndex: -1
			property int currentMatchRow: -1
			property string currentMatchStableId: ""

			onQueryChanged: {
				if (query.trim() === "alpha") {
					matchCount = 3
					currentMatchIndex = 0
					currentMatchRow = 4
					currentMatchStableId = "message-4"
				} else {
					matchCount = 0
					currentMatchIndex = -1
					currentMatchRow = -1
					currentMatchStableId = ""
				}
				currentMatchChanged()
			}

			function select(index) {
				if (matchCount <= 0)
					return false
				currentMatchIndex = (index + matchCount) % matchCount
				currentMatchRow = 4 + currentMatchIndex * 3
				currentMatchStableId = "message-" + currentMatchRow
				currentMatchChanged()
				return true
			}
			function nextMatch() { return select(currentMatchIndex + 1) }
			function previousMatch() { return select(currentMatchIndex - 1) }
			function clearSearch() { query = "" }
		}
	}

	SignalSpy {
		id: closeSpy
		target: testCase.searchBar
		signalName: "closeRequested"
	}

	SignalSpy {
		id: matchSpy
		target: testCase.searchBar
		signalName: "currentMatchRequested"
	}

	function createSearchBar(overrides) {
		searchModel = createTemporaryObject(modelComponent, testCase)
		verify(searchModel !== null)
		const component = Qt.createComponent("qrc:/qml-shell/ConversationSearchBar.qml")
		tryCompare(component, "status", Component.Ready)
		const properties = {
			"timelineModel": searchModel,
			"width": 720,
			"x": 20,
			"y": 40
		}
		for (const key in overrides)
			properties[key] = overrides[key]
		searchBar = createTemporaryObject(component, testCase, properties)
		verify(searchBar !== null)
		closeSpy.clear()
		matchSpy.clear()
		return searchBar
	}

	function cleanup() {
		searchBar = null
		searchModel = null
		wait(0)
	}

	function test_search_is_debounced_and_selects_the_first_stable_match() {
		const bar = createSearchBar({})
		const field = findChild(bar, "conversationSearchField")
		const count = findChild(bar, "conversationSearchResultCount")
		verify(field !== null)
		verify(count !== null)

		bar.activate()
		tryCompare(field, "activeFocus", true)
		keyClick(Qt.Key_A)
		keyClick(Qt.Key_L)
		keyClick(Qt.Key_P)
		keyClick(Qt.Key_H)
		keyClick(Qt.Key_A)
		compare(searchModel.query, "")
		tryCompare(searchModel, "query", "alpha", 500)
		compare(searchModel.matchCount, 3)
		compare(searchModel.currentMatchStableId, "message-4")
		compare(count.text, "1 of 3")
		verify(matchSpy.count >= 1)
		compare(matchSpy.signalArguments[matchSpy.count - 1][0], 4)
		compare(matchSpy.signalArguments[matchSpy.count - 1][1], "message-4")
	}

	function test_keyboard_and_buttons_navigate_without_filtering_the_timeline() {
		const bar = createSearchBar({})
		const field = findChild(bar, "conversationSearchField")
		const previous = findChild(bar, "conversationSearchPrevious")
		const next = findChild(bar, "conversationSearchNext")
		verify(field !== null && previous !== null && next !== null)

		field.text = "alpha"
		bar.commitQuery()
		compare(searchModel.currentMatchIndex, 0)
		mouseClick(next)
		compare(searchModel.currentMatchIndex, 1)
		compare(searchModel.currentMatchRow, 7)
		mouseClick(previous)
		compare(searchModel.currentMatchIndex, 0)

		field.forceActiveFocus()
		keyClick(Qt.Key_Return, Qt.ShiftModifier)
		compare(searchModel.currentMatchIndex, 2)
		compare(searchModel.currentMatchStableId, "message-10")
	}

	function test_escape_closes_and_reset_clears_model_state() {
		const bar = createSearchBar({})
		const field = findChild(bar, "conversationSearchField")
		field.text = "alpha"
		bar.commitQuery()
		compare(searchModel.matchCount, 3)

		field.forceActiveFocus()
		keyClick(Qt.Key_Escape)
		compare(closeSpy.count, 1)
		bar.reset()
		compare(field.text, "")
		compare(searchModel.query, "")
		compare(searchModel.matchCount, 0)
	}

	function test_compact_layout_keeps_all_controls_inside_the_bar() {
		const bar = createSearchBar({ "width": 396, "narrowLayout": true })
		const field = findChild(bar, "conversationSearchField")
		const close = findChild(bar, "conversationSearchClose")
		verify(field.width >= 96)
		verify(field.x >= 0)
		verify(close.x + close.width <= bar.width)
	}

	function test_visual_fixture_keeps_focus_but_suppresses_cursor_paint() {
		const bar = createSearchBar({})
		const field = findChild(bar, "conversationSearchField")
		verify(field !== null)
		bar.activate()
		tryCompare(field, "activeFocus", true)
		const cursorPaint = findChild(field, "conversationSearchCursorPaint")
		verify(cursorPaint !== null)
		compare(cursorPaint.visible, true)

		bar.visualFixtureMode = true
		compare(field.activeFocus, true)
		compare(cursorPaint.visible, false)
	}
}
