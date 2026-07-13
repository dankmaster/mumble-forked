import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: navigationRail
	objectName: "navigationRail"
    required property var roomModel
    required property var participantModel
    required property var selectionState
    required property var uiCommands
    required property var clientSession
	property bool commitOnSelection: false
    signal selectionCommitted()
	signal scopeMenuRequested(string scopeToken, string kind, var actions, var anchorPoint)
	signal participantMenuRequested(string sessionId, var actions, var anchorPoint)
	signal profileMenuRequested(var anchorPoint)

	function requestScopeMenu(scopeToken, kind, payload, anchorPoint) {
		uiCommands.selectScope(scopeToken)
		scopeMenuRequested(scopeToken, kind, (payload.source && payload.source.actions) || [], anchorPoint)
	}

	function requestParticipantMenu(sessionId, payload, anchorPoint) {
		uiCommands.selectParticipant(sessionId)
		participantMenuRequested(sessionId, (payload.source && payload.source.actions) || [], anchorPoint)
	}

	function dropPlacement(y, height) {
		if (!Number.isFinite(y) || !Number.isFinite(height) || height <= 0)
			return "inside"
		if (y < height * 0.25)
			return "before"
		if (y > height * 0.75)
			return "after"
		return "inside"
	}

	function normalizedProtocolId(value, allowZero) {
		const text = String(value === undefined || value === null ? "" : value).trim()
		if (!/^[0-9]+$/.test(text))
			return ""
		const number = Number(text)
		if (!Number.isFinite(number) || Math.floor(number) !== number
				|| number > 4294967295 || (!allowZero && number === 0))
			return ""
		return String(number)
	}

	function normalizedChannelScopeToken(value) {
		const token = String(value === undefined || value === null ? "" : value).trim()
		if (token.substring(0, 8) !== "channel:")
			return ""
		const channelId = normalizedProtocolId(token.substring(8), true)
		return channelId.length > 0 ? "channel:" + channelId : ""
	}

	function internalDragPayload(source) {
		const name = source ? String(source.objectName || "") : ""
		const participantPrefix = "mumble-drag:participant:"
		const roomPrefix = "mumble-drag:voice-room:"
		if (name.substring(0, participantPrefix.length) === participantPrefix)
			return { "kind": "participant", "stableId": name.substring(participantPrefix.length) }
		if (name.substring(0, roomPrefix.length) === roomPrefix)
			return { "kind": "voice-room", "stableId": name.substring(roomPrefix.length) }
		return { "kind": "", "stableId": "" }
	}

	// Drag payloads contain stable protocol IDs only. The controller validates
	// them again against the current models before forwarding a move request, so
	// delegates can be recycled while a later model update is applied safely.
	function dispatchStableDrop(kind, stableId, targetScopeToken, y, height) {
		const sourceKind = String(kind || "").trim()
		const target = normalizedChannelScopeToken(targetScopeToken)
		if (target.length === 0)
			return false
		if (sourceKind === "participant") {
			const sessionId = normalizedProtocolId(stableId, false)
			if (sessionId.length === 0)
				return false
			uiCommands.moveParticipant(sessionId, target)
			return true
		}
		if (sourceKind === "voice-room") {
			const sourceScope = normalizedChannelScopeToken(stableId)
			if (sourceScope.length === 0 || sourceScope === "channel:0" || sourceScope === target)
				return false
			uiCommands.moveScope(sourceScope, target, dropPlacement(y, height))
			return true
		}
		return false
	}

	function dispatchStableDropAtScene(kind, stableId, scenePoint) {
		const delegates = rooms.contentItem ? rooms.contentItem.children : []
		for (let index = 0; index < delegates.length; ++index) {
			const candidate = delegates[index]
			if (!candidate || !candidate.visible || candidate.scopeToken === undefined)
				continue
			const local = candidate.mapFromItem(null, scenePoint.x, scenePoint.y)
			if (local.x < 0 || local.y < 0 || local.x > candidate.width || local.y > candidate.height)
				continue
			return dispatchStableDrop(kind, stableId, candidate.scopeToken, local.y, candidate.height)
		}
		return false
	}

	function focusInitialItem() {
		if (rooms.count <= 0) {
			navigationRail.forceActiveFocus()
			return
		}
		let selectedIndex = -1
		for (let index = 0; index < rooms.count; ++index) {
			const row = roomModel.get(index)
			if (row && row.selected) {
				selectedIndex = index
				break
			}
		}
		rooms.currentIndex = selectedIndex >= 0 ? selectedIndex : Math.max(0, rooms.currentIndex)
		rooms.positionViewAtIndex(rooms.currentIndex, ListView.Contain)
		Qt.callLater(function() {
			if (rooms.currentItem)
				rooms.currentItem.forceActiveFocus()
			else
				rooms.forceActiveFocus()
		})
	}

	implicitWidth: 310
	implicitHeight: 600
	activeFocusOnTab: true
    color: Theme.rail
    border.color: Theme.divider
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            color: Theme.panel
            border.color: Theme.divider
            Column {
                anchors.left: parent.left
				anchors.right: parent.right
                anchors.leftMargin: 18
				anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
				Label {
					textFormat: Text.PlainText
					width: parent.width
					text: clientSession.serverName
					color: Theme.textStrong
					font.bold: true
					font.pixelSize: 14
					elide: Text.ElideRight
				}
				Label {
					textFormat: Text.PlainText
					width: parent.width
					text: clientSession.connectionLabel
					color: clientSession.connected ? Theme.accent : Theme.textMuted
					font.pixelSize: 11
					elide: Text.ElideRight
				}
            }
        }
        Label { Layout.margins: 18; textFormat: Text.PlainText; text: qsTr("ROOMS"); color: Theme.textMuted; font.pixelSize: 10; font.bold: true }
        ListView {
            id: rooms
			objectName: "navigationRooms"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: roomModel
            clip: true
            reuseItems: true
			// Incubate a bounded number of nearby delegates so keyboard and
			// accessibility traversal remain deterministic without realizing the
			// complete room tree.
			cacheBuffer: 400
            spacing: 2
            leftMargin: 10
            rightMargin: 10
			delegate: Rectangle {
				id: roomDelegate
				property bool accessibilityPooled: false
				ListView.onPooled: {
					roomMouse.clearDragSnapshot()
					accessibilityPooled = true
				}
				ListView.onReused: {
					roomMouse.clearDragSnapshot()
					accessibilityPooled = false
				}
				function activateSelection() {
					uiCommands.selectScope(scopeToken)
					if (navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
				function joinVoiceRoom() {
					if (kind === "voice")
						uiCommands.joinVoiceChannel(scopeToken)
					else
						activateSelection()
					if (kind === "voice" && navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
                required property string stableId
                required property string scopeToken
                required property string title
                required property string subtitle
                required property string kind
                required property bool selected
                required property int depth
                required property int unreadCount
                required property var payload
				objectName: "navigationRoom_" + stableId
                width: rooms.width - 20
                height: subtitle.length > 0 ? 48 : 38
                radius: 8
				color: roomDropArea.containsDrag ? Theme.selected
					: selected ? Theme.selected : roomMouse.containsMouse ? Theme.panel : "transparent"
				border.width: roomDropArea.containsDrag ? 2 : 0
				border.color: roomDropArea.containsDrag ? Theme.accent : "transparent"
				opacity: roomMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: !accessibilityPooled
				Accessible.ignored: accessibilityPooled
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: subtitle
                Accessible.selected: selected
				Accessible.onPressAction: activateSelection()
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10 + Math.min(depth, 5) * 12
                    anchors.rightMargin: 10
                    spacing: 2
                    Label { width: parent.width; textFormat: Text.PlainText; text: title; color: Theme.textStrong; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                    Label { width: parent.width; textFormat: Text.PlainText; visible: subtitle.length > 0; text: subtitle; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                }
                Rectangle {
                    visible: unreadCount > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(20, unreadLabel.implicitWidth + 10)
                    height: 20
                    radius: 10
                    color: Theme.accent
                    Label {
						textFormat: Text.PlainText
                        id: unreadLabel
                        anchors.centerIn: parent
                        text: unreadCount > 99 ? "99+" : unreadCount
                        color: Theme.strip
                        font.pixelSize: 9
                        font.bold: true
                    }
                }
				Item {
					id: roomDragSource
					// The delegate can be rebound while a pointer drag is active. Keep the
					// drag payload tied to the row that received the press rather than the
					// row currently displayed by this reused delegate.
					objectName: "mumble-drag:voice-room:" + roomMouse.dragSourceScopeToken
					property bool dropHandled: false
					width: 1
					height: 1
				}
				DropArea {
					id: roomDropArea
					anchors.fill: parent
					enabled: kind === "voice"
					keys: ["mumble/participant", "mumble/voice-room"]
					onDropped: drop => {
						const sourcePayload = navigationRail.internalDragPayload(drop.source)
						let sourceKind = sourcePayload.kind
						let sourceId = sourcePayload.stableId
						if (sourceKind.length === 0) {
							const participantId = drop.getDataAsString(
								"application/x-mumble-participant-session")
							if (participantId.length > 0) {
								sourceKind = "participant"
								sourceId = participantId
							} else {
								sourceKind = "voice-room"
								sourceId = drop.getDataAsString("application/x-mumble-room-scope")
							}
						}
						const handled = navigationRail.dispatchStableDrop(
							sourceKind, sourceId, scopeToken, drop.y, height)
						if (handled) {
							if (drop.source && drop.source.dropHandled !== undefined)
								drop.source.dropHandled = true
							drop.acceptProposedAction()
						}
					}
				}
				MouseArea {
					id: roomMouse
					objectName: "navigationRoomMouse_" + stableId
					anchors.fill: parent
					z: 2
					hoverEnabled: true
					acceptedButtons: Qt.LeftButton | Qt.RightButton
					preventStealing: true
					drag.target: dragSourceScopeToken.length > 0 ? roomDragSource : undefined
					drag.axis: Drag.XAndYAxis
					drag.threshold: 8
					property point pressScene: Qt.point(0, 0)
					property bool dragStarted: false
					property bool suppressClick: false
					property string dragSourceScopeToken: ""
					function clearDragSnapshot(preserveClickSuppression) {
						dragStarted = false
						dragSourceScopeToken = ""
						roomDragSource.dropHandled = false
						roomDragSource.x = 0
						roomDragSource.y = 0
						if (!preserveClickSuppression)
							suppressClick = false
					}
					onPressed: mouse => {
						pressScene = roomDelegate.mapToItem(null, mouse.x, mouse.y)
						dragStarted = false
						suppressClick = false
						dragSourceScopeToken = mouse.button === Qt.LeftButton && kind === "voice"
							? scopeToken : ""
						roomDragSource.dropHandled = false
					}
					onPositionChanged: mouse => {
						if (!pressed || dragSourceScopeToken.length === 0)
							return
						const current = roomDelegate.mapToItem(null, mouse.x, mouse.y)
						const dx = current.x - pressScene.x
						const dy = current.y - pressScene.y
						if (dx * dx + dy * dy >= 64)
							dragStarted = true
					}
					onReleased: mouse => {
						const sourceScopeToken = dragSourceScopeToken
						suppressClick = dragStarted
						if (dragStarted && sourceScopeToken.length > 0 && !roomDragSource.dropHandled)
							navigationRail.dispatchStableDropAtScene("voice-room", sourceScopeToken,
								roomDelegate.mapToItem(null, mouse.x, mouse.y))
						clearDragSnapshot(true)
					}
					onCanceled: {
						clearDragSnapshot()
						suppressClick = false
					}
					onClicked: mouse => {
						if (suppressClick) {
							suppressClick = false
							return
						}
						if (mouse.button === Qt.RightButton)
							navigationRail.requestScopeMenu(scopeToken, kind, payload,
								roomDelegate.mapToItem(null, mouse.x, mouse.y))
						else {
							roomDelegate.activateSelection()
						}
					}
					onPressAndHold: mouse => navigationRail.requestScopeMenu(scopeToken, kind, payload,
						roomDelegate.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: {
						if (kind === "voice")
							roomDelegate.joinVoiceRoom()
					}
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Menu
						|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
						navigationRail.requestScopeMenu(scopeToken, kind, payload,
							mapToItem(null, width / 2, height / 2))
						event.accepted = true
					}
				}
                Keys.onReturnPressed: event => {
					roomDelegate.activateSelection()
                    event.accepted = true
                }
                Keys.onEnterPressed: event => {
					roomDelegate.activateSelection()
                    event.accepted = true
                }
                Keys.onSpacePressed: event => {
					roomDelegate.joinVoiceRoom()
                    event.accepted = true
                }
            }
        }
        Label {
			textFormat: Text.PlainText
            Layout.leftMargin: 18
            Layout.topMargin: 10
            Layout.bottomMargin: 6
            text: qsTr("PARTICIPANTS")
            color: Theme.textMuted
            font.pixelSize: 10
            font.bold: true
			visible: participants.count > 0
        }
        ListView {
            id: participants
			objectName: "navigationParticipants"
            Layout.fillWidth: true
			Layout.preferredHeight: Math.min(count * 42, 180)
            model: participantModel
            clip: true
            reuseItems: true
            leftMargin: 10
            rightMargin: 10
			delegate: Rectangle {
				id: participantDelegate
				property bool accessibilityPooled: false
				ListView.onPooled: {
					participantMouse.clearDragSnapshot()
					accessibilityPooled = true
				}
				ListView.onReused: {
					participantMouse.clearDragSnapshot()
					accessibilityPooled = false
				}
				function activateSelection() {
					uiCommands.selectParticipant(stableId)
					if (navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
				function openConversation() {
					uiCommands.openDirectMessage(stableId)
					if (navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
                required property string stableId
                required property string title
                required property string subtitle
                required property string status
                required property var payload
				objectName: "navigationParticipant_" + stableId
                width: participants.width - 20
                height: 42
                radius: 8
                color: selectionState.selectedUserSession !== undefined
                       && String(selectionState.selectedUserSession) === stableId
                       ? Theme.selected
					   : participantMouse.containsMouse ? Theme.panel : "transparent"
				opacity: participantMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: !accessibilityPooled
				Accessible.ignored: accessibilityPooled
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: subtitle
                Accessible.selected: selectionState.selectedUserSession !== undefined
                                     && String(selectionState.selectedUserSession) === stableId
				Accessible.onPressAction: activateSelection()
                Rectangle {
                    id: presenceDot
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: status.length > 0 && status !== "passive" ? Theme.accent : Theme.textMuted
                }
                Column {
                    anchors.left: presenceDot.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    Label { width: parent.width; textFormat: Text.PlainText; text: title; color: Theme.textStrong; font.pixelSize: 11; elide: Text.ElideRight }
                    Label { width: parent.width; textFormat: Text.PlainText; text: subtitle; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: subtitle.length > 0 }
                }
				Item {
					id: participantDragSource
					objectName: "mumble-drag:participant:" + participantMouse.dragSourceStableId
					property bool dropHandled: false
					width: 1
					height: 1
				}
				MouseArea {
					id: participantMouse
					objectName: "navigationParticipantMouse_" + stableId
					anchors.fill: parent
					z: 2
					hoverEnabled: true
					acceptedButtons: Qt.LeftButton | Qt.RightButton
					preventStealing: true
					drag.target: dragSourceStableId.length > 0 ? participantDragSource : undefined
					drag.axis: Drag.XAndYAxis
					drag.threshold: 8
					property point pressScene: Qt.point(0, 0)
					property bool dragStarted: false
					property bool suppressClick: false
					property string dragSourceStableId: ""
					function clearDragSnapshot(preserveClickSuppression) {
						dragStarted = false
						dragSourceStableId = ""
						participantDragSource.dropHandled = false
						participantDragSource.x = 0
						participantDragSource.y = 0
						if (!preserveClickSuppression)
							suppressClick = false
					}
					onPressed: mouse => {
						pressScene = participantDelegate.mapToItem(null, mouse.x, mouse.y)
						dragStarted = false
						suppressClick = false
						dragSourceStableId = mouse.button === Qt.LeftButton ? stableId : ""
						participantDragSource.dropHandled = false
					}
					onPositionChanged: mouse => {
						if (!pressed || dragSourceStableId.length === 0)
							return
						const current = participantDelegate.mapToItem(null, mouse.x, mouse.y)
						const dx = current.x - pressScene.x
						const dy = current.y - pressScene.y
						if (dx * dx + dy * dy >= 64)
							dragStarted = true
					}
					onReleased: mouse => {
						const sourceStableId = dragSourceStableId
						suppressClick = dragStarted
						if (dragStarted && sourceStableId.length > 0 && !participantDragSource.dropHandled)
							navigationRail.dispatchStableDropAtScene("participant", sourceStableId,
								participantDelegate.mapToItem(null, mouse.x, mouse.y))
						clearDragSnapshot(true)
					}
					onCanceled: {
						clearDragSnapshot()
						suppressClick = false
					}
					onClicked: mouse => {
						if (suppressClick) {
							suppressClick = false
							return
						}
						if (mouse.button === Qt.RightButton)
							navigationRail.requestParticipantMenu(stableId, payload,
								participantDelegate.mapToItem(null, mouse.x, mouse.y))
						else {
							participantDelegate.activateSelection()
						}
					}
					onPressAndHold: mouse => navigationRail.requestParticipantMenu(stableId, payload,
						participantDelegate.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: participantDelegate.openConversation()
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Menu
						|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
						navigationRail.requestParticipantMenu(stableId, payload,
							mapToItem(null, width / 2, height / 2))
						event.accepted = true
					}
				}
                Keys.onReturnPressed: event => {
					participantDelegate.activateSelection()
                    event.accepted = true
                }
                Keys.onEnterPressed: event => {
					participantDelegate.activateSelection()
                    event.accepted = true
                }
                Keys.onSpacePressed: event => {
					participantDelegate.openConversation()
                    event.accepted = true
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            color: Theme.strip
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { textFormat: Text.PlainText; text: clientSession.selfName; color: Theme.textStrong; font.bold: true }
                    Label { textFormat: Text.PlainText; text: clientSession.selfStatusLabel; color: Theme.textMuted; font.pixelSize: 10 }
                }
                ModernButton {
					text: clientSession.selfMuted ? qsTr("Unmute") : qsTr("Mute")
					onClicked: uiCommands.toggleSelfMute()
				}
				ToolButton {
					id: profileMenuButton
					objectName: "profileMenuButton"
					text: "⋯"
					font.pixelSize: 18
					Accessible.name: qsTr("Profile menu")
					onClicked: navigationRail.profileMenuRequested(
						profileMenuButton.mapToItem(null, width / 2, 0))
					Keys.onReturnPressed: event => {
						navigationRail.profileMenuRequested(
							profileMenuButton.mapToItem(null, width / 2, 0))
						event.accepted = true
					}
					Keys.onEnterPressed: event => {
						navigationRail.profileMenuRequested(
							profileMenuButton.mapToItem(null, width / 2, 0))
						event.accepted = true
					}
					Keys.onSpacePressed: event => {
						navigationRail.profileMenuRequested(
							profileMenuButton.mapToItem(null, width / 2, 0))
						event.accepted = true
					}
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Menu
							|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
							navigationRail.profileMenuRequested(
								profileMenuButton.mapToItem(null, width / 2, 0))
							event.accepted = true
						}
					}
				}
            }
        }
    }
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Rooms and participants")
}
