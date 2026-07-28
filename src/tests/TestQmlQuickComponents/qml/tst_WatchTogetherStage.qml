import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "WatchTogetherStage"
	when: windowShown
	visible: true
	width: 900
	height: 700

	QtObject {
		id: session
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property bool active: false
		property bool detached: false
		property bool playbackControllable: true
		property string sharedTitle: "Shared short"
		property string sharedAspect: "wide"
		property string sharedSessionId: ""
		property string sessionId: ""
		property string provider: "youtube"
	}

	Loader {
		id: stageLoader
		anchors.fill: parent
		Component.onCompleted: setSource(
			Qt.resolvedUrl("../../../mumble/qml-shell/WatchTogetherStage.qml"), {
				"session": session,
				"inlinePlayerComponentUrl": Qt.resolvedUrl("WatchTogetherStagePlayerFixture.qml"),
				"maximumStageHeight": 520
			})
	}

	function init() {
		tryVerify(function() { return stageLoader.item !== null })
		session.sharedAvailable = false
		session.sharedJoined = false
		session.sharedHost = false
		session.active = false
		session.detached = false
		session.playbackControllable = true
		session.sharedTitle = "Shared short"
		session.sharedAspect = "wide"
		session.sharedSessionId = ""
		session.sessionId = ""
		session.provider = "youtube"
		stageLoader.item.inlinePlayerComponentUrl =
			Qt.resolvedUrl("WatchTogetherStagePlayerFixture.qml")
		stageLoader.item.renderActive = true
		stageLoader.item.maximumStageHeight = 520
		wait(0)
	}

	function activateSharedPlayer(aspect) {
		session.sharedAspect = aspect || "wide"
		session.sharedSessionId = "room-session-uuid"
		session.sessionId = "room-session-uuid"
		session.sharedAvailable = true
		session.sharedJoined = true
		session.active = true
	}

	function test_stage_stays_unmounted_until_the_shared_session_owns_the_inline_player() {
		const stage = stageLoader.item
		const player = findChild(stage, "watchTogetherPlayerLoader")
		verify(player !== null)
		verify(!stage.visible)
		verify(!player.active)

		session.sharedAvailable = true
		session.sharedJoined = true
		session.active = true
		session.sharedSessionId = "room-session-uuid"
		session.sessionId = "chat-message-id"
		verify(!stage.visible)

		session.sessionId = session.sharedSessionId
		tryCompare(stage, "visible", true)
		tryCompare(player, "status", Loader.Ready)
		verify(player.item !== null)
		compare(player.item.session, session)
		compare(stage.surfaceId, "watchTogether.stage")
	}

	function test_short_aspect_is_forwarded_and_bounded_inside_the_room_surface() {
		const stage = stageLoader.item
		activateSharedPlayer("short")
		tryCompare(stage, "visible", true)
		const player = findChild(stage, "watchTogetherPlayerLoader")
		tryCompare(player, "status", Loader.Ready)

		compare(stage.normalizedAspect, "short")
		compare(player.item.aspect, "short")
		verify(stage.playerWidth <= stage.maximumShortWidth)
		verify(stage.implicitHeight <= stage.maximumStageHeight)
		verify(stage.playerWidth < stage.availablePlayerWidth)
	}

	function test_detached_or_closed_shared_player_releases_the_stage() {
		const stage = stageLoader.item
		activateSharedPlayer("wide")
		tryCompare(stage, "visible", true)

		session.detached = true
		tryCompare(stage, "visible", false)
		session.detached = false
		session.active = false
		verify(!stage.visible)
	}

	function test_component_failure_is_visible_without_ending_the_room_session() {
		const stage = stageLoader.item
		stage.inlinePlayerComponentUrl = Qt.resolvedUrl("MissingWatchTogetherPlayer.qml")
		activateSharedPlayer("wide")
		tryCompare(stage, "visible", true)
		const player = findChild(stage, "watchTogetherPlayerLoader")
		tryCompare(player, "status", Loader.Error)
		tryCompare(stage, "componentLoadFailed", true)
		const failure = findChild(stage, "watchTogetherComponentFailureSurface")
		const retry = findChild(stage, "watchTogetherComponentRetryButton")
		verify(failure !== null && failure.visible)
		verify(retry !== null && retry.visible)
		verify(session.sharedAvailable)
		verify(session.sharedJoined)
	}
}
