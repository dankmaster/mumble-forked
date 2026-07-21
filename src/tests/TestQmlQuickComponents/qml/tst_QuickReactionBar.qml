import QtQuick
import QtTest
import Mumble.Theme 1.0
import "qrc:/qml-shell" as Shell

TestCase {
	id: testCase
	name: "QuickReactionBar"
	when: windowShown
	width: 480
	height: 120

	Shell.QuickReactionBar {
		id: reactionBar
		width: 440
		visible: true
		activeReactions: [
			{ "emoji": "❤️", "count": 2, "selfReacted": true }
		]
	}

	SignalSpy {
		id: reactionSpy
		target: reactionBar
		signalName: "reactionRequested"
	}

	function init() {
		reactionSpy.clear()
	}

	function test_exposes_complete_quick_reaction_set() {
		compare(reactionBar.optionCount, 8)
		compare(reactionBar.implicitWidth, 8 * 38 + 7 * Theme.space1)
		compare(reactionBar.optionAt(0).modelData.emoji, "👍")
		compare(reactionBar.optionAt(7).modelData.emoji, "🔥")
		verify(reactionBar.optionAt(1).reacted)
		verify(!reactionBar.optionAt(0).reacted)
	}

	function test_routes_selected_reaction() {
		reactionBar.optionAt(5).clicked()
		compare(reactionSpy.count, 1)
		compare(reactionSpy.signalArguments[0][0], "🎉")
	}
}
