import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "NavigationRail"
    when: windowShown
    width: 340
    height: 620
	visible: true

    ListModel {
        id: rooms
		dynamicRoles: true
		Component.onCompleted: {
			append({
				"stableId": "room-lobby", "scopeToken": "channel:1", "title": "Lobby",
				"subtitle": "Welcome", "kind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0,
				"payload": { "source": { "actions": [
					{ "kind": "action", "id": "join", "label": "Join", "enabled": true }
				] } }
			})
			append({
				"stableId": "room-games", "scopeToken": "channel:2", "title": "Games",
				"subtitle": "Playing", "kind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0,
				"payload": { "source": { "actions": [] } }
			})
		}
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
		property string movedParticipant: ""
		property string participantMoveTarget: ""
		property int participantMoveCount: 0
		property string movedScope: ""
		property string scopeMoveTarget: ""
		property string scopeMovePlacement: ""
		property int scopeMoveCount: 0
		function selectScope(token) { selectedScope = token; ++selectScopeCount }
		function joinVoiceChannel(token) { selectedScope = token; ++joinVoiceCount }
        function invokeScopeAction(token, action) {}
		function selectParticipant(stableId) { selectedParticipant = stableId; ++selectParticipantCount }
		function openDirectMessage(stableId) { selectedParticipant = stableId; ++directMessageCount }
		function moveParticipant(stableId, targetScope) {
			movedParticipant = stableId
			participantMoveTarget = targetScope
			++participantMoveCount
		}
		function moveScope(sourceScope, targetScope, placement) {
			movedScope = sourceScope
			scopeMoveTarget = targetScope
			scopeMovePlacement = placement
			++scopeMoveCount
		}
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
		commands.movedParticipant = ""
		commands.participantMoveTarget = ""
		commands.participantMoveCount = 0
		commands.movedScope = ""
		commands.scopeMoveTarget = ""
		commands.scopeMovePlacement = ""
		commands.scopeMoveCount = 0
    }

	function test_voice_room_return_selects_and_commits_navigation() {
        const room = findChild(loader.item, "navigationRoom_room-lobby")
        verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Return)
        compare(commands.selectedScope, "channel:1")
		compare(commands.selectScopeCount, 1)
		compare(commands.joinVoiceCount, 0)
        compare(committedSpy.count, 1)
    }

	function test_voice_room_space_joins_voice() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(commands.selectedScope, "channel:1")
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
		compare(scopeMenuSpy.signalArguments[0][0], "channel:1")
		compare(scopeMenuSpy.signalArguments[0][1], "voice")
		compare(scopeMenuSpy.signalArguments[0][2][0].id, "join")
		compare(commands.selectedScope, "channel:1")
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

	function test_pointer_clicks_remain_clicks_with_drag_handlers() {
		const roomItem = findChild(loader.item, "navigationRoom_room-lobby")
		var participantItem = null
		verify(roomItem !== null)
		tryVerify(function() {
			participantItem = findChild(loader.item, "navigationParticipant_42")
			return participantItem !== null
		})
		mouseClick(roomItem, roomItem.width / 2, roomItem.height / 2, Qt.LeftButton)
		compare(commands.selectedScope, "channel:1")
		compare(commands.selectScopeCount, 1)
		mouseClick(participantItem, participantItem.width / 2, participantItem.height / 2, Qt.LeftButton)
		compare(commands.selectedParticipant, "42")
		compare(commands.selectParticipantCount, 1)
	}

	function test_participant_drag_routes_stable_internal_payload() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		var participant = null
		verify(room !== null)
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const startX = participant.width / 2
		const startY = participant.height / 2
		const target = participant.mapFromItem(room, room.width / 2, room.height / 2)
		mouseDrag(participant, startX, startY, target.x - startX, target.y - startY,
			Qt.LeftButton, Qt.NoModifier, 20)
		tryCompare(commands, "movedParticipant", "42")
		compare(commands.participantMoveTarget, "channel:1")
		compare(commands.participantMoveCount, 1)
	}

	function test_participant_drag_keeps_press_identity_across_delegate_rebind() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		var participant = null
		verify(room !== null)
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const mouse = findChild(participant, "navigationParticipantMouse_42")
		verify(mouse !== null)
		const startX = participant.width / 2
		const startY = participant.height / 2
		const target = participant.mapFromItem(room, room.width / 2, room.height / 2)

		mousePress(participant, startX, startY, Qt.LeftButton)
		compare(mouse.dragSourceStableId, "42")
		participants.setProperty(0, "stableId", "84")
		wait(0)
		compare(participant.stableId, "84")
		compare(mouse.dragSourceStableId, "42")
		mouseMove(participant, target.x, target.y, 20)
		mouseRelease(participant, target.x, target.y, Qt.LeftButton)
		tryCompare(commands, "movedParticipant", "42")
		compare(commands.participantMoveTarget, "channel:1")
		compare(commands.participantMoveCount, 1)
		compare(mouse.dragSourceStableId, "")
		participants.setProperty(0, "stableId", "42")
	}

	function test_room_drag_keeps_press_scope_across_delegate_rebind() {
		const source = findChild(loader.item, "navigationRoom_room-lobby")
		const targetRoom = findChild(loader.item, "navigationRoom_room-games")
		verify(source !== null)
		verify(targetRoom !== null)
		const mouse = findChild(source, "navigationRoomMouse_room-lobby")
		verify(mouse !== null)
		const startX = source.width / 2
		const startY = source.height / 2
		const target = source.mapFromItem(targetRoom, targetRoom.width / 2, targetRoom.height / 2)

		mousePress(source, startX, startY, Qt.LeftButton)
		compare(mouse.dragSourceScopeToken, "channel:1")
		rooms.setProperty(0, "scopeToken", "channel:99")
		wait(0)
		compare(source.scopeToken, "channel:99")
		compare(mouse.dragSourceScopeToken, "channel:1")
		mouseMove(source, target.x, target.y, 20)
		mouseRelease(source, target.x, target.y, Qt.LeftButton)
		tryCompare(commands, "movedScope", "channel:1")
		compare(commands.scopeMoveTarget, "channel:2")
		compare(commands.scopeMoveCount, 1)
		compare(mouse.dragSourceScopeToken, "")
		rooms.setProperty(0, "scopeToken", "channel:1")
	}

	function test_drag_snapshot_clears_when_delegate_is_reused_or_cancelled() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const roomMouse = findChild(room, "navigationRoomMouse_room-lobby")
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const participantMouse = findChild(participant, "navigationParticipantMouse_42")
		verify(roomMouse !== null)
		verify(participantMouse !== null)

		mousePress(room, room.width / 2, room.height / 2, Qt.LeftButton)
		compare(roomMouse.dragSourceScopeToken, "channel:1")
		roomMouse.clearDragSnapshot()
		compare(roomMouse.dragSourceScopeToken, "")
		mouseRelease(room, room.width / 2, room.height / 2, Qt.LeftButton)

		mousePress(participant, participant.width / 2, participant.height / 2, Qt.LeftButton)
		compare(participantMouse.dragSourceStableId, "42")
		participantMouse.clearDragSnapshot()
		compare(participantMouse.dragSourceStableId, "")
		mouseRelease(participant, participant.width / 2, participant.height / 2, Qt.LeftButton)
	}

	function test_stable_participant_drop_routes_without_model_index() {
		verify(loader.item.dispatchStableDrop("participant", "00042", "channel:0001", 20, 40))
		compare(commands.movedParticipant, "42")
		compare(commands.participantMoveTarget, "channel:1")
	}

	function test_stable_room_drop_derives_placement() {
		verify(loader.item.dispatchStableDrop("voice-room", "channel:7", "channel:1", 2, 40))
		compare(commands.movedScope, "channel:7")
		compare(commands.scopeMoveTarget, "channel:1")
		compare(commands.scopeMovePlacement, "before")

		verify(loader.item.dispatchStableDrop("voice-room", "channel:7", "channel:1", 20, 40))
		compare(commands.scopeMovePlacement, "inside")

		verify(loader.item.dispatchStableDrop("voice-room", "channel:7", "channel:1", 39, 40))
		compare(commands.scopeMovePlacement, "after")
	}

	function test_stable_drop_rejects_empty_or_unknown_payload() {
		verify(!loader.item.dispatchStableDrop("participant", " ", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "0", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "42", "voice:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "42", "channel:4294967296", 20, 40))
		verify(!loader.item.dispatchStableDrop("voice-room", "channel:0", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("voice-room", "channel:1", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("unknown", "42", "channel:1", 20, 40))
		compare(commands.movedParticipant, "")
		compare(commands.movedScope, "")
	}
}
