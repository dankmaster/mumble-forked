pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Control {
	id: root

	property var stonks: ({})
	property string scopeToken: ""
	property string scopeLabel: ""
	property bool narrowLayout: false
	property bool accessibilitySuppressed: false

	readonly property bool supported: stonks.supported === true
	readonly property bool accessSupported: stonks.accessSupported === true
	readonly property bool allowed: stonks.allowed === true
	readonly property bool enabledForServer: stonks.enabled !== false
	readonly property bool automationVisible: stonks.automationHeaderVisible === true
	readonly property bool activeStonksScope: automationVisible || normalizedScopeLabel() === "stonks"
		|| configuredScopeMatches()
	readonly property bool shouldShow: supported && accessSupported && allowed
		&& enabledForServer && activeStonksScope
	readonly property int maximumLeaderboardRows: narrowLayout || width < 560 ? 2 : 3
	readonly property var leaderboardRows: (stonks.leaderboard || []).slice(0, maximumLeaderboardRows)
	readonly property int maximumPopularRows: narrowLayout || width < 560 ? 2 : 3
	readonly property var popularRows: (stonks.popularTickers || []).slice(0, maximumPopularRows)

	signal actionRequested(string actionId, var payload)

	function normalizedScopeLabel() {
		return String(scopeLabel || "").trim().toLowerCase().replace(/^#/, "")
	}

	function configuredScopeMatches() {
		const configuredId = Number(stonks.textChannelId || 0)
		if (configuredId <= 0)
			return false
		const normalizedToken = String(scopeToken || "").trim().toLowerCase()
		// Production tokens use "<ChatScope enum>:<id>"; TextChannel is 3.
		return normalizedToken === "text:" + configuredId
			|| normalizedToken === "3:" + configuredId
	}

	function periodOptions() {
		const periods = stonks.periods || ["1d", "7d", "30d", "ytd"]
		const options = []
		for (let index = 0; index < periods.length; ++index) {
			const period = String(periods[index] || "").trim().toLowerCase()
			if (period.length > 0)
				options.push({ "label": period.toUpperCase(), "value": period })
		}
		return options
	}

	function formatReturn(value, insufficientHistory) {
		if (insufficientHistory === true)
			return qsTr("N/A")
		const amount = Number(value)
		if (!isFinite(amount))
			return qsTr("N/A")
		return (amount >= 0 ? "+" : "") + amount.toFixed(1) + "%"
	}

	function returnTone(value, insufficientHistory) {
		if (insufficientHistory === true || !isFinite(Number(value)))
			return Theme.textMuted
		return Number(value) >= 0 ? Theme.success : Theme.danger
	}

	function normalizedTickerSymbol(ticker) {
		return String((ticker || {}).symbol || (ticker || {}).providerSymbol || "").trim().toUpperCase()
	}

	function isTickerPinned(symbol) {
		const normalized = String(symbol || "").trim().toUpperCase()
		const pinned = stonks.pinnedTickers || []
		for (let index = 0; index < pinned.length; ++index) {
			if (normalizedTickerSymbol(pinned[index]) === normalized)
				return true
		}
		return false
	}

	visible: shouldShow
	enabled: shouldShow
	implicitWidth: 420
	implicitHeight: visible ? panelLayout.implicitHeight + topPadding + bottomPadding : 0
	leftPadding: Theme.space3
	rightPadding: Theme.space3
	topPadding: Theme.space2
	bottomPadding: Theme.space2
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Stonks room dashboard")
	Accessible.description: qsTr("Portfolio leaderboard and popular tickers above the room chat")
	Accessible.ignored: accessibilitySuppressed || !visible

	background: Rectangle {
		radius: Theme.innerRadius
		color: Theme.panel
		border.color: Theme.surfaceBorder
		border.width: 1
	}

	contentItem: ColumnLayout {
		id: panelLayout
		spacing: Theme.space2

		GridLayout {
			id: panelToolbar
			Layout.fillWidth: true
			columns: root.narrowLayout || root.width < 760 ? 1 : 2
			columnSpacing: Theme.space2
			rowSpacing: Theme.space1

			ModernSegmentedControl {
				id: periodControl
				objectName: "stonksRoomPeriods"
				Layout.fillWidth: true
				Layout.preferredWidth: 420
				Layout.maximumWidth: 420
				Layout.alignment: Qt.AlignVCenter
				model: root.periodOptions()
				currentValue: String(root.stonks.selectedPeriod || "30d").toLowerCase()
				accessibleName: qsTr("Leaderboard period")
				optionObjectNamePrefix: "stonksRoomPeriod"
				onActivated: function(index, value) {
					root.actionRequested("stonks.selectPeriod", { "period": String(value) })
				}
			}

			RowLayout {
				Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
				spacing: Theme.space1

				ModernIconButton {
					objectName: "stonksRoomRefresh"
					dense: true
					iconName: "refresh"
					text: qsTr("Refresh")
					toolTipText: qsTr("Refresh Stonks")
					Accessible.description: qsTr("Refresh the Stonks room dashboard")
					onClicked: root.actionRequested("stonks.refresh", {})
				}
				ModernButton {
					objectName: "stonksRoomPortfolio"
					dense: true
					text: qsTr("Portfolio")
					onClicked: root.actionRequested("server.stonksPortfolio", {})
				}
				ModernButton {
					objectName: "stonksRoomLeaderboard"
					dense: true
					text: root.narrowLayout ? qsTr("Rankings") : qsTr("Full leaderboard")
					onClicked: root.actionRequested("server.stonksLeaderboard", {})
				}
			}
		}

		GridLayout {
			Layout.fillWidth: true
			columns: root.narrowLayout || root.width < 560 ? 1 : 2
			columnSpacing: Theme.space2
			rowSpacing: Theme.space2

			Rectangle {
				Layout.fillWidth: true
				Layout.preferredHeight: leaderboardLayout.implicitHeight + Theme.space1 * 2
				color: Theme.strip
				border.color: Theme.divider
				radius: Theme.innerRadius

				ColumnLayout {
					id: leaderboardLayout
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space2
					anchors.topMargin: Theme.space1
					anchors.bottomMargin: Theme.space1
					spacing: Theme.space1

					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Top portfolios · %1").arg(String(root.stonks.selectedPeriod || "30d").toUpperCase())
						color: Theme.textStrong
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
					}

					Repeater {
						model: root.leaderboardRows
						delegate: Rectangle {
							id: leaderboardRow
							required property int index
							required property var modelData
							Layout.fillWidth: true
							Layout.preferredHeight: 28
							color: modelData.followed === true ? Theme.surfaceHover : "transparent"
							radius: 6
							Accessible.role: Accessible.ListItem
							Accessible.name: qsTr("Rank %1, %2, %3")
								.arg(modelData.rank || index + 1)
								.arg(String(modelData.userName || qsTr("User")))
								.arg(root.formatReturn(modelData.returnPercent, modelData.insufficientHistory))

							RowLayout {
								anchors.fill: parent
								anchors.leftMargin: Theme.space2
								anchors.rightMargin: Theme.space1
								spacing: Theme.space1
								Label {
									textFormat: Text.PlainText
									text: String(modelData.rank || leaderboardRow.index + 1) + "."
									color: Theme.textMuted
									font.pixelSize: Theme.fontCaption
								}
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: String(modelData.userName || qsTr("User"))
									color: Theme.textStrong
									font.pixelSize: Theme.fontCaption
									font.weight: Font.Medium
									elide: Text.ElideRight
								}
								Label {
									textFormat: Text.PlainText
									text: root.formatReturn(modelData.returnPercent, modelData.insufficientHistory)
									color: root.returnTone(modelData.returnPercent, modelData.insufficientHistory)
									font.pixelSize: Theme.fontCaption
									font.weight: Font.DemiBold
								}
								ModernButton {
									objectName: "stonksRoomFollow_" + String(modelData.userId || 0)
									visible: Number(modelData.userId) !== Number(root.stonks.selfUserId)
									enabled: root.stonks.registered === true
									dense: true
									checkable: true
									checked: modelData.followed === true
									text: checked ? qsTr("Following") : qsTr("Follow")
									onClicked: root.actionRequested(modelData.followed === true ? "stonks.unfollow" : "stonks.follow",
										{ "userId": modelData.userId })
								}
							}
						}
					}

					Label {
						Layout.fillWidth: true
						visible: root.leaderboardRows.length === 0
						textFormat: Text.PlainText
						text: qsTr("No ranked portfolios yet.")
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
					}
				}
			}

			Rectangle {
				Layout.fillWidth: true
				Layout.preferredHeight: popularLayout.implicitHeight + Theme.space1 * 2
				color: Theme.strip
				border.color: Theme.divider
				radius: Theme.innerRadius

				ColumnLayout {
					id: popularLayout
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space2
					anchors.topMargin: Theme.space1
					anchors.bottomMargin: Theme.space1
					spacing: Theme.space1

					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Popular in this server")
						color: Theme.textStrong
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
					}

					Flow {
						Layout.fillWidth: true
						Layout.preferredHeight: childrenRect.height
						spacing: Theme.space1

						Repeater {
							model: root.popularRows
							delegate: ModernButton {
								required property var modelData
								readonly property string symbol: root.normalizedTickerSymbol(modelData)
								readonly property bool tickerPinned: root.isTickerPinned(symbol)
								objectName: "stonksRoomTicker_" + symbol
								dense: true
								checkable: true
								checked: tickerPinned
								text: qsTr("%1 · %2").arg(symbol).arg(modelData.holderCount || 0)
								Accessible.description: tickerPinned
									? qsTr("Unpin %1 from the Stonks ticker").arg(symbol)
									: qsTr("Pin %1 to the Stonks ticker").arg(symbol)
								onClicked: root.actionRequested("stonks.setTickerPin", {
									"symbol": symbol,
									"displayName": modelData.displayName || "",
									"providerId": modelData.providerId || "",
									"providerSymbol": modelData.providerSymbol || symbol,
									"exchange": modelData.exchange || "",
									"quoteSourceUrl": modelData.quoteSourceUrl || "",
									"pinned": !tickerPinned
								})
							}
						}
					}

					Label {
						Layout.fillWidth: true
						visible: root.popularRows.length === 0
						textFormat: Text.PlainText
						text: qsTr("Popular tickers appear after portfolios are saved.")
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						wrapMode: Text.Wrap
					}
				}
			}
		}
	}
}
