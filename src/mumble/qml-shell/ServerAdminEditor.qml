import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
	id: root
	property var field: ({})
	property bool visualFixtureMode: false
	property Item accessibilityViewport: null
	property bool animationsEnabled: !visualFixtureMode
	readonly property string adminKind: String(field.adminKind || "users").toLowerCase()
	readonly property bool usersMode: adminKind === "users"
	property var controller: {
		if (field.controller) return field.controller
		if (typeof serverAdmin === "undefined" || !serverAdmin) return null
		return usersMode ? serverAdmin.users : serverAdmin.bans
	}
	readonly property var adminModel: controller ? controller.model : null
	readonly property string controllerState: controller ? String(controller.state || "idle") : "idle"
	readonly property bool loading: controllerState === "loading" || controllerState === "refreshing"
	readonly property bool ready: controllerState === "ready" || controllerState === "refreshing"
	readonly property bool canManage: controller && !!controller.canManage
	readonly property bool busy: controller && !!controller.busy
	readonly property bool compactLayout: width < 700
	readonly property string selectedStableId: adminModel ? String(adminModel.selectedStableId || "") : ""
	property int modelRevision: 0
	readonly property var selectedItem: {
		modelRevision
		return adminModel && selectedStableId.length > 0 ? adminModel.item(selectedStableId) : ({})
	}
	readonly property var confirmation: controller ? (controller.pendingConfirmation || {}) : ({})
	readonly property bool confirmationVisible: Object.keys(confirmation).length > 0
	readonly property Item initialFocusTarget: searchField
	property Item focusBeforeConfirmation: null
	property int draftRevision: 0
	Component {
		id: adminTextCursorDelegate
		Item {
			width: 1
			Rectangle {
				anchors.fill: parent
				visible: !root.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}

	objectName: "serverAdminEditor_" + String(field.id || adminKind)
	implicitHeight: compactLayout ? 880 : 530
	height: implicitHeight
	Accessible.role: Accessible.Pane
	Accessible.name: usersMode ? qsTr("Registered users editor") : qsTr("Ban list editor")

	function select(stableId) {
		if (!adminModel) return
		adminModel.setSelectedStableId(String(stableId || ""))
		populateDraft()
	}

	function itemFullyInsideSingleViewport(item, viewport) {
		if (!item || !viewport || !item.visible || item.width <= 0 || item.height <= 0
				|| viewport.width <= 0 || viewport.height <= 0)
			return false
		try {
			// mapToItem() is not notifying. Depend explicitly on the Flickable
			// offset and both geometries so accessibility follows each clipped
			// viewport while the user scrolls either the form or the product dialog.
			const flickable = viewport.contentItem || null
			const dependency = Number(flickable ? flickable.contentY || 0 : 0)
				+ Number(item.x || 0) + Number(item.y || 0)
				+ Number(viewport.width || 0) + Number(viewport.height || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(viewport, 0, 0)
			const tolerance = 0.5
			return point.x >= -tolerance && point.y >= -tolerance
				&& point.x + item.width <= viewport.width + tolerance
				&& point.y + item.height <= viewport.height + tolerance
		} catch (error) {
			return false
		}
	}

	function itemFullyInsideViewport(item, viewport) {
		// A nested editor can be clipped by both its own detail pane and the outer
		// product dialog. Passing the outer viewport must therefore add a boundary,
		// never replace the local one.
		if (!itemFullyInsideSingleViewport(item, viewport))
			return false
		return !accessibilityViewport || accessibilityViewport === viewport
			|| itemFullyInsideSingleViewport(item, accessibilityViewport)
	}

	function populateDraft() {
		const item = adminModel && selectedStableId.length > 0 ? adminModel.item(selectedStableId) : ({})
		if (usersMode) {
			renameField.text = String(item.name || "")
			return
		}
		banAddress.text = String(item.address || "")
		banMask.value = Number(item.mask || 32)
		banUser.text = String(item.userName || "")
		banHash.text = String(item.hash || "")
		banReason.text = String(item.reason || "")
		banPermanent.checked = selectedStableId.length === 0 ? true : !!item.permanent
		banDuration.value = item.durationSeconds === undefined
			? 3600 : Math.max(60, Number(item.durationSeconds || 3600))
		draftRevision += 1
	}

	function scheduleDraftHydration() {
		// Loader assigns the typed field after this component has completed. Defer
		// one turn so adminKind, controller and the selected stable ID have all
		// rebound before restoring an existing draft.
		Qt.callLater(function() { root.populateDraft() })
	}

	function banDraft() {
		return {
			"address": banAddress.text,
			"mask": banMask.value,
			"userName": banUser.text,
			"hash": banHash.text,
			"reason": banReason.text,
			"startUtc": new Date().toISOString(),
			"permanent": banPermanent.checked,
			"durationSeconds": banPermanent.checked ? 0 : banDuration.value
		}
	}

	function clearBanDraft() {
		if (adminModel) adminModel.setSelectedStableId("")
		populateDraft()
		banAddress.forceActiveFocus(Qt.TabFocusReason)
	}

	Connections {
		target: root.adminModel
		function onSelectionChanged() { root.populateDraft() }
		function onModelReset() {
			root.modelRevision += 1
			root.populateDraft()
		}
		function onDataChanged() {
			root.modelRevision += 1
			root.populateDraft()
		}
	}
	onFieldChanged: scheduleDraftHydration()
	onSelectedStableIdChanged: scheduleDraftHydration()
	Component.onCompleted: scheduleDraftHydration()

	onConfirmationVisibleChanged: {
		if (confirmationVisible) {
			const window = root.Window.window
			focusBeforeConfirmation = window ? window.activeFocusItem : null
			Qt.callLater(function() {
				if (root.confirmationVisible) confirmationConfirmButton.forceActiveFocus(Qt.TabFocusReason)
			})
		} else {
			const restoreTarget = focusBeforeConfirmation
			focusBeforeConfirmation = null
			if (restoreTarget && restoreTarget.visible !== false && restoreTarget.enabled !== false
					&& restoreTarget.forceActiveFocus) {
				Qt.callLater(function() { restoreTarget.forceActiveFocus(Qt.TabFocusReason) })
			} else {
				Qt.callLater(function() { root.initialFocusTarget.forceActiveFocus(Qt.TabFocusReason) })
			}
		}
	}

	Rectangle {
		anchors.fill: parent
		radius: Theme.shellRadius
		color: Theme.panel
		border.color: Theme.surfaceBorder
		border.width: 1

		ColumnLayout {
			anchors.fill: parent
			anchors.margins: Theme.space3
			spacing: Theme.space3

			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2
				ModernTextField {
					id: searchField
					objectName: "serverAdminSearch"
					Layout.fillWidth: true
					cursorDelegate: adminTextCursorDelegate
					placeholderText: root.usersMode ? qsTr("Search name, ID or last room")
												: qsTr("Search user, address, hash or reason")
					Accessible.name: placeholderText
					onTextChanged: if (root.adminModel) root.adminModel.setFilter(text)
				}
				Label {
					textFormat: Text.PlainText
					text: root.adminModel
						? qsTr("%1 of %2").arg(root.adminModel.filteredCount).arg(root.adminModel.totalCount) : qsTr("0")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
				ModernIconButton {
					objectName: "serverAdminRefresh"
					iconName: "refresh"
					text: qsTr("Refresh")
					enabled: root.controller && !root.loading && !root.busy
					onClicked: root.controller.refresh()
				}
			}

			Rectangle {
				Layout.fillWidth: true
				visible: !root.canManage && root.ready
				implicitHeight: permissionText.implicitHeight + Theme.space3 * 2
				radius: Theme.innerRadius
				color: Theme.withAlpha(Theme.warning, 0.10)
				border.color: Theme.withAlpha(Theme.warning, 0.45)
				Label {
					textFormat: Text.PlainText
					id: permissionText
					anchors.fill: parent
					anchors.margins: Theme.space3
					text: root.usersMode ? qsTr("You can inspect registered users, but cannot change them.")
									 : qsTr("You can inspect bans, but cannot change them.")
					color: Theme.textMain
					wrapMode: Text.Wrap
					font.pixelSize: Theme.fontCaption
				}
			}

			Rectangle {
				Layout.fillWidth: true
				visible: root.controller && String(root.controller.operationError || "").length > 0
				implicitHeight: operationErrorText.implicitHeight + Theme.space3 * 2
				radius: Theme.innerRadius
				color: Theme.withAlpha(Theme.danger, 0.10)
				border.color: Theme.withAlpha(Theme.danger, 0.48)
				Label {
					textFormat: Text.PlainText
					id: operationErrorText
					objectName: "serverAdminOperationError"
					anchors.fill: parent
					anchors.margins: Theme.space3
					text: root.controller ? String(root.controller.operationError || "") : ""
					color: Theme.danger
					wrapMode: Text.Wrap
					Accessible.role: Accessible.AlertMessage
				}
			}

			GridLayout {
				Layout.fillWidth: true
				Layout.fillHeight: true
				columns: root.compactLayout ? 1 : 2
				rowSpacing: Theme.space3
				columnSpacing: Theme.space3

				Rectangle {
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.minimumWidth: 280
					Layout.preferredHeight: root.compactLayout ? 320 : -1
					radius: Theme.innerRadius
					color: Theme.surfaceRaised
					border.color: Theme.divider

					ModernBusyIndicator {
						objectName: "serverAdminLoading"
						anchors.centerIn: parent
						running: root.loading && (!root.adminModel || root.adminModel.totalCount === 0)
						visible: running
						animated: root.animationsEnabled
						Accessible.name: root.usersMode ? qsTr("Loading registered users") : qsTr("Loading bans")
					}

					Column {
						objectName: "serverAdminLoadError"
						anchors.centerIn: parent
						width: Math.min(parent.width - Theme.space6, 340)
						spacing: Theme.space3
						visible: root.controllerState === "error"
						Label {
							textFormat: Text.PlainText
							width: parent.width
							text: root.controller ? String(root.controller.errorMessage || qsTr("Unable to load server records.")) : ""
							color: Theme.danger
							wrapMode: Text.Wrap
							horizontalAlignment: Text.AlignHCenter
						}
						ModernButton {
							objectName: "serverAdminRetry"
							anchors.horizontalCenter: parent.horizontalCenter
							text: qsTr("Try again")
							tone: "accent"
							onClicked: root.controller.refresh()
						}
					}

					Label {
						textFormat: Text.PlainText
						objectName: "serverAdminEmpty"
						anchors.centerIn: parent
						visible: root.ready && root.adminModel && root.adminModel.filteredCount === 0
						text: searchField.text.length > 0 ? qsTr("No matching records")
							: root.usersMode ? qsTr("No registered users") : qsTr("No active bans")
						color: Theme.textMuted
						font.pixelSize: Theme.fontBody
					}

					ListView {
						id: recordsList
						objectName: "serverAdminRecordList"
						anchors.fill: parent
						anchors.margins: 1
						clip: true
						visible: root.adminModel && root.adminModel.filteredCount > 0
						model: root.adminModel
						boundsBehavior: Flickable.StopAtBounds
						reuseItems: true
						ScrollBar.vertical: ModernScrollBar { }
						delegate: ItemDelegate {
							id: recordRow
							objectName: "serverAdminRecord_" + stableId
							required property string stableId
							required property int index
							readonly property var record: root.controller ? root.controller.model.item(stableId) : ({})
							readonly property bool selected: stableId === root.selectedStableId
							width: ListView.view.width
							height: Math.max(58, recordText.implicitHeight + Theme.space3 * 2)
							text: root.usersMode ? String(record.name || qsTr("Unnamed user"))
								: String(record.userName || record.address || record.hash || qsTr("Ban"))
							Accessible.name: text
							Accessible.description: detailText.text
							onClicked: root.select(stableId)
							Keys.onReturnPressed: event => { root.select(stableId); event.accepted = true }
							Keys.onEnterPressed: event => { root.select(stableId); event.accepted = true }
							background: Rectangle {
								color: recordRow.selected ? Theme.selected
									: recordRow.hovered ? Theme.surfaceHover : "transparent"
								border.color: recordRow.activeFocus ? Theme.focus
									: recordRow.selected ? Theme.accent : "transparent"
								border.width: recordRow.activeFocus ? Theme.focusRingWidth : recordRow.selected ? 1 : 0
							}
							contentItem: Column {
								id: recordText
								spacing: Theme.space1
								Label {
									textFormat: Text.PlainText
									width: parent.width
									text: recordRow.text
									color: Theme.textStrong
									font.pixelSize: Theme.fontBody
									font.weight: recordRow.selected ? Font.DemiBold : Font.Medium
									elide: Text.ElideRight
								}
								Label {
									textFormat: Text.PlainText
									id: detailText
									width: parent.width
									text: root.usersMode
										? qsTr("ID %1 · %2").arg(recordRow.record.userId || "–")
											.arg(recordRow.record.lastChannelLabel || qsTr("No last room"))
										: [recordRow.record.address ? recordRow.record.address + "/" + recordRow.record.mask : "",
										   recordRow.record.reason || "",
										   recordRow.record.permanent ? qsTr("Permanent") : qsTr("Temporary")]
										  .filter(value => String(value).length > 0).join(" · ")
									color: Theme.textMuted
									font.pixelSize: Theme.fontCaption
									elide: Text.ElideRight
								}
							}
						}
					}
				}

				Rectangle {
					id: detailPanel
					Layout.fillWidth: root.compactLayout
					Layout.preferredWidth: root.compactLayout ? -1 : Math.max(280, Math.min(360, root.width * 0.39))
					Layout.fillHeight: true
					Layout.preferredHeight: root.compactLayout ? 430 : -1
					radius: Theme.innerRadius
					color: Theme.surfaceRaised
					border.color: Theme.divider

					ColumnLayout {
						anchors.fill: parent
						anchors.margins: Theme.space3
						spacing: Theme.space2

					ScrollView {
						id: detailScroll
						objectName: "serverAdminDetailScroll"
						Layout.fillWidth: true
						Layout.fillHeight: true
						clip: true
						contentWidth: availableWidth
						ScrollBar.vertical: ModernScrollBar {
							objectName: "serverAdminDetailScrollBar"
							parent: detailScroll
							anchors.top: detailScroll.top
							anchors.right: detailScroll.right
							anchors.bottom: detailScroll.bottom
						}

						ColumnLayout {
							id: detailColumn
							width: detailScroll.availableWidth
							spacing: Theme.space2
							Label {
								textFormat: Text.PlainText
								Layout.fillWidth: true
								text: root.usersMode ? qsTr("Account")
									: root.selectedStableId.length > 0 ? qsTr("Edit ban") : qsTr("Add ban")
								color: Theme.textStrong
								font.pixelSize: Theme.fontTitle
								font.weight: Font.DemiBold
							}

							ColumnLayout {
								Layout.fillWidth: true
								visible: root.usersMode
								spacing: Theme.space2
								Label {
									textFormat: Text.PlainText
									Layout.fillWidth: true
									text: root.selectedStableId.length > 0
										? qsTr("Registered user ID %1").arg(root.selectedItem.userId || "–")
										: qsTr("Select a user to rename or unregister.")
									color: Theme.textMuted
									wrapMode: Text.Wrap
								}
								Label { textFormat: Text.PlainText; text: qsTr("User name"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernTextField {
									id: renameField
									objectName: "registeredUserName"
									Layout.fillWidth: true
									cursorDelegate: adminTextCursorDelegate
									enabled: root.selectedStableId.length > 0 && root.canManage && !root.busy
									Accessible.name: qsTr("User name")
								}
							}

							ColumnLayout {
								Layout.fillWidth: true
								visible: !root.usersMode
								spacing: Theme.space2
								Label { textFormat: Text.PlainText; text: qsTr("IP address"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernTextField {
									id: banAddress
									objectName: "banAddress"
									Layout.fillWidth: true
									cursorDelegate: adminTextCursorDelegate
									placeholderText: qsTr("IPv4 or IPv6 (optional with hash)")
									enabled: root.canManage && !root.busy
									Accessible.name: qsTr("IP address")
									Accessible.description: text
								}
								Label { textFormat: Text.PlainText; text: qsTr("Subnet mask"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernSpinBox {
									id: banMask
									objectName: "banMask"
									Layout.fillWidth: true
									from: 8
									to: banAddress.text.indexOf(":") >= 0 ? 128 : 32
									value: to
									editable: true
									enabled: banAddress.enabled && banAddress.text.trim().length > 0
									Accessible.name: qsTr("Subnet mask")
									Accessible.description: String(value)
								}
								Label { textFormat: Text.PlainText; text: qsTr("User label"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernTextField { id: banUser; objectName: "banUser"; Layout.fillWidth: true; cursorDelegate: adminTextCursorDelegate; enabled: root.canManage && !root.busy; Accessible.name: qsTr("User label"); Accessible.description: text }
								Label { textFormat: Text.PlainText; text: qsTr("Certificate hash"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernTextField { id: banHash; objectName: "banHash"; Layout.fillWidth: true; cursorDelegate: adminTextCursorDelegate; enabled: root.canManage && !root.busy; Accessible.name: qsTr("Certificate hash"); Accessible.description: text }
								Label { textFormat: Text.PlainText; text: qsTr("Reason"); color: Theme.textMain; font.pixelSize: Theme.fontCaption }
								ModernTextArea {
									id: banReason
									objectName: "banReason"
									Layout.fillWidth: true
									Layout.preferredHeight: 72
									cursorDelegate: adminTextCursorDelegate
									enabled: root.canManage && !root.busy
									Accessible.name: qsTr("Reason")
									Accessible.description: text
								}
								ModernCheckBox {
									id: banPermanent
									objectName: "banPermanent"
									readonly property bool accessibilityExposed:
										root.itemFullyInsideViewport(banPermanent, detailScroll)
									text: qsTr("Permanent ban")
									checked: true
									enabled: root.canManage && !root.busy
								}
								ModalAccessibilityBarrier {
									objectName: "banPermanentAccessibilityBarrier"
									active: banPermanent.visible && !banPermanent.accessibilityExposed
									targets: [ banPermanent ]
								}
								Label {
									id: banDurationLabel
									textFormat: Text.PlainText
									visible: !banPermanent.checked
									text: qsTr("Duration (seconds)")
									color: Theme.textMain
									font.pixelSize: Theme.fontCaption
									Accessible.ignored: !root.itemFullyInsideViewport(banDurationLabel, detailScroll)
								}
								ModernSpinBox {
									id: banDuration
									objectName: "banDuration"
									readonly property bool accessibilityExposed:
										root.itemFullyInsideViewport(banDuration, detailScroll)
									Layout.fillWidth: true
									visible: !banPermanent.checked
									from: 60
									to: 31536000
									value: 3600
									editable: true
									enabled: root.canManage && !root.busy
									Accessible.name: qsTr("Duration (seconds)")
									Accessible.description: String(value)
								}
								ModalAccessibilityBarrier {
									objectName: "banDurationAccessibilityBarrier"
									active: banDuration.visible && !banDuration.accessibilityExposed
									targets: [ banDuration ]
								}
							}
						}
					}

						RowLayout {
							id: registeredUserActionRow
							objectName: "serverAdminRegisteredUserActionRow"
							readonly property bool accessibilityExposed:
								root.itemFullyInsideViewport(registeredUserActionRow, detailPanel)
							Layout.fillWidth: true
							visible: root.usersMode
							ModalAccessibilityBarrier {
								objectName: "serverAdminRegisteredUserActionAccessibilityBarrier"
								active: registeredUserActionRow.visible
									&& !registeredUserActionRow.accessibilityExposed
								targets: [ registeredUserActionRow ]
							}
							ModernButton {
								objectName: "registeredUserRename"
								Layout.fillWidth: true
								text: qsTr("Rename")
								tone: "accent"
								enabled: renameField.enabled && renameField.text.trim().length > 0
								onClicked: root.controller.beginRename(root.selectedStableId, renameField.text)
							}
							ModernButton {
								objectName: "registeredUserUnregister"
								text: qsTr("Unregister")
								tone: "danger"
								enabled: renameField.enabled
								onClicked: root.controller.beginUnregister(root.selectedStableId)
							}
						}

						RowLayout {
							id: banActionRow
							objectName: "serverAdminBanActionRow"
							readonly property bool accessibilityExposed:
								root.itemFullyInsideViewport(banActionRow, detailPanel)
							Layout.fillWidth: true
							visible: !root.usersMode
							ModalAccessibilityBarrier {
								objectName: "serverAdminBanActionAccessibilityBarrier"
								active: banActionRow.visible && !banActionRow.accessibilityExposed
								targets: [ banActionRow ]
							}
							ModernButton {
								objectName: "banSave"
								Layout.fillWidth: true
								text: root.selectedStableId.length > 0 ? qsTr("Update") : qsTr("Add ban")
								tone: "accent"
								enabled: root.canManage && !root.busy
								onClicked: root.selectedStableId.length > 0
									? root.controller.beginEdit(root.selectedStableId, root.banDraft())
									: root.controller.beginAdd(root.banDraft())
							}
							ModernButton {
								objectName: "banRemove"
								visible: root.selectedStableId.length > 0
								text: qsTr("Remove")
								tone: "danger"
								enabled: root.canManage && !root.busy
								onClicked: root.controller.beginRemove(root.selectedStableId)
							}
							ModernButton {
								objectName: "banClear"
								text: qsTr("Clear")
								enabled: !root.busy
								onClicked: root.clearBanDraft()
							}
						}
					}
				}
			}

			RowLayout {
				Layout.fillWidth: true
				visible: root.adminModel && root.adminModel.pageCount > 1
				ModernButton {
					text: qsTr("Previous")
					dense: true
					enabled: root.adminModel && root.adminModel.page > 0
					onClicked: root.adminModel.setPage(root.adminModel.page - 1)
				}
				Item { Layout.fillWidth: true }
				Label {
					textFormat: Text.PlainText
					text: root.adminModel ? qsTr("Page %1 of %2").arg(root.adminModel.page + 1).arg(root.adminModel.pageCount) : ""
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
				Item { Layout.fillWidth: true }
				ModernButton {
					text: qsTr("Next")
					dense: true
					enabled: root.adminModel && root.adminModel.page + 1 < root.adminModel.pageCount
					onClicked: root.adminModel.setPage(root.adminModel.page + 1)
				}
			}
		}
	}

	FocusScope {
		id: confirmationLayer
		anchors.fill: parent
		visible: root.confirmationVisible
		z: 100
		focus: visible
		Accessible.role: Accessible.Dialog
		Accessible.name: String(root.confirmation.title || qsTr("Confirm change"))
		Keys.onEscapePressed: event => {
			root.controller.cancelPending()
			event.accepted = true
		}
		Rectangle {
			anchors.fill: parent
			color: Theme.withAlpha(Theme.mediaCanvas, 0.72)
		}
		MouseArea { anchors.fill: parent }

		Rectangle {
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space6, 430)
			height: confirmationColumn.implicitHeight + Theme.space4 * 2
			radius: Theme.shellRadius
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
			ColumnLayout {
				id: confirmationColumn
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space4
				spacing: Theme.space3
				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: String(root.confirmation.title || qsTr("Confirm change"))
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
					wrapMode: Text.Wrap
				}
				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: String(root.confirmation.message || "")
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					wrapMode: Text.Wrap
				}
				RowLayout {
					Layout.fillWidth: true
					Item { Layout.fillWidth: true }
					ModernButton {
						id: confirmationCancelButton
						objectName: "serverAdminCancelConfirmation"
						text: qsTr("Cancel")
						activeFocusOnTab: true
						KeyNavigation.tab: confirmationConfirmButton
						KeyNavigation.backtab: confirmationConfirmButton
						onClicked: root.controller.cancelPending()
					}
					ModernButton {
						id: confirmationConfirmButton
						objectName: "serverAdminConfirm"
						text: String(root.confirmation.confirmLabel || qsTr("Confirm"))
						tone: String(root.confirmation.tone || "accent")
						highlighted: true
						activeFocusOnTab: true
						KeyNavigation.tab: confirmationCancelButton
						KeyNavigation.backtab: confirmationCancelButton
						onClicked: root.controller.confirmPending()
					}
				}
			}
		}
	}
}
