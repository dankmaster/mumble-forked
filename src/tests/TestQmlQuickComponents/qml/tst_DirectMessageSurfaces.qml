import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "DirectMessageSurfaces"
	when: windowShown
	width: 920
	height: 700
	visible: true
	readonly property string directMessageSource: String(directMessageQmlSource || "")

	function test_timeline_pooling_releases_resources_and_bounds_prefetch() {
		verify(directMessageSource.length > 1000)
		verify(/cacheBuffer:\s*Math\.max\(256,\s*Math\.min\(480,\s*height \* 0\.5\)\)/.test(directMessageSource))
		verify(/ListView\.onPooled:\s*\{[\s\S]*accessibilityPooled\s*=\s*true/.test(directMessageSource))
		verify(/ListView\.onReused:\s*\{[\s\S]*accessibilityPooled\s*=\s*false[\s\S]*hydrateWithinWindow/.test(directMessageSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*AttachmentGallery\s*\{/.test(directMessageSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*RichPreviewCard\s*\{/.test(directMessageSource))
		verify(/resourceActive:\s*!messageDelegate\.accessibilityPooled/.test(directMessageSource))
		verify(/renderActive:\s*messageDelegate\.inHydrationWindow/.test(directMessageSource))
	}

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
				"subtitle": "Can you check the reconnect dialog?", "status": "",
				"replyActor": "", "replySnippet": "", "reactions": [],
				"bodySegments": [{ "kind": "text", "text": "Can you check the reconnect dialog?" }],
				"preview": {}, "attachments": [], "source": {},
				"own": false, "deleted": false, "canReply": false, "canReact": false,
				"canDelete": false, "avatarUrl": ""
			})
			append({
				"stableId": "dm:2", "title": "You", "timestamp": "10:37",
				"subtitle": "Yes. The compact window is native QML.", "status": "",
				"replyActor": "", "replySnippet": "", "reactions": [],
				"bodySegments": [{ "kind": "text", "text": "Yes. The compact window is native QML." }],
				"preview": {}, "attachments": [], "source": {},
				"own": true, "deleted": false, "canReply": false, "canReact": false,
				"canDelete": false, "avatarUrl": ""
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
		property bool canAttachImages: true
		property bool canAttachFiles: true
		property var draftAttachments: []
		property bool hasPendingReply: false
		property string pendingReplyMessageId: ""
		property string pendingReplyActor: ""
		property string pendingReplySnippet: ""
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
		property string lastRichAction: ""
		property string lastRichMessageId: ""
		property string lastReaction: ""
		property int hydrationCalls: 0

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
		function replyToMessage(messageId) {
			lastRichAction = "reply"
			lastRichMessageId = String(messageId)
			hasPendingReply = true
			pendingReplyMessageId = String(messageId)
			pendingReplyActor = "C++ Alex"
			pendingReplySnippet = "C++ QVariantList message"
		}
		function cancelPendingReply() {
			hasPendingReply = false
			pendingReplyMessageId = ""
			pendingReplyActor = ""
			pendingReplySnippet = ""
		}
		function retryMessage(messageId) {
			lastRichAction = "retry"
			lastRichMessageId = String(messageId)
		}
		function deleteMessage(messageId) {
			lastRichAction = "delete"
			lastRichMessageId = String(messageId)
		}
		function toggleMessageReaction(messageId, emoji) {
			lastRichAction = "reaction"
			lastRichMessageId = String(messageId)
			lastReaction = String(emoji)
		}
		function chooseAttachment() { lastRichAction = "attach" }
		function removeDraftAttachment(attachmentId) { lastRichAction = "remove:" + String(attachmentId) }
		function retryDraftAttachment(attachmentId) { lastRichAction = "retry-attachment:" + String(attachmentId) }
		function openAttachment(assetId, fileName) {
			lastRichAction = "open-attachment"
			lastRichMessageId = String(assetId)
		}
		function downloadAttachment(assetId, fileName) {
			lastRichAction = "download-attachment"
			lastRichMessageId = String(assetId)
		}
		function requestContentHydration(messageId, highPriority) { hydrationCalls += 1 }
		function setWindowDocked(value) { windowDocked = !!value }
		function setWindowMinimized(value) { windowMinimized = !!value }
	}

	QtObject {
		id: fakeMediaSession
		property bool active: false
		property bool detached: true
		property bool sharedAvailable: false
		property bool sharedHost: false
		property bool playbackControllable: true
		property string sessionId: ""
		property string provider: ""
		property string url: ""
		property string audioUrl: ""
		property string mediaMime: ""
		property string audioMime: ""
		property real position: 0
		property string state: "idle"
		property int openInlineCalls: 0
		property int openCalls: 0
		property int openDirectInlineCalls: 0
		property int openDirectCalls: 0
		property int attachCalls: 0
		property int detachCalls: 0
		property int playCalls: 0
		function openInline() { openInlineCalls += 1; return true }
		function open() { openCalls += 1; return true }
		function openDirectInline() { openDirectInlineCalls += 1; return true }
		function openDirect() { openDirectCalls += 1; return true }
		function attach() { attachCalls += 1; detached = false }
		function detach() { detachCalls += 1; detached = true }
		function play() { playCalls += 1; state = "playing" }
	}

	SignalSpy { id: managedImageSpy; signalName: "managedImageOpenRequested" }
	SignalSpy { id: watchTogetherSpy; signalName: "watchTogetherRequested" }
	SignalSpy { id: sizePresetSpy; signalName: "previewSizePresetRequested" }

	Component {
		id: trayLoaderComponent
		Loader { active: false }
	}

	Component {
		id: directWindowLoaderComponent
		Loader { active: false }
	}

	function init() {
		cppDirectMessageTimelineModel.clearSearch()
		summaryRows.setProperty(0, "unreadCount", 2)
		fakeController.timelineModel = timelineRows
		timelineRows.setProperty(0, "preview", ({}))
		fakeController.trayOpen = true
		fakeController.conversationOpen = true
		fakeController.activeSessionId = "7"
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
		fakeController.draftAttachments = []
		fakeController.hasPendingReply = false
		fakeController.pendingReplyMessageId = ""
		fakeController.pendingReplyActor = ""
		fakeController.pendingReplySnippet = ""
		fakeController.lastRichAction = ""
		fakeController.lastRichMessageId = ""
		fakeController.lastReaction = ""
		fakeController.hydrationCalls = 0
		fakeMediaSession.active = false
		fakeMediaSession.detached = true
		fakeMediaSession.sharedAvailable = false
		fakeMediaSession.sessionId = ""
		fakeMediaSession.provider = ""
		fakeMediaSession.url = ""
		fakeMediaSession.audioUrl = ""
		fakeMediaSession.mediaMime = ""
		fakeMediaSession.audioMime = ""
		fakeMediaSession.position = 0
		fakeMediaSession.state = "idle"
		fakeMediaSession.openInlineCalls = 0
		fakeMediaSession.openCalls = 0
		fakeMediaSession.openDirectInlineCalls = 0
		fakeMediaSession.openDirectCalls = 0
		fakeMediaSession.attachCalls = 0
		fakeMediaSession.detachCalls = 0
		fakeMediaSession.playCalls = 0
		managedImageSpy.target = null
		watchTogetherSpy.target = null
		sizePresetSpy.target = null
		managedImageSpy.clear()
		watchTogetherSpy.clear()
		sizePresetSpy.clear()
	}

	function createSearchDirectWindow() {
		cppDirectMessageTimelineModel.clearSearch()
		fakeController.timelineModel = cppDirectMessageTimelineModel
		const loader = createTemporaryObject(directWindowLoaderComponent, testCase)
		verify(loader)
		loader.setSource("qrc:/qml-shell/DirectMessageWindow.qml", {
			"controller": fakeController,
			"parentWindow": testCase.Window.window,
			"visualFixtureMode": true
		})
		loader.active = true
		tryCompare(loader, "status", Loader.Ready)
		const directWindow = loader.item
		verify(directWindow)
		tryCompare(directWindow, "visible", true)
		directWindow.requestActivate()
		tryCompare(directWindow, "active", true)
		return { "loader": loader, "window": directWindow }
	}

	function createRichDirectWindow(preview, savedPresets) {
		fakeController.timelineModel = timelineRows
		timelineRows.setProperty(0, "preview", preview)
		const loader = createTemporaryObject(directWindowLoaderComponent, testCase)
		verify(loader)
		loader.setSource("qrc:/qml-shell/DirectMessageWindow.qml", {
			"controller": fakeController,
			"parentWindow": testCase.Window.window,
			"mediaSessionController": fakeMediaSession,
			"savedPreviewSizePresets": savedPresets || ({})
		})
		loader.active = true
		tryCompare(loader, "status", Loader.Ready)
		const directWindow = loader.item
		verify(directWindow)
		tryCompare(directWindow, "visible", true)
		const timeline = findChild(directWindow.contentItem, "directMessageTimeline")
		verify(timeline)
		timeline.positionViewAtIndex(0, ListView.Beginning)
		timeline.forceLayout()
		let card = null
		tryVerify(function() {
			card = findChild(directWindow.contentItem, "directMessagePreview_dm:1")
			return card !== null
		})
		return { "loader": loader, "window": directWindow, "timeline": timeline, "card": card }
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
		compare(row.Accessible.description, "2 unread messages")
		const conversationList = findChild(tray, "directMessageTrayList")
		verify(conversationList)
		tray.height = Qt.binding(function() { return tray.implicitHeight })
		tryVerify(function() { return conversationList.height >= row.height }, 1000,
			"a single DM row must fit without clipping or an unnecessary scrollbar; list="
			+ conversationList.height + ", row=" + row.height + ", tray=" + tray.height
			+ ", implicit=" + tray.implicitHeight + ", chrome=" + tray.chromeHeight)
		row.forceActiveFocus()
		keyClick(Qt.Key_Return)
		compare(fakeController.openCalls, 1)
		compare(fakeController.lastOpenedSession, "7")

		const markRead = findChild(tray, "directMessageTrayMarkAllRead")
		verify(markRead)
		compare(markRead.text, "Mark all read")
		compare(markRead.Accessible.name, "Mark all read")
		summaryRows.setProperty(0, "unreadCount", 1)
		tryCompare(row.Accessible, "description", "1 unread message")
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
		verify(directWindow.captureRect.width >= 340)
		compare(directWindow.minimumWidth, 340)
		compare(directWindow.minimumHeight, 320)
		verify(directWindow.windowResizeAvailable)

		const resizeTopLeft = findChild(directWindow.contentItem,
			"directMessageResizeTopLeft")
		const resizeRight = findChild(directWindow.contentItem,
			"directMessageResizeRight")
		const resizeBottom = findChild(directWindow.contentItem,
			"directMessageResizeBottom")
		verify(resizeTopLeft)
		verify(resizeRight)
		verify(resizeBottom)
		verify(resizeTopLeft.enabled)
		compare(resizeTopLeft.resizeEdges, Qt.TopEdge | Qt.LeftEdge)
		compare(resizeTopLeft.cursorShape, Qt.SizeFDiagCursor)
		compare(resizeRight.resizeEdges, Qt.RightEdge)
		compare(resizeRight.cursorShape, Qt.SizeHorCursor)
		compare(resizeBottom.resizeEdges, Qt.BottomEdge)
		compare(resizeBottom.cursorShape, Qt.SizeVerCursor)

		const banner = findChild(directWindow.contentItem, "directMessageModeBanner")
		verify(banner)
		verify(banner.visible)
		const composer = findChild(directWindow.contentItem, "directMessageComposer")
		verify(composer)
		directWindow.requestActivate()
		tryCompare(directWindow, "active", true)
		tryCompare(composer, "activeFocus", true)
		const modeToggle = findChild(directWindow.contentItem, "directMessageModeToggle")
		verify(modeToggle)
		fakeController.setWindowMinimized(true)
		tryCompare(directWindow, "minimumHeight", 74)
		tryCompare(resizeTopLeft, "enabled", false)
		modeToggle.forceActiveFocus()
		tryCompare(modeToggle, "activeFocus", true)
		fakeController.setWindowMinimized(false)
		tryCompare(directWindow, "minimumHeight", 320)
		tryCompare(resizeTopLeft, "enabled", true)
		tryCompare(composer, "activeFocus", true)
		composer.text = "A separate DM draft"
		compare(fakeController.draft, "A separate DM draft")
		keyClick(Qt.Key_Return)
		compare(fakeController.sendCalls, 1)
		compare(fakeController.sentMessage, "A separate DM draft")

		compare(modeToggle.iconName, "shield")
		mouseClick(modeToggle)
		compare(fakeController.mode, "history")
		compare(modeToggle.iconName, "history")

		const dockToggle = findChild(directWindow.contentItem, "directMessageDockToggle")
		verify(dockToggle)
		mouseClick(dockToggle)
		verify(fakeController.windowDocked)
		const minimizeButton = findChild(directWindow.contentItem, "directMessageWindowMinimize")
		verify(minimizeButton)
		compare(minimizeButton.iconName, "minimize")

		const closeButton = findChild(directWindow.contentItem, "directMessageWindowClose")
		verify(closeButton)
		mouseClick(closeButton)
		compare(fakeController.closeCalls, 1)
	}

	function test_private_window_renders_cpp_variant_list_segments() {
		fakeController.timelineModel = cppDirectMessageTimelineModel
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

		const timeline = findChild(directWindow.contentItem, "directMessageTimeline")
		verify(timeline)
		tryCompare(timeline, "count", 2)
		timeline.positionViewAtIndex(0, ListView.Beginning)
		timeline.forceLayout()
		let firstMessage = null
		tryVerify(function() {
			firstMessage = timeline.itemAtIndex(0)
			return firstMessage
				&& firstMessage.Accessible.name === "C++ Alex: C++ QVariantList message"
		})
		compare(firstMessage.laneMaximumWidth, Theme.chatLaneMaximumWidth)
		verify(firstMessage.wideContent)
		verify(firstMessage.messageWidth <= Theme.chatRichMaximumWidth)
		compare(directWindow.preferredAttachmentMessageWidth([
			{ "kind": "image", "width": 240 }
		], true), 240 + Theme.chatBubbleHorizontalPadding * 2)
		compare(directWindow.preferredAttachmentMessageWidth([
			{ "kind": "image" }, { "kind": "image" }
		], true), Theme.chatRichMaximumWidth)

		// ListView may legitimately recycle the first delegate before the second
		// one is instantiated. Verify each model row through its stable index
		// instead of depending on both delegates being QObject children at once.
		timeline.positionViewAtIndex(1, ListView.End)
		timeline.forceLayout()
		let secondMessage = null
		tryVerify(function() {
			secondMessage = timeline.itemAtIndex(1)
			return secondMessage
				&& secondMessage.Accessible.name === "You: C++ QVariantList reply"
		})
		verify(!secondMessage.wideContent)
		verify(secondMessage.messageWidth <= Theme.chatPlainMaximumWidth)
		verify(findChild(secondMessage, "directMessageAttachments_cpp-dm-2") === null)
		verify(findChild(secondMessage, "directMessagePreview_cpp-dm-2") === null)
	}

	function test_private_window_search_uses_stable_ids_and_keyboard_navigation() {
		const fixture = createSearchDirectWindow()
		const directWindow = fixture.window
		const composer = findChild(directWindow.contentItem, "directMessageComposer")
		const searchButton = findChild(directWindow.contentItem, "directMessageSearchButton")
		const timeline = findChild(directWindow.contentItem, "directMessageTimeline")
		verify(composer)
		verify(searchButton)
		verify(timeline)

		composer.forceActiveFocus()
		tryCompare(composer, "activeFocus", true)
		keyClick(Qt.Key_F, Qt.ControlModifier)

		const searchBar = findChild(directWindow.contentItem, "conversationSearchBar")
		const searchField = findChild(directWindow.contentItem, "conversationSearchField")
		const resultCount = findChild(directWindow.contentItem, "conversationSearchResultCount")
		verify(searchBar)
		verify(searchField)
		verify(resultCount)
		tryCompare(searchBar, "visible", true)
		tryCompare(searchField, "activeFocus", true)
		compare(searchBar.Accessible.role, Accessible.Grouping)
		verify(searchBar.Accessible.name.indexOf("Search") >= 0)
		verify(searchField.Accessible.description.indexOf("senders") >= 0)
		const cursorPaint = findChild(searchField, "conversationSearchCursorPaint")
		verify(cursorPaint)
		compare(cursorPaint.visible, false)

		keyClick(Qt.Key_V)
		keyClick(Qt.Key_A)
		keyClick(Qt.Key_R)
		keyClick(Qt.Key_I)
		keyClick(Qt.Key_A)
		keyClick(Qt.Key_N)
		keyClick(Qt.Key_T)
		tryCompare(cppDirectMessageTimelineModel, "query", "variant", 500)
		compare(cppDirectMessageTimelineModel.matchCount, 2)
		compare(cppDirectMessageTimelineModel.currentMatchStableId, "cpp-dm-1")
		compare(resultCount.text, "1 of 2")
		tryCompare(timeline, "currentIndex", 0)

		keyClick(Qt.Key_Return)
		compare(cppDirectMessageTimelineModel.currentMatchStableId, "cpp-dm-2")
		compare(cppDirectMessageTimelineModel.currentMatchRow, 1)
		compare(resultCount.text, "2 of 2")
		tryCompare(timeline, "currentIndex", 1)
		timeline.forceLayout()
		let currentMessage = null
		tryVerify(function() {
			currentMessage = timeline.itemAtIndex(1)
			return currentMessage && currentMessage.stableId === "cpp-dm-2"
				&& currentMessage.searchCurrent
		})

		keyClick(Qt.Key_Return, Qt.ShiftModifier)
		compare(cppDirectMessageTimelineModel.currentMatchStableId, "cpp-dm-1")
		tryCompare(timeline, "currentIndex", 0)

		keyClick(Qt.Key_Escape)
		tryCompare(searchBar, "visible", false)
		compare(cppDirectMessageTimelineModel.query, "")
		compare(cppDirectMessageTimelineModel.matchCount, 0)
		tryCompare(searchButton, "activeFocus", true)
	}

	function test_private_window_search_clears_when_the_conversation_changes() {
		const fixture = createSearchDirectWindow()
		const directWindow = fixture.window
		const composer = findChild(directWindow.contentItem, "directMessageComposer")
		verify(composer)
		composer.forceActiveFocus()
		keyClick(Qt.Key_F, Qt.ControlModifier)
		const searchBar = findChild(directWindow.contentItem, "conversationSearchBar")
		const searchField = findChild(directWindow.contentItem, "conversationSearchField")
		verify(searchBar)
		verify(searchField)
		tryCompare(searchField, "activeFocus", true)
		searchField.text = "rich-dm.pdf"
		searchBar.commitQuery()
		compare(cppDirectMessageTimelineModel.query, "rich-dm.pdf")
		compare(cppDirectMessageTimelineModel.matchCount, 1)
		compare(cppDirectMessageTimelineModel.currentMatchStableId, "cpp-dm-1")

		fakeController.activeSessionId = "8"
		tryCompare(searchBar, "visible", false)
		compare(cppDirectMessageTimelineModel.query, "")
		compare(cppDirectMessageTimelineModel.currentMatchStableId, "")
	}

	function test_private_window_renders_rich_message_state_and_routes_actions() {
		fakeController.timelineModel = cppDirectMessageTimelineModel
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
		directWindow.requestActivate()
		tryCompare(directWindow, "active", true)

		const timeline = findChild(directWindow.contentItem, "directMessageTimeline")
		verify(timeline)
		tryCompare(timeline, "count", 2)
		tryVerify(function() { return timeline.width > 0 && timeline.height > 0 })
		timeline.positionViewAtIndex(0, ListView.Beginning)
		timeline.forceLayout()
		let firstMessage = null
		tryVerify(function() {
			firstMessage = timeline.itemAtIndex(0)
			return firstMessage !== null
		})
		verify(findChild(firstMessage, "directMessageAttachments_cpp-dm-1"))
		verify(findChild(firstMessage, "directMessagePreview_cpp-dm-1"))
		verify(findChild(firstMessage, "directMessageReactions_cpp-dm-1"))
		// The rich fixture is taller than the compact DM viewport. Bring its
		// trailing action row on-screen before exercising pointer routing.
		timeline.positionViewAtIndex(0, ListView.End)
		timeline.forceLayout()

		let replyButton = null
		tryVerify(function() {
			firstMessage = timeline.itemAtIndex(0)
			replyButton = firstMessage
				? findChild(firstMessage, "directMessageReply_cpp-dm-1") : null
			return replyButton !== null && replyButton.enabled
		})
		// The fixture intentionally makes one delegate taller than the compact
		// viewport. Exercise the control contract directly instead of depending
		// on an offscreen plugin's clipping semantics for pointer delivery.
		replyButton.clicked()
		compare(fakeController.lastRichAction, "reply")
		compare(fakeController.lastRichMessageId, "cpp-dm-1")
		const replySurface = findChild(directWindow.contentItem, "directMessageComposerReply")
		verify(replySurface)
		tryCompare(replySurface, "visible", true)

		const cancelReply = findChild(directWindow.contentItem, "directMessageComposerReplyCancel")
		verify(cancelReply)
		// Visibility follows controller state immediately, while the layout pass
		// that gives the reply surface a pointer-active extent is asynchronous.
		tryVerify(function() {
			return replySurface.height > 0 && cancelReply.width > 0 && cancelReply.height > 0
		})
		mouseClick(cancelReply)
		verify(!fakeController.hasPendingReply)
		tryCompare(replySurface, "visible", false)
		timeline.positionViewAtIndex(0, ListView.End)
		timeline.forceLayout()

		const reactButton = findChild(firstMessage, "directMessageReact_cpp-dm-1")
		verify(reactButton)
		reactButton.clicked()
		compare(fakeController.lastRichAction, "reaction")
		compare(fakeController.lastReaction, "👍")

		const attachButton = findChild(directWindow.contentItem, "directMessageAttach")
		verify(attachButton)
		mouseClick(attachButton)
		compare(fakeController.lastRichAction, "attach")

		timeline.positionViewAtIndex(1, ListView.End)
		timeline.forceLayout()
		let secondMessage = null
		tryVerify(function() {
			secondMessage = timeline.itemAtIndex(1)
			return secondMessage !== null
		})
		const retryButton = findChild(secondMessage, "directMessageDeliveryRetry_cpp-dm-2")
		verify(retryButton)
		retryButton.clicked()
		compare(fakeController.lastRichAction, "retry")
		compare(fakeController.lastRichMessageId, "cpp-dm-2")
		const deleteButton = findChild(secondMessage, "directMessageDelete_cpp-dm-2")
		verify(deleteButton)
		deleteButton.clicked()
		compare(fakeController.lastRichAction, "delete")
	}

	function test_private_preview_routes_watch_together_and_preserves_size_preference() {
		const fixture = createRichDirectWindow({
			"state": "ready", "title": "Shared release stream",
			"url": "https://www.youtube.com/watch?v=dm-shared",
			"embedUrl": "https://www.youtube.com/embed/dm-shared",
			"embedKind": "youtube"
		}, { "dm:7:dm:1": "large" })
		const card = fixture.card
		compare(card.savedSizePreset, "large")
		compare(card.effectiveSizePreset, "large")
		watchTogetherSpy.target = fixture.window
		sizePresetSpy.target = fixture.window

		card.watchTogetherRequested(card.safeEmbedUrl, card.safeEmbedProvider, card.displayTitle)
		compare(watchTogetherSpy.count, 1)
		compare(watchTogetherSpy.signalArguments[0][0], "https://www.youtube.com/embed/dm-shared")
		compare(watchTogetherSpy.signalArguments[0][1], "youtube")
		compare(watchTogetherSpy.signalArguments[0][2], "Shared release stream")

		card.setSizePreset("compact")
		compare(sizePresetSpy.count, 1)
		compare(sizePresetSpy.signalArguments[0][0], "dm:7:dm:1")
		compare(sizePresetSpy.signalArguments[0][1], "compact")
	}

	function test_private_provider_preview_preserves_playback_when_moving_between_inline_and_popout() {
		const embedUrl = "https://www.youtube.com/embed/dm-continuity"
		const fixture = createRichDirectWindow({
			"state": "ready", "title": "DM continuity stream",
			"url": "https://www.youtube.com/watch?v=dm-continuity",
			"embedUrl": embedUrl, "embedKind": "youtube"
		})
		fakeMediaSession.active = true
		fakeMediaSession.detached = false
		fakeMediaSession.sessionId = "dm:1"
		fakeMediaSession.provider = "youtube"
		fakeMediaSession.url = embedUrl
		fakeMediaSession.position = 47.25
		fakeMediaSession.state = "playing"

		fixture.card.popoutPlayRequested(embedUrl, "youtube")
		compare(fakeMediaSession.detachCalls, 1)
		compare(fakeMediaSession.openCalls, 0)
		compare(fakeMediaSession.position, 47.25)
		compare(fakeMediaSession.state, "playing")
		verify(fakeMediaSession.detached)

		fixture.card.inlinePlayRequested(embedUrl, "youtube")
		compare(fakeMediaSession.attachCalls, 1)
		compare(fakeMediaSession.openInlineCalls, 0)
		compare(fakeMediaSession.playCalls, 1)
		compare(fakeMediaSession.position, 47.25)
		compare(fakeMediaSession.state, "playing")
		verify(!fakeMediaSession.detached)
	}

	function test_private_direct_preview_preserves_playback_when_moving_between_inline_and_popout() {
		const mediaUrl = "https://media.example.test/dm-continuity.mp4"
		const audioUrl = "https://media.example.test/dm-continuity.m4a"
		const fixture = createRichDirectWindow({
			"state": "ready", "title": "DM direct continuity",
			"mediaItems": [{
				"kind": "video", "mime": "video/mp4", "url": mediaUrl,
				"audioUrl": audioUrl, "audioMime": "audio/mp4"
			}]
		})
		fakeMediaSession.active = true
		fakeMediaSession.detached = false
		fakeMediaSession.sessionId = "dm:1"
		fakeMediaSession.provider = "direct"
		fakeMediaSession.url = mediaUrl
		fakeMediaSession.audioUrl = audioUrl
		fakeMediaSession.mediaMime = "video/mp4"
		fakeMediaSession.audioMime = "audio/mp4"
		fakeMediaSession.position = 83.5
		fakeMediaSession.state = "playing"

		fixture.card.popoutDirectMediaRequested(mediaUrl, "video/mp4", audioUrl,
			"audio/mp4", "DM direct continuity")
		compare(fakeMediaSession.detachCalls, 1)
		compare(fakeMediaSession.openDirectCalls, 0)
		compare(fakeMediaSession.position, 83.5)
		verify(fakeMediaSession.detached)

		fixture.card.directMediaRequested(mediaUrl, "video/mp4", audioUrl,
			"audio/mp4", "DM direct continuity")
		compare(fakeMediaSession.attachCalls, 1)
		compare(fakeMediaSession.openDirectInlineCalls, 0)
		compare(fakeMediaSession.playCalls, 1)
		compare(fakeMediaSession.position, 83.5)
		compare(fakeMediaSession.state, "playing")
		verify(!fakeMediaSession.detached)
	}

	function test_private_preview_routes_managed_image_to_native_viewer_contract() {
		const fixture = createRichDirectWindow({
			"state": "ready", "title": "Managed DM artwork",
			"mediaItems": [{
				"kind": "image", "mime": "image/png",
				"url": "image://mumble/dm-managed-artwork?g=1",
				"title": "Managed DM artwork"
			}]
		})
		managedImageSpy.target = fixture.window
		fixture.card.imageOpenRequested("image://mumble/dm-managed-artwork?g=1",
			"Managed DM artwork")
		compare(managedImageSpy.count, 1)
		compare(managedImageSpy.signalArguments[0][0],
			"image://mumble/dm-managed-artwork?g=1")
		compare(managedImageSpy.signalArguments[0][1], "Managed DM artwork")
		compare(managedImageSpy.signalArguments[0][2], "dm:1")
	}
}
