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
	// Both the desktop rail and the compact Drawer instantiate this component.
	// Give automation a stable, unambiguous list target when both instances are
	// present in the object tree but only one is visible.
	property string roomListObjectName: "navigationRooms"
	property bool commitOnSelection: false
	property bool accessibilitySuppressed: false
	property bool settingsEnabled: true
	property bool stonksEnabled: false
	property bool serverMenuOpen: false
	// Mirror identity chrome with the rail so the server card keeps the same
	// outside-to-inside reading order on either side of the conversation.
	property string railSide: Theme.railSide
	readonly property bool railOnLeft: railSide === "left"
	property bool classicUserIcons: Theme.classicUserIcons
	// The desktop shell supplies the matching conversation chrome heights so
	// the horizontal dividers form one continuous line regardless of whether
	// the rail is placed on the left or the right. Drawer instances keep the
	// standalone theme defaults.
	property real alignedHeaderHeight: Theme.railHeaderHeight
	property real alignedFooterHeight: Theme.railFooterHeight
	// Deterministic screenshot fixtures must not inherit the workstation cursor's
	// hover position. Product builds keep the normal pointer feedback.
	property bool visualFixtureMode: false
	property string activeScopeMenuToken: ""
	property string localFilterText: ""
	property var localRoomExpansion: ({})
	property int navigationPresentationRevision: 0
	property bool selectionRevealPending: false
	readonly property string effectiveFilterText: {
		const revision = navigationPresentationRevision
		if (navigationModel && navigationModel.filterText !== undefined)
			return String(navigationModel.filterText || "")
		return localFilterText
	}
	// Stable row key, rather than session alone: the same user may have a normal
	// row and one or more listener rows in different scopes.
	property string activeParticipantMenuKey: ""
    signal selectionCommitted()
	signal scopeMenuRequested(string scopeToken, string kind, var actions, var anchorPoint)
	signal participantMenuRequested(string sessionId, var actions, var anchorPoint,
		string entryKind, string scopeToken, string rowKey)
	signal settingsRequested()
	signal stonksRequested()
	signal profileMenuRequested(var anchorPoint)
	signal serverMenuRequested(var anchorPoint)
	Accessible.ignored: accessibilitySuppressed
	Component.onCompleted: scheduleSelectionReveal()


	function normalizedFilterText() {
		return String(effectiveFilterText || "").trim().toLocaleLowerCase()
	}

	function valueMatchesFilter(value, filter) {
		return filter.length > 0 && String(value || "").toLocaleLowerCase().indexOf(filter) >= 0
	}

	function listMatchesFilter(values, filter) {
		const list = values || []
		for (let index = 0; index < list.length; ++index) {
			const value = list[index] || ({})
			if (typeof value === "object") {
				if (valueMatchesFilter(value.label, filter) || valueMatchesFilter(value.kind, filter))
					return true
			} else if (valueMatchesFilter(value, filter)) {
				return true
			}
		}
		return false
	}

	function participantSessionForRow(row) {
		if (!row)
			return ""
		const payload = row.payload || row
		const source = payload.source || ({})
		const stableId = String(row.stableId || payload.id || "")
		return String(payload.participantSession || source.session
			|| (stableId.substring(0, 5) === "user:" ? stableId.substring(5)
				: (/^[0-9]+$/.test(stableId) ? stableId : "")))
	}

	function participantEntryKind(row) {
		const payload = (row && (row.payload || row)) || ({})
		const source = payload.source || ({})
		return String(payload.entryKind || source.entryKind || "user").toLowerCase()
	}

	function participantMatchesFilter(row, filter) {
		if (!row || filter.length === 0)
			return true
		const payload = row.payload || row
		const source = payload.source || ({})
		return valueMatchesFilter(row.title || payload.title || source.label, filter)
			|| valueMatchesFilter(row.subtitle || payload.subtitle, filter)
			|| valueMatchesFilter(row.status || payload.talkState || source.talkState, filter)
			|| valueMatchesFilter(payload.talkLabel || source.talkLabel, filter)
			|| listMatchesFilter(payload.badges || source.badges, filter)
			|| listMatchesFilter(payload.statuses || source.statuses, filter)
	}

	function roomOwnMatchesFilter(row, filter) {
		if (!row || filter.length === 0)
			return true
		const payload = row.payload || row
		return valueMatchesFilter(row.title || payload.title, filter)
			|| valueMatchesFilter(row.subtitle || payload.subtitle, filter)
			|| valueMatchesFilter(payload.pathLabel, filter)
			|| listMatchesFilter(payload.badges, filter)
	}

	function roomHasMatchingParticipant(scopeToken, filter) {
		if (filter.length === 0)
			return false
		const count = Number(navigationModel.count) || 0
		for (let index = 0; index < count; ++index) {
			const candidate = navigationModel.get(index)
			if (!candidate || String(candidate.kind || "") !== "participant")
				continue
			const payload = candidate.payload || candidate
			if (String(payload.parentScopeToken || "") === String(scopeToken || "")
					&& participantMatchesFilter(candidate, filter))
				return true
		}
		return false
	}

	function selectedParticipantMatches(row) {
		if (!row || selectionState.selectedUserSession === undefined
				|| selectionState.selectedUserSession === null || participantEntryKind(row) !== "user")
			return false
		return participantSessionForRow(row) === String(selectionState.selectedUserSession)
	}

	function roomContainsSelectedParticipant(scopeToken) {
		if (selectionState.selectedUserSession === undefined || selectionState.selectedUserSession === null)
			return false
		const count = Number(navigationModel.count) || 0
		for (let index = 0; index < count; ++index) {
			const candidate = navigationModel.get(index)
			if (!candidate || String(candidate.kind || "") !== "participant"
					|| participantEntryKind(candidate) !== "user")
				continue
			const payload = candidate.payload || candidate
			if (String(payload.parentScopeToken || "") === String(scopeToken || "")
					&& selectedParticipantMatches(candidate))
				return true
		}
		return false
	}

	function roomMatchesVoiceSelection(scopeToken) {
		if (selectionState.selectedVoiceChannelId === undefined
				|| selectionState.selectedVoiceChannelId === null)
			return false
		const parts = String(scopeToken || "").split(":")
		return parts.length === 2 && parts[1] === String(selectionState.selectedVoiceChannelId)
	}

	function roomExpandedFor(scopeToken, payload) {
		const revision = navigationPresentationRevision
		if (navigationModel && typeof navigationModel.isRoomExpanded === "function")
			return !!navigationModel.isRoomExpanded(String(scopeToken || ""))
		const token = String(scopeToken || "")
		if (localRoomExpansion[token] !== undefined)
			return !!localRoomExpansion[token]
		if (payload && payload.expanded !== undefined)
			return !!payload.expanded
		return true
	}

	function participantCountForRoom(scopeToken, payload) {
		const supplied = Number((payload || ({})).participantCount)
		if (Number.isFinite(supplied) && supplied >= 0)
			return Math.floor(supplied)
		let count = 0
		for (let index = 0; index < Number(navigationModel.count || 0); ++index) {
			const candidate = navigationModel.get(index)
			const candidatePayload = (candidate && (candidate.payload || candidate)) || ({})
			if (candidate && String(candidate.kind || "") === "participant"
					&& String(candidatePayload.parentScopeToken || "") === String(scopeToken || ""))
				++count
		}
		return count
	}

	function participantRowTalking(row) {
		const payload = (row && (row.payload || row)) || ({})
		const source = payload.source || ({})
		const state = String(row && row.status || payload.talkState || source.talkState || "").toLowerCase()
		return !!payload.talking || !!source.talking
			|| ["talking", "whispering", "shouting", "mutedtalking"].indexOf(state) >= 0
	}

	function talkingCountForRoom(scopeToken, payload) {
		const supplied = Number((payload || ({})).talkingParticipantCount)
		if (Number.isFinite(supplied) && supplied >= 0)
			return Math.floor(supplied)
		let count = 0
		for (let index = 0; index < Number(navigationModel.count || 0); ++index) {
			const candidate = navigationModel.get(index)
			const candidatePayload = (candidate && (candidate.payload || candidate)) || ({})
			if (candidate && String(candidate.kind || "") === "participant"
					&& String(candidatePayload.parentScopeToken || "") === String(scopeToken || "")
					&& participantRowTalking(candidate))
				++count
		}
		return count
	}

	function navigationRowIsVisible(row) {
		if (!row)
			return false
		const kind = String(row.kind || "")
		const payload = row.payload || row
		const scopeToken = String(row.scopeToken || payload.scopeToken || "")
		const filter = normalizedFilterText()
		if (kind !== "participant") {
			if (navigationRowIsSelected(row) || roomMatchesVoiceSelection(scopeToken))
				return true
			// The C++ model precomputes the steady-state/filter visibility role. Use
			// its O(1) positive path before the selected-user ancestry fallback so a
			// large connected server does not scan every participant for every room.
			if (payload.railVisible !== undefined && !!payload.railVisible)
				return true
			if (roomContainsSelectedParticipant(scopeToken))
				return true
			if (payload.railVisible !== undefined)
				return false
			return filter.length === 0 || roomOwnMatchesFilter(row, filter)
				|| roomHasMatchingParticipant(scopeToken, filter)
		}
		if (selectedParticipantMatches(row))
			return true
		const parentScope = String(payload.parentScopeToken || "")
		if (!roomExpandedFor(parentScope, payload))
			return false
		if (payload.railVisible !== undefined)
			return !!payload.railVisible
		if (filter.length === 0)
			return true
		let parentMatches = false
		for (let index = 0; index < Number(navigationModel.count || 0); ++index) {
			const candidate = navigationModel.get(index)
			if (candidate && String(candidate.kind || "") !== "participant"
					&& String(candidate.scopeToken || "") === parentScope) {
				parentMatches = roomOwnMatchesFilter(candidate, filter)
				break
			}
		}
		return parentMatches || participantMatchesFilter(row, filter)
	}

	function rowStartsVisibleSection(rowIndex, sectionKind) {
		const revision = navigationPresentationRevision
		const section = String(sectionKind || "")
		if (rowIndex < 0 || section.length === 0)
			return false
		for (let index = rowIndex - 1; index >= 0; --index) {
			const previous = navigationModel.get(index)
			if (!previous || String(previous.sectionKind || "") !== section)
				return true
			if (navigationRowIsVisible(previous))
				return false
		}
		return true
	}

	function setNavigationFilter(value) {
		const accepted = String(value || "").slice(0, 128)
		if (navigationModel && typeof navigationModel.setFilterText === "function")
			navigationModel.setFilterText(accepted)
		else
			localFilterText = accepted
		++navigationPresentationRevision
		Qt.callLater(navigationRail.ensureCurrentNavigationVisible)
	}

	function setRoomExpanded(scopeToken, expanded) {
		const token = String(scopeToken || "")
		if (token.length === 0)
			return
		const listHadFocus = rooms.activeFocus || (rooms.currentItem && rooms.currentItem.activeFocus)
		if (navigationModel && typeof navigationModel.setRoomExpanded === "function") {
			navigationModel.setRoomExpanded(token, !!expanded)
		} else {
			const state = Object.assign({}, localRoomExpansion)
			state[token] = !!expanded
			localRoomExpansion = state
		}
		++navigationPresentationRevision
		Qt.callLater(function() {
			const current = navigationModel.get(rooms.currentIndex)
			if (current && !navigationRowIsVisible(current)) {
				const roomIndex = navigationIndexForScope(token)
				if (listHadFocus)
					focusNavigationIndex(roomIndex, Qt.OtherFocusReason)
				else
					setCurrentNavigationIndex(roomIndex)
			} else {
				ensureCurrentNavigationVisible()
			}
		})
	}

	function toggleRoomExpanded(scopeToken, payload) {
		setRoomExpanded(scopeToken, !roomExpandedFor(scopeToken, payload))
	}

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
			if (room && String(room.kind) !== "participant"
					&& String(room.sectionKind || room.kind) === kind
					&& navigationRowIsVisible(room))
				++matches
		}
		return matches
	}

	function navigationIndexForScope(scopeToken) {
		const token = String(scopeToken || "")
		for (let index = 0; index < Number(navigationModel.count || 0); ++index) {
			const row = navigationModel.get(index)
			if (row && String(row.kind || "") !== "participant"
					&& String(row.scopeToken || "") === token)
				return index
		}
		return -1
	}

	function firstVisibleNavigationIndex() {
		for (let index = 0; index < Number(navigationModel.count || 0); ++index) {
			if (navigationRowIsVisible(navigationModel.get(index)))
				return index
		}
		return -1
	}

	function lastVisibleNavigationIndex() {
		for (let index = Number(navigationModel.count || 0) - 1; index >= 0; --index) {
			if (navigationRowIsVisible(navigationModel.get(index)))
				return index
		}
		return -1
	}

	function nextVisibleNavigationIndex(index, direction) {
		const step = direction < 0 ? -1 : 1
		for (let candidate = Number(index) + step;
				candidate >= 0 && candidate < Number(navigationModel.count || 0); candidate += step) {
			if (navigationRowIsVisible(navigationModel.get(candidate)))
				return candidate
		}
		return -1
	}

	function stepVisibleNavigationIndex(index, distance) {
		const direction = distance < 0 ? -1 : 1
		let target = Number(index)
		let remaining = Math.abs(Number(distance))
		while (remaining > 0) {
			const next = nextVisibleNavigationIndex(target, direction)
			if (next < 0)
				break
			target = next
			--remaining
		}
		return target
	}

	function ensureCurrentNavigationVisible() {
		const selected = selectedNavigationIndex()
		if (selected >= 0)
			return setCurrentNavigationIndex(selected)
		const current = navigationModel.get(rooms.currentIndex)
		if (current && navigationRowIsVisible(current))
			return true
		return setCurrentNavigationIndex(firstVisibleNavigationIndex())
	}

	// Qt 6.9's QQuickItemViewFxItem::setVisible() writes the delegate's private
	// isAccessible flag on every cull/layout pass. That can overwrite a QML
	// Accessible.ignored binding after a modal has already suppressed the rail.
	// Reassert only the virtualized semantic owners while the modal is active;
	// regular rows keep their native ListView accessibility at steady state.
	function refreshVirtualizedAccessibility() {
		if (!navigationRail.accessibilitySuppressed || !railAccessibilityBarrier.active
				|| !rooms.contentItem)
			return
		// ItemView delegates and section delegates are direct materialized
		// children of contentItem. Do not scan the model or descendant controls.
		const delegates = rooms.contentItem.children || []
		for (let index = 0; index < delegates.length; ++index) {
			const item = delegates[index]
			if (!item)
				continue
			const name = String(item.objectName || "")
			if (item.navigationVisible !== undefined && item.accessibilityPooled !== undefined)
				railAccessibilityBarrier.reassertItem(item)
		}
	}

	onAccessibilitySuppressedChanged: {
		if (accessibilitySuppressed) {
			refreshVirtualizedAccessibility()
			Qt.callLater(refreshVirtualizedAccessibility)
		}
	}

	function safeAvatarSource(value) {
		const source = String(value || "").trim()
		return /^(image:\/\/mumble\/|data:image\/)/i.test(source) ? source : ""
	}

	function classicParticipantIconSource(entryKind, talkState) {
		if (String(entryKind || "").toLowerCase() === "listener")
			return "qrc:/native/talking_off.svg"
		switch (String(talkState || "").toLowerCase()) {
		case "talking": return "qrc:/native/talking_on.svg"
		case "whispering": return "qrc:/native/talking_whisper.svg"
		case "shouting": return "qrc:/native/talking_shout.svg"
		case "mutedtalking": return "qrc:/native/muted_self.svg"
		default: return "qrc:/native/talking_off.svg"
		}
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
		const kind = String(row.kind || "")
		if (kind !== "participant") {
			const selectedScopeToken = selectionState.scopeToken === undefined
				|| selectionState.scopeToken === null ? "" : String(selectionState.scopeToken)
			return selectedScopeToken.length > 0
				&& selectedScopeToken === String(row.scopeToken || "")
		}
		if (selectionState.selectedUserSession === undefined
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

	function activateServerMenu(anchorItem) {
		const anchor = anchorItem || serverHeader
		serverMenuRequested(anchor.mapToItem(null, 0, anchor.height))
	}

	function selectedNavigationIndex() {
		for (let index = 0; index < rooms.count; ++index) {
			const row = navigationModel.get(index)
			if (navigationRowIsSelected(row) && navigationRowIsVisible(row))
				return index
		}
		return -1
	}

	function navigationSelectionRequested() {
		const scopeToken = selectionState.scopeToken === undefined
			|| selectionState.scopeToken === null ? "" : String(selectionState.scopeToken)
		return scopeToken.length > 0
			|| (selectionState.selectedUserSession !== undefined
				&& selectionState.selectedUserSession !== null)
			|| (selectionState.selectedVoiceChannelId !== undefined
				&& selectionState.selectedVoiceChannelId !== null)
	}

	function scheduleSelectionReveal() {
		if (!navigationSelectionRequested()) {
			selectionRevealPending = false
			return
		}
		selectionRevealPending = true
		Qt.callLater(navigationRail.revealCurrentSelection)
	}

	function revealCurrentSelection() {
		const selectedIndex = selectedNavigationIndex()
		const revealed = selectedIndex >= 0 && setCurrentNavigationIndex(selectedIndex)
		if (revealed)
			selectionRevealPending = false
		return revealed
	}

	function navigationPageStep() {
		return Math.max(1, Math.floor(Math.max(1, rooms.height) /
			Math.max(1, Theme.roomRowHeight)))
	}

	function setCurrentNavigationIndex(index, direction) {
		if (rooms.count <= 0)
			return false
		const requestedIndex = Number(index)
		if (!Number.isFinite(requestedIndex) || requestedIndex < 0)
			return false
		let targetIndex = Math.max(0, Math.min(rooms.count - 1, Math.floor(requestedIndex)))
		if (!navigationRowIsVisible(navigationModel.get(targetIndex))) {
			const preferredDirection = Number(direction || 0)
			if (preferredDirection !== 0)
				targetIndex = nextVisibleNavigationIndex(targetIndex, preferredDirection)
			else {
				const after = nextVisibleNavigationIndex(targetIndex, 1)
				const before = nextVisibleNavigationIndex(targetIndex, -1)
				targetIndex = after >= 0 ? after : before
			}
		}
		if (targetIndex < 0)
			return false
		if (rooms.currentIndex !== targetIndex)
			rooms.currentIndex = targetIndex
		rooms.forceLayout()
		rooms.positionViewAtIndex(targetIndex, ListView.Contain)
		// Newly inserted rows can still gain their section header height after the
		// first view-position pass. Re-apply containment once layout has settled,
		// but never pull the rail back if the user has already moved elsewhere.
		Qt.callLater(function() {
			if (rooms.currentIndex !== targetIndex)
				return
			rooms.forceLayout()
			rooms.positionViewAtIndex(targetIndex, ListView.Contain)
		})
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
			Qt.callLater(function() { selfIdentityButton.forceActiveFocus() })
			return selfIdentityButton.objectName
		}
		let selectedIndex = selectedNavigationIndex()
		const firstActionableIndex = firstVisibleNavigationIndex()
		focusNavigationIndex(selectedIndex >= 0 ? selectedIndex : Math.max(0, firstActionableIndex),
			Qt.TabFocusReason)
		return rooms.currentItem ? rooms.currentItem.objectName : rooms.objectName
	}

	implicitWidth: 310
	implicitHeight: 600
	// The rail is an accessibility container, not an actionable tab stop. Its
	// virtualized ListView contributes one navigation stop, followed by the fixed
	// footer actions.
	activeFocusOnTab: false
	Connections {
		target: selectionState
		ignoreUnknownSignals: true
		function onScopeTokenChanged() {
			navigationRail.scheduleSelectionReveal()
		}
		function onSelectedUserSessionChanged() {
			navigationRail.scheduleSelectionReveal()
		}
		function onSelectedVoiceChannelIdChanged() {
			navigationRail.scheduleSelectionReveal()
		}
	}
	Connections {
		target: navigationModel
		ignoreUnknownSignals: true
		function onModelReset() {
			navigationRail.scheduleSelectionReveal()
		}
		function onFilterTextChanged() {
			++navigationRail.navigationPresentationRevision
			Qt.callLater(navigationRail.ensureCurrentNavigationVisible)
		}
		function onRoomExpansionChanged() {
			++navigationRail.navigationPresentationRevision
			Qt.callLater(navigationRail.ensureCurrentNavigationVisible)
		}
	}
    color: Theme.rail
	border.width: 0
	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 1
		visible: !navigationRail.railOnLeft
		color: Theme.divider
		z: 20
	}
	Rectangle {
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 1
		visible: navigationRail.railOnLeft
		color: Theme.divider
		z: 20
	}
	ColumnLayout {
		id: railContentLayout
		anchors.fill: parent
        spacing: 0
		Rectangle {
			id: serverHeader
			objectName: "navigationServerHeader"
            Layout.fillWidth: true
			Layout.preferredHeight: navigationRail.alignedHeaderHeight
			color: serverHeaderClickArea.containsMouse || activeFocus
				|| navigationRail.serverMenuOpen ? Theme.surfaceHover : Theme.panel
			border.width: activeFocus ? Theme.focusRingWidth : 0
			border.color: Theme.focus
			Behavior on color { ColorAnimation { duration: Theme.motionFast } }
			activeFocusOnTab: !navigationRail.accessibilitySuppressed
			Accessible.role: Accessible.Button
			Accessible.name: qsTr("Open server actions for %1").arg(clientSession.serverName)
			Accessible.description: [clientSession.connectionLabel, clientSession.connectionDetail]
				.filter(function(value) { return String(value || "").trim().length > 0 }).join(". ")
			Accessible.onPressAction: navigationRail.activateServerMenu(serverActionsButton)
			Keys.onReturnPressed: event => {
				navigationRail.activateServerMenu(serverActionsButton)
				event.accepted = true
			}
			Keys.onSpacePressed: event => {
				navigationRail.activateServerMenu(serverActionsButton)
				event.accepted = true
			}
			DragHandler {
				enabled: !navigationRail.commitOnSelection
				target: null
				onActiveChanged: if (active && navigationRail.Window.window)
					navigationRail.Window.window.startSystemMove()
			}
			MouseArea {
				id: serverHeaderClickArea
				anchors.fill: parent
				enabled: !navigationRail.accessibilitySuppressed
				hoverEnabled: true
				cursorShape: Qt.PointingHandCursor
				onClicked: navigationRail.activateServerMenu(serverActionsButton)
			}
			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.bottom: parent.bottom
				height: 1
				color: Theme.divider
			}
			RowLayout {
				id: serverHeaderContent
				objectName: "navigationServerHeaderContent"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				// A slightly deeper outer inset pulls the identity cluster toward the
				// card's optical center without losing its side-aware alignment.
				anchors.leftMargin: Theme.space5
				anchors.rightMargin: Theme.space5
				LayoutMirroring.enabled: !navigationRail.railOnLeft
				LayoutMirroring.childrenInherit: true
				spacing: Theme.space3
				Rectangle {
					id: serverBadge
					objectName: "navigationServerBadge"
					readonly property string imageSource: navigationRail.safeAvatarSource(
						clientSession.serverImageUrl || "")
					Layout.alignment: Qt.AlignVCenter
					Layout.preferredWidth: Theme.railBadgeSize
					Layout.preferredHeight: Layout.preferredWidth
					radius: Theme.railBadgeRadius
					color: imageSource.length > 0 ? Theme.surfaceRaised : Theme.accent
					border.width: 1
					border.color: serverHeaderClickArea.containsMouse
						? Theme.accent : Theme.elevationHighlight
					clip: true
					Accessible.ignored: true
					Image {
						id: serverIdentityImage
						objectName: "navigationServerImage"
						anchors.fill: parent
						source: serverBadge.imageSource
						asynchronous: true
						cache: false
						sourceSize: Qt.size(64, 64)
						fillMode: Image.PreserveAspectCrop
						visible: status === Image.Ready
						Accessible.ignored: true
					}
					Label {
						objectName: "navigationServerMonogram"
						anchors.centerIn: parent
						textFormat: Text.PlainText
						text: String(clientSession.serverMonogram || "").trim()
							|| navigationRail.serverMonogram(clientSession.serverName)
						color: Theme.onAccent
						font.bold: true
						font.pixelSize: Theme.fontBody
						visible: serverIdentityImage.status !== Image.Ready
						Accessible.ignored: true
					}
				}
				ColumnLayout {
					Layout.fillWidth: true
					Layout.alignment: Qt.AlignVCenter
					spacing: Theme.railHeaderSpacing
					Label {
						objectName: "navigationServerName"
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: clientSession.serverName
						color: Theme.textStrong
						font.bold: true
						font.pixelSize: Theme.fontBody + 1
						elide: Text.ElideRight
						horizontalAlignment: navigationRail.railOnLeft
							? Text.AlignLeft : Text.AlignRight
						Accessible.ignored: true
					}
					Rectangle {
						id: connectionPill
						objectName: "navigationConnectionPill"
						readonly property color statusColor: navigationRail.toneColor(
							clientSession.connectionTone,
							clientSession.connected ? Theme.success : Theme.textFaint)
						Layout.preferredWidth: Math.min(parent.width,
							connectionPillContent.implicitWidth + 16)
						Layout.preferredHeight: 20
						Layout.alignment: navigationRail.railOnLeft
							? Qt.AlignLeft : Qt.AlignRight
						radius: 10
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
				ModernIconButton {
					id: serverActionsButton
					objectName: "navigationServerActions"
					Layout.alignment: Qt.AlignVCenter
					Layout.preferredWidth: 30
					Layout.preferredHeight: 30
					iconName: "more"
					dense: true
					selected: navigationRail.serverMenuOpen
					opacity: selected || hovered || visualFocus ? 1 : 0.72
					Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
					Accessible.name: qsTr("Server actions for %1").arg(clientSession.serverName)
					Accessible.description: qsTr("Server information, connection, and administration")
					ToolTip.visible: hovered
					ToolTip.text: qsTr("Server actions")
					onClicked: navigationRail.activateServerMenu(serverActionsButton)
				}
			}
        }
		Item {
			id: navigationFilterBar
			objectName: "navigationFilterBar"
			Layout.fillWidth: true
			Layout.preferredHeight: 50
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("Filter rooms and people")

			ModernTextField {
				id: navigationFilterField
				objectName: "navigationFilterField"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 12
				anchors.rightMargin: 12
				dense: true
				activeFocusOnTab: true
				KeyNavigation.tab: rooms
				leftPadding: 34
				rightPadding: text.length > 0 ? 34 : Theme.space3
				text: navigationRail.effectiveFilterText
				placeholderText: qsTr("Search rooms or people")
				Accessible.name: qsTr("Search rooms or people")
				Accessible.description: text.length > 0
					? qsTr("Showing matching rooms and people. The current selection remains visible.")
					: qsTr("Type to filter the room navigator")
				onTextEdited: navigationRail.setNavigationFilter(text)
				Keys.onEscapePressed: event => {
					if (text.length > 0)
						navigationRail.setNavigationFilter("")
					else
						rooms.forceActiveFocus(Qt.TabFocusReason)
					event.accepted = true
				}
				Keys.onDownPressed: event => {
					const index = navigationRail.selectedNavigationIndex() >= 0
						? navigationRail.selectedNavigationIndex()
						: navigationRail.firstVisibleNavigationIndex()
					event.accepted = navigationRail.focusNavigationIndex(index, Qt.OtherFocusReason)
				}
				Keys.onReturnPressed: event => {
					const index = navigationRail.selectedNavigationIndex() >= 0
						? navigationRail.selectedNavigationIndex()
						: navigationRail.firstVisibleNavigationIndex()
					event.accepted = navigationRail.focusNavigationIndex(index, Qt.OtherFocusReason)
				}
				ModernIcon {
					anchors.left: parent.left
					anchors.leftMargin: 11
					anchors.verticalCenter: parent.verticalCenter
					name: "search"
					size: 14
					color: navigationFilterField.activeFocus ? Theme.accent : Theme.textMuted
					Accessible.ignored: true
				}
				ModernIconButton {
					id: navigationFilterClear
					hoverEnabled: !navigationRail.visualFixtureMode
					objectName: "navigationFilterClear"
					anchors.right: parent.right
					anchors.rightMargin: 3
					anchors.verticalCenter: parent.verticalCenter
					width: 26
					height: 26
					visible: navigationFilterField.text.length > 0
					iconName: "close"
					dense: true
					activeFocusOnTab: false
					focusPolicy: Qt.ClickFocus
					Accessible.name: qsTr("Clear room filter")
					onClicked: {
						navigationRail.setNavigationFilter("")
						navigationFilterField.forceActiveFocus(Qt.MouseFocusReason)
					}
				}
			}
		}
		ListView {
			id: rooms
			objectName: navigationRail.roomListObjectName
            Layout.fillWidth: true
            Layout.fillHeight: true
			model: navigationModel
			onCountChanged: {
				if (navigationRail.selectionRevealPending)
					navigationRail.scheduleSelectionReveal()
			}
			clip: true
			// The virtualized list is the navigator's single tab stop. Rows remain
			// directly focusable by pointer, automation and focusInitialItem(), while
			// keyboard users move the current row with arrows without tabbing through
			// every delegate on a large server.
			activeFocusOnTab: activeFocus || count > 0
			KeyNavigation.tab: selfIdentityButton
			KeyNavigation.backtab: navigationFilterField
			Accessible.role: Accessible.List
			Accessible.name: qsTr("Rooms and participants")
			// Keep exactly one accessibility focus owner: the materialized current
			// row when available, otherwise this virtualized list itself.
			Accessible.focused: activeFocus && (!currentItem || !currentItem.activeFocus)
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
			MiddleDragScrollHandler {
				targetFlickable: rooms
				horizontalEnabled: false
			}
			Keys.onPressed: event => {
				if (event.key === Qt.Key_Up) {
					event.accepted = navigationRail.setCurrentNavigationIndex(currentIndex - 1, -1)
				} else if (event.key === Qt.Key_Down) {
					event.accepted = navigationRail.setCurrentNavigationIndex(currentIndex + 1, 1)
				} else if (event.key === Qt.Key_Home) {
					event.accepted = navigationRail.setCurrentNavigationIndex(
						navigationRail.firstVisibleNavigationIndex())
				} else if (event.key === Qt.Key_End) {
					event.accepted = navigationRail.setCurrentNavigationIndex(
						navigationRail.lastVisibleNavigationIndex())
				} else if (event.key === Qt.Key_PageUp) {
					event.accepted = navigationRail.setCurrentNavigationIndex(
						navigationRail.stepVisibleNavigationIndex(currentIndex,
							-navigationRail.navigationPageStep()))
				} else if (event.key === Qt.Key_PageDown) {
					event.accepted = navigationRail.setCurrentNavigationIndex(
						navigationRail.stepVisibleNavigationIndex(currentIndex,
							navigationRail.navigationPageStep()))
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
					if (kind === "voice" && participantCount > 0) {
						appendUnique(participantCount === 1 ? qsTr("1 participant")
							: qsTr("%1 participants").arg(participantCount))
						if (talkingParticipantCount === 1)
							appendUnique(qsTr("1 person speaking"))
						else if (talkingParticipantCount > 1)
							appendUnique(qsTr("%1 people speaking").arg(talkingParticipantCount))
						appendUnique(roomExpanded ? qsTr("Expanded") : qsTr("Collapsed"))
					}
					if (screenShareVisible)
						appendUnique(String(screenShare.statusLabel || screenShare.badgeLabel || ""))
					if (screenShareActive) {
						appendUnique(screenShare.ownerLabel)
						appendUnique(screenShare.resolutionLabel)
						appendUnique(screenShare.runtimeLabel)
					}
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
				readonly property var navigationRowState: ({
					"stableId": stableId, "scopeToken": scopeToken, "title": title,
					"subtitle": subtitle, "kind": kind, "status": status,
					"selected": selected, "payload": payload
				})
				readonly property bool navigationVisible: navigationRail.navigationRowIsVisible(navigationRowState)
				// ListView keeps a small materialized buffer beyond its clipped viewport.
				// Windows UIA reports those delegates at their unclipped scene bounds, so
				// publish a row only while its complete semantic surface is visible.
				readonly property bool accessibilityViewportVisible: navigationVisible
					&& height > 0 && rooms.height > 0
					&& y >= rooms.contentY - 0.5
					&& y + height <= rooms.contentY + rooms.height + 0.5
				readonly property bool startsVisibleSection: navigationVisible
					&& navigationRail.rowStartsVisibleSection(index, sectionKind)
				readonly property string sectionVisualLabel: sectionKind === "voice" ? qsTr("VOICE ROOMS")
					: sectionKind === "direct" ? qsTr("DIRECT MESSAGES")
					: sectionKind === "tool" ? qsTr("TOOLS") : qsTr("TEXT ROOMS")
				readonly property int sectionHeaderHeight: startsVisibleSection ? 34 : 0
				readonly property bool joined: !!payload.joined || payload.status === "joined"
				readonly property bool canJoin: kind === "voice" && !joined
					&& (payload.canJoin === undefined || !!payload.canJoin)
				readonly property var screenShare: payload.screenShare || ({})
				readonly property bool screenShareVisible: !!screenShare.visible
				readonly property bool screenShareActive: screenShareVisible
					&& String(screenShare.streamId || "").trim().length > 0
				readonly property string roomDetailText: screenShareActive
					? (joined ? qsTr("You are here · %1").arg(String(screenShare.statusLabel
						|| screenShare.ownerLabel || qsTr("Live screen share")))
						: String(screenShare.statusLabel || screenShare.ownerLabel || subtitle))
					: (joined ? qsTr("You are here") : subtitle)
				readonly property var roomActions: payload.actions
					|| (payload.source && payload.source.actions) || []
				readonly property var roomBadges: payload.badges || []
				readonly property int participantCount: isParticipant ? 0
					: navigationRail.participantCountForRoom(scopeToken, payload)
				readonly property int talkingParticipantCount: isParticipant ? 0
					: navigationRail.talkingCountForRoom(scopeToken, payload)
				readonly property bool roomExpanded: kind !== "voice" || participantCount <= 0
					? true : navigationRail.roomExpandedFor(scopeToken, payload)
				readonly property bool hasRoomActions: roomActions.some(function(action) {
					return action && String(action.kind || "action") !== "separator"
				})
				readonly property bool detailsVisible: !isParticipant && (screenShareActive || unreadCount > 0
					|| (selected && kind !== "voice" && subtitle.length > 0))
				readonly property int roomRowHeight: detailsVisible
					? Theme.roomRowHeightDetailed : Theme.roomRowHeight
				readonly property bool rowFocusVisible: activeFocus
					|| (rooms.activeFocus && ListView.isCurrentItem)
				readonly property bool revealActions: (!navigationRail.visualFixtureMode
					&& roomMouse.containsMouse) || rowFocusVisible
					|| (navigationRail.activeScopeMenuToken.length > 0
						&& navigationRail.activeScopeMenuToken === scopeToken)
					|| joinButton.activeFocus || shareButton.activeFocus || roomActionsButton.activeFocus
				objectName: isParticipant
					? "navigationParticipantSemantic_" + participantDelegate.participantObjectKey
					: "navigationRoom_" + stableId
				width: rooms.width - 20
				height: navigationVisible
					? sectionHeaderHeight + (isParticipant ? Theme.participantRowHeight : roomRowHeight) : 0
				visible: navigationVisible
				enabled: navigationVisible
                radius: 8
				color: isParticipant ? "transparent"
					: roomDropArea.containsDrag ? Theme.selected
					: (!navigationRail.visualFixtureMode && roomMouse.pressed) ? Theme.accentSubtle
					: selected ? Theme.selected
					: joined ? Theme.withAlpha(Theme.success, 0.045)
					: (!navigationRail.visualFixtureMode && roomMouse.containsMouse)
						? Theme.surfaceHover : "transparent"
				border.width: isParticipant ? 0 : rowFocusVisible ? Theme.focusRingWidth
					: roomDropArea.containsDrag ? 2 : 0
				border.color: rowFocusVisible ? Theme.focus
					: roomDropArea.containsDrag ? Theme.accent : "transparent"
				opacity: roomMouse.drag.active ? 0.72 : 1.0
				activeFocusOnTab: false
				onActiveFocusChanged: if (activeFocus) makeCurrent()
				Accessible.role: Accessible.ListItem
				Accessible.name: title
				Accessible.description: isParticipant ? participantDelegate.accessibleDetails()
					: accessibleRoomDetails()
				Accessible.selected: isParticipant ? participantDelegate.selectedParticipant : selected
				Accessible.focused: activeFocus
				Accessible.onPressAction: activateSelection()
				Binding {
					target: roomDelegate
					property: "Accessible.ignored"
					value: navigationRail.accessibilitySuppressed
						|| roomDelegate.accessibilityPooled || !roomDelegate.navigationVisible
						|| !roomDelegate.accessibilityViewportVisible
				}
				Item {
					id: inlineSectionHeader
					objectName: roomDelegate.startsVisibleSection
						? "navigationSection_" + roomDelegate.sectionKind : ""
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					height: roomDelegate.sectionHeaderHeight
					visible: roomDelegate.startsVisibleSection
					z: 12
					Accessible.role: Accessible.Heading
					Accessible.name: roomDelegate.sectionVisualLabel + " · "
						+ navigationRail.roomCountForKind(roomDelegate.sectionKind)
					Accessible.ignored: navigationRail.accessibilitySuppressed || !visible
						|| !roomDelegate.accessibilityViewportVisible

					Rectangle {
						anchors.fill: parent
						color: Theme.rail
						Accessible.ignored: true
					}
					Label {
						objectName: roomDelegate.startsVisibleSection
							? "navigationSectionLabel_" + roomDelegate.sectionKind : ""
						anchors.left: parent.left
						anchors.leftMargin: 8
						anchors.right: parent.right
						anchors.rightMargin: 8
						anchors.bottom: parent.bottom
						anchors.bottomMargin: Theme.space1
						textFormat: Text.PlainText
						text: roomDelegate.sectionVisualLabel
						color: Theme.withAlpha(Theme.accent, 0.76)
						font.pixelSize: Theme.fontCaption
						font.bold: true
						elide: Text.ElideRight
						Accessible.ignored: true
					}
				}
				Rectangle {
					objectName: "navigationRoomSelectionAccent_" + stableId
					anchors.left: parent.left
					anchors.top: parent.top
					anchors.topMargin: roomDelegate.sectionHeaderHeight + 5
					width: 2
					height: Math.max(16, roomDelegate.roomRowHeight - 10)
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
					ModernIconButton {
						id: roomDisclosureButton
						hoverEnabled: !navigationRail.visualFixtureMode
						objectName: "navigationRoomDisclosure_" + stableId
						visible: kind === "voice" && roomDelegate.participantCount > 0
						Layout.preferredWidth: visible ? 22 : 0
						Layout.preferredHeight: 22
						iconName: "next"
						rotation: roomDelegate.roomExpanded ? 90 : 0
						dense: true
						activeFocusOnTab: false
						focusPolicy: Qt.ClickFocus
						z: 4
						Accessible.name: roomDelegate.roomExpanded
							? qsTr("Collapse %1").arg(title) : qsTr("Expand %1").arg(title)
						Accessible.description: roomDelegate.participantCount === 1
							? qsTr("1 participant")
							: qsTr("%1 participants").arg(roomDelegate.participantCount)
						ToolTip.visible: hovered
						ToolTip.text: Accessible.name
						Behavior on rotation { NumberAnimation { duration: Theme.motionFast } }
						onClicked: navigationRail.toggleRoomExpanded(scopeToken, payload)
						onActiveFocusChanged: if (activeFocus) roomDelegate.makeCurrent()
					}
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
								: title === qsTr("Activity") ? "activity"
								: sectionKind === "tool" ? "terminal" : "text-room"
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
							text: roomDelegate.roomDetailText
							color: roomDelegate.screenShareActive
								? navigationRail.toneColor(screenShare.statusTone, Theme.accent)
								: joined ? Theme.success : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					RowLayout {
						id: roomMeta
						Layout.alignment: Qt.AlignVCenter
						spacing: 4
						Rectangle {
							objectName: "navigationRoomParticipantCount_" + stableId
							visible: kind === "voice" && roomDelegate.participantCount > 0
							Layout.preferredWidth: Math.max(18, participantCountLabel.implicitWidth + 8)
							Layout.preferredHeight: 18
							radius: 9
							readonly property color countTone: roomDelegate.talkingParticipantCount > 0
								? Theme.success : Theme.textMuted
							color: Theme.withAlpha(countTone, roomDelegate.talkingParticipantCount > 0 ? 0.16 : 0.08)
							border.width: roomDelegate.talkingParticipantCount > 0 ? 1 : 0
							border.color: Theme.withAlpha(countTone, 0.34)
							Label {
								id: participantCountLabel
								objectName: "navigationRoomParticipantCountLabel_" + stableId
								anchors.centerIn: parent
								textFormat: Text.PlainText
								text: roomDelegate.talkingParticipantCount > 0
									? qsTr("%1/%2").arg(roomDelegate.talkingParticipantCount)
										.arg(roomDelegate.participantCount)
									: String(roomDelegate.participantCount)
								color: parent.countTone
								font.pixelSize: 8
								font.bold: roomDelegate.talkingParticipantCount > 0
								Accessible.ignored: true
							}
							ToolTip.visible: participantCountHover.hovered
							ToolTip.text: roomDelegate.talkingParticipantCount > 0
								? qsTr("%1 of %2 participants speaking")
									.arg(roomDelegate.talkingParticipantCount).arg(roomDelegate.participantCount)
								: (roomDelegate.participantCount === 1 ? qsTr("1 participant")
									: qsTr("%1 participants").arg(roomDelegate.participantCount))
							HoverHandler { id: participantCountHover }
						}
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
							visible: roomDelegate.screenShareActive
								&& String(screenShare.badgeLabel || "").length > 0
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
							hoverEnabled: !navigationRail.visualFixtureMode
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
							hoverEnabled: !navigationRail.visualFixtureMode
							objectName: "navigationRoomShare_" + stableId
							visible: kind === "voice" && joined && screenShareVisible
								&& String(screenShare.primaryActionId || "").length > 0
							enabled: screenShare.primaryEnabled !== false
							opacity: revealActions ? 1 : 0.82
							Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
							activeFocusOnTab: false
							focusPolicy: Qt.ClickFocus
							Layout.preferredWidth: 28
							Layout.preferredHeight: 24
							padding: 0
							z: 4
							text: String(screenShare.primaryLabel || qsTr("Screen share"))
							Accessible.name: text
							Accessible.description: String(screenShare.primaryHint
								|| screenShare.statusLabel || "")
							contentItem: ModernIcon {
								name: "screen-share"
								size: 13
								color: shareButton.enabled
									? navigationRail.toneColor(screenShare.primaryTone, Theme.accent) : Theme.textMuted
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
							hoverEnabled: !navigationRail.visualFixtureMode
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
					if (event.key === Qt.Key_Left && isParticipant) {
						const parentScope = String(payload.parentScopeToken || "")
						navigationRail.setRoomExpanded(parentScope, false)
						event.accepted = parentScope.length > 0
					} else if (event.key === Qt.Key_Left && kind === "voice"
							&& participantCount > 0 && roomExpanded) {
						navigationRail.setRoomExpanded(scopeToken, false)
						event.accepted = true
					} else if (event.key === Qt.Key_Right && kind === "voice"
							&& participantCount > 0 && !roomExpanded) {
						navigationRail.setRoomExpanded(scopeToken, true)
						event.accepted = true
					} else if (event.key === Qt.Key_Up) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.nextVisibleNavigationIndex(index, -1),
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_Down) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.nextVisibleNavigationIndex(index, 1),
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_Home) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.firstVisibleNavigationIndex(),
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_End) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.lastVisibleNavigationIndex(),
							Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_PageUp) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.stepVisibleNavigationIndex(index,
								-navigationRail.navigationPageStep()), Qt.OtherFocusReason)
					} else if (event.key === Qt.Key_PageDown) {
						event.accepted = navigationRail.focusNavigationIndex(
							navigationRail.stepVisibleNavigationIndex(index,
								navigationRail.navigationPageStep()), Qt.OtherFocusReason)
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
				readonly property bool revealParticipantActions: (!navigationRail.visualFixtureMode
					&& participantMouse.containsMouse)
					|| roomDelegate.rowFocusVisible
					|| (stableId.length > 0
						&& navigationRail.activeParticipantMenuKey === stableId)
					|| participantJoinButton.activeFocus || participantActionsButton.activeFocus
				readonly property string avatarSource: navigationRail.safeAvatarSource(
					payload.avatarUrl || sourceState.avatarUrl || "")
				readonly property string classicIconSource: navigationRail.classicParticipantIconSource(
					entryKind, talkState)
				readonly property bool selectedParticipant: selectionState.selectedUserSession !== undefined
					&& String(selectionState.selectedUserSession) === participantSession
				function focusRow() {
					roomDelegate.focusRow()
				}
				objectName: "navigationParticipant_" + participantObjectKey
				height: Theme.participantRowHeight
                radius: 8
				color: (!navigationRail.visualFixtureMode && participantMouse.pressed) ? Theme.accentSubtle
					   : selectedParticipant ? Theme.selected
					   : (!navigationRail.visualFixtureMode && participantMouse.containsMouse)
						? Theme.surfaceHover : "transparent"
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
						radius: navigationRail.classicUserIcons ? 0 : 13
						clip: !navigationRail.classicUserIcons
						color: navigationRail.classicUserIcons ? "transparent"
							: participantDelegate.isListener
							? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : Theme.strip
						border.width: navigationRail.classicUserIcons ? 0
							: participantDelegate.talking || participantDelegate.isListener
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
							visible: !navigationRail.classicUserIcons && status === Image.Ready
						}
						Image {
							id: participantClassicIcon
							objectName: "navigationParticipantClassicIcon_"
								+ participantDelegate.participantObjectKey
							anchors.fill: parent
							anchors.margins: 1
							source: participantDelegate.classicIconSource
							sourceSize: Qt.size(Math.max(1, Math.ceil(width * 2)),
								Math.max(1, Math.ceil(height * 2)))
							fillMode: Image.PreserveAspectFit
							visible: navigationRail.classicUserIcons
							Accessible.ignored: true
						}
						Label {
							objectName: "navigationParticipantAvatarFallback_"
								+ participantDelegate.participantObjectKey
							anchors.centerIn: parent
							visible: !navigationRail.classicUserIcons
								&& participantAvatarImage.status !== Image.Ready
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
							visible: !navigationRail.classicUserIcons
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
							hoverEnabled: !navigationRail.visualFixtureMode
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
							hoverEnabled: !navigationRail.visualFixtureMode
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

				ModalAccessibilityBarrier {
					id: roomAccessibilityBarrier
					objectName: "navigationRoomAccessibilityBarrier_" + roomDelegate.stableId
					// The rail-wide barrier owns modal suppression. This delegate-local
					// barrier only withdraws materialized cache rows outside the viewport,
					// avoiding competing temporary bindings during modal transitions.
					active: !navigationRail.accessibilitySuppressed
						&& !roomDelegate.accessibilityViewportVisible
					targets: [ roomDelegate ]
				}
			}
		}
        Rectangle {
			id: selfDock
			objectName: "navigationSelfDock"
            Layout.fillWidth: true
			Layout.preferredHeight: navigationRail.alignedFooterHeight
			color: selfIdentityButton.activeFocus || selfMuteButton.activeFocus || selfDeafenButton.activeFocus
				|| stonksButton.activeFocus || settingsButton.activeFocus
				? Theme.selfCardHover : Theme.selfCardBackground
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
				anchors.leftMargin: 14
				anchors.rightMargin: 10
				anchors.topMargin: 7
				anchors.bottomMargin: 7
				spacing: 10
				Item {
					id: selfIdentityButton
					objectName: "selfIdentityButton"
					Layout.fillWidth: true
					Layout.preferredHeight: Theme.avatarMedium
					activeFocusOnTab: true
					KeyNavigation.tab: selfMuteButton
					KeyNavigation.backtab: rooms
					Accessible.role: Accessible.Button
					Accessible.name: qsTr("Open profile for %1").arg(clientSession.selfName)
					Accessible.description: clientSession.selfStatusLabel
					Accessible.onPressAction: navigationRail.profileMenuRequested(
						selfIdentityButton.mapToItem(null, width / 2, 0))
					Keys.onReturnPressed: event => {
						navigationRail.profileMenuRequested(
							selfIdentityButton.mapToItem(null, width / 2, 0))
						event.accepted = true
					}
					Keys.onSpacePressed: event => {
						navigationRail.profileMenuRequested(
							selfIdentityButton.mapToItem(null, width / 2, 0))
						event.accepted = true
					}
					HoverHandler {
						id: selfIdentityHover
						cursorShape: Qt.PointingHandCursor
					}
					TapHandler {
						onTapped: navigationRail.profileMenuRequested(
							selfIdentityButton.mapToItem(null, width / 2, 0))
					}
					Rectangle {
						anchors.fill: parent
						anchors.margins: -4
						radius: Theme.innerRadius
						color: selfIdentityButton.activeFocus || selfIdentityHover.hovered
							? Theme.selfCardHover : "transparent"
						border.width: selfIdentityButton.activeFocus ? Theme.focusRingWidth : 0
						border.color: Theme.focus
						Accessible.ignored: true
					}
					RowLayout {
						anchors.fill: parent
						spacing: 10
						Rectangle {
							id: selfAvatar
							objectName: "selfAvatar"
							Layout.preferredWidth: Theme.avatarMedium
							Layout.preferredHeight: Theme.avatarMedium
							radius: Math.max(8, Theme.innerRadius - 2)
							clip: true
							color: Theme.accentSubtle
							border.width: 1
							border.color: selfAvatarImage.status === Image.Ready
								? Theme.quietBorder : Theme.withAlpha(Theme.accent, 0.58)
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
								anchors.rightMargin: -1
								anchors.bottomMargin: -1
								width: 9
								height: 9
								radius: width / 2
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
								font.pixelSize: Theme.fontBody
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "selfStatusLabel"
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: clientSession.selfStatusLabel
								color: clientSession.selfDeafened || clientSession.selfMuted ? Theme.danger
									: clientSession.connected ? Theme.success : Theme.microLabelText
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
					}
				}
				RowLayout {
					id: selfActions
					objectName: "selfActionGroup"
					Layout.alignment: Qt.AlignVCenter
					spacing: Theme.space1
				ModernIconButton {
					id: selfMuteButton
					hoverEnabled: !navigationRail.visualFixtureMode
					objectName: "selfMuteButton"
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.tab: selfDeafenButton
					KeyNavigation.backtab: selfIdentityButton
					iconName: clientSession.selfMuted ? "mute" : "microphone"
					dense: true
					selected: !!clientSession.selfMuted
					tone: clientSession.selfMuted ? "danger" : "neutral"
					opacity: selected || hovered || activeFocus ? 1 : 0.72
					Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
					Accessible.name: clientSession.selfMuted ? qsTr("Unmute microphone") : qsTr("Mute microphone")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: uiCommands.toggleSelfMute()
				}
				ModernIconButton {
					id: selfDeafenButton
					hoverEnabled: !navigationRail.visualFixtureMode
					objectName: "selfDeafenButton"
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.tab: stonksButton.visible ? stonksButton : settingsButton
					KeyNavigation.backtab: selfMuteButton
					iconName: "deafen"
					dense: true
					selected: !!clientSession.selfDeafened
					tone: clientSession.selfDeafened ? "danger" : "neutral"
					opacity: selected || hovered || activeFocus ? 1 : 0.72
					Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
					Accessible.name: clientSession.selfDeafened ? qsTr("Undeafen") : qsTr("Deafen")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: uiCommands.toggleSelfDeaf()
				}
				Rectangle {
					id: selfActionDivider
					objectName: "selfActionDivider"
					Layout.alignment: Qt.AlignVCenter
					Layout.preferredWidth: 1
					Layout.preferredHeight: 20
					Layout.leftMargin: Theme.space1
					Layout.rightMargin: Theme.space1
					color: Theme.selfCardBorder
					Accessible.ignored: true
				}
				ModernIconButton {
					id: stonksButton
					hoverEnabled: !navigationRail.visualFixtureMode
					objectName: "stonksPortfolioButton"
					visible: navigationRail.stonksEnabled
					activeFocusOnTab: visible
					focusPolicy: visible ? Qt.StrongFocus : Qt.NoFocus
					KeyNavigation.tab: settingsButton
					KeyNavigation.backtab: selfDeafenButton
					iconName: "activity"
					dense: true
					opacity: hovered || activeFocus ? 1 : 0.78
					Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
					Accessible.name: qsTr("Open Stonks portfolio")
					Accessible.description: qsTr("Portfolio, leaderboard, following, and market pins")
					ToolTip.visible: hovered
					ToolTip.text: qsTr("Stonks portfolio")
					onClicked: navigationRail.stonksRequested()
					Keys.onReturnPressed: event => {
						navigationRail.stonksRequested()
						event.accepted = true
					}
					Keys.onEnterPressed: event => {
						navigationRail.stonksRequested()
						event.accepted = true
					}
					Keys.onSpacePressed: event => {
						navigationRail.stonksRequested()
						event.accepted = true
					}
				}
				ModernIconButton {
					id: settingsButton
					hoverEnabled: !navigationRail.visualFixtureMode
					objectName: "settingsButton"
					activeFocusOnTab: true
					focusPolicy: Qt.StrongFocus
					KeyNavigation.tab: rooms
					KeyNavigation.backtab: stonksButton.visible ? stonksButton : selfDeafenButton
					iconName: "settings"
					dense: true
					enabled: navigationRail.settingsEnabled
					opacity: hovered || activeFocus ? 1 : 0.72
					Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
					Accessible.name: qsTr("Settings")
					Accessible.description: qsTr("Audio, appearance, notifications, plugins, and more")
					ToolTip.visible: hovered
					ToolTip.text: Accessible.name
					onClicked: navigationRail.settingsRequested()
					Keys.onReturnPressed: event => {
						navigationRail.settingsRequested()
						event.accepted = true
					}
					Keys.onEnterPressed: event => {
						navigationRail.settingsRequested()
						event.accepted = true
					}
					Keys.onSpacePressed: event => {
						navigationRail.settingsRequested()
						event.accepted = true
					}
				}
				}
            }
        }
		}
	Timer {
		id: accessibilityReassertionTimer
		objectName: "navigationAccessibilityReassertionTimer"
		interval: 16
		repeat: true
		triggeredOnStart: true
		running: navigationRail.accessibilitySuppressed
		onTriggered: navigationRail.refreshVirtualizedAccessibility()
	}

	ModalAccessibilityBarrier {
		id: railAccessibilityBarrier
		objectName: "navigationRailAccessibilityBarrier"
		active: navigationRail.accessibilitySuppressed
		// ListView delegates are created and reused below the hosted content item.
		// Keep the barrier next to that virtualized owner so modal activation also
		// catches rows that settle after the main shell's background pass.
		targets: [ railContentLayout ]
	}
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Rooms and participants")
}
