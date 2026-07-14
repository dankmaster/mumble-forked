import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: root

	required property string stableId
	required property string title
	required property string subtitle
	required property string status
	required property var payload

	property bool narrowLayout: false
	property int maximumHeight: 420
	property var itemResultPageProvider: null
	property int expansionOverride: -1
	property bool resultDetailsExpanded: false
	property int resultPageIndex: 0
	property int successfulAutoDismissDelay: 8000

	readonly property bool notification: String(payload.tone || "").length > 0
	readonly property bool terminal: status === "succeeded" || status === "partial"
		|| status === "failed" || status === "cancelled"
	readonly property bool userInteracting: operationHover.hovered
		|| ownsItem(root.Window.activeFocusItem)
	readonly property bool successfulAutoDismissEligible: status === "succeeded"
		&& !notification && expansionOverride < 0 && !resultDetailsExpanded
	readonly property bool expanded: expansionOverride < 0
		? (!narrowLayout && (!terminal || status === "failed" || status === "partial"))
		: expansionOverride === 1
	readonly property int resultPageSize: 32
	readonly property int resultCount: Math.max(0, Number(payload.itemResultCount) || 0)
	readonly property int resultPageCount: Math.max(1, Math.ceil(resultCount / resultPageSize))
	readonly property real detailAvailableHeight: Math.max(84,
		maximumHeight - operationHeader.implicitHeight - Theme.space3 * 2 - Theme.space2)
	readonly property var failedResults: {
		const revision = Math.max(0, Number(payload.itemResultRevision) || 0)
		return terminal && revision >= 0 ? itemResultPage(0, 3, true) : []
	}
	readonly property var resultPageItems: {
		const revision = Math.max(0, Number(payload.itemResultRevision) || 0)
		return resultDetailsExpanded && terminal && revision >= 0
			? itemResultPage(resultPageIndex * resultPageSize, resultPageSize, false) : []
	}
	readonly property Item visualFixtureFocusTarget: dismissOperationButton.visible
		? dismissOperationButton : expandOperationButton

	signal cancelRequested(string operationId)
	signal dismissRequested(string operationId)

	function ownsItem(item) {
		let current = item
		while (current) {
			if (current === root)
				return true
			current = current.parent
		}
		return false
	}

	function itemResultPage(offset, limit, unsuccessfulOnly) {
		if (!itemResultPageProvider)
			return []
		return itemResultPageProvider(stableId, offset, limit, unsuccessfulOnly) || []
	}

	function toggleExpanded() {
		expansionOverride = expanded ? 0 : 1
	}

	onResultPageCountChanged: resultPageIndex = Math.min(resultPageIndex, resultPageCount - 1)

	width: 380
	implicitHeight: Theme.space3 * 2 + operationHeader.implicitHeight
		+ (expanded ? Theme.space2 + Math.min(operationDetails.implicitHeight, detailAvailableHeight) : 0)
	height: Math.min(maximumHeight, implicitHeight)
	radius: Theme.innerRadius
	color: Theme.surfaceRaised
	border.color: status === "failed" ? Theme.danger
		: status === "partial" ? Theme.warning
		: status === "cancelled" ? Theme.textMuted : Theme.surfaceBorder
	border.width: activeFocus ? Theme.focusRingWidth : 1
	clip: true
	Accessible.role: Accessible.AlertMessage
	Accessible.name: title + (subtitle.length > 0 ? ": " + subtitle : "")
	Accessible.description: expanded ? qsTr("Operation details expanded")
		: qsTr("Operation details collapsed")

	HoverHandler {
		id: operationHover
	}

	Timer {
		id: successfulAutoDismissTimer
		objectName: "successfulOperationAutoDismissTimer"
		interval: Math.max(1, root.successfulAutoDismissDelay)
		repeat: false
		running: root.visible && root.successfulAutoDismissEligible && !root.userInteracting
		onTriggered: {
			if (root.successfulAutoDismissEligible && !root.userInteracting)
				root.dismissRequested(root.stableId)
		}
	}

	Behavior on height {
		NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
	}

	ColumnLayout {
		anchors.fill: parent
		anchors.margins: Theme.space3
		spacing: Theme.space2

		RowLayout {
			id: operationHeader
			Layout.fillWidth: true
			spacing: Theme.space1

			Rectangle {
				Layout.preferredWidth: 8
				Layout.preferredHeight: 8
				radius: 4
				color: root.status === "failed" ? Theme.danger
					: root.status === "partial" ? Theme.warning
					: root.status === "succeeded" ? Theme.success
					: root.status === "cancelled" ? Theme.textMuted : Theme.accent
				Accessible.ignored: true
			}

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 1
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.title
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
					elide: Text.ElideRight
				}
				Label {
					Layout.fillWidth: true
					visible: !root.expanded
					textFormat: Text.PlainText
					text: root.subtitle.length > 0 ? root.subtitle : root.statusLabel
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideRight
				}
			}

			ModernButton {
				id: cancelOperationButton
				objectName: "operationCancelButton"
				visible: root.status === "running" && !!root.payload.cancellable
				dense: true
				text: qsTr("Cancel")
				Accessible.name: qsTr("Cancel %1").arg(root.title)
				onClicked: root.cancelRequested(root.stableId)
			}

			ModernButton {
				id: dismissOperationButton
				objectName: "visualFixtureDismissOperation"
				visible: root.terminal
				dense: true
				text: qsTr("Dismiss")
				Accessible.name: qsTr("Dismiss %1").arg(root.title)
				Accessible.focusable: true
				Accessible.focused: activeFocus
				onClicked: root.dismissRequested(root.stableId)
			}

			ModernIconButton {
				id: expandOperationButton
				objectName: "operationExpandButton"
				dense: true
				iconName: root.expanded ? "chevron-up" : "chevron-down"
				text: root.expanded ? qsTr("Collapse") : qsTr("Expand")
				Accessible.name: root.expanded ? qsTr("Collapse %1").arg(root.title)
					: qsTr("Expand %1").arg(root.title)
				Accessible.description: root.expanded ? qsTr("Hide operation details")
					: qsTr("Show operation details")
				onClicked: root.toggleExpanded()
			}
		}

		ScrollView {
			id: operationDetails
			objectName: "operationDetailsScroll"
			Layout.fillWidth: true
			Layout.preferredHeight: Math.min(operationDetailsContent.implicitHeight,
				root.detailAvailableHeight)
			visible: root.expanded
			clip: true
			contentWidth: availableWidth
			ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
			ScrollBar.vertical.policy: operationDetailsContent.implicitHeight > height
				? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

			ColumnLayout {
				id: operationDetailsContent
				width: operationDetails.availableWidth
				spacing: Theme.space2

				Label {
					Layout.fillWidth: true
					visible: root.subtitle.length > 0
					textFormat: Text.PlainText
					text: root.subtitle
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					wrapMode: Text.Wrap
				}

				RowLayout {
					Layout.fillWidth: true
					visible: !root.notification && String(root.payload.phase || "").length > 0
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: String(root.payload.phase || "").replace(/-/g, " ")
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
					}
					Label {
						visible: Number(root.payload.totalItems) > 0
						textFormat: Text.PlainText
						text: qsTr("%1 of %2").arg(Number(root.payload.completedItems) || 0)
							.arg(Number(root.payload.totalItems))
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
					}
				}

				ModernProgressBar {
					Layout.fillWidth: true
					visible: !root.notification && (root.status === "running"
						|| root.status === "cancelling" || Number(root.payload.progress) >= 0)
					indeterminate: !!root.payload.indeterminate
					from: 0
					to: 100
					value: Number(root.payload.progress) >= 0 ? Number(root.payload.progress) : 0
					Accessible.name: qsTr("Operation progress")
					Accessible.description: indeterminate ? qsTr("In progress")
						: qsTr("%1 percent").arg(value)
				}

				Label {
					Layout.fillWidth: true
					visible: !root.notification && root.terminal
						&& (Number(root.payload.successfulItems) > 0 || Number(root.payload.failedItems) > 0
							|| Number(root.payload.cancelledItems) > 0)
					textFormat: Text.PlainText
					text: qsTr("%1 succeeded · %2 failed · %3 cancelled")
						.arg(Number(root.payload.successfulItems) || 0)
						.arg(Math.max(0, Number(root.payload.failedItems) || 0))
						.arg(Number(root.payload.cancelledItems) || 0)
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
				}

				Repeater {
					model: root.resultDetailsExpanded ? [] : root.failedResults
					delegate: Label {
						required property var modelData
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: (modelData.cancelled ? qsTr("Cancelled: ") : qsTr("Failed: "))
							+ String(modelData.message || modelData.errorCode || modelData.itemId || "")
						color: modelData.cancelled ? Theme.textMuted : Theme.danger
						font.pixelSize: Theme.fontCaption
						wrapMode: Text.Wrap
					}
				}

				ModernButton {
					id: resultDetailsButton
					objectName: "operationResultDetailsButton"
					visible: !root.notification && root.terminal && root.resultCount > 0
					dense: true
					text: root.resultDetailsExpanded ? qsTr("Hide item results")
						: qsTr("Show item results (%1)").arg(root.resultCount)
					Accessible.description: qsTr("Shows a scrollable list of per-item operation results")
					onClicked: {
						root.resultDetailsExpanded = !root.resultDetailsExpanded
						if (!root.resultDetailsExpanded)
							root.resultPageIndex = 0
					}
				}

				Repeater {
					model: root.resultPageItems
					delegate: Label {
						required property var modelData
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: (modelData.success ? qsTr("Succeeded: ")
							: modelData.cancelled ? qsTr("Cancelled: ") : qsTr("Failed: "))
							+ String(modelData.message || modelData.errorCode || modelData.itemId || "")
						color: modelData.success ? Theme.success
							: modelData.cancelled ? Theme.textMuted : Theme.danger
						font.pixelSize: Theme.fontCaption
						wrapMode: Text.Wrap
					}
				}

				RowLayout {
					Layout.fillWidth: true
					visible: root.resultDetailsExpanded && root.resultPageCount > 1
					ModernButton {
						dense: true
						text: qsTr("Previous")
						enabled: root.resultPageIndex > 0
						onClicked: --root.resultPageIndex
					}
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Page %1 of %2").arg(root.resultPageIndex + 1)
							.arg(root.resultPageCount)
						color: Theme.textMuted
						horizontalAlignment: Text.AlignHCenter
						font.pixelSize: Theme.fontCaption
					}
					ModernButton {
						dense: true
						text: qsTr("Next")
						enabled: root.resultPageIndex + 1 < root.resultPageCount
						onClicked: ++root.resultPageIndex
					}
				}

				Label {
					Layout.fillWidth: true
					visible: !root.notification && root.status !== "running"
					textFormat: Text.PlainText
					text: root.statusLabel
					color: root.status === "succeeded" ? Theme.success
						: root.status === "partial" ? Theme.warning
						: root.status === "cancelled" || root.status === "cancelling"
							? Theme.textMuted : Theme.danger
					font.pixelSize: Theme.fontCaption
				}
			}
		}
	}

	readonly property string statusLabel: status === "succeeded" ? qsTr("Completed")
		: status === "partial" ? qsTr("Partially completed")
		: status === "cancelled" ? qsTr("Cancelled")
		: status === "cancelling" ? qsTr("Cancelling…")
		: status === "running" ? qsTr("In progress") : qsTr("Failed")
}
