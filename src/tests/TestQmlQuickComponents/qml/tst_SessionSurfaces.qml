import QtQuick
import QtTest
import Mumble.Theme 1.0
import "../../../mumble/qml-shell" as Shell

TestCase {
    id: testCase
    name: "SessionSurfaces"
    when: windowShown
    width: 720
    height: 520

    QtObject {
        id: session
        property string connectionState: "disconnected"
        property string connectionTone: "danger"
        property string connectionLabel: "Disconnected"
        property string selfStatusLabel: "Offline"
        property string connectionDetail: "Open the server browser to reconnect."
        property int connectionRetryRemainingMs: 0
        property bool canConnect: true
        property bool canCancel: false

        property bool hasMotd: true
        property bool motdExpanded: false
        property bool motdDismissed: false
        property bool motdChanged: true
        property string motdHtml: "<p><b>Welcome</b> to the test server.</p>"
		property var motdSegments: [
			{ "text": "Welcome", "bold": true },
			{ "text": " to the test server. " },
			{ "text": "Open", "href": "https://example.com/welcome" }
		]
        property string motdSummary: "Welcome to the test server."
        property string motdSignature: "v1:42:abcd"
        property var motdActions: [
            { "id": "motd.show", "label": "Expand", "enabled": true,
              "payload": { "signature": "v1:42:abcd" } },
            { "id": "motd.dismiss", "label": "Dismiss", "enabled": true,
              "payload": { "signature": "v1:42:abcd" } }
        ]

		property bool sharedAvailable: true
		property bool sharedHost: true
		property bool sharedJoined: true
		property bool active: true
		property string sharedTitle: "Release watch session"
		property int sharedParticipantCount: 2
		property var sharedParticipantSessions: ["7", "42"]
		property int sharedHostSession: 7
		property int joinCalls: 0
		property int reopenCalls: 0
		property int transferCalls: 0
		property int leaveCalls: 0
		property int endCalls: 0

		function joinShared() { joinCalls += 1 }
		function reopenSharedPlayer() { reopenCalls += 1 }
		function transferSharedHost(sessionId) { transferCalls += 1 }
		function leaveShared() { leaveCalls += 1 }
		function endShared() { endCalls += 1 }
    }

	ListModel {
		id: participants
		ListElement { stableId: "7"; title: "Host" }
		ListElement { stableId: "42"; title: "Guest" }
	}

    Loader {
        id: connectionLoader
        width: parent.width
        height: 100
        Component.onCompleted: setSource(
            Qt.resolvedUrl("../../../mumble/qml-shell/ConnectionBanner.qml"),
            { "session": session })
    }

    Loader {
        id: motdLoader
        anchors.top: connectionLoader.bottom
        anchors.topMargin: 12
        width: parent.width
        height: 320
        Component.onCompleted: setSource(
            Qt.resolvedUrl("../../../mumble/qml-shell/MotdPanel.qml"),
            { "session": session })
    }

	Shell.WatchTogetherBanner {
		id: watchTogether
		width: testCase.width
		height: Math.max(80, implicitHeight)
		session: session
		participantModel: participants
	}

	Component {
		id: updateBannerComponent
		Shell.UpdateBanner { }
	}

    SignalSpy {
        id: connectionActionSpy
        target: connectionLoader.item
        signalName: "actionRequested"
    }

    SignalSpy {
        id: motdActionSpy
        target: motdLoader.item
        signalName: "actionRequested"
    }

    SignalSpy {
        id: motdLinkSpy
        target: motdLoader.item
        signalName: "linkRequested"
    }

    function init() {
        tryVerify(function() { return connectionLoader.item !== null && motdLoader.item !== null })
        connectionActionSpy.clear()
        motdActionSpy.clear()
        motdLinkSpy.clear()
		testCase.width = 720
        session.connectionState = "disconnected"
        session.connectionTone = "danger"
		session.connectionDetail = "Open the server browser to reconnect."
        session.connectionRetryRemainingMs = 0
        session.canConnect = true
        session.canCancel = false
        session.motdExpanded = false
        session.motdDismissed = false
        session.motdChanged = true
		session.motdSummary = "Welcome to the test server."
        session.motdActions = [
            { "id": "motd.show", "label": "Expand", "enabled": true,
              "payload": { "signature": session.motdSignature } },
            { "id": "motd.dismiss", "label": "Dismiss", "enabled": true,
              "payload": { "signature": session.motdSignature } }
        ]
		session.sharedHost = true
		session.sharedJoined = true
		session.active = true
		session.sharedTitle = "Release watch session"
		session.sharedParticipantCount = 2
		session.endCalls = 0
		const endButton = findChild(watchTogether, "watchTogetherEndButton")
		if (endButton)
			endButton.text = "End"
    }

    function test_connection_state_and_keyboard_action() {
        const banner = connectionLoader.item
        compare(banner.connectionState, "disconnected")
        compare(banner.session, session)
        verify(banner.bannerVisible)
        compare(banner.Accessible.role, Accessible.AlertMessage)
        compare(banner.primaryActionId, "server.connect")
        verify(banner.focusPrimaryAction())
        keyClick(Qt.Key_Space)
        compare(connectionActionSpy.count, 1)
        compare(connectionActionSpy.signalArguments[0][0], "server.connect")

        session.connectionState = "retrying"
        session.connectionTone = "retry"
        session.connectionRetryRemainingMs = 2200
        session.canConnect = false
        session.canCancel = true
        compare(banner.retryRemainingSeconds, 3)
        compare(banner.primaryActionId, "server.disconnect")
        verify(banner.detail.indexOf("3s") >= 0)
		tryVerify(function() { return banner.retryRemainingSeconds <= 2 }, 1500)

        session.connectionState = "connected"
        verify(!banner.bannerVisible)
    }

    function test_motd_actions_focus_accessibility_and_probe() {
        const panel = motdLoader.item
        compare(panel.hasContent, true)
        compare(panel.session, session)
        verify(panel.surfaceVisible)
        verify(panel.contentVisible)
        compare(panel.Accessible.role, Accessible.Pane)
        verify(panel.focusPrimaryAction())
        keyClick(Qt.Key_Space)
        compare(motdActionSpy.count, 1)
        compare(motdActionSpy.signalArguments[0][0], "motd.show")
        compare(motdActionSpy.signalArguments[0][1].signature, session.motdSignature)

        motdActionSpy.clear()
        const result = panel.runProbe("dismiss", session.motdSignature)
        verify(result.handled)
        compare(result.action, "dismiss")
        compare(result.actionId, "motd.dismiss")
        verify(!result.visible)
        compare(result.dismissedSignature, session.motdSignature)
        compare(motdActionSpy.count, 1)
        compare(motdActionSpy.signalArguments[0][0], "motd.dismiss")
    }

	function test_motd_collapsed_summary_is_compact_clean_and_precedes_actions() {
		const panel = motdLoader.item
		compare(panel.activeFocusOnTab, false)
		session.motdSummary = "Welcome\uFFFC   friends"
		tryCompare(panel, "summary", "Welcome friends")
		compare(panel.Accessible.description, "Welcome friends")

		const summary = findChild(panel, "motdSummaryBody")
		const flow = findChild(panel, "motdActionFlow")
		verify(summary !== null, "collapsed MOTD summary was not created")
		verify(panel.contentVisible && !panel.expanded)
		verify(flow !== null)
		compare(summary.text, "Welcome friends")
		const summaryOrigin = summary.mapToItem(panel, 0, 0)
		const flowOrigin = flow.mapToItem(panel, 0, 0)
		verify(summaryOrigin.x < flowOrigin.x,
			"summary must appear before collapsed secondary actions")
		verify(panel.implicitHeight <= Theme.controlHeight + Theme.space3 * 2 + Theme.space2,
			"collapsed MOTD height " + panel.implicitHeight + " should stay near one control row")
	}

    function test_motd_dismissed_restore_surface() {
        session.motdDismissed = true
        session.motdActions = [
            { "id": "motd.restore", "label": "Show welcome message", "enabled": true }
        ]
        const panel = motdLoader.item
        compare(panel.hasContent, true)
        verify(panel.surfaceVisible)
        verify(!panel.contentVisible)
        tryVerify(function() { return findChild(panel, "motdAction_motd.restore") !== null })
        findChild(panel, "motdAction_motd.restore").forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(motdActionSpy.count, 1)
        compare(motdActionSpy.signalArguments[0][0], "motd.restore")
    }

    function test_motd_probe_rejects_unknown_action() {
        const result = motdLoader.item.runProbe("unknown", "")
        verify(!result.handled)
        compare(result.reason, "unknown-action")
        compare(motdActionSpy.count, 0)
    }

    function test_motd_links_are_allowlisted() {
        const panel = motdLoader.item
        compare(panel.safeExternalUrl("file:///C:/Windows/win.ini"), "")
        compare(panel.safeExternalUrl("javascript:alert(1)"), "")
        compare(panel.safeExternalUrl("https://example.com/welcome"), "https://example.com/welcome")
        panel.linkRequested(panel.safeExternalUrl("https://example.com/welcome"))
        compare(motdLinkSpy.count, 1)
        compare(motdLinkSpy.signalArguments[0][0].toString(), "https://example.com/welcome")
    }

	function test_motd_expanded_body_uses_structured_segments() {
		session.motdExpanded = true
		tryVerify(function() { return findChild(motdLoader.item, "motdStructuredBody") !== null })
		const text = findChild(motdLoader.item, "richMessageBodyText")
		verify(text !== null)
		verify(text.text.indexOf("Welcome") >= 0)
		verify(text.text.indexOf("<img") < 0)
		verify(text.text.indexOf("https://example.com/welcome") >= 0)
	}

	function test_watch_together_uses_the_supplied_participant_model() {
		const banner = watchTogether
		compare(banner.participantModel, participants)
		compare(banner.participantLabel("42"), "Guest")
		compare(banner.participantLabel("999"), "Session 999")
	}

	function test_connection_and_motd_actions_stay_bounded_at_420() {
		testCase.width = 420
		session.connectionDetail = "A deliberately long connection explanation that must wrap without pushing the primary action beyond the banner edge."
		session.motdActions = [
			{ "id": "motd.show", "label": "Expand the complete welcome message", "enabled": true },
			{ "id": "motd.dismiss", "label": "Hide this welcome message for now", "enabled": true }
		]

		const connection = connectionLoader.item
		const connectionAction = findChild(connection, "connectionBannerPrimaryAction")
		tryCompare(connection, "compactLayout", true)
		verify(connectionAction !== null)
		const actionOrigin = connectionAction.mapToItem(connection, 0, 0)
		verify(actionOrigin.x >= -0.5)
		verify(actionOrigin.x + connectionAction.width <= connection.width + 0.5)

		const panel = motdLoader.item
		const flow = findChild(panel, "motdActionFlow")
		tryCompare(panel, "compactLayout", true)
		tryVerify(function() { return panel.actionsWrapped })
		verify(flow !== null && flow.width <= panel.width)
		for (const id of [ "motd.show", "motd.dismiss" ]) {
			const button = findChild(panel, "motdAction_" + id)
			verify(button !== null)
			verify(button.x >= -0.5 && button.x + button.width <= flow.width + 0.5,
				id + " bounds " + button.x + "+" + button.width + " within " + flow.width)
			verify(button.Accessible.name.length > 0)
		}
	}

	function test_update_actions_wrap_at_420_and_relax_at_760() {
		const banner = createTemporaryObject(updateBannerComponent, testCase, {
			"width": 420,
			"state": {
				"visible": true,
				"title": "A client update is ready",
				"detail": "Install it now or review the complete release notes before continuing.",
				"actions": [
					{ "id": "update.install", "label": "Install update and restart Mumble" },
					{ "id": "update.notes", "label": "Read the complete release notes" }
				]
			}
		})
		verify(banner !== null)
		banner.height = banner.implicitHeight
		const baseState = banner.state
		banner.state = Object.assign({}, baseState, { "tone": "danger" })
		compare(banner.toneColor, Theme.danger)
		banner.state = Object.assign({}, baseState, { "tone": "warning" })
		compare(banner.toneColor, Theme.warning)
		banner.state = Object.assign({}, baseState, { "tone": "success" })
		compare(banner.toneColor, Theme.success)
		tryCompare(banner, "compactLayout", true)
		tryVerify(function() { return banner.actionsWrapped })
		const flow = findChild(banner, "updateBannerActionFlow")
		verify(flow !== null, "update action flow was not created")
		verify(flow.width <= banner.width,
			"update action flow width " + flow.width + " exceeded banner width " + banner.width)
		for (const id of [ "update.install", "update.notes" ]) {
			const button = findChild(banner, "updateAction_" + id)
			verify(button !== null)
			verify(button.x >= -0.5 && button.x + button.width <= flow.width + 0.5)
			verify(button.Accessible.name.length > 0)
		}

		banner.width = 760
		banner.height = banner.implicitHeight
		tryCompare(banner, "compactLayout", false)
		verify(banner.implicitHeight <= 180)
	}

	function test_watch_together_wraps_long_actions_at_narrow_width() {
		testCase.width = 420
		session.sharedTitle = "A deliberately long synchronized playback title that must elide without moving actions off screen"
		const banner = watchTogether
		banner.width = 420
		banner.height = Math.max(120, banner.implicitHeight)
		const flow = findChild(banner, "watchTogetherActionFlow")
		const transferButton = findChild(banner, "watchTogetherTransferButton")
		const endButton = findChild(banner, "watchTogetherEndButton")
		endButton.text = "End synchronized playback for every participant in this voice room"

		tryCompare(banner, "compactLayout", true)
		tryVerify(function() { return banner.actionsWrapped })
		verify(flow.width <= banner.width)
		for (const control of [ transferButton, endButton ]) {
			verify(control.width > 0 && control.height > 0)
			verify(control.Accessible.name.length > 0)
			verify(control.x >= -0.5 && control.x + control.width <= flow.width + 0.5,
				control.objectName + " bounds " + control.x + "+" + control.width + " within " + flow.width)
		}
		verify(banner.implicitHeight <= banner.height + 0.5)

		endButton.forceActiveFocus()
		keyClick(Qt.Key_Space)
		compare(session.endCalls, 1)
	}
}
