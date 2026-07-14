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
        id: navigationRows
		dynamicRoles: true
		Component.onCompleted: {
			append({
				"stableId": "room-lobby", "scopeToken": "channel:1", "title": "Lobby",
				"subtitle": "Welcome", "kind": "voice", "sectionKind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0, "status": "",
				"payload": { "rowKind": "room", "joined": true, "canJoin": false, "badges": ["Pinned"],
					"screenShare": { "visible": true, "mode": "publishing", "badgeLabel": "Live",
						"badgeTone": "success", "primaryActionId": "screenShareOpenWindow",
						"primaryLabel": "Manage share", "primaryEnabled": true, "primaryTone": "success" },
					"source": { "actions": [
					{ "kind": "action", "id": "join", "label": "Join", "enabled": true }
				] } }
			})
			append({
				"stableId": "user:42", "scopeToken": "channel:1", "title": "Alice", "subtitle": "Listening",
				"kind": "participant", "sectionKind": "voice", "selected": false, "depth": 0, "unreadCount": 0,
				"status": "talking", "payload": { "rowKind": "participant", "parentScopeToken": "channel:1",
					"parentKind": "voice", "parentDepth": 0, "participantSession": "42", "entryKind": "user",
					"actionsAvailable": true,
					"avatarUrl": "image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=7",
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
				"stableId": "room-games", "scopeToken": "channel:2", "title": "Games",
				"subtitle": "Playing", "kind": "voice", "sectionKind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0, "status": "",
				"payload": { "rowKind": "room", "joined": false, "canJoin": true, "screenShare": { "visible": true,
					"mode": "idle", "primaryActionId": "screenShareStart", "primaryEnabled": false },
					"source": { "actions": [] } }
			})
			append({
				"stableId": "listener:2:42", "scopeToken": "", "title": "Alice",
				"subtitle": "Listening to Games", "kind": "participant", "sectionKind": "voice", "selected": false,
				"depth": 0, "unreadCount": 0, "status": "passive",
				"payload": { "rowKind": "participant", "parentScopeToken": "channel:2", "parentKind": "voice", "parentDepth": 0,
					"participantSession": "42", "entryKind": "listener", "scopeToken": "channel:2",
					"actionsAvailable": true,
					"badges": ["Listener"], "statuses": [
						{ "kind": "listener", "label": "Listener", "tone": "accent" }
					], "source": { "session": "42", "entryKind": "listener", "scopeToken": "channel:2",
						"actions": [{ "kind": "action", "id": "listener.remove", "label": "Remove listener" }] } }
			})
			append({
				"stableId": "room-activity", "scopeToken": "-2:0", "title": "Activity",
				"subtitle": "Server notices", "kind": "text", "sectionKind": "text", "selected": false,
				"depth": 0, "unreadCount": 0, "status": "",
				"payload": { "rowKind": "room", "source": { "actions": [] } }
			})
			append({
				"stableId": "room-direct", "scopeToken": "-1:42", "title": "Alice",
				"subtitle": "Direct message", "kind": "direct", "sectionKind": "direct", "selected": false,
				"depth": 0, "unreadCount": 1, "status": "",
				"payload": { "rowKind": "room", "source": { "actions": [] } }
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
		property string connectionTone: "success"
		property string connectionDetail: "Current ping: 12 ms"
        property string selfStatusLabel: "Online"
        property bool connected: true
        property string selfName: "Tester"
		property bool selfMuted: false
		property bool selfDeafened: false
		property var selfMenu: ({
			"avatarUrl": "image://mumble/abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789?g=8"
		})
    }

    QtObject {
        id: commands
		property string selectedScope: ""
		property string selectedRailKind: ""
        property string selectedParticipant: ""
		property int selectScopeCount: 0
		property int selectScopeFromRailCount: 0
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
		property int participantActionsRequestCount: 0
		property string participantActionsRequestKey: ""
		property int selfMuteToggleCount: 0
		property int selfDeafToggleCount: 0
		function selectScope(token) { selectedScope = token; ++selectScopeCount }
		function selectScopeFromRail(token, kind) {
			selectedScope = token
			selectedRailKind = kind
			++selectScopeFromRailCount
		}
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
		function requestParticipantActions(sessionId, entryKind, scopeToken) {
			participantActionsRequestKey = sessionId + ":" + entryKind + ":" + scopeToken
			++participantActionsRequestCount
			return []
		}
		function toggleSelfMute() { ++selfMuteToggleCount }
		function toggleSelfDeaf() { ++selfDeafToggleCount }
    }

    Loader {
        id: loader
        anchors.fill: parent
		Component.onCompleted: {
			setSource("qrc:/qml-shell/NavigationRail.qml", {
				"navigationModel": navigationRows,
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
		if (roomList !== null) {
			roomList.currentIndex = 0
			roomList.positionViewAtBeginning()
			roomList.positionViewAtIndex(0, ListView.Beginning)
			roomList.forceLayout()
		}
		wait(0)
        committedSpy.clear()
		scopeMenuSpy.clear()
		participantMenuSpy.clear()
		profileMenuSpy.clear()
        commands.selectedScope = ""
		commands.selectedRailKind = ""
        commands.selectedParticipant = ""
		commands.selectScopeCount = 0
		commands.selectScopeFromRailCount = 0
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
		commands.participantActionsRequestCount = 0
		commands.participantActionsRequestKey = ""
		selection.selectedUserSession = undefined
		loader.item.activeScopeMenuToken = ""
		loader.item.activeParticipantMenuKey = ""
		const profile = findChild(loader.item, "profileMenuButton")
		if (profile !== null)
			profile.forceActiveFocus()
    }

	function test_header_restores_server_identity_and_connection_pill() {
		const header = findChild(loader.item, "navigationServerHeader")
		const badge = findChild(loader.item, "navigationServerBadge")
		const monogram = findChild(loader.item, "navigationServerMonogram")
		const serverName = findChild(loader.item, "navigationServerName")
		const pill = findChild(loader.item, "navigationConnectionPill")
		const dot = findChild(loader.item, "navigationConnectionDot")
		const connectionLabel = findChild(loader.item, "navigationConnectionLabel")
		verify(header !== null && badge !== null && monogram !== null && serverName !== null)
		verify(pill !== null && dot !== null && connectionLabel !== null)
		verify(header.height >= 98)
		compare(monogram.text, "TS")
		compare(String(monogram.color), String(Theme.onAccent))
		compare(serverName.text, "Test server")
		compare(connectionLabel.text, "Connected")
		compare(String(dot.color), String(Theme.success))
		compare(header.Accessible.role, Accessible.Grouping)
		compare(header.Accessible.name, "Test server")
		verify(header.Accessible.description.indexOf("Current ping: 12 ms") >= 0)
		compare(pill.Accessible.name, "Connected")
	}

	function test_voice_room_return_selects_and_commits_navigation() {
        const room = findChild(loader.item, "navigationRoom_room-lobby")
        verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Return)
        compare(commands.selectedScope, "channel:1")
		compare(commands.selectedRailKind, "voice")
		compare(commands.selectScopeFromRailCount, 1)
		compare(commands.selectScopeCount, 0)
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

	function test_text_row_propagates_its_rail_kind() {
		const room = findChild(loader.item, "navigationRoom_room-activity")
		verify(room !== null)
		room.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(commands.selectedScope, "-2:0")
		compare(commands.selectedRailKind, "text")
		compare(commands.selectScopeFromRailCount, 1)
		compare(commands.selectScopeCount, 0)
		compare(committedSpy.count, 1)
	}

	function test_participant_return_selects_and_commits_navigation() {
		const navigationList = findChild(loader.item, "navigationRooms")
		verify(navigationList !== null)
		compare(navigationList.count, 6)
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		participant.focusRow()
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
		participant.focusRow()
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
		navigationRows.setProperty(0, "selected", true)
		loader.item.focusInitialItem()
		tryVerify(function() {
			const room = findChild(loader.item, "navigationRoom_room-lobby")
			return room !== null && room.activeFocus
		})
		navigationRows.setProperty(0, "selected", false)
	}

	function test_initial_focus_prefers_selected_participant() {
		selection.selectedUserSession = "42"
		loader.item.focusInitialItem()
		tryVerify(function() {
			const participant = findChild(loader.item, "navigationParticipantSemantic_42")
			return participant !== null && participant.activeFocus
		})
		const navigationList = findChild(loader.item, "navigationRooms")
		compare(navigationList.currentIndex, 1)
		selection.selectedUserSession = undefined
	}

	function test_navigation_rows_use_one_roving_tab_stop() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const scrollBar = findChild(loader.item, "navigationScrollBar")
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipantSemantic_42")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		const direct = findChild(loader.item, "navigationRoom_room-direct")
		verify(navigationList !== null && scrollBar !== null && lobby !== null && participant !== null)
		verify(games !== null && activity !== null && direct !== null)
		verify(scrollBar.interactive)
		compare(scrollBar.orientation, Qt.Vertical)

		navigationList.currentIndex = 0
		wait(0)
		verify(navigationList.activeFocusOnTab)
		verify(!lobby.activeFocusOnTab)
		verify(!participant.activeFocusOnTab)
		verify(!games.activeFocusOnTab)
		verify(!activity.activeFocusOnTab)
		verify(!direct.activeFocusOnTab)

		for (const objectName of [
			"navigationRoomShare_room-lobby", "navigationRoomActions_room-lobby",
			"navigationParticipantJoin_42", "navigationParticipantActions_42",
			"navigationRoomJoin_room-games", "navigationRoomActions_room-games"
		]) {
			const action = findChild(loader.item, objectName)
			verify(action !== null, objectName)
			verify(!action.activeFocusOnTab, objectName)
		}

		navigationList.currentIndex = 2
		wait(0)
		verify(!lobby.activeFocusOnTab)
		verify(!participant.activeFocusOnTab)
		verify(!games.activeFocusOnTab)
		verify(!activity.activeFocusOnTab)
		verify(!direct.activeFocusOnTab)
	}

	function test_arrow_keys_move_roving_focus_without_activating_rows() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipantSemantic_42")
		const games = findChild(loader.item, "navigationRoom_room-games")
		verify(navigationList !== null && lobby !== null && participant !== null && games !== null)

		lobby.focusRow()
		compare(navigationList.currentIndex, 0)
		keyClick(Qt.Key_Down)
		tryCompare(navigationList, "currentIndex", 1)
		tryCompare(participant, "activeFocus", true)
		verify(navigationList.activeFocusOnTab)
		verify(!participant.activeFocusOnTab)
		verify(!lobby.activeFocusOnTab)

		keyClick(Qt.Key_Down)
		tryCompare(navigationList, "currentIndex", 2)
		tryCompare(games, "activeFocus", true)
		keyClick(Qt.Key_Up)
		tryCompare(navigationList, "currentIndex", 1)
		tryCompare(participant, "activeFocus", true)
		compare(commands.selectScopeFromRailCount, 0)
		compare(commands.selectParticipantCount, 0)
		compare(commands.joinVoiceCount, 0)
		compare(commands.directMessageCount, 0)
		compare(committedSpy.count, 0)
	}

	function test_pointer_and_action_focus_synchronize_roving_index() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const gamesMouse = findChild(loader.item, "navigationRoomMouse_room-games")
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		const participantJoin = findChild(loader.item, "navigationParticipantJoin_42")
		verify(navigationList !== null && gamesMouse !== null)
		verify(activity !== null && participantJoin !== null)

		mousePress(gamesMouse, gamesMouse.width / 2, gamesMouse.height / 2, Qt.LeftButton)
		tryCompare(navigationList, "currentIndex", 2)
		mouseRelease(gamesMouse, gamesMouse.width / 2, gamesMouse.height / 2, Qt.LeftButton)

		activity.forceActiveFocus()
		tryCompare(activity, "activeFocus", true)
		tryCompare(navigationList, "currentIndex", 4)

		const participant = findChild(loader.item, "navigationParticipantSemantic_42")
		participant.forceActiveFocus()
		tryCompare(participant, "activeFocus", true)
		tryCompare(navigationList, "currentIndex", 1)
		verify(navigationList.activeFocusOnTab)
		verify(!participantJoin.activeFocusOnTab)
	}

	function test_list_tab_stop_routes_arrows_return_space_and_context() {
		const navigationList = findChild(loader.item, "navigationRooms")
		verify(navigationList !== null)
		navigationList.currentIndex = 0
		navigationList.forceActiveFocus(Qt.TabFocusReason)
		tryCompare(navigationList, "activeFocus", true)
		keyClick(Qt.Key_Down)
		compare(navigationList.currentIndex, 1)
		keyClick(Qt.Key_Return)
		compare(commands.selectedParticipant, "42")
		compare(commands.selectParticipantCount, 1)
		compare(committedSpy.count, 1)

		commands.selectedParticipant = ""
		commands.directMessageCount = 0
		committedSpy.clear()
		keyClick(Qt.Key_Space)
		compare(commands.selectedParticipant, "42")
		compare(commands.directMessageCount, 1)
		compare(committedSpy.count, 1)

		participantMenuSpy.clear()
		keyClick(Qt.Key_Menu)
		compare(participantMenuSpy.count, 1)
		compare(participantMenuSpy.signalArguments[0][0], "42")
	}

	function test_participants_are_flat_rows_under_multiple_voice_rooms() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const alice = findChild(loader.item, "navigationParticipant_42")
		const listener = findChild(loader.item, "navigationParticipant_listener:2:42")
		const details = findChild(loader.item, "navigationRoomDetails_room-lobby")
		verify(navigationList !== null && room !== null && games !== null)
		verify(alice !== null && listener !== null && details !== null)
		compare(navigationList.count, 6)
		verify(alice.mapToItem(navigationList.contentItem, 0, 0).y
			> room.mapToItem(navigationList.contentItem, 0, 0).y)
		verify(alice.mapToItem(navigationList.contentItem, 0, 0).y
			< games.mapToItem(navigationList.contentItem, 0, 0).y)
		verify(listener.mapToItem(navigationList.contentItem, 0, 0).y
			> games.mapToItem(navigationList.contentItem, 0, 0).y)
		compare(alice.parentScopeToken, "channel:1")
		compare(listener.parentScopeToken, "channel:2")
		compare(alice.height, 36)
		compare(alice.mapToItem(navigationList, 0, 0).x,
			listener.mapToItem(navigationList, 0, 0).x)
		compare(details.text, "You are here")
		verify(String(room.color) !== String(Theme.selected))
		verify(String(room.border.color) !== String(Theme.accent))
	}

	function test_only_open_conversation_uses_selected_purple() {
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		const accent = findChild(loader.item, "navigationRoomSelectionAccent_room-activity")
		const gamesDetails = findChild(loader.item, "navigationRoomDetails_room-games")
		verify(activity !== null && accent !== null && gamesDetails !== null)
		verify(!gamesDetails.visible)
		navigationRows.setProperty(4, "selected", true)
		tryCompare(activity, "color", Theme.selected)
		compare(activity.border.width, 0)
		verify(accent.visible)
		compare(accent.color, Theme.accent)
		navigationRows.setProperty(4, "selected", false)
		tryCompare(accent, "visible", false)
	}

	function test_selected_participant_uses_selection_tokens() {
		var participant = null
		var semanticParticipant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			semanticParticipant = findChild(loader.item, "navigationParticipantSemantic_42")
			return participant !== null && semanticParticipant !== null
		})
		selection.selectedUserSession = "42"
		tryCompare(participant, "color", Theme.selected)
		compare(participant.border.width, 0)
		const accent = findChild(loader.item, "navigationParticipantSelectionAccent_42")
		verify(accent !== null && accent.visible)
		compare(accent.color, Theme.accent)
		verify(semanticParticipant.Accessible.selected)
		compare(semanticParticipant.Accessible.role, Accessible.ListItem)
		verify(participant.Accessible.ignored)
		selection.selectedUserSession = undefined
	}

	function test_room_and_participant_press_override_selection_with_theme_token() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const roomMouse = findChild(loader.item, "navigationRoomMouse_room-lobby")
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const participantMouse = findChild(loader.item, "navigationParticipantMouse_42")
		verify(room !== null && roomMouse !== null && participantMouse !== null)

		navigationRows.setProperty(0, "selected", true)
		tryCompare(room, "color", Theme.selected)
		mousePress(roomMouse, roomMouse.width / 2, roomMouse.height / 2, Qt.LeftButton)
		tryCompare(room, "color", Theme.accentSubtle)
		mouseRelease(roomMouse, roomMouse.width / 2, roomMouse.height / 2, Qt.LeftButton)
		navigationRows.setProperty(0, "selected", false)

		selection.selectedUserSession = "42"
		tryCompare(participant, "color", Theme.selected)
		mousePress(participantMouse, participantMouse.width / 2,
			participantMouse.height / 2, Qt.LeftButton)
		tryCompare(participant, "color", Theme.accentSubtle)
		mouseRelease(participantMouse, participantMouse.width / 2,
			participantMouse.height / 2, Qt.LeftButton)
		selection.selectedUserSession = undefined
	}

	function test_scrollbar_supports_wheel_and_pointer_drag() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const scrollBar = findChild(loader.item, "navigationScrollBar")
		const lobbyMouse = findChild(loader.item, "navigationRoomMouse_room-lobby")
		verify(navigationList !== null && scrollBar !== null && lobbyMouse !== null)
		const originalCount = navigationRows.count
		try {
			for (let index = 0; index < 24; ++index) {
				navigationRows.append({
					"stableId": "scroll-test-" + index,
					"scopeToken": "scroll:" + index,
					"title": "Scrollable room " + index,
					"subtitle": "Wheel and drag fixture",
					"kind": "text", "sectionKind": "text", "selected": false,
					"depth": 0, "unreadCount": 0, "status": "",
					"payload": { "rowKind": "room", "source": { "actions": [] } }
				})
			}
			navigationList.forceLayout()
			tryVerify(function() {
				return navigationList.contentHeight > navigationList.height && scrollBar.size < 1
			})

			navigationList.positionViewAtBeginning()
			wait(0)
			const wheelStart = navigationList.contentY
			mouseWheel(lobbyMouse, lobbyMouse.width / 2, lobbyMouse.height / 2,
				0, -120, Qt.NoButton, Qt.NoModifier, 20)
			tryVerify(function() { return navigationList.contentY > wheelStart })

			navigationList.positionViewAtBeginning()
			wait(0)
			const dragStart = navigationList.contentY
			const thumbCenter = Math.max(6, scrollBar.height * scrollBar.size / 2)
			mousePress(scrollBar, scrollBar.width / 2, thumbCenter, Qt.LeftButton)
			mouseMove(scrollBar, scrollBar.width / 2, scrollBar.height * 0.72, 40)
			mouseRelease(scrollBar, scrollBar.width / 2, scrollBar.height * 0.72, Qt.LeftButton)
			tryVerify(function() { return navigationList.contentY > dragStart + 8 })
		} finally {
			navigationRows.remove(originalCount, navigationRows.count - originalCount)
			navigationList.positionViewAtBeginning()
			navigationList.forceLayout()
		}
	}

	function test_sections_and_keyboard_focus_are_visually_explicit() {
		compare(loader.item.activeFocusOnTab, false)
		const voiceSection = findChild(loader.item, "navigationSection_voice")
		const textSection = findChild(loader.item, "navigationSection_text")
		const directSection = findChild(loader.item, "navigationSection_direct")
		verify(voiceSection !== null)
		verify(textSection !== null)
		verify(directSection !== null)
		compare(voiceSection.Accessible.name, "VOICE ROOMS · 2")
		compare(textSection.Accessible.name, "TEXT ROOMS · 1")
		compare(directSection.Accessible.name, "DIRECT MESSAGES · 1")
		for (const kind of [ "voice", "text", "direct" ]) {
			const visualLabel = findChild(loader.item, "navigationSectionLabel_" + kind)
			verify(visualLabel !== null)
			verify(visualLabel.Accessible.ignored)
			verify(String(visualLabel.text).indexOf(" · ") < 0)
			compare(String(visualLabel.color), String(Theme.withAlpha(Theme.accent, 0.76)))
		}
		compare(findChild(loader.item, "navigationSectionLabel_voice").text, "VOICE ROOMS")
		compare(findChild(loader.item, "navigationSectionLabel_text").text, "TEXT ROOMS")

		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipant_42")
		const semanticParticipant = findChild(loader.item, "navigationParticipantSemantic_42")
		verify(room !== null && participant !== null && semanticParticipant !== null)
		const roomTitle = findChild(loader.item, "navigationRoomTitle_room-lobby")
		const roomDetails = findChild(loader.item, "navigationRoomDetails_room-lobby")
		const participantTitle = findChild(loader.item, "navigationParticipantTitle_42")
		const participantDetails = findChild(loader.item, "navigationParticipantDetails_42")
		verify(roomTitle !== null && roomDetails !== null)
		verify(participantTitle !== null && participantDetails !== null)
		verify(roomTitle.Accessible.ignored)
		verify(roomDetails.Accessible.ignored)
		verify(participantTitle.Accessible.ignored)
		verify(participantDetails.Accessible.ignored)
		verify(room.Accessible.description.indexOf("You are here") >= 0)
		verify(room.Accessible.description.indexOf("Pinned") >= 0)
		verify(room.Accessible.description.indexOf("Live") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Talking") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Muted") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Local volume -6 dB") >= 0)
		room.forceActiveFocus()
		tryCompare(room, "activeFocus", true)
		compare(room.border.width, Theme.focusRingWidth)
		compare(room.border.color, Theme.focus)
		semanticParticipant.forceActiveFocus()
		tryCompare(semanticParticipant, "activeFocus", true)
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
		participant.focusRow()
		keyClick(Qt.Key_Menu)
		compare(participantMenuSpy.count, 1)
		compare(participantMenuSpy.signalArguments[0][0], "42")
		compare(participantMenuSpy.signalArguments[0][1][0].id, "message")
		compare(participantMenuSpy.signalArguments[0][3], "user")
		compare(participantMenuSpy.signalArguments[0][4], "channel:1")
		compare(participantMenuSpy.signalArguments[0][5], "user:42")
		compare(commands.participantActionsRequestCount, 1)
		compare(commands.participantActionsRequestKey, "42:user:channel:1")
		compare(commands.selectedParticipant, "")
		compare(commands.selectParticipantCount, 0)
		compare(committedSpy.count, 0)
	}

	function test_pointer_context_menu_does_not_change_active_scope() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		verify(room !== null)
		commands.selectedScope = "channel:2"
		mouseClick(room, room.width / 2,
			room.sectionHeaderHeight + room.roomRowHeight / 2, Qt.RightButton)
		compare(scopeMenuSpy.count, 1)
		compare(scopeMenuSpy.signalArguments[0][0], "channel:1")
		compare(commands.selectedScope, "channel:2")
		compare(commands.selectScopeCount, 0)
	}

	function test_room_join_share_and_overflow_take_priority_over_badge_noise() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const joined = findChild(loader.item, "navigationRoomJoined_room-lobby")
		const liveBadge = findChild(loader.item, "navigationRoomShareBadge_room-lobby")
		const roomBadge = findChild(loader.item, "navigationRoomBadge_room-lobby")
		const share = findChild(loader.item, "navigationRoomShare_room-lobby")
		const actions = findChild(loader.item, "navigationRoomActions_room-lobby")
		const join = findChild(loader.item, "navigationRoomJoin_room-games")
		const profile = findChild(loader.item, "profileMenuButton")
		verify(profile !== null)
		profile.forceActiveFocus()
		tryCompare(profile, "activeFocus", true)
		mouseMove(loader.item, loader.item.width - 2, 2)
		wait(0)
		verify(joined !== null && !joined.visible)
		verify(liveBadge !== null && !liveBadge.visible)
		verify(roomBadge !== null && !roomBadge.visible)
		verify(share !== null && share.visible && share.enabled)
		verify(room !== null && games !== null && actions !== null && join !== null)
		verify(actions.visible)
		verify(join.visible)
		verify(join.contentItem.Accessible.ignored)
		compare(join.Accessible.name, "Join")
		verify(share.contentItem.Accessible.ignored)
		compare(share.Accessible.name, "Live")
		verify(actions.opacity >= 0.62 && actions.opacity <= 1)
		verify(join.opacity >= 0.68 && join.opacity <= 1)
		verify(share.opacity >= 0.82 && share.opacity <= 1)
		room.forceActiveFocus()
		tryCompare(room, "activeFocus", true)
		tryCompare(actions, "opacity", 1)
		share.forceActiveFocus()
		tryCompare(share, "activeFocus", true)
		compare(share.background.border.color, Theme.focus)
		compare(share.background.border.width, Theme.focusRingWidth)
		games.forceActiveFocus()
		tryCompare(games, "activeFocus", true)
		verify(join.visible && join.enabled)
		tryCompare(join, "opacity", 1)
		join.forceActiveFocus()
		tryCompare(join, "activeFocus", true)
		compare(join.background.border.color, Theme.focus)

		commands.selectedScope = ""
		mouseClick(join, join.width / 2, join.height / 2, Qt.LeftButton)
		compare(commands.joinVoiceCount, 1)
		compare(commands.selectedScope, "channel:2")

		room.forceActiveFocus()
		tryCompare(room, "activeFocus", true)
		share.forceActiveFocus()
		tryCompare(share, "activeFocus", true)
		mouseClick(share, share.width / 2, share.height / 2, Qt.LeftButton)
		compare(commands.scopeActionCount, 1)
		compare(commands.scopeActionId, "screenShareOpenWindow")
		compare(commands.selectedScope, "channel:1")
	}

	function test_open_context_menu_keeps_its_anchor_actions_visible() {
		const profile = findChild(loader.item, "profileMenuButton")
		const roomActions = findChild(loader.item, "navigationRoomActions_room-lobby")
		const participantActions = findChild(loader.item, "navigationParticipantActions_42")
		verify(profile !== null && roomActions !== null && participantActions !== null)
		profile.forceActiveFocus()
		tryCompare(profile, "activeFocus", true)
		mouseMove(loader.item, loader.item.width - 2, 2)
		wait(0)
		verify(roomActions.visible)
		verify(participantActions.visible)
		verify(roomActions.opacity >= 0.62 && roomActions.opacity <= 1)
		verify(participantActions.opacity >= 0.72 && participantActions.opacity <= 1)

		loader.item.activeScopeMenuToken = "channel:1"
		loader.item.activeParticipantMenuKey = "user:42"
		tryCompare(roomActions, "opacity", 1)
		tryCompare(participantActions, "opacity", 1)

		loader.item.activeScopeMenuToken = ""
		loader.item.activeParticipantMenuKey = ""
		verify(roomActions.visible)
		verify(participantActions.visible)
	}

	function test_participant_renders_avatar_talk_mute_deafen_badges_volume_and_actions() {
		const participant = findChild(loader.item, "navigationParticipant_42")
		const avatar = findChild(loader.item, "navigationParticipantAvatar_42")
		const avatarImage = findChild(loader.item, "navigationParticipantAvatarImage_42")
		const talk = findChild(loader.item, "navigationParticipantTalk_42")
		const talkChip = findChild(loader.item, "navigationParticipantStatus_42_talking")
		const muted = findChild(loader.item, "navigationParticipantStatus_42_selfMuted")
		const deafened = findChild(loader.item, "navigationParticipantStatus_42_selfDeafened")
		const guide = findChild(loader.item, "navigationParticipantGuide_42")
		const volume = findChild(loader.item, "navigationParticipantVolume_42")
		const details = findChild(loader.item, "navigationParticipantDetails_42")
		const actions = findChild(loader.item, "navigationParticipantActions_42")
		const join = findChild(loader.item, "navigationParticipantJoin_42")
		verify(participant !== null)
		participant.focusRow()
		verify(avatar !== null && avatar.visible)
		verify(avatarImage !== null)
		compare(String(avatarImage.source),
			"image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=7")
		compare(avatarImage.sourceSize.width, 52)
		compare(avatarImage.sourceSize.height, 52)
		verify(talk !== null && talk.visible)
		compare(talkChip, null)
		verify(muted !== null && muted.visible)
		verify(deafened !== null && deafened.visible)
		verify(guide !== null && guide.visible)
		compare(guide.color, Theme.selfCardBorder)
		verify(volume !== null && volume.visible)
		verify(details !== null && details.visible)
		verify(String(details.text).indexOf("Talking") >= 0)
		verify(actions !== null && actions.visible)
		verify(join !== null && join.visible)
		verify(join.contentItem.Accessible.ignored)
		compare(join.Accessible.name, "Join")
		join.forceActiveFocus()
		tryCompare(join, "activeFocus", true)
		compare(join.background.border.color, Theme.focus)
		mouseClick(join, join.width / 2, join.height / 2, Qt.LeftButton)
		compare(commands.participantActionCount, 1)
		compare(commands.participantActionId, "join")
		compare(commands.selectedParticipant, "42")
	}

	function test_source_only_action_capability_and_missing_talk_state_stay_correct() {
		const previousPayload = navigationRows.get(1).payload
		const previousStatus = navigationRows.get(1).status
		navigationRows.setProperty(1, "status", "")
		navigationRows.setProperty(1, "payload", {
			"rowKind": "participant", "parentScopeToken": "channel:1",
			"participantSession": "42", "entryKind": "user", "talking": false,
			"canJoin": false, "actions": [],
			"source": { "session": "42", "entryKind": "user",
				"actionsAvailable": true, "actions": [] }
		})
		wait(0)
		const participant = findChild(loader.item, "navigationParticipant_42")
		const actions = findChild(loader.item, "navigationParticipantActions_42")
		verify(participant !== null && actions !== null)
		compare(participant.talking, false)
		verify(actions.visible)

		navigationRows.setProperty(1, "payload", previousPayload)
		navigationRows.setProperty(1, "status", previousStatus)
		wait(0)
	}

	function test_avatar_sources_accept_only_managed_or_inline_images() {
		const managed = "image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=9"
		compare(loader.item.safeAvatarSource(managed), managed)
		compare(loader.item.safeAvatarSource("data:image/png;base64,AAAA"), "data:image/png;base64,AAAA")
		compare(loader.item.safeAvatarSource("image://other/avatar"), "")
		compare(loader.item.safeAvatarSource("https://example.com/avatar.png"), "")

		const selfAvatar = findChild(loader.item, "selfAvatar")
		const selfAvatarImage = findChild(loader.item, "selfAvatarImage")
		const selfAvatarFallback = findChild(loader.item, "selfAvatarFallback")
		verify(selfAvatar !== null && selfAvatarImage !== null && selfAvatarFallback !== null)
		compare(selfAvatarFallback.Accessible.ignored, true)
		compare(selfAvatar.avatarSource,
			"image://mumble/abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789?g=8")
		compare(String(selfAvatarImage.source), selfAvatar.avatarSource)
		compare(selfAvatarImage.sourceSize.width, 68)
		compare(selfAvatarImage.sourceSize.height, 68)
	}

	function test_listener_row_keeps_stable_identity_and_scope_actions_without_selection() {
		tryVerify(function() {
			return findChild(loader.item, "navigationParticipant_listener:2:42") !== null
		})
		const listener = findChild(loader.item, "navigationParticipant_listener:2:42")
		const status = findChild(loader.item, "navigationParticipantStatus_listener:2:42_listener")
		verify(status !== null && status.visible)
		listener.focusRow()
		keyClick(Qt.Key_Return)
		compare(commands.selectParticipantCount, 0)
		keyClick(Qt.Key_Menu)
		compare(participantMenuSpy.count, 1)
		compare(participantMenuSpy.signalArguments[0][0], "42")
		compare(participantMenuSpy.signalArguments[0][3], "listener")
		compare(participantMenuSpy.signalArguments[0][4], "channel:2")
		compare(participantMenuSpy.signalArguments[0][5], "listener:2:42")
		compare(commands.participantActionsRequestCount, 1)
		compare(commands.participantActionsRequestKey, "42:listener:channel:2")
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

	function test_self_dock_uses_semantic_tokens_presence_and_explicit_focus_order() {
		const dock = findChild(loader.item, "navigationSelfDock")
		const avatar = findChild(loader.item, "selfAvatar")
		const presence = findChild(loader.item, "selfPresenceDot")
		const nameLabel = findChild(loader.item, "selfNameLabel")
		const statusLabel = findChild(loader.item, "selfStatusLabel")
		const muteButton = findChild(loader.item, "selfMuteButton")
		const deafenButton = findChild(loader.item, "selfDeafenButton")
		const profileButton = findChild(loader.item, "profileMenuButton")
		verify(dock !== null && avatar !== null && presence !== null)
		verify(nameLabel !== null && statusLabel !== null)
		verify(muteButton !== null && deafenButton !== null && profileButton !== null)
		compare(dock.Accessible.role, Accessible.Grouping)
		verify(dock.Accessible.name.indexOf("Tester") >= 0)
		verify(dock.Accessible.description.indexOf("Online") >= 0)
		verify(nameLabel.Accessible.ignored)
		verify(statusLabel.Accessible.ignored)
		compare(String(statusLabel.color), String(Theme.textFaint))
		compare(String(presence.color), String(Theme.success))
		compare(String(dock.color), String(Theme.selfCardHover))

		muteButton.forceActiveFocus()
		tryCompare(muteButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(deafenButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(profileButton, "activeFocus", true)
		keyClick(Qt.Key_Backtab)
		tryCompare(deafenButton, "activeFocus", true)

		session.selfMuted = true
		tryCompare(presence, "color", Theme.danger)
		verify(dock.Accessible.description.indexOf("Microphone muted") >= 0)
		session.selfMuted = false
	}

	function test_pointer_clicks_remain_clicks_with_drag_handlers() {
		const roomItem = findChild(loader.item, "navigationRoom_room-lobby")
		var participantItem = null
		verify(roomItem !== null)
		tryVerify(function() {
			participantItem = findChild(loader.item, "navigationParticipant_42")
			return participantItem !== null
		})
		mouseClick(roomItem, roomItem.width / 2,
			roomItem.sectionHeaderHeight + roomItem.roomRowHeight / 2, Qt.LeftButton)
		compare(commands.selectedScope, "channel:1")
		compare(commands.selectedRailKind, "voice")
		compare(commands.selectScopeFromRailCount, 1)
		compare(commands.selectScopeCount, 0)
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
		participant.focusRow()
		wait(0)
		const startX = participant.width / 2
		const startY = participant.height / 2
		const target = participant.mapFromItem(room, room.width / 2,
			room.sectionHeaderHeight + room.roomRowHeight / 2)
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
		mousePress(mouse, mouse.width / 2, mouse.height / 2, Qt.LeftButton)
		compare(mouse.dragSourceStableId, "42")
		navigationRows.setProperty(1, "stableId", "user:84")
		wait(0)
		compare(participant.stableId, "user:84")
		compare(mouse.dragSourceStableId, "42")
		mouse.clearDragSnapshot()
		mouseRelease(mouse, mouse.width / 2, mouse.height / 2, Qt.LeftButton)
		navigationRows.setProperty(1, "stableId", "user:42")
		tryCompare(participant, "stableId", "user:42")
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
		const startY = source.sectionHeaderHeight + source.roomRowHeight / 2
		const target = source.mapFromItem(targetRoom, targetRoom.width / 2,
			targetRoom.sectionHeaderHeight + targetRoom.roomRowHeight / 2)

		mousePress(source, startX, startY, Qt.LeftButton)
		compare(mouse.dragSourceScopeToken, "channel:1")
		navigationRows.setProperty(0, "scopeToken", "channel:99")
		wait(0)
		compare(source.scopeToken, "channel:99")
		compare(mouse.dragSourceScopeToken, "channel:1")
		mouseMove(source, target.x, target.y, 20)
		mouseRelease(source, target.x, target.y, Qt.LeftButton)
		tryCompare(commands, "movedScope", "channel:1")
		compare(commands.scopeMoveTarget, "channel:2")
		compare(commands.scopeMoveCount, 1)
		compare(mouse.dragSourceScopeToken, "")
		navigationRows.setProperty(0, "scopeToken", "channel:1")
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

		mousePress(roomMouse, roomMouse.width / 2, roomMouse.height / 2, Qt.LeftButton)
		compare(roomMouse.dragSourceScopeToken, "channel:1")
		roomMouse.clearDragSnapshot()
		compare(roomMouse.dragSourceScopeToken, "")
		mouseRelease(roomMouse, roomMouse.width / 2, roomMouse.height / 2, Qt.LeftButton)

		mousePress(participantMouse, participantMouse.width / 2, participantMouse.height / 2, Qt.LeftButton)
		compare(participantMouse.dragSourceStableId, "42")
		participantMouse.clearDragSnapshot()
		compare(participantMouse.dragSourceStableId, "")
		mouseRelease(participantMouse, participantMouse.width / 2, participantMouse.height / 2, Qt.LeftButton)
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
