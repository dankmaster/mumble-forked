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
					width: parent.width
					text: clientSession.serverName
					color: Theme.textStrong
					font.bold: true
					font.pixelSize: 14
					elide: Text.ElideRight
				}
				Label {
					width: parent.width
					text: clientSession.connectionLabel
					color: clientSession.connected ? Theme.accent : Theme.textMuted
					font.pixelSize: 11
					elide: Text.ElideRight
				}
            }
        }
        Label { Layout.margins: 18; text: qsTr("ROOMS"); color: Theme.textMuted; font.pixelSize: 10; font.bold: true }
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
                color: selected ? Theme.selected : roomMouse.containsMouse ? Theme.panel : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: subtitle
                Accessible.selected: selected
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10 + Math.min(depth, 5) * 12
                    anchors.rightMargin: 10
                    spacing: 2
                    Label { width: parent.width; text: title; color: Theme.textStrong; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                    Label { width: parent.width; visible: subtitle.length > 0; text: subtitle; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
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
                        id: unreadLabel
                        anchors.centerIn: parent
                        text: unreadCount > 99 ? "99+" : unreadCount
                        color: Theme.strip
                        font.pixelSize: 9
                        font.bold: true
                    }
                }
                MouseArea {
                    id: roomMouse
					objectName: "navigationRoomMouse_" + stableId
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
						if (mouse.button === Qt.RightButton)
							navigationRail.requestScopeMenu(scopeToken, kind, payload,
								roomMouse.mapToItem(null, mouse.x, mouse.y))
						else {
							uiCommands.selectScope(scopeToken)
							if (navigationRail.commitOnSelection)
							navigationRail.selectionCommitted()
						}
                    }
					onPressAndHold: navigationRail.requestScopeMenu(scopeToken, kind, payload,
						roomMouse.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: {
						if (kind === "voice")
							uiCommands.joinVoiceChannel(scopeToken)
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
                    uiCommands.selectScope(scopeToken)
					navigationRail.selectionCommitted()
                    event.accepted = true
                }
                Keys.onEnterPressed: event => {
                    uiCommands.selectScope(scopeToken)
					navigationRail.selectionCommitted()
                    event.accepted = true
                }
                Keys.onSpacePressed: event => {
                    if (kind === "voice")
                        uiCommands.joinVoiceChannel(scopeToken)
                    else
                        uiCommands.selectScope(scopeToken)
					navigationRail.selectionCommitted()
                    event.accepted = true
                }
            }
        }
        Label {
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
                activeFocusOnTab: true
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: subtitle
                Accessible.selected: selectionState.selectedUserSession !== undefined
                                     && String(selectionState.selectedUserSession) === stableId
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
                    Label { width: parent.width; text: title; color: Theme.textStrong; font.pixelSize: 11; elide: Text.ElideRight }
                    Label { width: parent.width; text: subtitle; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: subtitle.length > 0 }
                }
                MouseArea {
                    id: participantMouse
					objectName: "navigationParticipantMouse_" + stableId
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
						if (mouse.button === Qt.RightButton)
							navigationRail.requestParticipantMenu(stableId, payload,
								participantMouse.mapToItem(null, mouse.x, mouse.y))
						else {
							uiCommands.selectParticipant(stableId)
							if (navigationRail.commitOnSelection)
							navigationRail.selectionCommitted()
						}
                    }
					onPressAndHold: navigationRail.requestParticipantMenu(stableId, payload,
						participantMouse.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: {
						uiCommands.openDirectMessage(stableId)
					}
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
                    uiCommands.selectParticipant(stableId)
					navigationRail.selectionCommitted()
                    event.accepted = true
                }
                Keys.onEnterPressed: event => {
                    uiCommands.selectParticipant(stableId)
					navigationRail.selectionCommitted()
                    event.accepted = true
                }
                Keys.onSpacePressed: event => {
                    uiCommands.openDirectMessage(stableId)
					navigationRail.selectionCommitted()
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
                    Label { text: clientSession.selfName; color: Theme.textStrong; font.bold: true }
                    Label { text: clientSession.selfStatusLabel; color: Theme.textMuted; font.pixelSize: 10 }
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
