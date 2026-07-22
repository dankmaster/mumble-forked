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
			"accessSupported": true,
			"allowed": true,
			"enabled": true,
			"tickerBannerEnabled": true,
			"tickerDirection": "left",
			"tickerSpeed": "normal",
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
		loader.item.tickerDirection = state.tickerDirection
		loader.item.tickerSpeed = state.tickerSpeed
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

	function test_access_contract_and_permission_are_strict_visibility_gates() {
		const denied = populatedState()
		denied.allowed = false
		loader.item.stonks = denied
		tryCompare(loader.item, "allowed", false)
		tryCompare(loader.item, "visible", false)

		const legacy = populatedState()
		legacy.accessSupported = false
		loader.item.stonks = legacy
		tryCompare(loader.item, "accessSupported", false)
		tryCompare(loader.item, "visible", false)
	}

	function test_ticker_supports_every_explicit_direction_and_speed() {
		const header = loader.item
		const directions = ["left", "right", "up", "down"]
		for (let index = 0; index < directions.length; ++index) {
			header.tickerDirection = directions[index]
			compare(header.horizontalMovement, index < 2)
			tryCompare(header, "movementShouldRun", true)
			tryCompare(header, "tickerRunning", true)
		}
		header.tickerSpeed = "verySlow"
		compare(header.pixelsPerSecond(), 12)
		header.tickerSpeed = "slow"
		compare(header.pixelsPerSecond(), 18)
		header.tickerSpeed = "normal"
		compare(header.pixelsPerSecond(), 28)
		header.tickerSpeed = "fast"
		compare(header.pixelsPerSecond(), 42)
	}

	function test_horizontal_ticker_advances_smoothly_after_content_layout() {
		const header = loader.item
		header.tickerDirection = "left"
		header.tickerSpeed = "fast"
		const track = findChild(header, "stonksTickerHorizontalTrack")
		const animation = findChild(header, "stonksTickerHorizontalAnimation")
		verify(track !== null && animation !== null)
		tryVerify(function() { return animation.travel > 0 })
		tryVerify(function() { return track.x < -1 })
		const firstX = track.x
		wait(120)
		const secondX = track.x
		wait(120)
		const thirdX = track.x
		verify(secondX < firstX - 1)
		verify(thirdX < secondX - 1)
	}

	function test_short_horizontal_feed_repeats_before_the_viewport_can_empty() {
		const header = loader.item
		const state = populatedState()
		state.personalTickers = []
		state.popularTickers = []
		header.stonks = state
		header.tickerDirection = "left"
		loader.width = 620

		const viewport = findChild(header, "stonksTickerViewport")
		const track = findChild(header, "stonksTickerHorizontalTrack")
		const animation = findChild(header, "stonksTickerHorizontalAnimation")
		verify(viewport !== null && track !== null && animation !== null)
		tryVerify(function() { return animation.travel > 0 && track.requiredCopyCount > 2 })

		// The strip must remain wider than the viewport by one complete sequence.
		// That makes the next copy enter before the current loop wraps, even for a
		// single-symbol feed on a wide window.
		verify(track.width >= viewport.width + animation.travel)
		// Starting a zero-distance animation before layout used to consume the
		// minimum-duration cycle and make the populated ticker appear to hesitate.
		tryVerify(function() { return track.x < -1 }, 900)
		verify(track.width + track.x >= viewport.width)
	}

	function test_short_vertical_feed_repeats_before_the_viewport_can_empty() {
		const header = loader.item
		const state = populatedState()
		state.personalTickers = []
		state.popularTickers = []
		header.stonks = state
		header.tickerDirection = "up"

		const viewport = findChild(header, "stonksTickerViewport")
		const track = findChild(header, "stonksTickerVerticalTrack")
		const animation = findChild(header, "stonksTickerVerticalAnimation")
		verify(viewport !== null && track !== null && animation !== null)
		tryVerify(function() { return animation.travel > 0 && track.requiredCopyCount > 2 })
		verify(track.height >= viewport.height + animation.travel)
		tryVerify(function() { return track.y < -1 })
		verify(track.height + track.y >= viewport.height)
	}

	function test_quote_refresh_does_not_snap_ticker_back_to_start() {
		const header = loader.item
		header.tickerDirection = "left"
		header.tickerSpeed = "fast"
		const track = findChild(header, "stonksTickerHorizontalTrack")
		const animation = findChild(header, "stonksTickerHorizontalAnimation")
		verify(track !== null && animation !== null)
		tryVerify(function() { return track.x < -8 && track.x > -animation.travel / 2 })
		const beforeRefreshX = track.x
		const refreshedState = populatedState()
		refreshedState.tickerQuotes.RKLB.price = 18.61
		header.stonks = refreshedState
		wait(80)
		verify(track.x < beforeRefreshX - 1)
	}

	function test_pointer_focus_does_not_leave_ticker_paused() {
		const header = loader.item
		header.tickerDirection = "left"
		header.forceActiveFocus(Qt.MouseFocusReason)
		tryCompare(header, "activeFocus", true)
		tryCompare(header, "tickerRunning", true)

		testCase.forceActiveFocus()
		header.forceActiveFocus(Qt.TabFocusReason)
		tryCompare(header, "visualFocus", true)
		tryCompare(header, "tickerRunning", false)
	}

	function test_automation_fixture_disables_marquee_without_hiding_tickers() {
		const header = loader.item
		const state = populatedState()
		state.disableTickerAnimation = true
		header.stonks = state
		header.tickerDirection = "down"

		tryCompare(header, "automationAnimationDisabled", true)
		tryCompare(header, "movementShouldRun", false)
		tryCompare(header, "tickerRunning", false)
		tryCompare(header, "visible", true)
		compare(header.visibleTickerRows.length, 3)
	}

	function test_populated_state_is_visible_typed_and_keyboard_accessible() {
		const header = loader.item
		compare(header.visibleTickerRows.length, 3)
		verify(findChild(header, "stonksTicker_RKLB") !== null)
		verify(findChild(header, "stonksTicker_AMD") !== null)
		verify(findChild(header, "stonksTicker_ERIC-B.ST") !== null)
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
