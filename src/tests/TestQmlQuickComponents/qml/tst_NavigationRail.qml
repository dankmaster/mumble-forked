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
					"participantCount": 1, "talkingParticipantCount": 1,
					"screenShare": { "visible": true, "mode": "publishing", "streamId": "stream:1",
						"ownerLabel": "Tester", "statusLabel": "You are sharing in this room",
						"resolutionLabel": "1920x1080 @ 30 fps", "runtimeLabel": "GStreamer GPU",
						"badgeLabel": "Live",
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
				"payload": { "rowKind": "room", "joined": false, "canJoin": true,
					"participantCount": 1, "talkingParticipantCount": 0, "screenShare": { "visible": true,
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
				"subtitle": "Server notices", "kind": "text", "sectionKind": "tool", "selected": false,
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
		property string scopeToken: ""
		property var selectedUserSession: undefined
		property var selectedVoiceChannelId: undefined
	}

	Component {
		id: isolatedNavigationRowsComponent
		ListModel { dynamicRoles: true }
	}

	Component {
		id: isolatedSelectionComponent
		QtObject {
			property string scopeToken: ""
			property var selectedUserSession: undefined
			property var selectedVoiceChannelId: undefined
		}
	}

	Component {
		id: isolatedRailLoaderComponent
		Loader {
			anchors.fill: parent
			z: 100
		}
	}

    QtObject {
        id: session
		property string serverName: "Test server"
		property string serverMonogram: "1M"
		property string serverImageUrl: "image://mumble/server-identity-test?g=1"
        property string connectionLabel: "Connected"
		property string connectionTone: "success"
		property string connectionDetail: "Current ping: 12 ms"
        property string selfStatusLabel: "Online"
        property bool connected: true
        property string selfName: "Tester"
		property bool selfMuted: false
		property bool selfDeafened: false
		property var collapsedNavigationSections: []
		property var stonks: ({ "supported": true })
		property var selfMenu: ({
			"avatarUrl": "image://mumble/abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789?g=8"
		})
		function setNavigationSectionExpanded(sectionKind, expanded) {
			const sections = collapsedNavigationSections.slice()
			const index = sections.indexOf(sectionKind)
			if (expanded && index >= 0)
				sections.splice(index, 1)
			else if (!expanded && index < 0)
				sections.push(sectionKind)
			collapsedNavigationSections = sections
		}
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
				"stonksEnabled": true,
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

	SignalSpy {
		id: serverMenuSpy
		target: loader.item
		signalName: "serverMenuRequested"
	}

	SignalSpy {
		id: settingsSpy
		target: loader.item
		signalName: "settingsRequested"
	}

	SignalSpy {
		id: stonksSpy
		target: loader.item
		signalName: "stonksRequested"
	}

    function init() {
		tryVerify(function() { return loader.item !== null })
		loader.item.alignedHeaderHeight = Theme.railHeaderHeight
		loader.item.alignedFooterHeight = Theme.railFooterHeight
		loader.item.railSide = Theme.railSide
		loader.item.classicUserIcons = false
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
		serverMenuSpy.clear()
		settingsSpy.clear()
		stonksSpy.clear()
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
		selection.selectedVoiceChannelId = undefined
		selection.scopeToken = ""
		loader.item.localRoomExpansion = ({})
		session.collapsedNavigationSections = []
		loader.item.localCollapsedNavigationSections = []
		loader.item.setNavigationFilter("")
		loader.item.activeScopeMenuToken = ""
		loader.item.activeParticipantMenuKey = ""
		loader.item.accessibilitySuppressed = false
		loader.item.settingsEnabled = true
		loader.item.stonksEnabled = true
		loader.item.serverMenuOpen = false
		const profile = findChild(loader.item, "selfIdentityButton")
		if (profile !== null)
			profile.forceActiveFocus()
	}

	function test_modal_suppression_covers_virtualized_room_and_participant_rows() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const barrier = findChild(loader.item, "navigationRailAccessibilityBarrier")
		verify(navigationList !== null && barrier !== null)
		navigationList.positionViewAtBeginning()
		navigationList.forceLayout()
		const room = navigationList.itemAtIndex(0)
		const participant = navigationList.itemAtIndex(1)
		verify(room !== null && participant !== null)
		verify(!room.Accessible.ignored && !participant.Accessible.ignored)

		loader.item.accessibilitySuppressed = true
		tryCompare(barrier, "active", true)
		tryCompare(room.Accessible, "ignored", true)
		tryCompare(participant.Accessible, "ignored", true)
		verify(room.visible && participant.visible,
			"Modal accessibility suppression must not hide visual navigation rows")

		loader.item.accessibilitySuppressed = false
		tryCompare(barrier, "active", false)
		tryCompare(room.Accessible, "ignored", false)
		tryCompare(participant.Accessible, "ignored", false)
	}

	function test_cached_rows_outside_viewport_withdraw_complete_accessibility_subtree() {
		const navigationList = findChild(loader.item, "navigationRooms")
		verify(navigationList !== null)
		const originalCount = navigationRows.count
		try {
			for (let index = 0; index < 16; ++index) {
				navigationRows.append({
					"stableId": "cache-room-" + index,
					"scopeToken": "cache:" + index,
					"title": "Cached room " + index,
					"subtitle": "Outside viewport",
					"kind": "text", "sectionKind": "text", "selected": false,
					"depth": 0, "unreadCount": 0, "status": "",
					"payload": { "rowKind": "room", "source": { "actions": [] } }
				})
			}
			navigationList.positionViewAtBeginning()
			navigationList.forceLayout()
			wait(0)

			let cachedRow = null
			for (let index = 0; index < navigationList.count; ++index) {
				const candidate = navigationList.itemAtIndex(index)
				if (candidate && candidate.navigationVisible
						&& !candidate.accessibilityViewportVisible) {
					cachedRow = candidate
					break
				}
			}
			verify(cachedRow !== null,
				"The bounded ListView cache should materialize at least one clipped row")
			const stableId = String(cachedRow.stableId)
			const cachedAction = findChild(cachedRow, "navigationRoomActions_" + stableId)
			const cachedBarrier = findChild(cachedRow,
				"navigationRoomAccessibilityBarrier_" + stableId)
			verify(cachedAction !== null && cachedBarrier !== null)
			tryCompare(cachedBarrier, "active", true)
			tryCompare(cachedRow.Accessible, "ignored", true)
			tryCompare(cachedAction.Accessible, "ignored", true)
			verify(cachedRow.visible,
				"Accessibility suppression must not remove the cached visual delegate")

			navigationList.positionViewAtIndex(cachedRow.index, ListView.Contain)
			navigationList.forceLayout()
			wait(0)
			const exposedRow = findChild(loader.item, "navigationRoom_" + stableId)
			verify(exposedRow !== null)
			tryCompare(exposedRow, "accessibilityViewportVisible", true)
			tryCompare(exposedRow.Accessible, "ignored", false)
		} finally {
			while (navigationRows.count > originalCount)
				navigationRows.remove(originalCount)
			navigationList.positionViewAtBeginning()
			navigationList.forceLayout()
			wait(0)
		}
	}

	function test_visual_fixture_ignores_workstation_hover_without_disabling_product_hover() {
		const room = findChild(loader.item, "navigationRoom_room-games")
		const roomSurface = findChild(loader.item, "navigationRoomSurface_room-games")
		const participantActions = findChild(loader.item, "navigationParticipantActions_42")
		verify(room !== null && roomSurface !== null && participantActions !== null)
		const expectedIdleColor = String(roomSurface.color)
		try {
			loader.item.visualFixtureMode = true
			mouseMove(room, room.width / 2, room.height / 2)
			wait(Theme.motionFast + 20)
			compare(String(roomSurface.color), expectedIdleColor)
			compare(room.revealActions, false)
			mousePress(room, room.width / 2, room.height / 2, Qt.LeftButton)
			compare(String(roomSurface.color), expectedIdleColor)
			mouseRelease(room, room.width / 2, room.height / 2, Qt.LeftButton)
			compare(participantActions.hoverEnabled, false)
			mouseMove(participantActions, participantActions.width / 2,
				participantActions.height / 2)
			wait(Theme.motionFast + 20)
			compare(participantActions.hovered, false)

			loader.item.visualFixtureMode = false
			compare(participantActions.hoverEnabled, true)
			tryCompare(participantActions, "hovered", true)
			mouseMove(room, room.width / 2, room.height / 2)
			tryCompare(room, "revealActions", true)
			tryCompare(roomSurface, "color", Theme.surfaceHover)
		} finally {
			loader.item.visualFixtureMode = false
			mouseMove(loader.item, loader.item.width - 2, 2)
		}
	}

	function test_virtualized_semantic_owners_bind_modal_suppression_directly() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const barrier = findChild(loader.item, "navigationRailAccessibilityBarrier")
		verify(navigationList !== null && barrier !== null)
		navigationList.positionViewAtBeginning()
		navigationList.forceLayout()
		const room = navigationList.itemAtIndex(0)
		const participant = navigationList.itemAtIndex(1)
		const voiceSection = findChild(loader.item, "navigationSection_voice")
		const reassertionTimer = findChild(loader.item, "navigationAccessibilityReassertionTimer")
		verify(room !== null && participant !== null && voiceSection !== null
			&& reassertionTimer !== null)
		const originalTargets = barrier.targets
		try {
			// Remove generic traversal, then simulate Qt 6.9 ItemView's private
			// isAccessible=visible overwrite after the modal has suppressed rows.
			barrier.targets = []
			loader.item.accessibilitySuppressed = true
			tryCompare(reassertionTimer, "running", true)
			tryCompare(room.Accessible, "ignored", true)
			tryCompare(participant.Accessible, "ignored", true)
			tryCompare(voiceSection.Accessible, "ignored", true)

			room.Accessible.ignored = false
			participant.Accessible.ignored = false
			voiceSection.Accessible.ignored = false
			compare(room.Accessible.ignored, false)
			compare(participant.Accessible.ignored, false)
			compare(voiceSection.Accessible.ignored, false)
			tryCompare(room.Accessible, "ignored", true)
			tryCompare(participant.Accessible, "ignored", true)
			tryCompare(voiceSection.Accessible, "ignored", true)
			verify(room.visible && participant.visible && voiceSection.visible)

			loader.item.accessibilitySuppressed = false
			tryCompare(reassertionTimer, "running", false)
			tryCompare(room.Accessible, "ignored", false)
			tryCompare(participant.Accessible, "ignored", false)
			tryCompare(voiceSection.Accessible, "ignored", false)

			// Closing the modal must restore the delegate's original pooled-state
			// binding rather than leave behind the temporary override.
			room.accessibilityPooled = true
			tryCompare(room.Accessible, "ignored", true)
			room.accessibilityPooled = false
			tryCompare(room.Accessible, "ignored", false)
		} finally {
			loader.item.accessibilitySuppressed = false
			barrier.targets = originalTargets
		}
	}

	function test_header_restores_server_identity_and_connection_pill() {
		const header = findChild(loader.item, "navigationServerHeader")
		const badge = findChild(loader.item, "navigationServerBadge")
		const monogram = findChild(loader.item, "navigationServerMonogram")
		const serverImage = findChild(loader.item, "navigationServerImage")
		const serverName = findChild(loader.item, "navigationServerName")
		const pill = findChild(loader.item, "navigationConnectionPill")
		const dot = findChild(loader.item, "navigationConnectionDot")
		const connectionLabel = findChild(loader.item, "navigationConnectionLabel")
		verify(header !== null && badge !== null && monogram !== null && serverImage !== null
			&& serverName !== null)
		verify(pill !== null && dot !== null && connectionLabel !== null)
		compare(header.height, Theme.railHeaderHeight)
		compare(monogram.text, "1M")
		compare(String(monogram.color), String(Theme.onAccent))
		compare(serverImage.source.toString(), session.serverImageUrl)
		tryCompare(serverImage, "status", Image.Ready)
		compare(serverImage.visible, true)
		compare(monogram.visible, false)
		compare(serverName.text, "Test server")
		compare(connectionLabel.text, "Connected")
		compare(String(dot.color), String(Theme.success))
		compare(header.Accessible.role, Accessible.Button)
		verify(header.Accessible.name.indexOf("Test server") >= 0)
		verify(header.Accessible.description.indexOf("Current ping: 12 ms") >= 0)
		compare(pill.Accessible.name, "Connected")
		compare(badge.Accessible.ignored, true)
		compare(badge.activeFocusOnTab, false)
	}

	function test_server_header_centers_inward_and_mirrors_with_rail_side() {
		const header = findChild(loader.item, "navigationServerHeader")
		const content = findChild(loader.item, "navigationServerHeaderContent")
		const badge = findChild(loader.item, "navigationServerBadge")
		const serverName = findChild(loader.item, "navigationServerName")
		const actionsButton = findChild(loader.item, "navigationServerActions")
		const pill = findChild(loader.item, "navigationConnectionPill")
		verify(header !== null && content !== null && badge !== null
			&& serverName !== null && actionsButton !== null && pill !== null)

		loader.item.railSide = "left"
		tryVerify(function() {
			return badge.mapToItem(header, 0, 0).x
				< serverName.mapToItem(header, 0, 0).x
		})
		let badgePosition = badge.mapToItem(header, 0, 0)
		let namePosition = serverName.mapToItem(header, 0, 0)
		let actionsPosition = actionsButton.mapToItem(header, 0, 0)
		verify(badgePosition.x < namePosition.x,
			"Left rail must place the server badge before the server name: badge="
				+ badgePosition.x + ", name=" + namePosition.x)
		verify(actionsPosition.x > namePosition.x)
		verify(Math.abs(badgePosition.x - Theme.space5) < 0.5)
		compare(serverName.horizontalAlignment, Text.AlignLeft)
		compare(pill.Layout.alignment, Qt.AlignLeft)

		loader.item.railSide = "right"
		tryVerify(function() {
			return badge.mapToItem(header, 0, 0).x
				> serverName.mapToItem(header, 0, 0).x
		})
		badgePosition = badge.mapToItem(header, 0, 0)
		namePosition = serverName.mapToItem(header, 0, 0)
		actionsPosition = actionsButton.mapToItem(header, 0, 0)
		verify(badgePosition.x > namePosition.x,
			"Right rail must place the server badge after the server name: badge="
				+ badgePosition.x + ", name=" + namePosition.x)
		verify(actionsPosition.x < namePosition.x)
		verify(Math.abs((header.width - badgePosition.x - badge.width) - Theme.space5) < 0.5)
		compare(serverName.horizontalAlignment, Text.AlignRight)
		compare(pill.Layout.alignment, Qt.AlignRight)
	}

	function test_server_actions_have_an_always_visible_menu_button() {
		const button = findChild(loader.item, "navigationServerActions")
		verify(button !== null)
		compare(button.visible, true)
		compare(button.iconName, "more")
		verify(button.opacity >= 0.7)
		verify(button.Accessible.name.indexOf("Test server") >= 0)
		verify(button.Accessible.description.indexOf("connection") >= 0)

		mouseClick(button)
		compare(serverMenuSpy.count, 1)
		verify(serverMenuSpy.signalArguments[0][0].x >= 0)
		verify(serverMenuSpy.signalArguments[0][0].y >= 0)

		loader.item.serverMenuOpen = true
		tryCompare(button, "selected", true)
		loader.item.serverMenuOpen = false
	}

	function test_complete_server_card_opens_menu_by_pointer_keyboard_and_accessibility() {
		const header = findChild(loader.item, "navigationServerHeader")
		const badge = findChild(loader.item, "navigationServerBadge")
		const serverName = findChild(loader.item, "navigationServerName")
		const pill = findChild(loader.item, "navigationConnectionPill")
		verify(header !== null && badge !== null && serverName !== null && pill !== null)

		mouseClick(badge)
		compare(serverMenuSpy.count, 1)
		mouseClick(serverName)
		compare(serverMenuSpy.count, 2)
		mouseClick(pill)
		compare(serverMenuSpy.count, 3)
		compare(header.activeFocus, false)

		header.forceActiveFocus(Qt.TabFocusReason)
		tryCompare(header, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(serverMenuSpy.count, 4)
		keyClick(Qt.Key_Space)
		compare(serverMenuSpy.count, 5)
		header.Accessible.pressAction()
		compare(serverMenuSpy.count, 6)
	}

	function test_server_logo_falls_back_to_configured_monogram_and_opens_menu() {
		const header = findChild(loader.item, "navigationServerHeader")
		const badge = findChild(loader.item, "navigationServerBadge")
		const monogram = findChild(loader.item, "navigationServerMonogram")
		const serverImage = findChild(loader.item, "navigationServerImage")
		verify(header !== null && badge !== null && monogram !== null && serverImage !== null)
		const originalImage = session.serverImageUrl
		try {
			session.serverImageUrl = ""
			tryCompare(monogram, "visible", true)
			compare(monogram.text, "1M")
			mouseClick(badge)
			compare(serverMenuSpy.count, 1)
			verify(serverMenuSpy.signalArguments[0][0].x >= 0)
			verify(serverMenuSpy.signalArguments[0][0].y >= 0)
			header.forceActiveFocus(Qt.TabFocusReason)
			keyClick(Qt.Key_Return)
			compare(serverMenuSpy.count, 2)
		} finally {
			session.serverImageUrl = originalImage
		}
	}

	function test_desktop_chrome_heights_can_share_exact_divider_geometry() {
		const header = findChild(loader.item, "navigationServerHeader")
		const dock = findChild(loader.item, "navigationSelfDock")
		verify(header !== null && dock !== null)
		loader.item.alignedHeaderHeight = 104
		loader.item.alignedFooterHeight = 82
		tryCompare(header, "height", 104)
		tryCompare(dock, "height", 82)
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

	function test_voice_room_disclosure_preserves_collapsed_activity_summary() {
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipantSemantic_42")
		const disclosure = findChild(loader.item, "navigationRoomDisclosure_room-lobby")
		const countBadge = findChild(loader.item, "navigationRoomParticipantCount_room-lobby")
		const countLabel = findChild(loader.item, "navigationRoomParticipantCountLabel_room-lobby")
		verify(lobby !== null && participant !== null && disclosure !== null
			&& countBadge !== null && countLabel !== null)
		verify(disclosure.visible)
		verify(disclosure.Accessible.name.indexOf("Collapse") >= 0)
		verify(disclosure.Accessible.description.indexOf("1 participant") >= 0)
		compare(countBadge.visible, true)
		compare(countLabel.text, "1/1")
		verify(lobby.Accessible.description.indexOf("1 participant") >= 0)
		verify(lobby.Accessible.description.indexOf("1 person speaking") >= 0)

		lobby.focusRow()
		keyClick(Qt.Key_Left)
		tryCompare(participant, "visible", false)
		compare(participant.height, 0)
		verify(disclosure.Accessible.name.indexOf("Expand") >= 0)
		verify(lobby.Accessible.description.indexOf("Collapsed") >= 0)
		verify(countBadge.visible)
		compare(commands.selectScopeFromRailCount, 0)
		compare(commands.joinVoiceCount, 0)

		keyClick(Qt.Key_Right)
		tryCompare(participant, "visible", true)
		verify(participant.height > 0)
		verify(disclosure.Accessible.name.indexOf("Collapse") >= 0)
	}

	function test_room_filter_is_keyboard_accessible_and_keeps_stable_rows() {
		const field = findChild(loader.item, "navigationFilterField")
		const clear = findChild(loader.item, "navigationFilterClear")
		const navigationList = findChild(loader.item, "navigationRooms")
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		const lobbyParticipant = findChild(loader.item, "navigationParticipantSemantic_42")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const gamesListener = findChild(loader.item, "navigationParticipantSemantic_listener:2:42")
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		verify(field !== null && clear !== null && navigationList !== null)
		verify(lobby !== null && lobbyParticipant !== null && games !== null
			&& gamesListener !== null && activity !== null)
		compare(navigationList.count, 6)

		field.forceActiveFocus()
		tryCompare(field, "activeFocus", true)
		loader.item.setNavigationFilter("Games")
		tryCompare(field, "text", "Games")
		tryCompare(games, "visible", true)
		tryCompare(gamesListener, "visible", true)
		tryCompare(lobby, "visible", false)
		tryCompare(lobbyParticipant, "visible", false)
		tryCompare(activity, "visible", false)
		compare(navigationList.count, 6)
		compare(field.Accessible.role, Accessible.EditableText)

		keyClick(Qt.Key_Down)
		tryCompare(games, "activeFocus", true)
		compare(commands.selectScopeFromRailCount, 0)
		field.forceActiveFocus()
		keyClick(Qt.Key_Escape)
		tryCompare(field, "text", "")
		tryCompare(lobby, "visible", true)
		tryCompare(activity, "visible", true)
		compare(navigationList.count, 6)
	}

	function test_filter_and_collapse_never_hide_current_user_or_voice_selection() {
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		const participant = findChild(loader.item, "navigationParticipantSemantic_42")
		const games = findChild(loader.item, "navigationRoom_room-games")
		verify(lobby !== null && participant !== null && games !== null)

		loader.item.setRoomExpanded("channel:1", false)
		loader.item.setNavigationFilter("no matching room")
		selection.selectedUserSession = "42"
		tryCompare(lobby, "visible", true)
		tryCompare(participant, "visible", true)
		verify(participant.height > 0)

		selection.selectedUserSession = undefined
		selection.selectedVoiceChannelId = 2
		tryCompare(games, "visible", true)
		selection.scopeToken = "channel:1"
		tryCompare(lobby, "visible", true)
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

	function test_page_keys_move_a_viewport_without_activating_rows() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const lobby = findChild(loader.item, "navigationRoom_room-lobby")
		verify(navigationList !== null && lobby !== null)
		lobby.focusRow()
		keyClick(Qt.Key_PageDown)
		tryCompare(navigationList, "currentIndex", Math.min(navigationList.count - 1,
			loader.item.navigationPageStep()))
		tryVerify(function() { return navigationList.currentItem.activeFocus })
		keyClick(Qt.Key_PageUp)
		tryCompare(navigationList, "currentIndex", 0)
		compare(commands.selectScopeFromRailCount, 0)
		compare(commands.selectParticipantCount, 0)
		compare(commands.joinVoiceCount, 0)
		compare(commands.directMessageCount, 0)
		compare(committedSpy.count, 0)
	}

	function test_external_scope_transition_reveals_selection_without_stealing_focus() {
		const navigationList = findChild(loader.item, "navigationRooms")
		const profile = findChild(loader.item, "selfIdentityButton")
		verify(navigationList !== null && profile !== null)
		profile.forceActiveFocus()
		tryVerify(function() { return profile.activeFocus })
		selection.scopeToken = "-1:42"
		tryCompare(navigationList, "currentIndex", 5)
		verify(profile.activeFocus)
		compare(commands.selectScopeFromRailCount, 0)
		compare(commands.selectParticipantCount, 0)
		compare(committedSpy.count, 0)
	}

	function test_late_selected_tool_insertion_reveals_current_scope_without_stealing_focus() {
		const isolatedRows = createTemporaryObject(
			isolatedNavigationRowsComponent, testCase)
		const isolatedSelection = createTemporaryObject(
			isolatedSelectionComponent, testCase)
		const isolatedLoader = createTemporaryObject(
			isolatedRailLoaderComponent, testCase)
		verify(isolatedRows !== null && isolatedSelection !== null
			&& isolatedLoader !== null)

		for (let index = 0; index < 10; ++index) {
			isolatedRows.append({
				"stableId": "late-room-" + index,
				"scopeToken": "late:" + index,
				"title": "Late room " + index,
				"subtitle": "Available before selection",
				"kind": "text", "sectionKind": "text", "selected": false,
				"depth": 0, "unreadCount": 0, "status": "",
				"payload": { "rowKind": "room", "source": { "actions": [] } }
			})
		}
		isolatedSelection.scopeToken = "text:late-tool"
		isolatedLoader.setSource("qrc:/qml-shell/NavigationRail.qml", {
			"navigationModel": isolatedRows,
			"selectionState": isolatedSelection,
			"uiCommands": commands,
			"clientSession": session,
			"stonksEnabled": true,
			"commitOnSelection": true
		})
		tryVerify(function() { return isolatedLoader.item !== null })
		const navigationList = findChild(isolatedLoader.item, "navigationRooms")
		const profile = findChild(isolatedLoader.item, "selfIdentityButton")
		verify(navigationList !== null && profile !== null)
		profile.forceActiveFocus()
		tryVerify(function() { return profile.activeFocus })
		// Let the first reveal attempt finish before the selected model row exists.
		wait(0)

		const selectedIndex = isolatedRows.count
		isolatedRows.append({
			"stableId": "late-selected-tool", "scopeToken": "text:late-tool",
			"title": "#TestStuff", "subtitle": "Debug text room",
			"kind": "text", "sectionKind": "tool", "selected": false,
			"depth": 0, "unreadCount": 0, "status": "",
			"payload": { "rowKind": "room", "source": { "actions": [] } }
		})

		tryCompare(navigationList, "currentIndex", selectedIndex)
		navigationList.forceLayout()
		tryVerify(function() {
			const selectedRow = navigationList.itemAtIndex(selectedIndex)
			if (!selectedRow)
				return false
			const origin = selectedRow.mapToItem(navigationList, 0, 0)
			return origin.y >= -0.5
				&& origin.y + selectedRow.height <= navigationList.height + 0.5
		})
		verify(profile.activeFocus)
		compare(commands.selectScopeFromRailCount, 0)
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
		const roomSurface = findChild(loader.item, "navigationRoomSurface_room-lobby")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const alice = findChild(loader.item, "navigationParticipant_42")
		const listener = findChild(loader.item, "navigationParticipant_listener:2:42")
		const details = findChild(loader.item, "navigationRoomDetails_room-lobby")
		verify(navigationList !== null && room !== null && roomSurface !== null && games !== null)
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
		compare(details.text, "You are here · You are sharing in this room")
		verify(String(roomSurface.color) !== String(Theme.selected))
		verify(String(roomSurface.border.color) !== String(Theme.accent))
	}

	function test_only_open_conversation_uses_selected_purple() {
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		const activitySurface = findChild(loader.item, "navigationRoomSurface_room-activity")
		const accent = findChild(loader.item, "navigationRoomSelectionAccent_room-activity")
		const gamesDetails = findChild(loader.item, "navigationRoomDetails_room-games")
		verify(activity !== null && activitySurface !== null
			&& accent !== null && gamesDetails !== null)
		verify(!gamesDetails.visible)
		navigationRows.setProperty(4, "selected", true)
		tryCompare(activitySurface, "color", Theme.selected)
		compare(activitySurface.border.width, 0)
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
		const roomSurface = findChild(loader.item, "navigationRoomSurface_room-lobby")
		const roomMouse = findChild(loader.item, "navigationRoomMouse_room-lobby")
		var participant = null
		tryVerify(function() {
			participant = findChild(loader.item, "navigationParticipant_42")
			return participant !== null
		})
		const participantMouse = findChild(loader.item, "navigationParticipantMouse_42")
		verify(room !== null && roomSurface !== null
			&& roomMouse !== null && participantMouse !== null)

		navigationRows.setProperty(0, "selected", true)
		tryCompare(roomSurface, "color", Theme.selected)
		mousePress(roomMouse, roomMouse.width / 2, roomMouse.height / 2, Qt.LeftButton)
		tryCompare(roomSurface, "color", Theme.accentSubtle)
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
		const navigationList = findChild(loader.item, "navigationRooms")
		verify(navigationList !== null)
		compare(navigationList.section.property, "")
		const voiceSection = findChild(loader.item, "navigationSection_voice")
		const toolSection = findChild(loader.item, "navigationSection_tool")
		const directSection = findChild(loader.item, "navigationSection_direct")
		verify(voiceSection !== null)
		verify(toolSection !== null)
		verify(directSection !== null)
		compare(voiceSection.Accessible.name, "VOICE ROOMS · 2")
		compare(toolSection.Accessible.name, "TOOLS · 1")
		compare(directSection.Accessible.name, "DIRECT MESSAGES · 1")
		for (const kind of [ "voice", "tool", "direct" ]) {
			const visualLabel = findChild(loader.item, "navigationSectionLabel_" + kind)
			const toggleIcon = findChild(loader.item, "navigationSectionToggle_" + kind)
			const toggleMouse = findChild(loader.item, "navigationSectionToggleMouse_" + kind)
			verify(visualLabel !== null)
			verify(toggleIcon !== null && toggleMouse !== null && toggleMouse.enabled)
			verify(visualLabel.Accessible.ignored)
			verify(String(visualLabel.text).indexOf(" · ") < 0)
			compare(String(visualLabel.color), String(Theme.withAlpha(Theme.accent, 0.76)))
		}
		compare(findChild(loader.item, "navigationSectionLabel_voice").text, "VOICE ROOMS")
		compare(findChild(loader.item, "navigationSectionLabel_tool").text, "TOOLS")

		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const roomSurface = findChild(loader.item, "navigationRoomSurface_room-lobby")
		const activity = findChild(loader.item, "navigationRoom_room-activity")
		const activitySurface = findChild(loader.item, "navigationRoomSurface_room-activity")
		const direct = findChild(loader.item, "navigationRoom_room-direct")
		const directSurface = findChild(loader.item, "navigationRoomSurface_room-direct")
		const participant = findChild(loader.item, "navigationParticipant_42")
		const semanticParticipant = findChild(loader.item, "navigationParticipantSemantic_42")
		verify(room !== null && roomSurface !== null
			&& activity !== null && activitySurface !== null
			&& direct !== null && directSurface !== null
			&& participant !== null && semanticParticipant !== null)
		for (const sectionGeometry of [
			{ "section": voiceSection, "room": room, "surface": roomSurface },
			{ "section": toolSection, "room": activity, "surface": activitySurface },
			{ "section": directSection, "room": direct, "surface": directSurface }
		]) {
			const headerBottom = sectionGeometry.section.mapToItem(
				sectionGeometry.room, 0, sectionGeometry.section.height).y
			const surfaceTop = sectionGeometry.surface.mapToItem(
				sectionGeometry.room, 0, 0).y
			compare(sectionGeometry.room.sectionContentGap, Theme.space1)
			compare(sectionGeometry.room.sectionContentTopOffset,
				sectionGeometry.room.sectionHeaderHeight + Theme.space1)
			compare(surfaceTop - headerBottom, Theme.space1)
		}
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
		verify(room.Accessible.description.indexOf("You are sharing in this room") >= 0)
		verify(room.Accessible.description.indexOf("1920x1080 @ 30 fps") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Talking") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Muted") >= 0)
		verify(semanticParticipant.Accessible.description.indexOf("Local volume -6 dB") >= 0)
		room.forceActiveFocus()
		tryCompare(room, "activeFocus", true)
		compare(roomSurface.border.width, Theme.focusRingWidth)
		compare(roomSurface.border.color, Theme.focus)
		semanticParticipant.forceActiveFocus()
		tryCompare(semanticParticipant, "activeFocus", true)
		compare(participant.border.width, Theme.focusRingWidth)
		compare(participant.border.color, Theme.focus)
	}

	function test_voice_and_direct_sections_collapse_through_shared_session_state() {
		const rail = loader.item
		const navigationList = findChild(rail, "navigationRooms")
		const voiceToggle = findChild(rail, "navigationSectionToggleMouse_voice")
		verify(navigationList !== null && voiceToggle !== null)

		mouseClick(voiceToggle, voiceToggle.width / 2, voiceToggle.height / 2, Qt.LeftButton)
		tryVerify(function() {
			return session.collapsedNavigationSections.indexOf("voice") >= 0
				&& !rail.sectionExpandedFor("voice")
		})
		tryVerify(function() {
			return !rail.navigationIndexHasContent(0) && rail.navigationIndexIsVisible(0)
				&& !rail.navigationIndexIsVisible(1) && !rail.navigationIndexIsVisible(2)
		})
		const lobby = navigationList.itemAtIndex(0)
		verify(lobby !== null)
		compare(lobby.height, lobby.sectionHeaderHeight)
		compare(lobby.Accessible.role, Accessible.Button)
		compare(lobby.Accessible.name, "VOICE ROOMS")
		verify(lobby.Accessible.description.indexOf("2 voice rooms") >= 0)

		selection.scopeToken = "channel:2"
		tryVerify(function() {
			return rail.navigationIndexHasContent(2) && rail.navigationIndexIsVisible(2)
		})
		verify(!rail.sectionExpandedFor("voice"))
		selection.scopeToken = ""
		rail.setSectionExpanded("voice", true)
		tryVerify(function() {
			return rail.navigationIndexHasContent(0) && rail.navigationIndexHasContent(2)
		})

		rail.setSectionExpanded("direct", false)
		tryVerify(function() {
			return session.collapsedNavigationSections.indexOf("direct") >= 0
				&& !rail.sectionExpandedFor("direct")
		})
		navigationList.positionViewAtIndex(5, ListView.Contain)
		navigationList.forceLayout()
		let direct = null
		tryVerify(function() {
			direct = findChild(rail, "navigationRoom_room-direct")
			return direct !== null && !direct.sectionContentVisible
				&& direct.height === direct.sectionHeaderHeight
		})
		compare(direct.Accessible.name, "DIRECT MESSAGES")
	}

	function test_voice_selection_id_does_not_reveal_colliding_text_room() {
		const rail = loader.item
		const navigationList = findChild(rail, "navigationRooms")
		verify(navigationList !== null)
		navigationRows.insert(4, {
			"stableId": "room-text-first", "scopeToken": "3:8", "title": "#first",
			"subtitle": "Persistent text room", "kind": "text", "sectionKind": "text",
			"selected": false, "depth": 0, "unreadCount": 0, "status": "",
			"payload": { "rowKind": "room", "source": { "actions": [] } }
		})
		navigationRows.insert(5, {
			"stableId": "room-text-collision", "scopeToken": "3:2", "title": "#collision",
			"subtitle": "Persistent text room", "kind": "text", "sectionKind": "text",
			"selected": false, "depth": 0, "unreadCount": 0, "status": "",
			"payload": { "rowKind": "room", "source": { "actions": [] } }
		})
		try {
			rail.setSectionExpanded("text", false)
			selection.selectedVoiceChannelId = 2
			selection.scopeToken = "channel:2"
			navigationList.forceLayout()
			wait(0)
			verify(rail.navigationIndexIsVisible(4))
			verify(!rail.navigationIndexHasContent(4))
			verify(!rail.navigationIndexIsVisible(5))
			verify(!rail.navigationIndexHasContent(5))
		} finally {
			selection.selectedVoiceChannelId = undefined
			selection.scopeToken = ""
			rail.setSectionExpanded("text", true)
			navigationRows.remove(5)
			navigationRows.remove(4)
		}
	}

	function test_tools_section_collapses_without_losing_selected_tool_context() {
		const rail = loader.item
		const navigationList = findChild(rail, "navigationRooms")
		verify(navigationList !== null)
		navigationRows.insert(5, {
			"stableId": "room-test-stuff", "scopeToken": "text:9", "title": "#TestStuff",
			"subtitle": "Debug text room", "kind": "text", "sectionKind": "tool",
			"selected": false, "depth": 0, "unreadCount": 0, "status": "",
			"payload": { "rowKind": "room", "source": { "actions": [] } }
		})
		try {
			navigationList.forceLayout()
			navigationList.positionViewAtIndex(4, ListView.Contain)
			wait(0)
			let activity = null
			let testStuff = null
			let toggleMouse = null
			let toggleIcon = null
			tryVerify(function() {
				activity = findChild(rail, "navigationRoom_room-activity")
				testStuff = findChild(rail, "navigationRoom_room-test-stuff")
				toggleMouse = findChild(rail, "navigationSectionToggleMouse_tool")
				toggleIcon = findChild(rail, "navigationSectionToggle_tool")
				return activity !== null && testStuff !== null
					&& toggleMouse !== null && toggleIcon !== null
			})

			verify(toggleMouse.enabled && toggleMouse.width > 0 && toggleMouse.height > 0)
			mouseClick(activity, activity.width - 15, activity.sectionHeaderHeight / 2, Qt.LeftButton)
			tryCompare(rail, "toolsExpanded", false)
			tryVerify(function() {
				return !activity.sectionContentVisible && activity.height === activity.sectionHeaderHeight
					&& !testStuff.sectionContentVisible && testStuff.height === 0
			})
			compare(activity.Accessible.role, Accessible.Button)
			compare(activity.Accessible.name, "TOOLS")
			verify(activity.Accessible.description.indexOf("2 tools") >= 0)
			tryCompare(toggleIcon, "rotation", 0)
			rail.setNavigationFilter("#teststuff")
			tryVerify(function() { return testStuff.sectionContentVisible && testStuff.height > 0 })
			rail.setNavigationFilter("")
			tryVerify(function() { return !testStuff.sectionContentVisible && testStuff.height === 0 })

			navigationList.currentIndex = 4
			navigationList.forceActiveFocus(Qt.TabFocusReason)
			keyClick(Qt.Key_Right)
			tryCompare(rail, "toolsExpanded", true)
			tryVerify(function() { return activity.sectionContentVisible && testStuff.sectionContentVisible })
			tryCompare(toggleIcon, "rotation", 90)
			keyClick(Qt.Key_Left)
			tryCompare(rail, "toolsExpanded", false)

			selection.scopeToken = "text:9"
			tryCompare(navigationList, "currentIndex", 5)
			tryVerify(function() { return testStuff.sectionContentVisible && testStuff.height > 0 })
			verify(!rail.toolsExpanded)
			compare(commands.selectScopeFromRailCount, 0)
		} finally {
			selection.scopeToken = ""
			rail.setToolsExpanded(true)
			navigationRows.remove(5)
			navigationList.forceLayout()
		}
	}

	function test_slightly_scrolled_top_section_header_keeps_its_full_surface() {
		const isolatedRows = createTemporaryObject(
			isolatedNavigationRowsComponent, testCase)
		const isolatedSelection = createTemporaryObject(
			isolatedSelectionComponent, testCase)
		const isolatedLoader = createTemporaryObject(
			isolatedRailLoaderComponent, testCase)
		verify(isolatedRows !== null && isolatedSelection !== null
			&& isolatedLoader !== null)
		for (let index = 0; index < 20; ++index) {
			isolatedRows.append({
				"stableId": "header-scroll-" + index,
				"scopeToken": "header-scroll:" + index,
				"title": "Header scroll room " + index,
				"subtitle": "", "kind": "text", "sectionKind": "text",
				"selected": false, "depth": 0, "unreadCount": 0, "status": "",
				"payload": { "rowKind": "room", "source": { "actions": [] } }
			})
		}
		isolatedLoader.setSource("qrc:/qml-shell/NavigationRail.qml", {
			"navigationModel": isolatedRows,
			"selectionState": isolatedSelection,
			"uiCommands": commands,
			"clientSession": session,
			"stonksEnabled": true,
			"commitOnSelection": true
		})
		tryVerify(function() { return isolatedLoader.item !== null })
		const rail = isolatedLoader.item
		const navigationList = findChild(rail, "navigationRooms")
		verify(navigationList !== null)
		navigationList.currentIndex = 0
		navigationList.positionViewAtBeginning()
		navigationList.forceLayout()
		wait(0)
		const section = findChild(rail, "navigationSection_text")
		const firstRoom = navigationList.itemAtIndex(0)
		const firstRoomSurface = findChild(rail, "navigationRoomSurface_header-scroll-0")
		verify(section !== null && firstRoom !== null && firstRoomSurface !== null)
		compare(firstRoom.sectionContentGap, Theme.space1)
		compare(firstRoomSurface.mapToItem(firstRoom, 0, 0).y,
			firstRoom.sectionHeaderHeight + Theme.space1)
		compare(firstRoomSurface.mapToItem(firstRoom, 0, 0).y
			- section.mapToItem(firstRoom, 0, section.height).y, Theme.space1)
		navigationList.contentY = 8
		tryVerify(function() { return Math.abs(navigationList.contentY - 8) <= 0.5 })
		const origin = section.mapToItem(navigationList, 0, 0)
		verify(Math.abs(origin.y) <= 0.5,
			"A slightly scrolled heading must be pinned to the viewport instead of clipping")
		verify(Math.abs(firstRoom.sectionHeaderViewportOffset - navigationList.contentY) <= 0.5)
	}

	function test_section_geometry_recovers_after_hidden_layout_change() {
		const rail = loader.item
		const navigationList = findChild(rail, "navigationRooms")
		const voiceSection = findChild(rail, "navigationSection_voice")
		const toolSection = findChild(rail, "navigationSection_tool")
		verify(navigationList !== null && voiceSection !== null && toolSection !== null)

		rail.visible = false
		testCase.width = 420
		testCase.height = 520
		rail.visible = true
		tryVerify(function() {
			const voiceOrigin = voiceSection.mapToItem(navigationList, 0, 0)
			const toolOrigin = toolSection.mapToItem(navigationList, 0, 0)
			return toolOrigin.y >= voiceOrigin.y + voiceSection.height
		}, 5000, "Section headings remained stacked after the rail was restored")
		testCase.width = 340
		testCase.height = 620
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
			room.sectionContentTopOffset + room.roomRowHeight / 2, Qt.RightButton)
		compare(scopeMenuSpy.count, 1)
		compare(scopeMenuSpy.signalArguments[0][0], "channel:1")
		compare(commands.selectedScope, "channel:2")
		compare(commands.selectScopeCount, 0)
	}

	function test_room_join_share_and_overflow_expose_live_share_state() {
		const room = findChild(loader.item, "navigationRoom_room-lobby")
		const games = findChild(loader.item, "navigationRoom_room-games")
		const joined = findChild(loader.item, "navigationRoomJoined_room-lobby")
		const liveBadge = findChild(loader.item, "navigationRoomShareBadge_room-lobby")
		const roomBadge = findChild(loader.item, "navigationRoomBadge_room-lobby")
		const share = findChild(loader.item, "navigationRoomShare_room-lobby")
		const actions = findChild(loader.item, "navigationRoomActions_room-lobby")
		const join = findChild(loader.item, "navigationRoomJoin_room-games")
		const profile = findChild(loader.item, "selfIdentityButton")
		verify(profile !== null)
		profile.forceActiveFocus()
		tryCompare(profile, "activeFocus", true)
		mouseMove(loader.item, loader.item.width - 2, 2)
		wait(0)
		verify(joined !== null && !joined.visible)
		verify(liveBadge !== null && liveBadge.visible)
		verify(roomBadge !== null && !roomBadge.visible)
		verify(share !== null && share.visible && share.enabled)
		verify(room !== null && games !== null && actions !== null && join !== null)
		verify(actions.visible)
		verify(join.visible)
		verify(join.contentItem.Accessible.ignored)
		compare(join.Accessible.name, "Join")
		verify(share.contentItem.Accessible.ignored)
		compare(share.Accessible.name, "Manage share")
		compare(share.Accessible.description, "You are sharing in this room")
		const detail = findChild(loader.item, "navigationRoomDetails_room-lobby")
		verify(detail !== null && detail.visible)
		compare(detail.text, "You are here · You are sharing in this room")
		verify(room.Accessible.description.indexOf("Tester") >= 0)
		verify(room.Accessible.description.indexOf("1920x1080 @ 30 fps") >= 0)
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
		const profile = findChild(loader.item, "selfIdentityButton")
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
		const avatarEffect = findChild(loader.item, "navigationParticipantAvatarImageEffect_42")
		const avatarMask = findChild(loader.item, "navigationParticipantAvatarMask_42")
		const avatarHalo = findChild(loader.item, "navigationParticipantAvatarHalo_42")
		const avatarRing = findChild(loader.item, "navigationParticipantAvatarRing_42")
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
		verify(avatarImage !== null && avatarEffect !== null && avatarMask !== null)
		verify(avatarHalo !== null && avatarRing !== null)
		compare(avatar.radius, avatar.width / 2)
		compare(avatarMask.radius, Math.min(avatarMask.width, avatarMask.height) / 2)
		verify(avatarMask.layer.enabled)
		verify(avatarMask.antialiasing)
		compare(avatar.highlighted, true)
		tryCompare(avatarHalo, "opacity", 1)
		compare(avatarRing.border.width, 2)
		compare(String(avatarRing.border.color), String(Theme.success))
		compare(String(avatarImage.source),
			"image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=7")
		compare(avatarImage.sourceSize.width, 52)
		compare(avatarImage.sourceSize.height, 52)
		tryCompare(avatar, "imageReady", true)
		tryCompare(avatarEffect, "visible", true)
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

	function test_classic_user_icons_replace_avatars_and_talk_dots_live() {
		const avatar = findChild(loader.item, "navigationParticipantAvatar_42")
		const avatarImage = findChild(loader.item, "navigationParticipantAvatarImage_42")
		const avatarEffect = findChild(loader.item, "navigationParticipantAvatarImageEffect_42")
		const avatarFallback = findChild(loader.item, "navigationParticipantAvatarFallback_42")
		const talk = findChild(loader.item, "navigationParticipantTalk_42")
		const classic = findChild(loader.item, "navigationParticipantClassicIcon_42")
		const listenerClassic = findChild(loader.item,
			"navigationParticipantClassicIcon_listener:2:42")
		verify(avatar !== null && avatarImage !== null && avatarEffect !== null)
		verify(avatarFallback !== null && talk !== null)
		verify(classic !== null && listenerClassic !== null)
		verify(!classic.visible)
		verify(talk.visible)

		loader.item.classicUserIcons = true
		tryCompare(classic, "visible", true)
		compare(String(classic.source), "qrc:/native/talking_on.svg")
		compare(String(listenerClassic.source), "qrc:/native/talking_off.svg")
		compare(avatar.avatarVisible, false)
		verify(!avatarEffect.visible)
		verify(!talk.visible)

		loader.item.classicUserIcons = false
		tryCompare(classic, "visible", false)
		compare(avatar.avatarVisible, true)
		tryCompare(talk, "visible", true)
	}

	function test_speaking_avatar_highlight_tracks_live_talk_state_and_tone() {
		const previousPayload = navigationRows.get(1).payload
		const previousStatus = navigationRows.get(1).status
		const avatar = findChild(loader.item, "navigationParticipantAvatar_42")
		const halo = findChild(loader.item, "navigationParticipantAvatarHalo_42")
		const ring = findChild(loader.item, "navigationParticipantAvatarRing_42")
		verify(avatar !== null && halo !== null && ring !== null)

		navigationRows.setProperty(1, "status", "passive")
		navigationRows.setProperty(1, "payload", {
			"rowKind": "participant", "parentScopeToken": "channel:1",
			"participantSession": "42", "entryKind": "user",
			"avatarUrl": "image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=7",
			"talking": false, "talkTone": "", "badges": [],
			"localVolume": { "db": 0, "visible": true },
			"statuses": [], "source": { "session": "42", "entryKind": "user", "actions": [] }
		})
		tryCompare(avatar, "highlighted", false)
		tryCompare(halo, "opacity", 0)
		compare(ring.border.width, 1)
		compare(String(ring.border.color), String(Theme.quietBorder))

		navigationRows.setProperty(1, "status", "talking")
		navigationRows.setProperty(1, "payload", {
			"rowKind": "participant", "parentScopeToken": "channel:1",
			"participantSession": "42", "entryKind": "user",
			"avatarUrl": "image://mumble/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef?g=7",
			"talking": true, "talkTone": "warning", "badges": ["Talking"],
			"localVolume": { "db": 0, "visible": true },
			"statuses": [{ "kind": "talking", "label": "Talking", "tone": "warning" }],
			"source": { "session": "42", "entryKind": "user", "actions": [] }
		})
		tryCompare(avatar, "highlighted", true)
		tryCompare(halo, "opacity", 1)
		compare(String(avatar.highlightColor), String(Theme.warning))
		compare(ring.border.width, 2)
		compare(String(ring.border.color), String(Theme.warning))

		navigationRows.setProperty(1, "payload", previousPayload)
		navigationRows.setProperty(1, "status", previousStatus)
		wait(0)
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
		const selfAvatarEffect = findChild(loader.item, "selfAvatarImageEffect")
		const selfAvatarFallback = findChild(loader.item, "selfAvatarFallback")
		const selfAvatarMask = findChild(loader.item, "selfAvatarMask")
		verify(selfAvatar !== null && selfAvatarImage !== null && selfAvatarEffect !== null)
		verify(selfAvatarFallback !== null && selfAvatarMask !== null)
		compare(selfAvatarFallback.Accessible.ignored, true)
		compare(String(selfAvatar.source),
			"image://mumble/abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789?g=8")
		compare(String(selfAvatarImage.source), String(selfAvatar.source))
		compare(selfAvatarImage.sourceSize.width, Theme.avatarMedium * 2)
		compare(selfAvatarImage.sourceSize.height, Theme.avatarMedium * 2)
		tryCompare(selfAvatar, "imageReady", true)
		tryCompare(selfAvatarEffect, "visible", true)
		compare(selfAvatar.radius, selfAvatar.width / 2)
		compare(selfAvatarMask.radius,
			Math.min(selfAvatarMask.width, selfAvatarMask.height) / 2)
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

	function test_profile_identity_requests_accessible_profile_menu() {
		const button = findChild(loader.item, "selfIdentityButton")
		verify(button !== null)
		compare(button.Accessible.role, Accessible.Button)
		verify(button.Accessible.name.indexOf("Tester") >= 0)
		button.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(profileMenuSpy.count, 1)
	}

	function test_settings_is_a_direct_pointer_keyboard_and_accessibility_action() {
		const button = findChild(loader.item, "settingsButton")
		verify(button !== null)
		compare(button.iconName, "settings")
		compare(button.Accessible.role, Accessible.Button)
		compare(button.Accessible.name, "Settings")
		verify(button.Accessible.description.indexOf("Audio") >= 0)
		verify(button.Accessible.description.indexOf("plugins") >= 0)

		mouseClick(button)
		compare(settingsSpy.count, 1)
		compare(profileMenuSpy.count, 0)

		button.forceActiveFocus()
		tryCompare(button, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(settingsSpy.count, 2)
		compare(profileMenuSpy.count, 0)

		loader.item.settingsEnabled = false
		tryCompare(button, "enabled", false)
		mouseClick(button)
		compare(settingsSpy.count, 2)
	}

	function test_stonks_portfolio_is_a_direct_profile_card_action() {
		const button = findChild(loader.item, "stonksPortfolioButton")
		const divider = findChild(loader.item, "selfActionDivider")
		const deafen = findChild(loader.item, "selfDeafenButton")
		const settings = findChild(loader.item, "settingsButton")
		verify(button !== null)
		verify(divider !== null && deafen !== null && settings !== null)
		compare(button.visible, true)
		compare(divider.visible, true)
		const dividerPosition = divider.mapToItem(loader.item, 0, 0)
		verify(dividerPosition.x > deafen.mapToItem(loader.item, 0, 0).x)
		verify(dividerPosition.x < settings.mapToItem(loader.item, 0, 0).x)
		compare(button.iconName, "activity")
		compare(button.Accessible.role, Accessible.Button)
		compare(button.Accessible.name, "Open Stonks portfolio")
		verify(button.Accessible.description.indexOf("leaderboard") >= 0)

		mouseClick(button)
		compare(stonksSpy.count, 1)
		compare(settingsSpy.count, 0)
		compare(profileMenuSpy.count, 0)

		button.forceActiveFocus()
		tryCompare(button, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(stonksSpy.count, 2)

		loader.item.stonksEnabled = false
		tryCompare(button, "visible", false)
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
		const actionGroup = findChild(loader.item, "selfActionGroup")
		const muteButton = findChild(loader.item, "selfMuteButton")
		const deafenButton = findChild(loader.item, "selfDeafenButton")
		const settingsButton = findChild(loader.item, "settingsButton")
		const stonksButton = findChild(loader.item, "stonksPortfolioButton")
		const profileButton = findChild(loader.item, "selfIdentityButton")
		verify(dock !== null && avatar !== null && presence !== null && actionGroup !== null)
		verify(nameLabel !== null && statusLabel !== null)
		verify(muteButton !== null && deafenButton !== null && stonksButton !== null && settingsButton !== null
			&& profileButton !== null)
		compare(dock.Accessible.role, Accessible.Grouping)
		verify(dock.Accessible.name.indexOf("Tester") >= 0)
		verify(dock.Accessible.description.indexOf("Online") >= 0)
		verify(nameLabel.Accessible.ignored)
		verify(statusLabel.Accessible.ignored)
		compare(avatar.width, Theme.avatarMedium)
		compare(avatar.radius, avatar.width / 2,
			"The profile avatar should use the same circular silhouette as channel avatars")
		compare(actionGroup.spacing, Theme.space1)
		compare(String(statusLabel.color), String(Theme.success))
		compare(String(presence.color), String(Theme.success))
		tryCompare(dock, "color", Theme.selfCardHover)

		profileButton.forceActiveFocus()
		tryCompare(profileButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(muteButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(deafenButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(stonksButton, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(settingsButton, "activeFocus", true)
		keyClick(Qt.Key_Backtab)
		tryCompare(stonksButton, "activeFocus", true)
		keyClick(Qt.Key_Backtab)
		tryCompare(deafenButton, "activeFocus", true)

		session.selfMuted = true
		tryCompare(presence, "color", Theme.danger)
		tryCompare(statusLabel, "color", Theme.danger)
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
			roomItem.sectionContentTopOffset + roomItem.roomRowHeight / 2, Qt.LeftButton)
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
			room.sectionContentTopOffset + room.roomRowHeight / 2)
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
		const startY = source.sectionContentTopOffset + source.roomRowHeight / 2
		const target = source.mapFromItem(targetRoom, targetRoom.width / 2,
			targetRoom.sectionContentTopOffset + targetRoom.roomRowHeight / 2)

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
