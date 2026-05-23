(function() {
	"use strict";

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let lastRenderedMessageCount = 0;
	let lastRenderedTailKey = "";
	let lastScopeToken = "";
	let openMenuId = null;
	let openMenuPinned = false;
	let railCollapsed = false;
	let compactViewport = false;
	let contextMenuState = null;
	let unreadDetachedMessages = 0;
	let selfMenuOpen = false;
	let openReactionPickerMessageId = null;
	let reactionPickerScrollClosePausedUntil = 0;
	let keepMessageListPinnedToBottom = true;
	let pendingBottomPinFrames = 0;
	let pendingBottomPinHandle = 0;
	let footerAlignmentFrame = 0;
	let railLayoutFrame = 0;
	let railSelectionRevealFrame = 0;
	let pendingActiveRailReveal = false;
	let snapshotRenderFrame = 0;
	let menuDismissTimer = 0;
	let appMenuOpen = false;
	let noteExpanded = false;
	let railNoteAutoCollapsed = false;
	let lastActiveRailToken = "";
	let lastMotdSignature = "";
	let messageListMutationObserver = null;
	let dragState = null;
	let imageViewerDragState = null;
	let imageViewerDragFrame = 0;
	let imageViewerResizeObserver = null;
	let footerAlignmentResizeObserver = null;
	let cachedServerLogElement = null;
	let cachedServerLogRevision = "";
	let liveSnapshot = {};
	let railActionIntent = null;
	let railJoinPriorityUntil = 0;
	let lastVoiceJoinRequest = { token: "", time: 0 };
	let pendingVoiceJoin = null;
	let pendingVoiceJoinTimer = 0;
	let messageRenderGeneration = 0;
	let activeMessageChunkRender = null;
	let pendingMessageUpdatePatches = [];
	let pendingScopeLoading = null;
	let pendingScopeLoadingTimer = 0;
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
	let modernDialogReturnFocus = null;
	let modernDialogSelectState = null;

	const imageViewerStorageKey = "mumble-modern-image-viewer";
	const imageViewerMinWidth = 280;
	const imageViewerMinHeight = 220;
	const imageViewerViewportMargin = 12;
	const compactRailBreakpointPx = 940;
	const reactionPickerScrollCloseGraceMs = 220;
	const railActionSuppressMs = 850;
	const railJoinPriorityMs = 900;
	const voiceJoinInputDedupeMs = 180;
	const voiceJoinFeedbackMs = 3200;
	const scopeLoadingFallbackMs = 2800;
	const messageRenderChunkGroupCount = 28;
	const messageRenderChunkBudgetMs = 7;
	const messageRenderLoadingThreshold = 36;
	const contextMenuViewportMargin = 8;
	const contextMenuAnchorGap = 4;
	const renderContainedModernDialogs = document.body
		&& document.body.dataset.modernDialogHost === "contained";

	const refs = {
		appShell: document.querySelector(".app-shell"),
		modernHeader: document.querySelector(".modern-header"),
		conversationHeader: document.querySelector(".conversation-header"),
		brandTitle: document.getElementById("brand-title"),
		brandSubtitle: document.getElementById("brand-subtitle"),
		menuBar: document.getElementById("menu-bar"),
		railToggleButton: document.getElementById("rail-toggle-button"),
		utilityRail: document.querySelector(".utility-rail"),
		utilityScroll: document.querySelector(".utility-scroll"),
		utilityRailBackdrop: document.getElementById("utility-rail-backdrop"),
		railCloseButton: document.getElementById("rail-close-button"),
		noteCard: document.querySelector(".note-card"),
		noteToggleButton: document.getElementById("note-toggle-button"),
		serverEyebrow: document.getElementById("server-eyebrow"),
		serverTitle: document.getElementById("server-title"),
		serverSubtitle: document.getElementById("server-subtitle"),
		layoutPill: document.getElementById("layout-pill"),
		connectionPill: document.getElementById("connection-pill"),
		compatPill: document.getElementById("compat-pill"),
		connectButton: document.getElementById("connect-button"),
		disconnectButton: document.getElementById("disconnect-button"),
		layoutSwitchButton: document.getElementById("layout-switch-button"),
		settingsButton: document.getElementById("settings-button"),
		muteButton: document.getElementById("mute-button"),
		deafButton: document.getElementById("deaf-button"),
		textRoomCount: document.getElementById("text-room-count"),
		voiceRoomCount: document.getElementById("voice-room-count"),
		textRoomList: document.getElementById("text-room-list"),
		voiceRoomList: document.getElementById("voice-room-list"),
		scopeTitle: document.getElementById("scope-title"),
		scopeDescription: document.getElementById("scope-description"),
		scopeBanner: document.getElementById("scope-banner"),
		conversationMeta: document.getElementById("conversation-meta"),
		voicePresenceStack: document.getElementById("voice-presence-stack"),
		screenShareButton: document.getElementById("screen-share-button"),
		screenShareCard: document.getElementById("screen-share-card"),
		screenShareCardStatus: document.getElementById("screen-share-card-status"),
		screenShareCardTitle: document.getElementById("screen-share-card-title"),
		screenShareCardSummary: document.getElementById("screen-share-card-summary"),
		screenShareCardNote: document.getElementById("screen-share-card-note"),
		screenShareCardActions: document.getElementById("screen-share-card-actions"),
		messageList: document.getElementById("message-list"),
		jumpLatestButton: document.getElementById("jump-latest-button"),
		composerForm: document.getElementById("composer-form"),
		composerShell: document.querySelector(".composer-shell"),
		composerReplyBar: document.getElementById("composer-reply-bar"),
		composerReplyLabel: document.getElementById("composer-reply-label"),
		composerReplySnippet: document.getElementById("composer-reply-snippet"),
		composerReplyCancelButton: document.getElementById("composer-reply-cancel-button"),
		composerInput: document.getElementById("composer-input"),
		composerHint: document.getElementById("composer-hint"),
		attachButton: document.getElementById("attach-button"),
		sendButton: document.getElementById("send-button"),
		loadOlderButton: document.getElementById("load-older-button"),
		markReadButton: document.getElementById("mark-read-button"),
		selfCard: document.getElementById("self-card"),
		selfCardSettingsButton: document.getElementById("self-card-settings-button"),
		selfAvatar: document.getElementById("self-avatar"),
		selfCopy: document.querySelector(".self-copy"),
		selfName: document.getElementById("self-name"),
		selfStatus: document.getElementById("self-status"),
		appMenu: document.getElementById("app-menu-popover"),
		selfMenu: document.getElementById("self-menu-popover"),
		contextMenu: document.getElementById("context-menu"),
		modernDialogLayer: document.getElementById("modern-dialog-layer"),
		modernDialogBackdrop: document.getElementById("modern-dialog-backdrop"),
		modernDialog: document.getElementById("modern-dialog"),
		modernDialogEyebrow: document.getElementById("modern-dialog-eyebrow"),
		modernDialogTitle: document.getElementById("modern-dialog-title"),
		modernDialogSubtitle: document.getElementById("modern-dialog-subtitle"),
		modernDialogCloseButton: document.getElementById("modern-dialog-close-button"),
		modernDialogBody: document.getElementById("modern-dialog-body"),
		modernDialogActions: document.getElementById("modern-dialog-actions"),
		imageViewerLayer: document.getElementById("image-viewer-layer"),
		imageViewerBackdrop: document.getElementById("image-viewer-backdrop"),
		imageViewerWindow: document.getElementById("image-viewer-window"),
		imageViewerHeader: document.getElementById("image-viewer-header"),
		imageViewerTitle: document.getElementById("image-viewer-title"),
		imageViewerCloseButton: document.getElementById("image-viewer-close-button"),
		imageViewerImage: document.getElementById("image-viewer-image")
	};
	const starterReactionEmoji = ["👍", "❤️", "😂", "😮", "😢", "🎉", "👀", "🔥"];
	let imageViewerState = loadImageViewerState();

	function viewportMatchesCompactRail() {
		return window.innerWidth <= compactRailBreakpointPx;
	}

	function applyRailPresentation() {
		const railOpen = !railCollapsed;
		refs.appShell.classList.toggle("is-compact-layout", compactViewport);
		refs.appShell.classList.toggle("rail-is-collapsed", railCollapsed);
		refs.railToggleButton.classList.toggle("is-active", railOpen);
		refs.railToggleButton.textContent = compactViewport && railOpen ? "Close" : "Rooms";
		refs.railToggleButton.title = compactViewport && railOpen ? "Close room browser" : "Open room browser";
		refs.railToggleButton.setAttribute("aria-label", compactViewport && railOpen ? "Close room browser" : "Open room browser");
		refs.railToggleButton.setAttribute("aria-expanded", railOpen ? "true" : "false");
		refs.utilityRail.setAttribute("aria-hidden", compactViewport && !railOpen ? "true" : "false");
		refs.utilityRailBackdrop.classList.toggle("hidden", !(compactViewport && railOpen));
		refs.utilityRailBackdrop.setAttribute("aria-hidden", compactViewport && railOpen ? "false" : "true");
	}

	function setRailCollapsed(nextCollapsed) {
		const normalized = !!nextCollapsed;
		if (railCollapsed === normalized) {
			applyRailPresentation();
			return;
		}

		railCollapsed = normalized;
		if (!railCollapsed) {
			hideContextMenu();
			hideSelfMenu();
			pendingActiveRailReveal = true;
			scheduleRailLayoutSync();
		}
		applyRailPresentation();
	}

	function syncCompactRailState(force) {
		const nextCompactViewport = viewportMatchesCompactRail();
		if (force || nextCompactViewport !== compactViewport) {
			compactViewport = nextCompactViewport;
			railCollapsed = compactViewport ? true : false;
		}
		applyRailPresentation();
	}

	function measureRailSection(section, listElement, itemCount) {
		if (!section || !listElement || section.classList.contains("hidden")) {
			return null;
		}

		const listRect = listElement.getBoundingClientRect();
		const sectionRect = section.getBoundingClientRect();
		const rootLabel = listElement.querySelector(".rail-root-label");
		const row = listElement.querySelector(".rail-row, .rail-empty");
		const rootHeight = rootLabel ? Math.ceil(rootLabel.getBoundingClientRect().height) : 0;
		const fallbackRowHeight = compactViewport ? 36 : 40;
		const rowHeight = row ? Math.max(Math.ceil(row.getBoundingClientRect().height), fallbackRowHeight) : fallbackRowHeight;
		const targetRows = itemCount > 0 ? Math.min(itemCount, (compactViewport || noteExpanded) ? 2 : 3) : 1;
		const listMinHeight = rootHeight + (rowHeight * Math.max(targetRows, 1)) + 8;
		const chromeHeight = Math.max(26, Math.ceil(sectionRect.height - listRect.height));
		return {
			section: section,
			listElement: listElement,
			listMinHeight: listMinHeight,
			sectionMinHeight: chromeHeight + listMinHeight
		};
	}

	function scheduleRailLayoutSync() {
		if (railLayoutFrame) {
			return;
		}

		railLayoutFrame = requestAnimationFrame(function() {
			railLayoutFrame = 0;
			syncRailLayout();
		});
	}

	function railOverflowAmount() {
		if (!refs.utilityScroll) {
			return 0;
		}

		return Math.max(0, refs.utilityScroll.scrollHeight - refs.utilityScroll.clientHeight);
	}

	function syncRailOverflowState() {
		if (!refs.appShell || !refs.utilityScroll) {
			return 0;
		}

		const overflow = railOverflowAmount();
		refs.appShell.classList.toggle("rail-has-overflow", overflow > 1);
		refs.appShell.classList.toggle("rail-note-auto-compacted", railNoteAutoCollapsed);
		return overflow;
	}

	function scheduleActiveRailRowReveal() {
		if (railSelectionRevealFrame) {
			return;
		}

		railSelectionRevealFrame = requestAnimationFrame(function() {
			railSelectionRevealFrame = 0;
			revealActiveRailRow();
		});
	}

	function revealActiveRailRow() {
		if (!refs.utilityScroll) {
			return;
		}

		const activeRow = activeRailRow();
		if (!activeRow) {
			return;
		}

		const railRect = refs.utilityScroll.getBoundingClientRect();
		const rowRect = activeRow.getBoundingClientRect();
		const topGuard = 12;
		const bottomGuard = 16;
		if (rowRect.top < railRect.top + topGuard || rowRect.bottom > railRect.bottom - bottomGuard) {
			activeRow.scrollIntoView({ block: "nearest", inline: "nearest" });
		}
	}

	function activeRailRow() {
		if (!refs.utilityScroll) {
			return null;
		}

		return refs.utilityScroll.querySelector(".rail-row.is-selected")
			|| refs.utilityScroll.querySelector(".rail-row.is-joined");
	}

	function activeRailToken() {
		const row = activeRailRow();
		return row ? String(row.dataset.scopeToken || row.dataset.roomLabel || "") : "";
	}

	function syncRailLayout() {
		if (!refs.appShell || !refs.utilityScroll || !refs.noteCard || !refs.serverSubtitle
			|| !refs.voiceRoomList || !refs.textRoomList) {
			return;
		}

		const utilityHeight = Math.ceil(refs.utilityScroll.clientHeight || 0);
		if (utilityHeight <= 0) {
			return;
		}

		const voiceSection = refs.voiceRoomList.closest(".room-card-block");
		const textSection = refs.textRoomList.closest(".room-card-block");
		const sections = [
			measureRailSection(voiceSection, refs.voiceRoomList, Number(refs.voiceRoomCount.textContent || 0)),
			measureRailSection(textSection, refs.textRoomList, Number(refs.textRoomCount.textContent || 0))
		].filter(function(entry) {
			return !!entry;
		});

		sections.forEach(function(entry) {
			entry.listElement.style.minHeight = "";
			entry.section.style.minHeight = "";
			entry.section.style.flexBasis = "";
		});

		if (refs.noteCard.classList.contains("hidden")) {
			refs.noteCard.style.maxHeight = "";
			refs.serverSubtitle.style.maxHeight = "";
			railNoteAutoCollapsed = false;
			syncRailOverflowState();
			if (pendingActiveRailReveal) {
				pendingActiveRailReveal = false;
				scheduleActiveRailRowReveal();
			}
			return;
		}

		const noteCardRect = refs.noteCard.getBoundingClientRect();
		const noteBodyRect = refs.serverSubtitle.getBoundingClientRect();
		const noteChromeHeight = Math.max(52, Math.ceil(noteCardRect.height - noteBodyRect.height));
		const minimumNoteBodyHeight = noteExpanded ? (compactViewport ? 92 : 124) : (compactViewport ? 52 : 72);
		const sectionGapBudget = sections.length > 0 ? 10 : 0;
		const minimumSectionBudget = sections.reduce(function(total, entry) {
			return total + entry.sectionMinHeight;
		}, 0);
		const noteBudget = utilityHeight - minimumSectionBudget - sectionGapBudget;
		const desiredNoteMaxHeight = noteExpanded
			? Math.round(Math.max(compactViewport ? 208 : 260,
				Math.min(compactViewport ? 272 : 360, utilityHeight * (compactViewport ? 0.44 : 0.5))))
			: (compactViewport ? 160 : 220);
		const minimumNoteMaxHeight = noteChromeHeight + minimumNoteBodyHeight;
		const availableNoteBudget = Math.max(minimumNoteMaxHeight, noteBudget);
		const clampedNoteMaxHeight = Math.max(minimumNoteMaxHeight, Math.min(desiredNoteMaxHeight, availableNoteBudget));
		const noteBodyMaxHeight = Math.max(minimumNoteBodyHeight, clampedNoteMaxHeight - noteChromeHeight);

		refs.noteCard.style.maxHeight = Math.round(clampedNoteMaxHeight) + "px";
		refs.serverSubtitle.style.maxHeight = Math.round(noteBodyMaxHeight) + "px";

		syncRailOverflowState();

		if (pendingActiveRailReveal) {
			pendingActiveRailReveal = false;
			scheduleActiveRailRowReveal();
		}
	}

	function normalizeWheelDelta(event) {
		let deltaY = Number(event.deltaY || 0);
		if (!deltaY) {
			return 0;
		}

		if (event.deltaMode === 1) {
			deltaY *= 16;
		} else if (event.deltaMode === 2) {
			deltaY *= Math.max(1, refs.utilityScroll ? refs.utilityScroll.clientHeight : window.innerHeight);
		}
		return deltaY;
	}

	function canScrollElement(element, deltaY) {
		if (!element || Math.abs(deltaY) < 0.01) {
			return false;
		}

		const maxScrollTop = Math.max(0, element.scrollHeight - element.clientHeight);
		if (maxScrollTop <= 1) {
			return false;
		}

		return deltaY < 0 ? element.scrollTop > 0 : element.scrollTop < maxScrollTop - 1;
	}

	function findNestedRailScroller(target, deltaY) {
		let element = target && target.nodeType === Node.ELEMENT_NODE ? target : target && target.parentElement;
		while (element && element !== refs.utilityScroll && element !== document.body) {
			const style = window.getComputedStyle(element);
			if ((style.overflowY === "auto" || style.overflowY === "scroll") && canScrollElement(element, deltaY)) {
				return element;
			}
			element = element.parentElement;
		}
		return null;
	}

	function handleUtilityWheel(event) {
		if (!refs.utilityScroll || event.defaultPrevented || event.ctrlKey || event.metaKey || event.altKey) {
			return;
		}

		const deltaY = normalizeWheelDelta(event);
		if (!deltaY || Math.abs(Number(event.deltaX || 0)) > Math.abs(deltaY)) {
			return;
		}

		if (findNestedRailScroller(event.target, deltaY)) {
			return;
		}

		if (!canScrollElement(refs.utilityScroll, deltaY)) {
			return;
		}

		event.preventDefault();
		refs.utilityScroll.scrollTop = Math.max(0, Math.min(
			refs.utilityScroll.scrollTop + deltaY,
			refs.utilityScroll.scrollHeight - refs.utilityScroll.clientHeight
		));
		hideContextMenu();
		hideSelfMenu();
		syncRailOverflowState();
	}

	function dismissCompactRailAfterAction() {
		if (!compactViewport || railCollapsed) {
			return;
		}
		setRailCollapsed(true);
	}

	function stopRoomActionEvent(event) {
		if (!event) {
			return;
		}

		event.preventDefault();
		event.stopPropagation();
		if (typeof event.stopImmediatePropagation === "function") {
			event.stopImmediatePropagation();
		}
	}

	function markRailActionIntent(scopeToken) {
		if (!scopeToken) {
			return;
		}

		railActionIntent = {
			scopeToken: String(scopeToken),
			expiresAt: monotonicNow() + railActionSuppressMs
		};
	}

	function shouldSuppressRailSelect(scopeToken) {
		if (!railActionIntent) {
			return false;
		}

		if (monotonicNow() > railActionIntent.expiresAt) {
			railActionIntent = null;
			return false;
		}

		return !scopeToken || railActionIntent.scopeToken === String(scopeToken);
	}

	function markRailJoinPriority() {
		railJoinPriorityUntil = monotonicNow() + railJoinPriorityMs;
	}

	function railJoinPriorityActive() {
		if (!railJoinPriorityUntil) {
			return false;
		}
		if (monotonicNow() > railJoinPriorityUntil) {
			railJoinPriorityUntil = 0;
			return false;
		}
		return true;
	}

	function clearPendingVoiceJoinTimer() {
		if (!pendingVoiceJoinTimer) {
			return;
		}

		clearTimeout(pendingVoiceJoinTimer);
		pendingVoiceJoinTimer = 0;
	}

	function pendingVoiceJoinToken() {
		if (!pendingVoiceJoin || !pendingVoiceJoin.scopeToken) {
			return "";
		}

		if (monotonicNow() > pendingVoiceJoin.expiresAt) {
			pendingVoiceJoin = null;
			clearPendingVoiceJoinTimer();
			return "";
		}

		return pendingVoiceJoin.scopeToken;
	}

	function syncPendingVoiceJoinRows() {
		if (!refs.voiceRoomList) {
			return;
		}

		const token = pendingVoiceJoinToken();
		refs.voiceRoomList.querySelectorAll(".rail-row").forEach(function(row) {
			const joining = !!token && String(row.dataset.scopeToken || "") === token;
			row.classList.toggle("is-joining", joining);
			if (joining) {
				row.dataset.canJoin = "false";
			}
			const joinButton = row.querySelector(".room-join-action");
			if (!joinButton) {
				return;
			}
			const wasLoading = joinButton.classList.contains("is-loading");
			joinButton.classList.toggle("is-loading", joining);
			if (joining) {
				joinButton.textContent = "Joining";
				joinButton.disabled = true;
			} else if (wasLoading) {
				const joined = row.classList.contains("is-joined");
				joinButton.textContent = joined ? "Live" : "Join";
				joinButton.disabled = joined;
				row.dataset.canJoin = joined ? "false" : "true";
			}
		});
	}

	function clearPendingVoiceJoinFeedback() {
		pendingVoiceJoin = null;
		clearPendingVoiceJoinTimer();
		syncPendingVoiceJoinRows();
	}

	function reconcilePendingVoiceJoin(snapshot) {
		const token = pendingVoiceJoinToken();
		if (!token) {
			syncPendingVoiceJoinRows();
			return;
		}

		const joined = (snapshot && snapshot.voiceRooms || []).some(function(room) {
			return room && room.joined && String(room.token || "") === token;
		});
		if (joined) {
			clearPendingVoiceJoinFeedback();
			return;
		}

		syncPendingVoiceJoinRows();
	}

	function beginVoiceJoinFeedback(scopeToken) {
		const token = String(scopeToken || "");
		if (!token) {
			return;
		}

		pendingVoiceJoin = {
			scopeToken: token,
			expiresAt: monotonicNow() + voiceJoinFeedbackMs
		};
		clearPendingVoiceJoinTimer();
		pendingVoiceJoinTimer = setTimeout(function() {
			clearPendingVoiceJoinFeedback();
		}, voiceJoinFeedbackMs);
		beginScopeLoading(token, { force: true });
		syncPendingVoiceJoinRows();
	}

	function isRoomEmbeddedActionTarget(target, row) {
		if (!target || !row || typeof target.closest !== "function") {
			return false;
		}

		const action = target.closest("button, a, input, textarea, select, .mini-action, .room-action-button");
		return !!action && row.contains(action);
	}

	function isRoomJoinActionTarget(target, row) {
		if (!target || !row || typeof target.closest !== "function") {
			return false;
		}

		const action = target.closest(".room-join-action");
		return !!action && row.contains(action);
	}

	function isRoomNonJoinActionTarget(target, row) {
		if (!target || !row || typeof target.closest !== "function") {
			return false;
		}

		const action = target.closest(".room-action-button, .room-share-action");
		return !!action && row.contains(action);
	}

	function isRoomJoinHotZone(event, row) {
		if (!event || !row || typeof event.clientX !== "number" || typeof row.getBoundingClientRect !== "function") {
			return false;
		}

		const rowRect = row.getBoundingClientRect();
		if (event.clientX < rowRect.left || event.clientX > rowRect.right
				|| event.clientY < rowRect.top || event.clientY > rowRect.bottom) {
			return false;
		}

		const meta = row.querySelector(".rail-row-meta");
		const metaRect = meta && typeof meta.getBoundingClientRect === "function"
			? meta.getBoundingClientRect()
			: null;
		const metaStart = metaRect && metaRect.width > 0 ? metaRect.left - 10 : rowRect.right - 76;
		const fallbackStart = rowRect.right - 82;
		return event.clientX >= Math.min(metaStart, fallbackStart);
	}

	function isRoomJoinIntent(event, row) {
		if (!event || !row || isRoomNonJoinActionTarget(event.target, row)) {
			return false;
		}

		return isRoomJoinActionTarget(event.target, row)
			|| isRoomJoinHotZone(event, row)
			|| (railJoinPriorityActive() && row.dataset.roomType === "voice" && row.dataset.canJoin === "true");
	}

	function requestVoiceJoin(scopeToken) {
		const token = String(scopeToken || "");
		if (!token) {
			return false;
		}

		const now = monotonicNow();
		markRailJoinPriority();
		if (lastVoiceJoinRequest.token === token && now - lastVoiceJoinRequest.time < voiceJoinInputDedupeMs) {
			return false;
		}

		lastVoiceJoinRequest = { token, time: now };
		beginVoiceJoinFeedback(token);
		if (!notifyBridge("joinVoiceChannel", token)) {
			clearPendingVoiceJoinFeedback();
			clearChatLoadingIndicator();
			scheduleSnapshotRender();
			return false;
		}
		return true;
	}

	function handleJoinButtonActivation(room, event) {
		stopRoomActionEvent(event);
		if (!room || room.joined) {
			return;
		}

		markRailActionIntent(room.token);
		requestVoiceJoin(room.token);
		dismissCompactRailAfterAction();
	}

	function handleRailVoiceJoinCapture(event) {
		if (!event || event.defaultPrevented) {
			return;
		}
		if (event.type === "mousedown" && window.PointerEvent) {
			return;
		}
		if ((event.type === "pointerdown" || event.type === "mousedown")
				&& event.button !== undefined && event.button !== 0) {
			return;
		}
		if (event.type === "pointerdown" && event.isPrimary === false) {
			return;
		}
		if (!refs.voiceRoomList || !event.target || typeof event.target.closest !== "function") {
			return;
		}

		const row = event.target.closest(".rail-row");
		if (!row || !refs.voiceRoomList.contains(row)
				|| row.dataset.roomType !== "voice" || row.dataset.canJoin !== "true") {
			return;
		}
		if (!isRoomJoinIntent(event, row)) {
			return;
		}

		stopRoomActionEvent(event);
		markRailActionIntent(row.dataset.scopeToken);
		requestVoiceJoin(row.dataset.scopeToken);
		dismissCompactRailAfterAction();
	}

	function notifyBridge(method) {
		if (!modernBridge || typeof modernBridge[method] !== "function") {
			return false;
		}

		const args = Array.prototype.slice.call(arguments, 1);
		try {
			modernBridge[method].apply(modernBridge, args);
			return true;
		} catch (error) {
			console.warn("Modern bridge call failed:", method, error);
			return false;
		}
	}

	async function ensureBridge() {
		if (!window.qt || !window.qt.webChannelTransport) {
			return;
		}

		if (modernBridge) {
			return;
		}

		async function bindBridge() {
			return new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						modernBridge = channel.objects.modernBridge || null;
						if (modernBridge) {
							if (modernBridge.snapshotChanged && typeof modernBridge.snapshotChanged.connect === "function") {
								modernBridge.snapshotChanged.connect(syncSnapshot);
							}
							if (modernBridge.modernPatchChanged
									&& typeof modernBridge.modernPatchChanged.connect === "function") {
								modernBridge.modernPatchChanged.connect(syncSnapshotPatch);
							}
							if (modernBridge.participantTalkStateChanged
									&& typeof modernBridge.participantTalkStateChanged.connect === "function") {
								modernBridge.participantTalkStateChanged.connect(syncParticipantTalkState);
							}
							if (modernBridge.modernDialogStateChanged
									&& typeof modernBridge.modernDialogStateChanged.connect === "function") {
								modernBridge.modernDialogStateChanged.connect(syncModernDialogState);
							}
							syncModernDialogState(modernBridge.modernDialogState || { open: false });
							notifyBridge("ready");
							syncSnapshot();
						}
						resolve();
					});
				} catch (error) {
					console.warn("Modern bridge initialization failed:", error);
					resolve();
				}
			});
		}

		if (window.QWebChannel) {
			await bindBridge();
			return;
		}

		if (!bridgeLoadPromise) {
			bridgeLoadPromise = new Promise(function(resolve) {
				const script = document.createElement("script");
				script.src = "qrc:///qtwebchannel/qwebchannel.js";
				script.async = true;
				script.onload = function() {
					bindBridge().then(resolve);
				};
				script.onerror = function() {
					console.warn("Unable to load qwebchannel.js for the modern layout.");
					resolve();
				};
				document.head.appendChild(script);
			});
		}

		await bridgeLoadPromise;
	}

	function getSnapshot() {
		if (modernBridge && (!liveSnapshot || !Object.keys(liveSnapshot).length)) {
			liveSnapshot = modernBridge.snapshot || {};
		}
		return liveSnapshot || {};
	}

	function replaceChildrenWith(element, fragment) {
		if (!element) {
			return;
		}
		if (typeof element.replaceChildren === "function") {
			element.replaceChildren(fragment);
			return;
		}

		element.innerHTML = "";
		if (fragment) {
			element.appendChild(fragment);
		}
	}

	function createChatLoadingIndicator() {
		const indicator = document.createElement("div");
		indicator.className = "chat-loading-indicator";
		indicator.setAttribute("role", "status");
		indicator.setAttribute("aria-label", "Loading");

		const spinner = document.createElement("span");
		spinner.className = "chat-loading-spinner";
		spinner.setAttribute("aria-hidden", "true");
		indicator.appendChild(spinner);
		return indicator;
	}

	function showChatLoadingIndicator() {
		if (!refs.messageList) {
			return;
		}
		if (refs.messageList.classList.contains("is-chat-loading")
				&& refs.messageList.querySelector(".chat-loading-spinner")) {
			return;
		}

		cancelActiveMessageChunkRender("scope-loading");
		pendingMessageUpdatePatches = [];
		const fragment = document.createDocumentFragment();
		fragment.appendChild(createChatLoadingIndicator());
		refs.messageList.classList.add("is-chat-loading");
		replaceChildrenWith(refs.messageList, fragment);
	}

	function clearPendingScopeLoadingTimer() {
		if (!pendingScopeLoadingTimer) {
			return;
		}

		clearTimeout(pendingScopeLoadingTimer);
		pendingScopeLoadingTimer = 0;
	}

	function clearChatLoadingIndicator() {
		clearPendingScopeLoadingTimer();
		pendingScopeLoading = null;
		if (refs.messageList) {
			refs.messageList.classList.remove("is-chat-loading");
		}
	}

	function resolveScopeLoadingFallback() {
		pendingScopeLoadingTimer = 0;
		if (!pendingScopeLoading || !pendingScopeLoading.scopeToken) {
			return;
		}

		const token = pendingScopeLoading.scopeToken;
		const snapshot = getSnapshot();
		const activeToken = String((snapshot.activeScope || {}).scopeToken || "");
		clearChatLoadingIndicator();
		if (activeToken && activeToken === token && String(lastScopeToken || "") !== token) {
			snapshot.messages = [];
			cachedServerLogElement = null;
			cachedServerLogRevision = "";
			renderMessages(snapshot, { forceSync: true, resolvePendingScopeLoading: true });
			return;
		}

		scheduleSnapshotRender();
	}

	function scheduleScopeLoadingFallback() {
		clearPendingScopeLoadingTimer();
		pendingScopeLoadingTimer = setTimeout(resolveScopeLoadingFallback, scopeLoadingFallbackMs);
	}

	function beginScopeLoading(scopeToken, options) {
		const token = String(scopeToken || "");
		if (!token) {
			return false;
		}

		const force = !!(options && options.force);
		const useFallback = !(options && options.fallback === false);
		const activeToken = String((getSnapshot().activeScope || {}).scopeToken || "");
		if (!force && token === activeToken && (!pendingScopeLoading || pendingScopeLoading.scopeToken !== token)) {
			return false;
		}

		pendingScopeLoading = { scopeToken: token };
		showChatLoadingIndicator();
		if (useFallback) {
			scheduleScopeLoadingFallback();
		} else {
			clearPendingScopeLoadingTimer();
		}
		return true;
	}

	function pendingScopeLoadingBlocksRender(scopeToken, renderOptions) {
		if (!pendingScopeLoading || !pendingScopeLoading.scopeToken) {
			return false;
		}

		const token = String(scopeToken || "");
		if (token === pendingScopeLoading.scopeToken && renderOptions && renderOptions.resolvePendingScopeLoading) {
			clearChatLoadingIndicator();
			return false;
		}

		showChatLoadingIndicator();
		return true;
	}

	function scopeHistoryLoading(scope) {
		if (!scope || typeof scope !== "object") {
			return false;
		}

		const loadingState = String(scope.loadingState || "").toLowerCase();
		return !!scope.loading || loadingState === "initial" || loadingState === "refreshing" || loadingState === "older";
	}

	function shouldShowScopeLoading(scope, messages) {
		if (!scopeHistoryLoading(scope) || (messages || []).length > 0) {
			return false;
		}
		return !scope.serverLogRevision && !Object.prototype.hasOwnProperty.call(scope, "serverLogHtml");
	}

	function selectRoomScope(scopeToken) {
		const token = String(scopeToken || "");
		if (!token) {
			return;
		}

		const canSelectScope = modernBridge && typeof modernBridge.selectScope === "function";
		const showingLoading = canSelectScope ? beginScopeLoading(token) : false;
		if (!notifyBridge("selectScope", token) && showingLoading) {
			clearChatLoadingIndicator();
			scheduleSnapshotRender();
		}
	}

	function scheduleSnapshotRender() {
		if (snapshotRenderFrame) {
			return;
		}

		snapshotRenderFrame = requestAnimationFrame(function() {
			snapshotRenderFrame = 0;
			syncCompactRailState(false);
			render(getSnapshot());
		});
	}

	function participantSessionMatches(person, session) {
		if (!person || !session) {
			return false;
		}

		return String(person.session || person.ownerSession || "") === session;
	}

	function applyParticipantTalkState(person, state, session) {
		if (!participantSessionMatches(person, session)) {
			return false;
		}

		let changed = false;
		const nextTalkState = String(state.talkState || "passive");
		const nextTalkLabel = String(state.talkLabel || "");
		const nextTalkTone = String(state.talkTone || "");
		const nextTalking = !!state.talking;
		changed = person.talkState !== nextTalkState || changed;
		changed = person.talkLabel !== nextTalkLabel || changed;
		changed = person.talkTone !== nextTalkTone || changed;
		changed = !!person.talking !== nextTalking || changed;
		person.talkState = String(state.talkState || "passive");
		person.talkLabel = String(state.talkLabel || "");
		person.talkTone = String(state.talkTone || "");
		person.talking = !!state.talking;
		if (person.entryKind === "listener") {
			return changed;
		}
		if (Array.isArray(state.badges)) {
			changed = JSON.stringify(person.badges || []) !== JSON.stringify(state.badges) || changed;
			person.badges = state.badges;
		}
		if (Array.isArray(state.statuses)) {
			changed = JSON.stringify(person.statuses || []) !== JSON.stringify(state.statuses) || changed;
			person.statuses = state.statuses;
		}
		return changed;
	}

	function applyParticipantTalkStateList(people, state, session) {
		let changed = false;
		(people || []).forEach(function(person) {
			changed = applyParticipantTalkState(person, state, session) || changed;
		});
		return changed;
	}

	function syncParticipantTalkState(state) {
		const session = String(state && state.session || "");
		if (!session) {
			return;
		}

		const snapshot = getSnapshot();
		let changed = applyParticipantTalkStateList(snapshot.voicePresence, state, session);
		changed = applyParticipantTalkStateList(snapshot.participants, state, session) || changed;
		(snapshot.voiceRooms || []).forEach(function(room) {
			changed = applyParticipantTalkStateList(room && room.participants, state, session) || changed;
		});

		if (changed) {
			renderPresencePatch(snapshot);
		}
	}

	function plainTextFromHtml(html) {
		const template = document.createElement("template");
		template.innerHTML = String(html || "");
		return String(template.content.textContent || "").replace(/\s+/g, " ").trim();
	}

	function escapedMultilineText(value) {
		return escapeHtml(value).replace(/\n/g, "<br>");
	}

	function escapeHtml(value) {
		return String(value || "")
			.replace(/&/g, "&amp;")
			.replace(/</g, "&lt;")
			.replace(/>/g, "&gt;")
			.replace(/\"/g, "&quot;");
	}

	function initialsFor(label) {
		const parts = String(label || "").trim().split(/\s+/).filter(Boolean);
		if (!parts.length) {
			return "?";
		}
		if (parts.length === 1) {
			return parts[0].slice(0, 1).toUpperCase();
		}
		return (parts[0].slice(0, 1) + parts[1].slice(0, 1)).toUpperCase();
	}

	function hueForLabel(label, own) {
		if (own) {
			return 173;
		}

		let hash = 0;
		const source = String(label || "");
		for (let index = 0; index < source.length; index += 1) {
			hash = ((hash << 5) - hash) + source.charCodeAt(index);
			hash |= 0;
		}

		return Math.abs(hash) % 360;
	}

	function styleAvatar(element, label, own, avatarUrl) {
		const hue = hueForLabel(label, own);
		element.style.setProperty("--avatar-hue", String(hue));
		if (avatarUrl) {
			element.classList.add("has-image");
			element.style.backgroundImage = "url(\"" + String(avatarUrl).replace(/"/g, "%22") + "\")";
			element.textContent = "";
			return;
		}

		element.classList.remove("has-image");
		element.style.backgroundImage = "";
		element.textContent = initialsFor(label);
	}

	function applyStatePill(element, label, tone) {
		element.textContent = label;
		element.className = "state-pill";
		if (tone) {
			element.classList.add("is-" + tone);
		}
	}

	function kindChipText(kindLabel) {
		switch (String(kindLabel || "").toLowerCase()) {
			case "activity":
				return "LOG";
			case "voice room":
				return "VC";
			case "text room":
				return "TXT";
			case "direct message":
				return "DM";
			default:
				return "TXT";
		}
	}

	function eventElementTarget(event) {
		if (!event || !event.target) {
			return null;
		}

		if (event.target.nodeType === Node.ELEMENT_NODE) {
			return event.target;
		}

		return event.target.parentElement || null;
	}

	function anchorFromEvent(event) {
		const target = eventElementTarget(event);
		if (!target || typeof target.closest !== "function") {
			return null;
		}

		return target.closest("a[href]");
	}

	function imageFromEvent(event) {
		const target = eventElementTarget(event);
		if (!target || typeof target.closest !== "function") {
			return null;
		}

		const image = target.closest("img");
		if (!image || !refs.messageList || !refs.messageList.contains(image)) {
			return null;
		}

		return image;
	}

	function isInlineDataImageOpenHref(href) {
		return /^mumble-chat:\/\/inline-data-image\//i.test(String(href || "").trim());
	}

	function handleAnchorActivation(event) {
		const image = imageFromEvent(event);
		const imageAnchor = image ? anchorFromEvent(event) : null;
		if (image && imageAnchor) {
			const imageHref = String(imageAnchor.getAttribute("href") || imageAnchor.href || "").trim();
			event.preventDefault();
			event.stopPropagation();
			hideContextMenu();
			hideSelfMenu();
			if (isInlineDataImageOpenHref(imageHref) && modernBridge && typeof modernBridge.activateLink === "function") {
				notifyBridge("activateLink", imageHref);
				return;
			}
			openImageViewerFromElement(image);
			return;
		}

		if (!modernBridge || typeof modernBridge.activateLink !== "function") {
			return;
		}

		const anchor = anchorFromEvent(event);
		if (!anchor) {
			return;
		}

		const href = String(anchor.href || anchor.getAttribute("href") || "").trim();
		if (!href) {
			return;
		}

		event.preventDefault();
		event.stopPropagation();
		hideContextMenu();
		hideSelfMenu();
		notifyBridge("activateLink", href);
	}

	function defaultImageViewerState() {
		const width = Math.min(560, Math.max(imageViewerMinWidth, window.innerWidth - 64));
		const height = Math.min(400, Math.max(imageViewerMinHeight, window.innerHeight - 88));
		return {
			width: Math.round(width),
			height: Math.round(height),
			left: Math.round((window.innerWidth - width) / 2),
			top: Math.max(20, Math.round((window.innerHeight - height) / 2) - 24)
		};
	}

	function clampImageViewerState(state) {
		const fallback = defaultImageViewerState();
		const requestedWidth = Number(state && state.width);
		const requestedHeight = Number(state && state.height);
		const requestedLeft = Number(state && state.left);
		const requestedTop = Number(state && state.top);
		const maxWidth = Math.max(220, window.innerWidth - (imageViewerViewportMargin * 2));
		const maxHeight = Math.max(180, window.innerHeight - (imageViewerViewportMargin * 2));
		const minWidth = Math.min(imageViewerMinWidth, maxWidth);
		const minHeight = Math.min(imageViewerMinHeight, maxHeight);
		const width = Math.round(Math.min(maxWidth, Math.max(minWidth, isFinite(requestedWidth) ? requestedWidth : fallback.width)));
		const height = Math.round(Math.min(maxHeight, Math.max(minHeight, isFinite(requestedHeight) ? requestedHeight : fallback.height)));
		const maxLeft = Math.max(imageViewerViewportMargin, window.innerWidth - width - imageViewerViewportMargin);
		const maxTop = Math.max(imageViewerViewportMargin, window.innerHeight - height - imageViewerViewportMargin);
		return {
			width: width,
			height: height,
			left: Math.round(Math.min(maxLeft, Math.max(imageViewerViewportMargin, isFinite(requestedLeft) ? requestedLeft : fallback.left))),
			top: Math.round(Math.min(maxTop, Math.max(imageViewerViewportMargin, isFinite(requestedTop) ? requestedTop : fallback.top)))
		};
	}

	function loadImageViewerState() {
		try {
			const raw = window.localStorage ? window.localStorage.getItem(imageViewerStorageKey) : "";
			if (!raw) {
				return clampImageViewerState(defaultImageViewerState());
			}
			return clampImageViewerState(JSON.parse(raw));
		} catch (error) {
			return clampImageViewerState(defaultImageViewerState());
		}
	}

	function persistImageViewerState() {
		if (!imageViewerState) {
			return;
		}

		try {
			if (window.localStorage) {
				window.localStorage.setItem(imageViewerStorageKey, JSON.stringify(imageViewerState));
			}
		} catch (error) {}
	}

	function applyImageViewerGeometry() {
		if (!refs.imageViewerWindow || !imageViewerState) {
			return;
		}

		imageViewerState = clampImageViewerState(imageViewerState);
		refs.imageViewerWindow.style.width = imageViewerState.width + "px";
		refs.imageViewerWindow.style.height = imageViewerState.height + "px";
		refs.imageViewerWindow.style.left = imageViewerState.left + "px";
		refs.imageViewerWindow.style.top = imageViewerState.top + "px";
		if (!imageViewerDragState) {
			refs.imageViewerWindow.style.transform = "";
		}
	}

	function clearImageViewerDragFrame() {
		if (!imageViewerDragFrame) {
			return;
		}

		cancelAnimationFrame(imageViewerDragFrame);
		imageViewerDragFrame = 0;
	}

	function applyImageViewerDragPreview() {
		imageViewerDragFrame = 0;
		if (!refs.imageViewerWindow || !imageViewerDragState) {
			return;
		}

		const offsetLeft = Math.round(imageViewerDragState.currentLeft - imageViewerDragState.startLeft);
		const offsetTop = Math.round(imageViewerDragState.currentTop - imageViewerDragState.startTop);
		refs.imageViewerWindow.style.transform = "translate3d(" + offsetLeft + "px, " + offsetTop + "px, 0)";
	}

	function scheduleImageViewerDragPreview() {
		if (imageViewerDragFrame) {
			return;
		}

		imageViewerDragFrame = requestAnimationFrame(applyImageViewerDragPreview);
	}

	function stopObservingImageViewerSize() {
		if (!imageViewerResizeObserver) {
			return;
		}

		imageViewerResizeObserver.disconnect();
		imageViewerResizeObserver = null;
	}

	function observeImageViewerSize() {
		if (!refs.imageViewerWindow || typeof ResizeObserver !== "function") {
			return;
		}

		stopObservingImageViewerSize();
		imageViewerResizeObserver = new ResizeObserver(function() {
			const rect = refs.imageViewerWindow.getBoundingClientRect();
			const nextState = clampImageViewerState({
				width: rect.width,
				height: rect.height,
				left: imageViewerState ? imageViewerState.left : rect.left,
				top: imageViewerState ? imageViewerState.top : rect.top
			});
			const moved = !imageViewerState || nextState.left !== imageViewerState.left || nextState.top !== imageViewerState.top;
			const resized = !imageViewerState || nextState.width !== imageViewerState.width || nextState.height !== imageViewerState.height;
			imageViewerState = nextState;
			if (moved) {
				applyImageViewerGeometry();
			}
			if (moved || resized) {
				persistImageViewerState();
			}
		});
		imageViewerResizeObserver.observe(refs.imageViewerWindow);
	}

	function endImageViewerDrag(commitGeometry) {
		if (!imageViewerDragState) {
			return false;
		}

		imageViewerDragState = null;
		clearImageViewerDragFrame();
		document.body.classList.remove("image-viewer-dragging");
		window.removeEventListener("mousemove", trackImageViewerDrag, true);
		window.removeEventListener("mouseup", finishImageViewerDrag, true);
		if (commitGeometry) {
			applyImageViewerGeometry();
		} else if (refs.imageViewerWindow) {
			refs.imageViewerWindow.style.transform = "";
		}
		return true;
	}

	function cancelImageViewerDrag() {
		endImageViewerDrag(false);
	}

	function finishImageViewerDrag() {
		if (endImageViewerDrag(true)) {
			persistImageViewerState();
		}
	}

	function trackImageViewerDrag(event) {
		if (!imageViewerDragState) {
			return;
		}

		imageViewerState = clampImageViewerState({
			width: imageViewerState.width,
			height: imageViewerState.height,
			left: imageViewerDragState.startLeft + (event.clientX - imageViewerDragState.startX),
			top: imageViewerDragState.startTop + (event.clientY - imageViewerDragState.startY)
		});
		imageViewerDragState.currentLeft = imageViewerState.left;
		imageViewerDragState.currentTop = imageViewerState.top;
		scheduleImageViewerDragPreview();
		event.preventDefault();
	}

	function beginImageViewerDrag(event) {
		if (event.button !== 0 || !refs.imageViewerWindow || !refs.imageViewerLayer || refs.imageViewerLayer.classList.contains("hidden")) {
			return;
		}
		const dragTarget = eventElementTarget(event);
		if (dragTarget && typeof dragTarget.closest === "function" && dragTarget.closest("button")) {
			return;
		}

		const rect = refs.imageViewerWindow.getBoundingClientRect();
		imageViewerDragState = {
			startX: event.clientX,
			startY: event.clientY,
			startLeft: rect.left,
			startTop: rect.top,
			currentLeft: rect.left,
			currentTop: rect.top
		};
		document.body.classList.add("image-viewer-dragging");
		window.addEventListener("mousemove", trackImageViewerDrag, true);
		window.addEventListener("mouseup", finishImageViewerDrag, true);
		event.preventDefault();
	}

	function openImageViewer(source, title) {
		const normalizedSource = String(source || "").trim();
		if (!normalizedSource || !refs.imageViewerLayer || !refs.imageViewerImage) {
			return false;
		}

		imageViewerState = clampImageViewerState(imageViewerState || defaultImageViewerState());
		refs.imageViewerTitle.textContent = String(title || "").trim() || "Image";
		refs.imageViewerImage.src = normalizedSource;
		refs.imageViewerImage.alt = String(title || "").trim() || "Image";
		refs.imageViewerLayer.classList.remove("hidden");
		refs.imageViewerLayer.setAttribute("aria-hidden", "false");
		applyImageViewerGeometry();
		observeImageViewerSize();
		return true;
	}

	function closeImageViewer() {
		if (!refs.imageViewerLayer || refs.imageViewerLayer.classList.contains("hidden")) {
			return;
		}

		cancelImageViewerDrag();
		stopObservingImageViewerSize();
		refs.imageViewerLayer.classList.add("hidden");
		refs.imageViewerLayer.setAttribute("aria-hidden", "true");
		refs.imageViewerImage.removeAttribute("src");
		persistImageViewerState();
	}

	function openImageViewerFromElement(image) {
		if (!image) {
			return false;
		}

		const source = String(image.currentSrc || image.src || image.getAttribute("src") || "").trim();
		if (!source) {
			return false;
		}

		return openImageViewer(source, image.getAttribute("alt") || "");
	}

	function handleMessageImageActivation(event) {
		const image = imageFromEvent(event);
		if (!image) {
			return;
		}

		event.preventDefault();
		event.stopPropagation();
		hideContextMenu();
		hideSelfMenu();
		openImageViewerFromElement(image);
	}

	function dayLabelFromMs(createdAtMs) {
		if (!createdAtMs) {
			return "";
		}

		const target = new Date(Number(createdAtMs));
		const now = new Date();
		const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
		const yesterday = new Date(today);
		yesterday.setDate(today.getDate() - 1);
		const targetDay = new Date(target.getFullYear(), target.getMonth(), target.getDate());

		if (targetDay.getTime() === today.getTime()) {
			return "TODAY";
		}
		if (targetDay.getTime() === yesterday.getTime()) {
			return "YESTERDAY";
		}

		return target.toLocaleDateString(undefined, { month: "short", day: "numeric" }).toUpperCase();
	}

	function isSameDay(leftMs, rightMs) {
		if (!leftMs || !rightMs) {
			return false;
		}

		const left = new Date(Number(leftMs));
		const right = new Date(Number(rightMs));
		return left.getFullYear() === right.getFullYear()
			&& left.getMonth() === right.getMonth()
			&& left.getDate() === right.getDate();
	}

	function shouldGroupWith(previous, current) {
		if (!previous || !current || previous.system || current.system) {
			return false;
		}

		if (!!previous.own !== !!current.own) {
			return false;
		}

		if (String(previous.actor || "") !== String(current.actor || "")) {
			return false;
		}

		if (!isSameDay(previous.createdAtMs, current.createdAtMs)) {
			return false;
		}

		if (!previous.createdAtMs || !current.createdAtMs) {
			return true;
		}

		const gapMs = Math.abs(Number(current.createdAtMs) - Number(previous.createdAtMs));
		return gapMs <= (8 * 60 * 1000);
	}

	function renderMeta(meta) {
		refs.conversationMeta.innerHTML = "";
		(meta || []).forEach(function(entry) {
			const pill = document.createElement("span");
			pill.className = "meta-pill";
			pill.textContent = entry;
			refs.conversationMeta.appendChild(pill);
		});
	}

	function screenShareVisible(share) {
		return !!(share && share.visible);
	}

	function screenShareStatusText(share) {
		switch (String(share && share.mode || "")) {
			case "publishing":
				return "Publishing";
			case "available":
				return "Available";
			case "viewing":
				return "Viewing";
			case "fallback":
				return "Fallback";
			case "error":
				return "Unavailable";
			default:
				return "Idle";
		}
	}

	function compactScreenShareActionLabel(label) {
		switch (String(label || "").toLowerCase()) {
			case "share screen":
				return "Share";
			case "watch share":
				return "Watch";
			case "open share window":
				return "Open";
			default:
				return label || "Share";
		}
	}

	function renderScreenShareHeader(scope, share) {
		const visible = screenShareVisible(share);
		const active = String(share && share.mode || "") === "publishing"
			|| String(share && share.mode || "") === "viewing"
			|| String(share && share.mode || "") === "fallback";
		const buttonClasses = ["chip-button", "screen-share-button"];
		if (!visible) {
			buttonClasses.push("hidden");
		} else if (share.primaryTone) {
			buttonClasses.push("is-" + share.primaryTone);
		}
		if (active) {
			buttonClasses.push("is-active");
		}
		refs.screenShareButton.className = buttonClasses.join(" ");
		if (!visible) {
			refs.screenShareButton.disabled = true;
			refs.screenShareButton.textContent = "Share screen";
			refs.screenShareButton.title = "Share screen";
			refs.screenShareButton.dataset.scopeToken = "";
			refs.screenShareButton.dataset.actionId = "";
			return;
		}

		const label = share.primaryLabel || "Share screen";
		const hint = share.primaryHint || label;
		refs.screenShareButton.textContent = label;
		refs.screenShareButton.disabled = share.primaryEnabled === false;
		refs.screenShareButton.title = hint;
		refs.screenShareButton.dataset.scopeToken = scope.scopeToken || share.scopeToken || "";
		refs.screenShareButton.dataset.actionId = share.primaryActionId || "";
	}

	function renderScreenShareCard(scope, share) {
		const visible = screenShareVisible(share);
		const mode = String(share && share.mode || "idle");
		const statusTone = share && share.statusTone ? " is-" + share.statusTone : "";
		refs.screenShareCard.className = "screen-share-card" + statusTone + (mode ? " is-mode-" + mode : "")
			+ (visible ? "" : " hidden");
		if (!visible) {
			refs.screenShareCardActions.innerHTML = "";
			return;
		}

		let title = "Screen sharing";
		if (mode === "publishing") {
			title = "Your screen share";
		} else if ((mode === "available" || mode === "viewing" || mode === "fallback") && share.ownerLabel) {
			title = share.ownerLabel;
		} else if (mode === "idle") {
			title = "No active share";
		}

		refs.screenShareCardStatus.className = "meta-pill" + statusTone + (share.statusLabel ? "" : " hidden");
		refs.screenShareCardStatus.textContent = screenShareStatusText(share);
		refs.screenShareCardTitle.textContent = title;
		refs.screenShareCardSummary.textContent = share.statusLabel || "Screen sharing is available in voice rooms.";

		const note = share.fallbackLabel || ((share.primaryEnabled === false && share.primaryHint) ? share.primaryHint : "");
		refs.screenShareCardNote.textContent = note;
		refs.screenShareCardNote.classList.toggle("hidden", !note);

		refs.screenShareCardActions.innerHTML = "";
		actionItemsFromActionStates(share.overflowActions, {
			invokeAction: function(actionId) {
				notifyBridge("invokeScopeAction", scope.scopeToken || share.scopeToken || "", actionId);
			}
		}).forEach(function(item) {
			if (!item || item.kind !== "action") {
				return;
			}

			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button screen-share-card-action"
				+ (item.tone ? " is-" + item.tone : "");
			button.textContent = item.label || "Action";
			button.disabled = item.enabled === false;
			if (item.hint) {
				button.title = item.hint;
			}
			button.addEventListener("click", function() {
				if (item.enabled === false || typeof item.action !== "function") {
					return;
				}
				item.action();
			});
			refs.screenShareCardActions.appendChild(button);
		});
	}

	function findRoomState(snapshot, scopeToken) {
		const token = String(scopeToken || "");
		return (snapshot.voiceRooms || []).concat(snapshot.textRooms || []).find(function(room) {
			return String(room.token || "") === token;
		}) || null;
	}

	function participantStateKey(person) {
		if (!person) {
			return "";
		}

		return String(person.participantKey || person.session || "");
	}

	function participantActionWeight(person) {
		if (!person) {
			return -1;
		}

		let weight = 0;
		if (Array.isArray(person.actions) && person.actions.length) {
			weight += 1000 + person.actions.length;
		}
		if (person.canMessage) {
			weight += 10;
		}
		if (person.canJoin) {
			weight += 10;
		}
		if (person.avatarUrl) {
			weight += 1;
		}
		return weight;
	}

	function findParticipantState(snapshot, key, scopeToken) {
		const targetKey = String(key || "");
		const targetScopeToken = String(scopeToken || "");
		let bestMatch = null;

		function consider(person, preferredScope) {
			if (participantStateKey(person) !== targetKey) {
				return;
			}

			const score = participantActionWeight(person) + (preferredScope ? 100 : 0);
			const bestScore = participantActionWeight(bestMatch);
			if (!bestMatch || score > bestScore) {
				bestMatch = person;
			}
		}

		(snapshot.participants || []).forEach(function(person) {
			consider(person, !targetScopeToken || String(person.scopeToken || "") === targetScopeToken);
		});

		for (const room of (snapshot.voiceRooms || [])) {
			const roomScopeToken = String(room && room.token || "");
			(room.participants || []).forEach(function(person) {
				consider(person, !!targetScopeToken && roomScopeToken === targetScopeToken);
			});
		}

		(snapshot.voicePresence || []).forEach(function(person) {
			consider(person, !targetScopeToken || String(person.scopeToken || "") === targetScopeToken);
		});

		return bestMatch;
	}

	function findMessageState(snapshot, messageId) {
		const targetId = String(messageId || "");
		return (snapshot.messages || []).find(function(message) {
			return String(message.messageId || "") === targetId;
		}) || null;
	}

	function actionItemsFromActionStates(actionStates, handlers) {
		return (actionStates || []).map(function(item) {
			if (!item) {
				return null;
			}

			const kind = String(item.kind || "action");
			if (kind === "separator") {
				return { kind: "separator", separator: true };
			}

			if (kind === "label") {
				return {
					kind: "label",
					label: item.label || "",
					hint: item.hint || ""
				};
			}

			if (kind === "slider") {
				if (!item.id) {
					return null;
				}

				return {
					kind: "slider",
					id: item.id || "",
					label: item.label || "Value",
					enabled: item.enabled !== false,
					tone: item.tone || "",
					hint: item.hint || "",
					value: Number(item.value || 0),
					min: Number(item.min || 0),
					max: Number(item.max || 0),
					step: Number(item.step || 1),
					suffix: item.suffix || "",
					finalOnRelease: item.finalOnRelease !== false,
					onValueChange: function(value, final) {
						if (handlers && typeof handlers.invokeValueChanged === "function") {
							handlers.invokeValueChanged(item.id || "", value, final);
						}
					}
				};
			}

			if (!item.id) {
				return null;
			}

			return {
				kind: "action",
				id: item.id || "",
				label: item.label || "Action",
				enabled: item.enabled !== false,
				checked: !!item.checked,
				tone: item.tone || "",
				hint: item.hint || "",
				action: function() {
					if (handlers && typeof handlers.invokeAction === "function") {
						handlers.invokeAction(item.id || "");
					}
				}
			};
		});
	}

	function actionStatesContainId(actionStates, actionId) {
		const targetId = String(actionId || "");
		if (!targetId) {
			return false;
		}

		return (actionStates || []).some(function(item) {
			return item && String(item.id || "") === targetId;
		});
	}

	function actionPanelItemKind(item) {
		if (!item) {
			return "";
		}

		if (item.kind) {
			return String(item.kind);
		}
		if (item.separator) {
			return "separator";
		}
		return "action";
	}

	function normalizedActionPanelItems(items, options) {
		const normalized = [];
		const hideDisabled = !!(options && options.hideDisabled);

		(items || []).forEach(function(item) {
			const kind = actionPanelItemKind(item);
			if (!kind) {
				return;
			}

			if (kind === "submenu") {
				const childItems = normalizedActionPanelItems(item.items || [], options);
				if (!childItems.length) {
					return;
				}

				const submenu = Object.assign({}, item);
				submenu.items = childItems;
				normalized.push(submenu);
				return;
			}

			if (hideDisabled && (kind === "action" || kind === "slider") && item.enabled === false && !item.checked) {
				return;
			}

			if (kind === "separator") {
				if (!normalized.length || actionPanelItemKind(normalized[normalized.length - 1]) === "separator") {
					return;
				}

				normalized.push(item);
				return;
			}

			normalized.push(item);
		});

		while (normalized.length && actionPanelItemKind(normalized[normalized.length - 1]) === "separator") {
			normalized.pop();
		}

		return normalized.filter(function(item, index) {
			if (actionPanelItemKind(item) !== "label") {
				return true;
			}

			const nextKind = actionPanelItemKind(normalized[index + 1]);
			return !!nextKind && nextKind !== "separator" && nextKind !== "label";
		});
	}

	function compactContextSubmenu(label, items, hint) {
		const normalized = normalizedActionPanelItems(items, { hideDisabled: true });
		if (!normalized.length) {
			return null;
		}

		return {
			kind: "submenu",
			label: label || "More",
			enabled: true,
			hint: hint || "",
			items: normalized
		};
	}

	function contextActionMatches(item, ids) {
		if (!item || !item.id) {
			return false;
		}

		return ids.indexOf(String(item.id)) !== -1;
	}

	function groupedContextMenuItems(items, primaryIds, groupDefinitions, fallbackLabel) {
		const primary = [];
		const fallback = [];
		const groupBuckets = (groupDefinitions || []).map(function(group) {
			return {
				label: group.label,
				hint: group.hint || "",
				ids: group.ids || [],
				items: []
			};
		});
		let pendingLabels = [];

		(items || []).forEach(function(item) {
			const kind = actionPanelItemKind(item);
			if (!kind || kind === "separator") {
				pendingLabels = [];
				return;
			}

			if (kind === "label") {
				pendingLabels = [item];
				return;
			}

			let target = fallback;
			if (contextActionMatches(item, primaryIds || [])) {
				target = primary;
			} else {
				const group = groupBuckets.find(function(candidate) {
					return contextActionMatches(item, candidate.ids);
				});
				if (group) {
					target = group.items;
				}
			}

			if (pendingLabels.length && kind === "slider") {
				target.push.apply(target, pendingLabels);
			}
			pendingLabels = [];
			target.push(item);
		});

		const grouped = normalizedActionPanelItems(primary, { hideDisabled: true });
		groupBuckets.forEach(function(group) {
			const submenu = compactContextSubmenu(group.label, group.items, group.hint);
			if (!submenu) {
				return;
			}
			if (grouped.length) {
				grouped.push({ separator: true });
			}
			grouped.push(submenu);
		});

		const fallbackSubmenu = compactContextSubmenu(fallbackLabel || "More", fallback);
		if (fallbackSubmenu) {
			if (grouped.length) {
				grouped.push({ separator: true });
			}
			grouped.push(fallbackSubmenu);
		}

		return normalizedActionPanelItems(grouped, { hideDisabled: true });
	}

	function participantContextGroups(items) {
		return groupedContextMenuItems(
			items,
			["openMessage", "joinRoom", "join", "textMessage", "userInfo"],
			[
				{
					label: "Voice",
					ids: [
						"localVolume",
						"mute",
						"deaf",
						"prioritySpeaker",
						"localMute",
						"remoteSpeechCleanup",
						"self.toggleMute",
						"self.toggleDeaf"
					]
				},
				{
					label: "Screen Share",
					ids: [
						"screenShareStart",
						"screenShareStop",
						"screenShareWatch",
						"screenShareStopWatching",
						"screenShareOpenWindow"
					]
				},
				{
					label: "Messages",
					ids: ["ignoreMessages", "ignoreTts", "grantChatHistory"]
				},
				{
					label: "Profile",
					ids: [
						"localNickname",
						"selfComment",
						"commentView",
						"commentReset",
						"textureReset",
						"register",
						"friendAdd",
						"friendUpdate",
						"friendRemove",
						"avatarChange",
						"avatarRemove",
						"audioStats",
						"recording"
					]
				},
				{
					label: "Moderation",
					ids: ["move", "kick", "ban"]
				}
			],
			"More"
		);
	}

	function roomContextGroups(items) {
		return groupedContextMenuItems(
			items,
			["openRoom", "joinVoice", "join", "listen", "markRead"],
			[
				{
					label: "Screen Share",
					ids: [
						"screenShareStart",
						"screenShareStop",
						"screenShareWatch",
						"screenShareStopWatching",
						"screenShareOpenWindow"
					]
				},
				{
					label: "Room",
					ids: ["sendMessage", "copyUrl", "hide", "pin"]
				},
				{
					label: "Manage",
					ids: ["add", "acl", "remove", "link", "unlink", "unlinkAll"]
				}
			],
			"More"
		);
	}

	function sliderValueLabel(item, value) {
		const numericValue = Number(value || 0);
		if (!Number.isFinite(numericValue)) {
			return "";
		}

		const prefix = numericValue > 0 ? "+" : "";
		return prefix + String(numericValue) + String(item && item.suffix ? item.suffix : "");
	}

	function appendActionPanelItem(container, item, variant, hideOnAction) {
		const prefix = variant === "menu" ? "menu" : "context-menu";
		const kind = actionPanelItemKind(item);

		if (!kind) {
			return;
		}

		if (kind === "separator") {
			const separator = document.createElement("div");
			separator.className = prefix + "-separator";
			container.appendChild(separator);
			return;
		}

		if (kind === "label") {
			const labelClass = variant === "menu" ? "menu-item-label" : "context-menu-label";
			const label = document.createElement("div");
			label.className = prefix + "-label-row";
			if (item.hint) {
				label.title = item.hint;
			}
			label.innerHTML = "<span class=\"" + labelClass + "\"></span>";
			label.querySelector("." + labelClass).textContent = item.label || "";
			container.appendChild(label);
			return;
		}

		if (kind === "slider") {
			const slider = document.createElement("div");
			slider.className = prefix + "-slider" + (item.tone ? " is-" + item.tone : "");
			if (item.hint) {
				slider.title = item.hint;
			}
			slider.innerHTML = "<div class=\"" + prefix + "-slider-header\"><span class=\"" + prefix
				+ "-slider-label\"></span><span class=\"" + prefix + "-slider-value\"></span></div><input class=\""
				+ prefix + "-slider-input\" type=\"range\">";

			const label = slider.querySelector("." + prefix + "-slider-label");
			const value = slider.querySelector("." + prefix + "-slider-value");
			const input = slider.querySelector("." + prefix + "-slider-input");
			label.textContent = item.label || "Value";
			value.textContent = sliderValueLabel(item, item.value);
			input.min = String(item.min);
			input.max = String(item.max);
			input.step = String(item.step || 1);
			input.value = String(item.value || 0);
			input.disabled = item.enabled === false;

			const emitValue = function(final) {
				value.textContent = sliderValueLabel(item, input.value);
				if (typeof item.onValueChange === "function") {
					item.onValueChange(Number(input.value), final);
				}
			};

			input.addEventListener("input", function(event) {
				event.stopPropagation();
				emitValue(item.finalOnRelease ? false : true);
			});
			if (item.finalOnRelease) {
				input.addEventListener("change", function(event) {
					event.stopPropagation();
					emitValue(true);
				});
			}
			input.addEventListener("pointerdown", function(event) {
				event.stopPropagation();
			});
			input.addEventListener("click", function(event) {
				event.stopPropagation();
			});
			slider.addEventListener("click", function(event) {
				event.stopPropagation();
			});

			container.appendChild(slider);
			return;
		}

		if (kind === "submenu") {
			const submenu = document.createElement("div");
			submenu.className = prefix + "-submenu";
			if (item.hint) {
				submenu.title = item.hint;
			}

			const trigger = document.createElement("button");
			trigger.type = "button";
			const labelClass = variant === "menu" ? "menu-item-label" : "context-menu-label";
			const stateClass = variant === "menu" ? "menu-item-state" : "context-menu-state";
			trigger.className = prefix + "-item " + prefix + "-submenu-trigger";
			trigger.setAttribute("aria-haspopup", "menu");
			trigger.setAttribute("aria-expanded", "false");
			trigger.innerHTML = "<span class=\"" + labelClass + "\"></span><span class=\"" + stateClass
				+ "\" aria-hidden=\"true\">&gt;</span>";
			trigger.querySelector("." + labelClass).textContent = item.label || "More";

			const panel = document.createElement("div");
			panel.className = prefix + "-submenu-panel";
			panel.setAttribute("role", "menu");
			normalizedActionPanelItems(item.items || [], { hideDisabled: true }).forEach(function(childItem) {
				appendActionPanelItem(panel, childItem, variant, hideOnAction);
			});

			const setOpen = function(open) {
				submenu.classList.toggle("is-open", open);
				trigger.setAttribute("aria-expanded", open ? "true" : "false");
				if (variant !== "menu") {
					requestContextMenuViewportFit();
				}
			};

			submenu.addEventListener("pointerenter", function() {
				setOpen(true);
			});
			submenu.addEventListener("pointerleave", function() {
				setOpen(false);
			});
			submenu.addEventListener("focusin", function() {
				setOpen(true);
			});
			submenu.addEventListener("focusout", function(event) {
				if (!submenu.contains(event.relatedTarget)) {
					setOpen(false);
				}
			});
			trigger.addEventListener("click", function(event) {
				event.stopPropagation();
				setOpen(!submenu.classList.contains("is-open"));
			});

			submenu.appendChild(trigger);
			submenu.appendChild(panel);
			container.appendChild(submenu);
			return;
		}

		const button = document.createElement("button");
		button.type = "button";
		const labelClass = variant === "menu" ? "menu-item-label" : "context-menu-label";
		const stateClass = variant === "menu" ? "menu-item-state" : "context-menu-state";
		button.className = prefix + "-item"
			+ (item.checked ? " is-checked" : "")
			+ (item.tone ? " is-" + item.tone : "");
		button.disabled = item.enabled === false;
		if (item.hint) {
			button.title = item.hint;
		}
		button.innerHTML = "<span class=\"" + labelClass + "\"></span><span class=\"" + stateClass + "\"></span>";
		button.querySelector("." + labelClass).textContent = item.label || "Action";
		button.querySelector("." + stateClass).textContent = item.checked ? "On" : "";
		button.addEventListener("click", function(event) {
			event.stopPropagation();
			if (hideOnAction) {
				if (variant === "menu") {
					closeTopMenu();
				} else {
					hideContextMenu();
				}
			}
			if (item.enabled === false || typeof item.action !== "function") {
				return;
			}
			item.action();
		});
		container.appendChild(button);
	}

	function presenceStatusIconSvg(kind) {
		switch (String(kind || "")) {
			case "talking":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 5v14\"></path><path d=\"M7 9v6\"></path><path d=\"M17 9v6\"></path><path d=\"M4 11v2\"></path><path d=\"M20 11v2\"></path></svg>";
			case "whispering":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M16 10.5a3 3 0 010 3\"></path></svg>";
			case "shouting":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M15.5 8.5a5 5 0 010 7\"></path><path d=\"M18.5 5.5a9 9 0 010 13\"></path></svg>";
			case "mutedTalking":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M16 9l5 5\"></path><path d=\"M21 9l-5 5\"></path></svg>";
			case "priority":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3l2.1 5.9H20l-4.8 3.6 1.8 6.2L12 15l-5 3.7 1.8-6.2L4 8.9h5.9z\"></path></svg>";
			case "recording":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"6\"></circle><circle cx=\"12\" cy=\"12\" r=\"2.5\" fill=\"currentColor\" stroke=\"none\"></circle></svg>";
			case "friend":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3.8l2.55 5.18 5.72.83-4.13 4.03.98 5.69L12 16.83l-5.12 2.7.98-5.69-4.13-4.03 5.72-.83z\"></path></svg>";
			case "authenticated":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3l6 2.5V11c0 4-2.56 7.28-6 8.5-3.44-1.22-6-4.5-6-8.5V5.5z\"></path><path d=\"M9.3 11.8l1.8 1.8 3.6-3.9\"></path></svg>";
			case "listener":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 5a4 4 0 014 4v3.5\"></path><path d=\"M12 19a4.5 4.5 0 01-4.5-4.5V11a4.5 4.5 0 019 0\"></path><path d=\"M16.5 10.5a4.5 4.5 0 013 4\"></path></svg>";
			case "serverDeafened":
			case "selfDeafened":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M16 9l5 5\"></path><path d=\"M21 9l-5 5\"></path></svg>";
			case "serverMuted":
			case "selfMuted":
			case "localMuted":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M15.5 8.5a5 5 0 010 7\"></path></svg>";
			case "suppressed":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M5 12h14\"></path><path d=\"M8 8.5h8\"></path><path d=\"M8 15.5h8\"></path></svg>";
			default:
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"5\"></circle></svg>";
		}
	}

	function normalizedTalkState(person) {
		return String(person && person.talkState || "passive").trim();
	}

	function talkStateClass(person) {
		switch (normalizedTalkState(person)) {
			case "talking":
				return " is-talking is-talk-talking";
			case "whispering":
				return " is-talking is-talk-whispering";
			case "shouting":
				return " is-talking is-talk-shouting";
			case "mutedTalking":
				return " is-talking is-talk-muted";
			default:
				return "";
		}
	}

	function renderPresenceStatuses(container, statuses) {
		const visibleStatuses = (statuses || []).filter(function(status) {
			return !!(status && status.kind);
		});

		container.innerHTML = "";
		container.classList.toggle("hidden", !visibleStatuses.length);
		visibleStatuses.forEach(function(status) {
			const indicator = document.createElement("span");
			indicator.className = "presence-status" + (status.tone ? " is-" + status.tone : "");
			indicator.title = status.label || "";
			indicator.setAttribute("aria-label", status.label || "");
			indicator.innerHTML = presenceStatusIconSvg(status.kind);
			container.appendChild(indicator);
		});
	}

	function presenceRowTitle(person) {
		const parts = [];
		const detailLabels = [];

		if (person && person.subtitle) {
			parts.push(person.subtitle);
		}

		(person && person.statuses ? person.statuses : []).forEach(function(status) {
			const label = status && status.label ? String(status.label) : "";
			if (label && detailLabels.indexOf(label) === -1) {
				detailLabels.push(label);
			}
		});

		(person && person.badges ? person.badges : []).forEach(function(badge) {
			const label = badge ? String(badge) : "";
			if (label && detailLabels.indexOf(label) === -1) {
				detailLabels.push(label);
			}
		});

		if (detailLabels.length) {
			parts.push(detailLabels.join(", "));
		}

		return parts.join(" | ");
	}

	function renderVoicePresenceStack(people) {
		refs.voicePresenceStack.innerHTML = "";
		(people || []).filter(function(person) {
			return String(person && person.entryKind || "user") !== "listener";
		}).slice(0, 5).forEach(function(person) {
			const avatar = document.createElement("div");
			avatar.className = "stack-avatar" + (person.isSelf ? " is-self" : "") + talkStateClass(person);
			styleAvatar(avatar, person.label, !!person.isSelf, person.avatarUrl || "");
			avatar.title = [person.label || "", person.talkLabel || ""].filter(Boolean).join(" | ");
			refs.voicePresenceStack.appendChild(avatar);
		});
	}

	function clearRoomDropTarget(event) {
		const target = event.currentTarget || event.target;
		if (!target || !target.classList) {
			return;
		}
		target.classList.remove("is-drop-target", "is-drop-before", "is-drop-after", "is-drop-inside");
		target.removeAttribute("data-drop-placement");
	}

	function clearAllRoomDropTargets() {
		document.querySelectorAll(".rail-row.is-drop-target, .rail-root-label.is-drop-target").forEach(function(element) {
			element.classList.remove("is-drop-target", "is-drop-before", "is-drop-after", "is-drop-inside");
			element.removeAttribute("data-drop-placement");
		});
	}

	function roomDropPlacement(event, target) {
		if (!dragState || dragState.kind !== "room" || !target || target.dataset.dropTarget === "root") {
			return "inside";
		}

		const bounds = target.getBoundingClientRect();
		if (!bounds.height) {
			return "inside";
		}

		const y = Math.max(0, Math.min(bounds.height, event.clientY - bounds.top));
		if (y <= bounds.height * 0.30) {
			return "before";
		}
		if (y >= bounds.height * 0.70) {
			return "after";
		}
		return "inside";
	}

	function applyRoomDropTarget(target, placement) {
		if (!target || !target.classList) {
			return;
		}

		target.classList.remove("is-drop-before", "is-drop-after", "is-drop-inside");
		target.classList.add("is-drop-target", "is-drop-" + (placement || "inside"));
		target.dataset.dropPlacement = placement || "inside";
	}

	function handleRoomDragEnter(event) {
		const target = event.currentTarget;
		if (!target || !dragState || !target.classList) {
			return;
		}

		if (target.dataset.dropTarget === "root" && dragState.kind !== "room") {
			return;
		}

		if (dragState.kind === "participant" || dragState.kind === "room") {
			event.preventDefault();
			applyRoomDropTarget(target, roomDropPlacement(event, target));
		}
	}

	function handleRoomDragOver(event) {
		const target = event.currentTarget;
		if (!target || !dragState || !target.classList) {
			return;
		}

		if (target.dataset.dropTarget === "root" && dragState.kind !== "room") {
			return;
		}

		if (dragState.kind === "participant" || dragState.kind === "room") {
			event.preventDefault();
			if (event.dataTransfer) {
				event.dataTransfer.dropEffect = "move";
			}
			applyRoomDropTarget(target, roomDropPlacement(event, target));
		}
	}

	function participantContextMenuItems(participant, scopeToken) {
		const items = [];
		if (!participant) {
			return items;
		}

		if (participant.entryKind !== "listener" && participant.canMessage
			&& !actionStatesContainId(participant.actions, "textMessage")) {
			items.push({
				id: "openMessage",
				label: "Open message",
				enabled: true,
				action: function() {
					notifyBridge("messageParticipant", participant.session);
				}
			});
		}
		if (participant.entryKind !== "listener" && participant.canJoin
			&& !actionStatesContainId(participant.actions, "join")) {
			items.push({
				id: "joinRoom",
				label: "Join room",
				enabled: true,
				action: function() {
					notifyBridge("joinParticipant", participant.session);
				}
			});
		}

		const participantActionItems = actionItemsFromActionStates(participant.actions, participant.entryKind === "listener"
			? {
				invokeAction: function(actionId) {
					notifyBridge("invokeScopeAction", participant.scopeToken || scopeToken, actionId);
				},
				invokeValueChanged: function(actionId, value, final) {
					notifyBridge("scopeActionValueChanged",
						participant.scopeToken || scopeToken,
						actionId,
						value,
						final);
				}
			}
			: {
				invokeAction: function(actionId) {
					notifyBridge("invokeParticipantAction", participant.session, actionId);
				},
				invokeValueChanged: function(actionId, value, final) {
					notifyBridge("participantActionValueChanged", participant.session, actionId, value, final);
				}
			});
		if (items.length && participantActionItems.length) {
			items.push({ separator: true });
		}
		return participantContextGroups(items.concat(participantActionItems));
	}

	function renderPresenceList(container, room, people) {
		const list = document.createElement("div");
		list.className = "presence-list";

		(people || []).forEach(function(person) {
			const entry = document.createElement("div");
			entry.className = "presence-entry";

			const row = document.createElement("button");
			row.type = "button";
			row.className = "presence-row"
				+ (person.isSelf ? " is-self" : "")
				+ (person.entryKind === "listener" ? " is-listener" : "")
				+ talkStateClass(person);
			row.dataset.session = person.session ? String(person.session) : "";
			row.dataset.participantKey = participantStateKey(person);
			row.dataset.entryKind = String(person.entryKind || "user");
			row.dataset.canMessage = person.canMessage ? "true" : "false";
			row.dataset.canJoin = person.canJoin ? "true" : "false";
			row.dataset.roomToken = room && room.token ? String(room.token) : "";
			row.dataset.scopeToken = person.scopeToken ? String(person.scopeToken) : (room && room.token ? String(room.token) : "");
			row.draggable = person.entryKind !== "listener" && !!row.dataset.session;
			row.classList.toggle("is-disabled",
				!person.canMessage && !person.canJoin && !((person.actions || []).length));
			row.title = presenceRowTitle(person);

			const avatar = document.createElement("span");
			avatar.className = "presence-avatar"
				+ (person.isSelf ? " is-self" : "")
				+ (person.entryKind === "listener" ? " is-listener" : "")
				+ talkStateClass(person);
			styleAvatar(avatar, person.label, !!person.isSelf, person.avatarUrl || "");

			const dot = document.createElement("span");
			dot.className = "presence-dot" + (person.entryKind === "listener" ? " is-listener" : "")
				+ talkStateClass(person);

			const label = document.createElement("span");
			label.className = "presence-copy";
			label.innerHTML = "<span class=\"presence-heading\"><span class=\"presence-name\"></span><span class=\"presence-statuses hidden\"></span></span><span class=\"presence-subtitle\"></span>";
			label.querySelector(".presence-name").textContent = person.label || "Unknown";
			renderPresenceStatuses(label.querySelector(".presence-statuses"), person.statuses || []);
			label.querySelector(".presence-subtitle").textContent = person.subtitle || "";
			label.querySelector(".presence-subtitle").classList.toggle("hidden", !(person.subtitle || ""));

			row.appendChild(avatar);
			row.appendChild(dot);
			row.appendChild(label);
			entry.appendChild(row);

			if ((person.actions || []).length) {
				const actionsButton = document.createElement("button");
				actionsButton.type = "button";
				actionsButton.className = "presence-action-button";
				actionsButton.title = "More actions";
				actionsButton.setAttribute("aria-label", "More actions for " + (person.label || "participant"));
				actionsButton.innerHTML = "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"5\" cy=\"12\" r=\"1.8\"></circle><circle cx=\"12\" cy=\"12\" r=\"1.8\"></circle><circle cx=\"19\" cy=\"12\" r=\"1.8\"></circle></svg>";
				actionsButton.addEventListener("click", function(event) {
					event.stopPropagation();
					event.preventDefault();
					const bounds = actionsButton.getBoundingClientRect();
					showContextMenu(participantContextMenuItems(person, row.dataset.scopeToken),
						bounds.left,
						bounds.bottom + contextMenuAnchorGap,
						{
							anchorBounds: bounds,
							horizontal: "left-of-anchor"
						});
				});
				entry.appendChild(actionsButton);
			}

			row.addEventListener("click", function(event) {
				event.stopPropagation();
				if (row.dataset.entryKind === "listener") {
					return;
				}
				if (row.dataset.canMessage === "true") {
					notifyBridge("messageParticipant", row.dataset.session);
				} else if (row.dataset.canJoin === "true") {
					notifyBridge("joinParticipant", row.dataset.session);
				}
				dismissCompactRailAfterAction();
			});
			row.addEventListener("dragstart", function(event) {
				if (!row.dataset.session || row.dataset.entryKind === "listener") {
					event.preventDefault();
					return;
				}

				dragState = {
					kind: "participant",
					session: row.dataset.session,
					sourceRoomToken: row.dataset.roomToken || ""
				};
				row.classList.add("is-dragging");
				if (event.dataTransfer) {
					event.dataTransfer.effectAllowed = "move";
					event.dataTransfer.setData("text/plain", "participant:" + row.dataset.session);
				}
			});
			row.addEventListener("dragend", function() {
				row.classList.remove("is-dragging");
				dragState = null;
				clearAllRoomDropTargets();
			});
			list.appendChild(entry);
		});

		container.appendChild(list);
	}

	function buildRoomRow(room, joinable, selectedVoicePresence) {
		const depthValue = Number(room.depth);
		const depth = Number.isFinite(depthValue) && depthValue > 0 ? depthValue : 0;
		const roomPathLabel = room.pathLabel || "";
		const unreadCount = Number(room.unreadCount || 0);
		const memberCount = Number(room.memberCount || 0);
		const screenShare = room.screenShare || {};
		const isRootRoom = !!room.isRoot;
		const joining = joinable && String(room.token || "") === pendingVoiceJoinToken();
		const roomKind = String(room.kindLabel || "").trim().toLowerCase();
		const hasRoomActions = (room.actions || []).some(function(item) {
			return !!item && String(item.kind || "action") !== "separator";
		});
		const subtitleText = joinable
			? ((room.joined || room.selected) ? (roomPathLabel || room.description || "") : "")
			: ((room.selected || unreadCount > 0 || roomKind === "activity" || roomKind === "direct message")
				? (room.description || "")
				: "");
		const wrapper = document.createElement("div");
		wrapper.className = "rail-row-wrapper" + (joinable ? " is-voice-room" : "");
		wrapper.setAttribute("role", "option");
		wrapper.setAttribute("aria-label", room.label || "Room");
		wrapper.setAttribute("aria-selected", room.selected ? "true" : "false");
		wrapper.style.setProperty("--room-depth", String(depth));
		wrapper.dataset.scopeToken = room.token || "";
		wrapper.dataset.roomType = joinable ? "voice" : "text";

		const button = document.createElement("div");
		button.setAttribute("role", "button");
		button.tabIndex = 0;
		button.className = "rail-row"
			+ (room.selected ? " is-selected" : "")
			+ (room.joined ? " is-joined" : "")
			+ (joining ? " is-joining" : "")
			+ (isRootRoom ? " is-root-room" : "");
		button.classList.toggle("has-subtitle", !!subtitleText);
		button.classList.toggle("has-unread", unreadCount > 0);
		button.classList.toggle("is-populated", joinable && memberCount > 0);
		button.dataset.scopeToken = room.token || "";
		button.dataset.canJoin = joinable && !room.joined && !joining ? "true" : "false";
		button.dataset.roomLabel = room.label || "";
		button.dataset.depth = String(depth);
		button.dataset.roomType = joinable ? "voice" : "text";
		button.draggable = !!joinable && !isRootRoom;
		button.setAttribute("aria-label", room.label || "Room");
		if (room.participantSession) {
			button.dataset.participantSession = String(room.participantSession);
		}
		button.title = roomPathLabel || room.description || "";

		const chip = document.createElement("span");
		chip.className = "kind-chip";
		chip.textContent = kindChipText(room.kindLabel);

		const copy = document.createElement("span");
		copy.className = "rail-row-copy";

		const title = document.createElement("span");
		title.className = "rail-row-title";
		title.textContent = room.label || "Room";

		const subtitle = document.createElement("span");
		subtitle.className = "rail-row-subtitle";
		subtitle.textContent = subtitleText;
		subtitle.classList.toggle("hidden", !subtitleText);

		copy.appendChild(title);
		copy.appendChild(subtitle);

		const meta = document.createElement("span");
		meta.className = "rail-row-meta";

		if (unreadCount > 0) {
			const unread = document.createElement("span");
			unread.className = "row-badge";
			unread.textContent = String(unreadCount);
			meta.appendChild(unread);
		}

		if (joinable && memberCount > 0) {
			const count = document.createElement("span");
			count.className = "row-count";
			count.classList.toggle("is-active-room", !!room.joined);
			count.textContent = String(memberCount);
			count.title = memberCount === 1 ? "1 person in room" : (String(memberCount) + " people in room");
			meta.appendChild(count);
		}

		if (joinable && screenShareVisible(screenShare) && screenShare.badgeLabel) {
			const shareBadge = document.createElement("span");
			shareBadge.className = "meta-pill row-share-badge"
				+ (screenShare.badgeTone ? " is-" + screenShare.badgeTone : "");
			shareBadge.textContent = screenShare.badgeLabel;
			shareBadge.title = screenShare.statusLabel || screenShare.badgeLabel;
			meta.appendChild(shareBadge);
		}

		if (joinable) {
			const joinButton = document.createElement("button");
			joinButton.type = "button";
			joinButton.className = "mini-action room-join-action" + (joining ? " is-loading" : "");
			joinButton.textContent = room.joined ? "Live" : (joining ? "Joining" : "Join");
			joinButton.disabled = !!room.joined || joining;
			joinButton.addEventListener("pointerdown", function(event) {
				if (event.button !== undefined && event.button !== 0) {
					return;
				}
				if (event.isPrimary === false) {
					return;
				}
				handleJoinButtonActivation(room, event);
			}, true);
			joinButton.addEventListener("mousedown", function(event) {
				if (event.button !== undefined && event.button !== 0) {
					return;
				}
				handleJoinButtonActivation(room, event);
			}, true);
			joinButton.addEventListener("click", function(event) {
				handleJoinButtonActivation(room, event);
			});
			meta.appendChild(joinButton);
		}

		if (joinable && room.joined && screenShareVisible(screenShare) && screenShare.primaryActionId) {
			const shareActionButton = document.createElement("button");
			shareActionButton.type = "button";
			shareActionButton.className = "mini-action room-share-action"
				+ (screenShare.primaryTone ? " is-" + screenShare.primaryTone : "");
			shareActionButton.textContent = compactScreenShareActionLabel(screenShare.primaryLabel);
			shareActionButton.disabled = screenShare.primaryEnabled === false;
			shareActionButton.title = screenShare.primaryHint || screenShare.primaryLabel || "Screen share";
			shareActionButton.addEventListener("pointerdown", function(event) {
				markRailActionIntent(room.token);
				event.stopPropagation();
			}, true);
			shareActionButton.addEventListener("click", function(event) {
				markRailActionIntent(room.token);
				event.preventDefault();
				event.stopPropagation();
				notifyBridge("invokeScopeAction", room.token, screenShare.primaryActionId);
				dismissCompactRailAfterAction();
			});
			meta.appendChild(shareActionButton);
		}

		if (hasRoomActions) {
			const roomActionButton = document.createElement("button");
			roomActionButton.type = "button";
			roomActionButton.className = "room-action-button";
			roomActionButton.title = "Room actions";
			roomActionButton.setAttribute("aria-label", "Room actions for " + (room.label || "room"));
			roomActionButton.innerHTML = "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"5\" cy=\"12\" r=\"1.8\"></circle><circle cx=\"12\" cy=\"12\" r=\"1.8\"></circle><circle cx=\"19\" cy=\"12\" r=\"1.8\"></circle></svg>";
			roomActionButton.addEventListener("pointerdown", function(event) {
				markRailActionIntent(room.token);
				event.stopPropagation();
			}, true);
			roomActionButton.addEventListener("click", function(event) {
				markRailActionIntent(room.token);
				event.preventDefault();
				event.stopPropagation();
				const items = buildRoomContextMenuItems(getSnapshot(), room, button).filter(function(item) {
					return !!item;
				});
				const bounds = roomActionButton.getBoundingClientRect();
				showContextMenu(items, bounds.left, bounds.bottom + contextMenuAnchorGap, {
					anchorBounds: bounds,
					horizontal: "left-of-anchor"
				});
			});
			meta.appendChild(roomActionButton);
		}

		button.appendChild(chip);
		button.appendChild(copy);
		button.appendChild(meta);
		button.addEventListener("pointerdown", function(event) {
			if (!joinable || room.joined || (event.button !== undefined && event.button !== 0)
					|| event.isPrimary === false || !isRoomJoinIntent(event, button)) {
				return;
			}
			handleJoinButtonActivation(room, event);
		}, true);
		button.addEventListener("mousedown", function(event) {
			if (window.PointerEvent || !joinable || room.joined || (event.button !== undefined && event.button !== 0)
					|| !isRoomJoinIntent(event, button)) {
				return;
			}
			handleJoinButtonActivation(room, event);
		}, true);
		button.addEventListener("click", function(event) {
			if (joinable && !room.joined && isRoomJoinIntent(event, button)) {
				handleJoinButtonActivation(room, event);
				return;
			}
			if (isRoomEmbeddedActionTarget(event.target, button) || shouldSuppressRailSelect(room.token)) {
				stopRoomActionEvent(event);
				return;
			}
			selectRoomScope(room.token);
			dismissCompactRailAfterAction();
		});
		button.addEventListener("keydown", function(event) {
			if (event.target !== button || (event.key !== "Enter" && event.key !== " ")) {
				return;
			}
			event.preventDefault();
			selectRoomScope(room.token);
			dismissCompactRailAfterAction();
		});
		button.addEventListener("dblclick", function(event) {
			if (isRoomEmbeddedActionTarget(event.target, button) || shouldSuppressRailSelect(room.token)) {
				stopRoomActionEvent(event);
				return;
			}
			if (!joinable) {
				return;
			}
			event.preventDefault();
			event.stopPropagation();
			markRailActionIntent(room.token);
			requestVoiceJoin(room.token);
			dismissCompactRailAfterAction();
		});
		button.addEventListener("dragstart", function(event) {
			if (!joinable || isRootRoom || !room.token) {
				event.preventDefault();
				return;
			}

			dragState = {
				kind: "room",
				scopeToken: room.token
			};
			button.classList.add("is-dragging");
			if (event.dataTransfer) {
				event.dataTransfer.effectAllowed = "move";
				event.dataTransfer.setData("text/plain", "room:" + room.token);
			}
		});
		button.addEventListener("dragend", function() {
			button.classList.remove("is-dragging");
			dragState = null;
			clearAllRoomDropTargets();
		});
		button.addEventListener("dragenter", handleRoomDragEnter);
		button.addEventListener("dragover", handleRoomDragOver);
		button.addEventListener("dragleave", clearRoomDropTarget);
		button.addEventListener("drop", function(event) {
			clearRoomDropTarget(event);
			if (!joinable || !dragState) {
				return;
			}

			event.preventDefault();
			if (dragState.kind === "participant" && dragState.session) {
				notifyBridge("moveParticipantToChannel", dragState.session, room.token);
			} else if (dragState.kind === "room" && dragState.scopeToken && dragState.scopeToken !== room.token) {
				notifyBridge("moveChannelToChannel",
					dragState.scopeToken,
					room.token,
					button.dataset.dropPlacement || roomDropPlacement(event, button));
			}
		});

		wrapper.appendChild(button);

		const roomParticipants = joinable
			? ((room.participants && room.participants.length) ? room.participants : ((room.joined && selectedVoicePresence) || []))
			: [];
		if (roomParticipants.length) {
			renderPresenceList(wrapper, room, roomParticipants);
		}

		return wrapper;
	}

	function renderRoomList(container, rooms, options) {
		const roomSection = container.closest(".room-card-block");
		const roomList = rooms || [];
		const hasRooms = roomList.length > 0;
		if (roomSection) {
			roomSection.classList.toggle("is-empty", !hasRooms);
			roomSection.classList.toggle("hidden", !!options.hideWhenEmpty && !hasRooms);
		}

		const fragment = document.createDocumentFragment();

		if (!hasRooms) {
			if (options.hideWhenEmpty) {
				replaceChildrenWith(container, fragment);
				return;
			}

			const empty = document.createElement("div");
			empty.className = "rail-empty";
			empty.textContent = options.emptyText || "Waiting for room state.";
			fragment.appendChild(empty);
			replaceChildrenWith(container, fragment);
			return;
		}

		if (options.rootLabel) {
			const rootLabel = document.createElement("div");
			rootLabel.className = "rail-root-label";
			rootLabel.textContent = options.rootLabel;
			rootLabel.dataset.dropTarget = "root";
			rootLabel.dataset.scopeToken = options.rootToken || "";
			if (options.joinable && options.rootToken) {
				rootLabel.addEventListener("dragenter", handleRoomDragEnter);
				rootLabel.addEventListener("dragover", handleRoomDragOver);
				rootLabel.addEventListener("dragleave", clearRoomDropTarget);
				rootLabel.addEventListener("drop", function(event) {
					clearRoomDropTarget(event);
					if (!dragState || dragState.kind !== "room" || !dragState.scopeToken) {
						return;
					}

					event.preventDefault();
					notifyBridge("moveChannelToChannel", dragState.scopeToken, options.rootToken, "inside");
				});
			}
			fragment.appendChild(rootLabel);
		}

		roomList.forEach(function(room) {
			fragment.appendChild(buildRoomRow(room, options.joinable, options.voicePresence));
		});
		replaceChildrenWith(container, fragment);
	}

	function mergeParticipantPatchList(previousList, nextList) {
		if (!Array.isArray(nextList)) {
			return previousList || [];
		}

		const previousByKey = new Map();
		(previousList || []).forEach(function(person) {
			const key = participantStateKey(person);
			if (key) {
				previousByKey.set(key, person);
			}
		});

		return nextList.map(function(person) {
			const key = participantStateKey(person);
			const previous = key ? previousByKey.get(key) : null;
			const merged = Object.assign({}, previous || {}, person || {});
			if (previous && !Object.prototype.hasOwnProperty.call(person || {}, "actions")) {
				merged.actions = previous.actions || [];
			}
			if (previous && !Object.prototype.hasOwnProperty.call(person || {}, "avatarUrl")) {
				merged.avatarUrl = previous.avatarUrl || "";
			}
			return merged;
		});
	}

	function mergeRoomPatchList(previousList, nextList) {
		if (!Array.isArray(nextList)) {
			return previousList || [];
		}

		const previousByToken = new Map();
		(previousList || []).forEach(function(room) {
			const token = String(room && room.token || "");
			if (token) {
				previousByToken.set(token, room);
			}
		});

		return nextList.map(function(room) {
			const token = String(room && room.token || "");
			const previous = token ? previousByToken.get(token) : null;
			const merged = Object.assign({}, previous || {}, room || {});
			if (previous && !Object.prototype.hasOwnProperty.call(room || {}, "actions")) {
				merged.actions = previous.actions || [];
			}
			if (previous && !Object.prototype.hasOwnProperty.call(room || {}, "participantActions")) {
				merged.participantActions = previous.participantActions || [];
			}
			if (Object.prototype.hasOwnProperty.call(room || {}, "participants")) {
				merged.participants = mergeParticipantPatchList(previous ? previous.participants : [], room.participants);
			}
			return merged;
		});
	}

	function renderRoomsPatch(snapshot) {
		const app = snapshot.app || {};
		const textRooms = snapshot.textRooms || [];
		const voiceRooms = snapshot.voiceRooms || [];
		const voicePresence = snapshot.voicePresence || [];

		refs.connectButton.disabled = !app.canConnect;
		refs.disconnectButton.disabled = !app.canDisconnect;
		refs.muteButton.classList.toggle("is-active", !!app.selfMuted);
		refs.deafButton.classList.toggle("is-active", !!app.selfDeafened);
		renderMenus(resolvedAppMenus(app));
		renderSelfCard(app);
		if (appMenuOpen) {
			renderAppMenu(snapshot);
		}
		if (selfMenuOpen) {
			renderSelfMenu(snapshot);
		}

		reconcilePendingVoiceJoin(snapshot);
		refs.textRoomCount.textContent = String(textRooms.length);
		refs.voiceRoomCount.textContent = String(voiceRooms.length);
		renderRoomList(refs.voiceRoomList, voiceRooms, {
			joinable: true,
			voicePresence: voicePresence,
			rootLabel: app.voiceRootLabel || "",
			rootToken: app.voiceRootScopeToken || ""
		});
		renderRoomList(refs.textRoomList, textRooms, {
			joinable: false,
			voicePresence: null,
			hideWhenEmpty: !(app.canManageTextChannels || app.canCreateTextRoom)
		});
		const renderedActiveRailToken = activeRailToken();
		if (!renderedActiveRailToken) {
			lastActiveRailToken = "";
		} else if (renderedActiveRailToken !== lastActiveRailToken) {
			lastActiveRailToken = renderedActiveRailToken;
			pendingActiveRailReveal = true;
		}
		scheduleRailLayoutSync();
	}

	function renderPresencePatch(snapshot) {
		const voicePresence = snapshot.voicePresence || [];
		const headerPresence = voicePresence.length ? voicePresence : (snapshot.participants || []);
		renderVoicePresenceStack(headerPresence);
		reconcilePendingVoiceJoin(snapshot);
		renderRoomList(refs.voiceRoomList, snapshot.voiceRooms || [], {
			joinable: true,
			voicePresence: voicePresence,
			rootLabel: (snapshot.app || {}).voiceRootLabel || "",
			rootToken: (snapshot.app || {}).voiceRootScopeToken || ""
		});
		scheduleRailLayoutSync();
	}

	function syncRoomSelectionState(rooms, scopeToken) {
		(rooms || []).forEach(function(room) {
			if (room) {
				room.selected = String(room.token || "") === scopeToken;
			}
		});
	}

	function renderRailSelectionPatch(snapshot) {
		const scopeToken = String(snapshot && snapshot.activeScope && snapshot.activeScope.scopeToken || "");
		syncRoomSelectionState(snapshot.textRooms, scopeToken);
		syncRoomSelectionState(snapshot.voiceRooms, scopeToken);
		refs.utilityScroll.querySelectorAll(".rail-row").forEach(function(row) {
			row.classList.toggle("is-selected", !!scopeToken && String(row.dataset.scopeToken || "") === scopeToken);
		});
		const renderedActiveRailToken = activeRailToken();
		if (!renderedActiveRailToken) {
			lastActiveRailToken = "";
		} else if (renderedActiveRailToken !== lastActiveRailToken) {
			lastActiveRailToken = renderedActiveRailToken;
			pendingActiveRailReveal = true;
		}
		scheduleRailLayoutSync();
	}

	function renderActiveScopePatch(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};

		renderRailSelectionPatch(snapshot);
		refs.serverEyebrow.textContent = app.serverEyebrow || scope.kindLabel || "Mumble";
		refs.scopeTitle.textContent = scope.label || "Modern Layout";
		refs.scopeDescription.textContent = scope.description || "Select a room to see shared history.";
		renderMeta(scope.meta || []);
		renderScreenShareHeader(scope, scope.screenShare || null);
		renderScreenShareCard(scope, scope.screenShare || null);
		refs.scopeBanner.textContent = scope.banner || "";
		refs.scopeBanner.classList.toggle("hidden", !scope.banner);
		refs.loadOlderButton.disabled = !scope.canLoadOlder;
		refs.markReadButton.disabled = !scope.canMarkRead;
		refs.composerInput.disabled = !scope.canSend;
		refs.attachButton.disabled = !scope.canAttachImages;
		refs.sendButton.disabled = !scope.canSend;
		refs.composerInput.placeholder = scope.composerPlaceholder || "Write a message";
		refs.composerHint.textContent = scope.composerHint || "Persistent room history stays with the selected room.";
		renderComposerReplyState(scope);
		syncAmbientState(snapshot);
		syncComposerHeight();
		if (shouldShowScopeLoading(scope, snapshot.messages || [])) {
			beginScopeLoading(scope.scopeToken, { force: true, fallback: false });
		}
		if (scope.serverLogRevision || Object.prototype.hasOwnProperty.call(scope, "serverLogHtml")) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
		}
		if (scope.autoMarkRead) {
			notifyBridge("markRead");
		}
	}

	function composerCanAttachImages() {
		const scope = getSnapshot().activeScope || {};
		return !!scope.canAttachImages && !refs.composerInput.disabled;
	}

	function isImageFile(file) {
		if (!file) {
			return false;
		}

		const mime = String(file.type || "").toLowerCase();
		if (mime.indexOf("image/") === 0) {
			return true;
		}

		return /\.(png|jpe?g|gif|webp|bmp)$/i.test(String(file.name || ""));
	}

	function imageFilesFromList(fileList) {
		return Array.from(fileList || []).filter(isImageFile);
	}

	function imageFilesFromDataTransfer(dataTransfer) {
		if (!dataTransfer) {
			return [];
		}

		const directFiles = imageFilesFromList(dataTransfer.files);
		if (directFiles.length) {
			return directFiles;
		}

		return Array.from(dataTransfer.items || []).map(function(item) {
			if (!item || item.kind !== "file" || String(item.type || "").toLowerCase().indexOf("image/") !== 0) {
				return null;
			}
			return typeof item.getAsFile === "function" ? item.getAsFile() : null;
		}).filter(isImageFile);
	}

	function hasImageFileTransfer(dataTransfer) {
		return imageFilesFromDataTransfer(dataTransfer).length > 0;
	}

	function clipboardTransferLooksImageLike(dataTransfer) {
		if (!dataTransfer) {
			return false;
		}

		if (hasImageFileTransfer(dataTransfer)) {
			return true;
		}

		return Array.from(dataTransfer.types || []).some(function(type) {
			const normalizedType = String(type || "").toLowerCase();
			return normalizedType === "files"
				|| normalizedType === "text/uri-list"
				|| normalizedType === "application/x-qt-image"
				|| normalizedType.indexOf("image/") === 0;
		});
	}

	function readFileAsDataUrl(file) {
		return new Promise(function(resolve, reject) {
			const reader = new FileReader();
			reader.onload = function() {
				resolve(String(reader.result || ""));
			};
			reader.onerror = function() {
				reject(reader.error || new Error("Unable to read image file"));
			};
			reader.readAsDataURL(file);
		});
	}

	function attachImageDataUrl(dataUrl) {
		const normalizedDataUrl = String(dataUrl || "");
		if (!normalizedDataUrl || normalizedDataUrl.indexOf("data:image/") !== 0) {
			return;
		}

		notifyBridge("attachImageData", normalizedDataUrl);
	}

	function attachImageFiles(files) {
		imageFilesFromList(files).forEach(function(file) {
			readFileAsDataUrl(file).then(function(dataUrl) {
				attachImageDataUrl(dataUrl);
			}).catch(function(error) {
				console.warn("Unable to attach image file:", error);
			});
		});
	}

	function setComposerImageDropTarget(active) {
		refs.composerShell.classList.toggle("is-image-drop-target", !!active);
	}

	function handleComposerImagePaste(event) {
		if (!composerCanAttachImages()) {
			return;
		}

		if (!clipboardTransferLooksImageLike(event.clipboardData)) {
			return;
		}

		event.preventDefault();
		notifyBridge("attachClipboardImage");
	}

	function handleComposerImageDragEnter(event) {
		if (!composerCanAttachImages() || dragState || !hasImageFileTransfer(event.dataTransfer)) {
			return;
		}

		event.preventDefault();
		setComposerImageDropTarget(true);
	}

	function handleComposerImageDragOver(event) {
		if (!composerCanAttachImages() || dragState || !hasImageFileTransfer(event.dataTransfer)) {
			return;
		}

		event.preventDefault();
		if (event.dataTransfer) {
			event.dataTransfer.dropEffect = "copy";
		}
		setComposerImageDropTarget(true);
	}

	function clearComposerImageDropTarget() {
		setComposerImageDropTarget(false);
	}

	function bridgeClipboardHasImage(callback) {
		if (!modernBridge || typeof modernBridge.clipboardHasImage !== "function") {
			callback(false);
			return;
		}

		try {
			modernBridge.clipboardHasImage(function(result) {
				callback(!!result);
			});
		} catch (error) {
			console.warn("Unable to query clipboard image state:", error);
			callback(false);
		}
	}

	function bridgeClipboardText(callback) {
		if (!modernBridge || typeof modernBridge.clipboardText !== "function") {
			callback("");
			return;
		}

		try {
			modernBridge.clipboardText(function(text) {
				callback(String(text || ""));
			});
		} catch (error) {
			console.warn("Unable to read clipboard text:", error);
			callback("");
		}
	}

	function pastePlainTextIntoComposer() {
		bridgeClipboardText(function(text) {
			if (text) {
				replaceComposerSelection(text);
			}
		});
		if (modernBridge && typeof modernBridge.clipboardText === "function") {
			return;
		}

		try {
			if (typeof document.execCommand === "function" && document.execCommand("paste")) {
				return;
			}
		} catch (error) {}

		if (navigator.clipboard && typeof navigator.clipboard.readText === "function") {
			navigator.clipboard.readText().then(function(text) {
				if (text) {
					replaceComposerSelection(text);
				}
			}).catch(function() {});
		} else {
			document.execCommand("paste");
		}
	}

	function handleComposerImageDrop(event) {
		if (!composerCanAttachImages() || dragState || !hasImageFileTransfer(event.dataTransfer)) {
			return;
		}

		event.preventDefault();
		clearComposerImageDropTarget();
		attachImageFiles(imageFilesFromDataTransfer(event.dataTransfer));
		refs.composerInput.focus();
	}

	function sendComposerDraft() {
		const value = refs.composerInput.value.trim();
		if (!value) {
			return false;
		}

		notifyBridge("sendMessage", value);
		refs.composerInput.value = "";
		syncComposerHeight();
		return true;
	}

	function handleComposerInputKeyDown(event) {
		const key = String(event.key || "");
		if ((key === "Enter" || key === "Return") && !event.shiftKey && !event.isComposing && event.keyCode !== 229) {
			event.preventDefault();
			sendComposerDraft();
			return;
		}

		if (!composerCanAttachImages()) {
			return;
		}

		const isPasteShortcut = (event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey
			&& String(event.key || "").toLowerCase() === "v";
		if (!isPasteShortcut) {
			return;
		}

		event.preventDefault();
		bridgeClipboardHasImage(function(hasImage) {
			if (hasImage) {
				notifyBridge("attachClipboardImage");
				return;
			}

			pastePlainTextIntoComposer();
		});
	}

	function renderSystemMessage(message) {
		const article = document.createElement("article");
		article.className = "system-message";
		article.dataset.messageKey = messageKey(message);
		article.dataset.messageId = String(message.messageId || "");
		article.dataset.threadId = String(message.threadId || "");
		article.innerHTML =
			"<span class=\"system-label\"></span><span class=\"system-time\"></span><div class=\"system-body\"></div>";
		article.querySelector(".system-label").textContent = message.actor || "System";
		article.querySelector(".system-time").textContent = message.timeLabel || "";
		article.querySelector(".system-body").innerHTML = message.bodyHtml || escapeHtml(message.bodyText || "");
		return article;
	}

	function appendReplyBlock(container, message) {
		if (!message.replyActor && !message.replySnippet) {
			return;
		}

		const reply = document.createElement("div");
		reply.className = "reply-block";
		reply.innerHTML =
			"<div class=\"reply-actor\"></div><div class=\"reply-snippet\"></div>";
		reply.querySelector(".reply-actor").textContent = message.replyActor || "Reply";
		reply.querySelector(".reply-snippet").textContent = message.replySnippet || "";
		container.appendChild(reply);
	}

	function reactionStateForEmoji(message, emoji) {
		const reactions = (message && message.reactions) || [];
		for (let index = 0; index < reactions.length; index += 1) {
			if (String(reactions[index].emoji || "") === String(emoji || "")) {
				return reactions[index];
			}
		}
		return null;
	}

	function previewHostLabel(url) {
		const rawUrl = String(url || "").trim();
		if (!rawUrl) {
			return "";
		}

		try {
			return String(new URL(rawUrl).hostname || "").replace(/^www\./i, "");
		} catch (error) {
			return "";
		}
	}

	function previewSourceLabel(preview, hostLabel) {
		const subtitle = String((preview && preview.subtitle) || "").trim();
		if (subtitle) {
			return subtitle;
		}
		if (hostLabel) {
			return hostLabel;
		}
		return "Link preview";
	}

	function previewDescriptionText(preview) {
		const description = String((preview && preview.description) || "").trim();
		if (description) {
			return description;
		}
		if (preview && preview.loading) {
			return "Loading preview...";
		}
		if (preview && preview.failed) {
			return "Preview unavailable";
		}
		return "";
	}

	function previewBadgeText(preview, sourceLabel, hostLabel) {
		if (hostLabel && hostLabel.toLowerCase() !== sourceLabel.toLowerCase()) {
			return hostLabel;
		}
		if (preview && preview.loading) {
			return "Loading";
		}
		if (preview && preview.failed) {
			return "Limited";
		}
		return "";
	}

	function previewPlaceholderMark(preview, hostLabel) {
		const fallbackSource = hostLabel
			|| String((preview && preview.subtitle) || "").trim()
			|| String((preview && preview.title) || "").trim()
			|| "Link";
		const simplified = fallbackSource
			.replace(/^www\./i, "")
			.replace(/\.[a-z0-9-]+$/i, "")
			.replace(/[^a-z0-9]+/ig, " ")
			.trim();
		const tokens = simplified ? simplified.split(/\s+/).filter(Boolean) : [];
		if (!tokens.length) {
			return preview && preview.kind === "image" ? "IMG" : "LN";
		}
		if (tokens.length === 1) {
			return tokens[0].slice(0, Math.min(2, tokens[0].length)).toUpperCase();
		}
		return (tokens[0][0] + tokens[1][0]).toUpperCase();
	}

	function renderPreviewCard(message) {
		const preview = message && message.preview;
		if (!preview || (!preview.url && !preview.title && !preview.description && !preview.thumbnailUrl && !preview.mediaUrl)) {
			return null;
		}
		const mediaUrl = String(preview.mediaUrl || "").trim();
		const mediaMime = String(preview.mediaMime || "").trim().toLowerCase();
		const hasPlayableMedia = !!mediaUrl;
		const isVideoMedia = hasPlayableMedia && (preview.kind === "video" || /^video\//i.test(mediaMime));
		const isGifMedia = hasPlayableMedia && (preview.kind === "gif" || mediaMime === "image/gif");
		const hasThumbnail = !!preview.thumbnailUrl;
		const hostLabel = previewHostLabel(preview.url);
		const sourceLabel = previewSourceLabel(preview, hostLabel);
		const badgeText = previewBadgeText(preview, sourceLabel, hostLabel);
		const descriptionText = previewDescriptionText(preview);

		const card = document.createElement(hasPlayableMedia ? "div" : "button");
		if (!hasPlayableMedia) {
			card.type = "button";
		} else {
			card.tabIndex = 0;
			card.setAttribute("role", "button");
		}
		card.className = "preview-card"
			+ (hasThumbnail ? " has-thumbnail" : "")
			+ (hasPlayableMedia ? " has-media" : "")
			+ (isVideoMedia ? " is-video" : "")
			+ (isGifMedia ? " is-gif" : "")
			+ (preview.loading ? " is-loading" : "")
			+ (preview.failed ? " is-failed" : "")
			+ (preview.kind === "image" ? " is-image" : "");
		card.title = preview.openLabel || "Open link";
		card.setAttribute("aria-label", preview.openLabel || "Open link");
		const activateCard = function(event) {
			event.preventDefault();
			event.stopPropagation();
			if (preview.url) {
				notifyBridge("activateLink", preview.url);
			}
		};
		card.addEventListener("click", activateCard);
		if (hasPlayableMedia) {
			card.addEventListener("keydown", function(event) {
				if (event.key === "Enter" || event.key === " ") {
					activateCard(event);
				}
			});
		}

		if (hasPlayableMedia) {
			const media = document.createElement("div");
			media.className = "preview-card-media preview-card-playback";
			if (isVideoMedia) {
				media.addEventListener("click", function(event) {
					event.stopPropagation();
				});
				const video = document.createElement("video");
				video.className = "preview-card-video";
				video.src = mediaUrl;
				video.controls = true;
				video.muted = true;
				video.loop = true;
				video.playsInline = true;
				video.preload = preview.autoplay ? "auto" : "metadata";
				if (preview.autoplay) {
					video.autoplay = true;
				}
				if (preview.thumbnailUrl) {
					video.poster = preview.thumbnailUrl;
				}
				video.setAttribute("aria-label", preview.title || preview.subtitle || "Media preview");
				media.appendChild(video);
			} else {
				const image = document.createElement("img");
				image.className = "preview-card-image preview-card-media-image";
				image.loading = "lazy";
				image.src = mediaUrl;
				image.alt = preview.title || preview.subtitle || "Media preview";
				media.appendChild(image);
			}
			card.appendChild(media);
		} else if (preview.thumbnailUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-media";
			const image = document.createElement("img");
			image.className = "preview-card-image";
			image.loading = "lazy";
			image.src = preview.thumbnailUrl;
			image.alt = preview.title || preview.subtitle || "Preview";
			media.appendChild(image);
			card.appendChild(media);
		} else {
			const placeholder = document.createElement("div");
			placeholder.className = "preview-card-media preview-card-media-placeholder";
			const placeholderMark = document.createElement("span");
			placeholderMark.className = "preview-card-placeholder-mark";
			placeholderMark.textContent = previewPlaceholderMark(preview, hostLabel);
			placeholder.appendChild(placeholderMark);

			const placeholderLabel = document.createElement("span");
			placeholderLabel.className = "preview-card-placeholder-label";
			placeholderLabel.textContent = preview.loading
				? "Loading preview"
				: (preview.failed ? "No image available" : "Link preview");
			placeholder.appendChild(placeholderLabel);
			card.appendChild(placeholder);
		}

		const copy = document.createElement("div");
		copy.className = "preview-card-copy";

		const meta = document.createElement("div");
		meta.className = "preview-card-meta";
		if (sourceLabel) {
			const subtitle = document.createElement("div");
			subtitle.className = "preview-card-subtitle";
			subtitle.textContent = sourceLabel;
			meta.appendChild(subtitle);
		}
		if (badgeText) {
			const badge = document.createElement("div");
			badge.className = "preview-card-badge";
			badge.textContent = badgeText;
			meta.appendChild(badge);
		}
		if (meta.childNodes.length) {
			copy.appendChild(meta);
		}

		const title = document.createElement("div");
		title.className = "preview-card-title";
		title.textContent = preview.title || preview.url || "Link preview";
		copy.appendChild(title);

		if (descriptionText) {
			const description = document.createElement("div");
			description.className = "preview-card-description";
			description.textContent = descriptionText;
			copy.appendChild(description);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-footer";
		const action = document.createElement("span");
		action.className = "preview-card-action";
		action.textContent = preview.openLabel || "Open link";
		footer.appendChild(action);
		copy.appendChild(footer);

		card.appendChild(copy);
		return card;
	}

	function renderReactionChip(message, reaction) {
		const button = document.createElement("button");
		button.type = "button";
		button.className = "reaction-chip" + (reaction.selfReacted ? " is-self" : "");
		button.innerHTML = "<span class=\"reaction-chip-emoji\"></span><span class=\"reaction-chip-count\"></span>";
		button.querySelector(".reaction-chip-emoji").textContent = reaction.emoji || "";
		button.querySelector(".reaction-chip-count").textContent = String(reaction.count || 0);
		button.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			openReactionPickerMessageId = null;
			notifyBridge("toggleReaction", message.messageId, reaction.emoji || "", !reaction.selfReacted);
		});
		return button;
	}

	function renderReactionPicker(message) {
		if (!message || !message.canReact || openReactionPickerMessageId !== message.messageId) {
			return null;
		}

		const picker = document.createElement("div");
		picker.className = "reaction-picker";
		starterReactionEmoji.forEach(function(emoji) {
			const reaction = reactionStateForEmoji(message, emoji);
			const button = document.createElement("button");
			button.type = "button";
			button.className = "reaction-picker-button" + (reaction && reaction.selfReacted ? " is-self" : "");
			button.textContent = emoji;
			button.title = reaction && reaction.selfReacted ? "Remove reaction" : "Add reaction";
			button.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				openReactionPickerMessageId = null;
				notifyBridge("toggleReaction", message.messageId, emoji, !(reaction && reaction.selfReacted));
			});
			picker.appendChild(button);
		});
		return picker;
	}

	function requestDeleteMessage(message) {
		if (!message || !message.canDelete) {
			return;
		}

		openReactionPickerMessageId = null;
		notifyBridge("deleteMessage", message.messageId);
	}

	function renderMessageFooter(message) {
		const reactions = (message && message.reactions) || [];
		const canReply = !!(message && message.canReply);
		const canReact = !!(message && message.canReact);
		const canDelete = !!(message && message.canDelete);
		if (!reactions.length && !canReply && !canReact && !canDelete) {
			return null;
		}

		const footer = document.createElement("div");
		footer.className = "bubble-footer";

		if (reactions.length) {
			const reactionRow = document.createElement("div");
			reactionRow.className = "reaction-row";
			reactions.forEach(function(reaction) {
				reactionRow.appendChild(renderReactionChip(message, reaction));
			});
			footer.appendChild(reactionRow);
		}

		if (canReply || canReact || canDelete) {
			const toolbar = document.createElement("div");
			toolbar.className = "bubble-toolbar";

			if (canReply) {
				const replyButton = document.createElement("button");
				replyButton.type = "button";
				replyButton.className = "bubble-toolbar-button";
				replyButton.textContent = "Reply";
				replyButton.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					openReactionPickerMessageId = null;
					notifyBridge("startReply", message.messageId);
				});
				toolbar.appendChild(replyButton);
			}

			if (canReact) {
				const reactionButton = document.createElement("button");
				reactionButton.type = "button";
				reactionButton.className = "icon-button bubble-toolbar-button bubble-toolbar-icon reaction-picker-toggle"
					+ (openReactionPickerMessageId === message.messageId ? " is-active" : "");
				reactionButton.innerHTML =
					"<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M22 11v1a10 10 0 1 1-9-10\"></path><path d=\"M8 14s1.5 2 4 2 4-2 4-2\"></path><path d=\"M9 9h.01\"></path><path d=\"M15 9h.01\"></path><path d=\"M16 5h6\"></path><path d=\"M19 2v6\"></path></svg>";
				reactionButton.title = "Add reaction";
				reactionButton.setAttribute("aria-label", "Add reaction");
				reactionButton.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					const willOpen = openReactionPickerMessageId !== message.messageId;
					openReactionPickerMessageId = willOpen ? message.messageId : null;
					if (willOpen) {
						pauseReactionPickerScrollClose();
					} else {
						reactionPickerScrollClosePausedUntil = 0;
					}
					syncSnapshot();
				});
				toolbar.appendChild(reactionButton);
			}

			if (canDelete) {
				const deleteButton = document.createElement("button");
				deleteButton.type = "button";
				deleteButton.className = "bubble-toolbar-button is-danger";
				deleteButton.textContent = "Delete";
				deleteButton.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					requestDeleteMessage(message);
				});
				toolbar.appendChild(deleteButton);
			}

			footer.appendChild(toolbar);
		}

		const picker = renderReactionPicker(message);
		if (picker) {
			footer.appendChild(picker);
		}

		return footer;
	}

	function renderMessageBubble(message) {
		const bubble = document.createElement("div");
		bubble.className = "message-bubble" + (message.own ? " is-own" : "");
		bubble.dataset.bodyText = message.bodyText || "";
		bubble.dataset.messageId = String(message.messageId || "");
		bubble.dataset.threadId = String(message.threadId || "");
		bubble.dataset.messageKey = messageKey(message);
		appendReplyBlock(bubble, message);

		const body = document.createElement("div");
		body.className = "bubble-copy";
		body.innerHTML = message.bodyHtml || escapeHtml(message.bodyText || "");
		bubble.appendChild(body);

		const previewCard = renderPreviewCard(message);
		if (previewCard) {
			bubble.appendChild(previewCard);
		}

		const footer = renderMessageFooter(message);
		if (footer) {
			bubble.appendChild(footer);
		}
		return bubble;
	}

	function messageKey(message) {
		if (!message) {
			return "";
		}

		if (message.messageId) {
			return "id:" + String(message.messageId);
		}

		return [
			String(message.createdAtMs || ""),
			String(message.actor || ""),
			String(message.bodyText || ""),
			message.system ? "system" : "message"
		].join("|");
	}

	function latestTailMessageKey(messages) {
		for (let index = (messages || []).length - 1; index >= 0; index -= 1) {
			const key = messageKey(messages[index]);
			if (key) {
				return key;
			}
		}

		return "";
	}

	function countFreshTailMessages(messages, previousTailKey) {
		if (!previousTailKey) {
			return 0;
		}

		let count = 0;
		for (let index = (messages || []).length - 1; index >= 0; index -= 1) {
			const message = messages[index];
			if (messageKey(message) === previousTailKey) {
				break;
			}
			if (!message.system) {
				count += 1;
			}
		}

		return count;
	}

	function renderMessageGroups(messages) {
		const groups = [];
		let currentGroup = null;
		let previousMessage = null;

		(messages || []).forEach(function(message) {
			const dayLabel = dayLabelFromMs(message.createdAtMs);
			const needsDayDivider = !previousMessage || dayLabelFromMs(previousMessage.createdAtMs) !== dayLabel;

			if (needsDayDivider && dayLabel) {
				groups.push({ type: "day", label: dayLabel });
			}

			if (message.system) {
				groups.push({ type: "system", message: message });
				currentGroup = null;
				previousMessage = message;
				return;
			}

			if (!currentGroup || !shouldGroupWith(previousMessage, message)) {
				currentGroup = {
					type: "cluster",
					own: !!message.own,
					actor: message.actor || "Unknown",
					messages: []
				};
				groups.push(currentGroup);
			}

			currentGroup.messages.push(message);
			previousMessage = message;
		});

		return groups;
	}

	function renderDayDivider(label) {
		const divider = document.createElement("div");
		divider.className = "day-divider";
		divider.dataset.dayLabel = label || "";
		divider.innerHTML = "<span></span><strong></strong><span></span>";
		divider.querySelector("strong").textContent = label || "";
		return divider;
	}

	function renderMessageCluster(group, freshStartIndex) {
		const firstMessage = group.messages[0];
		const cluster = document.createElement("section");
		cluster.className = "message-cluster" + (group.own ? " is-own" : "");
		cluster.dataset.actor = group.actor || "";
		cluster.dataset.own = group.own ? "true" : "false";
		cluster.dataset.dayLabel = dayLabelFromMs(firstMessage && firstMessage.createdAtMs);
		cluster.style.setProperty("--avatar-hue", String(hueForLabel(group.actor, group.own)));
		cluster.classList.toggle("is-fresh", Number.isFinite(freshStartIndex) && group.messages.some(function(message) {
			return message.renderIndex >= freshStartIndex;
		}));

		if (!group.own) {
			const avatar = document.createElement("div");
			avatar.className = "message-avatar";
			styleAvatar(avatar, group.actor, false, firstMessage.avatarUrl || "");
			cluster.appendChild(avatar);
		}

		const stack = document.createElement("div");
		stack.className = "message-stack";

		const meta = document.createElement("div");
		meta.className = "message-meta";
		meta.innerHTML =
			"<span class=\"message-author\"></span><span class=\"message-time\"></span>";
		meta.querySelector(".message-author").textContent = group.own ? "" : group.actor;
		meta.querySelector(".message-time").textContent =
			group.own
				? group.messages[group.messages.length - 1].timeLabel || ""
				: firstMessage.timeLabel || "";
		stack.appendChild(meta);

		group.messages.forEach(function(message) {
			stack.appendChild(renderMessageBubble(message));
		});

		cluster.appendChild(stack);
		return cluster;
	}

	function appendRenderedMessageGroup(fragment, group, freshStartIndex) {
		if (group.type === "day") {
			fragment.appendChild(renderDayDivider(group.label));
			return;
		}

		if (group.type === "system") {
			fragment.appendChild(renderSystemMessage(group.message));
			return;
		}

		fragment.appendChild(renderMessageCluster(group, freshStartIndex));
	}

	function cancelActiveMessageChunkRender(reason) {
		if (!activeMessageChunkRender) {
			return;
		}

		const startedAt = activeMessageChunkRender.startedAt || monotonicNow();
		activeMessageChunkRender = null;
		messageRenderGeneration += 1;
		traceModernUi("messages chunk cancel " + (reason || "render"), startedAt);
	}

	function renderTimeline(messages, emptyCopy, freshTailCount) {
		cancelActiveMessageChunkRender("sync");
		refs.messageList.classList.remove("is-chat-loading");
		const indexedMessages = (messages || []).map(function(message, index) {
			return Object.assign({ renderIndex: index }, message);
		});
		const groups = renderMessageGroups(indexedMessages);
		const fragment = document.createDocumentFragment();

		if (!groups.length) {
			const empty = document.createElement("div");
			empty.className = "empty-state";
			empty.innerHTML = "<h2>No history yet</h2><p></p>";
			empty.querySelector("p").textContent =
				emptyCopy || "Messages will appear here once the selected room has activity.";
			fragment.appendChild(empty);
			replaceChildrenWith(refs.messageList, fragment);
			return;
		}

		const freshStartIndex = Math.max(0, indexedMessages.length - Math.max(0, freshTailCount || 0));

		groups.forEach(function(group) {
			appendRenderedMessageGroup(fragment, group, freshStartIndex);
		});
		replaceChildrenWith(refs.messageList, fragment);
	}

	function applyTimelineRenderScrollState(renderState, complete) {
		if (!renderState) {
			return;
		}

		if (renderState.shouldStickToBottom) {
			keepMessageListPinnedToBottom = true;
			if (complete) {
				scheduleMessageListBottomPin(4);
			} else {
				refs.messageList.scrollTop = Math.max(0, refs.messageList.scrollHeight - refs.messageList.clientHeight);
			}
			return;
		}

		if (renderState.detachedBeforeRender) {
			refs.messageList.scrollTop = Math.max(0,
				refs.messageList.scrollHeight - refs.messageList.clientHeight - renderState.distanceFromBottom);
		}
		if (complete) {
			syncScrollState();
		}
	}

	function renderTimelineChunked(messages, emptyCopy, freshTailCount, renderState) {
		cancelActiveMessageChunkRender("restart");
		const startedAt = monotonicNow();
		const generation = messageRenderGeneration + 1;
		messageRenderGeneration = generation;
		activeMessageChunkRender = {
			generation: generation,
			startedAt: startedAt,
			preparing: true
		};

		const sourceMessages = messages || [];
		const shouldYieldBeforeRender = refs.messageList.classList.contains("is-chat-loading")
			|| sourceMessages.length >= messageRenderLoadingThreshold;
		if (shouldYieldBeforeRender && !refs.messageList.querySelector(".chat-loading-spinner")) {
			const loadingFragment = document.createDocumentFragment();
			loadingFragment.appendChild(createChatLoadingIndicator());
			refs.messageList.classList.add("is-chat-loading");
			replaceChildrenWith(refs.messageList, loadingFragment);
		} else if (!shouldYieldBeforeRender) {
			refs.messageList.classList.remove("is-chat-loading");
		}

		const beginChunkRender = function() {
			if (!activeMessageChunkRender || activeMessageChunkRender.generation !== generation
					|| messageRenderGeneration !== generation) {
				return;
			}

			const indexedMessages = sourceMessages.map(function(message, index) {
				return Object.assign({ renderIndex: index }, message);
			});
			const groups = renderMessageGroups(indexedMessages);
			const emptyFragment = document.createDocumentFragment();
			refs.messageList.classList.remove("is-chat-loading");
			replaceChildrenWith(refs.messageList, emptyFragment);

			if (!groups.length) {
				const fragment = document.createDocumentFragment();
				const empty = document.createElement("div");
				empty.className = "empty-state";
				empty.innerHTML = "<h2>No history yet</h2><p></p>";
				empty.querySelector("p").textContent =
					emptyCopy || "Messages will appear here once the selected room has activity.";
				fragment.appendChild(empty);
				replaceChildrenWith(refs.messageList, fragment);
				activeMessageChunkRender = null;
				applyTimelineRenderScrollState(renderState, true);
				if (renderState && typeof renderState.onComplete === "function") {
					renderState.onComplete();
				}
				traceModernUi("messages chunk empty", startedAt);
				return;
			}

			const freshStartIndex = Math.max(0, indexedMessages.length - Math.max(0, freshTailCount || 0));
			activeMessageChunkRender = {
				generation: generation,
				startedAt: startedAt
			};

			const appendChunk = function() {
				if (!activeMessageChunkRender || activeMessageChunkRender.generation !== generation
						|| messageRenderGeneration !== generation) {
					return;
				}

				const chunkStartedAt = monotonicNow();
				const fragment = document.createDocumentFragment();
				let renderedGroupCount = 0;
				while (renderState.nextGroupIndex < groups.length
						&& renderedGroupCount < messageRenderChunkGroupCount
						&& (renderedGroupCount === 0 || monotonicNow() - chunkStartedAt < messageRenderChunkBudgetMs)) {
					appendRenderedMessageGroup(fragment, groups[renderState.nextGroupIndex], freshStartIndex);
					renderState.nextGroupIndex += 1;
					renderedGroupCount += 1;
				}

				refs.messageList.appendChild(fragment);
				applyPendingMessageUpdatePatches(true);
				applyTimelineRenderScrollState(renderState, false);
				traceModernUi("messages chunk " + String(renderState.nextGroupIndex) + "/" + String(groups.length), chunkStartedAt);

				if (renderState.nextGroupIndex < groups.length) {
					requestAnimationFrame(appendChunk);
					return;
				}

				activeMessageChunkRender = null;
				applyPendingMessageUpdatePatches(false);
				applyTimelineRenderScrollState(renderState, true);
				if (typeof renderState.onComplete === "function") {
					renderState.onComplete();
				}
				traceModernUi("messages chunk complete", startedAt);
			};

			renderState.nextGroupIndex = 0;
			if (shouldYieldBeforeRender) {
				requestAnimationFrame(appendChunk);
				return;
			}

			appendChunk();
		};

		if (shouldYieldBeforeRender) {
			requestAnimationFrame(beginChunkRender);
			return;
		}

		beginChunkRender();
	}

	function lastTimelineCluster() {
		for (let index = refs.messageList.children.length - 1; index >= 0; index -= 1) {
			const child = refs.messageList.children[index];
			if (child && child.classList && child.classList.contains("message-cluster")) {
				return child;
			}
			if (child && child.classList && child.classList.contains("system-message")) {
				return null;
			}
		}
		return null;
	}

	function appendTimelineMessages(appendedMessages, previousMessages) {
		const incoming = appendedMessages || [];
		if (!incoming.length) {
			return true;
		}
		if (refs.messageList.querySelector(".message-log")) {
			return false;
		}

		refs.messageList.classList.remove("is-chat-loading");
		const loadingState = refs.messageList.querySelector(".chat-loading-indicator");
		if (loadingState && refs.messageList.children.length === 1) {
			refs.messageList.innerHTML = "";
		}
		const emptyState = refs.messageList.querySelector(".empty-state");
		if (emptyState && refs.messageList.children.length === 1) {
			refs.messageList.innerHTML = "";
		}

		let previousMessage = (previousMessages || [])[(previousMessages || []).length - 1] || null;
		incoming.forEach(function(rawMessage, offset) {
			const message = Object.assign({ renderIndex: (previousMessages || []).length + offset }, rawMessage);
			const dayLabel = dayLabelFromMs(message.createdAtMs);
			const needsDayDivider = !previousMessage || dayLabelFromMs(previousMessage.createdAtMs) !== dayLabel;

			if (needsDayDivider && dayLabel) {
				refs.messageList.appendChild(renderDayDivider(dayLabel));
			}

			if (message.system) {
				refs.messageList.appendChild(renderSystemMessage(message));
				previousMessage = message;
				return;
			}

			const lastCluster = !needsDayDivider ? lastTimelineCluster() : null;
			if (lastCluster && previousMessage && shouldGroupWith(previousMessage, message)
					&& lastCluster.dataset.actor === (message.actor || "Unknown")
					&& lastCluster.dataset.own === (message.own ? "true" : "false")) {
				const stack = lastCluster.querySelector(".message-stack");
				if (stack) {
					const metaTime = lastCluster.querySelector(".message-time");
					stack.appendChild(renderMessageBubble(message));
					lastCluster.classList.add("is-fresh");
					if (message.own && metaTime) {
						metaTime.textContent = message.timeLabel || "";
					}
					previousMessage = message;
					return;
				}
			}

			refs.messageList.appendChild(renderMessageCluster({
				type: "cluster",
				own: !!message.own,
				actor: message.actor || "Unknown",
				messages: [message]
			}, message.renderIndex));
			previousMessage = message;
		});
		return true;
	}

	function renderedMessageElement(message) {
		const id = String(message && message.messageId || "");
		const threadId = String(message && message.threadId || "");
		const key = messageKey(message);
		const elements = refs.messageList.querySelectorAll("[data-message-key]");
		for (let index = 0; index < elements.length; index += 1) {
			const element = elements[index];
			if (id && element.dataset.messageId === id
					&& (!threadId || !element.dataset.threadId || element.dataset.threadId === threadId)) {
				return element;
			}
			if (!id && key && element.dataset.messageKey === key) {
				return element;
			}
		}
		return null;
	}

	function replaceRenderedMessage(message) {
		const element = renderedMessageElement(message);
		if (!element) {
			return false;
		}

		const replacement = message.system ? renderSystemMessage(message) : renderMessageBubble(message);
		element.replaceWith(replacement);
		requestAnimationFrame(syncScrollState);
		return true;
	}

	function pendingMessageUpdateKey(message) {
		const id = String(message && message.messageId || "");
		const threadId = String(message && message.threadId || "");
		if (id) {
			return "id:" + id + ":" + threadId;
		}
		return "key:" + messageKey(message);
	}

	function queuePendingMessageUpdatePatch(message) {
		const key = pendingMessageUpdateKey(message);
		for (let index = 0; index < pendingMessageUpdatePatches.length; index += 1) {
			if (pendingMessageUpdatePatches[index].key === key) {
				pendingMessageUpdatePatches[index].message = message;
				return;
			}
		}

		pendingMessageUpdatePatches.push({
			key: key,
			message: message
		});
	}

	function applyPendingMessageUpdatePatches(onlyRenderedTargets) {
		if (!pendingMessageUpdatePatches.length) {
			return 0;
		}

		let appliedCount = 0;
		const remaining = [];
		pendingMessageUpdatePatches.forEach(function(update) {
			if (replaceRenderedMessage(update.message)) {
				appliedCount += 1;
				return;
			}
			remaining.push(update);
		});
		pendingMessageUpdatePatches = remaining;

		if (appliedCount > 0) {
			traceModernUi("messages update replay " + String(appliedCount), monotonicNow());
		}
		if (!onlyRenderedTargets && pendingMessageUpdatePatches.length) {
			traceModernUi("messages update pending " + String(pendingMessageUpdatePatches.length), monotonicNow());
		}
		return appliedCount;
	}

	function messageListMetrics() {
		const maxScrollTop = Math.max(0, refs.messageList.scrollHeight - refs.messageList.clientHeight);
		const scrollTop = Math.max(0, refs.messageList.scrollTop);
		const distanceFromBottom = Math.max(0, maxScrollTop - scrollTop);
		return {
			scrollTop: scrollTop,
			maxScrollTop: maxScrollTop,
			distanceFromBottom: distanceFromBottom,
			nearBottom: distanceFromBottom <= 28
		};
	}

	function syncJumpLatestButton(metrics) {
		const state = metrics || messageListMetrics();
		const shouldShow = state.maxScrollTop > 0 && !state.nearBottom;
		refs.jumpLatestButton.classList.toggle("hidden", !shouldShow);
		refs.jumpLatestButton.textContent = unreadDetachedMessages > 0
			? "Jump to latest (" + String(unreadDetachedMessages) + " new)"
			: "Jump to latest";
	}

	function syncScrollState() {
		const metrics = messageListMetrics();
		if (metrics.nearBottom) {
			unreadDetachedMessages = 0;
		}

		keepMessageListPinnedToBottom = metrics.nearBottom || metrics.maxScrollTop <= 0;

		refs.appShell.classList.toggle("chat-has-overflow", metrics.maxScrollTop > 0);
		refs.appShell.classList.toggle("chat-is-scrolled", metrics.scrollTop > 8);
		refs.appShell.classList.toggle("chat-is-detached", !metrics.nearBottom);
		syncJumpLatestButton(metrics);
		return metrics;
	}

	function scheduleMessageListBottomPin(frameCount) {
		pendingBottomPinFrames = Math.max(pendingBottomPinFrames, Math.max(1, Number(frameCount) || 1));
		if (pendingBottomPinHandle) {
			return;
		}

		const applyBottomPin = function() {
			pendingBottomPinHandle = 0;
			if (pendingBottomPinFrames <= 0) {
				return;
			}

			scrollMessageListToBottom("auto");
			pendingBottomPinFrames -= 1;
			if (pendingBottomPinFrames > 0 && keepMessageListPinnedToBottom) {
				pendingBottomPinHandle = requestAnimationFrame(applyBottomPin);
			}
		};

		pendingBottomPinHandle = requestAnimationFrame(applyBottomPin);
		window.setTimeout(function() {
			if (keepMessageListPinnedToBottom) {
				scrollMessageListToBottom("auto");
			}
		}, 120);
	}

	function refreshMessageListPinning(frameCount) {
		if (keepMessageListPinnedToBottom) {
			scheduleMessageListBottomPin(frameCount || 2);
			return;
		}

		requestAnimationFrame(syncScrollState);
	}

	function ensureMessageListObservers() {
		if (messageListMutationObserver || typeof MutationObserver !== "function") {
			return;
		}

		messageListMutationObserver = new MutationObserver(function() {
			refreshMessageListPinning(2);
		});
		messageListMutationObserver.observe(refs.messageList, {
			childList: true,
			subtree: true,
			characterData: true
		});
	}

	function scrollMessageListToBottom(behavior) {
		unreadDetachedMessages = 0;
		const targetTop = Math.max(0, refs.messageList.scrollHeight - refs.messageList.clientHeight);
		refs.messageList.scrollTop = targetTop;
		if (typeof refs.messageList.scrollTo === "function") {
			try {
				refs.messageList.scrollTo({
					top: targetTop,
					behavior: behavior || "smooth"
				});
			} catch (error) {
				refs.messageList.scrollTop = targetTop;
			}
		}
		requestAnimationFrame(syncScrollState);
	}

	function monotonicNow() {
		return window.performance && typeof window.performance.now === "function"
			? window.performance.now()
			: Date.now();
	}

	function modernUiTraceEnabled() {
		try {
			return window.localStorage
				&& window.localStorage.getItem("mumbleModernTrace") === "1";
		} catch (error) {
			return false;
		}
	}

	function traceModernUi(label, startedAt) {
		if (!modernUiTraceEnabled() || !window.console || typeof window.console.debug !== "function") {
			return;
		}

		window.console.debug("[modern-ui] " + label + " " + Math.round((monotonicNow() - startedAt) * 10) / 10 + "ms");
	}

	function serverLogPatchRevision(patch) {
		if (patch && Object.prototype.hasOwnProperty.call(patch, "serverLogRevision")) {
			return String(patch.serverLogRevision || "");
		}
		if (patch && patch.activeScope && Object.prototype.hasOwnProperty.call(patch.activeScope, "serverLogRevision")) {
			return String(patch.activeScope.serverLogRevision || "");
		}
		return "";
	}

	function serverLogPatchHtml(patch) {
		if (patch && Object.prototype.hasOwnProperty.call(patch, "serverLogHtml")) {
			return String(patch.serverLogHtml || "");
		}
		if (patch && Object.prototype.hasOwnProperty.call(patch, "html")) {
			return String(patch.html || "");
		}
		if (patch && patch.activeScope && Object.prototype.hasOwnProperty.call(patch.activeScope, "serverLogHtml")) {
			return String(patch.activeScope.serverLogHtml || "");
		}
		return "";
	}

	function serverLogNodesFromHtml(html) {
		const template = document.createElement("template");
		template.innerHTML = String(html || "");
		const body = template.content.querySelector("body");
		const source = body || template.content;
		const nodes = [];
		while (source.firstChild) {
			nodes.push(source.removeChild(source.firstChild));
		}
		return nodes.filter(function(node) {
			return node.nodeType !== Node.TEXT_NODE || String(node.textContent || "").trim();
		});
	}

	function serverLogPatchRows(patch) {
		if (!patch || typeof patch !== "object") {
			return [];
		}
		if (Array.isArray(patch.rows)) {
			return patch.rows;
		}
		if (Array.isArray(patch.entries)) {
			return patch.entries;
		}
		if (Array.isArray(patch.fragments)) {
			return patch.fragments;
		}
		if (Array.isArray(patch.messages)) {
			return patch.messages;
		}
		if (Object.prototype.hasOwnProperty.call(patch, "rowHtml")) {
			return [ { html: patch.rowHtml } ];
		}
		const html = serverLogPatchHtml(patch);
		return html ? serverLogNodesFromHtml(html).map(function(node) {
			const fragment = document.createDocumentFragment();
			fragment.appendChild(node);
			return {
				fragment: fragment
			};
		}) : [];
	}

	function appendServerLogRow(logElement, row, rowIndex, revision) {
		const wrapper = document.createElement("div");
		wrapper.className = "server-log-row";
		const rowObject = row && typeof row === "object" && !row.nodeType ? row : {};
		const stableKey = rowObject.key || rowObject.id || rowObject.revision || (String(revision || "local") + ":" + String(rowIndex));
		wrapper.dataset.serverLogKey = String(stableKey);

		if (rowObject.fragment) {
			wrapper.appendChild(rowObject.fragment);
		} else if (row && row.nodeType) {
			wrapper.appendChild(row);
		} else if (Object.prototype.hasOwnProperty.call(rowObject, "html")) {
			serverLogNodesFromHtml(rowObject.html).forEach(function(node) {
				wrapper.appendChild(node);
			});
		} else {
			wrapper.textContent = String(Object.prototype.hasOwnProperty.call(rowObject, "text") ? rowObject.text : row);
		}

		logElement.appendChild(wrapper);
	}

	function ensureServerLogElement() {
		let logElement = refs.messageList.querySelector(".message-log");
		if (logElement) {
			cachedServerLogElement = logElement;
			return logElement;
		}

		logElement = cachedServerLogElement;
		if (!logElement) {
			logElement = document.createElement("div");
			logElement.className = "message-log";
		}
		cachedServerLogElement = logElement;
		const fragment = document.createDocumentFragment();
		fragment.appendChild(logElement);
		replaceChildrenWith(refs.messageList, fragment);
		return logElement;
	}

	function applyServerLogAppendPatch(snapshot, patch) {
		if (!patchScopeMatches(snapshot, patch)) {
			return true;
		}

		cancelActiveMessageChunkRender("server-log-append");
		const startedAt = monotonicNow();
		const hasAppendPayload = patch && (
			Array.isArray(patch.rows)
			|| Array.isArray(patch.entries)
			|| Array.isArray(patch.fragments)
			|| Array.isArray(patch.messages)
			|| Object.prototype.hasOwnProperty.call(patch, "rowHtml")
			|| Object.prototype.hasOwnProperty.call(patch, "html")
			|| Object.prototype.hasOwnProperty.call(patch, "serverLogHtml"));
		if (!hasAppendPayload && patch && patch.activeScope
				&& Object.prototype.hasOwnProperty.call(patch.activeScope, "serverLogHtml")) {
			return applyServerLogResetPatch(snapshot, patch);
		}

		const rows = serverLogPatchRows(patch);
		if (!rows.length) {
			return true;
		}

		const scope = snapshot.activeScope || {};
		const scopeToken = String(scope.scopeToken || lastScopeToken || "");
		if (pendingScopeLoadingBlocksRender(scopeToken, { resolvePendingScopeLoading: true })) {
			return true;
		}
		refs.messageList.classList.remove("is-chat-loading");
		const metricsBefore = messageListMetrics();
		const detachedBeforeRender = !metricsBefore.nearBottom;
		const distanceFromBottom = metricsBefore.distanceFromBottom;
		const logElement = ensureServerLogElement();
		const revision = serverLogPatchRevision(patch) || String(scope.serverLogRevision || cachedServerLogRevision || "");
		const firstRowIndex = logElement.children.length;
		rows.forEach(function(row, index) {
			appendServerLogRow(logElement, row, firstRowIndex + index, revision);
		});
		cachedServerLogElement = logElement;
		if (revision) {
			cachedServerLogRevision = revision;
			scope.serverLogRevision = revision;
		}
		if (detachedBeforeRender) {
			unreadDetachedMessages += rows.length;
		}

		requestAnimationFrame(function() {
			if (!detachedBeforeRender || keepMessageListPinnedToBottom) {
				keepMessageListPinnedToBottom = true;
				scheduleMessageListBottomPin(3);
				return;
			}

			refs.messageList.scrollTop = Math.max(0,
				refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
			syncScrollState();
		});
		lastRenderedMessageCount = 0;
		lastRenderedTailKey = "";
		lastScopeToken = scopeToken;
		traceModernUi("serverLog.append " + String(rows.length), startedAt);
		return true;
	}

	function applyServerLogResetPatch(snapshot, patch) {
		if (!patchScopeMatches(snapshot, patch)) {
			return true;
		}

		cancelActiveMessageChunkRender("server-log-reset");
		const startedAt = monotonicNow();
		const scope = snapshot.activeScope || {};
		const revision = serverLogPatchRevision(patch) || String(scope.serverLogRevision || "");
		const html = serverLogPatchHtml(patch);
		const rows = serverLogPatchRows(patch);

		cachedServerLogElement = document.createElement("div");
		cachedServerLogElement.className = "message-log";
		if (rows.length && !html) {
			rows.forEach(function(row, index) {
				appendServerLogRow(cachedServerLogElement, row, index, revision);
			});
		} else {
			cachedServerLogElement.innerHTML = html;
		}
		cachedServerLogRevision = revision;
		if (revision) {
			scope.serverLogRevision = revision;
		}
		if (html) {
			scope.serverLogHtml = html;
		}
		renderMessages(snapshot, { forceSync: true, resolvePendingScopeLoading: true });
		traceModernUi("serverLog.reset", startedAt);
		return true;
	}

	function pauseReactionPickerScrollClose() {
		reactionPickerScrollClosePausedUntil = monotonicNow() + reactionPickerScrollCloseGraceMs;
	}

	function shouldKeepReactionPickerOpenOnScroll() {
		return openReactionPickerMessageId !== null && monotonicNow() <= reactionPickerScrollClosePausedUntil;
	}

	function renderMessages(snapshot, options) {
		const renderOptions = options || {};
		const scope = snapshot.activeScope || {};
		const messages = snapshot.messages || [];
		const scopeToken = scope.scopeToken || [
			String(scope.kindLabel || ""),
			String(scope.label || ""),
			String(scope.description || "")
		].join("|");
		if (shouldShowScopeLoading(scope, messages)) {
			pendingScopeLoading = { scopeToken: String(scopeToken || "") };
			clearPendingScopeLoadingTimer();
			showChatLoadingIndicator();
			lastRenderedMessageCount = 0;
			lastRenderedTailKey = "";
			return;
		}
		if (pendingScopeLoadingBlocksRender(scopeToken, renderOptions)) {
			return;
		}
		refs.messageList.classList.remove("is-chat-loading");
		const scopeChanged = scopeToken !== lastScopeToken;
		const metricsBefore = messageListMetrics();
		const detachedBeforeRender = !scopeChanged && !metricsBefore.nearBottom;
		const distanceFromBottom = metricsBefore.distanceFromBottom;
		const latestTailKey = latestTailMessageKey(messages);
		const previousTailKey = scopeChanged ? "" : lastRenderedTailKey;
		const freshTailCount = latestTailKey && latestTailKey !== previousTailKey
			? countFreshTailMessages(messages, previousTailKey)
			: 0;

		if (scopeChanged) {
			unreadDetachedMessages = 0;
			openReactionPickerMessageId = null;
		}
		if (openReactionPickerMessageId !== null && !messages.some(function(message) {
			return message && message.messageId === openReactionPickerMessageId;
		})) {
			openReactionPickerMessageId = null;
		}

		if (detachedBeforeRender && freshTailCount > 0) {
			unreadDetachedMessages += freshTailCount;
		}

		if (scope.serverLogRevision || Object.prototype.hasOwnProperty.call(scope, "serverLogHtml")) {
			cancelActiveMessageChunkRender("server-log");
			const serverLogRevision = String(scope.serverLogRevision || "");
			if (Object.prototype.hasOwnProperty.call(scope, "serverLogHtml")) {
				cachedServerLogElement = document.createElement("div");
				cachedServerLogElement.className = "message-log";
				cachedServerLogElement.innerHTML = scope.serverLogHtml || "";
				cachedServerLogRevision = serverLogRevision;
			}

			const fragment = document.createDocumentFragment();
			if (cachedServerLogElement && (!serverLogRevision || cachedServerLogRevision === serverLogRevision)) {
				refs.messageList.classList.remove("is-chat-loading");
				fragment.appendChild(cachedServerLogElement);
			} else {
				refs.messageList.classList.add("is-chat-loading");
				fragment.appendChild(createChatLoadingIndicator());
			}
			replaceChildrenWith(refs.messageList, fragment);
			requestAnimationFrame(function() {
				if (!detachedBeforeRender) {
					keepMessageListPinnedToBottom = true;
					scheduleMessageListBottomPin(3);
					return;
				}

				if (detachedBeforeRender) {
					refs.messageList.scrollTop = Math.max(0,
						refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
				}
				syncScrollState();
			});
			lastRenderedMessageCount = 0;
			lastRenderedTailKey = "";
			lastScopeToken = scopeToken;
			return;
		}

		const shouldStickToBottom = (scope.scrollToBottom !== false)
			&& (scopeChanged || (!detachedBeforeRender && messages.length >= lastRenderedMessageCount));
		const finishRender = function() {
			lastRenderedMessageCount = messages.length;
			lastRenderedTailKey = latestTailKey;
			lastScopeToken = scopeToken;
		};

		if (renderOptions.forceSync) {
			renderTimeline(messages, scope.emptyCopy || "", freshTailCount);
			requestAnimationFrame(function() {
				if (shouldStickToBottom) {
					keepMessageListPinnedToBottom = true;
					scheduleMessageListBottomPin(4);
					return;
				}

				if (detachedBeforeRender) {
					refs.messageList.scrollTop = Math.max(0,
						refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
				}
				syncScrollState();
			});
			finishRender();
			return;
		}

		pendingMessageUpdatePatches = [];
		renderTimelineChunked(messages, scope.emptyCopy || "", freshTailCount, {
			detachedBeforeRender: detachedBeforeRender,
			distanceFromBottom: distanceFromBottom,
			shouldStickToBottom: shouldStickToBottom,
			onComplete: finishRender
		});
	}

	function renderNote(app, scope) {
		const motdHtml = String(app.motdHtml || "").trim();
		const motdSummary = String(app.motdSummary || "").trim();
		const hasMotd = !!motdHtml;
		refs.noteCard.classList.toggle("hidden", !hasMotd);
		if (!hasMotd) {
			noteExpanded = true;
			railNoteAutoCollapsed = false;
			lastMotdSignature = "";
			refs.noteCard.style.maxHeight = "";
			refs.serverSubtitle.style.maxHeight = "";
			refs.noteCard.classList.remove("is-expanded", "has-rich-note");
			refs.noteToggleButton.classList.add("hidden");
			refs.noteToggleButton.setAttribute("aria-expanded", "false");
			return;
		}

		const preferredExpanded = app.motdExpanded !== false;
		if (lastMotdSignature !== motdHtml) {
			lastMotdSignature = motdHtml;
			railNoteAutoCollapsed = false;
		}
		noteExpanded = preferredExpanded;

		const fullText = plainTextFromHtml(motdHtml);
		const hasSummary = !!motdSummary && motdSummary !== fullText;
		const hasRichMotdLayout = /<(img|svg|table|h[1-6]|font|center|div|p|span)\b/i.test(motdHtml);
		const isExpandable = fullText.length > 0;
		const expanded = noteExpanded;

		refs.serverTitle.textContent = app.serverTitle || "Mumble";
		refs.noteCard.classList.toggle("is-expanded", expanded);
		refs.noteCard.classList.toggle("has-rich-note", hasRichMotdLayout);
		refs.noteToggleButton.classList.toggle("hidden", !isExpandable);
		refs.noteToggleButton.setAttribute("aria-expanded", expanded ? "true" : "false");
		refs.noteToggleButton.textContent = expanded ? "Hide" : "Show";
		refs.noteToggleButton.title = expanded ? "Hide message of the day" : "Show the full message of the day";

		if (expanded) {
			refs.serverSubtitle.innerHTML = "<div class=\"note-body-content\">" + motdHtml + "</div>";
			refs.serverSubtitle.classList.remove("is-collapsed");
			refs.serverSubtitle.title = "";
		} else {
			refs.serverSubtitle.innerHTML = hasSummary ? escapedMultilineText(motdSummary) : motdHtml;
			refs.serverSubtitle.classList.add("is-collapsed");
			refs.serverSubtitle.title = fullText;
		}

		scheduleRailLayoutSync();
	}

	function renderSelfCard(app) {
		refs.selfAvatar.className = "self-avatar";
		styleAvatar(refs.selfAvatar, app.selfName || "You", true, app.selfAvatarUrl || "");
		refs.selfName.textContent = app.selfName || "You";
		refs.selfStatus.textContent = app.selfStatusLabel || "Offline";
		refs.selfStatus.className = "self-status";
		if (app.selfStatusTone) {
			refs.selfStatus.classList.add("is-" + app.selfStatusTone);
		}
		if (app.selfStatusTone) {
			refs.selfAvatar.classList.add("is-" + app.selfStatusTone);
		}
		refs.selfCard.dataset.statusTone = app.selfStatusTone || "";
		refs.selfCard.setAttribute("aria-expanded", selfMenuOpen ? "true" : "false");
	}

	function fallbackMenus(app) {
		return [
			{
				id: "server",
				label: "Server",
				items: [
					{ id: "server.connect", label: "Connect", enabled: !!app.canConnect },
					{ id: "server.disconnect", label: "Disconnect", enabled: !!app.canDisconnect, tone: "danger" },
					{ id: "server.createRoom", label: "Create text room", enabled: !!app.canCreateTextRoom },
					{ id: "server.settings", label: "Server settings", enabled: !!app.canManageTextChannels },
					{ id: "server.information", label: "Server info", enabled: !!app.canDisconnect },
					{ id: "server.favorite", label: "Add favorite", enabled: !!app.canDisconnect }
				]
			},
			{
				id: "self",
				label: "Self",
				items: [
					{ id: "self.comment", label: "Comment", enabled: !!app.canDisconnect },
					{ id: "self.register", label: "Register", enabled: !!app.canDisconnect },
					{ id: "self.prioritySpeaker", label: "Priority speaker", enabled: !!app.canDisconnect },
					{ id: "self.audioStats", label: "Audio stats", enabled: true }
				]
			},
			{
				id: "configure",
				label: "Configure",
				items: [
					{ id: "configure.settings", label: "Settings", enabled: true },
					{ id: "configure.screenShare", label: "Screen sharing settings", enabled: true },
					{ id: "configure.audioWizard", label: "Audio setup", enabled: true },
					{ id: "configure.certificate", label: "Certificate wizard", enabled: true },
					{ id: "configure.minimal", label: "Minimal view", enabled: true },
					{ id: "configure.hideFrame", label: "Hide native window border", enabled: true }
				]
			},
			{
				id: "help",
				label: "Help",
				items: [
					{ id: "help.whatsThis", label: "What's this", enabled: true },
					{ id: "help.versionCheck", label: "Check for updates", enabled: true },
					{ id: "help.about", label: "About Mumble", enabled: true },
					{ id: "help.aboutQt", label: "About Qt", enabled: true }
				]
			}
		];
	}

	function resolvedAppMenus(app) {
		if (app && Array.isArray(app.menus) && app.menus.length) {
			return app.menus;
		}

		return fallbackMenus(app || {});
	}

	function appMenuContextItems(snapshot, menuIds, blockedActionIds) {
		const app = (snapshot && snapshot.app) || {};
		const allowedMenuIds = Array.isArray(menuIds) && menuIds.length
			? menuIds.reduce(function(ids, id) {
				ids[String(id || "")] = true;
				return ids;
			}, {})
			: null;
		const items = [];

		resolvedAppMenus(app).forEach(function(menu) {
			if (!menu || !Array.isArray(menu.items) || !menu.items.length) {
				return;
			}

			const menuId = String(menu.id || "");
			if (allowedMenuIds && !allowedMenuIds[menuId]) {
				return;
			}

			const menuItems = actionItemsFromActionStates(menu.items, {
				invokeAction: function(actionId) {
					notifyBridge("invokeAppAction", actionId);
				}
			}).filter(function(item) {
				if (!actionPanelItemKind(item)) {
					return false;
				}
				return !(blockedActionIds && item.id && blockedActionIds[item.id]);
			});
			if (!menuItems.length) {
				return;
			}

			items.push({
				kind: "submenu",
				label: menu.label || "Menu",
				items: menuItems
			});
		});

		return items;
	}

	function contextActionIdSet(items) {
		return (items || []).reduce(function(ids, item) {
			if (item && item.id) {
				ids[item.id] = true;
			}
			return ids;
		}, {});
	}

	function withAppMenuContextItems(items, snapshot, menuIds) {
		const mergedItems = (items || []).slice();
		const appItems = appMenuContextItems(snapshot, menuIds, contextActionIdSet(mergedItems));
		if (appItems.length) {
			if (mergedItems.length) {
				mergedItems.push({ separator: true });
			}
			mergedItems.push.apply(mergedItems, appItems);
		}
		return mergedItems;
	}

	function syncModernDialogState(state) {
		if (!renderContainedModernDialogs) {
			modernDialogState = null;
			renderModernDialog();
			return;
		}

		modernDialogState = state || null;
		renderModernDialog();
	}

	function modernDialogFocusableElements() {
		if (!refs.modernDialog) {
			return [];
		}

		return Array.prototype.slice.call(refs.modernDialog.querySelectorAll(
			"button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex='-1'])"
		)).filter(function(element) {
			return !!(element.offsetWidth || element.offsetHeight || element.getClientRects().length);
		});
	}

	function restoreModernDialogFocus(focusState) {
		if (!focusState || !focusState.fieldId || !refs.modernDialog) {
			return false;
		}

		const candidates = Array.prototype.slice.call(
			refs.modernDialog.querySelectorAll("[data-modern-dialog-field-id]")
		);
		const target = candidates.find(function(element) {
			return element.dataset.modernDialogFieldId === focusState.fieldId;
		});
		if (!target) {
			return false;
		}

		target.focus({ preventScroll: true });
		if (focusState.hasSelection && typeof target.setSelectionRange === "function") {
			try {
				target.setSelectionRange(focusState.selectionStart, focusState.selectionEnd);
			} catch (error) {
				// Some input types expose selection APIs but reject range restoration.
			}
		}
		return true;
	}

	function focusFirstModernDialogControl() {
		const focusable = modernDialogFocusableElements();
		const firstField = focusable.find(function(element) {
			return !!element.dataset.modernDialogFieldId;
		});
		const target = firstField || focusable[0] || refs.modernDialog;
		if (target && typeof target.focus === "function") {
			target.focus({ preventScroll: true });
		}
	}

	function restoreModernDialogReturnFocus() {
		const target = modernDialogReturnFocus;
		modernDialogReturnFocus = null;
		if (target && typeof target.focus === "function" && document.contains(target)) {
			target.focus({ preventScroll: true });
		}
	}

	function trapModernDialogTab(event) {
		const focusable = modernDialogFocusableElements();
		if (!focusable.length) {
			event.preventDefault();
			if (refs.modernDialog && typeof refs.modernDialog.focus === "function") {
				refs.modernDialog.focus({ preventScroll: true });
			}
			return true;
		}

		const first = focusable[0];
		const last = focusable[focusable.length - 1];
		if (event.shiftKey && document.activeElement === first) {
			event.preventDefault();
			last.focus({ preventScroll: true });
			return true;
		}
		if (!event.shiftKey && document.activeElement === last) {
			event.preventDefault();
			first.focus({ preventScroll: true });
			return true;
		}
		return false;
	}

	function invokeModernDialogAction(actionId, payload) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (!dialogId || !actionId) {
			return;
		}
		notifyBridge("invokeModernDialogAction", dialogId, String(actionId), payload || {});
	}

	function closeModernDialog() {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (dialogId) {
			notifyBridge("closeModernDialog", dialogId);
		}
		modernDialogState = null;
		renderModernDialog();
	}

	function modernDialogFieldValue(field, input) {
		const type = String(field && field.type || "text");
		if (type === "checkbox") {
			return !!input.checked;
		}
		if (type === "select" && String(field && field.valueType || "number") === "string") {
			return input.value;
		}
		if (type === "number" || type === "range" || type === "select") {
			const numeric = Number(input.value);
			return Number.isFinite(numeric) ? numeric : 0;
		}
		return input.value;
	}

	function updateModernDialogField(field, input) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		const fieldId = String(field && field.id || "");
		if (!dialogId || !fieldId) {
			return;
		}
		notifyBridge("updateModernDialogField", dialogId, fieldId, modernDialogFieldValue(field, input));
	}

	function updateModernDialogFieldValue(fieldId, value) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		fieldId = String(fieldId || "");
		if (!dialogId || !fieldId) {
			return;
		}
		notifyBridge("updateModernDialogField", dialogId, fieldId, value);
	}

	function rememberModernDialogFieldValue(fieldId, value) {
		if (!modernDialogState || !Array.isArray(modernDialogState.sections)) {
			return;
		}
		modernDialogState.sections.forEach(function(section) {
			(section.fields || []).forEach(function(field) {
				if (String(field.id || "") === String(fieldId || "")) {
					field.value = value;
				}
			});
		});
	}

	function modernDialogOptionHint(option) {
		return String(option && (option.tooltip || option.hint) || "");
	}

	function syncModernDialogSelectHint(input, fieldTooltip) {
		const selectedOption = input && input.options ? input.options[input.selectedIndex] : null;
		const optionHint = selectedOption ? String(selectedOption.dataset.optionHint || selectedOption.title || "") : "";
		const titleParts = [];
		if (fieldTooltip) {
			titleParts.push(fieldTooltip);
		}
		if (optionHint) {
			titleParts.push(optionHint);
		}
		if (titleParts.length) {
			input.title = titleParts.join("\n\n");
		} else {
			input.removeAttribute("title");
		}
	}

	function selectedModernDialogSelectOption(select) {
		if (!select || !select.options || select.selectedIndex < 0) {
			return null;
		}
		return select.options[select.selectedIndex] || null;
	}

	function syncModernDialogSelectShell(shell) {
		if (!shell) {
			return;
		}
		const select = shell.querySelector("select");
		const button = shell.querySelector(".modern-select-button");
		const label = shell.querySelector(".modern-select-label");
		if (!select || !button || !label) {
			return;
		}
		const selectedOption = selectedModernDialogSelectOption(select);
		label.textContent = selectedOption ? selectedOption.textContent : "";
		button.disabled = select.disabled;
		shell.classList.toggle("is-disabled", select.disabled);
		const title = select.title || (selectedOption ? selectedOption.title : "");
		if (title) {
			button.title = title;
		} else {
			button.removeAttribute("title");
		}
	}

	function closeModernDialogSelect() {
		const state = modernDialogSelectState;
		if (!state) {
			return;
		}
		state.shell.classList.remove("is-open");
		state.button.setAttribute("aria-expanded", "false");
		if (state.menu.parentNode) {
			state.menu.parentNode.removeChild(state.menu);
		}
		modernDialogSelectState = null;
	}

	function positionModernDialogSelectMenu() {
		const state = modernDialogSelectState;
		if (!state || !document.body.contains(state.shell)) {
			closeModernDialogSelect();
			return;
		}
		const bounds = state.button.getBoundingClientRect();
		const viewportWidth = window.innerWidth || document.documentElement.clientWidth || 0;
		const viewportHeight = window.innerHeight || document.documentElement.clientHeight || 0;
		const width = Math.max(140, bounds.width);
		const left = Math.max(8, Math.min(bounds.left, viewportWidth - width - 8));
		const spaceBelow = viewportHeight - bounds.bottom - 8;
		const spaceAbove = bounds.top - 8;
		const openAbove = spaceBelow < 150 && spaceAbove > spaceBelow;
		const available = Math.max(76, openAbove ? spaceAbove : spaceBelow);
		const maxHeight = Math.min(260, available - 4);
		state.menu.style.left = left + "px";
		state.menu.style.width = width + "px";
		state.menu.style.maxHeight = Math.max(72, maxHeight) + "px";
		state.menu.style.top = (openAbove ? Math.max(8, bounds.top - Math.max(72, maxHeight) - 4) : bounds.bottom + 4) + "px";
	}

	function focusModernDialogSelectItem(menu, delta) {
		const items = Array.prototype.slice.call(menu.querySelectorAll(".modern-select-option:not(:disabled)"));
		if (!items.length) {
			return;
		}
		let index = items.indexOf(document.activeElement);
		if (index < 0) {
			index = items.findIndex(function(item) {
				return item.classList.contains("is-selected");
			});
		}
		index = Math.max(0, Math.min(items.length - 1, index + delta));
		items[index].focus({ preventScroll: true });
	}

	function openModernDialogSelect(shell) {
		const select = shell.querySelector("select");
		const button = shell.querySelector(".modern-select-button");
		if (!select || !button || select.disabled) {
			return;
		}
		if (modernDialogSelectState && modernDialogSelectState.shell === shell) {
			closeModernDialogSelect();
			return;
		}
		closeModernDialogSelect();

		const menu = document.createElement("div");
		menu.className = "modern-select-menu";
		menu.setAttribute("role", "listbox");
		Array.prototype.slice.call(select.options).forEach(function(option, index) {
			if (option.hidden) {
				return;
			}
			const item = document.createElement("button");
			item.type = "button";
			item.className = "modern-select-option" + (index === select.selectedIndex ? " is-selected" : "");
			item.setAttribute("role", "option");
			item.setAttribute("aria-selected", index === select.selectedIndex ? "true" : "false");
			item.disabled = option.disabled;
			item.textContent = option.textContent;
			if (option.title) {
				item.title = option.title;
			}
			item.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				if (option.disabled) {
					return;
				}
				select.selectedIndex = index;
				syncModernDialogSelectShell(shell);
				button.focus({ preventScroll: true });
				select.dispatchEvent(new Event("change", { bubbles: true }));
				closeModernDialogSelect();
			});
			menu.appendChild(item);
		});
		menu.addEventListener("click", function(event) {
			event.stopPropagation();
		});
		menu.addEventListener("keydown", function(event) {
			if (event.key === "Escape") {
				event.preventDefault();
				event.stopPropagation();
				closeModernDialogSelect();
				button.focus({ preventScroll: true });
				return;
			}
			if (event.key === "ArrowDown") {
				event.preventDefault();
				event.stopPropagation();
				focusModernDialogSelectItem(menu, 1);
				return;
			}
			if (event.key === "ArrowUp") {
				event.preventDefault();
				event.stopPropagation();
				focusModernDialogSelectItem(menu, -1);
				return;
			}
			if (event.key === "Home" || event.key === "End") {
				event.preventDefault();
				event.stopPropagation();
				focusModernDialogSelectItem(menu, event.key === "Home" ? -100000 : 100000);
				return;
			}
			if (event.key === "Tab") {
				closeModernDialogSelect();
			}
		});

		document.body.appendChild(menu);
		modernDialogSelectState = { shell: shell, select: select, button: button, menu: menu };
		shell.classList.add("is-open");
		button.setAttribute("aria-expanded", "true");
		positionModernDialogSelectMenu();
		const selectedItem = menu.querySelector(".modern-select-option.is-selected:not(:disabled)")
			|| menu.querySelector(".modern-select-option:not(:disabled)");
		if (selectedItem) {
			selectedItem.focus({ preventScroll: true });
		}
	}

	function enhanceModernDialogSelects(root) {
		if (!root) {
			return;
		}
		Array.prototype.slice.call(root.querySelectorAll("select")).forEach(function(select) {
			if (select.classList.contains("modern-select-native")) {
				return;
			}
			const shell = document.createElement("span");
			shell.className = "modern-select";
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-select-button";
			button.setAttribute("aria-haspopup", "listbox");
			button.setAttribute("aria-expanded", "false");
			const fieldId = select.dataset.modernDialogFieldId || "";
			if (fieldId) {
				button.dataset.modernDialogFieldId = fieldId;
				select.removeAttribute("data-modern-dialog-field-id");
			}

			const label = document.createElement("span");
			label.className = "modern-select-label";
			const arrow = document.createElement("span");
			arrow.className = "modern-select-arrow";
			button.appendChild(label);
			button.appendChild(arrow);

			select.classList.add("modern-select-native");
			select.tabIndex = -1;
			select.setAttribute("aria-hidden", "true");
			select.parentNode.insertBefore(shell, select);
			shell.appendChild(select);
			shell.appendChild(button);
			syncModernDialogSelectShell(shell);

			button.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				openModernDialogSelect(shell);
			});
			button.addEventListener("keydown", function(event) {
				if (event.key === "Enter" || event.key === " " || event.key === "ArrowDown" || event.key === "ArrowUp") {
					event.preventDefault();
					openModernDialogSelect(shell);
				}
			});
			select.addEventListener("change", function() {
				syncModernDialogSelectShell(shell);
			});
		});
	}

	function clampPercent(value) {
		const numeric = Number(value);
		if (!Number.isFinite(numeric)) {
			return 0;
		}
		return Math.max(0, Math.min(100, Math.round(numeric)));
	}

	function clampNumber(value, min, max, fallback) {
		const numeric = Number(value);
		if (!Number.isFinite(numeric)) {
			return fallback == null ? min : fallback;
		}
		return Math.max(min, Math.min(max, numeric));
	}

	function roundedStep(value, step) {
		const size = Math.max(1, Number(step) || 1);
		return Math.round(value / size) * size;
	}

	function setRangeProgress(input) {
		const min = Number(input.min || 0);
		const max = Number(input.max || 100);
		const value = Number(input.value || 0);
		const progress = max > min ? ((value - min) / (max - min)) * 100 : 0;
		input.style.setProperty("--range-progress", String(Math.max(0, Math.min(100, progress))) + "%");
	}

	function percentile(values, ratio) {
		if (!values.length) {
			return null;
		}
		const sorted = values.slice().sort(function(left, right) {
			return left - right;
		});
		const index = Math.max(0, Math.min(sorted.length - 1, Math.round((sorted.length - 1) * ratio)));
		return sorted[index];
	}

	function usableCalibrationSample(levels, phase) {
		const strongest = Math.max(
			levels.amplitude === null ? 0 : levels.amplitude,
			levels.signalToNoise === null ? 0 : levels.signalToNoise,
			levels.hybrid === null ? 0 : levels.hybrid
		);
		if (phase === "speech") {
			return strongest >= 2 || (levels.peakCleanMicDb !== null && levels.peakCleanMicDb > -55);
		}
		return strongest > 0 || levels.peakCleanMicDb === null || levels.peakCleanMicDb > -80;
	}

	function voiceMeterLevelFromPayload(element, meter) {
		const source = Number(element.dataset.vadSource || 0);
		const levels = audioMeterLevelsFromPayload(meter);
		const rawLevel = source === 1 ? levels.signalToNoise : (source === 2 ? levels.hybrid : levels.amplitude);
		return Number.isFinite(Number(rawLevel)) ? clampPercent(rawLevel) : null;
	}

	function audioMeterLevelsFromPayload(meter) {
		const amplitude = Number(meter && meter.amplitude);
		const signalToNoise = Number(meter && meter.signalToNoise);
		const hybrid = Number(meter && meter.hybrid);
		const peakCleanMicDb = Number(meter && meter.peakCleanMicDb);
		return {
			amplitude: Number.isFinite(amplitude) ? clampPercent(amplitude) : null,
			signalToNoise: Number.isFinite(signalToNoise) ? clampPercent(signalToNoise) : null,
			hybrid: Number.isFinite(hybrid) ? clampPercent(hybrid) : null,
			peakCleanMicDb: Number.isFinite(peakCleanMicDb) ? peakCleanMicDb : null
		};
	}

	function setVoiceCalibrationMessage(element, message, durationMs) {
		if (!element) {
			return;
		}
		element.dataset.calibrationMessage = message || "";
		element.dataset.calibrationMessageUntil = message ? String(Date.now() + (durationMs || 2600)) : "0";
	}

	function activeVoiceCalibrationFor(element) {
		return voiceCalibrationState && voiceCalibrationState.element === element;
	}

	function voiceCalibrationStatus() {
		if (!voiceCalibrationState) {
			return "";
		}
		const phase = voiceCalibrationPhase(voiceCalibrationState);
		if (phase === "prompt") {
			return "Ready to start";
		}
		if (phase === "leadIn") {
			return "Get ready";
		}
		if (phase === "quiet") {
			return "Measuring room noise";
		}
		if (phase === "speech") {
			return "Speak normally";
		}
		return "Calibrating";
	}

	function voiceCalibrationPhase(calibration) {
		if (!calibration || calibration.prompting) {
			return "prompt";
		}
		const elapsed = Date.now() - calibration.startedAt;
		if (elapsed < calibration.leadInMs) {
			return "leadIn";
		}
		if (elapsed < calibration.leadInMs + calibration.quietMs) {
			return "quiet";
		}
		if (elapsed < calibration.totalMs) {
			return "speech";
		}
		return "finish";
	}

	function syncVoiceCalibrationChrome(element) {
		if (!element) {
			return;
		}
		const button = element.querySelector(".modern-dialog-voice-meter-auto");
		const active = activeVoiceCalibrationFor(element);
		const prompting = active && voiceCalibrationState && voiceCalibrationState.prompting;
		element.classList.toggle("is-calibrating", !!active);
		element.classList.toggle("is-prompting", !!prompting);
		if (button) {
			button.disabled = !!active;
			button.textContent = prompting ? "Setup open" : (active ? "Listening..." : (button.dataset.defaultLabel || "Audio setup"));
		}
	}

	function clearVoiceCalibration() {
		const element = voiceCalibrationState ? voiceCalibrationState.element : null;
		voiceCalibrationState = null;
		if (element) {
			element.classList.remove("is-calibrating");
			element.classList.remove("is-prompting");
			syncVoiceCalibrationChrome(element);
		}
	}

	function voiceThresholdCandidate(name, vadSource, quietSamples, speechSamples) {
		quietSamples = quietSamples || [];
		speechSamples = speechSamples || [];
		if (speechSamples.length < 8) {
			return null;
		}
		const quietCeiling = percentile(quietSamples, 0.85);
		const quietFloor = percentile(quietSamples, 0.15);
		const speechLevel = percentile(speechSamples, 0.72);
		const speechPeak = percentile(speechSamples, 0.92);
		if (speechLevel === null && speechPeak === null) {
			return null;
		}

		const noise = quietCeiling === null ? 0 : quietCeiling;
		const voice = Math.max(speechLevel === null ? 0 : speechLevel, speechPeak === null ? 0 : speechPeak);
		const adjustedVoice = voice < noise + 8 ? Math.min(100, noise + 18) : voice;
		const span = Math.max(8, adjustedVoice - noise);
		let silenceThreshold = clampPercent(Math.round(noise + span * 0.34));
		let speechThreshold = clampPercent(Math.round(noise + span * 0.64));
		const jitter = quietFloor === null ? 0 : Math.max(0, quietCeiling - quietFloor);
		const gap = Math.max(0, adjustedVoice - noise);

		if (speechThreshold <= silenceThreshold + 2) {
			speechThreshold = Math.min(100, silenceThreshold + 3);
			silenceThreshold = Math.max(0, speechThreshold - 3);
		}
		return {
			name: name,
			vadSource: vadSource,
			silenceThreshold: silenceThreshold,
			speechThreshold: speechThreshold,
			noise: noise,
			voice: adjustedVoice,
			gap: gap,
			jitter: jitter,
			score: gap - jitter * 0.7 - noise * 0.08
		};
	}

	function calculatedVoiceThresholds(calibration) {
		const candidates = [
			voiceThresholdCandidate("Speech probability", 1, calibration.quietSignalToNoise, calibration.speechSignalToNoise),
			voiceThresholdCandidate("Speech + volume", 2, calibration.quietHybrid, calibration.speechHybrid),
			voiceThresholdCandidate("Volume level", 0, calibration.quietAmplitude, calibration.speechAmplitude)
		].filter(function(candidate) {
			return !!candidate;
		});
		if (!candidates.length) {
			return null;
		}

		const currentSource = Number(calibration.element.dataset.vadSource || 0);
		const recommendedSource = Number(calibration.element.dataset.recommendedVadSource || 2);
		candidates.sort(function(left, right) {
			return right.score - left.score;
		});
		let selected = candidates[0];
		const recommended = candidates.find(function(candidate) {
			return candidate.vadSource === recommendedSource;
		});
		if (recommended && recommended.gap >= 10 && recommended.score >= selected.score - 6) {
			selected = recommended;
		}
		const current = candidates.find(function(candidate) {
			return candidate.vadSource === currentSource;
		});
		if (current && current.score >= selected.score * 0.82 && current.gap >= 10) {
			selected = current;
		}

		const voiceHold = selected.gap < 14 ? 55 : (selected.jitter > 8 ? 40 : 30);
		const amplification = recommendedMaxAmplification(calibration, selected);
		const processing = recommendedNoiseProcessing(calibration, selected);
		return {
			vadSource: selected.vadSource,
			sourceLabel: selected.name,
			silenceThreshold: selected.silenceThreshold,
			speechThreshold: selected.speechThreshold,
			voiceHold: voiceHold,
			maxAmplification: amplification,
			noiseCancelMode: processing.noiseCancelMode,
			inputGateMode: processing.inputGateMode,
			speexNoiseStrength: processing.speexNoiseStrength,
			processingLabel: processing.label,
			noise: Math.round(selected.noise),
			voice: Math.round(selected.voice),
			gap: Math.round(selected.gap)
		};
	}

	function recommendedMaxAmplification(calibration) {
		const current = clampNumber(calibration.element.dataset.maxAmplification, 0, 20000, 0);
		const speechDb = percentile(calibration.speechPeakDb, 0.72);
		const quietDb = percentile(calibration.quietPeakDb, 0.85);
		const speechAmplitude = percentile(calibration.speechAmplitude, 0.82);
		let amplification = current;

		if (speechDb !== null) {
			if (speechDb < -52) {
				amplification = 17000;
			} else if (speechDb < -44) {
				amplification = 14000;
			} else if (speechDb < -36) {
				amplification = 11000;
			} else if (speechDb < -26) {
				amplification = 8000;
			} else if (speechDb < -16) {
				amplification = 5000;
			} else {
				amplification = 2500;
			}
		} else if (speechAmplitude !== null) {
			if (speechAmplitude < 36) {
				amplification = 14000;
			} else if (speechAmplitude < 52) {
				amplification = 9500;
			} else if (speechAmplitude > 82) {
				amplification = 3500;
			} else {
				amplification = 7000;
			}
		}

		if (quietDb !== null && quietDb > -34) {
			amplification = Math.min(amplification, 8000);
		} else if (quietDb !== null && quietDb > -42) {
			amplification = Math.min(amplification, 12000);
		}
		if (Math.abs(amplification - current) < 1500) {
			amplification = current;
		}
		return clampNumber(roundedStep(amplification, 500), 0, 20000, current);
	}

	function recommendedNoiseProcessing(calibration, selected) {
		const currentMode = clampNumber(calibration.element.dataset.noiseCancelMode, 0, 3, 0);
		const currentGateMode = clampNumber(calibration.element.dataset.inputGateMode, 0, 2, 0);
		const recommendedGateMode = clampNumber(calibration.element.dataset.recommendedInputGateMode, 0, 2, 1);
		const currentStrength = clampNumber(calibration.element.dataset.speexNoiseStrength, 0, 100, 14);
		const neuralAvailable = calibration.element.dataset.neuralCleanupAvailable === "true";
		const quietAmpCeiling = percentile(calibration.quietAmplitude, 0.85) || 0;
		const quietAmpFloor = percentile(calibration.quietAmplitude, 0.15) || 0;
		const quietSpeechProb = percentile(calibration.quietSignalToNoise, 0.85) || 0;
		const roomJitter = Math.max(selected.jitter || 0, quietAmpCeiling - quietAmpFloor);
		let noiseCancelMode = currentMode;
		let inputGateMode = currentGateMode;
		let speexNoiseStrength = currentStrength;
		let label = "cleanup kept";

		if (quietAmpCeiling >= 28 || quietSpeechProb >= 18 || roomJitter >= 16) {
			noiseCancelMode = neuralAvailable ? 3 : 1;
			speexNoiseStrength = Math.max(currentStrength, quietAmpCeiling >= 38 ? 42 : 34);
			label = neuralAvailable ? "neural cleanup + Speex" : "Speex cleanup";
		} else if (quietAmpCeiling >= 18 || quietSpeechProb >= 10 || roomJitter >= 9) {
			noiseCancelMode = currentMode === 0 ? 1 : currentMode;
			speexNoiseStrength = Math.max(currentStrength, 24);
			label = noiseCancelMode === 0 ? "cleanup kept" : "light cleanup";
		}

		if (currentGateMode === 0 && (quietSpeechProb >= 12 || roomJitter >= 12 || selected.vadSource === 1)) {
			inputGateMode = Math.max(inputGateMode, recommendedGateMode || 1);
		}
		if (inputGateMode <= 1 && (quietSpeechProb >= 35 || roomJitter >= 24)) {
			inputGateMode = 2;
		}
		if (inputGateMode === 2) {
			label += " + strict gate";
		} else if (inputGateMode === 1) {
			label += " + gate";
		}

		return {
			noiseCancelMode: Math.round(clampNumber(noiseCancelMode, 0, 3, currentMode)),
			inputGateMode: Math.round(clampNumber(inputGateMode, 0, 2, currentGateMode)),
			speexNoiseStrength: clampPercent(speexNoiseStrength),
			label: label
		};
	}

	function finishVoiceActivationCalibration() {
		const calibration = voiceCalibrationState;
		if (!calibration) {
			return;
		}

		const element = calibration.element;
		const thresholds = calculatedVoiceThresholds(calibration);
		clearVoiceCalibration();
		if (!thresholds) {
			setVoiceCalibrationMessage(element, "Audio setup needs a stronger speech sample");
			updateVoiceMeterElement(element, calibration.lastMeter || {});
			return;
		}

		const detail = thresholds.sourceLabel + " " + thresholds.silenceThreshold + "%/" + thresholds.speechThreshold
			+ "%, hold " + thresholds.voiceHold + " frames, amp " + thresholds.maxAmplification
			+ ", " + thresholds.processingLabel;
		const message = "Audio setup applied: " + detail;
		voiceCalibrationSummary = {
			message: message,
			status: "Audio setup applied",
			until: Date.now() + 5200
		};
		setVoiceCalibrationMessage(element, message, 5200);
		invokeModernDialogAction(calibration.actionId, thresholds);
	}

	function captureVoiceCalibrationSample(element, meter, level, available) {
		if (!activeVoiceCalibrationFor(element)) {
			return;
		}

		const calibration = voiceCalibrationState;
		if (calibration.prompting) {
			return;
		}
		calibration.lastMeter = meter || {};
		const phase = voiceCalibrationPhase(calibration);
		const levels = audioMeterLevelsFromPayload(meter || {});
		if (available && usableCalibrationSample(levels, phase)) {
			if (phase === "quiet") {
				if (levels.amplitude !== null) {
					calibration.quietAmplitude.push(levels.amplitude);
				}
				if (levels.signalToNoise !== null) {
					calibration.quietSignalToNoise.push(levels.signalToNoise);
				}
				if (levels.hybrid !== null) {
					calibration.quietHybrid.push(levels.hybrid);
				}
				if (levels.peakCleanMicDb !== null) {
					calibration.quietPeakDb.push(levels.peakCleanMicDb);
				}
			} else if (phase === "speech") {
				if (levels.amplitude !== null) {
					calibration.speechAmplitude.push(levels.amplitude);
				}
				if (levels.signalToNoise !== null) {
					calibration.speechSignalToNoise.push(levels.signalToNoise);
				}
				if (levels.hybrid !== null) {
					calibration.speechHybrid.push(levels.hybrid);
				}
				if (levels.peakCleanMicDb !== null) {
					calibration.speechPeakDb.push(levels.peakCleanMicDb);
				}
			}
		}

		if (phase === "finish") {
			finishVoiceActivationCalibration();
		}
	}

	function startVoiceActivationCalibration(element, field) {
		const actionId = String(field && field.calibrationActionId || "");
		if (!actionId || !element) {
			return;
		}

		clearVoiceCalibration();
		voiceCalibrationState = {
			element: element,
			actionId: actionId,
			prompting: true,
			startedAt: 0,
			leadInMs: 1200,
			quietMs: 3000,
			speechMs: 7400,
			totalMs: 11600,
			quietAmplitude: [],
			quietSignalToNoise: [],
			quietHybrid: [],
			quietPeakDb: [],
			speechAmplitude: [],
			speechSignalToNoise: [],
			speechHybrid: [],
			speechPeakDb: [],
			lastMeter: null
		};
		setVoiceCalibrationMessage(element, "");
		syncVoiceCalibrationChrome(element);
		updateVoiceMeterElement(element, {
			connected: element.dataset.serverConnected === "true",
			loopbackMode: Number(element.dataset.loopbackMode || 0)
		});
		refreshAudioInputMeters();
	}

	function beginVoiceActivationCalibration(element) {
		if (!activeVoiceCalibrationFor(element) || !voiceCalibrationState.prompting) {
			return;
		}
		voiceCalibrationState.prompting = false;
		voiceCalibrationState.startedAt = Date.now();
		setVoiceCalibrationMessage(element, "");
		syncVoiceCalibrationChrome(element);
		refreshAudioInputMeters();
	}

	function clearVoiceReplayStopTimer() {
		if (voiceReplayStopTimer) {
			window.clearTimeout(voiceReplayStopTimer);
			voiceReplayStopTimer = 0;
		}
	}

	function stopVoiceReplay(element) {
		const actionId = String(element && element.dataset.replayStopActionId || "");
		if (!actionId) {
			return;
		}
		clearVoiceReplayStopTimer();
		invokeModernDialogAction(actionId, {});
	}

	function toggleVoiceReplay(element) {
		if (!element) {
			return;
		}
		const loopbackMode = Number(element.dataset.loopbackMode || 0);
		if (loopbackMode !== 0) {
			stopVoiceReplay(element);
			return;
		}

		const actionId = String(element.dataset.replayStartActionId || "");
		if (!actionId) {
			return;
		}
		const mode = element.dataset.serverConnected === "true" ? "server" : "local";
		invokeModernDialogAction(actionId, { mode: mode });
		clearVoiceReplayStopTimer();
		voiceReplayStopTimer = window.setTimeout(function() {
			stopVoiceReplay(element);
		}, 30000);
	}

	function updateVoiceMeterCoach(element, meter, level, available) {
		const coach = element.querySelector(".modern-dialog-voice-meter-coach");
		if (!coach) {
			return;
		}
		const fill = coach.querySelector(".modern-dialog-voice-meter-coach-fill");
		const text = coach.querySelector(".modern-dialog-voice-meter-coach-text");
		const actions = coach.querySelector(".modern-dialog-voice-meter-coach-actions");
		const loopbackMode = Number(element.dataset.loopbackMode || 0);
		let progress = 0;
		let message = "";
		let showActions = false;

		if (activeVoiceCalibrationFor(element) && voiceCalibrationState && voiceCalibrationState.prompting) {
			message = "Audio setup takes about 12 seconds: stay quiet first, then speak normally.";
			showActions = true;
		} else if (activeVoiceCalibrationFor(element)) {
			const elapsed = Date.now() - voiceCalibrationState.startedAt;
			const phase = voiceCalibrationPhase(voiceCalibrationState);
			progress = Math.max(0, Math.min(100, Math.round((elapsed / voiceCalibrationState.totalMs) * 100)));
			if (phase === "leadIn") {
				message = "Get ready, then keep the room quiet";
			} else if (phase === "quiet") {
				message = "Keep the room quiet";
			} else {
				message = "Speak a normal test sentence";
			}
		} else if (voiceCalibrationSummary && voiceCalibrationSummary.until > Date.now()) {
			progress = 100;
			message = voiceCalibrationSummary.message || "";
		} else if (Number(element.dataset.calibrationMessageUntil || 0) > Date.now()) {
			progress = 100;
			message = element.dataset.calibrationMessage || "";
		} else if (loopbackMode === 2) {
			progress = available ? level : 0;
			message = "Server replay is active";
		} else if (loopbackMode === 1) {
			progress = available ? level : 0;
			message = "Local replay is active";
		} else {
			progress = available ? level : 0;
			message = available ? (element.dataset.calibrationStatusText || "Ready to tune") : "Waiting for microphone input";
		}

		coach.classList.toggle("is-active", activeVoiceCalibrationFor(element) || loopbackMode !== 0);
		coach.classList.toggle("has-actions", showActions);
		if (actions) {
			actions.style.display = showActions ? "flex" : "none";
		}
		if (fill) {
			fill.style.width = String(progress) + "%";
		}
		if (text) {
			text.textContent = message;
		}
	}

	function syncVoiceReplayChrome(element) {
		if (!element) {
			return;
		}
		const button = element.querySelector(".modern-dialog-voice-meter-replay");
		if (!button) {
			return;
		}
		const loopbackMode = Number(element.dataset.loopbackMode || 0);
		button.textContent = loopbackMode !== 0
			? "Stop replay"
			: (element.dataset.serverConnected === "true" ? "Server replay" : "Local replay");
	}

	function updateVoiceMeterElement(element, payload) {
		const meter = payload || {};
		const silenceThreshold = clampPercent(element.dataset.silenceThreshold);
		const speechThreshold = clampPercent(element.dataset.speechThreshold);
		const active = element.dataset.active === "true";
		const rawLevel = voiceMeterLevelFromPayload(element, meter);
		const available = !!meter.available && rawLevel !== null;
		const level = available ? rawLevel : 0;
		const levelText = element.querySelector(".modern-dialog-voice-meter-value");
		const statusText = element.querySelector(".modern-dialog-voice-meter-status");
		const messageUntil = Number(element.dataset.calibrationMessageUntil || 0);
		const summaryActive = voiceCalibrationSummary && voiceCalibrationSummary.until > Date.now();
		const loopbackMode = Number(meter.loopbackMode == null ? element.dataset.loopbackMode || 0 : meter.loopbackMode);

		element.dataset.loopbackMode = String(loopbackMode);
		element.dataset.serverConnected = meter.connected ? "true" : "false";

		element.style.setProperty("--voice-level", String(level) + "%");
		element.style.setProperty("--silence-threshold", String(silenceThreshold) + "%");
		element.style.setProperty("--speech-threshold", String(speechThreshold) + "%");
		element.classList.toggle("is-unavailable", !available);
		element.classList.toggle("is-inactive", !active && !activeVoiceCalibrationFor(element));
		element.classList.toggle("is-transmitting", !!meter.transmitting);

		if (levelText) {
			levelText.textContent = available ? String(level) + "%" : "No signal";
		}
		if (statusText) {
			if (activeVoiceCalibrationFor(element)) {
				statusText.textContent = voiceCalibrationStatus();
			} else if (summaryActive) {
				statusText.textContent = voiceCalibrationSummary.status || voiceCalibrationSummary.message || "";
			} else if (messageUntil > Date.now()) {
				statusText.textContent = element.dataset.calibrationMessage || "";
			} else if (loopbackMode === 2) {
				statusText.textContent = "Server replay";
			} else if (loopbackMode === 1) {
				statusText.textContent = "Local replay";
			} else if (!active) {
				statusText.textContent = "Voice activation is off";
			} else if (!available) {
				statusText.textContent = "Input inactive";
			} else {
				statusText.textContent = meter.transmitting ? "Transmitting" : "Listening";
			}
		}
		captureVoiceCalibrationSample(element, meter, available ? level : null, available);
		syncVoiceCalibrationChrome(element);
		syncVoiceReplayChrome(element);
		updateVoiceMeterCoach(element, meter, level, available);
	}

	function updateVoiceMeterThreshold(fieldId, value) {
		const threshold = clampPercent(value);
		const meters = Array.prototype.slice.call(document.querySelectorAll(".modern-dialog-voice-meter"));
		meters.forEach(function(element) {
			if (fieldId === "audio.vadMin") {
				element.dataset.silenceThreshold = String(threshold);
				element.style.setProperty("--silence-threshold", String(threshold) + "%");
				const silence = element.querySelector(".modern-dialog-voice-meter-silence-label");
				if (silence) {
					silence.textContent = "Stop below " + String(threshold) + "%";
				}
			} else if (fieldId === "audio.vadMax") {
				element.dataset.speechThreshold = String(threshold);
				element.style.setProperty("--speech-threshold", String(threshold) + "%");
				const speech = element.querySelector(".modern-dialog-voice-meter-speech-label");
				if (speech) {
					speech.textContent = "Start above " + String(threshold) + "%";
				}
			}
		});
	}

	function refreshAudioInputMeters() {
		if (!modernBridge || typeof modernBridge.currentAudioInputMeter !== "function") {
			return;
		}
		const meters = Array.prototype.slice.call(document.querySelectorAll(".modern-dialog-voice-meter"));
		if (!meters.length) {
			return;
		}
		try {
			modernBridge.currentAudioInputMeter(function(payload) {
				meters.forEach(function(element) {
					updateVoiceMeterElement(element, payload || {});
				});
			});
		} catch (error) {
			console.warn("Modern dialog audio meter update failed:", error);
		}
	}

	function syncAudioInputMeterTimer() {
		const shouldRun = !!(modernDialogState && modernDialogState.open
			&& document.querySelector(".modern-dialog-voice-meter"));
		if (shouldRun && !audioInputMeterTimer) {
			refreshAudioInputMeters();
			audioInputMeterTimer = window.setInterval(refreshAudioInputMeters, 150);
		} else if (!shouldRun && audioInputMeterTimer) {
			window.clearInterval(audioInputMeterTimer);
			audioInputMeterTimer = 0;
			clearVoiceCalibration();
			clearVoiceReplayStopTimer();
		}
	}

	function aclCloneModel(model) {
		try {
			return JSON.parse(JSON.stringify(model || {}));
		} catch (error) {
			return {};
		}
	}

	function aclCurrentModel(field, fallback) {
		return aclCloneModel((field && field.value) || fallback || {});
	}

	function aclNumberListFromText(text) {
		const seen = {};
		return String(text || "")
			.split(/[,\s]+/)
			.map(function(part) {
				const cleaned = part.replace(/^#/, "");
				const value = Number(cleaned);
				return Number.isInteger(value) && value >= 0 ? value : null;
			})
			.filter(function(value) {
				if (value === null || seen[value]) {
					return false;
				}
				seen[value] = true;
				return true;
			});
	}

	function aclNumberListToText(values) {
		return (values || []).map(function(value) {
			return String(value);
		}).join(", ");
	}

	function aclUpdateModel(field, model, rerender) {
		field.value = model;
		rememberModernDialogFieldValue(field.id, model);
		updateModernDialogFieldValue(field.id, model);
		if (rerender) {
			renderModernDialog();
		}
	}

	function aclTogglePermission(values, bit, checked) {
		values = (values || []).slice();
		const index = values.indexOf(bit);
		if (checked && index < 0) {
			values.push(bit);
		} else if (!checked && index >= 0) {
			values.splice(index, 1);
		}
		values.sort(function(left, right) {
			return left - right;
		});
		return values;
	}

	function appendAclGroupEditor(container, field, model) {
		const groupHeader = document.createElement("div");
		groupHeader.className = "modern-dialog-acl-subheader";
		const title = document.createElement("span");
		title.textContent = "Groups";
		const addButton = document.createElement("button");
		addButton.type = "button";
		addButton.className = "chip-button";
		addButton.textContent = "Add group";
		addButton.addEventListener("click", function(event) {
			event.preventDefault();
			const next = aclCurrentModel(field, model);
			next.groups = next.groups || [];
			next.groups.push({
				name: "newgroup",
				inherit: true,
				inheritable: true,
				inherited: false,
				add: [],
				remove: [],
				addText: "",
				removeText: "",
				inheritedMembers: []
			});
			aclUpdateModel(field, next, true);
		});
		groupHeader.appendChild(title);
		groupHeader.appendChild(addButton);
		container.appendChild(groupHeader);

		const groups = model.groups || [];
		if (!groups.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = "No custom groups.";
			container.appendChild(empty);
		}
		groups.forEach(function(group, groupIndex) {
			const card = document.createElement("div");
			card.className = "modern-dialog-acl-card" + (group.inherited ? " is-inherited" : "");
			const name = document.createElement("input");
			name.type = "text";
			name.value = group.name || "";
			name.disabled = !!group.inherited;
			name.setAttribute("aria-label", "Group name");
			name.addEventListener("input", function() {
				const next = aclCurrentModel(field, model);
				next.groups[groupIndex].name = name.value;
				aclUpdateModel(field, next, false);
			});
			card.appendChild(name);

			[
				["inherit", "Inherit members"],
				["inheritable", "Inheritable"]
			].forEach(function(pair) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-inline";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = !!group[pair[0]];
				checkbox.disabled = !!group.inherited;
				checkbox.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.groups[groupIndex][pair[0]] = checkbox.checked;
					aclUpdateModel(field, next, false);
				});
				label.appendChild(checkbox);
				label.appendChild(document.createTextNode(pair[1]));
				card.appendChild(label);
			});

			const addMembers = document.createElement("input");
			addMembers.type = "text";
			addMembers.value = group.addText != null ? String(group.addText) : aclNumberListToText(group.add);
			addMembers.placeholder = "Add users or IDs";
			addMembers.disabled = !!group.inherited;
			addMembers.addEventListener("input", function() {
				const next = aclCurrentModel(field, model);
				next.groups[groupIndex].addText = addMembers.value;
				next.groups[groupIndex].add = aclNumberListFromText(addMembers.value);
				aclUpdateModel(field, next, false);
			});
			card.appendChild(addMembers);

			const removeMembers = document.createElement("input");
			removeMembers.type = "text";
			removeMembers.value = group.removeText != null ? String(group.removeText) : aclNumberListToText(group.remove);
			removeMembers.placeholder = "Remove users or IDs";
			removeMembers.disabled = !!group.inherited;
			removeMembers.addEventListener("input", function() {
				const next = aclCurrentModel(field, model);
				next.groups[groupIndex].removeText = removeMembers.value;
				next.groups[groupIndex].remove = aclNumberListFromText(removeMembers.value);
				aclUpdateModel(field, next, false);
			});
			card.appendChild(removeMembers);

			const removeButton = document.createElement("button");
			removeButton.type = "button";
			removeButton.className = "chip-button is-danger";
			removeButton.textContent = "Delete";
			removeButton.disabled = !!group.inherited;
			removeButton.addEventListener("click", function(event) {
				event.preventDefault();
				const next = aclCurrentModel(field, model);
				next.groups.splice(groupIndex, 1);
				aclUpdateModel(field, next, true);
			});
			card.appendChild(removeButton);

			if (group.inheritedMembers && group.inheritedMembers.length) {
				const inherited = document.createElement("span");
				inherited.className = "modern-dialog-acl-muted";
				inherited.textContent = "Inherited members: " + aclNumberListToText(group.inheritedMembers);
				card.appendChild(inherited);
			}
			container.appendChild(card);
		});
	}

	function appendAclRuleEditor(container, field, model) {
		const ruleHeader = document.createElement("div");
		ruleHeader.className = "modern-dialog-acl-subheader";
		const title = document.createElement("span");
		title.textContent = "Access rules";
		const addButton = document.createElement("button");
		addButton.type = "button";
		addButton.className = "chip-button";
		addButton.textContent = "Add rule";
		addButton.addEventListener("click", function(event) {
			event.preventDefault();
			const next = aclCurrentModel(field, model);
			next.acls = next.acls || [];
			next.acls.push({
				targetType: "group",
				target: "all",
				userId: -1,
				applyHere: true,
				applySubs: true,
				inherited: false,
				allow: [],
				deny: []
			});
			aclUpdateModel(field, next, true);
		});
		ruleHeader.appendChild(title);
		ruleHeader.appendChild(addButton);
		container.appendChild(ruleHeader);

		(model.acls || []).forEach(function(rule, ruleIndex) {
			const card = document.createElement("div");
			card.className = "modern-dialog-acl-rule" + (rule.inherited ? " is-inherited" : "");
			const header = document.createElement("div");
			header.className = "modern-dialog-acl-rule-header";
			const targetType = document.createElement("select");
			["group", "user"].forEach(function(type) {
				const option = document.createElement("option");
				option.value = type;
				option.textContent = type === "group" ? "Group" : "User";
				targetType.appendChild(option);
			});
			targetType.value = rule.targetType || "group";
			targetType.disabled = !!rule.inherited;
			targetType.addEventListener("change", function() {
				const next = aclCurrentModel(field, model);
				next.acls[ruleIndex].targetType = targetType.value;
				next.acls[ruleIndex].target = targetType.value === "group" ? "all" : "";
				next.acls[ruleIndex].userId = -1;
				aclUpdateModel(field, next, true);
			});
			header.appendChild(targetType);

			const target = document.createElement("input");
			target.type = "text";
			target.value = rule.target || "";
			target.placeholder = targetType.value === "group" ? "all, auth, in, sub..." : "username or #id";
			target.disabled = !!rule.inherited;
			target.addEventListener("input", function() {
				const next = aclCurrentModel(field, model);
				next.acls[ruleIndex].target = target.value;
				aclUpdateModel(field, next, false);
			});
			header.appendChild(target);

			[
				["applyHere", "Here"],
				["applySubs", "Sub rooms"]
			].forEach(function(pair) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-inline";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = rule[pair[0]] !== false;
				checkbox.disabled = !!rule.inherited;
				checkbox.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.acls[ruleIndex][pair[0]] = checkbox.checked;
					aclUpdateModel(field, next, false);
				});
				label.appendChild(checkbox);
				label.appendChild(document.createTextNode(pair[1]));
				header.appendChild(label);
			});

			const moveUp = document.createElement("button");
			moveUp.type = "button";
			moveUp.className = "chip-button";
			moveUp.textContent = "Up";
			moveUp.disabled = !!rule.inherited || ruleIndex === 0;
			moveUp.addEventListener("click", function(event) {
				event.preventDefault();
				const next = aclCurrentModel(field, model);
				const item = next.acls.splice(ruleIndex, 1)[0];
				next.acls.splice(ruleIndex - 1, 0, item);
				aclUpdateModel(field, next, true);
			});
			header.appendChild(moveUp);

			const moveDown = document.createElement("button");
			moveDown.type = "button";
			moveDown.className = "chip-button";
			moveDown.textContent = "Down";
			moveDown.disabled = !!rule.inherited || ruleIndex >= (model.acls || []).length - 1;
			moveDown.addEventListener("click", function(event) {
				event.preventDefault();
				const next = aclCurrentModel(field, model);
				const item = next.acls.splice(ruleIndex, 1)[0];
				next.acls.splice(ruleIndex + 1, 0, item);
				aclUpdateModel(field, next, true);
			});
			header.appendChild(moveDown);

			const remove = document.createElement("button");
			remove.type = "button";
			remove.className = "chip-button is-danger";
			remove.textContent = "Delete";
			remove.disabled = !!rule.inherited;
			remove.addEventListener("click", function(event) {
				event.preventDefault();
				const next = aclCurrentModel(field, model);
				next.acls.splice(ruleIndex, 1);
				aclUpdateModel(field, next, true);
			});
			header.appendChild(remove);
			card.appendChild(header);

			const permissions = document.createElement("div");
			permissions.className = "modern-dialog-acl-permissions";
			(model.permissions || []).forEach(function(permission) {
				const permissionRow = document.createElement("div");
				permissionRow.className = "modern-dialog-acl-permission";
				const name = document.createElement("span");
				name.textContent = permission.label || String(permission.id);
				if (permission.hint) {
					name.title = String(permission.hint);
				}
				permissionRow.appendChild(name);
				[
					["deny", "Deny"],
					["allow", "Allow"]
				].forEach(function(pair) {
					const label = document.createElement("label");
					label.className = "modern-dialog-acl-inline";
					const checkbox = document.createElement("input");
					const bit = Number(permission.id) || 0;
					checkbox.type = "checkbox";
					checkbox.checked = (rule[pair[0]] || []).indexOf(bit) >= 0;
					checkbox.disabled = !!rule.inherited;
					checkbox.addEventListener("change", function() {
						const next = aclCurrentModel(field, model);
						const edited = next.acls[ruleIndex];
						edited[pair[0]] = aclTogglePermission(edited[pair[0]], bit, checkbox.checked);
						const other = pair[0] === "allow" ? "deny" : "allow";
						if (checkbox.checked) {
							edited[other] = aclTogglePermission(edited[other], bit, false);
						}
						aclUpdateModel(field, next, false);
					});
					label.appendChild(checkbox);
					label.appendChild(document.createTextNode(pair[1]));
					permissionRow.appendChild(label);
				});
				permissions.appendChild(permissionRow);
			});
			card.appendChild(permissions);
			container.appendChild(card);
		});
	}

	function appendModernAclEditor(container, field, errors) {
		const model = aclCloneModel(field.value || {});
		model.acls = model.acls || [];
		model.groups = model.groups || [];
		const wrap = document.createElement("div");
		wrap.className = "modern-dialog-acl-editor";
		const inherit = document.createElement("label");
		inherit.className = "modern-dialog-acl-inline modern-dialog-acl-inherit";
		const inheritBox = document.createElement("input");
		inheritBox.type = "checkbox";
		inheritBox.checked = model.inheritAcls !== false;
		inheritBox.addEventListener("change", function() {
			const next = aclCurrentModel(field, model);
			next.inheritAcls = inheritBox.checked;
			aclUpdateModel(field, next, false);
		});
		inherit.appendChild(inheritBox);
		inherit.appendChild(document.createTextNode("Inherit parent ACLs"));
		wrap.appendChild(inherit);

		const passwordRow = document.createElement("label");
		passwordRow.className = "modern-dialog-field is-text";
		const passwordLabel = document.createElement("span");
		passwordLabel.className = "modern-dialog-field-label";
		passwordLabel.textContent = "Room password";
		const passwordInput = document.createElement("input");
		passwordInput.type = "text";
		passwordInput.value = model.password || "";
		passwordInput.addEventListener("input", function() {
			const next = aclCurrentModel(field, model);
			next.password = passwordInput.value;
			aclUpdateModel(field, next, false);
		});
		passwordRow.appendChild(passwordLabel);
		passwordRow.appendChild(passwordInput);
		wrap.appendChild(passwordRow);

		appendAclGroupEditor(wrap, field, model);
		appendAclRuleEditor(wrap, field, model);

		const errorText = errors && field && field.id ? errors[field.id] : "";
		if (errorText) {
			const error = document.createElement("span");
			error.className = "modern-dialog-field-error";
			error.textContent = errorText;
			wrap.appendChild(error);
		}
		container.appendChild(wrap);
	}

	function appendModernResultList(container, field) {
		const list = document.createElement("div");
		list.className = "modern-dialog-result-list";
		const items = field.items || field.value || [];
		if (!items.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = field.emptyText || "No results.";
			list.appendChild(empty);
		}
		items.forEach(function(item) {
			const row = document.createElement("div");
			row.className = "modern-dialog-result";
			const copy = document.createElement("div");
			copy.className = "modern-dialog-result-copy";
			const title = document.createElement("span");
			title.className = "modern-dialog-result-title";
			title.textContent = item.title || "";
			const subtitle = document.createElement("span");
			subtitle.className = "modern-dialog-result-subtitle";
			subtitle.textContent = (item.type === "user" ? "User" : "Room") + (item.subtitle ? " · " + item.subtitle : "");
			copy.appendChild(title);
			copy.appendChild(subtitle);
			row.appendChild(copy);
			const actions = document.createElement("span");
			actions.className = "modern-dialog-result-actions";
			[
				["selectSearchResult", item.primaryAction || "Select"],
				["joinSearchResult", item.secondaryAction || "Join"]
			].forEach(function(pair) {
				const button = document.createElement("button");
				button.type = "button";
				button.className = "chip-button";
				button.textContent = pair[1];
				button.addEventListener("click", function(event) {
					event.preventDefault();
					invokeModernDialogAction(pair[0], {
						type: item.type || "",
						id: Number(item.id) || 0,
						index: Number(item.index) || 0
					});
				});
				actions.appendChild(button);
			});
			row.appendChild(actions);
			list.appendChild(row);
		});
		container.appendChild(list);
	}

	function aclPermissionName(model, bit) {
		const permissions = model.permissions || [];
		for (let i = 0; i < permissions.length; ++i) {
			if (Number(permissions[i].id) === Number(bit)) {
				return permissions[i].label || String(bit);
			}
		}
		return String(bit);
	}

	function aclPermissionSummary(model, values, emptyText) {
		const names = (values || []).map(function(bit) {
			return aclPermissionName(model, bit);
		});
		if (!names.length) {
			return emptyText || "None";
		}
		if (names.length > 3) {
			return names.slice(0, 3).join(", ") + " +" + String(names.length - 3);
		}
		return names.join(", ");
	}

	function aclRuleTargetLabel(rule) {
		const type = rule.targetType === "user" ? "User" : "Group";
		const target = String(rule.target || "").trim() || (type === "Group" ? "all" : "user");
		return type + ": " + target;
	}

	function aclRuleScopeLabel(rule) {
		const here = rule.applyHere !== false;
		const subs = rule.applySubs !== false;
		if (here && subs) {
			return "This room and sub rooms";
		}
		if (here) {
			return "This room only";
		}
		if (subs) {
			return "Sub rooms only";
		}
		return "No scope selected";
	}

	function aclPermissionState(rule, bit) {
		if ((rule.deny || []).indexOf(bit) >= 0) {
			return "deny";
		}
		if ((rule.allow || []).indexOf(bit) >= 0) {
			return "allow";
		}
		return "unset";
	}

	function aclSetPermissionState(rule, bit, state) {
		rule.allow = aclTogglePermission(rule.allow, bit, false);
		rule.deny = aclTogglePermission(rule.deny, bit, false);
		if (state === "allow") {
			rule.allow = aclTogglePermission(rule.allow, bit, true);
		} else if (state === "deny") {
			rule.deny = aclTogglePermission(rule.deny, bit, true);
		}
	}

	function aclSetExpandedRule(field, model, ruleIndex, expanded) {
		const next = aclCurrentModel(field, model);
		next.acls = next.acls || [];
		next.acls.forEach(function(rule, index) {
			rule.expanded = expanded && index === ruleIndex;
		});
		aclUpdateModel(field, next, true);
	}

	function aclMoveRule(field, model, ruleIndex, delta) {
		const next = aclCurrentModel(field, model);
		const targetIndex = ruleIndex + delta;
		if (!next.acls || targetIndex < 0 || targetIndex >= next.acls.length) {
			return;
		}
		const item = next.acls.splice(ruleIndex, 1)[0];
		next.acls.splice(targetIndex, 0, item);
		next.acls.forEach(function(rule, index) {
			rule.expanded = index === targetIndex;
		});
		next.activeTab = "rules";
		next.selectedRuleIndex = targetIndex;
		aclUpdateModel(field, next, true);
	}

	function appendAclButton(container, label, className, disabled, callback) {
		const button = document.createElement("button");
		button.type = "button";
		button.className = "chip-button" + (className ? " " + className : "");
		button.textContent = label;
		button.disabled = !!disabled;
		button.addEventListener("click", function(event) {
			event.preventDefault();
			callback();
		});
		container.appendChild(button);
		return button;
	}

	function appendAclGroupEditor(container, field, model) {
		const panel = document.createElement("section");
		panel.className = "modern-dialog-acl-panel";
		const header = document.createElement("div");
		header.className = "modern-dialog-acl-panel-header";
		const copy = document.createElement("div");
		const title = document.createElement("h3");
		title.className = "modern-dialog-acl-panel-title";
		title.textContent = "Groups";
		const subtitle = document.createElement("p");
		subtitle.className = "modern-dialog-acl-panel-subtitle";
		subtitle.textContent = "Create named member sets and decide whether they inherit parent membership.";
		copy.appendChild(title);
		copy.appendChild(subtitle);
		header.appendChild(copy);
		appendAclButton(header, "Add group", "", false, function() {
			const next = aclCurrentModel(field, model);
			next.groups = next.groups || [];
			next.groups.push({
				name: "newgroup",
				inherit: true,
				inheritable: true,
				inherited: false,
				add: [],
				remove: [],
				addText: "",
				removeText: "",
				inheritedMembers: []
			});
			aclUpdateModel(field, next, true);
		});
		panel.appendChild(header);

		const groups = model.groups || [];
		const list = document.createElement("div");
		list.className = "modern-dialog-acl-group-list";
		if (!groups.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = "No custom groups.";
			list.appendChild(empty);
		}
		groups.forEach(function(group, groupIndex) {
			const card = document.createElement("div");
			card.className = "modern-dialog-acl-group" + (group.inherited ? " is-inherited" : "");

			const grid = document.createElement("div");
			grid.className = "modern-dialog-acl-group-grid";
			[
				["name", "Group name", group.name || ""],
				["addText", "Add members", group.addText != null ? String(group.addText) : aclNumberListToText(group.add)],
				["removeText", "Remove members", group.removeText != null ? String(group.removeText) : aclNumberListToText(group.remove)]
			].forEach(function(item) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-control is-" + item[0];
				const caption = document.createElement("span");
				caption.textContent = item[1];
				const input = document.createElement("input");
				input.type = "text";
				input.value = item[2];
				input.disabled = !!group.inherited;
				input.placeholder = item[1];
				input.addEventListener("input", function() {
					const next = aclCurrentModel(field, model);
					if (item[0] === "name") {
						next.groups[groupIndex].name = input.value;
					} else if (item[0] === "addText") {
						next.groups[groupIndex].addText = input.value;
						next.groups[groupIndex].add = aclNumberListFromText(input.value);
					} else {
						next.groups[groupIndex].removeText = input.value;
						next.groups[groupIndex].remove = aclNumberListFromText(input.value);
					}
					aclUpdateModel(field, next, false);
				});
				label.appendChild(caption);
				label.appendChild(input);
				grid.appendChild(label);
			});

			const flags = document.createElement("div");
			flags.className = "modern-dialog-acl-group-flags";
			[
				["inherit", "Inherit members"],
				["inheritable", "Inheritable"]
			].forEach(function(pair) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-check";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = !!group[pair[0]];
				checkbox.disabled = !!group.inherited;
				checkbox.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.groups[groupIndex][pair[0]] = checkbox.checked;
					aclUpdateModel(field, next, false);
				});
				label.appendChild(checkbox);
				label.appendChild(document.createTextNode(pair[1]));
				flags.appendChild(label);
			});
			grid.appendChild(flags);

			const actions = document.createElement("div");
			actions.className = "modern-dialog-acl-card-actions";
			appendAclButton(actions, "Delete", "is-danger", !!group.inherited, function() {
				const next = aclCurrentModel(field, model);
				next.groups.splice(groupIndex, 1);
				aclUpdateModel(field, next, true);
			});
			grid.appendChild(actions);
			card.appendChild(grid);

			if (group.inheritedMembers && group.inheritedMembers.length) {
				const inherited = document.createElement("span");
				inherited.className = "modern-dialog-acl-muted";
				inherited.textContent = "Inherited members: " + aclNumberListToText(group.inheritedMembers);
				card.appendChild(inherited);
			}
			list.appendChild(card);
		});
		panel.appendChild(list);
		container.appendChild(panel);
	}

	function appendAclRuleEditor(container, field, model) {
		const panel = document.createElement("section");
		panel.className = "modern-dialog-acl-panel";
		const header = document.createElement("div");
		header.className = "modern-dialog-acl-panel-header";
		const copy = document.createElement("div");
		const title = document.createElement("h3");
		title.className = "modern-dialog-acl-panel-title";
		title.textContent = "Access rules";
		const subtitle = document.createElement("p");
		subtitle.className = "modern-dialog-acl-panel-subtitle";
		subtitle.textContent = "Scan compact rules, then expand one rule to edit its permissions.";
		copy.appendChild(title);
		copy.appendChild(subtitle);
		header.appendChild(copy);
		appendAclButton(header, "Add rule", "", false, function() {
			const next = aclCurrentModel(field, model);
			next.acls = next.acls || [];
			next.acls.forEach(function(rule) {
				rule.expanded = false;
			});
			next.acls.push({
				targetType: "group",
				target: "all",
				userId: -1,
				applyHere: true,
				applySubs: true,
				inherited: false,
				expanded: true,
				allow: [],
				deny: []
			});
			aclUpdateModel(field, next, true);
		});
		panel.appendChild(header);

		const rules = model.acls || [];
		const firstEditableIndex = rules.findIndex(function(rule) {
			return !rule.inherited;
		});
		const hasExpansionPreference = rules.some(function(rule) {
			return Object.prototype.hasOwnProperty.call(rule, "expanded");
		});
		const list = document.createElement("div");
		list.className = "modern-dialog-acl-rule-list";
		if (!rules.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = "No ACL rules.";
			list.appendChild(empty);
		}

		rules.forEach(function(rule, ruleIndex) {
			const expanded = rule.expanded === true || (!hasExpansionPreference && ruleIndex === firstEditableIndex);
			const inherited = !!rule.inherited;
			const card = document.createElement("article");
			card.className = "modern-dialog-acl-rule" + (inherited ? " is-inherited" : "") + (expanded ? " is-expanded" : "");

			const summary = document.createElement("div");
			summary.className = "modern-dialog-acl-rule-summary";
			const main = document.createElement("div");
			main.className = "modern-dialog-acl-rule-main";
			const target = document.createElement("strong");
			target.textContent = aclRuleTargetLabel(rule);
			const meta = document.createElement("span");
			meta.className = "modern-dialog-acl-muted";
			meta.textContent = aclRuleScopeLabel(rule) + (inherited ? " - inherited" : "");
			main.appendChild(target);
			main.appendChild(meta);
			summary.appendChild(main);

			const badges = document.createElement("div");
			badges.className = "modern-dialog-acl-badges";
			[
				["allow", "Allow", aclPermissionSummary(model, rule.allow, "No allows")],
				["deny", "Deny", aclPermissionSummary(model, rule.deny, "No denies")]
			].forEach(function(item) {
				const badge = document.createElement("span");
				badge.className = "modern-dialog-acl-badge is-" + item[0];
				badge.textContent = item[1] + ": " + item[2];
				badges.appendChild(badge);
			});
			summary.appendChild(badges);

			const actions = document.createElement("div");
			actions.className = "modern-dialog-acl-rule-actions";
			appendAclButton(actions, expanded ? "Collapse" : (inherited ? "View" : "Edit"), "", false, function() {
				aclSetExpandedRule(field, model, ruleIndex, !expanded);
			});
			appendAclButton(actions, "Up", "", inherited || ruleIndex === 0, function() {
				aclMoveRule(field, model, ruleIndex, -1);
			});
			appendAclButton(actions, "Down", "", inherited || ruleIndex >= rules.length - 1, function() {
				aclMoveRule(field, model, ruleIndex, 1);
			});
			appendAclButton(actions, "Delete", "is-danger", inherited, function() {
				const next = aclCurrentModel(field, model);
				next.acls.splice(ruleIndex, 1);
				aclUpdateModel(field, next, true);
			});
			summary.appendChild(actions);
			card.appendChild(summary);

			if (expanded) {
				const editor = document.createElement("div");
				editor.className = "modern-dialog-acl-rule-editor";
				const targetRow = document.createElement("div");
				targetRow.className = "modern-dialog-acl-rule-target";
				const targetType = document.createElement("select");
				["group", "user"].forEach(function(type) {
					const option = document.createElement("option");
					option.value = type;
					option.textContent = type === "group" ? "Group" : "User";
					targetType.appendChild(option);
				});
				targetType.value = rule.targetType || "group";
				targetType.disabled = inherited;
				targetType.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.acls[ruleIndex].targetType = targetType.value;
					next.acls[ruleIndex].target = targetType.value === "group" ? "all" : "";
					next.acls[ruleIndex].userId = -1;
					next.acls[ruleIndex].expanded = true;
					aclUpdateModel(field, next, true);
				});
				targetRow.appendChild(targetType);

				const targetInput = document.createElement("input");
				targetInput.type = "text";
				targetInput.value = rule.target || "";
				targetInput.placeholder = targetType.value === "group" ? "all, auth, in, sub..." : "username or #id";
				targetInput.disabled = inherited;
				targetInput.addEventListener("input", function() {
					const next = aclCurrentModel(field, model);
					next.acls[ruleIndex].target = targetInput.value;
					next.acls[ruleIndex].expanded = true;
					aclUpdateModel(field, next, false);
				});
				targetRow.appendChild(targetInput);

				const scopes = document.createElement("div");
				scopes.className = "modern-dialog-acl-scope";
				[
					["applyHere", "This room"],
					["applySubs", "Sub rooms"]
				].forEach(function(pair) {
					const label = document.createElement("label");
					label.className = "modern-dialog-acl-check";
					const checkbox = document.createElement("input");
					checkbox.type = "checkbox";
					checkbox.checked = rule[pair[0]] !== false;
					checkbox.disabled = inherited;
					checkbox.addEventListener("change", function() {
						const next = aclCurrentModel(field, model);
						next.acls[ruleIndex][pair[0]] = checkbox.checked;
						next.acls[ruleIndex].expanded = true;
						aclUpdateModel(field, next, false);
					});
					label.appendChild(checkbox);
					label.appendChild(document.createTextNode(pair[1]));
					scopes.appendChild(label);
				});
				targetRow.appendChild(scopes);
				editor.appendChild(targetRow);

				const matrix = document.createElement("div");
				matrix.className = "modern-dialog-acl-permission-matrix";
				(model.permissions || []).forEach(function(permission) {
					const bit = Number(permission.id) || 0;
					const state = aclPermissionState(rule, bit);
					const row = document.createElement("div");
					row.className = "modern-dialog-acl-permission-row is-" + state;
					const name = document.createElement("span");
					name.className = "modern-dialog-acl-permission-name";
					name.textContent = permission.label || String(permission.id);
					if (permission.hint) {
						name.title = String(permission.hint);
					}
					row.appendChild(name);

					const choices = document.createElement("div");
					choices.className = "modern-dialog-acl-state";
					[
						["unset", "Not set"],
						["deny", "Deny"],
						["allow", "Allow"]
					].forEach(function(choice) {
						const button = document.createElement("button");
						button.type = "button";
						button.className = "modern-dialog-acl-state-button is-" + choice[0]
							+ (state === choice[0] ? " is-selected" : "");
						button.textContent = choice[1];
						button.disabled = inherited;
						button.addEventListener("click", function(event) {
							event.preventDefault();
							const next = aclCurrentModel(field, model);
							aclSetPermissionState(next.acls[ruleIndex], bit, choice[0]);
							next.acls[ruleIndex].expanded = true;
							aclUpdateModel(field, next, false);
							renderModernDialog();
						});
						choices.appendChild(button);
					});
					row.appendChild(choices);
					matrix.appendChild(row);
				});
				editor.appendChild(matrix);
				card.appendChild(editor);
			}
			list.appendChild(card);
		});
		panel.appendChild(list);
		container.appendChild(panel);
	}

	function appendModernAclEditor(container, field, errors) {
		const model = aclCloneModel(field.value || {});
		model.acls = model.acls || [];
		model.groups = model.groups || [];
		const wrap = document.createElement("div");
		wrap.className = "modern-dialog-acl-editor";

		const policy = document.createElement("section");
		policy.className = "modern-dialog-acl-policy";
		const policyCopy = document.createElement("div");
		const policyTitle = document.createElement("h3");
		policyTitle.className = "modern-dialog-acl-panel-title";
		policyTitle.textContent = "Room policy";
		const policySummary = document.createElement("p");
		policySummary.className = "modern-dialog-acl-panel-subtitle";
		policySummary.textContent = String(model.groups.length) + " groups, " + String(model.acls.length) + " access rules";
		policyCopy.appendChild(policyTitle);
		policyCopy.appendChild(policySummary);
		policy.appendChild(policyCopy);

		const policyControls = document.createElement("div");
		policyControls.className = "modern-dialog-acl-policy-controls";
		const inherit = document.createElement("label");
		inherit.className = "modern-dialog-acl-check";
		const inheritBox = document.createElement("input");
		inheritBox.type = "checkbox";
		inheritBox.checked = model.inheritAcls !== false;
		inheritBox.addEventListener("change", function() {
			const next = aclCurrentModel(field, model);
			next.inheritAcls = inheritBox.checked;
			aclUpdateModel(field, next, false);
		});
		inherit.appendChild(inheritBox);
		inherit.appendChild(document.createTextNode("Inherit parent ACLs"));
		policyControls.appendChild(inherit);

		const passwordLabel = document.createElement("label");
		passwordLabel.className = "modern-dialog-acl-control";
		const passwordCaption = document.createElement("span");
		passwordCaption.textContent = "Room password";
		const passwordInput = document.createElement("input");
		passwordInput.type = "text";
		passwordInput.value = model.password || "";
		passwordInput.placeholder = "Optional join password";
		passwordInput.addEventListener("input", function() {
			const next = aclCurrentModel(field, model);
			next.password = passwordInput.value;
			aclUpdateModel(field, next, false);
		});
		passwordLabel.appendChild(passwordCaption);
		passwordLabel.appendChild(passwordInput);
		policyControls.appendChild(passwordLabel);
		policy.appendChild(policyControls);
		wrap.appendChild(policy);

		appendAclGroupEditor(wrap, field, model);
		appendAclRuleEditor(wrap, field, model);

		const errorText = errors && field && field.id ? errors[field.id] : "";
		if (errorText) {
			const error = document.createElement("span");
			error.className = "modern-dialog-field-error";
			error.textContent = errorText;
			wrap.appendChild(error);
		}
		container.appendChild(wrap);
	}

	function aclOptionId(option) {
		const value = Number(option && (option.id != null ? option.id : option.value));
		return Number.isFinite(value) ? value : -1;
	}

	function aclUserOptions(model) {
		return (model.userOptions || []).filter(function(option) {
			return aclOptionId(option) >= 0;
		});
	}

	function aclUserLabel(model, userId) {
		userId = Number(userId);
		const options = aclUserOptions(model);
		for (let i = 0; i < options.length; ++i) {
			if (aclOptionId(options[i]) === userId) {
				return options[i].label || String(userId);
			}
		}
		return "User " + String(userId);
	}

	function aclGroupLabel(value) {
		const target = String(value || "").trim() || "all";
		const options = aclSpecialGroups();
		for (let i = 0; i < options.length; ++i) {
			if (options[i].value === target) {
				return options[i].label;
			}
		}
		return "Group: " + target;
	}

	function aclRuleTitle(model, rule) {
		if (rule.targetType === "user") {
			return "User: " + aclUserLabel(model, Number(rule.userId));
		}
		return aclGroupLabel(rule.target);
	}

	function aclList(values) {
		return Array.isArray(values) ? values.slice() : [];
	}

	function aclSelectedIndex(model, key, length) {
		const selected = Number(model[key]);
		if (Number.isInteger(selected) && selected >= 0 && selected < length) {
			return selected;
		}
		return length > 0 ? 0 : -1;
	}

	function aclSetActiveTab(field, model, tab) {
		const next = aclCurrentModel(field, model);
		next.activeTab = tab;
		aclUpdateModel(field, next, true);
	}

	function aclSetSelectedGroup(field, model, index) {
		const next = aclCurrentModel(field, model);
		next.activeTab = "groups";
		next.selectedGroupIndex = index;
		aclUpdateModel(field, next, true);
	}

	function aclSetSelectedRule(field, model, index) {
		const next = aclCurrentModel(field, model);
		next.activeTab = "rules";
		next.selectedRuleIndex = index;
		aclUpdateModel(field, next, true);
	}

	function aclSpecialGroups() {
		return [
			{ value: "all", label: "Everyone (all)" },
			{ value: "auth", label: "Authenticated users (auth)" },
			{ value: "in", label: "Users in this room (in)" },
			{ value: "sub", label: "Users in sub rooms (sub)" },
			{ value: "out", label: "Users outside this room tree (out)" },
			{ value: "~in", label: "Users not in this room (~in)" },
			{ value: "~sub", label: "Users not in sub rooms (~sub)" },
			{ value: "~out", label: "Users inside this room tree (~out)" }
		];
	}

	function aclGroupTargetOptions(model, currentValue) {
		const seen = {};
		const values = [];
		function add(value, label) {
			value = String(value || "").trim();
			if (!value || seen[value]) {
				return;
			}
			seen[value] = true;
			values.push({ value: value, label: label || ("Group: " + value) });
		}
		aclSpecialGroups().forEach(function(option) {
			add(option.value, option.label);
		});
		(model.groups || []).forEach(function(group) {
			add(group.name, "Group: " + group.name);
		});
		add(currentValue, aclGroupLabel(currentValue));
		return values;
	}

	function aclRenderUserSelect(select, model, selectedId) {
		select.innerHTML = "";
		const options = aclUserOptions(model);
		const selected = Number(selectedId);
		let hasSelected = false;
		options.forEach(function(option) {
			const id = aclOptionId(option);
			const item = document.createElement("option");
			item.value = String(id);
			const label = option.label || String(id);
			item.textContent = option.subtitle ? label + " - " + option.subtitle : label;
			if (option.subtitle) {
				item.title = option.subtitle;
			}
			if (id === selected) {
				hasSelected = true;
			}
			select.appendChild(item);
		});
		if (selected >= 0 && !hasSelected) {
			const fallback = document.createElement("option");
			fallback.value = String(selected);
			fallback.textContent = aclUserLabel(model, selected);
			select.appendChild(fallback);
		}
		select.disabled = !select.options.length;
		if (select.options.length) {
			select.value = selected >= 0 ? String(selected) : select.options[0].value;
		}
	}

	function aclAddMember(field, model, groupIndex, key, userId) {
		const next = aclCurrentModel(field, model);
		const group = next.groups[groupIndex];
		const id = Number(userId);
		if (!group || !Number.isInteger(id) || id < 0) {
			return;
		}
		const otherKey = key === "add" ? "remove" : "add";
		group[key] = aclList(group[key]).filter(function(value) { return Number(value) !== id; });
		group[otherKey] = aclList(group[otherKey]).filter(function(value) { return Number(value) !== id; });
		group[key].push(id);
		group[key].sort(function(left, right) {
			return aclUserLabel(next, left).localeCompare(aclUserLabel(next, right));
		});
		delete group.addText;
		delete group.removeText;
		next.activeTab = "groups";
		next.selectedGroupIndex = groupIndex;
		aclUpdateModel(field, next, true);
	}

	function aclRemoveMember(field, model, groupIndex, key, userId) {
		const next = aclCurrentModel(field, model);
		const group = next.groups[groupIndex];
		const id = Number(userId);
		if (!group) {
			return;
		}
		group[key] = aclList(group[key]).filter(function(value) {
			return Number(value) !== id;
		});
		delete group.addText;
		delete group.removeText;
		next.activeTab = "groups";
		next.selectedGroupIndex = groupIndex;
		aclUpdateModel(field, next, true);
	}

	function appendAclMemberSection(container, field, model, groupIndex, key, title, editable) {
		const section = document.createElement("div");
		section.className = "modern-dialog-acl-member-section";
		const heading = document.createElement("span");
		heading.className = "modern-dialog-acl-member-heading";
		heading.textContent = title;
		section.appendChild(heading);

		const chips = document.createElement("div");
		chips.className = "modern-dialog-acl-chip-list";
		const group = model.groups[groupIndex] || {};
		const members = aclList(group[key]);
		if (!members.length) {
			const empty = document.createElement("span");
			empty.className = "modern-dialog-acl-empty-inline";
			empty.textContent = "None";
			chips.appendChild(empty);
		}
		members.forEach(function(userId) {
			const chip = document.createElement("span");
			chip.className = "modern-dialog-acl-chip";
			chip.textContent = aclUserLabel(model, userId);
			if (editable) {
				const remove = document.createElement("button");
				remove.type = "button";
				remove.textContent = "Remove";
				remove.addEventListener("click", function(event) {
					event.preventDefault();
					aclRemoveMember(field, model, groupIndex, key, userId);
				});
				chip.appendChild(remove);
			}
			chips.appendChild(chip);
		});
		section.appendChild(chips);

		if (editable) {
			const controls = document.createElement("div");
			controls.className = "modern-dialog-acl-add-member";
			const select = document.createElement("select");
			aclRenderUserSelect(select, model, -1);
			const add = document.createElement("button");
			add.type = "button";
			add.className = "chip-button";
			add.textContent = "Add";
			add.disabled = select.disabled;
			add.addEventListener("click", function(event) {
				event.preventDefault();
				aclAddMember(field, model, groupIndex, key, Number(select.value));
			});
			controls.appendChild(select);
			controls.appendChild(add);
			section.appendChild(controls);
		}
		container.appendChild(section);
	}

	function appendAclGroupEditor(container, field, model) {
		const panel = document.createElement("section");
		panel.className = "modern-dialog-acl-panel is-groups";
		const header = document.createElement("div");
		header.className = "modern-dialog-acl-panel-header";
		const copy = document.createElement("div");
		const title = document.createElement("h3");
		title.className = "modern-dialog-acl-panel-title";
		title.textContent = "Groups";
		const subtitle = document.createElement("p");
		subtitle.className = "modern-dialog-acl-panel-subtitle";
		subtitle.textContent = "Pick a group, then manage its member overrides.";
		copy.appendChild(title);
		copy.appendChild(subtitle);
		header.appendChild(copy);
		appendAclButton(header, "Add group", "", false, function() {
			const next = aclCurrentModel(field, model);
			next.groups = next.groups || [];
			next.groups.push({
				name: "newgroup",
				inherit: true,
				inheritable: true,
				inherited: false,
				add: [],
				remove: [],
				inheritedMembers: []
			});
			next.activeTab = "groups";
			next.selectedGroupIndex = next.groups.length - 1;
			aclUpdateModel(field, next, true);
		});
		panel.appendChild(header);

		const groups = model.groups || [];
		const selectedIndex = aclSelectedIndex(model, "selectedGroupIndex", groups.length);
		const workspace = document.createElement("div");
		workspace.className = "modern-dialog-acl-workspace";
		const nav = document.createElement("div");
		nav.className = "modern-dialog-acl-sidebar";
		if (!groups.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = "No custom groups.";
			nav.appendChild(empty);
		}
		groups.forEach(function(group, groupIndex) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-acl-nav-item" + (groupIndex === selectedIndex ? " is-selected" : "")
				+ (group.inherited ? " is-inherited" : "");
			const name = document.createElement("strong");
			name.textContent = group.name || "unnamed";
			const meta = document.createElement("span");
			meta.textContent = String(aclList(group.add).length) + " added, "
				+ String(aclList(group.remove).length) + " removed"
				+ (group.inherited ? " - inherited" : "");
			button.appendChild(name);
			button.appendChild(meta);
			button.addEventListener("click", function(event) {
				event.preventDefault();
				aclSetSelectedGroup(field, model, groupIndex);
			});
			nav.appendChild(button);
		});
		workspace.appendChild(nav);

		const detail = document.createElement("div");
		detail.className = "modern-dialog-acl-detail";
		if (selectedIndex >= 0) {
			const group = groups[selectedIndex];
			const editable = !group.inherited;
			const form = document.createElement("div");
			form.className = "modern-dialog-acl-detail-grid";
			const nameLabel = document.createElement("label");
			nameLabel.className = "modern-dialog-acl-control";
			const nameCaption = document.createElement("span");
			nameCaption.textContent = "Group";
			const nameInput = document.createElement("input");
			nameInput.type = "text";
			nameInput.value = group.name || "";
			nameInput.disabled = !editable;
			nameInput.addEventListener("input", function() {
				const next = aclCurrentModel(field, model);
				next.groups[selectedIndex].name = nameInput.value;
				next.activeTab = "groups";
				next.selectedGroupIndex = selectedIndex;
				aclUpdateModel(field, next, false);
			});
			nameLabel.appendChild(nameCaption);
			nameLabel.appendChild(nameInput);
			form.appendChild(nameLabel);

			const flags = document.createElement("div");
			flags.className = "modern-dialog-acl-group-flags";
			[
				["inherit", "Inherit members"],
				["inheritable", "Inheritable"]
			].forEach(function(pair) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-check";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = !!group[pair[0]];
				checkbox.disabled = !editable;
				checkbox.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.groups[selectedIndex][pair[0]] = checkbox.checked;
					next.activeTab = "groups";
					next.selectedGroupIndex = selectedIndex;
					aclUpdateModel(field, next, false);
				});
				label.appendChild(checkbox);
				label.appendChild(document.createTextNode(pair[1]));
				flags.appendChild(label);
			});
			form.appendChild(flags);
			const actions = document.createElement("div");
			actions.className = "modern-dialog-acl-card-actions";
			appendAclButton(actions, "Delete group", "is-danger", !editable, function() {
				const next = aclCurrentModel(field, model);
				next.groups.splice(selectedIndex, 1);
				next.selectedGroupIndex = Math.max(0, selectedIndex - 1);
				next.activeTab = "groups";
				aclUpdateModel(field, next, true);
			});
			form.appendChild(actions);
			detail.appendChild(form);

			appendAclMemberSection(detail, field, model, selectedIndex, "inheritedMembers", "Inherited members", false);
			appendAclMemberSection(detail, field, model, selectedIndex, "add", "Add members", editable);
			appendAclMemberSection(detail, field, model, selectedIndex, "remove", "Remove members", editable && group.inherit !== false);
		}
		workspace.appendChild(detail);
		panel.appendChild(workspace);
		container.appendChild(panel);
	}

	function appendAclPermissionControls(container, field, model, rule, ruleIndex, inherited) {
		const configured = [];
		(model.permissions || []).forEach(function(permission) {
			const bit = Number(permission.id) || 0;
			const state = aclPermissionState(rule, bit);
			if (state !== "unset") {
				configured.push({ bit: bit, state: state, label: permission.label || String(bit) });
			}
		});

		const configuredWrap = document.createElement("div");
		configuredWrap.className = "modern-dialog-acl-configured-permissions";
		const heading = document.createElement("span");
		heading.className = "modern-dialog-acl-member-heading";
		heading.textContent = "Configured permissions";
		configuredWrap.appendChild(heading);
		const chips = document.createElement("div");
		chips.className = "modern-dialog-acl-chip-list";
		if (!configured.length) {
			const empty = document.createElement("span");
			empty.className = "modern-dialog-acl-empty-inline";
			empty.textContent = "No permissions set.";
			chips.appendChild(empty);
		}
		configured.forEach(function(item) {
			const chip = document.createElement("span");
			chip.className = "modern-dialog-acl-chip is-" + item.state;
			chip.textContent = (item.state === "allow" ? "Allow " : "Deny ") + item.label;
			if (!inherited) {
				const clear = document.createElement("button");
				clear.type = "button";
				clear.textContent = "Clear";
				clear.addEventListener("click", function(event) {
					event.preventDefault();
					const next = aclCurrentModel(field, model);
					aclSetPermissionState(next.acls[ruleIndex], item.bit, "unset");
					next.activeTab = "rules";
					next.selectedRuleIndex = ruleIndex;
					aclUpdateModel(field, next, true);
				});
				chip.appendChild(clear);
			}
			chips.appendChild(chip);
		});
		configuredWrap.appendChild(chips);
		container.appendChild(configuredWrap);

		const unsetPermissions = (model.permissions || []).filter(function(permission) {
			return aclPermissionState(rule, Number(permission.id) || 0) === "unset";
		});
		const addRow = document.createElement("div");
		addRow.className = "modern-dialog-acl-add-permission";
		const select = document.createElement("select");
		unsetPermissions.forEach(function(permission) {
			const option = document.createElement("option");
			option.value = String(Number(permission.id) || 0);
			option.textContent = permission.label || String(permission.id);
			select.appendChild(option);
		});
		select.disabled = inherited || !unsetPermissions.length;
		addRow.appendChild(select);
		[
			["allow", "Allow"],
			["deny", "Deny"]
		].forEach(function(pair) {
			appendAclButton(addRow, pair[1], pair[0] === "deny" ? "is-danger" : "", inherited || select.disabled, function() {
				const next = aclCurrentModel(field, model);
				aclSetPermissionState(next.acls[ruleIndex], Number(select.value), pair[0]);
				next.activeTab = "rules";
				next.selectedRuleIndex = ruleIndex;
				aclUpdateModel(field, next, true);
			});
		});
		appendAclButton(addRow, rule.showAllPermissions ? "Hide all" : "Show all", "", false, function() {
			const next = aclCurrentModel(field, model);
			next.acls[ruleIndex].showAllPermissions = !rule.showAllPermissions;
			next.activeTab = "rules";
			next.selectedRuleIndex = ruleIndex;
			aclUpdateModel(field, next, true);
		});
		container.appendChild(addRow);

		if (!rule.showAllPermissions) {
			return;
		}
		const matrix = document.createElement("div");
		matrix.className = "modern-dialog-acl-permission-matrix";
		(model.permissions || []).forEach(function(permission) {
			const bit = Number(permission.id) || 0;
			const state = aclPermissionState(rule, bit);
			const row = document.createElement("div");
			row.className = "modern-dialog-acl-permission-row is-" + state;
			const name = document.createElement("span");
			name.className = "modern-dialog-acl-permission-name";
			name.textContent = permission.label || String(permission.id);
			if (permission.hint) {
				name.title = String(permission.hint);
			}
			row.appendChild(name);
			const choices = document.createElement("div");
			choices.className = "modern-dialog-acl-state";
			[
				["unset", "Not set"],
				["deny", "Deny"],
				["allow", "Allow"]
			].forEach(function(choice) {
				const button = document.createElement("button");
				button.type = "button";
				button.className = "modern-dialog-acl-state-button is-" + choice[0]
					+ (state === choice[0] ? " is-selected" : "");
				button.textContent = choice[1];
				button.disabled = inherited;
				button.addEventListener("click", function(event) {
					event.preventDefault();
					const next = aclCurrentModel(field, model);
					aclSetPermissionState(next.acls[ruleIndex], bit, choice[0]);
					next.activeTab = "rules";
					next.selectedRuleIndex = ruleIndex;
					next.acls[ruleIndex].showAllPermissions = true;
					aclUpdateModel(field, next, true);
				});
				choices.appendChild(button);
			});
			row.appendChild(choices);
			matrix.appendChild(row);
		});
		container.appendChild(matrix);
	}

	function appendAclRuleEditor(container, field, model) {
		const panel = document.createElement("section");
		panel.className = "modern-dialog-acl-panel is-rules";
		const header = document.createElement("div");
		header.className = "modern-dialog-acl-panel-header";
		const copy = document.createElement("div");
		const title = document.createElement("h3");
		title.className = "modern-dialog-acl-panel-title";
		title.textContent = "Access rules";
		const subtitle = document.createElement("p");
		subtitle.className = "modern-dialog-acl-panel-subtitle";
		subtitle.textContent = "Choose a rule on the left, then edit target, scope, and configured permissions.";
		copy.appendChild(title);
		copy.appendChild(subtitle);
		header.appendChild(copy);
		appendAclButton(header, "Add rule", "", false, function() {
			const next = aclCurrentModel(field, model);
			next.acls = next.acls || [];
			next.acls.push({
				targetType: "group",
				target: "all",
				userId: -1,
				applyHere: true,
				applySubs: true,
				inherited: false,
				allow: [],
				deny: []
			});
			next.activeTab = "rules";
			next.selectedRuleIndex = next.acls.length - 1;
			aclUpdateModel(field, next, true);
		});
		panel.appendChild(header);

		const rules = model.acls || [];
		const selectedIndex = aclSelectedIndex(model, "selectedRuleIndex", rules.length);
		const workspace = document.createElement("div");
		workspace.className = "modern-dialog-acl-workspace";
		const nav = document.createElement("div");
		nav.className = "modern-dialog-acl-sidebar";
		if (!rules.length) {
			const empty = document.createElement("p");
			empty.className = "modern-dialog-note";
			empty.textContent = "No ACL rules.";
			nav.appendChild(empty);
		}
		rules.forEach(function(rule, ruleIndex) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-acl-nav-item" + (ruleIndex === selectedIndex ? " is-selected" : "")
				+ (rule.inherited ? " is-inherited" : "");
			const name = document.createElement("strong");
			name.textContent = aclRuleTitle(model, rule);
			const meta = document.createElement("span");
			meta.textContent = aclRuleScopeLabel(rule) + (rule.inherited ? " - inherited" : "");
			const badges = document.createElement("span");
			badges.className = "modern-dialog-acl-nav-badges";
			badges.textContent = aclPermissionSummary(model, rule.allow, "No allows") + " / "
				+ aclPermissionSummary(model, rule.deny, "No denies");
			button.appendChild(name);
			button.appendChild(meta);
			button.appendChild(badges);
			button.addEventListener("click", function(event) {
				event.preventDefault();
				aclSetSelectedRule(field, model, ruleIndex);
			});
			nav.appendChild(button);
		});
		workspace.appendChild(nav);

		const detail = document.createElement("div");
		detail.className = "modern-dialog-acl-detail";
		if (selectedIndex >= 0) {
			const rule = rules[selectedIndex];
			const inherited = !!rule.inherited;
			const titleRow = document.createElement("div");
			titleRow.className = "modern-dialog-acl-detail-title";
			const titleText = document.createElement("div");
			const target = document.createElement("strong");
			target.textContent = aclRuleTitle(model, rule);
			const meta = document.createElement("span");
			meta.className = "modern-dialog-acl-muted";
			meta.textContent = aclRuleScopeLabel(rule) + (inherited ? " - inherited" : "");
			titleText.appendChild(target);
			titleText.appendChild(meta);
			titleRow.appendChild(titleText);
			const actions = document.createElement("div");
			actions.className = "modern-dialog-acl-rule-actions";
			appendAclButton(actions, "Up", "", inherited || selectedIndex === 0, function() {
				aclMoveRule(field, model, selectedIndex, -1);
			});
			appendAclButton(actions, "Down", "", inherited || selectedIndex >= rules.length - 1, function() {
				aclMoveRule(field, model, selectedIndex, 1);
			});
			appendAclButton(actions, "Delete", "is-danger", inherited, function() {
				const next = aclCurrentModel(field, model);
				next.acls.splice(selectedIndex, 1);
				next.selectedRuleIndex = Math.max(0, selectedIndex - 1);
				next.activeTab = "rules";
				aclUpdateModel(field, next, true);
			});
			titleRow.appendChild(actions);
			detail.appendChild(titleRow);

			const targetRow = document.createElement("div");
			targetRow.className = "modern-dialog-acl-rule-target";
			const targetType = document.createElement("select");
			["group", "user"].forEach(function(type) {
				const option = document.createElement("option");
				option.value = type;
				option.textContent = type === "group" ? "Group" : "User";
				targetType.appendChild(option);
			});
			targetType.value = rule.targetType || "group";
			targetType.disabled = inherited;
			targetType.addEventListener("change", function() {
				const next = aclCurrentModel(field, model);
				next.acls[selectedIndex].targetType = targetType.value;
				if (targetType.value === "group") {
					next.acls[selectedIndex].target = "all";
					next.acls[selectedIndex].userId = -1;
				} else {
					const users = aclUserOptions(next);
					const firstUser = users.length ? aclOptionId(users[0]) : -1;
					next.acls[selectedIndex].userId = firstUser;
					next.acls[selectedIndex].target = firstUser >= 0 ? aclUserLabel(next, firstUser) : "";
				}
				next.activeTab = "rules";
				next.selectedRuleIndex = selectedIndex;
				aclUpdateModel(field, next, true);
			});
			targetRow.appendChild(targetType);

			if ((rule.targetType || "group") === "user") {
				const userSelect = document.createElement("select");
				aclRenderUserSelect(userSelect, model, rule.userId);
				userSelect.disabled = inherited || userSelect.disabled;
				userSelect.addEventListener("change", function() {
					const id = Number(userSelect.value);
					const next = aclCurrentModel(field, model);
					next.acls[selectedIndex].userId = id;
					next.acls[selectedIndex].target = aclUserLabel(next, id);
					next.activeTab = "rules";
					next.selectedRuleIndex = selectedIndex;
					aclUpdateModel(field, next, true);
				});
				targetRow.appendChild(userSelect);
			} else {
				const groupSelect = document.createElement("select");
				aclGroupTargetOptions(model, rule.target).forEach(function(item) {
					const option = document.createElement("option");
					option.value = item.value;
					option.textContent = item.label;
					groupSelect.appendChild(option);
				});
				groupSelect.value = String(rule.target || "all");
				groupSelect.disabled = inherited;
				groupSelect.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.acls[selectedIndex].target = groupSelect.value;
					next.acls[selectedIndex].userId = -1;
					next.activeTab = "rules";
					next.selectedRuleIndex = selectedIndex;
					aclUpdateModel(field, next, true);
				});
				targetRow.appendChild(groupSelect);
			}

			const scopes = document.createElement("div");
			scopes.className = "modern-dialog-acl-scope";
			[
				["applyHere", "This room"],
				["applySubs", "Sub rooms"]
			].forEach(function(pair) {
				const label = document.createElement("label");
				label.className = "modern-dialog-acl-check";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = rule[pair[0]] !== false;
				checkbox.disabled = inherited;
				checkbox.addEventListener("change", function() {
					const next = aclCurrentModel(field, model);
					next.acls[selectedIndex][pair[0]] = checkbox.checked;
					next.activeTab = "rules";
					next.selectedRuleIndex = selectedIndex;
					aclUpdateModel(field, next, false);
				});
				label.appendChild(checkbox);
				label.appendChild(document.createTextNode(pair[1]));
				scopes.appendChild(label);
			});
			targetRow.appendChild(scopes);
			detail.appendChild(targetRow);
			appendAclPermissionControls(detail, field, model, rule, selectedIndex, inherited);
		}
		workspace.appendChild(detail);
		panel.appendChild(workspace);
		container.appendChild(panel);
	}

	function appendModernAclEditor(container, field, errors) {
		const model = aclCloneModel(field.value || {});
		model.acls = model.acls || [];
		model.groups = model.groups || [];
		if (!model.activeTab) {
			model.activeTab = "rules";
		}
		const wrap = document.createElement("div");
		wrap.className = "modern-dialog-acl-editor";

		const policy = document.createElement("section");
		policy.className = "modern-dialog-acl-policy";
		const policyCopy = document.createElement("div");
		const policyTitle = document.createElement("h3");
		policyTitle.className = "modern-dialog-acl-panel-title";
		policyTitle.textContent = "Room policy";
		const policySummary = document.createElement("p");
		policySummary.className = "modern-dialog-acl-panel-subtitle";
		policySummary.textContent = String(model.groups.length) + " groups, " + String(model.acls.length) + " access rules";
		policyCopy.appendChild(policyTitle);
		policyCopy.appendChild(policySummary);
		policy.appendChild(policyCopy);

		const policyControls = document.createElement("div");
		policyControls.className = "modern-dialog-acl-policy-controls";
		const inherit = document.createElement("label");
		inherit.className = "modern-dialog-acl-check";
		const inheritBox = document.createElement("input");
		inheritBox.type = "checkbox";
		inheritBox.checked = model.inheritAcls !== false;
		inheritBox.addEventListener("change", function() {
			const next = aclCurrentModel(field, model);
			next.inheritAcls = inheritBox.checked;
			aclUpdateModel(field, next, false);
		});
		inherit.appendChild(inheritBox);
		inherit.appendChild(document.createTextNode("Inherit parent ACLs"));
		policyControls.appendChild(inherit);

		const passwordLabel = document.createElement("label");
		passwordLabel.className = "modern-dialog-acl-control";
		const passwordCaption = document.createElement("span");
		passwordCaption.textContent = "Room password";
		const passwordInput = document.createElement("input");
		passwordInput.type = "text";
		passwordInput.value = model.password || "";
		passwordInput.placeholder = "Optional join password";
		passwordInput.addEventListener("input", function() {
			const next = aclCurrentModel(field, model);
			next.password = passwordInput.value;
			aclUpdateModel(field, next, false);
		});
		passwordLabel.appendChild(passwordCaption);
		passwordLabel.appendChild(passwordInput);
		policyControls.appendChild(passwordLabel);
		policy.appendChild(policyControls);
		wrap.appendChild(policy);

		const tabs = document.createElement("div");
		tabs.className = "modern-dialog-acl-tabs";
		[
			["rules", "Access rules", String(model.acls.length)],
			["groups", "Groups", String(model.groups.length)]
		].forEach(function(tab) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-acl-tab" + (model.activeTab === tab[0] ? " is-selected" : "");
			button.textContent = tab[1] + " (" + tab[2] + ")";
			button.addEventListener("click", function(event) {
				event.preventDefault();
				aclSetActiveTab(field, model, tab[0]);
			});
			tabs.appendChild(button);
		});
		wrap.appendChild(tabs);

		if (model.activeTab === "groups") {
			appendAclGroupEditor(wrap, field, model);
		} else {
			appendAclRuleEditor(wrap, field, model);
		}

		const errorText = errors && field && field.id ? errors[field.id] : "";
		if (errorText) {
			const error = document.createElement("span");
			error.className = "modern-dialog-field-error";
			error.textContent = errorText;
			wrap.appendChild(error);
		}
		container.appendChild(wrap);
	}

	function appendModernDialogField(container, field, errors) {
		const type = String(field && field.type || "text");
		if (type === "hidden") {
			return;
		}
		if (type === "note") {
			const note = document.createElement("p");
			note.className = "modern-dialog-note";
			note.textContent = field.text || "";
			container.appendChild(note);
			return;
		}
		if (type === "resultList") {
			appendModernResultList(container, field);
			return;
		}
		if (type === "aclEditor") {
			appendModernAclEditor(container, field, errors);
			return;
		}

		const row = document.createElement("label");
		row.className = "modern-dialog-field is-" + type;
		const fieldTooltip = String(field.tooltip || field.hint || "");
		if (fieldTooltip) {
			row.title = fieldTooltip;
		}
		const label = document.createElement("span");
		label.className = "modern-dialog-field-label";
		label.textContent = field.label || field.id || "Field";
		if (fieldTooltip) {
			label.title = fieldTooltip;
		}
		row.appendChild(label);

		let input = null;
		let syncSelectHint = null;
		if (type === "readonly") {
			const value = document.createElement("span");
			value.className = "modern-dialog-readonly-value";
			if (field.monospace) {
				value.classList.add("is-monospace");
			}
			value.textContent = field.value == null ? "" : String(field.value);
			row.appendChild(value);
		} else if (type === "checkbox") {
			input = document.createElement("input");
			input.type = "checkbox";
			input.checked = !!field.value;
		} else if (type === "select") {
			input = document.createElement("select");
			const selectedValue = field.value == null ? "" : String(field.value);
			let hasSelectedValue = false;
			let hasOptionHint = false;
			(field.options || []).forEach(function(option) {
				const item = document.createElement("option");
				item.value = String(option.value);
				item.textContent = option.label || String(option.value);
				item.disabled = option.enabled === false;
				if (item.value === selectedValue) {
					hasSelectedValue = true;
				}
				const optionHint = modernDialogOptionHint(option);
				if (optionHint) {
					item.title = optionHint;
					item.dataset.optionHint = optionHint;
					hasOptionHint = true;
				}
				input.appendChild(item);
			});
			if (field.value != null && !hasSelectedValue) {
				const fallback = document.createElement("option");
				fallback.value = selectedValue;
				fallback.textContent = field.valueLabel || selectedValue;
				fallback.hidden = true;
				input.appendChild(fallback);
			}
			input.value = selectedValue;
			if (hasOptionHint) {
				syncSelectHint = function() {
					syncModernDialogSelectHint(input, fieldTooltip);
				};
				input.addEventListener("change", syncSelectHint);
			}
		} else if (type === "range") {
			const rangeWrap = document.createElement("div");
			rangeWrap.className = "modern-dialog-range";
			input = document.createElement("input");
			input.type = "range";
			input.min = String(field.min || 0);
			input.max = String(field.max || 100);
			input.step = String(field.step || 1);
			input.value = String(field.value || 0);
			const value = document.createElement("span");
			value.className = "modern-dialog-range-value";
			const syncValue = function() {
				value.textContent = String(input.value) + String(field.suffix || "");
				setRangeProgress(input);
				updateVoiceMeterThreshold(String(field.id || ""), input.value);
			};
			input.addEventListener("input", syncValue);
			syncValue();
			rangeWrap.appendChild(input);
			rangeWrap.appendChild(value);
			row.appendChild(rangeWrap);
		} else if (type === "voiceMeter") {
			const meter = document.createElement("div");
			meter.className = "modern-dialog-voice-meter";
			meter.dataset.vadSource = String(field.vadSource || 0);
			meter.dataset.silenceThreshold = String(field.silenceThreshold || 0);
			meter.dataset.speechThreshold = String(field.speechThreshold || 0);
			meter.dataset.active = field.active === false ? "false" : "true";
			meter.dataset.calibrationState = String(field.calibrationState || "idle");
			meter.dataset.calibrationStatusText = String(field.calibrationStatusText || "");
			meter.dataset.loopbackMode = String(field.loopbackMode || 0);
			meter.dataset.serverConnected = "false";
			meter.dataset.replayStartActionId = String(field.replayStartActionId || "");
			meter.dataset.replayStopActionId = String(field.replayStopActionId || "");
			meter.dataset.maxAmplification = String(field.maxAmplification || 0);
			meter.dataset.noiseCancelMode = String(field.noiseCancelMode || 0);
			meter.dataset.inputGateMode = String(field.inputGateMode || 0);
			meter.dataset.speexNoiseStrength = String(field.speexNoiseStrength || 14);
			meter.dataset.neuralCleanupAvailable = field.neuralCleanupAvailable ? "true" : "false";
			meter.dataset.recommendedVadSource = String(field.recommendedVadSource == null ? 2 : field.recommendedVadSource);
			meter.dataset.recommendedInputGateMode = String(field.recommendedInputGateMode == null
				? 1
				: field.recommendedInputGateMode);
			meter.dataset.recommendedNoiseCancelMode = String(field.recommendedNoiseCancelMode == null
				? field.noiseCancelMode || 0
				: field.recommendedNoiseCancelMode);
			meter.dataset.recommendedMaxAmplification = String(field.recommendedMaxAmplification == null
				? field.maxAmplification || 0
				: field.recommendedMaxAmplification);
			if (fieldTooltip) {
				meter.title = fieldTooltip;
			}

			const header = document.createElement("div");
			header.className = "modern-dialog-voice-meter-header";
			const source = document.createElement("span");
			source.className = "modern-dialog-voice-meter-source";
			source.textContent = field.sourceLabel || "Input";
			const currentValue = document.createElement("span");
			currentValue.className = "modern-dialog-voice-meter-value";
			header.appendChild(source);
			header.appendChild(currentValue);
			if (field.calibrationActionId) {
				const autoButton = document.createElement("button");
				autoButton.type = "button";
				autoButton.className = "chip-button modern-dialog-voice-meter-auto";
				autoButton.textContent = field.calibrationLabel || "Audio setup";
				autoButton.dataset.defaultLabel = autoButton.textContent;
				if (field.calibrationTooltip) {
					autoButton.title = String(field.calibrationTooltip);
				}
				autoButton.addEventListener("click", function(event) {
					event.preventDefault();
					startVoiceActivationCalibration(meter, field);
				});
				header.appendChild(autoButton);
			}
			if (field.replayStartActionId && field.replayStopActionId) {
				const replayButton = document.createElement("button");
				replayButton.type = "button";
				replayButton.className = "chip-button modern-dialog-voice-meter-replay";
				replayButton.textContent = field.replayLabel || "Replay";
				if (field.replayTooltip) {
					replayButton.title = String(field.replayTooltip);
				}
				replayButton.addEventListener("click", function(event) {
					event.preventDefault();
					toggleVoiceReplay(meter);
				});
				header.appendChild(replayButton);
			}

			const track = document.createElement("div");
			track.className = "modern-dialog-voice-meter-track";
			const fill = document.createElement("span");
			fill.className = "modern-dialog-voice-meter-fill";
			const silenceMarker = document.createElement("span");
			silenceMarker.className = "modern-dialog-voice-meter-marker is-silence";
			const speechMarker = document.createElement("span");
			speechMarker.className = "modern-dialog-voice-meter-marker is-speech";
			track.appendChild(fill);
			track.appendChild(silenceMarker);
			track.appendChild(speechMarker);

			const footer = document.createElement("div");
			footer.className = "modern-dialog-voice-meter-footer";
			const silence = document.createElement("span");
			silence.className = "modern-dialog-voice-meter-silence-label";
			silence.textContent = "Stop below " + String(field.silenceThreshold || 0) + "%";
			const status = document.createElement("span");
			status.className = "modern-dialog-voice-meter-status";
			const speech = document.createElement("span");
			speech.className = "modern-dialog-voice-meter-speech-label";
			speech.textContent = "Start above " + String(field.speechThreshold || 0) + "%";
			footer.appendChild(silence);
			footer.appendChild(status);
			footer.appendChild(speech);

			const coach = document.createElement("div");
			coach.className = "modern-dialog-voice-meter-coach";
			const coachTrack = document.createElement("span");
			coachTrack.className = "modern-dialog-voice-meter-coach-track";
			const coachFill = document.createElement("span");
			coachFill.className = "modern-dialog-voice-meter-coach-fill";
			coachTrack.appendChild(coachFill);
			const coachText = document.createElement("span");
			coachText.className = "modern-dialog-voice-meter-coach-text";
			coach.appendChild(coachTrack);
			coach.appendChild(coachText);
			const coachActions = document.createElement("span");
			coachActions.className = "modern-dialog-voice-meter-coach-actions";
			coachActions.style.display = "none";
			const startButton = document.createElement("button");
			startButton.type = "button";
			startButton.className = "chip-button modern-dialog-voice-meter-coach-start";
			startButton.textContent = "Start";
			startButton.addEventListener("click", function(event) {
				event.preventDefault();
				beginVoiceActivationCalibration(meter);
			});
			const cancelButton = document.createElement("button");
			cancelButton.type = "button";
			cancelButton.className = "chip-button modern-dialog-voice-meter-coach-cancel";
			cancelButton.textContent = "Cancel";
			cancelButton.addEventListener("click", function(event) {
				event.preventDefault();
				clearVoiceCalibration();
				refreshAudioInputMeters();
			});
			coachActions.appendChild(startButton);
			coachActions.appendChild(cancelButton);
			coach.appendChild(coachActions);

			meter.appendChild(header);
			meter.appendChild(track);
			meter.appendChild(footer);
			meter.appendChild(coach);
			updateVoiceMeterElement(meter, null);
			row.appendChild(meter);
		} else if (type === "textarea") {
			input = document.createElement("textarea");
			input.value = field.value == null ? "" : String(field.value);
			input.rows = Math.max(2, Number(field.rows) || 4);
		} else if (type === "pathPicker") {
			const picker = document.createElement("div");
			picker.className = "modern-dialog-path-picker";
			input = document.createElement("input");
			input.type = "text";
			input.value = field.value == null ? "" : String(field.value);
			const browse = document.createElement("button");
			browse.type = "button";
			browse.className = "chip-button";
			browse.textContent = field.browseLabel || "Browse";
			browse.disabled = field.enabled === false;
			browse.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernDialogAction(field.browseActionId || "", { fieldId: field.id || "" });
			});
			picker.appendChild(input);
			picker.appendChild(browse);
			row.appendChild(picker);
		} else {
			input = document.createElement("input");
			input.type = type === "password" ? "password" : (type === "number" ? "number" : "text");
			input.value = field.value == null ? "" : String(field.value);
			if (type === "number") {
				if (field.min != null) {
					input.min = String(field.min);
				}
				if (field.max != null) {
					input.max = String(field.max);
				}
				if (field.step != null) {
					input.step = String(field.step);
				}
			}
		}

		if (input && type !== "range" && type !== "pathPicker") {
			row.appendChild(input);
		}
		if (input) {
			input.dataset.modernDialogFieldId = String(field.id || "");
			input.disabled = field.enabled === false;
			if (fieldTooltip) {
				input.title = fieldTooltip;
			}
			input.addEventListener(type === "checkbox" || type === "select" || type === "range" ? "change" : "input", function() {
				updateModernDialogField(field, input);
			});
			if (syncSelectHint) {
				syncSelectHint();
			}
		}

		if (field.hint) {
			const hint = document.createElement("span");
			hint.className = "modern-dialog-field-hint";
			hint.textContent = String(field.hint);
			row.appendChild(hint);
		}

		const errorText = errors && field && field.id ? errors[field.id] : "";
		if (errorText) {
			const error = document.createElement("span");
			error.className = "modern-dialog-field-error";
			error.textContent = errorText;
			row.appendChild(error);
		}
		container.appendChild(row);
	}

	function appendModernDialogFavorites(container, dialog) {
		if (!dialog || !Array.isArray(dialog.favorites) || !dialog.favorites.length) {
			return;
		}

		const section = document.createElement("section");
		section.className = "modern-dialog-section modern-dialog-favorites";
		const title = document.createElement("h3");
		title.className = "modern-dialog-section-title";
		title.textContent = "Saved servers";
		section.appendChild(title);

		const list = document.createElement("div");
		list.className = "modern-dialog-favorite-list";
		dialog.favorites.forEach(function(favorite) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-favorite" + (favorite.selected ? " is-selected" : "");
			button.innerHTML = "<span class=\"modern-dialog-favorite-label\"></span><span class=\"modern-dialog-favorite-subtitle\"></span>";
			button.querySelector(".modern-dialog-favorite-label").textContent = favorite.label || favorite.host || "Server";
			button.querySelector(".modern-dialog-favorite-subtitle").textContent = favorite.subtitle || "";
			button.addEventListener("click", function() {
				invokeModernDialogAction("selectFavorite", { index: Number(favorite.index) || 0 });
			});
			list.appendChild(button);
		});
		section.appendChild(list);
		container.appendChild(section);
	}

	function renderModernDialog() {
		const dialog = modernDialogState || {};
		const open = !!dialog.open;
		if (!refs.modernDialogLayer) {
			return;
		}
		closeModernDialogSelect();

		const activeElement = document.activeElement;
		const activeInDialog = !!(activeElement && refs.modernDialog && refs.modernDialog.contains(activeElement));
		const focusState = activeInDialog && activeElement.dataset
			? {
				fieldId: activeElement.dataset.modernDialogFieldId || "",
				hasSelection: typeof activeElement.selectionStart === "number"
					&& typeof activeElement.selectionEnd === "number",
				selectionStart: activeElement.selectionStart,
				selectionEnd: activeElement.selectionEnd
			}
			: null;
		const opening = open && !modernDialogRenderedOpen;
		const closing = !open && modernDialogRenderedOpen;
		if (opening && activeElement && !activeInDialog) {
			modernDialogReturnFocus = activeElement;
		}

		refs.modernDialogLayer.classList.toggle("hidden", !open);
		refs.modernDialogLayer.setAttribute("aria-hidden", open ? "false" : "true");
		if (!open) {
			refs.modernDialogBody.innerHTML = "";
			refs.modernDialogActions.innerHTML = "";
			modernDialogRenderedOpen = false;
			syncAudioInputMeterTimer();
			if (closing) {
				restoreModernDialogReturnFocus();
			}
			return;
		}

		refs.modernDialog.className = "modern-dialog" + (dialog.tone ? " is-" + dialog.tone : "")
			+ (dialog.kind ? " is-" + dialog.kind : "");
		refs.modernDialogEyebrow.textContent = dialog.kind === "settings" ? "Settings" : "Mumble";
		refs.modernDialogTitle.textContent = dialog.title || "Dialog";
		refs.modernDialogSubtitle.textContent = dialog.subtitle || "";
		refs.modernDialogBody.innerHTML = "";
		refs.modernDialogActions.innerHTML = "";

		if (Array.isArray(dialog.pages) && dialog.pages.length) {
			const tabs = document.createElement("div");
			tabs.className = "modern-dialog-tabs";
			dialog.pages.forEach(function(page) {
				const tab = document.createElement("button");
				tab.type = "button";
				tab.className = "modern-dialog-tab" + (page.selected ? " is-selected" : "");
				tab.textContent = page.label || page.id || "Page";
				tab.addEventListener("click", function() {
					invokeModernDialogAction("selectPage", { pageId: page.id || "" });
				});
				tabs.appendChild(tab);
			});
			refs.modernDialogBody.appendChild(tabs);
		}

		appendModernDialogFavorites(refs.modernDialogBody, dialog);

		const errors = dialog.errors || {};
		(dialog.sections || []).forEach(function(section) {
			const sectionElement = document.createElement("section");
			sectionElement.className = "modern-dialog-section";
			if (section.title) {
				const title = document.createElement("h3");
				title.className = "modern-dialog-section-title";
				title.textContent = section.title;
				sectionElement.appendChild(title);
			}
			(section.fields || []).forEach(function(field) {
				appendModernDialogField(sectionElement, field, errors);
			});
			refs.modernDialogBody.appendChild(sectionElement);
		});

		(dialog.actions || []).forEach(function(action) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button modern-dialog-action"
				+ (action.tone ? " is-" + action.tone : "")
				+ (dialog.primaryActionId === action.id ? " is-primary" : "");
			button.disabled = action.enabled === false;
			button.textContent = action.label || action.id || "Action";
			button.dataset.modernDialogActionId = String(action.id || "");
			button.addEventListener("click", function() {
				invokeModernDialogAction(action.id || "", {});
			});
			refs.modernDialogActions.appendChild(button);
		});
		enhanceModernDialogSelects(refs.modernDialogBody);
		syncAudioInputMeterTimer();
		modernDialogRenderedOpen = true;
		if (focusState && restoreModernDialogFocus(focusState)) {
			return;
		}
		if (opening || !activeInDialog) {
			focusFirstModernDialogControl();
		}
	}

	function hideAppMenu() {
		appMenuOpen = false;
		refs.appMenu.classList.add("hidden");
		refs.appMenu.setAttribute("aria-hidden", "true");
		refs.appMenu.innerHTML = "";
		refs.settingsButton.setAttribute("aria-expanded", "false");
	}

	function positionAppMenu() {
		if (refs.appMenu.classList.contains("hidden")) {
			return;
		}

		const anchorBounds = refs.settingsButton.getBoundingClientRect();
		const menuBounds = refs.appMenu.getBoundingClientRect();
		const left = Math.max(8, Math.min(anchorBounds.right - menuBounds.width, window.innerWidth - menuBounds.width - 8));
		const belowTop = anchorBounds.bottom + 10;
		const aboveTop = anchorBounds.top - menuBounds.height - 10;
		const top = (belowTop + menuBounds.height <= window.innerHeight - 8)
			? belowTop
			: Math.max(8, aboveTop);
		refs.appMenu.style.left = left + "px";
		refs.appMenu.style.top = top + "px";
	}

	function renderAppMenu(snapshot) {
		const app = snapshot.app || {};
		const menus = resolvedAppMenus(app);

		refs.appMenu.innerHTML = "";

		const header = document.createElement("div");
		header.className = "app-menu-header";

		const eyebrow = document.createElement("p");
		eyebrow.className = "app-menu-eyebrow";
		eyebrow.textContent = "Mumble menu";

		const title = document.createElement("p");
		title.className = "app-menu-title";
		title.textContent = app.serverTitle || "Mumble";

		const subtitle = document.createElement("p");
		subtitle.className = "app-menu-subtitle";
		subtitle.textContent = "Classic main menus collected behind the gear button.";

		header.appendChild(eyebrow);
		header.appendChild(title);
		header.appendChild(subtitle);
		refs.appMenu.appendChild(header);

		menus.forEach(function(menu) {
			if (!menu || !Array.isArray(menu.items) || !menu.items.length) {
				return;
			}

			const section = document.createElement("section");
			section.className = "app-menu-section";

			const heading = document.createElement("div");
			heading.className = "app-menu-section-heading";
			heading.textContent = menu.label || "Menu";

			const body = document.createElement("div");
			body.className = "app-menu-section-body";

			actionItemsFromActionStates(menu.items, {
				invokeAction: function(actionId) {
					hideAppMenu();
					notifyBridge("invokeAppAction", actionId);
				}
			}).forEach(function(item) {
				appendActionPanelItem(body, item, "menu", false);
			});

			section.appendChild(heading);
			section.appendChild(body);
			refs.appMenu.appendChild(section);
		});

		appMenuOpen = true;
		refs.appMenu.classList.remove("hidden");
		refs.appMenu.setAttribute("aria-hidden", "false");
		refs.settingsButton.setAttribute("aria-expanded", "true");
		positionAppMenu();
	}

	function toggleAppMenu(forceOpen) {
		const nextOpen = typeof forceOpen === "boolean" ? forceOpen : !appMenuOpen;
		if (!nextOpen) {
			hideAppMenu();
			return;
		}

		const snapshot = getSnapshot();
		hideContextMenu();
		hideSelfMenu();
		openMenuId = null;
		openMenuPinned = false;
		renderMenus(resolvedAppMenus(snapshot.app || {}));
		renderAppMenu(snapshot);
	}

	function hideSelfMenu() {
		selfMenuOpen = false;
		refs.selfMenu.classList.add("hidden");
		refs.selfMenu.setAttribute("aria-hidden", "true");
		refs.selfMenu.innerHTML = "";
		refs.selfCard.setAttribute("aria-expanded", "false");
	}

	function positionSelfMenu() {
		if (refs.selfMenu.classList.contains("hidden")) {
			return;
		}

		const anchorBounds = refs.selfCard.getBoundingClientRect();
		const menuBounds = refs.selfMenu.getBoundingClientRect();
		const left = Math.max(8, Math.min(anchorBounds.right - menuBounds.width + 12, window.innerWidth - menuBounds.width - 8));
		const top = Math.max(8, anchorBounds.top - menuBounds.height - 12);
		refs.selfMenu.style.left = left + "px";
		refs.selfMenu.style.top = top + "px";
	}

	function renderSelfMenu(snapshot) {
		const app = snapshot.app || {};
		const menuState = app.selfMenu || {};

		refs.selfMenu.innerHTML = "";

		const header = document.createElement("div");
		header.className = "self-menu-header";

		const avatar = document.createElement("div");
		avatar.className = "self-menu-avatar" + (menuState.statusTone ? " is-" + menuState.statusTone : "");
		styleAvatar(avatar, menuState.name || app.selfName || "You", true, app.selfAvatarUrl || "");

		const copy = document.createElement("div");
		copy.className = "self-menu-copy";

		const title = document.createElement("p");
		title.className = "self-menu-title";
		title.textContent = menuState.name || app.selfName || "You";

		const status = document.createElement("p");
		status.className = "self-menu-status" + (menuState.statusTone ? " is-" + menuState.statusTone : "");
		status.textContent = menuState.statusLabel || app.selfStatusLabel || "Offline";

		copy.appendChild(title);
		copy.appendChild(status);
		header.appendChild(avatar);
		header.appendChild(copy);
		refs.selfMenu.appendChild(header);

		(menuState.presence || []).forEach(function(item) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "self-menu-row is-presence"
				+ (item.checked ? " is-selected" : "")
				+ (item.tone ? " is-" + item.tone : "");
			button.disabled = item.enabled === false;
			if (item.hint) {
				button.title = item.hint;
			}

			const dot = document.createElement("span");
			dot.className = "self-menu-dot";

			const label = document.createElement("span");
			label.className = "self-menu-label";
			label.textContent = item.label || "Status";

			button.appendChild(dot);
			button.appendChild(label);
			button.addEventListener("click", function() {
				hideSelfMenu();
				if (item.enabled === false) {
					return;
				}
				notifyBridge("invokeAppAction", item.id || "");
			});
			refs.selfMenu.appendChild(button);
		});

		(menuState.actions || []).forEach(function(item) {
			if (!item || item.kind === "separator") {
				const separator = document.createElement("div");
				separator.className = "self-menu-separator";
				refs.selfMenu.appendChild(separator);
				return;
			}
			if (!item.id) {
				return;
			}

			const button = document.createElement("button");
			button.type = "button";
			button.className = "self-menu-row"
				+ (item.checked ? " is-selected" : "")
				+ (item.tone ? " is-" + item.tone : "");
			button.disabled = item.enabled === false;
			if (item.hint) {
				button.title = item.hint;
			}

			const label = document.createElement("span");
			label.className = "self-menu-label";
			label.textContent = item.label || "Action";

			button.appendChild(label);
			button.addEventListener("click", function() {
				hideSelfMenu();
				if (item.enabled === false) {
					return;
				}
				notifyBridge("invokeAppAction", item.id || "");
			});
			refs.selfMenu.appendChild(button);
		});

		selfMenuOpen = true;
		refs.selfMenu.classList.remove("hidden");
		refs.selfMenu.setAttribute("aria-hidden", "false");
		refs.selfCard.setAttribute("aria-expanded", "true");
		positionSelfMenu();
	}

	function toggleSelfMenu(forceOpen) {
		const nextOpen = typeof forceOpen === "boolean" ? forceOpen : !selfMenuOpen;
		if (!nextOpen) {
			hideSelfMenu();
			return;
		}

		hideAppMenu();
		hideContextMenu();
		renderSelfMenu(getSnapshot());
	}

	function syncComposerHeight() {
		refs.composerInput.style.height = "0px";
		refs.composerInput.style.height = Math.min(refs.composerInput.scrollHeight, 160) + "px";
		scheduleFooterAlignmentSync();
	}

	function syncFooterAlignment() {
		footerAlignmentFrame = 0;
		if (!refs.appShell || !refs.composerForm || !refs.selfCard) {
			return;
		}

		const composerHeight = refs.composerForm.getBoundingClientRect().height;
		if (!composerHeight) {
			refs.appShell.style.removeProperty("--footer-row-height");
			return;
		}

		const dpr = window.devicePixelRatio || 1;
		const selfCardStyle = window.getComputedStyle(refs.selfCard);
		const selfCardChromeHeight =
			Number.parseFloat(selfCardStyle.paddingTop || "0")
			+ Number.parseFloat(selfCardStyle.paddingBottom || "0")
			+ Number.parseFloat(selfCardStyle.borderTopWidth || "0")
			+ Number.parseFloat(selfCardStyle.borderBottomWidth || "0");
		const selfContentHeight = Math.max(
			refs.selfAvatar ? refs.selfAvatar.getBoundingClientRect().height : 0,
			refs.selfCopy ? refs.selfCopy.getBoundingClientRect().height : 0,
			refs.selfCardSettingsButton ? refs.selfCardSettingsButton.getBoundingClientRect().height : 0
		);
		const selfMinimumHeight = selfContentHeight + selfCardChromeHeight;
		const snappedHeight = Math.ceil(Math.max(composerHeight, selfMinimumHeight) * dpr) / dpr;
		refs.appShell.style.setProperty("--footer-row-height", snappedHeight + "px");
	}

	function scheduleFooterAlignmentSync() {
		if (footerAlignmentFrame) {
			cancelAnimationFrame(footerAlignmentFrame);
		}
		footerAlignmentFrame = requestAnimationFrame(syncFooterAlignment);
	}

	function ensureFooterAlignmentObservers() {
		if (footerAlignmentResizeObserver || typeof ResizeObserver !== "function") {
			return;
		}

		footerAlignmentResizeObserver = new ResizeObserver(scheduleFooterAlignmentSync);
		[refs.composerForm, refs.composerShell, refs.composerReplyBar, refs.selfAvatar, refs.selfCopy].forEach(function(element) {
			if (element) {
				footerAlignmentResizeObserver.observe(element);
			}
		});
	}

	function renderComposerReplyState(scope) {
		const hasReply = !!(scope && scope.hasPendingReply);
		refs.composerReplyBar.classList.toggle("hidden", !hasReply);
		refs.composerShell.classList.toggle("has-reply", hasReply);
		if (!hasReply) {
			refs.composerReplyLabel.textContent = "Replying";
			refs.composerReplySnippet.textContent = "";
			return;
		}

		refs.composerReplyLabel.textContent = scope.replyActor
			? "Replying to " + String(scope.replyActor)
			: "Replying";
		refs.composerReplySnippet.textContent = scope.replySnippet || "";
	}

	function syncAmbientState(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const toneSource = scope.label || app.serverTitle || "Mumble";
		const scopeHue = hueForLabel(toneSource, false);
		refs.appShell.style.setProperty("--scope-hue", String(scopeHue));
		refs.appShell.dataset.scopeKind = String(scope.kindLabel || "conversation").toLowerCase().replace(/\s+/g, "-");
	}

	function syncMenuBandChrome() {
		if (!refs.modernHeader) {
			return;
		}

		const hasMenus = !!(refs.menuBar && refs.menuBar.children.length);
		refs.modernHeader.classList.toggle("has-app-menus", hasMenus);
		refs.modernHeader.classList.toggle("menu-is-open", openMenuId !== null);
	}

	function renderCurrentMenus() {
		renderMenus(resolvedAppMenus((getSnapshot().app || {})));
	}

	function positionTopMenuPanel(group, panel) {
		if (!group || !panel || !group.classList.contains("is-open")) {
			return;
		}

		panel.style.left = "0px";
		const groupBounds = group.getBoundingClientRect();
		const panelBounds = panel.getBoundingClientRect();
		const leftAlignedPosition = groupBounds.left;
		const rightAlignedPosition = groupBounds.right - panelBounds.width;
		const hasUsefulLeftSpan = groupBounds.right >= panelBounds.width * 0.75;
		const preferredPosition =
			hasUsefulLeftSpan ? rightAlignedPosition : leftAlignedPosition;
		const globalLeft = clampedViewportPosition(preferredPosition, panelBounds.width, window.innerWidth);
		panel.style.left = Math.round(globalLeft - groupBounds.left) + "px";
	}

	function cancelMenuDismissTimer() {
		if (!menuDismissTimer) {
			return;
		}

		window.clearTimeout(menuDismissTimer);
		menuDismissTimer = 0;
	}

	function closeTopMenu() {
		cancelMenuDismissTimer();
		openMenuPinned = false;
		if (openMenuId === null) {
			syncMenuBandChrome();
			return;
		}

		openMenuId = null;
		renderCurrentMenus();
	}

	function scheduleTransientMenuDismiss() {
		if (openMenuPinned || openMenuId === null || menuDismissTimer) {
			return;
		}

		menuDismissTimer = window.setTimeout(function() {
			menuDismissTimer = 0;
			if (!openMenuPinned && openMenuId !== null) {
				closeTopMenu();
			}
		}, 90);
	}

	function menuPeekHotZoneContains(clientX, clientY) {
		if (!refs.modernHeader || !refs.menuBar || !refs.menuBar.children.length) {
			return false;
		}

		const headerRect = refs.modernHeader.getBoundingClientRect();
		const menuRect = refs.menuBar.getBoundingClientRect();
		const menuWidth = Math.max(refs.menuBar.scrollWidth || 0, menuRect.width || 0);
		if (!menuWidth) {
			return false;
		}

		const left = Math.max(headerRect.left, menuRect.left - 12);
		const right = Math.min(headerRect.right, menuRect.left + menuWidth + 18);
		const top = Math.max(headerRect.top, menuRect.top - 10);
		const bottom = menuRect.top + Math.max(30, menuRect.height || 0) + 8;
		return clientX >= left && clientX <= right && clientY >= top && clientY <= bottom;
	}

	function syncMenuPeekState(clientX, clientY) {
		if (!refs.modernHeader || !refs.menuBar
			|| !Number.isFinite(clientX) || !Number.isFinite(clientY)) {
			return;
		}

		const target = document.elementFromPoint(clientX, clientY);
		const targetInMenu = !!(target && typeof target.closest === "function"
			&& target.closest(".menu-band, .menu-group, .menu-panel"));
		const peeking = targetInMenu || menuPeekHotZoneContains(clientX, clientY);
		refs.modernHeader.classList.toggle("menu-is-peeked", peeking);
		if (peeking) {
			cancelMenuDismissTimer();
			return;
		}

		scheduleTransientMenuDismiss();
	}

	function clearMenuPeekState() {
		if (refs.modernHeader) {
			refs.modernHeader.classList.remove("menu-is-peeked");
		}
		if (!openMenuPinned) {
			closeTopMenu();
		}
	}

	function renderMenus(menus) {
		const appMenus = (menus || []).filter(function(menu) {
			return !!menu;
		});

		if (openMenuId !== null && !appMenus.some(function(menu) {
			return menu.id === openMenuId;
		})) {
			openMenuId = null;
			openMenuPinned = false;
		}

		refs.menuBar.innerHTML = "";

		appMenus.forEach(function(menu) {
			const menuItems = menu.items || [];
			const group = document.createElement("div");
			group.className = "menu-group" + (openMenuId === menu.id ? " is-open" : "");
			let panel = null;

			const trigger = document.createElement("button");
			trigger.type = "button";
			trigger.className = "menu-trigger";
			trigger.textContent = menu.label || "Menu";
			trigger.setAttribute("aria-expanded", openMenuId === menu.id ? "true" : "false");
			if (menuItems.length) {
				trigger.setAttribute("aria-haspopup", "menu");
			}
			trigger.addEventListener("mouseenter", function() {
				if (!menuItems.length || openMenuId === menu.id) {
					return;
				}

				hideAppMenu();
				hideContextMenu();
				openMenuPinned = false;
				openMenuId = menu.id;
				renderMenus(appMenus);
			});
			trigger.addEventListener("keydown", function(event) {
				if (event.key !== "ArrowDown" && event.key !== "Enter" && event.key !== " ") {
					return;
				}

				if (!menuItems.length) {
					return;
				}

				event.preventDefault();
				hideAppMenu();
				hideContextMenu();
				openMenuPinned = true;
				openMenuId = menu.id;
				renderMenus(appMenus);
				const activeGroup = refs.menuBar.querySelector(".menu-group.is-open");
				const firstItem = activeGroup ? activeGroup.querySelector(".menu-item:not(:disabled)") : null;
				if (firstItem) {
					firstItem.focus();
				}
			});
			trigger.addEventListener("click", function(event) {
				event.stopPropagation();
				hideAppMenu();
				hideContextMenu();
				if (openMenuId === menu.id && openMenuPinned) {
					openMenuId = null;
					openMenuPinned = false;
				} else {
					openMenuId = menu.id;
					openMenuPinned = true;
				}
				renderMenus(appMenus);
			});
			group.appendChild(trigger);

			if (menuItems.length) {
				panel = document.createElement("div");
				panel.className = "menu-panel";
				panel.setAttribute("role", "menu");

				actionItemsFromActionStates(menuItems, {
					invokeAction: function(actionId) {
						notifyBridge("invokeAppAction", actionId);
					}
				}).forEach(function(item) {
					appendActionPanelItem(panel, item, "menu", true);
				});

				group.appendChild(panel);
			}

			refs.menuBar.appendChild(group);
			if (openMenuId === menu.id && panel) {
				positionTopMenuPanel(group, panel);
			}
		});

		syncMenuBandChrome();
	}

	function render(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const textRooms = snapshot.textRooms || [];
		const voiceRooms = snapshot.voiceRooms || [];
		const voicePresence = snapshot.voicePresence || [];
		const headerPresence = voicePresence.length ? voicePresence : (snapshot.participants || []);

		refs.brandTitle.textContent = app.serverTitle || "Mumble";
		refs.brandSubtitle.textContent = app.serverSubtitle || "Room-first shell";
		refs.serverEyebrow.textContent = app.serverEyebrow || scope.kindLabel || "Mumble";
		refs.scopeTitle.textContent = scope.label || "Modern Layout";
		refs.scopeDescription.textContent = scope.description || "Select a room to see shared history.";

		applyStatePill(refs.layoutPill, app.layoutLabel || "Modern", app.layoutTone || "");
		applyStatePill(refs.connectionPill, app.connectionLabel || "Disconnected", app.connectionTone || "");
		applyStatePill(refs.compatPill, app.compatibilityLabel || "Standard server", app.compatibilityTone || "");
		refs.layoutSwitchButton.disabled = app.canToggleLayout === false;
		refs.layoutSwitchButton.classList.toggle("hidden", app.canToggleLayout === false);
		refs.layoutSwitchButton.title = app.layoutSwitchLabel || "Switch layout";
		refs.layoutSwitchButton.setAttribute("aria-label", app.layoutSwitchLabel || "Switch layout");

		refs.connectButton.disabled = !app.canConnect;
		refs.disconnectButton.disabled = !app.canDisconnect;
		refs.muteButton.classList.toggle("is-active", !!app.selfMuted);
		refs.deafButton.classList.toggle("is-active", !!app.selfDeafened);
		renderMenus(resolvedAppMenus(app));
		refs.settingsButton.setAttribute("aria-expanded", appMenuOpen ? "true" : "false");

		reconcilePendingVoiceJoin(snapshot);
		refs.textRoomCount.textContent = String(textRooms.length);
		refs.voiceRoomCount.textContent = String(voiceRooms.length);
		renderRoomList(refs.voiceRoomList, voiceRooms, {
			joinable: true,
			voicePresence: voicePresence,
			rootLabel: app.voiceRootLabel || "",
			rootToken: app.voiceRootScopeToken || ""
		});
		renderRoomList(refs.textRoomList, textRooms, {
			joinable: false,
			voicePresence: null,
			hideWhenEmpty: !(app.canManageTextChannels || app.canCreateTextRoom)
		});
		renderNote(app, snapshot.activeScope || {});
		const renderedActiveRailToken = activeRailToken();
		if (!renderedActiveRailToken) {
			lastActiveRailToken = "";
		} else if (renderedActiveRailToken !== lastActiveRailToken) {
			lastActiveRailToken = renderedActiveRailToken;
			pendingActiveRailReveal = true;
		}

		renderVoicePresenceStack(headerPresence);
		renderMeta(scope.meta || []);
		renderScreenShareHeader(scope, scope.screenShare || null);
		renderScreenShareCard(scope, scope.screenShare || null);
		renderNote(app, scope);
		renderMessages(snapshot, { resolvePendingScopeLoading: true });
		renderSelfCard(app);
		syncAmbientState(snapshot);
		if (appMenuOpen) {
			renderAppMenu(snapshot);
		}
		if (selfMenuOpen) {
			renderSelfMenu(snapshot);
		}

		refs.scopeBanner.textContent = scope.banner || "";
		refs.scopeBanner.classList.toggle("hidden", !scope.banner);
		refs.loadOlderButton.disabled = !scope.canLoadOlder;
		refs.markReadButton.disabled = !scope.canMarkRead;
		refs.composerInput.disabled = !scope.canSend;
		refs.attachButton.disabled = !scope.canAttachImages;
		refs.sendButton.disabled = !scope.canSend;
		refs.composerInput.placeholder = scope.composerPlaceholder || "Write a message";
		refs.composerHint.textContent = scope.composerHint || "Persistent room history stays with the selected room.";
		renderComposerReplyState(scope);
		applyRailPresentation();
		syncComposerHeight();
		scheduleRailLayoutSync();

		if (scope.autoMarkRead) {
			notifyBridge("markRead");
		}
	}

	function syncSnapshot() {
		liveSnapshot = modernBridge ? (modernBridge.snapshot || {}) : {};
		scheduleSnapshotRender();
	}

	function messageIdentityEquals(left, right) {
		if (!left || !right) {
			return false;
		}
		const leftId = String(left.messageId || "");
		const rightId = String(right.messageId || "");
		if (leftId && rightId && leftId === rightId) {
			const leftThreadId = String(left.threadId || "");
			const rightThreadId = String(right.threadId || "");
			return !leftThreadId || !rightThreadId || leftThreadId === rightThreadId;
		}
		return messageKey(left) === messageKey(right);
	}

	function messageIndexByIdentity(messages, message) {
		for (let index = 0; index < (messages || []).length; index += 1) {
			if (messageIdentityEquals(messages[index], message)) {
				return index;
			}
		}
		return -1;
	}

	function patchScopeMatches(snapshot, patch) {
		const patchScope = String(patch && patch.scopeToken || "");
		const activeScope = String(snapshot && snapshot.activeScope && snapshot.activeScope.scopeToken || "");
		return !patchScope || !activeScope || patchScope === activeScope;
	}

	function replaceActiveScopeFromPatch(snapshot, patch) {
		if (!patch || !patch.activeScope || typeof patch.activeScope !== "object") {
			return false;
		}

		snapshot.activeScope = Object.assign({}, patch.activeScope);
		return true;
	}

	function applyMessagesAppendPatch(snapshot, patch) {
		if (!patchScopeMatches(snapshot, patch)) {
			return true;
		}

		const incoming = Array.isArray(patch.messages) ? patch.messages : [];
		if (!incoming.length) {
			return true;
		}

		const previousMessages = Array.isArray(snapshot.messages) ? snapshot.messages.slice() : [];
		const nextMessages = previousMessages.slice();
		const appended = [];
		incoming.forEach(function(message) {
			const existingIndex = messageIndexByIdentity(nextMessages, message);
			if (existingIndex >= 0) {
				nextMessages[existingIndex] = Object.assign({}, nextMessages[existingIndex], message);
				return;
			}
			nextMessages.push(message);
			appended.push(message);
		});

		if (!appended.length) {
			snapshot.messages = nextMessages;
			return true;
		}

		const willTrimHead = nextMessages.length > 200;
		snapshot.messages = willTrimHead ? nextMessages.slice(nextMessages.length - 200) : nextMessages;
		if (activeMessageChunkRender) {
			renderMessages(snapshot, { forceSync: true, resolvePendingScopeLoading: true });
			return true;
		}
		if (willTrimHead || String((snapshot.activeScope || {}).scopeToken || "") !== lastScopeToken) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
			return true;
		}

		const activeScopeToken = String((snapshot.activeScope || {}).scopeToken || "");
		if (pendingScopeLoadingBlocksRender(activeScopeToken, { resolvePendingScopeLoading: true })) {
			return true;
		}
		refs.messageList.classList.remove("is-chat-loading");
		const metricsBefore = messageListMetrics();
		const detachedBeforeRender = !metricsBefore.nearBottom;
		const distanceFromBottom = metricsBefore.distanceFromBottom;
		const freshTailCount = appended.filter(function(message) {
			return !message.system;
		}).length;
		if (detachedBeforeRender) {
			unreadDetachedMessages += freshTailCount;
		}
		if (!appendTimelineMessages(appended, previousMessages)) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
			return true;
		}

		const shouldStickToBottom = (patch.scrollToBottom !== false)
			&& (!detachedBeforeRender || keepMessageListPinnedToBottom);
		requestAnimationFrame(function() {
			if (shouldStickToBottom) {
				keepMessageListPinnedToBottom = true;
				scheduleMessageListBottomPin(3);
				return;
			}

			refs.messageList.scrollTop = Math.max(0,
				refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
			syncScrollState();
		});

		lastRenderedMessageCount = snapshot.messages.length;
		lastRenderedTailKey = latestTailMessageKey(snapshot.messages);
		lastScopeToken = String((snapshot.activeScope || {}).scopeToken || lastScopeToken);
		return true;
	}

	function applyMessagesUpdatePatch(snapshot, patch) {
		if (!patchScopeMatches(snapshot, patch)) {
			return true;
		}

		const message = patch.message || (Array.isArray(patch.messages) ? patch.messages[0] : null);
		if (!message) {
			return true;
		}

		const messages = Array.isArray(snapshot.messages) ? snapshot.messages.slice() : [];
		const existingIndex = messageIndexByIdentity(messages, message);
		if (existingIndex < 0) {
			return false;
		}

		const updated = Object.assign({}, messages[existingIndex], message);
		messages[existingIndex] = updated;
		snapshot.messages = messages;
		if (activeMessageChunkRender) {
			queuePendingMessageUpdatePatch(Object.assign({ renderIndex: existingIndex }, updated));
			applyPendingMessageUpdatePatches(true);
			lastRenderedTailKey = latestTailMessageKey(snapshot.messages);
			return true;
		}

		if (!replaceRenderedMessage(Object.assign({ renderIndex: existingIndex }, updated))) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
		}
		lastRenderedTailKey = latestTailMessageKey(snapshot.messages);
		return true;
	}

	function applyMessagesResetPatch(snapshot, patch) {
		if (!patchScopeMatches(snapshot, patch)) {
			return true;
		}
		replaceActiveScopeFromPatch(snapshot, patch);
		snapshot.messages = Array.isArray(patch.messages) ? patch.messages : [];
		renderMessages(snapshot, { resolvePendingScopeLoading: true });
		renderActiveScopePatch(snapshot);
		return true;
	}

	function syncSnapshotPatch(patch) {
		const startedAt = monotonicNow();
		const kind = patch && typeof patch === "object" ? String(patch.kind || "") : "";
		try {
			if (!patch || typeof patch !== "object") {
				return;
			}

			const snapshot = getSnapshot();
			if (kind !== "messages.append" && kind !== "messages.update"
					&& kind !== "messages.reset" && kind !== "serverLog.append"
					&& kind !== "serverLog.reset") {
				replaceActiveScopeFromPatch(snapshot, patch);
			}

			if (kind === "messages.append") {
				applyMessagesAppendPatch(snapshot, patch);
				return;
			}
			if (kind === "messages.update") {
				if (!applyMessagesUpdatePatch(snapshot, patch)) {
					scheduleSnapshotRender();
				}
				return;
			}
			if (kind === "messages.reset") {
				applyMessagesResetPatch(snapshot, patch);
				return;
			}
			if (kind === "serverLog.append") {
				applyServerLogAppendPatch(snapshot, patch);
				return;
			}
			if (kind === "serverLog.reset") {
				applyServerLogResetPatch(snapshot, patch);
				return;
			}
			if (kind === "rooms.update") {
				if (patch.app && typeof patch.app === "object") {
					snapshot.app = Object.assign({}, snapshot.app || {}, patch.app);
				}
				if (Array.isArray(patch.textRooms)) {
					snapshot.textRooms = mergeRoomPatchList(snapshot.textRooms, patch.textRooms);
				}
				if (Array.isArray(patch.voiceRooms)) {
					snapshot.voiceRooms = mergeRoomPatchList(snapshot.voiceRooms, patch.voiceRooms);
				}
				if (Array.isArray(patch.participants)) {
					snapshot.participants = mergeParticipantPatchList(snapshot.participants, patch.participants);
				}
				if (Array.isArray(patch.voicePresence)) {
					snapshot.voicePresence = mergeParticipantPatchList(snapshot.voicePresence, patch.voicePresence);
				}
				if (Object.prototype.hasOwnProperty.call(patch, "voicePresenceChannelId")) {
					snapshot.voicePresenceChannelId = patch.voicePresenceChannelId;
				}
				renderRoomsPatch(snapshot);
				renderVoicePresenceStack((snapshot.voicePresence || []).length
					? (snapshot.voicePresence || [])
					: (snapshot.participants || []));
				return;
			}
			if (kind === "presence.update") {
				if (patch.state) {
					syncParticipantTalkState(patch.state);
				} else {
					renderPresencePatch(snapshot);
				}
				return;
			}
			if (kind === "activeScope.update" || kind === "serverLog.update") {
				renderActiveScopePatch(snapshot);
				if (kind === "serverLog.update") {
					renderMessages(snapshot, { resolvePendingScopeLoading: true });
				}
				return;
			}

			scheduleSnapshotRender();
		} finally {
			traceModernUi("patch " + (kind || "unknown"), startedAt);
		}
	}

	function hideContextMenu() {
		contextMenuState = null;
		refs.contextMenu.classList.add("hidden");
		refs.contextMenu.setAttribute("aria-hidden", "true");
		refs.contextMenu.style.maxHeight = "";
		refs.contextMenu.innerHTML = "";
	}

	function copyPlainText(text) {
		const value = String(text || "");
		if (!value) {
			return Promise.resolve(false);
		}

		if (modernBridge && typeof modernBridge.setClipboardText === "function") {
			try {
				modernBridge.setClipboardText(value);
				return Promise.resolve(true);
			} catch (error) {
				console.warn("Unable to write clipboard text:", error);
			}
		}

		if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
			return navigator.clipboard.writeText(value).then(function() {
				return true;
			}).catch(function() {
				return false;
			});
		}

		const scratch = document.createElement("textarea");
		scratch.value = value;
		scratch.setAttribute("readonly", "readonly");
		scratch.style.position = "fixed";
		scratch.style.left = "-9999px";
		document.body.appendChild(scratch);
		scratch.select();
		const copied = document.execCommand("copy");
		document.body.removeChild(scratch);
		return Promise.resolve(copied);
	}

	function replaceComposerSelection(replacement) {
		const input = refs.composerInput;
		const start = input.selectionStart || 0;
		const end = input.selectionEnd || 0;
		const before = input.value.slice(0, start);
		const after = input.value.slice(end);
		input.value = before + replacement + after;
		const caret = start + replacement.length;
		input.selectionStart = caret;
		input.selectionEnd = caret;
		syncComposerHeight();
		input.dispatchEvent(new Event("input", { bubbles: true }));
	}

	function buildRoomContextMenuItems(snapshot, room, roomRow) {
		const scope = (snapshot && snapshot.activeScope) || {};
		const roomToken = (room && room.token) || (roomRow && roomRow.dataset.scopeToken) || "";
		const isVoiceRoom = roomRow && roomRow.dataset.roomType === "voice";
		const items = [
			{
				id: "openRoom",
				label: "Open room",
				enabled: !!roomToken,
				action: function() {
					selectRoomScope(roomToken);
				}
			}
		];

		if (isVoiceRoom && !(room && actionStatesContainId(room.actions, "join"))) {
			items.push({
				id: "joinVoice",
				label: "Join voice",
				enabled: roomRow && roomRow.dataset.canJoin === "true",
				action: function() {
					notifyBridge("joinVoiceChannel", roomToken);
				}
			});
		}

		if (room && room.participantSession) {
			const participantItems = actionItemsFromActionStates(room.participantActions, {
				invokeAction: function(actionId) {
					notifyBridge("invokeParticipantAction", room.participantSession, actionId);
				},
				invokeValueChanged: function(actionId, value, final) {
					notifyBridge("participantActionValueChanged", room.participantSession, actionId, value, final);
				}
			});
			if (participantItems.length) {
				items.push({ separator: true });
				return participantContextGroups(items.concat(participantItems));
			}
		}

		const roomActionItems = room
			? actionItemsFromActionStates(room.actions, {
				invokeAction: function(actionId) {
					notifyBridge("invokeScopeAction", room.token || roomToken, actionId);
				},
				invokeValueChanged: function(actionId, value, final) {
					notifyBridge("scopeActionValueChanged", room.token || roomToken, actionId, value, final);
				}
			})
			: [];
		if (roomActionItems.length) {
			items.push({ separator: true });
			items.push.apply(items, roomActionItems);
		}

		if (room && room.selected && scope.canMarkRead) {
			items.push({ separator: true });
			items.push({
				id: "markRead",
				label: "Mark read",
				enabled: true,
				action: function() {
					notifyBridge("markRead");
				}
			});
		}

		return roomContextGroups(items);
	}

	function buildContextMenuItems(event) {
		const snapshot = getSnapshot();
		const scope = snapshot.activeScope || {};
		const roomRow = event.target.closest(".rail-row");
		const presenceActionButton = event.target.closest(".presence-action-button");
		const presenceRow = event.target.closest(".presence-row")
			|| (presenceActionButton && presenceActionButton.parentElement
				? presenceActionButton.parentElement.querySelector(".presence-row")
				: null);
		const bubble = event.target.closest(".message-bubble");
		const composer = event.target.closest("#composer-input");
		const selfCard = event.target.closest("#self-card");

		if (composer) {
			const input = refs.composerInput;
			const hasSelection = (input.selectionEnd || 0) > (input.selectionStart || 0);
			return [
				{
					label: "Cut",
					enabled: hasSelection,
					action: function() {
						copyPlainText(input.value.slice(input.selectionStart || 0, input.selectionEnd || 0)).then(function(copied) {
							if (copied) {
								replaceComposerSelection("");
							}
						});
					}
				},
				{
					label: "Copy",
					enabled: hasSelection,
					action: function() {
						copyPlainText(input.value.slice(input.selectionStart || 0, input.selectionEnd || 0));
					}
				},
				{
					label: "Paste",
					enabled: !input.disabled,
					action: function() {
						input.focus();
						if (!!scope.canAttachImages) {
							bridgeClipboardHasImage(function(hasImage) {
								if (hasImage) {
									notifyBridge("attachClipboardImage");
									return;
								}

								pastePlainTextIntoComposer();
							});
							return;
						}

						pastePlainTextIntoComposer();
					}
				},
				{
					label: "Attach image",
					enabled: !!scope.canAttachImages,
					action: function() {
						notifyBridge("openImagePicker");
					}
				},
				{
					label: "Clear",
					enabled: !!input.value,
					action: function() {
						input.value = "";
						syncComposerHeight();
						input.dispatchEvent(new Event("input", { bubbles: true }));
					}
				}
			];
		}

		if (presenceRow) {
			const participant = findParticipantState(snapshot,
				presenceRow.dataset.participantKey || presenceRow.dataset.session,
				presenceRow.dataset.scopeToken || presenceRow.dataset.roomToken);
			return participantContextMenuItems(participant, presenceRow.dataset.scopeToken);
		}

		if (roomRow) {
			const room = findRoomState(snapshot, roomRow.dataset.scopeToken);
			return buildRoomContextMenuItems(snapshot, room, roomRow);
		}

		if (bubble) {
			const message = findMessageState(snapshot, bubble.dataset.messageId);
			const items = [
				{
					label: "Copy message",
					enabled: !!bubble.dataset.bodyText,
					action: function() {
						copyPlainText(bubble.dataset.bodyText);
					}
				}
			];
			if (message && message.canReply) {
				items.push({
					label: "Reply",
					enabled: true,
					action: function() {
						notifyBridge("startReply", message.messageId);
					}
				});
			}
			if (message && message.canReact) {
				items.push({
					label: "Add reaction",
					enabled: true,
					action: function() {
						openReactionPickerMessageId = message.messageId;
						pauseReactionPickerScrollClose();
						syncSnapshot();
					}
				});
			}
			if (message && message.canDelete) {
				items.push({
					label: "Delete message",
					enabled: true,
					tone: "danger",
					action: function() {
						requestDeleteMessage(message);
					}
				});
			}
			items.push(
				{
					separator: true
				},
				{
					label: "Load older",
					enabled: !!scope.canLoadOlder,
					action: function() {
						notifyBridge("loadOlderHistory");
					}
				},
				{
					label: "Mark read",
					enabled: !!scope.canMarkRead,
					action: function() {
						notifyBridge("markRead");
					}
				}
			);
			return normalizedActionPanelItems(items, { hideDisabled: true });
		}

		if (selfCard) {
			const selfMenu = (snapshot.app && snapshot.app.selfMenu) || {};
			const handlers = {
				invokeAction: function(actionId) {
					notifyBridge("invokeAppAction", actionId);
				}
			};
			const presenceItems = actionItemsFromActionStates(selfMenu.presence, handlers);
			const actionItems = actionItemsFromActionStates(selfMenu.actions, handlers);
			if (presenceItems.length && actionItems.length) {
				presenceItems.push({ separator: true });
			}
			return normalizedActionPanelItems(presenceItems.concat(actionItems), { hideDisabled: true });
		}

		const defaultItems = [
			{
				label: "Load older",
				enabled: !!scope.canLoadOlder,
				action: function() {
					notifyBridge("loadOlderHistory");
				}
			},
			{
				label: "Mark read",
				enabled: !!scope.canMarkRead,
				action: function() {
					notifyBridge("markRead");
				}
			},
			{
				label: "Attach image",
				enabled: !!scope.canAttachImages,
				action: function() {
					notifyBridge("openImagePicker");
				}
			}
		];
		return withAppMenuContextItems(defaultItems, snapshot);
	}

	function clampedViewportPosition(position, size, viewportSize) {
		const maxPosition = Math.max(contextMenuViewportMargin, viewportSize - size - contextMenuViewportMargin);
		return Math.max(contextMenuViewportMargin, Math.min(position, maxPosition));
	}

	function contextMenuLeftForPlacement(bounds, clientX, options) {
		const anchorBounds = options && options.anchorBounds;
		if (options && options.horizontal === "left-of-anchor" && anchorBounds) {
			const leftOfAnchor = anchorBounds.left - bounds.width - contextMenuAnchorGap;
			const rightOfAnchor = anchorBounds.right + contextMenuAnchorGap;
			const hasLeftSpace = leftOfAnchor >= contextMenuViewportMargin;
			const hasRightSpace = rightOfAnchor + bounds.width <= window.innerWidth - contextMenuViewportMargin;
			const preferredLeft = hasLeftSpace || !hasRightSpace ? leftOfAnchor : rightOfAnchor;
			return clampedViewportPosition(preferredLeft, bounds.width, window.innerWidth);
		}

		return clampedViewportPosition(clientX, bounds.width, window.innerWidth);
	}

	function fitContextMenuToViewport() {
		if (!refs.contextMenu || refs.contextMenu.classList.contains("hidden")) {
			return;
		}

		const bounds = refs.contextMenu.getBoundingClientRect();
		const currentTop = Number.parseFloat(refs.contextMenu.style.top);
		let top = Number.isFinite(currentTop) ? currentTop : bounds.top;
		let availableHeight = window.innerHeight - top - contextMenuViewportMargin;
		if (availableHeight < 128) {
			top = Math.max(contextMenuViewportMargin, window.innerHeight - 128 - contextMenuViewportMargin);
			refs.contextMenu.style.top = top + "px";
			availableHeight = window.innerHeight - top - contextMenuViewportMargin;
		}
		if (contextMenuState) {
			contextMenuState.top = top;
		}
		refs.contextMenu.style.maxHeight = availableHeight + "px";
	}

	function requestContextMenuViewportFit() {
		if (!refs.contextMenu || refs.contextMenu.classList.contains("hidden")) {
			return;
		}

		window.requestAnimationFrame(fitContextMenuToViewport);
	}

	function showContextMenu(items, clientX, clientY, options) {
		hideAppMenu();
		hideSelfMenu();
		const filteredItems = normalizedActionPanelItems(items, { hideDisabled: true });
		if (!filteredItems.length) {
			hideContextMenu();
			return;
		}

		refs.contextMenu.innerHTML = "";
		refs.contextMenu.style.maxHeight = "";
		filteredItems.forEach(function(item) {
			appendActionPanelItem(refs.contextMenu, item, "context", true);
		});

		refs.contextMenu.classList.remove("hidden");
		refs.contextMenu.setAttribute("aria-hidden", "false");
		const bounds = refs.contextMenu.getBoundingClientRect();
		const left = contextMenuLeftForPlacement(bounds, clientX, options || {});
		const top = clampedViewportPosition(clientY, bounds.height, window.innerHeight);
		refs.contextMenu.style.left = left + "px";
		refs.contextMenu.style.top = top + "px";
		contextMenuState = { left: left, top: top };
		fitContextMenuToViewport();
	}

	function wireActions() {
		document.addEventListener("click", handleAnchorActivation, true);
		document.addEventListener("pointermove", function(event) {
			syncMenuPeekState(event.clientX, event.clientY);
		}, true);
		document.addEventListener("mouseout", function(event) {
			if (!event.relatedTarget) {
				clearMenuPeekState();
			}
		});
		refs.connectButton.addEventListener("click", function() { notifyBridge("openModernDialog", "connect", {}); });
		refs.disconnectButton.addEventListener("click", function() { notifyBridge("disconnectServer"); });
		refs.layoutSwitchButton.addEventListener("click", function() { notifyBridge("toggleLayout"); });
		refs.settingsButton.addEventListener("click", function(event) {
			event.stopPropagation();
			toggleAppMenu();
		});
		refs.muteButton.addEventListener("click", function() { notifyBridge("toggleSelfMute"); });
		refs.deafButton.addEventListener("click", function() { notifyBridge("toggleSelfDeaf"); });
		refs.screenShareButton.addEventListener("click", function() {
			const scopeToken = refs.screenShareButton.dataset.scopeToken || "";
			const actionId = refs.screenShareButton.dataset.actionId || "";
			if (!scopeToken || !actionId || refs.screenShareButton.disabled) {
				return;
			}
			notifyBridge("invokeScopeAction", scopeToken, actionId);
		});
		refs.loadOlderButton.addEventListener("click", function() { notifyBridge("loadOlderHistory"); });
		refs.markReadButton.addEventListener("click", function() { notifyBridge("markRead"); });
		refs.attachButton.addEventListener("click", function() { notifyBridge("openImagePicker"); });
		refs.composerReplyCancelButton.addEventListener("click", function() {
			notifyBridge("cancelReply");
			refs.composerInput.focus();
		});
		refs.selfCard.addEventListener("click", function(event) {
			if (event.target.closest("#self-card-settings-button")) {
				return;
			}
			toggleSelfMenu();
		});
		refs.selfCard.addEventListener("keydown", function(event) {
			if (event.key === "Enter" || event.key === " ") {
				event.preventDefault();
				toggleSelfMenu();
			}
		});
		refs.selfCardSettingsButton.addEventListener("click", function(event) {
			event.stopPropagation();
			notifyBridge("openModernDialog", "settings", {});
		});
		refs.modernDialogCloseButton.addEventListener("click", closeModernDialog);
		refs.modernDialogBackdrop.addEventListener("click", closeModernDialog);
		refs.noteToggleButton.addEventListener("click", function() {
			const snapshot = getSnapshot();
			const app = snapshot.app || {};
			const nextExpanded = !noteExpanded;
			app.motdExpanded = nextExpanded;
			snapshot.app = app;
			railNoteAutoCollapsed = false;
			notifyBridge("invokeAppAction", nextExpanded ? "motd.show" : "motd.hide");
			renderNote(app, snapshot.activeScope || {});
		});
		refs.jumpLatestButton.addEventListener("click", function() {
			keepMessageListPinnedToBottom = true;
			scrollMessageListToBottom("smooth");
		});
		refs.imageViewerBackdrop.addEventListener("click", closeImageViewer);
		refs.imageViewerCloseButton.addEventListener("click", closeImageViewer);
		refs.imageViewerHeader.addEventListener("mousedown", beginImageViewerDrag);
		refs.imageViewerImage.addEventListener("dragstart", function(event) {
			event.preventDefault();
		});
		refs.railToggleButton.addEventListener("click", function() {
			setRailCollapsed(!railCollapsed);
		});
		refs.utilityRailBackdrop.addEventListener("click", function() {
			setRailCollapsed(true);
		});
		refs.railCloseButton.addEventListener("click", function() {
			setRailCollapsed(true);
		});
		refs.composerInput.addEventListener("input", syncComposerHeight);
		refs.composerInput.addEventListener("keydown", handleComposerInputKeyDown);
		refs.composerInput.addEventListener("paste", handleComposerImagePaste);
		refs.composerForm.addEventListener("submit", function(event) {
			event.preventDefault();
			sendComposerDraft();
		});
		[refs.composerForm, refs.messageList].forEach(function(target) {
			target.addEventListener("dragenter", handleComposerImageDragEnter);
			target.addEventListener("dragover", handleComposerImageDragOver);
			target.addEventListener("dragleave", clearComposerImageDropTarget);
			target.addEventListener("drop", handleComposerImageDrop);
		});
		document.addEventListener("click", function(event) {
			const target = eventElementTarget(event);
			if (!target || typeof target.closest !== "function") {
				return;
			}
			if (!target.closest(".context-menu")) {
				hideContextMenu();
			}
			if (!target.closest(".app-menu-popover") && !target.closest("#settings-button")) {
				hideAppMenu();
			}
			if (!target.closest(".self-menu-popover") && !target.closest("#self-card")) {
				hideSelfMenu();
			}
			if (!target.closest(".reaction-picker") && !target.closest(".reaction-picker-toggle")) {
				if (openReactionPickerMessageId !== null) {
					openReactionPickerMessageId = null;
					syncSnapshot();
				}
			}
			if (!target.closest(".menu-group") && openMenuId !== null) {
				closeTopMenu();
			}
			if (modernDialogSelectState
					&& !modernDialogSelectState.shell.contains(target)
					&& !modernDialogSelectState.menu.contains(target)) {
				closeModernDialogSelect();
			}
		});
		window.addEventListener("scroll", positionModernDialogSelectMenu, true);
		document.addEventListener("contextmenu", function(event) {
			const contextTarget = eventElementTarget(event);
			if (contextTarget && typeof contextTarget.closest === "function" && contextTarget.closest(".image-viewer-layer")) {
				event.preventDefault();
				return;
			}
			if (contextTarget && contextTarget.closest(".context-menu")) {
				return;
			}
			const items = buildContextMenuItems(event).filter(function(item) {
				return !!item;
			});
			if (!items.length) {
				hideContextMenu();
				return;
			}
			event.preventDefault();
			const actionButton = contextTarget ? contextTarget.closest(".room-action-button, .presence-action-button") : null;
			if (actionButton) {
				const bounds = actionButton.getBoundingClientRect();
				showContextMenu(items, bounds.left, bounds.bottom + contextMenuAnchorGap, {
					anchorBounds: bounds,
					horizontal: "left-of-anchor"
				});
			} else {
				showContextMenu(items, event.clientX, event.clientY);
			}
		});
		window.addEventListener("keydown", function(event) {
			if (modernDialogState && modernDialogState.open) {
				if (event.key === "Escape") {
					event.preventDefault();
					if (modernDialogSelectState) {
						closeModernDialogSelect();
						return;
					}
					closeModernDialog();
					return;
				}
				if (event.key === "Tab") {
					trapModernDialogTab(event);
					return;
				}
			}
			if (event.key === "Escape" && refs.imageViewerLayer && !refs.imageViewerLayer.classList.contains("hidden")) {
				closeImageViewer();
				return;
			}
			if (event.key === "Escape" && compactViewport && !railCollapsed) {
				setRailCollapsed(true);
				return;
			}
			if (event.key === "Escape" && appMenuOpen) {
				hideAppMenu();
				return;
			}
			if (event.key === "Escape" && openMenuId !== null) {
				closeTopMenu();
				return;
			}
			if (event.key === "Escape" && openReactionPickerMessageId !== null) {
				openReactionPickerMessageId = null;
				syncSnapshot();
				return;
			}
			if (event.key === "Escape") {
				hideContextMenu();
				hideSelfMenu();
			}
		});
		window.addEventListener("resize", function() {
			syncCompactRailState(false);
			hideContextMenu();
			if (appMenuOpen) {
				positionAppMenu();
			}
			if (selfMenuOpen) {
				positionSelfMenu();
			}
			if (refs.imageViewerLayer && !refs.imageViewerLayer.classList.contains("hidden")) {
				applyImageViewerGeometry();
				persistImageViewerState();
			}
			positionModernDialogSelectMenu();
			refreshMessageListPinning(2);
			scheduleFooterAlignmentSync();
			scheduleRailLayoutSync();
		});
		if (window.visualViewport) {
			window.visualViewport.addEventListener("resize", scheduleFooterAlignmentSync);
		}
		window.addEventListener("blur", function() {
			clearMenuPeekState();
			hideContextMenu();
			hideAppMenu();
			hideSelfMenu();
			closeModernDialogSelect();
			clearComposerImageDropTarget();
		});
		refs.messageList.addEventListener("load", function(event) {
			if (!event.target || event.target.tagName !== "IMG") {
				return;
			}
			refreshMessageListPinning(2);
		}, true);
		refs.utilityRail.addEventListener("load", function(event) {
			if (!event.target || event.target.tagName !== "IMG") {
				return;
			}
			scheduleRailLayoutSync();
		}, true);
		refs.voiceRoomList.addEventListener("pointerdown", handleRailVoiceJoinCapture, true);
		refs.voiceRoomList.addEventListener("mousedown", handleRailVoiceJoinCapture, true);
		refs.voiceRoomList.addEventListener("click", handleRailVoiceJoinCapture, true);
		refs.utilityScroll.addEventListener("wheel", handleUtilityWheel, { passive: false });
		refs.utilityScroll.addEventListener("scroll", function() {
			hideContextMenu();
			hideSelfMenu();
			syncRailOverflowState();
		});
		refs.messageList.addEventListener("click", handleMessageImageActivation);
		refs.messageList.addEventListener("scroll", function() {
			if (contextMenuState) {
				hideContextMenu();
			}
			if (selfMenuOpen) {
				hideSelfMenu();
			}
			const shouldCloseReactionPicker =
				openReactionPickerMessageId !== null && !shouldKeepReactionPickerOpenOnScroll();
			if (shouldCloseReactionPicker) {
				openReactionPickerMessageId = null;
				reactionPickerScrollClosePausedUntil = 0;
			}
			syncScrollState();
			if (shouldCloseReactionPicker) {
				syncSnapshot();
			}
		});
	}

	async function boot() {
		wireActions();
		ensureMessageListObservers();
		ensureFooterAlignmentObservers();
		syncCompactRailState(true);
		syncComposerHeight();
		await ensureBridge();
		syncSnapshot();
	}

	boot();
})();
