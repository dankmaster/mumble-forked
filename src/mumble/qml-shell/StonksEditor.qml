import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root
	property var stonks: ({})
	property string activeTab: "overview"
	property string draftIdentity: ""
	property string draftCurrency: "USD"
	property string draftNote: ""
	property bool draftDirty: false
	property string pendingAction: ""
	property var pendingPayload: ({})
	property string pendingTitle: ""
	property string pendingMessage: ""
	readonly property bool confirmationVisible: pendingAction.length > 0
	property Item focusBeforeConfirmation: null
	property var modalHost: null
	property bool adminEnabled: true
	property bool adminAnnouncements: true
	property int adminTextChannelId: 0
	property int adminSelectedUserId: -1
	property bool adminDirty: false
	property alias draftPositionModel: portfolioPositions

	readonly property var snapshots: stonks.snapshots || []
	readonly property var latestSnapshot: snapshots.length > 0 ? snapshots[0] : null
	readonly property var tickerSettings: stonks.feature || stonks || ({})
	property bool tickerBannerEnabled: false
	property string tickerPlacement: "bottom"
	property string tickerDirection: "left"
	property string tickerSpeed: "normal"
	readonly property bool loading: stonks.loading === true
	readonly property bool contentAvailable: stonks.supported !== false
		&& (stonks.enabled !== false || stonks.canAdmin === true)
	readonly property var tabItems: {
		const items = [
			{ "id": "overview", "label": qsTr("Overview") },
			{ "id": "portfolio", "label": qsTr("Portfolio") },
			{ "id": "leaderboard", "label": qsTr("Leaderboard") },
			{ "id": "following", "label": qsTr("Following") },
			{ "id": "audit", "label": qsTr("Audit") },
			{ "id": "settings", "label": qsTr("Settings") }
		]
		if (stonks.canAdmin === true)
			items.push({ "id": "admin", "label": qsTr("Admin") })
		return items
	}

	width: parent ? parent.width : 0
	spacing: 12

	ListModel { id: portfolioPositions }

	function numberValue(value) {
		const converted = Number(value)
		return isFinite(converted) ? converted : 0
	}

	function selectedUserId() {
		const selected = Number(stonks.selectedUserId)
		if (isFinite(selected) && selected >= 0)
			return selected
		const self = Number(stonks.selfUserId)
		return isFinite(self) && self >= 0 ? self : 0
	}

	function selectedUserName() {
		if (selectedUserId() === Number(stonks.selfUserId))
			return qsTr("You")
		const direct = String(stonks.selectedUserName || "").trim()
		if (direct.length > 0)
			return direct
		const users = stonks.users || []
		for (let index = 0; index < users.length; ++index) {
			if (Number(users[index].userId) === selectedUserId())
				return String(users[index].userName || qsTr("User"))
		}
		return qsTr("Portfolio")
	}

	function canEditPortfolio() {
		return stonks.canAdmin === true
			|| (stonks.registered === true && selectedUserId() === Number(stonks.selfUserId))
	}

	function formatMoney(value, currency) {
		const amount = numberValue(value)
		const code = String(currency || "USD").trim().toUpperCase() || "USD"
		return qsTr("%1 %2").arg(amount.toLocaleString(Qt.locale(), "f", 2)).arg(code)
	}

	function formatPercent(value) {
		const amount = numberValue(value)
		return (amount >= 0 ? "+" : "") + amount.toFixed(2) + "%"
	}

	function formatTime(value) {
		const seconds = numberValue(value)
		return seconds > 0 ? Qt.formatDateTime(new Date(seconds * 1000), "yyyy-MM-dd HH:mm") : qsTr("Never")
	}

	function positionMarketValue(position) {
		const quantity = numberValue(position && position.quantity)
		const price = numberValue(position && position.price)
		const calculated = quantity * price
		return calculated > 0 ? calculated : numberValue(position && position.marketValue)
	}

	function draftKey() {
		const latest = latestSnapshot
		return [selectedUserId(), latest ? latest.snapshotId || 0 : 0,
			latest ? latest.createdAt || 0 : 0, latest ? latest.totalValue || 0 : 0].join(":")
	}

	function normalizedPosition(position, fallbackCurrency) {
		const symbol = String(position && position.symbol || "").trim().toUpperCase()
		const providerId = String(position && position.providerId || "manual").trim() || "manual"
		return {
			"symbol": symbol,
			"quantity": numberValue(position && position.quantity),
			"price": numberValue(position && position.price),
			"marketValue": positionMarketValue(position),
			"currency": String(position && position.currency || fallbackCurrency || "USD").trim().toUpperCase() || "USD",
			"displayName": String(position && position.displayName || "").trim(),
			"providerId": providerId,
			"providerSymbol": String(position && position.providerSymbol || symbol).trim(),
			"exchange": String(position && position.exchange || "").trim(),
			"quoteTime": numberValue(position && position.quoteTime),
			"quoteSourceUrl": String(position && position.quoteSourceUrl || "").trim(),
			"quoteConfidence": numberValue(position && position.quoteConfidence)
		}
	}

	function appendDraftPosition(position) {
		portfolioPositions.append(normalizedPosition(position || ({}), draftCurrency))
	}

	function synchronizeDraft() {
		const nextKey = draftKey()
		if (nextKey === draftIdentity && draftDirty)
			return
		draftIdentity = nextKey
		portfolioPositions.clear()
		const latest = latestSnapshot
		draftCurrency = String(latest && latest.currency || "USD").trim().toUpperCase() || "USD"
		draftNote = String(latest && latest.note || "")
		const positions = latest && latest.positions || []
		for (let index = 0; index < positions.length; ++index)
			appendDraftPosition(positions[index])
		if (portfolioPositions.count === 0)
			appendDraftPosition({ "currency": draftCurrency })
		draftDirty = false
	}

	function synchronizeAdmin() {
		if (!adminDirty) {
			adminEnabled = stonks.enabled !== false
			adminAnnouncements = stonks.socialAnnouncementsEnabled !== false
			adminTextChannelId = numberValue(stonks.textChannelId)
		}
		if (adminSelectedUserId < 0)
			adminSelectedUserId = selectedUserId()
	}

	function selectTab(tabId) {
		const requested = String(tabId || "overview")
		if (requested === "admin" && stonks.canAdmin !== true)
			activeTab = "overview"
		else
			activeTab = requested
	}

	function addPosition(position) {
		appendDraftPosition(position || { "currency": draftCurrency })
		draftDirty = true
	}

	function removePosition(index) {
		if (index < 0 || index >= portfolioPositions.count)
			return
		portfolioPositions.remove(index)
		if (portfolioPositions.count === 0)
			appendDraftPosition({ "currency": draftCurrency })
		draftDirty = true
	}

	function updatePosition(index, field, value) {
		if (index < 0 || index >= portfolioPositions.count)
			return
		portfolioPositions.setProperty(index, field, value)
		if (field === "quantity" || field === "price") {
			const position = portfolioPositions.get(index)
			portfolioPositions.setProperty(index, "marketValue",
				numberValue(position.quantity) * numberValue(position.price))
		}
		draftDirty = true
	}

	function draftPositionsPayload() {
		const positions = []
		for (let index = 0; index < portfolioPositions.count; ++index) {
			const position = normalizedPosition(portfolioPositions.get(index), draftCurrency)
			if (position.symbol.length > 0 || position.quantity > 0 || position.price > 0)
				positions.push(position)
		}
		return positions
	}

	function portfolioPayload() {
		return {
			"userId": selectedUserId(),
			"currency": String(draftCurrency || "USD").trim().toUpperCase() || "USD",
			"note": String(draftNote || ""),
			"positions": draftPositionsPayload()
		}
	}

	function draftValidation() {
		if (!canEditPortfolio())
			return qsTr("You cannot edit this portfolio.")
		const payload = portfolioPayload()
		if (payload.positions.length === 0)
			return qsTr("Add at least one position.")
		const symbols = ({})
		for (let index = 0; index < payload.positions.length; ++index) {
			const position = payload.positions[index]
			if (position.symbol.length === 0)
				return qsTr("Row %1 needs a ticker symbol.").arg(index + 1)
			if (symbols[position.symbol])
				return qsTr("Ticker %1 is listed more than once.").arg(position.symbol)
			symbols[position.symbol] = true
			if (position.quantity <= 0 || position.price <= 0)
				return qsTr("Row %1 needs a positive quantity and price.").arg(index + 1)
			if (position.currency !== payload.currency)
				return qsTr("Row %1 currency must match %2.").arg(index + 1).arg(payload.currency)
		}
		return ""
	}

	function draftTotal() {
		const positions = draftPositionsPayload()
		let total = 0
		for (let index = 0; index < positions.length; ++index)
			total += numberValue(positions[index].marketValue)
		return total
	}

	function savePortfolio() {
		if (draftValidation().length === 0)
			dialogState.invokeAction("savePortfolio", portfolioPayload())
	}

	function requestDestructiveAction(action, payload, title, message) {
		const window = root.Window.window
		focusBeforeConfirmation = window ? window.activeFocusItem : null
		pendingAction = action
		pendingPayload = payload || ({})
		pendingTitle = title || qsTr("Confirm action")
		pendingMessage = message || ""
	}

	function cancelDestructiveAction() {
		pendingAction = ""
		pendingPayload = ({})
		pendingTitle = ""
		pendingMessage = ""
	}

	function confirmDestructiveAction() {
		if (pendingAction.length === 0)
			return
		const action = pendingAction
		const payload = pendingPayload
		cancelDestructiveAction()
		dialogState.invokeAction(action, payload)
	}

	function optionIndex(options, value) {
		for (let index = 0; index < options.length; ++index) {
			if (String(options[index].value) === String(value))
				return index
		}
		return 0
	}

	function tickerPlacementOptions() {
		return [
			{ "value": "windowTop", "label": qsTr("Top of window") },
			{ "value": "top", "label": qsTr("Top of content") },
			{ "value": "aboveComposer", "label": qsTr("Above message field") },
			{ "value": "bottom", "label": qsTr("Bottom of window") }
		]
	}

	function tickerDirectionOptions() {
		return [
			{ "value": "left", "label": qsTr("Left") },
			{ "value": "right", "label": qsTr("Right") },
			{ "value": "up", "label": qsTr("Up") },
			{ "value": "down", "label": qsTr("Down") }
		]
	}

	function tickerSpeedOptions() {
		return [
			{ "value": "verySlow", "label": qsTr("Very slow") },
			{ "value": "slow", "label": qsTr("Slow") },
			{ "value": "normal", "label": qsTr("Normal") },
			{ "value": "fast", "label": qsTr("Fast") }
		]
	}

	function synchronizeTickerSettings() {
		tickerBannerEnabled = tickerSettings.tickerBannerEnabled === true
		tickerPlacement = String(tickerSettings.tickerPlacement || "bottom")
		tickerDirection = String(tickerSettings.tickerDirection || "left")
		tickerSpeed = String(tickerSettings.tickerSpeed || "normal")
	}

	function syncConfirmationPopup() {
		if (confirmationVisible && !confirmationPopup.visible)
			confirmationPopup.open()
		else if (!confirmationVisible && confirmationPopup.visible)
			confirmationPopup.close()
	}

	function chartValues() {
		const values = []
		for (let index = snapshots.length - 1; index >= 0; --index)
			values.push(numberValue(snapshots[index].totalValue))
		return values
	}

	function overviewStats() {
		return [
			{ "label": qsTr("Total value"), "value": latestSnapshot
				? formatMoney(latestSnapshot.totalValue, latestSnapshot.currency) : "-" },
			{ "label": qsTr("Last update"), "value": latestSnapshot
				? formatTime(latestSnapshot.createdAt) : qsTr("Never") },
			{ "label": qsTr("Owner"), "value": selectedUserName() },
			{ "label": qsTr("Snapshots"), "value": String(snapshots.length) }
		]
	}

	function isTickerPinned(symbol) {
		const target = String(symbol || "").toUpperCase()
		const pinned = stonks.pinnedTickers || []
		for (let index = 0; index < pinned.length; ++index) {
			if (String(pinned[index].symbol || "").toUpperCase() === target)
				return true
		}
		return false
	}

	function adminUsers() {
		const users = []
		const seen = ({})
		const source = stonks.users || []
		for (let index = 0; index < source.length; ++index) {
			const userId = numberValue(source[index].userId)
			if (!seen[userId]) {
				seen[userId] = true
				users.push({ "userId": userId, "userName": String(source[index].userName || qsTr("User %1").arg(userId)) })
			}
		}
		if (!seen[selectedUserId()])
			users.push({ "userId": selectedUserId(), "userName": selectedUserName() })
		return users
	}

	function adminUserIndex() {
		const users = adminUsers()
		for (let index = 0; index < users.length; ++index) {
			if (Number(users[index].userId) === Number(adminSelectedUserId))
				return index
		}
		return users.length > 0 ? 0 : -1
	}

	function adminChannels() {
		const channels = [{ "textChannelId": 0, "label": qsTr("Auto #stonks") }]
		const source = stonks.textChannels || []
		for (let index = 0; index < source.length; ++index)
			channels.push({ "textChannelId": numberValue(source[index].textChannelId),
				"label": "#" + String(source[index].name || source[index].textChannelId) })
		return channels
	}

	function adminChannelIndex() {
		const channels = adminChannels()
		for (let index = 0; index < channels.length; ++index) {
			if (Number(channels[index].textChannelId) === Number(adminTextChannelId))
				return index
		}
		return 0
	}

	function configurePayload() {
		return { "enabled": adminEnabled, "socialAnnouncementsEnabled": adminAnnouncements,
			"textChannelId": Number(adminTextChannelId || 0) }
	}

	onStonksChanged: {
		const requestedInitialTab = String(stonks.initialTab || "")
		if (requestedInitialTab.length > 0)
			selectTab(requestedInitialTab)
		if (activeTab === "admin" && stonks.canAdmin !== true)
			activeTab = "overview"
		synchronizeTickerSettings()
		Qt.callLater(synchronizeDraft)
		Qt.callLater(synchronizeAdmin)
	}
	onConfirmationVisibleChanged: syncConfirmationPopup()
	Component.onCompleted: {
		selectTab(stonks.initialTab || "overview")
		synchronizeTickerSettings()
		Qt.callLater(synchronizeDraft)
		Qt.callLater(synchronizeAdmin)
		Qt.callLater(syncConfirmationPopup)
	}

	ColumnLayout {
		id: backgroundContent
		objectName: "stonksBackgroundContent"
		Layout.fillWidth: true
		spacing: 12
		enabled: !root.confirmationVisible
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("Stonks content")

	RowLayout {
		Layout.fillWidth: true
		spacing: 8
		ColumnLayout {
			Layout.fillWidth: true
			spacing: 2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String(root.stonks.error || root.stonks.status || "")
				visible: text.length > 0
				color: root.stonks.error ? Theme.danger : Theme.textMuted
				wrapMode: Text.Wrap
			}
			ModernProgressBar {
				objectName: "stonksLoadingProgress"
				Layout.fillWidth: true
				visible: root.loading
				indeterminate: true
				Accessible.name: qsTr("Loading Stonks data")
			}
		}
		ModernButton {
			objectName: "stonksRegister"
			visible: root.stonks.registered !== true && root.contentAvailable
			text: qsTr("Register")
			onClicked: dialogState.invokeAction("register", {})
		}
		ModernButton {
			id: stonksRefresh
			objectName: "stonksRefresh"
			text: qsTr("Refresh")
			onClicked: dialogState.invokeAction("refresh", {})
		}
	}

	Rectangle {
		Layout.fillWidth: true
		Layout.preferredHeight: unavailableLabel.implicitHeight + 24
		visible: !root.contentAvailable
		color: Theme.strip
		border.color: Theme.divider
		radius: Theme.innerRadius
		Label {
			id: unavailableLabel
			anchors.fill: parent
			anchors.margins: 12
			textFormat: Text.PlainText
			text: root.stonks.error || root.stonks.status || qsTr("Stonks is unavailable on this server.")
			color: Theme.textMuted
			wrapMode: Text.Wrap
		}
	}

	Flow {
		id: tabs
		Layout.fillWidth: true
		Layout.preferredHeight: childrenRect.height
		visible: root.contentAvailable && !root.loading
		spacing: 6
		Repeater {
			model: root.tabItems
			delegate: ModernButton {
				required property var modelData
				objectName: "stonksTab_" + modelData.id
				checkable: true
				checked: root.activeTab === modelData.id
				text: modelData.label
				onClicked: root.selectTab(modelData.id)
			}
		}
	}

	ColumnLayout {
		id: overviewPanel
		objectName: "stonksPanel_overview"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "overview"
		spacing: 12

		GridLayout {
			Layout.fillWidth: true
			columns: root.width >= 650 ? 4 : 2
			columnSpacing: 8
			rowSpacing: 8
			Repeater {
				model: root.overviewStats()
				delegate: Rectangle {
					required property var modelData
					Layout.fillWidth: true
					Layout.preferredHeight: 68
					color: Theme.panel
					border.color: Theme.divider
					radius: Theme.innerRadius
					Column {
						anchors.fill: parent
						anchors.margins: 10
						spacing: 4
						Label { textFormat: Text.PlainText; text: modelData.label; color: Theme.textMuted; font.pixelSize: 10 }
						Label { width: parent.width; textFormat: Text.PlainText; text: modelData.value; color: Theme.textStrong; font.bold: true; font.pixelSize: 15; elide: Text.ElideRight }
					}
				}
			}
		}

		Rectangle {
			id: chartCard
			property var values: root.chartValues()
			Layout.fillWidth: true
			Layout.preferredHeight: chartLayout.implicitHeight + 24
			color: Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			ColumnLayout {
				id: chartLayout
				anchors.fill: parent
				anchors.margins: 12
				spacing: 6
				RowLayout {
					Layout.fillWidth: true
					Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: qsTr("Portfolio history"); color: Theme.textStrong; font.bold: true }
					Label {
						textFormat: Text.PlainText
						text: root.snapshots.length === 1 ? qsTr("1 save")
							: qsTr("%1 saves").arg(root.snapshots.length)
						color: Theme.textMuted
						font.pixelSize: 10
					}
				}
				Canvas {
					id: historyChart
					property var values: chartCard.values
					Layout.fillWidth: true
					Layout.preferredHeight: 126
					visible: values.length > 0
					onValuesChanged: requestPaint()
					onWidthChanged: requestPaint()
					onHeightChanged: requestPaint()
					onPaint: {
						const ctx = getContext("2d")
						ctx.clearRect(0, 0, width, height)
						if (values.length === 0)
							return
						let minimum = values[0]
						let maximum = values[0]
						for (let index = 1; index < values.length; ++index) {
							minimum = Math.min(minimum, values[index])
							maximum = Math.max(maximum, values[index])
						}
						const range = Math.max(1, maximum - minimum)
						const inset = 8
						ctx.strokeStyle = Theme.divider
						ctx.lineWidth = 1
						for (let row = 1; row < 4; ++row) {
							const y = Math.round(height * row / 4) + 0.5
							ctx.beginPath(); ctx.moveTo(inset, y); ctx.lineTo(width - inset, y); ctx.stroke()
						}
						ctx.strokeStyle = Theme.accent
						ctx.lineWidth = 2.5
						ctx.beginPath()
						for (let index = 0; index < values.length; ++index) {
							const x = values.length === 1 ? width / 2
								: inset + index * (width - inset * 2) / (values.length - 1)
							const y = values.length === 1 ? height / 2
								: height - inset - (values[index] - minimum) * (height - inset * 2) / range
							if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
						}
						ctx.stroke()
						if (values.length === 1) {
							ctx.fillStyle = Theme.accent
							ctx.beginPath()
							ctx.arc(width / 2, height / 2, 4, 0, Math.PI * 2)
							ctx.fill()
						}
					}
				}
				Label {
					Layout.fillWidth: true
					visible: chartCard.values.length === 0
					textFormat: Text.PlainText
					text: qsTr("Save a portfolio to start the history chart.")
					color: Theme.textMuted
				}
			}
		}

		Label { textFormat: Text.PlainText; text: qsTr("Portfolio allocation"); color: Theme.textStrong; font.bold: true }
		Flow {
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: 8
			Repeater {
				model: root.latestSnapshot && root.latestSnapshot.positions || []
				delegate: Rectangle {
					required property var modelData
					width: Math.max(142, Math.min(220, allocationText.implicitWidth + 24))
					height: 72
					color: Theme.selected
					border.color: Theme.divider
					radius: Theme.innerRadius
					Column {
						id: allocationText
						anchors.fill: parent
						anchors.margins: 10
						spacing: 3
						Label { textFormat: Text.PlainText; text: modelData.symbol || ""; color: Theme.textStrong; font.bold: true }
						Label { textFormat: Text.PlainText; text: root.formatMoney(root.positionMarketValue(modelData), modelData.currency); color: Theme.textMain }
						Label { textFormat: Text.PlainText; text: modelData.displayName || modelData.exchange || ""; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; width: parent.width }
					}
				}
			}
		}
		Label {
			Layout.fillWidth: true
			visible: !(root.latestSnapshot && (root.latestSnapshot.positions || []).length > 0)
			textFormat: Text.PlainText
			text: root.latestSnapshot && root.latestSnapshot.positionsRedacted
				? qsTr("Portfolio positions are private.") : qsTr("No open positions to map.")
			color: Theme.textMuted
		}

		Flow {
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: 7
			Repeater {
				model: root.stonks.popularTickers || []
				delegate: Rectangle {
					id: tickerCard
					required property var modelData
					readonly property bool pinned: root.isTickerPinned(modelData.symbol)
					width: tickerLine.implicitWidth + 18
					height: 38
					color: Theme.strip
					border.color: pinned ? Theme.accent : Theme.divider
					radius: 8
					Row {
						id: tickerLine
						anchors.centerIn: parent
						spacing: 7
						Label { textFormat: Text.PlainText; text: modelData.symbol || ""; color: Theme.textStrong; font.bold: true }
						Label { textFormat: Text.PlainText; text: qsTr("%1 holders").arg(modelData.holderCount || 0); color: Theme.textMuted; font.pixelSize: 9 }
						ModernIconButton {
							objectName: "stonksPin_" + String(modelData.symbol || "")
							text: tickerCard.pinned ? "×" : "+"
							Accessible.name: tickerCard.pinned ? qsTr("Unpin %1").arg(modelData.symbol) : qsTr("Pin %1").arg(modelData.symbol)
							onClicked: dialogState.invokeAction("setTickerPin", { "symbol": modelData.symbol, "pinned": !tickerCard.pinned })
						}
					}
				}
			}
		}
	}

	ColumnLayout {
		id: settingsPanel
		objectName: "stonksPanel_settings"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "settings"
		spacing: 12

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: tickerPresentationLayout.implicitHeight + 24
			color: Theme.strip
			border.color: root.tickerBannerEnabled ? Theme.accent : Theme.divider
			radius: Theme.innerRadius
			ColumnLayout {
				id: tickerPresentationLayout
				anchors.fill: parent
				anchors.margins: 12
				spacing: 10
				RowLayout {
					Layout.fillWidth: true
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 3
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: qsTr("Ticker strip")
							color: Theme.textStrong
							font.bold: true
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: qsTr("Off by default. Changes are saved and shown immediately.")
							color: Theme.textMuted
							font.pixelSize: 10
							wrapMode: Text.Wrap
						}
					}
					ModernCheckBox {
						objectName: "stonksTickerBannerEnabled"
						text: qsTr("Show ticker")
						checked: root.tickerBannerEnabled
						onToggled: {
							root.tickerBannerEnabled = checked
							dialogState.invokeAction("setTickerPresentation",
								{ "tickerBannerEnabled": checked })
						}
					}
				}

				GridLayout {
					Layout.fillWidth: true
					columns: root.width >= 620 ? 3 : 1
					columnSpacing: 12
					rowSpacing: 8
					ColumnLayout {
						Layout.fillWidth: true
						Label { textFormat: Text.PlainText; text: qsTr("Placement"); color: Theme.textMuted; font.pixelSize: 10 }
						ModernComboBox {
							id: tickerPlacementSelect
							objectName: "stonksTickerPlacement"
							Layout.fillWidth: true
							model: root.tickerPlacementOptions()
							textRole: "label"
							valueRole: "value"
							currentIndex: root.optionIndex(model, root.tickerPlacement)
							onActivated: {
								root.tickerPlacement = String(currentValue)
								dialogState.invokeAction("setTickerPresentation",
									{ "tickerPlacement": root.tickerPlacement })
							}
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Label { textFormat: Text.PlainText; text: qsTr("Direction"); color: Theme.textMuted; font.pixelSize: 10 }
						ModernComboBox {
							id: tickerDirectionSelect
							objectName: "stonksTickerDirection"
							Layout.fillWidth: true
							model: root.tickerDirectionOptions()
							textRole: "label"
							valueRole: "value"
							currentIndex: root.optionIndex(model, root.tickerDirection)
							onActivated: {
								root.tickerDirection = String(currentValue)
								dialogState.invokeAction("setTickerPresentation",
									{ "tickerDirection": root.tickerDirection })
							}
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Label { textFormat: Text.PlainText; text: qsTr("Speed"); color: Theme.textMuted; font.pixelSize: 10 }
						ModernComboBox {
							id: tickerSpeedSelect
							objectName: "stonksTickerSpeed"
							Layout.fillWidth: true
							model: root.tickerSpeedOptions()
							textRole: "label"
							valueRole: "value"
							currentIndex: root.optionIndex(model, root.tickerSpeed)
							onActivated: {
								root.tickerSpeed = String(currentValue)
								dialogState.invokeAction("setTickerPresentation",
									{ "tickerSpeed": root.tickerSpeed })
							}
						}
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: tickerSourcesLayout.implicitHeight + 24
			color: Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			ColumnLayout {
				id: tickerSourcesLayout
				anchors.fill: parent
				anchors.margins: 12
				spacing: 8
				Label { textFormat: Text.PlainText; text: qsTr("Ticker feed"); color: Theme.textStrong; font.bold: true }
				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: 12
					ModernCheckBox {
						objectName: "stonksFeedMine"
						text: qsTr("Mine")
						checked: (root.stonks.feedPreferences || {}).showMine !== false
						onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": checked,
							"showPopular": (root.stonks.feedPreferences || {}).showPopular !== false,
							"showPins": (root.stonks.feedPreferences || {}).showPins !== false })
					}
					ModernCheckBox {
						objectName: "stonksFeedPopular"
						text: qsTr("Popular")
						checked: (root.stonks.feedPreferences || {}).showPopular !== false
						onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": (root.stonks.feedPreferences || {}).showMine !== false,
							"showPopular": checked, "showPins": (root.stonks.feedPreferences || {}).showPins !== false })
					}
					ModernCheckBox {
						objectName: "stonksFeedPins"
						text: qsTr("Pinned")
						checked: (root.stonks.feedPreferences || {}).showPins !== false
						onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": (root.stonks.feedPreferences || {}).showMine !== false,
							"showPopular": (root.stonks.feedPreferences || {}).showPopular !== false, "showPins": checked })
					}
				}
			}
		}
	}

	ColumnLayout {
		id: portfolioPanel
		objectName: "stonksPanel_portfolio"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "portfolio"
		spacing: 10

		RowLayout {
			Layout.fillWidth: true
			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2
				Label { textFormat: Text.PlainText; text: root.selectedUserName(); color: Theme.textStrong; font.bold: true; font.pixelSize: 15 }
				Label { textFormat: Text.PlainText; text: root.latestSnapshot ? qsTr("Current save %1").arg(root.formatTime(root.latestSnapshot.createdAt)) : qsTr("No portfolio saves"); color: Theme.textMuted; font.pixelSize: 10 }
			}
			Label { textFormat: Text.PlainText; text: root.formatMoney(root.draftTotal(), root.draftCurrency); color: Theme.accent; font.bold: true; font.pixelSize: 16 }
		}

		GridLayout {
			Layout.fillWidth: true
			columns: root.width >= 520 ? 2 : 1
			columnSpacing: 8
			ColumnLayout {
				Layout.fillWidth: true
				Label { textFormat: Text.PlainText; text: qsTr("Currency"); color: Theme.textMuted; font.pixelSize: 10 }
				ModernTextField {
					objectName: "stonksPortfolioCurrency"
					Layout.fillWidth: true
					text: root.draftCurrency
					enabled: root.canEditPortfolio()
					onTextEdited: { root.draftCurrency = text.toUpperCase(); root.draftDirty = true }
				}
			}
			ColumnLayout {
				Layout.fillWidth: true
				Label { textFormat: Text.PlainText; text: qsTr("Note"); color: Theme.textMuted; font.pixelSize: 10 }
				ModernTextField {
					objectName: "stonksPortfolioNote"
					Layout.fillWidth: true
					text: root.draftNote
					enabled: root.canEditPortfolio()
					placeholderText: qsTr("Optional portfolio note")
					onTextEdited: { root.draftNote = text; root.draftDirty = true }
				}
			}
		}

		Repeater {
			model: portfolioPositions
			delegate: Rectangle {
				id: positionCard
				required property int index
				required property string symbol
				required property real quantity
				required property real price
				required property real marketValue
				required property string currency
				required property string displayName
				required property string providerId
				required property string exchange
				Layout.fillWidth: true
				Layout.preferredHeight: positionLayout.implicitHeight + 20
				color: Theme.panel
				border.color: Theme.divider
				radius: Theme.innerRadius
				ColumnLayout {
					id: positionLayout
					anchors.fill: parent
					anchors.margins: 10
					spacing: 6
					GridLayout {
						Layout.fillWidth: true
						columns: root.width >= 650 ? 6 : 2
						columnSpacing: 6
						ModernTextField {
							objectName: "stonksPositionSymbol_" + positionCard.index
							Layout.fillWidth: true
							placeholderText: qsTr("Symbol")
							text: positionCard.symbol
							enabled: root.canEditPortfolio()
							onTextEdited: root.updatePosition(positionCard.index, "symbol", text.toUpperCase())
						}
						ModernTextField {
							Layout.fillWidth: true
							placeholderText: qsTr("Quantity")
							text: String(positionCard.quantity)
							enabled: root.canEditPortfolio()
							validator: DoubleValidator { bottom: 0 }
							onTextEdited: root.updatePosition(positionCard.index, "quantity", root.numberValue(text))
						}
						ModernTextField {
							Layout.fillWidth: true
							placeholderText: qsTr("Price")
							text: String(positionCard.price)
							enabled: root.canEditPortfolio()
							validator: DoubleValidator { bottom: 0 }
							onTextEdited: root.updatePosition(positionCard.index, "price", root.numberValue(text))
						}
						ModernTextField {
							Layout.fillWidth: true
							placeholderText: qsTr("Value")
							text: String(positionCard.marketValue)
							enabled: root.canEditPortfolio()
							validator: DoubleValidator { bottom: 0 }
							onTextEdited: root.updatePosition(positionCard.index, "marketValue", root.numberValue(text))
						}
						ModernTextField {
							Layout.fillWidth: true
							placeholderText: qsTr("Currency")
							text: positionCard.currency
							enabled: root.canEditPortfolio()
							onTextEdited: root.updatePosition(positionCard.index, "currency", text.toUpperCase())
						}
						ModernIconButton {
							objectName: "stonksPositionRemove_" + positionCard.index
							iconName: "close"
							enabled: root.canEditPortfolio()
							Accessible.name: qsTr("Remove position %1").arg(positionCard.index + 1)
							onClicked: root.removePosition(positionCard.index)
						}
					}
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: [positionCard.displayName, positionCard.providerId, positionCard.exchange].filter(function(value) { return String(value || "").length > 0 }).join(" · ")
						visible: text.length > 0
						color: Theme.textMuted
						font.pixelSize: 9
						elide: Text.ElideRight
					}
				}
			}
		}

		Label {
			Layout.fillWidth: true
			textFormat: Text.PlainText
			text: root.draftValidation().length > 0 ? root.draftValidation() : qsTr("Ready to save")
			color: root.draftValidation().length > 0 ? Theme.danger : Theme.success
			wrapMode: Text.Wrap
		}
		Flow {
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: 8
			ModernButton { objectName: "stonksPortfolioAdd"; text: qsTr("Add position"); enabled: root.canEditPortfolio(); onClicked: root.addPosition() }
			ModernButton {
				objectName: "stonksPortfolioClear"
				text: qsTr("Clear portfolio")
				enabled: root.canEditPortfolio() && !!root.latestSnapshot && root.numberValue(root.latestSnapshot.totalValue) > 0
				onClicked: root.requestDestructiveAction("clearPortfolio", { "userId": root.selectedUserId(),
					"currency": root.draftCurrency, "note": qsTr("Portfolio cleared") }, qsTr("Clear portfolio"),
					qsTr("Clear the portfolio for %1?").arg(root.selectedUserName()))
			}
			ModernButton { objectName: "stonksPortfolioSave"; text: qsTr("Save portfolio"); enabled: root.draftValidation().length === 0; onClicked: root.savePortfolio() }
		}
	}

	ColumnLayout {
		id: leaderboardPanel
		objectName: "stonksPanel_leaderboard"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "leaderboard"
		spacing: 9
		Flow {
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: 6
			Repeater {
				model: root.stonks.periods || []
				delegate: ModernButton {
					required property var modelData
					objectName: "stonksPeriod_" + String(modelData)
					checkable: true
					checked: String(modelData) === String(root.stonks.selectedPeriod || "30d")
					text: String(modelData).toUpperCase()
					onClicked: dialogState.invokeAction("selectPeriod", { "period": String(modelData) })
				}
			}
		}
		Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: root.stonks.leaderboardDescription || qsTr("Leaderboard ranks portfolio return for the selected period."); color: Theme.textMuted; wrapMode: Text.Wrap }
		Repeater {
			model: root.stonks.leaderboard || []
			delegate: Rectangle {
				id: leaderboardRow
				required property var modelData
				Layout.fillWidth: true
				Layout.preferredHeight: leaderboardLayout.implicitHeight + 18
				color: Theme.panel
				border.color: Theme.divider
				radius: Theme.innerRadius
				RowLayout {
					id: leaderboardLayout
					anchors.fill: parent
					anchors.margins: 9
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2
						Label { textFormat: Text.PlainText; text: (modelData.insufficientHistory ? "" : String(modelData.rank || index + 1) + ". ") + String(modelData.userName || qsTr("User")); color: Theme.textStrong; font.bold: true }
						Label { textFormat: Text.PlainText; text: modelData.insufficientHistory ? qsTr("Insufficient history") : qsTr("PnL %1").arg(modelData.period || root.stonks.selectedPeriod || "30d"); color: Theme.textMuted; font.pixelSize: 10 }
					}
					Label { textFormat: Text.PlainText; text: modelData.insufficientHistory ? qsTr("Need PnL") : root.formatPercent(modelData.returnPercent); color: modelData.insufficientHistory ? Theme.textMuted : (root.numberValue(modelData.returnPercent) >= 0 ? Theme.success : Theme.danger); font.bold: true }
					ModernButton {
						visible: root.stonks.canAdmin === true
						text: qsTr("View")
						onClicked: { root.selectTab("portfolio"); dialogState.invokeAction("selectUser", { "userId": modelData.userId }) }
					}
					ModernButton {
						objectName: "stonksLeaderboardFollow_" + String(modelData.userId || 0)
						text: modelData.followed ? qsTr("Unfollow") : qsTr("Follow")
						enabled: root.stonks.registered === true && Number(modelData.userId) !== Number(root.stonks.selfUserId)
						onClicked: dialogState.invokeAction(modelData.followed ? "unfollow" : "follow", { "userId": modelData.userId })
					}
				}
			}
		}
		Label { Layout.fillWidth: true; visible: (root.stonks.leaderboard || []).length === 0; textFormat: Text.PlainText; text: qsTr("No ranked PnL for this period yet."); color: Theme.textMuted }
	}

	ColumnLayout {
		id: followingPanel
		objectName: "stonksPanel_following"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "following"
		spacing: 8
		Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: qsTr("Choose whose portfolio updates appear in your feed."); color: Theme.textMuted; wrapMode: Text.Wrap }
		Repeater {
			model: root.stonks.users || []
			delegate: Rectangle {
				id: followingRow
				required property var modelData
				visible: Number(modelData.userId) !== Number(root.stonks.selfUserId)
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? 52 : 0
				color: Theme.panel
				border.color: Theme.divider
				radius: Theme.innerRadius
				RowLayout {
					anchors.fill: parent
					anchors.margins: 9
					Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.userName || qsTr("User"); color: Theme.textStrong; font.bold: true }
					Label { textFormat: Text.PlainText; text: modelData.followed ? qsTr("Following") : ""; color: Theme.success; font.pixelSize: 10 }
					ModernButton {
						objectName: "stonksFollowingAction_" + String(modelData.userId || 0)
						text: modelData.followed ? qsTr("Unfollow") : qsTr("Follow")
						enabled: root.stonks.registered === true
						onClicked: dialogState.invokeAction(modelData.followed ? "unfollow" : "follow", { "userId": modelData.userId })
					}
				}
			}
		}
		Label { Layout.fillWidth: true; visible: (root.stonks.users || []).length <= 1; textFormat: Text.PlainText; text: qsTr("No other registered users yet."); color: Theme.textMuted }
	}

	ColumnLayout {
		id: auditPanel
		objectName: "stonksPanel_audit"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "audit"
		spacing: 8
		Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: qsTr("Portfolio updates for %1").arg(root.selectedUserName()); color: Theme.textStrong; font.bold: true }
		Repeater {
			model: root.snapshots
			delegate: Rectangle {
				id: auditCard
				required property var modelData
				Layout.fillWidth: true
				Layout.preferredHeight: auditLayout.implicitHeight + 20
				color: Theme.panel
				border.color: Theme.divider
				radius: Theme.innerRadius
				ColumnLayout {
					id: auditLayout
					anchors.fill: parent
					anchors.margins: 10
					RowLayout {
						Layout.fillWidth: true
						ColumnLayout {
							Layout.fillWidth: true
							spacing: 2
							Label { textFormat: Text.PlainText; text: root.formatMoney(modelData.totalValue, modelData.currency); color: Theme.textStrong; font.bold: true }
							Label { textFormat: Text.PlainText; text: root.formatTime(modelData.createdAt); color: Theme.textMuted; font.pixelSize: 10 }
						}
						ModernButton {
							objectName: "stonksAuditDelete_" + String(modelData.snapshotId || 0)
							visible: root.canEditPortfolio() && Number(modelData.snapshotId || 0) > 0
							text: qsTr("Delete")
							onClicked: root.requestDestructiveAction("deleteSnapshot", { "snapshotId": modelData.snapshotId,
								"userId": modelData.userId || root.selectedUserId() }, qsTr("Delete portfolio update"),
								qsTr("Delete this update for %1?").arg(root.selectedUserName()))
						}
					}
					Repeater {
						model: auditCard.modelData.positions || []
						delegate: Label {
							required property var modelData
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: [modelData.symbol, modelData.displayName,
								root.formatMoney(root.positionMarketValue(modelData), modelData.currency)].filter(function(value) { return String(value || "").length > 0 }).join(" · ")
							color: Theme.textMain
							elide: Text.ElideRight
						}
					}
					Label { Layout.fillWidth: true; visible: (auditCard.modelData.positions || []).length === 0; textFormat: Text.PlainText; text: auditCard.modelData.positionsRedacted ? qsTr("Positions are private.") : qsTr("No open positions."); color: Theme.textMuted }
					Label { Layout.fillWidth: true; visible: String(auditCard.modelData.note || "").length > 0; textFormat: Text.PlainText; text: auditCard.modelData.note || ""; color: Theme.textMuted; wrapMode: Text.Wrap }
				}
			}
		}
		Label { Layout.fillWidth: true; visible: root.snapshots.length === 0; textFormat: Text.PlainText; text: qsTr("No portfolio updates yet."); color: Theme.textMuted }
	}

	ColumnLayout {
		id: adminPanel
		objectName: "stonksPanel_admin"
		Layout.fillWidth: true
		visible: root.contentAvailable && !root.loading && root.activeTab === "admin" && root.stonks.canAdmin === true
		spacing: 10

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: adminUserLayout.implicitHeight + 20
			color: Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			ColumnLayout {
				id: adminUserLayout
				anchors.fill: parent
				anchors.margins: 10
				Label { textFormat: Text.PlainText; text: qsTr("Manage portfolio"); color: Theme.textStrong; font.bold: true }
				ModernComboBox {
					id: adminUserSelect
					objectName: "stonksAdminUser"
					Layout.fillWidth: true
					model: root.adminUsers()
					textRole: "userName"
					valueRole: "userId"
					currentIndex: root.adminUserIndex()
					onActivated: root.adminSelectedUserId = Number(currentValue)
				}
				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: 7
					ModernButton { text: qsTr("View portfolio"); enabled: adminUserSelect.count > 0; onClicked: { root.selectTab("portfolio"); dialogState.invokeAction("selectUser", { "userId": root.adminSelectedUserId }) } }
					ModernButton { text: qsTr("View updates"); enabled: adminUserSelect.count > 0; onClicked: { root.selectTab("audit"); dialogState.invokeAction("selectUser", { "userId": root.adminSelectedUserId }) } }
					ModernButton {
						objectName: "stonksAdminClear"
						text: qsTr("Clear portfolio")
						enabled: adminUserSelect.count > 0
						onClicked: root.requestDestructiveAction("clearPortfolio", { "userId": root.adminSelectedUserId,
							"currency": root.latestSnapshot && root.latestSnapshot.currency || "USD", "note": qsTr("Portfolio cleared by admin") },
							qsTr("Clear portfolio"), qsTr("Clear the selected user's portfolio?"))
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: adminConfigLayout.implicitHeight + 20
			color: Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			ColumnLayout {
				id: adminConfigLayout
				anchors.fill: parent
				anchors.margins: 10
				Label { textFormat: Text.PlainText; text: qsTr("Server settings"); color: Theme.textStrong; font.bold: true }
				ModernCheckBox { objectName: "stonksAdminEnabled"; text: qsTr("Stonks enabled"); checked: root.adminEnabled; onToggled: { root.adminEnabled = checked; root.adminDirty = true } }
				ModernCheckBox { objectName: "stonksAdminAnnouncements"; text: qsTr("Social announcements"); checked: root.adminAnnouncements; onToggled: { root.adminAnnouncements = checked; root.adminDirty = true } }
				ModernComboBox {
					id: adminChannelSelect
					objectName: "stonksAdminChannel"
					Layout.fillWidth: true
					model: root.adminChannels()
					textRole: "label"
					valueRole: "textChannelId"
					currentIndex: root.adminChannelIndex()
					onActivated: { root.adminTextChannelId = Number(currentValue); root.adminDirty = true }
					Accessible.name: qsTr("Announcement text channel")
				}
				ModernButton {
					objectName: "stonksAdminConfigure"
					text: qsTr("Save server settings")
					onClicked: { dialogState.invokeAction("configure", root.configurePayload()); root.adminDirty = false }
				}
			}
		}
	}
	}

	Popup {
		id: confirmationPopup
		objectName: "stonksConfirmationPopup"
		parent: Overlay.overlay
		modal: true
		dim: true
		focus: true
		closePolicy: Popup.NoAutoClose
		width: parent ? Math.min(460, Math.max(300, parent.width - Theme.space5 * 2)) : 420
		x: parent ? Math.round((parent.width - width) / 2) : 0
		y: parent ? Math.round((parent.height - height) / 2) : 0
		padding: Theme.space4
		onAboutToShow: {
			if (!root.focusBeforeConfirmation) {
				const window = root.Window.window
				root.focusBeforeConfirmation = window ? window.activeFocusItem : null
			}
			if (root.modalHost && typeof root.modalHost.rememberNestedModalFocus === "function") {
				root.modalHost.rememberNestedModalFocus(root.focusBeforeConfirmation)
				root.modalHost.nestedModalOpen = true
			}
		}
		onOpened: confirmationCancelButton.forceActiveFocus(Qt.PopupFocusReason)
		onClosed: {
			const restoreTarget = root.focusBeforeConfirmation
			root.focusBeforeConfirmation = null
			if (root.modalHost && typeof root.modalHost.restoreNestedModalFocus === "function") {
				root.modalHost.nestedModalOpen = false
				root.modalHost.restoreNestedModalFocus(restoreTarget)
				return
			}
			Qt.callLater(function() {
				if (!root.visible || root.confirmationVisible)
					return
				if (restoreTarget && restoreTarget.forceActiveFocus
						&& restoreTarget.visible !== false && restoreTarget.enabled !== false) {
					restoreTarget.forceActiveFocus(Qt.PopupFocusReason)
				} else if (stonksRefresh.visible && stonksRefresh.enabled) {
					stonksRefresh.forceActiveFocus(Qt.PopupFocusReason)
				}
			})
		}
		Overlay.modal: Rectangle { color: Theme.modalScrim }
		background: Rectangle {
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
			border.width: 1
			radius: Theme.shellRadius
		}
		contentItem: ColumnLayout {
			Accessible.role: Accessible.Dialog
			Accessible.name: root.pendingTitle || qsTr("Confirm action")
			spacing: Theme.space3
			Keys.priority: Keys.BeforeItem
			Keys.onEscapePressed: event => {
				root.cancelDestructiveAction()
				event.accepted = true
			}
			Label {
				objectName: "stonksConfirmationTitle"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.pendingTitle || qsTr("Confirm action")
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				wrapMode: Text.Wrap
			}
			Label {
				objectName: "stonksConfirmationMessage"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.pendingMessage
				color: Theme.textMain
				wrapMode: Text.Wrap
			}
			RowLayout {
				Layout.fillWidth: true
				Item { Layout.fillWidth: true }
				ModernButton {
					id: confirmationCancelButton
					objectName: "stonksConfirmCancel"
					text: qsTr("Cancel")
					tone: "secondary"
					activeFocusOnTab: true
					KeyNavigation.tab: confirmationActionButton
					KeyNavigation.backtab: confirmationActionButton
					Keys.priority: Keys.BeforeItem
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
							confirmationActionButton.forceActiveFocus(Qt.TabFocusReason)
							event.accepted = true
						}
					}
					onClicked: root.cancelDestructiveAction()
				}
				ModernButton {
					id: confirmationActionButton
					objectName: "stonksConfirmAction"
					text: qsTr("Confirm")
					tone: "danger"
					highlighted: true
					activeFocusOnTab: true
					KeyNavigation.tab: confirmationCancelButton
					KeyNavigation.backtab: confirmationCancelButton
					Keys.priority: Keys.BeforeItem
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
							confirmationCancelButton.forceActiveFocus(Qt.TabFocusReason)
							event.accepted = true
						}
					}
					onClicked: root.confirmDestructiveAction()
				}
			}
		}
	}

	ModalAccessibilityBarrier {
		id: confirmationAccessibilityBarrier
		objectName: "stonksConfirmationAccessibilityBarrier"
		active: root.confirmationVisible
		targets: [ backgroundContent ]
	}
}
