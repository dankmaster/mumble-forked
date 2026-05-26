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
	let previewHydrationObserver = null;
	let previewHydrationTimer = 0;
	let pendingPreviewHydrationIds = new Set();
	let requestedPreviewHydrationIds = new Set();
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
	let modernDialogReturnFocus = null;
	let modernDialogSelectState = null;
	let stonksActiveTab = "overview";
	let stonksDraftPositions = null;
	let stonksDraftNote = "";
	let stonksDraftCurrency = "USD";
	let stonksDraftKey = "";
	let stonksQuoteSearchText = "";
	let stonksQuoteSearchBusy = false;
	let stonksQuoteSearchError = "";
	let stonksQuoteSuggestions = [];
	let stonksQuoteSearchRequestId = "";

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
	const linkDenseMessageRenderChunkGroupCount = 8;
	const linkDenseMessageRenderChunkBudgetMs = 4;
	const messageRenderLoadingThreshold = 36;
	const previewHydrationDebounceMs = 80;
	const previewHydrationBatchSize = 6;
	const contextMenuViewportMargin = 8;
	const contextMenuAnchorGap = 4;
	const inlinePreviewMediaStorageKey = "mumble-modern-inline-preview-media";
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
		stonksButton: document.getElementById("stonks-button"),
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
		stonksLeaderboardHeader: document.getElementById("stonks-leaderboard-header"),
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

	function previewInlineMediaEnabled() {
		const bodySetting = document.body ? String(document.body.dataset.inlinePreviewMedia || "").trim().toLowerCase() : "";
		if (bodySetting === "false" || bodySetting === "0" || bodySetting === "off") {
			return false;
		}
		if (bodySetting === "true" || bodySetting === "1" || bodySetting === "on") {
			return true;
		}
		if (window.mumbleInlinePreviewMedia === false) {
			return false;
		}
		if (window.mumbleInlinePreviewMedia === true) {
			return true;
		}
		try {
			if (window.localStorage) {
				const stored = String(window.localStorage.getItem(inlinePreviewMediaStorageKey) || "").trim().toLowerCase();
				if (stored === "false" || stored === "0" || stored === "off") {
					return false;
				}
				if (stored === "true" || stored === "1" || stored === "on") {
					return true;
				}
			}
		} catch (error) {
			return true;
		}
		return true;
	}

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
		if (!room || room.joined || room.canJoin === false) {
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
							if (modernBridge.financeQuoteResultReady
									&& typeof modernBridge.financeQuoteResultReady.connect === "function") {
								modernBridge.financeQuoteResultReady.connect(handleStonksQuoteLookupResult);
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

	function currentScopeToken() {
		const snapshot = getSnapshot();
		return String((snapshot.activeScope || {}).scopeToken || "");
	}

	function clearPreviewHydrationState() {
		pendingPreviewHydrationIds.clear();
		requestedPreviewHydrationIds.clear();
		if (previewHydrationTimer) {
			clearTimeout(previewHydrationTimer);
			previewHydrationTimer = 0;
		}
	}

	function flushPreviewHydrationQueue() {
		previewHydrationTimer = 0;
		if (activeMessageChunkRender) {
			schedulePreviewHydrationFlush();
			return;
		}

		const scopeToken = currentScopeToken();
		if (!scopeToken || !pendingPreviewHydrationIds.size) {
			return;
		}

		const ids = Array.from(pendingPreviewHydrationIds).slice(0, previewHydrationBatchSize);
		ids.forEach(function(id) {
			pendingPreviewHydrationIds.delete(id);
		});
		if (!notifyBridge("hydrateMessagePreviews", scopeToken, ids, false)) {
			ids.forEach(function(id) {
				requestedPreviewHydrationIds.delete(id);
				pendingPreviewHydrationIds.add(id);
			});
		}
		if (pendingPreviewHydrationIds.size) {
			schedulePreviewHydrationFlush();
		}
	}

	function schedulePreviewHydrationFlush() {
		if (previewHydrationTimer) {
			return;
		}

		previewHydrationTimer = setTimeout(flushPreviewHydrationQueue, previewHydrationDebounceMs);
	}

	function queuePreviewHydration(messageId) {
		const id = Number(messageId || 0);
		if (!Number.isFinite(id) || id <= 0 || requestedPreviewHydrationIds.has(id)) {
			return;
		}

		requestedPreviewHydrationIds.add(id);
		pendingPreviewHydrationIds.add(id);
		schedulePreviewHydrationFlush();
	}

	function requestPreviewHydrationNow(messageId) {
		const id = Number(messageId || 0);
		const scopeToken = currentScopeToken();
		if (!Number.isFinite(id) || id <= 0 || !scopeToken) {
			return;
		}

		requestedPreviewHydrationIds.add(id);
		pendingPreviewHydrationIds.delete(id);
		if (!notifyBridge("hydrateMessagePreviews", scopeToken, [id], true)) {
			requestedPreviewHydrationIds.delete(id);
		}
	}

	function ensurePreviewHydrationObserver() {
		if (previewHydrationObserver || typeof IntersectionObserver !== "function" || !refs.messageList) {
			return previewHydrationObserver;
		}

		previewHydrationObserver = new IntersectionObserver(function(entries) {
			entries.forEach(function(entry) {
				if (!entry.isIntersecting) {
					return;
				}

				const target = entry.target;
				previewHydrationObserver.unobserve(target);
				queuePreviewHydration(target.dataset.messageId);
			});
		}, {
			root: refs.messageList,
			rootMargin: "600px 0px",
			threshold: 0.01
		});
		return previewHydrationObserver;
	}

	function observePreviewHydrationTarget(bubble) {
		if (!bubble || !bubble.querySelector(".preview-stub, .mumble-inline-image-placeholder")) {
			return;
		}

		const observer = ensurePreviewHydrationObserver();
		if (!observer) {
			queuePreviewHydration(bubble.dataset.messageId);
			return;
		}
		observer.observe(bubble);
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
		if (token !== pendingScopeLoading.scopeToken) {
			clearChatLoadingIndicator();
			return false;
		}

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

		if (messageActorKey(previous) !== messageActorKey(current)) {
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

	function messageActorKey(message) {
		if (!message) {
			return "";
		}

		return String(message.actorKey || message.actor || "");
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

	function parseScopeToken(token) {
		const parts = String(token || "").split(":");
		if (parts.length !== 2) {
			return null;
		}

		const scope = Number(parts[0]);
		const id = Number(parts[1]);
		if (!Number.isFinite(scope) || !Number.isFinite(id)) {
			return null;
		}
		return { scope: scope, id: id };
	}

	function normalizedStonksRoomLabel(label) {
		return String(label || "").trim().replace(/^#/, "").toLowerCase();
	}

	function activeScopeIsStonksRoom(snapshot) {
		const app = snapshot.app || {};
		const stonks = app.stonks || {};
		const scope = snapshot.activeScope || {};
		const parsedScope = parseScopeToken(scope.scopeToken);
		const configuredTextChannelId = Number(stonks.textChannelId || 0);
		if (parsedScope && parsedScope.scope === 3 && configuredTextChannelId > 0
				&& parsedScope.id === configuredTextChannelId) {
			return true;
		}
		return parsedScope && parsedScope.scope === 3 && normalizedStonksRoomLabel(scope.label) === "stonks";
	}

	function rankedStonksLeaderboardRows(stonks) {
		return (Array.isArray(stonks.leaderboard) ? stonks.leaderboard : []).filter(function(row) {
			return row && !row.insufficientHistory && Number(row.rank || 0) > 0;
		});
	}

	function renderStonksChatHeader(snapshot) {
		if (!refs.stonksLeaderboardHeader) {
			return;
		}

		const app = snapshot.app || {};
		const stonks = app.stonks || {};
		const visible = activeScopeIsStonksRoom(snapshot) && !!stonks.supported && stonks.enabled !== false;
		refs.stonksLeaderboardHeader.classList.toggle("hidden", !visible);
		refs.stonksLeaderboardHeader.innerHTML = "";
		if (!visible) {
			return;
		}

		const label = document.createElement("span");
		label.className = "stonks-chat-header-label";
		label.textContent = "Leaderboard " + (stonks.selectedPeriod || "30d");
		refs.stonksLeaderboardHeader.appendChild(label);

		const rows = rankedStonksLeaderboardRows(stonks).slice(0, 3);
		if (!rows.length) {
			const empty = document.createElement("span");
			empty.className = "stonks-chat-header-empty";
			empty.textContent = (Array.isArray(stonks.leaderboard) && stonks.leaderboard.length)
				? "Waiting for baselines"
				: "No ranked snapshots";
			refs.stonksLeaderboardHeader.appendChild(empty);
			return;
		}

		rows.forEach(function(row) {
			const item = document.createElement("span");
			item.className = "stonks-chat-header-rank"
				+ (stonksNumber(row.returnPercent) >= 0 ? " is-positive" : " is-negative");
			const name = String(row.userName || "User").trim() || "User";
			item.textContent = String(row.rank || "") + ". " + name + " " + formatStonksPercent(row.returnPercent);
			refs.stonksLeaderboardHeader.appendChild(item);
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
		const canJoinRoom = joinable && room.canJoin !== false && !room.joined && !joining;
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
		button.dataset.canJoin = canJoinRoom ? "true" : "false";
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
			joinButton.disabled = !canJoinRoom;
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
		renderStonksChatHeader(snapshot);
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
		renderStonksChatHeader(snapshot);
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
			beginScopeLoading(scope.scopeToken, { force: true });
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

	function reactionActorNames(reaction) {
		const rawNames = Array.isArray(reaction && reaction.actorNames) ? reaction.actorNames : [];
		const seenNames = new Set();
		const names = [];
		rawNames.forEach(function(rawName) {
			const name = String(rawName || "").trim();
			if (!name || seenNames.has(name)) {
				return;
			}
			seenNames.add(name);
			names.push(name);
		});
		return names;
	}

	function reactionTooltipText(reaction) {
		const names = reactionActorNames(reaction);
		const count = Math.max(0, Number(reaction && reaction.count) || 0);
		if (!names.length && reaction && reaction.selfReacted) {
			const app = (getSnapshot() && getSnapshot().app) || {};
			const selfName = String(app.selfName || "").trim();
			if (selfName) {
				names.push(selfName);
			}
		}

		const unnamedCount = Math.max(0, count - names.length);
		if (names.length && unnamedCount > 0) {
			names.push(unnamedCount === 1 ? "1 other reaction" : String(unnamedCount) + " other reactions");
		}
		if (names.length) {
			return names.join("\n");
		}

		return count === 1 ? "1 reaction" : String(count) + " reactions";
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

	function previewInlineMp4PlaybackSupported() {
		const snapshot = getSnapshot();
		const app = (snapshot && snapshot.app) || {};
		if (Object.prototype.hasOwnProperty.call(app, "inlineMp4PlaybackSupported")) {
			return !!app.inlineMp4PlaybackSupported;
		}
		return true;
	}

	function previewVideoCanPlayInline(mediaMime, mediaUrl) {
		const video = document.createElement("video");
		const mime = String(mediaMime || "").split(";")[0].trim().toLowerCase();
		const shellSupportsMp4 = previewInlineMp4PlaybackSupported();
		if (mime) {
			if (mime === "video/mp4" && !shellSupportsMp4) {
				return false;
			}
			if (video.canPlayType(mime)) {
				return true;
			}
			if (mime === "video/mp4") {
				return !!video.canPlayType('video/mp4; codecs="avc1.42E01E, mp4a.40.2"');
			}
			if (mime === "application/vnd.apple.mpegurl" || mime === "application/x-mpegurl"
				|| mime === "application/mpegurl" || mime === "audio/mpegurl") {
				return !!video.canPlayType("application/vnd.apple.mpegurl")
					|| !!video.canPlayType("application/x-mpegurl");
			}
			return false;
		}

		const path = String(mediaUrl || "").split("?")[0].toLowerCase();
		if (path.endsWith(".mp4") || path.endsWith(".m4v")) {
			if (!shellSupportsMp4) {
				return false;
			}
			return !!video.canPlayType('video/mp4; codecs="avc1.42E01E, mp4a.40.2"')
				|| !!video.canPlayType("video/mp4");
		}
		if (path.endsWith(".webm")) {
			return !!video.canPlayType('video/webm; codecs="vp8, vorbis"')
				|| !!video.canPlayType('video/webm; codecs="vp8, opus"')
				|| !!video.canPlayType('video/webm; codecs="vp9, opus"')
				|| !!video.canPlayType('video/webm; codecs="vp9, vorbis"')
				|| !!video.canPlayType('video/webm; codecs="av1, opus"')
				|| !!video.canPlayType("video/webm");
		}
		if (path.endsWith(".m3u8")) {
			return !!video.canPlayType("application/vnd.apple.mpegurl")
				|| !!video.canPlayType("application/x-mpegurl");
		}
		return false;
	}

	function previewAudioCanPlayInline(mediaMime, mediaUrl) {
		const audio = document.createElement("audio");
		const mime = String(mediaMime || "").split(";")[0].trim().toLowerCase();
		if (mime) {
			if (audio.canPlayType(mime)) {
				return true;
			}
			if (mime === "audio/mp4" || mime === "audio/aac") {
				return !!audio.canPlayType('audio/mp4; codecs="mp4a.40.2"');
			}
			return false;
		}

		const path = String(mediaUrl || "").split("?")[0].toLowerCase();
		if (path.endsWith(".m4a") || path.endsWith(".aac") || path.endsWith(".mp4")) {
			return !!audio.canPlayType('audio/mp4; codecs="mp4a.40.2"')
				|| !!audio.canPlayType("audio/mp4");
		}
		if (path.endsWith(".mp3")) {
			return !!audio.canPlayType("audio/mpeg");
		}
		return false;
	}

	function attachPreviewAudioTrack(media, video, audioUrl, audioMime) {
		if (!media || !video || !audioUrl || !previewAudioCanPlayInline(audioMime, audioUrl)) {
			return null;
		}

		const audio = document.createElement("audio");
		audio.className = "preview-card-audio-track";
		audio.src = audioUrl;
		audio.preload = video.preload || "metadata";
		audio.setAttribute("aria-hidden", "true");
		audio.tabIndex = -1;

		const syncAudioProperties = function() {
			audio.muted = video.muted;
			audio.volume = video.volume;
			audio.playbackRate = video.playbackRate;
		};
		const syncAudioPosition = function(force) {
			if (!Number.isFinite(video.currentTime)) {
				return;
			}
			const drift = Math.abs((audio.currentTime || 0) - video.currentTime);
			if (force || drift > 0.35) {
				try {
					audio.currentTime = video.currentTime;
				} catch (error) {
					// Some fragmented audio tracks reject early seeks until metadata is ready.
				}
			}
		};
		const playAudio = function() {
			syncAudioProperties();
			syncAudioPosition(true);
			const playPromise = audio.play();
			if (playPromise && typeof playPromise.catch === "function") {
				playPromise.catch(function(error) {
					console.warn("Preview audio playback failed", audioMime || audioUrl, error && error.name ? error.name : error);
				});
			}
		};

		video.addEventListener("play", playAudio);
		video.addEventListener("playing", function() {
			if (!video.paused) {
				playAudio();
			}
		});
		video.addEventListener("pause", function() {
			audio.pause();
		});
		video.addEventListener("waiting", function() {
			audio.pause();
		});
		video.addEventListener("seeking", function() {
			audio.pause();
			syncAudioPosition(true);
		});
		video.addEventListener("seeked", function() {
			syncAudioPosition(true);
			if (!video.paused) {
				playAudio();
			}
		});
		video.addEventListener("timeupdate", function() {
			syncAudioPosition(false);
		});
		video.addEventListener("ratechange", syncAudioProperties);
		video.addEventListener("volumechange", syncAudioProperties);
		video.addEventListener("ended", function() {
			audio.pause();
			try {
				audio.currentTime = 0;
			} catch (error) {
				// Ignore reset failures from short-lived media buffers.
			}
		});
		video.addEventListener("emptied", function() {
			audio.pause();
		});
		audio.addEventListener("error", function() {
			console.warn("Preview audio track failed", audioMime || audioUrl);
		}, { once: true });

		syncAudioProperties();
		media.appendChild(audio);
		return audio;
	}

	function formatPreviewMediaTime(value) {
		const totalSeconds = Math.max(0, Math.floor(Number(value) || 0));
		const minutes = Math.floor(totalSeconds / 60);
		const seconds = totalSeconds % 60;
		return String(minutes) + ":" + String(seconds).padStart(2, "0");
	}

	function normalizedPreviewMediaVolume(value) {
		const volume = Number(value);
		return Number.isFinite(volume) ? Math.max(0, Math.min(1, volume)) : 1;
	}

	function previewVideoControlState(video) {
		const duration = Number.isFinite(video.duration) && video.duration > 0 ? video.duration : 0;
		const currentTime = Number.isFinite(video.currentTime) ? video.currentTime : 0;
		return {
			currentTime: currentTime,
			duration: duration,
			mediaLabel: "media",
			muted: !!video.muted || video.volume <= 0,
			paused: !!video.paused,
			playEnabled: true,
			seekEnabled: !!duration,
			volume: normalizedPreviewMediaVolume(video.volume),
			volumeEnabled: true
		};
	}

	function syncPreviewMediaControlState(controls, state) {
		if (!controls) {
			return;
		}
		state = state || {};
		const playButton = controls.querySelector(".preview-card-media-play");
		const muteButton = controls.querySelector(".preview-card-media-mute");
		const seek = controls.querySelector(".preview-card-media-seek");
		const volume = controls.querySelector(".preview-card-media-volume");
		const time = controls.querySelector(".preview-card-media-time");
		const duration = Number.isFinite(state.duration) && state.duration > 0 ? state.duration : 0;
		const currentTime = Number.isFinite(state.currentTime) ? Math.max(0, state.currentTime) : 0;
		const mediaLabel = String(state.mediaLabel || "media");
		const isMuted = !!state.muted || normalizedPreviewMediaVolume(state.volume) <= 0;
		const playEnabled = state.playEnabled !== false;
		const seekEnabled = state.seekEnabled !== false && !!duration;
		const volumeEnabled = state.volumeEnabled !== false;
		if (playButton) {
			playButton.textContent = state.paused === false ? "Pause" : "Play";
			playButton.disabled = !playEnabled;
			playButton.setAttribute("aria-label", (state.paused === false ? "Pause " : "Play ") + mediaLabel);
		}
		if (muteButton) {
			muteButton.textContent = isMuted ? "Muted" : "Sound";
			muteButton.disabled = !volumeEnabled;
			muteButton.setAttribute("aria-label", (isMuted ? "Unmute " : "Mute ") + mediaLabel);
		}
		if (seek && duration && !seek.matches(":active")) {
			seek.value = String(Math.round((currentTime / duration) * 1000));
		}
		if (seek) {
			seek.disabled = !seekEnabled;
		}
		if (volume && !volume.matches(":active")) {
			volume.value = String(Math.round((isMuted ? 0 : normalizedPreviewMediaVolume(state.volume)) * 100));
		}
		if (volume) {
			volume.disabled = !volumeEnabled;
		}
		if (time) {
			time.textContent = duration
				? formatPreviewMediaTime(currentTime) + " / " + formatPreviewMediaTime(duration)
				: formatPreviewMediaTime(currentTime);
		}
		controls.classList.toggle("has-disabled-playback", !playEnabled);
	}

	function syncPreviewMediaControls(video, controls) {
		if (!video || !controls) {
			return;
		}
		syncPreviewMediaControlState(controls, previewVideoControlState(video));
	}

	function appendPreviewMediaControlSurface(card, media, options) {
		if (!card || !media) {
			return null;
		}
		options = options || {};

		const controls = document.createElement("div");
		controls.className = "preview-card-media-controls" + (options.className ? " " + options.className : "");
		controls.addEventListener("click", function(event) {
			event.stopPropagation();
		});
		controls.addEventListener("keydown", function(event) {
			event.stopPropagation();
		});

		if (options.play !== false) {
			const playButton = document.createElement("button");
			playButton.type = "button";
			playButton.className = "preview-card-media-button preview-card-media-play";
			playButton.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				if (typeof options.onPlay === "function") {
					options.onPlay(controls);
				}
			});
			controls.appendChild(playButton);
		}

		if (options.time !== false) {
			const time = document.createElement("span");
			time.className = "preview-card-media-time";
			time.textContent = "0:00";
			controls.appendChild(time);
		}

		if (options.seek !== false) {
			const seek = document.createElement("input");
			seek.type = "range";
			seek.className = "preview-card-media-seek";
			seek.min = "0";
			seek.max = "1000";
			seek.step = "1";
			seek.value = "0";
			seek.setAttribute("aria-label", "Seek media");
			seek.addEventListener("input", function(event) {
				event.stopPropagation();
				if (typeof options.onSeek === "function") {
					options.onSeek((Number(seek.value) || 0) / 1000, controls);
				}
			});
			controls.appendChild(seek);
		}

		if (options.mute !== false) {
			const muteButton = document.createElement("button");
			muteButton.type = "button";
			muteButton.className = "preview-card-media-button preview-card-media-mute";
			muteButton.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				if (typeof options.onMute === "function") {
					options.onMute(controls);
				}
			});
			controls.appendChild(muteButton);
		}

		if (options.volume !== false) {
			const volume = document.createElement("input");
			volume.type = "range";
			volume.className = "preview-card-media-volume";
			volume.min = "0";
			volume.max = "100";
			volume.step = "1";
			volume.value = "100";
			volume.setAttribute("aria-label", "Media volume");
			volume.addEventListener("input", function(event) {
				event.stopPropagation();
				if (typeof options.onVolume === "function") {
					options.onVolume((Number(volume.value) || 0) / 100, controls);
				}
			});
			controls.appendChild(volume);
		}

		if (options.size !== false) {
			appendPreviewCardSizeButton(card, controls);
		}

		media.appendChild(controls);
		return controls;
	}

	function playPreviewVideo(video) {
		if (!video) {
			return;
		}

		const playPromise = video.play();
		if (playPromise && typeof playPromise.catch === "function") {
			playPromise.catch(function(error) {
				console.warn("Preview video playback failed", error && error.name ? error.name : error);
			});
		}
	}

	function togglePreviewVideoPlayback(video, controls) {
		if (!video) {
			return;
		}

		if (video.paused) {
			playPreviewVideo(video);
		} else {
			video.pause();
		}
		syncPreviewMediaControls(video, controls);
	}

	function togglePreviewGifPlayback(media, image) {
		if (!media || !image) {
			return false;
		}

		const frozenFrame = media.querySelector(".preview-card-gif-freeze");
		if (frozenFrame) {
			frozenFrame.remove();
			image.hidden = false;
			media.classList.remove("is-gif-paused");
			return true;
		}

		if (!image.complete || !image.naturalWidth || !image.naturalHeight) {
			return false;
		}

		const canvas = document.createElement("canvas");
		canvas.className = image.className + " preview-card-gif-freeze";
		canvas.width = image.naturalWidth;
		canvas.height = image.naturalHeight;
		const context = canvas.getContext("2d");
		if (!context) {
			return false;
		}
		try {
			context.drawImage(image, 0, 0, canvas.width, canvas.height);
		} catch (error) {
			console.warn("Preview gif pause failed", error && error.name ? error.name : error);
			return false;
		}

		image.hidden = true;
		media.insertBefore(canvas, image);
		media.classList.add("is-gif-paused");
		return true;
	}

	const previewCardSizeOrder = ["default", "large", "compact"];
	const previewCardSizeLabels = {
		"default": "Default",
		"large": "Large",
		"compact": "Compact"
	};
	const previewCardSizeNextLabels = {
		"default": "Large",
		"large": "Compact",
		"compact": "Default"
	};
	const previewCardBubbleWidthVars = {
		"default": "var(--preview-card-width-default)",
		"large": "var(--preview-card-width-large)",
		"compact": "var(--preview-card-width-compact)"
	};
	let previewEmbedFrameSizeSyncFrame = 0;
	let youtubeIframeApiPromise = null;
	let youtubeIframeIdCounter = 0;
	const youtubeIframePlayers = new WeakMap();
	const tiktokIframePlayers = new WeakMap();
	const tiktokIframePlayerFrames = new Set();
	let tiktokIframeMessageListenerAttached = false;

	function previewCardSizeKey(card) {
		const size = String(card && card.dataset && card.dataset.previewSize || "").trim().toLowerCase();
		return previewCardSizeOrder.indexOf(size) >= 0 ? size : "default";
	}

	function previewCardNextSizeKey(card) {
		const current = previewCardSizeKey(card);
		const index = previewCardSizeOrder.indexOf(current);
		return previewCardSizeOrder[(index + 1) % previewCardSizeOrder.length];
	}

	function syncPreviewStackMediaSizeStateForStack(stack) {
		if (!stack) {
			return;
		}

		const cards = Array.prototype.slice.call(stack.querySelectorAll(".preview-card.has-media"));
		if (!cards.length) {
			stack.classList.remove("has-large-media-preview", "has-compact-media-preview");
			stack.style.removeProperty("--preview-card-stack-width");
			return;
		}
		const hasLarge = cards.some(function(currentCard) {
			return previewCardSizeKey(currentCard) === "large";
		});
		const hasCompact = cards.length > 0 && cards.every(function(currentCard) {
			return previewCardSizeKey(currentCard) === "compact";
		});
		const stackSize = hasLarge ? "large" : (hasCompact ? "compact" : "default");
		stack.classList.toggle("has-large-media-preview", hasLarge);
		stack.classList.toggle("has-compact-media-preview", !hasLarge && hasCompact);
		stack.style.setProperty("--preview-card-stack-width",
			previewCardBubbleWidthVars[stackSize] || previewCardBubbleWidthVars.default);
	}

	function syncPreviewStackMediaSizeState(card) {
		const stack = card && card.closest ? card.closest(".message-stack.has-media-preview") : null;
		syncPreviewStackMediaSizeStateForStack(stack);
	}

	function syncPreviewCardSizeButtons(card) {
		if (!card) {
			return;
		}
		card.querySelectorAll(".preview-card-media-size").forEach(function(button) {
			syncPreviewCardSizeButton(card, button);
		});
	}

	function syncPreviewCardSizeButton(card, button) {
		if (!card || !button) {
			return;
		}
		const current = previewCardSizeKey(card);
		const next = previewCardNextSizeKey(card);
		const currentLabel = previewCardSizeLabels[current] || previewCardSizeLabels.default;
		const nextLabel = previewCardSizeLabels[next] || previewCardSizeLabels.default;
		const buttonLabel = previewCardSizeNextLabels[current] || nextLabel;
		const label = button.querySelector(".preview-card-media-size-label");
		if (label) {
			label.textContent = buttonLabel;
		} else {
			button.textContent = buttonLabel;
		}
		button.dataset.sizeLabel = buttonLabel;
		button.title = "Preview size: " + currentLabel + ". Switch to " + nextLabel + ".";
		button.setAttribute("aria-label", button.title);
	}

	function setPreviewCardSize(card, size) {
		if (!card) {
			return;
		}
		const nextSize = previewCardSizeOrder.indexOf(size) >= 0 ? size : "default";
		const widthVar = previewCardBubbleWidthVars[nextSize] || previewCardBubbleWidthVars.default;
		card.dataset.previewSize = nextSize;
		card.classList.toggle("is-expanded", nextSize === "large");
		card.classList.toggle("is-compact", nextSize === "compact");
		card.style.setProperty("--preview-card-target-width", widthVar);
		const bubble = card.closest(".message-bubble.has-media-preview");
		if (bubble) {
			bubble.style.setProperty("--preview-card-bubble-width", widthVar);
		}
		syncPreviewStackMediaSizeState(card);
		syncPreviewCardSizeButtons(card);
		schedulePreviewEmbedFrameSizeSync();
		requestAnimationFrame(syncScrollState);
	}

	function advancePreviewCardSize(card) {
		setPreviewCardSize(card, previewCardNextSizeKey(card));
	}

	function appendPreviewCardSizeButton(card, controls) {
		if (!card || !controls) {
			return null;
		}
		const sizeButton = document.createElement("button");
		sizeButton.type = "button";
		sizeButton.className = "preview-card-media-button preview-card-media-size";
		sizeButton.dataset.sizeLabel = previewCardSizeNextLabels.default;
		const sizeLabel = document.createElement("span");
		sizeLabel.className = "preview-card-media-size-label";
		sizeLabel.textContent = previewCardSizeNextLabels.default;
		sizeButton.appendChild(sizeLabel);
		sizeButton.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			advancePreviewCardSize(card);
		});
		controls.appendChild(sizeButton);
		syncPreviewCardSizeButton(card, sizeButton);
		requestAnimationFrame(function() {
			syncPreviewCardSizeButton(card, sizeButton);
		});
		return sizeButton;
	}

	function previewEmbedIframeSize(iframe) {
		const frameWrap = iframe && iframe.closest ? iframe.closest(".preview-card-embed-frame-wrap") : null;
		if (!frameWrap) {
			return null;
		}
		const rect = frameWrap.getBoundingClientRect();
		const width = Math.round(rect.width);
		const height = Math.round(rect.height);
		if (width <= 0 || height <= 0) {
			return null;
		}
		return { width: width, height: height };
	}

	function previewIsYouTubeIframe(iframe) {
		if (!iframe) {
			return false;
		}
		try {
			const url = new URL(iframe.src || "", window.location.href);
			const host = url.hostname.toLowerCase();
			return host === "youtube.com" || host.endsWith(".youtube.com")
				|| host === "youtube-nocookie.com" || host.endsWith(".youtube-nocookie.com");
		} catch (error) {
			return false;
		}
	}

	function previewIsTikTokIframe(iframe) {
		if (!iframe) {
			return false;
		}
		try {
			const url = new URL(iframe.src || "", window.location.href);
			const host = url.hostname.toLowerCase();
			return host === "tiktok.com" || host.endsWith(".tiktok.com");
		} catch (error) {
			return false;
		}
	}

	function youtubeIframeTargetOrigin(iframe) {
		try {
			const url = new URL(iframe.src || "", window.location.href);
			const host = url.hostname.toLowerCase();
			if (host === "youtube-nocookie.com" || host.endsWith(".youtube-nocookie.com")) {
				return "https://www.youtube-nocookie.com";
			}
		} catch (error) {
			// Fall back to the normal player origin below.
		}
		return "https://www.youtube.com";
	}

	function tiktokIframeTargetOrigin(iframe) {
		try {
			const url = new URL(iframe.src || "", window.location.href);
			if (url.protocol === "https:" && previewIsTikTokIframe(iframe)) {
				return url.origin;
			}
		} catch (error) {
			// Fall back to the normal player origin below.
		}
		return "https://www.tiktok.com";
	}

	function normalizeYouTubeEmbedUrlForApi(embedUrl) {
		try {
			const url = new URL(embedUrl || "", window.location.href);
			const host = url.hostname.toLowerCase();
			if (host !== "youtube.com" && !host.endsWith(".youtube.com")
				&& host !== "youtube-nocookie.com" && !host.endsWith(".youtube-nocookie.com")) {
				return embedUrl;
			}
			url.searchParams.set("controls", "0");
			url.searchParams.set("disablekb", "1");
			url.searchParams.set("enablejsapi", "1");
			url.searchParams.set("fs", "0");
			url.searchParams.set("iv_load_policy", "3");
			url.searchParams.set("playsinline", "1");
			url.searchParams.delete("origin");
			if (window.location && /^https?:$/i.test(window.location.protocol || "")) {
				url.searchParams.set("origin", window.location.origin);
			}
			return url.toString();
		} catch (error) {
			return embedUrl;
		}
	}

	function normalizeTikTokEmbedUrlForApi(embedUrl) {
		try {
			const url = new URL(embedUrl || "", window.location.href);
			const host = url.hostname.toLowerCase();
			if (host !== "tiktok.com" && !host.endsWith(".tiktok.com")) {
				return embedUrl;
			}
			url.searchParams.set("controls", "0");
			url.searchParams.set("progress_bar", "0");
			url.searchParams.set("play_button", "0");
			url.searchParams.set("volume_control", "0");
			url.searchParams.set("fullscreen_button", "0");
			url.searchParams.set("timestamp", "0");
			url.searchParams.set("autoplay", "0");
			url.searchParams.set("music_info", "0");
			url.searchParams.set("description", "0");
			url.searchParams.set("closed_caption", "0");
			url.searchParams.set("native_context_menu", "0");
			return url.toString();
		} catch (error) {
			return embedUrl;
		}
	}

	function ensureYouTubeIframeApi() {
		if (window.YT && typeof window.YT.Player === "function") {
			return Promise.resolve(window.YT);
		}
		if (youtubeIframeApiPromise) {
			return youtubeIframeApiPromise;
		}

		youtubeIframeApiPromise = new Promise(function(resolve, reject) {
			const previousReady = window.onYouTubeIframeAPIReady;
			let resolved = false;
			const finish = function() {
				if (resolved) {
					return;
				}
				if (window.YT && typeof window.YT.Player === "function") {
					resolved = true;
					resolve(window.YT);
				}
			};

			window.onYouTubeIframeAPIReady = function() {
				if (typeof previousReady === "function") {
					try {
						previousReady();
					} catch (error) {
						console.warn("Previous YouTube iframe API callback failed", error && error.name ? error.name : error);
					}
				}
				finish();
			};

			const existingScript = document.querySelector("script[data-preview-youtube-api]");
			if (!existingScript) {
				const script = document.createElement("script");
				script.src = "https://www.youtube.com/iframe_api";
				script.async = true;
				script.dataset.previewYoutubeApi = "true";
				script.addEventListener("error", function() {
					if (!resolved) {
						reject(new Error("YouTube iframe API failed to load"));
					}
				}, { once: true });
				document.head.appendChild(script);
			}

			const startedAt = Date.now();
			const poll = function() {
				finish();
				if (resolved) {
					return;
				}
				if (Date.now() - startedAt > 8000) {
					reject(new Error("YouTube iframe API timed out"));
					return;
				}
				window.setTimeout(poll, 100);
			};
			poll();
		}).catch(function(error) {
			youtubeIframeApiPromise = null;
			throw error;
		});

		return youtubeIframeApiPromise;
	}

	function ensureYouTubeIframeId(iframe) {
		if (!iframe.id) {
			youtubeIframeIdCounter += 1;
			iframe.id = "preview-youtube-player-" + String(Date.now()) + "-" + String(youtubeIframeIdCounter);
		}
		return iframe.id;
	}

	function ensureYouTubeIframePlayer(iframe) {
		if (!previewIsYouTubeIframe(iframe)) {
			return Promise.reject(new Error("Not a YouTube iframe"));
		}
		const existing = youtubeIframePlayers.get(iframe);
		if (existing && existing.promise) {
			return existing.promise;
		}

		const state = {
			lastPlayerState: null,
			player: null,
			promise: null,
			ready: false,
			stateChangeCallbacks: []
		};
		youtubeIframePlayers.set(iframe, state);
		state.promise = ensureYouTubeIframeApi().then(function(YT) {
			return new Promise(function(resolve, reject) {
				const playerId = ensureYouTubeIframeId(iframe);
				try {
					state.player = new YT.Player(playerId, {
						events: {
							onReady: function(event) {
								state.player = event && event.target ? event.target : state.player;
								state.ready = true;
								resolve(state);
							},
							onError: function(event) {
								console.warn("YouTube embed player error", event && event.data);
							},
							onStateChange: function(event) {
								state.player = event && event.target ? event.target : state.player;
								state.lastPlayerState = event ? event.data : null;
								state.stateChangeCallbacks.forEach(function(callback) {
									try {
										callback(event);
									} catch (error) {
										console.warn("YouTube embed state callback failed",
											error && error.name ? error.name : error);
									}
								});
							}
						}
					});
				} catch (error) {
					reject(error);
				}
			});
		}).catch(function(error) {
			youtubeIframePlayers.delete(iframe);
			throw error;
		});
		return state.promise;
	}

	function invokeYouTubeIframePlayer(iframe, methodName, args) {
		if (!previewIsYouTubeIframe(iframe) || !methodName) {
			return false;
		}
		const apply = function(state) {
			const player = state && state.player;
			if (player && typeof player[methodName] === "function") {
				try {
					player[methodName].apply(player, Array.isArray(args) ? args : []);
					return true;
				} catch (error) {
					console.warn("YouTube embed API command failed", methodName,
						error && error.name ? error.name : error);
				}
			}
			return postYouTubeIframeCommand(iframe, methodName, args);
		};

		const state = youtubeIframePlayers.get(iframe);
		if (state && state.ready) {
			return apply(state);
		}

		ensureYouTubeIframePlayer(iframe).then(apply).catch(function(error) {
			console.warn("YouTube embed API unavailable", error && error.message ? error.message : error);
			postYouTubeIframeCommand(iframe, methodName, args);
		});
		return true;
	}

	function postYouTubeIframeCommand(iframe, func, args) {
		if (!previewIsYouTubeIframe(iframe) || !iframe.contentWindow || !func) {
			return false;
		}
		try {
			iframe.contentWindow.postMessage(JSON.stringify({
				event: "command",
				func: func,
				args: Array.isArray(args) ? args : []
			}), youtubeIframeTargetOrigin(iframe));
			return true;
		} catch (error) {
			console.warn("YouTube embed command failed", func, error && error.name ? error.name : error);
			return false;
		}
	}

	function normalizeTikTokIframeMessageData(data) {
		if (typeof data === "string") {
			try {
				data = JSON.parse(data);
			} catch (error) {
				return null;
			}
		}
		if (!data || typeof data !== "object" || data["x-tiktok-player"] !== true) {
			return null;
		}
		return data;
	}

	function findTikTokIframeStateFromSource(source) {
		if (!source) {
			return null;
		}
		let match = null;
		tiktokIframePlayerFrames.forEach(function(iframe) {
			if (!match && iframe && iframe.contentWindow === source) {
				match = tiktokIframePlayers.get(iframe) || null;
			}
		});
		return match;
	}

	function notifyTikTokIframeStateCallbacks(state, data) {
		if (!state || !Array.isArray(state.stateChangeCallbacks)) {
			return;
		}
		state.stateChangeCallbacks.forEach(function(callback) {
			try {
				callback(data, state);
			} catch (error) {
				console.warn("TikTok embed state callback failed", error && error.name ? error.name : error);
			}
		});
	}

	function updateTikTokIframeStateFromMessage(state, data) {
		if (!state || !data) {
			return;
		}
		const type = String(data.type || "");
		const value = data.value;
		if (type === "onPlayerReady") {
			state.ready = true;
		} else if (type === "onStateChange") {
			const nextState = Number(value);
			if (Number.isFinite(nextState)) {
				state.playerState = nextState;
			}
		} else if (type === "onCurrentTime") {
			if (value && typeof value === "object") {
				const currentTime = Number(value.currentTime);
				const duration = Number(value.duration);
				if (Number.isFinite(currentTime)) {
					state.currentTime = Math.max(0, currentTime);
				}
				if (Number.isFinite(duration)) {
					state.duration = Math.max(0, duration);
				}
			} else {
				const currentTime = Number(value);
				if (Number.isFinite(currentTime)) {
					state.currentTime = Math.max(0, currentTime);
				}
			}
		} else if (type === "onMute") {
			state.muted = !!value;
		} else if (type === "onVolumeChange") {
			const volume = Number(value);
			if (Number.isFinite(volume)) {
				state.volume = volume > 1 ? Math.max(0, Math.min(100, volume)) / 100 : normalizedPreviewMediaVolume(volume);
			}
		} else if (type === "onPlayerError" || type === "onError") {
			console.warn("TikTok embed player error", value);
		}
		notifyTikTokIframeStateCallbacks(state, data);
	}

	function handleTikTokIframeMessage(event) {
		const data = normalizeTikTokIframeMessageData(event && event.data);
		if (!data) {
			return;
		}
		const state = findTikTokIframeStateFromSource(event.source);
		if (!state) {
			return;
		}
		try {
			const origin = new URL(event.origin || "");
			const host = origin.hostname.toLowerCase();
			if (host !== "tiktok.com" && !host.endsWith(".tiktok.com")) {
				return;
			}
		} catch (error) {
			return;
		}
		updateTikTokIframeStateFromMessage(state, data);
	}

	function ensureTikTokIframeMessageListener() {
		if (tiktokIframeMessageListenerAttached) {
			return;
		}
		tiktokIframeMessageListenerAttached = true;
		window.addEventListener("message", handleTikTokIframeMessage);
	}

	function ensureTikTokIframePlayer(iframe) {
		if (!previewIsTikTokIframe(iframe)) {
			return null;
		}
		ensureTikTokIframeMessageListener();
		const existing = tiktokIframePlayers.get(iframe);
		if (existing) {
			return existing;
		}
		const state = {
			currentTime: 0,
			duration: 0,
			iframe: iframe,
			muted: false,
			playerState: -1,
			ready: false,
			stateChangeCallbacks: [],
			volume: 1
		};
		tiktokIframePlayers.set(iframe, state);
		tiktokIframePlayerFrames.add(iframe);
		return state;
	}

	function postTikTokIframeCommand(iframe, type, value) {
		if (!previewIsTikTokIframe(iframe) || !iframe.contentWindow || !type) {
			return false;
		}
		try {
			iframe.contentWindow.postMessage({
				type: type,
				value: value,
				"x-tiktok-player": true
			}, tiktokIframeTargetOrigin(iframe));
			return true;
		} catch (error) {
			console.warn("TikTok embed command failed", type, error && error.name ? error.name : error);
			return false;
		}
	}

	function syncPreviewEmbedFrameSize(iframe) {
		const size = previewEmbedIframeSize(iframe);
		if (!size) {
			return;
		}
		const sizeKey = String(size.width) + "x" + String(size.height);
		if (iframe.dataset.previewEmbedSize === sizeKey) {
			return;
		}
		iframe.dataset.previewEmbedSize = sizeKey;
		iframe.width = String(size.width);
		iframe.height = String(size.height);
		iframe.setAttribute("width", String(size.width));
		iframe.setAttribute("height", String(size.height));
		if (previewIsYouTubeIframe(iframe)) {
			invokeYouTubeIframePlayer(iframe, "setSize", [size.width, size.height]);
		}
	}

	function syncPreviewEmbedFrameSizes() {
		previewEmbedFrameSizeSyncFrame = 0;
		document.querySelectorAll(".preview-card-embed-frame").forEach(syncPreviewEmbedFrameSize);
	}

	function schedulePreviewEmbedFrameSizeSync() {
		if (previewEmbedFrameSizeSyncFrame) {
			return;
		}
		previewEmbedFrameSizeSyncFrame = requestAnimationFrame(syncPreviewEmbedFrameSizes);
	}

	function appendPreviewMediaControls(card, media, video) {
		if (!card || !media || !video) {
			return;
		}

		const controls = appendPreviewMediaControlSurface(card, media, {
			onPlay: function() {
				togglePreviewVideoPlayback(video, controls);
			},
			onSeek: function(fraction) {
				const duration = Number.isFinite(video.duration) && video.duration > 0 ? video.duration : 0;
				if (!duration) {
					return;
				}
				video.currentTime = Math.max(0, Math.min(1, Number(fraction) || 0)) * duration;
				syncPreviewMediaControls(video, controls);
			},
			onMute: function() {
				video.muted = !(video.muted || video.volume <= 0);
				if (!video.muted && video.volume <= 0) {
					video.volume = 0.75;
				}
				syncPreviewMediaControls(video, controls);
			},
			onVolume: function(volumeFraction) {
				const nextVolume = normalizedPreviewMediaVolume(volumeFraction);
				video.volume = nextVolume;
				video.muted = nextVolume <= 0;
				syncPreviewMediaControls(video, controls);
			}
		});

		["play", "pause", "loadedmetadata", "durationchange", "timeupdate", "volumechange", "ended"].forEach(function(name) {
			video.addEventListener(name, function() {
				syncPreviewMediaControls(video, controls);
			});
		});
		syncPreviewMediaControls(video, controls);
	}

	function createPreviewCartIcon(className) {
		const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
		svg.classList.add(className);
		svg.setAttribute("viewBox", "0 0 24 24");
		svg.setAttribute("aria-hidden", "true");
		svg.setAttribute("focusable", "false");
		const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
		path.setAttribute("d", "M6.2 6.5h14l-1.7 7.2H8L6.2 6.5ZM4 4h2l2.5 12h9.8M9.5 20a1.2 1.2 0 1 0 0-2.4 1.2 1.2 0 0 0 0 2.4Zm8 0a1.2 1.2 0 1 0 0-2.4 1.2 1.2 0 0 0 0 2.4Z");
		svg.appendChild(path);
		return svg;
	}

	function appendPreviewPlaybackFallback(media, preview, fallbackText) {
		media.classList.add("preview-card-playback-fallback");
		if (preview.thumbnailUrl) {
			const image = document.createElement("img");
			image.className = "preview-card-image preview-card-media-image";
			image.loading = "lazy";
			image.src = preview.thumbnailUrl;
			image.alt = preview.title || preview.subtitle || "Media preview";
			media.appendChild(image);
		} else {
			const placeholder = document.createElement("span");
			placeholder.className = "preview-card-placeholder-mark";
			placeholder.textContent = "VID";
			media.appendChild(placeholder);
		}
		const overlay = document.createElement("div");
		overlay.className = "preview-card-playback-note";
		overlay.textContent = fallbackText || "Open in browser";
		media.appendChild(overlay);
	}

	function previewStaticPlaybackText(preview, fallbackText) {
		const openLabel = String((preview && preview.openLabel) || "").trim();
		return openLabel || fallbackText || "Open in browser";
	}

	function youtubePlayerStateIsPlaying(playerState) {
		return playerState === 1 || playerState === 3;
	}

	function tiktokPlayerStateIsPlaying(playerState) {
		return playerState === 1 || playerState === 3;
	}

	function youtubeMediaControlStateFromPlayer(player, fallbackState) {
		const fallback = fallbackState || {};
		const getNumber = function(methodName, fallbackValue) {
			if (player && typeof player[methodName] === "function") {
				const value = Number(player[methodName]());
				if (Number.isFinite(value)) {
					return value;
				}
			}
			return fallbackValue;
		};
		const duration = Math.max(0, getNumber("getDuration", Number(fallback.duration) || 0));
		const currentTime = Math.max(0, getNumber("getCurrentTime", Number(fallback.currentTime) || 0));
		const playerVolume = Math.max(0, Math.min(100, getNumber("getVolume", normalizedPreviewMediaVolume(fallback.volume) * 100)));
		const playerState = player && typeof player.getPlayerState === "function" ? player.getPlayerState() : null;
		const muted = player && typeof player.isMuted === "function" ? !!player.isMuted() : !!fallback.muted;
		return {
			currentTime: currentTime,
			duration: duration,
			mediaLabel: "YouTube",
			muted: muted || playerVolume <= 0,
			paused: !youtubePlayerStateIsPlaying(playerState),
			playEnabled: true,
			seekEnabled: duration > 0,
			volume: playerVolume / 100,
			volumeEnabled: true
		};
	}

	function tiktokMediaControlStateFromState(state, fallbackState) {
		const fallback = fallbackState || {};
		const duration = Math.max(0, Number(state && state.duration) || Number(fallback.duration) || 0);
		const currentTime = Math.max(0, Number(state && state.currentTime) || Number(fallback.currentTime) || 0);
		const rawVolume = state && Number.isFinite(state.volume) ? state.volume : fallback.volume;
		const volume = normalizedPreviewMediaVolume(rawVolume);
		const muted = state ? !!state.muted : !!fallback.muted;
		const playerState = state ? state.playerState : fallback.playerState;
		return {
			currentTime: currentTime,
			duration: duration,
			mediaLabel: "TikTok",
			muted: muted || volume <= 0,
			paused: !tiktokPlayerStateIsPlaying(playerState),
			playEnabled: true,
			seekEnabled: duration > 0,
			volume: volume,
			volumeEnabled: true
		};
	}

	function appendYouTubeEmbedMediaControls(card, media, iframe) {
		let controlState = {
			currentTime: 0,
			duration: 0,
			mediaLabel: "YouTube",
			muted: false,
			paused: true,
			playEnabled: true,
			seekEnabled: false,
			volume: 1,
			volumeEnabled: true
		};
		let progressTimer = 0;

		const syncControlState = function() {
			syncPreviewMediaControlState(controls, controlState);
		};
		const scheduleProgressPoll = function() {
			if (progressTimer || controlState.paused !== false) {
				return;
			}
			progressTimer = window.setTimeout(function() {
				progressTimer = 0;
				withYouTubePlayer(function(player) {
					updateFromPlayer(player);
				});
			}, 500);
		};
		const updateFromPlayer = function(player) {
			controlState = youtubeMediaControlStateFromPlayer(player, controlState);
			syncControlState();
			scheduleProgressPoll();
		};
		const applyOptimisticState = function(nextState) {
			controlState = Object.assign({}, controlState, nextState || {});
			syncControlState();
			scheduleProgressPoll();
		};
		const registerPlayerStateCallback = function(state) {
			if (!state || state.previewMediaControlsAttached || !Array.isArray(state.stateChangeCallbacks)) {
				return;
			}
			state.previewMediaControlsAttached = true;
			state.stateChangeCallbacks.push(function(event) {
				updateFromPlayer(event && event.target ? event.target : state.player);
			});
		};
		const withYouTubePlayer = function(callback, fallback) {
			ensureYouTubeIframePlayer(iframe).then(function(state) {
				registerPlayerStateCallback(state);
				if (state && state.player) {
					callback(state.player, state);
				}
			}).catch(function(error) {
				console.warn("YouTube embed API unavailable", error && error.message ? error.message : error);
				if (typeof fallback === "function") {
					fallback();
				}
			});
		};

		const controls = appendPreviewMediaControlSurface(card, media, {
			className: "preview-card-youtube-controls",
			onPlay: function() {
				withYouTubePlayer(function(player) {
					const playerState = typeof player.getPlayerState === "function" ? player.getPlayerState() : null;
					if (youtubePlayerStateIsPlaying(playerState)) {
						if (typeof player.pauseVideo === "function") {
							player.pauseVideo();
						}
						applyOptimisticState({ paused: true });
					} else {
						if (typeof player.playVideo === "function") {
							player.playVideo();
						}
						applyOptimisticState({ paused: false });
					}
					window.setTimeout(function() {
						updateFromPlayer(player);
					}, 150);
				}, function() {
					const shouldPlay = controlState.paused !== false;
					postYouTubeIframeCommand(iframe, shouldPlay ? "playVideo" : "pauseVideo", []);
					applyOptimisticState({ paused: !shouldPlay });
				});
			},
			onSeek: function(fraction) {
				const nextTime = Math.max(0, Math.min(1, Number(fraction) || 0)) * (controlState.duration || 0);
				if (!controlState.duration) {
					return;
				}
				applyOptimisticState({ currentTime: nextTime });
				withYouTubePlayer(function(player) {
					if (typeof player.seekTo === "function") {
						player.seekTo(nextTime, true);
					}
					window.setTimeout(function() {
						updateFromPlayer(player);
					}, 150);
				}, function() {
					postYouTubeIframeCommand(iframe, "seekTo", [nextTime, true]);
				});
			},
			onMute: function() {
				const nextMuted = !controlState.muted;
				const nextVolume = !nextMuted && controlState.volume <= 0 ? 0.75 : controlState.volume;
				applyOptimisticState({ muted: nextMuted, volume: nextVolume });
				withYouTubePlayer(function(player) {
					if (nextMuted) {
						if (typeof player.mute === "function") {
							player.mute();
						}
					} else {
						if (typeof player.setVolume === "function") {
							player.setVolume(Math.round(nextVolume * 100));
						}
						if (typeof player.unMute === "function") {
							player.unMute();
						}
					}
					updateFromPlayer(player);
				}, function() {
					postYouTubeIframeCommand(iframe, nextMuted ? "mute" : "unMute", []);
					if (!nextMuted) {
						postYouTubeIframeCommand(iframe, "setVolume", [Math.round(nextVolume * 100)]);
					}
				});
			},
			onVolume: function(volumeFraction) {
				const nextVolume = normalizedPreviewMediaVolume(volumeFraction);
				const nextMuted = nextVolume <= 0;
				applyOptimisticState({ muted: nextMuted, volume: nextVolume });
				withYouTubePlayer(function(player) {
					if (typeof player.setVolume === "function") {
						player.setVolume(Math.round(nextVolume * 100));
					}
					if (nextMuted && typeof player.mute === "function") {
						player.mute();
					} else if (!nextMuted && typeof player.unMute === "function") {
						player.unMute();
					}
					updateFromPlayer(player);
				}, function() {
					postYouTubeIframeCommand(iframe, "setVolume", [Math.round(nextVolume * 100)]);
					postYouTubeIframeCommand(iframe, nextMuted ? "mute" : "unMute", []);
				});
			}
		});

		syncControlState();
		ensureYouTubeIframePlayer(iframe).then(function(state) {
			registerPlayerStateCallback(state);
			if (state && state.player) {
				updateFromPlayer(state.player);
			}
		}).catch(function(error) {
			console.warn("YouTube embed controls could not attach",
				error && error.message ? error.message : error);
		});
		iframe.addEventListener("load", function() {
			schedulePreviewEmbedFrameSizeSync();
			withYouTubePlayer(function(player) {
				updateFromPlayer(player);
			});
		});
		return controls;
	}

	function appendTikTokEmbedMediaControls(card, media, iframe) {
		const playerState = ensureTikTokIframePlayer(iframe);
		let controlState = tiktokMediaControlStateFromState(playerState, {
			currentTime: 0,
			duration: 0,
			mediaLabel: "TikTok",
			muted: false,
			paused: true,
			playEnabled: true,
			seekEnabled: false,
			volume: 1,
			volumeEnabled: true
		});

		const syncControlState = function() {
			syncPreviewMediaControlState(controls, controlState);
		};
		const updateFromPlayerState = function(state) {
			controlState = tiktokMediaControlStateFromState(state || playerState, controlState);
			syncControlState();
		};
		const applyOptimisticState = function(nextState) {
			controlState = Object.assign({}, controlState, nextState || {});
			syncControlState();
		};

		const controls = appendPreviewMediaControlSurface(card, media, {
			className: "preview-card-tiktok-controls",
			volume: false,
			onPlay: function() {
				const shouldPlay = controlState.paused !== false;
				postTikTokIframeCommand(iframe, shouldPlay ? "play" : "pause");
				applyOptimisticState({
					paused: !shouldPlay,
					playerState: shouldPlay ? 1 : 2
				});
			},
			onSeek: function(fraction) {
				if (!controlState.duration) {
					return;
				}
				const nextTime = Math.max(0, Math.min(1, Number(fraction) || 0)) * controlState.duration;
				postTikTokIframeCommand(iframe, "seekTo", nextTime);
				applyOptimisticState({ currentTime: nextTime });
			},
			onMute: function() {
				const nextMuted = !controlState.muted;
				postTikTokIframeCommand(iframe, nextMuted ? "mute" : "unMute");
				applyOptimisticState({
					muted: nextMuted,
					volume: nextMuted ? 0 : (controlState.volume > 0 ? controlState.volume : 1)
				});
			}
		});

		if (playerState && Array.isArray(playerState.stateChangeCallbacks) && !playerState.previewMediaControlsAttached) {
			playerState.previewMediaControlsAttached = true;
			playerState.stateChangeCallbacks.push(function(data, state) {
				updateFromPlayerState(state);
			});
		}
		syncControlState();
		iframe.addEventListener("load", function() {
			ensureTikTokIframePlayer(iframe);
			schedulePreviewEmbedFrameSizeSync();
		});
		return controls;
	}

	function appendPreviewEmbedControls(card, media, iframe, embedKind) {
		const isYouTube = embedKind === "youtube" && previewIsYouTubeIframe(iframe);
		if (isYouTube) {
			appendYouTubeEmbedMediaControls(card, media, iframe);
			return;
		}
		const isTikTok = embedKind === "tiktok" && previewIsTikTokIframe(iframe);
		if (isTikTok) {
			appendTikTokEmbedMediaControls(card, media, iframe);
			return;
		}
		const controls = appendPreviewMediaControlSurface(card, media, {
			className: "preview-card-embed-controls is-size-only",
			mute: false,
			play: false,
			seek: false,
			time: false,
			volume: false
		});
		if (controls) {
			syncPreviewMediaControlState(controls, { playEnabled: false, seekEnabled: false, volumeEnabled: false });
		}
	}

	function previewClassToken(value) {
		return String(value || "")
			.trim()
			.toLowerCase()
			.replace(/[^a-z0-9_-]+/g, "-")
			.replace(/^-+|-+$/g, "");
	}

	function appendEmbedPreview(card, preview, embedUrl, embedAspect, embedKind) {
		const kindToken = previewClassToken(embedKind) || "generic";
		const media = document.createElement("div");
		media.className = "preview-card-media preview-card-playback preview-card-embed-media preview-card-"
			+ kindToken + "-embed-media";
		if (!previewInlineMediaEnabled()) {
			appendPreviewPlaybackFallback(media, preview, previewStaticPlaybackText(preview, "Open in browser"));
			card.appendChild(media);
			requestAnimationFrame(syncScrollState);
			return media;
		}
		media.addEventListener("click", function(event) {
			event.stopPropagation();
			if (event.target && event.target.closest && event.target.closest(".preview-card-media-controls")) {
				return;
			}
			if (embedKind === "youtube" || embedKind === "tiktok") {
				const playButton = media.querySelector(".preview-card-media-play");
				if (playButton && !playButton.disabled) {
					event.preventDefault();
					playButton.click();
				}
			}
		});

		const frameWrap = document.createElement("div");
		frameWrap.className = "preview-card-embed-frame-wrap preview-card-" + kindToken + "-frame-wrap";
		const iframe = document.createElement("iframe");
		iframe.className = "preview-card-embed-frame preview-card-" + kindToken + "-frame";
		iframe.src = embedKind === "youtube" ? normalizeYouTubeEmbedUrlForApi(embedUrl)
			: (embedKind === "tiktok" ? normalizeTikTokEmbedUrlForApi(embedUrl) : embedUrl);
		iframe.title = preview.title || preview.subtitle || "Embedded preview";
		iframe.loading = embedKind === "youtube" || embedKind === "tiktok" ? "eager" : "lazy";
		if (embedKind === "youtube") {
			iframe.setAttribute("enablejsapi", "true");
		}
		iframe.allow = "accelerometer; autoplay; clipboard-write; encrypted-media; fullscreen; gyroscope; picture-in-picture";
		iframe.referrerPolicy = "strict-origin-when-cross-origin";
		iframe.allowFullscreen = true;
		frameWrap.appendChild(iframe);
		media.appendChild(frameWrap);
		appendPreviewEmbedControls(card, media, iframe, embedKind);
		card.appendChild(media);
		schedulePreviewEmbedFrameSizeSync();
		return media;
	}

	function createPreviewPlayableMedia(card, preview, mediaUrl, mediaMime, mediaAudioUrl, mediaAudioMime, isVideoMedia, isGifMedia, videoInlineSupported, extraClass) {
		const media = document.createElement("div");
		media.className = "preview-card-media preview-card-playback" + (extraClass ? " " + extraClass : "");
		if ((isVideoMedia || isGifMedia) && !previewInlineMediaEnabled()) {
			appendPreviewPlaybackFallback(media, preview, previewStaticPlaybackText(preview, "Open in browser"));
			return media;
		}
		if (isVideoMedia && videoInlineSupported) {
			const video = document.createElement("video");
			video.className = "preview-card-video";
			video.src = mediaUrl;
			video.controls = false;
			const startMuted = !!(preview.autoplay || preview.startMuted);
			video.muted = startMuted;
			video.defaultMuted = startMuted;
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
			media.addEventListener("click", function(event) {
				event.stopPropagation();
				if (event.defaultPrevented || event.detail > 1
					|| event.target.closest(".preview-card-media-controls")) {
					return;
				}
				event.preventDefault();
				togglePreviewVideoPlayback(video, media.querySelector(".preview-card-media-controls"));
			});
			const showPlaybackFallback = function(fallbackText) {
				if (media.querySelector(".preview-card-playback-note")) {
					return;
				}
				const controls = media.querySelector(".preview-card-media-controls");
				if (controls) {
					controls.remove();
				}
				video.remove();
				appendPreviewPlaybackFallback(media, preview, fallbackText || "Open in browser");
				requestAnimationFrame(syncScrollState);
			};
			video.addEventListener("loadedmetadata", function() {
				if (video.videoWidth <= 0 && video.videoHeight <= 0) {
					console.warn("Preview video has no visual track", mediaMime, mediaUrl);
					showPlaybackFallback("Open in browser");
				}
			}, { once: true });
			video.addEventListener("error", function() {
				const code = video.error ? video.error.code : 0;
				console.warn("Preview video playback failed", mediaMime, mediaUrl, code);
				showPlaybackFallback("Open in browser");
			}, { once: true });
			media.appendChild(video);
			if (mediaAudioUrl) {
				attachPreviewAudioTrack(media, video, mediaAudioUrl, mediaAudioMime);
			}
			appendPreviewMediaControls(card, media, video);
			media.addEventListener("dblclick", function(event) {
				event.preventDefault();
				event.stopPropagation();
				setPreviewCardSize(card, previewCardSizeKey(card) === "large" ? "default" : "large");
			});
		} else if (isVideoMedia) {
			appendPreviewPlaybackFallback(media, preview, "Open in browser");
		} else {
			const image = document.createElement("img");
			image.className = "preview-card-image preview-card-media-image";
			image.loading = "lazy";
			image.src = mediaUrl;
			image.alt = preview.title || preview.subtitle || "Media preview";
			if (isGifMedia) {
				media.addEventListener("click", function(event) {
					event.stopPropagation();
					if (event.defaultPrevented || event.detail > 1) {
						return;
					}
					event.preventDefault();
					togglePreviewGifPlayback(media, image);
				});
			}
			media.appendChild(image);
		}
		return media;
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

	function previewUrlObject(url) {
		const rawUrl = String(url || "").trim();
		if (!rawUrl) {
			return null;
		}
		try {
			return new URL(rawUrl);
		} catch (error) {
			return null;
		}
	}

	function normalizedPreviewHost(host) {
		return String(host || "")
			.trim()
			.toLowerCase()
			.replace(/^www\./, "")
			.replace(/^old\./, "")
			.replace(/^new\./, "")
			.replace(/^m\./, "")
			.replace(/^mobile\./, "");
	}

	function previewIsTwitch(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		const provider = String(metadata.provider || metadata.previewProvider || "").trim().toLowerCase();
		if (provider === "twitch") {
			return true;
		}
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		return host === "twitch.tv" || host === "clips.twitch.tv";
	}

	function previewIsXPost(preview, hostLabel) {
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		if (host !== "x.com" && host !== "twitter.com") {
			return false;
		}
		if (!url) {
			return false;
		}
		return /\/status(?:es)?\/[0-9]+/i.test(url.pathname);
	}

	function previewIsGoogleSearch(preview, hostLabel) {
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		if (!url || !/^google\./i.test(host)) {
			return false;
		}
		const path = String(url.pathname || "").replace(/\/+$/, "");
		return (!path || path === "/search")
			&& (!!url.searchParams.get("q") || !!url.searchParams.get("as_q"));
	}

	function previewGoogleSearchInfo(preview) {
		const url = previewUrlObject(preview && preview.url);
		const params = url ? url.searchParams : new URLSearchParams();
		const query = String(params.get("q") || params.get("as_q") || (preview && preview.description) || "").trim();
		const tbm = String(params.get("tbm") || "").toLowerCase();
		const udm = String(params.get("udm") || "").toLowerCase();
		let mode = "All";
		let title = "Google Search";
		if (tbm === "isch" || udm === "2") {
			mode = "Images";
			title = "Google Images";
		} else if (tbm === "vid" || udm === "7") {
			mode = "Videos";
			title = "Google Videos";
		} else if (tbm === "nws") {
			mode = "News";
			title = "Google News";
		} else if (tbm === "shop" || udm === "28") {
			mode = "Shopping";
			title = "Google Shopping";
		} else if (tbm === "bks") {
			mode = "Books";
			title = "Google Books";
		}
		return { query: query || "Search", mode: mode, title: title };
	}

	function appendGoogleSearchPreview(card, preview) {
		const info = previewGoogleSearchInfo(preview);
		const shell = document.createElement("div");
		shell.className = "preview-card-google-shell";

		const top = document.createElement("div");
		top.className = "preview-card-google-top";
		const logo = document.createElement("div");
		logo.className = "preview-card-google-logo";
		["G", "o", "o", "g", "l", "e"].forEach(function(letter, index) {
			const span = document.createElement("span");
			span.className = "preview-card-google-logo-letter is-" + index;
			span.textContent = letter;
			logo.appendChild(span);
		});
		top.appendChild(logo);
		const badge = document.createElement("span");
		badge.className = "preview-card-google-badge";
		badge.textContent = info.mode;
		top.appendChild(badge);
		shell.appendChild(top);

		const search = document.createElement("div");
		search.className = "preview-card-google-search";
		const query = document.createElement("div");
		query.className = "preview-card-google-query";
		query.textContent = info.query;
		search.appendChild(query);
		const icon = document.createElement("span");
		icon.className = "preview-card-google-icon";
		icon.textContent = "Search";
		search.appendChild(icon);
		shell.appendChild(search);

		const tabs = document.createElement("div");
		tabs.className = "preview-card-google-tabs";
		["All", "Images", "News", "Videos", "Shopping"].forEach(function(tabName) {
			const tab = document.createElement("span");
			tab.className = "preview-card-google-tab" + (tabName === info.mode ? " is-active" : "");
			tab.textContent = tabName;
			tabs.appendChild(tab);
		});
		shell.appendChild(tabs);

		const footer = document.createElement("div");
		footer.className = "preview-card-google-footer";
		const title = document.createElement("span");
		title.className = "preview-card-google-title";
		title.textContent = info.title;
		footer.appendChild(title);
		const action = document.createElement("span");
		action.className = "preview-card-google-action";
		action.textContent = (preview && preview.openLabel) || "Open on Google";
		footer.appendChild(action);
		shell.appendChild(footer);

		card.appendChild(shell);
	}

	function previewIsSteam(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		if (metadata.provider === "steam") {
			return true;
		}
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		return host === "store.steampowered.com" || host === "steamcommunity.com";
	}

	function previewSteamInfo(preview, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const discount = Number(metadata.steamDiscountPercent || 0);
		return {
			appName: String(metadata.steamAppName || "").trim(),
			price: String(metadata.steamPrice || "").trim(),
			originalPrice: String(metadata.steamOriginalPrice || "").trim(),
			discount: Number.isFinite(discount) && discount > 0 ? discount : 0,
			developer: String(metadata.steamDeveloper || "").trim(),
			releaseDate: String(metadata.steamReleaseDate || "").trim(),
			platforms: String(metadata.steamPlatforms || "").trim(),
			genres: String(metadata.steamGenres || "").trim(),
			description: String(descriptionText || "").trim()
		};
	}

	function previewSteamMediaItems(preview) {
		const metadata = (preview && preview.metadata) || {};
		const rawItems = Array.isArray(metadata.steamMediaItems) ? metadata.steamMediaItems : [];
		const seen = {};
		const items = [];
		rawItems.forEach(function(rawItem) {
			const item = rawItem || {};
			const url = String(item.url || "").trim();
			const kind = String(item.kind || "").trim().toLowerCase();
			if (!url || seen[url] || (kind !== "image" && kind !== "video")) {
				return;
			}
			seen[url] = true;
			let mime = String(item.mime || "").trim().toLowerCase();
			if (!mime) {
				const path = url.split("?")[0].toLowerCase();
				if (path.endsWith(".m3u8")) {
					mime = "application/vnd.apple.mpegurl";
				} else if (path.endsWith(".webm")) {
					mime = "video/webm";
				} else if (path.endsWith(".mp4") || path.endsWith(".m4v")) {
					mime = "video/mp4";
				} else if (kind === "image") {
					mime = "image/jpeg";
				}
			}
			items.push({
				kind: kind,
				url: url,
				mime: mime,
				title: String(item.title || "").trim(),
				thumbnail: String(item.thumbnail || item.poster || "").trim(),
				poster: String(item.poster || item.thumbnail || "").trim(),
				streamKind: String(item.streamKind || "").trim().toLowerCase()
			});
		});
		return items;
	}

	function steamHlsParseAttributes(value) {
		const attrs = {};
		String(value || "").replace(/([A-Z0-9-]+)=("[^"]*"|[^,]*)/ig, function(match, key, rawValue) {
			let text = String(rawValue || "").trim();
			if (text.charAt(0) === "\"" && text.charAt(text.length - 1) === "\"") {
				text = text.slice(1, -1);
			}
			attrs[String(key || "").toUpperCase()] = text;
			return match;
		});
		return attrs;
	}

	function steamHlsResolveUrl(baseUrl, path) {
		return new URL(String(path || "").trim(), baseUrl).toString();
	}

	function steamHlsFetchText(url) {
		return fetch(url, { mode: "cors", credentials: "omit" }).then(function(response) {
			if (!response.ok) {
				throw new Error("HTTP " + String(response.status));
			}
			return response.text();
		});
	}

	function steamHlsVideoCodec(codecs) {
		const parts = String(codecs || "").split(",").map(function(part) {
			return part.trim();
		});
		return parts.find(function(part) {
			return /^avc1\./i.test(part) || /^hvc1\./i.test(part) || /^hev1\./i.test(part);
		}) || "avc1.640029";
	}

	function steamHlsAudioCodec(codecs) {
		const parts = String(codecs || "").split(",").map(function(part) {
			return part.trim();
		});
		return parts.find(function(part) {
			return /^mp4a\./i.test(part);
		}) || "mp4a.40.2";
	}

	function steamHlsParseMaster(text, masterUrl) {
		const lines = String(text || "").split(/\r?\n/).map(function(line) {
			return line.trim();
		}).filter(Boolean);
		const result = {
			audioUrl: "",
			variants: []
		};
		for (let index = 0; index < lines.length; index += 1) {
			const line = lines[index];
			if (line.indexOf("#EXT-X-MEDIA:") === 0) {
				const attrs = steamHlsParseAttributes(line.slice("#EXT-X-MEDIA:".length));
				if (String(attrs.TYPE || "").toUpperCase() === "AUDIO" && attrs.URI && !result.audioUrl) {
					result.audioUrl = steamHlsResolveUrl(masterUrl, attrs.URI);
				}
			} else if (line.indexOf("#EXT-X-STREAM-INF:") === 0) {
				const attrs = steamHlsParseAttributes(line.slice("#EXT-X-STREAM-INF:".length));
				let nextLine = "";
				for (let nextIndex = index + 1; nextIndex < lines.length; nextIndex += 1) {
					if (lines[nextIndex].charAt(0) !== "#") {
						nextLine = lines[nextIndex];
						index = nextIndex;
						break;
					}
				}
				if (nextLine) {
					const resolution = String(attrs.RESOLUTION || "").toLowerCase().split("x");
					result.variants.push({
						url: steamHlsResolveUrl(masterUrl, nextLine),
						bandwidth: Number(attrs.BANDWIDTH || 0) || 0,
						width: Number(resolution[0] || 0) || 0,
						height: Number(resolution[1] || 0) || 0,
						codecs: String(attrs.CODECS || "")
					});
				}
			}
		}
		return result;
	}

	function steamHlsChooseVariant(variants) {
		const sorted = (variants || []).slice().sort(function(left, right) {
			return (left.width || 0) - (right.width || 0)
				|| (left.bandwidth || 0) - (right.bandwidth || 0);
		});
		const preferred = sorted.filter(function(item) {
			return (item.width || 0) <= 1280;
		}).pop();
		return preferred || sorted[sorted.length - 1] || null;
	}

	function steamHlsParseMediaPlaylist(text, playlistUrl) {
		const lines = String(text || "").split(/\r?\n/).map(function(line) {
			return line.trim();
		}).filter(Boolean);
		const parsed = {
			initUrl: "",
			segments: [],
			duration: 0
		};
		let pendingDuration = 0;
		lines.forEach(function(line) {
			if (line.indexOf("#EXT-X-MAP:") === 0) {
				const attrs = steamHlsParseAttributes(line.slice("#EXT-X-MAP:".length));
				if (attrs.URI) {
					parsed.initUrl = steamHlsResolveUrl(playlistUrl, attrs.URI);
				}
			} else if (line.indexOf("#EXTINF:") === 0) {
				pendingDuration = Number(line.slice("#EXTINF:".length).split(",")[0]) || 0;
			} else if (line.charAt(0) !== "#") {
				parsed.segments.push(steamHlsResolveUrl(playlistUrl, line));
				parsed.duration += pendingDuration;
				pendingDuration = 0;
			}
		});
		return parsed;
	}

	function steamHlsAppendBuffer(sourceBuffer, buffer) {
		return new Promise(function(resolve, reject) {
			const cleanup = function() {
				sourceBuffer.removeEventListener("updateend", onUpdateEnd);
				sourceBuffer.removeEventListener("error", onError);
			};
			const onUpdateEnd = function() {
				cleanup();
				resolve();
			};
			const onError = function() {
				cleanup();
				reject(new Error("SourceBuffer append failed"));
			};
			sourceBuffer.addEventListener("updateend", onUpdateEnd);
			sourceBuffer.addEventListener("error", onError);
			sourceBuffer.appendBuffer(buffer);
		});
	}

	function steamHlsAppendSegments(sourceBuffer, playlist) {
		const urls = (playlist.initUrl ? [playlist.initUrl] : []).concat(playlist.segments || []);
		return urls.reduce(function(chain, url) {
			return chain.then(function() {
				return fetch(url, { mode: "cors", credentials: "omit" });
			}).then(function(response) {
				if (!response.ok) {
					throw new Error("HTTP " + String(response.status));
				}
				return response.arrayBuffer();
			}).then(function(buffer) {
				return steamHlsAppendBuffer(sourceBuffer, buffer);
			});
		}, Promise.resolve());
	}

	function createSteamHlsPlayableMedia(card, preview, item, extraClass) {
		const media = document.createElement("div");
		media.className = "preview-card-media preview-card-playback" + (extraClass ? " " + extraClass : "");
		if (!previewInlineMediaEnabled()) {
			appendPreviewPlaybackFallback(media, preview, previewStaticPlaybackText(preview, "Open in browser"));
			return media;
		}
		const mediaSourceCtor = window.MediaSource || window.ManagedMediaSource;
		if (!mediaSourceCtor || !item.url) {
			appendPreviewPlaybackFallback(media, preview, "Open in browser");
			return media;
		}

		const video = document.createElement("video");
		video.className = "preview-card-video";
		video.controls = false;
		video.loop = false;
		const startMuted = !!(preview && preview.startMuted);
		video.muted = startMuted;
		video.defaultMuted = startMuted;
		if (startMuted) {
			video.volume = 0.75;
		}
		video.playsInline = true;
		video.preload = "metadata";
		if (preview.thumbnailUrl) {
			video.poster = preview.thumbnailUrl;
		}
		video.setAttribute("aria-label", preview.title || preview.subtitle || "Steam trailer");
		media.appendChild(video);

		const mediaSource = new mediaSourceCtor();
		let objectUrl = "";
		let failed = false;
		const showFallback = function() {
			if (failed || media.querySelector(".preview-card-playback-note")) {
				return;
			}
			failed = true;
			const controls = media.querySelector(".preview-card-media-controls");
			if (controls) {
				controls.remove();
			}
			video.remove();
			appendPreviewPlaybackFallback(media, preview, "Open in browser");
			requestAnimationFrame(syncScrollState);
		};

		try {
			objectUrl = URL.createObjectURL(mediaSource);
			video.src = objectUrl;
		} catch (error) {
			showFallback();
			return media;
		}

		mediaSource.addEventListener("sourceopen", function() {
			steamHlsFetchText(item.url).then(function(masterText) {
				const master = steamHlsParseMaster(masterText, item.url);
				const variant = steamHlsChooseVariant(master.variants);
				const videoPlaylistUrl = variant ? variant.url : item.url;
				return Promise.all([
					steamHlsFetchText(videoPlaylistUrl),
					master.audioUrl ? steamHlsFetchText(master.audioUrl) : Promise.resolve("")
				]).then(function(playlists) {
					const videoPlaylist = steamHlsParseMediaPlaylist(playlists[0], videoPlaylistUrl);
					const audioPlaylist = playlists[1] ? steamHlsParseMediaPlaylist(playlists[1], master.audioUrl) : null;
					const videoType = "video/mp4; codecs=\"" + steamHlsVideoCodec(variant && variant.codecs) + "\"";
					const audioType = "audio/mp4; codecs=\"" + steamHlsAudioCodec(variant && variant.codecs) + "\"";
					if (!mediaSourceCtor.isTypeSupported(videoType) || !videoPlaylist.segments.length) {
						throw new Error("Unsupported Steam HLS video stream");
					}
					const videoBuffer = mediaSource.addSourceBuffer(videoType);
					const appenders = [steamHlsAppendSegments(videoBuffer, videoPlaylist)];
					if (audioPlaylist && audioPlaylist.segments.length && mediaSourceCtor.isTypeSupported(audioType)) {
						const audioBuffer = mediaSource.addSourceBuffer(audioType);
						appenders.push(steamHlsAppendSegments(audioBuffer, audioPlaylist));
					}
					mediaSource.duration = Math.max(videoPlaylist.duration || 0,
						audioPlaylist ? audioPlaylist.duration || 0 : 0);
					return Promise.all(appenders);
				});
			}).then(function() {
				if (mediaSource.readyState === "open") {
					mediaSource.endOfStream();
				}
			}).catch(function(error) {
				console.warn("Steam trailer HLS playback failed", error && error.message ? error.message : error);
				showFallback();
			});
		}, { once: true });

		video.addEventListener("error", function() {
			if (!failed) {
				console.warn("Steam trailer video element failed", item.mime, item.url);
				showFallback();
			}
		}, { once: true });
		video.addEventListener("emptied", function() {
			if (objectUrl) {
				URL.revokeObjectURL(objectUrl);
				objectUrl = "";
			}
		}, { once: true });
		media.addEventListener("click", function(event) {
			event.stopPropagation();
			if (event.defaultPrevented || event.detail > 1
				|| event.target.closest(".preview-card-media-controls")) {
				return;
			}
			event.preventDefault();
			togglePreviewVideoPlayback(video, media.querySelector(".preview-card-media-controls"));
		});
		appendPreviewMediaControls(card, media, video);
		media.addEventListener("dblclick", function(event) {
			event.preventDefault();
			event.stopPropagation();
			setPreviewCardSize(card, previewCardSizeKey(card) === "large" ? "default" : "large");
		});
		return media;
	}

	function appendSteamMediaGallery(card, shell, preview, mediaItems) {
		const gallery = document.createElement("div");
		gallery.className = "preview-card-steam-gallery";
		gallery.tabIndex = 0;
		gallery.addEventListener("click", function(event) {
			event.stopPropagation();
		});

		const stage = document.createElement("div");
		stage.className = "preview-card-steam-stage";
		gallery.appendChild(stage);

		const previousButton = document.createElement("button");
		previousButton.type = "button";
		previousButton.className = "preview-card-carousel-button preview-card-carousel-previous preview-card-steam-gallery-previous";
		previousButton.textContent = "<";
		previousButton.setAttribute("aria-label", "Previous Steam media");
		stage.appendChild(previousButton);

		const nextButton = document.createElement("button");
		nextButton.type = "button";
		nextButton.className = "preview-card-carousel-button preview-card-carousel-next preview-card-steam-gallery-next";
		nextButton.textContent = ">";
		nextButton.setAttribute("aria-label", "Next Steam media");
		stage.appendChild(nextButton);

		const rail = document.createElement("div");
		rail.className = "preview-card-steam-thumbnails";
		const thumbButtons = mediaItems.map(function(item, itemIndex) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "preview-card-steam-thumbnail";
			button.setAttribute("aria-label", (item.kind === "video" ? "Show trailer " : "Show screenshot ")
				+ String(itemIndex + 1));
			const imageUrl = item.thumbnail || item.poster || (item.kind === "image" ? item.url : "");
			if (imageUrl) {
				const image = document.createElement("img");
				image.className = "preview-card-steam-thumbnail-image";
				image.loading = "lazy";
				image.src = imageUrl;
				image.alt = item.title || (item.kind === "video" ? "Steam trailer" : "Steam screenshot");
				button.appendChild(image);
			} else {
				const placeholder = document.createElement("span");
				placeholder.className = "preview-card-steam-thumbnail-placeholder";
				placeholder.textContent = item.kind === "video" ? "VID" : "IMG";
				button.appendChild(placeholder);
			}
			if (item.kind === "video") {
				const playMark = document.createElement("span");
				playMark.className = "preview-card-steam-thumbnail-play";
				playMark.textContent = "Play";
				button.appendChild(playMark);
			}
			button.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				setIndex(itemIndex);
			});
			rail.appendChild(button);
			return button;
		});
		gallery.appendChild(rail);

		let index = 0;
		const renderItem = function() {
			const activeVideo = stage.querySelector("video");
			if (activeVideo) {
				activeVideo.pause();
			}
			Array.prototype.slice.call(stage.children).forEach(function(child) {
				if (child !== previousButton && child !== nextButton) {
					stage.removeChild(child);
				}
			});

			const item = mediaItems[index];
			stage.classList.toggle("is-video", item.kind === "video");
			stage.classList.toggle("is-image", item.kind !== "video");
			if (item.kind === "video") {
				const mediaPreview = Object.assign({}, preview || {}, {
					title: item.title || (preview && preview.title) || "Steam trailer",
					thumbnailUrl: item.poster || item.thumbnail || (preview && preview.thumbnailUrl) || "",
					autoplay: false,
					startMuted: true
				});
				const playable = item.streamKind === "hls"
					? createSteamHlsPlayableMedia(card, mediaPreview, item, "preview-card-steam-stage-player")
					: createPreviewPlayableMedia(card, mediaPreview, item.url, item.mime, "", "", true, false,
						previewInlineMediaEnabled() && previewVideoCanPlayInline(item.mime, item.url),
						"preview-card-steam-stage-player");
				stage.insertBefore(playable, previousButton);
			} else {
				const image = document.createElement("img");
				image.className = "preview-card-steam-stage-image";
				image.loading = "lazy";
				image.src = item.url;
				image.alt = item.title || (preview && preview.title) || "Steam screenshot";
				image.addEventListener("load", function() {
					requestAnimationFrame(syncScrollState);
				}, { once: true });
				stage.insertBefore(image, previousButton);
			}

			thumbButtons.forEach(function(button, buttonIndex) {
				const active = buttonIndex === index;
				button.classList.toggle("is-active", active);
				button.setAttribute("aria-current", active ? "true" : "false");
				if (active && typeof button.scrollIntoView === "function") {
					button.scrollIntoView({ behavior: "auto", block: "nearest", inline: "nearest" });
				}
			});
			requestAnimationFrame(syncScrollState);
		};
		const setIndex = function(nextIndex) {
			index = (nextIndex + mediaItems.length) % mediaItems.length;
			renderItem();
		};
		const step = function(delta, event) {
			if (event) {
				event.preventDefault();
				event.stopPropagation();
			}
			setIndex(index + delta);
		};

		previousButton.addEventListener("click", function(event) {
			step(-1, event);
		});
		nextButton.addEventListener("click", function(event) {
			step(1, event);
		});
		gallery.addEventListener("keydown", function(event) {
			if (event.key === "ArrowLeft") {
				step(-1, event);
			} else if (event.key === "ArrowRight") {
				step(1, event);
			}
		});

		renderItem();
		shell.appendChild(gallery);
		return gallery;
	}

	function appendSteamPreview(card, preview, hostLabel, sourceLabel, descriptionText, mediaItems) {
		const info = previewSteamInfo(preview, descriptionText);
		const shell = document.createElement("div");
		shell.className = "preview-card-steam-shell";

		const galleryItems = Array.isArray(mediaItems) ? mediaItems : previewSteamMediaItems(preview);
		const imageUrl = String(preview.thumbnailUrl
			|| ((preview.metadata && (preview.metadata.steamHeaderImage || preview.metadata.steamCapsuleImage)) || ""))
			.trim();
		if (galleryItems.length) {
			appendSteamMediaGallery(card, shell, preview, galleryItems);
		} else if (imageUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-steam-media";
			const image = document.createElement("img");
			image.className = "preview-card-steam-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = preview.title || info.appName || "Steam preview";
			media.appendChild(image);
			shell.appendChild(media);
		}

		const body = document.createElement("div");
		body.className = "preview-card-steam-body";

		const meta = document.createElement("div");
		meta.className = "preview-card-steam-meta";
		const source = document.createElement("span");
		source.className = "preview-card-steam-source";
		source.textContent = sourceLabel || "Steam";
		meta.appendChild(source);

		const metaActions = document.createElement("div");
		metaActions.className = "preview-card-steam-meta-actions";
		appendPreviewCardSizeButton(card, metaActions);
		const host = document.createElement("span");
		host.className = "preview-card-steam-host";
		host.textContent = hostLabel || "store.steampowered.com";
		metaActions.appendChild(host);
		meta.appendChild(metaActions);
		body.appendChild(meta);

		const title = document.createElement("div");
		title.className = "preview-card-steam-title";
		title.textContent = preview.title || info.appName || "Steam";
		body.appendChild(title);

		if (info.description) {
			const description = document.createElement("div");
			description.className = "preview-card-steam-description";
			description.textContent = info.description;
			body.appendChild(description);
		}

		if (info.appName || info.price || info.developer || info.releaseDate || info.platforms) {
			const product = document.createElement("div");
			product.className = "preview-card-steam-product";

			const productText = document.createElement("div");
			productText.className = "preview-card-steam-product-text";
			const productName = document.createElement("div");
			productName.className = "preview-card-steam-product-name";
			productName.textContent = info.appName || "Steam app";
			productText.appendChild(productName);
			const detailParts = [info.developer, info.releaseDate, info.platforms || info.genres].filter(Boolean);
			if (detailParts.length) {
				const productDetail = document.createElement("div");
				productDetail.className = "preview-card-steam-product-detail";
				productDetail.textContent = detailParts.join(" / ");
				productText.appendChild(productDetail);
			}
			product.appendChild(productText);

			const purchase = document.createElement("div");
			purchase.className = "preview-card-steam-purchase";
			if (info.price || info.discount) {
				const price = document.createElement("div");
				price.className = "preview-card-steam-price";
				if (info.discount) {
					const discount = document.createElement("span");
					discount.className = "preview-card-steam-discount";
					discount.textContent = "-" + String(info.discount) + "%";
					price.appendChild(discount);
				}
				const priceStack = document.createElement("span");
				priceStack.className = "preview-card-steam-price-stack";
				if (info.originalPrice && info.originalPrice !== info.price) {
					const original = document.createElement("span");
					original.className = "preview-card-steam-price-original";
					original.textContent = info.originalPrice;
					priceStack.appendChild(original);
				}
				const finalPrice = document.createElement("span");
				finalPrice.className = "preview-card-steam-price-final";
				finalPrice.textContent = info.price || "On Steam";
				priceStack.appendChild(finalPrice);
				price.appendChild(priceStack);
				purchase.appendChild(price);
			}
			const cart = document.createElement("span");
			cart.className = "preview-card-steam-cart";
			cart.setAttribute("aria-hidden", "true");
			cart.appendChild(createPreviewCartIcon("preview-card-steam-cart-icon"));
			const cartLabel = document.createElement("span");
			cartLabel.className = "preview-card-steam-cart-label";
			cartLabel.textContent = info.price ? "Cart" : "Open";
			cart.appendChild(cartLabel);
			purchase.appendChild(cart);
			product.appendChild(purchase);

			body.appendChild(product);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-steam-footer";
		const action = document.createElement("span");
		action.className = "preview-card-steam-action";
		action.textContent = (preview && preview.openLabel) || "Open on Steam";
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	const previewGameStoreLabels = {
		g2a: "G2A",
		kinguin: "Kinguin",
		epic: "Epic Games Store",
		gog: "GOG",
		ubisoft: "Ubisoft Store",
		ea: "EA",
		humble: "Humble Store",
		fanatical: "Fanatical",
		greenmangaming: "Green Man Gaming",
		itch: "itch.io",
		battlenet: "Battle.net",
		xbox: "Xbox Store"
	};

	const richPreviewProviderSpecs = [
		{ provider: "yahoo-finance", kind: "finance", label: "Yahoo Finance", mark: "YF", hostSuffixes: ["finance.yahoo.com"] },
		{ provider: "tradera", kind: "marketplaceListing", label: "Tradera", mark: "T", hostSuffixes: ["tradera.com"] },
		{ provider: "blocket", kind: "marketplaceListing", label: "Blocket", mark: "B", hostSuffixes: ["blocket.se"] },
		{ provider: "flashback", kind: "forum", label: "Flashback", mark: "FB", hostSuffixes: ["flashback.org"] },
		{ provider: "sweclockers", kind: "article", label: "SweClockers", mark: "SC", hostSuffixes: ["sweclockers.com"] },
		{ provider: "existenz", kind: "linkDigest", label: "Existenz", mark: "E", hostSuffixes: ["existenz.se"] },
		{ provider: "hemnet", kind: "realEstate", label: "Hemnet", mark: "H", hostSuffixes: ["hemnet.se"] },
		{ provider: "booli", kind: "realEstate", label: "Booli", mark: "B", hostSuffixes: ["booli.se"] },
		{ provider: "prisjakt", kind: "product", label: "Prisjakt", mark: "PJ", hostSuffixes: ["prisjakt.nu", "prisjakt.se"] },
		{ provider: "pricerunner", kind: "product", label: "PriceRunner", mark: "PR", hostSuffixes: ["pricerunner.se", "pricerunner.com"] },
		{ provider: "gp", kind: "article", label: "GP", mark: "GP", hostSuffixes: ["gp.se"] },
		{ provider: "svt", kind: "article", label: "SVT", mark: "SVT", hostSuffixes: ["svt.se"] },
		{ provider: "omni", kind: "article", label: "Omni", mark: "O", hostSuffixes: ["omni.se"] },
		{ provider: "aftonbladet", kind: "article", label: "Aftonbladet", mark: "AB", hostSuffixes: ["aftonbladet.se"] },
		{ provider: "expressen", kind: "article", label: "Expressen", mark: "EX", hostSuffixes: ["expressen.se"] },
		{ provider: "dn", kind: "article", label: "DN", mark: "DN", hostSuffixes: ["dn.se"] },
		{ provider: "sverigesradio", kind: "audio", label: "Sveriges Radio", mark: "SR", hostSuffixes: ["sverigesradio.se", "sr.se"] },
		{ provider: "inet", kind: "product", label: "Inet", mark: "I", hostSuffixes: ["inet.se"] },
		{ provider: "webhallen", kind: "product", label: "Webhallen", mark: "W", hostSuffixes: ["webhallen.com"] },
		{ provider: "elgiganten", kind: "product", label: "Elgiganten", mark: "E", hostSuffixes: ["elgiganten.se"] },
		{ provider: "komplett", kind: "product", label: "Komplett", mark: "K", hostSuffixes: ["komplett.se"] },
		{ provider: "systembolaget", kind: "systembolagetProduct", label: "Systembolaget", mark: "SB", hostSuffixes: ["systembolaget.se"] },
		{ provider: "smhi", kind: "weather", label: "SMHI", mark: "SMHI", hostSuffixes: ["smhi.se"] },
		{ provider: "klart", kind: "weather", label: "Klart", mark: "K", hostSuffixes: ["klart.se"] },
		{ provider: "yr", kind: "weather", label: "Yr", mark: "YR", hostSuffixes: ["yr.no"] },
		{ provider: "hitta", kind: "place", label: "Hitta", mark: "H", hostSuffixes: ["hitta.se"] },
		{ provider: "eniro", kind: "place", label: "Eniro", mark: "E", hostSuffixes: ["eniro.se"] },
		{ provider: "googlemaps", kind: "place", label: "Google Maps", mark: "G", hostSuffixes: ["maps.google.com", "maps.app.goo.gl"] },
		{ provider: "sj", kind: "traffic", label: "SJ", mark: "SJ", hostSuffixes: ["sj.se"] },
		{ provider: "sl", kind: "traffic", label: "SL", mark: "SL", hostSuffixes: ["sl.se"] },
		{ provider: "vasttrafik", kind: "traffic", label: "V\u00e4sttrafik", mark: "V", hostSuffixes: ["vasttrafik.se"] },
		{ provider: "amazon", kind: "product", label: "Amazon", mark: "A", hostSuffixes: [
			"amazon.se", "amazon.com", "amazon.de", "amazon.co.uk", "amazon.fr", "amazon.it", "amazon.es",
			"amazon.nl", "amazon.pl"
		] }
	];

	function previewHostMatchesSuffix(host, suffix) {
		return host === suffix || host.endsWith("." + suffix);
	}

	function previewProviderSpec(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		const metadataProvider = String(metadata.previewProvider || metadata.provider || metadata.marketplaceProvider || "")
			.trim()
			.toLowerCase();
		const metadataKind = String(metadata.previewKind || metadata.swedishPreviewKind || "").trim();
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		const path = String((url && url.pathname) || "").toLowerCase();

		let spec = richPreviewProviderSpecs.find(function(item) {
			return item.provider === metadataProvider;
		}) || null;
		if (!spec) {
			spec = richPreviewProviderSpecs.find(function(item) {
				if (item.provider === "googlemaps") {
					return (host.indexOf("google.") === 0 && path.indexOf("/maps") === 0)
						|| host.indexOf("maps.google.") === 0
						|| previewHostMatchesSuffix(host, "maps.app.goo.gl");
				}
				return item.hostSuffixes.some(function(suffix) {
					return previewHostMatchesSuffix(host, suffix);
				});
			}) || null;
		}
		if (!spec) {
			return null;
		}

		const resolved = Object.assign({}, spec);
		if (metadataKind) {
			resolved.kind = metadataKind;
		}
		if (resolved.provider === "sweclockers" && /\/forum(?:\/|$)/i.test(path)) {
			resolved.kind = "forum";
		}
		return resolved;
	}

	function previewGameStoreKind(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		const provider = String(metadata.provider || "").trim().toLowerCase();
		const metadataKind = previewClassToken(metadata.gameStoreProvider || metadata.gameStoreName);
		if (provider === "game-store" && metadataKind) {
			return metadataKind;
		}

		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		const path = String(url && url.pathname || "").toLowerCase();
		if (host === "store.steampowered.com" || host === "steamcommunity.com") {
			return "";
		}
		if (host === "g2a.com" || host.endsWith(".g2a.com")) {
			return "g2a";
		}
		if (host === "kinguin.net" || host.endsWith(".kinguin.net")
			|| host === "kinguin.com" || host.endsWith(".kinguin.com")) {
			return "kinguin";
		}
		if (host === "store.epicgames.com" || host === "store.epic.com") {
			return "epic";
		}
		if (host === "gog.com" || host.endsWith(".gog.com")) {
			return "gog";
		}
		if (host === "store.ubisoft.com" || host === "store.ubi.com"
			|| ((host === "ubisoft.com" || host.endsWith(".ubisoft.com")) && path.indexOf("/game") === 0)) {
			return "ubisoft";
		}
		if ((host === "ea.com" || host.endsWith(".ea.com")) && path.indexOf("/games") === 0) {
			return "ea";
		}
		if ((host === "humblebundle.com" || host.endsWith(".humblebundle.com")) && path.indexOf("/store") === 0) {
			return "humble";
		}
		if (host === "fanatical.com" || host.endsWith(".fanatical.com")) {
			return "fanatical";
		}
		if (host === "greenmangaming.com" || host.endsWith(".greenmangaming.com")) {
			return "greenmangaming";
		}
		if (host === "itch.io" || host.endsWith(".itch.io")) {
			return "itch";
		}
		if (host === "shop.battle.net" || host === "battle.net" || host.endsWith(".battle.net")) {
			return "battlenet";
		}
		if ((host === "xbox.com" || host.endsWith(".xbox.com")) && path.indexOf("/store") >= 0) {
			return "xbox";
		}
		return "";
	}

	function previewGameStoreInfo(preview, kind, hostLabel, sourceLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const label = String(metadata.gameStoreName || previewGameStoreLabels[kind] || sourceLabel || hostLabel || "Store")
			.trim();
		const title = previewMetadataString(metadata, ["gameStoreProductTitle"])
			|| String((preview && preview.title) || "").trim()
			|| label;
		const description = previewMetadataString(metadata, ["gameStoreDescription"])
			|| String(descriptionText || "").trim();
		const platform = previewMetadataString(metadata, ["gameStorePlatform"]);
		const availability = previewMetadataString(metadata, ["gameStoreAvailability"]);
		const rating = previewMetadataString(metadata, ["gameStoreRating"]);
		const reviewCount = previewMetadataString(metadata, ["gameStoreReviewCount"]);
		const brand = previewMetadataString(metadata, ["gameStoreBrand"]);
		const sku = previewMetadataString(metadata, ["gameStoreSku"]);
		let tags = previewMetadataList(metadata, "gameStoreTags").map(function(value) {
			return String(value || "").trim();
		}).filter(Boolean);
		if (!tags.length) {
			tags = previewMetadataString(metadata, ["gameStoreTags"]).split(/\s*[,;|]\s*/).filter(Boolean);
		}
		const detail = [platform, availability, tags[0] || ""].filter(Boolean).join(" / ") || brand || label;
		const chips = [];
		const appendChip = function(value) {
			const text = String(value || "").trim();
			if (text && chips.indexOf(text) < 0) {
				chips.push(text);
			}
		};
		tags.slice(0, 4).forEach(appendChip);
		[platform, availability, rating, reviewCount ? reviewCount + " reviews" : "", brand, sku ? "SKU " + sku : ""]
			.forEach(appendChip);
		return {
			kind: kind,
			label: label,
			host: hostLabel || label,
			title: title,
			description: description,
			detail: detail,
			chips: chips.slice(0, 6),
			price: previewMetadataString(metadata, ["gameStorePrice", "listingPrice"]),
			originalPrice: previewMetadataString(metadata, ["gameStoreOriginalPrice", "listingOriginalPrice"]),
			discount: previewMetadataString(metadata, ["gameStoreDiscount", "listingDiscount"])
		};
	}

	function previewGameStoreMediaItems(preview) {
		const metadata = (preview && preview.metadata) || {};
		const productMedia = [];
		["gameStoreMedia", "gameStoreMediaItems"].forEach(function(key) {
			if (Array.isArray(metadata[key])) {
				metadata[key].forEach(function(item) {
					productMedia.push(item);
				});
			}
		});
		const proxyPreview = Object.assign({}, preview || {}, {
			metadata: Object.assign({}, metadata, {
				productMedia: productMedia,
				productImages: Array.isArray(metadata.gameStoreImages) ? metadata.gameStoreImages : [],
				productImage: String(metadata.gameStoreImage || "").trim()
			})
		});
		return previewProductMediaItems(proxyPreview);
	}

	function appendGameStoreChip(parent, value) {
		const text = String(value || "").trim();
		if (!text) {
			return;
		}
		const chip = document.createElement("span");
		chip.className = "preview-card-store-chip";
		chip.textContent = text;
		parent.appendChild(chip);
	}

	function appendGameStorePreview(card, preview, kind, hostLabel, sourceLabel, descriptionText, mediaItems) {
		const info = previewGameStoreInfo(preview, kind, hostLabel, sourceLabel, descriptionText);
		const metadata = (preview && preview.metadata) || {};
		const shell = document.createElement("div");
		shell.className = "preview-card-store-shell";

		const galleryItems = Array.isArray(mediaItems) ? mediaItems : previewGameStoreMediaItems(preview);
		const imageUrl = String(preview.thumbnailUrl || metadata.gameStoreImage || "").trim();
		if (galleryItems.length) {
			appendProductMediaGallery(card, shell, preview, galleryItems);
		} else if (imageUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-store-media";
			const image = document.createElement("img");
			image.className = "preview-card-store-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = info.title || info.label;
			media.appendChild(image);
			shell.appendChild(media);
		}

		const body = document.createElement("div");
		body.className = "preview-card-store-body";

		const meta = document.createElement("div");
		meta.className = "preview-card-store-meta";
		const source = document.createElement("span");
		source.className = "preview-card-store-source";
		source.textContent = info.label;
		meta.appendChild(source);
		const host = document.createElement("span");
		host.className = "preview-card-store-host";
		host.textContent = info.host;
		meta.appendChild(host);
		body.appendChild(meta);

		const title = document.createElement("div");
		title.className = "preview-card-store-title";
		title.textContent = info.title;
		body.appendChild(title);

		if (info.description) {
			const description = document.createElement("div");
			description.className = "preview-card-store-description";
			description.textContent = info.description;
			body.appendChild(description);
		}

		if (info.chips.length) {
			const chips = document.createElement("div");
			chips.className = "preview-card-store-chips";
			info.chips.forEach(function(chip) {
				appendGameStoreChip(chips, chip);
			});
			body.appendChild(chips);
		}

		const product = document.createElement("div");
		product.className = "preview-card-store-product";
		const productText = document.createElement("div");
		productText.className = "preview-card-store-product-text";
		const productName = document.createElement("div");
		productName.className = "preview-card-store-product-name";
		productName.textContent = info.title;
		productText.appendChild(productName);
		const productDetail = document.createElement("div");
		productDetail.className = "preview-card-store-product-detail";
		productDetail.textContent = info.detail;
		productText.appendChild(productDetail);
		product.appendChild(productText);

		const purchase = document.createElement("div");
		purchase.className = "preview-card-store-purchase";
		if (info.price || info.discount) {
			const price = document.createElement("div");
			price.className = "preview-card-store-price";
			if (info.discount) {
				const discount = document.createElement("span");
				discount.className = "preview-card-store-discount";
				discount.textContent = info.discount.charAt(0) === "-" ? info.discount : "-" + info.discount;
				price.appendChild(discount);
			}
			const priceStack = document.createElement("span");
			priceStack.className = "preview-card-store-price-stack";
			if (info.originalPrice && info.originalPrice !== info.price) {
				const original = document.createElement("span");
				original.className = "preview-card-store-price-original";
				original.textContent = info.originalPrice;
				priceStack.appendChild(original);
			}
			const finalPrice = document.createElement("span");
			finalPrice.className = "preview-card-store-price-final";
			finalPrice.textContent = info.price || "On store";
			priceStack.appendChild(finalPrice);
			price.appendChild(priceStack);
			purchase.appendChild(price);
		}
		const cart = document.createElement("span");
		cart.className = "preview-card-store-cart";
		cart.setAttribute("aria-hidden", "true");
		cart.appendChild(createPreviewCartIcon("preview-card-store-cart-icon"));
		const cartLabel = document.createElement("span");
		cartLabel.className = "preview-card-store-cart-label";
		cartLabel.textContent = /^free$/i.test(info.price) ? "Get" : (info.price ? "Cart" : "Open");
		cart.appendChild(cartLabel);
		purchase.appendChild(cart);
		product.appendChild(purchase);
		body.appendChild(product);

		const footer = document.createElement("div");
		footer.className = "preview-card-store-footer";
		const action = document.createElement("span");
		action.className = "preview-card-store-action";
		action.textContent = (preview && preview.openLabel) || ("Open on " + info.label);
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function previewSwedishSiteKind(preview, hostLabel) {
		const spec = previewProviderSpec(preview, hostLabel);
		return spec && (spec.provider === "tradera" || spec.provider === "blocket" || spec.provider === "flashback")
			? spec.provider
			: "";
	}

	function previewEscapeRegExp(value) {
		return String(value || "").replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	}

	function previewMetadataString(metadata, keys) {
		for (let index = 0; index < keys.length; index += 1) {
			const value = String((metadata && metadata[keys[index]]) || "").trim();
			if (value) {
				return value;
			}
		}
		return "";
	}

	function previewLabelValue(text, labels) {
		const normalized = String(text || "").replace(/\s+/g, " ").trim();
		if (!normalized) {
			return "";
		}
		for (let index = 0; index < labels.length; index += 1) {
			const pattern = new RegExp("(?:^|[.;]\\s*)" + previewEscapeRegExp(labels[index])
				+ "\\s*:\\s*([^.;]+)", "i");
			const match = pattern.exec(normalized);
			if (match && match[1]) {
				return match[1].trim();
			}
		}
		return "";
	}

	function previewCleanMarketplaceTitle(rawTitle, kind) {
		let title = String(rawTitle || "").replace(/\s+/g, " ").trim();
		title = title.replace(/\s*[|-]\s*(?:Blocket(?:\.se)?|Tradera)\s*$/i, "").trim();
		if (kind === "tradera") {
			title = title
				.replace(/^Se produkter som liknar\s+/i, "")
				.replace(/\s+p\u00e5\s+Tradera(?:\s+\([^)]+\))?$/i, "")
				.trim();
		}
		return title;
	}

	function previewCleanMarketplaceDescription(descriptionText, kind, title) {
		let description = String(descriptionText || "").replace(/\s+/g, " ").trim();
		if (!description) {
			return "";
		}

		const labels = kind === "tradera"
			? ["Utropspris", "K\u00f6p nu", "Pris", "Typ", "Slutar", "Skick"]
			: ["Pris"];
		labels.forEach(function(label) {
			const pattern = new RegExp("(^|[.;]\\s*)" + previewEscapeRegExp(label)
				+ "\\s*:\\s*[^.;]+[.;]?", "ig");
			description = description.replace(pattern, "$1");
		});
		description = description.replace(/\s*\.\s*/g, ". ").replace(/^\.\s*/, "").trim();
		if (description === "." || description.toLowerCase() === String(title || "").toLowerCase()) {
			return "";
		}
		return description;
	}

	function previewLastNumericPathSegment(preview, minDigits, maxDigits) {
		const url = previewUrlObject(preview && preview.url);
		if (!url) {
			return "";
		}
		const pattern = new RegExp("/([0-9]{" + String(minDigits) + "," + String(maxDigits) + "})(?=/|$)", "g");
		let match = null;
		let value = "";
		while ((match = pattern.exec(url.pathname)) !== null) {
			value = match[1];
		}
		return value;
	}

	function previewFlashbackThreadId(preview) {
		const metadata = (preview && preview.metadata) || {};
		const metadataId = String(metadata.threadId || "").trim();
		if (metadataId) {
			return metadataId;
		}
		const url = previewUrlObject(preview && preview.url);
		if (!url) {
			return "";
		}
		const match = /\/t([0-9]{3,14})(?:p[0-9]+)?(?:\/|$)/i.exec(url.pathname);
		return match ? match[1] : "";
	}

	function previewFlashbackPostId(preview) {
		const metadata = (preview && preview.metadata) || {};
		const metadataId = previewMetadataString(metadata, ["forumPostId", "forumLinkedPostId", "postId"]);
		if (metadataId) {
			return metadataId;
		}
		const url = previewUrlObject(preview && preview.url);
		if (!url) {
			return "";
		}
		const pathMatch = /\/s?p([0-9]{3,14})(?:\/|$)/i.exec(url.pathname);
		if (pathMatch) {
			return pathMatch[1];
		}
		const queryPost = String(url.searchParams.get("p") || "").trim();
		if (/^[0-9]{3,14}$/.test(queryPost)) {
			return queryPost;
		}
		const fragmentMatch = /^p?([0-9]{3,14})$/i.exec(String(url.hash || "").replace(/^#/, ""));
		return fragmentMatch ? fragmentMatch[1] : "";
	}

	function previewCleanFlashbackTitle(rawTitle) {
		let title = String(rawTitle || "").replace(/\s+/g, " ").trim();
		title = title
			.replace(/^Flashback Forum\s*-\s*Visa ett inl\u00e4gg\s*-\s*/i, "")
			.replace(/\s*-\s*Flashback Forum\s*$/i, "")
			.replace(/^\u00c4mne:\s*/i, "")
			.trim();
		return title;
	}

	function previewFlashbackDescriptionFallback(descriptionText, title, forumName, category) {
		let description = String(descriptionText || "").replace(/\s+/g, " ").trim();
		if (!description) {
			return "";
		}
		const lowerTitle = String(title || "").trim().toLowerCase();
		const lowerForum = String(forumName || "").trim().toLowerCase();
		const lowerCategory = String(category || "").trim().toLowerCase();
		const lowerDescription = description.toLowerCase();
		if (lowerDescription === lowerTitle || lowerDescription === lowerForum || lowerDescription === lowerCategory) {
			return "";
		}
		if (lowerTitle && lowerDescription.indexOf(lowerTitle) === 0) {
			description = description.slice(title.length).replace(/^[-:\s]+/, "").trim();
		}
		const lowerRemainder = description.toLowerCase();
		if (!description || lowerRemainder === lowerForum || lowerRemainder === lowerCategory) {
			return "";
		}
		return description;
	}

	function previewFlashbackInfo(preview, hostLabel, sourceLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const threadId = previewFlashbackThreadId(preview);
		const postId = previewFlashbackPostId(preview);
		const linkKind = previewMetadataString(metadata, ["forumLinkKind"]);
		const isPostLink = linkKind === "post" || !!postId;
		const title = previewCleanFlashbackTitle(
			previewMetadataString(metadata, ["forumThreadTitle"]) || (preview && preview.title) || "")
			|| previewCleanMarketplaceTitle((preview && preview.title) || "", "flashback")
			|| "Flashback thread";
		const category = previewMetadataString(metadata, ["forumCategory"]);
		const forumName = previewMetadataString(metadata, ["forumName"])
			|| previewFlashbackDescriptionFallback(descriptionText, title, "", category);
		const page = previewMetadataString(metadata, ["forumPage"]) || "1";
		const pageCount = previewMetadataString(metadata, ["forumPageCount"]);
		const postCount = previewMetadataString(metadata, ["forumPostCount"]);
		const author = previewMetadataString(metadata, ["forumPostAuthor", "forumFirstPostAuthor"]);
		const authorAvatarUrl = previewMetadataString(metadata,
			["forumPostAuthorAvatarUrl", "forumFirstPostAuthorAvatarUrl"]);
		const authorTitle = previewMetadataString(metadata, ["forumPostAuthorTitle", "forumFirstPostAuthorTitle"]);
		const authorRegistered = previewMetadataString(metadata,
			["forumPostAuthorRegistered", "forumFirstPostAuthorRegistered"]);
		const authorPosts = previewMetadataString(metadata, ["forumPostAuthorPosts", "forumFirstPostAuthorPosts"]);
		const postTime = previewMetadataString(metadata, ["forumPostTime", "forumFirstPostTime"]);
		const postNumber = previewMetadataString(metadata, ["forumPostNumber", "forumFirstPostNumber"])
			|| (isPostLink ? "" : "1");
		const excerpt = previewMetadataString(metadata, ["forumPostExcerpt", "forumFirstPostExcerpt"])
			|| previewFlashbackDescriptionFallback(descriptionText, title, forumName, category);
		const quoteAuthor = previewMetadataString(metadata, ["forumQuoteAuthor"]);
		const quoteExcerpt = previewMetadataString(metadata, ["forumQuoteExcerpt"]);
		const quotePostNumber = previewMetadataString(metadata, ["forumQuotePostNumber"]);
		const quotePostUrl = previewMetadataString(metadata, ["forumQuotePostUrl"]);
		return {
			threadId: threadId,
			postId: postId,
			isPostLink: isPostLink,
			title: title,
			category: category,
			forumName: forumName,
			pageLabel: pageCount ? ("Sidan " + page + " av " + pageCount) : (page ? "Sidan " + page : ""),
			postCountLabel: postCount ? (postCount + " inl\u00e4gg") : "",
			postLinkLabel: isPostLink && postNumber ? ("Svar #" + postNumber) : "",
			author: author,
			authorAvatarUrl: authorAvatarUrl,
			authorTitle: authorTitle,
			authorRegistered: authorRegistered,
			authorPosts: authorPosts,
			postTime: postTime,
			postNumber: postNumber,
			quoteAuthor: quoteAuthor,
			quoteExcerpt: quoteExcerpt,
			quotePostNumber: quotePostNumber,
			quotePostUrl: quotePostUrl,
			excerpt: excerpt,
			host: hostLabel || "flashback.org",
			source: sourceLabel || "Flashback"
		};
	}

	function previewSwedishMarketplaceInfo(preview, kind, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const rawTitle = previewMetadataString(metadata, ["listingTitle"]) || (preview && preview.title) || "";
		const title = previewCleanMarketplaceTitle(rawTitle, kind)
			|| (kind === "tradera" ? "Tradera listing" : "Blocket listing");
		const price = previewMetadataString(metadata, ["listingPrice"])
			|| previewLabelValue(descriptionText, ["Utropspris", "K\u00f6p nu", "Pris"]);
		const saleType = previewMetadataString(metadata, ["listingSaleType"])
			|| previewLabelValue(descriptionText, ["Typ"]);
		const endsAt = previewMetadataString(metadata, ["listingEndsAt"])
			|| previewLabelValue(descriptionText, ["Slutar"]);
		const condition = previewMetadataString(metadata, ["listingCondition"])
			|| previewLabelValue(descriptionText, ["Skick"]);
		const listingId = previewMetadataString(metadata, ["listingId"])
			|| previewLastNumericPathSegment(preview, kind === "tradera" ? 6 : 5, 14);
		return {
			brand: kind === "tradera" ? "Tradera" : "Blocket",
			mark: kind === "tradera" ? "T" : "B",
			badge: saleType || (kind === "tradera" ? "Auktion" : "Annons"),
			price: price,
			saleType: saleType,
			endsAt: endsAt,
			condition: condition,
			listingId: listingId,
			title: title,
			description: previewCleanMarketplaceDescription(descriptionText, kind, title)
		};
	}

	function appendSwedishPreviewChip(parent, value, extraClass) {
		const text = String(value || "").trim();
		if (!text) {
			return;
		}
		const chip = document.createElement("span");
		chip.className = "preview-card-sv-chip" + (extraClass ? " " + extraClass : "");
		chip.textContent = text;
		parent.appendChild(chip);
	}

	function appendSwedishMarketplacePreview(card, preview, kind, hostLabel, sourceLabel, descriptionText) {
		const info = previewSwedishMarketplaceInfo(preview, kind, descriptionText);
		const shell = document.createElement("div");
		shell.className = "preview-card-sv-market-shell";

		const media = document.createElement("div");
		media.className = "preview-card-sv-market-media";
		if (preview && preview.thumbnailUrl) {
			const image = document.createElement("img");
			image.className = "preview-card-sv-market-image";
			image.loading = "lazy";
			image.src = preview.thumbnailUrl;
			image.alt = info.title || sourceLabel || info.brand;
			media.appendChild(image);
		} else {
			const placeholder = document.createElement("div");
			placeholder.className = "preview-card-sv-market-placeholder";
			placeholder.textContent = info.mark;
			media.appendChild(placeholder);
		}
		const mediaBadge = document.createElement("span");
		mediaBadge.className = "preview-card-sv-market-media-badge";
		mediaBadge.textContent = info.brand;
		media.appendChild(mediaBadge);
		shell.appendChild(media);

		const body = document.createElement("div");
		body.className = "preview-card-sv-market-body";
		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = info.mark;
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = info.brand;
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = info.badge;
		top.appendChild(badge);
		body.appendChild(top);

		const title = document.createElement("div");
		title.className = "preview-card-sv-market-title";
		title.textContent = info.title;
		body.appendChild(title);

		if (info.price) {
			const price = document.createElement("div");
			price.className = "preview-card-sv-market-price";
			price.textContent = info.price;
			body.appendChild(price);
		}

		const chips = document.createElement("div");
		chips.className = "preview-card-sv-chips";
		appendSwedishPreviewChip(chips, info.endsAt ? "Slutar " + info.endsAt : "");
		appendSwedishPreviewChip(chips, info.condition);
		appendSwedishPreviewChip(chips, info.listingId ? "#" + info.listingId : "", "is-id");
		if (chips.childNodes.length) {
			body.appendChild(chips);
		}

		if (info.description) {
			const description = document.createElement("div");
			description.className = "preview-card-sv-description";
			description.textContent = info.description;
			body.appendChild(description);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || (kind === "tradera" ? "tradera.com" : "blocket.se");
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || (kind === "tradera" ? "Open on Tradera" : "Open on Blocket");
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function appendFlashbackPreview(card, preview, hostLabel, sourceLabel, descriptionText) {
		const info = previewFlashbackInfo(preview, hostLabel, sourceLabel, descriptionText);
		const shell = document.createElement("div");
		shell.className = "preview-card-flashback-shell";

		const brand = document.createElement("div");
		brand.className = "preview-card-flashback-brand";
		const logo = document.createElement("span");
		logo.className = "preview-card-flashback-logo";
		logo.setAttribute("role", "img");
		logo.setAttribute("aria-label", "Flashback");
		logo.title = "Flashback";
		brand.appendChild(logo);
		shell.appendChild(brand);

		const body = document.createElement("div");
		body.className = "preview-card-flashback-body";
		const titleNode = document.createElement("div");
		titleNode.className = "preview-card-flashback-title";
		titleNode.textContent = info.title;
		body.appendChild(titleNode);

		const contextText = [info.category, info.forumName].filter(Boolean).join(" / ");
		if (contextText) {
			const context = document.createElement("div");
			context.className = "preview-card-flashback-context";
			context.textContent = contextText;
			body.appendChild(context);
		}

		if (info.author || info.authorAvatarUrl) {
			const authorRow = document.createElement("div");
			authorRow.className = "preview-card-flashback-author-row";
			const avatar = document.createElement("span");
			avatar.className = "preview-card-flashback-author-avatar";
			styleAvatar(avatar, info.author || "Flashback", false, info.authorAvatarUrl);
			authorRow.appendChild(avatar);

			const authorCopy = document.createElement("span");
			authorCopy.className = "preview-card-flashback-author-copy";
			const authorName = document.createElement("span");
			authorName.className = "preview-card-flashback-author-name";
			authorName.textContent = info.author || "Flashback user";
			authorCopy.appendChild(authorName);
			const authorMetaText = [info.authorTitle, info.postTime, info.postNumber ? ("#" + info.postNumber) : ""]
				.filter(Boolean).join(" / ");
			if (authorMetaText) {
				const authorMeta = document.createElement("span");
				authorMeta.className = "preview-card-flashback-author-meta";
				authorMeta.textContent = authorMetaText;
				authorCopy.appendChild(authorMeta);
			}
			authorRow.appendChild(authorCopy);
			body.appendChild(authorRow);
		}

		const hasQuoteContext = !!(info.quoteExcerpt || info.quoteAuthor || info.quotePostNumber);
		const message = document.createElement("div");
		message.className = "preview-card-flashback-message" + (hasQuoteContext ? " has-quote" : "");
		if (hasQuoteContext) {
			const quote = document.createElement("div");
			quote.className = "preview-card-flashback-quote";
			const quoteHead = document.createElement("div");
			quoteHead.className = "preview-card-flashback-quote-head";
			const quoteParts = [];
			if (info.quotePostNumber) {
				quoteParts.push("#" + info.quotePostNumber);
			}
			if (info.quoteAuthor) {
				quoteParts.push(info.quoteAuthor);
			}
			quoteHead.textContent = quoteParts.length
				? ("Svarar p\u00e5 " + quoteParts.join(" / "))
				: "Svarar p\u00e5 tidigare inl\u00e4gg";
			quote.appendChild(quoteHead);
			if (info.quoteExcerpt) {
				const quoteText = document.createElement("div");
				quoteText.className = "preview-card-flashback-quote-text";
				quoteText.textContent = info.quoteExcerpt;
				quote.appendChild(quoteText);
			}
			message.appendChild(quote);

			if (info.excerpt) {
				const reply = document.createElement("div");
				reply.className = "preview-card-flashback-reply";
				reply.textContent = info.excerpt;
				message.appendChild(reply);
			}
		} else if (info.excerpt) {
			message.textContent = info.excerpt;
		} else {
			message.textContent = info.forumName || info.category || "Flashback forum thread";
		}
		body.appendChild(message);
		shell.appendChild(body);

		const footer = document.createElement("div");
		footer.className = "preview-card-flashback-footer";
		const meta = document.createElement("span");
		meta.className = "preview-card-flashback-meta";
		meta.textContent = info.host;
		footer.appendChild(meta);
		const action = document.createElement("span");
		action.className = "preview-card-flashback-action";
		action.textContent = info.isPostLink && (!preview || !preview.openLabel
			|| preview.openLabel === "Open on Flashback") ? "Open in thread"
			: ((preview && preview.openLabel) || "Open on Flashback");
		footer.appendChild(action);
		shell.appendChild(footer);

		card.appendChild(shell);
	}

	function previewMetadataList(metadata, key) {
		const value = metadata && metadata[key];
		return Array.isArray(value) ? value : [];
	}

	function previewSpecItems(preview, metadataKey) {
		const metadata = (preview && preview.metadata) || {};
		const seen = Object.create(null);
		const specs = [];
		previewMetadataList(metadata, metadataKey).forEach(function(item) {
			const label = String((item && item.label) || "").trim();
			const value = String((item && item.value) || "").trim();
			const key = label.toLowerCase();
			if (!label || !value || seen[key]) {
				return;
			}
			seen[key] = true;
			specs.push({ label: label, value: value });
		});
		return specs;
	}

	function previewListingSpecs(preview) {
		return previewSpecItems(preview, "listingSpecs");
	}

	function previewProductSpecs(preview) {
		return previewSpecItems(preview, "productSpecs");
	}

	function appendSwedishSpecGrid(parent, specs, className) {
		if (!Array.isArray(specs) || !specs.length) {
			return;
		}
		const grid = document.createElement("div");
		grid.className = className || "preview-card-sv-spec-grid";
		specs.slice(0, 10).forEach(function(spec) {
			const item = document.createElement("div");
			item.className = "preview-card-sv-spec";
			const label = document.createElement("span");
			label.className = "preview-card-sv-spec-label";
			label.textContent = spec.label;
			item.appendChild(label);
			const value = document.createElement("strong");
			value.className = "preview-card-sv-spec-value";
			value.textContent = spec.value;
			item.appendChild(value);
			grid.appendChild(item);
		});
		parent.appendChild(grid);
	}

	function appendBlocketListingPreview(card, preview, spec, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const title = previewMetadataString(metadata, ["listingTitle"])
			|| previewCleanMarketplaceTitle((preview && preview.title) || "", "blocket")
			|| "Blocket listing";
		const price = previewMetadataString(metadata, ["listingPrice", "productPrice"]);
		const description = previewMetadataString(metadata, ["listingDescription"])
			|| previewCleanMarketplaceDescription(descriptionText, "blocket", title);
		const location = previewMetadataString(metadata, ["listingLocation"]);
		const listingId = previewMetadataString(metadata, ["listingId"]) || previewLastNumericPathSegment(preview, 5, 14);
		const specs = previewListingSpecs(preview);
		const imageItems = previewImageMediaItems(preview, "", "");

		const shell = document.createElement("div");
		shell.className = "preview-card-blocket-shell";

		if (imageItems.length > 1) {
			appendPreviewImageCarousel(shell, preview, imageItems, "preview-card-blocket-gallery");
		} else if (imageItems.length === 1) {
			const media = document.createElement("div");
			media.className = "preview-card-blocket-media";
			const image = document.createElement("img");
			image.className = "preview-card-blocket-image";
			image.loading = "lazy";
			image.src = imageItems[0].url;
			image.alt = title;
			media.appendChild(image);
			shell.appendChild(media);
		}

		const body = document.createElement("div");
		body.className = "preview-card-blocket-body";
		const top = document.createElement("div");
		top.className = "preview-card-blocket-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = "B";
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = "Blocket";
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = "Annons";
		top.appendChild(badge);
		body.appendChild(top);

		const titleNode = document.createElement("div");
		titleNode.className = "preview-card-blocket-title";
		titleNode.textContent = title;
		body.appendChild(titleNode);

		if (price) {
			const priceNode = document.createElement("div");
			priceNode.className = "preview-card-blocket-price";
			priceNode.textContent = price;
			body.appendChild(priceNode);
		}

		if (description) {
			const descriptionNode = document.createElement("div");
			descriptionNode.className = "preview-card-blocket-description";
			descriptionNode.textContent = description;
			body.appendChild(descriptionNode);
		}

		appendSwedishSpecGrid(body, specs, "preview-card-blocket-spec-grid");

		const chips = document.createElement("div");
		chips.className = "preview-card-sv-chips";
		appendSwedishPreviewChip(chips, location);
		appendSwedishPreviewChip(chips, listingId ? "#" + listingId : "", "is-id");
		if (chips.childNodes.length) {
			body.appendChild(chips);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || "blocket.se";
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || "Open on Blocket";
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function previewProviderTitle(preview, spec) {
		const metadata = (preview && preview.metadata) || {};
		return previewMetadataString(metadata, ["listingTitle", "productTitle", "articleTitle", "audioTitle"])
			|| String((preview && preview.title) || "").trim()
			|| (spec && spec.label)
			|| "Link preview";
	}

	function previewProviderDescription(preview, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		return previewMetadataString(metadata, ["listingDescription", "productDescription", "articleDescription", "audioDescription"])
			|| String(descriptionText || "").trim();
	}

	function previewProviderPrice(metadata) {
		return previewMetadataString(metadata, ["listingPrice", "productPrice", "realEstatePrice"]);
	}

	function previewProviderChips(preview, spec, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const chips = [];
		const add = function(value) {
			const text = String(value || "").trim();
			if (text && chips.indexOf(text) < 0) {
				chips.push(text);
			}
		};
		if (spec.kind === "article") {
			add(previewMetadataString(metadata, ["articleSection"]));
			add(previewMetadataString(metadata, ["articlePublishedAt"]));
		} else if (spec.kind === "forum") {
			add(previewMetadataString(metadata, ["forumProvider"]));
			const threadId = previewMetadataString(metadata, ["forumThreadId", "threadId"]);
			add(threadId ? "Thread " + threadId : "");
		} else if (spec.kind === "realEstate") {
			add(previewProviderPrice(metadata));
			add(previewMetadataString(metadata, ["realEstateArea"]));
			add(previewMetadataString(metadata, ["realEstateRooms"]));
			add(previewMetadataString(metadata, ["realEstateFee"]));
		} else if (spec.kind === "product" || spec.kind === "systembolagetProduct") {
			add(previewProviderPrice(metadata));
			add(previewMetadataString(metadata, ["productAvailability"]));
			add(previewProductRatingLabel(previewMetadataString(metadata, ["productRating"])));
			add(previewProductReviewCountLabel(previewMetadataString(metadata, ["productReviewCount"])));
			add(previewMetadataString(metadata, ["productBrand"]));
			add(previewMetadataString(metadata, ["productSku"]));
			if (spec.kind === "systembolagetProduct") {
				add(previewMetadataString(metadata, ["productAlcohol"]));
				add(previewMetadataString(metadata, ["productVolume"]));
			}
		} else if (spec.kind === "audio") {
			add(previewMetadataString(metadata, ["audioProgram"]));
			add(previewMetadataString(metadata, ["articlePublishedAt"]));
		} else if (spec.kind === "finance") {
			add(previewMetadataString(metadata, ["tickerSymbol"]));
			add(previewMetadataString(metadata, ["statusLabel"]));
		} else {
			add(previewMetadataString(metadata, ["locationLabel"]));
			add(previewMetadataString(metadata, ["statusLabel"]) || descriptionText);
		}
		return chips.slice(0, 5);
	}

	function previewNeedsThumbnailBlur(preview) {
		const metadata = (preview && preview.metadata) || {};
		if (metadata.thumbnailBlur || metadata.contentWarning) {
			return true;
		}
		const text = [
			preview && preview.url,
			preview && preview.title,
			preview && preview.description
		].join(" ").toLowerCase();
		return /\b(nsfw|18\+|adult|vuxet|naken|nude|sex|gore)\b/i.test(text);
	}

	function appendExistenzPreview(card, preview, spec, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const shell = document.createElement("div");
		shell.className = "preview-card-existenz-shell";
		const imageUrl = String((preview && preview.thumbnailUrl) || "").trim();
		if (imageUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-existenz-media" + (previewNeedsThumbnailBlur(preview) ? " is-blurred" : "");
			const image = document.createElement("img");
			image.className = "preview-card-existenz-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = previewProviderTitle(preview, spec);
			media.appendChild(image);
			if (previewNeedsThumbnailBlur(preview)) {
				const warning = document.createElement("span");
				warning.className = "preview-card-existenz-warning";
				warning.textContent = String(metadata.contentWarning || "18+").trim();
				media.appendChild(warning);
			}
			shell.appendChild(media);
		}
		appendSwedishInfoBody(shell, preview, spec, hostLabel, descriptionText, "Dagens l\u00e4nk");
		card.appendChild(shell);
	}

	function appendSwedishInfoBody(parent, preview, spec, hostLabel, descriptionText, badgeText) {
		const metadata = (preview && preview.metadata) || {};
		const body = document.createElement("div");
		body.className = "preview-card-sv-info-body";
		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = spec.mark || String(spec.label || "S").slice(0, 2);
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = previewMetadataString(metadata, ["providerName"]) || spec.label;
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = badgeText || spec.kind;
		top.appendChild(badge);
		body.appendChild(top);

		const title = document.createElement("div");
		title.className = "preview-card-sv-info-title";
		title.textContent = previewProviderTitle(preview, spec);
		body.appendChild(title);

		const price = previewProviderPrice(metadata);
		if (price && (spec.kind === "product" || spec.kind === "systembolagetProduct" || spec.kind === "realEstate")) {
			const priceNode = document.createElement("div");
			priceNode.className = "preview-card-sv-info-price";
			priceNode.textContent = price;
			body.appendChild(priceNode);
		}

		const chips = document.createElement("div");
		chips.className = "preview-card-sv-chips";
		previewProviderChips(preview, spec, descriptionText).forEach(function(chip) {
			appendSwedishPreviewChip(chips, chip);
		});
		if (chips.childNodes.length) {
			body.appendChild(chips);
		}

		const description = String(descriptionText || "").trim();
		if (description) {
			const descriptionNode = document.createElement("div");
			descriptionNode.className = "preview-card-sv-description";
			descriptionNode.textContent = description;
			body.appendChild(descriptionNode);
		}

		if (spec.kind === "product" || spec.kind === "systembolagetProduct") {
			appendSwedishSpecGrid(body, previewProductSpecs(preview), "preview-card-sv-spec-grid");
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || spec.label;
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || ("Open on " + spec.label);
		footer.appendChild(action);
		body.appendChild(footer);

		parent.appendChild(body);
	}

	function appendSwedishInfoPreview(card, preview, spec, hostLabel, descriptionText) {
		const shell = document.createElement("div");
		shell.className = "preview-card-sv-info-shell";
		const imageItems = previewImageMediaItems(preview, "", "");
		const imageUrl = imageItems.length
			? String(imageItems[0].url || "").trim()
			: String((preview && preview.thumbnailUrl) || "").trim();
		if (imageUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-sv-info-media";
			const image = document.createElement("img");
			image.className = "preview-card-sv-info-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = previewProviderTitle(preview, spec);
			media.appendChild(image);
			shell.appendChild(media);
		}
		const badgeMap = {
			article: "Nyhet",
			forum: "Forum",
			realEstate: "Bostad",
			product: "Produkt",
			systembolagetProduct: "Produkt",
			audio: "Audio",
			finance: "Finans",
			weather: "V\u00e4der",
			place: "Plats",
			traffic: "Trafik"
		};
		appendSwedishInfoBody(
			shell,
			preview,
			spec,
			hostLabel,
			previewProviderDescription(preview, descriptionText),
			badgeMap[spec.kind] || "Link"
		);
		card.appendChild(shell);
	}

	function financePreviewPoints(metadata) {
		const rawPoints = Array.isArray(metadata && metadata.financeSparkline) ? metadata.financeSparkline : [];
		return rawPoints.map(function(item) {
			if (typeof item === "number") {
				return { close: item };
			}
			if (!item || typeof item !== "object") {
				return null;
			}
			const close = Number(item.close);
			if (!Number.isFinite(close)) {
				return null;
			}
			return {
				close: close,
				timestamp: Number(item.timestamp || 0)
			};
		}).filter(Boolean);
	}

	function financeTrendClass(value) {
		const text = String(value || "").trim().toLowerCase();
		if (text === "up" || text.charAt(0) === "+") {
			return " is-up";
		}
		if (text === "down" || text.charAt(0) === "-") {
			return " is-down";
		}
		return " is-flat";
	}

	function financeSparklinePath(points, width, height) {
		if (!points.length) {
			return "";
		}
		let min = points[0].close;
		let max = points[0].close;
		points.forEach(function(point) {
			min = Math.min(min, point.close);
			max = Math.max(max, point.close);
		});
		const span = Math.max(0.000001, max - min);
		return points.map(function(point, index) {
			const x = points.length === 1 ? width / 2 : (index / (points.length - 1)) * width;
			const y = height - ((point.close - min) / span) * height;
			return (index ? "L" : "M") + x.toFixed(1) + " " + y.toFixed(1);
		}).join(" ");
	}

	function appendFinanceStat(parent, label, value, trend) {
		const text = String(value || "").trim();
		if (!text) {
			return;
		}
		const stat = document.createElement("span");
		stat.className = "preview-card-finance-stat" + financeTrendClass(trend || text);
		const labelNode = document.createElement("span");
		labelNode.className = "preview-card-finance-stat-label";
		labelNode.textContent = label;
		stat.appendChild(labelNode);
		const valueNode = document.createElement("span");
		valueNode.className = "preview-card-finance-stat-value";
		valueNode.textContent = text;
		stat.appendChild(valueNode);
		parent.appendChild(stat);
	}

	function appendFinancePreview(card, preview, spec, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const symbol = previewMetadataString(metadata, ["tickerSymbol"]) || "Ticker";
		const name = previewMetadataString(metadata, ["financeName"]);
		const price = previewMetadataString(metadata, ["financePrice"]);
		const currency = previewMetadataString(metadata, ["financeCurrency"]);
		const dayChange = previewMetadataString(metadata, ["financeDayChange"]);
		const dayPercent = previewMetadataString(metadata, ["financeDayChangePercent"]);
		const rangeLabel = previewMetadataString(metadata, ["financeRangeLabel"]) || "1M";
		const rangeChange = previewMetadataString(metadata, ["financeRangeChange"]);
		const rangePercent = previewMetadataString(metadata, ["financeRangeChangePercent"]);
		const exchange = previewMetadataString(metadata, ["financeExchange"]);
		const instrument = previewMetadataString(metadata, ["financeInstrument"]);
		const points = financePreviewPoints(metadata);
		const trend = previewMetadataString(metadata, ["financeRangeTrend"])
			|| previewMetadataString(metadata, ["financeDayTrend"]);

		const shell = document.createElement("div");
		shell.className = "preview-card-finance-shell" + financeTrendClass(trend);

		const body = document.createElement("div");
		body.className = "preview-card-finance-body";

		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark preview-card-finance-mark";
		mark.textContent = spec.mark || "YF";
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = previewMetadataString(metadata, ["providerName"]) || spec.label || "Yahoo Finance";
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = "Finans";
		top.appendChild(badge);
		body.appendChild(top);

		const quoteRow = document.createElement("div");
		quoteRow.className = "preview-card-finance-quote";
		const title = document.createElement("div");
		title.className = "preview-card-finance-title";
		const symbolNode = document.createElement("span");
		symbolNode.className = "preview-card-finance-symbol";
		symbolNode.textContent = symbol;
		title.appendChild(symbolNode);
		if (name) {
			const nameNode = document.createElement("span");
			nameNode.className = "preview-card-finance-name";
			nameNode.textContent = name;
			title.appendChild(nameNode);
		}
		quoteRow.appendChild(title);
		if (price) {
			const priceNode = document.createElement("div");
			priceNode.className = "preview-card-finance-price";
			priceNode.textContent = price + (currency ? " " + currency : "");
			quoteRow.appendChild(priceNode);
		}
		body.appendChild(quoteRow);

		if (dayPercent || dayChange) {
			const day = document.createElement("div");
			day.className = "preview-card-finance-day" + financeTrendClass(previewMetadataString(metadata, ["financeDayTrend"]) || dayPercent || dayChange);
			day.textContent = ["1D", dayChange, dayPercent].filter(Boolean).join(" ");
			body.appendChild(day);
		}

		if (points.length >= 2) {
			const chart = document.createElement("div");
			chart.className = "preview-card-finance-chart";
			const chartLabels = document.createElement("div");
			chartLabels.className = "preview-card-finance-chart-labels";
			const chartRange = document.createElement("span");
			chartRange.className = "preview-card-finance-chart-range";
			chartRange.textContent = rangeLabel;
			chartLabels.appendChild(chartRange);
			if (price) {
				const chartValue = document.createElement("span");
				chartValue.className = "preview-card-finance-chart-value";
				chartValue.textContent = price + (currency ? " " + currency : "");
				chartLabels.appendChild(chartValue);
			}
			chart.appendChild(chartLabels);
			const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
			svg.setAttribute("viewBox", "0 0 240 76");
			svg.setAttribute("preserveAspectRatio", "none");
			svg.setAttribute("aria-hidden", "true");
			const base = document.createElementNS("http://www.w3.org/2000/svg", "path");
			base.setAttribute("class", "preview-card-finance-chart-base");
			base.setAttribute("d", "M0 75.5H240");
			svg.appendChild(base);
			const line = document.createElementNS("http://www.w3.org/2000/svg", "path");
			line.setAttribute("class", "preview-card-finance-chart-line");
			line.setAttribute("d", financeSparklinePath(points, 240, 68));
			svg.appendChild(line);
			chart.appendChild(svg);
			body.appendChild(chart);
		}

		const stats = document.createElement("div");
		stats.className = "preview-card-finance-stats";
		appendFinanceStat(stats, rangeLabel, [rangeChange, rangePercent].filter(Boolean).join(" "), previewMetadataString(metadata, ["financeRangeTrend"]));
		appendFinanceStat(stats, "1D", dayPercent || dayChange, previewMetadataString(metadata, ["financeDayTrend"]));
		appendSwedishPreviewChip(stats, exchange);
		appendSwedishPreviewChip(stats, instrument);
		if (stats.childNodes.length) {
			body.appendChild(stats);
		}

		const description = String(descriptionText || "").trim();
		if (description && !name) {
			const descriptionNode = document.createElement("div");
			descriptionNode.className = "preview-card-sv-description";
			descriptionNode.textContent = description;
			body.appendChild(descriptionNode);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || "finance.yahoo.com";
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || "Open on Yahoo Finance";
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function previewArticleDateLabel(value) {
		const text = String(value || "").replace(/\s+/g, " ").trim();
		if (!text) {
			return "";
		}
		const date = new Date(text);
		if (Number.isNaN(date.getTime())) {
			return text.replace(/\.\d+/, "").replace(/T/, " ").replace(/Z$/, "").slice(0, 16);
		}

		const now = new Date();
		const dayMs = 24 * 60 * 60 * 1000;
		const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
		const articleDay = new Date(date.getFullYear(), date.getMonth(), date.getDate());
		const diffDays = Math.round((today.getTime() - articleDay.getTime()) / dayMs);
		const time = new Intl.DateTimeFormat("sv-SE", {
			hour: "2-digit",
			minute: "2-digit"
		}).format(date);
		if (diffDays === 0) {
			return "Idag " + time;
		}
		if (diffDays === 1) {
			return "Ig\u00e5r " + time;
		}
		const options = date.getFullYear() === now.getFullYear()
			? { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }
			: { day: "numeric", month: "short", year: "numeric" };
		return new Intl.DateTimeFormat("sv-SE", options).format(date).replace(".", "");
	}

	function appendArticleMetaPill(parent, value, extraClass) {
		const text = String(value || "").trim();
		if (!text) {
			return;
		}
		const pill = document.createElement("span");
		pill.className = "preview-card-article-pill" + (extraClass ? " " + extraClass : "");
		pill.textContent = text;
		parent.appendChild(pill);
	}

	function appendArticlePreview(card, preview, spec, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const title = previewProviderTitle(preview, spec);
		const description = previewProviderDescription(preview, descriptionText);
		const section = previewMetadataString(metadata, ["articleSection"]);
		const author = previewMetadataString(metadata, ["articleAuthor"]);
		const publishedAt = previewArticleDateLabel(previewMetadataString(metadata, ["articlePublishedAt"]));
		const access = previewMetadataString(metadata, ["articleAccess"])
			|| (metadata.articlePremium ? "Premium" : "");
		const imageItems = previewImageMediaItems(preview, "", "");
		const imageUrl = imageItems.length
			? String(imageItems[0].url || "").trim()
			: String((preview && preview.thumbnailUrl) || "").trim();

		const shell = document.createElement("div");
		shell.className = "preview-card-article-shell" + (imageUrl ? " has-media" : "");

		if (imageUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-article-media";
			const image = document.createElement("img");
			image.className = "preview-card-article-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = title;
			media.appendChild(image);
			shell.appendChild(media);
		}

		const body = document.createElement("div");
		body.className = "preview-card-article-body";

		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = spec.mark || String(spec.label || "N").slice(0, 2);
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = previewMetadataString(metadata, ["providerName", "articlePublisher"]) || spec.label;
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = section || (spec.provider === "sweclockers" ? "Artikel" : "Nyhet");
		top.appendChild(badge);
		body.appendChild(top);

		const titleNode = document.createElement("div");
		titleNode.className = "preview-card-article-title";
		titleNode.textContent = title;
		body.appendChild(titleNode);

		if (description) {
			const descriptionNode = document.createElement("div");
			descriptionNode.className = "preview-card-article-description";
			descriptionNode.textContent = description;
			body.appendChild(descriptionNode);
		}

		const meta = document.createElement("div");
		meta.className = "preview-card-article-meta";
		appendArticleMetaPill(meta, publishedAt, "is-time");
		appendArticleMetaPill(meta, author, "is-author");
		appendArticleMetaPill(meta, access, "is-access");
		if (meta.childNodes.length) {
			body.appendChild(meta);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || spec.label;
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || ("Open on " + spec.label);
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function previewProductReviewCountLabel(value) {
		const text = String(value || "").replace(/\s+/g, " ").trim();
		if (!text) {
			return "";
		}
		if (/[A-Za-z\u00c0-\u024f]/.test(text)) {
			return text;
		}
		return text + " betyg";
	}

	function previewProductRatingLabel(value) {
		const text = String(value || "").replace(/\s+/g, " ").trim();
		if (!text) {
			return "";
		}
		const match = /^([0-9]+(?:[,.][0-9]+)?)(?:\s*\/\s*5)?$/i.exec(text);
		return match ? match[1].replace(".", ",") + "/5" : text;
	}

	function previewProductMediaItems(preview) {
		const metadata = (preview && preview.metadata) || {};
		const rawItems = Array.isArray(metadata.productMedia)
			? metadata.productMedia
			: (Array.isArray(metadata.productMediaItems) ? metadata.productMediaItems : []);
		const seen = Object.create(null);
		const items = [];
		const addItem = function(rawItem, fallbackKind) {
			const item = rawItem || {};
			const url = String(item.url || "").trim();
			if (!url || seen[url]) {
				return;
			}
			let mime = String(item.mime || "").trim().toLowerCase();
			let kind = String(item.kind || fallbackKind || "").trim().toLowerCase();
			const path = url.split("?")[0].toLowerCase();
			if (!mime) {
				if (path.endsWith(".m3u8")) {
					mime = "application/vnd.apple.mpegurl";
				} else if (path.endsWith(".webm")) {
					mime = "video/webm";
				} else if (path.endsWith(".mp4") || path.endsWith(".m4v")) {
					mime = "video/mp4";
				} else if (path.endsWith(".png")) {
					mime = "image/png";
				} else if (path.endsWith(".webp")) {
					mime = "image/webp";
				} else if (path.endsWith(".gif")) {
					mime = "image/gif";
				} else {
					mime = kind === "video" ? "video/mp4" : "image/jpeg";
				}
			}
			if (!kind) {
				kind = /^video\//i.test(mime) || /mpegurl|dash\+xml/i.test(mime) ? "video" : "image";
			}
			if (kind !== "image" && kind !== "video") {
				return;
			}
			seen[url] = true;
			items.push({
				kind: kind,
				url: url,
				mime: mime,
				title: String(item.title || "").trim(),
				thumbnail: String(item.thumbnail || item.poster || "").trim(),
				poster: String(item.poster || item.thumbnail || "").trim(),
				streamKind: String(item.streamKind || (/mpegurl/i.test(mime) ? "hls" : "")).trim().toLowerCase()
			});
		};

		rawItems.forEach(function(item) {
			addItem(item);
		});
		if (Array.isArray(preview && preview.mediaItems)) {
			preview.mediaItems.forEach(function(item) {
				addItem(item);
			});
		}
		if (Array.isArray(metadata.productImages)) {
			metadata.productImages.forEach(function(item) {
				addItem(item, "image");
			});
		}
		if (!items.length && metadata.productImage) {
			addItem({ url: metadata.productImage, mime: "image/jpeg", kind: "image" });
		}
		if (!items.length && preview && preview.thumbnailUrl) {
			addItem({ url: preview.thumbnailUrl, mime: "image/jpeg", kind: "image" });
		}
		return items.slice(0, 12);
	}

	function appendProductMediaGallery(card, shell, preview, mediaItems) {
		const gallery = document.createElement("div");
		gallery.className = "preview-card-product-gallery";
		gallery.tabIndex = 0;
		gallery.addEventListener("click", function(event) {
			event.stopPropagation();
		});

		const stage = document.createElement("div");
		stage.className = "preview-card-product-stage";
		gallery.appendChild(stage);

		const previousButton = document.createElement("button");
		previousButton.type = "button";
		previousButton.className = "preview-card-carousel-button preview-card-carousel-previous preview-card-product-gallery-previous";
		previousButton.textContent = "<";
		previousButton.setAttribute("aria-label", "Previous product media");
		stage.appendChild(previousButton);

		const nextButton = document.createElement("button");
		nextButton.type = "button";
		nextButton.className = "preview-card-carousel-button preview-card-carousel-next preview-card-product-gallery-next";
		nextButton.textContent = ">";
		nextButton.setAttribute("aria-label", "Next product media");
		stage.appendChild(nextButton);

		const rail = document.createElement("div");
		rail.className = "preview-card-product-thumbnails";
		const thumbButtons = mediaItems.map(function(item, itemIndex) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "preview-card-product-thumbnail";
			button.setAttribute("aria-label", (item.kind === "video" ? "Show video " : "Show image ")
				+ String(itemIndex + 1));
			const imageUrl = item.thumbnail || item.poster || (item.kind === "image" ? item.url : "");
			if (imageUrl) {
				const image = document.createElement("img");
				image.className = "preview-card-product-thumbnail-image";
				image.loading = "lazy";
				image.src = imageUrl;
				image.alt = item.title || (item.kind === "video" ? "Product video" : "Product image");
				if (item.url && item.url !== imageUrl) {
					image.dataset.fallbackSrc = item.url;
				}
				image.addEventListener("error", function() {
					const fallbackSrc = image.dataset.fallbackSrc || "";
					if (fallbackSrc && image.src !== fallbackSrc) {
						image.dataset.fallbackSrc = "";
						image.src = fallbackSrc;
						return;
					}
					image.hidden = true;
					if (!button.querySelector(".preview-card-product-thumbnail-placeholder")) {
						const placeholder = document.createElement("span");
						placeholder.className = "preview-card-product-thumbnail-placeholder";
						placeholder.textContent = item.kind === "video" ? "VID" : "IMG";
						button.appendChild(placeholder);
					}
				});
				button.appendChild(image);
			} else {
				const placeholder = document.createElement("span");
				placeholder.className = "preview-card-product-thumbnail-placeholder";
				placeholder.textContent = item.kind === "video" ? "VID" : "IMG";
				button.appendChild(placeholder);
			}
			if (item.kind === "video") {
				const playMark = document.createElement("span");
				playMark.className = "preview-card-product-thumbnail-play";
				playMark.textContent = "Play";
				button.appendChild(playMark);
			}
			button.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				setIndex(itemIndex);
			});
			rail.appendChild(button);
			return button;
		});
		gallery.appendChild(rail);

		if (mediaItems.length <= 1) {
			previousButton.hidden = true;
			nextButton.hidden = true;
			rail.hidden = true;
		}

		let index = 0;
		const renderItem = function() {
			const activeVideo = stage.querySelector("video");
			if (activeVideo) {
				activeVideo.pause();
			}
			Array.prototype.slice.call(stage.children).forEach(function(child) {
				if (child !== previousButton && child !== nextButton) {
					stage.removeChild(child);
				}
			});

			const item = mediaItems[index];
			stage.classList.toggle("is-video", item.kind === "video");
			stage.classList.toggle("is-image", item.kind !== "video");
			if (item.kind === "video") {
				const mediaPreview = Object.assign({}, preview || {}, {
					title: item.title || (preview && preview.title) || "Product video",
					thumbnailUrl: item.poster || item.thumbnail || (preview && preview.thumbnailUrl) || "",
					autoplay: false,
					startMuted: true
				});
				const playable = item.streamKind === "hls"
					? createSteamHlsPlayableMedia(card, mediaPreview, item, "preview-card-product-stage-player")
					: createPreviewPlayableMedia(card, mediaPreview, item.url, item.mime, "", "", true, false,
						previewInlineMediaEnabled() && previewVideoCanPlayInline(item.mime, item.url),
						"preview-card-product-stage-player");
				stage.insertBefore(playable, previousButton);
			} else {
				const image = document.createElement("img");
				image.className = "preview-card-product-stage-image";
				image.loading = "lazy";
				image.src = item.url;
				image.alt = item.title || (preview && preview.title) || "Product image";
				const fallbackImage = item.thumbnail || item.poster || "";
				if (fallbackImage && fallbackImage !== item.url) {
					image.dataset.fallbackSrc = fallbackImage;
				}
				image.addEventListener("error", function() {
					const fallbackSrc = image.dataset.fallbackSrc || "";
					if (fallbackSrc && image.src !== fallbackSrc) {
						image.dataset.fallbackSrc = "";
						image.src = fallbackSrc;
					}
				});
				image.addEventListener("load", function() {
					requestAnimationFrame(syncScrollState);
				}, { once: true });
				stage.insertBefore(image, previousButton);
			}

			thumbButtons.forEach(function(button, buttonIndex) {
				const active = buttonIndex === index;
				button.classList.toggle("is-active", active);
				button.setAttribute("aria-current", active ? "true" : "false");
				if (active && typeof button.scrollIntoView === "function") {
					button.scrollIntoView({ behavior: "auto", block: "nearest", inline: "nearest" });
				}
			});
			requestAnimationFrame(syncScrollState);
		};
		const setIndex = function(nextIndex) {
			index = (nextIndex + mediaItems.length) % mediaItems.length;
			renderItem();
		};
		const step = function(delta, event) {
			if (event) {
				event.preventDefault();
				event.stopPropagation();
			}
			setIndex(index + delta);
		};

		previousButton.addEventListener("click", function(event) {
			step(-1, event);
		});
		nextButton.addEventListener("click", function(event) {
			step(1, event);
		});
		gallery.addEventListener("keydown", function(event) {
			if (event.key === "ArrowLeft") {
				step(-1, event);
			} else if (event.key === "ArrowRight") {
				step(1, event);
			}
		});

		renderItem();
		shell.appendChild(gallery);
		return gallery;
	}

	function appendSwedishProductPreview(card, preview, spec, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const provider = previewMetadataString(metadata, ["providerName", "productProvider"]) || spec.label;
		const title = previewMetadataString(metadata, ["productTitle"])
			|| previewProviderTitle(preview, spec);
		const description = previewProviderDescription(preview, descriptionText);
		const price = previewMetadataString(metadata, ["productPrice"]);
		const originalPrice = previewMetadataString(metadata, ["productOriginalPrice", "listingOriginalPrice"]);
		const discount = previewMetadataString(metadata, ["productDiscount", "listingDiscount"]);
		const availability = previewMetadataString(metadata, ["productAvailability"]);
		const delivery = previewMetadataString(metadata, ["productDelivery"]);
		const rating = previewProductRatingLabel(previewMetadataString(metadata, ["productRating"]));
		const reviewCount = previewProductReviewCountLabel(previewMetadataString(metadata, ["productReviewCount"]));
		const brand = previewMetadataString(metadata, ["productBrand"]);
		const sku = previewMetadataString(metadata, ["productSku", "productId"]);
		const detail = [availability, delivery, brand, sku ? "SKU " + sku : ""].filter(Boolean).join(" / ") || provider;
		const mediaItems = previewProductMediaItems(preview);
		const specs = previewProductSpecs(preview);

		const shell = document.createElement("div");
		shell.className = "preview-card-product-shell" + (mediaItems.length === 1 ? " is-single-media" : "");
		if (mediaItems.length) {
			appendProductMediaGallery(card, shell, preview, mediaItems);
		}

		const body = document.createElement("div");
		body.className = "preview-card-product-body";

		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = spec.mark || String(provider || "P").slice(0, 2);
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = provider;
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = spec.kind === "systembolagetProduct" ? "Systembolaget" : "Produkt";
		top.appendChild(badge);
		body.appendChild(top);

		const titleNode = document.createElement("div");
		titleNode.className = "preview-card-product-title";
		titleNode.textContent = title || provider;
		body.appendChild(titleNode);

		if (description) {
			const descriptionNode = document.createElement("div");
			descriptionNode.className = "preview-card-product-description";
			descriptionNode.textContent = description;
			body.appendChild(descriptionNode);
		}

		const chips = document.createElement("div");
		chips.className = "preview-card-product-chips";
		[rating, reviewCount, availability, delivery, brand].forEach(function(chip) {
			appendGameStoreChip(chips, chip);
		});
		if (chips.childNodes.length) {
			body.appendChild(chips);
		}

		const product = document.createElement("div");
		product.className = "preview-card-product-commerce";
		const productText = document.createElement("div");
		productText.className = "preview-card-product-commerce-text";
		const productName = document.createElement("div");
		productName.className = "preview-card-product-commerce-name";
		productName.textContent = title || provider;
		productText.appendChild(productName);
		const productDetail = document.createElement("div");
		productDetail.className = "preview-card-product-commerce-detail";
		productDetail.textContent = detail;
		productText.appendChild(productDetail);
		product.appendChild(productText);

		const purchase = document.createElement("div");
		purchase.className = "preview-card-product-purchase";
		if (price || discount || availability) {
			const priceNode = document.createElement("div");
			priceNode.className = "preview-card-product-price";
			if (discount) {
				const discountNode = document.createElement("span");
				discountNode.className = "preview-card-product-discount";
				discountNode.textContent = discount.charAt(0) === "-" ? discount : "-" + discount;
				priceNode.appendChild(discountNode);
			}
			const priceStack = document.createElement("span");
			priceStack.className = "preview-card-product-price-stack";
			if (originalPrice && originalPrice !== price) {
				const original = document.createElement("span");
				original.className = "preview-card-product-price-original";
				original.textContent = originalPrice;
				priceStack.appendChild(original);
			}
			const finalPrice = document.createElement("span");
			finalPrice.className = "preview-card-product-price-final";
			finalPrice.textContent = price || availability || "Open";
			priceStack.appendChild(finalPrice);
			priceNode.appendChild(priceStack);
			purchase.appendChild(priceNode);
		}
		const cart = document.createElement("span");
		cart.className = "preview-card-product-cart";
		cart.setAttribute("aria-hidden", "true");
		cart.appendChild(createPreviewCartIcon("preview-card-product-cart-icon"));
		const cartLabel = document.createElement("span");
		cartLabel.className = "preview-card-product-cart-label";
		cartLabel.textContent = price ? "Cart" : "Open";
		cart.appendChild(cartLabel);
		purchase.appendChild(cart);
		product.appendChild(purchase);
		body.appendChild(product);

		appendSwedishSpecGrid(body, specs, "preview-card-product-spec-grid");

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || provider;
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || ("Open on " + provider);
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function appendAmazonProductPreview(card, preview, spec, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		const title = previewMetadataString(metadata, ["productTitle"])
			|| previewProviderTitle(preview, spec);
		const price = previewMetadataString(metadata, ["productPrice"]);
		const delivery = previewMetadataString(metadata, ["productDelivery"]);
		const rating = previewProductRatingLabel(previewMetadataString(metadata, ["productRating"]));
		const reviewCount = previewProductReviewCountLabel(previewMetadataString(metadata, ["productReviewCount"]));
		const availability = previewMetadataString(metadata, ["productAvailability"]);
		const brand = previewMetadataString(metadata, ["productBrand"]);
		const imageItems = previewImageMediaItems(preview, "", "");
		const imageUrl = imageItems.length ? String(imageItems[0].url || "").trim()
			: String((preview && preview.thumbnailUrl) || "").trim();

		const shell = document.createElement("div");
		shell.className = "preview-card-amazon-shell";

		const media = document.createElement("div");
		media.className = "preview-card-amazon-media";
		if (imageUrl) {
			const image = document.createElement("img");
			image.className = "preview-card-amazon-image";
			image.loading = "lazy";
			image.src = imageUrl;
			image.alt = title || "Amazon product";
			media.appendChild(image);
		} else {
			const placeholder = document.createElement("div");
			placeholder.className = "preview-card-amazon-placeholder";
			placeholder.textContent = "A";
			media.appendChild(placeholder);
		}
		shell.appendChild(media);

		const body = document.createElement("div");
		body.className = "preview-card-amazon-body";

		const top = document.createElement("div");
		top.className = "preview-card-sv-top";
		const identity = document.createElement("div");
		identity.className = "preview-card-sv-identity";
		const mark = document.createElement("span");
		mark.className = "preview-card-sv-mark";
		mark.textContent = "A";
		identity.appendChild(mark);
		const source = document.createElement("span");
		source.className = "preview-card-sv-source";
		source.textContent = previewMetadataString(metadata, ["providerName"]) || "Amazon";
		identity.appendChild(source);
		top.appendChild(identity);
		const badge = document.createElement("span");
		badge.className = "preview-card-sv-badge";
		badge.textContent = "Produkt";
		top.appendChild(badge);
		body.appendChild(top);

		const titleNode = document.createElement("div");
		titleNode.className = "preview-card-amazon-title";
		titleNode.textContent = title || "Amazon product";
		body.appendChild(titleNode);

		if (price || delivery) {
			const commerce = document.createElement("div");
			commerce.className = "preview-card-amazon-commerce";
			if (price) {
				const priceNode = document.createElement("div");
				priceNode.className = "preview-card-amazon-price";
				priceNode.textContent = price;
				commerce.appendChild(priceNode);
			}
			if (delivery) {
				const deliveryNode = document.createElement("div");
				deliveryNode.className = "preview-card-amazon-delivery";
				deliveryNode.textContent = delivery;
				commerce.appendChild(deliveryNode);
			}
			body.appendChild(commerce);
		}

		if (rating || reviewCount) {
			const stats = document.createElement("div");
			stats.className = "preview-card-amazon-stats";
			if (rating) {
				const ratingNode = document.createElement("span");
				ratingNode.className = "preview-card-amazon-rating";
				ratingNode.textContent = rating;
				stats.appendChild(ratingNode);
			}
			if (reviewCount) {
				const reviewsNode = document.createElement("span");
				reviewsNode.className = "preview-card-amazon-reviews";
				reviewsNode.textContent = reviewCount;
				stats.appendChild(reviewsNode);
			}
			body.appendChild(stats);
		}

		const chips = document.createElement("div");
		chips.className = "preview-card-sv-chips";
		appendSwedishPreviewChip(chips, availability);
		appendSwedishPreviewChip(chips, brand);
		if (chips.childNodes.length) {
			body.appendChild(chips);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-sv-footer";
		const host = document.createElement("span");
		host.className = "preview-card-sv-host";
		host.textContent = hostLabel || "amazon.se";
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-sv-action";
		action.textContent = (preview && preview.openLabel) || "Open on Amazon";
		footer.appendChild(action);
		body.appendChild(footer);

		shell.appendChild(body);
		card.appendChild(shell);
	}

	function appendRichProviderPreview(card, preview, spec, hostLabel, sourceLabel, descriptionText) {
		if (!spec) {
			return false;
		}
		if (spec.kind === "finance") {
			appendFinancePreview(card, preview, spec, hostLabel, descriptionText);
			return true;
		}
		if (spec.kind === "article") {
			appendArticlePreview(card, preview, spec, hostLabel, descriptionText);
			return true;
		}
		if (spec.provider === "amazon") {
			appendAmazonProductPreview(card, preview, spec, hostLabel);
			return true;
		}
		if (spec.kind === "product" || spec.kind === "systembolagetProduct") {
			appendSwedishProductPreview(card, preview, spec, hostLabel, descriptionText);
			return true;
		}
		if (spec.provider === "blocket") {
			appendBlocketListingPreview(card, preview, spec, hostLabel, descriptionText);
			return true;
		}
		if (spec.provider === "flashback") {
			appendFlashbackPreview(card, preview, hostLabel, sourceLabel, descriptionText);
			return true;
		}
		if (spec.provider === "existenz") {
			appendExistenzPreview(card, preview, spec, hostLabel, descriptionText);
			return true;
		}
		if (spec.kind === "marketplaceListing") {
			appendSwedishMarketplacePreview(card, preview, spec.provider, hostLabel, sourceLabel, descriptionText);
			return true;
		}
		appendSwedishInfoPreview(card, preview, spec, hostLabel, descriptionText);
		return true;
	}

	function previewIsGitHub(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		if (String(metadata.provider || "").trim().toLowerCase() === "github") {
			return true;
		}
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		if (host !== "github.com" || !url) {
			return false;
		}
		const segments = url.pathname.split("/").filter(Boolean);
		return segments.length >= 2;
	}

	function previewCompactCount(value) {
		const count = Number(value);
		if (!Number.isFinite(count) || count <= 0) {
			return "";
		}
		if (count >= 1000000) {
			return (count / 1000000).toFixed(count >= 10000000 ? 0 : 1).replace(/\.0$/, "") + "M";
		}
		if (count >= 1000) {
			return (count / 1000).toFixed(count >= 10000 ? 0 : 1).replace(/\.0$/, "") + "K";
		}
		return String(Math.round(count));
	}

	function previewDateLabel(value) {
		const raw = String(value || "").trim();
		if (!raw) {
			return "";
		}
		const date = new Date(raw);
		if (!Number.isFinite(date.getTime())) {
			return "";
		}
		return new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric", year: "numeric" }).format(date);
	}

	function appendGitHubMetaPill(parent, label, value, extraClass) {
		const text = String(value || "").trim();
		if (!text) {
			return;
		}
		const pill = document.createElement("span");
		pill.className = "preview-card-github-pill" + (extraClass ? " " + extraClass : "");
		pill.textContent = label ? label + " " + text : text;
		parent.appendChild(pill);
	}

	function appendGitHubLink(parent, label, url, primary) {
		const targetUrl = String(url || "").trim();
		if (!targetUrl) {
			return;
		}
		const button = document.createElement("button");
		button.type = "button";
		button.className = "preview-card-github-link" + (primary ? " is-primary" : "");
		button.textContent = label;
		button.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			notifyBridge("activateLink", targetUrl);
		});
		parent.appendChild(button);
	}

	function appendGitHubPreview(card, preview, hostLabel, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const fullName = String(metadata.githubFullName || preview.title || "").trim();
		const owner = String(metadata.githubOwner || "").trim();
		const repo = String(metadata.githubRepo || "").trim();
		const repoName = repo || (fullName.indexOf("/") >= 0 ? fullName.split("/").pop() : fullName) || "Repository";
		const ownerName = owner || (fullName.indexOf("/") >= 0 ? fullName.split("/")[0] : "GitHub");
		const description = String(metadata.githubDescription || descriptionText || "").trim();

		const shell = document.createElement("div");
		shell.className = "preview-card-github-shell";

		const header = document.createElement("div");
		header.className = "preview-card-github-header";
		const mark = document.createElement("div");
		mark.className = "preview-card-github-mark";
		const avatarUrl = String(metadata.githubOwnerAvatarUrl || "").trim();
		if (avatarUrl) {
			const avatar = document.createElement("img");
			avatar.className = "preview-card-github-avatar";
			avatar.loading = "lazy";
			avatar.src = avatarUrl;
			avatar.alt = ownerName;
			mark.appendChild(avatar);
		} else {
			mark.textContent = "GH";
		}
		header.appendChild(mark);

		const heading = document.createElement("div");
		heading.className = "preview-card-github-heading";
		const source = document.createElement("div");
		source.className = "preview-card-github-source";
		source.textContent = "GitHub";
		heading.appendChild(source);
		const path = document.createElement("div");
		path.className = "preview-card-github-path";
		const ownerNode = document.createElement("span");
		ownerNode.className = "preview-card-github-owner";
		ownerNode.textContent = ownerName;
		path.appendChild(ownerNode);
		const slash = document.createElement("span");
		slash.className = "preview-card-github-slash";
		slash.textContent = "/";
		path.appendChild(slash);
		const repoNode = document.createElement("span");
		repoNode.className = "preview-card-github-repo";
		repoNode.textContent = repoName;
		path.appendChild(repoNode);
		heading.appendChild(path);
		header.appendChild(heading);

		const badges = document.createElement("div");
		badges.className = "preview-card-github-badges";
		if (metadata.githubPrivate) {
			appendGitHubMetaPill(badges, "", "Private", "is-state");
		}
		if (metadata.githubArchived) {
			appendGitHubMetaPill(badges, "", "Archived", "is-state");
		}
		if (metadata.githubFork) {
			appendGitHubMetaPill(badges, "", "Fork", "is-state");
		}
		if (!badges.childNodes.length) {
			appendGitHubMetaPill(badges, "", "Repository", "is-state");
		}
		header.appendChild(badges);
		shell.appendChild(header);

		if (description) {
			const copy = document.createElement("div");
			copy.className = "preview-card-github-description";
			copy.textContent = description;
			shell.appendChild(copy);
		}

		const stats = document.createElement("div");
		stats.className = "preview-card-github-stats";
		appendGitHubMetaPill(stats, "", metadata.githubLanguage);
		appendGitHubMetaPill(stats, "Stars", previewCompactCount(metadata.githubStars));
		appendGitHubMetaPill(stats, "Forks", previewCompactCount(metadata.githubForks));
		appendGitHubMetaPill(stats, "Issues", previewCompactCount(metadata.githubOpenIssues));
		appendGitHubMetaPill(stats, "", metadata.githubLicense);
		if (Array.isArray(metadata.githubTopics)) {
			metadata.githubTopics.slice(0, 3).forEach(function(topic) {
				appendGitHubMetaPill(stats, "#", topic, "is-topic");
			});
		}
		if (stats.childNodes.length) {
			shell.appendChild(stats);
		}

		const release = document.createElement("div");
		release.className = "preview-card-github-release";
		const releaseTop = document.createElement("div");
		releaseTop.className = "preview-card-github-release-top";
		const releaseLabel = document.createElement("span");
		releaseLabel.className = "preview-card-github-release-label";
		releaseLabel.textContent = "Latest release";
		releaseTop.appendChild(releaseLabel);
		const releaseDate = previewDateLabel(metadata.githubLatestReleasePublishedAt);
		if (releaseDate) {
			const date = document.createElement("span");
			date.className = "preview-card-github-release-date";
			date.textContent = releaseDate;
			releaseTop.appendChild(date);
		}
		release.appendChild(releaseTop);

		if (metadata.githubLatestReleaseLoading) {
			const loading = document.createElement("div");
			loading.className = "preview-card-github-release-title";
			loading.textContent = "Checking latest release...";
			release.appendChild(loading);
		} else if (metadata.githubLatestReleaseTag || metadata.githubLatestReleaseName) {
			const title = document.createElement("div");
			title.className = "preview-card-github-release-title";
			title.textContent = String(metadata.githubLatestReleaseName || metadata.githubLatestReleaseTag || "").trim();
			release.appendChild(title);
			if (metadata.githubLatestReleaseTag && metadata.githubLatestReleaseName
				&& metadata.githubLatestReleaseTag !== metadata.githubLatestReleaseName) {
				const tag = document.createElement("div");
				tag.className = "preview-card-github-release-tag";
				tag.textContent = String(metadata.githubLatestReleaseTag);
				release.appendChild(tag);
			}
			if (metadata.githubLatestReleaseNotes) {
				const notes = document.createElement("div");
				notes.className = "preview-card-github-release-notes";
				notes.textContent = String(metadata.githubLatestReleaseNotes);
				release.appendChild(notes);
			}
			const releaseStats = document.createElement("div");
			releaseStats.className = "preview-card-github-release-stats";
			appendGitHubMetaPill(releaseStats, "Assets", previewCompactCount(metadata.githubLatestReleaseAssetCount));
			appendGitHubMetaPill(releaseStats, "Downloads", previewCompactCount(metadata.githubLatestReleaseDownloadCount));
			if (releaseStats.childNodes.length) {
				release.appendChild(releaseStats);
			}
			const releaseLinks = document.createElement("div");
			releaseLinks.className = "preview-card-github-links";
			appendGitHubLink(releaseLinks, "Open release", metadata.githubLatestReleaseUrl, true);
			appendGitHubLink(releaseLinks, "Download " + String(metadata.githubLatestReleaseAssetName || "asset"),
				metadata.githubLatestReleaseAssetUrl, false);
			if (releaseLinks.childNodes.length) {
				release.appendChild(releaseLinks);
			}
		} else {
			const missing = document.createElement("div");
			missing.className = "preview-card-github-release-title";
			missing.textContent = "No public release found";
			release.appendChild(missing);
		}
		shell.appendChild(release);

		const footer = document.createElement("div");
		footer.className = "preview-card-github-footer";
		const host = document.createElement("span");
		host.className = "preview-card-github-host";
		host.textContent = hostLabel || "github.com";
		footer.appendChild(host);
		const action = document.createElement("span");
		action.className = "preview-card-github-action";
		action.textContent = (preview && preview.openLabel) || "Open on GitHub";
		footer.appendChild(action);
		shell.appendChild(footer);

		card.appendChild(shell);
	}

	function previewIsInstagramPost(preview, hostLabel) {
		const metadata = (preview && preview.metadata) || {};
		if (String(metadata.provider || "").trim().toLowerCase() === "instagram"
			|| String(metadata.previewProvider || "").trim().toLowerCase() === "instagram") {
			return true;
		}
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		if (host !== "instagram.com" && host !== "instagr.am") {
			return false;
		}
		const segments = url ? url.pathname.split("/").filter(Boolean).map(function(segment) {
			try {
				return decodeURIComponent(segment);
			} catch (error) {
				return segment;
			}
		}) : [];
		const first = String(segments[0] || "").toLowerCase();
		const second = String(segments[1] || "").toLowerCase();
		return (first === "p" || first === "reel" || first === "reels" || first === "tv")
			|| (segments.length >= 3 && second && /^(p|reel|reels|tv)$/.test(second));
	}

	function previewInstagramHandle(preview) {
		const metadata = (preview && preview.metadata) || {};
		const metadataHandle = String(metadata.instagramHandle || "").trim();
		if (metadataHandle) {
			return metadataHandle.charAt(0) === "@" ? metadataHandle : "@" + metadataHandle.replace(/^@+/, "");
		}
		const url = previewUrlObject(preview && preview.url);
		if (!url) {
			return "";
		}
		const segments = url.pathname.split("/").filter(Boolean).map(function(segment) {
			try {
				return decodeURIComponent(segment);
			} catch (error) {
				return segment;
			}
		});
		if (segments.length >= 3 && /^(p|reel|reels|tv)$/i.test(String(segments[1] || ""))) {
			return "@" + String(segments[0] || "").replace(/^@+/, "");
		}
		return "";
	}

	function previewInstagramDisplayName(preview, handle) {
		const metadata = (preview && preview.metadata) || {};
		const metadataName = String(metadata.instagramDisplayName || "").trim();
		if (metadataName) {
			return metadataName;
		}
		const subtitle = String((preview && preview.subtitle) || "").trim();
		if (subtitle && !/^instagram$/i.test(subtitle)
			&& subtitle.toLowerCase() !== String(handle || "").toLowerCase()) {
			return subtitle;
		}
		return "";
	}

	function previewInstagramCaption(preview, descriptionText) {
		const metadata = (preview && preview.metadata) || {};
		const caption = String(metadata.instagramCaption || "").trim();
		if (caption) {
			return caption;
		}
		const title = String((preview && preview.title) || "").trim();
		if (title && !/^instagram(?: post| reel)?$/i.test(title)
			&& !/^fetching instagram/i.test(title)
			&& !/^post by @/i.test(title)) {
			return title;
		}
		const description = String(descriptionText || "").trim();
		if (description && !/^@?[a-z0-9._]+$/i.test(description)
			&& !/^video preview$/i.test(description)
			&& !/^image preview$/i.test(description)) {
			return description;
		}
		return "";
	}

	function appendInstagramPostPreview(card, preview, hostLabel, descriptionText, mediaState) {
		mediaState = mediaState || {};
		const metadata = (preview && preview.metadata) || {};
		const handle = previewInstagramHandle(preview);
		const displayName = previewInstagramDisplayName(preview, handle);
		const caption = previewInstagramCaption(preview, descriptionText);
		const shell = document.createElement("div");
		shell.className = "preview-card-instagram-shell";

		if (mediaState.hasPlayableMedia && (mediaState.isVideoMedia || mediaState.isGifMedia)) {
			shell.appendChild(createPreviewPlayableMedia(card, preview, mediaState.mediaUrl, mediaState.mediaMime,
				mediaState.mediaAudioUrl, mediaState.mediaAudioMime, mediaState.isVideoMedia, mediaState.isGifMedia,
				mediaState.videoInlineSupported, "preview-card-instagram-media"));
		} else if (mediaState.imageMediaItems && mediaState.imageMediaItems.length > 1) {
			appendPreviewImageCarousel(shell, preview, mediaState.imageMediaItems, "preview-card-instagram-media");
		} else {
			const imageItem = mediaState.imageMediaItems && mediaState.imageMediaItems.length
				? mediaState.imageMediaItems[0]
				: null;
			const imageUrl = imageItem ? imageItem.url : String(preview.thumbnailUrl || "").trim();
			if (imageUrl) {
				const media = document.createElement("div");
				media.className = "preview-card-media preview-card-image-preview-media preview-card-instagram-media";
				const image = document.createElement("img");
				image.className = "preview-card-image preview-card-inline-image preview-card-instagram-image";
				image.loading = "lazy";
				image.src = imageUrl;
				image.alt = preview.title || handle || "Instagram preview";
				media.appendChild(image);
				shell.appendChild(media);
			}
		}

		const body = document.createElement("div");
		body.className = "preview-card-instagram-body";

		const header = document.createElement("div");
		header.className = "preview-card-x-header preview-card-instagram-header";

		const avatar = document.createElement("span");
		avatar.className = "preview-card-x-avatar preview-card-instagram-avatar";
		styleAvatar(avatar, displayName || handle || "Instagram", false, String(metadata.instagramAvatarUrl || "").trim());
		header.appendChild(avatar);

		const heading = document.createElement("div");
		heading.className = "preview-card-x-heading";

		const sourceRow = document.createElement("div");
		sourceRow.className = "preview-card-x-source-row";
		const source = document.createElement("span");
		source.className = "preview-card-x-source";
		const username = handle ? handle.replace(/^@+/, "") : "";
		source.textContent = username || displayName || "Instagram";
		sourceRow.appendChild(source);
		heading.appendChild(sourceRow);

		const subline = document.createElement("div");
		subline.className = "preview-card-x-handle";
		const timestamp = previewXTimestamp(metadata.instagramCreatedAt);
		const displayHint = displayName && displayName.toLowerCase() !== String(username || "").toLowerCase()
			? displayName
			: (handle || hostLabel || "instagram.com");
		subline.textContent = displayHint + (timestamp ? " · " + timestamp : "");
		heading.appendChild(subline);
		header.appendChild(heading);

		const badge = document.createElement("span");
		badge.className = "preview-card-x-badge preview-card-instagram-badge";
		badge.textContent = String(metadata.instagramMediaKind || "").toLowerCase() === "reel" ? "Reel" : "Post";
		header.appendChild(badge);
		body.appendChild(header);

		if (caption) {
			const copy = document.createElement("div");
			copy.className = "preview-card-x-copy preview-card-instagram-copy";
			const captionNode = document.createElement("div");
			captionNode.className = "preview-card-x-title preview-card-instagram-caption";
			captionNode.textContent = caption;
			copy.appendChild(captionNode);
			body.appendChild(copy);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-x-footer preview-card-instagram-footer";
		const action = document.createElement("span");
		action.className = "preview-card-x-action preview-card-instagram-action";
		action.textContent = (preview && preview.openLabel) || "Open on Instagram";
		footer.appendChild(action);
		body.appendChild(footer);
		shell.appendChild(body);
		card.appendChild(shell);
	}

	function previewIsSocialPost(preview, hostLabel) {
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		if (!url) {
			return false;
		}
		const segments = url.pathname.split("/").filter(Boolean).map(function(segment) {
			try {
				return decodeURIComponent(segment);
			} catch (error) {
				return segment;
			}
		});
		if (host === "bsky.app") {
			return segments.length >= 4 && segments[0] === "profile" && segments[2] === "post";
		}
		if (host === "threads.net") {
			return segments.length >= 3 && /^@/.test(segments[0]) && segments[1] === "post";
		}
		if ((host === "tiktok.com" || host.endsWith(".tiktok.com"))
			&& segments.length >= 3 && /^@/.test(segments[0]) && segments[1] === "video"
			&& /^[0-9]{8,32}$/.test(segments[2])) {
			return true;
		}
		if (host && segments.length >= 2 && /^@/.test(segments[0]) && /^[0-9]{5,32}$/.test(segments[1])) {
			return true;
		}
		return false;
	}

	function previewSocialPostInfo(preview, hostLabel, sourceLabel) {
		const url = previewUrlObject(preview && preview.url);
		const host = normalizedPreviewHost(url ? url.hostname : hostLabel);
		const source = String(sourceLabel || "").trim();
		const segments = url ? url.pathname.split("/").filter(Boolean).map(function(segment) {
			try {
				return decodeURIComponent(segment);
			} catch (error) {
				return segment;
			}
		}) : [];
		if (host === "bsky.app") {
			return {
				mark: "B",
				badge: "bsky.app",
				source: source && !/^bsky\.app$/i.test(source) ? source : "Bluesky",
				handle: segments.length >= 2 ? "@" + String(segments[1] || "").replace(/^@+/, "") : host
			};
		}
		if (host === "threads.net") {
			return {
				mark: "T",
				badge: "threads.net",
				source: source && !/^threads\.net$/i.test(source) ? source : "Threads",
				handle: segments.length ? String(segments[0] || "") : host
			};
		}
		if (host === "tiktok.com" || host.endsWith(".tiktok.com")) {
			return {
				mark: "TT",
				badge: "TikTok",
				source: source && !/^tiktok(?:\.com)?$/i.test(source) ? source : "TikTok",
				handle: segments.length ? String(segments[0] || "") : host
			};
		}
		return {
			mark: "M",
			badge: host || "mastodon",
			source: source && source !== host ? source : "Mastodon",
			handle: segments.length ? String(segments[0] || "") : host
		};
	}

	function previewXPostHandle(preview) {
		const metadata = (preview && preview.metadata) || {};
		const metadataHandle = String(metadata.xHandle || "").trim();
		if (metadataHandle) {
			return metadataHandle.charAt(0) === "@" ? metadataHandle : "@" + metadataHandle;
		}
		const url = previewUrlObject(preview && preview.url);
		if (!url) {
			return "";
		}
		const segments = url.pathname.split("/").filter(Boolean).map(function(segment) {
			try {
				return decodeURIComponent(segment);
			} catch (error) {
				return segment;
			}
		});
		const statusIndex = segments.findIndex(function(segment) {
			return /^status(?:es)?$/i.test(segment);
		});
		if (statusIndex <= 0) {
			return "";
		}
		const handle = String(segments[statusIndex - 1] || "").trim();
		if (!handle || /^i$/i.test(handle)) {
			return "";
		}
		return "@" + handle.replace(/^@+/, "");
	}

	function previewXPostMetadata(preview) {
		return (preview && preview.metadata) || {};
	}

	function previewXCompactCount(value) {
		const count = Number(value);
		if (!Number.isFinite(count) || count <= 0) {
			return "";
		}
		if (count >= 1000000) {
			return (count / 1000000).toFixed(count >= 10000000 ? 0 : 1).replace(/\.0$/, "") + "M";
		}
		if (count >= 1000) {
			return (count / 1000).toFixed(count >= 10000 ? 0 : 1).replace(/\.0$/, "") + "K";
		}
		return String(Math.round(count));
	}

	function previewXTimestamp(value) {
		const raw = String(value || "").trim();
		if (!raw) {
			return "";
		}
		const date = new Date(raw);
		if (!Number.isFinite(date.getTime())) {
			return "";
		}
		const now = Date.now();
		const ageMs = Math.max(0, now - date.getTime());
		const minuteMs = 60 * 1000;
		const hourMs = 60 * minuteMs;
		const dayMs = 24 * hourMs;
		if (ageMs < hourMs) {
			return Math.max(1, Math.floor(ageMs / minuteMs)) + "m";
		}
		if (ageMs < dayMs) {
			return Math.floor(ageMs / hourMs) + "h";
		}
		return new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric" }).format(date);
	}

	function previewXPostTitle(preview, handle) {
		const rawTitle = String((preview && preview.title) || "").trim();
		const normalizedTitle = rawTitle.toLowerCase();
		if (rawTitle
			&& normalizedTitle !== "post on x"
			&& normalizedTitle !== "x"
			&& normalizedTitle !== "fetching post metadata") {
			return rawTitle;
		}
		if (handle) {
			return "Post by " + handle;
		}
		return rawTitle || "Post on X";
	}

	function previewXPostDisplayName(preview, handle, sourceLabel) {
		const metadataName = String(previewXPostMetadata(preview).xDisplayName || "").trim();
		if (metadataName) {
			return metadataName;
		}
		const subtitle = String((preview && preview.subtitle) || "").trim();
		if (subtitle
			&& !/^x$/i.test(subtitle)
			&& !/^twitter\/x$/i.test(subtitle)
			&& subtitle.toLowerCase() !== String(handle || "").toLowerCase()) {
			return subtitle;
		}
		const source = String(sourceLabel || "").trim();
		if (source
			&& !/^x\.com$/i.test(source)
			&& !/^twitter\/x$/i.test(source)
			&& source.toLowerCase() !== String(handle || "").toLowerCase()) {
			return source;
		}
		return "";
	}

	function previewXSummaryHandle(item) {
		const rawHandle = String((item && item.handle) || "").trim();
		if (!rawHandle) {
			return "";
		}
		return rawHandle.charAt(0) === "@" ? rawHandle : "@" + rawHandle.replace(/^@+/, "");
	}

	function appendXSummaryPost(parent, item, className) {
		if (!item || typeof item !== "object") {
			return false;
		}
		const handle = previewXSummaryHandle(item);
		const displayName = String(item.displayName || "").trim();
		const text = String(item.text || "").trim();
		if (!displayName && !handle && !text) {
			return false;
		}

		const row = document.createElement("div");
		row.className = className || "preview-card-x-context-post";

		const avatar = document.createElement("span");
		avatar.className = "preview-card-x-context-avatar";
		styleAvatar(avatar, displayName || handle || "X", false, String(item.avatarUrl || "").trim());
		row.appendChild(avatar);

		const body = document.createElement("div");
		body.className = "preview-card-x-context-body";

		const sourceRow = document.createElement("div");
		sourceRow.className = "preview-card-x-context-source-row";
		const source = document.createElement("span");
		source.className = "preview-card-x-context-source";
		source.textContent = displayName || handle || "X post";
		sourceRow.appendChild(source);
		if (item.verified) {
			const verified = document.createElement("span");
			verified.className = "preview-card-x-verified preview-card-x-context-verified";
			verified.textContent = "✓";
			verified.setAttribute("aria-label", "Verified");
			sourceRow.appendChild(verified);
		}
		const timestamp = previewXTimestamp(item.createdAt);
		const handleNode = document.createElement("span");
		handleNode.className = "preview-card-x-context-handle";
		handleNode.textContent = (handle || "x.com") + (timestamp ? " · " + timestamp : "");
		sourceRow.appendChild(handleNode);
		body.appendChild(sourceRow);

		if (text) {
			const textNode = document.createElement("div");
			textNode.className = "preview-card-x-context-text";
			textNode.textContent = text;
			body.appendChild(textNode);
		}

		row.appendChild(body);
		parent.appendChild(row);
		return true;
	}

	function appendXReplyContext(shell, preview) {
		const context = previewXPostMetadata(preview).xReplyContext;
		if (!Array.isArray(context) || !context.length) {
			return;
		}
		const wrapper = document.createElement("div");
		wrapper.className = "preview-card-x-context";
		context.slice(-3).forEach(function(item) {
			appendXSummaryPost(wrapper, item, "preview-card-x-context-post");
		});
		if (wrapper.childNodes.length) {
			shell.appendChild(wrapper);
		}
	}

	function appendXQuotedPost(shell, preview) {
		const quoted = previewXPostMetadata(preview).xQuotedPost;
		if (!quoted || typeof quoted !== "object") {
			return;
		}
		const wrapper = document.createElement("div");
		wrapper.className = "preview-card-x-quoted";
		appendXSummaryPost(wrapper, quoted, "preview-card-x-quoted-post");
		if (wrapper.childNodes.length) {
			shell.appendChild(wrapper);
		}
	}

	function appendSocialPostPreview(card, preview, hostLabel, sourceLabel, descriptionText) {
		const info = previewSocialPostInfo(preview, hostLabel, sourceLabel);
		const shell = document.createElement("div");
		shell.className = "preview-card-x-shell preview-card-social-shell";

		const header = document.createElement("div");
		header.className = "preview-card-x-header";

		const mark = document.createElement("span");
		mark.className = "preview-card-x-mark preview-card-social-mark";
		mark.textContent = info.mark;
		header.appendChild(mark);

		const heading = document.createElement("div");
		heading.className = "preview-card-x-heading";

		const source = document.createElement("div");
		source.className = "preview-card-x-source";
		source.textContent = info.source || info.handle || "Post";
		heading.appendChild(source);

		const subline = document.createElement("div");
		subline.className = "preview-card-x-handle";
		subline.textContent = info.handle || info.badge || hostLabel || "";
		heading.appendChild(subline);
		header.appendChild(heading);

		const badge = document.createElement("div");
		badge.className = "preview-card-x-badge";
		badge.textContent = info.badge || hostLabel || "";
		header.appendChild(badge);
		shell.appendChild(header);

		const copy = document.createElement("div");
		copy.className = "preview-card-x-copy";

		const title = document.createElement("div");
		title.className = "preview-card-x-title";
		title.textContent = previewXPostTitle(preview, info.handle);
		copy.appendChild(title);

		const lowerDescription = String(descriptionText || "").trim().toLowerCase();
		const hasUsefulDescription = lowerDescription
			&& lowerDescription !== String(info.handle || "").toLowerCase()
			&& lowerDescription !== String(info.source || "").toLowerCase()
			&& lowerDescription !== "fetching page metadata";
		if (hasUsefulDescription) {
			const description = document.createElement("div");
			description.className = "preview-card-x-description";
			description.textContent = descriptionText;
			copy.appendChild(description);
		}
		shell.appendChild(copy);

		if (preview.thumbnailUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-media preview-card-image-preview-media preview-card-x-media preview-card-x-gallery";
			const image = document.createElement("img");
			image.className = "preview-card-image preview-card-inline-image preview-card-x-image";
			image.loading = "lazy";
			image.src = preview.thumbnailUrl;
			image.alt = preview.title || info.source || "Post preview";
			media.appendChild(image);
			shell.appendChild(media);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-x-footer";
		const action = document.createElement("span");
		action.className = "preview-card-x-action";
		action.textContent = (preview && preview.openLabel) || "Open post";
		footer.appendChild(action);
		shell.appendChild(footer);
		card.appendChild(shell);
	}

	function appendXPostPreview(card, preview, hostLabel, sourceLabel, descriptionText, mediaState) {
		mediaState = mediaState || {};
		const metadata = previewXPostMetadata(preview);
		const handle = previewXPostHandle(preview);
		const displayName = previewXPostDisplayName(preview, handle, sourceLabel);
		const shell = document.createElement("div");
		shell.className = "preview-card-x-shell";
		appendXReplyContext(shell, preview);

		const header = document.createElement("div");
		header.className = "preview-card-x-header";

		const mark = document.createElement("span");
		mark.className = "preview-card-x-avatar";
		styleAvatar(mark, displayName || handle || "X", false, String(metadata.xAvatarUrl || "").trim());
		header.appendChild(mark);

		const heading = document.createElement("div");
		heading.className = "preview-card-x-heading";

		const sourceRow = document.createElement("div");
		sourceRow.className = "preview-card-x-source-row";

		const source = document.createElement("span");
		source.className = "preview-card-x-source";
		source.textContent = displayName || handle || "X post";
		sourceRow.appendChild(source);
		if (metadata.xVerified) {
			const verified = document.createElement("span");
			verified.className = "preview-card-x-verified";
			verified.textContent = "✓";
			verified.setAttribute("aria-label", "Verified");
			sourceRow.appendChild(verified);
		}
		heading.appendChild(sourceRow);

		const subline = document.createElement("div");
		subline.className = "preview-card-x-handle";
		const timestamp = previewXTimestamp(metadata.xCreatedAt);
		subline.textContent = (handle || (hostLabel || "x.com")) + (timestamp ? " · " + timestamp : "");
		heading.appendChild(subline);
		header.appendChild(heading);

		const menu = document.createElement("span");
		menu.className = "preview-card-x-menu";
		menu.textContent = "...";
		header.appendChild(menu);
		shell.appendChild(header);

		const copy = document.createElement("div");
		copy.className = "preview-card-x-copy";

		const title = document.createElement("div");
		title.className = "preview-card-x-title";
		title.textContent = previewXPostTitle(preview, handle);
		copy.appendChild(title);

		const lowerDescription = String(descriptionText || "").trim().toLowerCase();
		const hasUsefulDescription = lowerDescription
			&& lowerDescription !== String(handle || "").toLowerCase()
			&& lowerDescription !== "video preview"
			&& lowerDescription !== "image preview"
			&& lowerDescription !== "fetching page metadata";
		if (hasUsefulDescription) {
			const description = document.createElement("div");
			description.className = "preview-card-x-description";
			description.textContent = descriptionText;
			copy.appendChild(description);
		}
		shell.appendChild(copy);
		appendXQuotedPost(shell, preview);

		if (mediaState.hasPlayableMedia && (mediaState.isVideoMedia || mediaState.isGifMedia)) {
			shell.appendChild(createPreviewPlayableMedia(card, preview, mediaState.mediaUrl, mediaState.mediaMime,
				mediaState.mediaAudioUrl, mediaState.mediaAudioMime, mediaState.isVideoMedia, mediaState.isGifMedia,
				mediaState.videoInlineSupported, "preview-card-x-media preview-card-x-player"));
		} else if (mediaState.imageMediaItems && mediaState.imageMediaItems.length > 1) {
			appendPreviewImageCarousel(shell, preview, mediaState.imageMediaItems,
				"preview-card-x-media preview-card-x-gallery");
		} else {
			const imageItem = mediaState.imageMediaItems && mediaState.imageMediaItems.length
				? mediaState.imageMediaItems[0]
				: null;
			const imageUrl = imageItem ? imageItem.url : String(preview.thumbnailUrl || "").trim();
			if (imageUrl) {
				const media = document.createElement("div");
				media.className = "preview-card-media preview-card-image-preview-media preview-card-x-media preview-card-x-gallery";
				const image = document.createElement("img");
				image.className = "preview-card-image preview-card-inline-image preview-card-x-image";
				image.loading = "lazy";
				image.src = imageUrl;
				image.alt = preview.title || handle || "X post preview";
				media.appendChild(image);
				shell.appendChild(media);
			}
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-x-footer";
		const metrics = document.createElement("div");
		metrics.className = "preview-card-x-metrics";
		const repostCount = Number(metadata.xRepostCount || 0) + Number(metadata.xQuoteCount || 0);
		[
			{ token: "reply", label: "Reply", mark: "○", count: metadata.xReplyCount },
			{ token: "repost", label: "Repost", mark: "↻", count: repostCount },
			{ token: "like", label: "Like", mark: "♡", count: metadata.xLikeCount },
			{ token: "views", label: "Views", mark: "▥", count: metadata.xViewCount },
			{ token: "bookmark", label: "Bookmark", mark: "□", count: metadata.xBookmarkCount },
			{ token: "share", label: "Share", mark: "↗" }
		].forEach(function(item) {
			const metric = document.createElement("span");
			metric.className = "preview-card-x-metric preview-card-x-metric-" + item.token;
			metric.setAttribute("aria-label", item.label);
			const mark = document.createElement("span");
			mark.className = "preview-card-x-metric-mark";
			mark.textContent = item.mark;
			metric.appendChild(mark);
			const count = previewXCompactCount(item.count);
			if (count) {
				const countNode = document.createElement("span");
				countNode.className = "preview-card-x-metric-count";
				countNode.textContent = count;
				metric.appendChild(countNode);
			}
			metrics.appendChild(metric);
		});
		footer.appendChild(metrics);
		const action = document.createElement("span");
		action.className = "preview-card-x-action";
		action.textContent = (preview && preview.openLabel) || "Open on X";
		footer.appendChild(action);
		shell.appendChild(footer);
		card.appendChild(shell);
	}

	function previewImageMediaItems(preview, fallbackMediaUrl, fallbackMediaMime) {
		const items = [];
		const seen = Object.create(null);
		const addItem = function(rawUrl, rawMime, rawKind) {
			const url = String(rawUrl || "").trim();
			if (!url) {
				return;
			}
			const mime = String(rawMime || "").trim().toLowerCase();
			const kind = String(rawKind || "").trim().toLowerCase();
			if (kind && kind !== "image") {
				return;
			}
			if (mime && (!/^image\//i.test(mime) || mime === "image/gif")) {
				return;
			}
			if (seen[url]) {
				return;
			}
			seen[url] = true;
			items.push({ url: url, mime: mime, kind: "image" });
		};

		if (Array.isArray(preview && preview.mediaItems)) {
			preview.mediaItems.forEach(function(item) {
				addItem(item && item.url, item && item.mime, item && item.kind);
			});
		}
		const metadataImages = (preview && preview.metadata && Array.isArray(preview.metadata.listingImages))
			? preview.metadata.listingImages
			: [];
		metadataImages.forEach(function(item) {
			addItem(item && item.url, item && item.mime, item && item.kind);
		});
		const productImages = (preview && preview.metadata && Array.isArray(preview.metadata.productImages))
			? preview.metadata.productImages
			: [];
		productImages.forEach(function(item) {
			addItem(item && item.url, item && item.mime, item && item.kind);
		});
		if (!productImages.length && preview && preview.metadata) {
			addItem(preview.metadata.productImage, "image/jpeg", "image");
		}
		const articleImages = (preview && preview.metadata && Array.isArray(preview.metadata.articleImages))
			? preview.metadata.articleImages
			: [];
		articleImages.forEach(function(item) {
			addItem(item && item.url, item && item.mime, item && item.kind);
		});
		if (!articleImages.length && preview && preview.metadata) {
			addItem(preview.metadata.articleImage, "image/jpeg", "image");
		}
		if (!items.length) {
			addItem(fallbackMediaUrl || (preview && preview.thumbnailUrl), fallbackMediaMime, "image");
		}
		return items;
	}

	function appendPreviewImageCarousel(parent, preview, mediaItems, extraClass) {
		const media = document.createElement("div");
		media.className = "preview-card-media preview-card-image-preview-media preview-card-carousel"
			+ (extraClass ? " " + extraClass : "");
		media.tabIndex = 0;

		const image = document.createElement("img");
		image.className = "preview-card-image preview-card-inline-image preview-card-carousel-image";
		image.loading = "lazy";
		media.appendChild(image);

		const previousButton = document.createElement("button");
		previousButton.type = "button";
		previousButton.className = "preview-card-carousel-button preview-card-carousel-previous";
		previousButton.textContent = "<";
		previousButton.setAttribute("aria-label", "Previous image");
		media.appendChild(previousButton);

		const nextButton = document.createElement("button");
		nextButton.type = "button";
		nextButton.className = "preview-card-carousel-button preview-card-carousel-next";
		nextButton.textContent = ">";
		nextButton.setAttribute("aria-label", "Next image");
		media.appendChild(nextButton);

		const dots = document.createElement("div");
		dots.className = "preview-card-carousel-dots";
		const dotButtons = mediaItems.map(function(item, itemIndex) {
			const dot = document.createElement("button");
			dot.type = "button";
			dot.className = "preview-card-carousel-dot";
			dot.setAttribute("aria-label", "Show image " + String(itemIndex + 1));
			dot.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				setIndex(itemIndex);
			});
			dots.appendChild(dot);
			return dot;
		});
		media.appendChild(dots);

		let index = 0;
		const setIndex = function(nextIndex) {
			index = (nextIndex + mediaItems.length) % mediaItems.length;
			const item = mediaItems[index];
			image.src = item.url;
			image.alt = (preview && (preview.title || preview.subtitle)) || "Image preview";
			dotButtons.forEach(function(dot, dotIndex) {
				dot.classList.toggle("is-active", dotIndex === index);
				dot.setAttribute("aria-current", dotIndex === index ? "true" : "false");
			});
		};
		const step = function(delta, event) {
			if (event) {
				event.preventDefault();
				event.stopPropagation();
			}
			setIndex(index + delta);
		};

		previousButton.addEventListener("click", function(event) {
			step(-1, event);
		});
		nextButton.addEventListener("click", function(event) {
			step(1, event);
		});
		media.addEventListener("keydown", function(event) {
			if (event.key === "ArrowLeft") {
				step(-1, event);
			} else if (event.key === "ArrowRight") {
				step(1, event);
			}
		});
		image.addEventListener("load", function() {
			requestAnimationFrame(syncScrollState);
		});

		setIndex(0);
		parent.appendChild(media);
		return media;
	}

	function renderPreviewCard(message) {
		const preview = message && message.preview;
		if (!preview || (!preview.url && !preview.title && !preview.description && !preview.thumbnailUrl && !preview.mediaUrl && !preview.embedUrl)) {
			return null;
		}
		const mediaUrl = String(preview.mediaUrl || "").trim();
		const mediaMime = String(preview.mediaMime || "").trim().toLowerCase();
		const mediaAudioUrl = String(preview.mediaAudioUrl || "").trim();
		const mediaAudioMime = String(preview.mediaAudioMime || "").trim().toLowerCase();
		const embedUrl = String(preview.embedUrl || "").trim();
		const embedKind = String(preview.embedKind || "").trim().toLowerCase();
		const embedAspect = String(preview.embedAspect || "").trim().toLowerCase();
		const hasPlayableMedia = !!mediaUrl;
		const hasEmbedMedia = !!embedUrl;
		const isVideoMedia = hasPlayableMedia && (preview.kind === "video" || /^video\//i.test(mediaMime));
		const isGifMedia = hasPlayableMedia && (preview.kind === "gif" || mediaMime === "image/gif");
		const imageMediaItems = previewImageMediaItems(preview, mediaUrl, mediaMime);
		const hostLabel = previewHostLabel(preview.url);
		const sourceLabel = previewSourceLabel(preview, hostLabel);
		const badgeText = previewBadgeText(preview, sourceLabel, hostLabel);
		const descriptionText = previewDescriptionText(preview);
		const isGoogleSearch = previewIsGoogleSearch(preview, hostLabel);
		const isSteam = previewIsSteam(preview, hostLabel);
		const steamMediaItems = isSteam ? previewSteamMediaItems(preview) : [];
		const hasSteamGallery = steamMediaItems.length > 0;
		const gameStoreKind = isSteam ? "" : previewGameStoreKind(preview, hostLabel);
		const isGameStore = !!gameStoreKind;
		const gameStoreMediaItems = isGameStore ? previewGameStoreMediaItems(preview) : [];
		const hasGameStoreGallery = gameStoreMediaItems.length > 0;
		const richProviderSpec = (!isSteam && !isGameStore) ? previewProviderSpec(preview, hostLabel) : null;
		const swedishSiteKind = previewSwedishSiteKind(preview, hostLabel);
		const isSwedishMarketplace = swedishSiteKind === "tradera" || swedishSiteKind === "blocket";
		const isFlashbackThread = swedishSiteKind === "flashback";
		const isGitHub = previewIsGitHub(preview, hostLabel);
		const isXPost = previewIsXPost(preview, hostLabel);
		const isInstagramPost = !isXPost && previewIsInstagramPost(preview, hostLabel);
		const isTwitch = previewIsTwitch(preview, hostLabel);
		const isSocialPost = !isXPost && !isInstagramPost && !hasEmbedMedia && previewIsSocialPost(preview, hostLabel);
		const hasImageCarousel = imageMediaItems.length > 1 && !isVideoMedia && !isGifMedia;
		const hasInteractiveMedia = hasPlayableMedia || hasEmbedMedia || hasImageCarousel || hasSteamGallery || hasGameStoreGallery;
		const cardUsesDiv = hasInteractiveMedia || isGitHub || isGameStore || !!richProviderSpec;
		const isImagePreview = !isVideoMedia && !isGifMedia
			&& (preview.kind === "image" || /^image\//i.test(mediaMime))
			&& (!!mediaUrl || !!preview.thumbnailUrl || hasImageCarousel);
		const videoInlineSupported = isVideoMedia && previewInlineMediaEnabled()
			&& previewVideoCanPlayInline(mediaMime, mediaUrl);
		const hasThumbnail = !!preview.thumbnailUrl;
		const isYouTubeEmbed = hasEmbedMedia && embedKind === "youtube";
		const embedKindToken = previewClassToken(embedKind);
		const isShortEmbed = hasEmbedMedia && embedAspect === "short";
		const isSquareEmbed = hasEmbedMedia && embedAspect === "square";
		const isAudioEmbed = hasEmbedMedia && (embedAspect === "audio" || embedAspect === "compact-audio");

		const card = document.createElement(cardUsesDiv ? "div" : "button");
		if (!cardUsesDiv) {
			card.type = "button";
		} else {
			card.tabIndex = 0;
			card.setAttribute("role", "button");
		}
		card.className = "preview-card"
			+ (hasThumbnail ? " has-thumbnail" : "")
			+ (hasInteractiveMedia ? " has-media" : "")
			+ ((isVideoMedia || hasEmbedMedia) ? " is-video" : "")
			+ (isGifMedia ? " is-gif" : "")
			+ (preview.loading ? " is-loading" : "")
			+ (preview.failed ? " is-failed" : "")
			+ (preview.kind === "image" ? " is-image" : "")
			+ (isImagePreview ? " is-image-preview" : "")
			+ (hasImageCarousel ? " is-carousel" : "")
			+ (isGoogleSearch ? " is-google-search" : "")
			+ (isSteam ? " is-steam" : "")
			+ (isGameStore ? " is-game-store is-game-store-" + previewClassToken(gameStoreKind) : "")
			+ (richProviderSpec ? " is-sv-provider is-provider-" + previewClassToken(richProviderSpec.provider)
				+ " is-kind-" + previewClassToken(richProviderSpec.kind) : "")
			+ (swedishSiteKind ? " is-swedish-site is-" + previewClassToken(swedishSiteKind) : "")
			+ (isSwedishMarketplace ? " is-swedish-marketplace" : "")
			+ (isFlashbackThread ? " is-flashback-thread" : "")
			+ (isGitHub ? " is-github" : "")
			+ (isXPost ? " is-x-post" : "")
			+ (isInstagramPost ? " is-instagram-post is-x-post" : "")
			+ (isTwitch ? " is-twitch" : "")
			+ (isSocialPost ? " is-social-post is-x-post" : "")
			+ (hasEmbedMedia ? " is-embed" : "")
			+ (embedKindToken ? " is-" + embedKindToken + "-embed" : "")
			+ (isShortEmbed ? " is-embed-short" : "")
			+ (isSquareEmbed ? " is-embed-square" : "")
			+ (isAudioEmbed ? " is-embed-audio" : "")
			+ (embedAspect === "compact-audio" ? " is-embed-compact-audio" : "")
			+ (isYouTubeEmbed ? " is-youtube-embed" : "")
			+ (isYouTubeEmbed && embedAspect === "short" ? " is-youtube-short" : "");
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
		if (cardUsesDiv) {
			card.addEventListener("keydown", function(event) {
				if (event.key === "Enter" || event.key === " ") {
					activateCard(event);
				}
			});
		}

		if (isGoogleSearch) {
			appendGoogleSearchPreview(card, preview);
			return card;
		}
		if (isSteam) {
			appendSteamPreview(card, preview, hostLabel, sourceLabel, descriptionText, steamMediaItems);
			return card;
		}
		if (isGameStore) {
			appendGameStorePreview(card, preview, gameStoreKind, hostLabel, sourceLabel, descriptionText,
				gameStoreMediaItems);
			return card;
		}
		if (richProviderSpec) {
			appendRichProviderPreview(card, preview, richProviderSpec, hostLabel, sourceLabel, descriptionText);
			return card;
		}
		if (isSwedishMarketplace) {
			appendSwedishMarketplacePreview(card, preview, swedishSiteKind, hostLabel, sourceLabel, descriptionText);
			return card;
		}
		if (isFlashbackThread) {
			appendFlashbackPreview(card, preview, hostLabel, sourceLabel, descriptionText);
			return card;
		}
		if (isGitHub) {
			appendGitHubPreview(card, preview, hostLabel, descriptionText);
			return card;
		}
		if (isXPost) {
			appendXPostPreview(card, preview, hostLabel, sourceLabel, descriptionText, {
				mediaUrl: mediaUrl,
				mediaMime: mediaMime,
				mediaAudioUrl: mediaAudioUrl,
				mediaAudioMime: mediaAudioMime,
				hasPlayableMedia: hasPlayableMedia,
				isVideoMedia: isVideoMedia,
				isGifMedia: isGifMedia,
				videoInlineSupported: videoInlineSupported,
				imageMediaItems: imageMediaItems
			});
			return card;
		}
		if (isInstagramPost) {
			appendInstagramPostPreview(card, preview, hostLabel, descriptionText, {
				mediaUrl: mediaUrl,
				mediaMime: mediaMime,
				mediaAudioUrl: mediaAudioUrl,
				mediaAudioMime: mediaAudioMime,
				hasPlayableMedia: hasPlayableMedia,
				isVideoMedia: isVideoMedia,
				isGifMedia: isGifMedia,
				videoInlineSupported: videoInlineSupported,
				imageMediaItems: imageMediaItems
			});
			return card;
		}
		if (isSocialPost) {
			appendSocialPostPreview(card, preview, hostLabel, sourceLabel, descriptionText);
			return card;
		}

		if (hasEmbedMedia) {
			appendEmbedPreview(card, preview, embedUrl, embedAspect, embedKind);
		} else if (isImagePreview) {
			if (hasImageCarousel) {
				appendPreviewImageCarousel(card, preview, imageMediaItems);
			} else {
				const media = document.createElement("div");
				media.className = "preview-card-media preview-card-image-preview-media";
				const image = document.createElement("img");
				image.className = "preview-card-image preview-card-inline-image";
				image.loading = "lazy";
				image.src = mediaUrl || preview.thumbnailUrl;
				image.alt = preview.title || preview.subtitle || "Image preview";
				media.appendChild(image);
				card.appendChild(media);
			}
			return card;
		}

		if (hasPlayableMedia) {
			const media = createPreviewPlayableMedia(card, preview, mediaUrl, mediaMime, mediaAudioUrl, mediaAudioMime,
				isVideoMedia, isGifMedia, videoInlineSupported);
			card.appendChild(media);
		} else if (!hasEmbedMedia && preview.thumbnailUrl) {
			const media = document.createElement("div");
			media.className = "preview-card-media";
			const image = document.createElement("img");
			image.className = "preview-card-image";
			image.loading = "lazy";
			image.src = preview.thumbnailUrl;
			image.alt = preview.title || preview.subtitle || "Preview";
			media.appendChild(image);
			card.appendChild(media);
		} else if (!hasEmbedMedia) {
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
		const action = document.createElement(hasInteractiveMedia ? "button" : "span");
		action.className = "preview-card-action" + (hasInteractiveMedia ? " preview-card-open-button" : "");
		action.textContent = hasInteractiveMedia ? (preview.openLabel || "Open in browser") : (preview.openLabel || "Open link");
		if (hasInteractiveMedia) {
			action.type = "button";
			action.addEventListener("click", activateCard);
		}
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
		const tooltip = reactionTooltipText(reaction);
		if (tooltip) {
			button.title = tooltip;
			button.setAttribute("aria-label", String(reaction.emoji || "Reaction") + ": "
				+ tooltip.replace(/\n/g, ", "));
		}
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
				replyButton.className = "icon-button bubble-toolbar-button";
				replyButton.innerHTML =
					"<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"m9 17-5-5 5-5\"></path><path d=\"M20 18v-2a4 4 0 0 0-4-4H4\"></path></svg>";
				replyButton.title = "Reply";
				replyButton.setAttribute("aria-label", "Reply");
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
				deleteButton.className = "icon-button bubble-toolbar-button is-danger";
				deleteButton.innerHTML =
					"<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M3 6h18\"></path><path d=\"M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2\"></path><path d=\"M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6\"></path><path d=\"M10 11v6\"></path><path d=\"M14 11v6\"></path></svg>";
				deleteButton.title = "Delete";
				deleteButton.setAttribute("aria-label", "Delete");
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

	function messageHasMediaPreview(message) {
		const preview = message && message.preview;
		return !!String(preview && preview.url || "").trim()
			|| !!String(preview && preview.title || "").trim()
			|| !!String(preview && preview.description || "").trim()
			|| !!String(preview && preview.mediaUrl || "").trim()
			|| !!String(preview && preview.embedUrl || "").trim()
			|| !!String(preview && preview.thumbnailUrl || "").trim()
			|| (preview && Array.isArray(preview.mediaItems) && preview.mediaItems.length > 0);
	}

	function renderPreviewStub(message) {
		const stub = message && message.previewStub;
		if (!stub || message.preview) {
			return null;
		}

		const url = String(stub.url || "").trim();
		const host = String(stub.host || "").trim() || previewHostLabel(url) || "Link";
		const button = document.createElement("button");
		button.type = "button";
		button.className = "preview-stub";
		button.dataset.messageId = String(message.messageId || "");
		button.innerHTML =
			"<span class=\"preview-stub-mark\" aria-hidden=\"true\">&rarr;</span>"
			+ "<span class=\"preview-stub-copy\"><strong></strong><small></small></span>";
		button.querySelector("strong").textContent = host;
		button.querySelector("small").textContent = String(stub.loadingLabel || "Load preview");
		button.title = url || host;
		button.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			requestPreviewHydrationNow(message.messageId);
			button.classList.add("is-loading");
			button.querySelector("small").textContent = "Loading preview...";
		});
		return button;
	}

	function renderMessageBubble(message) {
		const bubble = document.createElement("div");
		bubble.className = "message-bubble"
			+ (message.own ? " is-own" : "")
			+ (messageHasMediaPreview(message) ? " has-media-preview" : "")
			+ (message.previewStub && !message.preview ? " has-preview-stub" : "");
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
		const previewStub = renderPreviewStub(message);
		if (previewStub) {
			bubble.appendChild(previewStub);
		}

		const footer = renderMessageFooter(message);
		if (footer) {
			bubble.appendChild(footer);
		}
		observePreviewHydrationTarget(bubble);
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
			messageActorKey(message),
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
					actorKey: messageActorKey(message),
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
		cluster.dataset.actorKey = group.actorKey || "";
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
		const groupHasMediaPreview = group.messages.some(messageHasMediaPreview);
		stack.className = "message-stack" + (groupHasMediaPreview ? " has-media-preview" : "");

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
		if (groupHasMediaPreview) {
			syncPreviewStackMediaSizeStateForStack(stack);
		}

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
		const linkDenseMode = renderState && renderState.timelineMode === "linkDense";
		const chunkGroupCount = linkDenseMode ? linkDenseMessageRenderChunkGroupCount : messageRenderChunkGroupCount;
		const chunkBudgetMs = linkDenseMode ? linkDenseMessageRenderChunkBudgetMs : messageRenderChunkBudgetMs;
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
						&& renderedGroupCount < chunkGroupCount
						&& (renderedGroupCount === 0 || monotonicNow() - chunkStartedAt < chunkBudgetMs)) {
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
				schedulePreviewHydrationFlush();
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
					&& lastCluster.dataset.actorKey === messageActorKey(message)
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
				actorKey: messageActorKey(message),
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
		const timelineMode = String(snapshot.timelineMode || scope.timelineMode || "").trim() === "linkDense"
			? "linkDense"
			: "normal";
		if (shouldShowScopeLoading(scope, messages) && !renderOptions.resolvePendingScopeLoading) {
			beginScopeLoading(scopeToken, { force: true });
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
			clearPreviewHydrationState();
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
			timelineMode: timelineMode,
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
					{ id: "help.feedback", label: "Report feedback...", enabled: true },
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
			const inherited = !!group.inherited;
			const form = document.createElement("div");
			form.className = "modern-dialog-acl-detail-grid";
			const nameLabel = document.createElement("label");
			nameLabel.className = "modern-dialog-acl-control";
			const nameCaption = document.createElement("span");
			nameCaption.textContent = "Group";
			const nameInput = document.createElement("input");
			nameInput.type = "text";
			nameInput.value = group.name || "";
			nameInput.disabled = inherited;
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
			appendAclButton(actions, inherited ? "Reset group" : "Delete group", "is-danger", false, function() {
				const next = aclCurrentModel(field, model);
				if (inherited) {
					next.groups[selectedIndex].inherit = true;
					next.groups[selectedIndex].inheritable = true;
					next.groups[selectedIndex].add = [];
					next.groups[selectedIndex].remove = [];
					delete next.groups[selectedIndex].addText;
					delete next.groups[selectedIndex].removeText;
					next.selectedGroupIndex = selectedIndex;
				} else {
					next.groups.splice(selectedIndex, 1);
					next.selectedGroupIndex = Math.max(0, selectedIndex - 1);
				}
				next.activeTab = "groups";
				aclUpdateModel(field, next, true);
			});
			form.appendChild(actions);
			detail.appendChild(form);

			appendAclMemberSection(detail, field, model, selectedIndex, "inheritedMembers", "Inherited members", false);
			appendAclMemberSection(detail, field, model, selectedIndex, "add", "Add members", true);
			appendAclMemberSection(detail, field, model, selectedIndex, "remove", "Remove members", group.inherit !== false);
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
		} else if (type === "button") {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button modern-dialog-field-button"
				+ (field.tone ? " is-" + field.tone : "");
			button.textContent = field.buttonLabel || field.value || field.label || "Action";
			button.disabled = field.enabled === false;
			if (fieldTooltip) {
				button.title = fieldTooltip;
			}
			button.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernDialogAction(field.actionId || field.id || "", { fieldId: field.id || "" });
			});
			row.appendChild(button);
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

	function stonksNumber(value) {
		const number = Number(value);
		return Number.isFinite(number) ? number : 0;
	}

	function formatStonksMoney(value, currency) {
		return (currency || "USD").toUpperCase() + " " + stonksNumber(value).toLocaleString(undefined, {
			minimumFractionDigits: 2,
			maximumFractionDigits: 2
		});
	}

	function formatStonksPercent(value) {
		const number = stonksNumber(value);
		return (number > 0 ? "+" : "") + number.toFixed(2) + "%";
	}

	function formatStonksQuantity(value) {
		return stonksNumber(value).toLocaleString(undefined, {
			maximumFractionDigits: 4
		});
	}

	function formatStonksSignedMoney(value, currency) {
		const number = stonksNumber(value);
		return (number > 0 ? "+" : number < 0 ? "-" : "") + formatStonksMoney(Math.abs(number), currency);
	}

	function formatStonksTime(seconds) {
		const value = Number(seconds);
		if (!Number.isFinite(value) || value <= 0) {
			return "-";
		}
		return new Date(value * 1000).toLocaleString(undefined, {
			month: "short",
			day: "numeric",
			hour: "2-digit",
			minute: "2-digit"
		});
	}

	function normalizeStonksSymbol(value) {
		let symbol = String(value || "").trim();
		if (symbol.startsWith("$")) {
			symbol = symbol.slice(1);
		}
		symbol = symbol.replace(/\s+/g, " ");
		symbol = symbol.toUpperCase();
		const stockholmClass = symbol.match(/^(\^?[A-Z][A-Z0-9]{0,5}|\d{3,}[A-Z0-9]*)\s+([A-Z])$/);
		if (stockholmClass) {
			symbol = stockholmClass[1] + "-" + stockholmClass[2];
		}
		return /^(\^?[A-Z][A-Z0-9]*(?:[.=-][A-Z0-9]+){0,4}|\d{3,}[A-Z0-9]*(?:[.=-][A-Z0-9]+){1,4})$/.test(symbol)
			? symbol
			: "";
	}

	function stonksYahooQuoteUrl(symbol) {
		const normalized = normalizeStonksSymbol(symbol);
		return normalized ? "https://finance.yahoo.com/quote/" + encodeURIComponent(normalized) : "";
	}

	function stonksYahooChartUrl(symbol) {
		const normalized = normalizeStonksSymbol(symbol);
		return normalized
			? "https://query1.finance.yahoo.com/v8/finance/chart/" + encodeURIComponent(normalized) + "?interval=1d&range=5d"
			: "";
	}

	function stonksProviderLinks(symbol) {
		const normalized = normalizeStonksSymbol(symbol);
		if (!normalized) {
			return [];
		}
		return [
			{ id: "yahoo-finance", label: "Yahoo", url: stonksYahooQuoteUrl(normalized) },
			{ id: "avanza", label: "Avanza", url: "https://www.avanza.se/sok.html?query=" + encodeURIComponent(normalized) },
			{ id: "nordnet", label: "Nordnet", url: "https://www.nordnet.se/aktier/kurser?search=" + encodeURIComponent(normalized) }
		];
	}

	function stonksProviderLabel(providerId) {
		if (providerId === "yahoo-finance") {
			return "Yahoo";
		}
		if (providerId === "avanza") {
			return "Avanza";
		}
		if (providerId === "nordnet") {
			return "Nordnet";
		}
		return "Manual";
	}

	function stonksEmptyPosition(currency) {
		return {
			symbol: "",
			quantity: 0,
			price: 0,
			marketValue: 0,
			currency: currency || "USD",
			displayName: "",
			providerId: "manual",
			providerSymbol: "",
			exchange: "",
			quoteTime: 0,
			quoteSourceUrl: "",
			quoteConfidence: 0.35
		};
	}

	function stonksNormalizePosition(position, fallbackCurrency) {
		const quantity = stonksNumber(position && position.quantity);
		const price = stonksNumber(position && position.price);
		const marketValue = quantity > 0 && price > 0 ? quantity * price : stonksNumber(position && position.marketValue);
		const symbol = normalizeStonksSymbol(position && position.symbol) || String(position && position.symbol || "").trim().toUpperCase();
		const providerId = String(position && position.providerId || "").trim() || "manual";
		return {
			symbol: symbol,
			quantity: quantity,
			price: price,
			marketValue: marketValue,
			currency: String(position && position.currency || fallbackCurrency || "USD").trim().toUpperCase() || "USD",
			displayName: String(position && position.displayName || "").trim(),
			providerId: providerId,
			providerSymbol: String(position && position.providerSymbol || symbol).trim(),
			exchange: String(position && position.exchange || "").trim(),
			quoteTime: Number(position && position.quoteTime) || 0,
			quoteSourceUrl: String(position && position.quoteSourceUrl || "").trim(),
			quoteConfidence: Number.isFinite(Number(position && position.quoteConfidence)) ? Number(position.quoteConfidence) : (providerId === "manual" ? 0.35 : 0.75)
		};
	}

	function stonksSelectedUserId(stonks) {
		return Number(stonks && (stonks.selectedUserId || stonks.selfUserId || 0)) || 0;
	}

	function stonksSelectedUserName(stonks) {
		const selectedId = stonksSelectedUserId(stonks);
		const directName = String(stonks && stonks.selectedUserName || "").trim();
		if (directName) {
			return directName;
		}
		const users = Array.isArray(stonks && stonks.users) ? stonks.users : [];
		const match = users.find(function(user) {
			return Number(user && user.userId) === selectedId;
		});
		return String(match && match.userName || (selectedId ? "user " + selectedId : "Portfolio")).trim();
	}

	function stonksCanEditPortfolio(stonks) {
		const selectedId = stonksSelectedUserId(stonks);
		const selfId = Number(stonks && stonks.selfUserId || 0);
		return selectedId > 0 && (!!(stonks && stonks.canAdmin) || (!!(stonks && stonks.registered) && selectedId === selfId));
	}

	function stonksLatestSnapshot(stonks) {
		const snapshots = Array.isArray(stonks && stonks.snapshots) ? stonks.snapshots : [];
		return snapshots.length ? snapshots[0] : null;
	}

	function stonksDraftIdentity(stonks) {
		const latest = stonksLatestSnapshot(stonks);
		return [
			stonksSelectedUserId(stonks),
			latest && latest.snapshotId || 0,
			latest && latest.createdAt || 0,
			latest && latest.totalValue || 0
		].join(":");
	}

	function stonksQuoteFreshnessText(position) {
		if (!position || !position.quoteTime) {
			return "manual";
		}
		const ageSeconds = Math.max(0, (Date.now() / 1000) - Number(position.quoteTime));
		if (ageSeconds < 60 * 60) {
			return "fresh";
		}
		if (ageSeconds < 36 * 60 * 60) {
			return Math.round(ageSeconds / 3600) + "h";
		}
		return "stale";
	}

	function stonksSourceLabel(position) {
		const provider = stonksProviderLabel(position && position.providerId);
		const freshness = stonksQuoteFreshnessText(position);
		return freshness === "manual" ? provider : provider + " / " + freshness;
	}

	function stonksDraftTotalFromPositions(positions) {
		return (positions || []).reduce(function(total, position) {
			const normalized = stonksNormalizePosition(position, stonksDraftCurrency);
			return total + stonksNumber(normalized.marketValue);
		}, 0);
	}

	function seedStonksDraft(stonks) {
		const snapshots = Array.isArray(stonks.snapshots) ? stonks.snapshots : [];
		const latest = snapshots.length ? snapshots[0] : null;
		const positions = latest && Array.isArray(latest.positions) && latest.positions.length
			? latest.positions
			: [stonksEmptyPosition(latest && latest.currency || "USD")];
		stonksDraftPositions = positions.map(function(position) {
			return stonksNormalizePosition(position, latest && latest.currency || "USD");
		});
		stonksDraftCurrency = latest && latest.currency || "USD";
		stonksDraftNote = "";
		stonksDraftKey = stonksDraftIdentity(stonks || {});
	}

	function ensureStonksDraft(stonks) {
		if (!Array.isArray(stonksDraftPositions) || stonksDraftKey !== stonksDraftIdentity(stonks || {})) {
			seedStonksDraft(stonks || {});
		}
	}

	function stonksDraftTotal() {
		return stonksDraftTotalFromPositions(stonksDraftPositions || []);
	}

	function stonksPositionMap(snapshot) {
		const map = new Map();
		(snapshot && Array.isArray(snapshot.positions) ? snapshot.positions : []).forEach(function(position) {
			const normalized = stonksNormalizePosition(position, snapshot && snapshot.currency || "USD");
			if (normalized.symbol) {
				map.set(normalized.symbol, normalized);
			}
		});
		return map;
	}

	function stonksValueChanged(before, after) {
		const delta = Math.abs(stonksNumber(after) - stonksNumber(before));
		return delta > Math.max(0.01, Math.abs(stonksNumber(before)) * 0.001);
	}

	function stonksSnapshotChanges(snapshot, previousSnapshot) {
		const currentPositions = stonksPositionMap(snapshot);
		const previousPositions = stonksPositionMap(previousSnapshot);
		const currency = snapshot && snapshot.currency || previousSnapshot && previousSnapshot.currency || "USD";
		const changes = [];

		if (!currentPositions.size && previousPositions.size) {
			return ["Cleared portfolio"];
		}
		if (currentPositions.size && !previousPositions.size) {
			currentPositions.forEach(function(position) {
				changes.push("Added " + position.symbol + " at " + formatStonksMoney(position.marketValue, position.currency));
			});
			return changes;
		}

		currentPositions.forEach(function(position, symbol) {
			const previous = previousPositions.get(symbol);
			if (!previous) {
				changes.push("Added " + symbol + " at " + formatStonksMoney(position.marketValue, position.currency));
				return;
			}
			if (stonksValueChanged(previous.marketValue, position.marketValue)) {
				changes.push(symbol + " value " + formatStonksSignedMoney(stonksNumber(position.marketValue) - stonksNumber(previous.marketValue), position.currency));
			}
			if (stonksValueChanged(previous.quantity, position.quantity)) {
				changes.push(symbol + " qty " + formatStonksQuantity(previous.quantity) + " -> " + formatStonksQuantity(position.quantity));
			}
			if (stonksValueChanged(previous.price, position.price)) {
				changes.push(symbol + " price " + formatStonksMoney(previous.price, position.currency) + " -> " + formatStonksMoney(position.price, position.currency));
			}
		});
		previousPositions.forEach(function(position, symbol) {
			if (!currentPositions.has(symbol)) {
				changes.push("Removed " + symbol + " worth " + formatStonksMoney(position.marketValue, position.currency));
			}
		});

		if (previousSnapshot && stonksValueChanged(previousSnapshot.totalValue, snapshot && snapshot.totalValue)) {
			changes.unshift("Total " + formatStonksSignedMoney(stonksNumber(snapshot && snapshot.totalValue) - stonksNumber(previousSnapshot.totalValue), currency));
		}
		if (!changes.length) {
			changes.push(previousSnapshot ? "Metadata update" : "Initial save");
		}
		return changes;
	}

	function stonksInput(value, field, type, placeholder) {
		const input = document.createElement("input");
		input.type = type || "text";
		input.value = value == null ? "" : String(value);
		input.dataset.stonksField = field;
		if (placeholder) {
			input.placeholder = placeholder;
		}
		return input;
	}

	function stonksCollectDraft(root) {
		const positions = [];
		root.querySelectorAll(".stonks-position-row").forEach(function(row) {
			const symbol = row.querySelector("[data-stonks-field='symbol']").value.trim();
			const quantity = Number(row.querySelector("[data-stonks-field='quantity']").value || 0);
			const price = Number(row.querySelector("[data-stonks-field='price']").value || 0);
			const computedMarketValue = Number.isFinite(quantity * price) ? quantity * price : 0;
			const marketValue = computedMarketValue > 0 ? computedMarketValue : Number(row.querySelector("[data-stonks-field='marketValue']").value || 0);
			const currency = row.querySelector("[data-stonks-field='currency']").value.trim() || stonksDraftCurrency || "USD";
			const displayName = row.querySelector("[data-stonks-field='displayName']").value.trim();
			if (symbol || quantity || price || marketValue) {
				positions.push(stonksNormalizePosition({
					symbol: symbol,
					quantity: quantity,
					price: price,
					marketValue: marketValue,
					currency: currency,
					displayName: displayName,
					providerId: row.dataset.stonksProviderId || "manual",
					providerSymbol: row.dataset.stonksProviderSymbol || symbol,
					exchange: row.dataset.stonksExchange || "",
					quoteTime: Number(row.dataset.stonksQuoteTime || 0),
					quoteSourceUrl: row.dataset.stonksQuoteSourceUrl || "",
					quoteConfidence: Number(row.dataset.stonksQuoteConfidence || 0)
				}, stonksDraftCurrency));
			}
		});
		stonksDraftPositions = positions.length ? positions : [stonksEmptyPosition(stonksDraftCurrency || "USD")];
		const currencyInput = root.querySelector("[data-stonks-field='snapshotCurrency']");
		const noteInput = root.querySelector("[data-stonks-field='snapshotNote']");
		stonksDraftCurrency = currencyInput ? currencyInput.value.trim() || "USD" : stonksDraftCurrency;
		stonksDraftNote = noteInput ? noteInput.value : stonksDraftNote;
		return { positions: positions, currency: stonksDraftCurrency, note: stonksDraftNote };
	}

	function stonksValidateDraft(draft) {
		const errors = [];
		const warnings = [];
		const positions = Array.isArray(draft.positions) ? draft.positions : [];
		const snapshotCurrency = String(draft.currency || "USD").trim().toUpperCase() || "USD";
		const seen = new Set();
		if (!positions.length) {
			errors.push("Add at least one position.");
		}
		positions.forEach(function(position, index) {
			const label = "Row " + (index + 1);
			const symbol = normalizeStonksSymbol(position.symbol);
			if (!symbol) {
				errors.push(label + ": invalid ticker symbol.");
			} else if (seen.has(symbol)) {
				errors.push(label + ": duplicate ticker " + symbol + ".");
			}
			seen.add(symbol);
			if (!(stonksNumber(position.quantity) > 0)) {
				errors.push(label + ": quantity must be greater than zero.");
			}
			if (!(stonksNumber(position.price) > 0)) {
				errors.push(label + ": price must be greater than zero.");
			}
			if (String(position.currency || "").trim().toUpperCase() !== snapshotCurrency) {
				errors.push(label + ": currency must match " + snapshotCurrency + ".");
			}
			if (!position.providerId || position.providerId === "manual") {
				warnings.push(label + ": manual quote source.");
			}
			if (!position.exchange && position.providerId !== "manual") {
				warnings.push(label + ": exchange is unknown.");
			}
			const confidence = stonksNumber(position.quoteConfidence);
			if (confidence > 0 && confidence < 0.5) {
				warnings.push(label + ": low quote confidence.");
			}
			if (position.quoteTime) {
				const ageSeconds = Math.max(0, (Date.now() / 1000) - Number(position.quoteTime));
				if (ageSeconds > 36 * 60 * 60) {
					warnings.push(label + ": quote looks stale.");
				}
			}
		});
		return { errors: errors, warnings: warnings };
	}

	function updateStonksValidation(summary, draft, submit, total) {
		const validation = stonksValidateDraft(draft);
		summary.innerHTML = "";
		summary.className = "stonks-validation" + (validation.errors.length ? " has-errors" : "");
		const title = document.createElement("strong");
		title.textContent = validation.errors.length
			? validation.errors.length + " issue" + (validation.errors.length === 1 ? "" : "s")
			: "Ready to save";
		summary.appendChild(title);
		const messages = validation.errors.concat(validation.warnings).slice(0, 5);
		if (messages.length) {
			const list = document.createElement("div");
			list.className = "stonks-validation-list";
			messages.forEach(function(message) {
				const item = document.createElement("span");
				item.textContent = message;
				list.appendChild(item);
			});
			summary.appendChild(list);
		}
		if (total) {
			total.textContent = formatStonksMoney(stonksDraftTotalFromPositions(draft.positions), draft.currency);
		}
		if (submit) {
			submit.disabled = !draft.registered || validation.errors.length > 0;
		}
		return validation;
	}

	function stonksApplyQuoteSuggestion(suggestion) {
		const position = stonksNormalizePosition(suggestion, stonksDraftCurrency || suggestion.currency || "USD");
		const positions = Array.isArray(stonksDraftPositions) ? stonksDraftPositions.slice() : [];
		const emptyIndex = positions.findIndex(function(current) {
			return !String(current.symbol || "").trim() && !stonksNumber(current.quantity) && !stonksNumber(current.price);
		});
		if (emptyIndex >= 0) {
			positions[emptyIndex] = position;
		} else {
			positions.push(position);
		}
		stonksDraftCurrency = position.currency || stonksDraftCurrency || "USD";
		stonksDraftPositions = positions;
		stonksQuoteSuggestions = [];
		stonksQuoteSearchError = "";
		renderModernDialog();
	}

	function handleStonksQuoteLookupResult(result) {
		const payload = result || {};
		if (!stonksQuoteSearchRequestId || String(payload.requestId || "") !== stonksQuoteSearchRequestId) {
			return;
		}

		stonksQuoteSearchBusy = false;
		if (!payload.ok) {
			stonksQuoteSuggestions = [];
			stonksQuoteSearchError = String(payload.error || "Quote lookup failed.");
			renderModernDialog();
			return;
		}

		const quoteSymbol = normalizeStonksSymbol(payload.symbol || payload.providerSymbol || stonksQuoteSearchText);
		const price = Number(payload.price);
		if (!quoteSymbol || !Number.isFinite(price) || price <= 0) {
			stonksQuoteSuggestions = [];
			stonksQuoteSearchError = "Quote lookup returned unusable data.";
			renderModernDialog();
			return;
		}

		stonksQuoteSuggestions = [stonksNormalizePosition({
			symbol: quoteSymbol,
			quantity: 0,
			price: price,
			marketValue: 0,
			currency: payload.currency || stonksDraftCurrency || "USD",
			displayName: payload.displayName || quoteSymbol,
			providerId: payload.providerId || "yahoo-finance",
			providerSymbol: payload.providerSymbol || quoteSymbol,
			exchange: payload.exchange || "",
			quoteTime: Number(payload.quoteTime || 0),
			quoteSourceUrl: payload.quoteSourceUrl || stonksYahooQuoteUrl(quoteSymbol),
			quoteConfidence: Number(payload.quoteConfidence || 1)
		}, stonksDraftCurrency)];
		stonksQuoteSearchError = "";
		renderModernDialog();
	}

	function runStonksQuoteSearch(query) {
		const symbol = normalizeStonksSymbol(query);
		stonksQuoteSearchText = query || "";
		stonksQuoteSearchError = "";
		stonksQuoteSuggestions = [];
		if (!symbol) {
			stonksQuoteSearchError = "Enter a ticker like RKLB, SAAB B, or ERIC-B.ST.";
			renderModernDialog();
			return;
		}
		stonksQuoteSearchBusy = true;
		stonksQuoteSearchRequestId = String(Date.now()) + "-" + Math.random().toString(36).slice(2);
		renderModernDialog();
		if (notifyBridge("lookupFinanceQuote", stonksQuoteSearchRequestId, symbol)) {
			return;
		}

		fetch(stonksYahooChartUrl(symbol), { cache: "no-store" })
			.then(function(response) {
				if (!response.ok) {
					throw new Error("Yahoo returned " + response.status);
				}
				return response.json();
			})
			.then(function(payload) {
				const chart = payload && payload.chart || {};
				if (chart.error) {
					throw new Error(chart.error.description || "Yahoo Finance returned an error.");
				}
				const result = chart.result && chart.result[0];
				const meta = result && result.meta || {};
				const quoteSymbol = normalizeStonksSymbol(meta.symbol || symbol);
				const price = Number(meta.regularMarketPrice);
				if (!quoteSymbol || !Number.isFinite(price) || price <= 0) {
					throw new Error("Yahoo did not return a usable price.");
				}
				const quoteTime = Number(meta.regularMarketTime || 0);
				stonksQuoteSuggestions = [stonksNormalizePosition({
					symbol: quoteSymbol,
					quantity: 0,
					price: price,
					marketValue: 0,
					currency: meta.currency || stonksDraftCurrency || "USD",
					displayName: meta.shortName || meta.longName || quoteSymbol,
					providerId: "yahoo-finance",
					providerSymbol: quoteSymbol,
					exchange: meta.fullExchangeName || meta.exchangeName || "",
					quoteTime: Number.isFinite(quoteTime) ? quoteTime : 0,
					quoteSourceUrl: stonksYahooQuoteUrl(quoteSymbol),
					quoteConfidence: 1.0
				}, stonksDraftCurrency)];
			})
			.catch(function(error) {
				stonksQuoteSearchError = error && error.message ? error.message : "Quote lookup failed.";
			})
			.finally(function() {
				stonksQuoteSearchBusy = false;
				renderModernDialog();
			});
	}

	function appendStonksStatus(parent, stonks) {
		if (stonks.error) {
			const error = document.createElement("div");
			error.className = "stonks-status is-error";
			error.textContent = stonks.error;
			parent.appendChild(error);
		}
		if (stonks.status) {
			const status = document.createElement("div");
			status.className = "stonks-status";
			status.textContent = stonks.status;
			parent.appendChild(status);
		}
	}

	function appendStonksTabs(parent, stonks) {
		const tabs = [["overview", "Overview"], ["ledger", "Portfolio"], ["leaderboard", "Leaderboard"], ["following", "Following"], ["audit", "Audit"]];
		if (stonks.canAdmin) {
			tabs.push(["admin", "Admin"]);
		}
		if (!tabs.some(function(tab) { return tab[0] === stonksActiveTab; })) {
			stonksActiveTab = "overview";
		}
		const wrap = document.createElement("div");
		wrap.className = "stonks-tabs";
		tabs.forEach(function(tab) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "stonks-tab" + (stonksActiveTab === tab[0] ? " is-selected" : "");
			button.textContent = tab[1];
			button.addEventListener("click", function() {
				stonksActiveTab = tab[0];
				renderModernDialog();
			});
			wrap.appendChild(button);
		});
		parent.appendChild(wrap);
	}

	function appendStonksHeatmap(parent, stonks) {
		const latest = stonksLatestSnapshot(stonks);
		const positions = latest && Array.isArray(latest.positions) ? latest.positions.slice() : [];
		const visiblePositions = positions
			.map(function(position) {
				return stonksNormalizePosition(position, latest && latest.currency || "USD");
			})
			.filter(function(position) {
				return position.symbol && stonksNumber(position.marketValue) > 0;
			})
			.sort(function(left, right) {
				return stonksNumber(right.marketValue) - stonksNumber(left.marketValue);
			});
		if (!visiblePositions.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = latest && latest.positionsRedacted ? "Portfolio positions are private." : "No open positions to map.";
			parent.appendChild(empty);
			return;
		}

		const total = visiblePositions.reduce(function(sum, position) {
			return sum + stonksNumber(position.marketValue);
		}, 0);
		const heatmap = document.createElement("section");
		heatmap.className = "stonks-heatmap";
		const title = document.createElement("div");
		title.className = "stonks-heatmap-title";
		title.innerHTML = "<strong></strong><span></span>";
		title.querySelector("strong").textContent = "Portfolio heatmap";
		title.querySelector("span").textContent = stonksSelectedUserName(stonks);
		heatmap.appendChild(title);
		const grid = document.createElement("div");
		grid.className = "stonks-heatmap-grid";
		visiblePositions.forEach(function(position) {
			const share = total > 0 ? stonksNumber(position.marketValue) / total : 0;
			const tile = document.createElement("div");
			tile.className = "stonks-heatmap-tile";
			tile.style.setProperty("--tile-alpha", String(Math.min(0.82, 0.18 + share * 1.9)));
			tile.style.setProperty("--tile-weight", Math.max(1, Math.round(share * 7)) + "fr");
			tile.innerHTML = "<strong></strong><span></span><small></small>";
			tile.querySelector("strong").textContent = position.symbol;
			tile.querySelector("span").textContent = formatStonksMoney(position.marketValue, position.currency);
			tile.querySelector("small").textContent = Math.round(share * 1000) / 10 + "%";
			grid.appendChild(tile);
		});
		heatmap.appendChild(grid);
		parent.appendChild(heatmap);
	}

	function appendStonksOverview(parent, stonks) {
		const snapshots = Array.isArray(stonks.snapshots) ? stonks.snapshots : [];
		const latest = stonksLatestSnapshot(stonks);
		const stats = document.createElement("div");
		stats.className = "stonks-stat-grid";
		[["Total value", latest ? formatStonksMoney(latest.totalValue, latest.currency) : "-"],
		 ["Last saved", latest ? formatStonksTime(latest.createdAt) : "-"],
		 ["Owner", stonksSelectedUserName(stonks)],
		 ["Leaderboard rows", String((stonks.leaderboard || []).length)]].forEach(function(item) {
			const card = document.createElement("div");
			card.className = "stonks-stat";
			const label = document.createElement("span");
			label.textContent = item[0];
			const value = document.createElement("strong");
			value.textContent = item[1];
			card.append(label, value);
			stats.appendChild(card);
		});
		parent.appendChild(stats);
		const periods = document.createElement("div");
		periods.className = "stonks-periods";
		(stonks.periods || ["1d", "7d", "30d", "ytd"]).forEach(function(period) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "stonks-chip" + (period === stonks.selectedPeriod ? " is-selected" : "");
			button.textContent = period;
			button.addEventListener("click", function() {
				invokeModernDialogAction("selectPeriod", { period: period });
			});
			periods.appendChild(button);
		});
		parent.appendChild(periods);
		appendStonksHeatmap(parent, stonks);
		const quick = document.createElement("button");
		quick.type = "button";
		quick.className = "chip-button is-primary";
		quick.textContent = "Save portfolio";
		quick.disabled = !stonksCanEditPortfolio(stonks);
		quick.addEventListener("click", function() {
			if (!refs.modernDialogBody.querySelector(".stonks-position-row")) {
				stonksActiveTab = "overview";
				renderModernDialog();
				return;
			}
			const draft = stonksCollectDraft(refs.modernDialogBody);
			draft.registered = stonksCanEditPortfolio(stonks);
			draft.userId = stonksSelectedUserId(stonks);
			if (stonksValidateDraft(draft).errors.length) {
				stonksActiveTab = "overview";
				renderModernDialog();
				return;
			}
			invokeModernDialogAction("savePortfolio", draft);
		});
		parent.appendChild(quick);
	}

	function appendStonksEditor(parent, stonks) {
		ensureStonksDraft(stonks);
		const canEdit = stonksCanEditPortfolio(stonks);
		const ownerUserId = stonksSelectedUserId(stonks);
		const ownerName = stonksSelectedUserName(stonks);
		const latest = stonksLatestSnapshot(stonks);
		const editor = document.createElement("section");
		editor.className = "stonks-editor";
		const owner = document.createElement("div");
		owner.className = "stonks-portfolio-owner";
		owner.innerHTML = "<strong></strong><span></span>";
		owner.querySelector("strong").textContent = ownerName;
		owner.querySelector("span").textContent = latest
			? "Current save " + formatStonksTime(latest.createdAt)
			: "No saved portfolio";
		editor.appendChild(owner);
		const top = document.createElement("div");
		top.className = "stonks-editor-top";
		top.appendChild(stonksInput(stonksDraftCurrency, "snapshotCurrency", "text", "Currency"));
		const note = stonksInput(stonksDraftNote, "snapshotNote", "text", "Note");
		top.appendChild(note);
		editor.appendChild(top);

		const search = document.createElement("div");
		search.className = "stonks-quote-search";
		const searchInput = document.createElement("input");
		searchInput.type = "search";
		searchInput.value = stonksQuoteSearchText;
		searchInput.placeholder = "Search Yahoo ticker";
		searchInput.addEventListener("input", function() {
			stonksQuoteSearchText = searchInput.value;
		});
		searchInput.addEventListener("keydown", function(event) {
			if (event.key === "Enter") {
				event.preventDefault();
				runStonksQuoteSearch(searchInput.value);
			}
		});
		const searchButton = document.createElement("button");
		searchButton.type = "button";
		searchButton.className = "chip-button";
		searchButton.textContent = stonksQuoteSearchBusy ? "Searching..." : "Add instrument";
		searchButton.disabled = stonksQuoteSearchBusy;
		searchButton.addEventListener("click", function() {
			runStonksQuoteSearch(searchInput.value);
		});
		const manualButton = document.createElement("button");
		manualButton.type = "button";
		manualButton.className = "chip-button";
		manualButton.textContent = "Manual row";
		manualButton.addEventListener("click", function() {
			stonksCollectDraft(editor);
			const symbol = normalizeStonksSymbol(searchInput.value);
			stonksDraftPositions.push(stonksNormalizePosition({
				symbol: symbol,
				currency: stonksDraftCurrency || "USD",
				providerId: "manual",
				providerSymbol: symbol,
				quoteConfidence: 0.35
			}, stonksDraftCurrency));
			renderModernDialog();
		});
		search.append(searchInput, searchButton, manualButton);
		const providerLinks = stonksProviderLinks(stonksQuoteSearchText);
		if (providerLinks.length || stonksQuoteSearchError || stonksQuoteSuggestions.length) {
			const results = document.createElement("div");
			results.className = "stonks-quote-results";
			stonksQuoteSuggestions.forEach(function(suggestion) {
				const button = document.createElement("button");
				button.type = "button";
				button.className = "stonks-quote-result";
				button.innerHTML = "<strong></strong><span></span>";
				button.querySelector("strong").textContent = suggestion.symbol + " " + formatStonksMoney(suggestion.price, suggestion.currency);
				button.querySelector("span").textContent = [suggestion.displayName, suggestion.exchange, stonksSourceLabel(suggestion)].filter(Boolean).join(" / ");
				button.addEventListener("click", function() {
					stonksApplyQuoteSuggestion(suggestion);
				});
				results.appendChild(button);
			});
			if (stonksQuoteSearchError) {
				const error = document.createElement("span");
				error.className = "stonks-quote-error";
				error.textContent = stonksQuoteSearchError;
				results.appendChild(error);
			}
			providerLinks.forEach(function(link) {
				const anchor = document.createElement("a");
				anchor.className = "stonks-provider-link";
				anchor.href = link.url;
				anchor.target = "_blank";
				anchor.rel = "noreferrer";
				anchor.textContent = link.label;
				results.appendChild(anchor);
			});
			search.appendChild(results);
		}
		editor.appendChild(search);

		const table = document.createElement("div");
		table.className = "stonks-position-table";
		const head = document.createElement("div");
		head.className = "stonks-position-head";
		["Symbol", "Qty", "Price", "Value", "Currency", "Source", "Name", ""].forEach(function(label) {
			const cell = document.createElement("span");
			cell.textContent = label;
			head.appendChild(cell);
		});
		table.appendChild(head);
		stonksDraftPositions.forEach(function(position, index) {
			position = stonksNormalizePosition(position, stonksDraftCurrency);
			const row = document.createElement("div");
			row.className = "stonks-position-row";
			row.dataset.stonksProviderId = position.providerId || "manual";
			row.dataset.stonksProviderSymbol = position.providerSymbol || position.symbol || "";
			row.dataset.stonksExchange = position.exchange || "";
			row.dataset.stonksQuoteTime = String(position.quoteTime || 0);
			row.dataset.stonksQuoteSourceUrl = position.quoteSourceUrl || "";
			row.dataset.stonksQuoteConfidence = String(position.quoteConfidence || 0);
			row.appendChild(stonksInput(position.symbol, "symbol", "text", "RKLB"));
			row.appendChild(stonksInput(position.quantity, "quantity", "number"));
			row.appendChild(stonksInput(position.price, "price", "number"));
			row.appendChild(stonksInput(position.marketValue, "marketValue", "number"));
			row.appendChild(stonksInput(position.currency || stonksDraftCurrency, "currency"));
			const source = document.createElement("div");
			source.className = "stonks-source";
			const sourceLabel = document.createElement(position.quoteSourceUrl ? "a" : "span");
			sourceLabel.textContent = stonksSourceLabel(position);
			if (position.quoteSourceUrl) {
				sourceLabel.href = position.quoteSourceUrl;
				sourceLabel.target = "_blank";
				sourceLabel.rel = "noreferrer";
			}
			source.appendChild(sourceLabel);
			if (position.exchange) {
				const exchange = document.createElement("small");
				exchange.textContent = position.exchange;
				source.appendChild(exchange);
			}
			row.appendChild(source);
			row.appendChild(stonksInput(position.displayName, "displayName"));
			const remove = document.createElement("button");
			remove.type = "button";
			remove.className = "icon-button stonks-remove-row";
			remove.title = "Remove";
			remove.setAttribute("aria-label", "Remove");
			remove.textContent = "x";
			remove.addEventListener("click", function() {
				stonksDraftPositions.splice(index, 1);
				if (!stonksDraftPositions.length) {
					stonksDraftPositions.push(stonksEmptyPosition(stonksDraftCurrency || "USD"));
				}
				renderModernDialog();
			});
			row.appendChild(remove);
			row.addEventListener("input", function(event) {
				if (event.target.dataset.stonksField === "quantity" || event.target.dataset.stonksField === "price") {
					const quantity = Number(row.querySelector("[data-stonks-field='quantity']").value || 0);
					const price = Number(row.querySelector("[data-stonks-field='price']").value || 0);
					row.querySelector("[data-stonks-field='marketValue']").value = Number.isFinite(quantity * price) ? String(quantity * price) : "0";
				}
				const liveDraft = stonksCollectDraft(editor);
				liveDraft.registered = canEdit;
				liveDraft.userId = ownerUserId;
				updateStonksValidation(validationSummary, liveDraft, submit, total);
			});
			table.appendChild(row);
		});
		editor.appendChild(table);
		const actions = document.createElement("div");
		actions.className = "stonks-editor-actions";
		const add = document.createElement("button");
		add.type = "button";
		add.className = "chip-button";
		add.textContent = "Add position";
		add.addEventListener("click", function() {
			stonksCollectDraft(editor);
			stonksDraftPositions.push(stonksEmptyPosition(stonksDraftCurrency || "USD"));
			renderModernDialog();
		});
		const total = document.createElement("strong");
		total.className = "stonks-draft-total";
		total.textContent = formatStonksMoney(stonksDraftTotal(), stonksDraftCurrency);
		const submit = document.createElement("button");
		submit.type = "button";
		submit.className = "chip-button is-primary";
		submit.textContent = ownerUserId && ownerUserId !== Number(stonks.selfUserId || 0)
			? "Save for " + ownerName
			: "Save portfolio";
		submit.disabled = !canEdit;
		submit.addEventListener("click", function() {
			const draft = stonksCollectDraft(editor);
			draft.registered = canEdit;
			draft.userId = ownerUserId;
			if (stonksValidateDraft(draft).errors.length) {
				updateStonksValidation(validationSummary, draft, submit, total);
				return;
			}
			invokeModernDialogAction("savePortfolio", draft);
		});
		const clear = document.createElement("button");
		clear.type = "button";
		clear.className = "chip-button is-danger";
		clear.textContent = "Clear portfolio";
		clear.disabled = !canEdit || !(latest && stonksNumber(latest.totalValue) > 0);
		clear.addEventListener("click", function() {
			if (window.confirm("Clear portfolio for " + ownerName + "?")) {
				invokeModernDialogAction("clearPortfolio", {
					userId: ownerUserId,
					currency: stonksDraftCurrency || latest && latest.currency || "USD",
					note: "Portfolio cleared"
				});
			}
		});
		actions.append(add, total, clear, submit);
		editor.appendChild(actions);
		const validationSummary = document.createElement("div");
		const draft = { positions: stonksDraftPositions || [], currency: stonksDraftCurrency, registered: canEdit };
		updateStonksValidation(validationSummary, draft, submit, total);
		editor.appendChild(validationSummary);
		parent.appendChild(editor);
	}

	function appendStonksLedger(parent, stonks) {
		appendStonksEditor(parent, stonks);
		appendStonksHeatmap(parent, stonks);
	}

	function appendStonksAudit(parent, stonks) {
		const snapshots = Array.isArray(stonks.snapshots) ? stonks.snapshots : [];
		const ownerUserId = stonksSelectedUserId(stonks);
		const ownerName = stonksSelectedUserName(stonks);
		const canEdit = stonksCanEditPortfolio(stonks);
		const description = document.createElement("div");
		description.className = "stonks-leaderboard-description";
		description.textContent = "Portfolio saves are kept here because leaderboard periods compare the latest save with older saves.";
		const owner = document.createElement("span");
		owner.textContent = ownerName;
		description.appendChild(owner);
		parent.appendChild(description);
		const list = document.createElement("div");
		list.className = "stonks-list";
		snapshots.forEach(function(snapshot, index) {
			const previousSnapshot = snapshots[index + 1] || null;
			const details = document.createElement("details");
			details.className = "stonks-ledger-item";
			const summary = document.createElement("summary");
			const summaryText = document.createElement("span");
			summaryText.textContent = formatStonksMoney(snapshot.totalValue, snapshot.currency) + " · saved " + formatStonksTime(snapshot.createdAt);
			summary.appendChild(summaryText);
			if (canEdit && snapshot.snapshotId) {
				const remove = document.createElement("button");
				remove.type = "button";
				remove.className = "chip-button is-danger stonks-delete-snapshot";
				remove.textContent = "Delete";
				remove.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					if (window.confirm("Delete this portfolio snapshot for " + ownerName + "?")) {
						invokeModernDialogAction("deleteSnapshot", {
							snapshotId: snapshot.snapshotId,
							userId: snapshot.userId || ownerUserId
						});
					}
				});
				summary.appendChild(remove);
			}
			details.appendChild(summary);
			const changes = stonksSnapshotChanges(snapshot, previousSnapshot).slice(0, 10);
			const changeList = document.createElement("div");
			changeList.className = "stonks-audit-changes";
			changes.forEach(function(change) {
				const item = document.createElement("span");
				item.className = "stonks-audit-change";
				item.textContent = change;
				changeList.appendChild(item);
			});
			details.appendChild(changeList);
			(snapshot.positions || []).forEach(function(position) {
				const row = document.createElement("div");
				row.className = "stonks-ledger-position";
				const quote = stonksSourceLabel(position);
				row.textContent = [
					position.symbol,
					position.displayName,
					formatStonksMoney(position.marketValue, position.currency),
					quote
				].filter(Boolean).join(" · ");
				details.appendChild(row);
			});
			if (snapshot.note) {
				const note = document.createElement("p");
				note.className = "stonks-note";
				note.textContent = snapshot.note;
				details.appendChild(note);
			}
			if (!snapshot.positions || !snapshot.positions.length) {
				const empty = document.createElement("p");
				empty.className = "stonks-note";
				empty.textContent = snapshot.positionsRedacted ? "Positions redacted." : "No open positions.";
				details.appendChild(empty);
			}
			list.appendChild(details);
		});
		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No audit entries yet.";
			list.appendChild(empty);
		}
		parent.appendChild(list);
	}

	function appendStonksLeaderboard(parent, stonks) {
		const description = document.createElement("div");
		description.className = "stonks-leaderboard-description";
		description.textContent = stonks.leaderboardDescription
			|| "Portfolio return compares the latest saved portfolio value with the closest older saved value for this period.";
		if (stonks.leaderboardUpdatedAt) {
			const updated = document.createElement("span");
			updated.textContent = "Updated " + formatStonksTime(stonks.leaderboardUpdatedAt);
			description.appendChild(updated);
		}
		parent.appendChild(description);
		const list = document.createElement("div");
		list.className = "stonks-list";
		(stonks.leaderboard || []).forEach(function(row) {
			const item = document.createElement("div");
			item.className = "stonks-row";
			const main = document.createElement("div");
			main.innerHTML = "<strong></strong><span></span><small></small>";
			main.querySelector("strong").textContent = (row.insufficientHistory ? "" : row.rank + ". ") + row.userName;
			main.querySelector("span").textContent = "Latest save " + formatStonksTime(row.endSnapshotAt);
			main.querySelector("small").textContent = row.insufficientHistory
				? "Insufficient history for " + (row.period || stonks.selectedPeriod || "30d")
				: "Baseline " + formatStonksTime(row.startSnapshotAt);
			const percent = document.createElement("strong");
			percent.className = "stonks-return" + (row.insufficientHistory ? " is-muted" : (stonksNumber(row.returnPercent) >= 0 ? " is-positive" : " is-negative"));
			percent.textContent = row.insufficientHistory ? "Need baseline" : formatStonksPercent(row.returnPercent);
			const follow = document.createElement("button");
			follow.type = "button";
			follow.className = "chip-button";
			follow.textContent = row.followed ? "Unfollow" : "Follow";
			follow.disabled = !stonks.registered || row.userId === stonks.selfUserId;
			follow.addEventListener("click", function() {
				invokeModernDialogAction(row.followed ? "unfollow" : "follow", { userId: row.userId });
			});
			const actions = document.createElement("div");
			actions.className = "stonks-row-actions";
			if (stonks.canAdmin) {
				const view = document.createElement("button");
				view.type = "button";
				view.className = "chip-button";
				view.textContent = "View";
				view.addEventListener("click", function() {
					stonksActiveTab = "ledger";
					invokeModernDialogAction("selectUser", { userId: row.userId });
				});
				actions.appendChild(view);
			}
			actions.appendChild(follow);
			item.append(main, percent, actions);
			list.appendChild(item);
		});
		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No ranked portfolios for this period yet.";
			list.appendChild(empty);
		}
		parent.appendChild(list);
	}

	function appendStonksFollowing(parent, stonks) {
		const list = document.createElement("div");
		list.className = "stonks-list";
		(stonks.users || []).forEach(function(user) {
			if (user.userId === stonks.selfUserId) {
				return;
			}
			const item = document.createElement("div");
			item.className = "stonks-row";
			const name = document.createElement("strong");
			name.textContent = user.userName;
			const state = document.createElement("span");
			state.textContent = user.followed ? "Following" : "";
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button";
			button.textContent = user.followed ? "Unfollow" : "Follow";
			button.disabled = !stonks.registered;
			button.addEventListener("click", function() {
				invokeModernDialogAction(user.followed ? "unfollow" : "follow", { userId: user.userId });
			});
			item.append(name, state, button);
			list.appendChild(item);
		});
		parent.appendChild(list);
	}

	function appendStonksAdmin(parent, stonks) {
		const form = document.createElement("section");
		form.className = "stonks-admin";
		const enabled = document.createElement("label");
		enabled.className = "stonks-check";
		const enabledInput = document.createElement("input");
		enabledInput.type = "checkbox";
		enabledInput.checked = stonks.enabled !== false;
		enabled.append(enabledInput, document.createTextNode("Enabled"));
		const announcements = document.createElement("label");
		announcements.className = "stonks-check";
		const announcementsInput = document.createElement("input");
		announcementsInput.type = "checkbox";
		announcementsInput.checked = stonks.socialAnnouncementsEnabled !== false;
		announcements.append(announcementsInput, document.createTextNode("Social announcements"));
		const select = document.createElement("select");
		const none = document.createElement("option");
		none.value = "0";
		none.textContent = "Auto #stonks";
		select.appendChild(none);
		(stonks.textChannels || []).forEach(function(channel) {
			const option = document.createElement("option");
			option.value = String(channel.textChannelId || 0);
			option.textContent = "#" + (channel.name || channel.textChannelId);
			option.selected = channel.textChannelId === stonks.textChannelId;
			select.appendChild(option);
		});
		const save = document.createElement("button");
		save.type = "button";
		save.className = "chip-button is-primary";
		save.textContent = "Save";
		save.addEventListener("click", function() {
			invokeModernDialogAction("configure", {
				enabled: enabledInput.checked,
				socialAnnouncementsEnabled: announcementsInput.checked,
				textChannelId: Number(select.value || 0)
			});
		});
		form.append(enabled, announcements, select, save);
		parent.appendChild(form);

		const manager = document.createElement("section");
		manager.className = "stonks-admin stonks-admin-manager";
		const selectedUserId = stonksSelectedUserId(stonks);
		const userSelect = document.createElement("select");
		(stonks.users || []).forEach(function(user) {
			const option = document.createElement("option");
			option.value = String(user.userId || 0);
			option.textContent = user.userName || ("user " + user.userId);
			option.selected = Number(user.userId || 0) === selectedUserId;
			userSelect.appendChild(option);
		});
		if (!userSelect.children.length) {
			const option = document.createElement("option");
			option.value = "0";
			option.textContent = "No registered users";
			userSelect.appendChild(option);
		}
		const view = document.createElement("button");
		view.type = "button";
		view.className = "chip-button";
		view.textContent = "View portfolio";
		view.disabled = Number(userSelect.value || 0) <= 0;
		view.addEventListener("click", function() {
			stonksActiveTab = "ledger";
			invokeModernDialogAction("selectUser", { userId: Number(userSelect.value || 0) });
		});
		const audit = document.createElement("button");
		audit.type = "button";
		audit.className = "chip-button";
		audit.textContent = "View audit";
		audit.disabled = Number(userSelect.value || 0) <= 0;
		audit.addEventListener("click", function() {
			stonksActiveTab = "audit";
			invokeModernDialogAction("selectUser", { userId: Number(userSelect.value || 0) });
		});
		const clear = document.createElement("button");
		clear.type = "button";
		clear.className = "chip-button is-danger";
		clear.textContent = "Clear portfolio";
		clear.disabled = Number(userSelect.value || 0) <= 0;
		clear.addEventListener("click", function() {
			const targetName = userSelect.options[userSelect.selectedIndex]
				? userSelect.options[userSelect.selectedIndex].textContent
				: "selected user";
			if (window.confirm("Clear portfolio for " + targetName + "?")) {
				invokeModernDialogAction("clearPortfolio", {
					userId: Number(userSelect.value || 0),
					currency: stonksLatestSnapshot(stonks) && stonksLatestSnapshot(stonks).currency || "USD",
					note: "Portfolio cleared by admin"
				});
			}
		});
		manager.append(userSelect, view, audit, clear);
		parent.appendChild(manager);
	}

	function renderStonksDialog(dialog) {
		const stonks = dialog.stonks || {};
		ensureStonksDraft(stonks);
		refs.modernDialogEyebrow.textContent = "Stonks";
		appendStonksStatus(refs.modernDialogBody, stonks);
		if (!stonks.supported || stonks.enabled === false) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = stonks.error || "Stonks is unavailable on this server.";
			refs.modernDialogBody.appendChild(empty);
			return;
		}
		if (!stonks.registered) {
			const needed = document.createElement("section");
			needed.className = "stonks-register-needed";
			const text = document.createElement("strong");
			text.textContent = "Registered user required";
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button is-primary";
			button.textContent = "Register";
			button.addEventListener("click", function() {
				invokeModernDialogAction("register", {});
			});
			needed.append(text, button);
			refs.modernDialogBody.appendChild(needed);
		}
		appendStonksTabs(refs.modernDialogBody, stonks);
		const panel = document.createElement("section");
		panel.className = "stonks-panel";
		if (stonksActiveTab === "ledger") {
			appendStonksLedger(panel, stonks);
		} else if (stonksActiveTab === "leaderboard") {
			appendStonksOverview(panel, stonks);
			appendStonksLeaderboard(panel, stonks);
		} else if (stonksActiveTab === "following") {
			appendStonksFollowing(panel, stonks);
		} else if (stonksActiveTab === "audit") {
			appendStonksAudit(panel, stonks);
		} else if (stonksActiveTab === "admin" && stonks.canAdmin) {
			appendStonksAdmin(panel, stonks);
		} else {
			appendStonksOverview(panel, stonks);
			appendStonksEditor(panel, stonks);
		}
		refs.modernDialogBody.appendChild(panel);
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

		if (dialog.kind === "stonks") {
			renderStonksDialog(dialog);
			syncAudioInputMeterTimer();
			modernDialogRenderedOpen = true;
			if (focusState && restoreModernDialogFocus(focusState)) {
				return;
			}
			if (opening || !activeInDialog) {
				focusFirstModernDialogControl();
			}
			return;
		}

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
		if (target && typeof target.closest === "function" && target.closest(".window-actions")) {
			refs.modernHeader.classList.remove("menu-is-peeked");
			scheduleTransientMenuDismiss();
			return;
		}

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
		const stonks = app.stonks || {};
		const stonksAvailable = !!stonks.supported;
		refs.stonksButton.disabled = !stonksAvailable;
		refs.stonksButton.classList.toggle("is-disabled", !stonksAvailable);

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
		renderStonksChatHeader(snapshot);
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
		snapshot.timelineMode = String(patch.timelineMode || "").trim() === "linkDense" ? "linkDense" : "normal";
		clearPreviewHydrationState();
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
		refs.stonksButton.addEventListener("click", function() {
			if (!refs.stonksButton.disabled) {
				notifyBridge("openModernDialog", "stonks", {});
			}
		});
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
			schedulePreviewEmbedFrameSizeSync();
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
