import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: navigationRail
	objectName: "navigationRail"
	// Flat, ordered navigation rows. Voice-room rows are followed immediately by
	// their participant rows. This keeps one scroll position and one virtualized
	// delegate pool for the complete navigator.
	required property var navigationModel
	required property var selectionState
	required property var uiCommands
	required property var clientSession
	property bool commitOnSelection: false
	property string activeScopeMenuToken: ""
	// Stable row key, rather than session alone: the same user may have a normal
	// row and one or more listener rows in different scopes.
	property string activeParticipantMenuKey: ""
    signal selectionCommitted()
	signal scopeMenuRequested(string scopeToken, string kind, var actions, var anchorPoint)
	signal participantMenuRequested(string sessionId, var actions, var anchorPoint,
		string entryKind, string scopeToken, string rowKey)
	signal profileMenuRequested(var anchorPoint)

	function requestScopeMenu(scopeToken, kind, payload, anchorPoint) {
		scopeMenuRequested(scopeToken, kind, (payload.source && payload.source.actions) || [], anchorPoint)
	}

	function requestParticipantMenu(sessionId, rowKey, payload, anchorPoint) {
		const source = payload.source || ({})
		const entryKind = String(payload.entryKind || source.entryKind || "user").toLowerCase()
		const scopeToken = String(payload.scopeToken || payload.parentScopeToken || source.scopeToken || "")
		let actions = payload.actions || source.actions || []
		if (sessionId.length > 0 && uiCommands.requestParticipantActions) {
			const requested = uiCommands.requestParticipantActions(sessionId, entryKind, scopeToken)
			if (requested && requested.length > 0)
				actions = requested
		}
		participantMenuRequested(sessionId, actions, anchorPoint,
			entryKind, scopeToken,
			String(rowKey || ""))
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

	function roomCountForKind(kind) {
		const count = Number(navigationModel.count) || 0
		let matches = 0
		for (let index = 0; index < count; ++index) {
			const room = navigationModel.get(index)
			if (room && String(room.kind) === kind)
				++matches
		}
		return matches
	}

	function safeAvatarSource(value) {
		const source = String(value || "").trim()
		return /^(image:\/\/mumble\/|data:image\/)/i.test(source) ? source : ""
	}

	function serverMonogram(value) {
		const label = String(value || "").trim()
		const parts = label.split(/[\s._-]+/).filter(function(part) { return part.length > 0 })
		if (parts.length > 1)
			return (parts[0].slice(0, 1) + parts[parts.length - 1].slice(0, 1)).toUpperCase()
		const compact = label.replace(/[^\p{L}\p{N}]/gu, "")
		if (compact.length === 0)
			return "M"
		return (compact.slice(0, 1) + compact.slice(-1)).toUpperCase()
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
			if (!candidate || !candidate.visible || candidate.kind === "participant"
				|| candidate.scopeToken === undefined)
				continue
			const targetHeight = candidate.roomRowHeight === undefined
				? candidate.height : candidate.roomRowHeight
			const targetTop = candidate.sectionHeaderHeight === undefined
				? 0 : candidate.sectionHeaderHeight
			const local = candidate.mapFromItem(null, scenePoint.x, scenePoint.y)
			if (local.x < 0 || local.y < targetTop || local.x > candidate.width
				|| local.y > targetTop + targetHeight)
				continue
			return dispatchStableDrop(kind, stableId, candidate.scopeToken,
				local.y - targetTop, targetHeight)
		}
		return false
	}

	function navigationRowIsSelected(row) {
		if (!row)
			return false
		if (!!row.selected)
			return true
		if (String(row.kind || "") !== "participant"
				|| selectionState.selectedUserSession === undefined
				|| selectionState.selectedUserSession === null)
			return false
		const payload = row.payload || ({})
		const source = payload.source || ({})
		const entryKind = String(payload.entryKind || source.entryKind || "user").toLowerCase()
		if (entryKind !== "user")
			return false
		const stableId = String(row.stableId || "")
		const session = String(payload.participantSession || source.session
			|| (stableId.substring(0, 5) === "user:" ? stableId.substring(5) : stableId))
		return session.length > 0 && session === String(selectionState.selectedUserSession)
	}

	function setCurrentNavigationIndex(index) {
		if (rooms.count <= 0)
			return false
		const targetIndex = Math.max(0, Math.min(rooms.count - 1, Number(index)))
		if (!Number.isFinite(targetIndex))
			return false
		if (rooms.currentIndex !== targetIndex)
			rooms.currentIndex = targetIndex
		rooms.positionViewAtIndex(targetIndex, ListView.Contain)
		return true
	}

	function focusNavigationIndex(index, focusReason) {
		if (!setCurrentNavigationIndex(index))
			return false
		const targetIndex = rooms.currentIndex
		const reason = focusReason === undefined ? Qt.OtherFocusReason : focusReason
		Qt.callLater(function() {
			if (rooms.currentIndex !== targetIndex)
				return
			if (rooms.currentItem)
				rooms.currentItem.forceActiveFocus(reason)
			else
				rooms.forceActiveFocus(reason)
		})
		return true
	}

	function focusInitialItem() {
		if (rooms.count <= 0) {
			Qt.callLater(function() { profileMenuButton.forceActiveFocus() })
			return
		}
		let selectedIndex = -1
		let firstActionableIndex = -1
		for (let index = 0; index < rooms.count; ++index) {
			const row = navigationModel.get(index)
			if (row && firstActionableIndex < 0)
				firstActionableIndex = index
			if (navigationRowIsSelected(row)) {
				selectedIndex = index
				break
			}
		}
		focusNavigationIndex(selectedIndex >= 0 ? selectedIndex : Math.max(0, firstActionableIndex),
			Qt.TabFocusReason)
	}

	implicitWidth: 310
	implicitHeight: 600
	// The rail is an accessibility container, not an actionable tab stop. Its
	// virtualized ListView contributes one navigation stop, followed by the fixed
	// footer actions.
	activeFocusOnTab: false
    color: Theme.rail
	border.width: 0
	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 1
		visible: Theme.railSide === "right"
		color: Theme.divider
		z: 20
	}
	Rectangle {
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 1
		visible: Theme.railSide === "left"
		color: Theme.divider
		z: 20
	}
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
			id: serverHeader
			objectName: "navigationServerHeader"
            Layout.fillWidth: true
			Layout.preferredHeight: Theme.compact ? 98 : 112
            color: Theme.panel
			border.width: 0
			Accessible.role: Accessible.Grouping
			Accessible.name: clientSession.serverName
			Accessible.description: [clientSession.connectionLabel, clientSession.connectionDetail]
				.filter(function(value) { return String(value || "").trim().length > 0 }).join(". ")
			DragHandler {
				enabled: !navigationRail.commitOnSelection
				target: null
				onActiveChanged: if (active && navigationRail.Window.window)
					navigationRail.Window.window.startSystemMove()
			}
			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.bottom: parent.bottom
				height: 1
				color: Theme.divider
			}
			ColumnLayout {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.leftMargin: 18
				anchors.rightMargin: 18
				anchors.topMargin: Theme.compact ? 8 : 10
				spacing: Theme.compact ? 3 : 4
				Rectangle {
					id: serverBadge
					objectName: "navigationServerBadge"
					Layout.alignment: Qt.AlignHCenter
					Layout.preferredWidth: Theme.compact ? 34 : 40
					Layout.preferredHeight: Layout.preferredWidth
					radius: Theme.compact ? 10 : 12
					color: Theme.accent
					border.width: 1
					border.color: Theme.elevationHighlight
					Label {
						objectName: "navigationServerMonogram"
						anchors.centerIn: parent
						textFormat: Text.PlainText
						text: navigationRail.serverMonogram(clientSession.serverName)
						color: Theme.onAccent
						font.bold: true
						font.pixelSize: Theme.fontBody
						Accessible.ignored: true
					}
				}
				Label {
					objectName: "navigationServerName"
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: clientSession.serverName
					color: Theme.textStrong
					font.bold: true
					font.pixelSize: 14
					elide: Text.ElideRight
					horizontalAlignment: Text.AlignHCenter
					Accessible.ignored: true
				}
				Rectangle {
					id: connectionPill
					objectName: "navigationConnectionPill"
					readonly property color statusColor: navigationRail.toneColor(
						clientSession.connectionTone,
						clientSession.connected ? Theme.success : Theme.textFaint)
					Layout.alignment: Qt.AlignHCenter
					Layout.preferredWidth: Math.min(parent.width,
						connectionPillContent.implicitWidth + 16)
					Layout.preferredHeight: 22
					radius: 11
					color: Theme.withAlpha(statusColor, 0.08)
					border.width: 1
					border.color: Theme.withAlpha(statusColor, 0.24)
					Accessible.role: Accessible.StaticText
					Accessible.name: clientSession.connectionLabel
					Accessible.description: clientSession.connectionDetail
					RowLayout {
						id: connectionPillContent
						anchors.centerIn: parent
						spacing: 6
						Rectangle {
							objectName: "navigationConnectionDot"
							Layout.preferredWidth: 7
							Layout.preferredHeight: 7
							radius: 4
							color: connectionPill.statusColor
							Accessible.ignored: true
						}
						Label {
							objectName: "navigationConnectionLabel"
							textFormat: Text.PlainText
							text: clientSession.connectionLabel
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					HoverHandler { id: connectionPillHover }
					ToolTip.visible: connectionPillHover.hovered
						&& String(clientSession.connectionDetail || "").length > 0
					ToolTip.text: clientSession.connectionDetail
				}
            }
        }
		ListView {
			id: rooms
			objectName: "navigationRooms"
            Layout.fillWidth: true
            Layout.fillHeight: true
			model: navigationModel
			clip: true
			// The virtualized list is the navigator's single tab stop. Rows remain
			// directly focusable by pointer, automation and focusInitialItem(), while
			// keyboard users move the current row with arrows without tabbing through
			// every delegate on a large server.
			activeFocusOnTab: count > 0
			KeyNavigation.tab: selfMuteButton
			Accessible.role: Accessible.List
			Accessible.name: qsTr("Rooms and participants")
			reuseItems: true
			// Incubate a bounded number of nearby delegates so keyboard and
			// accessibility traversal remain deterministic without realizing the
			// complete room tree.
			cacheBuffer: 400
            spacing: 2
            leftMargin: 10
            rightMargin: 10
			boundsBehavior: Flickable.StopAtBounds
			ScrollBar.vertical: ModernScrollBar {
				objectName: "navigationScrollBar"
			}
			section.property: "sectionKind"
			section.criteria: ViewSection.FullString
			Keys.onPressed: event => {
				if (event.key === Qt.Key_Up) {
					event.accepted = navigationRail.setCurrentNavigationIndex(currentIndex - 1)
				} else if (event.key === Qt.Key_Down) {
					event.accepted = navigationRail.setCurrentNavigationIndex(currentIndex + 1)
				} else if (event.key === Qt.Key_Home) {
					event.accepted = navigationRail.setCurrentNavigationIndex(0)
				} else if (event.key === Qt.Key_End) {
					event.accepted = navigationRail.setCurrentNavigationIndex(count - 1)
				} else if ((event.key === Qt.Key_Menu
						|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))
						&& currentItem) {
					currentItem.requestContextMenuAtCenter()
					event.accepted = true
				}
			}
			Keys.onReturnPressed: event => {
				if (currentItem) {
					currentItem.activateSelection()
					event.accepted = true
				}
			}
			Keys.onEnterPressed: event => {
				if (currentItem) {
					currentItem.activateSelection()
					event.accepted = true
				}
			}
			Keys.onSpacePressed: event => {
				if (currentItem) {
					currentItem.activateSpaceAction()
					event.accepted = true
				}
			}
			section.delegate: Item {
				required property string section
				readonly property string visualLabel: section === "voice" ? qsTr("VOICE ROOMS")
					: section === "direct" ? qsTr("DIRECT MESSAGES") : qsTr("TEXT ROOMS")
				objectName: "navigationSection_" + section
				width: ListView.view ? ListView.view.width : 0
				height: 34
				Accessible.role: Accessible.Heading
				Accessible.name: visualLabel + " · " + navigationRail.roomCountForKind(section)

				Label {
					id: sectionLabel
					objectName: "navigationSectionLabel_" + parent.section
					anchors.left: parent.left
					anchors.leftMargin: 8
					anchors.right: parent.right
					anchors.rightMargin: 8
					anchors.bottom: parent.bottom
					anchors.bottomMargin: Theme.space1
					textFormat: Text.PlainText
					text: parent.visualLabel
					color: Theme.withAlpha(Theme.accent, 0.76)
					font.pixelSize: Theme.fontCaption
					font.bold: true
					elide: Text.ElideRight
					Accessible.ignored: true
				}
			}
			delegate: Rectangle {
				id: roomDelegate
				property bool accessibilityPooled: false
				ListView.onPooled: {
					roomMouse.clearDragSnapshot()
					participantMouse.clearDragSnapshot()
					accessibilityPooled = true
				}
				ListView.onReused: {
					roomMouse.clearDragSnapshot()
					participantMouse.clearDragSnapshot()
					accessibilityPooled = false
				}
				function makeCurrent() {
					if (!accessibilityPooled)
						navigationRail.setCurrentNavigationIndex(index)
				}
				function focusRow(focusReason) {
					makeCurrent()
					forceActiveFocus(focusReason === undefined ? Qt.OtherFocusReason : focusReason)
				}
				function activateSelection() {
					if (isParticipant) {
						participantDelegate.activateSelection()
						return
					}
					uiCommands.selectScopeFromRail(scopeToken, kind)
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
				function activateSpaceAction() {
					if (isParticipant)
						participantDelegate.openConversation()
					else
						joinVoiceRoom()
				}
				function requestContextMenuAtCenter() {
					if (isParticipant)
						navigationRail.requestParticipantMenu(participantDelegate.participantSession,
							participantDelegate.stableId, payload,
							mapToItem(null, width / 2, height / 2))
					else
						navigationRail.requestScopeMenu(scopeToken, kind, payload,
							mapToItem(null, width / 2, height / 2))
				}
				function accessibleRoomDetails() {
					const details = []
					function appendUnique(value) {
						const label = String(value || "").trim()
						if (label.length > 0 && details.indexOf(label) < 0)
							details.push(label)
					}
					appendUnique(subtitle)
					for (let index = 0; index < roomBadges.length; ++index)
						appendUnique(roomBadges[index])
					if (unreadCount === 1)
						appendUnique(qsTr("1 unread message"))
					else if (unreadCount > 1)
						appendUnique(qsTr("%1 unread messages").arg(unreadCount))
					if (joined)
						appendUnique(qsTr("You are here"))
					if (screenShareVisible)
						appendUnique(String(screenShare.statusLabel || screenShare.badgeLabel || ""))
					return details.join(". ")
				}
				required property int index
				required property string stableId
				required property string scopeToken
				required property string title
				required property string subtitle
				required property string kind
				required property string sectionKind
				required property bool selected
				required property int depth
				required property int unreadCount
				required property string status
				required property var payload
				readonly property bool isParticipant: kind === "participant"
				// Kept as a stable delegate API for pointer/automation helpers. Section
				// headers are owned by the same outer ListView, not by a row.
				readonly property int sectionHeaderHeight: 0
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
				readonly property bool detailsVisible: !isParticipant && (unreadCount > 0
					|| (selected && kind !== "voice" && subtitle.length > 0))
				readonly property int roomRowHeight: detailsVisible ? 48 : 40
				readonly property bool rowFocusVisible: activeFocus
					|| (rooms.activeFocus && ListView.isCurrentItem)
				readonly property bool revealActions: roomMouse.containsMouse || rowFocusVisible
					|| (navigationRail.activeScopeMenuToken.length > 0
						&& navigationRail.activeScopeMenuToken === scopeToken)
					|| joinButton.activeFocus || shareButton.activeFocus || roomActionsButton.activeFocus
				objectName: isParticipant
					? "navigationParticipantSemantic_" + participantDelegate.participantObjectKey
					: "navigationRoom_" + stableId
                width: rooms.width - 20
				height: sectionHeaderHeight + (isParticipant ? (Theme.compact ? 31 : 36) : roomRowHeight)
                radius: 8
				color: isParticipant ? "transparent"
					: roomDropArea.containsDrag ? Theme.selected
					: roomMouse.pressed ? Theme.accentSubtle
					: selected ? Theme.selected
					: joined ? Theme.withAlpha(Theme.success, 0.045)
					: roomMouse.containsMouse ? Theme.surfaceHover : "transparent"
				border.width: isParticipant ? 0 : rowFocusVisible ? Theme.focusRingWidth
					: roomDropArea.containsDrag ? 2 : 0
				border.color: rowFocusVisible ? Theme.focus
					: roomDropArea.containsDrag ? Theme.accent : "transparent"
				opacity: roomMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: false
				onActiveFocusChanged: if (activeFocus) makeCurrent()
				Accessible.ignored: accessibilityPooled
				Accessible.role: Accessible.ListItem
                Accessible.name: title
				Accessible.description: isParticipant ? participantDelegate.accessibleDetails()
					: accessibleRoomDetails()
				Accessible.selected: isParticipant ? participantDelegate.selectedParticipant : selected
				Accessible.onPressAction: activateSelection()
				Rectangle {
					objectName: "navigationRoomSelectionAccent_" + stableId
					anchors.left: parent.left
					anchors.verticalCenter: parent.verticalCenter
					width: 2
					height: Math.max(16, parent.height - 10)
					radius: 1
					color: Theme.accent
					visible: !roomDelegate.isParticipant && roomDelegate.selected
					z: 4
					Accessible.ignored: true
				}
				RowLayout {
					id: roomContent
					z: 3
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.topMargin: roomDelegate.sectionHeaderHeight
					height: roomDelegate.roomRowHeight
					visible: !roomDelegate.isParticipant
					anchors.leftMargin: 8 + Math.min(depth, 5) * 11
					anchors.rightMargin: 6
					spacing: 7
					Rectangle {
						Layout.preferredWidth: 24
						Layout.preferredHeight: 24
						radius: 6
						color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
							: joined ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.14)
							: Qt.rgba(Theme.textMuted.r, Theme.textMuted.g, Theme.textMuted.b, 0.09)
						ModernIcon {
							anchors.centerIn: parent
							name: kind === "voice" ? "voice-room"
								: kind === "direct" ? "direct"
								: title === qsTr("Activity") ? "activity" : "text-room"
							color: selected ? Theme.accent : joined ? Theme.success : Theme.textMuted
							size: 14
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 1
						Label {
							objectName: "navigationRoomTitle_" + stableId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: title
							color: selected ? Theme.textStrong : Theme.textMain
							font.bold: selected || unreadCount > 0
							font.pixelSize: Theme.fontLabel
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "navigationRoomDetails_" + stableId
							Layout.fillWidth: true
							textFormat: Text.PlainText
							visible: detailsVisible
							text: joined ? qsTr("You are here") : subtitle
							color: joined ? Theme.success : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					RowLayout {
						id: roomMeta
						Layout.alignment: Qt.AlignVCenter
						spacing: 4
						Label {
							objectName: "navigationRoomBadge_" + stableId
							// Room badges remain available in the row's accessible description.
							// Keeping them out of the compact action lane gives join/share/menu
							// stable priority at every supported rail width.
							visible: false
							textFormat: Text.PlainText
							text: roomBadges.length > 0 ? String(roomBadges[0]) : ""
							color: Theme.textMuted
							font.pixelSize: 8
							font.bold: true
							Accessible.ignored: true
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
								Accessible.ignored: true
							}
						}
						Rectangle {
							objectName: "navigationRoomShareBadge_" + stableId
							visible: false
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
								Accessible.ignored: true
							}
						}
						Rectangle {
							objectName: "navigationRoomJoined_" + stableId
							visible: false
							Layout.preferredWidth: 18
							Layout.preferredHeight: 18
							radius: 9
							color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16)
							ModernIcon { anchors.centerIn: parent; name: "check"; color: Theme.success; size: 11 }
							Accessible.ignored: true
						}
						Button {
							id: joinButton
							objectName: "navigationRoomJoin_" + stableId
							visible: kind === "voice" && !joined
							enabled: canJoin
							opacity: revealActions ? 1 : selected ? 0.84 : 0.68
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							Layout.preferredWidth: 40
							Layout.preferredHeight: 24
							padding: 0
							z: 4
							text: qsTr("Join")
							Accessible.name: text
							contentItem: Label {
								textFormat: Text.PlainText
								text: joinButton.text
								Accessible.ignored: true
								color: joinButton.enabled ? Theme.accent : Theme.textMuted
								font.pixelSize: 9
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
							}
							background: Rectangle {
								radius: 7
								color: !joinButton.enabled ? Theme.panel
									: joinButton.down ? Theme.accentSubtle
									: joinButton.hovered
									? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
									: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
								border.width: joinButton.activeFocus ? Theme.focusRingWidth : 1
								border.color: joinButton.activeFocus ? Theme.focus
									: joinButton.enabled ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.36)
									: Theme.divider
							}
							onClicked: roomDelegate.joinVoiceRoom()
							onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
						}
						Button {
							id: shareButton
							objectName: "navigationRoomShare_" + stableId
							visible: kind === "voice" && joined && screenShareVisible
								&& String(screenShare.primaryActionId || "").length > 0
							enabled: screenShare.primaryEnabled !== false
							opacity: revealActions ? 1 : 0.82
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							Layout.preferredWidth: 44
							Layout.preferredHeight: 24
							padding: 0
							z: 4
							text: String(screenShare.mode || "") === "idle" ? qsTr("Share") : qsTr("Live")
							Accessible.name: text
							contentItem: Label {
								textFormat: Text.PlainText
								text: shareButton.text
								color: shareButton.enabled
									? navigationRail.toneColor(screenShare.primaryTone, Theme.accent) : Theme.textMuted
								font.pixelSize: 9
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
								Accessible.ignored: true
							}
							background: Rectangle {
								radius: 7
								color: !shareButton.enabled ? Theme.panel
									: shareButton.down ? Theme.accentSubtle
									: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
										shareButton.hovered ? 0.18 : 0.10)
								border.width: shareButton.activeFocus ? Theme.focusRingWidth : 1
								border.color: shareButton.activeFocus ? Theme.focus : Theme.divider
							}
							ToolTip.visible: hovered && String(screenShare.primaryHint || screenShare.primaryLabel || "").length > 0
							ToolTip.text: String(screenShare.primaryHint || screenShare.primaryLabel || qsTr("Screen share"))
							onClicked: uiCommands.invokeScopeAction(scopeToken, String(screenShare.primaryActionId || ""))
							onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
						}
						ModernIconButton {
							id: roomActionsButton
							objectName: "navigationRoomActions_" + stableId
							visible: hasRoomActions || kind === "voice" || kind === "text"
							opacity: revealActions ? 1 : (selected || joined ? 0.62 : 0.48)
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							Layout.preferredWidth: 24
							Layout.preferredHeight: 24
							iconName: "more"
							dense: true
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							z: 4
							Accessible.name: qsTr("Room actions for %1").arg(title)
							ToolTip.visible: hovered
							ToolTip.text: qsTr("Room actions")
							onClicked: navigationRail.requestScopeMenu(scopeToken, kind, payload,
								roomActionsButton.mapToItem(null, 0, height))
							onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
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
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.topMargin: roomDelegate.sectionHeaderHeight
					height: roomDelegate.roomRowHeight
					enabled: !roomDelegate.isParticipant && kind === "voice"
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
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.topMargin: roomDelegate.sectionHeaderHeight
					height: roomDelegate.roomRowHeight
					enabled: !roomDelegate.isParticipant
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
						roomDelegate.focusRow(Qt.MouseFocusReason)
						pressScene = roomMouse.mapToItem(null, mouse.x, mouse.y)
						dragStarted = false
						suppressClick = false
						dragSourceScopeToken = mouse.button === Qt.LeftButton && kind === "voice"
							? scopeToken : ""
						roomDragSource.dropHandled = false
					}
					onPositionChanged: mouse => {
						if (!pressed || dragSourceScopeToken.length === 0)
							return
						const current = roomMouse.mapToItem(null, mouse.x, mouse.y)
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
								roomMouse.mapToItem(null, mouse.x, mouse.y))
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
					if (event.key === Qt.Key_Up) {
						event.accepted = navigationRail.focusNavigationIndex(index - 1,
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_Down) {
						event.accepted = navigationRail.focusNavigationIndex(index + 1,
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_Home) {
						event.accepted = navigationRail.focusNavigationIndex(0,
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_End) {
						event.accepted = navigationRail.focusNavigationIndex(rooms.count - 1,
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_Menu
						|| (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
						requestContextMenuAtCenter()
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
					roomDelegate.activateSpaceAction()
                    event.accepted = true
                }
				Rectangle {
				id: participantDelegate
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.topMargin: roomDelegate.sectionHeaderHeight
				visible: roomDelegate.isParticipant
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
					if (isSelf)
						details.push(qsTr("You"))
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
				readonly property var sourceState: payload.source || ({})
				readonly property string stableId: roomDelegate.stableId
				readonly property string participantSession: String(payload.participantSession
					|| sourceState.session || (stableId.substring(0, 5) === "user:"
						? stableId.substring(5) : (/^[0-9]+$/.test(stableId) ? stableId : "")))
				readonly property string participantObjectKey: entryKind === "user"
					? participantSession : stableId
				readonly property string parentScopeToken: String(payload.parentScopeToken || "")
				readonly property string entryKind: String(payload.entryKind || sourceState.entryKind || "user").toLowerCase()
				readonly property bool isListener: entryKind === "listener" || !!payload.listener
				readonly property bool isSelf: !!payload.isSelf || !!sourceState.isSelf
				readonly property string talkState: String(status || payload.talkState
					|| sourceState.talkState || "").toLowerCase()
				readonly property bool talking: !!payload.talking || !!sourceState.talking
					|| ["talking", "whispering", "shouting", "mutedtalking"].indexOf(talkState) >= 0
				readonly property string talkTone: String(payload.talkTone || sourceState.talkTone || "")
				readonly property var statuses: payload.statuses || sourceState.statuses || []
				// Talk activity already has a high-salience avatar ring. Do not repeat it
				// as a status chip; reserve the compact lane for mute/deafen/listener state.
				readonly property var visibleStatuses: statuses.filter(function(item) {
					return ["talking", "whispering", "shouting", "mutedtalking"]
						.indexOf(String((item && item.kind) || "").toLowerCase()) < 0
				}).slice(0, 2)
				readonly property var badges: payload.badges || sourceState.badges || []
				readonly property var localVolume: payload.localVolume || sourceState.localVolume || ({})
				readonly property bool localVolumeVisible: localVolume.visible !== false
					&& Number.isFinite(Number(localVolume.db)) && Math.round(Number(localVolume.db)) !== 0
				readonly property string localVolumeLabel: String(localVolume.compactLabel
					|| ((Number(localVolume.db) > 0 ? "+" : "") + Math.round(Number(localVolume.db)))) + " dB"
				readonly property bool canJoin: !isListener && !!(payload.canJoin || sourceState.canJoin)
				readonly property bool canMessage: !isListener && !!(payload.canMessage || sourceState.canMessage)
				readonly property var participantActions: payload.actions || sourceState.actions || []
				readonly property bool hasParticipantActions: !!payload.actionsAvailable
					|| !!sourceState.actionsAvailable
					|| participantActions.some(function(action) {
					return action && String(action.kind || "action") !== "separator"
				})
				readonly property bool revealParticipantActions: participantMouse.containsMouse
					|| roomDelegate.rowFocusVisible
					|| (stableId.length > 0
						&& navigationRail.activeParticipantMenuKey === stableId)
					|| participantJoinButton.activeFocus || participantActionsButton.activeFocus
				readonly property string avatarSource: navigationRail.safeAvatarSource(
					payload.avatarUrl || sourceState.avatarUrl || "")
				readonly property bool selectedParticipant: selectionState.selectedUserSession !== undefined
					&& String(selectionState.selectedUserSession) === participantSession
				function focusRow() {
					roomDelegate.focusRow()
				}
				objectName: "navigationParticipant_" + participantObjectKey
				height: Theme.compact ? 31 : 36
                radius: 8
				color: participantMouse.pressed ? Theme.accentSubtle
					   : selectedParticipant ? Theme.selected
					   : participantMouse.containsMouse ? Theme.surfaceHover : "transparent"
				border.width: roomDelegate.rowFocusVisible ? Theme.focusRingWidth : 0
				border.color: roomDelegate.rowFocusVisible ? Theme.focus : "transparent"
				opacity: participantMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: false
				Accessible.ignored: true
				Rectangle {
					objectName: "navigationParticipantSelectionAccent_"
						+ participantDelegate.participantObjectKey
					anchors.left: parent.left
					anchors.verticalCenter: parent.verticalCenter
					width: 2
					height: Math.max(14, parent.height - 8)
					radius: 1
					color: Theme.accent
					visible: participantDelegate.selectedParticipant
					z: 4
					Accessible.ignored: true
				}
				Rectangle {
					objectName: "navigationParticipantGuide_" + participantDelegate.participantObjectKey
					x: 9 + Math.min(Number(payload.parentDepth || 0), 5) * 11
					anchors.top: parent.top
					anchors.bottom: parent.bottom
					width: 1
					color: Theme.selfCardBorder
					Accessible.ignored: true
				}
				RowLayout {
					id: participantContent
					z: 3
					anchors.fill: parent
					anchors.leftMargin: 22 + Math.min(Number(payload.parentDepth || 0), 5) * 11
					anchors.rightMargin: 4
					spacing: 5
					Rectangle {
						id: participantAvatar
						objectName: "navigationParticipantAvatar_" + participantDelegate.participantObjectKey
						Layout.preferredWidth: 26
						Layout.preferredHeight: 26
						radius: 13
						clip: true
						color: participantDelegate.isListener
							? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : Theme.strip
						border.width: participantDelegate.talking || participantDelegate.isListener
							? 2 : participantDelegate.isSelf ? 1 : 0
						border.color: participantDelegate.isListener ? Theme.accent
							: participantDelegate.talking
							? navigationRail.toneColor(participantDelegate.talkTone, Theme.success) : Theme.divider
						Image {
							id: participantAvatarImage
							objectName: "navigationParticipantAvatarImage_" + participantDelegate.participantObjectKey
							anchors.fill: parent
							source: participantDelegate.avatarSource
							sourceSize: Qt.size(Math.max(1, Math.ceil(width * 2)),
								Math.max(1, Math.ceil(height * 2)))
							asynchronous: true
							cache: false
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
						}
						Label {
							objectName: "navigationParticipantAvatarFallback_"
								+ participantDelegate.participantObjectKey
							anchors.centerIn: parent
							visible: participantAvatarImage.status !== Image.Ready
							textFormat: Text.PlainText
							text: roomDelegate.title.length > 0
								? roomDelegate.title.slice(0, 1).toUpperCase() : "?"
							color: participantDelegate.isListener ? Theme.accent : Theme.textStrong
							font.pixelSize: 11
							font.bold: true
							Accessible.ignored: true
						}
						Rectangle {
							objectName: "navigationParticipantTalk_" + participantDelegate.participantObjectKey
							anchors.right: parent.right
							anchors.bottom: parent.bottom
							width: 9
							height: 9
							radius: 5
							border.width: 2
							border.color: participantDelegate.color
							color: participantDelegate.isListener ? Theme.accent
								: participantDelegate.talking
								? navigationRail.toneColor(participantDelegate.talkTone, Theme.success) : Theme.textMuted
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 1
						RowLayout {
							Layout.fillWidth: true
							spacing: 5
							Label {
								objectName: "navigationParticipantTitle_"
									+ participantDelegate.participantObjectKey
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: roomDelegate.title
								color: participantDelegate.talking ? Theme.textStrong
									: participantDelegate.isListener ? Theme.accent : Theme.textMain
								font.pixelSize: Theme.fontLabel
								font.bold: participantDelegate.talking || participantDelegate.isSelf
								font.italic: participantDelegate.isListener
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "navigationParticipantSelf_"
									+ participantDelegate.participantObjectKey
								visible: participantDelegate.isSelf
								textFormat: Text.PlainText
								text: qsTr("YOU")
								color: Theme.success
								font.pixelSize: 8
								font.bold: true
								Accessible.ignored: true
							}
							Rectangle {
								objectName: "navigationParticipantVolume_" + participantDelegate.participantObjectKey
								visible: participantDelegate.localVolumeVisible
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
									text: participantDelegate.localVolumeLabel
									color: Theme.accent
									font.pixelSize: 8
									font.bold: true
									Accessible.ignored: true
								}
								ToolTip.visible: volumeHover.containsMouse
								ToolTip.text: qsTr("Local volume %1").arg(String(
									participantDelegate.localVolume.label || participantDelegate.localVolumeLabel))
								MouseArea { id: volumeHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
							}
						}
						Label {
							objectName: "navigationParticipantDetails_" + participantDelegate.participantObjectKey
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: [roomDelegate.subtitle, participantDelegate.badges.slice(0, 2).join(" · ")]
								.filter(function(value) { return value.length > 0 }).join(" · ")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							visible: text.length > 0
							Accessible.ignored: true
						}
					}
					RowLayout {
						spacing: 3
						Repeater {
						model: participantDelegate.visibleStatuses
							delegate: Rectangle {
								required property var modelData
								readonly property color statusColor: navigationRail.toneColor(modelData.tone, Theme.textMuted)
								objectName: "navigationParticipantStatus_"
									+ participantDelegate.participantObjectKey + "_"
									+ String(modelData.kind || "status")
								Layout.preferredWidth: 16
								Layout.preferredHeight: 16
								radius: 8
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
									Accessible.ignored: true
								}
								ToolTip.visible: statusHover.containsMouse
								ToolTip.text: String(modelData.label || modelData.kind || "")
								MouseArea { id: statusHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
							}
						}
						Button {
							id: participantJoinButton
							objectName: "navigationParticipantJoin_" + participantDelegate.participantObjectKey
							visible: participantDelegate.canJoin
							opacity: participantDelegate.revealParticipantActions
								? 1 : (participantDelegate.talking ? 0.72 : 0.52)
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							Layout.preferredWidth: 36
							Layout.preferredHeight: 22
							padding: 0
							z: 4
							text: qsTr("Join")
							Accessible.name: text
							contentItem: Label {
								textFormat: Text.PlainText
								text: participantJoinButton.text
								Accessible.ignored: true
								color: Theme.accent
								font.pixelSize: 8
								font.bold: true
								horizontalAlignment: Text.AlignHCenter
								verticalAlignment: Text.AlignVCenter
							}
							background: Rectangle {
								radius: 7
								color: participantJoinButton.down ? Theme.accentSubtle
									: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
										participantJoinButton.hovered ? 0.18 : 0.10)
								border.width: participantJoinButton.activeFocus ? Theme.focusRingWidth : 1
								border.color: participantJoinButton.activeFocus ? Theme.focus
									: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
							}
							onClicked: participantDelegate.joinParticipantRoom()
							onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
						}
						ModernIconButton {
							id: participantActionsButton
							objectName: "navigationParticipantActions_" + participantDelegate.participantObjectKey
							visible: participantDelegate.hasParticipantActions
							opacity: participantDelegate.revealParticipantActions
								? 1 : (participantDelegate.talking ? 0.72 : 0.52)
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							Layout.preferredWidth: 24
							Layout.preferredHeight: 24
							iconName: "more"
							dense: true
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							z: 4
							Accessible.name: qsTr("Participant actions for %1").arg(roomDelegate.title)
							onClicked: navigationRail.requestParticipantMenu(
								participantDelegate.participantSession, participantDelegate.stableId,
								roomDelegate.payload,
								participantActionsButton.mapToItem(null, 0, height))
							onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
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
					objectName: "navigationParticipantMouse_" + participantDelegate.participantObjectKey
					anchors.fill: parent
					enabled: participantDelegate.visible
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
						roomDelegate.focusRow(Qt.MouseFocusReason)
						pressScene = participantDelegate.mapToItem(null, mouse.x, mouse.y)
						dragStarted = false
						suppressClick = false
						dragSourceStableId = mouse.button === Qt.LeftButton && !participantDelegate.isListener
							? participantDelegate.participantSession : ""
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
							navigationRail.requestParticipantMenu(participantDelegate.participantSession,
								participantDelegate.stableId, roomDelegate.payload,
								participantDelegate.mapToItem(null, mouse.x, mouse.y))
						else {
							participantDelegate.activateSelection()
						}
					}
					onPressAndHold: mouse => navigationRail.requestParticipantMenu(
					participantDelegate.participantSession, participantDelegate.stableId, roomDelegate.payload,
						participantDelegate.mapToItem(null, mouse.x, mouse.y))
					onDoubleClicked: participantDelegate.openConversation()
				}
				}
			}
		}
        Rectangle {
			id: selfDock
			objectName: "navigationSelfDock"
            Layout.fillWidth: true
			Layout.preferredHeight: Theme.compact ? 68 : 76
			color: selfMuteButton.activeFocus || selfDeafenButton.activeFocus
				|| profileMenuButton.activeFocus ? Theme.selfCardHover : Theme.selfCardBackground
			border.width: 0
			Behavior on color { ColorAnimation { duration: Theme.motionFast } }
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("%1 profile and audio controls").arg(clientSession.selfName)
			Accessible.description: [clientSession.selfStatusLabel,
				clientSession.selfMuted ? qsTr("Microphone muted") : "",
				clientSession.selfDeafened ? qsTr("Deafened") : ""]
				.filter(function(value) { return String(value || "").length > 0 }).join(". ")
			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				height: 1
				color: Theme.selfCardBorder
			}
            RowLayout {
                anchors.fill: parent
				anchors.leftMargin: 12
				anchors.rightMargin: 10
				anchors.topMargin: 10
				anchors.bottomMargin: 10
				spacing: 8
				Rectangle {
					id: selfAvatar
					objectName: "selfAvatar"
					Layout.preferredWidth: 34
					Layout.preferredHeight: 34
					radius: 17
					clip: true
					color: Theme.selfCardHover
					border.width: 1
					border.color: clientSession.connected ? Theme.withAlpha(Theme.accent, 0.68)
						: Theme.selfCardBorder
					readonly property string avatarSource: navigationRail.safeAvatarSource(
						((clientSession.selfMenu || ({})).avatarUrl) || "")
					Image {
						id: selfAvatarImage
						objectName: "selfAvatarImage"
						anchors.fill: parent
						source: selfAvatar.avatarSource
						sourceSize: Qt.size(Math.max(1, Math.ceil(width * 2)),
							Math.max(1, Math.ceil(height * 2)))
						asynchronous: true
						cache: false
						fillMode: Image.PreserveAspectCrop
						visible: status === Image.Ready
					}
					Label {
						objectName: "selfAvatarFallback"
						anchors.centerIn: parent
						visible: selfAvatarImage.status !== Image.Ready
						textFormat: Text.PlainText
						text: clientSession.selfName.length > 0
							? clientSession.selfName.slice(0, 1).toUpperCase() : "?"
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.bold: true
						Accessible.ignored: true
					}
					Rectangle {
						objectName: "selfPresenceDot"
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						anchors.rightMargin: 1
						anchors.bottomMargin: 1
						width: 10
						height: 10
						radius: 5
						color: clientSession.selfDeafened || clientSession.selfMuted ? Theme.danger
							: clientSession.connected ? Theme.success : Theme.textFaint
						border.width: 2
						border.color: selfDock.color
						Accessible.ignored: true
					}
				}
                ColumnLayout {
                    Layout.fillWidth: true
					spacing: 1
					Label {
						objectName: "selfNameLabel"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: clientSession.selfName
						color: Theme.textStrong
						font.bold: true
						elide: Text.ElideRight
						Accessible.ignored: true
					}
					Label {
						objectName: "selfStatusLabel"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: clientSession.selfStatusLabel
						color: Theme.textFaint
						font.pixelSize: Theme.fontCaption
						elide: Text.ElideRight
						Accessible.ignored: true
					}
                }
				ModernIconButton {
					id: selfMuteButton
					objectName: "selfMuteButton"
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.tab: selfDeafenButton
					KeyNavigation.backtab: rooms
					iconName: clientSession.selfMuted ? "mute" : "microphone"
					selected: !!clientSession.selfMuted
					tone: clientSession.selfMuted ? "danger" : "neutral"
					Accessible.name: clientSession.selfMuted ? qsTr("Unmute microphone") : qsTr("Mute microphone")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: uiCommands.toggleSelfMute()
				}
				ModernIconButton {
					id: selfDeafenButton
					objectName: "selfDeafenButton"
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.tab: profileMenuButton
					KeyNavigation.backtab: selfMuteButton
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
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.backtab: selfDeafenButton
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
