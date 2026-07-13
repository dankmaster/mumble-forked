import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "NavigationRail"
    when: windowShown
    width: 340
    height: 620

    ListModel {
        id: rooms
		dynamicRoles: true
		Component.onCompleted: append({
			"stableId": "room-lobby", "scopeToken": "voice:1", "title": "Lobby",
			"subtitle": "Welcome", "kind": "voice", "selected": false,
			"depth": 0, "unreadCount": 0,
			"payload": { "source": { "actions": [
				{ "kind": "action", "id": "join", "label": "Join", "enabled": true }
			] } }
		})
    }

    ListModel {
        id: participants
		dynamicRoles: true
		Component.onCompleted: append({
			"stableId": "42", "title": "Alice", "subtitle": "Listening",
			"status": "passive", "payload": { "source": { "actions": [
				{ "kind": "action", "id": "message", "label": "Message", "enabled": true }
			] } }
		})
    }

    QtObject {
        id: selection
        property var selectedUserSession: undefined
    }

    QtObject {
        id: session
        property string serverName: "Test server"
        property string connectionLabel: "Connected"
        property string selfStatusLabel: "Online"
        property bool connected: true
        property string selfName: "Tester"
        property bool selfMuted: false
    }

    QtObject {
        id: commands
        property string selectedScope: ""
        property string selectedParticipant: ""
		property int selectScopeCount: 0
		property int joinVoiceCount: 0
		property int selectParticipantCount: 0
		property int directMessageCount: 0
		function selectScope(token) { selectedScope = token; ++selectScopeCount }
		function joinVoiceChannel(token) { selectedScope = token; ++joinVoiceCount }
        function invokeScopeAction(token, action) {}
		function selectParticipant(stableId) { selectedParticipant = stableId; ++selectParticipantCount }
		function openDirectMessage(stableId) { selectedParticipant = stableId; ++directMessageCount }
        function invokeParticipantAction(stableId, action) {}
        function toggleSelfMute() {}
    }

    Loader {
        id: loader
        anchors.fill: parent
		Component.onCompleted: {
			setSource("qrc:/qml-shell/NavigationRail.qml", {
				"roomModel": rooms,
				"participantModel": participants,
				"selectionState": selection,
				"uiCommands": commands,
				"clientSession": session,
				"commitOnSelection": true
			})
        }
    }

    SignalSpy {
        id: committedSpy
        target: loader.item
        signalName: "selectionCommitted"
    }

	SignalSpy {
		id: scopeMenuSpy
		target: loader.item
		signalName: "scopeMenuRequested"
	}

	SignalSpy {
		id: participantMenuSpy
		target: loader.item
		signalName: "participantMenuRequested"
	}

	SignalSpy {
		id: profileMenuSpy
		target: loader.item
		signalName: "profileMenuRequested"
	}

    function init() {
		tryVerify(function() { return loader.item !== null })
        committedSpy.clear()
		scopeMenuSpy.clear()
		participantMenuSpy.clear()
		profileMenuSpy.clear()
        commands.selectedScope = ""
        commands.selectedParticipant = ""
		commands.selectScopeCount = 0
		commands.joinVoiceCount = 0
		commands.selectParticipantCount = 0
		commands.directMessageCount = 0
    }

	function test_voice_room_return_selects_and_commits_navigation() {
        const room = findChild(loader.item, "navigationRoom_room-lobby")
        verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Return)
        compare(commands.selectedScope, "voice:1")
		compare(commands.selectScopeCount, 1)
		compare(commands.joinVoiceCount, 0)
        compare(committedSpy.count, 1)
    }

	function test_voice_room_space_joins_voice() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(commands.selectedScope, "voice:1")
		compare(commands.joinVoiceCount, 1)
		compare(commands.selectScopeCount, 0)
		compare(committedSpy.count, 1)
	}

	function test_participant_return_selects_and_commits_navigation() {
		const participantList = findChild(loader.item, "navigationParticipants")
		verify(participantList !== null)
		compare(participantList.count, 1)
		tryVerify(function() { return participantList.height > 0 })
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		participant.forceActiveFocus()
		keyClick(Qt.Key_Return)
        compare(commands.selectedParticipant, "42")
		compare(commands.selectParticipantCount, 1)
		compare(commands.directMessageCount, 0)
        compare(committedSpy.count, 1)
    }

	function test_participant_space_opens_direct_message() {
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		participant.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(commands.selectedParticipant, "42")
		compare(commands.directMessageCount, 1)
		compare(commands.selectParticipantCount, 0)
		compare(committedSpy.count, 1)
	}

	function test_initial_focus_moves_to_room() {
		loader.item.focusInitialItem()
		tryVerify(function() {
			const room = findChild(loader.item, "navigationRoom_room-lobby")
			return room !== null && room.activeFocus
		})
	}

	function test_initial_focus_prefers_selected_room() {
		rooms.setProperty(0, "selected", true)
		loader.item.focusInitialItem()
		tryVerify(function() {
			const room = findChild(loader.item, "navigationRoom_room-lobby")
			return room !== null && room.activeFocus
		})
		rooms.setProperty(0, "selected", false)
	}

	function test_room_context_key_uses_typed_scope_payload() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Menu)
		compare(scopeMenuSpy.count, 1)
		compare(scopeMenuSpy.signalArguments[0][0], "voice:1")
		compare(scopeMenuSpy.signalArguments[0][1], "voice")
		compare(scopeMenuSpy.signalArguments[0][2][0].id, "join")
		compare(commands.selectedScope, "voice:1")
		compare(committedSpy.count, 0)
	}

	function test_participant_context_key_uses_typed_participant_payload() {
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		participant.forceActiveFocus()
		keyClick(Qt.Key_Menu)
		compare(participantMenuSpy.count, 1)
		compare(participantMenuSpy.signalArguments[0][0], "42")
		compare(participantMenuSpy.signalArguments[0][1][0].id, "message")
		compare(commands.selectedParticipant, "42")
		compare(committedSpy.count, 0)
	}

	function test_profile_button_requests_accessible_profile_menu() {
		const button = findChild(loader.item, "profileMenuButton")
		verify(button !== null)
		button.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(profileMenuSpy.count, 1)
	}
}
