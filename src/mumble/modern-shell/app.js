(function() {
	"use strict";

	function modernBuildFlag(name, fallback) {
		const flags = window.__mumbleModernBuildFlags || {};
		if (Object.prototype.hasOwnProperty.call(flags, name)) {
			return !!flags[name];
		}
		return fallback;
	}

	function modernAutomationEnabled() {
		return modernBuildFlag("automation", true);
	}

	function modernMockupsEnabled() {
		return modernBuildFlag("mockups", true);
	}

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let lastRenderedMessageCount = 0;
	let lastRenderedTailKey = "";
	let lastScopeToken = "";
	let openMenuId = null;
	let openMenuPinned = false;
	let railCollapsed = true;
	let railOpenedByUser = false;
	let compactViewport = false;
	let contextMenuState = null;
	let nativeContextMenuCounter = 0;
	let nativeContextMenuActions = {};
	let nativeContextMenuKinds = {};
	let activeNativeContextMenuToken = "";
	let activeNativeContextMenuKind = "";
	let automationBridgeCalls = [];
	let automationLastCopiedText = "";
	let automationCopyCount = 0;
	let automationLastMenuProbe = {};
	let unreadDetachedMessages = 0;
	let selfMenuOpen = false;
	let openReactionPickerMessageId = null;
	let reactionPickerScrollClosePausedUntil = 0;
	let keepMessageListPinnedToBottom = true;
	let pendingBottomPinFrames = 0;
	let pendingBottomPinHandle = 0;
	let messageListScrollSyncFrame = 0;
	let footerAlignmentFrame = 0;
	let stableFooterRowHeight = 0;
	let railLayoutFrame = 0;
	let railSelectionRevealFrame = 0;
	let pendingActiveRailReveal = false;
	let snapshotRenderFrame = 0;
	let menuDismissTimer = 0;
	let appMenuOpen = false;
	let directMessageTrayOpen = false;
	let directMessageDrafts = {};
	let directMessageWindowUiState = {};
	let noteExpanded = false;
	let railNoteAutoCollapsed = false;
	let lastActiveRailToken = "";
	let lastMotdSignature = "";
	let motdExpansionTouched = false;
	let conversationMotdHiddenForHistory = false;
	let messageListMutationObserver = null;
	let dragState = null;
	let imageViewerDragState = null;
	let imageViewerDragFrame = 0;
	let imageViewerResizeObserver = null;
	let footerAlignmentResizeObserver = null;
	let cachedServerLogElement = null;
	let cachedServerLogRevision = "";
	let liveSnapshot = {};
	let walkthroughSnapshotCache = null;
	let mockupSettingsActivePage = "";
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
	let previewHydrationScrollIdleTimer = 0;
	let previewHydrationPausedUntil = 0;
	let messageListScrollActiveUntil = 0;
	let messageListEnterTimer = 0;
	let pendingPreviewHydrationIds = new Set();
	let requestedPreviewHydrationIds = new Set();
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let modernDialogAdvancedPages = {};
	let modernDialogPendingFieldUpdates = {};
	let modernDialogLocalFieldValues = {};
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
	let modernDialogReturnFocus = null;
	let modernDialogSelectState = null;
	let detachedModernDialogUiTweaks = null;
	let modernDialogLastRenderKey = "";
	let stonksActiveTab = "market";
	let stonksFocusIndex = 0;
	let stonksLocalPins = null;
	let stonksDraftPositions = null;
	let stonksDraftNote = "";
	let stonksDraftCurrency = "USD";
	let stonksDraftKey = "";
	let stonksQuoteSearchText = "";
	let stonksQuoteSearchBusy = false;
	let stonksQuoteSearchError = "";
	let stonksQuoteSuggestions = [];
	let stonksQuoteSearchRequestId = "";
	let stonksPopularQuoteCache = {};
	let stonksPopularQuoteRequests = {};
	let stonksVisibleRefreshTimer = 0;
	let stonksTickerScrollFrame = 0;
	let stonksTickerRenderSignature = "";
	let stonksEmptyStateRefreshAt = 0;
	let stonksPendingConfirm = null;
	let stonksPendingConfirmFocus = false;
	let messageSearchOpen = false;
	let messageSearchText = "";
	let motdDismissedSignature = "";
	let motdLastSeenSignature = "";
	let toastStack = null;
	let toastTimers = {};

	const imageViewerStorageKey = "mumble-modern-image-viewer";
	const imageViewerMinWidth = 280;
	const imageViewerMinHeight = 220;
	const imageViewerViewportMargin = 12;
	const compactRailBreakpointPx = 940;
	const reactionPickerScrollCloseGraceMs = 900;
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
	const previewHydrationDebounceMs = 120;
	const previewHydrationScrollIdleMs = 280;
	const previewHydrationTimelineSettleMs = 650;
	const previewHydrationViewportMarginPx = 220;
	const previewHydrationBatchSize = 3;
	const stonksVisibleRefreshMs = 5 * 60 * 1000;
	const stonksEmptyStateRefreshMs = 15 * 1000;
	const stonksPopularQuoteStaleMs = 5 * 60 * 1000;
	const serverIdentityImageMaxFileBytes = 4 * 1024 * 1024;
	const serverIdentityImageAccept = "image/*,.png,.jpg,.jpeg,.jpe,.jfif,.pjpeg,.pjp,.webp,.gif,.bmp,.dib,.tif,.tiff,.ico,.cur,.icns,.avif,.heic,.heif,.jxl,.svg,.svgz,.tga,.tpic,.pbm,.pgm,.ppm,.wbmp,.xbm,.xpm";
	const serverIdentityImageExtensionMime = {
		avif: "image/avif",
		bmp: "image/bmp",
		cur: "image/x-icon",
		dib: "image/bmp",
		gif: "image/gif",
		heic: "image/heic",
		heif: "image/heif",
		icns: "image/icns",
		ico: "image/x-icon",
		jpe: "image/jpeg",
		jpeg: "image/jpeg",
		jfif: "image/jpeg",
		jpg: "image/jpeg",
		jxl: "image/jxl",
		pbm: "image/x-portable-bitmap",
		pgm: "image/x-portable-graymap",
		png: "image/png",
		pjp: "image/jpeg",
		pjpeg: "image/jpeg",
		ppm: "image/x-portable-pixmap",
		svg: "image/svg+xml",
		svgz: "image/svg+xml",
		tga: "image/x-tga",
		tif: "image/tiff",
		tiff: "image/tiff",
		tpic: "image/x-tga",
		wbmp: "image/vnd.wap.wbmp",
		webp: "image/webp",
		xbm: "image/x-xbitmap",
		xpm: "image/x-xpixmap"
	};
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
		brandBadge: document.getElementById("brand-badge"),
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
		textRoomList: document.getElementById("text-room-list"),
		voiceRoomList: document.getElementById("voice-room-list"),
		scopeTitle: document.getElementById("scope-title"),
		scopeDescription: document.getElementById("scope-description"),
		scopeBanner: document.getElementById("scope-banner"),
		stonksLeaderboardHeader: document.getElementById("stonks-leaderboard-header"),
		conversationMeta: document.getElementById("conversation-meta"),
		voicePresenceStack: document.getElementById("voice-presence-stack"),
		msgSearchButton: document.getElementById("msg-search-button"),
		msgSearchBar: document.getElementById("msg-search-bar"),
		msgSearchInput: document.getElementById("msg-search-input"),
		msgSearchCount: document.getElementById("msg-search-count"),
		msgSearchClose: document.getElementById("msg-search-close"),
		motdToggle: document.getElementById("motd-toggle"),
		motdBanner: document.getElementById("motd-banner"),
		motdBody: document.getElementById("motd-body"),
		motdCollapse: document.getElementById("motd-collapse"),
		motdDismiss: document.getElementById("motd-dismiss"),
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
		directMessageButton: null,
		directMessageBadge: null,
		directMessageSection: null,
		directMessageList: null,
		directMessageCount: null,
		directMessageTray: null,
		directMessageDock: null,
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
		const widths = [window.innerWidth || compactRailBreakpointPx + 1];
		const deviceScale = Math.max(window.devicePixelRatio || 1, 1);
		if (window.mumbleHostViewportWidth > 0) {
			widths.push(window.mumbleHostViewportWidth);
		}
		if (window.outerWidth > 0) {
			widths.push(window.outerWidth);
		}
		if (window.visualViewport && window.visualViewport.width > 0) {
			widths.push(window.visualViewport.width);
		}
		if (deviceScale > 1) {
			widths.push((window.innerWidth || compactRailBreakpointPx + 1) / deviceScale);
			if (window.outerWidth > 0) {
				widths.push(window.outerWidth / deviceScale);
			}
			if (window.visualViewport && window.visualViewport.width > 0) {
				widths.push(window.visualViewport.width / deviceScale);
			}
		}
		return Math.min.apply(Math, widths) <= compactRailBreakpointPx;
	}

	function applyRailPresentation() {
		const railOpen = !railCollapsed;
		refs.appShell.classList.toggle("is-compact-layout", compactViewport);
		refs.appShell.classList.toggle("rail-is-collapsed", railCollapsed);
		refs.appShell.classList.toggle("rail-user-opened", compactViewport && railOpen && railOpenedByUser);
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
		if (railCollapsed) {
			railOpenedByUser = false;
		}
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
		const wasCompactViewport = compactViewport;
		if (force || nextCompactViewport !== compactViewport) {
			compactViewport = nextCompactViewport;
			railCollapsed = compactViewport ? true : false;
			railOpenedByUser = false;
			if (wasCompactViewport && !compactViewport) {
				resetUtilityRailScroll();
			}
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

	function countRailRoomRows(listElement) {
		return listElement ? listElement.querySelectorAll(".rail-row").length : 0;
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

	function resetUtilityRailScroll() {
		if (!refs.utilityScroll) {
			return;
		}

		if (refs.utilityScroll.scrollTop > 0) {
			refs.utilityScroll.scrollTop = 0;
		}
		syncRailOverflowState();
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

	function trackActiveRailTokenForReveal(renderedActiveRailToken) {
		if (!renderedActiveRailToken) {
			lastActiveRailToken = "";
			return;
		}

		if (!lastActiveRailToken) {
			lastActiveRailToken = renderedActiveRailToken;
			return;
		}

		if (renderedActiveRailToken !== lastActiveRailToken) {
			lastActiveRailToken = renderedActiveRailToken;
			pendingActiveRailReveal = true;
		}
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
			measureRailSection(voiceSection, refs.voiceRoomList, countRailRoomRows(refs.voiceRoomList)),
			measureRailSection(textSection, refs.textRoomList, countRailRoomRows(refs.textRoomList))
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
		const args = Array.prototype.slice.call(arguments, 1);
		if (!modernBridge || typeof modernBridge[method] !== "function") {
			return handleMockupBridgeCall(method, args);
		}

		if (modernAutomationEnabled() && method !== "openNativeContextMenu" && method !== "closeNativeContextMenu") {
			automationBridgeCalls.push({
				method: String(method || ""),
				args: args.map(function(arg) {
					if (arg === null || arg === undefined || typeof arg === "string"
							|| typeof arg === "number" || typeof arg === "boolean") {
						return arg;
					}
					return String(arg);
				}),
				timeMs: Date.now()
			});
			if (automationBridgeCalls.length > 32) {
				automationBridgeCalls = automationBridgeCalls.slice(-32);
			}
		}
		try {
			modernBridge[method].apply(modernBridge, args);
			return true;
		} catch (error) {
			console.warn("Modern bridge call failed:", method, error);
			return false;
		}
	}

	/* MUMBLE_MODERN_MOCKUPS_BEGIN */
	function mockupBridgeFallbackEnabled() {
		return !modernBridge && walkthroughAllowedForLocation() && requestedWalkthroughMode() === "mockup";
	}

	function mockupDialogNameForAppAction(actionId) {
		const id = String(actionId || "").trim();
		const aliases = {
			"server.information": "server-information",
			"server.search": "search",
			"server.connect": "connect",
			"server.disconnect": "disconnect",
			"server.createRoom": "create-room",
			"server.tokens": "tokens",
			"server.users": "registered-users",
			"server.bans": "ban-list",
			"server.acl": "acl",
			"server.settings": "server-settings",
			"room.create": "create-room",
			"room.acl": "acl",
			"room.unlink": "unlink-room",
			"room.unlinkAll": "unlink-all-rooms",
			"room.remove": "remove-room",
			"participant.info": "member-user-information",
			"participant.comment": "member-comment",
			"participant.nickname": "local-nickname",
			"participant.history": "grant-history",
			"participant.kick": "kick-user",
			"participant.ban": "ban-user",
			"userInfo": "member-user-information",
			"commentView": "member-comment",
			"localNickname": "local-nickname",
			"grantChatHistory": "grant-history",
			"kick": "kick-user",
			"ban": "ban-user",
			"screenShare.settings": "settings",
			"stonks.open": "stonks",
			"configure.settings": "settings",
			"configure.screenShare": "settings",
			"configure.audioWizard": "settings",
			"configure.minimal": "settings",
			"configure.hideFrame": "settings",
			"configure.certificate": "certificate",
			"self.info": "self-user-information",
			"self.comment": "self-comment",
			"self.avatar": "avatar",
			"self.resetAvatar": "self-reset-avatar",
			"self.register": "register",
			"self.audioStats": "audio-statistics",
			"self.voiceRecorder": "voice-recorder",
			"help.update": "update",
			"help.versionCheck": "update",
			"help.feedback": "feedback",
			"help.help": "help",
			"help.whatsThis": "help",
			"about.openMumble": "about",
			"about.openQt": "about-qt",
			"app.quit": "quit"
		};
		return aliases[id] || "";
	}

	function mockupSettingsPageForAction(actionId) {
		const id = String(actionId || "").trim();
		const pages = {
			"screenShare.settings": "screenShare",
			"screenShare.start": "screenShare",
			"room.shareScreen": "screenShare",
			"configure.screenShare": "screenShare",
			"configure.audioWizard": "audioInput",
			"configure.minimal": "ui",
			"configure.hideFrame": "ui"
		};
		return pages[id] || "";
	}

	function openMockupModernDialog(name, options) {
		if (!mockupBridgeFallbackEnabled()) {
			return false;
		}

		const opts = options || {};
		if (opts.settingsPage) {
			mockupSettingsActivePage = String(opts.settingsPage || "");
		}
		const dialog = mockupWalkthroughDialogState(name);
		if (!dialog) {
			return false;
		}

		hideAppMenu();
		hideSelfMenu();
		hideContextMenu();
		hideDirectMessageTray();
		closeTopMenu();
		stonksPendingConfirm = null;
		stonksPendingConfirmFocus = false;
		modernDialogPendingFieldUpdates = {};
		modernDialogLocalFieldValues = {};
		modernDialogReturnFocus = document.activeElement || null;
		syncModernDialogState(dialog);
		return true;
	}

	function openMockupDialogForAction(actionId) {
		const id = String(actionId || "").trim();
		const dialogName = mockupDialogNameForAppAction(id);
		if (!dialogName) {
			return false;
		}

		return openMockupModernDialog(dialogName, {
			settingsPage: mockupSettingsPageForAction(id)
		});
	}

	function mockupScopeAlias(scopeToken) {
		const raw = String(scopeToken || "").trim();
		const key = raw.toLowerCase().replace(/^#/, "");
		const aliases = {
			general: "text:general",
			clips: "text:clips",
			stonks: "text:stonks",
			rules: "text:rules",
			valorant: "voice:valorant",
			lobby: "voice:lobby",
			minecraft: "voice:minecraft"
		};
		return aliases[key] || raw;
	}

	function selectMockupScope(scopeToken) {
		const token = mockupScopeAlias(scopeToken);
		if (!token) {
			return false;
		}

		const snapshot = getSnapshot();
		if (isDirectMessageScopeToken(token)) {
			return openMockupDirectMessage(directMessageSessionFromToken(token), true);
		}

		const normalizedToken = token.toLowerCase();
		const allRooms = [].concat(snapshot.voiceRooms || [], snapshot.textRooms || []);
		let room = null;
		allRooms.forEach(function(candidate) {
			if (String(candidate && candidate.token || "").toLowerCase() === normalizedToken) {
				room = candidate;
			}
			if (candidate) {
				candidate.selected = false;
			}
		});
		if (!room) {
			return false;
		}

		room.selected = true;
		if (String(room.token || "").indexOf("voice:") === 0) {
			room.joined = true;
		}
		const isText = String(room.token || "").indexOf("text:") === 0;
		const label = isText ? "#" + String(room.label || "").replace(/^#/, "") : (room.label || "Room");
		snapshot.activeScope = {
			scopeToken: room.token,
			kindLabel: room.kindLabel || (isText ? "Text room" : "Voice room"),
			label: label,
			description: room.description || "",
			meta: isText
				? ["Text room", "Persistent history", (room.unreadCount || 0) + " unread"]
				: ["Voice room", "Persistent history", "28 ms"],
			canLoadOlder: true,
			canMarkRead: true,
			canSend: true,
			canAttachImages: true,
			composerPlaceholder: "Message " + label,
			composerHint: "Persistent room history stays with " + label + ".",
			screenShare: room.screenShare || null,
			emptyCopy: "No messages in " + label + " yet."
		};
		snapshot.messages = walkthroughRoomMessages(snapshot.activeScope);
		if (!isText) {
			snapshot.voicePresence = room.participants || [];
		}
		hideDirectMessageTray();
		scheduleSnapshotRender();
		return true;
	}

	function markMockupRead() {
		const snapshot = getSnapshot();
		const activeToken = String(snapshot.activeScope && snapshot.activeScope.scopeToken || "");
		(snapshot.textRooms || []).forEach(function(room) {
			if (String(room && room.token || "") === activeToken) {
				room.unreadCount = 0;
			}
		});
		if (snapshot.activeScope) {
			snapshot.activeScope.canMarkRead = false;
		}
		scheduleSnapshotRender();
		return true;
	}

	function mockupSetSelfPresence(actionId) {
		const id = String(actionId || "");
		const snapshot = getSnapshot();
		const state = {
			"presence.online": ["grinding ranked", "success"],
			"presence.away": ["Away", "warning"],
			"presence.busy": ["Do not disturb", "danger"]
		}[id];
		if (!state) {
			return false;
		}

		if (snapshot.app) {
			snapshot.app.selfStatusLabel = state[0];
			snapshot.app.selfStatusTone = state[1];
			if (snapshot.app.selfMenu) {
				snapshot.app.selfMenu.statusLabel = state[0];
				snapshot.app.selfMenu.statusTone = state[1];
				(snapshot.app.selfMenu.presence || []).forEach(function(item) {
					if (item) {
						item.checked = String(item.id || "") === id;
					}
				});
			}
		}
		scheduleSnapshotRender();
		return true;
	}

	function mockupSetScreenShare(scopeToken, active) {
		const snapshot = getSnapshot();
		const token = scopeToken || (snapshot.activeScope && snapshot.activeScope.scopeToken) || "voice:lobby";
		const rooms = snapshot.voiceRooms || [];
		let share = null;
		rooms.forEach(function(room) {
			if (String(room && room.token || "") === String(token)) {
				if (!room.screenShare) {
					room.screenShare = {};
				}
				share = room.screenShare;
			}
		});
		if (!share && snapshot.activeScope) {
			if (!snapshot.activeScope.screenShare) {
				snapshot.activeScope.screenShare = {};
			}
			share = snapshot.activeScope.screenShare;
		}
		if (!share) {
			return false;
		}

		share.visible = true;
		share.available = true;
		share.mode = active ? "publishing" : "available";
		share.ownerLabel = active ? "Your screen share" : "Lobby screen share";
		share.statusLabel = active ? "Sharing" : "Available";
		share.statusTone = active ? "warning" : "success";
		share.badgeLabel = active ? "You" : "Live";
		share.badgeTone = active ? "warning" : "success";
		share.primaryActionId = active ? "screenShare.stop" : "screenShare.start";
		share.primaryLabel = active ? "Stop sharing" : "Share screen";
		share.primaryTone = active ? "danger" : "primary";
		share.primaryHint = active ? "Stop sharing your screen" : "Share your screen to Lobby";
		share.primaryEnabled = true;
		share.note = active ? "Mockup sharing state is active for this presentation." : share.note;
		if (snapshot.activeScope && String(snapshot.activeScope.scopeToken || "") === String(token)) {
			snapshot.activeScope.screenShare = share;
		}
		scheduleSnapshotRender();
		showToast({
			kind: active ? "success" : "info",
			title: "Screen sharing",
			message: active ? "Mockup sharing state is now visible." : "Mockup sharing state was reset.",
			timeoutMs: 2200
		});
		return true;
	}

	function mockupDirectMessageConversation(session) {
		const snapshot = getSnapshot();
		const state = snapshot.app && snapshot.app.directMessages || {};
		const conversations = Array.isArray(state.conversations) ? state.conversations : [];
		const requestedSession = Number(session || 0);
		return conversations.find(function(conversation) {
			return directMessageSessionValue(conversation) === requestedSession;
		}) || conversations.find(function(conversation) {
			return String(conversation && conversation.label || "").toLowerCase() === "kira";
		}) || conversations[0] || null;
	}

	function openMockupDirectMessage(session, selectScope) {
		const conversation = mockupDirectMessageConversation(session);
		if (!conversation) {
			return false;
		}
		const targetSession = directMessageSessionValue(conversation);
		updateLocalDirectMessageState(function(state, snapshot) {
			const windowState = ensureLocalDirectMessageWindow(state, targetSession) || conversation;
			conversation.open = true;
			conversation.unreadCount = 0;
			if (selectScope === false) {
				return;
			}
			(snapshot.voiceRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
			(snapshot.textRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
			snapshot.activeScope = directMessageActiveScope(windowState);
			snapshot.messages = [];
			snapshot.voicePresence = [snapshot.participants && snapshot.participants[0], snapshot.participants && snapshot.participants[1]].filter(Boolean);
		});
		hideDirectMessageTray();
		scheduleSnapshotRender();
		return true;
	}

	function selectMockupParticipantRoom(session) {
		const targetSession = Number(session || 0);
		if (!targetSession) {
			return false;
		}
		const snapshot = getSnapshot();
		const room = (snapshot.voiceRooms || []).find(function(candidate) {
			return (candidate && candidate.participants || []).some(function(participant) {
				return Number(participant && participant.session || 0) === targetSession;
			});
		});
		return room ? selectMockupScope(room.token) : false;
	}

	function handleMockupAppAction(actionId) {
		const id = String(actionId || "").trim();
		if (!id) {
			return false;
		}
		if (mockupSetSelfPresence(id)) {
			return true;
		}
		if (id === "stonks.refreshVisible" || id === "motd.markSeen") {
			return true;
		}
		if (id === "motd.show" || id === "motd.restore") {
			const snapshot = getSnapshot();
			if (snapshot.app) {
				snapshot.app.motdDismissedSignature = "";
				snapshot.app.motdExpanded = true;
			}
			renderNote(snapshot.app || {}, snapshot.activeScope || {}, snapshot.messages || []);
			return true;
		}
		if (id === "motd.hide" || id === "motd.dismiss") {
			const snapshot = getSnapshot();
			if (snapshot.app) {
				snapshot.app.motdDismissedSignature = motdContentSignature(snapshot.app.motdHtml || "");
				snapshot.app.motdExpanded = false;
			}
			renderNote(snapshot.app || {}, snapshot.activeScope || {}, snapshot.messages || []);
			return true;
		}
		if (id === "server.favorite" || id === "room.muteNotifications" || id === "room.copyInvite") {
			showToast({ title: "Mockup", message: "Action state captured for presentation.", timeoutMs: 1800 });
			return true;
		}
		return openMockupDialogForAction(id);
	}

	function handleMockupScopeAction(scopeToken, actionId) {
		const id = String(actionId || "").trim();
		if (id === "room.openChat") {
			return selectMockupScope(scopeToken);
		}
		if (id === "room.markRead") {
			return markMockupRead();
		}
		if (id === "room.copyInvite" || id === "room.muteNotifications") {
			showToast({ title: "Mockup", message: "Room action state captured for presentation.", timeoutMs: 1800 });
			return true;
		}
		if (id === "room.shareScreen" || id === "screenShare.start") {
			return mockupSetScreenShare(scopeToken, true);
		}
		if (id === "screenShare.stop") {
			return mockupSetScreenShare(scopeToken, false);
		}
		if (openMockupDialogForAction(id)) {
			return true;
		}
		return handleMockupAppAction(id);
	}

	function handleMockupParticipantAction(session, actionId) {
		const id = String(actionId || "").trim();
		if (id === "participant.message" || id === "textMessage" || id === "openMessage") {
			return openMockupDirectMessage(session, true);
		}
		if (id === "join" || id === "joinRoom") {
			return selectMockupParticipantRoom(session);
		}
		return openMockupDialogForAction(id);
	}

	function mockupModernDialogAction(actionId, payload) {
		if (!mockupBridgeFallbackEnabled()) {
			return false;
		}

		const id = String(actionId || "");
		const dialog = modernDialogState || {};
		if (id === "selectPage" && dialog.kind === "settings") {
			mockupSettingsActivePage = String(payload && payload.pageId || "");
			return openMockupModernDialog("settings");
		}
		if (openMockupDialogForAction(id)) {
			return true;
		}
		if (id === "screenShare.diagnostics") {
			showToast({ title: "Screen sharing", message: "Diagnostics are represented in the mockup settings.", timeoutMs: 2200 });
			return true;
		}

		const action = (dialog.actions || []).find(function(candidate) {
			return candidate && String(candidate.id || "") === id;
		});
		if (id === "close" || id === "cancel" || id === "done" || id === "ok"
				|| (action && action.closesDialog !== false)) {
			closeModernDialog();
		}
		return true;
	}

	function handleMockupBridgeCall(method, args) {
		if (!mockupBridgeFallbackEnabled()) {
			return false;
		}

		const methodName = String(method || "");
		const bridgeArgs = args || [];
		if (methodName === "openModernDialog") {
			return openMockupModernDialog(bridgeArgs[0]);
		}
		if (methodName === "invokeModernDialogAction") {
			return mockupModernDialogAction(bridgeArgs[1], bridgeArgs[2] || {});
		}
		if (methodName === "invokeAppAction" || methodName === "invokeAppActionPayload") {
			return handleMockupAppAction(bridgeArgs[0]);
		}
		if (methodName === "invokeScopeAction") {
			return handleMockupScopeAction(bridgeArgs[0], bridgeArgs[1]);
		}
		if (methodName === "invokeParticipantAction") {
			return handleMockupParticipantAction(bridgeArgs[0], bridgeArgs[1]);
		}
		if (methodName === "messageParticipant" || methodName === "openDirectMessage") {
			return openMockupDirectMessage(bridgeArgs[0], true);
		}
		if (methodName === "joinParticipant") {
			return selectMockupParticipantRoom(bridgeArgs[0]);
		}
		if (methodName === "joinVoiceChannel" || methodName === "selectScope") {
			return selectMockupScope(bridgeArgs[0]);
		}
		if (methodName === "markRead" || methodName === "markDirectMessageRead") {
			return markMockupRead();
		}
		if (methodName === "loadOlderHistory") {
			showToast({ title: "Mockup", message: "Older history is represented in this presentation snapshot.", timeoutMs: 1800 });
			return true;
		}
		if (methodName === "openImagePicker" || methodName === "attachClipboardImage"
				|| methodName === "cancelReply" || methodName === "toggleLayout"
				|| methodName === "toggleSelfMute" || methodName === "toggleSelfDeaf") {
			showToast({ title: "Mockup", message: "Control state captured for presentation.", timeoutMs: 1800 });
			return true;
		}
		return false;
	}

	function ensureToastStack() {
		if (toastStack && document.body.contains(toastStack)) {
			return toastStack;
		}

		toastStack = document.createElement("div");
		toastStack.className = "toast-stack";
		toastStack.setAttribute("aria-live", "polite");
		toastStack.setAttribute("aria-relevant", "additions");
		document.body.appendChild(toastStack);
		return toastStack;
	}

	function removeToast(id) {
		id = String(id || "");
		if (!id) {
			return;
		}
		if (toastTimers[id]) {
			clearTimeout(toastTimers[id]);
			delete toastTimers[id];
		}
		if (!toastStack) {
			return;
		}
		const node = toastStack.querySelector("[data-toast-id=\"" + id.replace(/"/g, "\\\"") + "\"]");
		if (node) {
			node.remove();
		}
	}

	function showToast(payload) {
		const toast = payload && typeof payload === "object" ? payload : {};
		const id = String(toast.id || ("toast-" + Date.now() + "-" + Math.random().toString(36).slice(2)));
		const kind = String(toast.kind || "info").trim().toLowerCase() || "info";
		const title = String(toast.title || "");
		const message = String(toast.message || "");
		const timeoutMs = Math.max(0, Number(toast.timeoutMs || 4500));
		const stack = ensureToastStack();

		removeToast(id);
		const node = document.createElement("section");
		node.className = "toast is-" + kind;
		node.dataset.toastId = id;
		node.setAttribute("role", kind === "error" ? "alert" : "status");

		const copy = document.createElement("div");
		copy.className = "toast-copy";
		if (title) {
			const titleElement = document.createElement("p");
			titleElement.className = "toast-title";
			titleElement.textContent = title;
			copy.appendChild(titleElement);
		}
		if (message) {
			const messageElement = document.createElement("p");
			messageElement.className = "toast-message";
			messageElement.textContent = message;
			copy.appendChild(messageElement);
		}
		node.appendChild(copy);

		if (toast.actionId && toast.actionLabel) {
			const action = document.createElement("button");
			action.type = "button";
			action.className = "toast-action";
			action.textContent = String(toast.actionLabel);
			action.addEventListener("click", function() {
				notifyBridge("invokeAppAction", String(toast.actionId));
				removeToast(id);
			});
			node.appendChild(action);
		}

		const close = document.createElement("button");
		close.type = "button";
		close.className = "toast-close";
		close.textContent = "x";
		close.setAttribute("aria-label", "Dismiss notification");
		close.addEventListener("click", function() {
			removeToast(id);
		});
		node.appendChild(close);
		stack.appendChild(node);

		if (timeoutMs > 0) {
			toastTimers[id] = setTimeout(function() {
				removeToast(id);
			}, timeoutMs);
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
							if (modernBridge.toastRequested
									&& typeof modernBridge.toastRequested.connect === "function") {
								modernBridge.toastRequested.connect(showToast);
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

	function walkthroughQueryValue(name) {
		const query = String(window.location && window.location.search || "").replace(/^\?/, "");
		if (!query) {
			return "";
		}

		const pairs = query.split("&");
		for (let index = 0; index < pairs.length; index += 1) {
			const pair = pairs[index].split("=");
			if (decodeURIComponent(pair[0] || "") === name) {
				return decodeURIComponent(pair.slice(1).join("=") || "");
			}
		}
		return "";
	}

	function walkthroughAllowedForLocation() {
		if (!modernMockupsEnabled()) {
			return false;
		}
		const protocol = String(window.location && window.location.protocol || "");
		const host = String(window.location && window.location.hostname || "").toLowerCase();
		return protocol === "file:" || host === "localhost" || host === "127.0.0.1" || host === "::1";
	}

	function requestedWalkthroughMode() {
		const value = walkthroughQueryValue("walkthrough") || walkthroughQueryValue("demo");
		return String(value || "").trim().toLowerCase();
	}

	function walkthroughAction(id, label, options) {
		const opts = options || {};
		return {
			kind: "action",
			id: id,
			label: label,
			enabled: opts.enabled !== false,
			checked: !!opts.checked,
			tone: opts.tone || "",
			hint: opts.hint || ""
		};
	}

	function walkthroughDialogAction(id, label, options) {
		const opts = options || {};
		return {
			kind: "action",
			id: id,
			label: label,
			enabled: opts.enabled !== false,
			checked: !!opts.checked,
			tone: opts.tone || "",
			closesDialog: opts.closesDialog !== false
		};
	}

	function walkthroughSeparator() {
		return { kind: "separator" };
	}

	function walkthroughField(id, label, type, value, options) {
		const opts = options || {};
		const field = {
			id: id || "",
			label: label || "",
			type: type || "text",
			value: value,
			enabled: opts.enabled !== false
		};
		if (type === "voiceMeter" && value && typeof value === "object" && !Array.isArray(value)) {
			Object.keys(value).forEach(function(key) {
				field[key] = value[key];
			});
		}
		["hint", "tooltip", "presentation", "valueType", "browseActionId", "browseLabel", "chooseLabel",
			"removeLabel", "monospace", "rows", "min", "max", "step", "suffix", "advanced", "actionId",
			"buttonLabel", "tone"].forEach(function(key) {
			if (Object.prototype.hasOwnProperty.call(opts, key)) {
				field[key] = opts[key];
			}
		});
		if (opts.options) {
			field.options = opts.options;
		}
		return field;
	}

	function walkthroughNote(text) {
		return { type: "note", text: text || "" };
	}

	function walkthroughReadonly(label, value, options) {
		return walkthroughField("", label, "readonly", value, options);
	}

	function walkthroughSection(title, fields, options) {
		const opts = options || {};
		return {
			title: title || "",
			subtitle: opts.subtitle || "",
			presentation: opts.presentation || "",
			fields: fields || []
		};
	}

	function walkthroughHighlight(label, value, tone) {
		return {
			label: label || "",
			value: value == null ? "" : value,
			tone: tone || ""
		};
	}

	function walkthroughDialogBase(id, kind, title, subtitle, options) {
		const opts = options || {};
		return {
			open: true,
			id: id,
			kind: kind || "info",
			eyebrow: opts.eyebrow || "Mumble",
			title: title || "Dialog",
			subtitle: subtitle || "",
			tone: opts.tone || "",
			primaryActionId: opts.primaryActionId || "close",
			highlights: opts.highlights || [],
			pages: opts.pages || [],
			favorites: opts.favorites || null,
			sections: opts.sections || [],
			actions: opts.actions || [
				walkthroughDialogAction("close", "Close", { tone: "accent" })
			],
			errors: opts.errors || {},
			statusMessage: opts.statusMessage || ""
		};
	}

	function walkthroughPreviewImage(title, subtitle, accent, accent2) {
		const safeTitle = String(title || "Preview").replace(/[<>&]/g, "");
		const safeSubtitle = String(subtitle || "").replace(/[<>&]/g, "");
		const svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"540\" viewBox=\"0 0 960 540\">"
			+ "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
			+ "<stop offset=\"0\" stop-color=\"" + (accent || "#43c6ac") + "\"/>"
			+ "<stop offset=\"1\" stop-color=\"" + (accent2 || "#2e86de") + "\"/>"
			+ "</linearGradient></defs>"
			+ "<rect width=\"960\" height=\"540\" fill=\"#101823\"/>"
			+ "<rect x=\"32\" y=\"32\" width=\"896\" height=\"476\" rx=\"28\" fill=\"url(#g)\" opacity=\"0.92\"/>"
			+ "<circle cx=\"774\" cy=\"128\" r=\"86\" fill=\"#fff\" opacity=\"0.14\"/>"
			+ "<circle cx=\"168\" cy=\"420\" r=\"118\" fill=\"#000\" opacity=\"0.18\"/>"
			+ "<text x=\"72\" y=\"266\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"54\" "
			+ "font-weight=\"700\" fill=\"#fff\">" + safeTitle + "</text>"
			+ "<text x=\"76\" y=\"326\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"26\" "
			+ "fill=\"#dff8ff\" opacity=\"0.92\">" + safeSubtitle + "</text>"
			+ "</svg>";
		return "data:image/svg+xml;charset=UTF-8," + encodeURIComponent(svg);
	}

	function walkthroughQuote(price, changePercent, currency) {
		return {
			ok: true,
			pending: false,
			price: price,
			changePercent: changePercent,
			currency: currency || "USD",
			quoteTime: 1780147200
		};
	}

	function walkthroughTicker(symbol, name, holders, quantity, marketValue, currency, exchange) {
		return {
			symbol: symbol,
			displayName: name,
			holderCount: holders,
			totalQuantity: quantity,
			totalMarketValue: marketValue,
			currency: currency || "USD",
			providerId: "yahoo-finance",
			providerSymbol: symbol,
			exchange: exchange || "Nasdaq",
			quoteSourceUrl: "https://finance.yahoo.com/quote/" + encodeURIComponent(symbol),
			latestUpdatedAt: 1780147200
		};
	}

	function walkthroughParticipant(session, label, options) {
		const opts = options || {};
		return {
			session: session,
			label: label,
			scopeToken: opts.scopeToken || "voice:lobby",
			subtitle: opts.subtitle || "",
			entryKind: opts.entryKind || "user",
			isSelf: !!opts.isSelf,
			talkState: opts.talkState || "passive",
			canMessage: opts.canMessage !== false,
			canJoin: !!opts.canJoin,
			statuses: opts.statuses || [],
			actions: opts.actions || [
				walkthroughAction("participant.info", "User information..."),
				walkthroughAction("participant.message", "Direct message"),
				walkthroughAction("participant.comment", "View comment..."),
				walkthroughSeparator(),
				walkthroughAction("participant.nickname", "Local nickname..."),
				walkthroughAction("participant.history", "Grant chat history..."),
				walkthroughSeparator(),
				walkthroughAction("participant.kick", "Kick...", { tone: "danger" }),
				walkthroughAction("participant.ban", "Ban...", { tone: "danger" })
			]
		};
	}

	function buildMockupWalkthroughSnapshot() {
		const messageBaseMs = Date.UTC(2026, 4, 30, 14, 20, 0);
		const screenShare = {
			visible: true,
			mode: "available",
			scopeToken: "voice:lobby",
			ownerLabel: "Lobby screen share",
			statusLabel: "Available",
			statusTone: "success",
			badgeLabel: "Live",
			badgeTone: "success",
			primaryActionId: "screenShare.start",
			primaryLabel: "Share screen",
			primaryTone: "primary",
			primaryHint: "Share your screen to Lobby",
			primaryEnabled: true,
			overflowActions: [
				walkthroughAction("screenShare.start", "Share screen"),
				walkthroughAction("screenShare.settings", "Screen sharing settings...")
			]
		};
		const userActions = [
			walkthroughAction("participant.info", "User information..."),
			walkthroughAction("participant.message", "Direct message"),
			walkthroughAction("participant.comment", "View comment..."),
			walkthroughSeparator(),
			walkthroughAction("participant.nickname", "Local nickname..."),
			walkthroughAction("participant.history", "Grant chat history..."),
			walkthroughSeparator(),
			walkthroughAction("participant.kick", "Kick...", { tone: "danger" }),
			walkthroughAction("participant.ban", "Ban...", { tone: "danger" })
		];
		const selfActions = [
			walkthroughAction("self.info", "User information..."),
			walkthroughAction("self.comment", "My comment..."),
			walkthroughAction("self.avatar", "Change avatar..."),
			walkthroughAction("self.resetAvatar", "Reset avatar...", { tone: "danger" }),
			walkthroughAction("self.register", "Register..."),
			walkthroughSeparator(),
			walkthroughAction("self.audioStats", "Audio statistics..."),
			walkthroughAction("self.voiceRecorder", "Voice recorder..."),
			walkthroughAction("configure.settings", "Settings..."),
			walkthroughAction("configure.certificate", "Certificate...")
		];
		const you = walkthroughParticipant(1, "You", {
			isSelf: true,
			subtitle: "grinding ranked",
			talkState: "talking",
			statuses: [{ kind: "talking", label: "Talking", tone: "success" }],
			actions: selfActions
		});
		const kira = walkthroughParticipant(2, "Kira", {
			subtitle: "queueing ranked",
			talkState: "talking",
			statuses: [{ kind: "friend", label: "Friend", tone: "success" }],
			actions: userActions
		});
		const devOps = walkthroughParticipant(3, "Dev_Ops", {
			subtitle: "redeploying relay",
			statuses: [{ kind: "authenticated", label: "Authenticated" }],
			actions: userActions
		});
		const nova = walkthroughParticipant(4, "Nova", {
			scopeToken: "voice:valorant",
			subtitle: "duelist",
			actions: userActions
		});
		const byte = walkthroughParticipant(5, "Byte", {
			scopeToken: "voice:valorant",
			subtitle: "controller",
			actions: userActions
		});
		const rhea = walkthroughParticipant(6, "Rhea", {
			scopeToken: "voice:valorant",
			subtitle: "sentinel",
			actions: userActions
		});
		const lux = walkthroughParticipant(7, "Lux", {
			scopeToken: "voice:valorant",
			subtitle: "coach",
			actions: userActions
		});
		const zed = walkthroughParticipant(8, "Zed", {
			scopeToken: "voice:valorant",
			subtitle: "observer",
			actions: userActions
		});
		const pixel = walkthroughParticipant(9, "Pixel", {
			scopeToken: "voice:minecraft",
			subtitle: "building spawn",
			actions: userActions
		});
		const roomActions = [
			walkthroughAction("room.openChat", "Open chat"),
			walkthroughAction("room.markRead", "Mark as read"),
			walkthroughAction("room.muteNotifications", "Mute notifications"),
			walkthroughAction("room.copyInvite", "Copy invite link"),
			walkthroughAction("room.shareScreen", "Share screen"),
			walkthroughSeparator(),
			walkthroughAction("room.create", "Create room..."),
			walkthroughAction("room.acl", "Edit room..."),
			walkthroughAction("room.unlink", "Unlink room..."),
			walkthroughAction("room.unlinkAll", "Unlink all rooms..."),
			walkthroughAction("room.remove", "Remove room...", { tone: "danger" })
		];
		const stonks = {
			supported: true,
			enabled: true,
			registered: true,
			canAdmin: true,
			selfUserId: 1,
			selectedUserId: 1,
			selectedUserName: "You",
			selectedPeriod: "30d",
			periods: ["1d", "7d", "30d", "ytd"],
			automationHeaderVisible: true,
			disableQuoteLookup: true,
			personalTickers: [
				walkthroughTicker("RKLB", "Rocket Lab USA", 1, 42, 1192.8, "USD", "Nasdaq"),
				walkthroughTicker("GME", "GameStop", 1, 12, 290.16, "USD", "NYSE")
			],
			popularTickers: [
				walkthroughTicker("RKLB", "Rocket Lab USA", 4, 113, 3209.2, "USD", "Nasdaq"),
				walkthroughTicker("GME", "GameStop", 3, 55, 1329.9, "USD", "NYSE"),
				walkthroughTicker("NVDA", "NVIDIA", 2, 21, 2760.45, "USD", "Nasdaq")
			],
			tickerQuotes: {
				RKLB: walkthroughQuote(28.40, 4.2, "USD"),
				GME: walkthroughQuote(24.18, 12.6, "USD"),
				NVDA: walkthroughQuote(131.45, 2.2, "USD")
			},
			snapshots: [{
				snapshotId: 77,
				userId: 1,
				userName: "You",
				createdAt: 1780147200,
				currency: "USD",
				totalValue: 8582.64,
				note: "Walkthrough portfolio with live-looking quote metadata.",
				positionsRedacted: false,
				positions: [
					{ symbol: "RKLB", quantity: 42, price: 28.40, marketValue: 1192.8, currency: "USD", displayName: "Rocket Lab USA" },
					{ symbol: "GME", quantity: 12, price: 24.18, marketValue: 290.16, currency: "USD", displayName: "GameStop" }
				]
			}, {
				snapshotId: 71,
				userId: 1,
				userName: "You",
				createdAt: 1777555200,
				currency: "USD",
				totalValue: 7702.10,
				note: "Baseline update before the weekend run.",
				positionsRedacted: false,
				positions: [
					{ symbol: "RKLB", quantity: 36, price: 23.10, marketValue: 831.6, currency: "USD", displayName: "Rocket Lab USA" },
					{ symbol: "GME", quantity: 10, price: 21.04, marketValue: 210.4, currency: "USD", displayName: "GameStop" }
				]
			}],
			leaderboard: [
				{ rank: 1, userId: 2, userName: "Kira", period: "30d", returnPercent: 18.42, followed: true },
				{ rank: 2, userId: 1, userName: "You", period: "30d", returnPercent: 9.75, followed: false },
				{ rank: 3, userId: 4, userName: "Nova", period: "30d", returnPercent: -2.35, followed: false }
			],
			leaderboardDescription: "Leaderboard ranks only PnL for the selected period.",
			users: [
				{ userId: 1, userName: "You", followed: false },
				{ userId: 2, userName: "Kira", followed: true },
				{ userId: 4, userName: "Nova", followed: false }
			],
			textChannels: [
				{ textChannelId: 1, name: "stonks" },
				{ textChannelId: 2, name: "general" },
				{ textChannelId: 3, name: "clips" }
			]
		};
		const directMessages = {
			available: true,
			transport: "persistentChat",
			persistentHistory: true,
			persistentHistoryAvailable: true,
			privateModeAvailable: true,
			defaultMode: "history",
			canOpen: true,
			title: "Direct messages",
			description: "Direct messages can use persistent history or private in-memory mode.",
			unreadTotal: 2,
			hasUnread: true,
			conversations: [
				{
					token: "dm:2",
					session: 2,
					peerSession: 2,
					label: "Kira",
					subtitle: "In Lobby",
					open: false,
					unreadCount: 0,
					canSend: true,
					mode: "history",
					persistentHistory: true,
					privateMode: false,
					persistentHistoryAvailable: true,
					canPersistHistory: true,
					status: "available",
					statusLabel: "Direct message",
					lastActivityAtMs: messageBaseMs + 720000,
					lastMessagePreview: "dm mockup pass looks clean",
					lastMessageOwn: false,
					emptyCopy: "Direct messages with Kira will appear here.",
					messages: [
						{ id: "kira-1", localId: 1, peerSession: 2, actorName: "Kira", own: false, direction: "incoming", messageHtml: "hey, can you check the new shell?", plainText: "hey, can you check the new shell?", createdAtMs: messageBaseMs + 600000 },
						{ id: "kira-2", localId: 2, peerSession: 2, actorName: "You", own: true, direction: "outgoing", messageHtml: "yeah - mapping it against the API walkthrough now", plainText: "yeah - mapping it against the API walkthrough now", createdAtMs: messageBaseMs + 660000 },
						{ id: "kira-3", localId: 3, peerSession: 2, actorName: "Kira", own: false, direction: "incoming", messageHtml: "dm mockup pass looks clean", plainText: "dm mockup pass looks clean", createdAtMs: messageBaseMs + 720000 }
					]
				},
				{
					token: "dm:4",
					session: 4,
					peerSession: 4,
					label: "Nova",
					subtitle: "In Valorant",
					open: false,
					unreadCount: 2,
					canSend: true,
					mode: "private",
					persistentHistory: false,
					privateMode: true,
					persistentHistoryAvailable: true,
					canPersistHistory: true,
					status: "available",
					statusLabel: "Private direct message",
					lastActivityAtMs: messageBaseMs + 780000,
					lastMessagePreview: "private mode needs the ghost badge",
					lastMessageOwn: false,
					emptyCopy: "Direct messages with Nova will appear here.",
					messages: [
						{ id: "nova-1", localId: 4, peerSession: 4, actorName: "Nova", own: false, direction: "incoming", messageHtml: "private mode needs the ghost badge", plainText: "private mode needs the ghost badge", createdAtMs: messageBaseMs + 780000 }
					]
				}
			],
			windows: []
		};

		return {
			app: {
				serverEyebrow: "Twinfinity",
				serverTitle: "Twinfinity",
				serverSubtitle: "Connected - voice.twinfinity.com - 28 ms",
				serverAvatarUrl: "",
				connectionLabel: "28 ms",
				connectionTone: "success",
				compatibilityLabel: "Modern server",
				compatibilityTone: "success",
				layoutLabel: "Modern",
				layoutTone: "",
				canConnect: false,
				canDisconnect: true,
				canCreateTextRoom: true,
				canManageTextChannels: true,
				voiceRootLabel: "Voice rooms",
				voiceRootScopeToken: "voice:root",
				selfName: "You",
				selfStatusLabel: "grinding ranked",
				selfStatusTone: "success",
				selfMuted: false,
				selfDeafened: false,
				motdHtml: "<p><strong>Welcome to Twinfinity</strong></p><p>Ranked scrims every night at 21:00 CET in <em>Valorant</em>. Drop clips in <code>#clips</code> and keep <code>#general</code> on-topic.</p><p>New: the Stonks board is live. Pin your plays and climb the PnL leaderboard.</p>",
				motdSummary: "Ranked scrims, clips, Stonks, and room rules for the weekend.",
				motdExpanded: true,
				motdAlwaysVisible: true,
				uiTweaks: {
					theme: "dark",
					density: "comfortable",
					railSide: "right",
					tickerBannerAlwaysScroll: true
				},
				serverIdentity: {
					canEdit: true,
					resolvedMonogram: "T",
					imageUrl: ""
				},
				menus: [
					{
						id: "server",
						label: "Server",
						items: [
							walkthroughAction("server.information", "Server information..."),
							walkthroughAction("server.search", "Search..."),
							walkthroughAction("server.connect", "Connect to a server...", { enabled: false }),
							walkthroughAction("server.disconnect", "Disconnect...", { tone: "danger" }),
							walkthroughSeparator(),
							walkthroughAction("server.tokens", "Access tokens..."),
							walkthroughAction("server.users", "Registered users..."),
							walkthroughAction("server.bans", "Ban list..."),
							walkthroughAction("server.acl", "Access control (ACL)..."),
							walkthroughAction("server.settings", "Server settings...")
						]
					},
					{
						id: "room",
						label: "Room",
						items: [
							walkthroughAction("room.create", "Create room..."),
							walkthroughAction("stonks.open", "Stonks...")
						]
					},
					{
						id: "configure",
						label: "Configure",
						items: [
							walkthroughAction("configure.settings", "Settings..."),
							walkthroughAction("configure.screenShare", "Screen sharing settings..."),
							walkthroughAction("configure.audioWizard", "Audio setup..."),
							walkthroughAction("configure.certificate", "Certificate..."),
							walkthroughAction("self.avatar", "Change avatar...")
						]
					},
					{
						id: "help",
						label: "Help",
						items: [
							walkthroughAction("help.update", "Check for updates..."),
							walkthroughAction("help.feedback", "Report feedback..."),
							walkthroughAction("help.help", "Help"),
							walkthroughAction("app.quit", "Quit Mumble...", { tone: "danger" })
						]
					}
				],
				selfMenu: {
					name: "You",
					statusLabel: "grinding ranked",
					statusTone: "success",
					presence: [
						walkthroughAction("presence.online", "Online", { checked: true, tone: "success" }),
						walkthroughAction("presence.away", "Away"),
						walkthroughAction("presence.busy", "Do not disturb", { tone: "danger" })
					],
					actions: selfActions
				},
				directMessages: directMessages,
				stonks: stonks
			},
			voiceRooms: [
				{ token: "voice:lobby", kindLabel: "Voice room", label: "Lobby", description: "General hangout", pathLabel: "Voice rooms / Lobby", selected: true, joined: true, canJoin: true, participants: [you, kira, devOps], actions: roomActions, screenShare: screenShare },
				{ token: "voice:valorant", kindLabel: "Voice room", label: "Valorant", description: "Ranked scrims - 21:00 CET", pathLabel: "Voice rooms / Valorant", selected: false, joined: false, canJoin: true, participants: [nova, byte, rhea, lux, zed], actions: roomActions },
				{ token: "voice:minecraft", kindLabel: "Voice room", label: "Minecraft", description: "Survival build", pathLabel: "Voice rooms / Minecraft", selected: false, joined: false, canJoin: true, participants: [pixel], actions: roomActions },
				{ token: "voice:afk", kindLabel: "Voice room", label: "AFK / Away", description: "", pathLabel: "Voice rooms / AFK / Away", selected: false, joined: false, canJoin: true, participants: [], actions: roomActions }
			],
			textRooms: [
				{ token: "text:general", kindLabel: "Text room", label: "general", description: "Server-wide chatter", selected: false, unreadCount: 3, actions: roomActions },
				{ token: "text:clips", kindLabel: "Text room", label: "clips", description: "Drop your best plays", selected: false, unreadCount: 1, actions: roomActions },
				{ token: "text:stonks", kindLabel: "Text room", label: "stonks", description: "Not financial advice", selected: false, unreadCount: 0, actions: roomActions },
				{ token: "text:rules", kindLabel: "Text room", label: "rules", description: "Read before posting", selected: false, unreadCount: 0, actions: roomActions }
			],
			voicePresence: [you, kira, devOps],
			participants: [you, kira, devOps, nova, byte, rhea, lux, zed, pixel],
			activeScope: {
				scopeToken: "voice:lobby",
				kindLabel: "Voice room",
				label: "Lobby",
				description: "General hangout",
				meta: ["3 in voice", "Persistent history", "28 ms"],
				canLoadOlder: true,
				canMarkRead: true,
				canSend: true,
				canAttachImages: true,
				composerPlaceholder: "Message Lobby",
				composerHint: "Persistent room history stays with Lobby.",
				screenShare: screenShare,
				emptyCopy: "No messages in Lobby yet."
			},
			messages: [
				{ messageId: 9001, threadId: 1, createdAtMs: messageBaseMs, actor: "Kira", actorKey: "kira", timeLabel: "14:21", bodyText: "anyone up for ranked tonight?\ni can host", bodyHtml: "anyone up for ranked tonight?<br>i can host", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [] },
				{ messageId: 9002, threadId: 1, createdAtMs: messageBaseMs + 120000, actor: "Dev_Ops", actorKey: "dev_ops", timeLabel: "14:23", bodyText: "give me 10, redeploying the relay", bodyHtml: "give me 10, redeploying the relay", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [{ emoji: "ship", count: 2, selfReacted: false }] },
				{ messageId: 9003, threadId: 1, createdAtMs: messageBaseMs + 240000, actor: "Kira", actorKey: "kira", timeLabel: "14:24", replyActor: "Dev_Ops", replySnippet: "give me 10, redeploying the relay", bodyText: "no rush - found this while waiting\nhttps://youtu.be/dQw4w9WgXcQ", bodyHtml: "no rush - found this while waiting<br>https://youtu.be/dQw4w9WgXcQ", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [], preview: { kind: "link", url: "https://youtu.be/dQw4w9WgXcQ", title: "Cellist sets off the most FIRE jam ever", subtitle: "YouTube", description: "Embedded video card with playback controls.", thumbnailUrl: walkthroughPreviewImage("YouTube preview", "Lobby watch party", "#d93a35", "#101823"), openLabel: "Open on YouTube", previewSize: "large" } },
				{ messageId: 9004, threadId: 1, createdAtMs: messageBaseMs + 300000, actor: "You", actorKey: "you", timeLabel: "14:25", bodyText: "lol that's unreal\nok relay is back - joining Lobby", bodyHtml: "lol that's unreal<br>ok relay is back - joining Lobby", own: true, system: false, canReply: true, canReact: true, canDelete: true, reactions: [{ emoji: "check", count: 1, selfReacted: true }] },
				{ messageId: 9005, threadId: 1, createdAtMs: messageBaseMs + 420000, actor: "preview-bot", actorKey: "preview-bot", timeLabel: "14:27", bodyText: "Mockup previews should feel native in chat", bodyHtml: "Mockup previews should feel native in chat", own: false, system: false, canReply: true, canReact: true, canDelete: true, reactions: [], preview: { kind: "link", url: "https://x.com/dankpreview/status/1790000000000000000", title: "Mockup previews should feel native in chat", subtitle: "dankpreview", description: "A social card with post text, media, metrics, and source chrome.", thumbnailUrl: walkthroughPreviewImage("X preview", "Social post media", "#14171a", "#1d9bf0"), openLabel: "Open on X", metadata: { xHandle: "@dankpreview", xDisplayName: "Dank Preview", xLikeCount: 362000, xViewCount: 8100000 } } },
				{ messageId: 9006, threadId: 1, createdAtMs: messageBaseMs + 540000, actor: "preview-bot", actorKey: "preview-bot", timeLabel: "14:29", bodyText: "Attached image from chat composer", bodyHtml: "Attached image from chat composer", own: false, system: false, canReply: true, canReact: true, canDelete: true, reactions: [], preview: { kind: "image", url: "https://example.com/local-image", title: "Inline image", subtitle: "Local chat attachment", description: "Image preview state from the walkthrough snapshot.", mediaUrl: walkthroughPreviewImage("Inline image", "Local chat attachment", "#43c6ac", "#2e86de"), mediaMime: "image/svg+xml", openLabel: "Open image", previewSize: "default" } }
			]
		};
	}

	function mockupWalkthroughDialogState(name) {
		name = String(name || "").trim().toLowerCase();
		if (!name || !walkthroughAllowedForLocation() || requestedWalkthroughMode() !== "mockup") {
			return null;
		}

		const close = walkthroughDialogAction("close", "Close", { tone: "accent" });
		const cancel = walkthroughDialogAction("cancel", "Cancel");
		const snapshot = fallbackWalkthroughSnapshot() || buildMockupWalkthroughSnapshot();
		const stonks = snapshot.app && snapshot.app.stonks ? snapshot.app.stonks : {};
		const select = function(label, value) {
			return { label: label, value: value, enabled: true };
		};
		const aclValue = function() {
			return {
				inheritAcls: true,
				password: "scrim-night",
				activeTab: "rules",
				permissions: [
					{ id: 1, label: "Write" },
					{ id: 2, label: "Traverse" },
					{ id: 4, label: "Enter" },
					{ id: 8, label: "Speak" },
					{ id: 16, label: "Mute/deafen" },
					{ id: 32, label: "Move" }
				],
				groups: [
					{ name: "auth", inherit: true, inheritable: true, inherited: true, add: [], remove: [], inheritedMembers: [1, 2, 3] },
					{ name: "scrim-team", inherit: false, inheritable: true, inherited: false, add: [1, 2, 4, 7], remove: [] }
				],
				acls: [
					{ targetType: "group", target: "all", inherited: true, applyHere: true, applySubs: true, allow: [2], deny: [1], expanded: false },
					{ targetType: "group", target: "scrim-team", inherited: false, applyHere: true, applySubs: false, allow: [1, 2, 4, 8], deny: [], expanded: true },
					{ targetType: "user", target: "Kira", userId: 2, inherited: false, applyHere: true, applySubs: true, allow: [16, 32], deny: [] }
				]
			};
		};
		const settingsPage = function() {
			const raw = String(mockupSettingsActivePage || walkthroughQueryValue("page")
					|| walkthroughQueryValue("settingsPage") || "audioInput")
				.trim()
				.toLowerCase()
				.replace(/[\s_-]+/g, "");
			const aliases = {
				audio: "audioInput",
				audioinput: "audioInput",
				input: "audioInput",
				audiooutput: "audioOutput",
				output: "audioOutput",
				appearance: "look",
				look: "look",
				ui: "ui",
				userinterface: "ui",
				interface: "ui",
				messages: "messages",
				sounds: "messages",
				messagessounds: "messages",
				messagesandsounds: "messages",
				keys: "keys",
				keybindings: "keys",
				network: "network",
				screen: "screenShare",
				screenshare: "screenShare",
				screensharing: "screenShare",
				sharing: "screenShare",
				about: "about"
			};
			return aliases[raw] || "audioInput";
		};
		const settingsPages = function(activePage) {
			return [
				{ id: "audioInput", label: "Audio Input", selected: activePage === "audioInput" },
				{ id: "audioOutput", label: "Audio Output", selected: activePage === "audioOutput" },
				{ id: "look", label: "Appearance", selected: activePage === "look" },
				{ id: "ui", label: "User Interface", selected: activePage === "ui" },
				{ id: "messages", label: "Messages & Sounds", selected: activePage === "messages" },
				{ id: "keys", label: "Key Bindings", selected: activePage === "keys" },
				{ id: "network", label: "Network", selected: activePage === "network" },
				{ id: "screenShare", label: "Screen Sharing", selected: activePage === "screenShare" },
				{ id: "about", label: "About", selected: activePage === "about" }
			];
		};
		const settingsSections = function(activePage) {
			if (activePage === "look") {
				return [
					walkthroughSection("Theme", [
						walkthroughField("look.modernTheme", "Theme", "select", "dark", { valueType: "string",
							options: [select("Dark", "dark"), select("Light", "light"), select("Mocha", "mocha"),
								select("Macchiato", "macchiato"), select("Frappe", "frappe"), select("Latte", "latte"),
								select("Nord", "nord"), select("Gruvbox", "gruvbox")] }),
						walkthroughField("look.modernAccent", "Accent", "select", "teal", { valueType: "string",
							options: [select("Auto", "auto"), select("Teal", "teal"), select("Blue", "blue"),
								select("Violet", "violet"), select("Amber", "amber"), select("Rose", "rose")] })
					]),
					walkthroughSection("Layout", [
						walkthroughField("look.roomBrowserLayout", "Room browser layout", "select", "roster", { valueType: "string",
							options: [select("Tree", "tree"), select("Rooms", "rooms"), select("Roster", "roster")] }),
						walkthroughField("look.modernRailSide", "Rail side", "select", "right", { valueType: "string",
							options: [select("Left", "left"), select("Right", "right")] }),
						walkthroughField("look.modernDensity", "Density", "select", "comfortable", { valueType: "string",
							options: [select("Compact", "compact"), select("Comfortable", "comfortable"),
								select("Spacious", "spacious")] }),
						walkthroughField("look.modernClassicUserIcons", "Use classic user icons", "checkbox", false)
					])
				];
			}
			if (activePage === "ui") {
				return [
					walkthroughSection("Window behavior", [
						walkthroughField("look.quitBehavior", "Close button", "select", "ask", { valueType: "string",
							options: [select("Ask", "ask"), select("Minimize to tray", "tray"), select("Quit", "quit")] }),
						walkthroughField("look.hideInTray", "Hide in tray when minimized", "checkbox", true),
						walkthroughField("look.alwaysOnTop", "Always on top", "select", "never", { valueType: "string",
							options: [select("Never", "never"), select("Always", "always"), select("When active", "active")] })
					]),
					walkthroughSection("Room browser", [
						walkthroughField("look.showVolumeAdjustments", "Show local volume badges", "checkbox", true),
						walkthroughField("look.presenceIdleTimeout", "Idle presence timeout", "number", 10, { min: 1, max: 240, suffix: " min" })
					])
				];
			}
			if (activePage === "messages") {
				return [
					walkthroughSection("Chat", [
						walkthroughField("network.linkPreviews", "Link previews", "checkbox", true),
						walkthroughField("messages.enterToSend", "Enter sends message", "checkbox", true)
					]),
					walkthroughSection("Sounds", [
						walkthroughField("audio.cuePtt", "Push-to-talk cue", "checkbox", true),
						walkthroughField("audio.cueVad", "Voice activity cue", "checkbox", false),
						walkthroughField("audio.muteCue", "Mute cue", "checkbox", true),
						walkthroughField("audio.cueOnPath", "Transmit cue on", "pathPicker", "C:/Sounds/on.wav", {
							browseActionId: "browseCueOn",
							browseLabel: "Browse"
						})
					])
				];
			}
			if (activePage === "keys") {
				return [
					walkthroughSection("Shortcut controls", [
						walkthroughField("keys.globalShortcuts", "Enable global shortcuts", "checkbox", true)
					]),
					walkthroughSection("Configured shortcuts", [
						{
							id: "keys.shortcuts",
							label: "Shortcuts",
							type: "shortcutEditor",
							enabled: true,
							canCapture: true,
							canSuppress: true,
							actionOptions: [
								select("Unassigned", -1),
								select("Push-to-Talk", 0),
								select("Mute Self", 2),
								select("Deafen Self", 3),
								select("Whisper/Shout", 12),
								select("Toggle search dialog", 32),
								select("Send Text Message", 24)
							],
							toggleOptions: [
								select("Off", -1),
								select("Toggle", 0),
								select("On", 1)
							],
							targetOptions: [
								select("Current channel", "current"),
								select("Current selection", "selection"),
								select("Root", "root")
							],
							targetModeOptions: [
								select("Current selection", "selection"),
								select("List of users", "users"),
								select("Channel", "channel")
							],
							targetChannelOptions: [
								select("Current", -3),
								select("Root", -1),
								select("Parent", -2),
								select("Current / Subchannel #1", -4),
								select("Parent / Subchannel #1", -12),
								select("VC Root / AFK", 1),
								select("VC Root / Moneyhaters", 2)
							],
							targetUserOptions: [
								select("Kira", "hash-kira"),
								select("Nova", "hash-nova"),
								select("dänkmäster", "hash-dank")
							],
							channelOptions: [
								select("Root", 0),
								select("VC Root / AFK", 1),
								select("VC Root / Moneyhaters", 2)
							],
							rows: [
								{ index: 0, actionIndex: 0, actionLabel: "Push-to-Talk", dataType: "none", dataLabel: "No data", inputLabel: "Mouse 4", assigned: true, suppress: false },
								{ index: 1, actionIndex: 2, actionLabel: "Mute Self", dataType: "toggle", dataValue: 0, dataLabel: "Toggle", inputLabel: "Ctrl+Shift+M", assigned: true, suppress: true },
								{ index: 2, actionIndex: 3, actionLabel: "Deafen Self", dataType: "toggle", dataValue: 0, dataLabel: "Toggle", inputLabel: "Ctrl+Shift+D", assigned: true, suppress: true },
								{ index: 3, actionIndex: 24, actionLabel: "Send Text Message", dataType: "text", dataValue: "brb", dataLabel: "brb", inputLabel: "Ctrl+Enter", assigned: true, suppress: false },
								{
									index: 4,
									actionIndex: 12,
									actionLabel: "Whisper/Shout",
									dataType: "target",
									dataValue: "channel",
									dataLabel: "VC Root / Moneyhaters",
									target: {
										mode: "channel",
										channelId: 2,
										channelLabel: "VC Root / Moneyhaters",
										group: "raid",
										links: true,
										children: true,
										forceCenter: false,
										users: [],
										summary: "VC Root / Moneyhaters"
									},
									inputLabel: "Alt+W",
									assigned: true,
									suppress: false
								},
								{ index: 5, actionIndex: -1, actionLabel: "New shortcut", dataType: "none", dataLabel: "No data", inputLabel: "Not assigned", assigned: false, suppress: false, capturing: true }
							]
						}
					]),
					walkthroughSection("Additional shortcut engines", [
						walkthroughField("keys.enableUiAccess", "Enable shortcuts in privileged applications", "checkbox", true, { advanced: true }),
						walkthroughField("keys.enableGKey", "Enable GKey", "checkbox", false, { advanced: true }),
						walkthroughField("keys.enableXboxInput", "Enable XInput", "checkbox", true, { advanced: true })
					])
				];
			}
			if (activePage === "screenShare") {
				return [
					walkthroughSection("Session", [
						walkthroughField("screenShare.enabled", "Enable screen sharing", "checkbox", true),
						walkthroughField("screenShare.mode", "Default action", "select", "ask", { valueType: "string",
							options: [select("Ask before sharing", "ask"), select("Share current monitor", "monitor"),
								select("Share selected window", "window")] }),
						walkthroughField("screenShare.includeCursor", "Include cursor", "checkbox", true),
						walkthroughField("screenShare.showPreview", "Show local preview before publishing", "checkbox", true)
					]),
					walkthroughSection("Capture quality", [
						walkthroughField("screenShare.frameRate", "Frame rate", "range", 30, { min: 5, max: 60, suffix: " fps" }),
						walkthroughField("screenShare.quality", "Quality", "range", 82, { min: 10, max: 100, suffix: "%" }),
						walkthroughField("screenShare.maxBitrate", "Maximum bitrate", "number", 3500, { min: 250, max: 20000, suffix: " kbit/s" })
					]),
					walkthroughSection("Helper runtime", [
						walkthroughReadonly("Helper", "Packaged and ready"),
						walkthroughReadonly("Transport", "Server relay"),
						walkthroughField("screenShare.openDiagnostics", "Diagnostics", "button", "Open diagnostics", {
							actionId: "screenShare.diagnostics",
							buttonLabel: "Open diagnostics"
						})
					], { presentation: "list" })
				];
			}
			if (activePage === "network") {
				return [
					walkthroughSection("Connection", [
						walkthroughField("network.autoReconnect", "Reconnect automatically", "checkbox", true),
						walkthroughField("network.autoConnect", "Connect to last server on startup", "checkbox", true),
						walkthroughField("network.reconnectToLastChannel",
							"Reconnect to last known channel within server", "checkbox", true),
						walkthroughField("network.tcpMode", "Force TCP mode", "checkbox", false),
						walkthroughField("network.qos", "Use Quality of Service", "checkbox", true)
					]),
					walkthroughSection("Proxy and privacy", [
						walkthroughField("network.proxyType", "Proxy type", "select", "none", { valueType: "string",
							options: [select("None", "none"), select("HTTP", "http"), select("SOCKS5", "socks5")] }),
						walkthroughField("network.suppressIdentity", "Suppress certificate identity", "checkbox", false),
						walkthroughField("network.hideOS", "Hide operating system", "checkbox", false)
					])
				];
			}
			if (activePage === "about") {
				return [
					walkthroughSection("Mumble", [
						walkthroughReadonly("Version", "Local development build"),
						walkthroughReadonly("Architecture", "x64"),
						walkthroughField("about.openMumble", "Project, license, and credits", "button",
							"Open About Mumble", {
								actionId: "about.openMumble",
								buttonLabel: "Open About Mumble",
								tone: "accent"
							})
					]),
					walkthroughSection("Qt runtime", [
						walkthroughReadonly("Qt version", "Qt WebEngine runtime"),
						walkthroughReadonly("Operating system", "Windows"),
						walkthroughField("about.openQt", "Qt details", "button", "Open About Qt", {
							actionId: "about.openQt",
							buttonLabel: "Open About Qt"
						})
					])
				];
			}
			return [
				walkthroughSection("Device", [
					walkthroughField("audio.inputDevice", "Input device", "select", "default", {
						valueType: "string",
						options: [select("Default - Realtek", "default"), select("Yeti X", "yeti"),
							select("Elgato Wave:3", "wave"), select("Virtual Cable", "virtual")]
					}),
					walkthroughField("audio.transmit", "Transmission", "select", "vad", {
						valueType: "string",
						options: [select("Voice activity", "vad"), select("Push-to-talk", "ptt"),
							select("Continuous", "continuous")]
					})
				]),
				walkthroughSection("Voice activity", [
					walkthroughField("audio.inputLevel", "Input level", "voiceMeter", { level: 28, peak: 54, transmitting: false, source: "Default - Realtek" }),
					walkthroughField("audio.vadMin", "Silence below", "range", 30, { min: 0, max: 100, suffix: "%" }),
					walkthroughField("audio.vadMax", "Speech above", "range", 52, { min: 0, max: 100, suffix: "%" })
				]),
				walkthroughSection("Processing", [
					walkthroughField("audio.cleanup", "Noise cleanup", "select", "neural", {
						valueType: "string",
						options: [select("Off", "off"), select("Speex", "speex"), select("Neural cleanup", "neural")]
					}),
					walkthroughField("audio.agc", "Automatic gain", "checkbox", true)
				])
			];
		};

		if (name === "settings") {
			const activeSettingsPage = settingsPage();
			return walkthroughDialogBase("settings", "settings", "Settings", "", {
				eyebrow: "Settings",
				primaryActionId: "done",
				pages: settingsPages(activeSettingsPage),
				sections: settingsSections(activeSettingsPage),
				actions: [cancel, walkthroughDialogAction("done", "Done", { tone: "accent" })]
			});
		}

		if (name === "stonks") {
			const requestedTab = String(walkthroughQueryValue("tab") || "").trim().toLowerCase();
			if (["overview", "ledger", "portfolio", "leaderboard", "following", "audit", "admin"].indexOf(requestedTab) >= 0) {
				stonksActiveTab = requestedTab === "overview" ? "market" : (requestedTab === "ledger" ? "portfolio" : requestedTab);
			}
			return Object.assign(walkthroughDialogBase("stonks", "stonks", "Stonks", "Portfolio, leaderboard, following, and server settings.", {
				primaryActionId: "close",
				tone: "wide",
				actions: [close]
			}), { stonks: stonks });
		}

		if (name === "server-info" || name === "server-information") {
			return walkthroughDialogBase("serverInformation", "info", "Server information", "Current server details and advertised limits.", {
				highlights: [walkthroughHighlight("Status", "Connected", "good"), walkthroughHighlight("Codec", "Opus")],
				sections: [walkthroughSection("Server", [
					walkthroughReadonly("Name", "Twinfinity"),
					walkthroughReadonly("Version", "Mumble 1.7.0 DEV"),
					walkthroughReadonly("Uptime", "3 days, 4 hours"),
					walkthroughReadonly("Users", "9 / 100"),
					walkthroughReadonly("Max bitrate", "72 kbit/s")
				], { presentation: "list" })]
			});
		}

		if (name === "search" || name === "message-search") {
			return walkthroughDialogBase("search", "form", "Search", "Search messages, rooms, and users on the current server.", {
				primaryActionId: "search",
				highlights: [walkthroughHighlight("Scope", "Twinfinity"), walkthroughHighlight("Results", 4)],
				sections: [
					walkthroughSection("Query", [
						walkthroughField("search.query", "Search for", "text", "ranked relay"),
						walkthroughField("search.scope", "Scope", "select", "messages", { valueType: "string",
							options: [select("Messages", "messages"), select("Rooms", "rooms"), select("Users", "users")] }),
						walkthroughField("search.regex", "Regular expression", "checkbox", false)
					]),
					walkthroughSection("Results", [
						walkthroughField("search.results", "", "resultList", [
							{ type: "room", id: 1, index: 0, title: "Lobby", subtitle: "Voice room - 3 matching messages", primaryAction: "Open", secondaryAction: "Join" },
							{ type: "room", id: 2, index: 1, title: "#general", subtitle: "Text room - 2 matching messages", primaryAction: "Open", secondaryAction: "Open" },
							{ type: "user", id: 2, index: 2, title: "Kira", subtitle: "Mentioned ranked tonight", primaryAction: "Open", secondaryAction: "Message" },
							{ type: "user", id: 3, index: 3, title: "Dev_Ops", subtitle: "Relay deployment thread", primaryAction: "Open", secondaryAction: "Message" }
						])
					])
				],
				actions: [cancel, walkthroughDialogAction("search", "Search", { tone: "accent" })]
			});
		}

		if (name === "connect") {
			return walkthroughDialogBase("connect", "connect", "Connect to a server", "Choose a saved server or add one.", {
				primaryActionId: "connect",
				favorites: [
					{ id: "twinfinity", label: "Twinfinity", subtitle: "voice.twinfinity.com:64738 / You", selected: true,
						stats: [{ label: "Users", value: "9/50" }, { label: "Ping", value: "28 ms" }] },
					{ id: "scrimhub", label: "Scrim Hub EU", subtitle: "eu.scrimhub.gg:64738 / You",
						stats: [{ label: "Users", value: "142/50" }, { label: "Ping", value: "41 ms" }] },
					{ id: "basement", label: "Basement LAN", subtitle: "10.0.0.4:64738 / You",
						stats: [{ label: "Users", value: "0/50" }, { label: "Ping", value: "3 ms" }] }
				],
				sections: [walkthroughSection("Add server", [
					walkthroughField("connect.host", "Address", "text", "", { hint: "voice.example.com" }),
					walkthroughField("connect.port", "Port", "number", 64738),
					walkthroughField("connect.nick", "Nickname", "text", "You"),
					walkthroughField("connect.password", "Password", "password", "")
				])],
				actions: [cancel, walkthroughDialogAction("edit", "Edit"), walkthroughDialogAction("connect", "Connect", { tone: "accent" })]
			});
		}

		if (name === "disconnect") {
			return walkthroughDialogBase("disconnect", "confirm", "Disconnect", "Disconnect from this server?", {
				primaryActionId: "disconnect",
				tone: "danger",
				sections: [walkthroughSection("Confirmation", [
					walkthroughReadonly("Server", "Twinfinity"),
					walkthroughNote("Voice, text, screen sharing, and room state will leave the current server.")
				])],
				actions: [cancel, walkthroughDialogAction("disconnect", "Disconnect", { tone: "danger" })]
			});
		}

		if (name === "tokens") {
			return walkthroughDialogBase("tokens", "form", "Access tokens", "Manage temporary access tokens for the current server.", {
				highlights: [walkthroughHighlight("Saved tokens", 2), walkthroughHighlight("Scope", "Current server")],
				primaryActionId: "saveTokens",
				sections: [walkthroughSection("Tokens", [
					walkthroughField("token.1", "Token 1", "text", "design-review-token"),
					walkthroughField("token.2", "Token 2", "text", "temporary-event-pass"),
					walkthroughNote("Access tokens are saved for this server and sent with future reconnects.")
				])],
				actions: [cancel, walkthroughDialogAction("addToken", "Add token"), walkthroughDialogAction("saveTokens", "Save tokens", { tone: "accent" })]
			});
		}

		if (name === "users" || name === "registered-users") {
			return walkthroughDialogBase("registeredUsers", "info", "Registered users", "Registered accounts on this server.", {
				highlights: [walkthroughHighlight("Registered", 5), walkthroughHighlight("Shown", 5), walkthroughHighlight("Mode", "Read only")],
				sections: [
					walkthroughSection("Kira", [walkthroughReadonly("User ID", 1), walkthroughReadonly("Last seen", "Online now"), walkthroughReadonly("Certificate", "Verified")], { presentation: "records" }),
					walkthroughSection("Dev_Ops", [walkthroughReadonly("User ID", 2), walkthroughReadonly("Last seen", "Online now"), walkthroughReadonly("Certificate", "Verified")], { presentation: "records" }),
					walkthroughSection("Nova", [walkthroughReadonly("User ID", 3), walkthroughReadonly("Last seen", "2h ago"), walkthroughReadonly("Certificate", "Verified")], { presentation: "records" })
				]
			});
		}

		if (name === "bans" || name === "ban-list") {
			return walkthroughDialogBase("banList", "info", "Ban list", "Read-only Modern view of the server ban list.", {
				highlights: [walkthroughHighlight("Active bans", 2), walkthroughHighlight("Mode", "Read only")],
				sections: [
					walkthroughSection("203.0.113.x", [walkthroughReadonly("Username", "spammer_99"), walkthroughReadonly("Reason", "Advertising"), walkthroughReadonly("Expires", "2026-06-02")], { presentation: "records" }),
					walkthroughSection("198.51.100.x", [walkthroughReadonly("Username", "rage_quit"), walkthroughReadonly("Reason", "Harassment"), walkthroughReadonly("Expires", "Never")], { presentation: "records" })
				]
			});
		}

		if (name === "acl" || name === "room-acl") {
			return walkthroughDialogBase("acl", "form", "Edit room", "Manage room details, inherited groups, and explicit access rules.", {
				primaryActionId: "saveAcl",
				tone: "wide",
				highlights: [walkthroughHighlight("Room", "Lobby"), walkthroughHighlight("Rules", 3), walkthroughHighlight("Password", "Set")],
				sections: [walkthroughSection("Room details", [
					walkthroughField("channel.name", "Name", "text", "Lobby"),
					walkthroughField("channel.description", "Topic", "textarea", "Main voice lobby.", { rows: 4 }),
					walkthroughField("channel.position", "Order", "number", 0),
					walkthroughField("channel.maxUsers", "Max users", "number", 0)
				]), walkthroughSection("Access control", [
					walkthroughField("acl.model", "ACL", "aclEditor", aclValue())
				])],
				actions: [cancel, walkthroughDialogAction("saveAcl", "Save room", { tone: "accent" })]
			});
		}

		if (name === "create-room" || name === "room-create-room") {
			return walkthroughDialogBase("createRoom", "form", "Create room", "Create a voice room or persistent text room without leaving Modern layout.", {
				primaryActionId: "createRoom",
				sections: [walkthroughSection("Room", [
					walkthroughField("room.type", "Type", "select", "voice", { valueType: "string",
						options: [select("Voice room", "voice"), select("Text room", "text")] }),
					walkthroughField("room.name", "Name", "text", "Demo room"),
					walkthroughField("room.description", "Topic", "textarea", "Modern UI review room created from walkthrough.", { rows: 3 }),
					walkthroughField("room.parent", "Parent room", "select", "root", { valueType: "string",
						options: [select("Root", "root"), select("Lobby", "lobby"), select("Valorant", "valorant"),
							select("general", "general"), select("clips", "clips")] }),
					walkthroughField("room.temporary", "Temporary", "checkbox", false)
				])],
				actions: [cancel, walkthroughDialogAction("createRoom", "Create", { tone: "accent" })]
			});
		}

		if (name === "self-user-information" || name === "member-user-information" || name === "user-information" || name === "user-info") {
			const isSelf = name.indexOf("self") === 0;
			const userName = isSelf ? "You" : "Kira";
			return walkthroughDialogBase(isSelf ? "selfUserInformation" : "userInformation", "info", "User information", "Identity, certificate, and server registration details.", {
				highlights: [walkthroughHighlight("User", userName), walkthroughHighlight("Session", isSelf ? 1 : 2), walkthroughHighlight("Status", "Online", "good")],
				sections: [
					walkthroughSection("Identity", [
						walkthroughReadonly("Name", userName),
						walkthroughReadonly("User ID", isSelf ? 1 : 2),
						walkthroughReadonly("Session", isSelf ? 1 : 2),
						walkthroughReadonly("Hash", "7b51901e...a4", { monospace: true })
					], { presentation: "list" }),
					walkthroughSection("Connection", [
						walkthroughReadonly("Channel", "Lobby"),
						walkthroughReadonly("Online time", "42 min"),
						walkthroughReadonly("Authenticated", "Yes")
					], { presentation: "records" })
				],
				actions: [close, walkthroughDialogAction("messageUser", "Message", { tone: "accent" })]
			});
		}

		if (name === "self-comment" || name === "member-comment" || name === "user-comment") {
			const editingSelf = name.indexOf("self") === 0;
			return walkthroughDialogBase(editingSelf ? "selfComment" : "userComment", "form", editingSelf ? "My comment" : "User comment", "Edit rich text that appears in user information.", {
				primaryActionId: "saveComment",
				sections: [walkthroughSection("Comment", [
					walkthroughReadonly("User", editingSelf ? "You" : "Kira"),
					walkthroughField("comment.body", "Comment", "textarea", "<p>Ranked queue, relay ops, and weekend scrims.</p>", { rows: 6 }),
					walkthroughField("comment.preview", "Preview HTML", "checkbox", true)
				])],
				actions: [cancel, walkthroughDialogAction("resetComment", "Reset", { tone: "danger" }), walkthroughDialogAction("saveComment", "Save", { tone: "accent" })]
			});
		}

		if (name === "self-change-avatar" || name === "member-change-avatar") {
			const member = name === "member-change-avatar";
			return walkthroughDialogBase(member ? "memberChangeAvatar" : "selfChangeAvatar", "form", "Change Avatar", "Choose a new server-side avatar for " + (member ? "Kira" : "You") + ".", {
				primaryActionId: "applyAvatar",
				sections: [walkthroughSection("Avatar", [
					walkthroughReadonly("User", member ? "Kira" : "You"),
					walkthroughField("avatar.path", "Image file", "pathPicker", "", {
						browseActionId: "browseAvatar",
						browseLabel: "Browse",
						hint: "Choose a PNG or JPEG image to upload as a server avatar."
					})
				])],
				actions: [cancel, walkthroughDialogAction("applyAvatar", "Apply avatar", { tone: "accent" })]
			});
		}

		if (name === "self-reset-avatar" || name === "member-reset-avatar") {
			const member = name === "member-reset-avatar";
			return walkthroughDialogBase(member ? "memberResetAvatar" : "selfResetAvatar", "confirm", "Reset avatar", "Remove the server-side avatar for " + (member ? "Kira" : "You") + "?", {
				primaryActionId: "resetAvatar",
				tone: "danger",
				sections: [walkthroughSection("Confirmation", [
					walkthroughReadonly("User", member ? "Kira" : "You"),
					walkthroughNote("The avatar will be removed from the server and replaced by the generated initials avatar.")
				])],
				actions: [cancel, walkthroughDialogAction("resetAvatar", "Reset avatar", { tone: "danger" })]
			});
		}

		if (name === "reset-comment") {
			return walkthroughDialogBase("resetComment", "confirm", "Reset comment", "Clear the stored user comment?", {
				primaryActionId: "resetComment",
				tone: "danger",
				sections: [walkthroughSection("Confirmation", [
					walkthroughReadonly("User", "Kira"),
					walkthroughNote("The saved comment HTML will be removed from the server.")
				])],
				actions: [cancel, walkthroughDialogAction("resetComment", "Reset comment", { tone: "danger" })]
			});
		}

		if (name === "self-register" || name === "register") {
			return walkthroughDialogBase("selfRegister", "confirm", "Register", "Register your current certificate with this server?", {
				primaryActionId: "register",
				highlights: [walkthroughHighlight("User", "You"), walkthroughHighlight("Server", "Twinfinity")],
				sections: [walkthroughSection("Registration", [
					walkthroughReadonly("Name", "You"),
					walkthroughReadonly("Certificate", "7B:51:90:1E:...:A4", { monospace: true }),
					walkthroughNote("Registration lets server admins grant persistent permissions to this identity.")
				])],
				actions: [cancel, walkthroughDialogAction("register", "Register", { tone: "accent" })]
			});
		}

		if (name === "audio-statistics") {
			return walkthroughDialogBase("audioStatistics", "info", "Audio statistics", "Live input, packet, and jitter diagnostics for the selected user.", {
				highlights: [walkthroughHighlight("User", "You"), walkthroughHighlight("Signal", "-18 dB", "good"), walkthroughHighlight("Jitter", "2 ms")],
				sections: [
					walkthroughSection("Input", [
						walkthroughField("audio.meter", "Voice level", "voiceMeter", { level: 74, peak: 91, transmitting: true, source: "Yeti X" }),
						walkthroughReadonly("Audio bandwidth", "72 kbit/s"),
						walkthroughReadonly("Packets lost", "0.2%")
					]),
					walkthroughSection("Network", [
						walkthroughReadonly("Ping", "28 ms"),
						walkthroughReadonly("Jitter", "2 ms"),
						walkthroughReadonly("Codec", "Opus")
					], { presentation: "list" })
				],
				actions: [close, walkthroughDialogAction("resetStats", "Reset stats")]
			});
		}

		if (name === "local-nickname") {
			return walkthroughDialogBase("localNickname", "form", "Local nickname", "Choose a local display name override for this user.", {
				primaryActionId: "saveNickname",
				sections: [walkthroughSection("Nickname", [
					walkthroughReadonly("User", "Kira"),
					walkthroughField("nickname.value", "Local nickname", "text", "Kira - IGL"),
					walkthroughNote("This label is only shown on this client.")
				])],
				actions: [cancel, walkthroughDialogAction("clearNickname", "Clear"), walkthroughDialogAction("saveNickname", "Save", { tone: "accent" })]
			});
		}

		if (name === "grant-history") {
			return walkthroughDialogBase("grantHistory", "form", "Grant chat history", "Allow a user to read recent persistent room history.", {
				primaryActionId: "grantHistory",
				sections: [walkthroughSection("Access", [
					walkthroughReadonly("User", "Kira"),
					walkthroughField("history.room", "Room", "select", "lobby", { valueType: "string",
						options: [select("Lobby", "lobby"), select("#general", "general"), select("#clips", "clips")] }),
					walkthroughField("history.window", "History window", "select", "24h", { valueType: "string",
						options: [select("Last hour", "1h"), select("Last 24 hours", "24h"), select("Last 7 days", "7d")] }),
					walkthroughField("history.includeMedia", "Include rich previews", "checkbox", true)
				])],
				actions: [cancel, walkthroughDialogAction("grantHistory", "Grant", { tone: "accent" })]
			});
		}

		if (name === "kick-user" || name === "kick") {
			return walkthroughDialogBase("kickUser", "confirm", "Kick user", "Kick Kira from the current server?", {
				primaryActionId: "kick",
				tone: "danger",
				sections: [walkthroughSection("Moderation", [
					walkthroughReadonly("User", "Kira"),
					walkthroughField("kick.reason", "Reason", "textarea", "Take a break and rejoin when ready.", { rows: 4 })
				])],
				actions: [cancel, walkthroughDialogAction("kick", "Kick", { tone: "danger" })]
			});
		}

		if (name === "ban-user" || name === "ban") {
			return walkthroughDialogBase("banUser", "confirm", "Ban user", "Ban Kira from the current server?", {
				primaryActionId: "ban",
				tone: "danger",
				sections: [walkthroughSection("Moderation", [
					walkthroughReadonly("User", "Kira"),
					walkthroughField("ban.reason", "Reason", "textarea", "Repeated disruption after warning.", { rows: 4 }),
					walkthroughField("ban.ip", "Ban IP address", "checkbox", true),
					walkthroughField("ban.duration", "Duration", "select", "24h", { valueType: "string",
						options: [select("1 hour", "1h"), select("24 hours", "24h"), select("Permanent", "permanent")] })
				])],
				actions: [cancel, walkthroughDialogAction("ban", "Ban", { tone: "danger" })]
			});
		}

		if (name === "certificate" || name === "self-certificate") {
			return walkthroughDialogBase("certificate", "form", "Certificate", "Manage the client certificate used for account identity and server authentication.", {
				highlights: [walkthroughHighlight("Status", "Installed", "good"), walkthroughHighlight("Action", "Export"), walkthroughHighlight("Expires", "2042-04-06")],
				primaryActionId: "applyCertificate",
				sections: [
					walkthroughSection("Current certificate", [
						walkthroughReadonly("Status", "A valid certificate is installed."),
						walkthroughReadonly("Name", "You"),
						walkthroughReadonly("Email", "None"),
						walkthroughReadonly("Issuer", "You"),
						walkthroughReadonly("Fingerprint", "7B:51:90:1E:...:A4", { monospace: true })
					], { presentation: "certificate-current" }),
					walkthroughSection("Certificate action", [
						walkthroughField("cert.mode", "Action", "select", "export", { valueType: "string",
							options: [select("Export current certificate", "export"), select("Import certificate", "import"),
								select("Generate new certificate", "generate")] }),
						walkthroughField("cert.exportPath", "Export file", "pathPicker", "C:/Users/You/Desktop/mumble-cert.p12", {
							browseActionId: "browseCertificateExport",
							browseLabel: "Browse"
						})
					], { presentation: "certificate-action" })
				],
				actions: [close, walkthroughDialogAction("applyCertificate", "Apply", { tone: "accent" })]
			});
		}

		if (name === "avatar" || name === "change-avatar") {
			return walkthroughDialogBase("changeAvatar", "form", "Change Avatar", "Choose a new server-side avatar for You.", {
				primaryActionId: "applyAvatar",
				sections: [walkthroughSection("Avatar", [
					walkthroughReadonly("User", "You"),
					walkthroughField("avatar.path", "Image file", "pathPicker", "", {
						browseActionId: "browseAvatar",
						browseLabel: "Browse",
						hint: "Choose a PNG or JPEG image to upload as your server avatar."
					})
				])],
				actions: [cancel, walkthroughDialogAction("applyAvatar", "Apply avatar", { tone: "accent" })]
			});
		}

		if (name === "server-settings") {
			return walkthroughDialogBase("serverSettings", "form", "Server settings", "Change connected server settings from the Modern layout.", {
				primaryActionId: "saveServerSettings",
				sections: [walkthroughSection("Chat", [
					walkthroughField("server.welcome", "Welcome text", "textarea", "Welcome to Twinfinity. Ranked scrims every night at 21:00 CET.", { rows: 4 }),
					walkthroughField("server.allowHtml", "Allow HTML", "checkbox", true),
					walkthroughField("server.chatEnabled", "Server-wide chat", "checkbox", true)
				])],
				actions: [cancel, walkthroughDialogAction("saveServerSettings", "Apply", { tone: "accent" })]
			});
		}

		if (name === "about") {
			return walkthroughDialogBase("about", "info", "About Mumble", "Version, license, and project information.", {
				highlights: [walkthroughHighlight("Version", "1.7.0"), walkthroughHighlight("Architecture", "x64"), walkthroughHighlight("License", "BSD")],
				sections: [walkthroughSection("Project", [
					walkthroughReadonly("Website", "https://www.mumble.info/"),
					walkthroughNote("An Open Source, low-latency, high quality voice-chat utility.")
				])],
				actions: [close, walkthroughDialogAction("website", "Open website", { tone: "accent" })]
			});
		}

		if (name === "about-qt") {
			return walkthroughDialogBase("aboutQt", "info", "About Qt", "Qt runtime and licensing details.", {
				highlights: [walkthroughHighlight("Qt", "6.x"), walkthroughHighlight("WebEngine", "Enabled"), walkthroughHighlight("License", "LGPL")],
				sections: [walkthroughSection("Runtime", [
					walkthroughReadonly("Qt build", "Qt 6 shared WebEngine"),
					walkthroughReadonly("Platform", "Windows x64"),
					walkthroughNote("Modern shell rendering uses Qt WebEngine inside the desktop client.")
				], { presentation: "list" })],
				actions: [close, walkthroughDialogAction("qtWebsite", "Open Qt", { tone: "accent" })]
			});
		}

		if (name === "update") {
			return walkthroughDialogBase("versionCheck", "update", "Update available", "A new mumble-forked build is ready.", {
				primaryActionId: "openForkInstaller",
				highlights: [walkthroughHighlight("Status", "Available", "warning"), walkthroughHighlight("Current build", "0"), walkthroughHighlight("Latest", "1.7.1, build 42")],
				sections: [
					walkthroughSection("Available update", [
						walkthroughReadonly("Latest build", "1.7.1, build 42"),
						walkthroughReadonly("Published", "May 30, 2026"),
						walkthroughReadonly("Commit", "abc1234"),
						walkthroughReadonly("Release", "https://github.com/dankmaster/mumble/releases/tag/mumble-forked"),
						walkthroughReadonly("Installer", "https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi")
					], { presentation: "list", subtitle: "Security and Modern shell polish are included in this release." }),
					walkthroughSection("Installed build", [
					walkthroughReadonly("Current version", "1.7.0"),
					walkthroughReadonly("Current build", "0"),
					walkthroughReadonly("Release channel", "mumble-forked")
					], { presentation: "list" }),
					walkthroughSection("Release notes", [
						walkthroughReadonly("Notes", "Modern dialogs now follow the hardened shell mockup and update checks use the in-app flow.")
					], { presentation: "list" })
				],
				actions: [
					close,
					walkthroughDialogAction("runVersionCheck", "Check again", { closesDialog: false }),
					walkthroughDialogAction("openForkRelease", "Open releases", { closesDialog: false }),
					walkthroughDialogAction("openForkInstaller", "Download update", { tone: "accent", closesDialog: false })
				]
			});
		}

		if (name === "feedback" || name === "report-feedback") {
			return walkthroughDialogBase("feedback", "feedback", "Report feedback", "Bug reports, suggestions, and questions for this fork.", {
				primaryActionId: "submitFeedback",
				highlights: [walkthroughHighlight("Server submit", "Fallback"), walkthroughHighlight("Diagnostics", "Included"), walkthroughHighlight("Max body", "58 KiB")],
				sections: [
					walkthroughSection("Report", [
						walkthroughField("feedback.kind", "Type", "select", 0, {
							options: [select("Bug", 0), select("Suggestion", 1), select("Question", 2)]
						}),
						walkthroughField("feedback.title", "Title", "text", "Update window does not match mockup"),
						walkthroughField("feedback.description", "Description", "textarea", "The update available flow needs to use the same narrow Modern dialog style as the mockup.", { rows: 5 }),
						walkthroughField("feedback.steps", "Steps to reproduce", "textarea", "1. Open Help.\n2. Choose Check for updates.\n3. Compare the result dialog with the mockup.", { rows: 4 })
					]),
					walkthroughSection("Evidence", [
						walkthroughField("feedback.evidence", "Pasted evidence", "textarea", "Mockup slide 37 and API capture need to match.", { rows: 3 })
					]),
					walkthroughSection("Diagnostics", [
						walkthroughField("feedback.includeDiagnostics", "Include diagnostics", "checkbox", true),
						walkthroughReadonly("Status", "Submit will use the GitHub fallback because server-side feedback submission is disabled."),
						walkthroughReadonly("Diagnostics preview", "Client: Mumble 1.7.0\nQt: packaged runtime\nServer feedback: fallback", { monospace: true })
					])
				],
				actions: [
					close,
					walkthroughDialogAction("toggleFeedbackCapture", "Start capture", { closesDialog: false }),
					walkthroughDialogAction("copyFeedbackReport", "Copy report", { closesDialog: false }),
					walkthroughDialogAction("openFeedbackGitHub", "Open GitHub", { closesDialog: false }),
					walkthroughDialogAction("submitFeedback", "Submit", { tone: "accent", closesDialog: false })
				]
			});
		}

		if (name === "help") {
			return walkthroughDialogBase("help", "info", "Help", "Modern layout keeps contextual help inside the client shell.", {
				highlights: [walkthroughHighlight("Menus", 6), walkthroughHighlight("Layout", "Modern"), walkthroughHighlight("Context", "Current view")],
				sections: [walkthroughSection("Modern header", [
					walkthroughReadonly("Server", "Connect, search, server information, tokens, registered users, and bans."),
					walkthroughReadonly("Room", "Create rooms, manage ACLs, and room-specific actions."),
					walkthroughReadonly("User", "User information, comments, registration, moderation, and history grants.")
				], { presentation: "list" })]
			});
		}

		if (name === "unlink-room") {
			return walkthroughDialogBase("unlinkRoom", "confirm", "Unlink room", "Unlink Lobby from its linked room group?", {
				primaryActionId: "unlinkRoom",
				tone: "danger",
				sections: [walkthroughSection("Room links", [
					walkthroughReadonly("Room", "Lobby"),
					walkthroughReadonly("Linked rooms", "Valorant, Minecraft"),
					walkthroughNote("This room will stop sharing audio with the linked group.")
				])],
				actions: [cancel, walkthroughDialogAction("unlinkRoom", "Unlink", { tone: "danger" })]
			});
		}

		if (name === "unlink-all-rooms") {
			return walkthroughDialogBase("unlinkAllRooms", "confirm", "Unlink all rooms", "Remove all room links for Lobby?", {
				primaryActionId: "unlinkAllRooms",
				tone: "danger",
				sections: [walkthroughSection("Room links", [
					walkthroughReadonly("Room", "Lobby"),
					walkthroughReadonly("Links removed", "2"),
					walkthroughNote("Every linked room edge connected to Lobby will be removed.")
				])],
				actions: [cancel, walkthroughDialogAction("unlinkAllRooms", "Unlink all", { tone: "danger" })]
			});
		}

		if (name === "remove-room") {
			return walkthroughDialogBase("removeRoom", "confirm", "Remove room", "Remove Lobby and its persistent settings?", {
				primaryActionId: "removeRoom",
				tone: "danger",
				sections: [walkthroughSection("Room", [
					walkthroughReadonly("Room", "Lobby"),
					walkthroughReadonly("Messages", "Persistent history retained by server policy"),
					walkthroughNote("This cannot be undone from the Modern shell.")
				])],
				actions: [cancel, walkthroughDialogAction("removeRoom", "Remove room", { tone: "danger" })]
			});
		}

		if (name === "voice-recorder") {
			return walkthroughDialogBase("voiceRecorder", "form", "Voice recorder", "Record the current session from the Modern shell.", {
				primaryActionId: "startRecording",
				sections: [
					walkthroughSection("Recorder", [
						walkthroughReadonly("Status", "Idle"),
						walkthroughReadonly("Elapsed", "00:00:00")
					]),
					walkthroughSection("Output", [
						walkthroughField("recording.path", "Target directory", "pathPicker", "C:/Recordings", {
							browseActionId: "browseRecordingDirectory",
							browseLabel: "Browse"
						}),
						walkthroughField("recording.format", "Format", "select", "wav", { valueType: "string",
							options: [select("WAV", "wav"), select("FLAC", "flac"), select("Ogg Opus", "opus")] })
					])
				],
				actions: [close, walkthroughDialogAction("refresh", "Refresh"), walkthroughDialogAction("startRecording", "Start", { tone: "accent" })]
			});
		}

		if (name === "quit") {
			return walkthroughDialogBase("quit", "confirm", "Quit Mumble", "Are you sure you want to quit Mumble?", {
				primaryActionId: "quit",
				tone: "danger",
				sections: [walkthroughSection("Confirmation", [
					walkthroughNote("Quitting will disconnect from the current server and close Mumble.")
				])],
				actions: [cancel, walkthroughDialogAction("quit", "Quit Mumble", { tone: "danger" })]
			});
		}

		return null;
	}

	function syncWalkthroughDialogFromQuery() {
		const dialog = mockupWalkthroughDialogState(walkthroughQueryValue("dialog") || walkthroughQueryValue("window"));
		if (dialog) {
			modernDialogState = dialog;
			renderModernDialog();
		}
	}

	function syncWalkthroughUiFromQuery() {
		if (modernBridge || !walkthroughAllowedForLocation() || requestedWalkthroughMode() !== "mockup") {
			return;
		}
		const search = String(walkthroughQueryValue("search") || "").trim();
		if (!search) {
			return;
		}
		requestAnimationFrame(function() {
			messageSearchText = search;
			messageSearchOpen = true;
			syncMessageSearchState();
		});
	}

	function cloneWalkthroughSnapshot(snapshot) {
		try {
			return JSON.parse(JSON.stringify(snapshot || {}));
		} catch (error) {
			return snapshot || {};
		}
	}

	function walkthroughRoomMessages(scope) {
		const token = String(scope && scope.scopeToken || "");
		const label = String(scope && scope.label || "room");
		const base = Date.UTC(2026, 4, 30, 15, 0, 0);
		if (token === "text:clips") {
			return [
				{ messageId: 9101, threadId: 2, createdAtMs: base, actor: "Nova", actorKey: "nova", timeLabel: "15:01", bodyText: "ace clip from last round", bodyHtml: "ace clip from last round", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [{ emoji: "fire", count: 4, selfReacted: false }], preview: { kind: "image", url: "https://example.com/clip.png", title: "Valorant ace", subtitle: "#clips", description: "Image preview for a shared gameplay clip.", mediaUrl: walkthroughPreviewImage("Clip preview", "Round-winning ace", "#7c3aed", "#43c6ac"), mediaMime: "image/svg+xml", openLabel: "Open image" } },
				{ messageId: 9102, threadId: 2, createdAtMs: base + 90000, actor: "You", actorKey: "you", timeLabel: "15:02", bodyText: "that flick was nasty", bodyHtml: "that flick was nasty", own: true, system: false, canReply: true, canReact: true, canDelete: true, reactions: [] }
			];
		}
		if (token === "text:stonks") {
			return [
				{ messageId: 9201, threadId: 3, createdAtMs: base, actor: "Kira", actorKey: "kira", timeLabel: "15:06", bodyText: "#stonks RKLB 42", bodyHtml: "#stonks RKLB 42", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [] },
				{ messageId: 9202, threadId: 3, createdAtMs: base + 70000, actor: "stonks-bot", actorKey: "stonks-bot", timeLabel: "15:07", bodyText: "Kira updated their portfolio.", bodyHtml: "Kira updated their portfolio.", own: false, system: true, canReply: false, canReact: false, canDelete: false, reactions: [] }
			];
		}
		if (token === "text:rules") {
			return [
				{ messageId: 9301, threadId: 4, createdAtMs: base, actor: "Server", actorKey: "server", timeLabel: "15:10", bodyText: "Keep comms civil, use clips for media, and no spoilers in Lobby.", bodyHtml: "Keep comms civil, use clips for media, and no spoilers in Lobby.", own: false, system: true, canReply: false, canReact: false, canDelete: false, reactions: [] }
			];
		}
		if (token === "voice:valorant") {
			return [
				{ messageId: 9401, threadId: 5, createdAtMs: base, actor: "Nova", actorKey: "nova", timeLabel: "15:12", bodyText: "queue pops in two minutes", bodyHtml: "queue pops in two minutes", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [] },
				{ messageId: 9402, threadId: 5, createdAtMs: base + 120000, actor: "Byte", actorKey: "byte", timeLabel: "15:14", bodyText: "I'll fill smokes", bodyHtml: "I'll fill smokes", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [{ emoji: "check", count: 2, selfReacted: false }] }
			];
		}
		return [
			{ messageId: 9007, threadId: 1, createdAtMs: base, actor: "Kira", actorKey: "kira", timeLabel: "15:00", bodyText: "checking " + label + " from the walkthrough", bodyHtml: "checking " + escapeHtml(label) + " from the walkthrough", own: false, system: false, canReply: true, canReact: true, canDelete: false, reactions: [] }
		];
	}

	function applyMockupWalkthroughVariant(snapshot) {
		const scopeQuery = String(walkthroughQueryValue("scope") || "").trim().toLowerCase();
		const dmQuery = String(walkthroughQueryValue("dm") || walkthroughQueryValue("direct") || "").trim().toLowerCase();
		const motdQuery = String(walkthroughQueryValue("motd") || "").trim().toLowerCase();
		const variantQuery = String(walkthroughQueryValue("state") || walkthroughQueryValue("variant") || "").trim().toLowerCase();
		const scopeAliases = {
			general: "text:general",
			clips: "text:clips",
			stonks: "text:stonks",
			rules: "text:rules",
			valorant: "voice:valorant",
			lobby: "voice:lobby"
		};
		const targetScope = scopeAliases[scopeQuery] || scopeQuery;

		if (targetScope) {
			const allRooms = [].concat(snapshot.voiceRooms || [], snapshot.textRooms || []);
			let room = null;
			allRooms.forEach(function(candidate) {
				if (String(candidate && candidate.token || "").toLowerCase() === targetScope) {
					room = candidate;
				}
				if (candidate) {
					candidate.selected = false;
				}
			});
			if (room) {
				room.selected = true;
				const isText = String(room.token || "").indexOf("text:") === 0;
				const label = isText ? "#" + String(room.label || "").replace(/^#/, "") : (room.label || "Room");
				snapshot.activeScope = {
					scopeToken: room.token,
					kindLabel: room.kindLabel || (isText ? "Text room" : "Voice room"),
					label: label,
					description: room.description || "",
					meta: isText
						? ["Text room", "Persistent history", (room.unreadCount || 0) + " unread"]
						: ["Voice room", "Persistent history", "28 ms"],
					canLoadOlder: true,
					canMarkRead: true,
					canSend: true,
					canAttachImages: true,
					composerPlaceholder: "Message " + label,
					composerHint: "Persistent room history stays with " + label + ".",
					screenShare: room.screenShare || null,
					emptyCopy: "No messages in " + label + " yet."
				};
				snapshot.messages = walkthroughRoomMessages(snapshot.activeScope);
				if (!isText) {
					snapshot.voicePresence = room.participants || [];
				}
			}
		}

		if (dmQuery) {
			const dmState = snapshot.app.directMessages || {};
			const conversations = Array.isArray(dmState.conversations) ? dmState.conversations : [];
			const target = conversations.find(function(conversation) {
				return String(conversation && conversation.label || "").toLowerCase() === "kira";
			}) || conversations[0];
			if (target) {
				const privateMode = dmQuery === "private" || variantQuery === "dm-private";
				target.open = dmQuery !== "main";
				target.unreadCount = 0;
				target.mode = privateMode ? "private" : "history";
				target.privateMode = privateMode;
				target.persistentHistory = !privateMode;
				target.statusLabel = privateMode ? "Private direct message" : "Direct message";
				const windowState = Object.assign({}, target, {
					open: true,
					messages: Array.isArray(target.messages) ? target.messages.slice() : []
				});

				if (dmQuery === "tray" || variantQuery === "dm-tray") {
					dmState.trayOpen = true;
					dmState.windows = [];
				} else if (dmQuery === "window" || dmQuery === "private" || variantQuery === "dm-window"
						|| variantQuery === "dm-private") {
					dmState.trayOpen = false;
					dmState.windows = [windowState];
				} else {
					dmState.trayOpen = false;
					dmState.windows = [];
				}

				dmState.unreadTotal = conversations.reduce(function(total, conversation) {
					return total + Number(conversation && conversation.unreadCount || 0);
				}, 0);
				dmState.hasUnread = dmState.unreadTotal > 0;
				snapshot.app.directMessages = dmState;

				if (dmQuery !== "tray") {
					(snapshot.voiceRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
					(snapshot.textRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
					snapshot.activeScope = directMessageActiveScope(windowState);
					if (isDirectMessageScopeToken(scopeQuery)) {
						snapshot.activeScope.scopeToken = scopeQuery;
					}
					snapshot.messages = [];
					snapshot.voicePresence = [snapshot.participants[0], snapshot.participants[1]].filter(Boolean);
				}
			}
		}

		if (motdQuery === "collapsed" || variantQuery === "motd-collapsed") {
			snapshot.app.motdExpanded = false;
			snapshot.app.motdAlwaysVisible = true;
		} else if (motdQuery === "dismissed" || variantQuery === "motd-dismissed") {
			const dismissedSignature = motdContentSignature(snapshot.app.motdHtml || "");
			snapshot.app.motdDismissedSignature = dismissedSignature;
			snapshot.app.motdLastSeenSignature = dismissedSignature;
			snapshot.app.motdExpanded = false;
			snapshot.app.motdAlwaysVisible = true;
		}

		if (variantQuery === "update-available") {
			snapshot.app.updateBanner = {
				visible: true,
				phase: "available",
				tone: "warning",
				title: "Update available",
				detail: "Security and Modern shell polish are included in this release. Latest: 1.7.0, build 124",
				actions: [
					{ id: "app.update.download", label: "Install update", tone: "accent", enabled: true },
					{ id: "app.update.details", label: "Details", enabled: true },
					{ id: "app.update.dismiss", label: "Not now", enabled: true }
				]
			};
		} else if (variantQuery === "update-downloading") {
			snapshot.app.updateBanner = {
				visible: true,
				phase: "downloading",
				tone: "accent",
				title: "Downloading update",
				detail: "Mumble is downloading and verifying the update. You can keep using the client.",
				progressVisible: true,
				progressIndeterminate: false,
				progressPercent: 46,
				progressLabel: "46%",
				actions: [{ id: "app.update.details", label: "Details", enabled: true }]
			};
		} else if (variantQuery === "update-ready") {
			snapshot.app.updateBanner = {
				visible: true,
				phase: "ready",
				tone: "success",
				title: "Update ready to install",
				detail: "Mumble will close, mumble-updater will run the installer, and Mumble will reopen to restore this server and chat.",
				progressVisible: true,
				progressIndeterminate: false,
				progressPercent: 100,
				progressLabel: "Verified",
				actions: [
					{ id: "app.update.restart", label: "Install and restart", tone: "accent", enabled: true },
					{ id: "app.update.details", label: "Details", enabled: true },
					{ id: "app.update.dismiss", label: "Later", enabled: true }
				]
			};
		}

		return snapshot;
	}

	function fallbackWalkthroughSnapshot() {
		if (modernBridge || !walkthroughAllowedForLocation() || requestedWalkthroughMode() !== "mockup") {
			return null;
		}
		if (!walkthroughSnapshotCache) {
			walkthroughSnapshotCache = applyMockupWalkthroughVariant(cloneWalkthroughSnapshot(buildMockupWalkthroughSnapshot()));
		}
		return walkthroughSnapshotCache;
	}
	/* MUMBLE_MODERN_MOCKUPS_END */

	function getSnapshot() {
		if (modernBridge && (!liveSnapshot || !Object.keys(liveSnapshot).length)) {
			liveSnapshot = modernBridge.snapshot || {};
		}
		if (!modernBridge && (!liveSnapshot || !Object.keys(liveSnapshot).length)) {
			liveSnapshot = fallbackWalkthroughSnapshot() || {};
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
		if (previewHydrationScrollIdleTimer) {
			clearTimeout(previewHydrationScrollIdleTimer);
			previewHydrationScrollIdleTimer = 0;
		}
		messageListScrollActiveUntil = 0;
		previewHydrationPausedUntil = 0;
	}

	function reopenPreviewHydrationForStubs(messages) {
		(messages || []).forEach(function(message) {
			if (!message || !message.previewStub || message.preview) {
				return;
			}

			const id = Number(message.messageId || 0);
			if (!Number.isFinite(id) || id <= 0) {
				return;
			}

			requestedPreviewHydrationIds.delete(id);
			pendingPreviewHydrationIds.delete(id);
		});
	}

	function messageListScrollBusy() {
		return monotonicNow() < messageListScrollActiveUntil;
	}

	function previewHydrationPaused() {
		return monotonicNow() < previewHydrationPausedUntil;
	}

	function noteMessageListScrollActivity() {
		messageListScrollActiveUntil = monotonicNow() + previewHydrationScrollIdleMs;
		if (previewHydrationScrollIdleTimer) {
			clearTimeout(previewHydrationScrollIdleTimer);
		}
		previewHydrationScrollIdleTimer = setTimeout(function() {
			previewHydrationScrollIdleTimer = 0;
			if (pendingPreviewHydrationIds.size) {
				schedulePreviewHydrationFlush(0);
			}
		}, previewHydrationScrollIdleMs + 20);
	}

	function previewHydrationTargetForMessageId(messageId) {
		if (!refs.messageList) {
			return null;
		}

		const id = String(messageId || "").replace(/\\/g, "\\\\").replace(/"/g, "\\\"");
		if (!id) {
			return null;
		}

		return refs.messageList.querySelector(".message-bubble[data-message-id=\"" + id + "\"]");
	}

	function previewHydrationTargetNearViewport(messageId) {
		const target = previewHydrationTargetForMessageId(messageId);
		if (!target || !target.querySelector(".preview-stub, .mumble-inline-image-placeholder")) {
			pendingPreviewHydrationIds.delete(messageId);
			requestedPreviewHydrationIds.delete(messageId);
			return false;
		}

		const rootRect = refs.messageList.getBoundingClientRect();
		const targetRect = target.getBoundingClientRect();
		const margin = previewHydrationViewportMarginPx;
		return targetRect.bottom >= rootRect.top - margin
			&& targetRect.top <= rootRect.bottom + margin;
	}

	function flushPreviewHydrationQueue() {
		previewHydrationTimer = 0;
		if (activeMessageChunkRender) {
			schedulePreviewHydrationFlush();
			return;
		}
		if (messageListScrollBusy()) {
			schedulePreviewHydrationFlush(previewHydrationScrollIdleMs);
			return;
		}
		if (previewHydrationPaused()) {
			schedulePreviewHydrationFlush(previewHydrationPausedUntil - monotonicNow() + 20);
			return;
		}

		const scopeToken = currentScopeToken();
		if (!scopeToken || !pendingPreviewHydrationIds.size) {
			return;
		}

		const ids = [];
		Array.from(pendingPreviewHydrationIds).some(function(id) {
			if (ids.length >= previewHydrationBatchSize) {
				return true;
			}
			if (previewHydrationTargetNearViewport(id)) {
				ids.push(id);
			}
			return false;
		});
		if (!ids.length) {
			return;
		}

		ids.forEach(function(id) {
			pendingPreviewHydrationIds.delete(id);
		});
		if (!notifyBridge("hydrateMessagePreviews", scopeToken, ids, false)) {
			ids.forEach(function(id) {
				requestedPreviewHydrationIds.delete(id);
				pendingPreviewHydrationIds.add(id);
			});
		}
		if (ids.length >= previewHydrationBatchSize) {
			schedulePreviewHydrationFlush();
		}
	}

	function schedulePreviewHydrationFlush(delayMs) {
		if (previewHydrationTimer) {
			return;
		}

		const requestedDelay = Number.isFinite(Number(delayMs)) ? Math.max(0, Number(delayMs)) : previewHydrationDebounceMs;
		const scrollDelay = messageListScrollBusy()
			? Math.max(0, messageListScrollActiveUntil - monotonicNow() + 20)
			: 0;
		const pauseDelay = previewHydrationPaused()
			? Math.max(0, previewHydrationPausedUntil - monotonicNow() + 20)
			: 0;
		previewHydrationTimer = setTimeout(flushPreviewHydrationQueue,
			Math.max(requestedDelay, scrollDelay, pauseDelay));
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
			rootMargin: previewHydrationViewportMarginPx + "px 0px",
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

	function messageListHasRenderedTimeline() {
		return !!refs.messageList
			&& !!refs.messageList.querySelector(".message-cluster, .system-message, .message-log, .empty-state");
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
		cancelActiveMessageChunkRender("scope-loading");
		pendingMessageUpdatePatches = [];
		if (messageListHasRenderedTimeline()) {
			refs.messageList.classList.remove("is-chat-loading");
			refs.messageList.classList.add("is-chat-transitioning");
			return;
		}
		if (refs.messageList.classList.contains("is-chat-loading")
				&& refs.messageList.querySelector(".chat-loading-spinner")) {
			return;
		}

		const fragment = document.createDocumentFragment();
		fragment.appendChild(createChatLoadingIndicator());
		refs.messageList.classList.add("is-chat-loading");
		refs.messageList.classList.remove("is-chat-transitioning");
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
			refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
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

	function applyStatePill(element, label, tone, tooltip) {
		element.textContent = label;
		element.className = "state-pill";
		element.title = tooltip || label || "";
		if (tone) {
			element.classList.add("is-" + tone);
		}
	}

	function appCanCancelConnection(app) {
		return Object.prototype.hasOwnProperty.call(app || {}, "canCancelConnection")
			? !!app.canCancelConnection
			: !!(app && app.canDisconnect);
	}

	function connectionBannerState(app) {
		const state = String(app && app.connectionState || "").toLowerCase();
		if (!state || state === "connected") {
			return null;
		}

		if (state === "retrying") {
			const remainingMs = Number(app.connectionRetryRemainingMs || 0);
			const remainingSeconds = remainingMs > 0 ? Math.max(1, Math.ceil(remainingMs / 1000)) : 0;
			return {
				tone: "retry",
				title: "Connection lost - reconnecting...",
				detail: remainingSeconds > 0
					? "Automatic reconnect will retry in " + remainingSeconds + "s."
					: (app.connectionTooltip || app.connectionLabel || "Automatic reconnect is scheduled."),
				action: appCanCancelConnection(app) ? "cancel" : ""
			};
		}

		if (state === "connecting") {
			return {
				tone: "warning",
				title: "Connecting to server...",
				detail: app.connectionTooltip || app.connectionLabel || "Opening the voice connection.",
				action: appCanCancelConnection(app) ? "cancel" : ""
			};
		}

		if (state === "disconnected") {
			return {
				tone: "danger",
				title: "You're disconnected",
				detail: app.connectionTooltip || "Open the server browser to reconnect.",
				action: app && app.canConnect ? "connect" : ""
			};
		}

		return null;
	}

	function updateBannerState(app) {
		const update = app && app.updateBanner;
		if (!update || typeof update !== "object" || !update.visible) {
			return null;
		}

		const actions = Array.isArray(update.actions)
			? update.actions.filter(function(action) {
				return action && action.id && action.label && action.enabled !== false;
			})
			: [];
		const percent = Number(update.progressPercent);
		return {
			tone: String(update.tone || "accent").trim().toLowerCase() || "accent",
			title: String(update.title || "Mumble update"),
			detail: String(update.detail || ""),
			actions: actions,
			progressVisible: !!update.progressVisible,
			progressIndeterminate: !!update.progressIndeterminate || !(percent >= 0),
			progressPercent: percent >= 0 ? Math.max(0, Math.min(100, percent)) : -1,
			progressLabel: String(update.progressLabel || "")
		};
	}

	function primaryBannerState(app) {
		return updateBannerState(app) || connectionBannerState(app);
	}

	function renderConnectionOrScopeBanner(app, scope) {
		if (!refs.scopeBanner) {
			return;
		}

		const banner = primaryBannerState(app || {});
		const scopeText = String(scope && scope.banner || "");
		refs.scopeBanner.replaceChildren();
		refs.scopeBanner.className = "banner connection-banner";
		if (!banner && !scopeText) {
			refs.scopeBanner.classList.add("hidden");
			return;
		}

		refs.scopeBanner.classList.remove("hidden");
		if (!banner) {
			refs.scopeBanner.classList.add("is-scope");
			refs.scopeBanner.textContent = scopeText;
			return;
		}

		refs.scopeBanner.classList.add("is-" + banner.tone);
		const copy = document.createElement("div");
		copy.className = "connection-banner-copy";
		const title = document.createElement("strong");
		title.textContent = banner.title;
		const detail = document.createElement("span");
		detail.textContent = banner.detail;
		copy.append(title, detail);
		if (banner.progressVisible) {
			const progress = document.createElement("div");
			progress.className = "connection-banner-progress";
			const track = document.createElement("div");
			track.className = "connection-banner-progress-track";
			track.setAttribute("role", "progressbar");
			track.setAttribute("aria-label", banner.title);
			const bar = document.createElement("div");
			bar.className = "connection-banner-progress-bar";
			if (banner.progressIndeterminate) {
				progress.classList.add("is-indeterminate");
			} else {
				track.setAttribute("aria-valuemin", "0");
				track.setAttribute("aria-valuemax", "100");
				track.setAttribute("aria-valuenow", String(Math.round(banner.progressPercent)));
				bar.style.width = String(banner.progressPercent) + "%";
			}
			track.appendChild(bar);
			progress.appendChild(track);
			const label = document.createElement("span");
			label.className = "connection-banner-progress-label";
			label.textContent = banner.progressLabel || (banner.progressIndeterminate
				? "Working"
				: String(Math.round(banner.progressPercent)) + "%");
			progress.appendChild(label);
			copy.appendChild(progress);
		}
		refs.scopeBanner.appendChild(copy);

		const bannerActions = Array.isArray(banner.actions) && banner.actions.length
			? banner.actions
			: (banner.action ? [{ id: banner.action, label: banner.action === "connect" ? "Reconnect" : "Cancel" }] : []);
		if (bannerActions.length) {
			const actions = document.createElement("div");
			actions.className = "connection-banner-actions";
			bannerActions.forEach(function(bannerAction) {
				const actionId = String(bannerAction.id || "");
				if (!actionId) {
					return;
				}
				const button = document.createElement("button");
				button.type = "button";
				button.className = "chip-button connection-banner-action";
				if (bannerAction.tone) {
					button.classList.add("is-" + String(bannerAction.tone).trim().toLowerCase());
				}
				button.textContent = String(bannerAction.label || actionId);
				button.addEventListener("click", function() {
					if (actionId === "connect") {
						notifyBridge("openModernDialog", "connect", {});
						return;
					}
					if (actionId === "cancel") {
						notifyBridge("disconnectServer");
						return;
					}
					notifyBridge("invokeAppAction", actionId);
				});
				actions.appendChild(button);
			});
			refs.scopeBanner.appendChild(actions);
		}
	}

	function kindChipKind(kindLabel) {
		switch (String(kindLabel || "").toLowerCase()) {
			case "activity":
				return "activity";
			case "voice room":
				return "voice";
			case "text room":
				return "text";
			case "direct message":
				return "direct";
			default:
				return "text";
		}
	}

	function kindChipText(kindLabel) {
		switch (kindChipKind(kindLabel)) {
			case "activity":
				return "LOG";
			case "text":
				return "#";
			case "direct":
				return "DM";
			default:
				return "";
		}
	}

	function kindChipIconSvg(kindLabel) {
		if (kindChipKind(kindLabel) !== "voice") {
			return "";
		}
		return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M15.5 8.5a5 5 0 010 7\"></path><path d=\"M18.5 5.5a9 9 0 010 13\"></path></svg>";
	}

	function privateModeGhostSvg() {
		return "<svg class=\"private-mode-ghost\" viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path class=\"private-mode-ghost-body\" d=\"M5.75 20V10.25A6.25 6.25 0 0112 4a6.25 6.25 0 016.25 6.25V20l-2.15-1.45L14.05 20 12 18.55 9.95 20 7.9 18.55 5.75 20z\"></path><circle class=\"private-mode-ghost-eye\" cx=\"9.8\" cy=\"11\" r=\"1.35\"></circle><circle class=\"private-mode-ghost-eye\" cx=\"14.2\" cy=\"11\" r=\"1.35\"></circle></svg>";
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

	function stonksTickerLookupSymbol(ticker) {
		return normalizeStonksSymbol(ticker && (ticker.providerSymbol || ticker.symbol));
	}

	function stonksTickerDedupKeys(ticker) {
		const keys = [
			stonksTickerLookupSymbol(ticker),
			normalizeStonksSymbol(ticker && ticker.symbol),
			normalizeStonksSymbol(ticker && ticker.providerSymbol)
		].filter(Boolean);
		return Array.from(new Set(keys));
	}

	function stonksTickerKeySet(tickers) {
		const keys = new Set();
		(tickers || []).forEach(function(ticker) {
			stonksTickerDedupKeys(ticker).forEach(function(key) {
				keys.add(key);
			});
		});
		return keys;
	}

	function stonksTickerMatchesKeySet(ticker, keys) {
		if (!keys || !keys.size) {
			return false;
		}
		return stonksTickerDedupKeys(ticker).some(function(key) {
			return keys.has(key);
		});
	}

	function stonksPopularTickerRows(stonks, excludedTickerKeys, limit) {
		const maxRows = limit === Number.POSITIVE_INFINITY
			? Number.MAX_SAFE_INTEGER
			: (Number.isFinite(Number(limit)) ? Math.max(0, Number(limit)) : 5);
		return (Array.isArray(stonks.popularTickers) ? stonks.popularTickers : []).map(function(ticker) {
			const symbol = normalizeStonksSymbol(ticker && ticker.symbol);
			if (!symbol) {
				return null;
			}
			return {
				symbol: symbol,
				displayName: String(ticker.displayName || "").trim(),
				holderCount: Number(ticker.holderCount || 0),
				totalQuantity: stonksNumber(ticker.totalQuantity),
				totalMarketValue: stonksNumber(ticker.totalMarketValue),
				currency: String(ticker.currency || "").trim().toUpperCase(),
				providerId: String(ticker.providerId || "").trim(),
				providerSymbol: normalizeStonksSymbol(ticker.providerSymbol || symbol) || symbol,
				exchange: String(ticker.exchange || "").trim(),
				quoteSourceUrl: String(ticker.quoteSourceUrl || stonksYahooQuoteUrl(symbol)).trim(),
				latestUpdatedAt: Number(ticker.latestUpdatedAt || 0),
				source: "popular"
			};
		}).filter(function(ticker) {
			return !!ticker && !stonksTickerMatchesKeySet(ticker, excludedTickerKeys);
		}).slice(0, maxRows);
	}

	function stonksPersonalTickerRows(stonks, limit) {
		const maxRows = limit === Number.POSITIVE_INFINITY
			? Number.MAX_SAFE_INTEGER
			: (Number.isFinite(Number(limit)) ? Math.max(0, Number(limit)) : 5);
		const personal = Array.isArray(stonks.personalTickers) ? stonks.personalTickers : [];
		const rows = [];
		const seen = new Set();
		function add(row) {
			if (!row || !row.symbol || seen.has(row.symbol)) {
				return;
			}
			seen.add(row.symbol);
			rows.push(row);
		}
		if (personal.length) {
			personal.forEach(function(ticker) {
				const symbol = normalizeStonksSymbol(ticker && ticker.symbol);
				if (!symbol) {
					return;
				}
				add({
					symbol: symbol,
					displayName: String(ticker.displayName || "").trim(),
					holderCount: 1,
					totalQuantity: stonksNumber(ticker.totalQuantity),
					totalMarketValue: stonksNumber(ticker.totalMarketValue),
					currency: String(ticker.currency || "").trim().toUpperCase(),
					providerId: String(ticker.providerId || "").trim(),
					providerSymbol: normalizeStonksSymbol(ticker.providerSymbol || symbol) || symbol,
					exchange: String(ticker.exchange || "").trim(),
					quoteSourceUrl: String(ticker.quoteSourceUrl || stonksYahooQuoteUrl(symbol)).trim(),
					latestUpdatedAt: Number(ticker.latestUpdatedAt || 0),
					source: "mine"
				});
			});
		}

		const latest = stonksLatestSnapshot(stonks);
		const positions = latest && Array.isArray(latest.positions) ? latest.positions : [];
		positions.map(function(position) {
			const normalized = stonksNormalizePosition(position, latest && latest.currency || "USD");
			if (!normalized.symbol || !(stonksNumber(normalized.marketValue) > 0 || stonksNumber(normalized.quantity) > 0)) {
				return null;
			}
			return Object.assign({}, normalized, {
				holderCount: 1,
				totalQuantity: stonksNumber(normalized.quantity),
				totalMarketValue: stonksNumber(normalized.marketValue),
				latestUpdatedAt: Number(latest && latest.createdAt || normalized.quoteTime || 0),
				source: "mine"
			});
		}).filter(Boolean).sort(function(left, right) {
			return stonksNumber(right.marketValue || right.totalMarketValue)
				- stonksNumber(left.marketValue || left.totalMarketValue);
		}).forEach(add);
		return rows.slice(0, maxRows);
	}

	function stonksPinnedTickerRows(stonks) {
		return (Array.isArray(stonks.pinnedTickers) ? stonks.pinnedTickers : []).map(function(ticker) {
			const symbol = normalizeStonksSymbol(ticker && (ticker.symbol || ticker.ticker));
			if (!symbol) {
				return null;
			}
			return {
				symbol: symbol,
				displayName: String(ticker.displayName || "").trim(),
				holderCount: 1,
				totalQuantity: stonksNumber(ticker.totalQuantity),
				totalMarketValue: stonksNumber(ticker.totalMarketValue),
				currency: String(ticker.currency || "").trim().toUpperCase(),
				providerId: String(ticker.providerId || "").trim(),
				providerSymbol: normalizeStonksSymbol(ticker.providerSymbol || symbol) || symbol,
				exchange: String(ticker.exchange || "").trim(),
				quoteSourceUrl: String(ticker.quoteSourceUrl || stonksYahooQuoteUrl(symbol)).trim(),
				latestUpdatedAt: Number(ticker.latestUpdatedAt || ticker.updatedAt || ticker.createdAt || 0),
				source: "pinned"
			};
		}).filter(Boolean);
	}

	function stonksFeedPreferences(stonks) {
		const preferences = stonks && stonks.feedPreferences && typeof stonks.feedPreferences === "object"
			? stonks.feedPreferences
			: {};
		return {
			showMine: preferences.showMine !== false,
			showPopular: preferences.showPopular !== false,
			showPins: preferences.showPins !== false
		};
	}

	function stonksHeaderTickerRows(stonks) {
		const preferences = stonksFeedPreferences(stonks);
		const pinnedTickers = preferences.showPins ? stonksPinnedTickerRows(stonks) : [];
		const pinnedKeys = stonksTickerKeySet(pinnedTickers);
		const personalLimit = preferences.showPopular ? 8 : Number.POSITIVE_INFINITY;
		const personalTickers = preferences.showMine
			? stonksPersonalTickerRows(stonks, personalLimit).filter(function(ticker) {
				return !stonksTickerMatchesKeySet(ticker, pinnedKeys);
			})
			: [];
		const usedKeys = stonksTickerKeySet(pinnedTickers.concat(personalTickers));
		const popularTickers = preferences.showPopular ? stonksPopularTickerRows(stonks, usedKeys, 5) : [];
		return {
			pinnedTickers: pinnedTickers,
			personalTickers: personalTickers,
			popularTickers: popularTickers
		};
	}

	function stonksTickerQuote(symbol, stonks) {
		const normalized = normalizeStonksSymbol(symbol);
		if (!normalized) {
			return null;
		}
		const stateQuotes = stonks && stonks.tickerQuotes;
		if (stateQuotes && typeof stateQuotes === "object") {
			const direct = stateQuotes[normalized] || stateQuotes[String(symbol || "").trim().toUpperCase()];
			if (direct) {
				return direct;
			}
		}
		return stonksPopularQuoteCache[normalized] || null;
	}

	function stonksTickerQuoteTone(quote) {
		if (!quote || quote.pending) {
			return "is-loading";
		}
		if (!quote.ok) {
			return "is-unavailable";
		}
		const changePercent = Number(quote.changePercent);
		if (!Number.isFinite(changePercent) || Math.abs(changePercent) < 0.005) {
			return "is-flat";
		}
		return changePercent > 0 ? "is-positive" : "is-negative";
	}

	function stonksTickerQuoteLabel(quote) {
		if (!quote || quote.pending) {
			return "...";
		}
		if (!quote.ok) {
			return "-";
		}
		const changePercent = Number(quote.changePercent);
		if (Number.isFinite(changePercent)) {
			return formatStonksPercent(changePercent);
		}
		const price = Number(quote.price);
		return Number.isFinite(price) ? formatStonksMoney(price, quote.currency || "USD") : "-";
	}

	function stonksTickerTooltip(ticker, quote) {
		const parts = [];
		const name = String(ticker.displayName || "").trim();
		if (name && name !== ticker.symbol) {
			parts.push(name);
		}
		if (ticker.source === "popular") {
			const holders = Number(ticker.holderCount || 0);
			parts.push(holders === 1 ? "1 holder" : holders + " holders");
		} else if (ticker.source === "pinned") {
			parts.push("Pinned ticker");
		} else {
			parts.push("Your latest portfolio position");
		}
		if (quote && quote.ok) {
			const price = Number(quote.price);
			if (Number.isFinite(price)) {
				parts.push(formatStonksMoney(price, quote.currency || ticker.currency || "USD"));
			}
			if (quote.quoteTime) {
				parts.push("Quote " + formatStonksTime(quote.quoteTime));
			}
		} else if (quote && quote.error) {
			parts.push(String(quote.error));
		}
		return parts.filter(Boolean).join(" / ");
	}

	function requestStonksTickerQuote(ticker, force) {
		const symbol = stonksTickerLookupSymbol(ticker);
		if (!symbol) {
			return;
		}
		const now = Date.now();
		const cached = stonksPopularQuoteCache[symbol];
		if (!force && cached) {
			if (cached.pending || (now - Number(cached.fetchedAt || 0)) < stonksPopularQuoteStaleMs) {
				return;
			}
		}
		const requestId = "stonks-popular-" + now + "-" + Math.random().toString(36).slice(2);
		stonksPopularQuoteRequests[requestId] = { symbol: symbol };
		stonksPopularQuoteCache[symbol] = Object.assign({}, cached || {}, {
			pending: true,
			requestedAt: now
		});
		if (!notifyBridge("lookupFinanceQuote", requestId, symbol)) {
			delete stonksPopularQuoteRequests[requestId];
			stonksPopularQuoteCache[symbol] = {
				ok: false,
				pending: false,
				error: "Quote bridge unavailable.",
				fetchedAt: now
			};
		}
	}

	function requestStonksTickerQuotes(tickers, force) {
		const seen = new Set();
		(tickers || []).forEach(function(ticker) {
			const symbol = stonksTickerLookupSymbol(ticker);
			if (!symbol || seen.has(symbol)) {
				return;
			}
			seen.add(symbol);
			requestStonksTickerQuote(ticker, force);
		});
	}

	function handleStonksPopularQuoteLookupResult(result) {
		const requestId = String(result && result.requestId || "");
		const request = stonksPopularQuoteRequests[requestId];
		if (!request) {
			return false;
		}

		delete stonksPopularQuoteRequests[requestId];
		const requestedSymbol = normalizeStonksSymbol(request.symbol);
		const quoteSymbol = normalizeStonksSymbol(result && (result.symbol || result.providerSymbol || request.symbol));
		const quote = Object.assign({}, result || {}, {
			symbol: quoteSymbol || requestedSymbol,
			pending: false,
			fetchedAt: Date.now()
		});
		if (requestedSymbol) {
			stonksPopularQuoteCache[requestedSymbol] = quote;
		}
		if (quoteSymbol && quoteSymbol !== requestedSymbol) {
			stonksPopularQuoteCache[quoteSymbol] = quote;
		}
		renderStonksChatHeader(getSnapshot());
		return true;
	}

	function stopStonksVisibleRefreshTimer() {
		if (stonksVisibleRefreshTimer) {
			clearTimeout(stonksVisibleRefreshTimer);
			stonksVisibleRefreshTimer = 0;
		}
	}

	function scheduleStonksVisibleRefresh() {
		if (stonksVisibleRefreshTimer) {
			return;
		}
		stonksVisibleRefreshTimer = setTimeout(function() {
			stonksVisibleRefreshTimer = 0;
			const snapshot = getSnapshot();
			const stonks = (snapshot.app || {}).stonks || {};
			if (!stonks.supported || stonks.enabled === false) {
				return;
			}
			const headerTickers = stonksHeaderTickerRows(stonks);
			const visibleTickers = headerTickers.pinnedTickers
				.concat(headerTickers.personalTickers, headerTickers.popularTickers);
			if (!activeScopeIsStonksRoom(snapshot) && !visibleTickers.length) {
				return;
			}
			notifyBridge("invokeAppAction", "stonks.refreshVisible");
			requestStonksTickerQuotes(visibleTickers, true);
			scheduleStonksVisibleRefresh();
		}, stonksVisibleRefreshMs);
	}

	function visibleModernUiTweaks(snapshot) {
		const app = (snapshot || getSnapshot()).app || {};
		if (modernDialogState && modernDialogState.open && modernDialogState.uiTweaks) {
			return modernDialogState.uiTweaks;
		}
		return detachedModernDialogUiTweaks || app.uiTweaks || {};
	}

	function stonksTickerBannerAlwaysScroll(snapshot) {
		return !!visibleModernUiTweaks(snapshot).tickerBannerAlwaysScroll;
	}

	function syncStonksTickerScrollState() {
		stonksTickerScrollFrame = 0;
		const header = refs.stonksLeaderboardHeader;
		if (!header || header.classList.contains("hidden")) {
			return;
		}
		const viewport = header.querySelector(".stonks-chat-header-viewport");
		const track = header.querySelector(".stonks-chat-header-track");
		const primaryRun = header.querySelector(".stonks-chat-header-run:not(.is-clone)");
		if (!viewport || !track || !primaryRun) {
			header.classList.remove("is-scrolling", "can-scroll", "is-static");
			delete header.dataset.stonksMarqueeSignature;
			return;
		}
		const hasTickerContent = !!primaryRun.children.length;
		const overflows = hasTickerContent && primaryRun.scrollWidth > viewport.clientWidth + 1;
		const alwaysScroll = hasTickerContent && stonksTickerBannerAlwaysScroll();
		const shouldScroll = overflows || alwaysScroll;
		const gap = stonksTickerMarqueeGap(track);
		const distance = Math.max(1, Math.round(primaryRun.scrollWidth + gap));
		const runCount = shouldScroll
			? stonksTickerRunCount(viewport.clientWidth, primaryRun.scrollWidth, gap)
			: 1;
		const clonesChanged = syncStonksTickerCloneRuns(track, primaryRun, runCount);
		if (shouldScroll) {
			const durationSeconds = Math.max(18, Math.min(60, Math.round(distance / 28)));
			header.style.setProperty("--stonks-marquee-duration", durationSeconds + "s");
			header.style.setProperty("--stonks-marquee-distance", "-" + distance + "px");
			const signature = [
				runCount,
				durationSeconds,
				distance,
				Math.round(viewport.clientWidth),
				overflows ? "overflow" : "fit"
			].join(":");
			if (clonesChanged || header.dataset.stonksMarqueeSignature !== signature) {
				header.dataset.stonksMarqueeSignature = signature;
				restartStonksTickerAnimation(track);
			}
		} else {
			header.style.removeProperty("--stonks-marquee-duration");
			header.style.removeProperty("--stonks-marquee-distance");
			delete header.dataset.stonksMarqueeSignature;
		}
		header.classList.toggle("can-scroll", overflows);
		header.classList.toggle("is-scrolling", shouldScroll);
		header.classList.toggle("is-static", hasTickerContent && !shouldScroll);
	}

	function stonksTickerMarqueeGap(track) {
		if (!track || typeof window.getComputedStyle !== "function") {
			return 0;
		}
		const style = window.getComputedStyle(track);
		const value = style.getPropertyValue("--stonks-marquee-gap") || style.columnGap || style.gap || "0";
		const gap = Number.parseFloat(value);
		return Number.isFinite(gap) ? Math.max(0, gap) : 0;
	}

	function stonksTickerRunCount(viewportWidth, runWidth, gap) {
		const safeViewportWidth = Math.max(0, Number(viewportWidth) || 0);
		const safeRunWidth = Math.max(1, Number(runWidth) || 1);
		const safeGap = Math.max(0, Number(gap) || 0);
		const periodWidth = Math.max(1, safeRunWidth + safeGap);
		const runCount = Math.ceil((safeViewportWidth + safeRunWidth + (safeGap * 2)) / periodWidth);
		return Math.max(2, Math.min(16, runCount));
	}

	function syncStonksTickerCloneRuns(track, primaryRun, runCount) {
		const targetCloneCount = Math.max(0, runCount - 1);
		const clones = Array.from(track.querySelectorAll(".stonks-chat-header-run.is-clone"));
		let changed = false;
		while (clones.length > targetCloneCount) {
			const clone = clones.pop();
			clone.remove();
			changed = true;
		}
		while (clones.length < targetCloneCount) {
			const clone = primaryRun.cloneNode(true);
			clone.classList.add("is-clone");
			clone.setAttribute("aria-hidden", "true");
			track.appendChild(clone);
			clones.push(clone);
			changed = true;
		}
		return changed;
	}

	function restartStonksTickerAnimation(track) {
		if (!track) {
			return;
		}
		track.style.animation = "none";
		void track.offsetWidth;
		track.style.removeProperty("animation");
	}

	function scheduleStonksTickerScrollSync() {
		if (stonksTickerScrollFrame) {
			return;
		}
		stonksTickerScrollFrame = requestAnimationFrame(syncStonksTickerScrollState);
	}

	function appendStonksTickerGroup(parent, labelText, tickers, emptyText, stonks) {
		if (!tickers.length && !emptyText) {
			return;
		}

		const group = document.createElement("div");
		group.className = "stonks-chat-header-group";
		const label = document.createElement("span");
		label.className = "stonks-chat-header-label";
		label.textContent = labelText;
		group.appendChild(label);

		if (!tickers.length) {
			const empty = document.createElement("span");
			empty.className = "stonks-chat-header-empty";
			empty.textContent = emptyText;
			group.appendChild(empty);
			parent.appendChild(group);
			return;
		}

		tickers.forEach(function(ticker) {
			const quote = stonksTickerQuote(stonksTickerLookupSymbol(ticker), stonks);
			const item = document.createElement("span");
			item.className = "stonks-chat-header-ticker " + stonksTickerQuoteTone(quote);
			item.innerHTML = "<strong></strong><span></span>";
			item.querySelector("strong").textContent = ticker.symbol;
			item.querySelector("span").textContent = stonksTickerQuoteLabel(quote);
			item.title = stonksTickerTooltip(ticker, quote);
			group.appendChild(item);
		});
		parent.appendChild(group);
	}

	function clearStonksTickerHeader(header) {
		if (!header) {
			return;
		}
		stonksTickerRenderSignature = "";
		header.innerHTML = "";
		header.classList.remove("can-scroll", "is-scrolling", "is-static");
		header.style.removeProperty("--stonks-marquee-duration");
		header.style.removeProperty("--stonks-marquee-distance");
		delete header.dataset.stonksMarqueeSignature;
	}

	function stonksTickerHeaderRenderSignature(pinnedTickers, personalTickers, popularTickers, stonks) {
		const tickerSignature = function(ticker) {
			const quote = stonksTickerQuote(stonksTickerLookupSymbol(ticker), stonks);
			return {
				symbol: ticker.symbol,
				tone: stonksTickerQuoteTone(quote),
				label: stonksTickerQuoteLabel(quote),
				title: stonksTickerTooltip(ticker, quote)
			};
		};
		return JSON.stringify({
			pinned: pinnedTickers.map(tickerSignature),
			yours: personalTickers.map(tickerSignature),
			popular: popularTickers.map(tickerSignature)
		});
	}

	function renderStonksChatHeader(snapshot) {
		if (!refs.stonksLeaderboardHeader) {
			return;
		}

		const app = snapshot.app || {};
		const stonks = app.stonks || {};
		const headerTickers = stonksHeaderTickerRows(stonks);
		const pinnedTickers = headerTickers.pinnedTickers;
		const personalTickers = headerTickers.personalTickers;
		const popularTickers = headerTickers.popularTickers;
		const hasTickers = pinnedTickers.length || personalTickers.length || popularTickers.length;
		const connected = !!app.canDisconnect;
		const supported = !!stonks.supported;
		const automationHeaderVisible = !!stonks.automationHeaderVisible;
		const visible = (connected || automationHeaderVisible) && stonks.enabled !== false;
		const showTickerHeader = visible && supported && (hasTickers || automationHeaderVisible);
		refs.stonksLeaderboardHeader.classList.toggle("hidden", !showTickerHeader);
		refs.stonksLeaderboardHeader.classList.toggle("is-empty", visible && supported && !hasTickers);
		refs.stonksLeaderboardHeader.classList.toggle("is-unavailable", visible && !supported);
		refs.stonksLeaderboardHeader.title = hasTickers
			? "Followed tickers - click to open Stonks"
			: (supported ? "Waiting for Stonks tickers - click to open Stonks" : "This server has not advertised Stonks support");
		if (!visible) {
			clearStonksTickerHeader(refs.stonksLeaderboardHeader);
			stopStonksVisibleRefreshTimer();
			return;
		}
		if (!supported) {
			clearStonksTickerHeader(refs.stonksLeaderboardHeader);
			stopStonksVisibleRefreshTimer();
			return;
		}

		if (stonks.disableQuoteLookup) {
			stopStonksVisibleRefreshTimer();
		} else {
			requestStonksTickerQuotes(pinnedTickers.concat(personalTickers, popularTickers), false);
			scheduleStonksVisibleRefresh();
		}
		if (!hasTickers) {
			const now = Date.now();
			if (!stonks.disableQuoteLookup && (now - stonksEmptyStateRefreshAt) > stonksEmptyStateRefreshMs) {
				stonksEmptyStateRefreshAt = now;
				notifyBridge("invokeAppAction", "stonks.refreshVisible");
			}
			clearStonksTickerHeader(refs.stonksLeaderboardHeader);
			return;
		}
		const nextSignature = stonksTickerHeaderRenderSignature(pinnedTickers, personalTickers, popularTickers, stonks);
		if (stonksTickerRenderSignature === nextSignature
				&& refs.stonksLeaderboardHeader.querySelector(".stonks-chat-header-viewport")) {
			scheduleStonksTickerScrollSync();
			return;
		}
		stonksTickerRenderSignature = nextSignature;
		refs.stonksLeaderboardHeader.innerHTML = "";
		refs.stonksLeaderboardHeader.classList.remove("can-scroll", "is-scrolling", "is-static");
		refs.stonksLeaderboardHeader.style.removeProperty("--stonks-marquee-duration");
		refs.stonksLeaderboardHeader.style.removeProperty("--stonks-marquee-distance");
		delete refs.stonksLeaderboardHeader.dataset.stonksMarqueeSignature;
		appendStonksTickerGroup(refs.stonksLeaderboardHeader, "Pinned", pinnedTickers, "", stonks);
		appendStonksTickerGroup(refs.stonksLeaderboardHeader, "Portfolio", personalTickers, "", stonks);
		appendStonksTickerGroup(refs.stonksLeaderboardHeader, "Popular", popularTickers, "", stonks);
		const viewport = document.createElement("div");
		viewport.className = "stonks-chat-header-viewport";
		const track = document.createElement("div");
		track.className = "stonks-chat-header-track";
		const primaryRun = document.createElement("div");
		primaryRun.className = "stonks-chat-header-run";
		while (refs.stonksLeaderboardHeader.firstChild) {
			primaryRun.appendChild(refs.stonksLeaderboardHeader.firstChild);
		}
		track.appendChild(primaryRun);
		viewport.appendChild(track);
		refs.stonksLeaderboardHeader.appendChild(viewport);
		scheduleStonksTickerScrollSync();
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

	function renderScreenShareButtonContent(label, status) {
		refs.screenShareButton.innerHTML = "<svg class=\"screen-share-button-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" width=\"15\" height=\"15\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><rect x=\"3\" y=\"4\" width=\"18\" height=\"12\" rx=\"2\"></rect><path d=\"M8 20h8\"></path><path d=\"M12 16v4\"></path></svg><span class=\"screen-share-button-label\"></span><span class=\"screen-share-button-status\"></span>";
		const labelEl = refs.screenShareButton.querySelector(".screen-share-button-label");
		const statusEl = refs.screenShareButton.querySelector(".screen-share-button-status");
		if (labelEl) {
			labelEl.textContent = label || "Share screen";
		}
		if (statusEl) {
			statusEl.textContent = status || "";
			statusEl.classList.toggle("hidden", !status);
		}
	}

	function renderScreenShareHeader(scope, share) {
		const visible = screenShareVisible(share);
		if (refs.appShell) {
			refs.appShell.classList.toggle("scope-has-screen-share", visible);
		}
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
			renderScreenShareButtonContent("Share screen", "");
			refs.screenShareButton.title = "Share screen";
			refs.screenShareButton.dataset.scopeToken = "";
			refs.screenShareButton.dataset.actionId = "";
			return;
		}

		const label = share.primaryLabel || "Share screen";
		const hint = share.primaryHint || label;
		renderScreenShareButtonContent(label, screenShareStatusText(share));
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

	function directMessageSessionValue(conversation) {
		const value = Number(conversation && (conversation.session || conversation.peerSession || 0));
		return Number.isFinite(value) && value > 0 ? value : 0;
	}

	function directMessageToken(session) {
		return "dm:" + String(Number(session || 0));
	}

	function directMessageBridgeScopeToken(session) {
		return "-2:" + String(Number(session || 0));
	}

	function isDirectMessageScopeToken(scopeToken) {
		return /^(?:dm|-2):\d+$/i.test(String(scopeToken || "").trim());
	}

	function directMessageSessionFromToken(scopeToken) {
		const match = /^(?:dm|-2):(\d+)$/i.exec(String(scopeToken || "").trim());
		if (!match) {
			return 0;
		}
		const session = Number(match[1]);
		return Number.isFinite(session) && session > 0 ? session : 0;
	}

	function directMessageState(snapshot) {
		const app = snapshot && snapshot.app ? snapshot.app : {};
		const state = app.directMessages;
		return state && typeof state === "object" ? state : null;
	}

	function directMessageConversations(state) {
		return Array.isArray(state && state.conversations) ? state.conversations.filter(Boolean) : [];
	}

	function directMessageWindows(state) {
		return Array.isArray(state && state.windows) ? state.windows.filter(Boolean) : [];
	}

	function directMessageDraftKey(session) {
		session = Number(session || 0);
		return Number.isFinite(session) && session > 0 ? String(session) : "";
	}

	function directMessageInputForSession(session) {
		const key = directMessageDraftKey(session);
		if (!key || !refs.directMessageDock) {
			return null;
		}
		const windows = Array.prototype.slice.call(
			refs.directMessageDock.querySelectorAll(".direct-message-window")
		);
		for (let index = 0; index < windows.length; index += 1) {
			if (String(windows[index].dataset.session || "") === key) {
				return windows[index].querySelector(".direct-message-input");
			}
		}
		return null;
	}

	function syncDirectMessageInputHeight(input) {
		if (!input) {
			return;
		}
		input.style.height = "0px";
		input.style.height = Math.min(input.scrollHeight, 120) + "px";
	}

	function rememberDirectMessageInputDraft(session, input) {
		const key = directMessageDraftKey(session);
		if (!key || !input) {
			return;
		}
		const value = String(input.value || "");
		if (!value) {
			delete directMessageDrafts[key];
			return;
		}
		directMessageDrafts[key] = {
			value: value,
			selectionStart: typeof input.selectionStart === "number" ? input.selectionStart : value.length,
			selectionEnd: typeof input.selectionEnd === "number" ? input.selectionEnd : value.length,
			updatedAt: Date.now()
		};
	}

	function captureDirectMessageDockState() {
		if (!refs.directMessageDock) {
			return null;
		}

		const activeElement = document.activeElement;
		let focusState = null;
		refs.directMessageDock.querySelectorAll(".direct-message-window").forEach(function(windowElement) {
			const session = Number(windowElement.dataset.session || 0);
			const key = directMessageDraftKey(session);
			if (!key) {
				return;
			}
			directMessageWindowUiState[key] = {
				minimized: windowElement.classList.contains("is-minimized")
			};
			const input = windowElement.querySelector(".direct-message-input");
			if (input) {
				rememberDirectMessageInputDraft(session, input);
				if (input === activeElement) {
					focusState = {
						session: session,
						selectionStart: typeof input.selectionStart === "number" ? input.selectionStart : null,
						selectionEnd: typeof input.selectionEnd === "number" ? input.selectionEnd : null
					};
				}
			}
		});
		return focusState;
	}

	function restoreDirectMessageDockFocus(focusState) {
		if (!focusState || !focusState.session) {
			return;
		}
		const input = directMessageInputForSession(focusState.session);
		if (!input || typeof input.focus !== "function") {
			return;
		}
		input.focus({ preventScroll: true });
		if (typeof input.setSelectionRange === "function"
				&& typeof focusState.selectionStart === "number"
				&& typeof focusState.selectionEnd === "number") {
			try {
				input.setSelectionRange(focusState.selectionStart, focusState.selectionEnd);
			} catch (error) {
				// Some input implementations reject selection restoration.
			}
		}
		syncDirectMessageInputHeight(input);
	}

	function pruneDirectMessageDraftState(windows) {
		const activeKeys = {};
		(windows || []).forEach(function(conversation) {
			const key = directMessageDraftKey(directMessageSessionValue(conversation));
			if (key) {
				activeKeys[key] = true;
			}
		});
		Object.keys(directMessageDrafts).forEach(function(key) {
			if (!activeKeys[key]) {
				delete directMessageDrafts[key];
			}
		});
		Object.keys(directMessageWindowUiState).forEach(function(key) {
			if (!activeKeys[key]) {
				delete directMessageWindowUiState[key];
			}
		});
	}

	function mergedDirectMessageConversation(state, session) {
		if (!state || !session) {
			return null;
		}

		const matchSession = function(conversation) {
			return directMessageSessionValue(conversation) === session;
		};
		const base = directMessageConversations(state).find(matchSession) || null;
		const windowState = directMessageWindows(state).find(matchSession) || null;
		if (!base && !windowState) {
			return null;
		}

		const merged = Object.assign({}, base || {}, windowState || {});
		if (!Array.isArray(merged.messages)) {
			merged.messages = Array.isArray(base && base.messages) ? base.messages : [];
		}
		if (!merged.token) {
			merged.token = directMessageToken(session);
		}
		if (!merged.session) {
			merged.session = session;
		}
		if (!merged.peerSession) {
			merged.peerSession = session;
		}
		return merged;
	}

	function activeDirectMessageConversation(snapshot) {
		const scopeToken = String(snapshot && snapshot.activeScope && snapshot.activeScope.scopeToken || "");
		const session = directMessageSessionFromToken(scopeToken);
		const state = directMessageState(snapshot);
		if (session) {
			const conversation = mergedDirectMessageConversation(state, session);
			if (conversation) {
				return conversation;
			}
		}

		const activeScope = snapshot && snapshot.activeScope ? snapshot.activeScope : {};
		const activeLabel = String(activeScope.label || "").trim();
		const conversations = directMessageConversations(state);
		const match = activeLabel
			? conversations.find(function(conversation) {
				return String(conversation && conversation.label || "").trim() === activeLabel;
			})
			: null;
		if (match) {
			return mergedDirectMessageConversation(state, directMessageSessionValue(match));
		}

		const kindLabel = String(activeScope.kindLabel || "").trim().toLowerCase();
		if (kindLabel !== "direct message") {
			return null;
		}

		const fallback = conversations[0] || null;
		return fallback ? mergedDirectMessageConversation(state, directMessageSessionValue(fallback)) : null;
	}

	function directMessageIsPrivate(conversation) {
		const mode = String(conversation && conversation.mode || "").toLowerCase();
		return !!(conversation && (conversation.privateMode || mode === "private"));
	}

	function formatDirectMessageTime(createdAtMs) {
		const value = Number(createdAtMs || 0);
		if (!Number.isFinite(value) || value <= 0) {
			return "";
		}
		const date = new Date(value);
		if (Number.isNaN(date.getTime())) {
			return "";
		}
		const hours = String(date.getHours()).padStart(2, "0");
		const minutes = String(date.getMinutes()).padStart(2, "0");
		return hours + ":" + minutes;
	}

	function directMessageTimelineMessages(conversation) {
		const peerSession = directMessageSessionValue(conversation);
		return (conversation && Array.isArray(conversation.messages) ? conversation.messages : []).map(function(entry, index) {
			const messageId = entry.messageId || entry.localId || entry.id || (peerSession + "-" + index);
			const own = !!entry.own || String(entry.direction || "").toLowerCase() === "outgoing";
			const bodyHtml = String(entry.messageHtml || "").trim()
				|| escapedMultilineText(entry.plainText || "");
			return {
				messageId: "dm-" + String(messageId),
				threadId: "dm-" + String(peerSession || "conversation"),
				createdAtMs: entry.createdAtMs || 0,
				actor: entry.actorName || (own ? "You" : (conversation.label || "Direct message")),
				actorKey: own ? "self" : ("dm-" + String(peerSession || "peer")),
				timeLabel: formatDirectMessageTime(entry.createdAtMs),
				bodyText: entry.plainText || "",
				bodyHtml: bodyHtml,
				own: own,
				system: false,
				canReply: false,
				canReact: false,
				canDelete: false,
				reactions: []
			};
		});
	}

	function directMessagePreviewText(conversation) {
		const preview = String(conversation && conversation.lastMessagePreview || "").trim();
		if (preview) {
			return (conversation.lastMessageOwn ? "You: " : "") + preview;
		}
		return String(conversation && conversation.subtitle || conversation && conversation.statusLabel || "Direct message");
	}

	function directMessageActiveScope(conversation) {
		const session = directMessageSessionValue(conversation);
		const label = String(conversation && conversation.label || "Direct message");
		const privateMode = directMessageIsPrivate(conversation);
		const canSend = conversation && conversation.canSend !== false;
		return {
			scopeToken: directMessageToken(session),
			kindLabel: "Direct message",
			label: label,
			description: privateMode ? "Private - in-memory only" : (conversation.statusLabel || "Direct message"),
			meta: [
				privateMode ? "Private mode" : (conversation.persistentHistory ? "Persistent history" : "Private transport"),
				conversation && conversation.subtitle ? conversation.subtitle : "",
				conversation && conversation.unreadCount ? String(conversation.unreadCount) + " unread" : ""
			].filter(Boolean),
			canLoadOlder: false,
			canMarkRead: !!(conversation && conversation.unreadCount),
			canSend: canSend,
			canAttachImages: false,
			composerPlaceholder: "Message " + label,
			composerHint: privateMode
				? "Private direct messages stay in memory for this session."
				: "Direct messages stay separate from room chat.",
			emptyCopy: conversation && conversation.emptyCopy
				? conversation.emptyCopy
				: "Direct messages with " + label + " will appear here.",
			screenShare: { visible: false, available: false, mode: "idle", overflowActions: [] },
			scrollToBottom: true
		};
	}

	function updateLocalDirectMessageState(mutator) {
		if (modernBridge) {
			return false;
		}
		const snapshot = getSnapshot();
		const app = snapshot.app || {};
		const state = app.directMessages;
		if (!state || typeof state !== "object" || typeof mutator !== "function") {
			return false;
		}
		snapshot.app = app;
		mutator(state, snapshot);
		render(snapshot);
		return true;
	}

	function ensureLocalDirectMessageWindow(state, session) {
		if (!state || !session) {
			return null;
		}
		if (!Array.isArray(state.conversations)) {
			state.conversations = [];
		}
		if (!Array.isArray(state.windows)) {
			state.windows = [];
		}

		let conversation = state.conversations.find(function(candidate) {
			return directMessageSessionValue(candidate) === session;
		});
		if (!conversation) {
			conversation = {
				token: directMessageToken(session),
				session: session,
				peerSession: session,
				label: "User " + session,
				subtitle: "Direct message",
				canSend: true,
				mode: state.defaultMode || "private",
				messages: []
			};
			state.conversations.push(conversation);
		}

		let windowState = state.windows.find(function(candidate) {
			return directMessageSessionValue(candidate) === session;
		});
		if (!windowState) {
			windowState = Object.assign({}, conversation);
			windowState.messages = Array.isArray(conversation.messages) ? conversation.messages.slice() : [];
			state.windows.push(windowState);
		}

		conversation.open = true;
		conversation.unreadCount = 0;
		windowState.open = true;
		windowState.unreadCount = 0;
		return windowState;
	}

	function selectLocalDirectMessage(conversation) {
		const session = directMessageSessionValue(conversation);
		if (!session) {
			return;
		}
		updateLocalDirectMessageState(function(state, snapshot) {
			const windowState = ensureLocalDirectMessageWindow(state, session) || conversation;
			(snapshot.voiceRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
			(snapshot.textRooms || []).forEach(function(room) { if (room) { room.selected = false; } });
			snapshot.activeScope = directMessageActiveScope(windowState);
			snapshot.messages = [];
		});
	}

	function invokeDirectMessageOpen(conversation, selectScope) {
		const session = directMessageSessionValue(conversation);
		if (!session) {
			return false;
		}
		hideDirectMessageTray();
		let handled = notifyBridge("openDirectMessage", session);
		if (!handled) {
			handled = updateLocalDirectMessageState(function(state) {
				ensureLocalDirectMessageWindow(state, session);
			});
		}
		if (selectScope !== false) {
			if (!notifyBridge("selectScope", directMessageBridgeScopeToken(session))) {
				selectLocalDirectMessage(conversation);
			}
		}
		notifyBridge("markDirectMessageRead", session);
		return handled;
	}

	function invokeDirectMessageClose(session) {
		session = Number(session || 0);
		if (!session) {
			return false;
		}
		if (notifyBridge("closeDirectMessage", session)) {
			return true;
		}
		return updateLocalDirectMessageState(function(state) {
			(state.conversations || []).forEach(function(conversation) {
				if (directMessageSessionValue(conversation) === session) {
					conversation.open = false;
				}
			});
			state.windows = (state.windows || []).filter(function(conversation) {
				return directMessageSessionValue(conversation) !== session;
			});
		});
	}

	function invokeDirectMessageMode(session, mode) {
		session = Number(session || 0);
		mode = String(mode || "private");
		if (!session) {
			return false;
		}
		if (notifyBridge("setDirectMessageMode", session, mode)) {
			return true;
		}
		return updateLocalDirectMessageState(function(state, snapshot) {
			const windowState = ensureLocalDirectMessageWindow(state, session);
			[state.conversations || [], state.windows || []].forEach(function(list) {
				list.forEach(function(conversation) {
					if (directMessageSessionValue(conversation) !== session) {
						return;
					}
					conversation.mode = mode === "history" ? "history" : "private";
					conversation.privateMode = conversation.mode === "private";
					conversation.persistentHistory = conversation.mode === "history";
				});
			});
			if (isDirectMessageScopeToken(snapshot.activeScope && snapshot.activeScope.scopeToken)) {
				snapshot.activeScope = directMessageActiveScope(windowState || mergedDirectMessageConversation(state, session));
			}
		});
	}

	function invokeDirectMessageSend(session, message) {
		session = Number(session || 0);
		const value = String(message || "").trim();
		if (!session || !value) {
			return false;
		}
		if (notifyBridge("sendDirectMessage", session, value)) {
			return true;
		}
		return updateLocalDirectMessageState(function(state) {
			const windowState = ensureLocalDirectMessageWindow(state, session);
			if (!windowState) {
				return;
			}
			const entry = {
				id: "local-" + Date.now(),
				localId: Date.now(),
				peerSession: session,
				actorName: "You",
				own: true,
				direction: "outgoing",
				messageHtml: escapedMultilineText(value),
				plainText: value,
				createdAtMs: Date.now()
			};
			if (!Array.isArray(windowState.messages)) {
				windowState.messages = [];
			}
			windowState.messages.push(entry);
			(state.conversations || []).forEach(function(conversation) {
				if (directMessageSessionValue(conversation) !== session) {
					return;
				}
				conversation.lastMessagePreview = value;
				conversation.lastMessageOwn = true;
				conversation.messages = windowState.messages.slice();
			});
		});
	}

	function ensureDirectMessageButton() {
		if (refs.directMessageButton && document.body.contains(refs.directMessageButton)) {
			return refs.directMessageButton;
		}
		const controls = refs.selfCard ? refs.selfCard.querySelector(".self-controls") : null;
		if (!controls) {
			return null;
		}

		const button = document.createElement("button");
		button.id = "direct-message-button";
		button.type = "button";
		button.className = "icon-button self-control direct-message-button hidden";
		button.title = "Direct messages";
		button.setAttribute("aria-label", "Direct messages");
		button.setAttribute("aria-haspopup", "dialog");
		button.setAttribute("aria-expanded", "false");
		button.innerHTML =
			"<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 5h16v11H8l-4 4z\"></path><path d=\"M8 9h8\"></path><path d=\"M8 12h5\"></path></svg>"
			+ "<span class=\"direct-message-button-badge\" hidden></span>";
		button.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			toggleDirectMessageTray();
		});

		controls.insertBefore(button, refs.stonksButton || refs.selfCardSettingsButton || null);
		refs.directMessageButton = button;
		refs.directMessageBadge = button.querySelector(".direct-message-button-badge");
		return button;
	}

	function ensureDirectMessageRailSection() {
		if (refs.directMessageSection && document.body.contains(refs.directMessageSection)) {
			return refs.directMessageSection;
		}
		if (!refs.utilityScroll) {
			return null;
		}

		const section = document.createElement("section");
		section.id = "direct-message-section";
		section.className = "rail-section direct-message-section hidden";
		section.innerHTML =
			"<div class=\"section-heading\">"
			+ "<span id=\"direct-message-section-label\" class=\"section-label\">Direct messages</span>"
			+ "<span id=\"direct-message-count\" class=\"quiet-count\">0</span>"
			+ "</div>"
			+ "<div id=\"direct-message-list\" class=\"rail-list direct-message-list\" role=\"listbox\" aria-labelledby=\"direct-message-section-label\"></div>";

		const textSection = document.getElementById("text-section");
		if (textSection && textSection.parentElement === refs.utilityScroll) {
			textSection.insertAdjacentElement("afterend", section);
		} else {
			refs.utilityScroll.appendChild(section);
		}

		refs.directMessageSection = section;
		refs.directMessageList = section.querySelector("#direct-message-list");
		refs.directMessageCount = section.querySelector("#direct-message-count");
		return section;
	}

	function ensureDirectMessageTray() {
		if (refs.directMessageTray && document.body.contains(refs.directMessageTray)) {
			return refs.directMessageTray;
		}
		const tray = document.createElement("div");
		tray.id = "direct-message-tray";
		tray.className = "direct-message-tray self-menu-popover hidden";
		tray.setAttribute("role", "dialog");
		tray.setAttribute("aria-hidden", "true");
		document.body.appendChild(tray);
		refs.directMessageTray = tray;
		return tray;
	}

	function ensureDirectMessageDock() {
		if (refs.directMessageDock && document.body.contains(refs.directMessageDock)) {
			return refs.directMessageDock;
		}
		const dock = document.createElement("div");
		dock.id = "direct-message-dock";
		dock.className = "direct-message-dock";
		document.body.appendChild(dock);
		refs.directMessageDock = dock;
		return dock;
	}

	function hideDirectMessageTray() {
		directMessageTrayOpen = false;
		if (refs.directMessageTray) {
			refs.directMessageTray.classList.add("hidden");
			refs.directMessageTray.setAttribute("aria-hidden", "true");
		}
		if (refs.directMessageButton) {
			refs.directMessageButton.setAttribute("aria-expanded", "false");
		}
	}

	function positionDirectMessageTray() {
		if (!directMessageTrayOpen || !refs.directMessageTray || !refs.directMessageButton
				|| refs.directMessageTray.classList.contains("hidden")) {
			return;
		}
		const anchorBounds = refs.directMessageButton.getBoundingClientRect();
		const trayBounds = refs.directMessageTray.getBoundingClientRect();
		const left = Math.max(8, Math.min(anchorBounds.right - trayBounds.width + 8,
			window.innerWidth - trayBounds.width - 8));
		const belowTop = anchorBounds.bottom + 10;
		const aboveTop = anchorBounds.top - trayBounds.height - 10;
		const top = (belowTop + trayBounds.height <= window.innerHeight - 8)
			? belowTop
			: Math.max(8, aboveTop);
		refs.directMessageTray.style.left = left + "px";
		refs.directMessageTray.style.top = top + "px";
	}

	function toggleDirectMessageTray(forceOpen) {
		const nextOpen = typeof forceOpen === "boolean" ? forceOpen : !directMessageTrayOpen;
		if (!nextOpen) {
			hideDirectMessageTray();
			return;
		}
		hideAppMenu();
		hideSelfMenu();
		hideContextMenu();
		directMessageTrayOpen = true;
		renderDirectMessageTray(directMessageState(getSnapshot()) || {});
	}

	function renderDirectMessageButton(state) {
		const button = ensureDirectMessageButton();
		if (!button) {
			return;
		}
		const conversations = directMessageConversations(state);
		const unreadTotal = Number(state && state.unreadTotal || conversations.reduce(function(total, conversation) {
			return total + Number(conversation.unreadCount || 0);
		}, 0));
		const available = !!state && (state.available !== false || conversations.length > 0);
		button.classList.toggle("hidden", !available);
		button.disabled = !available;
		button.classList.toggle("has-unread", unreadTotal > 0);
		button.title = unreadTotal > 0
			? String(unreadTotal) + " unread direct message" + (unreadTotal === 1 ? "" : "s")
			: (state && state.title) || "Direct messages";
		if (refs.directMessageBadge) {
			const hasUnread = unreadTotal > 0;
			refs.directMessageBadge.hidden = !hasUnread;
			refs.directMessageBadge.textContent = hasUnread ? (unreadTotal > 9 ? "9+" : String(unreadTotal)) : "";
		}
	}

	function renderDirectMessageRail(state, snapshot) {
		const section = ensureDirectMessageRailSection();
		if (!section || !refs.directMessageList) {
			return;
		}
		const conversations = directMessageConversations(state);
		const hasConversations = conversations.length > 0;
		section.classList.toggle("hidden", !hasConversations);
		if (refs.directMessageCount) {
			const unreadTotal = Number(state && state.unreadTotal || 0);
			refs.directMessageCount.textContent = String(unreadTotal || conversations.length);
			refs.directMessageCount.classList.toggle("has-unread", unreadTotal > 0);
		}
		if (!hasConversations) {
			refs.directMessageList.innerHTML = "";
			return;
		}

		const activeSession = directMessageSessionFromToken(snapshot && snapshot.activeScope && snapshot.activeScope.scopeToken);
		const fragment = document.createDocumentFragment();
		conversations.forEach(function(conversation) {
			const session = directMessageSessionValue(conversation);
			const token = conversation.token || directMessageToken(session);
			const selected = !!session && activeSession === session;
			const unread = Number(conversation.unreadCount || 0);
			const wrapper = document.createElement("div");
			wrapper.className = "rail-row-wrapper direct-message-row-wrapper";
			wrapper.setAttribute("role", "option");
			wrapper.setAttribute("aria-label", conversation.label || "Direct message");
			wrapper.setAttribute("aria-selected", selected ? "true" : "false");
			wrapper.dataset.scopeToken = token;

			const row = document.createElement("button");
			row.type = "button";
			row.className = "rail-row room-node direct-message-row"
				+ (selected ? " is-selected is-active" : "")
				+ (conversation.open ? " is-open" : "")
				+ (unread > 0 ? " has-unread" : "");
			row.dataset.scopeToken = token;
			row.dataset.session = String(session || "");
			row.title = directMessagePreviewText(conversation);

			const avatar = document.createElement("span");
			avatar.className = "dm-rail-avatar avatar";
			styleAvatar(avatar, conversation.label || "Direct message", false, conversation.avatarUrl || "");

			const copy = document.createElement("span");
			copy.className = "rail-row-copy room-copy";
			const title = document.createElement("span");
			title.className = "rail-row-title room-name";
			title.textContent = conversation.label || "Direct message";
			const subtitle = document.createElement("span");
			subtitle.className = "rail-row-subtitle room-subtitle";
			subtitle.textContent = directMessagePreviewText(conversation);
			copy.appendChild(title);
			copy.appendChild(subtitle);

			const meta = document.createElement("span");
			meta.className = "rail-row-meta room-meta";
			if (directMessageIsPrivate(conversation)) {
				const modeGlyph = document.createElement("span");
				modeGlyph.className = "dm-mode-glyph";
				modeGlyph.innerHTML = privateModeGhostSvg();
				modeGlyph.title = "Private mode";
				meta.appendChild(modeGlyph);
			}
			if (unread > 0) {
				const unreadBadge = document.createElement("span");
				unreadBadge.className = "row-count dm-unread-count";
				unreadBadge.textContent = unread > 99 ? "99+" : String(unread);
				meta.appendChild(unreadBadge);
			}

			row.appendChild(avatar);
			row.appendChild(copy);
			row.appendChild(meta);
			row.addEventListener("click", function(event) {
				event.preventDefault();
				invokeDirectMessageOpen(conversation, true);
				dismissCompactRailAfterAction();
			});
			row.addEventListener("contextmenu", function(event) {
				event.preventDefault();
				event.stopPropagation();
				showContextMenu(directMessageContextItems(conversation), event.clientX, event.clientY);
			});
			wrapper.appendChild(row);
			fragment.appendChild(wrapper);
		});
		replaceChildrenWith(refs.directMessageList, fragment);
	}

	function directMessageContextItems(conversation) {
		const session = directMessageSessionValue(conversation);
		const privateMode = directMessageIsPrivate(conversation);
		const historyUnavailableReason = conversation && conversation.historyUnavailableReason
			? String(conversation.historyUnavailableReason)
			: "Persistent history is unavailable for this direct message.";
		return normalizedActionPanelItems([
			{
				label: "Open",
				enabled: !!session,
				action: function() {
					invokeDirectMessageOpen(conversation, true);
				}
			},
			{
				label: "Mark read",
				enabled: !!session && Number(conversation.unreadCount || 0) > 0,
				action: function() {
					if (!notifyBridge("markDirectMessageRead", session)) {
						updateLocalDirectMessageState(function(state) {
							(state.conversations || []).concat(state.windows || []).forEach(function(item) {
								if (directMessageSessionValue(item) === session) {
									item.unreadCount = 0;
								}
							});
						});
					}
				}
			},
			{ separator: true },
			{
				label: privateMode ? "Use history mode" : "Use private mode",
				icon: privateMode ? "history" : "ghost",
				enabled: !!session && (!privateMode || conversation.canPersistHistory !== false),
				hint: privateMode && conversation.canPersistHistory === false ? historyUnavailableReason : "",
				action: function() {
					invokeDirectMessageMode(session, privateMode ? "history" : "private");
				}
			},
			{
				label: "Close window",
				enabled: !!session && !!conversation.open,
				tone: "danger",
				action: function() {
					invokeDirectMessageClose(session);
				}
			}
		], { hideDisabled: true });
	}

	function renderDirectMessageTray(state) {
		const tray = ensureDirectMessageTray();
		if (!tray) {
			return;
		}
		const conversations = directMessageConversations(state);
		tray.innerHTML = "";

		const header = document.createElement("div");
		header.className = "direct-message-tray-header";
		const title = document.createElement("p");
		title.className = "direct-message-tray-title";
		title.textContent = (state && state.title) || "Direct messages";
		const description = document.createElement("p");
		description.className = "direct-message-tray-description";
		description.textContent = (state && state.description) || "Private conversations stay separate from room chat.";
		header.appendChild(title);
		header.appendChild(description);
		tray.appendChild(header);

		if (!conversations.length) {
			const empty = document.createElement("div");
			empty.className = "direct-message-tray-empty";
			empty.textContent = "No conversations yet.";
			tray.appendChild(empty);
		} else {
			conversations.forEach(function(conversation) {
				const session = directMessageSessionValue(conversation);
				const row = document.createElement("button");
				row.type = "button";
				row.className = "direct-message-tray-row" + (conversation.unreadCount ? " has-unread" : "");
				row.dataset.session = String(session || "");

				const avatar = document.createElement("span");
				avatar.className = "direct-message-tray-avatar avatar";
				styleAvatar(avatar, conversation.label || "Direct message", false, conversation.avatarUrl || "");
				const copy = document.createElement("span");
				copy.className = "direct-message-tray-copy";
				const name = document.createElement("span");
				name.className = "direct-message-tray-name";
				name.textContent = conversation.label || "Direct message";
				const preview = document.createElement("span");
				preview.className = "direct-message-tray-preview";
				preview.textContent = directMessagePreviewText(conversation);
				copy.appendChild(name);
				copy.appendChild(preview);
				row.appendChild(avatar);
				row.appendChild(copy);
				if (conversation.unreadCount) {
					const badge = document.createElement("span");
					badge.className = "direct-message-tray-badge";
					badge.textContent = conversation.unreadCount > 99 ? "99+" : String(conversation.unreadCount);
					row.appendChild(badge);
				}
				row.addEventListener("click", function(event) {
					event.preventDefault();
					invokeDirectMessageOpen(conversation, true);
				});
				tray.appendChild(row);
			});
		}

		if (directMessageTrayOpen || state.trayOpen === true) {
			directMessageTrayOpen = true;
			tray.classList.remove("hidden");
			tray.setAttribute("aria-hidden", "false");
			if (refs.directMessageButton) {
				refs.directMessageButton.setAttribute("aria-expanded", "true");
			}
			requestAnimationFrame(positionDirectMessageTray);
		} else {
			hideDirectMessageTray();
		}
	}

	function renderDirectMessageWindowMessage(list, entry, conversation) {
		const message = document.createElement("article");
		const own = !!entry.own || String(entry.direction || "").toLowerCase() === "outgoing";
		message.className = "direct-message-message" + (own ? " is-own" : "");
		message.dataset.messageId = String(entry.messageId || entry.localId || entry.id || "");

		const avatar = document.createElement("span");
		avatar.className = "direct-message-message-avatar avatar";
		styleAvatar(avatar, entry.actorName || (own ? "You" : conversation.label), own, own ? "" : conversation.avatarUrl || "");

		const body = document.createElement("div");
		body.className = "direct-message-message-body";
		const head = document.createElement("div");
		head.className = "direct-message-message-head";
		const author = document.createElement("span");
		author.className = "direct-message-message-author";
		author.textContent = entry.actorName || (own ? "You" : conversation.label || "Direct message");
		const time = document.createElement("span");
		time.className = "direct-message-message-time";
		time.textContent = formatDirectMessageTime(entry.createdAtMs);
		head.appendChild(author);
		head.appendChild(time);
		const text = document.createElement("div");
		text.className = "direct-message-message-text";
		const rawMessageHtml = String(entry.messageHtml || "").trim();
		const embeddedReply = extractEmbeddedReplyQuote(rawMessageHtml);
		const messageHtml = embeddedReply.found ? embeddedReply.bodyHtml : rawMessageHtml;
		text.innerHTML = messageHtml || escapedMultilineText(entry.plainText || "");
		body.appendChild(head);
		if (embeddedReply.found) {
			appendReplyBlock(body, embeddedReply);
		}
		body.appendChild(text);
		message.appendChild(avatar);
		message.appendChild(body);
		list.appendChild(message);
	}

	function renderDirectMessageWindow(conversation) {
		const session = directMessageSessionValue(conversation);
		const draftKey = directMessageDraftKey(session);
		const windowUiState = draftKey ? directMessageWindowUiState[draftKey] || {} : {};
		const windowElement = document.createElement("section");
		const privateMode = directMessageIsPrivate(conversation);
		windowElement.className = "direct-message-window" + (privateMode ? " is-private" : "")
			+ (windowUiState.minimized ? " is-minimized" : "");
		windowElement.dataset.session = String(session || "");
		windowElement.setAttribute("role", "dialog");
		windowElement.setAttribute("aria-label", "Direct message with " + (conversation.label || "user"));

		const header = document.createElement("header");
		header.className = "direct-message-window-header";
		const avatar = document.createElement("span");
		avatar.className = "direct-message-window-avatar avatar";
		styleAvatar(avatar, conversation.label || "Direct message", false, conversation.avatarUrl || "");
		const copy = document.createElement("div");
		copy.className = "direct-message-window-copy";
		const title = document.createElement("p");
		title.className = "direct-message-window-title";
		title.textContent = conversation.label || "Direct message";
		const subtitle = document.createElement("p");
		subtitle.className = "direct-message-window-subtitle";
		subtitle.textContent = privateMode ? "Private - in-memory only" : (conversation.statusLabel || conversation.subtitle || "Direct message");
		copy.appendChild(title);
		copy.appendChild(subtitle);
		header.appendChild(avatar);
		header.appendChild(copy);

		const modeButton = document.createElement("button");
		modeButton.type = "button";
		modeButton.className = "icon-button direct-message-window-mode";
		const historyUnavailableReason = conversation && conversation.historyUnavailableReason
			? String(conversation.historyUnavailableReason)
			: "Persistent history is unavailable for this direct message.";
		modeButton.title = privateMode && conversation.canPersistHistory === false
			? historyUnavailableReason
			: (privateMode ? "Use history mode" : "Use private mode");
		modeButton.setAttribute("aria-label", modeButton.title);
		modeButton.setAttribute("aria-pressed", privateMode ? "true" : "false");
		modeButton.disabled = privateMode && conversation.canPersistHistory === false;
		modeButton.innerHTML = privateModeGhostSvg();
		modeButton.addEventListener("click", function() {
			invokeDirectMessageMode(session, privateMode ? "history" : "private");
		});
		header.appendChild(modeButton);

		const minButton = document.createElement("button");
		minButton.type = "button";
		minButton.className = "icon-button direct-message-window-minimize";
		minButton.title = "Minimize";
		minButton.setAttribute("aria-label", "Minimize direct message");
		minButton.innerHTML = "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M6 12h12\"></path></svg>";
		minButton.addEventListener("click", function() {
			const minimized = windowElement.classList.toggle("is-minimized");
			if (draftKey) {
				directMessageWindowUiState[draftKey] = { minimized: minimized };
			}
		});
		header.appendChild(minButton);

		const closeButton = document.createElement("button");
		closeButton.type = "button";
		closeButton.className = "icon-button direct-message-window-close";
		closeButton.title = "Close";
		closeButton.setAttribute("aria-label", "Close direct message");
		closeButton.innerHTML = "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M8 8l8 8\"></path><path d=\"M16 8l-8 8\"></path></svg>";
		closeButton.addEventListener("click", function() {
			invokeDirectMessageClose(session);
		});
		header.appendChild(closeButton);
		windowElement.appendChild(header);

		const list = document.createElement("div");
		list.className = "direct-message-window-list";
		if (privateMode) {
			const banner = document.createElement("div");
			banner.className = "direct-message-mode-banner";
			banner.textContent = "Private mode - messages clear when this window closes.";
			list.appendChild(banner);
		}
		if (conversation.historyLoading) {
			const loading = document.createElement("div");
			loading.className = "direct-message-mode-banner";
			loading.textContent = "Loading direct-message history...";
			list.appendChild(loading);
		}
		if (conversation.historyError) {
			const error = document.createElement("div");
			error.className = "direct-message-mode-banner is-error";
			error.textContent = conversation.historyError;
			list.appendChild(error);
		}
		const messages = Array.isArray(conversation.messages) ? conversation.messages : [];
		if (!messages.length) {
			const empty = document.createElement("div");
			empty.className = "direct-message-empty";
			empty.textContent = conversation.emptyCopy || "Direct messages will appear here.";
			list.appendChild(empty);
		} else {
			messages.forEach(function(entry) {
				renderDirectMessageWindowMessage(list, entry, conversation);
			});
		}
		windowElement.appendChild(list);

		const form = document.createElement("form");
		form.className = "direct-message-composer";
		const input = document.createElement("textarea");
		input.className = "direct-message-input";
		input.rows = 1;
		input.placeholder = conversation.canSend === false ? "Unavailable" : "Message " + (conversation.label || "user");
		input.disabled = conversation.canSend === false;
		if (draftKey && directMessageDrafts[draftKey]) {
			input.value = directMessageDrafts[draftKey].value || "";
		}
		const send = document.createElement("button");
		send.type = "submit";
		send.className = "icon-button direct-message-send";
		send.title = "Send";
		send.setAttribute("aria-label", "Send direct message");
		send.disabled = conversation.canSend === false;
		send.innerHTML = "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M22 2L11 13\"></path><path d=\"M22 2L15 22l-4-9-9-4z\"></path></svg>";
		form.appendChild(input);
		form.appendChild(send);
		form.addEventListener("submit", function(event) {
			event.preventDefault();
			const value = input.value.trim();
			if (!value) {
				return;
			}
			if (invokeDirectMessageSend(session, value)) {
				input.value = "";
				input.style.height = "";
				if (draftKey) {
					delete directMessageDrafts[draftKey];
				}
			}
		});
		input.addEventListener("keydown", function(event) {
			if ((event.key === "Enter" || event.key === "Return") && !event.shiftKey && !event.isComposing) {
				event.preventDefault();
				form.requestSubmit();
			}
		});
		input.addEventListener("input", function() {
			rememberDirectMessageInputDraft(session, input);
			syncDirectMessageInputHeight(input);
		});
		windowElement.appendChild(form);
		requestAnimationFrame(function() {
			list.scrollTop = list.scrollHeight;
			syncDirectMessageInputHeight(input);
		});
		return windowElement;
	}

	function renderDirectMessageDock(state) {
		const dock = ensureDirectMessageDock();
		if (!dock) {
			return;
		}
		const focusState = captureDirectMessageDockState();
		const windows = directMessageWindows(state).filter(function(conversation) {
			return conversation.open !== false;
		});
		pruneDirectMessageDraftState(windows);
		const fragment = document.createDocumentFragment();
		windows.forEach(function(conversation) {
			fragment.appendChild(renderDirectMessageWindow(conversation));
		});
		replaceChildrenWith(dock, fragment);
		dock.classList.toggle("hidden", !windows.length);
		restoreDirectMessageDockFocus(focusState);
	}

	function renderDirectMessages(snapshot) {
		const state = directMessageState(snapshot);
		if (!state) {
			if (refs.directMessageButton) {
				refs.directMessageButton.classList.add("hidden");
			}
			if (refs.directMessageSection) {
				refs.directMessageSection.classList.add("hidden");
			}
			if (refs.directMessageDock) {
				refs.directMessageDock.innerHTML = "";
				refs.directMessageDock.classList.add("hidden");
			}
			directMessageDrafts = {};
			directMessageWindowUiState = {};
			hideDirectMessageTray();
			return;
		}
		renderDirectMessageButton(state);
		renderDirectMessageRail(state, snapshot);
		renderDirectMessageDock(state);
		renderDirectMessageTray(state);
		scheduleRailLayoutSync();
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
				icon: item.icon || "",
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

	function cloneContextItem(item, overrides) {
		if (!item) {
			return null;
		}
		return Object.assign({}, item, overrides || {});
	}

	function takeContextItem(pool, ids, overrides) {
		const idList = Array.isArray(ids) ? ids : [ids];
		const index = (pool || []).findIndex(function(item) {
			if (!item || actionPanelItemKind(item) === "separator" || actionPanelItemKind(item) === "label") {
				return false;
			}
			return idList.indexOf(String(item.id || "")) !== -1;
		});
		if (index < 0) {
			return null;
		}
		const item = pool.splice(index, 1)[0];
		return cloneContextItem(item, overrides);
	}

	function discardContextItems(pool, ids) {
		const idList = Array.isArray(ids) ? ids : [ids];
		for (let index = (pool || []).length - 1; index >= 0; index--) {
			const item = pool[index];
			if (item && idList.indexOf(String(item.id || "")) !== -1) {
				pool.splice(index, 1);
			}
		}
	}

	function pushContextItem(target, item) {
		if (item) {
			target.push(item);
		}
	}

	function pushContextSeparator(target) {
		if (!target.length || actionPanelItemKind(target[target.length - 1]) === "separator") {
			return;
		}
		target.push({ separator: true });
	}

	function appendRemainingContextItems(target, pool) {
		const remaining = normalizedActionPanelItems(pool, { hideDisabled: true }).filter(function(item) {
			return actionPanelItemKind(item) !== "label";
		});
		if (!remaining.length) {
			return;
		}
		pushContextSeparator(target);
		remaining.forEach(function(item) {
			target.push(item);
		});
	}

	function participantContextGroups(items) {
		const pool = normalizedActionPanelItems(items, { hideDisabled: true }).slice();
		const flat = [];
		pool.filter(function(item) {
			return actionPanelItemKind(item) === "label";
		}).forEach(function(item) {
			flat.push(item);
		});

		pushContextItem(flat, takeContextItem(pool, "userInfo", { label: "User information..." }));
		pushContextItem(flat, takeContextItem(pool, ["openMessage", "textMessage"], { label: "Direct message" }));
		discardContextItems(pool, ["openMessage", "textMessage"]);
		pushContextItem(flat, takeContextItem(pool, ["joinRoom", "join"], { label: "Join room" }));
		discardContextItems(pool, ["joinRoom", "join"]);
		pushContextItem(flat, takeContextItem(pool, "commentView", { label: "View comment..." }));
		pushContextItem(flat, takeContextItem(pool, "localMute", { label: "Local mute" }));
		pushContextItem(flat, takeContextItem(pool, "prioritySpeaker", { label: "Set priority speaker" }));
		pushContextItem(flat, takeContextItem(pool, "localNickname", { label: "Local nickname..." }));
		pushContextItem(flat, takeContextItem(pool, "copyUsername", { label: "Copy username" }));

		const adminItems = [
			takeContextItem(pool, "register", { label: "Register..." }),
			takeContextItem(pool, "move", { label: "Move to my room" }),
			takeContextItem(pool, "acl", { label: "Channel access (ACL)..." }),
			takeContextItem(pool, "grantChatHistory", { label: "Grant chat history..." }),
			takeContextItem(pool, "avatarChange", { label: "Change avatar..." }),
			takeContextItem(pool, ["avatarRemove", "textureReset"], { label: "Reset avatar..." }),
			takeContextItem(pool, "commentReset", { label: "Reset comment..." }),
			takeContextItem(pool, "kick", { label: "Kick..." }),
			takeContextItem(pool, "ban", { label: "Ban..." })
		].filter(Boolean);
		if (adminItems.length) {
			pushContextSeparator(flat);
			adminItems.forEach(function(item) {
				flat.push(item);
			});
		}

		appendRemainingContextItems(flat, pool);
		return normalizedActionPanelItems(flat, { hideDisabled: true });
	}

	function roomContextGroups(items) {
		const pool = normalizedActionPanelItems(items, { hideDisabled: true }).slice();
		const flat = [];
		pool.filter(function(item) {
			return actionPanelItemKind(item) === "label";
		}).forEach(function(item) {
			flat.push(item);
		});

		pushContextItem(flat, takeContextItem(pool, ["joinVoice", "join", "listen"]));
		discardContextItems(pool, ["joinVoice", "join", "listen"]);
		pushContextItem(flat, takeContextItem(pool, ["openRoom", "sendMessage"], { label: "Open chat" }));
		discardContextItems(pool, ["openRoom", "sendMessage"]);
		pushContextItem(flat, takeContextItem(pool, "markRead", { label: "Mark as read" }));
		pushContextItem(flat, takeContextItem(pool, "hide", { label: "Mute notifications" }));
		pushContextItem(flat, takeContextItem(pool, "pin"));

		const shareItem = takeContextItem(pool, [
			"screenShareStart",
			"screenShareStop",
			"screenShareWatch",
			"screenShareStopWatching",
			"screenShareOpenWindow"
		]);
		const copyItem = takeContextItem(pool, "copyUrl", { label: "Copy invite link" });
		if (copyItem || shareItem) {
			pushContextSeparator(flat);
			pushContextItem(flat, copyItem);
			pushContextItem(flat, shareItem);
		}

		const manageItems = [
			takeContextItem(pool, "add", { label: "Create room..." }),
			takeContextItem(pool, ["acl", "textRoom.acl"], { label: "Room ACL..." }),
			takeContextItem(pool, "textRoom.edit", { label: "Edit text room..." }),
			takeContextItem(pool, "textRoom.setDefault", { label: "Set as default" }),
			takeContextItem(pool, "textRoom.visibilitySource", { label: "Go to visibility source" }),
			takeContextItem(pool, "link", { label: "Link room" }),
			takeContextItem(pool, "unlink", { label: "Unlink room..." }),
			takeContextItem(pool, "unlinkAll", { label: "Unlink all rooms..." }),
			takeContextItem(pool, "remove", { label: "Remove room..." }),
			takeContextItem(pool, "textRoom.delete", { label: "Delete text room..." })
		].filter(Boolean);
		if (manageItems.length) {
			pushContextSeparator(flat);
			manageItems.forEach(function(item) {
				flat.push(item);
			});
		}

		appendRemainingContextItems(flat, pool);
		return normalizedActionPanelItems(flat, { hideDisabled: true });
	}

	function sliderValueLabel(item, value) {
		const numericValue = Number(value || 0);
		if (!Number.isFinite(numericValue)) {
			return "";
		}

		const prefix = numericValue > 0 ? "+" : "";
		return prefix + String(numericValue) + String(item && item.suffix ? item.suffix : "");
	}

	function actionPanelIconName(item) {
		const explicit = String(item && item.icon ? item.icon : "").toLowerCase();
		if (explicit) {
			return explicit;
		}

		const id = String(item && item.id ? item.id : "").toLowerCase();
		const label = String(item && item.label ? item.label : "").toLowerCase();
		const text = id + " " + label;

		if (text.indexOf("disconnect") !== -1 || text.indexOf("quit") !== -1 || text.indexOf("exit") !== -1) {
			return "log-out";
		}
		if (text.indexOf("delete") !== -1 || text.indexOf("remove") !== -1 || text.indexOf("clear") !== -1
			|| text.indexOf("reset") !== -1 || text.indexOf("unlink") !== -1) {
			return "trash";
		}
		if (text.indexOf("ban") !== -1 || text.indexOf("block") !== -1) {
			return "ban";
		}
		if (text.indexOf("kick") !== -1) {
			return "log-out";
		}
		if (text.indexOf("certificate") !== -1 || text.indexOf("acl") !== -1
			|| text.indexOf("access control") !== -1 || text.indexOf("privilege") !== -1) {
			return "shield";
		}
		if (text.indexOf("token") !== -1 || text.indexOf("key") !== -1 || text.indexOf("password") !== -1) {
			return "key";
		}
		if (text.indexOf("private mode") !== -1) {
			return "ghost";
		}
		if (text.indexOf("registered") !== -1 || text.indexOf("users") !== -1 || text.indexOf("members") !== -1) {
			return "users";
		}
		if (text.indexOf("server information") !== -1 || text.indexOf("user information") !== -1
			|| text.indexOf("about") !== -1 || text.indexOf("info") !== -1) {
			return "info";
		}
		if (text.indexOf("search") !== -1 || text.indexOf("find") !== -1) {
			return "search";
		}
		if (text.indexOf("update") !== -1 || text.indexOf("download") !== -1) {
			return "download";
		}
		if (text.indexOf("feedback") !== -1 || text.indexOf("bug") !== -1 || text.indexOf("report") !== -1) {
			return "bug";
		}
		if (text.indexOf("settings") !== -1 || text.indexOf("configuration") !== -1
			|| text.indexOf("preferences") !== -1 || text.indexOf("configure") !== -1) {
			return "settings";
		}
		if (text.indexOf("stonks") !== -1 || text.indexOf("finance") !== -1 || text.indexOf("stock") !== -1) {
			return "chart";
		}
		if (text.indexOf("screen share") !== -1 || text.indexOf("screenshare") !== -1
			|| text.indexOf("screen") !== -1 || text.indexOf("watch") !== -1) {
			return "screen";
		}
		if (text.indexOf("message") !== -1 || text.indexOf("chat") !== -1 || text.indexOf("comment") !== -1) {
			return "message";
		}
		if (text.indexOf("reply") !== -1) {
			return "reply";
		}
		if (text.indexOf("reaction") !== -1 || text.indexOf("emoji") !== -1) {
			return "smile";
		}
		if (text.indexOf("copy") !== -1) {
			return "copy";
		}
		if (text.indexOf("quote") !== -1) {
			return "quote";
		}
		if (text.indexOf("mark read") !== -1 || text.indexOf("read") !== -1) {
			return "check";
		}
		if (text.indexOf("load older") !== -1 || text.indexOf("history") !== -1) {
			return "history";
		}
		if (text.indexOf("pin") !== -1) {
			return "pin";
		}
		if (text.indexOf("hide") !== -1 || text.indexOf("ignore") !== -1) {
			return "eye-off";
		}
		if (text.indexOf("join") !== -1 || text.indexOf("connect") !== -1 || text.indexOf("open room") !== -1
			|| text.indexOf("listen") !== -1) {
			return "log-in";
		}
		if (text.indexOf("add") !== -1 || text.indexOf("new") !== -1 || text.indexOf("create") !== -1) {
			return "plus";
		}
		if (text.indexOf("move") !== -1) {
			return "move";
		}
		if (text.indexOf("link") !== -1 || text.indexOf("url") !== -1) {
			return "link";
		}
		if (text.indexOf("avatar") !== -1 || text.indexOf("texture") !== -1 || text.indexOf("image") !== -1) {
			return "image";
		}
		if (text.indexOf("friend") !== -1 || text.indexOf("priority") !== -1) {
			return "star";
		}
		if (text.indexOf("record") !== -1) {
			return "record";
		}
		if (text.indexOf("statistics") !== -1 || text.indexOf("stats") !== -1 || text.indexOf("activity") !== -1) {
			return "activity";
		}
		if (text.indexOf("deaf") !== -1) {
			return "headphones";
		}
		if (text.indexOf("mute") !== -1 || text.indexOf("microphone") !== -1) {
			return "mic";
		}
		if (text.indexOf("volume") !== -1 || text.indexOf("voice") !== -1) {
			return "speaker";
		}
		if (text.indexOf("room") !== -1 || text.indexOf("channel") !== -1) {
			return "hash";
		}
		if (text.indexOf("profile") !== -1 || text.indexOf("nickname") !== -1 || text.indexOf("self") !== -1) {
			return "user";
		}
		if (text.indexOf("manage") !== -1) {
			return "sliders";
		}
		if (text.indexOf("more") !== -1) {
			return "more";
		}
		return "dot";
	}

	function actionPanelIconSvg(name) {
		switch (String(name || "dot")) {
			case "activity":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M3 12h4l3-7 4 14 3-7h4\"></path></svg>";
			case "ban":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"8\"></circle><path d=\"M7 7l10 10\"></path></svg>";
			case "bug":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M8 8a4 4 0 018 0v8a4 4 0 01-8 0z\"></path><path d=\"M3 13h5\"></path><path d=\"M16 13h5\"></path><path d=\"M5 6l3 3\"></path><path d=\"M19 6l-3 3\"></path></svg>";
			case "chart":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 19V5\"></path><path d=\"M4 19h16\"></path><path d=\"M7 15l4-4 3 3 5-7\"></path></svg>";
			case "check":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4.5 12.5l5 5L20 7\"></path></svg>";
			case "copy":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"8\" y=\"8\" width=\"11\" height=\"11\" rx=\"2\"></rect><path d=\"M5 15V5h10\"></path></svg>";
			case "download":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 4v10\"></path><path d=\"M8 10l4 4 4-4\"></path><path d=\"M5 19h14\"></path></svg>";
			case "eye-off":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M3 3l18 18\"></path><path d=\"M10.5 10.5a2 2 0 002.8 2.8\"></path><path d=\"M9 5.5A9.8 9.8 0 0121 12a10.7 10.7 0 01-3 3.7\"></path><path d=\"M6.5 6.5A10.6 10.6 0 003 12a10.8 10.8 0 009.7 5.9\"></path></svg>";
			case "hash":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M9 4L7 20\"></path><path d=\"M17 4l-2 16\"></path><path d=\"M4 9h16\"></path><path d=\"M3 15h16\"></path></svg>";
			case "headphones":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 14a8 8 0 0116 0\"></path><path d=\"M4 14v4a2 2 0 002 2h2v-6H4z\"></path><path d=\"M20 14v4a2 2 0 01-2 2h-2v-6h4z\"></path></svg>";
			case "ghost":
				return privateModeGhostSvg();
			case "history":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 12a8 8 0 108-8\"></path><path d=\"M4 5v7h7\"></path><path d=\"M12 8v5l3 2\"></path></svg>";
			case "image":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"4\" y=\"5\" width=\"16\" height=\"14\" rx=\"2\"></rect><path d=\"M8 14l3-3 3 3 2-2 3 4\"></path><circle cx=\"9\" cy=\"9\" r=\"1.3\"></circle></svg>";
			case "info":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"9\"></circle><path d=\"M12 10v6\"></path><path d=\"M12 7h.01\"></path></svg>";
			case "key":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"7.5\" cy=\"12.5\" r=\"3.5\"></circle><path d=\"M11 12.5h9\"></path><path d=\"M17 12.5v3\"></path><path d=\"M14.5 12.5v2\"></path></svg>";
			case "link":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M9.5 14.5l5-5\"></path><path d=\"M8 10l-1.2 1.2a4 4 0 105.7 5.6l1.1-1.1\"></path><path d=\"M16 14l1.2-1.2a4 4 0 10-5.7-5.6l-1.1 1.1\"></path></svg>";
			case "log-in":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M15 4h3a2 2 0 012 2v12a2 2 0 01-2 2h-3\"></path><path d=\"M10 17l5-5-5-5\"></path><path d=\"M15 12H3\"></path></svg>";
			case "log-out":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M9 4H6a2 2 0 00-2 2v12a2 2 0 002 2h3\"></path><path d=\"M14 17l5-5-5-5\"></path><path d=\"M19 12H8\"></path></svg>";
			case "message":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 5h16v11H8l-4 4z\"></path><path d=\"M8 9h8\"></path><path d=\"M8 12h5\"></path></svg>";
			case "mic":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"9\" y=\"3\" width=\"6\" height=\"11\" rx=\"3\"></rect><path d=\"M5 11a7 7 0 0014 0\"></path><path d=\"M12 18v3\"></path></svg>";
			case "more":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"5\" cy=\"12\" r=\"1.6\"></circle><circle cx=\"12\" cy=\"12\" r=\"1.6\"></circle><circle cx=\"19\" cy=\"12\" r=\"1.6\"></circle></svg>";
			case "move":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3v18\"></path><path d=\"M8 7l4-4 4 4\"></path><path d=\"M8 17l4 4 4-4\"></path><path d=\"M3 12h18\"></path><path d=\"M7 8l-4 4 4 4\"></path><path d=\"M17 8l4 4-4 4\"></path></svg>";
			case "pin":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M14 4l6 6-3 1-4 4v5l-2 2-2-7-7-2 2-2h5l4-4z\"></path></svg>";
			case "plus":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 5v14\"></path><path d=\"M5 12h14\"></path></svg>";
			case "quote":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M8 11H5a4 4 0 014-4v8H5v-4\"></path><path d=\"M18 11h-3a4 4 0 014-4v8h-4v-4\"></path></svg>";
			case "record":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"8\"></circle><circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"currentColor\" stroke=\"none\"></circle></svg>";
			case "reply":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M9 17l-5-5 5-5\"></path><path d=\"M20 18v-2a4 4 0 00-4-4H4\"></path></svg>";
			case "screen":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"3\" y=\"5\" width=\"18\" height=\"12\" rx=\"2\"></rect><path d=\"M8 21h8\"></path><path d=\"M12 17v4\"></path></svg>";
			case "search":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"11\" cy=\"11\" r=\"7\"></circle><path d=\"M20 20l-4-4\"></path></svg>";
			case "settings":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"3\"></circle><path d=\"M19 12a7 7 0 00-.1-1l2-1.5-2-3.4-2.4 1a7 7 0 00-1.7-1L14.5 3h-5l-.4 3.1a7 7 0 00-1.7 1L5 6.1l-2 3.4L5 11a7 7 0 000 2l-2 1.5 2 3.4 2.4-1a7 7 0 001.7 1l.4 3.1h5l.4-3.1a7 7 0 001.7-1l2.4 1 2-3.4-2-1.5a7 7 0 00.1-1z\"></path></svg>";
			case "shield":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3l7 3v5c0 4.5-2.8 8-7 10-4.2-2-7-5.5-7-10V6z\"></path><path d=\"M9 12l2 2 4-4\"></path></svg>";
			case "sliders":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 7h10\"></path><path d=\"M18 7h2\"></path><circle cx=\"16\" cy=\"7\" r=\"2\"></circle><path d=\"M4 17h2\"></path><path d=\"M10 17h10\"></path><circle cx=\"8\" cy=\"17\" r=\"2\"></circle></svg>";
			case "smile":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"9\"></circle><path d=\"M8 14s1.5 2 4 2 4-2 4-2\"></path><path d=\"M9 9h.01\"></path><path d=\"M15 9h.01\"></path></svg>";
			case "speaker":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M11 5L6 9H3v6h3l5 4z\"></path><path d=\"M15.5 8.5a5 5 0 010 7\"></path><path d=\"M18.5 5.5a9 9 0 010 13\"></path></svg>";
			case "star":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3l2.6 5.5 6 .8-4.3 4.2 1 6-5.3-2.8-5.3 2.8 1-6L3.4 9.3l6-.8z\"></path></svg>";
			case "trash":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 7h16\"></path><path d=\"M9 7V5h6v2\"></path><path d=\"M18 7l-1 13H7L6 7\"></path><path d=\"M10 11v5\"></path><path d=\"M14 11v5\"></path></svg>";
			case "user":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"8\" r=\"4\"></circle><path d=\"M5 21a7 7 0 0114 0\"></path></svg>";
			case "users":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"9\" cy=\"8\" r=\"3\"></circle><path d=\"M3 20a6 6 0 0112 0\"></path><path d=\"M16 11a3 3 0 10-1-5.8\"></path><path d=\"M17 20h4a5 5 0 00-5-5\"></path></svg>";
			default:
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"3\"></circle></svg>";
		}
	}

	function themedActionPanelIconSvg(name) {
		return actionPanelIconSvg(name).replace("<svg ",
			"<svg fill=\"none\" stroke=\"currentColor\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" ");
	}

	function actionPanelIconHtml(item, prefix) {
		return "<span class=\"" + prefix + "-icon\" aria-hidden=\"true\">" + themedActionPanelIconSvg(actionPanelIconName(item)) + "</span>";
	}

	function updateContextMenuOpenSubmenuState(menu) {
		if (!menu || !menu.classList) {
			return;
		}
		menu.classList.toggle("has-open-submenu", !!menu.querySelector(".context-menu-submenu.is-open"));
	}

	function positionContextSubmenuFlyout(submenu, trigger, panel) {
		if (!submenu || !trigger || !panel) {
			return;
		}

		const margin = contextMenuViewportMargin;
		const gap = 6;
		const triggerBounds = trigger.getBoundingClientRect();
		const submenuBounds = submenu.getBoundingClientRect();
		const measuredWidth = panel.offsetWidth || 236;
		const hasRightSpace = triggerBounds.right + gap + measuredWidth <= window.innerWidth - margin;
		const hasLeftSpace = triggerBounds.left - gap - measuredWidth >= margin;
		const useLeft = !hasRightSpace && hasLeftSpace;
		submenu.classList.toggle("is-flyout-left", useLeft);

		const availableHeight = Math.max(96, window.innerHeight - (margin * 2));
		const measuredHeight = Math.min(panel.scrollHeight || availableHeight, availableHeight);
		const absoluteTop = Math.max(
			margin,
			Math.min(triggerBounds.top - 4, window.innerHeight - margin - measuredHeight));
		panel.style.left = "";
		panel.style.right = "";
		panel.style.top = (absoluteTop - submenuBounds.top) + "px";
		panel.style.maxHeight = Math.max(96, window.innerHeight - margin - absoluteTop) + "px";
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
			trigger.innerHTML = actionPanelIconHtml(item, prefix) + "<span class=\"" + labelClass + "\"></span><span class=\""
				+ stateClass + "\" aria-hidden=\"true\">&gt;</span>";
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
					const menu = submenu.closest(".context-menu");
					if (open) {
						positionContextSubmenuFlyout(submenu, trigger, panel);
						window.requestAnimationFrame(function() {
							positionContextSubmenuFlyout(submenu, trigger, panel);
						});
					} else {
						panel.style.left = "";
						panel.style.right = "";
						panel.style.top = "";
						panel.style.maxHeight = "";
					}
					updateContextMenuOpenSubmenuState(menu);
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
		button.innerHTML = actionPanelIconHtml(item, prefix) + "<span class=\"" + labelClass + "\"></span><span class=\""
			+ stateClass + "\"></span>";
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
			const kind = String(status.kind || "");
			const indicator = document.createElement("span");
			indicator.className = "presence-status is-status-" + kind
				+ (classicStatusIconUrl(kind) ? " has-classic-icon" : "")
				+ (classicTalkStatusKind(kind) ? " is-talk-state-status" : "")
				+ (status.tone ? " is-" + status.tone : "");
			indicator.title = status.label || "";
			indicator.setAttribute("aria-label", status.label || "");
			const modernIcon = document.createElement("span");
			modernIcon.className = "presence-status-modern-icon";
			modernIcon.innerHTML = presenceStatusIconSvg(kind);
			indicator.appendChild(modernIcon);
			const classicIconUrl = classicStatusIconUrl(kind);
			if (classicIconUrl) {
				const classicIcon = document.createElement("img");
				classicIcon.className = "presence-status-classic-icon";
				classicIcon.src = classicIconUrl;
				classicIcon.alt = "";
				classicIcon.setAttribute("aria-hidden", "true");
				indicator.appendChild(classicIcon);
			}
			container.appendChild(indicator);
		});
	}

	const classicUserIconBaseUrl = "qrc:///themes/Default/";

	function classicUserIconUrl(name) {
		return classicUserIconBaseUrl + name;
	}

	function classicTalkStatusKind(kind) {
		return ["talking", "whispering", "shouting", "mutedTalking"].indexOf(String(kind || "")) >= 0;
	}

	function classicStateIconUrl(person) {
		if (person && person.entryKind === "listener") {
			return classicUserIconUrl("ear.svg");
		}

		switch (normalizedTalkState(person)) {
			case "talking":
				return classicUserIconUrl("talking_on.svg");
			case "whispering":
				return classicUserIconUrl("talking_whisper.svg");
			case "shouting":
				return classicUserIconUrl("talking_alt.svg");
			case "mutedTalking":
				return classicUserIconUrl("talking_muted.svg");
			case "passive":
			default:
				return classicUserIconUrl("talking_off.svg");
		}
	}

	function classicStatusIconUrl(kind) {
		switch (String(kind || "")) {
			case "friend":
				return classicUserIconUrl("emblems/emblem-favorite.svg");
			case "authenticated":
				return classicUserIconUrl("authenticated.svg");
			case "priority":
				return classicUserIconUrl("priority_speaker.svg");
			case "recording":
				return classicUserIconUrl("actions/media-record.svg");
			case "listener":
				return classicUserIconUrl("ear.svg");
			case "serverDeafened":
				return classicUserIconUrl("deafened_server.svg");
			case "selfDeafened":
				return classicUserIconUrl("deafened_self.svg");
			case "serverMuted":
				return classicUserIconUrl("muted_server.svg");
			case "selfMuted":
				return classicUserIconUrl("muted_self.svg");
			case "localMuted":
				return classicUserIconUrl("muted_local.svg");
			case "suppressed":
				return classicUserIconUrl("muted_suppressed.svg");
			default:
				return "";
		}
	}

	function appendClassicUserIcon(element, person) {
		const icon = document.createElement("img");
		icon.className = "classic-user-icon";
		icon.src = classicStateIconUrl(person);
		icon.alt = "";
		icon.setAttribute("aria-hidden", "true");
		element.appendChild(icon);
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
			avatar.className = "stack-avatar participant-context-target" + (person.isSelf ? " is-self" : "") + talkStateClass(person);
			styleAvatar(avatar, person.label, !!person.isSelf, person.avatarUrl || "");
			appendClassicUserIcon(avatar, person);
			avatar.title = [person.label || "", person.talkLabel || ""].filter(Boolean).join(" | ");
			avatar.setAttribute("role", "button");
			avatar.tabIndex = 0;
			avatar.dataset.session = person.session ? String(person.session) : "";
			avatar.dataset.participantKey = participantStateKey(person);
			avatar.dataset.scopeToken = person.scopeToken || "";
			avatar.addEventListener("contextmenu", function(event) {
				event.preventDefault();
				event.stopPropagation();
				showContextMenu(participantContextMenuItems(person, avatar.dataset.scopeToken),
					event.clientX,
					event.clientY);
			});
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

		items.push({
			kind: "label",
			label: participant.label || participant.name || "User"
		});

		if (participant.entryKind !== "listener" && participant.canMessage
			&& !actionStatesContainId(participant.actions, "textMessage")) {
			items.push({
				id: "openMessage",
				label: "Direct message",
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
		const hasPrimaryItems = items.some(function(item) {
			const kind = actionPanelItemKind(item);
			return kind && kind !== "label" && kind !== "separator";
		});
		if (hasPrimaryItems && participantActionItems.length) {
			items.push({ separator: true });
		}
		const allItems = items.concat(participantActionItems);
		if (participant.entryKind !== "listener") {
			allItems.push({
				id: "copyUsername",
				label: "Copy username",
				enabled: !!(participant.label || participant.name),
				action: function() {
					copyPlainText(participant.label || participant.name || "");
				}
			});
		}
		return participantContextGroups(allItems);
	}

	function renderPresenceList(container, room, people) {
		const list = document.createElement("div");
		list.className = "presence-list";

		(people || []).forEach(function(person) {
			const entry = document.createElement("div");
			entry.className = "presence-entry";

			const row = document.createElement("button");
			row.type = "button";
			row.className = "presence-row member-row"
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
			avatar.className = "presence-avatar avatar"
				+ (person.isSelf ? " is-self" : "")
				+ (person.entryKind === "listener" ? " is-listener" : "")
				+ talkStateClass(person);
			styleAvatar(avatar, person.label, !!person.isSelf, person.avatarUrl || "");
			appendClassicUserIcon(avatar, person);

			const dot = document.createElement("span");
			dot.className = "presence-dot" + (person.entryKind === "listener" ? " is-listener" : "")
				+ talkStateClass(person);

			const label = document.createElement("span");
			label.className = "presence-copy";
			label.innerHTML = "<span class=\"presence-heading\"><span class=\"presence-name\"></span><span class=\"presence-statuses hidden\"></span></span><span class=\"presence-subtitle\"></span>";
			label.querySelector(".presence-name").classList.add("member-name");
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
			row.addEventListener("contextmenu", function(event) {
				event.preventDefault();
				event.stopPropagation();
				showContextMenu(participantContextMenuItems(person, row.dataset.scopeToken),
					event.clientX,
					event.clientY);
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
		const roomPathLabel = String(room.pathLabel || "").trim();
		const hasRoomTopic = Object.prototype.hasOwnProperty.call(room || {}, "topic");
		const roomTopic = String(hasRoomTopic ? (room.topic || "") : (joinable ? (room.description || "") : "")).trim();
		const roomDescription = String(room.description || "").trim();
		const unreadCount = Number(room.unreadCount || 0);
		const screenShare = room.screenShare || {};
		const isRootRoom = !!room.isRoot;
		const joining = joinable && String(room.token || "") === pendingVoiceJoinToken();
		const roomKind = String(room.kindLabel || "").trim().toLowerCase();
		const hasRoomActions = (room.actions || []).some(function(item) {
			return !!item && String(item.kind || "action") !== "separator";
		});
		const canJoinRoom = joinable && room.canJoin !== false && !room.joined && !joining;
		const subtitleText = joinable
			? roomTopic
			: ((room.selected || unreadCount > 0 || roomKind === "activity" || roomKind === "direct message")
				? roomDescription
				: "");
		const tooltipParts = [];
		if (roomTopic) {
			tooltipParts.push(roomTopic);
		}
		if (roomPathLabel && roomPathLabel !== roomTopic) {
			tooltipParts.push(roomPathLabel);
		}
		if (!tooltipParts.length && roomDescription) {
			tooltipParts.push(roomDescription);
		}
		const wrapper = document.createElement("div");
		wrapper.className = "rail-row-wrapper room-item" + (joinable ? " is-voice-room" : "");
		wrapper.setAttribute("role", "option");
		wrapper.setAttribute("aria-label", room.label || "Room");
		wrapper.setAttribute("aria-selected", room.selected ? "true" : "false");
		wrapper.style.setProperty("--room-depth", String(depth));
		wrapper.dataset.scopeToken = room.token || "";
		wrapper.dataset.roomType = joinable ? "voice" : "text";

		const button = document.createElement("div");
		button.setAttribute("role", "button");
		button.tabIndex = 0;
		button.className = "rail-row room-node"
			+ (room.selected ? " is-selected" : "")
			+ (room.selected ? " is-active" : "")
			+ (room.joined ? " is-joined" : "")
			+ (joining ? " is-joining" : "")
			+ (isRootRoom ? " is-root-room" : "");
		button.classList.toggle("has-subtitle", !!subtitleText);
		button.classList.toggle("has-unread", unreadCount > 0);
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
		button.title = tooltipParts.join(" - ");

		const chip = document.createElement("span");
		chip.className = "kind-chip room-icon";
		const chipKind = kindChipKind(room.kindLabel);
		const chipText = kindChipText(room.kindLabel);
		chip.dataset.kindLabel = chipKind;
		chip.title = room.kindLabel || (joinable ? "Voice room" : "Text room");
		chip.setAttribute("aria-hidden", "true");
		const chipIcon = kindChipIconSvg(room.kindLabel);
		if (chipIcon) {
			chip.innerHTML = chipIcon;
		} else {
			chip.textContent = chipText;
		}

		const copy = document.createElement("span");
		copy.className = "rail-row-copy room-copy";

		const title = document.createElement("span");
		title.className = "rail-row-title room-name";
		title.textContent = room.label || "Room";

		const subtitle = document.createElement("span");
		subtitle.className = "rail-row-subtitle room-subtitle";
		subtitle.textContent = subtitleText;
		subtitle.classList.toggle("hidden", !subtitleText);

		copy.appendChild(title);
		copy.appendChild(subtitle);

		const meta = document.createElement("span");
		meta.className = "rail-row-meta room-meta";

		if (unreadCount > 0) {
			const unread = document.createElement("span");
			unread.className = "row-badge";
			unread.textContent = String(unreadCount);
			meta.appendChild(unread);
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
			joinButton.className = "mini-action room-join room-join-action" + (joining ? " is-loading" : "");
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

		syncMockupShellChrome(app);
		refs.connectButton.disabled = !app.canConnect;
		refs.disconnectButton.disabled = !appCanCancelConnection(app);
		refs.muteButton.classList.toggle("is-active", !!app.selfMuted);
		refs.deafButton.classList.toggle("is-active", !!app.selfDeafened);
		renderMenus(resolvedAppMenus(app));
		renderSelfCard(app);
		if (appMenuOpen) {
			renderAppMenu(snapshot);
		}
		if (selfMenuOpen && !nativeSelfMenuOpen()) {
			renderSelfMenu(snapshot);
		}

		reconcilePendingVoiceJoin(snapshot);
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
		renderDirectMessages(snapshot);
		renderStonksChatHeader(snapshot);
		trackActiveRailTokenForReveal(activeRailToken());
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
		trackActiveRailTokenForReveal(activeRailToken());
		scheduleRailLayoutSync();
	}

	function renderActiveScopePatch(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const directConversation = activeDirectMessageConversation(snapshot);

		renderRailSelectionPatch(snapshot);
		renderDirectMessages(snapshot);
		refs.serverEyebrow.textContent = app.serverEyebrow || scope.kindLabel || "Mumble";
		refs.scopeTitle.textContent = scope.label || app.serverTitle || "Mumble";
		refs.scopeDescription.textContent = scope.description || app.serverSubtitle || "Select a room to see shared history.";
		renderMeta(scope.meta || []);
		renderStonksChatHeader(snapshot);
		renderScreenShareHeader(scope, scope.screenShare || null);
		renderScreenShareCard(scope, scope.screenShare || null);
		renderNote(app, scope, snapshot.messages || []);
		renderConnectionOrScopeBanner(app, scope);
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
		if (directConversation) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
		} else if (scope.serverLogRevision || Object.prototype.hasOwnProperty.call(scope, "serverLogHtml")) {
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

	function serverIdentityImageExtension(file) {
		const name = String(file && file.name || "").toLowerCase();
		const dot = name.lastIndexOf(".");
		return dot >= 0 ? name.slice(dot + 1) : "";
	}

	function serverIdentityImageMimeForFile(file) {
		const type = String(file && file.type || "").trim().toLowerCase();
		if (type.indexOf("image/") === 0) {
			return type;
		}
		const extension = serverIdentityImageExtension(file);
		if (Object.prototype.hasOwnProperty.call(serverIdentityImageExtensionMime, extension)) {
			return serverIdentityImageExtensionMime[extension];
		}
		return type ? "" : "image/x-unknown";
	}

	function serverIdentityFileLooksLikeImage(file) {
		return !!serverIdentityImageMimeForFile(file);
	}

	function normalizedServerIdentityImageDataUrl(file, dataUrl) {
		const value = String(dataUrl || "");
		if (/^data:image\//i.test(value)) {
			return value;
		}
		const mime = serverIdentityImageMimeForFile(file);
		const comma = value.indexOf(",");
		if (!mime || comma < 0) {
			return value;
		}
		return "data:" + mime + ";base64," + value.slice(comma + 1);
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

		const directSession = directMessageSessionFromToken(currentScopeToken());
		if (directSession) {
			invokeDirectMessageSend(directSession, value);
		} else {
			notifyBridge("sendMessage", value);
		}
		refs.composerInput.value = "";
		syncComposerHeight();
		return true;
	}

	function focusComposerInput() {
		if (!refs.composerInput || refs.composerInput.disabled || typeof refs.composerInput.focus !== "function") {
			return false;
		}

		refs.composerInput.focus({ preventScroll: true });
		return document.activeElement === refs.composerInput;
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
		article.dataset.bodyText = message.bodyText || "";
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

	function normalizedReplyPreviewText(value) {
		return String(value || "").replace(/\s+/g, " ").trim();
	}

	function extractEmbeddedReplyQuote(bodyHtml) {
		const source = String(bodyHtml || "");
		const result = {
			bodyHtml: source,
			replyActor: "",
			replySnippet: "",
			found: false
		};
		if (!source || (source.indexOf("data-mumble-reply-quote") === -1 && source.indexOf("mumble-reply:") === -1)) {
			return result;
		}

		const template = document.createElement("template");
		template.innerHTML = source;
		const nodes = Array.prototype.slice.call(template.content.childNodes);
		let metadataNode = null;
		let quoteNode = null;

		for (let index = 0; index < nodes.length; index += 1) {
			const node = nodes[index];
			if (node.nodeType === 3 && !String(node.textContent || "").trim()) {
				continue;
			}
			if (node.nodeType === 8 && String(node.data || "").trim().indexOf("mumble-reply:") === 0) {
				metadataNode = node;
				continue;
			}
			if (node.nodeType === 1 && node.matches && node.matches("blockquote[data-mumble-reply-quote]")) {
				quoteNode = node;
			}
			break;
		}

		if (!metadataNode && !quoteNode) {
			return result;
		}

		if (metadataNode) {
			const metadataText = String(metadataNode.data || "").trim();
			try {
				const metadata = JSON.parse(metadataText.slice("mumble-reply:".length));
				result.replyActor = normalizedReplyPreviewText(metadata.actor);
				result.replySnippet = normalizedReplyPreviewText(metadata.snippet);
			} catch (error) {
				// Older reply payloads still expose readable text in the quote block.
			}
			if (metadataNode.parentNode) {
				metadataNode.parentNode.removeChild(metadataNode);
			}
		}

		if (quoteNode) {
			if (!result.replyActor) {
				result.replyActor = normalizedReplyPreviewText(
					quoteNode.querySelector("strong") && quoteNode.querySelector("strong").textContent);
			}
			if (!result.replySnippet) {
				const quoteCopy = quoteNode.cloneNode(true);
				const actor = quoteCopy.querySelector("strong");
				if (actor && actor.parentNode) {
					actor.parentNode.removeChild(actor);
				}
				Array.prototype.slice.call(quoteCopy.querySelectorAll("br")).forEach(function(lineBreak) {
					lineBreak.parentNode.replaceChild(document.createTextNode("\n"), lineBreak);
				});
				result.replySnippet = normalizedReplyPreviewText(quoteCopy.textContent);
			}
			if (quoteNode.parentNode) {
				quoteNode.parentNode.removeChild(quoteNode);
			}
		}

		result.bodyHtml = template.innerHTML.trim();
		result.found = true;
		return result;
	}

	function appendReplyBlock(container, message) {
		const rawReplyActor = normalizedReplyPreviewText(message && message.replyActor);
		const replySnippet = normalizedReplyPreviewText(message && message.replySnippet);
		if (!rawReplyActor && !replySnippet) {
			return;
		}
		const replyActor = rawReplyActor || "Reply";

		const reply = document.createElement("div");
		reply.className = "reply-block" + (replySnippet ? "" : " has-no-snippet");
		reply.setAttribute("role", "note");
		reply.setAttribute("aria-label", replySnippet ? (replyActor + ": " + replySnippet) : replyActor);

		const copy = document.createElement("span");
		copy.className = "reply-copy";
		const actor = document.createElement("span");
		actor.className = "reply-actor";
		actor.textContent = replyActor;
		copy.appendChild(actor);

		if (replySnippet) {
			const snippet = document.createElement("span");
			snippet.className = "reply-snippet";
			snippet.textContent = replySnippet;
			copy.appendChild(snippet);
			reply.title = replyActor + ": " + replySnippet;
		}

		reply.appendChild(copy);
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

	function previewNumericValue(value) {
		const number = Number(value);
		return Number.isFinite(number) ? number : 0;
	}

	function formatPreviewCompactNumber(value) {
		const number = previewNumericValue(value);
		if (number <= 0) {
			return "";
		}

		if (window.Intl && typeof Intl.NumberFormat === "function") {
			try {
				return new Intl.NumberFormat(undefined, {
					notation: "compact",
					maximumFractionDigits: number >= 1000000 ? 1 : 0
				}).format(number);
			} catch (error) {
				// Fall back to explicit suffixes below when the embedded engine lacks compact notation.
			}
		}

		if (number >= 1000000000) {
			return (number / 1000000000).toFixed(number >= 10000000000 ? 0 : 1).replace(/\.0$/, "") + "B";
		}
		if (number >= 1000000) {
			return (number / 1000000).toFixed(number >= 10000000 ? 0 : 1).replace(/\.0$/, "") + "M";
		}
		if (number >= 1000) {
			return (number / 1000).toFixed(number >= 10000 ? 0 : 1).replace(/\.0$/, "") + "K";
		}
		return Math.round(number).toLocaleString();
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
			audio.loop = video.loop;
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

		video.__previewAudioTrack = audio;
		video.__previewPlayAudioTrack = playAudio;
		video.__previewSyncAudioTrack = syncAudioPosition;
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

	function previewMediaIconSvg(iconName) {
		switch (String(iconName || "")) {
			case "pause":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path class=\"preview-card-media-icon-fill\" d=\"M7 5h4v14H7zM13 5h4v14h-4z\"></path></svg>";
			case "volume":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"M4 9v6h4l5 4V5L8 9H4z\"></path><path d=\"M16 9.5a4 4 0 010 5\"></path><path d=\"M18.5 6.5a8 8 0 010 11\"></path></svg>";
			case "muted":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"M4 9v6h4l5 4V5L8 9H4z\"></path><path d=\"M16 9l5 6\"></path><path d=\"M21 9l-5 6\"></path></svg>";
			case "size-compact":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"M4 14h6v6\"></path><path d=\"M10 14l-7 7\"></path><path d=\"M20 10h-6V4\"></path><path d=\"M14 10l7-7\"></path></svg>";
			case "size-default":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><rect x=\"5\" y=\"6\" width=\"14\" height=\"12\" rx=\"2\"></rect><path d=\"M8 3h13v11\"></path></svg>";
			case "size-large":
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"M8 3H3v5\"></path><path d=\"M3 3l7 7\"></path><path d=\"M16 21h5v-5\"></path><path d=\"M21 21l-7-7\"></path></svg>";
			case "play":
			default:
				return "<svg class=\"preview-card-media-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path class=\"preview-card-media-icon-fill\" d=\"M8 5.5v13l10-6.5z\"></path></svg>";
		}
	}

	function setPreviewMediaButtonIcon(button, iconName) {
		if (!button) {
			return;
		}
		const normalizedIcon = String(iconName || "play");
		if (button.dataset.previewIcon === normalizedIcon && button.firstElementChild) {
			return;
		}
		button.dataset.previewIcon = normalizedIcon;
		button.innerHTML = previewMediaIconSvg(normalizedIcon);
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
			const isPlaying = state.paused === false;
			const label = (isPlaying ? "Pause " : "Play ") + mediaLabel;
			setPreviewMediaButtonIcon(playButton, isPlaying ? "pause" : "play");
			playButton.classList.toggle("is-playing", isPlaying);
			playButton.disabled = !playEnabled;
			playButton.title = label;
			playButton.setAttribute("aria-label", label);
		}
		if (muteButton) {
			const label = (isMuted ? "Unmute " : "Mute ") + mediaLabel;
			setPreviewMediaButtonIcon(muteButton, isMuted ? "muted" : "volume");
			muteButton.classList.toggle("is-muted", isMuted);
			muteButton.disabled = !volumeEnabled;
			muteButton.title = label;
			muteButton.setAttribute("aria-label", label);
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

		const playLinkedAudio = function() {
			if (typeof video.__previewPlayAudioTrack === "function") {
				video.__previewPlayAudioTrack();
			}
		};
		const playPromise = video.play();
		playLinkedAudio();
		if (playPromise && typeof playPromise.catch === "function") {
			playPromise.catch(function(error) {
				if (video.__previewAudioTrack) {
					video.__previewAudioTrack.pause();
				}
				console.warn("Preview video playback failed", error && error.name ? error.name : error);
			});
		}
		if (playPromise && typeof playPromise.then === "function") {
			playPromise.then(function() {
				if (!video.paused) {
					playLinkedAudio();
				}
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
	const previewCardSizeNextIcons = {
		"default": "size-large",
		"large": "size-compact",
		"compact": "size-default"
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
		setPreviewMediaButtonIcon(button, previewCardSizeNextIcons[current] || previewCardSizeNextIcons.default);
		button.dataset.sizeLabel = buttonLabel;
		button.dataset.sizeAction = next;
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
		setPreviewMediaButtonIcon(sizeButton, previewCardSizeNextIcons.default);
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

	function normalizeTikTokEmbedUrlForNativeControls(embedUrl) {
		try {
			const url = new URL(embedUrl || "", window.location.href);
			const host = url.hostname.toLowerCase();
			if (host !== "tiktok.com" && !host.endsWith(".tiktok.com")) {
				return embedUrl;
			}
			[
				"controls",
				"progress_bar",
				"play_button",
				"volume_control",
				"fullscreen_button",
				"timestamp",
				"closed_caption",
				"native_context_menu"
			].forEach(function(parameter) {
				url.searchParams.delete(parameter);
			});
			url.searchParams.set("autoplay", "0");
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
		iframe.addEventListener("load", function() {
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
			if (embedKind === "youtube") {
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
			: (embedKind === "tiktok" ? normalizeTikTokEmbedUrlForNativeControls(embedUrl) : embedUrl);
		iframe.title = preview.title || preview.subtitle || "Embedded preview";
		iframe.loading = "lazy";
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

	function previewTikTokUrlHandle(preview) {
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
		const handle = segments.length && /^@/.test(segments[0]) ? segments[0] : "";
		return handle ? "@" + handle.replace(/^@+/, "") : "";
	}

	function previewTikTokAuthor(preview, sourceLabel) {
		const candidates = [
			String((preview && preview.subtitle) || "").trim(),
			String(sourceLabel || "").trim()
		];
		for (let i = 0; i < candidates.length; ++i) {
			let candidate = candidates[i];
			if (!candidate || /^tiktok(?:\.com)?$/i.test(candidate)) {
				continue;
			}
			const byMatch = candidate.match(/^TikTok\s+by\s+(.+)$/i);
			if (byMatch && byMatch[1]) {
				candidate = byMatch[1].trim();
			}
			if (candidate && !/^tiktok(?:\.com)?$/i.test(candidate)) {
				return candidate;
			}
		}
		return "";
	}

	function previewCleanTikTokCaption(value) {
		return String(value || "")
			.replace(/\s+/g, " ")
			.replace(/([^\s#])#/g, "$1 #")
			.trim();
	}

	function previewTikTokCaptionText(preview, descriptionText) {
		const placeholders = {
			"fetching page metadata": true,
			"loading link preview...": true,
			"preview unavailable": true,
			"tiktok video": true
		};
		const title = previewCleanTikTokCaption(preview && preview.title);
		const description = previewCleanTikTokCaption(descriptionText);
		const titleKey = title.toLowerCase();
		if (title && !placeholders[titleKey]) {
			return title;
		}
		const descriptionKey = description.toLowerCase();
		return description && !placeholders[descriptionKey] ? description : "";
	}

	function previewTikTokCaptionParts(captionText) {
		const caption = previewCleanTikTokCaption(captionText);
		const tags = caption.match(/#[^\s#]+/g) || [];
		const body = caption.replace(/#[^\s#]+/g, "").replace(/\s+/g, " ").trim();
		return { body: body, tags: tags };
	}

	function appendTikTokEmbedCopy(card, preview, hostLabel, sourceLabel, descriptionText, activateCard) {
		const copy = document.createElement("div");
		copy.className = "preview-card-copy preview-card-tiktok-copy";

		const meta = document.createElement("div");
		meta.className = "preview-card-meta preview-card-tiktok-meta";
		const source = document.createElement("div");
		source.className = "preview-card-subtitle preview-card-tiktok-source";
		source.textContent = "TikTok";
		meta.appendChild(source);
		const badge = document.createElement("div");
		badge.className = "preview-card-badge preview-card-tiktok-badge";
		badge.textContent = hostLabel || "tiktok.com";
		meta.appendChild(badge);
		copy.appendChild(meta);

		const author = previewTikTokAuthor(preview, sourceLabel);
		const handle = previewTikTokUrlHandle(preview);
		if (author || handle) {
			const authorLine = document.createElement("div");
			authorLine.className = "preview-card-tiktok-author";
			const normalizedAuthor = author.replace(/^@+/, "").toLowerCase();
			const normalizedHandle = handle.replace(/^@+/, "").toLowerCase();
			authorLine.textContent = author && handle && normalizedAuthor !== normalizedHandle
				? "By " + author + " " + handle
				: "By " + (author || handle);
			copy.appendChild(authorLine);
		}

		const caption = previewTikTokCaptionParts(previewTikTokCaptionText(preview, descriptionText));
		const captionBody = caption.body || "TikTok video";
		const title = document.createElement("div");
		title.className = "preview-card-title preview-card-tiktok-caption";
		title.textContent = captionBody;
		copy.appendChild(title);

		if (caption.tags.length) {
			const tags = document.createElement("div");
			tags.className = "preview-card-tiktok-tags";
			const visibleTags = caption.tags.slice(0, 5);
			visibleTags.forEach(function(tagText) {
				const tag = document.createElement("span");
				tag.className = "preview-card-tiktok-tag";
				tag.textContent = tagText;
				tags.appendChild(tag);
			});
			if (caption.tags.length > visibleTags.length) {
				const more = document.createElement("span");
				more.className = "preview-card-tiktok-tag preview-card-tiktok-more";
				more.textContent = "+" + String(caption.tags.length - visibleTags.length);
				tags.appendChild(more);
			}
			copy.appendChild(tags);
		}

		const footer = document.createElement("div");
		footer.className = "preview-card-footer preview-card-tiktok-footer";
		const action = document.createElement("button");
		action.type = "button";
		action.className = "preview-card-action preview-card-open-button preview-card-tiktok-open-button";
		action.textContent = (preview && preview.openLabel) || "Open on TikTok";
		action.addEventListener("click", activateCard);
		footer.appendChild(action);
		copy.appendChild(footer);

		card.appendChild(copy);
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
		const reviewPercent = Number(metadata.steamReviewPercent);
		const reviewTotal = Number(metadata.steamReviewTotal);
		const reviewScore = Number(metadata.steamReviewScore);
		const recommendationsTotal = Number(metadata.steamRecommendationsTotal);
		const metacriticScore = Number(metadata.steamMetacriticScore);
		return {
			appName: String(metadata.steamAppName || "").trim(),
			price: String(metadata.steamPrice || "").trim(),
			originalPrice: String(metadata.steamOriginalPrice || "").trim(),
			discount: Number.isFinite(discount) && discount > 0 ? discount : 0,
			developer: String(metadata.steamDeveloper || "").trim(),
			releaseDate: String(metadata.steamReleaseDate || "").trim(),
			platforms: String(metadata.steamPlatforms || "").trim(),
			genres: String(metadata.steamGenres || "").trim(),
			reviewSummary: String(metadata.steamReviewSummary || "").trim(),
			reviewPercent: Number.isFinite(reviewPercent) ? Math.max(0, Math.min(100, Math.round(reviewPercent))) : null,
			reviewTotal: Number.isFinite(reviewTotal) && reviewTotal > 0 ? reviewTotal : 0,
			reviewScore: Number.isFinite(reviewScore) ? reviewScore : 0,
			recommendationsTotal: Number.isFinite(recommendationsTotal) && recommendationsTotal > 0 ? recommendationsTotal : 0,
			metacriticScore: Number.isFinite(metacriticScore) && metacriticScore > 0 ? metacriticScore : 0,
			description: String(descriptionText || "").trim()
		};
	}

	function steamReviewSentimentClass(info) {
		const score = Number(info && info.reviewScore);
		const summary = String(info && info.reviewSummary || "").toLowerCase();
		if (score >= 7 || summary.indexOf("positive") >= 0) {
			return "is-positive";
		}
		if (score >= 5 || summary.indexOf("mixed") >= 0) {
			return "is-mixed";
		}
		if (score > 0 || summary.indexOf("negative") >= 0) {
			return "is-negative";
		}
		return "";
	}

	function appendSteamStat(container, labelText, valueText, detailText, className) {
		if (!container || !valueText) {
			return false;
		}

		const stat = document.createElement("div");
		stat.className = "preview-card-steam-stat" + (className ? " " + className : "");
		const label = document.createElement("span");
		label.className = "preview-card-steam-stat-label";
		label.textContent = labelText;
		stat.appendChild(label);
		const value = document.createElement("strong");
		value.className = "preview-card-steam-stat-value";
		value.textContent = valueText;
		stat.appendChild(value);
		if (detailText) {
			const detail = document.createElement("span");
			detail.className = "preview-card-steam-stat-detail";
			detail.textContent = detailText;
			stat.appendChild(detail);
		}
		container.appendChild(stat);
		return true;
	}

	function appendSteamStats(body, info) {
		const stats = document.createElement("div");
		stats.className = "preview-card-steam-stats";
		let appended = false;

		if (info.reviewSummary) {
			const detailParts = [];
			if (info.reviewPercent !== null) {
				detailParts.push(String(info.reviewPercent) + "% positive");
			}
			if (info.reviewTotal) {
				detailParts.push(formatPreviewCompactNumber(info.reviewTotal) + " reviews");
			}
			appended = appendSteamStat(stats, "User reviews", info.reviewSummary, detailParts.join(" / "),
				steamReviewSentimentClass(info)) || appended;
		}
		if (info.metacriticScore) {
			appended = appendSteamStat(stats, "Metacritic", String(Math.round(info.metacriticScore)), "critic score",
				"is-mixed") || appended;
		}
		if (info.recommendationsTotal) {
			appended = appendSteamStat(stats, "Recommendations",
				formatPreviewCompactNumber(info.recommendationsTotal), "Steam users", "") || appended;
		}

		if (appended) {
			body.appendChild(stats);
		}
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

		appendSteamStats(body, info);

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
		{ provider: "power", kind: "product", label: "POWER", mark: "P", hostSuffixes: ["power.se"] },
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
		if (Array.isArray(metadata.productImages)) {
			metadata.productImages.forEach(function(item) {
				addItem(item, "image");
			});
		}
		if (!items.length && metadata.productImage) {
			addItem({ url: metadata.productImage, mime: "image/jpeg", kind: "image" });
		}
		if (!items.length && Array.isArray(preview && preview.mediaItems)) {
			preview.mediaItems.forEach(function(item) {
				addItem(item);
			});
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

	function previewInstagramLooksLikeHost(value) {
		let normalized = String(value || "").trim().toLowerCase();
		normalized = normalized.replace(/^https?:\/\//, "").replace(/\/+$/, "");
		return normalized === "instagram.com" || normalized === "www.instagram.com"
			|| normalized === "instagr.am" || normalized === "www.instagr.am";
	}

	function previewInstagramDisplayName(preview, handle) {
		const metadata = (preview && preview.metadata) || {};
		const metadataName = String(metadata.instagramDisplayName || "").trim();
		if (metadataName) {
			return metadataName;
		}
		const subtitle = String((preview && preview.subtitle) || "").trim();
		if (subtitle && !/^instagram$/i.test(subtitle)
			&& !previewInstagramLooksLikeHost(subtitle)
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
			&& !previewInstagramLooksLikeHost(title)
			&& !/^post by @/i.test(title)) {
			return title;
		}
		const description = String(descriptionText || "").trim();
		if (description && !/^@?[a-z0-9._]+$/i.test(description)
			&& !previewInstagramLooksLikeHost(description)
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
		const cardUsesDiv = hasInteractiveMedia || isSteam || isGitHub || isGameStore || !!richProviderSpec;
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
		const requestedPreviewSize = String(preview.previewSize || preview.size || "").trim().toLowerCase();
		const initialPreviewSize = previewCardSizeOrder.indexOf(requestedPreviewSize) >= 0
			? requestedPreviewSize
			: "";

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
			+ (isYouTubeEmbed && embedAspect === "short" ? " is-youtube-short" : "")
			+ (initialPreviewSize === "large" ? " is-expanded" : "")
			+ (initialPreviewSize === "compact" ? " is-compact" : "");
		if (initialPreviewSize) {
			card.dataset.previewSize = initialPreviewSize;
			card.style.setProperty("--preview-card-target-width",
				previewCardBubbleWidthVars[initialPreviewSize] || previewCardBubbleWidthVars.default);
		}
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
			if (embedKind === "tiktok") {
				appendTikTokEmbedCopy(card, preview, hostLabel, sourceLabel, descriptionText, activateCard);
				return card;
			}
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
			setOpenReactionPickerMessageId(null);
			notifyBridge("toggleReaction", message.messageId, reaction.emoji || "", !reaction.selfReacted);
		});
		return button;
	}

	function renderReactionPicker(message) {
		if (!message || !message.canReact || !reactionPickerOpenForMessage(message)) {
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
				setOpenReactionPickerMessageId(null);
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

		setOpenReactionPickerMessageId(null);
		notifyBridge("deleteMessage", message.messageId);
	}

	function normalizedDeliveryState(message) {
		if (!message || !message.own) {
			return "";
		}
		const state = String(message.deliveryState || "").trim().toLowerCase();
		if (state === "sending" || state === "failed") {
			return state;
		}
		return "";
	}

	function renderMessageDeliveryStatus(message) {
		const state = normalizedDeliveryState(message);
		if (!state) {
			return null;
		}

		const row = document.createElement("div");
		row.className = "message-delivery-status is-" + state;
		row.dataset.deliveryState = state;
		const label = document.createElement("span");
		label.className = "message-delivery-label";
		label.textContent = String(message.deliveryLabel || (state === "sending" ? "Sending..." : "Not delivered"));
		row.appendChild(label);

		if (state === "failed" && message.deliveryCanRetry !== false) {
			const retry = document.createElement("button");
			retry.type = "button";
			retry.className = "message-delivery-retry";
			retry.textContent = String(message.deliveryRetryLabel || "Retry");
			retry.addEventListener("click", function(event) {
				event.preventDefault();
				event.stopPropagation();
				notifyBridge("retryMessageDelivery",
					String(message.clientMessageId || message.localMessageId || message.messageId || ""));
			});
			row.appendChild(retry);
		}

		return row;
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
					setOpenReactionPickerMessageId(null);
					notifyBridge("startReply", message.messageId);
					focusComposerInput();
				});
				toolbar.appendChild(replyButton);
			}

			if (canReact) {
				const reactionButton = document.createElement("button");
				reactionButton.type = "button";
				reactionButton.className = "icon-button bubble-toolbar-button bubble-toolbar-icon reaction-picker-toggle"
					+ (reactionPickerOpenForMessage(message) ? " is-active" : "");
				reactionButton.innerHTML =
					"<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M22 11v1a10 10 0 1 1-9-10\"></path><path d=\"M8 14s1.5 2 4 2 4-2 4-2\"></path><path d=\"M9 9h.01\"></path><path d=\"M15 9h.01\"></path><path d=\"M16 5h6\"></path><path d=\"M19 2v6\"></path></svg>";
				reactionButton.title = "Add reaction";
				reactionButton.setAttribute("aria-label", "Add reaction");
				reactionButton.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					const willOpen = !reactionPickerOpenForMessage(message);
					setOpenReactionPickerMessageId(willOpen ? message.messageId : null);
					if (willOpen) {
						pauseReactionPickerScrollClose();
					} else {
						reactionPickerScrollClosePausedUntil = 0;
					}
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

	function normalizedMessageSearchText() {
		return String(messageSearchText || "").trim().toLowerCase();
	}

	function setMessageSearchOpen(open) {
		messageSearchOpen = !!open;
		if (!messageSearchOpen) {
			messageSearchText = "";
			if (refs.msgSearchInput) {
				refs.msgSearchInput.value = "";
			}
		}
		syncMessageSearchState();
		if (messageSearchOpen && refs.msgSearchInput) {
			refs.msgSearchInput.focus({ preventScroll: true });
			refs.msgSearchInput.select();
		}
	}

	function messageElementSearchText(element) {
		if (!element) {
			return "";
		}
		return String(element.dataset.bodyText || element.textContent || "").toLowerCase();
	}

	function syncMessageSearchState() {
		if (!refs.msgSearchBar || !refs.messageList) {
			return;
		}

		const query = normalizedMessageSearchText();
		const active = messageSearchOpen || !!query;
		let matchCount = 0;
		refs.msgSearchBar.classList.toggle("hidden", !active);
		refs.msgSearchBar.setAttribute("aria-hidden", active ? "false" : "true");
		if (refs.msgSearchButton) {
			refs.msgSearchButton.classList.toggle("is-active", active);
			refs.msgSearchButton.setAttribute("aria-pressed", active ? "true" : "false");
		}
		if (refs.msgSearchInput && refs.msgSearchInput.value !== messageSearchText) {
			refs.msgSearchInput.value = messageSearchText;
		}

		refs.messageList.querySelectorAll(".message-bubble").forEach(function(bubble) {
			const matched = !!query && messageElementSearchText(bubble).indexOf(query) !== -1;
			bubble.classList.toggle("is-search-match", matched);
			if (matched) {
				matchCount += 1;
			}
		});
		refs.messageList.querySelectorAll(".message-cluster").forEach(function(cluster) {
			if (!query) {
				cluster.classList.remove("is-search-hidden");
				return;
			}
			cluster.classList.toggle("is-search-hidden", !cluster.querySelector(".message-bubble.is-search-match"));
		});
		refs.messageList.querySelectorAll(".system-message").forEach(function(message) {
			if (!query) {
				message.classList.remove("is-search-hidden", "is-search-match");
				return;
			}
			const matched = messageElementSearchText(message).indexOf(query) !== -1;
			message.classList.toggle("is-search-hidden", !matched);
			message.classList.toggle("is-search-match", matched);
			if (matched) {
				matchCount += 1;
			}
		});
		refs.messageList.querySelectorAll(".day-divider").forEach(function(divider) {
			divider.classList.toggle("is-search-hidden", !!query);
		});
		if (refs.msgSearchCount) {
			refs.msgSearchCount.textContent = query
				? (matchCount === 1 ? "1 match" : String(matchCount) + " matches")
				: "";
		}
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
		const embeddedReply = extractEmbeddedReplyQuote(message.bodyHtml || "");
		appendReplyBlock(bubble, {
			replyActor: normalizedReplyPreviewText(message.replyActor) || embeddedReply.replyActor,
			replySnippet: normalizedReplyPreviewText(message.replySnippet) || embeddedReply.replySnippet
		});

		const body = document.createElement("div");
		body.className = "bubble-copy";
		const rawBodyHtml = String(message.bodyHtml || "");
		const bodyHtml = embeddedReply.found ? embeddedReply.bodyHtml : rawBodyHtml;
		body.innerHTML = bodyHtml || (rawBodyHtml.trim() ? "" : escapeHtml(message.bodyText || ""));
		bubble.appendChild(body);

		const previewCard = renderPreviewCard(message);
		if (previewCard) {
			bubble.appendChild(previewCard);
		}
		const previewStub = renderPreviewStub(message);
		if (previewStub) {
			bubble.appendChild(previewStub);
		}

		const deliveryStatus = renderMessageDeliveryStatus(message);
		if (deliveryStatus) {
			bubble.appendChild(deliveryStatus);
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

	function setTimelineEmptyState(isEmpty) {
		if (refs.messageList) {
			refs.messageList.classList.toggle("is-empty-timeline", !!isEmpty);
		}
	}

	function markTimelineEntered() {
		if (!refs.messageList) {
			return;
		}
		if (messageListEnterTimer) {
			clearTimeout(messageListEnterTimer);
		}
		refs.messageList.classList.add("is-chat-entering");
		messageListEnterTimer = setTimeout(function() {
			messageListEnterTimer = 0;
			if (refs.messageList) {
				refs.messageList.classList.remove("is-chat-entering");
			}
		}, 220);
	}

	function renderTimeline(messages, emptyCopy, freshTailCount) {
		cancelActiveMessageChunkRender("sync");
		refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
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
			setTimelineEmptyState(true);
			return;
		}

		setTimelineEmptyState(false);
		const freshStartIndex = Math.max(0, indexedMessages.length - Math.max(0, freshTailCount || 0));

		groups.forEach(function(group) {
			appendRenderedMessageGroup(fragment, group, freshStartIndex);
		});
		replaceChildrenWith(refs.messageList, fragment);
		markTimelineEntered();
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
		const atomicReplace = !!(renderState && renderState.atomicReplace);
		messageRenderGeneration = generation;
		activeMessageChunkRender = {
			generation: generation,
			startedAt: startedAt,
			preparing: true
		};

		const sourceMessages = messages || [];
		const shouldYieldBeforeRender = refs.messageList.classList.contains("is-chat-loading")
			|| sourceMessages.length >= messageRenderLoadingThreshold;
		const keepExistingDuringPrepare = atomicReplace && messageListHasRenderedTimeline();
		if (shouldYieldBeforeRender && keepExistingDuringPrepare) {
			refs.messageList.classList.remove("is-chat-loading");
			refs.messageList.classList.add("is-chat-transitioning");
			setTimelineEmptyState(false);
		} else if (shouldYieldBeforeRender && !refs.messageList.querySelector(".chat-loading-spinner")) {
			const loadingFragment = document.createDocumentFragment();
			loadingFragment.appendChild(createChatLoadingIndicator());
			refs.messageList.classList.add("is-chat-loading");
			refs.messageList.classList.remove("is-chat-transitioning");
			setTimelineEmptyState(false);
			replaceChildrenWith(refs.messageList, loadingFragment);
		} else if (!shouldYieldBeforeRender) {
			refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
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
			if (!atomicReplace) {
				const emptyFragment = document.createDocumentFragment();
				refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
				replaceChildrenWith(refs.messageList, emptyFragment);
			}

			if (!groups.length) {
				const fragment = document.createDocumentFragment();
				const empty = document.createElement("div");
				empty.className = "empty-state";
				empty.innerHTML = "<h2>No history yet</h2><p></p>";
				empty.querySelector("p").textContent =
					emptyCopy || "Messages will appear here once the selected room has activity.";
				fragment.appendChild(empty);
				refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
				replaceChildrenWith(refs.messageList, fragment);
				setTimelineEmptyState(true);
				markTimelineEntered();
				activeMessageChunkRender = null;
				applyTimelineRenderScrollState(renderState, true);
				if (renderState && typeof renderState.onComplete === "function") {
					renderState.onComplete();
				}
				traceModernUi("messages chunk empty", startedAt);
				return;
			}

			const freshStartIndex = Math.max(0, indexedMessages.length - Math.max(0, freshTailCount || 0));
			setTimelineEmptyState(false);
			activeMessageChunkRender = {
				generation: generation,
				startedAt: startedAt
			};
			if (atomicReplace) {
				renderState.fragment = document.createDocumentFragment();
			}

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

				(atomicReplace ? renderState.fragment : refs.messageList).appendChild(fragment);
				if (!atomicReplace) {
					applyPendingMessageUpdatePatches(true);
					applyTimelineRenderScrollState(renderState, false);
				}
				traceModernUi("messages chunk " + String(renderState.nextGroupIndex) + "/" + String(groups.length), chunkStartedAt);

				if (renderState.nextGroupIndex < groups.length) {
					requestAnimationFrame(appendChunk);
					return;
				}

				activeMessageChunkRender = null;
				if (atomicReplace) {
					refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
					replaceChildrenWith(refs.messageList, renderState.fragment);
					markTimelineEntered();
				}
				applyPendingMessageUpdatePatches(false);
				applyTimelineRenderScrollState(renderState, true);
				if (typeof renderState.onComplete === "function") {
					renderState.onComplete();
				}
				if (renderState.deferPreviewHydrationMs) {
					previewHydrationPausedUntil = Math.max(
						previewHydrationPausedUntil,
						monotonicNow() + Math.max(0, Number(renderState.deferPreviewHydrationMs) || 0));
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

		refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
		setTimelineEmptyState(false);
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

	function stableRenderValue(value) {
		if (Array.isArray(value)) {
			return value.map(stableRenderValue);
		}
		if (value && typeof value === "object") {
			const result = {};
			Object.keys(value).sort().forEach(function(key) {
				result[key] = stableRenderValue(value[key]);
			});
			return result;
		}
		return value;
	}

	function messageNonFooterRenderSignature(message) {
		const omittedKeys = {
			canDelete: true,
			canReact: true,
			canReply: true,
			reactions: true,
			renderIndex: true
		};
		const state = {};
		Object.keys(message || {}).sort().forEach(function(key) {
			if (!omittedKeys[key]) {
				state[key] = stableRenderValue(message[key]);
			}
		});
		return JSON.stringify(state);
	}

	function messageCanPatchFooterOnly(previousMessage, nextMessage) {
		return !!previousMessage
			&& !!nextMessage
			&& !previousMessage.system
			&& !nextMessage.system
			&& messageNonFooterRenderSignature(previousMessage) === messageNonFooterRenderSignature(nextMessage);
	}

	function directChildWithClass(parent, className) {
		if (!parent) {
			return null;
		}
		for (let index = 0; index < parent.children.length; index += 1) {
			const child = parent.children[index];
			if (child && child.classList && child.classList.contains(className)) {
				return child;
			}
		}
		return null;
	}

	function replaceRenderedMessageFooter(message) {
		const element = renderedMessageElement(message);
		if (!element || message.system) {
			return false;
		}

		const previousFooter = directChildWithClass(element, "bubble-footer");
		const nextFooter = renderMessageFooter(message);
		if (previousFooter && nextFooter) {
			previousFooter.replaceWith(nextFooter);
		} else if (previousFooter) {
			previousFooter.remove();
		} else if (nextFooter) {
			element.appendChild(nextFooter);
		} else {
			return true;
		}

		requestAnimationFrame(syncScrollState);
		return true;
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

	function applyRenderedMessageUpdate(message, options) {
		if (options && options.footerOnly && replaceRenderedMessageFooter(message)) {
			return true;
		}
		return replaceRenderedMessage(message);
	}

	function pendingMessageUpdateKey(message) {
		const id = String(message && message.messageId || "");
		const threadId = String(message && message.threadId || "");
		if (id) {
			return "id:" + id + ":" + threadId;
		}
		return "key:" + messageKey(message);
	}

	function queuePendingMessageUpdatePatch(message, options) {
		const key = pendingMessageUpdateKey(message);
		for (let index = 0; index < pendingMessageUpdatePatches.length; index += 1) {
			if (pendingMessageUpdatePatches[index].key === key) {
				pendingMessageUpdatePatches[index].message = message;
				pendingMessageUpdatePatches[index].footerOnly =
					pendingMessageUpdatePatches[index].footerOnly && !!(options && options.footerOnly);
				return;
			}
		}

		pendingMessageUpdatePatches.push({
			key: key,
			message: message,
			footerOnly: !!(options && options.footerOnly)
		});
	}

	function applyPendingMessageUpdatePatches(onlyRenderedTargets) {
		if (!pendingMessageUpdatePatches.length) {
			return 0;
		}

		let appliedCount = 0;
		const remaining = [];
		pendingMessageUpdatePatches.forEach(function(update) {
			if (applyRenderedMessageUpdate(update.message, update)) {
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

	function scheduleMessageListScrollStateSync() {
		if (messageListScrollSyncFrame) {
			return;
		}

		messageListScrollSyncFrame = requestAnimationFrame(function() {
			messageListScrollSyncFrame = 0;
			syncScrollState();
		});
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

	function normalizedReactionPickerMessageId(messageId) {
		const id = String(messageId === undefined || messageId === null ? "" : messageId).trim();
		return id ? id : null;
	}

	function reactionPickerOpenForMessage(message) {
		return !!message
			&& normalizedReactionPickerMessageId(openReactionPickerMessageId)
				=== normalizedReactionPickerMessageId(message.messageId);
	}

	function refreshRenderedMessageFooter(messageId) {
		const normalizedId = normalizedReactionPickerMessageId(messageId);
		if (!normalizedId) {
			return false;
		}

		const message = findMessageState(getSnapshot(), normalizedId);
		return !!message && replaceRenderedMessageFooter(message);
	}

	function setOpenReactionPickerMessageId(messageId) {
		const previousId = normalizedReactionPickerMessageId(openReactionPickerMessageId);
		const nextId = normalizedReactionPickerMessageId(messageId);
		if (previousId === nextId) {
			openReactionPickerMessageId = nextId;
			return false;
		}

		openReactionPickerMessageId = nextId;
		refreshRenderedMessageFooter(previousId);
		refreshRenderedMessageFooter(nextId);
		return true;
	}

	function renderMessages(snapshot, options) {
		const renderOptions = options || {};
		const scope = snapshot.activeScope || {};
		const directConversation = activeDirectMessageConversation(snapshot);
		const messages = directConversation ? directMessageTimelineMessages(directConversation) : (snapshot.messages || []);
		const emptyCopy = directConversation
			? (directConversation.emptyCopy || ("Direct messages with " + (directConversation.label || "this user") + " will appear here."))
			: (scope.emptyCopy || "");
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
		reopenPreviewHydrationForStubs(messages);
		if (openReactionPickerMessageId !== null && !messages.some(function(message) {
			return message && normalizedReactionPickerMessageId(message.messageId)
				=== normalizedReactionPickerMessageId(openReactionPickerMessageId);
		})) {
			openReactionPickerMessageId = null;
		}

		if (detachedBeforeRender && freshTailCount > 0) {
			unreadDetachedMessages += freshTailCount;
		}

		if (!directConversation
			&& (scope.serverLogRevision || Object.prototype.hasOwnProperty.call(scope, "serverLogHtml"))) {
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
				refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
				fragment.appendChild(cachedServerLogElement);
			} else {
				refs.messageList.classList.add("is-chat-loading");
				refs.messageList.classList.remove("is-chat-transitioning");
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
			syncMessageSearchState();
			return;
		}

		const shouldStickToBottom = (scope.scrollToBottom !== false)
			&& (scopeChanged || (!detachedBeforeRender && messages.length >= lastRenderedMessageCount));
		const shouldAtomicReplaceTimeline = scopeChanged
			|| (lastRenderedMessageCount === 0 && messages.length >= messageRenderLoadingThreshold)
			|| (timelineMode === "linkDense" && messages.length >= messageRenderLoadingThreshold);
		const finishRender = function() {
			lastRenderedMessageCount = messages.length;
			lastRenderedTailKey = latestTailKey;
			lastScopeToken = scopeToken;
			syncMessageSearchState();
		};

		if (renderOptions.forceSync) {
			renderTimeline(messages, emptyCopy, freshTailCount);
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
		renderTimelineChunked(messages, emptyCopy, freshTailCount, {
			detachedBeforeRender: detachedBeforeRender,
			distanceFromBottom: distanceFromBottom,
			shouldStickToBottom: shouldStickToBottom,
			timelineMode: timelineMode,
			atomicReplace: shouldAtomicReplaceTimeline,
			deferPreviewHydrationMs: shouldAtomicReplaceTimeline ? previewHydrationTimelineSettleMs : 0,
			onComplete: finishRender
		});
	}

	function setMotdExpanded(nextExpanded) {
		const snapshot = getSnapshot();
		const app = snapshot.app || {};
		motdExpansionTouched = true;
		app.motdExpanded = !!nextExpanded;
		snapshot.app = app;
		railNoteAutoCollapsed = false;
		markMotdSeen(app);
		notifyBridge("invokeAppAction", nextExpanded ? "motd.show" : "motd.hide");
		renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
	}

	function motdContentSignature(value) {
		const text = String(value || "").trim();
		if (!text) {
			return "";
		}

		let hash = 2166136261;
		for (let index = 0; index < text.length; index += 1) {
			hash ^= text.charCodeAt(index);
			hash = Math.imul(hash, 16777619) >>> 0;
		}
		return "v1:" + text.length + ":" + hash.toString(16);
	}

	function syncMotdDismissedSignature(app) {
		if (app && Object.prototype.hasOwnProperty.call(app, "motdDismissedSignature")) {
			motdDismissedSignature = String(app.motdDismissedSignature || "").trim();
		}
		if (app && Object.prototype.hasOwnProperty.call(app, "motdLastSeenSignature")) {
			motdLastSeenSignature = String(app.motdLastSeenSignature || "").trim();
		}
		return motdDismissedSignature;
	}

	function markMotdSeen(app, signature) {
		const seenSignature = String(signature || motdContentSignature(app && app.motdHtml || "")).trim();
		if (!seenSignature) {
			return;
		}
		if (motdLastSeenSignature === seenSignature) {
			return;
		}
		motdLastSeenSignature = seenSignature;
		if (app) {
			app.motdLastSeenSignature = seenSignature;
		}
		notifyBridge("invokeAppActionPayload", "motd.markSeen", {
			signature: seenSignature
		});
	}

	function setMotdDismissedSignature(app, signature) {
		motdDismissedSignature = String(signature || "").trim();
		if (app) {
			app.motdDismissedSignature = motdDismissedSignature;
		}
		if (motdDismissedSignature) {
			motdLastSeenSignature = motdDismissedSignature;
			if (app) {
				app.motdLastSeenSignature = motdDismissedSignature;
			}
		}
		notifyBridge("invokeAppActionPayload", motdDismissedSignature ? "motd.dismiss" : "motd.restore", {
			signature: motdDismissedSignature
		});
	}

	function motdMatchesDismissedSignature(motdHtml, signature) {
		const normalizedSignature = String(signature || "").trim();
		return !!normalizedSignature
			&& (normalizedSignature === motdContentSignature(motdHtml)
				|| normalizedSignature === String(motdHtml || "").trim());
	}

	function motdChangedSinceLastSeen(motdHtml) {
		const comparisonSignature = String(motdLastSeenSignature || motdDismissedSignature || "").trim();
		return !!String(motdHtml || "").trim()
			&& !!comparisonSignature
			&& !motdMatchesDismissedSignature(motdHtml, comparisonSignature);
	}

	window.__mumbleModernMotdProbeAction = function(action, signature) {
		const snapshot = getSnapshot();
		const app = snapshot.app || {};
		const normalizedAction = String(action || "").trim().toLowerCase();
		const resolvedSignature = String(signature || motdContentSignature(app.motdHtml || "")).trim();
		if (!String(app.motdHtml || "").trim()) {
			return { handled: false, reason: "missing-motd" };
		}
		if (normalizedAction === "collapse") {
			setMotdDismissedSignature(app, "");
			motdExpansionTouched = true;
			setMotdExpanded(false);
			renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
			return { handled: true, expanded: false, dismissedSignature: "" };
		}
		if (normalizedAction === "dismiss") {
			if (!resolvedSignature) {
				return { handled: false, reason: "missing-signature" };
			}
			motdExpansionTouched = true;
			setMotdDismissedSignature(app, resolvedSignature);
			renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
			return { handled: true, expanded: !!app.motdExpanded, dismissedSignature: resolvedSignature };
		}
		if (normalizedAction === "restore") {
			setMotdDismissedSignature(app, "");
			motdExpansionTouched = true;
			renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
			return { handled: true, expanded: !!app.motdExpanded, dismissedSignature: "" };
		}
		return { handled: false, reason: "unknown-action" };
	};

	function syncMotdToggleState(hasMotd, dismissed, hiddenForHistory, changed) {
		if (!refs.motdToggle) {
			return;
		}

		const visible = hasMotd && !dismissed && !hiddenForHistory;
		refs.motdToggle.classList.toggle("hidden", !hasMotd);
		refs.motdToggle.classList.toggle("is-active", visible);
		refs.motdToggle.classList.toggle("has-update", hasMotd && changed);
		refs.motdToggle.setAttribute("aria-expanded", visible ? "true" : "false");
		refs.motdToggle.setAttribute("aria-pressed", visible ? "true" : "false");
		refs.motdToggle.dataset.motdToast = hasMotd && changed ? "New" : "";
		refs.motdToggle.title = !hasMotd
			? "Server message of the day"
			: (visible ? "Close welcome message" : (changed ? "Welcome message changed" : "Show welcome message"));
	}

	function renderConversationMotd(app, motdHtml, motdSummary, fullText, hasSummary, messages) {
		if (!refs.motdBanner || !refs.motdBody) {
			return;
		}

		const hasMotd = !!String(motdHtml || "").trim();
		const dismissed = hasMotd && motdMatchesDismissedSignature(motdHtml, motdDismissedSignature);
		const changed = hasMotd && motdChangedSinceLastSeen(motdHtml);
		const hasHistory = (messages || []).some(function(message) {
			return message && !message.system && !message.deleted;
		});
		const hiddenForHistory = hasMotd && hasHistory && !motdExpansionTouched && app.motdAlwaysVisible !== true;
		conversationMotdHiddenForHistory = hiddenForHistory;
		refs.motdBanner.classList.toggle("hidden", !hasMotd || dismissed || hiddenForHistory);
		refs.motdBanner.setAttribute("aria-hidden", hasMotd && !dismissed && !hiddenForHistory ? "false" : "true");
		syncMotdToggleState(hasMotd, dismissed, hiddenForHistory, changed);
		if (hasMotd && !dismissed && !hiddenForHistory && !changed
			&& !motdLastSeenSignature && !motdDismissedSignature) {
			markMotdSeen(app, motdContentSignature(motdHtml));
		}
		if (!hasMotd || dismissed || hiddenForHistory) {
			refs.motdBody.innerHTML = "";
			return;
		}

		refs.motdBanner.classList.toggle("is-collapsed", !noteExpanded);
		if (refs.motdCollapse) {
			refs.motdCollapse.setAttribute("aria-expanded", noteExpanded ? "true" : "false");
			refs.motdCollapse.title = noteExpanded ? "Collapse" : "Expand";
			refs.motdCollapse.setAttribute("aria-label", noteExpanded ? "Collapse message of the day" : "Expand message of the day");
		}
		refs.motdBody.innerHTML = noteExpanded
			? "<div class=\"motd-body-content\">" + motdHtml + "</div>"
			: (hasSummary ? escapedMultilineText(motdSummary) : escapeHtml(fullText || ""));
	}

	function renderNote(app, scope, messages) {
		const motdHtml = String(app.motdHtml || "").trim();
		const motdSummary = String(app.motdSummary || "").trim();
		syncMotdDismissedSignature(app);
		const hasMotd = !!motdHtml;
		refs.noteCard.classList.toggle("hidden", !hasMotd);
		if (!hasMotd) {
			noteExpanded = true;
			railNoteAutoCollapsed = false;
			lastMotdSignature = "";
			motdExpansionTouched = false;
			refs.noteCard.style.maxHeight = "";
			refs.serverSubtitle.style.maxHeight = "";
			refs.noteCard.classList.remove("is-expanded", "has-rich-note");
			refs.noteToggleButton.classList.add("hidden");
			refs.noteToggleButton.setAttribute("aria-expanded", "false");
			renderConversationMotd(app, "", "", "", false, messages || []);
			return;
		}

		const preferredExpanded = app.motdExpanded === true;
		if (lastMotdSignature !== motdHtml) {
			lastMotdSignature = motdHtml;
			railNoteAutoCollapsed = false;
			motdExpansionTouched = false;
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

		renderConversationMotd(app, motdHtml, motdSummary, fullText, hasSummary, messages || []);
		scheduleRailLayoutSync();
	}

	function renderSelfCard(app) {
		const selfName = String(app.selfName || "You").trim() || "You";
		refs.selfAvatar.className = "self-avatar";
		styleAvatar(refs.selfAvatar, selfName, true, app.selfAvatarUrl || "");
		refs.selfName.textContent = selfName;
		refs.selfName.title = selfName;
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

	function syncMockupShellChrome(app) {
		app = app || {};
		const serverIdentity = app.serverIdentity || {};
		const serverIdentityKind = String(app.serverIdentityKind || "").trim().toLowerCase();
		const appIdentity = serverIdentityKind === "application" || (!app.canDisconnect && !app.serverAvatarUrl);
		refs.brandTitle.textContent = app.serverTitle || "Mumble";
		refs.brandSubtitle.textContent = app.serverSubtitle || "Room-first shell";
		if (refs.brandBadge) {
			refs.brandBadge.classList.toggle("is-mumble-app", appIdentity);
			refs.brandBadge.classList.toggle("is-editable", !!serverIdentity.canEdit);
			refs.brandBadge.dataset.identityKind = appIdentity ? "application" : "server";
			styleAvatar(refs.brandBadge,
				serverIdentity.resolvedMonogram || app.serverTitle || "Mumble",
				false,
				app.serverAvatarUrl || serverIdentity.imageUrl || "");
			if (!(app.serverAvatarUrl || serverIdentity.imageUrl) && serverIdentity.resolvedMonogram) {
				refs.brandBadge.textContent = String(serverIdentity.resolvedMonogram).slice(0, 4).toUpperCase();
			}
			refs.brandBadge.tabIndex = serverIdentity.canEdit ? 0 : -1;
			refs.brandBadge.setAttribute("role", serverIdentity.canEdit ? "button" : "img");
			refs.brandBadge.title = serverIdentity.canEdit
				? "Open server settings"
				: (app.serverTitle || "Server");
		}
		applyStatePill(refs.layoutPill, app.layoutLabel || "Modern", app.layoutTone || "");
		applyStatePill(refs.connectionPill, app.connectionLabel || "Disconnected", app.connectionTone || "",
			app.connectionTooltip || "");
		applyStatePill(refs.compatPill, app.compatibilityLabel || "Standard server", app.compatibilityTone || "");
		refs.muteButton.setAttribute("aria-pressed", app.selfMuted ? "true" : "false");
		refs.deafButton.setAttribute("aria-pressed", app.selfDeafened ? "true" : "false");
		refs.settingsButton.title = app.serverTitle ? "Open " + app.serverTitle + " menu" : "Open Mumble menu";
		refs.settingsButton.setAttribute("aria-label", app.serverTitle ? "Open " + app.serverTitle + " menu" : "Open Mumble menu");
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
					{ id: "help.versionCheck", label: "Check for updates", enabled: true }
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

	function modernDialogFieldKey(dialogId, fieldId) {
		return String(dialogId || "") + "\n" + String(fieldId || "");
	}

	function modernDialogValuesEqual(left, right) {
		if (left === right) {
			return true;
		}
		if (left == null || right == null) {
			return left == null && right == null;
		}
		if (typeof left === "object" || typeof right === "object") {
			try {
				return JSON.stringify(left) === JSON.stringify(right);
			} catch (error) {
				// Fall through to string comparison for non-serializable values.
			}
		}
		return String(left) === String(right);
	}

	function modernDialogCloneFieldValue(value) {
		if (value == null || typeof value !== "object") {
			return value;
		}
		try {
			return JSON.parse(JSON.stringify(value));
		} catch (error) {
			return value;
		}
	}

	function forEachModernDialogField(state, callback) {
		if (!state || !Array.isArray(state.sections)) {
			return;
		}

		state.sections.forEach(function(section) {
			(section.fields || []).forEach(function(field) {
				callback(field);
			});
		});
	}

	function findModernDialogField(state, fieldId) {
		const normalizedId = String(fieldId || "");
		if (!normalizedId || !state || !Array.isArray(state.sections)) {
			return null;
		}
		for (let sectionIndex = 0; sectionIndex < state.sections.length; sectionIndex += 1) {
			const fields = Array.isArray(state.sections[sectionIndex] && state.sections[sectionIndex].fields)
				? state.sections[sectionIndex].fields
				: [];
			for (let fieldIndex = 0; fieldIndex < fields.length; fieldIndex += 1) {
				const field = fields[fieldIndex] || {};
				if (String(field.id || "") === normalizedId) {
					return field;
				}
			}
		}
		return null;
	}

	function modernDialogFocusedFieldId() {
		const activeElement = document.activeElement;
		if (!activeElement || !activeElement.dataset) {
			return "";
		}
		return String(activeElement.dataset.modernDialogFieldId || "");
	}

	function rememberFocusedModernDialogFieldValue() {
		const activeElement = document.activeElement;
		const fieldId = modernDialogFocusedFieldId();
		if (!fieldId || !activeElement) {
			return;
		}
		const field = findModernDialogField(modernDialogState, fieldId);
		if (!field) {
			return;
		}
		rememberModernDialogFieldValue(fieldId, modernDialogInputValue(field, activeElement));
	}

	function applyPendingModernDialogFieldValues(state) {
		if (!state || !Array.isArray(state.sections)) {
			return state;
		}

		const dialogId = String(state.id || "");
		const focusedFieldId = modernDialogFocusedFieldId();
		const now = Date.now();
		const localRetentionMs = 10 * 60 * 1000;
		const sourceConflictGraceMs = 2500;

		forEachModernDialogField(state, function(field) {
			const fieldId = String(field && field.id || "");
			if (!fieldId) {
				return;
			}

			const pending = modernDialogPendingFieldUpdates[fieldId];
			if (pending && String(pending.dialogId || "") === dialogId) {
				field.value = pending.value;
				return;
			}

			const key = modernDialogFieldKey(dialogId, fieldId);
			const local = modernDialogLocalFieldValues[key];
			if (!local) {
				return;
			}
			const localAgeMs = Math.max(0, now - Number(local.updatedAt || 0));

			if (modernDialogValuesEqual(field.value, local.value)) {
				if (focusedFieldId !== fieldId) {
					delete modernDialogLocalFieldValues[key];
				}
				return;
			}

			if (focusedFieldId === fieldId) {
				field.value = modernDialogCloneFieldValue(local.value);
				return;
			}

			if (Object.prototype.hasOwnProperty.call(local, "sourceValue")
					&& !modernDialogValuesEqual(field.value, local.sourceValue)
					&& localAgeMs >= sourceConflictGraceMs) {
				delete modernDialogLocalFieldValues[key];
				return;
			}

			if (localAgeMs < localRetentionMs) {
				field.value = modernDialogCloneFieldValue(local.value);
				return;
			}

			delete modernDialogLocalFieldValues[key];
		});

		return state;
	}

	function syncModernDialogState(state) {
		const detachedDialogHost = state && state.open
			&& String(state.host || "") === "window";
		detachedModernDialogUiTweaks = detachedDialogHost && state.uiTweaks
			? state.uiTweaks
			: null;
		if (!renderContainedModernDialogs || detachedDialogHost) {
			modernDialogState = null;
			modernDialogPendingFieldUpdates = {};
			modernDialogLocalFieldValues = {};
			renderModernDialog();
			syncAmbientState(getSnapshot());
			return;
		}

		if (state && state.open) {
			rememberFocusedModernDialogFieldValue();
		}
		if (!state || !state.open) {
			modernDialogPendingFieldUpdates = {};
			modernDialogLocalFieldValues = {};
		}
		if (!state || !state.open || String(state.kind || "") !== "stonks") {
			stonksPendingConfirm = null;
			stonksPendingConfirmFocus = false;
		}
		modernDialogState = applyPendingModernDialogFieldValues(state || null);
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
		const firstBodyControl = focusable.find(function(element) {
			return refs.modernDialogBody && refs.modernDialogBody.contains(element);
		});
		const firstAction = focusable.find(function(element) {
			return refs.modernDialogActions && refs.modernDialogActions.contains(element);
		});
		const firstNonClose = focusable.find(function(element) {
			return element !== refs.modernDialogCloseButton;
		});
		const target = firstField || firstBodyControl || firstAction || firstNonClose || focusable[0] || refs.modernDialog;
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
		flushModernDialogFieldUpdates();
		notifyBridge("invokeModernDialogAction", dialogId, String(actionId), payload || {});
	}

	function closeModernDialog() {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (dialogId) {
			notifyBridge("closeModernDialog", dialogId);
		}
		modernDialogPendingFieldUpdates = {};
		modernDialogLocalFieldValues = {};
		modernDialogState = null;
		renderModernDialog();
	}

	function modernDialogInputValue(field, input) {
		const type = String(field && field.type || "text");
		if (type === "checkbox") {
			return !!input.checked;
		}
		if (type === "select" && String(field && field.valueType || "number") === "string") {
			return input.value;
		}
		if (type === "number" && input.value === "") {
			return "";
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
		const value = modernDialogInputValue(field, input);
		rememberModernDialogFieldValue(fieldId, value);
		scheduleModernDialogFieldUpdate(dialogId, fieldId, value, !modernDialogFieldUpdateShouldDebounce(field));
	}

	function updateModernDialogFieldValue(fieldId, value) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		fieldId = String(fieldId || "");
		if (!dialogId || !fieldId) {
			return;
		}
		rememberModernDialogFieldValue(fieldId, value);
		flushModernDialogFieldUpdate(fieldId);
		notifyBridge("updateModernDialogField", dialogId, fieldId, value);
	}

	function modernDialogFieldUpdateShouldDebounce(field) {
		const type = String(field && field.type || "text");
		return type === "text" || type === "password" || type === "textarea"
			|| type === "number" || type === "pathPicker";
	}

	function scheduleModernDialogFieldUpdate(dialogId, fieldId, value, immediate) {
		const existing = modernDialogPendingFieldUpdates[fieldId];
		if (existing && existing.timer) {
			clearTimeout(existing.timer);
		}
		modernDialogPendingFieldUpdates[fieldId] = { dialogId: dialogId, fieldId: fieldId, value: value, timer: 0 };
		if (immediate) {
			flushModernDialogFieldUpdate(fieldId);
			return;
		}
		modernDialogPendingFieldUpdates[fieldId].timer = window.setTimeout(function() {
			flushModernDialogFieldUpdate(fieldId);
		}, 350);
	}

	function flushModernDialogFieldUpdate(fieldId) {
		const pending = modernDialogPendingFieldUpdates[fieldId];
		if (!pending) {
			return;
		}
		if (pending.timer) {
			clearTimeout(pending.timer);
		}
		delete modernDialogPendingFieldUpdates[fieldId];
		notifyBridge("updateModernDialogField", pending.dialogId, pending.fieldId, pending.value);
	}

	function flushModernDialogFieldUpdates() {
		Object.keys(modernDialogPendingFieldUpdates).forEach(flushModernDialogFieldUpdate);
	}

	function rememberModernDialogFieldValue(fieldId, value) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		fieldId = String(fieldId || "");
		const key = modernDialogFieldKey(dialogId, fieldId);
		const previousLocal = modernDialogLocalFieldValues[key];
		const existingField = findModernDialogField(modernDialogState, fieldId);
		if (dialogId && fieldId) {
			modernDialogLocalFieldValues[key] = {
				dialogId: dialogId,
				fieldId: fieldId,
				value: modernDialogCloneFieldValue(value),
				sourceValue: previousLocal && Object.prototype.hasOwnProperty.call(previousLocal, "sourceValue")
					? previousLocal.sourceValue
					: (existingField ? modernDialogCloneFieldValue(existingField.value) : undefined),
				updatedAt: Date.now()
			};
		}
		if (!modernDialogState || !Array.isArray(modernDialogState.sections)) {
			return;
		}
		forEachModernDialogField(modernDialogState, function(field) {
			if (String(field.id || "") === fieldId) {
				field.value = modernDialogCloneFieldValue(value);
			}
		});
	}

	function modernDialogOptionHint(option) {
		return String(option && (option.tooltip || option.hint) || "");
	}

	function modernDialogActivePageId(dialog) {
		const pages = Array.isArray(dialog && dialog.pages) ? dialog.pages : [];
		for (let i = 0; i < pages.length; ++i) {
			if (pages[i] && pages[i].selected) {
				return String(pages[i].id || pages[i].label || i);
			}
		}
		return String(dialog && dialog.kind || "dialog");
	}

	function modernDialogAdvancedKey(dialog) {
		return String(dialog && dialog.kind || "dialog") + ":" + modernDialogActivePageId(dialog);
	}

	function modernDialogAdvancedVisible(dialog) {
		return !!modernDialogAdvancedPages[modernDialogAdvancedKey(dialog)];
	}

	function setModernDialogAdvancedVisible(dialog, visible) {
		modernDialogAdvancedPages[modernDialogAdvancedKey(dialog)] = !!visible;
	}

	function modernDialogFieldIsAdvanced(field) {
		return !!(field && field.advanced);
	}

	function modernDialogFieldValueById(dialog, fieldId) {
		const sections = Array.isArray(dialog && dialog.sections) ? dialog.sections : [];
		const normalizedId = String(fieldId || "");
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const fields = Array.isArray(sections[sectionIndex] && sections[sectionIndex].fields)
				? sections[sectionIndex].fields
				: [];
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				const field = fields[fieldIndex] || {};
				if (String(field.id || "") === normalizedId) {
					return field.value;
				}
			}
		}
		return undefined;
	}

	function modernDialogFieldMatchesVisibility(dialog, field) {
		if (field && field.visible === false) {
			return false;
		}

		const rule = field && (field.visibleWhen || field.when);
		if (!rule || !rule.fieldId) {
			return true;
		}

		const value = modernDialogFieldValueById(dialog, rule.fieldId);
		const allowedValues = Array.isArray(rule.values) ? rule.values : [rule.value];
		return allowedValues.some(function(allowedValue) {
			return String(allowedValue) === String(value);
		});
	}

	function modernDialogFieldShouldRender(field, showAdvanced, dialog) {
		const type = String(field && field.type || "text");
		return type !== "hidden"
			&& modernDialogFieldMatchesVisibility(dialog, field)
			&& (showAdvanced || !modernDialogFieldIsAdvanced(field));
	}

	function modernDialogHasAdvancedContent(dialog) {
		return !!(dialog && Array.isArray(dialog.sections) && dialog.sections.some(function(section) {
			if (section && section.advanced) {
				return true;
			}
			return Array.isArray(section && section.fields) && section.fields.some(function(field) {
				const type = String(field && field.type || "text");
				return type !== "hidden" && modernDialogFieldIsAdvanced(field);
			});
		}));
	}

	function appendModernDialogAdvancedToggle(container, dialog) {
		if (!modernDialogHasAdvancedContent(dialog)) {
			return;
		}

		const showAdvanced = modernDialogAdvancedVisible(dialog);
		const bar = document.createElement("div");
		bar.className = "modern-dialog-advanced-bar";
		const button = document.createElement("button");
		button.type = "button";
		button.className = "chip-button modern-dialog-advanced-toggle" + (showAdvanced ? " is-active" : "");
		button.textContent = showAdvanced ? "Basic" : "Advanced";
		button.setAttribute("aria-pressed", showAdvanced ? "true" : "false");
		button.title = showAdvanced ? "Hide advanced settings" : "Show advanced settings";
		button.addEventListener("click", function() {
			setModernDialogAdvancedVisible(dialog, !showAdvanced);
			renderModernDialog();
		});
		bar.appendChild(button);
		container.appendChild(bar);
	}

	function createModernDialogLineIcon(paths) {
		const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
		svg.setAttribute("viewBox", "0 0 24 24");
		svg.setAttribute("aria-hidden", "true");
		(paths || []).forEach(function(pathData) {
			const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
			path.setAttribute("d", pathData);
			svg.appendChild(path);
		});
		return svg;
	}

	function modernSettingsIconPaths(pageId) {
		const paths = {
			audioInput: ["M12 3v8", "M8 11a4 4 0 0 0 8 0", "M12 17v4", "M8 21h8"],
			audioOutput: ["M4 9v6h4l5 4V5L8 9H4z", "M16 9a4 4 0 0 1 0 6"],
			look: ["M12 3a9 9 0 1 0 9 9h-4a2 2 0 0 1-2-2V8a5 5 0 0 0-5-5z", "M7 11h.01", "M10 7h.01", "M15 6h.01"],
			ui: ["M4 5h16v14H4z", "M4 10h16", "M9 5v14"],
			messages: ["M5 6h14v9H8l-3 3V6z", "M8 10h8", "M8 13h5"],
			keys: ["M8 14a4 4 0 1 1 2.8-6.8A4 4 0 0 1 8 14z", "M11 11l7 7", "M15 18l3-3", "M13 16l3-3"],
			network: ["M12 4a12 12 0 0 1 0 16", "M12 4a12 12 0 0 0 0 16", "M3 12h18", "M5 7h14", "M5 17h14"],
			screenShare: ["M4 5h16v12H4z", "M8 21h8", "M12 17v4"],
			about: ["M12 11v6", "M12 7h.01", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z"],
			close: ["M8 8l8 8", "M16 8l-8 8"]
		};
		return paths[pageId] || paths.ui;
	}

	function createModernSettingsPageIcon(pageId) {
		const icon = document.createElement("span");
		icon.className = "modern-dialog-tab-icon";
		icon.appendChild(createModernDialogLineIcon(modernSettingsIconPaths(pageId)));
		return icon;
	}

	function modernSettingsSelectedPage(dialog) {
		const pages = Array.isArray(dialog && dialog.pages) ? dialog.pages : [];
		return pages.find(function(page) {
			return page && page.selected;
		}) || pages.find(function(page) {
			return page && page.id === dialog.activePage;
		}) || pages[0] || {};
	}

	function modernSettingsPageMeta(pageId) {
		const meta = {
			audioInput: { eyebrow: "Audio", title: "Audio Input", status: "Input changes are staged until Done." },
			audioOutput: { eyebrow: "Audio", title: "Audio Output", status: "Output changes are staged until Done." },
			look: { eyebrow: "Preferences", title: "Appearance", status: "Visual changes preview live." },
			ui: { eyebrow: "Preferences", title: "User Interface", status: "Window and room-list changes are staged until Done." },
			messages: { eyebrow: "Preferences", title: "Messages & Sounds", status: "Message and sound changes are staged until Done." },
			keys: { eyebrow: "Controls", title: "Key Bindings", status: "Shortcut edits are staged until Done." },
			network: { eyebrow: "Network", title: "Network", status: "Connection changes apply after Done." },
			screenShare: { eyebrow: "Sharing", title: "Screen Sharing", status: "Screen-share changes are staged until Done." },
			about: { eyebrow: "About", title: "About", status: "Version, Qt, and runtime details." }
		};
		return meta[pageId] || { eyebrow: "Settings", title: "Settings", status: "" };
	}

	function modernDialogAdvancedContentCount(dialog) {
		let count = 0;
		(dialog.sections || []).forEach(function(section) {
			if (section && section.advanced) {
				count += 1;
				return;
			}
			(section && section.fields || []).forEach(function(field) {
				if (field && field.advanced && String(field.type || "") !== "hidden") {
					count += 1;
				}
			});
		});
		return count;
	}

	function modernDialogChoiceOptionValue(field, option) {
		const value = option ? option.value : "";
		if (String(field && field.valueType || "number") === "string") {
			return value == null ? "" : String(value);
		}
		const numeric = Number(value);
		return Number.isFinite(numeric) ? numeric : 0;
	}

	function modernDialogChoiceSwatch(field, option, presentation) {
		const swatch = document.createElement("span");
		swatch.className = "modern-dialog-choice-swatch";
		const value = String(option && option.value || "").toLowerCase();
		const themeColors = {
			dark: ["#20262f", "#5ec8b0"],
			light: ["#eff3f7", "#5ec8b0"],
			mocha: ["#2d2433", "#b59cff"],
			macchiato: ["#2a2d42", "#b59cff"],
			frappe: ["#30344a", "#b59cff"],
			latte: ["#eef0f5", "#8d6ce6"],
			nord: ["#2f3845", "#73b7ff"],
			gruvbox: ["#332c24", "#f2c76f"]
		};
		const accentColors = {
			auto: ["#2b3340", "#5ec8b0"],
			teal: ["#203734", "#5ec8b0"],
			blue: ["#21344a", "#73b7ff"],
			violet: ["#302a4a", "#b59cff"],
			amber: ["#3d3321", "#f2c76f"],
			rose: ["#432833", "#ff8aa0"]
		};
		const colors = presentation === "accentGrid" ? accentColors[value] : themeColors[value];
		if (colors) {
			swatch.style.setProperty("--choice-swatch-bg", colors[0]);
			swatch.style.setProperty("--choice-swatch-accent", colors[1]);
		}
		return swatch;
	}

	function appendModernDialogChoiceControl(row, field) {
		const presentation = String(field && field.presentation || "");
		if (presentation !== "themeGrid" && presentation !== "accentGrid" && presentation !== "segmented") {
			return false;
		}
		const selectedValue = !field || field.value == null ? "" : String(field.value);
		const wrap = document.createElement("div");
		wrap.className = "modern-dialog-choice-control is-" + presentation;
		(field.options || []).forEach(function(option) {
			const optionValue = !option || option.value == null ? "" : String(option.value);
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-choice-option" + (optionValue === selectedValue ? " is-selected" : "");
			button.disabled = field.enabled === false || option.enabled === false;
			button.setAttribute("aria-pressed", optionValue === selectedValue ? "true" : "false");
			button.title = modernDialogOptionHint(option) || option.label || optionValue;
			if (presentation === "themeGrid" || presentation === "accentGrid") {
				button.appendChild(modernDialogChoiceSwatch(field, option, presentation));
			}
			const label = document.createElement("span");
			label.className = "modern-dialog-choice-label";
			label.textContent = option.label || optionValue;
			button.appendChild(label);
			button.addEventListener("click", function(event) {
				event.preventDefault();
				if (button.disabled) {
					return;
				}
				updateModernDialogFieldValue(field.id || "", modernDialogChoiceOptionValue(field, option));
				renderModernDialog();
			});
			wrap.appendChild(button);
		});
		row.appendChild(wrap);
		return true;
	}

	function appendModernDialogHighlights(container, dialog) {
		const highlights = Array.isArray(dialog && dialog.highlights) ? dialog.highlights : [];
		if (!highlights.length) {
			return;
		}

		const grid = document.createElement("div");
		grid.className = "modern-dialog-highlights";
		highlights.forEach(function(item) {
			const card = document.createElement("div");
			card.className = "modern-dialog-highlight" + (item.tone ? " is-" + item.tone : "");
			const label = document.createElement("span");
			label.className = "modern-dialog-highlight-label";
			label.textContent = item.label || "";
			const value = document.createElement("strong");
			value.className = "modern-dialog-highlight-value";
			value.textContent = item.value == null || item.value === "" ? "-" : String(item.value);
			card.appendChild(label);
			card.appendChild(value);
			grid.appendChild(card);
		});
		container.appendChild(grid);
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

	function voiceMeterPayloadNumber(candidate, keys) {
		if (!candidate || typeof candidate !== "object") {
			return null;
		}
		for (let index = 0; index < keys.length; index += 1) {
			if (!Object.prototype.hasOwnProperty.call(candidate, keys[index])) {
				continue;
			}
			const value = Number(candidate[keys[index]]);
			if (Number.isFinite(value)) {
				return value;
			}
		}
		return null;
	}

	function voiceMeterInitialPayload(field) {
		if (!field || typeof field !== "object") {
			return null;
		}
		const value = field.value && typeof field.value === "object" && !Array.isArray(field.value)
			? field.value
			: null;
		const meter = field.meter && typeof field.meter === "object" && !Array.isArray(field.meter)
			? field.meter
			: null;
		const candidate = value || meter;
		if (!candidate) {
			return null;
		}
		const amplitude = voiceMeterPayloadNumber(candidate, ["amplitude", "level", "value"]);
		const signalToNoise = voiceMeterPayloadNumber(candidate, ["signalToNoise", "snr", "level"]);
		const hybrid = voiceMeterPayloadNumber(candidate, ["hybrid", "level"]);
		const peakCleanMicDb = voiceMeterPayloadNumber(candidate, ["peakCleanMicDb", "peakDb"]);
		const payload = {
			available: Object.prototype.hasOwnProperty.call(candidate, "available")
				? !!candidate.available
				: (amplitude !== null || signalToNoise !== null || hybrid !== null),
			transmitting: !!candidate.transmitting,
			connected: candidate.connected !== false
		};
		if (amplitude !== null) {
			payload.amplitude = amplitude;
		}
		if (signalToNoise !== null) {
			payload.signalToNoise = signalToNoise;
		}
		if (hybrid !== null) {
			payload.hybrid = hybrid;
		}
		if (peakCleanMicDb !== null) {
			payload.peakCleanMicDb = peakCleanMicDb;
		}
		if (Object.prototype.hasOwnProperty.call(candidate, "loopbackMode")) {
			payload.loopbackMode = Number(candidate.loopbackMode) || 0;
		}
		return payload;
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
		const meters = Array.prototype.slice.call(document.querySelectorAll(".modern-dialog-voice-meter")).filter(function(element) {
			return element.dataset.staticMeter !== "true";
		});
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

	function modernShortcutEditorOptions(options) {
		return Array.isArray(options) ? options : [];
	}

	function modernShortcutEditorSelect(options, selectedValue, valueType, onChange) {
		const select = document.createElement("select");
		const selected = selectedValue == null ? "" : String(selectedValue);
		modernShortcutEditorOptions(options).forEach(function(option) {
			const item = document.createElement("option");
			const optionValue = option && option.value != null ? option.value : "";
			item.value = String(optionValue);
			item.textContent = option && option.label ? String(option.label) : item.value;
			item.disabled = option && option.enabled === false;
			const hint = modernDialogOptionHint(option);
			if (hint) {
				item.title = hint;
			}
			select.appendChild(item);
		});
		if (selected && !Array.prototype.some.call(select.options, function(option) {
			return option.value === selected;
		})) {
			const fallback = document.createElement("option");
			fallback.value = selected;
			fallback.textContent = selected;
			fallback.hidden = true;
			select.appendChild(fallback);
		}
		select.value = selected;
		select.addEventListener("change", function() {
			const raw = select.value;
			const value = valueType === "number" ? Number(raw) : raw;
			onChange(Number.isFinite(value) || valueType !== "number" ? value : 0);
		});
		return select;
	}

	function modernShortcutEditorChip(text, tone) {
		const chip = document.createElement("span");
		chip.className = "modern-shortcut-chip" + (tone ? " is-" + tone : "");
		chip.textContent = text || "";
		return chip;
	}

	function invokeModernShortcutTarget(rowIndex, patch) {
		const payload = Object.assign({ index: rowIndex }, patch || {});
		invokeModernDialogAction("keys.shortcutTarget", payload);
	}

	function appendModernShortcutTargetToggle(container, rowIndex, label, targetAction, checked, disabled) {
		const toggle = document.createElement("label");
		toggle.className = "modern-shortcut-target-check";
		const checkbox = document.createElement("input");
		checkbox.type = "checkbox";
		checkbox.checked = !!checked;
		checkbox.disabled = disabled;
		checkbox.addEventListener("change", function() {
			invokeModernShortcutTarget(rowIndex, {
				targetAction: targetAction,
				enabled: checkbox.checked
			});
		});
		toggle.appendChild(checkbox);
		toggle.appendChild(document.createTextNode(label));
		container.appendChild(toggle);
	}

	function appendModernShortcutTargetControl(container, field, row, disabled) {
		const rowIndex = Number(row && row.index) || 0;
		const target = row && row.target && typeof row.target === "object" ? row.target : {};
		const mode = String(target.mode || "channel");
		const panel = document.createElement("div");
		panel.className = "modern-shortcut-target";

		const summary = document.createElement("div");
		summary.className = "modern-shortcut-target-summary";
		summary.appendChild(modernShortcutEditorChip(target.summary || row && row.dataLabel || "Current", "assigned"));
		panel.appendChild(summary);

		const modeLabel = document.createElement("label");
		modeLabel.className = "modern-shortcut-target-mode";
		const modeText = document.createElement("span");
		modeText.textContent = "Target";
		modeLabel.appendChild(modeText);
		const modeSelect = modernShortcutEditorSelect(field.targetModeOptions || [
			{ value: "selection", label: "Current selection" },
			{ value: "users", label: "List of users" },
			{ value: "channel", label: "Channel" }
		], mode, "string", function(value) {
			invokeModernShortcutTarget(rowIndex, {
				targetAction: "mode",
				mode: value
			});
		});
		modeSelect.disabled = disabled;
		modeLabel.appendChild(modeSelect);
		panel.appendChild(modeLabel);

		if (mode === "users") {
			const users = Array.isArray(target.users) ? target.users : [];
			const userList = document.createElement("div");
			userList.className = "modern-shortcut-target-users";
			if (!users.length) {
				const empty = document.createElement("span");
				empty.className = "modern-shortcut-target-empty";
				empty.textContent = "No users selected";
				userList.appendChild(empty);
			}
			users.forEach(function(user) {
				const chip = document.createElement("span");
				chip.className = "modern-shortcut-target-user";
				chip.appendChild(document.createTextNode(user && user.label || user && user.value || "Unknown"));
				const remove = document.createElement("button");
				remove.type = "button";
				remove.textContent = "Remove";
				remove.disabled = disabled;
				remove.addEventListener("click", function(event) {
					event.preventDefault();
					invokeModernShortcutTarget(rowIndex, {
						targetAction: "removeUser",
						hash: user && user.value || ""
					});
				});
				chip.appendChild(remove);
				userList.appendChild(chip);
			});
			panel.appendChild(userList);

			const addRow = document.createElement("div");
			addRow.className = "modern-shortcut-target-add-user";
			const userOptions = Array.isArray(field.targetUserOptions) ? field.targetUserOptions : [];
			const addSelect = modernShortcutEditorSelect(userOptions, userOptions[0] && userOptions[0].value, "string", function() {});
			addSelect.disabled = disabled || !userOptions.length;
			addRow.appendChild(addSelect);
			const addButton = document.createElement("button");
			addButton.type = "button";
			addButton.className = "chip-button";
			addButton.textContent = "Add";
			addButton.disabled = disabled || !userOptions.length;
			addButton.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernShortcutTarget(rowIndex, {
					targetAction: "addUser",
					hash: addSelect.value
				});
			});
			addRow.appendChild(addButton);
			panel.appendChild(addRow);
		} else if (mode === "channel") {
			const channelLabel = document.createElement("label");
			channelLabel.className = "modern-shortcut-target-channel";
			const label = document.createElement("span");
			label.textContent = "Channel";
			channelLabel.appendChild(label);
			const channelSelect = modernShortcutEditorSelect(field.targetChannelOptions || [], target.channelId, "number", function(value) {
				invokeModernShortcutTarget(rowIndex, {
					targetAction: "channel",
					channelId: value
				});
			});
			channelSelect.disabled = disabled;
			channelLabel.appendChild(channelSelect);
			panel.appendChild(channelLabel);

			const groupLabel = document.createElement("label");
			groupLabel.className = "modern-shortcut-target-group";
			const groupText = document.createElement("span");
			groupText.textContent = "Restrict to group";
			const groupInput = document.createElement("input");
			groupInput.type = "text";
			groupInput.value = target.group == null ? "" : String(target.group);
			groupInput.disabled = disabled;
			const commitGroup = function() {
				invokeModernShortcutTarget(rowIndex, {
					targetAction: "group",
					group: groupInput.value
				});
			};
			groupInput.addEventListener("change", commitGroup);
			groupInput.addEventListener("keydown", function(event) {
				if (event.key === "Enter") {
					event.preventDefault();
					commitGroup();
				}
			});
			groupLabel.appendChild(groupText);
			groupLabel.appendChild(groupInput);
			panel.appendChild(groupLabel);
		}

		const checks = document.createElement("div");
		checks.className = "modern-shortcut-target-checks";
		appendModernShortcutTargetToggle(checks, rowIndex, "Linked channels", "links", target.links, disabled);
		appendModernShortcutTargetToggle(checks, rowIndex, "Subchannels", "children", target.children, disabled);
		appendModernShortcutTargetToggle(checks, rowIndex, "Ignore positional audio", "forceCenter", target.forceCenter, disabled);
		panel.appendChild(checks);

		container.appendChild(panel);
	}

	function appendModernShortcutDataControl(container, field, row, disabled) {
		const type = String(row && row.dataType || "none");
		if (type === "toggle") {
			const select = modernShortcutEditorSelect(field.toggleOptions, row.dataValue, "number", function(value) {
				invokeModernDialogAction("keys.shortcutData", {
					index: Number(row.index) || 0,
					value: value
				});
			});
			select.disabled = disabled;
			container.appendChild(select);
			return;
		}
		if (type === "channel") {
			const select = modernShortcutEditorSelect(field.channelOptions, row.dataValue, "number", function(value) {
				invokeModernDialogAction("keys.shortcutData", {
					index: Number(row.index) || 0,
					value: value
				});
			});
			select.disabled = disabled;
			container.appendChild(select);
			return;
		}
		if (type === "target") {
			appendModernShortcutTargetControl(container, field, row || {}, disabled);
			return;
		}
		if (type === "text") {
			const input = document.createElement("input");
			input.type = "text";
			input.value = row.dataValue == null ? "" : String(row.dataValue);
			input.disabled = disabled;
			const commit = function() {
				invokeModernDialogAction("keys.shortcutData", {
					index: Number(row.index) || 0,
					value: input.value
				});
			};
			input.addEventListener("change", commit);
			input.addEventListener("keydown", function(event) {
				if (event.key === "Enter") {
					event.preventDefault();
					commit();
				}
			});
			container.appendChild(input);
			return;
		}
		container.appendChild(modernShortcutEditorChip(row && row.dataLabel || "No data", "muted"));
	}

	function appendModernShortcutEditor(container, field, errors) {
		const wrap = document.createElement("div");
		wrap.className = "modern-shortcut-editor";

		const rows = Array.isArray(field && field.rows) ? field.rows : [];
		const disabled = field && field.enabled === false;
		const assigned = rows.filter(function(row) {
			return row && row.assigned;
		}).length;

		const toolbar = document.createElement("div");
		toolbar.className = "modern-shortcut-toolbar";
		const summary = document.createElement("div");
		summary.className = "modern-shortcut-summary";
		const title = document.createElement("strong");
		title.textContent = String(assigned) + " assigned";
		const subtitle = document.createElement("span");
		subtitle.textContent = String(rows.length) + " total";
		summary.appendChild(title);
		summary.appendChild(subtitle);
		toolbar.appendChild(summary);

		const add = document.createElement("button");
		add.type = "button";
		add.className = "chip-button is-accent modern-shortcut-add";
		add.textContent = "Add shortcut";
		add.disabled = disabled;
		add.addEventListener("click", function(event) {
			event.preventDefault();
			invokeModernDialogAction("keys.addShortcut", {});
		});
		toolbar.appendChild(add);
		wrap.appendChild(toolbar);

		if (!rows.length) {
			const empty = document.createElement("div");
			empty.className = "modern-shortcut-empty";
			empty.textContent = "No shortcuts are configured yet.";
			wrap.appendChild(empty);
		}

		const list = document.createElement("div");
		list.className = "modern-shortcut-list";
		rows.forEach(function(row) {
			const rowIndex = Number(row && row.index) || 0;
			const item = document.createElement("article");
			item.className = "modern-shortcut-row"
				+ (row && row.capturing ? " is-capturing" : "")
				+ (row && row.assigned ? " is-assigned" : "");

			const main = document.createElement("div");
			main.className = "modern-shortcut-main";

			const action = document.createElement("label");
			action.className = "modern-shortcut-cell is-action";
			const actionLabel = document.createElement("span");
			actionLabel.textContent = "Function";
			const actionSelect = modernShortcutEditorSelect(field.actionOptions, row && row.actionIndex, "number", function(value) {
				invokeModernDialogAction("keys.shortcutAction", {
					index: rowIndex,
					actionIndex: value
				});
			});
			actionSelect.disabled = disabled;
			action.appendChild(actionLabel);
			action.appendChild(actionSelect);
			main.appendChild(action);

			const data = document.createElement("label");
			data.className = "modern-shortcut-cell is-data";
			const dataLabel = document.createElement("span");
			dataLabel.textContent = "Data";
			data.appendChild(dataLabel);
			appendModernShortcutDataControl(data, field, row || {}, disabled || row && row.dataEditable === false);
			main.appendChild(data);

			const input = document.createElement("div");
			input.className = "modern-shortcut-cell is-input";
			const inputLabel = document.createElement("span");
			inputLabel.textContent = "Shortcut";
			input.appendChild(inputLabel);
			input.appendChild(modernShortcutEditorChip(row && row.capturing ? "Listening..." : row && row.inputLabel || "Not assigned",
				row && row.capturing ? "capture" : (row && row.assigned ? "assigned" : "muted")));
			main.appendChild(input);
			item.appendChild(main);

			const controls = document.createElement("div");
			controls.className = "modern-shortcut-controls";
			if (field && field.canSuppress) {
				const suppress = document.createElement("label");
				suppress.className = "modern-shortcut-suppress";
				const checkbox = document.createElement("input");
				checkbox.type = "checkbox";
				checkbox.checked = !!(row && row.suppress);
				checkbox.disabled = disabled;
				checkbox.addEventListener("change", function() {
					invokeModernDialogAction("keys.shortcutSuppress", {
						index: rowIndex,
						suppress: checkbox.checked
					});
				});
				suppress.appendChild(checkbox);
				suppress.appendChild(document.createTextNode("Suppress"));
				controls.appendChild(suppress);
			}

			const record = document.createElement("button");
			record.type = "button";
			record.className = "chip-button modern-shortcut-record" + (row && row.capturing ? " is-warning" : "");
			record.textContent = row && row.capturing ? "Cancel" : "Record";
			record.disabled = disabled || !(field && field.canCapture);
			record.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernDialogAction(row && row.capturing ? "keys.cancelShortcutCapture" : "keys.beginShortcutCapture", {
					index: rowIndex
				});
			});
			controls.appendChild(record);

			const clear = document.createElement("button");
			clear.type = "button";
			clear.className = "chip-button modern-shortcut-clear";
			clear.textContent = "Clear";
			clear.disabled = disabled || !(row && (row.assigned || row.capturing));
			clear.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernDialogAction("keys.clearShortcut", { index: rowIndex });
			});
			controls.appendChild(clear);

			const remove = document.createElement("button");
			remove.type = "button";
			remove.className = "chip-button modern-shortcut-remove is-danger";
			remove.textContent = "Remove";
			remove.disabled = disabled;
			remove.addEventListener("click", function(event) {
				event.preventDefault();
				invokeModernDialogAction("keys.removeShortcut", { index: rowIndex });
			});
			controls.appendChild(remove);
			item.appendChild(controls);
			list.appendChild(item);
		});
		wrap.appendChild(list);

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
		if (type === "shortcutEditor") {
			appendModernShortcutEditor(container, field, errors);
			return;
		}

		const row = document.createElement("label");
		const fieldClass = field && field.id
			? " field-id-" + String(field.id).replace(/[^a-z0-9_-]/gi, "-")
			: "";
		const presentationClass = field && field.presentation
			? " is-" + String(field.presentation).replace(/[^a-z0-9_-]/gi, "-")
			: "";
		row.className = "modern-dialog-field is-" + type + fieldClass
			+ presentationClass + (modernDialogFieldIsAdvanced(field) ? " is-advanced" : "");
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
		} else if (type === "select" && appendModernDialogChoiceControl(row, field)) {
			input = null;
		} else if (type === "select") {
			input = document.createElement("select");
			const selectedValue = field.value == null ? "" : String(field.value);
			let hasSelectedValue = false;
			let hasOptionHint = false;
			let selectedOptionIndex = -1;
			(field.options || []).forEach(function(option, optionIndex) {
				const item = document.createElement("option");
				item.value = String(option.value);
				item.textContent = option.label || String(option.value);
				item.disabled = option.enabled === false;
				if (item.value === selectedValue) {
					hasSelectedValue = true;
					selectedOptionIndex = optionIndex;
					item.selected = true;
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
			if (selectedOptionIndex >= 0 && input.value !== selectedValue) {
				input.selectedIndex = selectedOptionIndex;
			}
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
			meter.dataset.staticMeter = field.staticMeter ? "true" : "false";
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
				meter.setAttribute("aria-label", fieldTooltip);
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
			updateVoiceMeterElement(meter, voiceMeterInitialPayload(field));
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
		} else if (type === "imagePicker") {
			const picker = document.createElement("div");
			picker.className = "modern-dialog-image-picker";
			input = document.createElement("input");
			input.type = "hidden";
			input.value = field.value == null ? "" : String(field.value);

			const preview = document.createElement("div");
			preview.className = "modern-dialog-image-preview";
			const syncPreview = function() {
				const value = String(input.value || "");
				if (value) {
					preview.classList.add("has-image");
					preview.style.backgroundImage = "url(\"" + value.replace(/"/g, "%22") + "\")";
					preview.textContent = "";
				} else {
					preview.classList.remove("has-image");
					preview.style.backgroundImage = "";
					preview.textContent = initialsFor(field.label || "Server");
				}
			};

			const fileInput = document.createElement("input");
			fileInput.type = "file";
			fileInput.className = "modern-dialog-image-file";
			fileInput.accept = field.accept || serverIdentityImageAccept;

			const controls = document.createElement("div");
			controls.className = "modern-dialog-image-controls";
			const choose = document.createElement("button");
			choose.type = "button";
			choose.className = "chip-button";
			choose.textContent = field.chooseLabel || "Choose image";
			choose.disabled = field.enabled === false;
			choose.addEventListener("click", function(event) {
				event.preventDefault();
				fileInput.click();
			});
			const remove = document.createElement("button");
			remove.type = "button";
			remove.className = "chip-button";
			remove.textContent = field.removeLabel || "Remove";
			remove.disabled = field.enabled === false;
			remove.addEventListener("click", function(event) {
				event.preventDefault();
				input.value = "";
				fileInput.value = "";
				syncPreview();
				updateModernDialogField(field, input);
				rememberModernDialogFieldValue(field.id, input.value);
			});
			fileInput.addEventListener("change", function() {
				const file = fileInput.files && fileInput.files[0];
				if (!file) {
					return;
				}
				if (!serverIdentityFileLooksLikeImage(file)) {
					showToast({ kind: "error", title: "Server image", message: "Choose an image file." });
					return;
				}
				if (file.size > serverIdentityImageMaxFileBytes) {
					showToast({ kind: "error", title: "Server image", message: "Choose an image smaller than 4 MB." });
					return;
				}
				readFileAsDataUrl(file).then(function(dataUrl) {
					input.value = normalizedServerIdentityImageDataUrl(file, dataUrl);
					syncPreview();
					updateModernDialogField(field, input);
					rememberModernDialogFieldValue(field.id, input.value);
				}).catch(function(error) {
					console.warn("Unable to read server image:", error);
					showToast({ kind: "error", title: "Server image", message: "Could not read that image." });
				});
			});
			controls.appendChild(choose);
			controls.appendChild(remove);
			picker.appendChild(preview);
			picker.appendChild(controls);
			picker.appendChild(fileInput);
			picker.appendChild(input);
			syncPreview();
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

		if (input && type !== "range" && type !== "pathPicker" && type !== "imagePicker") {
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
			if (modernDialogFieldUpdateShouldDebounce(field)) {
				input.addEventListener("change", function() {
					flushModernDialogFieldUpdate(String(field.id || ""));
				});
				input.addEventListener("blur", function() {
					flushModernDialogFieldUpdate(String(field.id || ""));
				});
			}
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

	function appendModernDialogSections(container, sections, errors, dialog) {
		const showAdvanced = modernDialogAdvancedVisible(dialog);
		(sections || []).forEach(function(section) {
			if (section && section.advanced && !showAdvanced) {
				return;
			}

			const fields = (section && section.fields || []).filter(function(field) {
				return modernDialogFieldShouldRender(field, showAdvanced, dialog);
			});
			if (!fields.length) {
				return;
			}

			const sectionElement = document.createElement("section");
			sectionElement.className = "modern-dialog-section"
				+ (section && section.advanced ? " is-advanced" : "")
				+ (section && section.presentation ? " is-" + section.presentation : "");
			if (section && section.title) {
				const title = document.createElement("h3");
				title.className = "modern-dialog-section-title";
				title.textContent = section.title;
				sectionElement.appendChild(title);
			}
			if (section && section.subtitle) {
				const subtitle = document.createElement("p");
				subtitle.className = "modern-dialog-section-subtitle";
				subtitle.textContent = section.subtitle;
				sectionElement.appendChild(subtitle);
			}
			fields.forEach(function(field) {
				appendModernDialogField(sectionElement, field, errors);
			});
			container.appendChild(sectionElement);
		});
	}

	function appendModernDialogFavorites(container, dialog) {
		if (!dialog || !Array.isArray(dialog.favorites)) {
			return;
		}

		const section = document.createElement("section");
		section.className = "modern-dialog-section modern-dialog-favorites";
		const header = document.createElement("div");
		header.className = "modern-dialog-section-header";
		const title = document.createElement("h3");
		title.className = "modern-dialog-section-title";
		title.textContent = "Saved servers";
		header.appendChild(title);
		const addButton = document.createElement("button");
		addButton.type = "button";
		addButton.className = "chip-button modern-dialog-favorites-add";
		addButton.textContent = "Add server";
		addButton.addEventListener("click", function() {
			invokeModernDialogAction("newFavorite", {});
		});
		header.appendChild(addButton);
		section.appendChild(header);

		const list = document.createElement("div");
		list.className = "modern-dialog-favorite-list";
		const compactStatLabel = function(value, prefix, emptyText) {
			const text = String(value || "").trim();
			if (!text || text === prefix + " -" || text === prefix + "-") {
				return emptyText;
			}
			return text.indexOf(prefix) === 0 ? text.slice(prefix.length).trim() : text;
		};
		dialog.favorites.forEach(function(favorite) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-favorite" + (favorite.selected ? " is-selected" : "");
			button.setAttribute("aria-pressed", favorite.selected ? "true" : "false");
			button.title = favorite.tooltip || favorite.label || favorite.host || "Server";
			const copy = document.createElement("span");
			copy.className = "modern-dialog-favorite-copy";
			const label = document.createElement("span");
			label.className = "modern-dialog-favorite-label";
			label.textContent = favorite.label || favorite.host || "Server";
			const subtitle = document.createElement("span");
			subtitle.className = "modern-dialog-favorite-subtitle";
			subtitle.textContent = favorite.subtitle || [favorite.host, favorite.port].filter(Boolean).join(":");
			copy.appendChild(label);
			copy.appendChild(subtitle);
			const stats = document.createElement("span");
			stats.className = "modern-dialog-favorite-stats";
			const users = document.createElement("span");
			users.className = "modern-dialog-favorite-stat";
			users.textContent = favorite.usersValue || compactStatLabel(favorite.usersLabel, "Users:", "-");
			users.title = favorite.usersLabel || "Users: -";
			const ping = document.createElement("span");
			ping.className = "modern-dialog-favorite-stat";
			ping.textContent = favorite.pingValue || compactStatLabel(favorite.pingLabel, "Ping:", "-");
			ping.title = favorite.pingLabel || "Ping: -";
			stats.appendChild(users);
			stats.appendChild(ping);
			button.appendChild(copy);
			button.appendChild(stats);
			button.addEventListener("click", function() {
				invokeModernDialogAction("selectFavorite", { index: Number(favorite.index) || 0 });
			});
			list.appendChild(button);
		});
		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "modern-dialog-favorite-empty";
			const emptyTitle = document.createElement("p");
			emptyTitle.className = "modern-dialog-favorite-empty-title";
			emptyTitle.textContent = "No saved servers";
			empty.appendChild(emptyTitle);
			list.appendChild(empty);
		}
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

	function stonksUserIdValue(value) {
		const userId = Number(value);
		return Number.isFinite(userId) && userId >= 0 ? userId : null;
	}

	function stonksSelectedUserId(stonks) {
		if (stonks && Object.prototype.hasOwnProperty.call(stonks, "selectedUserId")) {
			const selectedUserId = stonksUserIdValue(stonks.selectedUserId);
			if (selectedUserId !== null) {
				return selectedUserId;
			}
		}
		const selfUserId = stonksUserIdValue(stonks && stonks.selfUserId);
		return selfUserId !== null ? selfUserId : 0;
	}

	function stonksSelectedUserName(stonks) {
		const selectedId = stonksSelectedUserId(stonks);
		const selfId = stonksUserIdValue(stonks && stonks.selfUserId);
		if (selfId !== null && selectedId === selfId) {
			return "You";
		}
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
		const selfId = stonksUserIdValue(stonks && stonks.selfUserId);
		return selectedId >= 0 && (!!(stonks && stonks.canAdmin) || (!!(stonks && stonks.registered) && selectedId === selfId));
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
			: [];
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

	function stonksDraftHasUserPositions() {
		return (stonksDraftPositions || []).some(function(position) {
			const normalized = stonksNormalizePosition(position, stonksDraftCurrency || "USD");
			return !!(normalized.symbol || stonksNumber(normalized.quantity) || stonksNumber(normalized.price)
				|| stonksNumber(normalized.marketValue) || normalized.displayName);
		});
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

	function stonksPositionPriceLabel(position) {
		const price = stonksNumber(position && position.price);
		if (!(price > 0)) {
			return "";
		}
		return formatStonksMoney(price, position && position.currency || "USD") + "/share";
	}

	function stonksPositionActionText(action, symbol, position) {
		const priceLabel = stonksPositionPriceLabel(position);
		return action + " " + symbol + (priceLabel ? " at " + priceLabel : "");
	}

	function stonksSnapshotChanges(snapshot, previousSnapshot) {
		const currentPositions = stonksPositionMap(snapshot);
		const previousPositions = stonksPositionMap(previousSnapshot);
		const changes = [];

		if (!currentPositions.size && previousPositions.size) {
			return ["Cleared portfolio"];
		}
		if (currentPositions.size && !previousPositions.size) {
			currentPositions.forEach(function(position, symbol) {
				changes.push(stonksPositionActionText("Entered", symbol, position));
			});
			return changes;
		}

		currentPositions.forEach(function(position, symbol) {
			const previous = previousPositions.get(symbol);
			if (!previous) {
				changes.push(stonksPositionActionText("Entered", symbol, position));
				return;
			}
			if (stonksValueChanged(previous.quantity, position.quantity)) {
				changes.push(stonksPositionActionText(stonksNumber(position.quantity) > stonksNumber(previous.quantity)
					? "Added more"
					: "Removed some", symbol, position));
			} else if (stonksValueChanged(previous.price, position.price)) {
				changes.push(stonksPositionActionText("Updated", symbol, position));
			}
		});
		previousPositions.forEach(function(position, symbol) {
			if (!currentPositions.has(symbol)) {
				changes.push("Exited " + symbol);
			}
		});

		if (!changes.length) {
			changes.push(previousSnapshot ? "Portfolio details updated" : "Initial portfolio update");
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

	function stonksInputNumber(value, maxFractionDigits) {
		const number = Number(value);
		if (!Number.isFinite(number)) {
			return "";
		}
		const digits = Number.isFinite(Number(maxFractionDigits)) ? Number(maxFractionDigits) : 2;
		return Number(number.toFixed(digits)).toString();
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
		const positions = Array.isArray(draft.positions) ? draft.positions : [];
		if (draft.pristine && !positions.length) {
			summary.innerHTML = "";
			summary.className = "stonks-validation is-idle";
			const title = document.createElement("strong");
			title.textContent = "Add positions to save";
			summary.appendChild(title);
			if (total) {
				total.textContent = formatStonksMoney(0, draft.currency);
			}
			if (submit) {
				submit.disabled = true;
			}
			return { errors: [], warnings: [] };
		}

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
		if (handleStonksPopularQuoteLookupResult(result)) {
			return;
		}

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
		if ((stonks.error || stonks.loading) && stonks.status) {
			const status = document.createElement("div");
			status.className = "stonks-status";
			status.textContent = stonks.status;
			parent.appendChild(status);
		}
	}

	function clearStonksPendingConfirm() {
		stonksPendingConfirm = null;
		stonksPendingConfirmFocus = false;
	}

	function requestStonksConfirm(options) {
		const opts = options || {};
		stonksPendingConfirm = {
			title: String(opts.title || "Confirm action"),
			message: String(opts.message || "Continue?"),
			confirmLabel: String(opts.confirmLabel || "Confirm"),
			cancelLabel: String(opts.cancelLabel || "Cancel"),
			tone: String(opts.tone || "danger"),
			onConfirm: typeof opts.onConfirm === "function" ? opts.onConfirm : null
		};
		stonksPendingConfirmFocus = true;
		renderModernDialog();
	}

	function appendStonksPendingConfirm(parent) {
		const pending = stonksPendingConfirm;
		if (!pending) {
			return;
		}

		const overlay = document.createElement("div");
		overlay.className = "stonks-confirm-overlay";
		overlay.addEventListener("click", function(event) {
			if (event.target === overlay) {
				clearStonksPendingConfirm();
				renderModernDialog();
			}
		});

		const panel = document.createElement("section");
		panel.className = "stonks-confirm-panel" + (pending.tone ? " is-" + pending.tone : "");
		panel.setAttribute("role", "alertdialog");
		panel.setAttribute("aria-modal", "true");
		panel.setAttribute("aria-labelledby", "stonks-confirm-title");
		panel.setAttribute("aria-describedby", "stonks-confirm-message");

		const header = document.createElement("div");
		header.className = "stonks-confirm-header";
		const icon = document.createElement("span");
		icon.className = "stonks-confirm-icon";
		icon.textContent = "!";
		icon.setAttribute("aria-hidden", "true");
		const copy = document.createElement("div");
		copy.className = "stonks-confirm-copy";
		const title = document.createElement("h3");
		title.id = "stonks-confirm-title";
		title.textContent = pending.title;
		const message = document.createElement("p");
		message.id = "stonks-confirm-message";
		message.textContent = pending.message;
		copy.append(title, message);
		header.append(icon, copy);

		const actions = document.createElement("div");
		actions.className = "stonks-confirm-actions";
		const cancel = document.createElement("button");
		cancel.type = "button";
		cancel.className = "chip-button";
		cancel.textContent = pending.cancelLabel;
		cancel.dataset.stonksConfirmCancel = "true";
		cancel.addEventListener("click", function() {
			clearStonksPendingConfirm();
			renderModernDialog();
		});
		const confirm = document.createElement("button");
		confirm.type = "button";
		confirm.className = "chip-button is-" + (pending.tone || "danger");
		confirm.textContent = pending.confirmLabel;
		confirm.addEventListener("click", function() {
			const onConfirm = pending.onConfirm;
			clearStonksPendingConfirm();
			renderModernDialog();
			if (onConfirm) {
				onConfirm();
			}
		});
		actions.append(cancel, confirm);

		panel.append(header, actions);
		overlay.appendChild(panel);
		parent.appendChild(overlay);

		if (stonksPendingConfirmFocus) {
			stonksPendingConfirmFocus = false;
			window.requestAnimationFrame(function() {
				if (document.contains(cancel)) {
					cancel.focus({ preventScroll: true });
				}
			});
		}
	}

	function appendStonksTabs(parent, stonks) {
		const tabs = [["overview", "Overview"], ["ledger", "Portfolio"], ["leaderboard", "Leaderboard"], ["following", "Following"], ["audit", "Audit"]];
		if (stonks.canAdmin) {
			tabs.push(["admin", "Admin"]);
		}
		if (!tabs.some(function(tab) { return tab[0] === stonksActiveTab; })) {
			stonksActiveTab = "ledger";
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
		 ["Last update", latest ? formatStonksTime(latest.createdAt) : "-"],
		 ["Owner", stonksSelectedUserName(stonks)],
		 ["PnL rows", String((stonks.leaderboard || []).length)]].forEach(function(item) {
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
			: "No portfolio saves";
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
		["Symbol", "Qty", "Price", "Value", "Currency", "Source", ""].forEach(function(label) {
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
			row.appendChild(stonksInput(stonksInputNumber(position.quantity, 6), "quantity", "number"));
			row.appendChild(stonksInput(stonksInputNumber(position.price, 4), "price", "number"));
			row.appendChild(stonksInput(stonksInputNumber(position.marketValue, 2), "marketValue", "number"));
			row.appendChild(stonksInput(position.currency || stonksDraftCurrency, "currency"));
			const source = document.createElement("div");
			source.className = "stonks-source";
			const sourceLabel = document.createElement(position.quoteSourceUrl ? "a" : "span");
			sourceLabel.textContent = stonksProviderLabel(position.providerId);
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
			const displayName = stonksInput(position.displayName, "displayName");
			displayName.type = "hidden";
			row.appendChild(displayName);
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
		if (!stonksDraftPositions.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-position-empty";
			empty.innerHTML = "<strong></strong><span></span>";
			empty.querySelector("strong").textContent = "No positions yet";
			empty.querySelector("span").textContent = canEdit
				? "Search for a ticker or add a manual row to start the portfolio."
				: "This portfolio has no visible positions.";
			table.appendChild(empty);
		}
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
			? "Save portfolio for " + ownerName
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
			requestStonksConfirm({
				title: "Clear portfolio",
				message: "Clear portfolio for " + ownerName + "?",
				confirmLabel: "Clear portfolio",
				onConfirm: function() {
					invokeModernDialogAction("clearPortfolio", {
						userId: ownerUserId,
						currency: stonksDraftCurrency || latest && latest.currency || "USD",
						note: "Portfolio cleared"
					});
				}
			});
		});
		actions.append(add, total, clear, submit);
		editor.appendChild(actions);
		const validationSummary = document.createElement("div");
		const draft = {
			positions: stonksDraftHasUserPositions() ? (stonksDraftPositions || []) : [],
			currency: stonksDraftCurrency,
			registered: canEdit,
			pristine: !stonksDraftHasUserPositions()
		};
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
		description.textContent = "Portfolio updates are kept here for your own review.";
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
			summaryText.textContent = formatStonksMoney(snapshot.totalValue, snapshot.currency) + " · updated " + formatStonksTime(snapshot.createdAt);
			summary.appendChild(summaryText);
			if (canEdit && snapshot.snapshotId) {
				const remove = document.createElement("button");
				remove.type = "button";
				remove.className = "chip-button is-danger stonks-delete-snapshot";
				remove.textContent = "Delete";
				remove.addEventListener("click", function(event) {
					event.preventDefault();
					event.stopPropagation();
					requestStonksConfirm({
						title: "Delete portfolio update",
						message: "Delete this portfolio update for " + ownerName + "?",
						confirmLabel: "Delete update",
						onConfirm: function() {
							invokeModernDialogAction("deleteSnapshot", {
								snapshotId: snapshot.snapshotId,
								userId: snapshot.userId || ownerUserId
							});
						}
					});
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
				empty.textContent = snapshot.positionsRedacted ? "Positions are private." : "No open positions.";
				details.appendChild(empty);
			}
			list.appendChild(details);
		});
		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No portfolio updates yet.";
			list.appendChild(empty);
		}
		parent.appendChild(list);
	}

	function appendStonksLeaderboard(parent, stonks) {
		const description = document.createElement("div");
		description.className = "stonks-leaderboard-description";
		description.textContent = stonks.leaderboardDescription
			|| "Leaderboard ranks only PnL for the selected period.";
		parent.appendChild(description);
		const list = document.createElement("div");
		list.className = "stonks-list";
		(stonks.leaderboard || []).forEach(function(row) {
			const item = document.createElement("div");
			item.className = "stonks-row";
			const main = document.createElement("div");
			main.innerHTML = "<strong></strong><span></span><small></small>";
			main.querySelector("strong").textContent = (row.insufficientHistory ? "" : row.rank + ". ") + row.userName;
			main.querySelector("span").textContent = "PnL " + (row.period || stonks.selectedPeriod || "30d");
			main.querySelector("small").textContent = row.insufficientHistory
				? "Insufficient history"
				: "";
			const percent = document.createElement("strong");
			percent.className = "stonks-return" + (row.insufficientHistory ? " is-muted" : (stonksNumber(row.returnPercent) >= 0 ? " is-positive" : " is-negative"));
			percent.textContent = row.insufficientHistory ? "Need PnL" : formatStonksPercent(row.returnPercent);
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
			empty.textContent = "No ranked PnL for this period yet.";
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
		const selectedUserId = stonksSelectedUserId(stonks);
		const adminUsers = [];
		const seenUserIds = {};
		(Array.isArray(stonks && stonks.users) ? stonks.users : []).forEach(function(user) {
			const userId = stonksUserIdValue(user && user.userId);
			if (userId === null || seenUserIds[userId]) {
				return;
			}
			seenUserIds[userId] = true;
			adminUsers.push({
				userId: userId,
				userName: String(user && user.userName || ("user " + userId)).trim()
			});
		});
		if (selectedUserId >= 0 && !seenUserIds[selectedUserId]) {
			adminUsers.push({
				userId: selectedUserId,
				userName: stonksSelectedUserName(stonks) || ("user " + selectedUserId)
			});
		}

		const manager = document.createElement("section");
		manager.className = "stonks-admin stonks-admin-manager";
		const userSelect = document.createElement("select");
		adminUsers.forEach(function(user) {
			const option = document.createElement("option");
			option.value = String(user.userId);
			option.textContent = user.userName || ("user " + user.userId);
			option.selected = user.userId === selectedUserId;
			userSelect.appendChild(option);
		});
		if (!userSelect.children.length) {
			const option = document.createElement("option");
			option.value = "-1";
			option.textContent = "No registered users";
			userSelect.appendChild(option);
		}
		const view = document.createElement("button");
		view.type = "button";
		view.className = "chip-button";
		view.textContent = "View portfolio";
		view.disabled = !userSelect.children.length || stonksUserIdValue(userSelect.value) === null;
		view.addEventListener("click", function() {
			stonksActiveTab = "ledger";
			invokeModernDialogAction("selectUser", { userId: Number(userSelect.value || 0) });
		});
		const audit = document.createElement("button");
		audit.type = "button";
		audit.className = "chip-button";
		audit.textContent = "View updates";
		audit.disabled = !userSelect.children.length || stonksUserIdValue(userSelect.value) === null;
		audit.addEventListener("click", function() {
			stonksActiveTab = "audit";
			invokeModernDialogAction("selectUser", { userId: Number(userSelect.value || 0) });
		});
		const clear = document.createElement("button");
		clear.type = "button";
		clear.className = "chip-button is-danger";
		clear.textContent = "Clear portfolio";
		clear.disabled = !userSelect.children.length || stonksUserIdValue(userSelect.value) === null;
		clear.addEventListener("click", function() {
			const targetName = userSelect.options[userSelect.selectedIndex]
				? userSelect.options[userSelect.selectedIndex].textContent
				: "selected user";
			requestStonksConfirm({
				title: "Clear portfolio",
				message: "Clear portfolio for " + targetName + "?",
				confirmLabel: "Clear portfolio",
				onConfirm: function() {
					invokeModernDialogAction("clearPortfolio", {
						userId: Number(userSelect.value || 0),
						currency: stonksLatestSnapshot(stonks) && stonksLatestSnapshot(stonks).currency || "USD",
						note: "Portfolio cleared by admin"
					});
				}
			});
		});
		manager.append(userSelect, view, audit, clear);
		parent.appendChild(manager);

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
		}
		refs.modernDialogBody.appendChild(panel);
		appendStonksPendingConfirm(refs.modernDialogBody);
	}

	function appendModernDialogTabs(container, dialog) {
		if (!Array.isArray(dialog.pages) || !dialog.pages.length) {
			return;
		}

		const settingsTabs = dialog && dialog.kind === "settings";
		const tabs = document.createElement("div");
		tabs.className = "modern-dialog-tabs" + (settingsTabs ? " modern-settings-rail" : "");
		dialog.pages.forEach(function(page) {
			const pageId = String(page.id || "");
			const tab = document.createElement("button");
			tab.type = "button";
			tab.className = "modern-dialog-tab"
				+ (page.selected ? " is-selected" : "")
				+ (pageId ? " page-id-" + pageId.replace(/[^a-z0-9_-]/gi, "-") : "");
			if (pageId) {
				tab.dataset.pageId = pageId;
			}
			if (settingsTabs) {
				tab.appendChild(createModernSettingsPageIcon(pageId));
				const label = document.createElement("span");
				label.className = "modern-dialog-tab-label";
				label.textContent = page.label || pageId || "Page";
				tab.appendChild(label);
			} else {
				tab.textContent = page.label || page.id || "Page";
			}
			tab.addEventListener("click", function() {
				invokeModernDialogAction("selectPage", { pageId: page.id || "" });
			});
			tabs.appendChild(tab);
		});
		container.appendChild(tabs);
	}

	function renderSettingsDialog(dialog) {
		const selectedPage = modernSettingsSelectedPage(dialog);
		const pageId = String(selectedPage.id || dialog.activePage || "");
		const meta = modernSettingsPageMeta(pageId);
		const showAdvanced = modernDialogAdvancedVisible(dialog);
		const hiddenAdvancedCount = showAdvanced ? 0 : modernDialogAdvancedContentCount(dialog);

		appendModernDialogTabs(refs.modernDialogBody, dialog);

		const content = document.createElement("div");
		content.className = "modern-settings-content";

		const header = document.createElement("header");
		header.className = "modern-settings-header";
		const copy = document.createElement("div");
		copy.className = "modern-settings-title-copy";
		const eyebrow = document.createElement("p");
		eyebrow.className = "modern-settings-eyebrow";
		eyebrow.textContent = meta.eyebrow;
		const title = document.createElement("h2");
		title.className = "modern-settings-title";
		title.textContent = meta.title || selectedPage.label || "Settings";
		copy.appendChild(eyebrow);
		copy.appendChild(title);
		header.appendChild(copy);

		const tools = document.createElement("div");
		tools.className = "modern-settings-tools";
		if (modernDialogHasAdvancedContent(dialog)) {
			const advanced = document.createElement("button");
			advanced.type = "button";
			advanced.className = "chip-button modern-settings-advanced-toggle" + (showAdvanced ? " is-active" : "");
			advanced.textContent = showAdvanced ? "Basic" : "Advanced";
			advanced.setAttribute("aria-pressed", showAdvanced ? "true" : "false");
			advanced.addEventListener("click", function() {
				setModernDialogAdvancedVisible(dialog, !showAdvanced);
				renderModernDialog();
			});
			tools.appendChild(advanced);
		}
		const closeButton = document.createElement("button");
		closeButton.type = "button";
		closeButton.className = "icon-button modern-settings-close";
		closeButton.title = "Close";
		closeButton.setAttribute("aria-label", "Close");
		closeButton.appendChild(createModernDialogLineIcon(modernSettingsIconPaths("close")));
		closeButton.addEventListener("click", closeModernDialog);
		tools.appendChild(closeButton);
		header.appendChild(tools);
		content.appendChild(header);

		const scroll = document.createElement("div");
		scroll.className = "modern-settings-scroll";
		appendModernDialogHighlights(scroll, dialog);
		appendModernDialogSections(scroll, dialog.sections || [], dialog.errors || {}, dialog);
		content.appendChild(scroll);

		const footer = document.createElement("footer");
		footer.className = "modern-settings-footer";
		const status = document.createElement("div");
		status.className = "modern-settings-status";
		if (hiddenAdvancedCount > 0 || meta.status) {
			const text = document.createElement("span");
			if (hiddenAdvancedCount > 0) {
				text.textContent = String(hiddenAdvancedCount) + " advanced option"
					+ (hiddenAdvancedCount === 1 ? "" : "s") + " hidden";
				const show = document.createElement("button");
				show.type = "button";
				show.className = "modern-settings-status-link";
				show.textContent = "Show advanced";
				show.addEventListener("click", function() {
					setModernDialogAdvancedVisible(dialog, true);
					renderModernDialog();
				});
				status.appendChild(text);
				status.appendChild(show);
			} else {
				text.textContent = meta.status;
				status.appendChild(text);
			}
		}
		footer.appendChild(status);

		const footerActions = document.createElement("div");
		footerActions.className = "modern-settings-footer-actions";
		const actions = Array.isArray(dialog.actions) && dialog.actions.length
			? dialog.actions
			: [
				{ id: "cancel", label: "Close", enabled: true },
				{ id: "ok", label: "Done", enabled: true }
			];
		actions.forEach(function(action) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button modern-settings-footer-action"
				+ (action.tone ? " is-" + action.tone : "")
				+ (dialog.primaryActionId === action.id ? " is-primary" : "");
			button.disabled = action.enabled === false;
			button.textContent = action.label || action.id || "Action";
			button.addEventListener("click", function() {
				invokeModernDialogAction(action.id || "", {});
			});
			footerActions.appendChild(button);
		});
		footer.appendChild(footerActions);
		content.appendChild(footer);
		refs.modernDialogBody.appendChild(content);
	}

	function modernDialogRenderKey(dialog) {
		const stonks = dialog && dialog.stonks || {};
		return [
			dialog && dialog.id || "",
			dialog && dialog.kind || "",
			dialog && dialog.title || "",
			dialog && dialog.subtitle || "",
			dialog && dialog.tone || "",
			stonksActiveTab || "",
			stonks.status || "",
			stonks.error || "",
			stonks.loading ? "loading" : "",
			stonks.supported === false ? "unsupported" : "",
			stonks.enabled === false ? "disabled" : "",
			stonks.registered === false ? "anonymous" : "",
			stonks.selectedUserId || "",
			Array.isArray(stonks.snapshots) ? stonks.snapshots.length : "",
			Array.isArray(stonks.leaderboard) ? stonks.leaderboard.length : "",
			Array.isArray(stonks.personalTickers) ? stonks.personalTickers.length : ""
		].join("|");
	}

	function renderModernDialog() {
		const dialog = modernDialogState || {};
		const open = !!dialog.open;
		if (!refs.modernDialogLayer) {
			return;
		}
		if (open && dialog.uiTweaks) {
			applyModernUiTweaks(dialog.uiTweaks);
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
			modernDialogLastRenderKey = "";
			syncAudioInputMeterTimer();
			applyModernUiTweaks(((getSnapshot().app || {}).uiTweaks) || {});
			if (closing) {
				restoreModernDialogReturnFocus();
			}
			return;
		}

		const renderKey = modernDialogRenderKey(dialog);
		const shouldResetBodyScroll = opening || renderKey !== modernDialogLastRenderKey;
		refs.modernDialog.className = "modern-dialog" + (dialog.tone ? " is-" + dialog.tone : "")
			+ (dialog.kind ? " is-" + dialog.kind : "")
			+ (dialog.id ? " dialog-id-" + String(dialog.id).replace(/[^a-z0-9_-]/gi, "-") : "");
		refs.modernDialogEyebrow.textContent = dialog.eyebrow || (dialog.kind === "settings" ? "Settings" : "Mumble");
		refs.modernDialogTitle.textContent = dialog.title || "Dialog";
		refs.modernDialogSubtitle.textContent = dialog.subtitle || "";
		refs.modernDialogBody.innerHTML = "";
		refs.modernDialogActions.innerHTML = "";

		if (dialog.kind === "stonks") {
			renderStonksDialog(dialog);
			syncAudioInputMeterTimer();
			if (shouldResetBodyScroll) {
				refs.modernDialogBody.scrollTop = 0;
			}
			modernDialogLastRenderKey = renderKey;
			modernDialogRenderedOpen = true;
			if (focusState && restoreModernDialogFocus(focusState)) {
				return;
			}
			if (opening || !activeInDialog) {
				focusFirstModernDialogControl();
			}
			return;
		}

		if (dialog.kind === "settings") {
			renderSettingsDialog(dialog);
		} else {
			appendModernDialogTabs(refs.modernDialogBody, dialog);
			appendModernDialogHighlights(refs.modernDialogBody, dialog);
			appendModernDialogAdvancedToggle(refs.modernDialogBody, dialog);
			appendModernDialogFavorites(refs.modernDialogBody, dialog);

			const errors = dialog.errors || {};
			appendModernDialogSections(refs.modernDialogBody, dialog.sections || [], errors, dialog);
		}

		if (dialog.kind !== "settings") {
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
		}
		enhanceModernDialogSelects(refs.modernDialogBody);
		syncAudioInputMeterTimer();
		if (shouldResetBodyScroll) {
			refs.modernDialogBody.scrollTop = 0;
		}
		modernDialogLastRenderKey = renderKey;
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
		eyebrow.textContent = app.serverEyebrow || app.connectionLabel || "Mumble";

		const title = document.createElement("p");
		title.className = "app-menu-title";
		title.textContent = app.serverTitle || "Mumble";

		const subtitleParts = [];
		if (app.selfName) {
			subtitleParts.push(app.selfName);
		}
		if (app.selfStatusLabel) {
			subtitleParts.push(app.selfStatusLabel);
		}

		header.appendChild(eyebrow);
		header.appendChild(title);
		if (subtitleParts.length) {
			const subtitle = document.createElement("p");
			subtitle.className = "app-menu-subtitle";
			subtitle.textContent = subtitleParts.join(" · ");
			header.appendChild(subtitle);
		}
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
		hideDirectMessageTray();
		openMenuId = null;
		openMenuPinned = false;
		renderMenus(resolvedAppMenus(snapshot.app || {}));
		renderAppMenu(snapshot);
	}

	function hideSelfMenu() {
		const wasOpen = selfMenuOpen;
		selfMenuOpen = false;
		refs.selfMenu.classList.add("hidden");
		refs.selfMenu.setAttribute("aria-hidden", "true");
		refs.selfMenu.innerHTML = "";
		refs.selfCard.setAttribute("aria-expanded", "false");
		if (wasOpen) {
			closeNativeContextMenu();
		}
	}

	function nativeSelfMenuOpen() {
		return !!activeNativeContextMenuToken && activeNativeContextMenuKind === "self";
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

		const icon = document.createElement("span");
		icon.className = "self-menu-icon";
		icon.setAttribute("aria-hidden", "true");
		icon.innerHTML = themedActionPanelIconSvg(actionPanelIconName(item));

			const label = document.createElement("span");
			label.className = "self-menu-label";
			label.textContent = item.label || "Action";

			button.appendChild(icon);
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

	function selfMenuContextItems(snapshot, includeHeader) {
		const app = (snapshot && snapshot.app) || {};
		const menuState = app.selfMenu || {};
		const handlers = {
			invokeAction: function(actionId) {
				notifyBridge("invokeAppAction", actionId);
			}
		};
		const items = [];
		if (includeHeader) {
			const name = menuState.name || app.selfName || "You";
			items.push({
				kind: "profileHeader",
				label: name,
				statusLabel: menuState.statusLabel || app.selfStatusLabel || "Offline",
				statusTone: menuState.statusTone || app.selfStatusTone || "",
				avatarText: initialsFor(name),
				avatarUrl: app.selfAvatarUrl || ""
			});
		}
		const presenceItems = actionItemsFromActionStates(menuState.presence, handlers);
		const actionItems = actionItemsFromActionStates(menuState.actions, handlers);
		if (presenceItems.length) {
			items.push.apply(items, presenceItems);
		}
		if (presenceItems.length && actionItems.length) {
			items.push({ separator: true });
		}
		if (actionItems.length) {
			items.push.apply(items, actionItems);
		}
		return normalizedActionPanelItems(items, { hideDisabled: true });
	}

	function toggleSelfMenu(forceOpen) {
		const nextOpen = typeof forceOpen === "boolean" ? forceOpen : !selfMenuOpen;
		if (!nextOpen) {
			closeNativeContextMenu();
			hideSelfMenu();
			return;
		}

		hideAppMenu();
		hideContextMenu();
		hideDirectMessageTray();
		const bounds = refs.selfCard.getBoundingClientRect();
		const opened = showContextMenu(selfMenuContextItems(getSnapshot(), true),
			bounds.right,
			bounds.top,
			{
				horizontal: "left-of-anchor",
				anchorBounds: { left: bounds.left, right: bounds.right },
				menuKind: "self"
			});
		selfMenuOpen = !!opened;
		refs.selfCard.setAttribute("aria-expanded", selfMenuOpen ? "true" : "false");
	}

	function syncComposerHeight() {
		refs.composerInput.style.height = "0px";
		refs.composerInput.style.height = Math.min(refs.composerInput.scrollHeight, 160) + "px";
		scheduleFooterAlignmentSync();
	}

	function composerRaisesFooter() {
		if (refs.composerShell && refs.composerShell.classList.contains("has-reply")) {
			return true;
		}
		if (!refs.composerInput) {
			return false;
		}

		const draft = String(refs.composerInput.value || "");
		if (draft.indexOf("\n") !== -1) {
			return true;
		}

		const inputRect = refs.composerInput.getBoundingClientRect();
		const inputStyle = window.getComputedStyle(refs.composerInput);
		const lineHeight = Number.parseFloat(inputStyle.lineHeight || "0");
		const verticalChrome =
			Number.parseFloat(inputStyle.paddingTop || "0")
			+ Number.parseFloat(inputStyle.paddingBottom || "0")
			+ Number.parseFloat(inputStyle.borderTopWidth || "0")
			+ Number.parseFloat(inputStyle.borderBottomWidth || "0");
		const singleLineHeight = lineHeight > 0 ? lineHeight + verticalChrome : 0;
		return singleLineHeight > 0 && inputRect.height > singleLineHeight + 4;
	}

	function syncFooterAlignment() {
		footerAlignmentFrame = 0;
		if (!refs.appShell || !refs.composerForm || !refs.selfCard) {
			return;
		}

		const composerHeight = refs.composerForm.getBoundingClientRect().height;
		if (!composerHeight) {
			refs.appShell.style.removeProperty("--footer-row-height");
			refs.appShell.style.removeProperty("--self-card-row-height");
			return;
		}

		const dpr = window.devicePixelRatio || 1;
		const snappedHeight = Math.ceil(composerHeight * dpr) / dpr;
		refs.appShell.style.setProperty("--footer-row-height", snappedHeight + "px");
		if (!composerRaisesFooter()) {
			stableFooterRowHeight = snappedHeight;
		} else if (!stableFooterRowHeight) {
			stableFooterRowHeight = Math.ceil(refs.selfCard.getBoundingClientRect().height * dpr) / dpr;
		}
		if (stableFooterRowHeight) {
			refs.appShell.style.setProperty("--self-card-row-height", stableFooterRowHeight + "px");
		}
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
		refs.composerReplySnippet.textContent = normalizedReplyPreviewText(scope.replySnippet);
	}

	function applyModernUiTweaks(uiTweaks) {
		if (window.MumbleModernTheme && typeof window.MumbleModernTheme.apply === "function") {
			window.MumbleModernTheme.apply(uiTweaks || {});
		}
	}

	function syncAmbientState(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const visibleUiTweaks = visibleModernUiTweaks(snapshot);
		const toneSource = scope.label || app.serverTitle || "Mumble";
		const scopeHue = hueForLabel(toneSource, false);
		refs.appShell.style.setProperty("--scope-hue", String(scopeHue));
		refs.appShell.dataset.scopeKind = String(scope.kindLabel || "conversation").toLowerCase().replace(/\s+/g, "-");
		refs.appShell.classList.toggle("ticker-banner-always-scroll", !!visibleUiTweaks.tickerBannerAlwaysScroll);
		applyModernUiTweaks(visibleUiTweaks);
		scheduleStonksTickerScrollSync();
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

		syncMockupShellChrome(app);
		refs.serverEyebrow.textContent = app.serverEyebrow || scope.kindLabel || "Mumble";
		refs.scopeTitle.textContent = scope.label || app.serverTitle || "Mumble";
		refs.scopeDescription.textContent = scope.description || app.serverSubtitle || "Select a room to see shared history.";

		refs.layoutSwitchButton.disabled = app.canToggleLayout === false;
		refs.layoutSwitchButton.classList.toggle("hidden", app.canToggleLayout === false);
		refs.layoutSwitchButton.title = app.layoutSwitchLabel || "Switch layout";
		refs.layoutSwitchButton.setAttribute("aria-label", app.layoutSwitchLabel || "Switch layout");
		const stonks = app.stonks || {};
		const stonksAvailable = !!stonks.supported;
		refs.stonksButton.disabled = !stonksAvailable;
		refs.stonksButton.classList.toggle("is-disabled", !stonksAvailable);

		refs.connectButton.disabled = !app.canConnect;
		refs.disconnectButton.disabled = !appCanCancelConnection(app);
		refs.muteButton.classList.toggle("is-active", !!app.selfMuted);
		refs.deafButton.classList.toggle("is-active", !!app.selfDeafened);
		renderMenus(resolvedAppMenus(app));
		refs.settingsButton.setAttribute("aria-expanded", appMenuOpen ? "true" : "false");

		reconcilePendingVoiceJoin(snapshot);
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
		renderDirectMessages(snapshot);
		renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
		trackActiveRailTokenForReveal(activeRailToken());

		renderVoicePresenceStack(headerPresence);
		renderMeta(scope.meta || []);
		renderStonksChatHeader(snapshot);
		renderScreenShareHeader(scope, scope.screenShare || null);
		renderScreenShareCard(scope, scope.screenShare || null);
		renderNote(app, scope, snapshot.messages || []);
		renderMessages(snapshot, { resolvePendingScopeLoading: true });
		renderSelfCard(app);
		syncAmbientState(snapshot);
		if (appMenuOpen) {
			renderAppMenu(snapshot);
		}
		if (selfMenuOpen && !nativeSelfMenuOpen()) {
			renderSelfMenu(snapshot);
		}

		renderConnectionOrScopeBanner(app, scope);
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
		liveSnapshot = modernBridge ? (modernBridge.snapshot || {}) : (fallbackWalkthroughSnapshot() || {});
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
		renderNote(snapshot.app || {}, snapshot.activeScope || {}, snapshot.messages || []);
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
		refs.messageList.classList.remove("is-chat-loading", "is-chat-transitioning");
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
		syncMessageSearchState();

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
		const footerOnlyUpdate = messageCanPatchFooterOnly(messages[existingIndex], updated);
		messages[existingIndex] = updated;
		snapshot.messages = messages;
		if (activeMessageChunkRender) {
			queuePendingMessageUpdatePatch(Object.assign({ renderIndex: existingIndex }, updated), {
				footerOnly: footerOnlyUpdate
			});
			applyPendingMessageUpdatePatches(true);
			lastRenderedTailKey = latestTailMessageKey(snapshot.messages);
			return true;
		}

		if (!applyRenderedMessageUpdate(Object.assign({ renderIndex: existingIndex }, updated), {
			footerOnly: footerOnlyUpdate
		})) {
			renderMessages(snapshot, { resolvePendingScopeLoading: true });
		} else {
			syncMessageSearchState();
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
				renderActiveScopePatch(snapshot);
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
		refs.contextMenu.classList.remove("has-open-submenu");
		refs.contextMenu.setAttribute("aria-hidden", "true");
		refs.contextMenu.style.maxHeight = "";
		refs.contextMenu.innerHTML = "";
	}

	function copyPlainText(text) {
		const value = String(text || "");
		if (!value) {
			return Promise.resolve(false);
		}
		automationLastCopiedText = value;
		automationCopyCount += 1;

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

	function quotedMessageText(text) {
		return "> " + String(text || "").split(/\r?\n/).join("\n> ") + "\n";
	}

	function buildMessageContextItems(message, fallbackText) {
		const bodyText = String((message && (message.bodyText || message.plainText)) || fallbackText || "");
		const items = [];
		if (message && message.canReply) {
			items.push({
				label: "Reply",
				enabled: true,
				action: function() {
					notifyBridge("startReply", message.messageId);
					focusComposerInput();
				}
			});
		}
		if (message && message.canReact) {
			items.push({
				label: "Add reaction",
				enabled: true,
				action: function() {
					setOpenReactionPickerMessageId(message.messageId);
					pauseReactionPickerScrollClose();
				}
			});
		}
		items.push(
			{
				label: "Copy text",
				enabled: !!bodyText,
				action: function() {
					copyPlainText(bodyText);
				}
			},
			{
				label: "Quote",
				enabled: !!bodyText,
				action: function() {
					replaceComposerSelection(quotedMessageText(bodyText));
					if (refs.composerInput) {
						refs.composerInput.focus();
					}
				}
			}
		);
		if (message && message.canDelete) {
			items.push(
				{ separator: true },
				{
					label: "Delete message",
					enabled: true,
					tone: "danger",
					action: function() {
						requestDeleteMessage(message);
					}
				}
			);
		}
		return normalizedActionPanelItems(items, { hideDisabled: true });
	}

	function buildRoomContextMenuItems(snapshot, room, roomRow) {
		const scope = (snapshot && snapshot.activeScope) || {};
		const roomToken = (room && room.token) || (roomRow && roomRow.dataset.scopeToken) || "";
		const isVoiceRoom = roomRow && roomRow.dataset.roomType === "voice";
		const roomLabel = (room && room.label) || (roomRow && roomRow.dataset.roomLabel) || "Room";
		const items = [
			{
				kind: "label",
				label: roomLabel
			},
			{
				id: "openRoom",
				label: "Open chat",
				enabled: !!roomToken,
				action: function() {
					selectRoomScope(roomToken);
				}
			}
		];

		if (isVoiceRoom && !(room && actionStatesContainId(room.actions, "join"))) {
			items.push({
				id: "joinVoice",
				label: "Join room",
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
		if (roomToken && room && !actionStatesContainId(room.actions, "copyUrl")) {
			items.push({
				id: "copyUrl",
				label: "Copy invite link",
				enabled: true,
				action: function() {
					copyPlainText(roomLabel);
				}
			});
		}

		if (room && room.selected && scope.canMarkRead) {
			items.push({ separator: true });
			items.push({
				id: "markRead",
				label: "Mark as read",
				enabled: true,
				action: function() {
					notifyBridge("markRead");
				}
			});
		}

		return roomContextGroups(items);
	}

	function buildBackgroundContextMenuItems(snapshot) {
		const scope = (snapshot && snapshot.activeScope) || {};
		const items = [
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

		const appItems = appMenuContextItems(snapshot, ["server", "room", "configure"]);
		if (appItems.length) {
			items.push({ separator: true });
			items.push.apply(items, appItems);
		}

		return items;
	}

	function buildContextMenuItems(event) {
		const snapshot = getSnapshot();
		const scope = snapshot.activeScope || {};
		const roomRow = event.target.closest(".rail-row");
		const presenceActionButton = event.target.closest(".presence-action-button");
		const presenceRow = event.target.closest(".presence-row, .participant-context-target")
			|| (presenceActionButton && presenceActionButton.parentElement
				? presenceActionButton.parentElement.querySelector(".presence-row")
				: null);
		const bubble = event.target.closest(".message-bubble, .system-message");
		const composer = event.target.closest("#composer-input");
		const selfCard = event.target.closest("#self-card");
		const brandBadge = event.target.closest("#brand-badge");

		if (brandBadge) {
			const app = snapshot.app || {};
			const serverIdentity = app.serverIdentity || {};
			if (!serverIdentity.canEdit) {
				return [];
			}
			return [
				{
					id: "server.settings",
					label: "Server settings",
					enabled: !!app.canManageTextChannels,
					action: function() {
						notifyBridge("invokeAppAction", "server.settings");
					}
				}
			];
		}

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
			const message = bubble.dataset.messageId ? findMessageState(snapshot, bubble.dataset.messageId) : null;
			return buildMessageContextItems(message, bubble.dataset.bodyText);
		}

		if (selfCard) {
			return selfMenuContextItems(snapshot, true);
		}

		return buildBackgroundContextMenuItems(snapshot);
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

	function openContextSubmenuByLabel(label) {
		if (!refs.contextMenu || refs.contextMenu.classList.contains("hidden")) {
			return;
		}

		const targetLabel = String(label || "").trim().toLowerCase();
		if (!targetLabel) {
			return;
		}

		const triggers = refs.contextMenu.querySelectorAll(".context-menu-submenu-trigger");
		for (let index = 0; index < triggers.length; index += 1) {
			const trigger = triggers[index];
			const labelNode = trigger.querySelector(".context-menu-label");
			if (String(labelNode && labelNode.textContent || "").trim().toLowerCase() !== targetLabel) {
				continue;
			}
			trigger.click();
			break;
		}
	}

	/* MUMBLE_MODERN_AUTOMATION_BEGIN */
	function visibleMenuLabels(kind) {
		const variant = String(kind || "").trim().toLowerCase();
		let root = null;
		let selector = "";
		if (variant === "app") {
			root = refs.appMenu;
			selector = ".app-menu-section-heading, .menu-item-label";
		} else if (variant === "self") {
			root = refs.selfMenu;
			selector = ".self-menu-title, .self-menu-status, .self-menu-label";
		} else if (variant === "context" || variant === "chat" || variant === "background") {
			root = refs.contextMenu;
			selector = ".context-menu-label";
		}
		if (!root || root.classList.contains("hidden")) {
			return [];
		}

		return Array.prototype.slice.call(root.querySelectorAll(selector)).map(function(node) {
			return String(node.textContent || "").trim();
		}).filter(function(label) {
			return !!label;
		});
	}

	function automationActionState() {
		const input = refs.composerInput;
		return {
			composerText: input ? String(input.value || "") : "",
			activeElementId: document.activeElement && document.activeElement.id ? document.activeElement.id : "",
			openReactionPickerMessageId: normalizedReactionPickerMessageId(openReactionPickerMessageId),
			bridgeCalls: automationBridgeCalls.slice(-16),
			lastCopiedText: automationLastCopiedText,
			copyCount: automationCopyCount,
			lastMenuProbe: Object.assign({}, automationLastMenuProbe || {})
		};
	}

	function resetAutomationActionState(options) {
		const resetOptions = options || {};
		automationBridgeCalls = [];
		automationLastCopiedText = "";
		automationCopyCount = 0;
		automationLastMenuProbe = {};
		setOpenReactionPickerMessageId(null);
		if (resetOptions.clearComposer !== false && refs.composerInput) {
			refs.composerInput.value = "";
			syncComposerHeight();
			refs.composerInput.dispatchEvent(new Event("input", { bubbles: true }));
		}
		return automationActionState();
	}

	function automationModernDialogFieldElement(fieldId) {
		const targetId = String(fieldId || "");
		const root = refs.modernDialog || document.getElementById("modern-dialog") || document;
		if (!targetId || !root) {
			return null;
		}
		const candidates = root.querySelectorAll("[data-modern-dialog-field-id]");
		for (let index = 0; index < candidates.length; index += 1) {
			const candidate = candidates[index];
			if (String(candidate.dataset.modernDialogFieldId || "") === targetId) {
				return candidate;
			}
		}
		return null;
	}

	function automationModernDialogFieldState(fieldId) {
		const element = automationModernDialogFieldElement(fieldId);
		const root = refs.modernDialog || document.getElementById("modern-dialog") || document;
		const dialog = modernDialogState || {};
		const field = findModernDialogField(dialog, fieldId);
		const type = String(field && field.type || (element && element.type) || "");
		const value = element
			? (type === "checkbox" ? !!element.checked : String(element.value || ""))
			: "";
		const availableFieldIds = root
			? Array.prototype.slice.call(root.querySelectorAll("[data-modern-dialog-field-id]")).map(function(candidate) {
				return String(candidate.dataset.modernDialogFieldId || "");
			}).filter(function(id) {
				return !!id;
			})
			: [];
		return {
			dialogId: String(dialog.id || ""),
			fieldId: String(fieldId || ""),
			exists: !!element,
			active: !!(element && document.activeElement === element),
			type: type,
			value: value,
			availableFieldIds: availableFieldIds,
			stateValue: field ? modernDialogCloneFieldValue(field.value) : null
		};
	}

	function automationSetModernDialogFieldValue(fieldId, value, options) {
		const element = automationModernDialogFieldElement(fieldId);
		if (!element) {
			return automationModernDialogFieldState(fieldId);
		}
		const setOptions = options || {};
		const field = findModernDialogField(modernDialogState, fieldId);
		const type = String(field && field.type || element.type || "");
		if (setOptions.focus !== false && typeof element.focus === "function") {
			element.focus({ preventScroll: true });
		}
		if (type === "checkbox") {
			element.checked = !!value;
			element.dispatchEvent(new Event("change", { bubbles: true }));
		} else {
			element.value = value == null ? "" : String(value);
			if (typeof element.setSelectionRange === "function") {
				try {
					const end = element.value.length;
					element.setSelectionRange(end, end);
				} catch (error) {
					// Some input types expose selection APIs but reject range updates.
				}
			}
			element.dispatchEvent(new Event(type === "select" || type === "range" ? "change" : "input", {
				bubbles: true
			}));
		}
		return automationModernDialogFieldState(fieldId);
	}

	function contextMenuCanUseNativePopup(items) {
		return !(items || []).some(function(item) {
			const kind = actionPanelItemKind(item);
			if (kind === "slider") {
				return true;
			}
			if (kind === "submenu") {
				return !contextMenuCanUseNativePopup(item.items || []);
			}
			return false;
		});
	}

	function serializeNativeContextMenuItems(items, actionItems) {
		const serialized = [];
		(items || []).forEach(function(item) {
			const kind = actionPanelItemKind(item);
			if (!kind) {
				return;
			}
			if (kind === "separator") {
				serialized.push({ kind: "separator" });
				return;
			}
			if (kind === "label") {
				serialized.push({
					kind: "label",
					label: item.label || ""
				});
				return;
			}
			if (kind === "profileHeader") {
				serialized.push({
					kind: "profileHeader",
					label: item.label || "",
					statusLabel: item.statusLabel || "",
					statusTone: item.statusTone || "",
					avatarText: item.avatarText || "",
					avatarUrl: item.avatarUrl || ""
				});
				return;
			}
			if (kind === "submenu") {
				const childItems = serializeNativeContextMenuItems(
					normalizedActionPanelItems(item.items || [], { hideDisabled: true }),
					actionItems);
				if (!childItems.length) {
					return;
				}
				serialized.push({
					kind: "submenu",
					label: item.label || "More",
					enabled: item.enabled !== false,
					icon: actionPanelIconName(item),
					items: childItems
				});
				return;
			}

			const actionIndex = actionItems.length;
			actionItems.push(item);
			serialized.push({
				kind: "action",
				label: item.label || "Action",
				enabled: item.enabled !== false,
				checked: !!item.checked,
				tone: item.tone || "",
				icon: actionPanelIconName(item),
				actionIndex: actionIndex
			});
		});
		return serialized;
	}

	function tryShowNativeContextMenu(items, clientX, clientY, options) {
		if (window.mumbleDisableNativeContextPopups === true) {
			return false;
		}
		if (!modernBridge || typeof modernBridge.openNativeContextMenu !== "function"
			|| !contextMenuCanUseNativePopup(items)) {
			return false;
		}

		const actionItems = [];
		const serializedItems = serializeNativeContextMenuItems(items, actionItems);
		if (!serializedItems.length || !actionItems.length) {
			return false;
		}

		const token = "ctx-" + Date.now().toString(36) + "-" + (++nativeContextMenuCounter).toString(36);
		const menuKind = String(options && options.menuKind || "context");
		nativeContextMenuActions = {};
		nativeContextMenuKinds = {};
		nativeContextMenuActions[token] = actionItems;
		nativeContextMenuKinds[token] = menuKind;
		activeNativeContextMenuToken = token;
		activeNativeContextMenuKind = menuKind;
		const request = {
			token: token,
			x: Math.round(clientX || 0),
			y: Math.round(clientY || 0),
			items: serializedItems,
			uiTweaks: Object.assign({}, visibleModernUiTweaks(getSnapshot()), {
				theme: document.documentElement.dataset.theme || "dark",
				accent: document.documentElement.dataset.accent || "auto",
				density: document.body.dataset.density || "comfortable",
				userIcons: document.body.dataset.userIcons || "avatars",
				classicUserIcons: document.body.dataset.userIcons === "classic"
			})
		};
		if (options && options.openSubmenuLabel) {
			request.openSubmenuLabel = String(options.openSubmenuLabel);
		}
		if (!notifyBridge("openNativeContextMenu", request)) {
			delete nativeContextMenuActions[token];
			delete nativeContextMenuKinds[token];
			if (activeNativeContextMenuToken === token) {
				activeNativeContextMenuToken = "";
				activeNativeContextMenuKind = "";
			}
			return false;
		}
		return true;
	}

	function closeNativeContextMenu() {
		if (modernBridge && typeof modernBridge.closeNativeContextMenu === "function") {
			modernBridge.closeNativeContextMenu();
		}
		nativeContextMenuActions = {};
		nativeContextMenuKinds = {};
		activeNativeContextMenuToken = "";
		activeNativeContextMenuKind = "";
	}

	window.__mumbleModernInvokeNativeContextMenuAction = function(token, actionIndex) {
		const key = String(token || "");
		if (!key || key !== activeNativeContextMenuToken) {
			return false;
		}
		const actionItems = nativeContextMenuActions[key];
		const item = actionItems && actionItems[Number(actionIndex)];
		if (!item || item.enabled === false || typeof item.action !== "function") {
			return false;
		}
		item.action();
		return true;
	};

	window.__mumbleModernNativeContextMenuClosed = function(token) {
		const key = String(token || "");
		const isActive = !!key && key === activeNativeContextMenuToken;
		const kind = nativeContextMenuKinds[key] || (isActive ? activeNativeContextMenuKind : "");
		if (isActive) {
			activeNativeContextMenuToken = "";
			activeNativeContextMenuKind = "";
		}
		if (isActive && kind === "self") {
			selfMenuOpen = false;
			if (refs.selfMenu) {
				refs.selfMenu.classList.add("hidden");
				refs.selfMenu.setAttribute("aria-hidden", "true");
				refs.selfMenu.innerHTML = "";
			}
			if (refs.selfCard) {
				refs.selfCard.setAttribute("aria-expanded", "false");
			}
		}
		window.setTimeout(function() {
			delete nativeContextMenuActions[key];
			delete nativeContextMenuKinds[key];
		}, 250);
	};

	function showContextMenu(items, clientX, clientY, options) {
		hideAppMenu();
		hideSelfMenu();
		hideDirectMessageTray();
		const filteredItems = normalizedActionPanelItems(items, { hideDisabled: true });
		if (!filteredItems.length) {
			hideContextMenu();
			return false;
		}

		if (!(options && options.forceDom) && tryShowNativeContextMenu(filteredItems, clientX, clientY, options || {})) {
			hideContextMenu();
			return true;
		}

		closeNativeContextMenu();
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
		if (options && options.openSubmenuLabel) {
			openContextSubmenuByLabel(options.openSubmenuLabel);
		}
		fitContextMenuToViewport();
		return true;
	}

	function firstAutomationRoomForMenu(snapshot) {
		const voiceRooms = snapshot.voiceRooms || [];
		const textRooms = snapshot.textRooms || [];
		const rooms = voiceRooms.concat(textRooms).filter(Boolean);
		return rooms.find(function(room) {
			return Array.isArray(room.actions) && room.actions.length;
		}) || rooms.find(function(room) {
			return !!room.token;
		}) || null;
	}

	function automationTextRoomMenuFallback() {
		return {
			label: "Demo text room",
			token: "3:9001",
			selected: true,
			actions: [
				{ id: "textRoom.edit", label: "Edit text room...", enabled: true },
				{ id: "textRoom.acl", label: "Configure ACL...", enabled: true },
				{ id: "textRoom.setDefault", label: "Set as default", enabled: true },
				{ id: "textRoom.visibilitySource", label: "Go to visibility source", enabled: true },
				{ id: "textRoom.delete", label: "Delete text room...", enabled: true, tone: "danger" }
			]
		};
	}

	function firstAutomationTextRoomForMenu(snapshot) {
		const textRooms = (snapshot.textRooms || []).filter(Boolean);
		return textRooms.find(function(room) {
			return Array.isArray(room.actions) && room.actions.some(function(action) {
				return action && (action.id === "textRoom.edit" || action.id === "textRoom.delete");
			});
		}) || textRooms.find(function(room) {
			return Array.isArray(room.actions) && room.actions.length;
		}) || automationTextRoomMenuFallback();
	}

	function firstAutomationRealTextRoomForMenu(snapshot) {
		const textRooms = (snapshot.textRooms || []).filter(Boolean);
		return textRooms.find(function(room) {
			return Array.isArray(room.actions) && room.actions.some(function(action) {
				return action && (action.id === "textRoom.edit" || action.id === "textRoom.delete");
			});
		}) || null;
	}

	function firstAutomationParticipantForMenu(snapshot) {
		const candidates = [];
		(snapshot.participants || []).forEach(function(person) {
			if (person) {
				candidates.push(person);
			}
		});
		(snapshot.voiceRooms || []).forEach(function(room) {
			(room && room.participants || []).forEach(function(person) {
				if (person) {
					candidates.push(person);
				}
			});
		});
		const canUseNativeMenu = function(person) {
			return !!person && !person.isSelf
				&& contextMenuCanUseNativePopup(participantContextMenuItems(person, person.scopeToken || ""));
		};
		return candidates.find(function(person) {
			return canUseNativeMenu(person) && Array.isArray(person.actions) && person.actions.length;
		}) || candidates.find(function(person) {
			return canUseNativeMenu(person) && (person.canMessage || person.canJoin);
		}) || {
			label: "Demo User",
			session: 7,
			canMessage: true,
			canJoin: true,
			entryKind: "user",
			actions: [
				{ id: "userInfo", label: "User information...", enabled: true },
				{ id: "localNickname", label: "Local nickname...", enabled: true },
				{ id: "commentView", label: "View comment...", enabled: true },
				{ id: "grantChatHistory", label: "Grant history...", enabled: true },
				{ id: "kick", label: "Kick...", enabled: true, tone: "danger" },
				{ id: "ban", label: "Ban...", enabled: true, tone: "danger" }
			]
		};
	}

	function firstAutomationMessageForMenu(snapshot) {
		return (snapshot.messages || []).find(function(message) {
			return message && (message.canReply || message.canReact || message.canDelete);
		}) || (snapshot.messages || []).find(Boolean) || null;
	}

	function automationMessageContextItems(snapshot, message) {
		return buildMessageContextItems(message, message && (message.bodyText || message.plainText));
	}

	function openAutomationMenuProbe(kind) {
		const snapshot = getSnapshot();
		const variant = String(kind || "").trim().toLowerCase();
		automationLastMenuProbe = { variant: variant };
		hideContextMenu();
		hideAppMenu();
		hideSelfMenu();
		hideDirectMessageTray();
		closeTopMenu();

		if (variant === "app") {
			closeNativeContextMenu();
			toggleAppMenu(true);
			return true;
		}
		if (variant === "self") {
			closeNativeContextMenu();
			toggleSelfMenu(true);
			return true;
		}
		if (variant === "chat" || variant === "background" || variant === "chatbackground") {
			showContextMenu(buildBackgroundContextMenuItems(snapshot),
				48,
				Math.max(132, Math.floor(window.innerHeight * 0.28)),
				{ openSubmenuLabel: "Configure" });
			return true;
		}
		if (variant === "room") {
			const room = firstAutomationRoomForMenu(snapshot);
			if (!room) {
				return false;
			}
			const isVoiceRoom = (snapshot.voiceRooms || []).indexOf(room) >= 0;
			automationLastMenuProbe = {
				variant: "room",
				scopeToken: String(room.token || ""),
				label: String(room.label || ""),
				roomType: isVoiceRoom ? "voice" : "text",
				canJoin: room.canJoin !== false
			};
			const fakeRow = {
				dataset: {
					scopeToken: String(room.token || ""),
					roomType: isVoiceRoom ? "voice" : "text",
					canJoin: room.canJoin === false ? "false" : "true"
				}
			};
			showContextMenu(buildRoomContextMenuItems(snapshot, room, fakeRow),
				window.innerWidth - 48,
				190,
				{
					horizontal: "left-of-anchor",
					anchorBounds: { left: window.innerWidth - 64, right: window.innerWidth - 32 }
			});
			return true;
		}
		if (variant === "textroom" || variant === "textroomreal") {
			const room = variant === "textroomreal"
				? firstAutomationRealTextRoomForMenu(snapshot)
				: automationTextRoomMenuFallback();
			if (!room) {
				return false;
			}
			automationLastMenuProbe = {
				variant: variant === "textroomreal" ? "textRoomReal" : "textRoom",
				scopeToken: String(room.token || ""),
				label: String(room.label || ""),
				roomType: "text",
				canJoin: false
			};
			const fakeRow = {
				dataset: {
					scopeToken: String(room.token || ""),
					roomType: "text",
					canJoin: "false"
				}
			};
			showContextMenu(buildRoomContextMenuItems(snapshot, room, fakeRow),
				window.innerWidth - 48,
				218,
				{
					horizontal: "left-of-anchor",
					anchorBounds: { left: window.innerWidth - 64, right: window.innerWidth - 32 }
				});
			return true;
		}
		if (variant === "member") {
			const participant = firstAutomationParticipantForMenu(snapshot);
			if (!participant) {
				return false;
			}
			automationLastMenuProbe = {
				variant: "member",
				session: String(participant.session || participant.ownerSession || ""),
				label: String(participant.label || participant.name || "User"),
				entryKind: String(participant.entryKind || "user")
			};
			showContextMenu(participantContextMenuItems(participant, participant.scopeToken || ""),
				window.innerWidth - 48,
				245,
				{
					horizontal: "left-of-anchor",
					anchorBounds: { left: window.innerWidth - 64, right: window.innerWidth - 32 }
				});
			return true;
		}
		if (variant === "message") {
			const message = firstAutomationMessageForMenu(snapshot);
			if (!message) {
				return false;
			}
			automationLastMenuProbe = {
				variant: "message",
				messageId: String(message.messageId || ""),
				bodyText: String((message && (message.bodyText || message.plainText)) || "")
			};
			showContextMenu(automationMessageContextItems(snapshot, message),
				Math.max(360, Math.floor(window.innerWidth * 0.34)),
				Math.max(220, Math.floor(window.innerHeight * 0.52)));
			return true;
		}
		return false;
	}

	if (modernAutomationEnabled()) {
		window.__mumbleModernOpenMenuProbe = openAutomationMenuProbe;
		window.__mumbleModernVisibleMenuLabels = visibleMenuLabels;
		window.__mumbleModernAutomationActionState = automationActionState;
		window.__mumbleModernResetAutomationActionState = resetAutomationActionState;
		window.__mumbleModernDialogFieldState = automationModernDialogFieldState;
		window.__mumbleModernSetDialogFieldValue = automationSetModernDialogFieldValue;
	}
	/* MUMBLE_MODERN_AUTOMATION_END */

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
		if (refs.brandBadge) {
			refs.brandBadge.addEventListener("click", function(event) {
				const app = getSnapshot().app || {};
				if (!(app.serverIdentity && app.serverIdentity.canEdit)) {
					return;
				}
				event.preventDefault();
				event.stopPropagation();
				notifyBridge("invokeAppAction", "server.settings");
			});
			refs.brandBadge.addEventListener("keydown", function(event) {
				if (event.key !== "Enter" && event.key !== " ") {
					return;
				}
				const app = getSnapshot().app || {};
				if (!(app.serverIdentity && app.serverIdentity.canEdit)) {
					return;
				}
				event.preventDefault();
				event.stopPropagation();
				notifyBridge("invokeAppAction", "server.settings");
			});
		}
		refs.muteButton.addEventListener("click", function() { notifyBridge("toggleSelfMute"); });
		refs.deafButton.addEventListener("click", function() { notifyBridge("toggleSelfDeaf"); });
		if (refs.stonksLeaderboardHeader) {
			refs.stonksLeaderboardHeader.addEventListener("click", function() {
				const stonks = (getSnapshot().app || {}).stonks || {};
				if (stonks.supported && stonks.enabled !== false) {
					notifyBridge("openModernDialog", "stonks", {});
				}
			});
		}
		if (refs.msgSearchButton) {
			refs.msgSearchButton.addEventListener("click", function() {
				setMessageSearchOpen(!messageSearchOpen);
			});
		}
		if (refs.msgSearchInput) {
			refs.msgSearchInput.addEventListener("input", function() {
				messageSearchText = refs.msgSearchInput.value || "";
				messageSearchOpen = true;
				syncMessageSearchState();
			});
			refs.msgSearchInput.addEventListener("keydown", function(event) {
				if (event.key === "Escape") {
					event.preventDefault();
					setMessageSearchOpen(false);
					refs.msgSearchButton.focus({ preventScroll: true });
				}
			});
		}
		if (refs.msgSearchClose) {
			refs.msgSearchClose.addEventListener("click", function() {
				setMessageSearchOpen(false);
				refs.msgSearchButton.focus({ preventScroll: true });
			});
		}
		if (refs.motdToggle) {
			refs.motdToggle.addEventListener("click", function() {
				const snapshot = getSnapshot();
				const app = snapshot.app || {};
				const motdHtml = String(app.motdHtml || "").trim();
				const wasDismissed = !!motdHtml && motdMatchesDismissedSignature(motdHtml, motdDismissedSignature);
				const signature = motdContentSignature(motdHtml);
				if (wasDismissed || conversationMotdHiddenForHistory) {
					setMotdDismissedSignature(app, "");
					motdExpansionTouched = true;
					markMotdSeen(app, signature);
					renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
					return;
				}
				setMotdDismissedSignature(app, signature);
				renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
			});
		}
		if (refs.motdCollapse) {
			refs.motdCollapse.addEventListener("click", function() {
				setMotdExpanded(!noteExpanded);
			});
		}
		if (refs.motdDismiss) {
			refs.motdDismiss.addEventListener("click", function() {
				const snapshot = getSnapshot();
				const app = snapshot.app || {};
				setMotdDismissedSignature(app, motdContentSignature(app.motdHtml || ""));
				renderNote(app, snapshot.activeScope || {}, snapshot.messages || []);
			});
		}
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
			if (event.target.closest("#self-card-settings-button, .self-control")) {
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
		refs.modernDialogBackdrop.addEventListener("click", function() {
			if (!modernDialogState || !modernDialogState.preventBackdropClose) {
				closeModernDialog();
			}
		});
		refs.noteToggleButton.addEventListener("click", function() {
			setMotdExpanded(!noteExpanded);
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
			const nextCollapsed = !railCollapsed;
			railOpenedByUser = compactViewport && !nextCollapsed;
			setRailCollapsed(nextCollapsed);
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
			if (!target.closest(".direct-message-tray") && !target.closest("#direct-message-button")) {
				hideDirectMessageTray();
			}
			if (!target.closest(".reaction-picker") && !target.closest(".reaction-picker-toggle")) {
				if (openReactionPickerMessageId !== null) {
					setOpenReactionPickerMessageId(null);
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
					if (stonksPendingConfirm) {
						clearStonksPendingConfirm();
						renderModernDialog();
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
			if (event.key === "Escape" && directMessageTrayOpen) {
				hideDirectMessageTray();
				return;
			}
			if (event.key === "Escape" && openMenuId !== null) {
				closeTopMenu();
				return;
			}
			if (event.key === "Escape" && openReactionPickerMessageId !== null) {
				setOpenReactionPickerMessageId(null);
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
			if (directMessageTrayOpen) {
				positionDirectMessageTray();
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
			scheduleStonksTickerScrollSync();
		});
		window.addEventListener("mumble-host-viewport", function() {
			syncCompactRailState(true);
			scheduleFooterAlignmentSync();
			scheduleRailLayoutSync();
			scheduleStonksTickerScrollSync();
		});
		if (window.visualViewport) {
			window.visualViewport.addEventListener("resize", scheduleFooterAlignmentSync);
		}
		window.addEventListener("blur", function() {
			clearMenuPeekState();
			hideContextMenu();
			hideAppMenu();
			hideSelfMenu();
			hideDirectMessageTray();
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
			hideDirectMessageTray();
			syncRailOverflowState();
		});
		refs.messageList.addEventListener("click", handleMessageImageActivation);
		refs.messageList.addEventListener("scroll", function() {
			noteMessageListScrollActivity();
			if (contextMenuState) {
				hideContextMenu();
			}
			if (selfMenuOpen) {
				hideSelfMenu();
			}
			const shouldCloseReactionPicker =
				openReactionPickerMessageId !== null && !shouldKeepReactionPickerOpenOnScroll();
			if (shouldCloseReactionPicker) {
				setOpenReactionPickerMessageId(null);
				reactionPickerScrollClosePausedUntil = 0;
			}
			scheduleMessageListScrollStateSync();
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
		syncWalkthroughUiFromQuery();
		syncWalkthroughDialogFromQuery();
	}

	boot();
})();
