import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
	id: root
	onClosing: close => close.accepted = false
    property bool pttToolVisible: false
    property var pttToolPopup: null
    visible: false
    width: 1280
    height: 820
    minimumWidth: 420
    minimumHeight: 520
    // Keep native automation and assistive technology aware of the active
    // modal even though Qt Quick dialogs live inside the main window.
    title: dialogState.open && dialogState.title ? dialogState.title : clientSession.serverName
    color: Theme.strip
	property real performanceChatScrollStartY: 0
	property real performanceChatScrollTargetY: 0
	readonly property bool compactNavigation: width < 900
	readonly property bool automationNavigationOpen: navigationDrawer.visible
	readonly property var navigationRoomModel: roomModel
	readonly property var navigationParticipantModel: participantModel
	readonly property var navigationSelectionState: selectionState
	readonly property var navigationCommands: uiCommands
	readonly property var navigationSession: clientSession
	property string contextScopeToken: ""
	property string contextScopeKind: ""
	property var contextScopeActions: []
	property string contextParticipantId: ""
	property var contextParticipantActions: []
	property string automationMenuVariant: ""

	function normalizedMenuVariant(value) {
		return String(value === undefined || value === null ? "" : value).trim()
	}

	function isAppAction(actionId) {
		return !actionId.startsWith("qaUser") && !actionId.startsWith("qaChannel")
			&& actionId !== "qaEmpty" && actionId !== "qaTransmitModeSeparator"
	}

	function isProfileAction(actionId) {
		return actionId === "qaAudioMute" || actionId === "qaAudioDeaf"
			|| actionId === "qaAudioStats" || actionId === "qaServerTexture"
			|| actionId === "qaServerTextureRemove" || actionId.startsWith("qaSelf")
	}

	function closeProductMenus() {
		appMenuPopup.close()
		profileMenuPopup.close()
		roomMenuPopup.close()
		textRoomMenuPopup.close()
		participantMenuPopup.close()
		chatBackgroundMenuPopup.close()
	}

	function roomRowForKind(kind) {
		for (let index = 0; index < roomModel.count; ++index) {
			const row = roomModel.get(index)
			if (row && row.kind === kind)
				return row
		}
		return null
	}

	function selectedRoomRow() {
		for (let index = 0; index < roomModel.count; ++index) {
			const row = roomModel.get(index)
			if (row && row.selected)
				return row
		}
		return roomModel.count > 0 ? roomModel.get(0) : null
	}

	function openMenuAt(menu, anchorPoint) {
		const point = anchorPoint && anchorPoint.x !== undefined
			? anchorPoint : Qt.point(Math.round(root.width / 2), Math.round(root.height / 3))
		menu.x = Math.max(8, Math.min(root.width - menu.width - 8, Math.round(point.x)))
		menu.y = Math.max(8, Math.min(root.height - menu.implicitHeight - 8, Math.round(point.y)))
		menu.open()
		return menu.visible
	}

	function openScopeMenu(scopeToken, kind, actions, anchorPoint) {
		closeProductMenus()
		contextScopeToken = String(scopeToken || "")
		contextScopeKind = String(kind || "")
		contextScopeActions = actions || []
		const menu = contextScopeKind === "text" ? textRoomMenuPopup : roomMenuPopup
		return openMenuAt(menu, anchorPoint)
	}

	function openParticipantMenu(sessionId, actions, anchorPoint) {
		closeProductMenus()
		contextParticipantId = String(sessionId || "")
		contextParticipantActions = actions || []
		return openMenuAt(participantMenuPopup, anchorPoint)
	}

	function openProfileMenu(anchorPoint) {
		closeProductMenus()
		return openMenuAt(profileMenuPopup, anchorPoint)
	}

	function openChatBackgroundMenu(anchorPoint) {
		closeProductMenus()
		const row = selectedRoomRow()
		contextScopeToken = row ? String(row.scopeToken || "") : String(activeScope.scopeToken || "")
		contextScopeKind = row ? String(row.kind || "") : ""
		contextScopeActions = row && row.source ? (row.source.actions || []) : []
		return openMenuAt(chatBackgroundMenuPopup, anchorPoint)
	}

	function openAutomationMenuProbe(variant) {
		const inputVariant = normalizedMenuVariant(variant)
		const alias = inputVariant.toLowerCase()
		let normalized = inputVariant
		if (alias === "self" || alias === "profile")
			normalized = "profile"
		else if (alias === "member" || alias === "participant")
			normalized = "participant"
		else if (alias === "chat" || alias === "background" || alias === "chatbackground")
			normalized = "chatBackground"
		else if (alias === "textroom" || alias === "textroomreal")
			normalized = "textRoom"
		else if (alias === "app" || alias === "room" || alias === "message")
			normalized = alias
		automationMenuVariant = normalized
		let handled = true
		let menu = null
		if (normalized === "app") {
			closeProductMenus()
			menu = appMenuPopup
			openMenuAt(menu, Qt.point(root.width - menu.width - 24, 72))
		} else if (normalized === "profile") {
			menu = profileMenuPopup
			openProfileMenu(Qt.point(root.width - menu.width - 24, root.height - 90))
		} else if (normalized === "room" || normalized === "textRoom") {
			const row = roomRowForKind(normalized === "textRoom" ? "text" : "voice")
			if (!row) {
				handled = false
			} else {
				menu = normalized === "textRoom" ? textRoomMenuPopup : roomMenuPopup
				openScopeMenu(row.scopeToken, row.kind, row.source ? (row.source.actions || []) : [],
					Qt.point(root.width - menu.width - 24, 150))
			}
		} else if (normalized === "participant") {
			const row = participantModel.count > 0 ? participantModel.get(0) : null
			if (!row) {
				handled = false
			} else {
				menu = participantMenuPopup
				openParticipantMenu(row.stableId || row.id,
					row.source ? (row.source.actions || []) : [],
					Qt.point(root.width - menu.width - 24, 250))
			}
		} else if (normalized === "chatBackground") {
			menu = chatBackgroundMenuPopup
			openChatBackgroundMenu(Qt.point(Math.round(root.width / 2), Math.round(root.height / 2)))
		} else if (normalized === "message") {
			closeProductMenus()
			const message = timeline.currentItem || (timeline.count > 0 ? timeline.itemAtIndex(0) : null)
			if (!message || !message.openAutomationActions) {
				handled = false
			} else {
				menu = message.openAutomationActions()
			}
		} else {
			handled = false
		}
		return {
			"handled": handled,
			"variant": normalized,
			"inputVariant": inputVariant,
			"open": handled && menu !== null && (menu.opened || menu.visible),
			"visible": handled && menu !== null && menu.visible
		}
	}

	function runMotdUiProbe(action, signature) {
		if (!motdPanel || !motdPanel.runProbe) {
			return {
				"handled": false,
				"action": String(action || "").trim().toLowerCase(),
				"expanded": false,
				"visible": false,
				"dismissedSignature": "",
				"reason": "missing-motd-surface"
			}
		}
		return motdPanel.runProbe(action, signature)
	}

	function setAutomationNavigationOpen(open) {
		if (!compactNavigation)
			return false
		if (open)
			navigationDrawer.open()
		else
			navigationDrawer.close()
		return true
	}

	function itemIsWithin(item, ancestor) {
		let cursor = item
		while (cursor) {
			if (cursor === ancestor)
				return true
			cursor = cursor.parent
		}
		return false
	}

	onCompactNavigationChanged: {
		const focusedItem = root.activeFocusItem
		const focusWasInDesktopRail = root.itemIsWithin(focusedItem, desktopNavigationRail)
		const focusWasInDrawer = root.itemIsWithin(focusedItem, navigationDrawerRail)
			|| focusedItem === navigationToggle
		if (!compactNavigation)
			navigationDrawer.close()
		Qt.callLater(function() {
			if (root.compactNavigation && focusWasInDesktopRail && navigationToggle.visible)
				navigationToggle.forceActiveFocus(Qt.OtherFocusReason)
			else if (!root.compactNavigation && focusWasInDrawer)
				desktopNavigationRail.focusInitialItem()
		})
	}

	function focusVisualFixture(state) {
		if (state === "connected") {
			// The deterministic fixture intentionally has no writable live scope,
			// so its composer is disabled and cannot own accessibility focus.
			appMenuButton.forceActiveFocus(Qt.OtherFocusReason)
			return appMenuButton
		}
		if (state === "error") {
			const operation = operationRepeater.itemAt(0)
			if (operation && operation.visualFixtureFocusTarget) {
				operation.visualFixtureFocusTarget.forceActiveFocus(Qt.OtherFocusReason)
				return operation.visualFixtureFocusTarget
			}
		}
		appMenuButton.forceActiveFocus(Qt.OtherFocusReason)
		return appMenuButton
	}

	Instantiator {
		model: actionModel
		delegate: Shortcut {
			required property string stableId
			required property var payload
			sequence: payload.shortcutPortableText || ""
			enabled: payload.enabled && payload.visible && sequence.length > 0
			context: Qt.ApplicationShortcut
			onActivated: actionModel.trigger(stableId)
		}
	}

	Shortcut {
		sequence: "F6"
		context: Qt.ApplicationShortcut
		onActivated: {
			const next = root.contentItem.nextItemInFocusChain(true)
			if (next)
				next.forceActiveFocus(Qt.TabFocusReason)
		}
	}

    function createScreenShareView(backend) {
        return screenShareViewComponent.createObject(null, { "backend": backend })
    }

	function runPerformanceChatScrollWorkload() {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		if (timeline.count < 20 || maximumY - minimumY < 8)
			return { "started": false, "reason": qsTr("The chat timeline is not scrollable."),
				"count": timeline.count, "contentHeight": timeline.contentHeight, "viewportHeight": timeline.height }
		performanceChatScrollStartY = timeline.contentY
		performanceChatScrollTargetY = Math.abs(performanceChatScrollStartY - minimumY) > 8 ? minimumY : maximumY
		timelineScrollWorkload.stop()
		timelineScrollWorkload.from = performanceChatScrollStartY
		timelineScrollWorkload.to = performanceChatScrollTargetY
		timelineScrollWorkload.start()
		return { "started": true, "beforeY": performanceChatScrollStartY,
			"targetY": performanceChatScrollTargetY, "count": timeline.count }
	}

	function performanceChatFixtureState() {
		const minimumY = timeline.originY
		const maximumY = Math.max(minimumY, timeline.originY + timeline.contentHeight - timeline.height)
		return { "count": timeline.count, "contentHeight": timeline.contentHeight,
			"viewportHeight": timeline.height, "originY": minimumY, "maximumY": maximumY,
			"scrollable": timeline.count === 96 && maximumY - minimumY >= 8 }
	}

	function performanceChatScrollState() {
		return { "beforeY": performanceChatScrollStartY, "currentY": timeline.contentY,
			"targetY": performanceChatScrollTargetY,
			"moved": Math.abs(timeline.contentY - performanceChatScrollStartY) > 1,
			"running": timelineScrollWorkload.running }
	}

	NumberAnimation {
		id: timelineScrollWorkload
		target: timeline
		property: "contentY"
		duration: 450
		easing.type: Easing.InOutQuad
	}

    Component {
        id: screenShareViewComponent
        ScreenShareViewWindow { }
    }

    onPttToolVisibleChanged: {
        if (pttToolVisible) {
            if (!pttToolPopup)
                pttToolPopup = pttToolComponent.createObject(root.contentItem)
            pttToolPopup.open()
        } else if (!pttToolVisible && pttToolPopup) {
            pttToolPopup.close()
        }
    }

    Component {
        id: pttToolComponent
        PttTool { }
    }

    QmlDialog { visible: dialogState.open && dialogState.kind !== "imageViewer" }
    Component {
        id: imageViewerComponent
        ImageViewer { controller: dialogState }
    }
    Loader {
        active: dialogState.open && dialogState.kind === "imageViewer"
        sourceComponent: imageViewerComponent
    }
    MediaSessionWindow { }

	Drawer {
		id: navigationDrawer
		edge: Theme.railSide === "left" ? Qt.LeftEdge : Qt.RightEdge
		width: Math.min(340, root.width * 0.86)
		height: root.height
		enabled: root.compactNavigation
		modal: true
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		onOpened: navigationDrawerRail.focusInitialItem()
		onClosed: {
			if (navigationToggle.visible)
				navigationToggle.forceActiveFocus()
			else
				desktopNavigationRail.focusInitialItem()
		}
		background: Rectangle { color: Theme.rail; border.color: Theme.divider }
		NavigationRail {
			id: navigationDrawerRail
			anchors.fill: parent
			Accessible.role: Accessible.Dialog
			Accessible.name: qsTr("Rooms and participants")
			roomModel: root.navigationRoomModel
			participantModel: root.navigationParticipantModel
			selectionState: root.navigationSelectionState
			uiCommands: root.navigationCommands
			clientSession: root.navigationSession
			commitOnSelection: true
			onSelectionCommitted: navigationDrawer.close()
			onScopeMenuRequested: (scopeToken, kind, actions, anchorPoint) =>
				root.openScopeMenu(scopeToken, kind, actions, anchorPoint)
			onParticipantMenuRequested: (sessionId, actions, anchorPoint) =>
				root.openParticipantMenu(sessionId, actions, anchorPoint)
			onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
		}
	}

	ModernMenu {
		id: profileMenuPopup
		objectName: "profileMenu"
		width: 280
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		Repeater {
			model: actionModel
			delegate: MenuItem {
				required property string stableId
				required property string title
				required property var payload
				visible: root.isProfileAction(stableId)
						 && (payload.visible === undefined || !!payload.visible)
				height: visible ? implicitHeight : 0
				enabled: visible && (payload.enabled === undefined || !!payload.enabled)
				checkable: !!payload.checkable
				checked: !!payload.checked
				text: title + (payload.shortcut.length > 0 ? "    " + payload.shortcut : "")
				Accessible.name: title
				Accessible.description: payload.toolTip || ""
				onTriggered: actionModel.trigger(stableId)
			}
		}
	}

	ModernMenu {
		id: roomMenuPopup
		objectName: "voiceRoomMenu"
		width: 280
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		MenuItem {
			visible: root.contextScopeActions.length === 0
			height: visible ? implicitHeight : 0
			text: qsTr("No room actions available")
			enabled: false
		}
		Repeater {
			model: root.contextScopeActions
			delegate: PayloadMenuItem {
				required property var modelData
				payload: modelData
				onActionRequested: actionId => uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
				onValueRequested: (actionId, value, finalValue) =>
					uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
			}
		}
	}

	ModernMenu {
		id: textRoomMenuPopup
		objectName: "textRoomMenu"
		width: 280
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		MenuItem {
			visible: root.contextScopeActions.length === 0
			height: visible ? implicitHeight : 0
			text: qsTr("No text-room actions available")
			enabled: false
		}
		Repeater {
			model: root.contextScopeActions
			delegate: PayloadMenuItem {
				required property var modelData
				payload: modelData
				onActionRequested: actionId => uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
				onValueRequested: (actionId, value, finalValue) =>
					uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
			}
		}
	}

	ModernMenu {
		id: participantMenuPopup
		objectName: "participantMenu"
		width: 300
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		MenuItem {
			visible: root.contextParticipantActions.length === 0
			height: visible ? implicitHeight : 0
			text: qsTr("No participant actions available")
			enabled: false
		}
		Repeater {
			model: root.contextParticipantActions
			delegate: PayloadMenuItem {
				required property var modelData
				payload: modelData
				onActionRequested: actionId =>
					uiCommands.invokeParticipantAction(root.contextParticipantId, actionId)
				onValueRequested: (actionId, value, finalValue) =>
					uiCommands.invokeParticipantActionValue(root.contextParticipantId, actionId, value, finalValue)
			}
		}
	}

	ModernMenu {
		id: chatBackgroundMenuPopup
		objectName: "chatBackgroundMenu"
		width: 280
		focus: true
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
		MenuItem {
			visible: activeScope.canLoadOlder
			height: visible ? implicitHeight : 0
			text: qsTr("Load older messages")
			enabled: activeScope.canLoadOlder && activeScope.loadingState !== "older"
			onTriggered: uiCommands.requestOlderMessages()
		}
		MenuItem {
			visible: !activeScope.canLoadOlder && root.contextScopeActions.length === 0
			height: visible ? implicitHeight : 0
			text: qsTr("No conversation actions available")
			enabled: false
		}
		Repeater {
			model: root.contextScopeActions
			delegate: PayloadMenuItem {
				required property var modelData
				payload: modelData
				onActionRequested: actionId => {
					if (root.contextScopeToken.length > 0)
						uiCommands.invokeScopeAction(root.contextScopeToken, actionId)
				}
				onValueRequested: (actionId, value, finalValue) => {
					if (root.contextScopeToken.length > 0)
						uiCommands.invokeScopeActionValue(root.contextScopeToken, actionId, value, finalValue)
				}
			}
		}
	}

    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: Math.min(380, parent.width - 40)
        spacing: 8
        z: 40
		Repeater {
			id: operationRepeater
			model: operationModel
			delegate: Rectangle {
                required property string stableId
                required property string title
                required property string subtitle
				required property string status
				required property var payload
				property Item visualFixtureFocusTarget: dismissOperationButton.visible ? dismissOperationButton : null
                width: parent.width
                height: operationContent.implicitHeight + 24
                radius: Theme.innerRadius
                color: Theme.panel
                border.color: status === "failed" ? "#ef4444" : Theme.divider
                Accessible.role: Accessible.AlertMessage
                Accessible.name: title + (subtitle.length > 0 ? ": " + subtitle : "")
                ColumnLayout {
                    id: operationContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: title; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                        ModernButton {
                            visible: status === "running" && !!payload.cancellable
                            text: qsTr("Cancel")
                            Accessible.name: qsTr("Cancel %1").arg(title)
                            onClicked: operationModel.cancel(stableId)
                        }
						ModernButton {
							id: dismissOperationButton
							objectName: "visualFixtureDismissOperation"
							visible: status !== "running"
                            text: qsTr("Dismiss")
                            Accessible.name: qsTr("Dismiss %1").arg(title)
							Accessible.focusable: true
							Accessible.focused: activeFocus
                            onClicked: operationModel.dismiss(stableId)
                        }
                    }
                    Label { Layout.fillWidth: true; text: subtitle; color: Theme.textMuted; wrapMode: Text.Wrap }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: status === "running" || Number(payload.progress) >= 0
                        indeterminate: !!payload.indeterminate
                        from: 0
                        to: 100
                        value: Number(payload.progress) >= 0 ? Number(payload.progress) : 0
                    }
                    Label {
                        visible: status !== "running"
                        text: status === "succeeded" ? qsTr("Completed") : qsTr("Failed")
                        color: status === "succeeded" ? "#34d399" : "#f87171"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    Rectangle {
		id: productSurface
        anchors.fill: parent
        anchors.margins: 8
		enabled: !dialogState.open
		Accessible.ignored: dialogState.open
        radius: Theme.shellRadius
        color: Theme.shellBackground
        border.color: Theme.divider
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0
			layoutDirection: Theme.railSide === "left" ? Qt.RightToLeft : Qt.LeftToRight

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    id: shellHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: Theme.panel
                    border.color: Theme.divider
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
						anchors.right: headerActions.left
						anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Label {
							width: parent.width
                            text: activeScope.label.length > 0 ? activeScope.label : clientSession.serverName
                            color: Theme.textStrong
                            font.pixelSize: 20
                            font.bold: true
							elide: Text.ElideRight
                        }
                        Label {
                            text: activeScope.description.length > 0 ? activeScope.description : activeScope.kindLabel
                            color: Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
							width: parent.width
                        }
                    }
					Row {
						id: headerActions
						anchors.right: parent.right
						anchors.rightMargin: 16
						anchors.verticalCenter: parent.verticalCenter
						spacing: 6
						ToolButton {
							id: navigationToggle
							objectName: "compactNavigationToggle"
							visible: root.compactNavigation
							text: "☰"
							font.pixelSize: 20
							Accessible.name: qsTr("Open rooms and participants")
							onClicked: navigationDrawer.open()
						}
					ToolButton {
						id: appMenuButton
						objectName: "visualFixtureApplicationMenu"
                        text: "⋯"
                        font.pixelSize: 22
                        Accessible.name: qsTr("Application menu")
						Accessible.focusable: true
						Accessible.focused: activeFocus
						onClicked: {
							root.closeProductMenus()
							root.openMenuAt(appMenuPopup,
								appMenuButton.mapToItem(null, appMenuButton.width, appMenuButton.height))
						}
                    }
					}
                    ModernMenu {
                        id: appMenuPopup
                        width: 260
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
						Repeater {
							model: actionModel
							delegate: MenuItem {
								required property string stableId
								required property string title
								required property var payload
								visible: root.isAppAction(stableId)
										 && (payload.visible === undefined || !!payload.visible)
								height: visible ? implicitHeight : 0
								enabled: visible && (payload.enabled === undefined || !!payload.enabled)
								checkable: !!payload.checkable
								checked: !!payload.checked
								text: title + (payload.shortcut.length > 0 ? "    " + payload.shortcut : "")
								Accessible.name: title
								Accessible.description: payload.toolTip || ""
								onTriggered: actionModel.trigger(stableId)
							}
						}
                    }
                }

                UpdateBanner {
                    Layout.fillWidth: true
                    state: clientSession.updateBanner
                    onActionRequested: actionId => uiCommands.invokeAction(actionId)
                }

				ConnectionBanner {
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Theme.spacing : 0
					session: clientSession
					onActionRequested: (actionId, payload) => uiCommands.invokeAppAction(actionId, payload)
				}

				MotdPanel {
					id: motdPanel
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					maximumBodyHeight: Math.max(120, Math.min(260, root.height * 0.28))
					session: clientSession
					onActionRequested: (actionId, payload) => uiCommands.invokeAppAction(actionId, payload)
					onLinkRequested: link => Qt.openUrlExternally(link)
				}

                ListView {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: chatModel
                    clip: true
                    spacing: 8
                    leftMargin: 28
                    rightMargin: 28
                    topMargin: 20
                    bottomMargin: 20
                    reuseItems: true
					MouseArea {
						anchors.fill: parent
						z: 20
						acceptedButtons: Qt.RightButton
						onClicked: mouse => root.openChatBackgroundMenu(
							timeline.mapToItem(null, mouse.x, mouse.y))
					}
                    headerPositioning: ListView.InlineHeader
                    header: Item {
                        width: timeline.width
                        height: (activeScope.canLoadOlder || activeScope.loadingState === "older") ? 48 : 0
                        visible: height > 0
                        ModernButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 4
                            enabled: activeScope.canLoadOlder && activeScope.loadingState !== "older"
                            text: activeScope.loadingState === "older"
                                  ? qsTr("Loading older messages…")
                                  : qsTr("Load older messages")
                            Accessible.name: text
                            Accessible.description: qsTr("Load messages sent before the currently visible history")
                            onClicked: uiCommands.requestOlderMessages()
                        }
                    }
					delegate: Rectangle {
						id: messageDelegate
						function openAutomationActions() {
							messageActions.open()
							return messageActions
						}
						property bool accessibilityPooled: false
						ListView.onPooled: accessibilityPooled = true
						ListView.onReused: accessibilityPooled = false
						visible: !accessibilityPooled
						Accessible.ignored: accessibilityPooled
                        required property string stableId
                        required property string title
                        required property string subtitle
                        required property string status
                        required property string avatarUrl
                        required property string timestamp
                        required property string replyActor
                        required property string replySnippet
                        required property var reactions
                        required property var preview
                        required property var attachments
                        required property var source
                        required property bool own
                        required property bool deleted
                        required property bool canReply
                        required property bool canReact
                        required property bool canDelete
                        width: Math.min(timeline.width - 56, 680)
                        height: messageRow.implicitHeight + 24
                        radius: Theme.innerRadius
                        color: own ? Theme.selected : Theme.panel
                        RowLayout {
                            id: messageRow
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                Layout.alignment: Qt.AlignTop
                                radius: 18
                                color: Theme.strip
                                clip: true
                                Image {
                                    id: avatarImage
                                    anchors.fill: parent
                                    source: avatarUrl
                                    asynchronous: true
                                    cache: false
                                    sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                                    fillMode: Image.PreserveAspectCrop
                                    visible: avatarImage.status === Image.Ready
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: avatarUrl.length === 0
                                    text: (title || "S").slice(0, 1).toUpperCase()
                                    color: Theme.textStrong
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: title || qsTr("System"); color: Theme.accent; font.bold: true; font.pixelSize: 11 }
                                    Label { text: timestamp; color: Theme.textMuted; font.pixelSize: 9; visible: timestamp.length > 0 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: status; color: Theme.textMuted; font.pixelSize: 9; visible: status.length > 0 }
                                    ToolButton {
                                        visible: messageDelegate.canReply || messageDelegate.canReact
                                                 || messageDelegate.canDelete || !!messageDelegate.source.deliveryCanRetry
                                        text: "⋯"
                                        Accessible.name: qsTr("Message actions")
                                        onClicked: messageActions.open()
                                        ModernMenu {
                                            id: messageActions
                                            MenuItem {
                                                text: qsTr("Reply")
                                                visible: messageDelegate.canReply
                                                onTriggered: uiCommands.replyToMessage(messageDelegate.stableId)
                                            }
                                            MenuItem {
                                                text: qsTr("Add reaction")
                                                visible: messageDelegate.canReact
                                                onTriggered: uiCommands.toggleMessageReaction(messageDelegate.stableId, "👍")
                                            }
                                            MenuItem {
                                                text: qsTr("Retry")
                                                visible: !!messageDelegate.source.deliveryCanRetry
                                                onTriggered: uiCommands.retryMessage(messageDelegate.stableId)
                                            }
                                            MenuItem {
                                                text: qsTr("Delete")
                                                visible: messageDelegate.canDelete
                                                onTriggered: uiCommands.deleteMessage(messageDelegate.stableId)
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    id: previewCard
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: replyColumn.implicitHeight + 12
                                    visible: replyActor.length > 0 || replySnippet.length > 0
                                    radius: 6
                                    color: Theme.strip
                                    Column {
                                        id: replyColumn
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        Label { text: replyActor; color: Theme.accent; font.pixelSize: 9; font.bold: true }
                                        Label { width: parent.width; text: replySnippet; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: deleted ? qsTr("Message deleted") : subtitle
                                    color: deleted ? Theme.textMuted : Theme.textMain
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 12
                                    font.italic: deleted
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
									visible: !!attachments && attachments.length > 0
                                    Repeater {
                                        model: attachments || []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: Math.min(attachmentImage.implicitWidth > 0 ? attachmentImage.implicitWidth : 180, 320)
                                            height: Math.min(attachmentImage.implicitHeight > 0 ? attachmentImage.implicitHeight : 120, 240)
                                            radius: 8
                                            color: Theme.strip
                                            clip: true
                                            Image {
                                                id: attachmentImage
                                                anchors.fill: parent
                                                source: modelData.thumbnailUrl || modelData.url || ""
                                                asynchronous: true
                                                cache: false
                                                fillMode: Image.PreserveAspectFit
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: previewContent.implicitHeight + 16
									visible: !!preview && ((preview.title || "").length > 0
                                                         || (preview.url || "").length > 0)
                                    radius: 8
                                    color: Theme.strip
                                    RowLayout {
                                        id: previewContent
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 10
                                        Image {
                                            Layout.preferredWidth: 72
                                            Layout.preferredHeight: 54
                                            source: preview ? (preview.thumbnailUrl || "") : ""
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectCrop
                                            visible: source.toString().length > 0
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Label { Layout.fillWidth: true; text: preview ? (preview.title || preview.host || qsTr("Link preview")) : ""; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                                            Label { Layout.fillWidth: true; text: preview ? (preview.description || preview.url || "") : ""; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                            RowLayout {
												visible: !!preview && ((preview.url || "").length > 0
                                                                     || ((preview.embedUrl || "").length > 0
                                                                         && (preview.embedKind || "").length > 0))
                                                ModernButton {
													visible: !!preview && (preview.url || "").length > 0
                                                    text: qsTr("Open")
                                                    onClicked: Qt.openUrlExternally(preview.url)
                                                }
                                                ModernButton {
													visible: !!preview && (preview.embedUrl || "").length > 0
                                                             && (preview.embedKind || "").length > 0
                                                    text: qsTr("Play")
                                                    onClicked: mediaSession.open(preview.embedUrl,
                                                                                 preview.embedKind,
                                                                                 messageDelegate.stableId)
                                                }
                                                Item { Layout.fillWidth: true }
                                            }
                                        }
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 5
									visible: !!reactions && reactions.length > 0
                                    Repeater {
                                        model: reactions || []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: reactionLabel.implicitWidth + 12
                                            height: 24
                                            radius: 12
                                            color: modelData.selfReacted ? Theme.selected : Theme.strip
                                            Label {
                                                id: reactionLabel
                                                anchors.centerIn: parent
                                                text: (modelData.emoji || "") + " " + (modelData.count || 0)
                                                color: Theme.textMain
                                                font.pixelSize: 10
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                enabled: messageDelegate.canReact && (modelData.emoji || "").length > 0
                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                onClicked: uiCommands.toggleMessageReaction(messageDelegate.stableId,
                                                                                             modelData.emoji)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: chatModel.count === 0
                        text: !clientSession.connected
                              ? qsTr("Connect to load rooms and messages")
                              : (activeScope.canSend
                                 ? qsTr("No messages in %1 yet.").arg(activeScope.label)
                                 : qsTr("Select a room to start chatting"))
                        color: Theme.textMuted
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: (activeScope.hasPendingReply ? 112 : 76) + (composer.attachments.count > 0 ? 58 : 0) + (composer.autocompleteItems.length > 0 ? 34 : 0)
                    color: Theme.strip
                    border.color: Theme.divider
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        ModernButton {
                            visible: activeScope.canAttachImages
                            enabled: activeScope.canSend
                            text: "+"
                            Accessible.name: qsTr("Attach image")
                            onClicked: uiCommands.chooseAttachment()
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.innerRadius
                            color: Theme.panel
                            border.color: Theme.divider
                            DropArea {
                                anchors.fill: parent
                                onDropped: drop => { if (drop.hasUrls) composer.addUrls(drop.urls) }
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: activeScope.hasPendingReply
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Replying to %1: %2").arg(activeScope.replyActor)
                                                .arg(activeScope.replySnippet)
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                    ToolButton {
                                        text: "×"
                                        Accessible.name: qsTr("Cancel reply")
                                        onClicked: uiCommands.cancelPendingReply()
                                    }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: composer.attachments.count > 0 ? 52 : 0
                                    visible: composer.attachments.count > 0
                                    orientation: ListView.Horizontal
                                    spacing: 6
                                    model: composer.attachments
                                    delegate: Rectangle {
                                        required property string stableId
                                        required property string thumbnailUrl
                                        required property string fileName
                                        required property string status
										required property real progress
										required property string error
                                        width: 150; height: 48; radius: 7; color: Theme.strip; border.color: Theme.divider
                                        RowLayout { anchors.fill: parent; anchors.margins: 4
                                            Image { Layout.preferredWidth: 38; Layout.preferredHeight: 38; source: thumbnailUrl; asynchronous: true; cache: false; fillMode: Image.PreserveAspectCrop }
											ColumnLayout {
												Layout.fillWidth: true
												Label { Layout.fillWidth: true; text: fileName; color: Theme.textMain; elide: Text.ElideMiddle; font.pixelSize: 9 }
												Label { Layout.fillWidth: true; visible: status !== "ready"; text: error || status; color: status === "failed" ? Theme.danger : Theme.textMuted; elide: Text.ElideRight; font.pixelSize: 8 }
											}
											ToolButton { visible: status === "failed"; text: "↻"; onClicked: composer.retryAttachment(stableId); Accessible.name: qsTr("Retry %1").arg(fileName) }
                                            ToolButton { text: "×"; onClicked: composer.removeAttachment(stableId); Accessible.name: qsTr("Remove %1").arg(fileName) }
                                        }
                                    }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: composer.autocompleteItems.length > 0 ? 30 : 0
                                    visible: composer.autocompleteItems.length > 0
                                    orientation: ListView.Horizontal
                                    spacing: 4
                                    model: composer.autocompleteItems
                                    delegate: ToolButton {
                                        required property var modelData
                                        text: modelData.label || ""
                                        onClicked: composer.complete(modelData.value || "")
                                    }
                                }
							TextArea {
								id: composerInput
								objectName: "visualFixtureComposer"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: composer.text
                                onTextChanged: if (composer.text !== text) composer.text = text
                                placeholderText: activeScope.composerPlaceholder.length > 0
                                                 ? activeScope.composerPlaceholder
                                                 : qsTr("Connect to send messages")
                                enabled: composer.canSend && !composer.sending
                                color: Theme.textMain
								placeholderTextColor: Theme.textMuted
								Accessible.name: activeScope.composerPlaceholder.length > 0
												 ? activeScope.composerPlaceholder
												 : qsTr("Message composer")
								Accessible.description: activeScope.composerHint
								Accessible.focusable: true
								Accessible.focused: activeFocus
                                background: null
                                wrapMode: TextEdit.Wrap
                                Keys.onReturnPressed: event => {
                                    if (!(event.modifiers & Qt.ShiftModifier) && (text.trim().length > 0 || composer.attachments.count > 0)) {
                                        composer.send()
                                        event.accepted = true
                                    }
                                }
                            }
                            }
                        }
                        ModernButton {
                            text: "Send"
                            enabled: composer.canSend && !composer.sending && (composer.text.trim().length > 0 || composer.attachments.count > 0)
                            onClicked: composer.send()
                        }
                    }
                }
            }

            NavigationRail {
				id: desktopNavigationRail
                Layout.preferredWidth: 310
                Layout.fillHeight: true
                visible: !root.compactNavigation
                roomModel: root.navigationRoomModel
                participantModel: root.navigationParticipantModel
                selectionState: root.navigationSelectionState
                uiCommands: root.navigationCommands
                clientSession: root.navigationSession
				onSelectionCommitted: navigationDrawer.close()
				onScopeMenuRequested: (scopeToken, kind, actions, anchorPoint) =>
					root.openScopeMenu(scopeToken, kind, actions, anchorPoint)
				onParticipantMenuRequested: (sessionId, actions, anchorPoint) =>
					root.openParticipantMenu(sessionId, actions, anchorPoint)
				onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
            }
        }
    }
}
