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

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let modernDialogAdvancedPages = {};
	let modernDialogPendingFieldUpdates = {};
	let modernDialogLocalFieldValues = {};
	let modernSettingsScrollPositions = {};
	let modernSettingsRenderedScrollKey = "";
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
	let modernDialogFavoriteMenu = null;
	let modernDialogFavoriteClickTimer = 0;
	let pendingModernDialogFieldFocus = "";
	let modernDialogSelectState = null;
	let modernDialogLastRenderKey = "";
	let stonksActiveTab = "market";
	let stonksLocalPins = null;
	let stonksPinMenuOpen = false;
	let stonksDraftPositions = null;
	let stonksDraftNote = "";
	let stonksDraftCurrency = "USD";
	let stonksDraftKey = "";
	let stonksQuoteSearchText = "";
	let stonksQuoteSearchBusy = false;
	let stonksQuoteSearchError = "";
	let stonksQuoteSuggestions = [];
	let stonksQuoteSearchRequestId = "";
	const bridgeRetryDelayMs = 50;
	const bridgeRetryLimit = 160;
	let bridgeRetryTimer = 0;
	let bridgeRetryCount = 0;
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

	const refs = {
		layer: document.getElementById("modern-dialog-layer"),
		backdrop: document.getElementById("modern-dialog-backdrop"),
		dialog: document.getElementById("modern-dialog"),
		eyebrow: document.getElementById("modern-dialog-eyebrow"),
		title: document.getElementById("modern-dialog-title"),
		subtitle: document.getElementById("modern-dialog-subtitle"),
		closeButton: document.getElementById("modern-dialog-close-button"),
		body: document.getElementById("modern-dialog-body"),
		actions: document.getElementById("modern-dialog-actions")
	};

	function applyModernUiTweaks(uiTweaks) {
		if (window.MumbleModernTheme && typeof window.MumbleModernTheme.apply === "function") {
			window.MumbleModernTheme.apply(uiTweaks || {});
		}
	}

	function initialModernUiTweaks() {
		if (!window.MumbleModernTheme) {
			return {};
		}
		const fromBootstrap = typeof window.MumbleModernTheme.uiTweaksFromBootstrap === "function"
			? window.MumbleModernTheme.uiTweaksFromBootstrap()
			: {};
		const fromSearch = typeof window.MumbleModernTheme.uiTweaksFromSearch === "function"
			? window.MumbleModernTheme.uiTweaksFromSearch(window.location.search)
			: {};
		return Object.assign({}, fromBootstrap || {}, fromSearch || {});
	}

	function resolvedModernDialogUiTweaks(state) {
		return Object.assign({}, initialModernUiTweaks(), (state && state.uiTweaks) || {});
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

	function notifyBridge(method) {
		if (!modernBridge || typeof modernBridge[method] !== "function") {
			return false;
		}

		const args = Array.prototype.slice.call(arguments, 1);
		try {
			modernBridge[method].apply(modernBridge, args);
			return true;
		} catch (error) {
			console.warn("Modern dialog bridge call failed:", method, error);
			return false;
		}
	}

	function scheduleBridgeRetry() {
		if (modernBridge || bridgeRetryTimer || bridgeRetryCount >= bridgeRetryLimit) {
			return;
		}

		bridgeRetryCount += 1;
		bridgeRetryTimer = window.setTimeout(function() {
			bridgeRetryTimer = 0;
			ensureBridge();
		}, bridgeRetryDelayMs);
	}

	async function ensureBridge() {
		if (modernBridge) {
			return;
		}

		async function bindBridge() {
			return new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						modernBridge = channel.objects.modernBridge || null;
						if (modernBridge) {
							bridgeRetryCount = 0;
							if (modernBridge.modernDialogStateChanged
									&& typeof modernBridge.modernDialogStateChanged.connect === "function") {
								modernBridge.modernDialogStateChanged.connect(syncModernDialogState);
							}
							if (modernBridge.financeQuoteResultReady
									&& typeof modernBridge.financeQuoteResultReady.connect === "function") {
								modernBridge.financeQuoteResultReady.connect(handleStonksQuoteLookupResult);
							}
							syncModernDialogState(modernBridge.modernDialogState || { open: false });
						} else {
							scheduleBridgeRetry();
						}
						resolve();
					});
				} catch (error) {
					console.warn("Modern dialog bridge initialization failed:", error);
					scheduleBridgeRetry();
					resolve();
				}
			});
		}

		if (!window.qt || !window.qt.webChannelTransport) {
			scheduleBridgeRetry();
			return;
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
					console.warn("Unable to load qwebchannel.js for the modern dialog.");
					bridgeLoadPromise = null;
					scheduleBridgeRetry();
					resolve();
				};
				document.head.appendChild(script);
			});
		}

		await bridgeLoadPromise;
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
		if (state && state.open) {
			applyModernUiTweaks(resolvedModernDialogUiTweaks(state));
		}
		if (state && state.open) {
			rememberFocusedModernDialogFieldValue();
		}
		if (!state || !state.open) {
			modernDialogPendingFieldUpdates = {};
			modernDialogLocalFieldValues = {};
			modernSettingsScrollPositions = {};
			modernSettingsRenderedScrollKey = "";
		}
		modernDialogState = applyPendingModernDialogFieldValues(state || null);
		renderModernDialog();
	}

	function modernDialogFocusableElements() {
		if (!refs.dialog) {
			return [];
		}

		return Array.prototype.slice.call(refs.dialog.querySelectorAll(
			"button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex='-1'])"
		)).filter(function(element) {
			return !!(element.offsetWidth || element.offsetHeight || element.getClientRects().length);
		});
	}

	function focusFirstModernDialogControl() {
		const focusable = modernDialogFocusableElements();
		const firstField = focusable.find(function(element) {
			return !!element.dataset.modernDialogFieldId;
		});
		const firstBodyControl = focusable.find(function(element) {
			return refs.body && refs.body.contains(element);
		});
		const firstAction = focusable.find(function(element) {
			return refs.actions && refs.actions.contains(element);
		});
		const firstNonClose = focusable.find(function(element) {
			return element !== refs.closeButton;
		});
		const target = firstField || firstBodyControl || firstAction || firstNonClose || focusable[0] || refs.dialog;
		if (target && typeof target.focus === "function") {
			target.focus({ preventScroll: true });
		}
	}

	function focusModernDialogField(fieldId) {
		if (!fieldId || !refs.dialog) {
			return false;
		}
		const target = refs.dialog.querySelector("[data-modern-dialog-field-id='" + String(fieldId).replace(/'/g, "\\'") + "']");
		if (!target || typeof target.focus !== "function") {
			return false;
		}
		target.focus({ preventScroll: false });
		if (typeof target.select === "function") {
			try {
				target.select();
			} catch (error) {
				// Some input types expose select APIs but reject selection.
			}
		}
		return true;
	}

	function requestModernDialogFieldFocus(fieldId) {
		pendingModernDialogFieldFocus = String(fieldId || "");
	}

	function restoreModernDialogFocus(focusState) {
		if (!focusState || !focusState.fieldId || !refs.dialog) {
			return false;
		}

		const candidates = Array.prototype.slice.call(
			refs.dialog.querySelectorAll("[data-modern-dialog-field-id]")
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

	function trapModernDialogTab(event) {
		const focusable = modernDialogFocusableElements();
		if (!focusable.length) {
			event.preventDefault();
			refs.dialog.focus({ preventScroll: true });
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

	function closeModernDialog() {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (dialogId) {
			notifyBridge("closeModernDialog", dialogId);
		}
		modernDialogPendingFieldUpdates = {};
		modernDialogLocalFieldValues = {};
		modernSettingsScrollPositions = {};
		modernSettingsRenderedScrollKey = "";
	}

	function invokeModernDialogAction(actionId, payload) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (!dialogId || !actionId) {
			return;
		}
		flushModernDialogFieldUpdates();
		notifyBridge("invokeModernDialogAction", dialogId, String(actionId), payload || {});
	}

	function modernDialogValueElement(input) {
		if (input && input.classList && input.classList.contains("modern-select-button")) {
			const shell = input.closest(".modern-select");
			const select = shell ? shell.querySelector("select") : null;
			if (select) {
				return select;
			}
		}
		return input;
	}

	function modernDialogInputValue(field, input) {
		input = modernDialogValueElement(input);
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

	function modernSettingsScrollKey(dialog) {
		if (!dialog || dialog.kind !== "settings") {
			return "";
		}
		const selectedPage = modernSettingsSelectedPage(dialog);
		const pageId = String(selectedPage.id || dialog.activePage || "");
		return modernDialogFieldKey(dialog.id || "", pageId || "settings");
	}

	function currentModernSettingsScrollElement() {
		return refs.body ? refs.body.querySelector(".modern-settings-scroll") : null;
	}

	function rememberRenderedModernSettingsScrollPosition() {
		const key = modernSettingsRenderedScrollKey;
		const scroll = currentModernSettingsScrollElement();
		if (!key || !scroll) {
			return;
		}
		modernSettingsScrollPositions[key] = {
			left: scroll.scrollLeft,
			top: scroll.scrollTop
		};
	}

	function restoreModernSettingsScrollPosition(key) {
		const scroll = currentModernSettingsScrollElement();
		const position = key ? modernSettingsScrollPositions[key] : null;
		if (!scroll || !position) {
			return;
		}
		scroll.scrollLeft = Math.max(0, Number(position.left) || 0);
		scroll.scrollTop = Math.max(0, Number(position.top) || 0);
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

	function closeModernDialogFavoriteMenu() {
		if (modernDialogFavoriteMenu && modernDialogFavoriteMenu.parentNode) {
			modernDialogFavoriteMenu.parentNode.removeChild(modernDialogFavoriteMenu);
		}
		modernDialogFavoriteMenu = null;
	}

	function clearModernDialogFavoriteClickTimer() {
		if (modernDialogFavoriteClickTimer) {
			window.clearTimeout(modernDialogFavoriteClickTimer);
			modernDialogFavoriteClickTimer = 0;
		}
	}

	function addFavoriteMenuButton(menu, label, actionId, favorite, options) {
		const button = document.createElement("button");
		button.type = "button";
		button.className = "modern-dialog-favorite-menu-item" + (options && options.danger ? " is-danger" : "");
		button.textContent = label;
		button.addEventListener("click", function(event) {
			event.preventDefault();
			event.stopPropagation();
			closeModernDialogFavoriteMenu();
			if (options && options.focusField) {
				requestModernDialogFieldFocus(options.focusField);
			}
			invokeModernDialogAction(actionId, { index: Number(favorite.index) || 0 });
		});
		menu.appendChild(button);
	}

	function openModernDialogFavoriteMenu(event, favorite) {
		event.preventDefault();
		event.stopPropagation();
		clearModernDialogFavoriteClickTimer();
		closeModernDialogFavoriteMenu();

		const menu = document.createElement("div");
		menu.className = "modern-dialog-favorite-menu";
		menu.setAttribute("role", "menu");
		addFavoriteMenuButton(menu, "Connect", "connectFavorite", favorite);
		addFavoriteMenuButton(menu, "Edit", "editFavorite", favorite, { focusField: "name" });
		addFavoriteMenuButton(menu, "Remove", "removeFavorite", favorite, { danger: true });
		menu.addEventListener("click", function(menuEvent) {
			menuEvent.stopPropagation();
		});
		document.body.appendChild(menu);
		modernDialogFavoriteMenu = menu;

		const rect = menu.getBoundingClientRect();
		const left = Math.min(event.clientX, window.innerWidth - rect.width - 8);
		const top = Math.min(event.clientY, window.innerHeight - rect.height - 8);
		menu.style.left = String(Math.max(8, left)) + "px";
		menu.style.top = String(Math.max(8, top)) + "px";
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

	function modernResultItemKind(item) {
		const type = String(item && item.type || "").trim().toLowerCase();
		const title = String(item && item.title || "").trim();
		const subtitle = String(item && item.subtitle || "").trim().toLowerCase();
		if (type === "user") {
			return "user";
		}
		if (type === "textroom" || type === "text-room" || type === "text_room" || type === "text") {
			return "textRoom";
		}
		if (title.charAt(0) === "#" || subtitle.indexOf("text room") !== -1) {
			return "textRoom";
		}
		return "room";
	}

	function modernResultTypeLabel(kind) {
		if (kind === "user") {
			return "User";
		}
		if (kind === "textRoom") {
			return "Text room";
		}
		return "Room";
	}

	function modernResultSubtitle(kind, subtitle, separator) {
		const label = modernResultTypeLabel(kind);
		const text = String(subtitle || "").trim();
		if (!text) {
			return label;
		}
		if (text.toLowerCase().indexOf(label.toLowerCase()) === 0) {
			return text;
		}
		return label + separator + text;
	}

	function modernResultLabel(value) {
		return String(value || "").trim();
	}

	function modernResultActionLabel(item, role, fallback) {
		const primary = modernResultLabel(item && item.primaryAction);
		const secondary = modernResultLabel(item && item.secondaryAction);
		const direct = modernResultLabel(item && item[role + "Action"]);
		const primaryKey = primary.toLowerCase();
		const secondaryKey = secondary.toLowerCase();
		if (direct) {
			return direct;
		}
		if (role === "message") {
			if (primaryKey.indexOf("message") !== -1) {
				return primary;
			}
			if (secondaryKey.indexOf("message") !== -1) {
				return secondary;
			}
		}
		if (role === "select") {
			if (primaryKey === "select") {
				return primary;
			}
			if (secondaryKey === "select") {
				return secondary;
			}
		}
		if (role === "join") {
			if (secondary && secondaryKey !== "open" && secondaryKey !== "select" && secondaryKey.indexOf("message") === -1) {
				return secondary;
			}
		}
		if (role === "open") {
			if (primary && primaryKey !== "select" && primaryKey !== "join" && primaryKey.indexOf("message") === -1) {
				return primary;
			}
		}
		return fallback;
	}

	function modernResultActions(item) {
		const kind = modernResultItemKind(item);
		const actions = kind === "user"
			? [
				{ id: "messageSearchResult", label: modernResultActionLabel(item, "message", "Message"), primary: true },
				{ id: "selectSearchResult", label: modernResultActionLabel(item, "select", "Select"), primary: false }
			]
			: (kind === "textRoom"
				? [
					{ id: "selectSearchResult", label: modernResultActionLabel(item, "open", "Open"), primary: true }
				]
				: [
					{ id: "selectSearchResult", label: modernResultActionLabel(item, "open", "Open"), primary: true },
					{ id: "joinSearchResult", label: modernResultActionLabel(item, "join", "Join"), primary: false }
				]);
		const seen = {};
		return actions.filter(function(action) {
			const key = String(action.label || "").trim().toLowerCase();
			if (!key || seen[key]) {
				return false;
			}
			seen[key] = true;
			return true;
		});
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
			const kind = modernResultItemKind(item);
			const row = document.createElement("div");
			row.className = "modern-dialog-result";
			const copy = document.createElement("div");
			copy.className = "modern-dialog-result-copy";
			const title = document.createElement("span");
			title.className = "modern-dialog-result-title";
			title.textContent = item.title || "";
			const subtitle = document.createElement("span");
			subtitle.className = "modern-dialog-result-subtitle";
			subtitle.textContent = modernResultSubtitle(kind, item.subtitle, " - ");
			copy.appendChild(title);
			copy.appendChild(subtitle);
			row.appendChild(copy);
			const actions = document.createElement("span");
			actions.className = "modern-dialog-result-actions";
			modernResultActions(item).forEach(function(action) {
				const button = document.createElement("button");
				button.type = "button";
				button.className = "chip-button" + (action.primary ? " is-primary" : "");
				button.textContent = action.label;
				button.addEventListener("click", function(event) {
					event.preventDefault();
					invokeModernDialogAction(action.id, {
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
				invokeModernDialogAction("keys.shortcutData", { index: Number(row.index) || 0, value: value });
			});
			select.disabled = disabled;
			container.appendChild(select);
			return;
		}
		if (type === "channel") {
			const select = modernShortcutEditorSelect(field.channelOptions, row.dataValue, "number", function(value) {
				invokeModernDialogAction("keys.shortcutData", { index: Number(row.index) || 0, value: value });
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
				invokeModernDialogAction("keys.shortcutData", { index: Number(row.index) || 0, value: input.value });
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
				invokeModernDialogAction("keys.shortcutAction", { index: rowIndex, actionIndex: value });
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
					invokeModernDialogAction("keys.shortcutSuppress", { index: rowIndex, suppress: checkbox.checked });
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
			value.textContent = field.value == null || field.value === "" ? "—" : String(field.value);
			if (field.monospace) {
				value.classList.add("is-monospace");
			}
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

			const imageError = document.createElement("span");
			imageError.className = "modern-dialog-field-error";
			imageError.style.display = "none";
			const setImageError = function(message) {
				const text = String(message || "");
				imageError.textContent = text;
				imageError.style.display = text ? "" : "none";
			};

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
				setImageError("");
				syncPreview();
				updateModernDialogField(field, input);
			});
			fileInput.addEventListener("change", function() {
				const file = fileInput.files && fileInput.files[0];
				if (!file) {
					return;
				}
				if (!serverIdentityFileLooksLikeImage(file)) {
					setImageError("Choose an image file.");
					return;
				}
				if (file.size > serverIdentityImageMaxFileBytes) {
					setImageError("Choose an image smaller than 4 MB.");
					return;
				}
				readFileAsDataUrl(file).then(function(dataUrl) {
					input.value = normalizedServerIdentityImageDataUrl(file, dataUrl);
					setImageError("");
					syncPreview();
					updateModernDialogField(field, input);
				}).catch(function(error) {
					console.warn("Unable to read server image:", error);
					setImageError("Could not read that image.");
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
			row.appendChild(imageError);
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

	function createModernDialogFavorites(dialog) {
		const section = document.createElement("section");
		section.className = "modern-dialog-section modern-dialog-favorites";
		const favorites = dialog.favorites || [];
		const header = document.createElement("div");
		header.className = "modern-dialog-section-header";
		const title = document.createElement("h2");
		title.className = "modern-dialog-section-title";
		title.textContent = "Saved servers";
		header.appendChild(title);
		const addButton = document.createElement("button");
		addButton.type = "button";
		addButton.className = "chip-button modern-dialog-favorites-add";
		addButton.textContent = "Add server";
		addButton.addEventListener("click", function() {
			requestModernDialogFieldFocus("host");
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
		favorites.forEach(function(favorite) {
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
			copy.appendChild(label);
			const subtitle = document.createElement("span");
			subtitle.className = "modern-dialog-favorite-subtitle";
			subtitle.textContent = favorite.subtitle || [favorite.host, favorite.port].filter(Boolean).join(":");
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
				clearModernDialogFavoriteClickTimer();
				modernDialogFavoriteClickTimer = window.setTimeout(function() {
					modernDialogFavoriteClickTimer = 0;
					invokeModernDialogAction("selectFavorite", { index: Number(favorite.index) || 0 });
				}, 180);
			});
			button.addEventListener("dblclick", function(event) {
				event.preventDefault();
				clearModernDialogFavoriteClickTimer();
				invokeModernDialogAction("connectFavorite", { index: Number(favorite.index) || 0 });
			});
			button.addEventListener("contextmenu", function(event) {
				openModernDialogFavoriteMenu(event, favorite);
			});
			list.appendChild(button);
		});

		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "modern-dialog-favorite-empty";
			const emptyTitle = document.createElement("p");
			emptyTitle.className = "modern-dialog-favorite-empty-title";
			emptyTitle.textContent = "No saved servers";
			const emptyButton = document.createElement("button");
			emptyButton.type = "button";
			emptyButton.className = "chip-button modern-dialog-favorite-empty-button";
			emptyButton.textContent = "Add server";
			emptyButton.addEventListener("click", function() {
				requestModernDialogFieldFocus("host");
				invokeModernDialogAction("newFavorite", {});
			});
			empty.appendChild(emptyTitle);
			empty.appendChild(emptyButton);
			list.appendChild(empty);
		}

		section.appendChild(list);
		return section;
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

	function stonksUserIdValue(value) {
		const number = Number(value);
		return Number.isFinite(number) && number >= 0 ? number : null;
	}

	function stonksSelectedUserId(stonks) {
		if (stonks && stonks.selectedUserId !== undefined && stonks.selectedUserId !== null) {
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

	function createModernSkeleton(kind, size) {
		const skeleton = document.createElement("span");
		skeleton.className = "modern-skeleton"
			+ (kind ? " is-" + kind : " is-line")
			+ (size ? " " + size : "");
		skeleton.setAttribute("aria-hidden", "true");
		return skeleton;
	}

	function appendModernSkeletonLines(parent, sizes) {
		(sizes || ["is-medium"]).forEach(function(size) {
			parent.appendChild(createModernSkeleton("line", size));
		});
	}

	function modernDialogLoadingText(dialog) {
		const sections = Array.isArray(dialog && dialog.sections) ? dialog.sections : [];
		for (let sectionIndex = 0; sectionIndex < sections.length; sectionIndex += 1) {
			const fields = Array.isArray(sections[sectionIndex] && sections[sectionIndex].fields)
				? sections[sectionIndex].fields
				: [];
			for (let fieldIndex = 0; fieldIndex < fields.length; fieldIndex += 1) {
				const field = fields[fieldIndex] || {};
				const text = String(field.value || field.text || "").trim();
				if (field.type === "note" && text) {
					return text;
				}
			}
		}
		return String(dialog && dialog.subtitle || "Loading...").trim() || "Loading...";
	}

	function modernDialogLoadingScaffoldKind(dialog) {
		const explicit = String(dialog && dialog.loadingScaffold || "").trim().toLowerCase();
		if (explicit) {
			return explicit;
		}
		const id = String(dialog && dialog.id || "").trim().toLowerCase();
		if (id === "acl") {
			return "acl";
		}
		if (id === "serveruserlist" || id === "registeredusers" || id === "serverbanlist" || id === "banlist") {
			return "records";
		}
		return "records";
	}

	function appendModernRecordLoadingScaffold(container, dialog) {
		const shell = document.createElement("section");
		shell.className = "modern-dialog-loading-scaffold is-records";
		shell.setAttribute("aria-busy", "true");

		const status = document.createElement("div");
		status.className = "modern-dialog-loading-status";
		const statusText = document.createElement("span");
		statusText.textContent = modernDialogLoadingText(dialog);
		status.appendChild(statusText);
		appendModernSkeletonLines(status, ["is-short"]);
		shell.appendChild(status);

		const stats = document.createElement("div");
		stats.className = "modern-dialog-loading-stats";
		for (let index = 0; index < 3; index += 1) {
			const stat = document.createElement("div");
			stat.className = "modern-dialog-loading-stat";
			appendModernSkeletonLines(stat, ["is-tiny", index === 1 ? "is-medium" : "is-short"]);
			stats.appendChild(stat);
		}
		shell.appendChild(stats);

		const list = document.createElement("div");
		list.className = "modern-dialog-loading-list";
		for (let index = 0; index < 4; index += 1) {
			const row = document.createElement("div");
			row.className = "modern-dialog-loading-row";
			row.appendChild(createModernSkeleton("avatar"));
			const copy = document.createElement("div");
			copy.className = "modern-dialog-loading-row-copy";
			appendModernSkeletonLines(copy, ["is-medium", index % 2 ? "is-short" : "is-long"]);
			row.appendChild(copy);
			row.appendChild(createModernSkeleton("pill", "is-short"));
			list.appendChild(row);
		}
		shell.appendChild(list);
		container.appendChild(shell);
	}

	function appendModernAclLoadingScaffold(container, dialog) {
		const shell = document.createElement("section");
		shell.className = "modern-dialog-loading-scaffold is-acl";
		shell.setAttribute("aria-busy", "true");

		const status = document.createElement("div");
		status.className = "modern-dialog-loading-status";
		const statusText = document.createElement("span");
		statusText.textContent = modernDialogLoadingText(dialog);
		status.appendChild(statusText);
		appendModernSkeletonLines(status, ["is-short"]);
		shell.appendChild(status);

		const policy = document.createElement("div");
		policy.className = "modern-dialog-loading-acl-policy";
		appendModernSkeletonLines(policy, ["is-medium", "is-long"]);
		shell.appendChild(policy);

		const tabs = document.createElement("div");
		tabs.className = "modern-dialog-loading-acl-tabs";
		for (let index = 0; index < 3; index += 1) {
			tabs.appendChild(createModernSkeleton("pill", index === 0 ? "is-medium" : "is-short"));
		}
		shell.appendChild(tabs);

		const workspace = document.createElement("div");
		workspace.className = "modern-dialog-loading-acl-workspace";
		const sidebar = document.createElement("div");
		sidebar.className = "modern-dialog-loading-acl-sidebar";
		for (let index = 0; index < 4; index += 1) {
			appendModernSkeletonLines(sidebar, [index === 0 ? "is-long" : "is-medium"]);
		}
		const detail = document.createElement("div");
		detail.className = "modern-dialog-loading-acl-detail";
		appendModernSkeletonLines(detail, ["is-medium", "is-long", "is-long"]);
		const matrix = document.createElement("div");
		matrix.className = "modern-dialog-loading-acl-matrix";
		for (let index = 0; index < 8; index += 1) {
			matrix.appendChild(createModernSkeleton("block"));
		}
		detail.appendChild(matrix);
		workspace.append(sidebar, detail);
		shell.appendChild(workspace);
		container.appendChild(shell);
	}

	function appendModernDialogLoadingScaffold(container, dialog) {
		if (modernDialogLoadingScaffoldKind(dialog) === "acl") {
			appendModernAclLoadingScaffold(container, dialog);
			return;
		}
		appendModernRecordLoadingScaffold(container, dialog);
	}

	function appendStonksLoadingSkeleton(parent) {
		const shell = document.createElement("section");
		shell.className = "stonks-loading-scaffold";
		shell.setAttribute("aria-busy", "true");

		const stats = document.createElement("div");
		stats.className = "stonks-stat-grid stonks-loading-stats";
		for (let index = 0; index < 3; index += 1) {
			const card = document.createElement("div");
			card.className = "stonks-stat is-loading";
			appendModernSkeletonLines(card, ["is-tiny", index === 0 ? "is-long" : "is-medium"]);
			stats.appendChild(card);
		}
		shell.appendChild(stats);

		const chart = document.createElement("div");
		chart.className = "stonks-chart-card is-loading";
		const chartTop = document.createElement("div");
		chartTop.className = "stonks-chart-top";
		const copy = document.createElement("div");
		appendModernSkeletonLines(copy, ["is-medium", "is-short"]);
		const value = document.createElement("div");
		value.className = "stonks-focus-price";
		appendModernSkeletonLines(value, ["is-medium", "is-short"]);
		chartTop.append(copy, value);
		const block = createModernSkeleton("block", "is-chart");
		chart.append(chartTop, block);
		shell.appendChild(chart);

		const listCard = document.createElement("div");
		listCard.className = "stonks-list-card is-loading";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		appendModernSkeletonLines(head, ["is-short", "is-tiny"]);
		listCard.appendChild(head);
		const list = document.createElement("div");
		list.className = "stonks-list";
		for (let index = 0; index < 4; index += 1) {
			const row = document.createElement("div");
			row.className = "stonks-row is-loading";
			row.appendChild(createModernSkeleton("avatar"));
			const rowCopy = document.createElement("div");
			rowCopy.className = "stonks-row-copy";
			appendModernSkeletonLines(rowCopy, ["is-short", "is-medium"]);
			row.appendChild(rowCopy);
			row.appendChild(createModernSkeleton("line", "is-short"));
			row.appendChild(createModernSkeleton("pill", "is-tiny"));
			list.appendChild(row);
		}
		listCard.appendChild(list);
		shell.appendChild(listCard);
		parent.appendChild(shell);
	}

	const stonksTickerPalette = ["#86cf86", "#e3b95f", "#5ec8b0", "#6fb0e8", "#e88f7a", "#b18ce0", "#e58bb6", "#9aa6b6"];

	function stonksHash(value) {
		const text = String(value || "");
		let hash = 0;
		for (let index = 0; index < text.length; index += 1) {
			hash = ((hash << 5) - hash + text.charCodeAt(index)) | 0;
		}
		return Math.abs(hash);
	}

	function stonksTickerColor(symbol) {
		const key = normalizeStonksSymbol(symbol) || String(symbol || "").trim().toUpperCase();
		if (key === "RKLB") {
			return "#86cf86";
		}
		if (key === "GME") {
			return "#e3b95f";
		}
		if (key === "NVDA") {
			return "#5ec8b0";
		}
		if (key === "PLTR") {
			return "#6fb0e8";
		}
		if (key === "TSLA") {
			return "#e88f7a";
		}
		if (key === "AMD") {
			return "#b18ce0";
		}
		return stonksTickerPalette[stonksHash(key) % stonksTickerPalette.length];
	}

	function stonksBadgeText(symbol) {
		const compact = String(symbol || "?").replace(/[^A-Za-z0-9]/g, "");
		return (compact || "?").slice(0, 2).toUpperCase();
	}

	function formatStonksQuoteMoney(value, currency) {
		const amount = stonksNumber(value);
		const code = String(currency || "USD").trim().toUpperCase() || "USD";
		const text = amount.toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 2 });
		if (code === "USD") {
			return "$" + text;
		}
		return code + " " + text;
	}

	function formatStonksSignedMoney(value, currency) {
		const amount = stonksNumber(value);
		const sign = amount >= 0 ? "+" : "-";
		return sign + formatStonksQuoteMoney(Math.abs(amount), currency).replace(/^\$/, "$");
	}

	function stonksQuoteForSymbol(stonks, symbol) {
		const normalized = normalizeStonksSymbol(symbol);
		if (!normalized || !stonks || !stonks.tickerQuotes || typeof stonks.tickerQuotes !== "object") {
			return null;
		}
		return stonks.tickerQuotes[normalized] || stonks.tickerQuotes[String(symbol || "").trim().toUpperCase()] || null;
	}

	function stonksSparkPoints(symbol, price, changePct) {
		const endPrice = Math.max(0.01, stonksNumber(price) || 1);
		const change = Number.isFinite(Number(changePct)) ? Number(changePct) : 0;
		const startPrice = Math.max(0.01, endPrice / (1 + change / 100));
		let seed = stonksHash(symbol) || 17;
		let value = startPrice;
		const points = [];
		for (let index = 0; index < 40; index += 1) {
			seed = (seed * 9301 + 49297) % 233280;
			const drift = (seed / 233280 - 0.46) * endPrice * 0.018;
			value = Math.max(0.01, value + drift);
			points.push(value);
		}
		points[0] = startPrice;
		points[points.length - 1] = endPrice;
		return points;
	}

	function stonksSparkSvg(ticker, width, height, big) {
		const points = Array.isArray(ticker && ticker.points) ? ticker.points : stonksSparkPoints(ticker && ticker.ticker, ticker && ticker.price, ticker && ticker.changePct);
		const pad = big ? 6 : 2;
		const min = Math.min.apply(null, points);
		const max = Math.max.apply(null, points);
		const range = (max - min) || 1;
		const step = (width - pad * 2) / Math.max(1, points.length - 1);
		const mapped = points.map(function(point, index) {
			const x = pad + index * step;
			const y = pad + (height - pad * 2) * (1 - (point - min) / range);
			return x.toFixed(1) + "," + y.toFixed(1);
		}).join(" ");
		const stroke = stonksNumber(ticker && ticker.changePct) >= 0 ? "var(--good)" : "var(--danger)";
		let svg = '<svg viewBox="0 0 ' + width + " " + height + '" preserveAspectRatio="none" aria-hidden="true">';
		if (big) {
			svg += '<polygon fill="' + stroke + '" opacity="0.10" points="' + pad + "," + (height - pad) + " " + mapped + " " + (width - pad) + "," + (height - pad) + '"></polygon>';
		}
		svg += '<polyline fill="none" stroke="' + stroke + '" stroke-width="' + (big ? "2.4" : "1.6") + '" stroke-linecap="round" stroke-linejoin="round" points="' + mapped + '"></polyline></svg>';
		return svg;
	}

	function stonksTickerFromSource(stonks, source) {
		const symbol = normalizeStonksSymbol(source && source.symbol) || String(source && source.symbol || "").trim().toUpperCase();
		if (!symbol) {
			return null;
		}
		const quote = stonksQuoteForSymbol(stonks, symbol);
		const providerId = String(quote && quote.providerId || source && source.providerId || "yahoo-finance").trim() || "yahoo-finance";
		const providerSymbol = normalizeStonksSymbol(quote && quote.providerSymbol || source && source.providerSymbol || symbol) || symbol;
		const sourceUrl = stonksSafeExternalUrl(quote && quote.quoteSourceUrl)
			|| stonksSafeExternalUrl(source && source.quoteSourceUrl)
			|| stonksYahooQuoteUrl(providerSymbol);
		const quantity = stonksNumber(source && (source.totalQuantity || source.quantity));
		const sourceValue = stonksNumber(source && (source.totalMarketValue || source.marketValue));
		const quotePrice = quote && quote.ok !== false ? stonksNumber(quote.price) : 0;
		const sourcePrice = stonksNumber(source && source.price);
		const price = quotePrice || sourcePrice || (quantity > 0 ? sourceValue / quantity : 0) || Math.max(1, (stonksHash(symbol) % 180) + 12);
		const changePct = quote && quote.ok !== false && Number.isFinite(Number(quote.changePercent))
			? Number(quote.changePercent)
			: ((stonksHash(symbol) % 96) / 10 - 4.2);
		const currency = String(quote && quote.currency || source && source.currency || "USD").trim().toUpperCase() || "USD";
		return {
			ticker: symbol,
			name: String(quote && quote.displayName || source && source.displayName || symbol).trim() || symbol,
			color: stonksTickerColor(symbol),
			price: price,
			changePct: changePct,
			currency: currency,
			providerId: providerId,
			providerSymbol: providerSymbol,
			quoteSourceUrl: sourceUrl,
			points: stonksSparkPoints(symbol, price, changePct)
		};
	}

	function stonksSafeExternalUrl(value) {
		const url = String(value || "").trim();
		return /^https?:\/\//i.test(url) ? url : "";
	}

	function stonksTickerSourceUrl(row) {
		if (!row) {
			return "";
		}
		return stonksSafeExternalUrl(row.quoteSourceUrl)
			|| stonksYahooQuoteUrl(row.providerSymbol || row.ticker);
	}

	function stonksTickerSourceLabel(row) {
		const label = stonksProviderLabel(row && row.providerId);
		return label === "Manual" ? "Yahoo" : label;
	}

	function openStonksTickerSource(row) {
		const url = stonksTickerSourceUrl(row);
		if (!url) {
			return;
		}
		if (!notifyBridge("activateLink", url)) {
			try {
				window.open(url, "_blank", "noopener");
			} catch (error) {
				console.warn("Unable to open ticker source:", error);
			}
		}
	}

	function stonksLedgerOverviewModel(stonks, marketRows) {
		const portfolio = stonksPortfolioModel(stonks, marketRows);
		const snapshots = (Array.isArray(stonks && stonks.snapshots) ? stonks.snapshots : [])
			.filter(function(snapshot) {
				return snapshot && stonksNumber(snapshot.totalValue) > 0;
			})
			.slice()
			.sort(function(left, right) {
				return Number(left && left.createdAt || 0) - Number(right && right.createdAt || 0);
			});
		const points = snapshots.length > 1
			? snapshots.map(function(snapshot) { return stonksNumber(snapshot.totalValue); })
			: stonksSparkPoints("ledger:" + stonksSelectedUserId(stonks), portfolio.equity, portfolio.pnlPct);
		const equity = portfolio.equity || points[points.length - 1] || 0;
		if (points.length) {
			points[points.length - 1] = equity;
		}
		const startValue = points.length ? points[0] : equity;
		const changePct = startValue > 0 ? ((equity - startValue) / startValue) * 100 : portfolio.pnlPct;
		const firstSnapshot = snapshots[0] || null;
		const lastSnapshot = snapshots[snapshots.length - 1] || null;
		return {
			ticker: "Ledger value",
			name: stonksSelectedUserName(stonks) + " · " + portfolio.holdings.length + " positions",
			price: equity,
			changePct: changePct,
			currency: portfolio.currency,
			points: points,
			startLabel: firstSnapshot ? formatStonksTime(firstSnapshot.createdAt) : "Start",
			endLabel: lastSnapshot ? formatStonksTime(lastSnapshot.createdAt) : "Now"
		};
	}

	function stonksMarketRows(stonks) {
		if (stonks && stonks.loading) {
			return [];
		}
		const rows = [];
		const seen = {};
		function add(source) {
			const ticker = stonksTickerFromSource(stonks, source);
			if (!ticker || seen[ticker.ticker]) {
				return;
			}
			seen[ticker.ticker] = true;
			rows.push(ticker);
		}
		(Array.isArray(stonks && stonks.personalTickers) ? stonks.personalTickers : []).forEach(add);
		const latest = stonksLatestSnapshot(stonks);
		(latest && Array.isArray(latest.positions) ? latest.positions : []).forEach(add);
		(Array.isArray(stonks && stonks.popularTickers) ? stonks.popularTickers : []).forEach(add);
		if (!rows.length) {
			["RKLB", "GME", "NVDA", "AMD"].forEach(function(symbol) {
				add({ symbol: symbol, displayName: symbol, currency: "USD" });
			});
		}
		return rows.slice(0, 8);
	}

	function stonksFeedPreferencesForModel(stonks) {
		const preferences = stonks && stonks.feedPreferences && typeof stonks.feedPreferences === "object"
			? stonks.feedPreferences
			: {};
		return {
			showMine: preferences.showMine !== false,
			showPopular: preferences.showPopular !== false,
			showPins: preferences.showPins !== false
		};
	}

	function stonksSendFeedPreferences(stonks, overrides) {
		const preferences = Object.assign({}, stonksFeedPreferencesForModel(stonks), overrides || {});
		if (stonks && stonks.feedPreferences && typeof stonks.feedPreferences === "object") {
			Object.assign(stonks.feedPreferences, preferences);
		} else if (stonks) {
			stonks.feedPreferences = preferences;
		}
		invokeModernDialogAction("setFeedPreferences", { feedPreferences: preferences });
		renderModernDialog();
	}

	function stonksTickerBannerAlwaysScrollEnabled() {
		const tweaks = modernDialogState && modernDialogState.uiTweaks && typeof modernDialogState.uiTweaks === "object"
			? modernDialogState.uiTweaks
			: {};
		return !!tweaks.tickerBannerAlwaysScroll;
	}

	function stonksSetTickerBannerAlwaysScroll(enabled) {
		if (!modernDialogState.uiTweaks || typeof modernDialogState.uiTweaks !== "object") {
			modernDialogState.uiTweaks = {};
		}
		modernDialogState.uiTweaks.tickerBannerAlwaysScroll = !!enabled;
		invokeModernDialogAction("setTickerBannerAlwaysScroll", { tickerBannerAlwaysScroll: !!enabled });
		renderModernDialog();
	}

	function stonksPinnedTickerPayload(stonks, symbol, source) {
		const normalized = normalizeStonksSymbol(symbol || source && (source.symbol || source.ticker)) || "";
		const row = source || stonksTickerFromSource(stonks, { symbol: normalized });
		return {
			symbol: normalized,
			displayName: String(row && (row.displayName || row.name) || "").trim(),
			providerId: String(row && row.providerId || "").trim(),
			providerSymbol: normalizeStonksSymbol(row && row.providerSymbol || normalized) || normalized,
			exchange: String(row && row.exchange || "").trim(),
			quoteSourceUrl: stonksSafeExternalUrl(row && row.quoteSourceUrl) || stonksYahooQuoteUrl(normalized)
		};
	}

	function stonksSetLocalPin(symbol, pinned) {
		const normalized = normalizeStonksSymbol(symbol) || String(symbol || "").trim().toUpperCase();
		if (!normalized) {
			return;
		}
		if (!Array.isArray(stonksLocalPins)) {
			stonksLocalPins = [];
		}
		const index = stonksLocalPins.indexOf(normalized);
		if (pinned && index < 0) {
			stonksLocalPins.push(normalized);
		} else if (!pinned && index >= 0) {
			stonksLocalPins.splice(index, 1);
		}
	}

	function stonksPinsForModel(stonks, marketRows) {
		if (Array.isArray(stonksLocalPins)) {
			return stonksLocalPins;
		}
		const pins = [];
		(Array.isArray(stonks && stonks.pinnedTickers) ? stonks.pinnedTickers : []).forEach(function(pin) {
			const symbol = normalizeStonksSymbol(pin && (pin.symbol || pin.ticker) || pin);
			if (symbol && pins.indexOf(symbol) < 0) {
				pins.push(symbol);
			}
		});
		stonksLocalPins = pins;
		return stonksLocalPins;
	}

	function stonksIsPinned(symbol) {
		const normalized = normalizeStonksSymbol(symbol) || String(symbol || "").trim().toUpperCase();
		return Array.isArray(stonksLocalPins) && stonksLocalPins.indexOf(normalized) >= 0;
	}

	function stonksTogglePin(symbol, source) {
		const normalized = normalizeStonksSymbol(symbol) || String(symbol || "").trim().toUpperCase();
		if (!normalized) {
			return;
		}
		const pinned = !stonksIsPinned(normalized);
		const stonks = modernDialogState && modernDialogState.stonks || {};
		stonksSetLocalPin(normalized, pinned);
		stonksPinMenuOpen = false;
		invokeModernDialogAction("setTickerPin", {
			ticker: stonksPinnedTickerPayload(stonks, normalized, source),
			pinned: pinned
		});
		renderModernDialog();
	}

	function clearStonksDialogHeader() {
		if (!refs.dialog) {
			return;
		}
		const extra = refs.dialog.querySelector(".stonks-head-actions");
		if (extra && extra.parentNode) {
			extra.parentNode.removeChild(extra);
		}
	}

	function syncStonksDialogHeader(stonks, marketRows) {
		clearStonksDialogHeader();
		refs.eyebrow.textContent = "#stonks · live tape";
		refs.title.textContent = "";
		refs.title.appendChild(document.createTextNode("Stonks "));
		const meme = document.createElement("span");
		meme.className = "stonks-meme";
		meme.textContent = "↗";
		refs.title.appendChild(meme);
		refs.subtitle.textContent = "";

		const header = refs.dialog && refs.dialog.querySelector(".modern-dialog-header");
		if (!header) {
			return;
		}
		const index = document.createElement("div");
		index.className = "stonks-head-actions";
		const indexValue = document.createElement("div");
		indexValue.className = "stonks-index";
		const rows = Array.isArray(stonks && stonks.leaderboard) ? stonks.leaderboard : [];
		const selfRow = rows.find(function(row) { return Number(row && row.userId) === Number(stonks && stonks.selfUserId); }) || rows[0] || null;
		const change = selfRow ? stonksNumber(selfRow.returnPercent) : (marketRows[0] ? stonksNumber(marketRows[0].changePct) : 0);
		const name = document.createElement("span");
		name.className = "idx-name";
		name.textContent = selfRow ? "Server PnL" : "S&P 500";
		const value = document.createElement("span");
		value.className = "idx-val";
		if (stonks && stonks.loading) {
			value.classList.add("is-loading");
			value.appendChild(createModernSkeleton("line", "is-short"));
		} else {
			value.classList.add(change >= 0 ? "is-up" : "is-down");
			value.textContent = (change >= 0 ? "▲ " : "▼ ") + Math.abs(change).toFixed(1) + "%";
		}
		indexValue.append(name, value);
		index.appendChild(indexValue);
		header.insertBefore(index, refs.closeButton || null);
	}

	function appendStonksPinPicker(parent, marketRows) {
		const picker = document.createElement("div");
		picker.className = "pin-picker-wrap";
		const add = document.createElement("button");
		add.type = "button";
		add.className = "pin-add" + (stonksPinMenuOpen ? " is-open" : "");
		add.textContent = "Pin";
		add.setAttribute("aria-haspopup", "menu");
		add.setAttribute("aria-expanded", stonksPinMenuOpen ? "true" : "false");
		add.addEventListener("click", function() {
			stonksPinMenuOpen = !stonksPinMenuOpen;
			renderModernDialog();
		});
		add.addEventListener("keydown", function(event) {
			if (event.key === "Escape" && stonksPinMenuOpen) {
				event.preventDefault();
				stonksPinMenuOpen = false;
				renderModernDialog();
			}
		});
		picker.appendChild(add);

		if (stonksPinMenuOpen) {
			const menu = document.createElement("div");
			menu.className = "pin-picker";
			menu.setAttribute("role", "menu");
			const candidates = (marketRows || []).filter(function(row) {
				return row && row.ticker && !stonksIsPinned(row.ticker);
			});
			candidates.forEach(function(row) {
				const item = document.createElement("button");
				item.type = "button";
				item.className = "pin-picker-item";
				item.setAttribute("role", "menuitem");
				const dot = document.createElement("span");
				dot.className = "pin-dot";
				dot.style.background = row.color || stonksTickerColor(row.ticker);
				const copy = document.createElement("span");
				copy.className = "pin-picker-copy";
				const ticker = document.createElement("strong");
				ticker.textContent = row.ticker;
				const name = document.createElement("small");
				name.textContent = row.name || stonksTickerSourceLabel(row);
				copy.append(ticker, name);
				const change = document.createElement("span");
				change.className = "pin-change " + (stonksNumber(row.changePct) >= 0 ? "is-up" : "is-down");
				change.textContent = (stonksNumber(row.changePct) >= 0 ? "▲" : "▼")
					+ Math.abs(stonksNumber(row.changePct)).toFixed(1) + "%";
				item.append(dot, copy, change);
				item.addEventListener("click", function() {
					stonksTogglePin(row.ticker, row);
				});
				menu.appendChild(item);
			});
			if (!candidates.length) {
				const empty = document.createElement("div");
				empty.className = "pin-picker-empty";
				empty.textContent = "All visible tickers are pinned.";
				menu.appendChild(empty);
			}
			picker.appendChild(menu);
		}

		parent.appendChild(picker);
	}

	function appendStonksPins(parent, stonks, marketRows) {
		const pins = stonksPinsForModel(stonks, marketRows);
		const wrap = document.createElement("div");
		wrap.className = "stonks-pins";
		pins.forEach(function(symbol) {
			const ticker = marketRows.find(function(row) { return row.ticker === symbol; }) || stonksTickerFromSource(stonks, { symbol: symbol });
			const chip = document.createElement("div");
			chip.className = "pin-chip is-link";
			chip.setAttribute("role", "link");
			chip.tabIndex = 0;
			chip.title = "Open " + symbol + " on " + stonksTickerSourceLabel(ticker);
			chip.addEventListener("click", function() {
				openStonksTickerSource(ticker);
			});
			chip.addEventListener("keydown", function(event) {
				if (event.key === "Enter" || event.key === " ") {
					event.preventDefault();
					openStonksTickerSource(ticker);
				}
			});
			const dot = document.createElement("span");
			dot.className = "pin-dot";
			dot.style.background = ticker ? ticker.color : stonksTickerColor(symbol);
			const label = document.createElement("b");
			label.textContent = symbol;
			chip.append(dot, label);
			if (ticker) {
				const change = document.createElement("span");
				change.className = "pin-change " + (ticker.changePct >= 0 ? "is-up" : "is-down");
				change.textContent = (ticker.changePct >= 0 ? "▲" : "▼") + Math.abs(ticker.changePct).toFixed(1) + "%";
				chip.appendChild(change);
			}
			const close = document.createElement("button");
			close.type = "button";
			close.className = "pin-x";
			close.title = "Unpin";
			close.innerHTML = '<svg viewBox="0 0 24 24"><path d="M8 8l8 8M16 8l-8 8"></path></svg>';
			close.addEventListener("click", function(event) {
				event.stopPropagation();
				stonksTogglePin(symbol, ticker);
			});
			chip.appendChild(close);
			wrap.appendChild(chip);
		});
		appendStonksPinPicker(wrap, marketRows);
		parent.appendChild(wrap);
	}

	function appendStonksTickerTapeControls(parent, stonks) {
		const preferences = stonksFeedPreferencesForModel(stonks);
		const registered = stonks && stonks.registered !== false;
		const card = document.createElement("section");
		card.className = "stonks-tape-controls";
		const copy = document.createElement("div");
		copy.className = "stonks-tape-copy";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Ticker tape";
		const sub = document.createElement("small");
		sub.textContent = registered ? "Main window banner source" : "Register to save banner sources";
		copy.append(label, sub);
		const actions = document.createElement("div");
		actions.className = "stonks-tape-actions";
		const button = function(text, checked, enabled, callback) {
			const item = document.createElement("button");
			item.type = "button";
			item.className = "sel-pill stonks-tape-toggle " + (checked ? "is-buy" : "is-muted");
			item.setAttribute("role", "switch");
			item.setAttribute("aria-checked", checked ? "true" : "false");
			item.textContent = text;
			item.disabled = !enabled;
			item.addEventListener("click", callback);
			actions.appendChild(item);
		};
		button("Scroll", stonksTickerBannerAlwaysScrollEnabled(), true, function() {
			stonksSetTickerBannerAlwaysScroll(!stonksTickerBannerAlwaysScrollEnabled());
		});
		button("Pinned", preferences.showPins, registered, function() {
			stonksSendFeedPreferences(stonks, { showPins: !preferences.showPins });
		});
		button("Portfolio", preferences.showMine, registered, function() {
			stonksSendFeedPreferences(stonks, { showMine: !preferences.showMine });
		});
		button("Popular", preferences.showPopular, registered, function() {
			stonksSendFeedPreferences(stonks, { showPopular: !preferences.showPopular });
		});
		card.append(copy, actions);
		parent.appendChild(card);
	}

	function appendStonksMarketTab(parent, stonks, marketRows) {
		if (!marketRows.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No ticker quotes yet.";
			parent.appendChild(empty);
			return;
		}
		const overview = stonksLedgerOverviewModel(stonks, marketRows);
		const up = stonksNumber(overview.changePct) >= 0;
		const chart = document.createElement("section");
		chart.className = "stonks-chart-card";
		const top = document.createElement("div");
		top.className = "stonks-chart-top";
		const copy = document.createElement("div");
		const ticker = document.createElement("p");
		ticker.className = "stonks-focus-ticker";
		ticker.textContent = overview.ticker;
		const name = document.createElement("p");
		name.className = "stonks-focus-name";
		name.textContent = overview.name;
		copy.append(ticker, name);
		const price = document.createElement("div");
		price.className = "stonks-focus-price";
		const value = document.createElement("p");
		value.className = "stonks-focus-value";
		value.textContent = formatStonksQuoteMoney(overview.price, overview.currency);
		const change = document.createElement("p");
		change.className = "stonks-focus-change " + (up ? "is-up" : "is-down");
		change.textContent = (up ? "▲ " : "▼ ") + Math.abs(overview.changePct).toFixed(1) + "% total";
		price.append(value, change);
		top.append(copy, price);
		const spark = document.createElement("div");
		spark.className = "stonks-spark";
		spark.innerHTML = stonksSparkSvg(overview, 660, 120, true);
		const range = document.createElement("div");
		range.className = "stonks-range";
		range.innerHTML = "<span></span><span></span>";
		range.children[0].textContent = overview.startLabel;
		range.children[1].textContent = overview.endLabel;
		chart.append(top, spark, range);
		parent.appendChild(chart);

		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Watchlist";
		head.appendChild(label);
		const list = document.createElement("div");
		list.className = "stonks-list";
		list.setAttribute("role", "list");
		marketRows.forEach(function(row, index) {
			const rowUp = stonksNumber(row.changePct) >= 0;
			const item = document.createElement("div");
			item.className = "stonks-row";
			item.setAttribute("role", "link");
			item.tabIndex = 0;
			item.title = "Open " + row.ticker + " on " + stonksTickerSourceLabel(row);
			const badge = document.createElement("div");
			badge.className = "stonks-row-badge";
			badge.style.background = row.color;
			badge.textContent = stonksBadgeText(row.ticker);
			const rowCopy = document.createElement("div");
			rowCopy.className = "stonks-row-copy";
			const rowTicker = document.createElement("div");
			rowTicker.className = "stonks-row-ticker";
			rowTicker.textContent = row.ticker;
			const rowName = document.createElement("div");
			rowName.className = "stonks-row-name";
			rowName.textContent = row.name;
			rowCopy.append(rowTicker, rowName);
			const mini = document.createElement("div");
			mini.className = "stonks-row-mini";
			mini.innerHTML = stonksSparkSvg(row, 56, 24, false);
			const rowNum = document.createElement("div");
			rowNum.className = "stonks-row-num";
			const rowPrice = document.createElement("div");
			rowPrice.className = "stonks-row-price";
			rowPrice.textContent = formatStonksQuoteMoney(row.price, row.currency);
			const rowChange = document.createElement("div");
			rowChange.className = "stonks-row-change " + (rowUp ? "is-up" : "is-down");
			rowChange.textContent = (rowUp ? "▲ " : "▼ ") + Math.abs(row.changePct).toFixed(1) + "%";
			rowNum.append(rowPrice, rowChange);
			const pin = document.createElement("button");
			pin.type = "button";
			pin.className = "stonks-pinbtn" + (stonksIsPinned(row.ticker) ? " is-pinned" : "");
			pin.title = stonksIsPinned(row.ticker) ? "Unpin" : "Pin";
			pin.innerHTML = '<svg viewBox="0 0 24 24"><path d="M9 4h6l-1 6 3 3v2h-4v5l-1 1-1-1v-5H6v-2l3-3z"></path></svg>';
			pin.addEventListener("click", function(event) {
				event.stopPropagation();
				stonksTogglePin(row.ticker, row);
			});
			item.append(badge, rowCopy, mini, rowNum, pin);
			item.addEventListener("click", function() {
				openStonksTickerSource(row);
			});
			item.addEventListener("keydown", function(event) {
				if (event.key === "Enter" || event.key === " ") {
					event.preventDefault();
					openStonksTickerSource(row);
				}
			});
			list.appendChild(item);
		});
		card.append(head, list);
		parent.appendChild(card);
	}

	function stonksPortfolioModel(stonks, marketRows) {
		const latest = stonksLatestSnapshot(stonks);
		const previous = Array.isArray(stonks && stonks.snapshots) ? stonks.snapshots[1] : null;
		const previousBySymbol = {};
		(previous && Array.isArray(previous.positions) ? previous.positions : []).forEach(function(position) {
			const normalized = stonksNormalizePosition(position, previous.currency || "USD");
			if (normalized.symbol) {
				previousBySymbol[normalized.symbol] = normalized;
			}
		});
		const positions = (latest && Array.isArray(latest.positions) ? latest.positions : []).map(function(position) {
			return stonksNormalizePosition(position, latest && latest.currency || "USD");
		}).filter(function(position) {
			return !!position.symbol;
		});
		let value = 0;
		let cost = 0;
		let dayAbs = 0;
		const holdings = positions.map(function(position) {
			const market = marketRows.find(function(row) { return row.ticker === position.symbol; });
			const quantity = stonksNumber(position.quantity);
			const price = market ? market.price : (stonksNumber(position.price) || (quantity > 0 ? stonksNumber(position.marketValue) / quantity : 0));
			const marketValue = quantity > 0 ? quantity * price : stonksNumber(position.marketValue);
			const previousPosition = previousBySymbol[position.symbol];
			const avgCost = previousPosition && stonksNumber(previousPosition.price) > 0
				? stonksNumber(previousPosition.price)
				: (market ? price / (1 + stonksNumber(market.changePct) / 100) : price);
			const positionCost = quantity * avgCost;
			const previousPrice = market ? price / (1 + stonksNumber(market.changePct) / 100) : avgCost;
			value += marketValue;
			cost += positionCost;
			dayAbs += quantity * (price - previousPrice);
			return {
				ticker: position.symbol,
				name: position.displayName || position.symbol,
				shares: quantity,
				avgCost: avgCost,
				value: marketValue,
				currency: position.currency,
				color: market ? market.color : stonksTickerColor(position.symbol),
				pnlAbs: marketValue - positionCost,
				pnlPct: positionCost ? (marketValue - positionCost) / positionCost * 100 : 0
			};
		});
		const latestTotal = latest ? stonksNumber(latest.totalValue) : value;
		const cash = Math.max(0, latestTotal - value);
		const equity = Math.max(latestTotal, value + cash);
		const baseline = previous && stonksNumber(previous.totalValue) > 0 ? stonksNumber(previous.totalValue) : cost;
		const pnlAbs = baseline ? equity - baseline : value - cost;
		const pnlPct = baseline ? pnlAbs / baseline * 100 : (cost ? (value - cost) / cost * 100 : 0);
		const dayPct = value ? dayAbs / Math.max(1, value - dayAbs) * 100 : 0;
		return { holdings: holdings, value: value, cash: cash, equity: equity, pnlAbs: pnlAbs, pnlPct: pnlPct, dayAbs: dayAbs, dayPct: dayPct, currency: latest && latest.currency || "USD" };
	}

	function appendStonksPortfolioTab(parent, stonks, marketRows) {
		if (stonksCanEditPortfolio(stonks)) {
			appendStonksEditor(parent, stonks);
		}

		const portfolio = stonksPortfolioModel(stonks, marketRows);
		const upTotal = portfolio.pnlAbs >= 0;
		const upDay = portfolio.dayAbs >= 0;
		const summary = document.createElement("section");
		summary.className = "pf-summary";
		[["Net worth", formatStonksQuoteMoney(portfolio.equity, portfolio.currency), ""],
		 ["Total PnL", formatStonksSignedMoney(portfolio.pnlAbs, portfolio.currency) + " (" + formatStonksPercent(portfolio.pnlPct) + ")", upTotal ? "is-up" : "is-down"],
		 ["Today", (upDay ? "▲ " : "▼ ") + Math.abs(portfolio.dayPct).toFixed(2) + "%", upDay ? "is-up" : "is-down"]].forEach(function(cell) {
			const item = document.createElement("div");
			item.className = "pf-cell";
			const key = document.createElement("div");
			key.className = "k";
			key.textContent = cell[0];
			const value = document.createElement("div");
			value.className = "v" + (cell[0] === "Net worth" ? "" : " sm") + (cell[2] ? " " + cell[2] : "");
			value.textContent = cell[1];
			item.append(key, value);
			summary.appendChild(item);
		});
		parent.appendChild(summary);
		const allocWrap = document.createElement("div");
		const alloc = document.createElement("div");
		alloc.className = "pf-alloc";
		portfolio.holdings.forEach(function(holding) {
			const segment = document.createElement("span");
			segment.style.width = (portfolio.equity > 0 ? holding.value / portfolio.equity * 100 : 0) + "%";
			segment.style.background = holding.color;
			segment.title = holding.ticker;
			alloc.appendChild(segment);
		});
		if (portfolio.cash > 0) {
			const cash = document.createElement("span");
			cash.style.width = (portfolio.cash / Math.max(1, portfolio.equity) * 100) + "%";
			cash.style.background = "var(--bg-4)";
			cash.title = "Cash";
			alloc.appendChild(cash);
		}
		const range = document.createElement("div");
		range.className = "stonks-range";
		const holdings = document.createElement("span");
		holdings.textContent = "Holdings " + formatStonksQuoteMoney(portfolio.value, portfolio.currency);
		const cashText = document.createElement("span");
		cashText.textContent = "Cash " + formatStonksQuoteMoney(portfolio.cash, portfolio.currency);
		range.append(holdings, cashText);
		allocWrap.append(alloc, range);
		parent.appendChild(allocWrap);
		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Positions";
		const count = document.createElement("span");
		count.className = "quiet-count";
		count.textContent = String(portfolio.holdings.length);
		head.append(label, count);
		const list = document.createElement("div");
		if (!portfolio.holdings.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No portfolio positions yet.";
			list.appendChild(empty);
		}
		portfolio.holdings.forEach(function(holding) {
			const up = holding.pnlAbs >= 0;
			const row = document.createElement("div");
			row.className = "holding-row";
			const badge = document.createElement("div");
			badge.className = "holding-badge";
			badge.style.background = holding.color;
			badge.textContent = stonksBadgeText(holding.ticker);
			const copy = document.createElement("div");
			copy.className = "holding-copy";
			const ticker = document.createElement("div");
			ticker.className = "holding-ticker";
			ticker.textContent = holding.ticker;
			const sub = document.createElement("div");
			sub.className = "holding-sub";
			sub.textContent = holding.shares + " sh · avg " + formatStonksQuoteMoney(holding.avgCost, holding.currency);
			copy.append(ticker, sub);
			const num = document.createElement("div");
			num.className = "holding-num";
			const val = document.createElement("div");
			val.className = "holding-val";
			val.textContent = formatStonksQuoteMoney(holding.value, holding.currency);
			const pnl = document.createElement("div");
			pnl.className = "holding-pnl " + (up ? "is-up" : "is-down");
			pnl.textContent = formatStonksPercent(holding.pnlPct);
			num.append(val, pnl);
			const pin = document.createElement("button");
			pin.type = "button";
			pin.className = "stonks-pinbtn" + (stonksIsPinned(holding.ticker) ? " is-pinned" : "");
			pin.title = stonksIsPinned(holding.ticker) ? "Unpin" : "Pin";
			pin.innerHTML = '<svg viewBox="0 0 24 24"><path d="M9 4h6l-1 6 3 3v2h-4v5l-1 1-1-1v-5H6v-2l3-3z"></path></svg>';
			pin.addEventListener("click", function() {
				stonksTogglePin(holding.ticker, holding);
			});
			row.append(badge, copy, num, pin);
			list.appendChild(row);
		});
		card.append(head, list);
		parent.appendChild(card);
	}

	function stonksAvatarColor(index) {
		return stonksTickerPalette[index % stonksTickerPalette.length];
	}

	function appendStonksLeaderboardTab(parent, stonks, marketRows) {
		const rows = (Array.isArray(stonks && stonks.leaderboard) ? stonks.leaderboard : []).slice().sort(function(left, right) {
			return stonksNumber(right.returnPercent) - stonksNumber(left.returnPercent);
		});
		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Today's PnL · server";
		const count = document.createElement("span");
		count.className = "quiet-count";
		count.textContent = String(rows.length);
		head.append(label, count);
		const list = document.createElement("div");
		rows.forEach(function(row, index) {
			const up = stonksNumber(row.returnPercent) >= 0;
			const item = document.createElement("div");
			item.className = "lb-row medal-" + (index + 1) + (Number(row.userId) === Number(stonks && stonks.selfUserId) ? " is-self" : "");
			const rank = document.createElement("div");
			rank.className = "lb-rank";
			rank.textContent = index === 0 ? "1" : index === 1 ? "2" : index === 2 ? "3" : String(index + 1);
			const avatar = document.createElement("div");
			avatar.className = "lb-av";
			avatar.style.background = stonksAvatarColor(index);
			avatar.textContent = initialsFor(row.userName || "U");
			const copy = document.createElement("div");
			copy.className = "lb-copy";
			const name = document.createElement("div");
			name.className = "lb-name";
			name.textContent = row.userName || ("user " + row.userId);
			const pinLine = document.createElement("div");
			pinLine.className = "lb-pin";
			const pinTag = document.createElement("span");
			pinTag.className = "pin-tag";
			const pinnedTicker = marketRows[index % Math.max(1, marketRows.length)];
			pinTag.textContent = pinnedTicker ? pinnedTicker.ticker : "CASH";
			if (pinnedTicker) {
				pinTag.style.color = pinnedTicker.color;
			}
			pinLine.append(document.createTextNode("playing "), pinTag);
			copy.append(name, pinLine);
			const num = document.createElement("div");
			num.className = "lb-num";
			const pct = document.createElement("div");
			pct.className = "lb-pct " + (up ? "is-up" : "is-down");
			pct.textContent = formatStonksPercent(row.returnPercent);
			const val = document.createElement("div");
			val.className = "lb-val";
			val.textContent = row.insufficientHistory ? "need baseline" : (row.period || stonks.selectedPeriod || "today");
			num.append(pct, val);
			item.append(rank, avatar, copy, num);
			list.appendChild(item);
		});
		if (!rows.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No ranked PnL for this period yet.";
			list.appendChild(empty);
		}
		card.append(head, list);
		parent.appendChild(card);
	}

	function appendStonksFollowingTab(parent, stonks, marketRows) {
		const users = (Array.isArray(stonks && stonks.users) ? stonks.users : []).filter(function(user) {
			return Number(user && user.userId) !== Number(stonks && stonks.selfUserId);
		});
		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Following · " + users.length;
		const live = document.createElement("span");
		live.className = "quiet-count";
		live.textContent = "live";
		head.append(label, live);
		const list = document.createElement("div");
		list.className = "sel-list";
		users.forEach(function(user, index) {
			const ticker = marketRows[index % Math.max(1, marketRows.length)];
			const row = document.createElement("div");
			row.className = "sel-row";
			const avatar = document.createElement("div");
			avatar.className = "lb-av";
			avatar.style.background = stonksAvatarColor(index + 2);
			avatar.textContent = initialsFor(user.userName || "U");
			const copy = document.createElement("div");
			copy.className = "grow";
			const name = document.createElement("div");
			name.className = "nm";
			name.textContent = user.userName || ("user " + user.userId);
			const sub = document.createElement("div");
			sub.className = "sub";
			sub.textContent = user.followed ? "following" : "not followed";
			if (ticker) {
				sub.textContent += " · " + ticker.ticker;
			}
			copy.append(name, sub);
			const pill = document.createElement("button");
			pill.type = "button";
			pill.className = "sel-pill";
			pill.textContent = user.followed ? "Unfollow" : "Follow";
			pill.disabled = !stonks.registered;
			pill.addEventListener("click", function() {
				invokeModernDialogAction(user.followed ? "unfollow" : "follow", { userId: user.userId });
			});
			row.append(avatar, copy, pill);
			list.appendChild(row);
		});
		if (!users.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No traders to follow yet.";
			list.appendChild(empty);
		}
		card.append(head, list);
		parent.appendChild(card);
	}

	function appendStonksAuditTab(parent, stonks) {
		const snapshots = Array.isArray(stonks && stonks.snapshots) ? stonks.snapshots : [];
		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Recent trades · audit";
		const count = document.createElement("span");
		count.className = "quiet-count";
		count.textContent = String(snapshots.length);
		head.append(label, count);
		const list = document.createElement("div");
		list.className = "sel-list";
		snapshots.forEach(function(snapshot, index) {
			const row = document.createElement("div");
			row.className = "sel-row";
			const side = document.createElement("span");
			side.className = "sel-pill";
			side.textContent = index === 0 ? "BUY" : "SAVE";
			side.classList.add(index === 0 ? "is-buy" : "is-neutral");
			const copy = document.createElement("div");
			copy.className = "grow";
			const name = document.createElement("div");
			name.className = "nm";
			name.textContent = formatStonksMoney(snapshot.totalValue, snapshot.currency || "USD");
			const sub = document.createElement("div");
			sub.className = "sub";
			sub.textContent = (snapshot.userName || stonksSelectedUserName(stonks)) + " · " + formatStonksTime(snapshot.createdAt);
			copy.append(name, sub);
			const pill = document.createElement("span");
			pill.className = "sel-pill";
			pill.textContent = String((snapshot.positions || []).length) + " pos";
			row.append(side, copy, pill);
			list.appendChild(row);
		});
		if (!snapshots.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No portfolio updates yet.";
			list.appendChild(empty);
		}
		card.append(head, list);
		parent.appendChild(card);
	}

	function appendStonksAdminTab(parent, stonks) {
		if (!stonks.canAdmin) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "Root Write permission is required.";
			parent.appendChild(empty);
			return;
		}
		const currentTextChannelId = Number(stonks.textChannelId || 0);
		const adminConfigPayload = function(overrides) {
			const payload = {
				enabled: stonks.enabled !== false,
				socialAnnouncementsEnabled: stonks.socialAnnouncementsEnabled !== false,
				textChannelId: currentTextChannelId
			};
			Object.keys(overrides || {}).forEach(function(key) {
				payload[key] = overrides[key];
			});
			return payload;
		};
		const sendConfig = function(overrides) {
			invokeModernDialogAction("configure", adminConfigPayload(overrides));
		};
		const configuredChannel = (stonks.textChannels || []).find(function(channel) {
			return Number(channel && channel.textChannelId || 0) === currentTextChannelId;
		});
		const card = document.createElement("section");
		card.className = "stonks-list-card";
		const head = document.createElement("div");
		head.className = "stonks-list-head";
		const label = document.createElement("span");
		label.className = "section-label";
		label.textContent = "Server Stonks settings";
		const badge = document.createElement("span");
		badge.className = "quiet-count";
		badge.textContent = stonks.canAdmin ? "admin" : "read only";
		head.append(label, badge);
		const list = document.createElement("div");
		list.className = "sel-list";
		const appendRow = function(label, control, subtitle) {
			const row = document.createElement("div");
			row.className = "sel-row";
			const copy = document.createElement("div");
			copy.className = "grow";
			const name = document.createElement("div");
			name.className = "nm";
			name.textContent = label;
			copy.appendChild(name);
			if (subtitle) {
				const sub = document.createElement("div");
				sub.className = "sub";
				sub.textContent = subtitle;
				copy.appendChild(sub);
			}
			row.append(copy, control);
			list.appendChild(row);
		};
		const toggle = function(label, checked, callback) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "sel-pill stonks-admin-toggle " + (checked ? "is-buy" : "is-muted");
			button.setAttribute("role", "switch");
			button.setAttribute("aria-checked", checked ? "true" : "false");
			button.textContent = checked ? "On" : "Off";
			button.addEventListener("click", callback);
			appendRow(label, button);
		};
		const readonlyPill = function(text, muted) {
			const pill = document.createElement("span");
			pill.className = "sel-pill" + (muted ? " is-muted" : "");
			pill.textContent = text;
			return pill;
		};
		toggle("Enable Stonks", stonks.enabled !== false, function() {
			sendConfig({ enabled: stonks.enabled === false });
		});
		appendRow("Server-wide leaderboard", readonlyPill("On"));
		toggle("Social announcements", stonks.socialAnnouncementsEnabled !== false, function() {
			sendConfig({ socialAnnouncementsEnabled: stonks.socialAnnouncementsEnabled === false });
		});
		appendRow("Quote source", readonlyPill(stonks.disableQuoteLookup ? "Automation feed" : "Delayed feed", true));
		const select = document.createElement("select");
		select.className = "stonks-admin-select";
		const none = document.createElement("option");
		none.value = "0";
		none.textContent = "Auto #stonks";
		select.appendChild(none);
		(stonks.textChannels || []).forEach(function(channel) {
			const option = document.createElement("option");
			option.value = String(channel.textChannelId || 0);
			option.textContent = "#" + (channel.name || channel.textChannelId);
			select.appendChild(option);
		});
		if (currentTextChannelId > 0 && !configuredChannel) {
			const missing = document.createElement("option");
			missing.value = String(currentTextChannelId);
			missing.textContent = "#" + currentTextChannelId;
			select.appendChild(missing);
		}
		select.value = String(currentTextChannelId);
		select.addEventListener("change", function() {
			sendConfig({ textChannelId: Number(select.value || 0) });
		});
		appendRow("Text room", select);
		card.append(head, list);
		parent.appendChild(card);
	}

	function appendStonksTabs(parent, stonks) {
		const tabs = [["market", "Overview"], ["portfolio", "Portfolio"], ["leaderboard", "Leaderboard"], ["following", "Following"], ["audit", "Audit"]];
		if (stonks.canAdmin) {
			tabs.push(["admin", "Admin"]);
		}
		if (!tabs.some(function(tab) { return tab[0] === stonksActiveTab; })) {
			stonksActiveTab = "market";
		}
		const wrap = document.createElement("div");
		wrap.className = "stonks-tabs";
		tabs.forEach(function(tab) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "stonks-tab" + (stonksActiveTab === tab[0] ? " is-on is-selected" : "");
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
		submit.textContent = "Save portfolio";
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
			invokeModernDialogAction("clearPortfolio", {
				userId: ownerUserId,
				currency: stonksDraftCurrency || latest && latest.currency || "USD",
				note: "Portfolio cleared"
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
		const description = document.createElement("div");
		description.className = "stonks-leaderboard-description";
		description.textContent = "Portfolio updates are kept here for review.";
		const owner = document.createElement("span");
		owner.textContent = stonksSelectedUserName(stonks);
		description.appendChild(owner);
		parent.appendChild(description);

		const list = document.createElement("div");
		list.className = "stonks-list";
		snapshots.forEach(function(snapshot) {
			const details = document.createElement("details");
			details.className = "stonks-ledger-item";
			const summary = document.createElement("summary");
			summary.textContent = formatStonksMoney(snapshot.totalValue, snapshot.currency) + " - updated " + formatStonksTime(snapshot.createdAt);
			details.appendChild(summary);
			(snapshot.positions || []).forEach(function(position) {
				const row = document.createElement("div");
				row.className = "stonks-ledger-position";
				const quote = stonksSourceLabel(position);
				row.textContent = [
					position.symbol,
					position.displayName,
					formatStonksMoney(position.marketValue, position.currency),
					quote
				].filter(Boolean).join(" - ");
				details.appendChild(row);
			});
			if (snapshot.note) {
				const note = document.createElement("p");
				note.className = "stonks-note";
				note.textContent = snapshot.note;
				details.appendChild(note);
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
			|| "Snapshot return compares the latest accepted snapshot with the closest older snapshot for this period. It updates when users submit new snapshots.";
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
			main.querySelector("span").textContent = "Latest " + formatStonksTime(row.endSnapshotAt);
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
			item.append(main, percent, follow);
			list.appendChild(item);
		});
		if (!list.children.length) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = "No ranked snapshots for this period yet.";
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
	}

	function renderStonksDialog(dialog) {
		const stonks = dialog.stonks || {};
		ensureStonksDraft(stonks);
		const marketRows = stonksMarketRows(stonks);
		syncStonksDialogHeader(stonks, marketRows);
		appendStonksStatus(refs.body, stonks);
		if (!stonks.supported || (stonks.enabled === false && !stonks.canAdmin)) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = stonks.error || "Stonks is unavailable on this server.";
			refs.body.appendChild(empty);
			return;
		}
		if (stonks.loading) {
			appendStonksLoadingSkeleton(refs.body);
			return;
		}
		if (stonks.enabled === false && stonks.canAdmin) {
			stonksActiveTab = "admin";
		}
		if (!stonks.registered && stonks.enabled !== false) {
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
			refs.body.appendChild(needed);
		}
		appendStonksTabs(refs.body, stonks);
		if (stonks.enabled !== false) {
			appendStonksPins(refs.body, stonks, marketRows);
			appendStonksTickerTapeControls(refs.body, stonks);
		}
		const panel = document.createElement("section");
		panel.className = "stonks-panel";
		if (stonks.enabled === false && stonks.canAdmin) {
			appendStonksAdminTab(panel, stonks);
		} else if (stonksActiveTab === "portfolio") {
			appendStonksPortfolioTab(panel, stonks, marketRows);
		} else if (stonksActiveTab === "leaderboard") {
			appendStonksLeaderboardTab(panel, stonks, marketRows);
		} else if (stonksActiveTab === "following") {
			appendStonksFollowingTab(panel, stonks, marketRows);
		} else if (stonksActiveTab === "audit") {
			appendStonksAuditTab(panel, stonks);
		} else if (stonksActiveTab === "admin" && stonks.canAdmin) {
			appendStonksAdminTab(panel, stonks);
		} else {
			appendStonksMarketTab(panel, stonks, marketRows);
		}
		refs.body.appendChild(panel);
		const foot = document.createElement("footer");
		foot.className = "stonks-foot";
		const note = document.createElement("span");
		note.className = "stonks-foot-note";
		note.textContent = "Delayed quotes · not financial advice";
		const clock = document.createElement("span");
		clock.className = "stonks-foot-note";
		clock.textContent = new Date().toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
		foot.append(note, clock);
		refs.body.appendChild(foot);
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
				const title = document.createElement("h2");
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

	function appendModernDialogTabs(container, dialog) {
		if (!Array.isArray(dialog.pages) || !dialog.pages.length) {
			return;
		}

		const tabs = document.createElement("div");
		const settingsTabs = dialog && dialog.kind === "settings";
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

	function renderConnectDialog(dialog) {
		const grid = document.createElement("div");
		grid.className = "modern-dialog-connect-shell" + (dialog.editorOpen ? " has-editor" : "");

		grid.appendChild(createModernDialogFavorites(dialog));

		if (dialog.editorOpen) {
			const details = document.createElement("div");
			details.className = "modern-dialog-connect-details";
			const detailsTitle = document.createElement("h2");
			detailsTitle.className = "modern-dialog-connect-details-title";
			detailsTitle.textContent = dialog.editorTitle || "Server";
			details.appendChild(detailsTitle);
			appendModernDialogSections(details, dialog.sections || [], dialog.errors || {}, dialog);
			grid.appendChild(details);
		}

		refs.body.appendChild(grid);
	}

	function renderGenericDialog(dialog) {
		appendModernDialogTabs(refs.body, dialog);
		appendModernDialogHighlights(refs.body, dialog);
		appendModernDialogAdvancedToggle(refs.body, dialog);
		if (dialog.loading) {
			appendModernDialogLoadingScaffold(refs.body, dialog);
			return;
		}
		appendModernDialogSections(refs.body, dialog.sections || [], dialog.errors || {}, dialog);
	}

	function renderSettingsDialog(dialog) {
		const selectedPage = modernSettingsSelectedPage(dialog);
		const pageId = String(selectedPage.id || dialog.activePage || "");
		const meta = modernSettingsPageMeta(pageId);
		const showAdvanced = modernDialogAdvancedVisible(dialog);
		const hiddenAdvancedCount = showAdvanced ? 0 : modernDialogAdvancedContentCount(dialog);

		appendModernDialogTabs(refs.body, dialog);

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
		refs.body.appendChild(content);
	}

	function renderModernDialogActions(dialog) {
		(dialog.actions || []).forEach(function(action) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "chip-button modern-dialog-action"
				+ (action.tone ? " is-" + action.tone : "")
				+ (dialog.primaryActionId === action.id ? " is-primary" : "");
			button.disabled = action.enabled === false;
			button.textContent = action.label || action.id || "Action";
			button.addEventListener("click", function() {
				invokeModernDialogAction(action.id || "", {});
			});
			refs.actions.appendChild(button);
		});
	}

	function modernDialogRenderKey(dialog) {
		const stonks = dialog && dialog.stonks || {};
		return [
			dialog && dialog.id || "",
			dialog && dialog.kind || "",
			dialog && dialog.title || "",
			dialog && dialog.subtitle || "",
			dialog && dialog.tone || "",
			dialog && dialog.loading ? "loading" : "",
			dialog && dialog.loadingScaffold || "",
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
		if (!refs.layer || !refs.body || !refs.actions || !refs.dialog) {
			return;
		}
		if (open) {
			applyModernUiTweaks(resolvedModernDialogUiTweaks(dialog));
		}
		clearModernDialogFavoriteClickTimer();
		closeModernDialogFavoriteMenu();
		closeModernDialogSelect();

		const activeElement = document.activeElement;
		const activeInDialog = !!(activeElement && refs.dialog && refs.dialog.contains(activeElement));
		const focusState = activeInDialog && activeElement.dataset
			? {
				fieldId: activeElement.dataset.modernDialogFieldId || "",
				hasSelection: typeof activeElement.selectionStart === "number"
					&& typeof activeElement.selectionEnd === "number",
				selectionStart: activeElement.selectionStart,
				selectionEnd: activeElement.selectionEnd
			}
			: null;
		refs.layer.classList.toggle("hidden", !open);
		refs.layer.setAttribute("aria-hidden", open ? "false" : "true");
		if (!open) {
			refs.body.innerHTML = "";
			refs.actions.innerHTML = "";
			modernDialogRenderedOpen = false;
			modernDialogLastRenderKey = "";
			modernSettingsScrollPositions = {};
			modernSettingsRenderedScrollKey = "";
			syncAudioInputMeterTimer();
			return;
		}

		const opening = !modernDialogRenderedOpen;
		const renderKey = modernDialogRenderKey(dialog);
		const shouldResetBodyScroll = opening || renderKey !== modernDialogLastRenderKey;
		rememberRenderedModernSettingsScrollPosition();
		const settingsScrollKey = dialog.kind === "settings" ? modernSettingsScrollKey(dialog) : "";
		refs.dialog.className = "modern-dialog" + (dialog.tone ? " is-" + dialog.tone : "")
			+ (dialog.kind ? " is-" + dialog.kind : "")
			+ (dialog.kind === "connect" && dialog.editorOpen ? " has-connect-editor" : "")
			+ (dialog.id ? " dialog-id-" + String(dialog.id).replace(/[^a-z0-9_-]/gi, "-") : "");
		clearStonksDialogHeader();
		refs.eyebrow.textContent = dialog.eyebrow || (dialog.kind === "settings" ? "Settings" : "Mumble");
		refs.title.textContent = dialog.title || "Dialog";
		refs.subtitle.textContent = dialog.subtitle || "";
		refs.body.innerHTML = "";
		refs.actions.innerHTML = "";

		if (dialog.kind === "stonks") {
			renderStonksDialog(dialog);
		} else if (dialog.kind === "connect") {
			renderConnectDialog(dialog);
		} else if (dialog.kind === "settings") {
			renderSettingsDialog(dialog);
		} else {
			renderGenericDialog(dialog);
		}
		if (dialog.kind !== "settings" && dialog.kind !== "stonks") {
			renderModernDialogActions(dialog);
		}
		enhanceModernDialogSelects(refs.body);
		syncAudioInputMeterTimer();
		if (shouldResetBodyScroll) {
			refs.body.scrollTop = 0;
		}
		modernSettingsRenderedScrollKey = settingsScrollKey;
		restoreModernSettingsScrollPosition(settingsScrollKey);
		modernDialogLastRenderKey = renderKey;

		modernDialogRenderedOpen = true;
		if (pendingModernDialogFieldFocus) {
			const requestedField = pendingModernDialogFieldFocus;
			pendingModernDialogFieldFocus = "";
			if (focusModernDialogField(requestedField)) {
				return;
			}
		}
		if (focusState && restoreModernDialogFocus(focusState)) {
			return;
		}
		if (opening || !activeInDialog) {
			focusFirstModernDialogControl();
		}
	}

	/* MUMBLE_MODERN_AUTOMATION_BEGIN */
	function automationModernDialogFieldElement(fieldId) {
		const targetId = String(fieldId || "");
		const root = refs.dialog || document.getElementById("modern-dialog") || document;
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
		const valueElement = modernDialogValueElement(element);
		const root = refs.dialog || document.getElementById("modern-dialog") || document;
		const dialog = modernDialogState || {};
		const field = findModernDialogField(dialog, fieldId);
		const type = String(field && field.type || (valueElement && valueElement.type) || "");
		const value = valueElement
			? (type === "checkbox" ? !!valueElement.checked : String(valueElement.value || ""))
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
		const valueElement = modernDialogValueElement(element);
		const setOptions = options || {};
		const field = findModernDialogField(modernDialogState, fieldId);
		const type = String(field && field.type || valueElement.type || "");
		if (setOptions.focus !== false && typeof element.focus === "function") {
			element.focus({ preventScroll: true });
		}
		if (type === "checkbox") {
			valueElement.checked = !!value;
			valueElement.dispatchEvent(new Event("change", { bubbles: true }));
		} else {
			valueElement.value = value == null ? "" : String(value);
			if (typeof valueElement.setSelectionRange === "function") {
				try {
					const end = valueElement.value.length;
					valueElement.setSelectionRange(end, end);
				} catch (error) {
					// Some input types expose selection APIs but reject range updates.
				}
			}
			valueElement.dispatchEvent(new Event(type === "select" || type === "range" ? "change" : "input", {
				bubbles: true
			}));
		}
		return automationModernDialogFieldState(fieldId);
	}

	if (modernAutomationEnabled()) {
		window.__mumbleModernDialogFieldState = automationModernDialogFieldState;
		window.__mumbleModernSetDialogFieldValue = automationSetModernDialogFieldValue;
	}
	/* MUMBLE_MODERN_AUTOMATION_END */

	function wireActions() {
		if (refs.closeButton) {
			refs.closeButton.addEventListener("click", closeModernDialog);
		}
		if (refs.backdrop) {
			refs.backdrop.addEventListener("click", function() {
				if (!modernDialogState || !modernDialogState.preventBackdropClose) {
					closeModernDialog();
				}
			});
		}
		window.addEventListener("click", function(event) {
			if (modernDialogFavoriteMenu && !modernDialogFavoriteMenu.contains(event.target)) {
				closeModernDialogFavoriteMenu();
			}
			if (modernDialogSelectState
					&& !modernDialogSelectState.shell.contains(event.target)
					&& !modernDialogSelectState.menu.contains(event.target)) {
				closeModernDialogSelect();
			}
		});
		window.addEventListener("resize", positionModernDialogSelectMenu);
		window.addEventListener("scroll", positionModernDialogSelectMenu, true);
		window.addEventListener("blur", function() {
			closeModernDialogFavoriteMenu();
			closeModernDialogSelect();
		});
		window.addEventListener("keydown", function(event) {
			if (!modernDialogState || !modernDialogState.open) {
				return;
			}
			if (event.key === "Escape") {
				event.preventDefault();
				if (modernDialogSelectState) {
					closeModernDialogSelect();
					return;
				}
				if (modernDialogFavoriteMenu) {
					closeModernDialogFavoriteMenu();
					return;
				}
				closeModernDialog();
				return;
			}
			if (event.key === "Tab") {
				trapModernDialogTab(event);
			}
		});
	}

	async function boot() {
		wireActions();
		await ensureBridge();
	}

	boot();
})();
