import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "StonksEditor"
	when: windowShown
	visible: true
	width: 820
	height: 1100

	Loader {
		id: loader
		anchors.fill: parent
		source: "qrc:/qml-shell/StonksEditor.qml"
	}

	function populatedState() {
		return {
			"supported": true,
			"accessSupported": true,
			"allowed": true,
			"enabled": true,
			"feature": { "tickerBannerEnabled": false, "tickerPlacement": "bottom",
				"tickerDirection": "left", "tickerSpeed": "normal" },
			"registered": true,
			"canAdmin": true,
			"selfUserId": 1,
			"selectedUserId": 1,
			"selectedUserName": "Alice",
			"selectedPeriod": "30d",
			"periods": ["7d", "30d", "ytd"],
			"status": "Ready",
			"snapshots": [{
				"snapshotId": 77,
				"userId": 1,
				"userName": "Alice",
				"createdAt": 1779926400,
				"currency": "USD",
				"totalValue": 2744.04,
				"note": "Long term",
				"positionsRedacted": false,
				"positions": [
					{ "symbol": "RKLB", "quantity": 42, "price": 18.42, "marketValue": 773.64,
						"currency": "USD", "displayName": "Rocket Lab", "providerId": "yahoo",
						"providerSymbol": "RKLB", "exchange": "Nasdaq", "quoteTime": 1779926300,
						"quoteSourceUrl": "https://finance.yahoo.com/quote/RKLB", "quoteConfidence": 0.9 },
					{ "symbol": "AMD", "quantity": 12, "price": 164.2, "marketValue": 1970.4,
						"currency": "USD", "displayName": "AMD", "providerId": "yahoo",
						"providerSymbol": "AMD", "exchange": "Nasdaq", "quoteTime": 1779926300,
						"quoteSourceUrl": "https://finance.yahoo.com/quote/AMD", "quoteConfidence": 0.9 }
				]
			}],
			"leaderboard": [
				{ "rank": 1, "userId": 2, "userName": "Bob", "period": "30d", "returnPercent": 18.42, "followed": true },
				{ "rank": 2, "userId": 1, "userName": "Alice", "period": "30d", "returnPercent": 9.75, "followed": false }
			],
			"following": [{ "userId": 2, "userName": "Bob", "followed": true }],
			"users": [
				{ "userId": 1, "userName": "Alice", "followed": false },
				{ "userId": 2, "userName": "Bob", "followed": true },
				{ "userId": 3, "userName": "Carol", "followed": false }
			],
			"popularTickers": [
				{ "symbol": "RKLB", "displayName": "Rocket Lab", "holderCount": 4, "currency": "USD" },
				{ "symbol": "AMD", "displayName": "AMD", "holderCount": 3, "currency": "USD" }
			],
			"personalTickers": [{ "symbol": "RKLB", "holderCount": 1, "currency": "USD" }],
			"pinnedTickers": [{ "symbol": "RKLB", "displayName": "Rocket Lab" }],
			"feedPreferences": { "showMine": true, "showPopular": true, "showPins": true },
			"textChannelId": 7,
			"socialAnnouncementsEnabled": true,
			"textChannels": [{ "textChannelId": 7, "name": "stonks" }, { "textChannelId": 8, "name": "trading" }],
			"leaderboardDescription": "Portfolio returns over 30 days."
		}
	}

	function test_portfolio_shortcut_selects_portfolio_and_survives_state_refresh() {
		const state = populatedState()
		state.initialTab = "portfolio"
		loader.item.stonks = state
		compare(loader.item.activeTab, "portfolio")

		loader.item.stonks = populatedState()
		compare(loader.item.activeTab, "portfolio")
	}

	function test_ticker_settings_are_explicit_live_client_choices() {
		const editor = loader.item
		editor.selectTab("settings")
		const enabled = findChild(editor, "stonksTickerBannerEnabled")
		const placement = findChild(editor, "stonksTickerPlacement")
		const direction = findChild(editor, "stonksTickerDirection")
		const speed = findChild(editor, "stonksTickerSpeed")
		verify(enabled !== null)
		verify(placement !== null)
		verify(direction !== null)
		verify(speed !== null)
		compare(enabled.checked, false)
		compare(placement.currentValue, "bottom")
		compare(direction.currentValue, "left")
		compare(speed.currentValue, "normal")
		compare(placement.count, 4)
		compare(direction.count, 4)
		compare(speed.count, 4)

		mouseClick(enabled, enabled.width / 2, enabled.height / 2, Qt.LeftButton)
		compare(dialogState.lastAction, "setTickerPresentation")
		compare(dialogState.lastPayload.tickerBannerEnabled, true)

		placement.currentIndex = 0
		placement.activated(0)
		compare(dialogState.lastAction, "setTickerPresentation")
		compare(dialogState.lastPayload.tickerPlacement, "windowTop")
		placement.currentIndex = 1
		placement.activated(1)
		compare(dialogState.lastPayload.tickerPlacement, "top")
		direction.currentIndex = 3
		direction.activated(3)
		compare(dialogState.lastPayload.tickerDirection, "down")
		speed.currentIndex = 1
		speed.activated(1)
		compare(dialogState.lastPayload.tickerSpeed, "slow")
	}

	function init() {
		verify(loader.item !== null)
		loader.item.cancelDestructiveAction()
		loader.item.activeTab = "overview"
		loader.item.draftDirty = false
		loader.item.draftIdentity = ""
		loader.item.adminDirty = false
		loader.item.adminSelectedUserId = -1
		loader.item.stonks = populatedState()
		loader.item.synchronizeDraft()
		loader.item.synchronizeAdmin()
		tryCompare(loader.item.draftPositionModel, "count", 2)
	}

	function test_exposes_all_parity_tabs_and_admin_gate() {
		const editor = loader.item
		compare(loader.visible, true)
		compare(editor.visible, true)
		const tabIds = ["overview", "portfolio", "leaderboard", "following", "audit", "settings", "admin"]
		for (let index = 0; index < tabIds.length; ++index) {
			const button = findChild(editor, "stonksTab_" + tabIds[index])
			verify(button !== null)
			editor.selectTab(tabIds[index])
			compare(editor.activeTab, tabIds[index])
			compare(editor.contentAvailable, true, tabIds[index] + " availability")
			compare(editor.loading, false, tabIds[index] + " loading")
			const panel = findChild(editor, "stonksPanel_" + tabIds[index])
			verify(panel !== null, tabIds[index])
			compare(panel.visible, true, tabIds[index])
		}

		const nonAdmin = populatedState()
		nonAdmin.canAdmin = false
		editor.stonks = nonAdmin
		editor.selectTab("admin")
		compare(editor.activeTab, "overview")
	}

	function test_loading_state_uses_shared_progress_component() {
		const state = populatedState()
		state.loading = true
		state.status = "Loading Stonks leaderboard and ticker quotes..."
		loader.item.stonks = state
		const progress = findChild(loader.item, "stonksLoadingProgress")
		tryVerify(function() { return progress !== null && progress.visible })
		verify(progress.indeterminate)
		compare(progress.Accessible.role, Accessible.ProgressBar)
		compare(progress.Accessible.name, "Loading Stonks data")
	}

	function test_access_denial_removes_every_interactive_editor_surface() {
		const denied = populatedState()
		denied.allowed = false
		loader.item.stonks = denied
		tryCompare(loader.item, "contentAvailable", false)
		const unavailable = findChild(loader.item, "stonksUnavailableLabel")
		verify(unavailable !== null)
		tryCompare(unavailable, "visible", true)
		verify(unavailable.text.indexOf("Use Stonks") >= 0)

		const legacy = populatedState()
		legacy.accessSupported = false
		loader.item.stonks = legacy
		tryCompare(loader.item, "contentAvailable", false)
		verify(unavailable.text.indexOf("access-control contract") >= 0)
	}

	function test_portfolio_payload_preserves_quote_metadata_and_routes_save() {
		const editor = loader.item
		editor.selectTab("portfolio")
		editor.updatePosition(0, "quantity", 50)
		editor.updatePosition(0, "price", 20)
		editor.draftNote = "Updated allocation"
		const payload = editor.portfolioPayload()
		compare(payload.userId, 1)
		compare(payload.currency, "USD")
		compare(payload.note, "Updated allocation")
		compare(payload.positions.length, 2)
		compare(payload.positions[0].symbol, "RKLB")
		compare(payload.positions[0].marketValue, 1000)
		compare(payload.positions[0].providerId, "yahoo")
		compare(payload.positions[0].providerSymbol, "RKLB")
		compare(payload.positions[0].exchange, "Nasdaq")
		compare(editor.draftValidation(), "")

		editor.savePortfolio()
		compare(dialogState.lastAction, "savePortfolio")
		compare(dialogState.lastPayload.userId, 1)
		compare(dialogState.lastPayload.positions.length, 2)
		compare(dialogState.lastPayload.positions[0].marketValue, 1000)
	}

	function test_destructive_actions_require_confirmation() {
		const editor = loader.item
		editor.selectTab("portfolio")
		const clearButton = findChild(editor, "stonksPortfolioClear")
		const background = findChild(editor, "stonksBackgroundContent")
		const popup = findChild(editor, "stonksConfirmationPopup")
		const barrier = findChild(editor, "stonksConfirmationAccessibilityBarrier")
		const refreshButton = findChild(editor, "stonksRefresh")
		verify(clearButton !== null && clearButton.enabled)
		verify(background !== null && popup !== null && barrier !== null && refreshButton !== null)
		clearButton.forceActiveFocus()
		tryCompare(clearButton, "activeFocus", true)
		clearButton.clicked()
		compare(editor.pendingAction, "clearPortfolio")
		compare(editor.pendingPayload.userId, 1)
		const cancelButton = findChild(editor, "stonksConfirmCancel")
		const confirmButton = findChild(editor, "stonksConfirmAction")
		tryCompare(popup, "opened", true)
		verify(cancelButton !== null && confirmButton !== null)
		tryCompare(cancelButton, "activeFocus", true)
		tryCompare(barrier, "active", true)
		compare(popup.contentItem.Accessible.role, Accessible.Dialog)
		compare(popup.contentItem.Accessible.name, "Clear portfolio")
		verify(!background.enabled)
		tryVerify(function() {
			return background.Accessible.ignored && refreshButton.Accessible.ignored
				&& clearButton.Accessible.ignored;
		})
		verify(!cancelButton.Accessible.ignored)

		keyClick(Qt.Key_Tab)
		tryCompare(confirmButton, "activeFocus", true)
		keyClick(Qt.Key_Backtab)
		tryCompare(cancelButton, "activeFocus", true)
		keyClick(Qt.Key_Escape)
		tryCompare(popup, "opened", false)
		compare(editor.pendingAction, "")
		tryVerify(function() {
			return background.enabled && !background.Accessible.ignored
				&& !refreshButton.Accessible.ignored && !clearButton.Accessible.ignored;
		})
		tryCompare(clearButton, "activeFocus", true)

		clearButton.clicked()
		tryVerify(function() { return popup.opened && cancelButton.activeFocus })
		keyClick(Qt.Key_Tab)
		tryCompare(editor.Window.window.activeFocusItem, "objectName", "stonksConfirmAction")
		verify(confirmButton.activeFocus)
		keyClick(Qt.Key_Return)
		compare(dialogState.lastAction, "clearPortfolio")
		compare(dialogState.lastPayload.userId, 1)
		compare(editor.pendingAction, "")
		tryCompare(popup, "opened", false)

		editor.selectTab("audit")
		const deleteButton = findChild(editor, "stonksAuditDelete_77")
		verify(deleteButton !== null)
		deleteButton.clicked()
		compare(editor.pendingAction, "deleteSnapshot")
		editor.confirmDestructiveAction()
		compare(dialogState.lastAction, "deleteSnapshot")
		compare(dialogState.lastPayload.snapshotId, 77)
	}

	function test_follow_period_pin_and_admin_actions_are_structured() {
		const editor = loader.item
		editor.selectTab("leaderboard")
		const period = findChild(editor, "stonksPeriod_7d")
		verify(period !== null)
		period.clicked()
		compare(dialogState.lastAction, "selectPeriod")
		compare(dialogState.lastPayload.period, "7d")

		const follow = findChild(editor, "stonksLeaderboardFollow_2")
		verify(follow !== null)
		follow.clicked()
		compare(dialogState.lastAction, "unfollow")
		compare(dialogState.lastPayload.userId, 2)

		editor.selectTab("overview")
		const pin = findChild(editor, "stonksPin_AMD")
		verify(pin !== null)
		pin.clicked()
		compare(dialogState.lastAction, "setTickerPin")
		compare(dialogState.lastPayload.symbol, "AMD")
		compare(dialogState.lastPayload.pinned, true)

		editor.selectTab("admin")
		editor.adminEnabled = false
		editor.adminAnnouncements = false
		editor.adminTextChannelId = 8
		const configure = findChild(editor, "stonksAdminConfigure")
		verify(configure !== null)
		configure.clicked()
		compare(dialogState.lastAction, "configure")
		compare(dialogState.lastPayload.enabled, false)
		compare(dialogState.lastPayload.socialAnnouncementsEnabled, false)
		compare(dialogState.lastPayload.textChannelId, 8)
	}
}
