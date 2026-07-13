import QtQuick
import QtTest
import Mumble.Theme 1.0

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
				"payload": { "joined": true, "canJoin": false, "badges": ["Pinned"],
					"screenShare": { "visible": true, "mode": "publishing", "badgeLabel": "Live",
						"badgeTone": "success", "primaryActionId": "screenShareOpenWindow",
						"primaryLabel": "Manage share", "primaryEnabled": true, "primaryTone": "success" },
					"source": { "actions": [
					{ "kind": "action", "id": "join", "label": "Join", "enabled": true }
				] } }
			})
			append({
				"stableId": "room-games", "scopeToken": "channel:2", "title": "Games",
				"subtitle": "Playing", "kind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0,
				"payload": { "joined": false, "canJoin": true, "screenShare": { "visible": true,
					"mode": "idle", "primaryActionId": "screenShareStart", "primaryEnabled": false },
					"source": { "actions": [] } }
			})
			append({
				"stableId": "room-activity", "scopeToken": "-2:0", "title": "Activity",
				"subtitle": "Server notices", "kind": "text", "selected": false,
				"depth": 0, "unreadCount": 0, "payload": { "source": { "actions": [] } }
			})
			append({
				"stableId": "room-direct", "scopeToken": "-1:42", "title": "Alice",
				"subtitle": "Direct message", "kind": "direct", "selected": false,
				"depth": 0, "unreadCount": 1, "payload": { "source": { "actions": [] } }
			})
		}
    }

    ListModel {
        id: participants
		dynamicRoles: true
		Component.onCompleted: {
			append({
				"stableId": "42", "title": "Alice", "subtitle": "Listening",
				"status": "talking", "payload": { "participantSession": "42", "entryKind": "user",
					"talking": true, "talkTone": "speaking", "badges": ["Talking", "Priority"],
					"localVolume": { "db": -6, "compactLabel": "-6", "label": "-6 dB", "visible": true },
					"canJoin": true, "statuses": [
						{ "kind": "talking", "label": "Talking", "tone": "speaking" },
						{ "kind": "selfMuted", "label": "Muted", "tone": "danger" },
						{ "kind": "selfDeafened", "label": "Deafened", "tone": "danger" }
					], "source": { "session": "42", "entryKind": "user", "actions": [
					{ "kind": "action", "id": "message", "label": "Message", "enabled": true }
				] } }
			})
			append({
				"stableId": "listener:2:42", "title": "Alice", "subtitle": "Listening to Games",
				"status": "passive", "payload": { "participantSession": "42", "entryKind": "listener",
					"scopeToken": "channel:2", "badges": ["Listener"], "statuses": [
						{ "kind": "listener", "label": "Listener", "tone": "accent" }
					], "source": { "session": "42", "entryKind": "listener", "scopeToken": "channel:2",
						"actions": [{ "kind": "action", "id": "listener.remove", "label": "Remove listener" }] } }
			})
		}
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
		property bool selfDeafened: false
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
		property int scopeActionCount: 0
		property string scopeActionId: ""
		property int participantActionCount: 0
		property string participantActionId: ""
		property int selfMuteToggleCount: 0
		property int selfDeafToggleCount: 0
		function selectScope(token) { selectedScope = token; ++selectScopeCount }
		function joinVoiceChannel(token) { selectedScope = token; ++joinVoiceCount }
		function invokeScopeActionValue(token, action, value, finalValue) {}
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
		function invokeScopeAction(token, action) {
			selectedScope = token
			scopeActionId = action
			++scopeActionCount
		}
		function invokeParticipantAction(stableId, action) {
			selectedParticipant = stableId
			participantActionId = action
			++participantActionCount
		}
		function invokeParticipantActionValue(stableId, action, value, finalValue) {}
		function toggleSelfMute() { ++selfMuteToggleCount }
		function toggleSelfDeaf() { ++selfDeafToggleCount }
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
		// Drain delegate rebind/reuse work from the previous test before resetting
		// the command probe. Otherwise a queued release from a recycled row can
		// make the following pointer-drag assertion observe stale target state.
		wait(0)
		const roomList = findChild(loader.item, "navigationRooms")
		const participantList = findChild(loader.item, "navigationParticipants")
		if (roomList !== null) {
			roomList.currentIndex = 0
			roomList.positionViewAtBeginning()
			roomList.positionViewAtIndex(0, ListView.Beginning)
			roomList.forceLayout()
		}
		if (participantList !== null) {
			participantList.currentIndex = 0
			participantList.positionViewAtBeginning()
			participantList.positionViewAtIndex(0, ListView.Beginning)
			participantList.forceLayout()
		}
		wait(0)
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
		commands.scopeActionCount = 0
		commands.scopeActionId = ""
		commands.participantActionCount = 0
		commands.participantActionId = ""
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
		compare(participantList.count, 2)
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

	function test_sections_and_keyboard_focus_are_visually_explicit() {
		compare(loader.item.activeFocusOnTab, false)
		const voiceSection = findChild(loader.item, "navigationSection_voice")
		const textSection = findChild(loader.item, "navigationSection_text")
		const directSection = findChild(loader.item, "navigationSection_direct")
		verify(voiceSection !== null)
		verify(textSection !== null)
		verify(directSection !== null)
		compare(voiceSection.Accessible.name, "VOICE ROOMS")
		compare(textSection.Accessible.name, "TEXT & ACTIVITY")
		compare(directSection.Accessible.name, "DIRECT MESSAGES")

		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipant_42")
		verify(room !== null && participant !== null)
		room.forceActiveFocus()
		tryCompare(room, "activeFocus", true)
		compare(room.border.width, Theme.focusRingWidth)
		compare(room.border.color, Theme.focus)
		participant.forceActiveFocus()
		tryCompare(participant, "activeFocus", true)
		compare(participant.border.width, Theme.focusRingWidth)
		compare(participant.border.color, Theme.focus)
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
		compare(commands.selectedScope, "")
		compare(commands.selectScopeCount, 0)
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
		compare(participantMenuSpy.signalArguments[0][3], "user")
		compare(commands.selectedParticipant, "")
		compare(commands.selectParticipantCount, 0)
		compare(committedSpy.count, 0)
	}

	function test_pointer_context_menu_does_not_change_active_scope() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		verify(room !== null)
		commands.selectedScope = "channel:2"
		mouseClick(room, room.width / 2, room.height / 2, Qt.RightButton)
		compare(scopeMenuSpy.count, 1)
		compare(scopeMenuSpy.signalArguments[0][0], "channel:1")
		compare(commands.selectedScope, "channel:2")
		compare(commands.selectScopeCount, 0)
	}

	function test_room_join_share_live_badge_and_actions_are_visible() {
		const joined = findChild(loader.item, "navigationRoomJoined_room-lobby")
		const liveBadge = findChild(loader.item, "navigationRoomShareBadge_room-lobby")
		const share = findChild(loader.item, "navigationRoomShare_room-lobby")
		const actions = findChild(loader.item, "navigationRoomActions_room-lobby")
		const join = findChild(loader.item, "navigationRoomJoin_room-games")
		verify(joined !== null && joined.visible)
		verify(liveBadge !== null && liveBadge.visible)
		verify(share !== null && share.visible && share.enabled)
		verify(actions !== null && actions.visible)
		verify(join !== null && join.visible && join.enabled)

		mouseClick(share, share.width / 2, share.height / 2, Qt.LeftButton)
		compare(commands.scopeActionCount, 1)
		compare(commands.scopeActionId, "screenShareOpenWindow")
		compare(commands.selectedScope, "channel:1")

		commands.selectedScope = ""
		mouseClick(join, join.width / 2, join.height / 2, Qt.LeftButton)
		compare(commands.joinVoiceCount, 1)
		compare(commands.selectedScope, "channel:2")
	}

	function test_participant_renders_avatar_talk_mute_deafen_badges_volume_and_actions() {
		const avatar = findChild(loader.item, "navigationParticipantAvatar_42")
		const talk = findChild(loader.item, "navigationParticipantTalk_42")
		const muted = findChild(loader.item, "navigationParticipantStatus_42_selfMuted")
		const deafened = findChild(loader.item, "navigationParticipantStatus_42_selfDeafened")
		const volume = findChild(loader.item, "navigationParticipantVolume_42")
		const details = findChild(loader.item, "navigationParticipantDetails_42")
		const actions = findChild(loader.item, "navigationParticipantActions_42")
		const join = findChild(loader.item, "navigationParticipantJoin_42")
		verify(avatar !== null && avatar.visible)
		verify(talk !== null && talk.visible)
		verify(muted !== null && muted.visible)
		verify(deafened !== null && deafened.visible)
		verify(volume !== null && volume.visible)
		verify(details !== null && details.visible)
		verify(String(details.text).indexOf("Talking") >= 0)
		verify(actions !== null && actions.visible)
		verify(join !== null && join.visible)
		mouseClick(join, join.width / 2, join.height / 2, Qt.LeftButton)
		compare(commands.participantActionCount, 1)
		compare(commands.participantActionId, "join")
		compare(commands.selectedParticipant, "42")
	}

	function test_listener_row_keeps_stable_identity_and_scope_actions_without_selection() {
		tryVerify(function() {
			return findChild(loader.item, "navigationParticipant_listener:2:42") !== null
		})
		const listener = findChild(loader.item, "navigationParticipant_listener:2:42")
		const status = findChild(loader.item, "navigationParticipantStatus_listener:2:42_listener")
		verify(status !== null && status.visible)
		listener.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(commands.selectParticipantCount, 0)
		keyClick(Qt.Key_Menu)
		compare(participantMenuSpy.count, 1)
		compare(participantMenuSpy.signalArguments[0][0], "42")
		compare(participantMenuSpy.signalArguments[0][3], "listener")
		compare(participantMenuSpy.signalArguments[0][4], "channel:2")
	}

	function test_profile_button_requests_accessible_profile_menu() {
		const button = findChild(loader.item, "profileMenuButton")
		verify(button !== null)
		button.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(profileMenuSpy.count, 1)
	}

	function test_self_controls_expose_mute_and_deafen_without_opening_profile_menu() {
		const muteButton = findChild(loader.item, "selfMuteButton")
		const deafenButton = findChild(loader.item, "selfDeafenButton")
		verify(muteButton !== null)
		verify(deafenButton !== null)
		compare(muteButton.Accessible.name, "Mute microphone")
		compare(deafenButton.Accessible.name, "Deafen")

		mouseClick(muteButton)
		mouseClick(deafenButton)
		compare(commands.selfMuteToggleCount, 1)
		compare(commands.selfDeafToggleCount, 1)
		compare(profileMenuSpy.count, 0)
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
		participant.forceActiveFocus()
		wait(0)
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
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const mouse = findChild(participant, "navigationParticipantMouse_42")
		verify(mouse !== null)
		const startX = participant.width / 2
		const startY = participant.height / 2
		mousePress(participant, startX, startY, Qt.LeftButton)
		compare(mouse.dragSourceStableId, "42")
		participants.setProperty(0, "stableId", "84")
		wait(0)
		compare(participant.stableId, "84")
		compare(mouse.dragSourceStableId, "42")
		mouse.clearDragSnapshot()
		mouseRelease(participant, startX, startY, Qt.LeftButton)
		participants.setProperty(0, "stableId", "42")
		tryCompare(participant, "stableId", "42")
		wait(0)
		compare(commands.movedParticipant, "")
		compare(commands.participantMoveTarget, "")
		compare(commands.participantMoveCount, 0)
		compare(mouse.dragSourceStableId, "")
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

		verify(loader.item.dispatchStableDrop("participant", "43", "0:0002", 20, 40))
		compare(commands.movedParticipant, "43")
		compare(commands.participantMoveTarget, "0:2")
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

		verify(loader.item.dispatchStableDrop("voice-room", "0:7", "0:1", 20, 40))
		compare(commands.movedScope, "0:7")
		compare(commands.scopeMoveTarget, "0:1")
		compare(commands.scopeMovePlacement, "inside")
	}

	function test_stable_drop_rejects_empty_or_unknown_payload() {
		verify(!loader.item.dispatchStableDrop("participant", " ", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "0", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "42", "voice:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("participant", "42", "channel:4294967296", 20, 40))
		verify(!loader.item.dispatchStableDrop("voice-room", "channel:0", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("voice-room", "0:0", "0:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("voice-room", "channel:1", "channel:1", 20, 40))
		verify(!loader.item.dispatchStableDrop("unknown", "42", "channel:1", 20, 40))
		compare(commands.movedParticipant, "")
		compare(commands.movedScope, "")
	}
}
