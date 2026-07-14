import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
	id: root
	onClosing: close => close.accepted = false
    property var attachmentViewerPayload: null
    property var pendingPreviewHydrationIds: ({})
    property bool pendingPreviewHydrationHighPriority: false
    visible: false
    width: 1280
    height: 820
    minimumWidth: 420
    minimumHeight: 520
	palette.window: Theme.shellBackground
	palette.base: Theme.surfaceRaised
	palette.alternateBase: Theme.panel
	palette.button: Theme.surfaceRaised
	palette.text: Theme.textMain
	palette.windowText: Theme.textMain
	palette.buttonText: Theme.textStrong
	palette.brightText: Theme.textStrong
	palette.highlight: Theme.accent
	palette.highlightedText: Theme.strip
	palette.placeholderText: Theme.textMuted
	palette.disabled.text: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
    // Keep native automation and assistive technology aware of the active
    // modal even though Qt Quick dialogs live inside the main window.
	title: dialogState.open && dialogState.title ? dialogState.title
		: clientSession.connected && clientSession.serverName.length > 0
			? qsTr("%1 — Mumble").arg(clientSession.serverName) : qsTr("Mumble")
    color: Theme.strip
	property real performanceChatScrollStartY: 0
	property real performanceChatScrollTargetY: 0
	readonly property bool compactNavigation: width < 900
	readonly property bool narrowShell: width < 600
	readonly property int timelineHorizontalMargin: narrowShell ? Theme.space3
		: compactNavigation ? Theme.space5 : 28
	readonly property int timelineVerticalMargin: narrowShell ? Theme.space3 : Theme.space5
	readonly property int conversationLaneMaximumWidth: 840
	readonly property bool automationNavigationOpen: navigationDrawer.visible
	readonly property real automationNavigationPosition: navigationDrawer.position
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
	property string contextParticipantEntryKind: "user"
	property string contextParticipantScopeToken: ""
	property string automationMenuVariant: ""

	function messageStartsGroup(row, source, title) {
		if (row <= 0 || !source || source.system)
			return true
		const previous = chatModel.get(row - 1)
		const previousSource = previous && previous.source ? previous.source : ({})
		if (previousSource.system)
			return true
		const actorKey = String(source.actorKey || title || "")
		const previousActorKey = String(previousSource.actorKey || (previous ? previous.title : "") || "")
		if (actorKey !== previousActorKey || !!source.own !== !!previousSource.own)
			return true
		const createdAt = Number(source.createdAtMs || 0)
		const previousCreatedAt = Number(previousSource.createdAtMs || 0)
		return createdAt > 0 && previousCreatedAt > 0 && createdAt - previousCreatedAt > 300000
	}

	function preferredOutgoingMessageWidth(segments, startsGroup) {
		let longestLine = 0
		for (const segment of (segments || [])) {
			const text = String(segment && segment.text !== undefined ? segment.text : "")
			for (const line of text.split(/\r\n|\r|\n/))
				longestLine = Math.max(longestLine, line.length)
		}
		// This only chooses a comfortable bubble width. RichMessageBody remains
		// responsible for exact text measurement and wrapping.
		const textEstimate = longestLine * Theme.fontBody * 0.58 + Theme.space4 * 2
		const headerFloor = startsGroup ? 260 : 176
		return Math.max(headerFloor, Math.min(520, textEstimate))
	}

	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}

	function openAttachment(attachment, titleOverride) {
		if (!attachment)
			return false
		const source = safeRenderImageSource(attachment.url || attachment.thumbnailUrl || "")
		if (source.length === 0)
			return false
		attachmentViewerPayload = {
			"url": source,
			"thumbnailUrl": safeRenderImageSource(attachment.thumbnailUrl || source),
			"name": String(titleOverride || attachment.name || ""),
			"alt": String(titleOverride || attachment.alt || attachment.name || qsTr("Image attachment"))
		}
		return true
	}

    function clearPreviewHydrationQueue() {
        pendingPreviewHydrationIds = ({})
        pendingPreviewHydrationHighPriority = false
        previewHydrationTimer.stop()
    }

    function queuePreviewHydration(messageId, highPriority) {
        const normalized = String(messageId === undefined || messageId === null ? "" : messageId).trim()
        if (!/^[1-9][0-9]*$/.test(normalized) || String(activeScope.scopeToken || "").length === 0)
            return false
        // Keep protocol uint64 IDs as decimal strings. JavaScript numbers lose
        // precision above 2^53 and would hydrate a different message.
        if (normalized.length > 20)
            return false
        pendingPreviewHydrationIds[normalized] = normalized
        pendingPreviewHydrationHighPriority = pendingPreviewHydrationHighPriority || !!highPriority
        if (!previewHydrationTimer.running)
            previewHydrationTimer.start()
        return true
    }

    function flushPreviewHydrationQueue() {
        const keys = Object.keys(pendingPreviewHydrationIds)
        if (keys.length === 0)
            return
        const batch = []
        for (let index = 0; index < keys.length && batch.length < 32; ++index) {
            const key = keys[index]
            batch.push(pendingPreviewHydrationIds[key])
            delete pendingPreviewHydrationIds[key]
        }
        const highPriority = pendingPreviewHydrationHighPriority
        pendingPreviewHydrationHighPriority = false
        uiCommands.requestPreviewHydration(String(activeScope.scopeToken || ""), batch, highPriority)
        if (Object.keys(pendingPreviewHydrationIds).length > 0)
            previewHydrationTimer.start()
    }

	function normalizedMenuVariant(value) {
		return String(value === undefined || value === null ? "" : value).trim()
	}

	function profileMenuGroups() {
		const state = clientSession.selfMenu || ({})
		const groups = []
		const presence = state.presence || []
		const actions = state.actions || []
		if (presence.length > 0) {
			groups.push({
				"id": "presence",
				"label": qsTr("Presence"),
				"items": presence
			})
		}
		if (actions.length > 0) {
			groups.push({
				"id": "profile-actions",
				"label": qsTr("Account and app"),
				"items": actions
			})
		}
		return groups
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
		let resolvedActions = actions || []
		if (resolvedActions.length === 0 && contextScopeToken.length > 0)
			resolvedActions = uiCommands.requestScopeActions(contextScopeToken, contextScopeKind) || []
		contextScopeActions = resolvedActions
		const menu = contextScopeKind === "text" ? textRoomMenuPopup : roomMenuPopup
		return openMenuAt(menu, anchorPoint)
	}

	function openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken) {
		closeProductMenus()
		contextParticipantId = String(sessionId || "")
		contextParticipantActions = actions || []
		contextParticipantEntryKind = String(entryKind || "user").toLowerCase()
		contextParticipantScopeToken = String(scopeToken || "")
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

	function visibleMenuLabels(menu) {
		const labels = []
		if (!menu || menu.count === undefined || !menu.itemAt)
			return labels
		for (let index = 0; index < menu.count; ++index) {
			const item = menu.itemAt(index)
			if (!item || !item.visible || item.height <= 0)
				continue
			const label = String(item.text || "").trim()
			if (label.length > 0)
				labels.push(label)
		}
		return labels
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
		// Every probe starts from a clean popup state. A requested live context can
		// legitimately be absent (for example, a server with no other participants),
		// and that must not leave the previously probed menu visible in the capture.
		closeProductMenus()
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
				openParticipantMenu(row.participantSession || (row.source && row.source.session)
						|| row.stableId || row.id,
					row.source ? (row.source.actions || []) : [],
					Qt.point(root.width - menu.width - 24, 250),
					row.entryKind || (row.source && row.source.entryKind) || "user",
					row.scopeToken || (row.source && row.source.scopeToken) || "")
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
			"visible": handled && menu !== null && menu.visible,
			"labels": handled ? visibleMenuLabels(menu) : []
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
		if (dialogState.open && productDialog.visible) {
			productDialog.applyInitialFocus()
			return root.activeFocusItem
		}
		if (state === "connected") {
			// The deterministic fixture intentionally has no writable live scope,
			// so its composer is disabled and cannot own accessibility focus.
			appMenuButton.forceActiveFocus(Qt.OtherFocusReason)
			return appMenuButton
		}
		if (state === "error") {
			const operationTarget = operationOverlay.visualFixtureFocusTarget
			if (operationTarget) {
				operationTarget.forceActiveFocus(Qt.OtherFocusReason)
				return operationTarget
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

    Timer {
        id: previewHydrationTimer
        interval: 16
        repeat: false
        onTriggered: root.flushPreviewHydrationQueue()
    }

    Connections {
        target: activeScope
		function onScopeTokenChanged() {
			root.clearPreviewHydrationQueue()
			timeline.beginScopeChange()
		}
    }

    Component {
        id: screenShareViewComponent
        ScreenShareViewWindow { }
    }

	QmlDialog { id: productDialog }
	Connections {
		target: productDialog
		function onOpened() { root.closeProductMenus() }
	}
    Component {
        id: imageViewerComponent
        ImageViewer {
			controller: dialogState
			transientParent: root
		}
    }
    Loader {
        active: dialogState.open && dialogState.kind === "imageViewer"
        sourceComponent: imageViewerComponent
    }
    Component {
        id: attachmentViewerComponent
        AttachmentViewer {
            attachment: root.attachmentViewerPayload || ({})
			transientParent: root
            onClosing: root.attachmentViewerPayload = null
        }
    }
    Loader {
        active: root.attachmentViewerPayload !== null
        sourceComponent: attachmentViewerComponent
    }
    // Keep the isolated media QML plugin out of the main-shell import graph. The
    // media window (and therefore Chromium) is resolved only after an explicit
    // media-session action makes the backend active, and is destroyed again
    // when that session closes.
    Loader {
        id: mediaSessionWindowLoader
        active: mediaSession.active && mediaSession.detached
        asynchronous: true
        source: active ? Qt.resolvedUrl("MediaSessionWindow.qml") : ""
		onLoaded: item.transientParent = root
    }

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
			onParticipantMenuRequested: (sessionId, actions, anchorPoint, entryKind, scopeToken) =>
				root.openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken)
			onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
		}
	}

	SemanticMenu {
		id: profileMenuPopup
		objectName: "profileMenu"
		width: 292
		maximumHeight: Math.max(260, root.height - 48)
		headerTitle: String((clientSession.selfMenu || ({})).name || clientSession.selfName || qsTr("You"))
		headerSubtitle: String((clientSession.selfMenu || ({})).statusLabel
			|| clientSession.selfStatusLabel || "")
		headerTone: String((clientSession.selfMenu || ({})).statusTone || "")
		groups: root.profileMenuGroups()
		onActionRequested: (actionId, payload) =>
			uiCommands.invokeAppAction(actionId, payload && payload.payload ? payload.payload : ({}))
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
				onActionRequested: actionId => {
					if (root.contextParticipantEntryKind === "listener")
						uiCommands.invokeScopeAction(root.contextParticipantScopeToken, actionId)
					else
						uiCommands.invokeParticipantAction(root.contextParticipantId, actionId)
				}
				onValueRequested: (actionId, value, finalValue) => {
					if (root.contextParticipantEntryKind === "listener")
						uiCommands.invokeScopeActionValue(root.contextParticipantScopeToken,
							actionId, value, finalValue)
					else
						uiCommands.invokeParticipantActionValue(root.contextParticipantId,
							actionId, value, finalValue)
				}
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

	Item {
		id: operationOverlay
		objectName: "asyncOperationOverlay"
		parent: timeline
		x: Math.max(Theme.space3, timeline.width - width - Theme.space3)
		y: Theme.space3
		width: Math.min(380, Math.max(1, timeline.width - Theme.space3 * 2))
		height: operationList.count > 0
			? Math.min(Math.max(56, operationList.contentHeight), maximumHeight) : 0
		readonly property int maximumHeight: Math.max(56, timeline.height - Theme.space3 * 2)
		readonly property bool productMenuOpen: appMenuPopup.visible || profileMenuPopup.visible
			|| roomMenuPopup.visible || textRoomMenuPopup.visible
			|| participantMenuPopup.visible || chatBackgroundMenuPopup.visible
		readonly property Item firstOperationItem: operationList.count > 0
			? operationList.itemAtIndex(0) : null
		readonly property Item visualFixtureFocusTarget: firstOperationItem
			? firstOperationItem.visualFixtureFocusTarget : null
		visible: operationList.count > 0 && !productMenuOpen
		enabled: visible
		z: 24
		clip: true

		Behavior on height {
			NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
		}

		ListView {
			id: operationList
			objectName: "asyncOperationList"
			anchors.fill: parent
			model: operationModel
			spacing: Theme.space2
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			ScrollBar.vertical: ScrollBar { policy: operationList.contentHeight > operationList.height
				? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
			delegate: AsyncOperationCard {
				width: operationList.width - (operationList.contentHeight > operationList.height ? 10 : 0)
				maximumHeight: operationOverlay.maximumHeight
				narrowLayout: root.width < 640
				itemResultPageProvider: function(operationId, offset, limit, unsuccessfulOnly) {
					return operationModel.itemResultPage(operationId, offset, limit, unsuccessfulOnly)
				}
				onCancelRequested: operationId => operationModel.cancel(operationId)
				onDismissRequested: operationId => operationModel.dismiss(operationId)
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
					Layout.preferredHeight: root.narrowShell ? 64 : 72
                    color: Theme.panel
					border.width: 0
					DragHandler {
						target: null
						onActiveChanged: if (active) root.startSystemMove()
					}
					TapHandler {
						acceptedButtons: Qt.LeftButton
						onDoubleTapped: root.visibility === Window.Maximized
							? root.showNormal() : root.showMaximized()
					}
					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						height: 1
						color: Theme.divider
					}
                    Column {
                        anchors.left: parent.left
						anchors.leftMargin: root.narrowShell ? Theme.space3 : Theme.space5
						anchors.right: stonksHeader.visible ? stonksHeader.left : headerActions.left
						anchors.rightMargin: Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Label {
							textFormat: Text.PlainText
							width: parent.width
                            text: activeScope.label.length > 0 ? activeScope.label : clientSession.serverName
                            color: Theme.textStrong
                            font.pixelSize: Theme.fontHeading
                            font.bold: true
							elide: Text.ElideRight
                        }
                        Label {
							textFormat: Text.PlainText
                            text: activeScope.description.length > 0 ? activeScope.description : activeScope.kindLabel
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontLabel
                            elide: Text.ElideRight
							width: parent.width
                        }
                    }
					StonksHeader {
						id: stonksHeader
						objectName: "stonksConversationHeader"
						anchors.right: headerActions.left
						anchors.rightMargin: Theme.space2
						anchors.verticalCenter: parent.verticalCenter
						width: Math.max(140, Math.min(root.narrowShell ? 190 : 480,
							parent.width - headerActions.width - (root.narrowShell ? 170 : 300)))
						stonks: clientSession.stonks || ({})
						scopeToken: activeScope.scopeToken
						scopeLabel: activeScope.label
						narrowLayout: root.narrowShell || width < 300
						onOpenRequested: uiCommands.invokeAppAction("server.stonks", {})
					}
					Row {
						id: headerActions
						anchors.right: parent.right
						anchors.rightMargin: root.narrowShell ? Theme.space2 : Theme.space4
						anchors.verticalCenter: parent.verticalCenter
						spacing: 6
						ModernIconButton {
							id: navigationToggle
							objectName: "compactNavigationToggle"
							visible: root.compactNavigation
							iconName: "menu"
							Accessible.name: qsTr("Open rooms and participants")
							onClicked: navigationDrawer.open()
						}
						ModernButton {
							readonly property var share: activeScope.screenShare || ({})
							visible: !!share.visible && String(share.primaryActionId || "").length > 0
							dense: true
							text: root.narrowShell ? qsTr("Share")
								: String(share.primaryLabel || qsTr("Screen share"))
							tone: String(share.primaryTone || "neutral")
							enabled: share.primaryEnabled === undefined || !!share.primaryEnabled
							Accessible.name: String(share.primaryLabel || qsTr("Screen share"))
							Accessible.description: String(share.primaryHint || "")
							onClicked: uiCommands.invokeScopeAction(activeScope.scopeToken,
								String(share.primaryActionId || ""))
						}
						ModernIconButton {
							visible: clientSession.connected
							iconName: "search"
							Accessible.name: qsTr("Search users and rooms")
							onClicked: uiCommands.invokeAction("server.search")
						}
					ModernIconButton {
						id: appMenuButton
						objectName: "visualFixtureApplicationMenu"
						iconName: "more"
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
                    SemanticMenu {
                        id: appMenuPopup
						objectName: "applicationMenu"
						width: 292
						maximumHeight: Math.max(280, root.height - 104)
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
						headerTitle: clientSession.connected
							? qsTr("Connected to %1").arg(clientSession.serverName) : qsTr("Mumble")
						headerSubtitle: clientSession.connected
							? qsTr("%1 · %2").arg(clientSession.selfName).arg(clientSession.selfStatusLabel)
							: qsTr("Choose a server to get started")
						headerTone: clientSession.connected ? "success" : ""
						groups: clientSession.appMenus
						onActionRequested: (actionId, payload) =>
							uiCommands.invokeAppAction(actionId, payload && payload.payload ? payload.payload : ({}))
                    }
                }

                UpdateBanner {
                    Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Theme.spacing : 0
                    state: clientSession.updateBanner
                    onActionRequested: actionId => uiCommands.invokeAppAction(actionId, {})
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

				WatchTogetherBanner {
					Layout.fillWidth: true
					Layout.leftMargin: Theme.spacing
					Layout.rightMargin: Theme.spacing
					Layout.topMargin: visible ? Math.max(4, Math.round(Theme.spacing / 2)) : 0
					session: mediaSession
					participantModel: root.navigationParticipantModel
				}

                ListView {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: chatModel
                    clip: true
					spacing: 0
					leftMargin: root.timelineHorizontalMargin
					rightMargin: root.timelineHorizontalMargin
					topMargin: root.timelineVerticalMargin
					bottomMargin: root.timelineVerticalMargin
                    reuseItems: true
					property string prependAnchorId: ""
					property real prependAnchorOffset: 0
					property bool prependAnchorActive: false
					property bool restoringPrependAnchor: false
					property int prependAnchorRetries: 0
					property bool stickToBottom: true
					property bool followTailAfterInsert: false
					property bool restoringBottom: false
					property bool scopeResetPending: false
					property real bottomFollowThreshold: 48

					function isNearBottom() {
						if (count === 0 || atYEnd)
							return true
						const maximumY = Math.max(originY, originY + contentHeight - height)
						return maximumY - contentY <= bottomFollowThreshold
					}

					function requestBottomFollow() {
						if (stickToBottom && !prependAnchorActive)
							bottomFollowTimer.restart()
					}

					function beginScopeChange() {
						releasePrependAnchor()
						bottomFollowTimer.stop()
						followTailAfterInsert = false
						stickToBottom = true
						scopeResetPending = true
					}

					function firstVisibleMessageDelegate() {
						const children = contentItem ? contentItem.children : []
						let candidate = null
						for (let index = 0; index < children.length; ++index) {
							const item = children[index]
							if (!item || item.stableId === undefined || !item.visible
									|| item.accessibilityPooled === true || item.y + item.height < contentY)
								continue
							if (!candidate || item.y < candidate.y)
								candidate = item
						}
						return candidate
					}

					function capturePrependAnchor() {
						const item = firstVisibleMessageDelegate()
						if (!item)
							return false
						prependAnchorId = String(item.stableId || "")
						if (prependAnchorId.length === 0)
							return false
						prependAnchorOffset = item.y - contentY
						prependAnchorRetries = 0
						prependAnchorActive = true
						stickToBottom = false
						return true
					}

					function releasePrependAnchor() {
						prependAnchorSettleTimer.stop()
						prependAnchorActive = false
						prependAnchorId = ""
						prependAnchorRetries = 0
					}

					function restorePrependAnchor() {
						if (!prependAnchorActive || restoringPrependAnchor)
							return
						const row = chatModel.rowForStableId(prependAnchorId)
						if (row < 0) {
							releasePrependAnchor()
							return
						}
						let item = itemAtIndex(row)
						if (!item) {
							if (++prependAnchorRetries > 8) {
								releasePrependAnchor()
								return
							}
							positionViewAtIndex(row, ListView.Beginning)
							Qt.callLater(function() { timeline.restorePrependAnchor() })
							return
						}
						restoringPrependAnchor = true
						const minimumY = originY
						const maximumY = Math.max(minimumY, originY + contentHeight - height)
						contentY = Math.max(minimumY, Math.min(maximumY, item.y - prependAnchorOffset))
						restoringPrependAnchor = false
						prependAnchorSettleTimer.restart()
					}

					onContentHeightChanged: {
						if (prependAnchorActive && !restoringPrependAnchor)
							Qt.callLater(function() { timeline.restorePrependAnchor() })
						else
							requestBottomFollow()
					}
					onMovementStarted: {
						if (!restoringPrependAnchor && !restoringBottom) {
							stickToBottom = false
							releasePrependAnchor()
						}
					}
					onMovementEnded: {
						if (!restoringBottom) {
							stickToBottom = isNearBottom()
							requestBottomFollow()
						}
					}

					Timer {
						id: prependAnchorSettleTimer
						interval: 1500
						repeat: false
						onTriggered: timeline.releasePrependAnchor()
					}

					Timer {
						id: bottomFollowTimer
						interval: 0
						repeat: false
						onTriggered: {
							if (!timeline.stickToBottom || timeline.prependAnchorActive)
								return
							timeline.restoringBottom = true
							timeline.positionViewAtEnd()
							Qt.callLater(function() {
								timeline.restoringBottom = false
								timeline.scopeResetPending = false
							})
						}
					}

					ModernButton {
						id: jumpToLatestButton
						parent: timeline
						anchors.horizontalCenter: parent.horizontalCenter
						anchors.bottom: parent.bottom
						anchors.bottomMargin: Theme.space3
						z: 30
						visible: timeline.count > 0 && !timeline.stickToBottom
						dense: true
						tone: "accent"
						text: qsTr("Jump to latest")
						Accessible.description: qsTr("Scroll to the newest message")
						onClicked: {
							timeline.stickToBottom = true
							timeline.requestBottomFollow()
						}
					}

					Connections {
						target: chatModel
						function onRowsAboutToChange(first, last) {
							if (!timeline.scopeResetPending && !timeline.stickToBottom
									&& !timeline.prependAnchorActive)
								timeline.capturePrependAnchor()
						}
						function onDataChanged(topLeft, bottomRight, roles) {
							if (timeline.prependAnchorActive)
								Qt.callLater(function() { timeline.restorePrependAnchor() })
							else
								timeline.requestBottomFollow()
						}
						function onRowsAboutToBeInserted(parentIndex, first, last) {
							timeline.followTailAfterInsert = false
							if (timeline.scopeResetPending) {
								timeline.followTailAfterInsert = true
							} else if (first === 0 && timeline.count > 0) {
								timeline.capturePrependAnchor()
							} else if (first >= timeline.count) {
								timeline.followTailAfterInsert = timeline.count === 0
									|| (timeline.stickToBottom && timeline.isNearBottom())
							}
						}
						function onRowsInserted(parentIndex, first, last) {
							if (first === 0 && timeline.prependAnchorActive)
								Qt.callLater(function() { timeline.restorePrependAnchor() })
							else if (timeline.followTailAfterInsert) {
								timeline.stickToBottom = true
								timeline.requestBottomFollow()
							}
							timeline.followTailAfterInsert = false
						}
						function onModelReset() {
							timeline.releasePrependAnchor()
							timeline.stickToBottom = true
							timeline.requestBottomFollow()
						}
						function onCountChanged() {
							if (timeline.count === 0) {
								timeline.releasePrependAnchor()
								timeline.stickToBottom = true
							}
						}
					}
					TapHandler {
						acceptedButtons: Qt.RightButton
						onTapped: point => {
							const row = timeline.itemAt(point.position.x + timeline.contentX,
								point.position.y + timeline.contentY)
							if (!row || row.stableId === undefined)
								root.openChatBackgroundMenu(
									timeline.mapToItem(null, point.position.x, point.position.y))
						}
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
					delegate: ChatMessageFrame {
						id: messageDelegate
						required property int index
						function openAutomationActions() {
							openMessageActions()
							return messageActions
						}
						function openMessageActions() {
							messageActions.targetId = stableId
							messageActions.targetCanReply = canReply
							messageActions.targetCanReact = canReact
							messageActions.targetCanDelete = canDelete
							messageActions.targetCanRetry = !!source.deliveryCanRetry
							messageActions.open()
						}
						function closeMessageActionsForReuse() {
							if (!messageActions)
								return
							if (messageActions.visible)
								messageActions.close()
							messageActions.targetId = ""
						}
						property bool accessibilityPooled: false
						readonly property bool previewNeedsHydration: !!preview
							&& Object.keys(preview).length > 0
							&& (preview.state === "loading" || preview.loading === true)
						readonly property bool inHydrationWindow: !accessibilityPooled
							&& y + height >= timeline.contentY - timeline.height * 0.5
							&& y <= timeline.contentY + timeline.height * 1.5
						readonly property bool inVisibleViewport: !accessibilityPooled
							&& y + height >= timeline.contentY
							&& y <= timeline.contentY + timeline.height
						function requestPreviewHydrationIfNeeded() {
							if (previewNeedsHydration && inHydrationWindow)
								root.queuePreviewHydration(stableId, inVisibleViewport)
						}
						ListView.onPooled: {
							closeMessageActionsForReuse()
							accessibilityPooled = true
						}
						ListView.onReused: {
							closeMessageActionsForReuse()
							accessibilityPooled = false
							messagePreviewCard.resetForReuse()
							Qt.callLater(function() { messageDelegate.requestPreviewHydrationIfNeeded() })
						}
						onPreviewNeedsHydrationChanged: requestPreviewHydrationIfNeeded()
						onInHydrationWindowChanged: requestPreviewHydrationIfNeeded()
						onStableIdChanged: {
							closeMessageActionsForReuse()
							requestPreviewHydrationIfNeeded()
						}
						Component.onCompleted: requestPreviewHydrationIfNeeded()
						Accessible.ignored: accessibilityPooled
						required property string title
                        required property string subtitle
                        required property string status
                        required property string avatarUrl
                        required property string timestamp
                        required property string replyActor
                        required property string replySnippet
                        required property var reactions
                        required property var bodySegments
                        required property var preview
                        required property var attachments
                        required property var source
                        required property bool deleted
                        required property bool canReply
						required property bool canReact
						required property bool canDelete
						systemMessage: !!source.system
							|| (!source.actorKey && avatarUrl.length === 0 && !canReply && !canReact && !own)
						startsGroup: root.messageStartsGroup(index, source, title)
						bodyImplicitHeight: messageRow.implicitHeight
						laneAvailableWidth: Math.max(1, timeline.width
							- timeline.leftMargin - timeline.rightMargin)
						laneMaximumWidth: root.conversationLaneMaximumWidth
						readonly property bool hasPreviewContent: !!preview && Object.keys(preview).length > 0
						readonly property bool hasAttachmentContent: !!attachments && attachments.length > 0
						readonly property bool hasReplyContent: replyActor.length > 0 || replySnippet.length > 0
						readonly property bool hasReactionContent: !!reactions && reactions.length > 0
						wideContent: hasPreviewContent || hasAttachmentContent || hasReplyContent
							|| hasReactionContent || hasDeliveryStatus
						preferredOwnWidth: root.preferredOutgoingMessageWidth(bodySegments, startsGroup)
						readonly property string deliveryState: String(source.deliveryState || status || "").trim().toLowerCase()
						readonly property string deliveryLabel: String(source.deliveryLabel || status || "").trim()
						readonly property bool hasDeliveryStatus: deliveryState === "sending"
							|| deliveryState === "failed" || deliveryState === "cancelled"
						width: laneAvailableWidth
						TapHandler {
							acceptedButtons: Qt.RightButton
							onTapped: messageDelegate.openMessageActions()
						}
                        RowLayout {
							id: messageRow
							anchors.fill: parent
							spacing: Theme.space2
                            Rectangle {
								Layout.preferredWidth: messageDelegate.own || messageDelegate.systemMessage ? 0 : Theme.avatarMedium
								Layout.preferredHeight: Layout.preferredWidth
                                Layout.alignment: Qt.AlignTop
								visible: !messageDelegate.own && !messageDelegate.systemMessage
								radius: width / 2
                                color: Theme.strip
                                clip: true
								opacity: messageDelegate.startsGroup ? 1 : 0
                                Image {
                                    id: avatarImage
                                    anchors.fill: parent
                                    source: root.safeRenderImageSource(avatarUrl)
                                    asynchronous: true
                                    cache: false
                                    sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                                    fillMode: Image.PreserveAspectCrop
                                    visible: avatarImage.status === Image.Ready
                                }
                                Label {
									textFormat: Text.PlainText
                                    anchors.centerIn: parent
                                    visible: avatarUrl.length === 0
                                    text: (title || "S").slice(0, 1).toUpperCase()
                                    color: Theme.textStrong
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space1
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space2
									visible: messageDelegate.startsGroup || messageDelegate.systemMessage
									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: title || qsTr("System")
										color: messageDelegate.systemMessage ? Theme.textMuted : Theme.accent
										font.bold: true
										font.pixelSize: Theme.fontCaption
										elide: Text.ElideRight
									}
                                    Label { textFormat: Text.PlainText; text: timestamp; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; visible: timestamp.length > 0 }
									Label {
										Layout.maximumWidth: root.narrowShell ? 72 : 120
										textFormat: Text.PlainText
										text: status
										color: Theme.textMuted
										font.pixelSize: Theme.fontCaption
										visible: status.length > 0
										elide: Text.ElideRight
									}
									ModernIconButton {
                                        visible: messageDelegate.canReply || messageDelegate.canReact
                                                 || messageDelegate.canDelete || !!messageDelegate.source.deliveryCanRetry
										iconName: "more"
                                        Accessible.name: qsTr("Message actions")
										onClicked: messageDelegate.openMessageActions()
										ModernMenu {
											id: messageActions
											property string targetId: ""
											property bool targetCanReply: false
											property bool targetCanReact: false
											property bool targetCanDelete: false
											property bool targetCanRetry: false
											onClosed: targetId = ""
											MenuItem {
												text: qsTr("Reply")
												visible: messageActions.targetCanReply
												onTriggered: uiCommands.replyToMessage(messageActions.targetId)
											}
											MenuItem {
												text: qsTr("Add reaction")
												visible: messageActions.targetCanReact
												onTriggered: uiCommands.toggleMessageReaction(messageActions.targetId, "👍")
											}
											MenuItem {
												text: qsTr("Retry")
												visible: messageActions.targetCanRetry
												onTriggered: uiCommands.retryMessage(messageActions.targetId)
											}
											MenuItem {
												text: qsTr("Delete")
												visible: messageActions.targetCanDelete
												onTriggered: uiCommands.deleteMessage(messageActions.targetId)
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    id: previewCard
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: replyColumn.implicitHeight + Theme.space3
                                    visible: replyActor.length > 0 || replySnippet.length > 0
                                    radius: Theme.innerRadius
                                    color: Theme.strip
                                    Column {
                                        id: replyColumn
                                        anchors.fill: parent
                                        anchors.margins: Theme.space2
                                        Label { textFormat: Text.PlainText; text: replyActor; color: Theme.accent; font.pixelSize: Theme.fontCaption; font.bold: true }
                                        Label { width: parent.width; textFormat: Text.PlainText; text: replySnippet; color: Theme.textMuted; font.pixelSize: Theme.fontLabel; elide: Text.ElideRight }
                                    }
                                }
								RichMessageBody {
									Layout.fillWidth: true
									visible: !messageDelegate.deleted
                                    segments: messageDelegate.bodySegments || []
                                    textColor: Theme.textMain
                                    pixelSize: Theme.fontBody
									onLinkRequested: link => Qt.openUrlExternally(link)
								}
								RowLayout {
									Layout.fillWidth: true
									visible: messageDelegate.hasDeliveryStatus
									spacing: Theme.space2
									Label {
										Layout.fillWidth: true
										textFormat: Text.PlainText
										text: messageDelegate.deliveryLabel.length > 0
											? messageDelegate.deliveryLabel
											: messageDelegate.deliveryState
										color: messageDelegate.deliveryState === "failed" ? Theme.danger
											: messageDelegate.deliveryState === "cancelled" ? Theme.textMuted : Theme.warning
										font.pixelSize: Theme.fontCaption
										font.weight: Font.Medium
										Accessible.name: text
									}
									ModernButton {
										visible: !!messageDelegate.source.deliveryCanRetry
										dense: true
										tone: "retry"
										text: String(messageDelegate.source.deliveryRetryLabel || qsTr("Retry"))
										onClicked: uiCommands.retryMessage(messageDelegate.stableId)
									}
								}
								Label {
									textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    visible: messageDelegate.deleted
                                    text: qsTr("Message deleted")
                                    color: Theme.textMuted
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Theme.fontBody
                                    font.italic: true
                                }
                                AttachmentGallery {
                                    Layout.fillWidth: true
                                    attachments: messageDelegate.attachments || []
                                    onAttachmentRequested: attachment => root.openAttachment(attachment)
                                    onAttachmentRefreshRequested: root.queuePreviewHydration(messageDelegate.stableId, true)
                                }
								RichPreviewCard {
									id: messagePreviewCard
                                    Layout.fillWidth: true
                                    visible: !!messageDelegate.preview && Object.keys(messageDelegate.preview).length > 0
									preview: messageDelegate.preview || ({})
									mediaSessionController: mediaSession
									mediaSessionId: messageDelegate.stableId
									previewIdentity: messageDelegate.stableId + "|" + String(messageDelegate.preview
										? (messageDelegate.preview.url || messageDelegate.preview.embedUrl
											|| messageDelegate.preview.mediaUrl || messageDelegate.preview.title || "") : "")
									renderActive: messageDelegate.inHydrationWindow && !messageDelegate.accessibilityPooled
									watchTogetherAvailable: !mediaSession.sharedAvailable
                                    onExternalOpenRequested: url => Qt.openUrlExternally(url)
                                    onImageOpenRequested: (source, title) => root.openAttachment({
                                        "url": source, "thumbnailUrl": source, "kind": "image", "alt": title
                                    }, title)
									onImageRefreshRequested: root.queuePreviewHydration(messageDelegate.stableId, true)
									onDirectMediaRequested: (url, mime, audioUrl, audioMime, title) =>
										mediaSession.openDirect(url, mime, audioUrl, audioMime,
																messageDelegate.stableId)
									onInlinePlayRequested: (url, provider) => mediaSession.openInline(url, provider,
										messageDelegate.stableId)
									onPopoutPlayRequested: (url, provider) => mediaSession.open(url, provider,
										messageDelegate.stableId)
                                    onWatchTogetherRequested: (url, provider, title) => {
                                        if (!mediaSession.sharedAvailable)
                                            mediaSession.startShared(url, provider, title)
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: Theme.space2
									visible: !!reactions && reactions.length > 0
                                    Repeater {
                                        model: reactions || []
										delegate: Button {
											id: reactionButton
                                            required property var modelData
											implicitWidth: contentItem.implicitWidth + Theme.space3
											implicitHeight: Math.max(24, Theme.avatarSmall)
											enabled: messageDelegate.canReact && (modelData.emoji || "").length > 0
											activeFocusOnTab: true
											focusPolicy: Qt.StrongFocus
											Accessible.name: qsTr("%1 reaction, %2").arg(modelData.emoji || "")
												.arg(modelData.count || 0)
											background: Rectangle {
												radius: reactionButton.implicitHeight / 2
												color: reactionButton.modelData.selfReacted ? Theme.selected : Theme.strip
												border.color: reactionButton.activeFocus ? Theme.focus : Theme.divider
											}
											contentItem: Label {
												textFormat: Text.PlainText
												text: (reactionButton.modelData.emoji || "") + " "
													+ (reactionButton.modelData.count || 0)
												color: Theme.textMain
												font.pixelSize: Theme.fontCaption
												horizontalAlignment: Text.AlignHCenter
												verticalAlignment: Text.AlignVCenter
											}
											onClicked: uiCommands.toggleMessageReaction(messageDelegate.stableId,
																			 modelData.emoji)
                                        }
                                    }
                                }
                            }
                        }
                    }
					Rectangle {
						id: emptyConversationState
						objectName: "emptyConversationState"
						anchors.centerIn: parent
						width: Math.max(1, Math.min(380, timeline.width
							- root.timelineHorizontalMargin * 2 - Theme.space4 * 2))
						height: emptyConversationContent.implicitHeight + Theme.space5 * 2
						visible: chatModel.count === 0
						radius: Theme.innerRadius
						color: Theme.surfaceRaised
						border.color: Theme.surfaceBorder
						Accessible.role: Accessible.Pane
						Accessible.name: emptyConversationTitle.text
						Accessible.description: emptyConversationDetail.text

						Column {
							id: emptyConversationContent
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.verticalCenter: parent.verticalCenter
							anchors.leftMargin: Theme.space5
							anchors.rightMargin: Theme.space5
							spacing: Theme.space2
							ModernBusyIndicator {
								objectName: "emptyConversationBusyIndicator"
								anchors.horizontalCenter: parent.horizontalCenter
								visible: activeScope.loading
								running: visible
								Accessible.name: qsTr("Loading conversation")
							}
							Label {
								id: emptyConversationTitle
								width: parent.width
								textFormat: Text.PlainText
								text: activeScope.loading ? qsTr("Loading conversation")
									: !clientSession.connected ? qsTr("Connect to Mumble")
									: activeScope.canSend ? qsTr("This conversation is quiet")
									: qsTr("Choose a conversation")
								color: Theme.textStrong
								font.pixelSize: Theme.fontTitle
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								wrapMode: Text.Wrap
							}
							Label {
								id: emptyConversationDetail
								width: parent.width
								textFormat: Text.PlainText
								text: activeScope.loading ? qsTr("Messages will appear here when history is ready.")
									: !clientSession.connected ? qsTr("Open the server browser to load rooms and messages.")
									: activeScope.canSend ? qsTr("Be the first to write in %1.").arg(activeScope.label)
									: qsTr("Select a text room, voice room, or direct message to get started.")
								color: Theme.textMuted
								font.pixelSize: Theme.fontBody
								horizontalAlignment: Text.AlignHCenter
								wrapMode: Text.Wrap
							}
						}
					}
                }

				Rectangle {
					id: composerSurface
                    Layout.fillWidth: true
					Layout.preferredHeight: (activeScope.hasPendingReply ? 108 : 72)
						+ Math.min(3, Math.max(0, composerInput.lineCount - 1)) * 18
						+ (composer.attachments.count > 0 ? 58 : 0)
						+ (composer.autocompleteItems.length > 0 ? 34 : 0)
                    color: Theme.strip
					border.width: 0
					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.top: parent.top
						height: 1
						color: Theme.divider
					}
                    RowLayout {
						anchors.top: parent.top
						anchors.bottom: parent.bottom
						anchors.horizontalCenter: parent.horizontalCenter
						anchors.topMargin: root.narrowShell ? Theme.space2 : 14
						anchors.bottomMargin: anchors.topMargin
						width: Math.max(1, Math.min(root.conversationLaneMaximumWidth,
							parent.width - (root.narrowShell ? Theme.space2 : 14) * 2))
						spacing: root.narrowShell ? Theme.space2 : Theme.space3
                        ModernIconButton {
							objectName: "composerAttachButton"
                            visible: activeScope.canAttachImages
                            enabled: activeScope.canSend
							dense: root.narrowShell
							iconName: "attach"
                            Accessible.name: qsTr("Attach image")
                            onClicked: uiCommands.chooseAttachment()
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.innerRadius
							color: Theme.panel
							border.color: composerInput.activeFocus ? Theme.focus : Theme.divider
							border.width: composerInput.activeFocus ? Theme.focusRingWidth : 1
							Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
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
										textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        text: qsTr("Replying to %1: %2").arg(activeScope.replyActor)
                                                .arg(activeScope.replySnippet)
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
									ModernIconButton {
										dense: true
										iconName: "close"
                                        Accessible.name: qsTr("Cancel reply")
                                        onClicked: uiCommands.cancelPendingReply()
                                    }
                                }
                                ListView {
									id: attachmentStrip
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
										width: Math.min(150, Math.max(112, attachmentStrip.width - 4))
										height: 48; radius: 7; color: Theme.strip; border.color: Theme.divider
                                        RowLayout { anchors.fill: parent; anchors.margins: 4
                                            Image { Layout.preferredWidth: 38; Layout.preferredHeight: 38; source: root.safeRenderImageSource(thumbnailUrl); asynchronous: true; cache: false; fillMode: Image.PreserveAspectCrop }
											ColumnLayout {
												Layout.fillWidth: true
												Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: fileName; color: Theme.textMain; elide: Text.ElideMiddle; font.pixelSize: 9 }
												Label { Layout.fillWidth: true; textFormat: Text.PlainText; visible: status !== "ready"; text: error || status; color: status === "failed" ? Theme.danger : Theme.textMuted; elide: Text.ElideRight; font.pixelSize: 8 }
											}
											ModernIconButton { dense: true; visible: status === "failed"; iconName: "retry"; onClicked: composer.retryAttachment(stableId); Accessible.name: qsTr("Retry %1").arg(fileName) }
											ModernIconButton { dense: true; iconName: "close"; onClicked: composer.removeAttachment(stableId); Accessible.name: qsTr("Remove %1").arg(fileName) }
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
									delegate: ModernButton {
										required property var modelData
										dense: true
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
							objectName: "composerSendButton"
							tone: "primary"
							dense: root.narrowShell
							text: qsTr("Send")
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
				onParticipantMenuRequested: (sessionId, actions, anchorPoint, entryKind, scopeToken) =>
					root.openParticipantMenu(sessionId, actions, anchorPoint, entryKind, scopeToken)
				onProfileMenuRequested: anchorPoint => root.openProfileMenu(anchorPoint)
            }
        }
    }
}
