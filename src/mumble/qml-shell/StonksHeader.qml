import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

AbstractButton {
	id: root

	property var stonks: ({})
	property string scopeToken: ""
	property string scopeLabel: ""
	property bool narrowLayout: false
	property bool docked: false
	property bool tickerBannerEnabled: false
	property string tickerDirection: "left"
	property string tickerSpeed: "normal"
	property bool animationReady: false
	readonly property bool loading: stonks.loading === true
	readonly property string errorText: String(stonks.error || "").trim()
	readonly property bool automationVisible: stonks.automationHeaderVisible === true
	readonly property bool supported: stonks.supported === true
	readonly property bool enabledForServer: stonks.enabled !== false
	readonly property bool activeStonksScope: automationVisible || normalizedScopeLabel() === "stonks"
		|| configuredScopeMatches()
	readonly property var tickerRows: buildTickerRows()
	readonly property int maximumTickerCount: narrowLayout || width < 300 ? 1 : width < 430 ? 2 : 3
	readonly property var visibleTickerRows: tickerRows.slice(0, maximumTickerCount)
	readonly property bool hasVisibleState: loading || errorText.length > 0 || tickerRows.length > 0
	readonly property bool shouldShow: tickerBannerEnabled && supported && enabledForServer
		&& hasVisibleState
	readonly property bool automationAnimationDisabled: stonks.disableTickerAnimation === true
	readonly property bool horizontalMovement: tickerDirection === "left" || tickerDirection === "right"
	readonly property bool movementShouldRun: !automationAnimationDisabled && tickerRows.length > 0
	readonly property string tickerSequenceKey: tickerRows.map(function(row) {
		return String(row.symbol || "")
	}).join("\u001f")
	readonly property bool tickerRunning: horizontalMovement
		? horizontalAnimation.running && !horizontalAnimation.paused
		: verticalAnimation.running && !verticalAnimation.paused

	signal openRequested()

	function normalizedScopeLabel() {
		return String(scopeLabel || "").trim().toLowerCase().replace(/^#/, "")
	}

	function configuredScopeMatches() {
		const configuredId = Number(stonks.textChannelId || 0)
		if (configuredId <= 0)
			return false
		const normalizedToken = String(scopeToken || "").trim().toLowerCase()
		return normalizedToken === "text:" + configuredId
			|| normalizedToken.endsWith(":" + configuredId)
	}

	function normalizedSymbol(ticker) {
		return String((ticker || {}).providerSymbol || (ticker || {}).symbol || "").trim().toUpperCase()
	}

	function tickerQuote(symbol) {
		const quotes = stonks.tickerQuotes || ({})
		return quotes[symbol] || quotes[String(symbol).toUpperCase()] || ({})
	}

	function appendRows(result, seen, source, group) {
		for (let index = 0; index < source.length; ++index) {
			const ticker = source[index] || ({})
			const symbol = normalizedSymbol(ticker)
			if (symbol.length === 0 || seen[symbol])
				continue
			seen[symbol] = true
			result.push({ "symbol": symbol, "group": group, "ticker": ticker,
				"quote": tickerQuote(symbol) })
		}
	}

	function buildTickerRows() {
		const rows = []
		const seen = ({})
		const preferences = stonks.feedPreferences || ({})
		if (preferences.showPins !== false)
			appendRows(rows, seen, stonks.pinnedTickers || [], qsTr("Pinned"))
		if (preferences.showMine !== false)
			appendRows(rows, seen, stonks.personalTickers || [], qsTr("Portfolio"))
		if (preferences.showPopular !== false)
			appendRows(rows, seen, stonks.popularTickers || [], qsTr("Popular"))
		return rows
	}

	function quoteLabel(row) {
		const quote = row.quote || ({})
		if (quote.pending === true)
			return qsTr("Loading")
		if (quote.ok === false || String(quote.error || "").length > 0)
			return qsTr("Unavailable")
		const price = Number(quote.price)
		if (!isFinite(price))
			return qsTr("Waiting")
		return Number(price).toLocaleString(Qt.locale(), "f", Math.abs(price) >= 100 ? 1 : 2)
	}

	function changeLabel(row) {
		const change = Number((row.quote || {}).changePercent)
		if (!isFinite(change))
			return ""
		return (change > 0 ? "+" : "") + change.toLocaleString(Qt.locale(), "f", 1) + "%"
	}

	function quoteTone(row) {
		const quote = row.quote || ({})
		if (quote.ok === false || String(quote.error || "").length > 0)
			return Theme.textMuted
		const change = Number(quote.changePercent)
		return !isFinite(change) || change === 0 ? Theme.textMuted
			: change > 0 ? Theme.success : Theme.danger
	}

	function pixelsPerSecond() {
		switch (tickerSpeed) {
		case "verySlow": return 12
		case "slow": return 18
		case "fast": return 42
		default: return 28
		}
	}

	function animationDuration(distance) {
		return Math.max(2600, Math.round(Math.max(1, distance) * 1000 / pixelsPerSecond()))
	}

	function updateTickerPause() {
		const animation = horizontalMovement ? horizontalAnimation : verticalAnimation
		if (!animation.running)
			return
		if (hovered || visualFocus)
			animation.pause()
		else
			animation.resume()
	}

	function restartTickerAnimation() {
		if (!animationReady)
			return
		Qt.callLater(function() {
			if (!root.visible || !root.movementShouldRun)
				return
			const animation = root.horizontalMovement ? horizontalAnimation : verticalAnimation
			if (animation.travel <= 0)
				return
			animation.restart()
			root.updateTickerPause()
		})
	}

	onTickerDirectionChanged: restartTickerAnimation()
	onTickerSpeedChanged: restartTickerAnimation()
	// Quote refreshes replace tickerRows even when the ordered symbols are unchanged.
	// Restarting for every new price snaps the strip back to its origin, so only a
	// real feed-order change is allowed to reset the loop.
	onTickerSequenceKeyChanged: restartTickerAnimation()
	onVisibleChanged: restartTickerAnimation()
	Component.onCompleted: {
		animationReady = true
		restartTickerAnimation()
	}

	visible: shouldShow
	enabled: shouldShow
	hoverEnabled: true
	focusPolicy: Qt.StrongFocus
	implicitWidth: narrowLayout ? 176 : 420
	implicitHeight: Theme.controlHeight
	padding: 0
	Accessible.role: Accessible.Button
	Accessible.name: loading ? qsTr("Loading Stonks market data")
		: errorText.length > 0 ? qsTr("Stonks quotes unavailable")
		: qsTr("Open Stonks ticker details")
	Accessible.description: loading ? String(stonks.status || qsTr("Loading ticker quotes"))
		: errorText.length > 0 ? errorText : qsTr("Shows pinned, portfolio, and popular ticker quotes")
	onClicked: openRequested()
	onHoveredChanged: updateTickerPause()
	onActiveFocusChanged: updateTickerPause()
	Keys.onReturnPressed: event => {
		root.openRequested()
		event.accepted = true
	}
	Keys.onEnterPressed: event => {
		root.openRequested()
		event.accepted = true
	}

	background: Rectangle {
		radius: root.docked ? 0 : Theme.innerRadius
		color: root.down || root.hovered ? Theme.surfaceHover : Theme.strip
		border.color: root.visualFocus ? Theme.focus
			: root.errorText.length > 0 ? Theme.danger : Theme.surfaceBorder
		border.width: root.visualFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}

	contentItem: RowLayout {
		spacing: Theme.space2

		RowLayout {
			Layout.leftMargin: Theme.space2
			spacing: Theme.space1
			ModernIcon {
				name: "activity"
				size: 15
				color: root.errorText.length > 0 ? Theme.danger : Theme.accent
			}
			Label {
				objectName: "stonksHeaderLabel"
				textFormat: Text.PlainText
				text: qsTr("STONKS")
				color: Theme.textStrong
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				font.letterSpacing: 0.5
			}
		}

		Item {
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true

			Column {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				spacing: 3
				visible: root.loading
				Label {
					objectName: "stonksHeaderLoadingLabel"
					width: parent.width
					textFormat: Text.PlainText
					text: qsTr("Loading Stonks…")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideRight
				}
				ModernProgressBar {
					objectName: "stonksHeaderProgress"
					width: parent.width
					indeterminate: true
					trackHeight: 3
					Accessible.name: qsTr("Loading Stonks ticker quotes")
				}
			}

			Label {
				objectName: "stonksHeaderErrorLabel"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				visible: !root.loading && root.errorText.length > 0
				textFormat: Text.PlainText
				text: root.narrowLayout ? qsTr("Quotes unavailable") : root.errorText
				color: Theme.danger
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
			}

			Item {
				id: tickerViewport
				objectName: "stonksTickerViewport"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.bottom: parent.bottom
				clip: true
				visible: !root.loading && root.errorText.length === 0

				Row {
					id: horizontalTrack
					objectName: "stonksTickerHorizontalTrack"
					anchors.verticalCenter: parent.verticalCenter
					spacing: Theme.space4
					visible: root.horizontalMovement

					Row {
						id: horizontalPrimary
						spacing: Theme.space4
						Repeater {
							model: root.tickerRows
							delegate: tickerDelegate
						}
					}
					Row {
						id: horizontalDuplicate
						spacing: Theme.space4
						Repeater {
							model: root.tickerRows
							delegate: tickerDelegate
						}
					}
				}

				Column {
					id: verticalTrack
					objectName: "stonksTickerVerticalTrack"
					width: parent.width
					spacing: Theme.space2
					visible: !root.horizontalMovement
					Column {
						id: verticalPrimary
						width: parent.width
						spacing: Theme.space2
						Repeater {
							model: root.tickerRows
							delegate: tickerDelegate
						}
					}
					Column {
						id: verticalDuplicate
						width: parent.width
						spacing: Theme.space2
						Repeater {
							model: root.tickerRows
							delegate: tickerDelegate
						}
					}
				}

				NumberAnimation {
					id: horizontalAnimation
					objectName: "stonksTickerHorizontalAnimation"
					target: horizontalTrack
					property: "x"
					readonly property real travel: horizontalPrimary.width + horizontalTrack.spacing
					from: root.tickerDirection === "right" ? -travel : 0
					to: root.tickerDirection === "right" ? 0 : -travel
					duration: root.animationDuration(travel)
					easing.type: Easing.Linear
					loops: Animation.Infinite
					onTravelChanged: {
						if (travel > 0 && horizontalTrack.x === 0)
							root.restartTickerAnimation()
					}
					running: root.visible && root.horizontalMovement && root.movementShouldRun
						&& tickerViewport.visible
					onRunningChanged: {
						if (!running)
							horizontalTrack.x = 0
						else
							root.updateTickerPause()
					}
				}

				NumberAnimation {
					id: verticalAnimation
					objectName: "stonksTickerVerticalAnimation"
					target: verticalTrack
					property: "y"
					readonly property real travel: verticalPrimary.height + verticalTrack.spacing
					from: root.tickerDirection === "down" ? -travel : 0
					to: root.tickerDirection === "down" ? 0 : -travel
					duration: root.animationDuration(travel)
					easing.type: Easing.Linear
					loops: Animation.Infinite
					onTravelChanged: {
						if (travel > 0 && verticalTrack.y === 0)
							root.restartTickerAnimation()
					}
					running: root.visible && !root.horizontalMovement && root.movementShouldRun
						&& tickerViewport.visible
					onRunningChanged: {
						if (!running)
							verticalTrack.y = 0
						else
							root.updateTickerPause()
					}
				}
			}
		}

		ModernIcon {
			Layout.rightMargin: Theme.space2
			name: "next"
			size: 14
			color: Theme.textMuted
		}
	}

	Component {
		id: tickerDelegate
		Row {
			required property var modelData
			objectName: "stonksTicker_" + String(modelData.symbol || "")
			height: Math.max(18, implicitHeight)
			spacing: Theme.space1
			Label {
				textFormat: Text.PlainText
				text: String(modelData.symbol || "")
				color: Theme.textStrong
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}
			Label {
				textFormat: Text.PlainText
				text: root.quoteLabel(modelData)
				color: Theme.textMain
				font.pixelSize: Theme.fontCaption
			}
			Label {
				visible: text.length > 0 && !root.narrowLayout
				textFormat: Text.PlainText
				text: root.changeLabel(modelData)
				color: root.quoteTone(modelData)
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}
		}
	}
}
