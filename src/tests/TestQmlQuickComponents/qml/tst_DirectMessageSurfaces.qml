import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "DirectMessageSurfaces"
	when: windowShown
	width: 920
	height: 700
	visible: true

	ListModel {
		id: summaryRows
		dynamicRoles: true
		Component.onCompleted: {
			append({
				"stableId": "7", "title": "Kira Mockup",
				"subtitle": "I'll keep this as a focused direct-message thread.",
				"unreadCount": 2, "avatarUrl": "", "selected": false
			})
		}
	}

	ListModel {
		id: timelineRows
		dynamicRoles: true
		Component.onCompleted: {
			append({
				"stableId": "dm:1", "title": "Kira Mockup", "timestamp": "10:36",
				"bodySegments": [{ "kind": "text", "text": "Can you check the reconnect dialog?" }],
				"own": false, "avatarUrl": ""
			})
			append({
				"stableId": "dm:2", "title": "You", "timestamp": "10:37",
				"bodySegments": [{ "kind": "text", "text": "Yes. The compact window is native QML." }],
				"own": true, "avatarUrl": ""
			})
		}
	}

	QtObject {
		id: fakeController
		property var summaryModel: summaryRows
		property var timelineModel: timelineRows
		property bool available: true
		property string title: "Direct messages"
		property string description: "Direct messages can use persistent history or private in-memory mode."
		property int unreadTotal: 2
		property bool hasUnread: unreadTotal > 0
		property bool trayOpen: true
		property bool conversationOpen: true
		property string activeSessionId: "7"
		property string activeScopeToken: "-1:7"
		property string activeLabel: "Kira Mockup"
		property string activeSubtitle: "Automation direct-message probe"
		property string activeAvatarUrl: ""
		property int activeUnreadCount: 2
		property bool canSend: true
		property string mode: "private"
		property bool persistentHistoryAvailable: true
		property bool historyLoading: false
		property string historyError: ""
		property string emptyCopy: "Messages with Kira will appear here."
		property string draft: ""
		property bool windowDocked: false
		property bool windowMinimized: false
		property int openCalls: 0
		property int readCalls: 0
		property int sendCalls: 0
		property int closeCalls: 0
		property string lastOpenedSession: ""
		property string lastReadSession: ""
		property string sentMessage: ""

		function setTrayOpen(open) { trayOpen = open }
		function openConversation(sessionId) {
			lastOpenedSession = String(sessionId)
			openCalls += 1
			conversationOpen = true
		}
		function closeConversation() { closeCalls += 1 }
		function markRead(sessionId) {
			lastReadSession = String(sessionId || activeSessionId)
			readCalls += 1
		}
		function setMode(value) { mode = String(value) }
		function setDraft(value) { draft = String(value) }
		function clearDraft() { draft = "" }
		function sendDraft() {
			sentMessage = draft
			sendCalls += 1
		}
		function setWindowDocked(value) { windowDocked = !!value }
		function setWindowMinimized(value) { windowMinimized = !!value }
	}

	Component {
		id: trayLoaderComponent
		Loader { active: false }
	}

	Component {
		id: directWindowLoaderComponent
		Loader { active: false }
	}

	function init() {
		fakeController.trayOpen = true
		fakeController.conversationOpen = true
		fakeController.unreadTotal = 2
		fakeController.mode = "private"
		fakeController.draft = ""
		fakeController.windowDocked = false
		fakeController.windowMinimized = false
		fakeController.openCalls = 0
		fakeController.readCalls = 0
		fakeController.sendCalls = 0
		fakeController.closeCalls = 0
		fakeController.lastOpenedSession = ""
		fakeController.lastReadSession = ""
		fakeController.sentMessage = ""
	}

	function test_tray_exposes_late_product_summary_and_semantics() {
		const loader = createTemporaryObject(trayLoaderComponent, testCase)
		verify(loader)
		loader.setSource("qrc:/qml-shell/DirectMessageTray.qml", {
			"controller": fakeController, "width": 310, "height": 230
		})
		loader.active = true
		tryCompare(loader, "status", Loader.Ready)
		const tray = loader.item
		verify(tray)
		compare(tray.surfaceId, "directMessage.tray")
		compare(tray.Accessible.role, Accessible.Pane)
		verify(tray.Accessible.name.indexOf("Direct messages") >= 0)
		verify(tray.captureRect.width > 0)

		tryCompare(tray.controller.summaryModel, "count", 1)
		const row = findChild(tray, "directMessageTrayRow_7")
		verify(row)
		row.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(fakeController.openCalls, 1)
		compare(fakeController.lastOpenedSession, "7")

		const markRead = findChild(tray, "directMessageTrayMarkAllRead")
		verify(markRead)
		mouseClick(markRead)
		compare(fakeController.readCalls, 1)
		compare(fakeController.lastReadSession, "7")
	}

	function test_private_window_has_separate_composer_mode_and_controls() {
		const loader = createTemporaryObject(directWindowLoaderComponent, testCase)
		verify(loader)
		loader.setSource("qrc:/qml-shell/DirectMessageWindow.qml", {
			"controller": fakeController,
			"parentWindow": testCase.Window.window
		})
		loader.active = true
		tryCompare(loader, "status", Loader.Ready)
		const directWindow = loader.item
		verify(directWindow)
		tryCompare(directWindow, "visible", true)
		compare(directWindow.surfaceId, "directMessage.window")
		verify(directWindow.captureRect.width >= 320)

		const banner = findChild(directWindow.contentItem, "directMessageModeBanner")
		verify(banner)
		verify(banner.visible)
		const composer = findChild(directWindow.contentItem, "directMessageComposer")
		verify(composer)
		directWindow.requestActivate()
		tryCompare(directWindow, "active", true)
		composer.forceActiveFocus()
		tryCompare(composer, "activeFocus", true)
		composer.text = "A separate DM draft"
		compare(fakeController.draft, "A separate DM draft")
		keyClick(Qt.Key_Return)
		compare(fakeController.sendCalls, 1)
		compare(fakeController.sentMessage, "A separate DM draft")

		const modeToggle = findChild(directWindow.contentItem, "directMessageModeToggle")
		verify(modeToggle)
		mouseClick(modeToggle)
		compare(fakeController.mode, "history")

		const dockToggle = findChild(directWindow.contentItem, "directMessageDockToggle")
		verify(dockToggle)
		mouseClick(dockToggle)
		verify(fakeController.windowDocked)

		const closeButton = findChild(directWindow.contentItem, "directMessageWindowClose")
		verify(closeButton)
		mouseClick(closeButton)
		compare(fakeController.closeCalls, 1)
	}
}
