(function() {
	"use strict";

	const allowedThemes = ["dark", "light", "engine", "mocha", "macchiato", "frappe", "latte", "nord", "gruvbox"];
	const allowedAccents = ["auto", "teal", "blue", "violet", "amber", "rose"];
	const allowedDensities = ["compact", "comfortable", "spacious"];
	const allowedUserIcons = ["avatars", "classic"];
	const allowedRailSides = ["left", "right"];
	const accentPalette = {
		teal: { accent: "#5ec8b0", rgb: "94, 200, 176", soft: "rgba(94, 200, 176, 0.16)", border: "rgba(94, 200, 176, 0.42)" },
		blue: { accent: "#73b7ff", rgb: "115, 183, 255", soft: "rgba(115, 183, 255, 0.16)", border: "rgba(115, 183, 255, 0.42)" },
		violet: { accent: "#b59cff", rgb: "181, 156, 255", soft: "rgba(181, 156, 255, 0.16)", border: "rgba(181, 156, 255, 0.42)" },
		amber: { accent: "#f2c76f", rgb: "242, 199, 111", soft: "rgba(242, 199, 111, 0.16)", border: "rgba(242, 199, 111, 0.42)" },
		rose: { accent: "#ff8aa0", rgb: "255, 138, 160", soft: "rgba(255, 138, 160, 0.16)", border: "rgba(255, 138, 160, 0.42)" }
	};
	let lastAppliedTweaks = null;
	let lastAppliedThemeTokenNames = [];

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

	function applyAccent(root, accent) {
		const palette = accent === "auto" ? null : accentPalette[accent];
		if (palette) {
			root.style.setProperty("--accent", palette.accent);
			root.style.setProperty("--accent-rgb", palette.rgb);
			root.style.setProperty("--accent-soft", palette.soft);
			root.style.setProperty("--accent-border", palette.border);
			root.style.setProperty("--on-accent", contrastTextForAccent(palette.accent));
			return;
		}

		root.style.removeProperty("--accent");
		root.style.removeProperty("--accent-rgb");
		root.style.removeProperty("--accent-soft");
		root.style.removeProperty("--accent-border");
		root.style.removeProperty("--on-accent");
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
		const normalized = normalizedTweaks(uiTweaks);
		lastAppliedTweaks = normalized;

		const root = document.documentElement;
		if (root) {
			root.dataset.theme = normalized.theme;
			root.dataset.accent = normalized.accent;
			applyThemeTokens(root, normalized.themeTokens);
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
		normalizedTweaks: normalizedTweaks,
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
