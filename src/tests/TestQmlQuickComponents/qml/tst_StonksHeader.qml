import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
	id: testCase
	name: "StonksHeader"
	when: windowShown
	visible: true
	width: 700
	height: 180

	Loader {
		id: loader
		width: 620
		height: 40
		anchors.centerIn: parent
		source: "qrc:/qml-shell/StonksHeader.qml"
	}

	SignalSpy {
		id: openSpy
		target: loader.item
		signalName: "openRequested"
	}

	function ticker(symbol, name) {
		return { "symbol": symbol, "providerSymbol": symbol, "displayName": name }
	}

	function populatedState() {
		return {
			"supported": true,
			"enabled": true,
			"tickerBannerEnabled": true,
			"tickerBannerAlwaysScroll": true,
			"automationHeaderVisible": true,
			"textChannelId": 7,
			"feedPreferences": { "showPins": true, "showMine": true, "showPopular": true },
			"pinnedTickers": [ticker("RKLB", "Rocket Lab")],
			"personalTickers": [ticker("AMD", "AMD")],
			"popularTickers": [ticker("ERIC-B.ST", "Ericsson")],
			"tickerQuotes": {
				"RKLB": { "ok": true, "price": 18.42, "changePercent": 4.7 },
				"AMD": { "ok": true, "price": 164.2, "changePercent": -1.3 },
				"ERIC-B.ST": { "ok": true, "price": 85.8, "changePercent": 0.1 }
			}
		}
	}

	function init() {
		verify(loader.item !== null)
		testCase.forceActiveFocus()
		loader.width = 620
		loader.item.narrowLayout = false
		loader.item.scopeToken = "text:7"
		loader.item.scopeLabel = "stonks"
		const state = populatedState()
		loader.item.stonks = state
		loader.item.tickerBannerEnabled = state.tickerBannerEnabled
		loader.item.tickerBannerAlwaysScroll = state.tickerBannerAlwaysScroll
		openSpy.clear()
		tryCompare(loader.item, "supported", true)
		tryCompare(loader.item, "enabledForServer", true)
		tryCompare(loader.item, "activeStonksScope", true)
		tryCompare(loader.item, "hasVisibleState", true)
		tryCompare(loader.item, "shouldShow", true)
		tryCompare(loader.item, "visible", true)
	}

	function test_client_opt_in_gate_is_off_until_enabled() {
		const header = loader.item
		header.tickerBannerEnabled = false
		tryCompare(header, "visible", false)
		compare(header.tickerRows.length, 3)

		header.tickerBannerEnabled = true
		tryCompare(header, "visible", true)
	}

	function test_ticker_scrolls_continuously_and_can_fall_back_to_overflow_only() {
		const header = loader.item
		header.tickerBannerAlwaysScroll = true
		tryCompare(header, "marqueeShouldRun", true)
		tryCompare(header, "marqueeRunning", true)

		header.tickerBannerAlwaysScroll = false
		const singleTickerState = populatedState()
		singleTickerState.personalTickers = []
		singleTickerState.popularTickers = []
		header.stonks = singleTickerState
		loader.width = 680
		tryCompare(header, "marqueeShouldRun", false)
		tryCompare(header, "marqueeRunning", false)

		loader.width = 170
		tryCompare(header, "marqueeShouldRun", true)
		tryCompare(header, "marqueeRunning", true)
	}

	function test_automation_fixture_disables_marquee_without_hiding_tickers() {
		const header = loader.item
		const state = populatedState()
		state.disableTickerAnimation = true
		header.stonks = state
		header.tickerBannerAlwaysScroll = true

		tryCompare(header, "automationAnimationDisabled", true)
		tryCompare(header, "marqueeShouldRun", false)
		tryCompare(header, "marqueeRunning", false)
		tryCompare(header, "visible", true)
		compare(header.visibleTickerRows.length, 3)
	}

	function test_populated_state_is_visible_typed_and_keyboard_accessible() {
		const header = loader.item
		compare(header.visibleTickerRows.length, 3)
		verify(findChild(header, "stonksHeaderTicker_RKLB") !== null)
		verify(findChild(header, "stonksHeaderTicker_AMD") !== null)
		verify(findChild(header, "stonksHeaderTicker_ERIC-B.ST") !== null)
		compare(header.Accessible.role, Accessible.Button)
		compare(header.Accessible.name, "Open Stonks ticker details")
		header.forceActiveFocus()
		tryCompare(header, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(openSpy.count, 1)
	}

	function test_loading_uses_shared_progress_language() {
		const state = populatedState()
		state.loading = true
		state.status = "Loading Stonks leaderboard and ticker quotes..."
		state.pinnedTickers = []
		state.personalTickers = []
		state.popularTickers = []
		state.tickerQuotes = {}
		loader.item.stonks = state

		const label = findChild(loader.item, "stonksHeaderLoadingLabel")
		const progress = findChild(loader.item, "stonksHeaderProgress")
		tryVerify(function() { return label !== null && label.visible && progress !== null && progress.visible })
		verify(progress.indeterminate)
		compare(progress.Accessible.role, Accessible.ProgressBar)
		compare(loader.item.Accessible.name, "Loading Stonks market data")
	}

	function test_error_is_distinct_from_loading_and_populated() {
		const state = populatedState()
		state.error = "Quote lookup is temporarily unavailable."
		state.status = "Showing cached ticker symbols."
		state.loading = false
		loader.item.stonks = state

		const error = findChild(loader.item, "stonksHeaderErrorLabel")
		const loading = findChild(loader.item, "stonksHeaderLoadingLabel")
		tryVerify(function() { return error !== null && error.visible && error.text.indexOf("temporarily unavailable") >= 0 })
		verify(loading !== null && !loading.visible)
		compare(loader.item.Accessible.name, "Stonks quotes unavailable")
	}

	function test_opted_in_banner_remains_visible_across_scopes_and_narrow_layout_is_bounded() {
		const state = populatedState()
		state.automationHeaderVisible = false
		state.textChannelId = 12
		loader.item.scopeToken = "text:7"
		loader.item.scopeLabel = "general"
		loader.item.stonks = state
		tryCompare(loader.item, "activeStonksScope", false)
		tryCompare(loader.item, "visible", true)

		loader.item.scopeLabel = "#stonks"
		tryCompare(loader.item, "activeStonksScope", true)
		tryCompare(loader.item, "visible", true)
		loader.width = 180
		loader.item.narrowLayout = true
		tryCompare(loader.item, "maximumTickerCount", 1)
		compare(loader.item.visibleTickerRows.length, 1)
		verify(loader.item.contentItem.width <= loader.item.width)
	}
}
