import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "StonksRoomPanel"
	when: windowShown
	visible: true
	width: 820
	height: 500

	Loader {
		id: loader
		width: 760
		height: item ? item.implicitHeight : 0
		anchors.centerIn: parent
		source: "qrc:/qml-shell/StonksRoomPanel.qml"
	}

	SignalSpy {
		id: actionSpy
		target: loader.item
		signalName: "actionRequested"
	}

	function ticker(symbol, holders) {
		return {
			"symbol": symbol,
			"providerSymbol": symbol,
			"displayName": symbol,
			"holderCount": holders,
			"providerId": "probe",
			"exchange": "Test"
		}
	}

	function populatedState() {
		return {
			"supported": true,
			"accessSupported": true,
			"allowed": true,
			"enabled": true,
			"registered": true,
			"selfUserId": 1,
			"selectedPeriod": "30d",
			"periods": ["1d", "7d", "30d", "ytd"],
			"textChannelId": 7,
			"leaderboard": [
				{ "rank": 1, "userId": 2, "userName": "Bob", "returnPercent": 18.42, "followed": true },
				{ "rank": 2, "userId": 1, "userName": "Alice", "returnPercent": 9.75, "followed": false },
				{ "rank": 3, "userId": 3, "userName": "Carol", "returnPercent": -2.35, "followed": false }
			],
			"popularTickers": [ticker("RKLB", 4), ticker("AMD", 3), ticker("ERIC-B.ST", 2)],
			"pinnedTickers": [ticker("RKLB", 1)]
		}
	}

	function init() {
		verify(loader.item !== null)
		loader.width = 760
		loader.item.narrowLayout = false
		loader.item.scopeToken = "text:7"
		loader.item.scopeLabel = "#stonks"
		loader.item.stonks = populatedState()
		actionSpy.clear()
		tryCompare(loader.item, "visible", true)
	}

	function lastAction() {
		return actionSpy.signalArguments[actionSpy.count - 1]
	}

	function test_access_and_room_identity_are_both_required() {
		const denied = populatedState()
		denied.allowed = false
		loader.item.stonks = denied
		tryCompare(loader.item, "visible", false)
		compare(loader.item.implicitHeight, 0)

		const unsupported = populatedState()
		unsupported.accessSupported = false
		loader.item.stonks = unsupported
		tryCompare(loader.item, "visible", false)

		const permitted = populatedState()
		permitted.automationHeaderVisible = false
		permitted.textChannelId = 12
		loader.item.stonks = permitted
		loader.item.scopeToken = "text:7"
		loader.item.scopeLabel = "general"
		tryCompare(loader.item, "visible", false)
		loader.item.scopeLabel = "stonks"
		tryCompare(loader.item, "visible", true)
	}

	function test_scope_type_is_required_when_channel_ids_collide() {
		const permitted = populatedState()
		permitted.automationHeaderVisible = false
		loader.item.stonks = permitted
		loader.item.scopeLabel = "Landing"
		loader.item.scopeToken = "0:7"
		tryCompare(loader.item, "visible", false)
		compare(loader.item.implicitHeight, 0)

		loader.item.scopeToken = "3:7"
		tryCompare(loader.item, "visible", true)

		loader.item.scopeToken = "text:7"
		tryCompare(loader.item, "visible", true)
	}

	function test_period_follow_and_navigation_actions_are_structured() {
		const period = findChild(loader.item, "stonksRoomPeriod_7d")
		verify(period !== null)
		period.clicked()
		compare(actionSpy.count, 1)
		compare(lastAction()[0], "stonks.selectPeriod")
		compare(lastAction()[1].period, "7d")

		const follow = findChild(loader.item, "stonksRoomFollow_2")
		verify(follow !== null)
		follow.clicked()
		compare(lastAction()[0], "stonks.unfollow")
		compare(lastAction()[1].userId, 2)

		findChild(loader.item, "stonksRoomPortfolio").clicked()
		compare(lastAction()[0], "server.stonksPortfolio")
		findChild(loader.item, "stonksRoomLeaderboard").clicked()
		compare(lastAction()[0], "server.stonksLeaderboard")
		findChild(loader.item, "stonksRoomRefresh").clicked()
		compare(lastAction()[0], "stonks.refresh")
	}

	function test_popular_tickers_toggle_server_pins() {
		const pinned = findChild(loader.item, "stonksRoomTicker_RKLB")
		const unpinned = findChild(loader.item, "stonksRoomTicker_AMD")
		verify(pinned !== null && unpinned !== null)
		compare(pinned.checked, true)
		compare(unpinned.checked, false)

		unpinned.clicked()
		compare(lastAction()[0], "stonks.setTickerPin")
		compare(lastAction()[1].symbol, "AMD")
		compare(lastAction()[1].pinned, true)
	}

	function test_narrow_panel_stays_bounded_and_accessible() {
		loader.width = 340
		loader.item.narrowLayout = true
		tryCompare(loader.item, "maximumLeaderboardRows", 2)
		tryCompare(loader.item, "maximumPopularRows", 2)
		verify(loader.item.contentItem.width <= loader.item.width)
		verify(loader.item.implicitHeight > 0)
		verify(loader.item.implicitHeight < 300)
		compare(loader.item.Accessible.role, Accessible.Pane)
		compare(loader.item.Accessible.name, "Stonks room dashboard")
	}

	function test_wide_panel_uses_a_single_compact_toolbar() {
		loader.width = 760
		loader.item.narrowLayout = false
		wait(0)

		const periods = findChild(loader.item, "stonksRoomPeriods")
		const refresh = findChild(loader.item, "stonksRoomRefresh")
		const portfolio = findChild(loader.item, "stonksRoomPortfolio")
		verify(periods !== null && refresh !== null && portfolio !== null)
		const periodsPosition = periods.mapToItem(loader.item, 0, 0)
		const refreshPosition = refresh.mapToItem(loader.item, 0, 0)
		const portfolioPosition = portfolio.mapToItem(loader.item, 0, 0)
		verify(Math.abs(periodsPosition.y - refreshPosition.y) < refresh.height)
		verify(Math.abs(refreshPosition.y - portfolioPosition.y) < portfolio.height)
		verify(loader.item.implicitHeight < 190)
		compare(refresh.Accessible.name, "Refresh")
	}
}
