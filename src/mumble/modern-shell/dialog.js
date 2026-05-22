(function() {
	"use strict";

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let modernDialogState = null;
	let modernDialogRenderedOpen = false;
	let audioInputMeterTimer = 0;
	let voiceCalibrationState = null;
	let voiceCalibrationSummary = null;
	let voiceReplayStopTimer = 0;
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
		candidates.sort(function(left, right) {
			return right.score - left.score;
		});
		let selected = candidates[0];
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
		const currentStrength = clampNumber(calibration.element.dataset.speexNoiseStrength, 0, 100, 14);
		const neuralAvailable = calibration.element.dataset.neuralCleanupAvailable === "true";
		const quietAmpCeiling = percentile(calibration.quietAmplitude, 0.85) || 0;
		const quietAmpFloor = percentile(calibration.quietAmplitude, 0.15) || 0;
		const quietSpeechProb = percentile(calibration.quietSignalToNoise, 0.85) || 0;
		const roomJitter = Math.max(selected.jitter || 0, quietAmpCeiling - quietAmpFloor);
		let noiseCancelMode = currentMode;
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

		return {
			noiseCancelMode: Math.round(clampNumber(noiseCancelMode, 0, 3, currentMode)),
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
			meter.dataset.speexNoiseStrength = String(field.speexNoiseStrength || 14);
			meter.dataset.neuralCleanupAvailable = field.neuralCleanupAvailable ? "true" : "false";
			meter.dataset.recommendedVadSource = String(field.recommendedVadSource == null ? 1 : field.recommendedVadSource);
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

			const stats = document.createElement("span");
			stats.className = "modern-dialog-favorite-stats";
			const users = document.createElement("span");
			users.className = "modern-dialog-favorite-stat";
			users.textContent = favorite.usersLabel || "Users: -";
			const ping = document.createElement("span");
			ping.className = "modern-dialog-favorite-stat";
			ping.textContent = favorite.pingLabel || "Ping: -";
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
		grid.className = "modern-dialog-connect-shell" + (dialog.editorOpen ? " has-editor" : "");

		grid.appendChild(createModernDialogFavorites(dialog));

		if (dialog.editorOpen) {
			const details = document.createElement("div");
			details.className = "modern-dialog-connect-details";
			const detailsTitle = document.createElement("h2");
			detailsTitle.className = "modern-dialog-connect-details-title";
			detailsTitle.textContent = dialog.editorTitle || "Server";
			details.appendChild(detailsTitle);
			appendModernDialogSections(details, dialog.sections || [], dialog.errors || {});
			grid.appendChild(details);
		}

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
