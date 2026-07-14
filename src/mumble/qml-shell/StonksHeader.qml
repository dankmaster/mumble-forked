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
	readonly property bool shouldShow: supported && enabledForServer && activeStonksScope && hasVisibleState

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
	Keys.onReturnPressed: event => {
		root.openRequested()
		event.accepted = true
	}
	Keys.onEnterPressed: event => {
		root.openRequested()
		event.accepted = true
	}

	background: Rectangle {
		radius: Theme.innerRadius
		color: root.down || root.hovered ? Theme.surfaceHover : Theme.strip
		border.color: root.activeFocus ? Theme.focus
			: root.errorText.length > 0 ? Theme.danger : Theme.surfaceBorder
		border.width: root.activeFocus ? Theme.focusRingWidth : 1
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

			Row {
				id: tickerRow
				objectName: "stonksHeaderTickerRow"
				anchors.left: parent.left
				anchors.verticalCenter: parent.verticalCenter
				spacing: Theme.space3
				visible: !root.loading && root.errorText.length === 0

				Repeater {
					model: root.visibleTickerRows
					delegate: Row {
						required property var modelData
						objectName: "stonksHeaderTicker_" + String(modelData.symbol || "")
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
		}

		ModernIcon {
			Layout.rightMargin: Theme.space2
			name: "next"
			size: 14
			color: Theme.textMuted
		}
	}
}
