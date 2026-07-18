import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ServerAdminEditor"
	when: windowShown
	visible: true
	width: 920
	height: 980
	property int accessibilityViewportHeight: 980

	Item {
		id: editorAccessibilityViewport
		width: testCase.width
		height: testCase.accessibilityViewportHeight
	}

	ListModel {
		id: usersModel
		dynamicRoles: true
		property int totalCount: count
		property int filteredCount: count
		property int page: 0
		property int pageCount: Math.max(1, Math.ceil(filteredCount / 50))
		property int pageSize: 50
		property string selectedStableId: ""
		property string lastFilter: ""
		signal selectionChanged()
		function setFilter(value) { lastFilter = String(value) }
		function setPage(value) { page = Math.max(0, Number(value)) }
		function setSelectedStableId(value) {
			selectedStableId = String(value || "")
			selectionChanged()
		}
		function item(stableId) {
			for (let index = 0; index < count; ++index) {
				const row = get(index)
				if (String(row.stableId) === String(stableId)) return row
			}
			return ({})
		}
	}

	ListModel {
		id: bansModel
		dynamicRoles: true
		property int totalCount: count
		property int filteredCount: count
		property int page: 0
		property int pageCount: Math.max(1, Math.ceil(filteredCount / 50))
		property int pageSize: 50
		property string selectedStableId: ""
		property string lastFilter: ""
		signal selectionChanged()
		function setFilter(value) { lastFilter = String(value) }
		function setPage(value) { page = Math.max(0, Number(value)) }
		function setSelectedStableId(value) {
			selectedStableId = String(value || "")
			selectionChanged()
		}
		function item(stableId) {
			for (let index = 0; index < count; ++index) {
				const row = get(index)
				if (String(row.stableId) === String(stableId)) return row
			}
			return ({})
		}
	}

	QtObject {
		id: usersController
		property var model: usersModel
		property string state: "idle"
		property string errorMessage: ""
		property string operationError: ""
		property bool canManage: true
		property bool busy: false
		property var pendingConfirmation: ({})
		property int refreshCalls: 0
		property int renameCalls: 0
		property int unregisterCalls: 0
		property int confirmCalls: 0
		property int cancelCalls: 0
		property string lastStableId: ""
		property string lastName: ""
		function refresh() { ++refreshCalls; return true }
		function beginRename(stableId, name) {
			++renameCalls
			lastStableId = String(stableId)
			lastName = String(name)
			pendingConfirmation = { "kind": "renameUser", "title": "Rename registered user?",
				"message": "Apply the new name?", "confirmLabel": "Rename", "tone": "accent" }
			return true
		}
		function beginUnregister(stableId) {
			++unregisterCalls
			lastStableId = String(stableId)
			pendingConfirmation = { "kind": "unregisterUser", "title": "Unregister user?",
				"message": "Remove this registration?", "confirmLabel": "Unregister", "tone": "danger" }
			return true
		}
		function confirmPending() { ++confirmCalls; pendingConfirmation = ({}); return true }
		function cancelPending() { ++cancelCalls; pendingConfirmation = ({}) }
	}

	QtObject {
		id: bansController
		property var model: bansModel
		property string state: "idle"
		property string errorMessage: ""
		property string operationError: ""
		property bool canManage: true
		property bool busy: false
		property var pendingConfirmation: ({})
		property int refreshCalls: 0
		property int addCalls: 0
		property int editCalls: 0
		property int removeCalls: 0
		property int confirmCalls: 0
		property int cancelCalls: 0
		property string lastStableId: ""
		property var lastDraft: ({})
		function refresh() { ++refreshCalls; return true }
		function beginAdd(draft) {
			++addCalls; lastDraft = draft
			pendingConfirmation = { "kind": "addBan", "title": "Add ban?", "message": "Add it?",
				"confirmLabel": "Add ban", "tone": "danger" }
			return true
		}
		function beginEdit(stableId, draft) {
			++editCalls; lastStableId = String(stableId); lastDraft = draft
			pendingConfirmation = { "kind": "editBan", "title": "Update ban?", "message": "Apply it?",
				"confirmLabel": "Update ban", "tone": "danger" }
			return true
		}
		function beginRemove(stableId) {
			++removeCalls; lastStableId = String(stableId)
			pendingConfirmation = { "kind": "removeBan", "title": "Remove ban?", "message": "Remove it?",
				"confirmLabel": "Remove ban", "tone": "danger" }
			return true
		}
		function confirmPending() { ++confirmCalls; pendingConfirmation = ({}); return true }
		function cancelPending() { ++cancelCalls; pendingConfirmation = ({}) }
	}

	function resetUsers() {
		usersModel.clear()
		usersModel.selectedStableId = ""
		usersModel.lastFilter = ""
		usersModel.page = 0
		usersController.state = "idle"
		usersController.errorMessage = ""
		usersController.operationError = ""
		usersController.canManage = true
		usersController.busy = false
		usersController.pendingConfirmation = ({})
		usersController.refreshCalls = 0
		usersController.renameCalls = 0
		usersController.unregisterCalls = 0
		usersController.confirmCalls = 0
		usersController.cancelCalls = 0
		usersController.lastStableId = ""
		usersController.lastName = ""
	}

	function resetBans() {
		bansModel.clear()
		bansModel.selectedStableId = ""
		bansModel.lastFilter = ""
		bansModel.page = 0
		bansController.state = "idle"
		bansController.errorMessage = ""
		bansController.operationError = ""
		bansController.canManage = true
		bansController.busy = false
		bansController.pendingConfirmation = ({})
		bansController.refreshCalls = 0
		bansController.addCalls = 0
		bansController.editCalls = 0
		bansController.removeCalls = 0
		bansController.confirmCalls = 0
		bansController.cancelCalls = 0
		bansController.lastStableId = ""
		bansController.lastDraft = ({})
	}

	function init() {
		testCase.accessibilityViewportHeight = 980
		resetUsers()
		resetBans()
	}

	function createEditor(kind, controller, width) {
		const component = Qt.createComponent("qrc:/qml-shell/ServerAdminEditor.qml")
		compare(component.status, Component.Ready, component.errorString())
		const editor = component.createObject(testCase, {
			"field": { "id": "admin", "adminKind": kind, "controller": controller },
			"accessibilityViewport": editorAccessibilityViewport,
			"width": width || 880
		})
		verify(editor !== null, component.errorString())
		editor.height = editor.implicitHeight
		return editor
	}

	function test_usersLoadingAndErrorRetryAreExplicit() {
		usersController.state = "loading"
		let editor = createEditor("users", usersController)
		const loading = findChild(editor, "serverAdminLoading")
		verify(loading && loading.visible && loading.running)
		compare(loading.animated, true)
		editor.visualFixtureMode = true
		compare(loading.animated, false)
		editor.destroy()

		usersController.state = "error"
		usersController.errorMessage = "The server did not answer."
		editor = createEditor("users", usersController)
		const error = findChild(editor, "serverAdminLoadError")
		const retry = findChild(editor, "serverAdminRetry")
		verify(error && error.visible && retry && retry.enabled)
		retry.clicked()
		compare(usersController.refreshCalls, 1)
		editor.destroy()
	}

	function test_usersReadySelectRenameConfirmAndUnregister() {
		usersController.state = "ready"
		usersModel.append({ "stableId": "user:42", "userId": 42, "name": "Alice",
			"lastSeen": "2026-07-01T12:00:00Z", "lastChannelLabel": "Lobby", "pending": false })
		usersModel.append({ "stableId": "user:77", "userId": 77, "name": "Bob",
			"lastSeen": "", "lastChannelLabel": "Games", "pending": false })
		const editor = createEditor("users", usersController)
		const list = findChild(editor, "serverAdminRecordList")
		tryVerify(function() { return list && list.count === 2 && list.itemAtIndex(0) !== null })
		list.itemAtIndex(0).clicked()
		compare(usersModel.selectedStableId, "user:42")
		const name = findChild(editor, "registeredUserName")
		const rename = findChild(editor, "registeredUserRename")
		const unregister = findChild(editor, "registeredUserUnregister")
		const search = findChild(editor, "serverAdminSearch")
		verify(name && rename && unregister)
		compare(editor.initialFocusTarget, search)
		compare(name.text, "Alice")
		usersModel.setProperty(0, "name", "Alice Optimistic")
		tryCompare(name, "text", "Alice Optimistic")
		name.text = "Alice Cooper"
		name.forceActiveFocus(Qt.TabFocusReason)
		rename.clicked()
		compare(usersController.renameCalls, 1)
		compare(usersController.lastStableId, "user:42")
		compare(usersController.lastName, "Alice Cooper")
		const confirm = findChild(editor, "serverAdminConfirm")
		const cancel = findChild(editor, "serverAdminCancelConfirmation")
		verify(confirm && confirm.visible)
		tryCompare(confirm, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(cancel, "activeFocus", true)
		keyClick(Qt.Key_Tab)
		tryCompare(confirm, "activeFocus", true)
		confirm.clicked()
		compare(usersController.confirmCalls, 1)
		tryCompare(name, "activeFocus", true)

		unregister.clicked()
		compare(usersController.unregisterCalls, 1)
		compare(usersController.pendingConfirmation.kind, "unregisterUser")
		findChild(editor, "serverAdminCancelConfirmation").clicked()
		compare(usersController.cancelCalls, 1)
		editor.destroy()
	}

	function test_bansEmptyEditConfirmAndRemove() {
		bansController.state = "ready"
		let editor = createEditor("bans", bansController)
		const empty = findChild(editor, "serverAdminEmpty")
		verify(empty && empty.visible)
		compare(empty.text, "No active bans")
		const emptyDetailScroll = findChild(editor, "serverAdminDetailScroll")
		const emptyActionRow = findChild(editor, "serverAdminBanActionRow")
		const emptyActionBarrier = findChild(editor, "serverAdminBanActionAccessibilityBarrier")
		const emptySave = findChild(editor, "banSave")
		verify(emptyDetailScroll && emptyActionRow && emptyActionBarrier && emptySave)
		const emptyDetailFlickable = emptyDetailScroll.contentItem
		emptyDetailFlickable.contentY = Math.max(0,
			emptyDetailFlickable.contentHeight - emptyDetailFlickable.height)
		tryCompare(emptyActionRow, "accessibilityExposed", true)
		const emptyActionPoint = emptyActionRow.mapToItem(testCase, 0, 0)
		testCase.accessibilityViewportHeight = emptyActionPoint.y + emptyActionRow.height - 2
		tryCompare(emptyActionRow, "accessibilityExposed", false)
		tryCompare(emptyActionBarrier, "active", true)
		tryCompare(emptySave.Accessible, "ignored", true)
		testCase.accessibilityViewportHeight = emptyActionPoint.y + emptyActionRow.height + 2
		tryCompare(emptyActionRow, "accessibilityExposed", true)
		tryCompare(emptyActionBarrier, "active", false)
		tryCompare(emptySave.Accessible, "ignored", false)
		editor.destroy()

		bansModel.append({ "stableId": "ban:one", "address": "192.168.1.5", "mask": 32,
			"userName": "Trouble", "hash": "abc123", "reason": "Abuse",
			"startUtc": new Date("2026-07-01T12:00:00Z"), "durationSeconds": 3600,
			"permanent": false, "pending": false })
		editor = createEditor("bans", bansController)
		const list = findChild(editor, "serverAdminRecordList")
		tryVerify(function() { return list && list.itemAtIndex(0) !== null })
		list.itemAtIndex(0).clicked()
		compare(bansModel.selectedStableId, "ban:one")
		const reason = findChild(editor, "banReason")
		const save = findChild(editor, "banSave")
		const remove = findChild(editor, "banRemove")
		const detailScroll = findChild(editor, "serverAdminDetailScroll")
		const detailScrollBar = findChild(editor, "serverAdminDetailScrollBar")
		const permanent = findChild(editor, "banPermanent")
		const permanentBarrier = findChild(editor, "banPermanentAccessibilityBarrier")
		const duration = findChild(editor, "banDuration")
		const durationBarrier = findChild(editor, "banDurationAccessibilityBarrier")
		verify(reason && save && remove && remove.visible)
		verify(detailScroll && detailScrollBar && permanent && permanentBarrier
			&& duration && durationBarrier)
		compare(detailScroll.contentWidth, detailScroll.availableWidth)
		verify(detailScrollBar.x + detailScrollBar.width >= detailScroll.availableWidth - 1,
			"The detail scrollbar must stay on the right edge instead of crossing the editor heading")
		tryCompare(permanentBarrier, "active", true)
		tryCompare(durationBarrier, "active", true)
		tryCompare(permanent.Accessible, "ignored", true)
		tryCompare(duration.Accessible, "ignored", true)
		const detailFlickable = detailScroll.contentItem
		detailFlickable.contentY = Math.max(0, detailFlickable.contentHeight - detailFlickable.height)
		tryCompare(permanent, "accessibilityExposed", true)
		tryCompare(duration, "accessibilityExposed", true)
		tryCompare(permanentBarrier, "active", false)
		tryCompare(durationBarrier, "active", false)
		tryCompare(permanent.Accessible, "ignored", false)
		tryCompare(duration.Accessible, "ignored", false)
		compare(reason.text, "Abuse")
		bansModel.setProperty(0, "reason", "Optimistic moderation update")
		tryCompare(reason, "text", "Optimistic moderation update")
		reason.text = "Repeated abuse"
		save.clicked()
		compare(bansController.editCalls, 1)
		compare(bansController.lastStableId, "ban:one")
		compare(bansController.lastDraft.reason, "Repeated abuse")
		findChild(editor, "serverAdminConfirm").clicked()
		compare(bansController.confirmCalls, 1)
		remove.clicked()
		compare(bansController.removeCalls, 1)
		compare(bansController.pendingConfirmation.kind, "removeBan")
		editor.destroy()
	}

	function test_preselected_ban_hydrates_draft_when_editor_opens() {
		bansController.state = "ready"
		bansModel.append({ "stableId": "ban:restored", "address": "192.0.2.42", "mask": 32,
			"userName": "Demo Spammer", "hash": "fixture-hash", "reason": "Repeated channel spam",
			"startUtc": new Date("2026-07-01T12:00:00Z"), "durationSeconds": 3600,
			"permanent": false, "pending": false })
		// Reopening the dialog may restore selection before the editor component
		// exists, so no selectionChanged signal will be replayed to QML.
		bansModel.selectedStableId = "ban:restored"
		const editor = createEditor("bans", bansController)
		tryCompare(findChild(editor, "banAddress"), "text", "192.0.2.42")
		compare(findChild(editor, "banUser").text, "Demo Spammer")
		compare(findChild(editor, "banHash").text, "fixture-hash")
		compare(findChild(editor, "banReason").text, "Repeated channel spam")
		compare(findChild(editor, "banPermanent").checked, false)
		editor.destroy()
	}

	function test_loader_style_late_field_assignment_hydrates_preselected_ban() {
		bansController.state = "ready"
		bansModel.append({ "stableId": "ban:late-field", "address": "198.51.100.7", "mask": 24,
			"userName": "Late Fixture", "hash": "late-field-hash", "reason": "Restored after loader assignment",
			"startUtc": new Date("2026-07-01T12:00:00Z"), "durationSeconds": 7200,
			"permanent": false, "pending": false })
		bansModel.selectedStableId = "ban:late-field"

		const component = Qt.createComponent("qrc:/qml-shell/ServerAdminEditor.qml")
		compare(component.status, Component.Ready, component.errorString())
		const editor = component.createObject(testCase, { "width": 880 })
		verify(editor !== null, component.errorString())
		editor.field = { "id": "admin", "adminKind": "bans", "controller": bansController }
		editor.height = editor.implicitHeight

		tryCompare(findChild(editor, "banAddress"), "text", "198.51.100.7")
		compare(findChild(editor, "banMask").value, 24)
		compare(findChild(editor, "banUser").text, "Late Fixture")
		compare(findChild(editor, "banHash").text, "late-field-hash")
		compare(findChild(editor, "banReason").text, "Restored after loader assignment")
		compare(findChild(editor, "banPermanent").checked, false)
		editor.destroy()
	}

	function test_banAddSearchPermissionAndCompactLayout() {
		bansController.state = "ready"
		bansController.canManage = false
		let editor = createEditor("bans", bansController, 620)
		verify(editor.compactLayout)
		verify(editor.implicitHeight >= 800)
		const save = findChild(editor, "banSave")
		verify(save && !save.enabled)
		const search = findChild(editor, "serverAdminSearch")
		search.text = "certificate"
		compare(bansModel.lastFilter, "certificate")
		editor.destroy()

		bansController.canManage = true
		editor = createEditor("bans", bansController)
		findChild(editor, "banHash").text = "certificate-hash"
		findChild(editor, "banSave").clicked()
		compare(bansController.addCalls, 1)
		compare(bansController.lastDraft.hash, "certificate-hash")
		editor.destroy()
	}
}
