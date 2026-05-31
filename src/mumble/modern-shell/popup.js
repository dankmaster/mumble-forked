(function() {
	"use strict";

	let popupBridge = null;
	let bridgeLoadPromise = null;
	let pendingPayload = null;

	const menu = document.getElementById("context-menu");
	function applyPopupUiTweaks(uiTweaks) {
		if (window.MumbleModernTheme && typeof window.MumbleModernTheme.apply === "function") {
			window.MumbleModernTheme.apply(uiTweaks || {});
		}
	}

	function iconSvg(name) {
		switch (String(name || "")) {
			case "activity":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M3 12h4l3-7 4 14 3-7h4\"></path></svg>";
			case "ban":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"9\"></circle><path d=\"M7 17L17 7\"></path></svg>";
			case "bell":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M6 10a6 6 0 1112 0c0 4 1.5 5 1.5 5h-15S6 14 6 10\"></path><path d=\"M10 19a2 2 0 004 0\"></path></svg>";
			case "bug":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M8 8a4 4 0 018 0v8a4 4 0 01-8 0z\"></path><path d=\"M3 13h5\"></path><path d=\"M16 13h5\"></path><path d=\"M5 6l3 3\"></path><path d=\"M19 6l-3 3\"></path></svg>";
			case "chart":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 19V5\"></path><path d=\"M4 19h16\"></path><path d=\"M7 15l4-4 3 3 5-7\"></path></svg>";
			case "check":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4.5 12.5l5 5L20 7\"></path></svg>";
			case "copy":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"8\" y=\"8\" width=\"11\" height=\"11\" rx=\"2\"></rect><path d=\"M5 15V5h10\"></path></svg>";
			case "dot":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"currentColor\" stroke=\"none\"></circle></svg>";
			case "download":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 4v10\"></path><path d=\"M8 10l4 4 4-4\"></path><path d=\"M5 19h14\"></path></svg>";
			case "eye-off":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M3 3l18 18\"></path><path d=\"M10.5 10.5a2 2 0 002.8 2.8\"></path><path d=\"M9 5.5A9.8 9.8 0 0121 12a10.7 10.7 0 01-3 3.7\"></path><path d=\"M6.5 6.5A10.6 10.6 0 003 12a10.8 10.8 0 009.7 5.9\"></path></svg>";
			case "ghost":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M5.75 20V10.25A6.25 6.25 0 0112 4a6.25 6.25 0 016.25 6.25V20l-2.15-1.45L14.05 20 12 18.55 9.95 20 7.9 18.55 5.75 20z\"></path><circle cx=\"9.8\" cy=\"11\" r=\"1.35\" fill=\"currentColor\" stroke=\"none\"></circle><circle cx=\"14.2\" cy=\"11\" r=\"1.35\" fill=\"currentColor\" stroke=\"none\"></circle></svg>";
			case "hash":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M9 4L7 20\"></path><path d=\"M17 4l-2 16\"></path><path d=\"M4 9h16\"></path><path d=\"M3 15h16\"></path></svg>";
			case "headphones":
				return "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 14a8 8 0 0116 0\"></path><path d=\"M4 14v4a2 2 0 002 2h2v-6H4z\"></path><path d=\"M20 14v4a2 2 0 01-2 2h-2v-6h4z\"></path></svg>";
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
				return iconSvg("dot");
		}
	}

	function themedIconSvg(name) {
		return iconSvg(name).replace("<svg ",
			"<svg fill=\"none\" stroke=\"currentColor\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" ");
	}

	function setText(parent, selector, text) {
		const node = parent.querySelector(selector);
		if (node) {
			node.textContent = text || "";
		}
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

	function updateOpenSubmenuState() {
		if (!menu || !menu.classList) {
			return;
		}
		menu.classList.toggle("has-open-submenu", !!menu.querySelector(".context-menu-submenu.is-open"));
	}

	function roundedRectPayload(bounds) {
		return {
			left: Math.round(bounds.left),
			top: Math.round(bounds.top),
			width: Math.round(bounds.width),
			height: Math.round(bounds.height)
		};
	}

	function visibleMaskRects() {
		const rects = [];
		if (menu) {
			rects.push(roundedRectPayload(menu.getBoundingClientRect()));
		}
		const panels = document.querySelectorAll(".context-menu-submenu.is-open > .context-menu-submenu-panel");
		panels.forEach(function(panel) {
			const bounds = panel.getBoundingClientRect();
			if (bounds.width > 0 && bounds.height > 0) {
				rects.push(roundedRectPayload(bounds));
			}
		});
		return rects;
	}

	function reportPopupMask() {
		if (!window.qt || !window.qt.webChannelTransport) {
			return;
		}
		ensureBridge().then(function() {
			if (popupBridge && typeof popupBridge.updateMask === "function") {
				popupBridge.updateMask({ rects: visibleMaskRects() });
			}
		});
	}

	function schedulePopupMaskReport() {
		window.requestAnimationFrame(reportPopupMask);
	}

	function positionSubmenuFlyout(submenu, trigger, panel) {
		if (!submenu || !trigger || !panel) {
			return;
		}

		const margin = 8;
		const gap = 6;
		const triggerBounds = trigger.getBoundingClientRect();
		const submenuBounds = submenu.getBoundingClientRect();
		const measuredWidth = panel.offsetWidth || 236;
		const hasRightSpace = triggerBounds.right + gap + measuredWidth <= window.innerWidth - margin;
		const hasLeftSpace = triggerBounds.left - gap - measuredWidth >= margin;
		const useLeft = document.body.classList.contains("submenu-flyout-left")
			|| (!hasRightSpace && hasLeftSpace);
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

	function appendMenuItem(container, item) {
		const kind = String(item && item.kind || "action");
		if (kind === "separator") {
			const separator = document.createElement("div");
			separator.className = "context-menu-separator";
			container.appendChild(separator);
			return;
		}
		if (kind === "label") {
			const label = document.createElement("div");
			label.className = "context-menu-label-row";
			label.innerHTML = "<span class=\"context-menu-label\"></span>";
			setText(label, ".context-menu-label", item.label || "");
			container.appendChild(label);
			return;
		}
		if (kind === "profileHeader") {
			const header = document.createElement("div");
			header.className = "context-menu-profile-header";
			header.innerHTML = "<span class=\"context-menu-profile-avatar\"></span><span class=\"context-menu-profile-copy\"><span class=\"context-menu-profile-name\"></span><span class=\"context-menu-profile-status\"></span></span>";
			const name = item.label || "You";
			const avatar = header.querySelector(".context-menu-profile-avatar");
			const avatarUrl = String(item.avatarUrl || "").trim();
			if (avatarUrl) {
				avatar.classList.add("has-image");
				avatar.style.backgroundImage = "url(\"" + avatarUrl.replace(/"/g, "%22") + "\")";
			} else {
				avatar.textContent = item.avatarText || initialsFor(name);
			}
			const status = header.querySelector(".context-menu-profile-status");
			status.className = "context-menu-profile-status" + (item.statusTone ? " is-" + item.statusTone : "");
			setText(header, ".context-menu-profile-name", name);
			setText(header, ".context-menu-profile-status", item.statusLabel || "");
			container.appendChild(header);
			return;
		}
		if (kind === "submenu") {
			const submenu = document.createElement("div");
			submenu.className = "context-menu-submenu";
			const trigger = document.createElement("button");
			trigger.type = "button";
			trigger.className = "context-menu-item context-menu-submenu-trigger";
			trigger.setAttribute("aria-haspopup", "menu");
			trigger.setAttribute("aria-expanded", "false");
			trigger.innerHTML = "<span class=\"context-menu-icon\" aria-hidden=\"true\">" + themedIconSvg(item.icon || "more")
				+ "</span><span class=\"context-menu-label\"></span><span class=\"context-menu-state\" aria-hidden=\"true\">&gt;</span>";
			setText(trigger, ".context-menu-label", item.label || "More");
			const panel = document.createElement("div");
			panel.className = "context-menu-submenu-panel";
			panel.setAttribute("role", "menu");
			(item.items || []).forEach(function(child) {
				appendMenuItem(panel, child);
			});
			function setOpen(open) {
				submenu.classList.toggle("is-open", open);
				trigger.setAttribute("aria-expanded", open ? "true" : "false");
				if (open) {
					positionSubmenuFlyout(submenu, trigger, panel);
					window.requestAnimationFrame(function() {
						positionSubmenuFlyout(submenu, trigger, panel);
						reportPopupMask();
					});
				} else {
					panel.style.left = "";
					panel.style.right = "";
					panel.style.top = "";
					panel.style.maxHeight = "";
				}
				updateOpenSubmenuState();
				schedulePopupMaskReport();
			}
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
			if (String(pendingPayload && pendingPayload.openSubmenuLabel || "").trim().toLowerCase()
				=== String(item.label || "").trim().toLowerCase()) {
				window.setTimeout(function() {
					setOpen(true);
				}, 0);
			}
			return;
		}

		const button = document.createElement("button");
		button.type = "button";
		button.className = "context-menu-item"
			+ (item.checked ? " is-checked" : "")
			+ (item.tone ? " is-" + item.tone : "");
		button.disabled = item.enabled === false;
		button.innerHTML = "<span class=\"context-menu-icon\" aria-hidden=\"true\">" + themedIconSvg(item.icon)
			+ "</span><span class=\"context-menu-label\"></span><span class=\"context-menu-state\"></span>";
		setText(button, ".context-menu-label", item.label || "Action");
		setText(button, ".context-menu-state", item.checked ? "On" : "");
		button.addEventListener("click", function(event) {
			event.stopPropagation();
			if (button.disabled || !popupBridge || typeof popupBridge.activateAction !== "function") {
				return;
			}
			popupBridge.activateAction(Number(item.actionIndex));
		});
		container.appendChild(button);
	}

	function resetPopupScrollState() {
		if (menu) {
			menu.scrollTop = 0;
			menu.scrollLeft = 0;
		}
		document.querySelectorAll(".context-menu-submenu-panel").forEach(function(panel) {
			panel.scrollTop = 0;
			panel.scrollLeft = 0;
		});
	}

	function renderPayload(payload) {
		pendingPayload = payload || pendingPayload || {};
		if (!menu || !pendingPayload) {
			return;
		}
		menu.innerHTML = "";
		menu.classList.remove("has-open-submenu");
		resetPopupScrollState();
		applyPopupUiTweaks(pendingPayload.uiTweaks || pendingPayload);
		const layout = pendingPayload.layout || {};
		const rootLeft = Number(layout.rootLeft || 0);
		const rootWidth = Number(layout.rootWidth || 272);
		menu.style.setProperty("--context-menu-root-left", Math.max(0, rootLeft) + "px");
		menu.style.setProperty("--context-menu-root-width", Math.max(176, rootWidth) + "px");
		document.body.classList.toggle("submenu-flyout-left", String(layout.submenuSide || "") === "left");
		(pendingPayload.items || []).forEach(function(item) {
			appendMenuItem(menu, item || {});
		});
		resetPopupScrollState();
		schedulePopupMaskReport();
	}

	async function ensureBridge() {
		if (!window.qt || !window.qt.webChannelTransport || popupBridge) {
			return;
		}

		function bindBridge() {
			return new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						popupBridge = channel.objects.contextMenuHost || null;
						resolve();
					});
				} catch (error) {
					console.warn("Modern popup bridge initialization failed:", error);
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
					console.warn("Unable to load qwebchannel.js for the modern popup.");
					resolve();
				};
				document.head.appendChild(script);
			});
		}
		await bridgeLoadPromise;
	}

	window.__mumbleModernPopupSetItems = function(payload) {
		renderPayload(payload || {});
		ensureBridge();
	};

	document.addEventListener("keydown", function(event) {
		if (event.key === "Escape" && popupBridge && typeof popupBridge.closePopup === "function") {
			event.preventDefault();
			popupBridge.closePopup();
		}
	});

	document.addEventListener("DOMContentLoaded", function() {
		ensureBridge().then(function() {
			if (pendingPayload) {
				renderPayload(pendingPayload);
			}
		});
	});
})();
