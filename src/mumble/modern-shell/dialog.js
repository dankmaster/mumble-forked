(function() {
	"use strict";

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let audioInputMeterTimer = 0;
	let modernDialogFavoriteMenu = null;
	let modernDialogFavoriteClickTimer = 0;
	let pendingModernDialogFieldFocus = "";

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

	async function ensureBridge() {
		if (!window.qt || !window.qt.webChannelTransport || modernBridge) {
			return;
		}

		async function bindBridge() {
			return new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						modernBridge = channel.objects.modernBridge || null;
						if (modernBridge) {
							if (modernBridge.modernDialogStateChanged
									&& typeof modernBridge.modernDialogStateChanged.connect === "function") {
								modernBridge.modernDialogStateChanged.connect(syncModernDialogState);
							}
							syncModernDialogState(modernBridge.modernDialogState || { open: false });
						}
						resolve();
					});
				} catch (error) {
					console.warn("Modern dialog bridge initialization failed:", error);
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
					console.warn("Unable to load qwebchannel.js for the modern dialog.");
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
		addFavoriteMenuButton(menu, "Edit", "selectFavorite", favorite, { focusField: "name" });
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

	function setRangeProgress(input) {
		const min = Number(input.min || 0);
		const max = Number(input.max || 100);
		const value = Number(input.value || 0);
		const progress = max > min ? ((value - min) / (max - min)) * 100 : 0;
		input.style.setProperty("--range-progress", String(Math.max(0, Math.min(100, progress))) + "%");
	}

	function updateVoiceMeterElement(element, payload) {
		const meter = payload || {};
		const source = Number(element.dataset.vadSource || 0);
		const silenceThreshold = clampPercent(element.dataset.silenceThreshold);
		const speechThreshold = clampPercent(element.dataset.speechThreshold);
		const active = element.dataset.active === "true";
		const rawLevel = source === 1 ? meter.signalToNoise : meter.amplitude;
		const available = !!meter.available && Number.isFinite(Number(rawLevel));
		const level = available ? clampPercent(rawLevel) : 0;
		const levelText = element.querySelector(".modern-dialog-voice-meter-value");
		const statusText = element.querySelector(".modern-dialog-voice-meter-status");

		element.style.setProperty("--voice-level", String(level) + "%");
		element.style.setProperty("--silence-threshold", String(silenceThreshold) + "%");
		element.style.setProperty("--speech-threshold", String(speechThreshold) + "%");
		element.classList.toggle("is-unavailable", !available);
		element.classList.toggle("is-inactive", !active);
		element.classList.toggle("is-transmitting", !!meter.transmitting);

		if (levelText) {
			levelText.textContent = available ? String(level) + "%" : "No signal";
		}
		if (statusText) {
			if (!active) {
				statusText.textContent = "Voice Activity is not selected";
			} else if (!available) {
				statusText.textContent = "Input inactive";
			} else {
				statusText.textContent = meter.transmitting ? "Transmitting" : "Listening";
			}
		}
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
		}
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

		const row = document.createElement("label");
		row.className = "modern-dialog-field is-" + type;
		const label = document.createElement("span");
		label.className = "modern-dialog-field-label";
		label.textContent = field.label || field.id || "Field";
		row.appendChild(label);

		let input = null;
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
			(field.options || []).forEach(function(option) {
				const item = document.createElement("option");
				item.value = String(option.value);
				item.textContent = option.label || String(option.value);
				item.disabled = option.enabled === false;
				if (option.hint) {
					item.title = String(option.hint);
				}
				input.appendChild(item);
			});
			input.value = String(field.value);
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

			const header = document.createElement("div");
			header.className = "modern-dialog-voice-meter-header";
			const source = document.createElement("span");
			source.className = "modern-dialog-voice-meter-source";
			source.textContent = field.sourceLabel || "Input";
			const currentValue = document.createElement("span");
			currentValue.className = "modern-dialog-voice-meter-value";
			header.appendChild(source);
			header.appendChild(currentValue);

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

			meter.appendChild(header);
			meter.appendChild(track);
			meter.appendChild(footer);
			updateVoiceMeterElement(meter, null);
			row.appendChild(meter);
		} else if (type === "textarea") {
			input = document.createElement("textarea");
			input.value = field.value == null ? "" : String(field.value);
			input.rows = Number(field.rows) || 4;
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

		if (input && type !== "range") {
			row.appendChild(input);
		}
		if (input) {
			input.dataset.modernDialogFieldId = String(field.id || "");
			input.disabled = field.enabled === false;
			if (field.hint) {
				input.title = String(field.hint);
			}
			input.addEventListener(type === "checkbox" || type === "select" || type === "range" ? "change" : "input", function() {
				updateModernDialogField(field, input);
			});
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
		if (favorites.length) {
			const addButton = document.createElement("button");
			addButton.type = "button";
			addButton.className = "chip-button modern-dialog-favorites-add";
			addButton.textContent = "Add server";
			addButton.addEventListener("click", function() {
				requestModernDialogFieldFocus("host");
				invokeModernDialogAction("newFavorite", {});
			});
			header.appendChild(addButton);
		}
		section.appendChild(header);

		const list = document.createElement("div");
		list.className = "modern-dialog-favorite-list";
		favorites.forEach(function(favorite) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "modern-dialog-favorite" + (favorite.selected ? " is-selected" : "");
			button.innerHTML = "<span class=\"modern-dialog-favorite-label\"></span><span class=\"modern-dialog-favorite-subtitle\"></span>";
			button.querySelector(".modern-dialog-favorite-label").textContent = favorite.label || favorite.host || "Server";
			button.querySelector(".modern-dialog-favorite-subtitle").textContent = favorite.subtitle || "";
			button.title = "Double-click to connect. Right-click for actions.";
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
			const emptyButton = document.createElement("button");
			emptyButton.type = "button";
			emptyButton.className = "chip-button modern-dialog-favorite-empty-button";
			emptyButton.textContent = "Click here to add your first server!";
			emptyButton.addEventListener("click", function() {
				requestModernDialogFieldFocus("host");
				invokeModernDialogAction("newFavorite", {});
			});
			empty.appendChild(emptyButton);
			list.appendChild(empty);
		}

		section.appendChild(list);
		return section;
	}

	function appendModernDialogSections(container, sections, errors) {
		(sections || []).forEach(function(section) {
			const sectionElement = document.createElement("section");
			sectionElement.className = "modern-dialog-section";
			if (section.title) {
				const title = document.createElement("h2");
				title.className = "modern-dialog-section-title";
				title.textContent = section.title;
				sectionElement.appendChild(title);
			}
			(section.fields || []).forEach(function(field) {
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
		grid.className = "modern-dialog-connect-grid";

		grid.appendChild(createModernDialogFavorites(dialog));

		const details = document.createElement("div");
		details.className = "modern-dialog-connect-details";
		appendModernDialogSections(details, dialog.sections || [], dialog.errors || {});
		grid.appendChild(details);

		refs.body.appendChild(grid);
	}

	function renderGenericDialog(dialog) {
		appendModernDialogTabs(refs.body, dialog);
		appendModernDialogSections(refs.body, dialog.sections || [], dialog.errors || {});
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
			+ (dialog.kind ? " is-" + dialog.kind : "");
		refs.eyebrow.textContent = dialog.kind === "settings" ? "Settings" : "Mumble";
		refs.title.textContent = dialog.title || "Dialog";
		refs.subtitle.textContent = dialog.subtitle || "";
		refs.body.innerHTML = "";
		refs.actions.innerHTML = "";

		if (dialog.kind === "connect") {
			renderConnectDialog(dialog);
		} else {
			renderGenericDialog(dialog);
		}
		renderModernDialogActions(dialog);
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
		});
		window.addEventListener("blur", closeModernDialogFavoriteMenu);
		window.addEventListener("keydown", function(event) {
			if (!modernDialogState || !modernDialogState.open) {
				return;
			}
			if (event.key === "Escape") {
				event.preventDefault();
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
