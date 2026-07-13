import QtQuick
import QtTest

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
        property string motdSummary: "Welcome to the test server."
        property string motdSignature: "v1:42:abcd"
        property var motdActions: [
            { "id": "motd.show", "label": "Expand", "enabled": true,
              "payload": { "signature": "v1:42:abcd" } },
            { "id": "motd.dismiss", "label": "Dismiss", "enabled": true,
              "payload": { "signature": "v1:42:abcd" } }
        ]
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

    function init() {
        tryVerify(function() { return connectionLoader.item !== null && motdLoader.item !== null })
        connectionActionSpy.clear()
        motdActionSpy.clear()
        session.connectionState = "disconnected"
        session.connectionTone = "danger"
        session.connectionRetryRemainingMs = 0
        session.canConnect = true
        session.canCancel = false
        session.motdExpanded = false
        session.motdDismissed = false
        session.motdChanged = true
        session.motdActions = [
            { "id": "motd.show", "label": "Expand", "enabled": true,
              "payload": { "signature": session.motdSignature } },
            { "id": "motd.dismiss", "label": "Dismiss", "enabled": true,
              "payload": { "signature": session.motdSignature } }
        ]
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
}
