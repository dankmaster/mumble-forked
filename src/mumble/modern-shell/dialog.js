(function() {
	"use strict";

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let modernDialogAdvancedPages = {};
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
	let modernDialogFavoriteMenu = null;
	let modernDialogFavoriteClickTimer = 0;
	let pendingModernDialogFieldFocus = "";
	let modernDialogSelectState = null;
	let stonksActiveTab = "overview";
	let stonksDraftPositions = null;
	let stonksDraftNote = "";
	let stonksDraftCurrency = "USD";
	let stonksQuoteSearchText = "";
	let stonksQuoteSearchBusy = false;
	let stonksQuoteSearchError = "";
	let stonksQuoteSuggestions = [];
	let stonksQuoteSearchRequestId = "";
	const bridgeRetryDelayMs = 50;
	const bridgeRetryLimit = 160;
	let bridgeRetryTimer = 0;
	let bridgeRetryCount = 0;

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

	function syncModernDialogState(state) {
		modernDialogState = state || null;
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
		const target = firstField || focusable[0] || refs.dialog;
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
	}

	function invokeModernDialogAction(actionId, payload) {
		const dialogId = String(modernDialogState && modernDialogState.id || "");
		if (!dialogId || !actionId) {
			return;
		}
		notifyBridge("invokeModernDialogAction", dialogId, String(actionId), payload || {});
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

	function modernDialogFieldValue(dialog, fieldId) {
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

		const value = modernDialogFieldValue(dialog, rule.fieldId);
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
			subtitle.textContent = (item.type === "user" ? "User" : "Room") + (item.subtitle ? " - " + item.subtitle : "");
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
	}

	function ensureStonksDraft(stonks) {
		if (!Array.isArray(stonksDraftPositions)) {
			seedStonksDraft(stonks || {});
		}
	}

	function stonksDraftTotal() {
		return stonksDraftTotalFromPositions(stonksDraftPositions || []);
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
			: "Ready to submit";
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
		const tabs = [["overview", "Overview"], ["ledger", "Ledger"], ["leaderboard", "Leaderboard"], ["following", "Following"]];
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

	function appendStonksOverview(parent, stonks) {
		const snapshots = Array.isArray(stonks.snapshots) ? stonks.snapshots : [];
		const latest = snapshots.length ? snapshots[0] : null;
		const stats = document.createElement("div");
		stats.className = "stonks-stat-grid";
		[["Total value", latest ? formatStonksMoney(latest.totalValue, latest.currency) : "-"],
		 ["Last snapshot", latest ? formatStonksTime(latest.createdAt) : "-"],
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
		const quick = document.createElement("button");
		quick.type = "button";
		quick.className = "chip-button is-primary";
		quick.textContent = "Quick snapshot";
		quick.disabled = !stonks.registered;
		quick.addEventListener("click", function() {
			if (!refs.body.querySelector(".stonks-position-row")) {
				stonksActiveTab = "overview";
				renderModernDialog();
				return;
			}
			const draft = stonksCollectDraft(refs.body);
			draft.registered = stonks.registered;
			if (stonksValidateDraft(draft).errors.length) {
				stonksActiveTab = "overview";
				renderModernDialog();
				return;
			}
			invokeModernDialogAction("submitSnapshot", draft);
		});
		parent.appendChild(quick);
	}

	function appendStonksEditor(parent, stonks) {
		ensureStonksDraft(stonks);
		const editor = document.createElement("section");
		editor.className = "stonks-editor";
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
				liveDraft.registered = stonks.registered;
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
		submit.textContent = "Submit snapshot";
		submit.disabled = !stonks.registered;
		submit.addEventListener("click", function() {
			const draft = stonksCollectDraft(editor);
			draft.registered = stonks.registered;
			if (stonksValidateDraft(draft).errors.length) {
				updateStonksValidation(validationSummary, draft, submit, total);
				return;
			}
			invokeModernDialogAction("submitSnapshot", draft);
		});
		actions.append(add, total, submit);
		editor.appendChild(actions);
		const validationSummary = document.createElement("div");
		const draft = { positions: stonksDraftPositions || [], currency: stonksDraftCurrency, registered: stonks.registered };
		updateStonksValidation(validationSummary, draft, submit, total);
		editor.appendChild(validationSummary);
		parent.appendChild(editor);
	}

	function appendStonksLedger(parent, stonks) {
		appendStonksEditor(parent, stonks);
		const list = document.createElement("div");
		list.className = "stonks-list";
		(stonks.snapshots || []).forEach(function(snapshot) {
			const details = document.createElement("details");
			details.className = "stonks-ledger-item";
			const summary = document.createElement("summary");
			summary.textContent = formatStonksMoney(snapshot.totalValue, snapshot.currency) + " - " + formatStonksTime(snapshot.createdAt);
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
			empty.textContent = "No snapshots yet.";
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
		refs.eyebrow.textContent = "Stonks";
		appendStonksStatus(refs.body, stonks);
		if (!stonks.supported || stonks.enabled === false) {
			const empty = document.createElement("div");
			empty.className = "stonks-empty";
			empty.textContent = stonks.error || "Stonks is unavailable on this server.";
			refs.body.appendChild(empty);
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
			refs.body.appendChild(needed);
		}
		appendStonksTabs(refs.body, stonks);
		const panel = document.createElement("section");
		panel.className = "stonks-panel";
		if (stonksActiveTab === "ledger") {
			appendStonksLedger(panel, stonks);
		} else if (stonksActiveTab === "leaderboard") {
			appendStonksOverview(panel, stonks);
			appendStonksLeaderboard(panel, stonks);
		} else if (stonksActiveTab === "following") {
			appendStonksFollowing(panel, stonks);
		} else if (stonksActiveTab === "admin" && stonks.canAdmin) {
			appendStonksAdmin(panel, stonks);
		} else {
			appendStonksOverview(panel, stonks);
			appendStonksEditor(panel, stonks);
		}
		refs.body.appendChild(panel);
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
		appendModernDialogSections(refs.body, dialog.sections || [], dialog.errors || {}, dialog);
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

	function renderModernDialog() {
		const dialog = modernDialogState || {};
		const open = !!dialog.open;
		if (!refs.layer || !refs.body || !refs.actions || !refs.dialog) {
			return;
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
			syncAudioInputMeterTimer();
			return;
		}

		const opening = !modernDialogRenderedOpen;
		refs.dialog.className = "modern-dialog" + (dialog.tone ? " is-" + dialog.tone : "")
			+ (dialog.kind ? " is-" + dialog.kind : "")
			+ (dialog.id ? " dialog-id-" + String(dialog.id).replace(/[^a-z0-9_-]/gi, "-") : "");
		refs.eyebrow.textContent = dialog.eyebrow || (dialog.kind === "settings" ? "Settings" : "Mumble");
		refs.title.textContent = dialog.title || "Dialog";
		refs.subtitle.textContent = dialog.subtitle || "";
		refs.body.innerHTML = "";
		refs.actions.innerHTML = "";

		if (dialog.kind === "stonks") {
			renderStonksDialog(dialog);
		} else if (dialog.kind === "connect") {
			renderConnectDialog(dialog);
		} else {
			renderGenericDialog(dialog);
		}
		renderModernDialogActions(dialog);
		enhanceModernDialogSelects(refs.body);
		syncAudioInputMeterTimer();

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

	function wireActions() {
		if (refs.closeButton) {
			refs.closeButton.addEventListener("click", closeModernDialog);
		}
		if (refs.backdrop) {
			refs.backdrop.addEventListener("click", closeModernDialog);
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
