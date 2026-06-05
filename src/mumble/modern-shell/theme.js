(function() {
	"use strict";

	const fallbackThemes = ["dark", "light", "engine", "mocha", "macchiato", "frappe", "latte", "nord", "gruvbox"];
	const fallbackAccents = ["auto", "teal", "blue", "violet", "amber", "rose", "custom"];
	let allowedThemes = fallbackThemes.slice();
	let allowedAccents = fallbackAccents.slice();
	const allowedDensities = ["compact", "comfortable", "spacious"];
	const allowedUserIcons = ["avatars", "classic"];
	const allowedRailSides = ["left", "right"];
	const accentPalette = {};
	let lastAppliedTweaks = null;
	let lastAppliedThemeTokenNames = [];
	let lastAppliedAccentOverrideTokenNames = [];

	function normalizedToken(value, fallback, allowed) {
		const token = String(value || "").trim().toLowerCase();
		return allowed.indexOf(token) >= 0 ? token : fallback;
	}

	function booleanLike(value) {
		if (value === true) {
			return true;
		}
		if (value === false || value == null) {
			return false;
		}
		return /^(1|true|yes|on)$/i.test(String(value).trim());
	}

	function hexRgb(hex) {
		const match = /^#?([0-9a-f]{6})$/i.exec(String(hex || "").trim());
		if (!match) {
			return null;
		}
		const value = parseInt(match[1], 16);
		return {
			r: (value >> 16) & 255,
			g: (value >> 8) & 255,
			b: value & 255
		};
	}

	function relativeLuminance(channel) {
		const normalized = channel / 255;
		return normalized <= 0.03928
			? normalized / 12.92
			: Math.pow((normalized + 0.055) / 1.055, 2.4);
	}

	function contrastTextForAccent(hex) {
		const rgb = hexRgb(hex);
		if (!rgb) {
			return "#081210";
		}
		const luminance = (0.2126 * relativeLuminance(rgb.r))
			+ (0.7152 * relativeLuminance(rgb.g))
			+ (0.0722 * relativeLuminance(rgb.b));
		const contrastWithBlack = (luminance + 0.05) / 0.05;
		const contrastWithWhite = 1.05 / (luminance + 0.05);
		return contrastWithBlack >= contrastWithWhite ? "#081210" : "#ffffff";
	}

	function computedCssVariable(root, name) {
		if (!root || !window.getComputedStyle) {
			return "";
		}
		return window.getComputedStyle(root).getPropertyValue(name).trim();
	}

	function cssTokenList(root, name, fallback) {
		const value = computedCssVariable(root, name);
		const tokens = value.split(/\s+/).map(function(token) {
			return token.trim().toLowerCase();
		}).filter(function(token) {
			return /^[a-z0-9-]+$/i.test(token);
		});
		return tokens.length ? tokens : fallback.slice();
	}

	function refreshThemeMetadata(root) {
		allowedThemes = cssTokenList(root, "--theme-supported-themes", fallbackThemes);
		allowedAccents = cssTokenList(root, "--theme-supported-accents", fallbackAccents);
	}

	function accentPaletteEntryFromTokens(root, accent) {
		const prefix = "--theme-accent-" + accent;
		const color = computedCssVariable(root, prefix);
		const rgb = computedCssVariable(root, prefix + "-rgb");
		if (!color || !rgb) {
			return null;
		}
		return {
			accent: color,
			rgb: rgb,
			soft: computedCssVariable(root, prefix + "-soft") || ("rgba(" + rgb + ", 0.16)"),
			border: computedCssVariable(root, prefix + "-border") || ("rgba(" + rgb + ", 0.42)"),
			glow: computedCssVariable(root, prefix + "-glow") || ("rgba(" + rgb + ", 0.09)")
		};
	}

	function refreshAccentPalette(root) {
		allowedAccents.forEach(function(accent) {
			if (accent === "auto") {
				return;
			}
			const palette = accentPaletteEntryFromTokens(root, accent);
			if (palette) {
				accentPalette[accent] = palette;
			} else {
				delete accentPalette[accent];
			}
		});
	}

	function clearAccentOverride(root) {
		if (!root || !root.style) {
			return;
		}
		lastAppliedAccentOverrideTokenNames.forEach(function(name) {
			root.style.removeProperty(name);
		});
		lastAppliedAccentOverrideTokenNames = [];
	}

	function setAccentOverrideTokens(root, tokens) {
		Object.keys(tokens).forEach(function(name) {
			root.style.setProperty(name, tokens[name]);
			lastAppliedAccentOverrideTokenNames.push(name);
		});
	}

	function accentOverrideTokens(palette) {
		const selectedText = "color-mix(in srgb, var(--text-strong) 88%, var(--accent) 12%)";
		return {
			"--accent": palette.accent,
			"--accent-rgb": palette.rgb,
			"--accent-soft": palette.soft,
			"--accent-border": palette.border,
			"--accent-strong": "color-mix(in srgb, var(--accent) 78%, var(--text-strong) 22%)",
			"--accent-ink": "color-mix(in srgb, var(--accent) 72%, var(--text-strong) 28%)",
			"--body-bg-glow": palette.glow,
			"--chat-accent-surface": "color-mix(in srgb, var(--shell-panel-soft) 74%, var(--accent) 26%)",
			"--chat-accent-surface-strong": "color-mix(in srgb, var(--shell-panel-soft) 66%, var(--accent) 34%)",
			"--chat-accent-text": selectedText,
			"--chat-accent-muted": "color-mix(in srgb, var(--text-muted) 84%, var(--accent) 16%)",
			"--chat-accent-border": "color-mix(in srgb, var(--accent) 34%, var(--surface-border) 66%)",
			"--reply-bg": "var(--chat-accent-surface)",
			"--reply-text": "var(--chat-accent-text)",
			"--selected-bg": palette.soft,
			"--selected-text": selectedText,
			"--settings-selected-text": selectedText,
			"--scrollbar-thumb": "rgba(" + palette.rgb + ", 0.30)",
			"--scrollbar-thumb-hover": "rgba(" + palette.rgb + ", 0.46)",
			"--on-accent": contrastTextForAccent(palette.accent)
		};
	}

	function applyAccent(root, accent) {
		refreshAccentPalette(root);
		clearAccentOverride(root);
		const palette = accent === "auto" ? null : accentPalette[accent];
		if (palette) {
			setAccentOverrideTokens(root, accentOverrideTokens(palette));
			return;
		}
	}

	function safeCssVariableName(name) {
		const value = String(name || "").trim();
		return /^--[a-z0-9-]+$/i.test(value) ? value : "";
	}

	function safeCssVariableValue(value) {
		const text = String(value == null ? "" : value).trim();
		if (!text || /[;{}]/.test(text)) {
			return "";
		}
		return text;
	}

	function normalizedThemeTokens(tokens) {
		const source = tokens && typeof tokens === "object" ? tokens : {};
		const normalized = {};
		Object.keys(source).forEach(function(name) {
			const safeName = safeCssVariableName(name);
			const safeValue = safeCssVariableValue(source[name]);
			if (safeName && safeValue) {
				normalized[safeName] = safeValue;
			}
		});
		return normalized;
	}

	function applyThemeTokens(root, tokens) {
		if (!root || !root.style) {
			return;
		}

		clearAccentOverride(root);
		lastAppliedThemeTokenNames.forEach(function(name) {
			root.style.removeProperty(name);
		});
		lastAppliedThemeTokenNames = [];

		const normalized = normalizedThemeTokens(tokens);
		Object.keys(normalized).forEach(function(name) {
			root.style.setProperty(name, normalized[name]);
			lastAppliedThemeTokenNames.push(name);
		});
	}

	function normalizedTweaks(uiTweaks) {
		uiTweaks = uiTweaks || {};
		const classicUserIcons = booleanLike(uiTweaks.classicUserIcons);
		const requestedUserIcons = uiTweaks.userIcons || (classicUserIcons ? "classic" : "avatars");
		return {
			theme: normalizedToken(uiTweaks.theme, "dark", allowedThemes),
			accent: normalizedToken(uiTweaks.accent, "auto", allowedAccents),
			density: normalizedToken(uiTweaks.density, "comfortable", allowedDensities),
			userIcons: normalizedToken(requestedUserIcons, "avatars", allowedUserIcons),
			railSide: normalizedToken(uiTweaks.railSide, "right", allowedRailSides),
			themeTokens: normalizedThemeTokens(uiTweaks.themeTokens)
		};
	}

	function apply(uiTweaks) {
		uiTweaks = uiTweaks || {};
		const root = document.documentElement;
		const incomingThemeTokens = normalizedThemeTokens(uiTweaks.themeTokens);
		if (root) {
			applyThemeTokens(root, incomingThemeTokens);
			refreshThemeMetadata(root);
		}
		const normalized = normalizedTweaks(Object.assign({}, uiTweaks, { themeTokens: incomingThemeTokens }));
		lastAppliedTweaks = normalized;

		if (root) {
			root.dataset.theme = normalized.theme;
			root.dataset.accent = normalized.accent;
			refreshAccentPalette(root);
			if (normalized.accent !== "auto" || !Object.keys(normalized.themeTokens).length) {
				applyAccent(root, normalized.accent);
			}
		}

		if (document.body) {
			document.body.dataset.density = normalized.density;
			document.body.dataset.userIcons = normalized.userIcons;
			document.body.dataset.railSide = normalized.railSide;
		}

		return normalized;
	}

	function applyTokens(target, tokens) {
		const root = target && target.style ? target : document.documentElement;
		const normalized = normalizedThemeTokens(tokens);
		Object.keys(normalized).forEach(function(name) {
			root.style.setProperty(name, normalized[name]);
		});
		if (root === document.documentElement) {
			refreshThemeMetadata(root);
			if (root.dataset.accent && root.dataset.accent !== "auto") {
				applyAccent(root, root.dataset.accent);
			} else {
				refreshAccentPalette(root);
			}
		}
		return normalized;
	}

	function uiTweaksFromSearch(search) {
		const params = new URLSearchParams(search || "");
		const keys = ["theme", "accent", "density", "userIcons", "classicUserIcons", "railSide"];
		const tweaks = {};
		keys.forEach(function(key) {
			if (params.has(key)) {
				tweaks[key] = params.get(key);
			}
		});
		return tweaks;
	}

	function uiTweaksFromBootstrap() {
		const value = window.__mumbleModernInitialUiTweaks;
		return value && typeof value === "object" && !Array.isArray(value) ? value : {};
	}

	function applyInitialTweaks() {
		apply(Object.assign({}, uiTweaksFromBootstrap(), uiTweaksFromSearch(window.location.search)));
	}

	window.MumbleModernTheme = {
		accentPalette: accentPalette,
		apply: apply,
		applyTokens: applyTokens,
		normalizedTweaks: normalizedTweaks,
		normalizedThemeTokens: normalizedThemeTokens,
		supportedAccents: function() { return allowedAccents.slice(); },
		supportedThemes: function() { return allowedThemes.slice(); },
		uiTweaksFromBootstrap: uiTweaksFromBootstrap,
		uiTweaksFromSearch: uiTweaksFromSearch
	};

	applyInitialTweaks();
	if (document.readyState === "loading") {
		document.addEventListener("DOMContentLoaded", function() {
			apply(lastAppliedTweaks || {});
		}, { once: true });
	}
})();
