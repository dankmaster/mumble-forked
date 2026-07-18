import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var field
	property var asyncOperationController: null
	// Optional viewport owned by the containing dialog. Settings is the single
	// scroll owner, but Qt Quick accessibility does not inherit its visual clip.
	// Plugin cards outside that viewport therefore withdraw their semantic
	// subtree until scrolling brings the complete card into view.
	property Item accessibilityViewport: null
	// Deterministic screenshot fixtures keep the operation state visible while
	// freezing its spinner and progress motion. Interactive product surfaces
	// retain animation by default.
	property bool animationsEnabled: true
	readonly property var rows: field && field.rows ? field.rows : []
	readonly property int rowCount: rows.length || 0
	readonly property bool compactLayout: width < 620
	readonly property bool loading: !!(field && field.loading)
	readonly property string errorText: field && field.error ? String(field.error) : ""
	readonly property var operation: field && field.operation ? field.operation : ({})
	readonly property string operationId: String(operation.id || "").trim()
	readonly property string operationStatus: String(operation.status || "").toLowerCase()
	readonly property bool operationVisible: operationStatus.length > 0
	readonly property bool operationTerminal: [ "succeeded", "partial", "failed", "cancelled" ]
		.indexOf(operationStatus) >= 0
	readonly property bool operationRunning: operationVisible && !operationTerminal
	readonly property bool operationCancellable: operationRunning && !!operation.cancellable
	readonly property string operationTitleText: String(operation.title || operation.label
		|| qsTr("Plugin operation in progress"))
	readonly property string operationSubtitleText: String(operation.subtitle || operation.message || "")
	readonly property var operationResults: normalizedOperationResults(
		operation && operation.itemResults ? operation.itemResults : [])
	// Bind the count to operation itself. A var property holding a JavaScript
	// array does not emit change notifications for its nested length in every
	// QML engine path, which could leave a derived `operationResults.length`
	// binding at its initial zero even though the result array had updated.
	readonly property int operationResultCount: normalizedOperationResults(
		operation && operation.itemResults ? operation.itemResults : []).length
	readonly property bool showLoadingState: loading && rowCount === 0 && errorText.length === 0
	readonly property bool showEmptyState: !loading && rowCount === 0 && errorText.length === 0
	readonly property bool showErrorState: errorText.length > 0
	readonly property real operationProgress: {
		const raw = Number(operation.progress)
		if (!Number.isFinite(raw))
			return -1
		return Math.max(0, Math.min(1, raw > 1 ? raw / 100 : raw))
	}
	property var lastOwnedFocusItem: null
	property var previousOwnedFocusItem: null
	property var operationFocusOrigin: null
	property bool cancelFocusHandoffEligible: false
	property bool cancelFocusHandoffPending: false
	property int cancelFocusHandoffGeneration: 0
	function ownsItem(item) {
		let candidate = item
		while (candidate) {
			if (candidate === root)
				return true
			candidate = candidate.parent
		}
		return false
	}
	function rememberOwnedFocus() {
		const window = root.Window.window
		const focused = window ? window.activeFocusItem : null
		if (root.ownsItem(focused) && focused !== root.lastOwnedFocusItem) {
			root.previousOwnedFocusItem = root.lastOwnedFocusItem
			root.lastOwnedFocusItem = focused
		}
	}
	function disabledOperationFocusOrigin() {
		const candidates = [ root.operationFocusOrigin, root.lastOwnedFocusItem,
			root.previousOwnedFocusItem ]
		for (let index = 0; index < candidates.length; ++index) {
			const candidate = candidates[index]
			if (candidate && root.ownsItem(candidate) && candidate.enabled === false)
				return candidate
		}
		return null
	}
	function completeCancelFocusHandoff(generation, attempt) {
		if (generation !== root.cancelFocusHandoffGeneration
				|| !root.cancelFocusHandoffPending || !root.operationCancellable
				|| !root.cancelFocusHandoffEligible)
			return
		const origin = root.disabledOperationFocusOrigin()
		if (origin && operationCancelLoader.status === Loader.Ready
				&& operationCancelLoader.item && operationCancelLoader.item.enabled) {
			// Clear pending before forceActiveFocus(): the window focus signal is
			// synchronous and must not schedule a second handoff to the same button.
			root.cancelFocusHandoffPending = false
			operationCancelLoader.item.forceActiveFocus(Qt.OtherFocusReason)
			return
		}
		// operationRunning, the initiating action's enabled binding and the async
		// Loader settle on separate QML turns. Retry only this local handoff for a
		// bounded number of turns; a real external focus destination always wins.
		if (attempt >= 4) {
			root.cancelFocusHandoffPending = false
			return
		}
		Qt.callLater(function() {
			root.completeCancelFocusHandoff(generation, attempt + 1)
		})
	}
	function scheduleCancelFocusHandoff() {
		const generation = ++root.cancelFocusHandoffGeneration
		root.cancelFocusHandoffPending = root.operationCancellable
			&& root.cancelFocusHandoffEligible
		if (!root.cancelFocusHandoffPending)
			return
		Qt.callLater(function() {
			root.completeCancelFocusHandoff(generation, 0)
		})
	}
	onOperationRunningChanged: {
		if (operationRunning) {
			const window = root.Window.window
			const focused = window ? window.activeFocusItem : null
			cancelFocusHandoffEligible = ownsItem(focused)
			operationFocusOrigin = cancelFocusHandoffEligible ? focused : null
		} else {
			cancelFocusHandoffEligible = false
			operationFocusOrigin = null
		}
	}
	onOperationCancellableChanged: scheduleCancelFocusHandoff()
	Connections {
		target: root.Window.window
		function onActiveFocusItemChanged() { root.rememberOwnedFocus() }
	}
	function cancelCurrentOperation() {
		if (root.asyncOperationController && root.operationId.length > 0
				&& root.asyncOperationController.cancel)
			root.asyncOperationController.cancel(root.operationId)
	}
	function normalizedOperationResults(source) {
		if (!source)
			return []
		if (Array.isArray(source))
			return source
		let count = Number(source.length)
		if (!Number.isFinite(count))
			count = Number(typeof source.count === "function" ? source.count() : source.count)
		if (!Number.isFinite(count) || count < 0)
			return []
		const output = []
		for (let index = 0; index < Math.floor(count); ++index)
			output.push(typeof source.get === "function" ? source.get(index) : source[index])
		return output
	}
	function operationResultName(result) {
		const item = result || {}
		const prefix = item.cancelled ? qsTr("Cancelled") : item.success ? qsTr("Updated") : qsTr("Failed")
		const name = String(item.name || item.itemId || qsTr("Plugin"))
		const message = String(item.message || item.errorCode || "")
		return prefix + ": " + name + (message.length > 0 ? " — " + message : "")
	}
	function itemFitsViewport(item, viewport) {
		if (!item || !viewport || item.width <= 0 || item.height <= 0
				|| viewport.width <= 0 || viewport.height <= 0)
			return false
		try {
			const topLeft = item.mapToItem(viewport, 0, 0)
			// QAccessible exposes the delegate's complete bounding rectangle; it
			// does not clamp a partially clipped row to either Flickable viewport.
			// Publish a plugin row only when its whole semantic surface is visible.
			// This also prevents a few clipped pixels from becoming a keyboard or
			// screen-reader target before the user scrolls the card into view.
			const tolerance = 0.5
			return topLeft.x >= -tolerance && topLeft.y >= -tolerance
				&& topLeft.x + item.width <= viewport.width + tolerance
				&& topLeft.y + item.height <= viewport.height + tolerance
		} catch (error) {
			return false
		}
	}
	function viewportContentY(viewport) {
		try {
			return viewport && viewport.contentItem
				&& viewport.contentItem.contentY !== undefined
				? Number(viewport.contentItem.contentY) : 0
		} catch (error) {
			return 0
		}
	}

	width: parent ? parent.width : 0
	spacing: Theme.space4
	Accessible.role: Accessible.Pane
	Accessible.name: field && field.label ? String(field.label) : qsTr("Installed plugins")

	ColumnLayout {
		id: pluginIntroductionSection
		Layout.fillWidth: true
		spacing: Theme.space1

		RowLayout {
			id: pluginIntroduction
			objectName: "pluginIntroduction"
			readonly property bool accessibilityExposed: {
				if (!root.accessibilityViewport)
					return true
				const viewport = root.accessibilityViewport
				// mapToItem() does not establish notifying dependencies. Track both the
				// dialog scroll position and this header's complete settled geometry so a
				// sticky Settings heading cannot leave the clipped plugin heading in UIA.
				const geometryDependency = Number(viewport.x || 0) + Number(viewport.y || 0)
					+ Number(viewport.width || 0) + Number(viewport.height || 0)
					+ root.viewportContentY(viewport)
					+ Number(root.x || 0) + Number(root.y || 0)
					+ Number(pluginIntroductionSection.x || 0)
					+ Number(pluginIntroductionSection.y || 0)
					+ Number(pluginIntroduction.x || 0) + Number(pluginIntroduction.y || 0)
					+ Number(pluginIntroduction.width || 0) + Number(pluginIntroduction.height || 0)
				if (!Number.isFinite(geometryDependency))
					return false
				return root.itemFitsViewport(pluginIntroduction, viewport)
			}
			Layout.fillWidth: true
			spacing: Theme.space3

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2

				Label {
					id: pluginIntroductionHeading
					objectName: "pluginIntroductionHeading"
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.field && root.field.label
						? String(root.field.label) : qsTr("Installed plugins")
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
					Accessible.role: Accessible.Heading
					Accessible.ignored: !pluginIntroduction.accessibilityExposed
				}

				Label {
					id: pluginIntroductionSummary
					objectName: "pluginIntroductionSummary"
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.loading ? qsTr("Refreshing plugin information…")
						: root.rowCount === 1 ? qsTr("1 plugin available")
						: qsTr("%1 plugins available").arg(root.rowCount)
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					Accessible.ignored: !pluginIntroduction.accessibilityExposed
				}
			}

			Rectangle {
				visible: !root.compactLayout && root.rowCount > 0
				implicitWidth: pluginCountLabel.implicitWidth + Theme.space3 * 2
				implicitHeight: 28
				radius: height / 2
				color: Theme.accentSubtle
				border.color: Theme.accent

				Label {
					id: pluginCountLabel
					textFormat: Text.PlainText
					anchors.centerIn: parent
					text: String(root.rowCount)
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
					Accessible.ignored: !pluginIntroduction.accessibilityExposed
				}
			}
		}

		ModalAccessibilityBarrier {
			id: pluginIntroductionAccessibilityBarrier
			objectName: "pluginIntroductionAccessibilityBarrier"
			active: !pluginIntroduction.accessibilityExposed
			targets: [ pluginIntroduction ]
		}

		Flow {
			id: toolbar
			objectName: "pluginToolbar"
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: Theme.space2

			ModernButton {
				objectName: "pluginInstallButton"
				enabled: !root.operationRunning
				text: qsTr("Install plugin…")
				highlighted: true
				tone: "accent"
				onClicked: dialogState.invokeAction("plugins.install", {})
			}
			ModernButton {
				objectName: "pluginRescanButton"
				enabled: !root.operationRunning
				text: qsTr("Rescan")
				onClicked: dialogState.invokeAction("plugins.rescan", {})
			}
			ModernButton {
				objectName: "pluginCheckUpdatesButton"
				enabled: !root.operationRunning
				text: qsTr("Check for updates")
				onClicked: dialogState.invokeAction("plugins.checkUpdates", {})
			}
		}
	}

	Rectangle {
		id: operationCard
		objectName: "pluginOperationCard"
		Layout.fillWidth: true
		implicitHeight: operationContent.implicitHeight + Theme.space4 * 2
		visible: root.operationVisible
		radius: Theme.innerRadius
		color: root.operationStatus === "partial" ? Theme.withAlpha(Theme.warning, 0.08)
			: root.operationStatus === "failed" ? Theme.withAlpha(Theme.danger, 0.08) : Theme.surfaceRaised
		border.color: root.operationStatus === "partial" ? Theme.warning
			: root.operationStatus === "failed" ? Theme.danger : Theme.surfaceBorder
		Accessible.role: Accessible.StatusBar
		Accessible.name: root.operationTitleText + (root.operationSubtitleText.length > 0
			? ": " + root.operationSubtitleText : "")
		Accessible.description: root.operationTerminal
			? qsTr("Plugin operation finished with %1 item results").arg(root.operationResultCount)
			: qsTr("Plugin operation in progress")

		ColumnLayout {
			id: operationContent
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			anchors.margins: Theme.space4
			spacing: Theme.space2

			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2

				ModernBusyIndicator {
					objectName: "pluginOperationBusyIndicator"
					running: root.operationRunning
					animated: root.animationsEnabled
					// The operation card and its exact progress remain visible in
					// deterministic fixtures. Hide only the rotational affordance: an
					// animator can retain a scene-graph phase even when it is paused.
					visible: running && root.animationsEnabled
					implicitWidth: 22
					implicitHeight: 22
					Accessible.ignored: !visible
					Accessible.name: qsTr("Plugin operation in progress")
				}

				Label {
					id: operationTitle
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.operationTitleText
					color: Theme.textStrong
					font.weight: Font.DemiBold
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				Label {
					textFormat: Text.PlainText
					visible: root.operationProgress >= 0
					text: qsTr("%1%").arg(Math.round(root.operationProgress * 100))
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					Accessible.ignored: true
				}

				Loader {
					id: operationCancelLoader
					objectName: "pluginOperationCancelLoader"
					active: root.operationCancellable
					visible: active && status === Loader.Ready
					sourceComponent: ModernButton {
						objectName: "pluginOperationCancelButton"
						dense: true
						text: qsTr("Cancel")
						Accessible.name: qsTr("Cancel plugin update")
						Accessible.description: root.operationSubtitleText
						onClicked: root.cancelCurrentOperation()
					}
					onLoaded: root.scheduleCancelFocusHandoff()
				}
			}

			Label {
				id: operationSubtitle
				objectName: "pluginOperationSubtitle"
				Layout.fillWidth: true
				visible: root.operationSubtitleText.length > 0
				textFormat: Text.PlainText
				text: root.operationSubtitleText
				color: Theme.textMain
				font.pixelSize: Theme.fontBody
				wrapMode: Text.WordWrap
				Accessible.ignored: true
			}

			ModernProgressBar {
				id: operationProgress
				objectName: "pluginOperationProgress"
				Layout.fillWidth: true
				from: 0
				to: 1
				value: root.operationProgress < 0 ? 0 : root.operationProgress
				indeterminate: root.operationProgress < 0
				animated: root.animationsEnabled
				Accessible.name: qsTr("Plugin update progress")
				Accessible.description: root.operationProgress < 0
					? qsTr("Progress is not yet available")
					: qsTr("%1 percent complete").arg(Math.round(root.operationProgress * 100))
			}

			Label {
				id: operationSummary
				objectName: "pluginOperationSummary"
				Layout.fillWidth: true
				visible: root.operationTerminal
				textFormat: Text.PlainText
				text: qsTr("%1 succeeded · %2 failed · %3 cancelled")
					.arg(Number(root.operation.successfulItems) || 0)
					.arg(Number(root.operation.failedItems) || 0)
					.arg(Number(root.operation.cancelledItems) || 0)
				color: root.operationStatus === "partial" ? Theme.warning
					: root.operationStatus === "failed" ? Theme.danger : Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
				wrapMode: Text.WordWrap
				Accessible.ignored: true
			}

			ColumnLayout {
				id: operationResults
				objectName: "pluginOperationResults"
				Layout.fillWidth: true
				visible: root.operationTerminal && root.operationResultCount > 0
				spacing: Theme.space1

				Repeater {
					// Item results are bounded by the async operation DTO. Keep their
					// lifecycle tied to that typed model, not to effective visibility:
					// the card may be hidden for one layout frame while a dialog page or
					// compact breakpoint changes, which must not discard terminal state.
					// Read operation directly for the same reason as
					// operationResultCount: do not depend on nested change
					// notification from a var-backed JavaScript array.
					model: root.normalizedOperationResults(root.operation && root.operation.itemResults
						? root.operation.itemResults : [])
					delegate: Rectangle {
						required property var modelData
						readonly property string resultId: String(modelData.itemId || modelData.pluginId || index)
						objectName: "pluginOperationResult_" + resultId
						Layout.fillWidth: true
						implicitHeight: resultContent.implicitHeight + Theme.space2 * 2
						radius: Theme.innerRadius
						color: modelData.success ? Theme.withAlpha(Theme.success, 0.08)
							: modelData.cancelled ? Theme.panel : Theme.withAlpha(Theme.danger, 0.09)
						border.color: modelData.success ? Theme.withAlpha(Theme.success, 0.35)
							: modelData.cancelled ? Theme.divider : Theme.withAlpha(Theme.danger, 0.45)
						Accessible.role: Accessible.ListItem
						Accessible.name: root.operationResultName(modelData)
						Accessible.description: String(modelData.errorCode || "").length > 0
							? qsTr("Error code: %1").arg(String(modelData.errorCode)) : ""

						RowLayout {
							id: resultContent
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.top: parent.top
							anchors.margins: Theme.space2
							spacing: Theme.space2
							Rectangle {
								Layout.preferredWidth: 8
								Layout.preferredHeight: 8
								radius: 4
								color: modelData.success ? Theme.success
									: modelData.cancelled ? Theme.textMuted : Theme.danger
								Accessible.ignored: true
							}
							ColumnLayout {
								Layout.fillWidth: true
								spacing: 1
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: String(modelData.name || modelData.itemId || qsTr("Plugin"))
									color: Theme.textStrong
									font.pixelSize: Theme.fontLabel
									font.weight: Font.DemiBold
									elide: Text.ElideRight
									Accessible.ignored: true
								}
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: String(modelData.message || modelData.errorCode || "")
									visible: text.length > 0
									color: modelData.success ? Theme.textMuted : Theme.danger
									font.pixelSize: Theme.fontCaption
									wrapMode: Text.WordWrap
									Accessible.ignored: true
								}
							}
						}
					}
				}
			}
		}
	}

	Rectangle {
		id: errorCard
		objectName: "pluginErrorCard"
		Layout.fillWidth: true
		implicitHeight: errorContent.implicitHeight + Theme.space4 * 2
		visible: root.showErrorState
		radius: Theme.innerRadius
		color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
		border.color: Theme.danger
		Accessible.role: Accessible.AlertMessage
		Accessible.name: errorLabel.text

		RowLayout {
			id: errorContent
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			anchors.margins: Theme.space4
			spacing: Theme.space3

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2
				Label {
					textFormat: Text.PlainText
					text: qsTr("Plugin information could not be loaded")
					color: Theme.textStrong
					font.weight: Font.DemiBold
				}
				Label {
					id: errorLabel
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.errorText
					color: Theme.textMain
					wrapMode: Text.WordWrap
				}
			}

			ModernButton {
				objectName: "pluginRetryButton"
				text: qsTr("Try again")
				onClicked: dialogState.invokeAction("plugins.rescan", {})
			}
		}
	}

	Item {
		id: loadingState
		objectName: "pluginLoadingState"
		Layout.fillWidth: true
		Layout.preferredHeight: 150
		visible: root.showLoadingState
		Accessible.role: Accessible.StatusBar
		Accessible.name: qsTr("Loading installed plugins")

		Column {
			anchors.centerIn: parent
			spacing: Theme.space2
			ModernBusyIndicator {
				objectName: "pluginLoadingBusyIndicator"
				anchors.horizontalCenter: parent.horizontalCenter
				running: root.showLoadingState
				animated: root.animationsEnabled
				visible: running && root.animationsEnabled
				Accessible.name: qsTr("Loading installed plugins")
			}
			Label {
				textFormat: Text.PlainText
				anchors.horizontalCenter: parent.horizontalCenter
				text: qsTr("Loading installed plugins…")
				color: Theme.textMuted
			}
		}
	}

	Rectangle {
		id: emptyState
		objectName: "pluginEmptyState"
		Layout.fillWidth: true
		implicitHeight: emptyContent.implicitHeight + Theme.space5 * 2
		visible: root.showEmptyState
		radius: Theme.innerRadius
		color: Theme.panel
		border.color: Theme.divider
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("No plugins installed")

		ColumnLayout {
			id: emptyContent
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space5 * 2, 420)
			spacing: Theme.space2

			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: qsTr("No plugins found")
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: qsTr("Install a Mumble plugin or rescan the configured plugin folders.")
				color: Theme.textMuted
				wrapMode: Text.WordWrap
				horizontalAlignment: Text.AlignHCenter
			}
			ModernButton {
				Layout.alignment: Qt.AlignHCenter
				enabled: !root.operationRunning
				text: qsTr("Install plugin…")
				highlighted: true
				tone: "accent"
				onClicked: dialogState.invokeAction("plugins.install", {})
			}
		}
	}

	Column {
		id: pluginList
		objectName: "pluginList"
		Layout.fillWidth: true
		Layout.preferredWidth: root.width
		visible: root.rowCount > 0
		spacing: Theme.space3
		property int count: root.rowCount
		function itemAtIndex(row) { return pluginRepeater.itemAt(row) }
		function focusRelative(row, delta) {
			const next = pluginRepeater.itemAt(Math.max(0,
				Math.min(root.rowCount - 1, row + delta)))
			if (next)
				next.forceActiveFocus(Qt.TabFocusReason)
		}
		Accessible.role: Accessible.List
		Accessible.name: qsTr("Installed plugins")

		// The Settings dialog already owns the scrolling viewport. A second nested
		// Flickable made wheel/keyboard navigation ambiguous and caused Windows UIA
		// to publish unclipped ListView delegates. Plugin counts are small and the
		// outer ScrollView now provides one predictable navigation surface.
		Repeater {
			id: pluginRepeater
			model: root.rows

			delegate: Item {
			id: pluginCard
			required property var modelData
			readonly property bool cardCompact: width < 560
			readonly property string pluginName: modelData.name || qsTr("Unnamed plugin")
			readonly property string pluginId: String(modelData.id === undefined ? index : modelData.id)
			readonly property bool fitsListViewport: true
			readonly property bool fitsOuterViewport: {
				if (!root.accessibilityViewport)
					return true
				const viewport = root.accessibilityViewport
				// mapToItem() is not a notifying binding dependency. Read every local
				// geometry input that can move or resize a card, plus the owning
				// Flickable offset and viewport size, before sampling the mapped bounds.
				// Without these reads a card could remain semantically hidden after the
				// Settings layout had visibly settled.
				const geometryDependency = Number(viewport.x || 0) + Number(viewport.y || 0)
					+ Number(viewport.width || 0) + Number(viewport.height || 0)
					+ root.viewportContentY(viewport)
					+ Number(root.x || 0) + Number(root.y || 0)
					+ Number(pluginList.x || 0) + Number(pluginList.y || 0)
					+ Number(pluginCard.x || 0) + Number(pluginCard.y || 0)
					+ Number(pluginCard.width || 0) + Number(pluginCard.height || 0)
				if (!Number.isFinite(geometryDependency))
					return false
				return root.itemFitsViewport(pluginCard, viewport)
			}
			readonly property bool accessibilityExposed: fitsOuterViewport

			objectName: "pluginCard_" + pluginId
			width: pluginList.width > 0 ? pluginList.width : root.width
			height: pluginColumn.implicitHeight + Theme.space4 * 2
			activeFocusOnTab: true
			// Windows' ListView provider may publish the delegate itself even when a
			// dynamic ignored binding is true. Keep the layout delegate permanently
			// non-semantic and let the child surface own the ListItem contract.
			Accessible.ignored: true
			Keys.onUpPressed: pluginList.focusRelative(index, -1)
			Keys.onDownPressed: pluginList.focusRelative(index, 1)

			Rectangle {
				id: pluginCardSurface
				objectName: "pluginSemanticCard_" + pluginCard.pluginId
				anchors.fill: parent
				radius: Theme.innerRadius
				color: Theme.surfaceRaised
				border.color: pluginCard.activeFocus ? Theme.focus : Theme.surfaceBorder
				border.width: pluginCard.activeFocus ? Theme.focusRingWidth : 1
				Accessible.role: Accessible.ListItem
				Accessible.name: pluginCard.pluginName
				Accessible.description: modelData.loaded
					? qsTr("Loaded plugin") : qsTr("Plugin is not loaded")
				Accessible.ignored: false

				ColumnLayout {
					id: pluginColumn
					objectName: "pluginContent_" + pluginCard.pluginId
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.margins: Theme.space4
					spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space3

					Rectangle {
						implicitWidth: 38
						implicitHeight: 38
						radius: 12
						color: modelData.loaded ? Theme.accentSubtle : Theme.panel
						border.color: modelData.loaded ? Theme.accent : Theme.divider

						Label {
							textFormat: Text.PlainText
							anchors.centerIn: parent
							text: pluginCard.pluginName.length > 0
								? pluginCard.pluginName.charAt(0).toUpperCase() : "P"
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2

						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: pluginCard.pluginName
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
							elide: Text.ElideRight
						}
						Label {
							objectName: "pluginMetadata_" + pluginCard.pluginId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: [modelData.author || "", modelData.version || ""]
								.filter(function(part) { return String(part).length > 0 }).join(" · ")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							visible: text.length > 0
						}
					}

					Rectangle {
						implicitWidth: loadStateLabel.implicitWidth + Theme.space3 * 2
						implicitHeight: 26
						radius: height / 2
						color: modelData.loaded ? Theme.accentSubtle : Theme.panel
						border.color: modelData.loaded ? Theme.accent : Theme.divider

						Label {
							id: loadStateLabel
							textFormat: Text.PlainText
							anchors.centerIn: parent
							text: modelData.loaded ? qsTr("Loaded") : qsTr("Not loaded")
							color: modelData.loaded ? Theme.accentHover : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
					}
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: modelData.description || qsTr("No description supplied by this plugin.")
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					wrapMode: Text.WordWrap
					maximumLineCount: pluginCard.cardCompact ? 3 : 2
					elide: Text.ElideRight
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: modelData.path || ""
					visible: text.length > 0
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideMiddle
					Accessible.name: qsTr("Plugin path: %1").arg(text)
				}

				Flow {
					id: permissionFlow
					objectName: "pluginPermissionFlow_" + pluginCard.pluginId
					Layout.fillWidth: true
					// Compact rows need a stable tap/focus lane even when the Basic
					// control style reports a shorter checkbox implicit height in the
					// off-screen test backend.
					Layout.minimumHeight: root.compactLayout ? 36 : childrenRect.height
					Layout.preferredHeight: Math.max(childrenRect.height,
						Layout.minimumHeight)
					spacing: Theme.space2

					ModernCheckBox {
						objectName: "pluginEnable_" + pluginCard.pluginId
						dense: true
						text: qsTr("Enabled")
						enabled: !root.operationRunning
						checked: !!modelData.enabled
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "enabled", "value": checked })
					}
					ModernCheckBox {
						objectName: "pluginPositional_" + pluginCard.pluginId
						dense: true
						text: qsTr("Positional audio")
						visible: !!modelData.positionalAvailable
						enabled: !root.operationRunning
						checked: !!modelData.positionalEnabled
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "positional", "value": checked })
					}
					ModernCheckBox {
						objectName: "pluginKeyboard_" + pluginCard.pluginId
						dense: true
						text: qsTr("Keyboard monitoring")
						enabled: !root.operationRunning
						checked: !!modelData.keyboardMonitoringAllowed
						Accessible.description: qsTr("Allow this plugin to observe keyboard input")
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "keyboard", "value": checked })
					}
				}

				Flow {
					id: actionFlow
					objectName: "pluginActionFlow_" + pluginCard.pluginId
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Theme.space2

					ModernButton {
						objectName: "pluginLoad_" + pluginCard.pluginId
						dense: true
						text: qsTr("Load")
						visible: !modelData.loaded
						enabled: !root.operationRunning
						Accessible.name: qsTr("Load %1").arg(pluginCard.pluginName)
						onClicked: dialogState.invokeAction("plugins.load", { "pluginId": modelData.id })
					}
					ModernButton {
						objectName: "pluginConfigure_" + pluginCard.pluginId
						dense: true
						text: qsTr("Configure")
						visible: !!modelData.loaded && !!modelData.canConfigure
						enabled: !root.operationRunning
						onClicked: dialogState.invokeAction("plugins.configure", { "pluginId": modelData.id })
					}
					ModernButton {
						objectName: "pluginAbout_" + pluginCard.pluginId
						dense: true
						text: qsTr("About")
						visible: !!modelData.loaded && !!modelData.canShowAbout
						enabled: !root.operationRunning
						onClicked: dialogState.invokeAction("plugins.about", { "pluginId": modelData.id })
					}
					ModernButton {
						objectName: "pluginUnload_" + pluginCard.pluginId
						dense: true
						text: qsTr("Unload")
						visible: !!modelData.loaded && !modelData.builtIn
						enabled: !root.operationRunning
						onClicked: dialogState.invokeAction("plugins.unload", { "pluginId": modelData.id })
					}
				}
				}
			}

			ModalAccessibilityBarrier {
				// Keep partially clipped cards visually intact while withdrawing the
				// complete semantic subtree. Accessible.ignored on only the card would
				// promote its controls into UIA, so the shared barrier owns every
				// descendant binding until the outer Settings viewport reveals it.
				active: !pluginCard.accessibilityExposed
				targets: [ pluginCardSurface ]
			}
			}
		}
	}
}
