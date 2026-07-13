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
	signal participantMenuRequested(string sessionId, var actions, var anchorPoint,
		string entryKind, string scopeToken)
	signal profileMenuRequested(var anchorPoint)

	function requestScopeMenu(scopeToken, kind, payload, anchorPoint) {
		scopeMenuRequested(scopeToken, kind, (payload.source && payload.source.actions) || [], anchorPoint)
	}

	function requestParticipantMenu(sessionId, payload, anchorPoint) {
		const source = payload.source || ({})
		participantMenuRequested(sessionId, payload.actions || source.actions || [], anchorPoint,
			String(payload.entryKind || source.entryKind || "user").toLowerCase(),
			String(payload.scopeToken || source.scopeToken || ""))
	}

	function toneColor(tone, fallback) {
		switch (String(tone || "").toLowerCase()) {
		case "danger": return Theme.danger
		case "warning": return Theme.warning
		case "success":
		case "speaking": return Theme.success
		case "accent":
		case "whisper": return Theme.accent
		case "favorite": return Theme.warning
		default: return fallback
		}
	}

	function safeAvatarSource(value) {
		const source = String(value || "").trim()
		return source.substring(0, 22) === "image://mumble/avatar/"
			|| source.substring(0, 11) === "data:image/" ? source : ""
	}

	function statusGlyph(kind) {
		switch (String(kind || "").toLowerCase()) {
		case "serverdeafened":
		case "selfdeafened": return "D"
		case "servermuted":
		case "selfmuted": return "M"
		case "localmuted": return "L"
		case "suppressed": return "S"
		case "listener": return "↘"
		case "recording": return "●"
		case "priority": return "★"
		case "friend": return "♥"
		case "authenticated": return "✓"
		case "whispering": return "W"
		case "shouting": return "!"
		case "mutedtalking": return "M"
		default: return "•"
		}
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
		if (token.substring(0, 8) === "channel:") {
			const legacyChannelId = normalizedProtocolId(token.substring(8), true)
			return legacyChannelId.length > 0 ? "channel:" + legacyChannelId : ""
		}
		const parts = token.split(":")
		if (parts.length !== 2 || !/^-?[0-9]+$/.test(parts[0]))
			return ""
		const scopeValue = Number(parts[0])
		const channelId = normalizedProtocolId(parts[1], true)
		if (!Number.isFinite(scopeValue) || Math.floor(scopeValue) !== scopeValue || channelId.length === 0)
			return ""
		return String(scopeValue) + ":" + channelId
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
			if (sourceScope.length === 0 || sourceScope.split(":").pop() === "0" || sourceScope === target)
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
			if (participants.count > 0) {
				participants.currentIndex = Math.max(0, participants.currentIndex)
				participants.positionViewAtIndex(participants.currentIndex, ListView.Contain)
				Qt.callLater(function() {
					if (participants.currentItem)
						participants.currentItem.forceActiveFocus()
					else
						profileMenuButton.forceActiveFocus()
				})
			} else {
				Qt.callLater(function() { profileMenuButton.forceActiveFocus() })
			}
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
	// The rail is an accessibility container, not an actionable tab stop. Focus
	// always enters through a room, participant, or footer action.
	activeFocusOnTab: false
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
        Label {
			Layout.leftMargin: 18
			Layout.rightMargin: 18
			Layout.topMargin: Theme.space3
			Layout.bottomMargin: Theme.space1
			textFormat: Text.PlainText
			text: qsTr("NAVIGATION")
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			font.bold: true
		}
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
			section.property: "kind"
			section.criteria: ViewSection.FullString
			section.delegate: Item {
				required property string section
				objectName: "navigationSection_" + section
				width: ListView.view ? ListView.view.width : 0
				height: 30
				Accessible.role: Accessible.Heading
				Accessible.name: sectionLabel.text

				Label {
					id: sectionLabel
					anchors.left: parent.left
					anchors.leftMargin: 8
					anchors.right: parent.right
					anchors.rightMargin: 8
					anchors.bottom: parent.bottom
					anchors.bottomMargin: Theme.space1
					textFormat: Text.PlainText
					text: parent.section === "voice" ? qsTr("VOICE ROOMS")
						: parent.section === "direct" ? qsTr("DIRECT MESSAGES")
						: qsTr("TEXT & ACTIVITY")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					font.bold: true
					elide: Text.ElideRight
				}
			}
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
				readonly property bool joined: !!payload.joined || payload.status === "joined"
				readonly property bool canJoin: kind === "voice" && !joined
					&& (payload.canJoin === undefined || !!payload.canJoin)
				readonly property var screenShare: payload.screenShare || ({})
				readonly property bool screenShareVisible: !!screenShare.visible
				readonly property var roomActions: payload.actions
					|| (payload.source && payload.source.actions) || []
				readonly property var roomBadges: payload.badges || []
				readonly property bool hasRoomActions: roomActions.some(function(action) {
					return action && String(action.kind || "action") !== "separator"
				})
				objectName: "navigationRoom_" + stableId
                width: rooms.width - 20
				height: subtitle.length > 0 ? 56 : 46
                radius: 8
				color: roomDropArea.containsDrag ? Theme.selected
					: selected ? Theme.selected
					: joined ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
					: roomMouse.containsMouse ? Theme.panel : "transparent"
				border.width: activeFocus ? Theme.focusRingWidth
					: roomDropArea.containsDrag ? 2 : selected || joined ? 1 : 0
				border.color: activeFocus ? Theme.focus
					: roomDropArea.containsDrag || selected ? Theme.accent
					: joined ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.48) : "transparent"
				opacity: roomMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: !accessibilityPooled
				Accessible.ignored: accessibilityPooled
                Accessible.role: Accessible.ListItem
                Accessible.name: title
				Accessible.description: [subtitle, joined ? qsTr("Joined") : "",
					screenShareVisible ? String(screenShare.statusLabel || screenShare.badgeLabel || "") : ""]
					.filter(function(value) { return value.length > 0 }).join(". ")
                Accessible.selected: selected
				Accessible.onPressAction: activateSelection()
				RowLayout {
					id: roomContent
					z: 3
					anchors.fill: parent
					anchors.leftMargin: 8 + Math.min(depth, 5) * 11
					anchors.rightMargin: 6
					spacing: 7
					Rectangle {
						Layout.preferredWidth: 24
						Layout.preferredHeight: 24
						radius: 6
						color: selected || joined ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
							: Qt.rgba(Theme.textMuted.r, Theme.textMuted.g, Theme.textMuted.b, 0.09)
						ModernIcon {
							anchors.centerIn: parent
							name: kind === "voice" ? "voice-room"
								: kind === "direct" ? "direct"
								: title === qsTr("Activity") ? "activity" : "text-room"
							color: selected || joined ? Theme.accent : Theme.textMuted
							size: 14
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 1
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: title
							color: selected ? Theme.textStrong : Theme.textMain
							font.bold: selected || joined
							font.pixelSize: Theme.fontLabel
							elide: Text.ElideRight
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							visible: subtitle.length > 0
							text: subtitle
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}
					RowLayout {
						id: roomMeta
						Layout.alignment: Qt.AlignVCenter
						spacing: 4
						Label {
							objectName: "navigationRoomBadge_" + stableId
							visible: roomBadges.length > 0
							textFormat: Text.PlainText
							text: roomBadges.length > 0 ? String(roomBadges[0]) : ""
							color: Theme.textMuted
							font.pixelSize: 8
							font.bold: true
						}
						Rectangle {
							objectName: "navigationRoomUnread_" + stableId
							visible: unreadCount > 0
							Layout.preferredWidth: Math.max(18, unreadLabel.implicitWidth + 8)
							Layout.preferredHeight: 18
							radius: 9
							color: Theme.accent
							Label {
								id: unreadLabel
								anchors.centerIn: parent
								textFormat: Text.PlainText
								text: unreadCount > 99 ? "99+" : unreadCount
								color: Theme.strip
								font.pixelSize: 8
								font.bold: true
							}
						}
						Rectangle {
							objectName: "navigationRoomShareBadge_" + stableId
							visible: screenShareVisible && String(screenShare.badgeLabel || "").length > 0
							Layout.preferredWidth: shareBadgeLabel.implicitWidth + 10
							Layout.preferredHeight: 18
							radius: 9
							color: Qt.rgba(navigationRail.toneColor(screenShare.badgeTone, Theme.accent).r,
								navigationRail.toneColor(screenShare.badgeTone, Theme.accent).g,
								navigationRail.toneColor(screenShare.badgeTone, Theme.accent).b, 0.16)
							Label {
								id: shareBadgeLabel
								anchors.centerIn: parent
								textFormat: Text.PlainText
								text: String(screenShare.badgeLabel || "")
								color: navigationRail.toneColor(screenShare.badgeTone, Theme.accent)
								font.pixelSize: 8
								font.bold: true
							}
						}
						Rectangle {
							objectName: "navigationRoomJoined_" + stableId
							visible: joined
							Layout.preferredWidth: 18
							Layout.preferredHeight: 18
							radius: 9
							color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
							ModernIcon { anchors.centerIn: parent; name: "check"; color: Theme.accent; size: 11 }
							Accessible.role: Accessible.StaticText
							Accessible.name: qsTr("Joined")
						}
						Button {
							id: joinButton
							objectName: "navigationRoomJoin_" + stableId
							visible: kind === "voice" && !joined
							enabled: canJoin
							Layout.preferredWidth: 40
							Layout.preferredHeight: 24
							padding: 0
							z: 4
							contentItem: Label {
								textFormat: Text.PlainText
								text: qsTr("Join")
								color: joinButton.enabled ? Theme.accent : Theme.textMuted
								font.pixelSize: 9
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
							}
							background: Rectangle {
								radius: 7
								color: joinButton.hovered
									? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
									: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
								border.width: 1
								border.color: joinButton.enabled ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.36)
									: Theme.divider
							}
							onClicked: roomDelegate.joinVoiceRoom()
						}
						Button {
							id: shareButton
							objectName: "navigationRoomShare_" + stableId
							visible: kind === "voice" && joined && screenShareVisible
								&& String(screenShare.primaryActionId || "").length > 0
							enabled: screenShare.primaryEnabled !== false
							Layout.preferredWidth: 44
							Layout.preferredHeight: 24
							padding: 0
							z: 4
							contentItem: Label {
								textFormat: Text.PlainText
								text: String(screenShare.mode || "") === "idle" ? qsTr("Share") : qsTr("Live")
								color: shareButton.enabled
									? navigationRail.toneColor(screenShare.primaryTone, Theme.accent) : Theme.textMuted
								font.pixelSize: 9
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
							}
							background: Rectangle {
								radius: 7
								color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
									shareButton.hovered ? 0.18 : 0.10)
								border.width: 1
								border.color: Theme.divider
							}
							ToolTip.visible: hovered && String(screenShare.primaryHint || screenShare.primaryLabel || "").length > 0
							ToolTip.text: String(screenShare.primaryHint || screenShare.primaryLabel || qsTr("Screen share"))
							onClicked: uiCommands.invokeScopeAction(scopeToken, String(screenShare.primaryActionId || ""))
						}
						ModernIconButton {
							id: roomActionsButton
							objectName: "navigationRoomActions_" + stableId
							visible: hasRoomActions || kind === "voice" || kind === "text"
							Layout.preferredWidth: 24
							Layout.preferredHeight: 24
							iconName: "more"
							dense: true
							z: 4
							Accessible.name: qsTr("Room actions for %1").arg(title)
							ToolTip.visible: hovered
							ToolTip.text: qsTr("Room actions")
							onClicked: navigationRail.requestScopeMenu(scopeToken, kind, payload,
								roomActionsButton.mapToItem(null, 0, height))
						}
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
			Layout.preferredHeight: Math.min(count * 58, Math.max(174, navigationRail.height * 0.34))
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
					if (isListener || participantSession.length === 0)
						return
					uiCommands.selectParticipant(participantSession)
					if (navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
				function openConversation() {
					if (isListener || participantSession.length === 0)
						return
					uiCommands.openDirectMessage(participantSession)
					if (navigationRail.commitOnSelection)
						navigationRail.selectionCommitted()
				}
				function joinParticipantRoom() {
					if (!isListener && canJoin && participantSession.length > 0)
						uiCommands.invokeParticipantAction(participantSession, "join")
				}
				function accessibleDetails() {
					const details = []
					if (subtitle.length > 0)
						details.push(subtitle)
					for (let index = 0; index < statuses.length; ++index) {
						const label = String((statuses[index] && statuses[index].label) || "")
						if (label.length > 0 && details.indexOf(label) < 0)
							details.push(label)
					}
					for (let index = 0; index < badges.length; ++index) {
						const label = String(badges[index] || "")
						if (label.length > 0 && details.indexOf(label) < 0)
							details.push(label)
					}
					if (localVolumeVisible)
						details.push(qsTr("Local volume %1").arg(localVolumeLabel))
					return details.join(". ")
				}
                required property string stableId
                required property string title
                required property string subtitle
                required property string status
                required property var payload
				readonly property var sourceState: payload.source || ({})
				readonly property string participantSession: String(payload.participantSession
					|| sourceState.session || (/^[0-9]+$/.test(stableId) ? stableId : ""))
				readonly property string entryKind: String(payload.entryKind || sourceState.entryKind || "user").toLowerCase()
				readonly property bool isListener: entryKind === "listener" || !!payload.listener
				readonly property bool isSelf: !!payload.isSelf || !!sourceState.isSelf
				readonly property bool talking: !!payload.talking || !!sourceState.talking || status !== "passive"
				readonly property string talkTone: String(payload.talkTone || sourceState.talkTone || "")
				readonly property var statuses: payload.statuses || sourceState.statuses || []
				readonly property var visibleStatuses: statuses.slice(0, 4)
				readonly property var badges: payload.badges || sourceState.badges || []
				readonly property var localVolume: payload.localVolume || sourceState.localVolume || ({})
				readonly property bool localVolumeVisible: localVolume.visible !== false
					&& Number.isFinite(Number(localVolume.db)) && Math.round(Number(localVolume.db)) !== 0
				readonly property string localVolumeLabel: String(localVolume.compactLabel
					|| ((Number(localVolume.db) > 0 ? "+" : "") + Math.round(Number(localVolume.db)))) + " dB"
				readonly property bool canJoin: !isListener && !!(payload.canJoin || sourceState.canJoin)
				readonly property bool canMessage: !isListener && !!(payload.canMessage || sourceState.canMessage)
				readonly property var participantActions: payload.actions || sourceState.actions || []
				readonly property bool hasParticipantActions: participantActions.some(function(action) {
					return action && String(action.kind || "action") !== "separator"
				})
				readonly property string avatarSource: navigationRail.safeAvatarSource(
					payload.avatarUrl || sourceState.avatarUrl || "")
				readonly property bool selectedParticipant: selectionState.selectedUserSession !== undefined
					&& String(selectionState.selectedUserSession) === participantSession
				objectName: "navigationParticipant_" + stableId
                width: participants.width - 20
				height: 56
                radius: 8
				color: selectedParticipant ? Theme.selected
					   : participantMouse.containsMouse ? Theme.panel : "transparent"
				border.width: activeFocus ? Theme.focusRingWidth : selectedParticipant ? 1 : 0
				border.color: activeFocus ? Theme.focus
					: selectedParticipant ? Theme.accent : "transparent"
				opacity: participantMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: !accessibilityPooled
				Accessible.ignored: accessibilityPooled
                Accessible.role: Accessible.ListItem
                Accessible.name: title
				Accessible.description: accessibleDetails()
				Accessible.selected: selectedParticipant
				Accessible.onPressAction: activateSelection()
				RowLayout {
					z: 3
					anchors.fill: parent
					anchors.leftMargin: 4
					anchors.rightMargin: 4
					spacing: 7
					Rectangle {
						id: participantAvatar
						objectName: "navigationParticipantAvatar_" + stableId
						Layout.preferredWidth: 32
						Layout.preferredHeight: 32
						radius: 16
						clip: true
						color: isListener
							? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : Theme.strip
						border.width: talking || isListener ? 2 : isSelf ? 1 : 0
						border.color: isListener ? Theme.accent
							: talking ? navigationRail.toneColor(talkTone, Theme.success) : Theme.divider
						Image {
							id: participantAvatarImage
							anchors.fill: parent
							source: avatarSource
							asynchronous: true
							cache: false
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
						}
						Label {
							anchors.centerIn: parent
							visible: participantAvatarImage.status !== Image.Ready
							textFormat: Text.PlainText
							text: title.length > 0 ? title.slice(0, 1).toUpperCase() : "?"
							color: isListener ? Theme.accent : Theme.textStrong
							font.pixelSize: 11
							font.bold: true
						}
						Rectangle {
							objectName: "navigationParticipantTalk_" + stableId
							anchors.right: parent.right
							anchors.bottom: parent.bottom
							width: 9
							height: 9
							radius: 5
							border.width: 2
							border.color: participantDelegate.color
							color: isListener ? Theme.accent
								: talking ? navigationRail.toneColor(talkTone, Theme.success) : Theme.textMuted
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 1
						RowLayout {
							Layout.fillWidth: true
							spacing: 5
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: title
								color: talking ? Theme.textStrong : isListener ? Theme.accent : Theme.textMain
								font.pixelSize: Theme.fontLabel
								font.bold: talking || isSelf
								font.italic: isListener
								elide: Text.ElideRight
							}
							Rectangle {
								objectName: "navigationParticipantVolume_" + stableId
								visible: localVolumeVisible
								Layout.preferredWidth: volumeLabel.implicitWidth + 8
								Layout.preferredHeight: 16
								radius: 8
								color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
								border.width: 1
								border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
								Label {
									id: volumeLabel
									anchors.centerIn: parent
									textFormat: Text.PlainText
									text: localVolumeLabel
									color: Theme.accent
									font.pixelSize: 8
									font.bold: true
								}
								ToolTip.visible: volumeHover.containsMouse
								ToolTip.text: qsTr("Local volume %1").arg(String(localVolume.label || localVolumeLabel))
								MouseArea { id: volumeHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
							}
						}
						Label {
							objectName: "navigationParticipantDetails_" + stableId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: [subtitle, badges.slice(0, 2).join(" · ")]
								.filter(function(value) { return value.length > 0 }).join(" · ")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							visible: text.length > 0
						}
					}
					RowLayout {
						spacing: 3
						Repeater {
							model: visibleStatuses
							delegate: Rectangle {
								required property var modelData
								readonly property color statusColor: navigationRail.toneColor(modelData.tone, Theme.textMuted)
								objectName: "navigationParticipantStatus_" + stableId + "_" + String(modelData.kind || "status")
								Layout.preferredWidth: 18
								Layout.preferredHeight: 18
								radius: 9
								color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.14)
								border.width: 1
								border.color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.28)
								Label {
									anchors.centerIn: parent
									textFormat: Text.PlainText
									text: navigationRail.statusGlyph(modelData.kind)
									color: parent.statusColor
									font.pixelSize: 8
									font.bold: true
								}
								ToolTip.visible: statusHover.containsMouse
								ToolTip.text: String(modelData.label || modelData.kind || "")
								MouseArea { id: statusHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
							}
						}
						Label {
							visible: statuses.length > visibleStatuses.length
							textFormat: Text.PlainText
							text: "+" + (statuses.length - visibleStatuses.length)
							color: Theme.textMuted
							font.pixelSize: 8
						}
						Button {
							id: participantJoinButton
							objectName: "navigationParticipantJoin_" + stableId
							visible: canJoin
							Layout.preferredWidth: 36
							Layout.preferredHeight: 22
							padding: 0
							z: 4
							contentItem: Label {
								textFormat: Text.PlainText
								text: qsTr("Join")
								color: Theme.accent
								font.pixelSize: 8
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
							}
							background: Rectangle {
								radius: 7
								color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
									participantJoinButton.hovered ? 0.18 : 0.10)
								border.width: 1
								border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
							}
							onClicked: participantDelegate.joinParticipantRoom()
						}
						ModernIconButton {
							id: participantActionsButton
							objectName: "navigationParticipantActions_" + stableId
							visible: hasParticipantActions
							Layout.preferredWidth: 24
							Layout.preferredHeight: 24
							iconName: "more"
							dense: true
							z: 4
							Accessible.name: qsTr("Participant actions for %1").arg(title)
							onClicked: navigationRail.requestParticipantMenu(participantSession, payload,
								participantActionsButton.mapToItem(null, 0, height))
						}
					}
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
					dragSourceStableId = mouse.button === Qt.LeftButton && !isListener
						? participantSession : ""
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
							navigationRail.requestParticipantMenu(participantSession, payload,
								participantDelegate.mapToItem(null, mouse.x, mouse.y))
						else {
							participantDelegate.activateSelection()
						}
					}
					onPressAndHold: mouse => navigationRail.requestParticipantMenu(participantSession, payload,
						participantDelegate.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: participantDelegate.openConversation()
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Menu
						|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
						navigationRail.requestParticipantMenu(participantSession, payload,
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
            Layout.preferredHeight: 72
            color: Theme.strip
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
				anchors.leftMargin: 12
				anchors.rightMargin: 10
				anchors.topMargin: 10
				anchors.bottomMargin: 10
				spacing: 8
				Rectangle {
					Layout.preferredWidth: 34
					Layout.preferredHeight: 34
					radius: 17
					color: Theme.surfaceRaised
					border.width: 1
					border.color: clientSession.connected ? Theme.accent : Theme.divider
					Label {
						anchors.centerIn: parent
						textFormat: Text.PlainText
						text: clientSession.selfName.length > 0
							? clientSession.selfName.slice(0, 1).toUpperCase() : "?"
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.bold: true
					}
				}
                ColumnLayout {
                    Layout.fillWidth: true
					spacing: 1
                    Label { textFormat: Text.PlainText; text: clientSession.selfName; color: Theme.textStrong; font.bold: true }
                    Label { textFormat: Text.PlainText; text: clientSession.selfStatusLabel; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                }
				ModernIconButton {
					id: selfMuteButton
					objectName: "selfMuteButton"
					iconName: clientSession.selfMuted ? "mute" : "microphone"
					selected: !!clientSession.selfMuted
					tone: clientSession.selfMuted ? "danger" : "neutral"
					Accessible.name: clientSession.selfMuted ? qsTr("Unmute microphone") : qsTr("Mute microphone")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: uiCommands.toggleSelfMute()
				}
				ModernIconButton {
					objectName: "selfDeafenButton"
					iconName: "deafen"
					selected: !!clientSession.selfDeafened
					tone: clientSession.selfDeafened ? "danger" : "neutral"
					Accessible.name: clientSession.selfDeafened ? qsTr("Undeafen") : qsTr("Deafen")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: uiCommands.toggleSelfDeaf()
				}
				ModernIconButton {
					id: profileMenuButton
					objectName: "profileMenuButton"
					iconName: "more"
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
