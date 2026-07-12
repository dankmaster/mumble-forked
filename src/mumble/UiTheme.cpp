// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UiTheme.h"

#include "Global.h"
#include "ModernTheme.h"
#include "Themes.h"

#include <QtCore/QtGlobal>
#include <QtCore/QVariant>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#	include <windows.h>
#endif

namespace {
	QColor mixUiThemeColors(const QColor &baseColor, const QColor &overlayColor, qreal overlayRatio) {
		const qreal clampedRatio = qBound< qreal >(0.0, overlayRatio, 1.0);
		const qreal baseRatio    = 1.0 - clampedRatio;
		return QColor::fromRgbF(baseColor.redF() * baseRatio + overlayColor.redF() * clampedRatio,
								baseColor.greenF() * baseRatio + overlayColor.greenF() * clampedRatio,
								baseColor.blueF() * baseRatio + overlayColor.blueF() * clampedRatio, 1.0);
	}

	void applyRuntimeAliases(UiThemeTokens &tokens) {
		tokens.surface       = tokens.mantle;
		tokens.overlay       = tokens.surface0;
		tokens.highlight     = tokens.surface1;
		tokens.border        = tokens.surface1;
		tokens.textPrimary   = tokens.text;
		tokens.textSecondary = tokens.subtext0;
		tokens.textMuted     = tokens.overlay0;
		tokens.success       = tokens.green;
		tokens.warning       = tokens.yellow;
		tokens.danger        = tokens.red;
		tokens.orange        = tokens.peach;
		tokens.purple        = tokens.mauve;
		if (!tokens.focusAccent.isValid()) {
			tokens.focusAccent = tokens.lavender.isValid() ? tokens.lavender : tokens.accent;
		}
	}

	QColor colorWithAlpha(const QColor &color, qreal alpha) {
		QColor adjusted = color;
		adjusted.setAlphaF(qBound< qreal >(0.0, alpha, 1.0));
		return adjusted;
	}

	void applyCustomAccentOverlay(UiThemeTokens &tokens) {
		if (!Global::g_global_struct
			|| Mumble::ModernTheme::normalizedAccentId(Global::get().s.qsModernShellAccent)
				   != Mumble::ModernTheme::customAccentId()) {
			return;
		}

		const QColor accent(Mumble::ModernTheme::normalizedCustomAccentColor(Global::get().s.qsModernShellCustomAccent));
		if (!accent.isValid()) {
			return;
		}

		const qreal strength =
			static_cast< qreal >(
				Mumble::ModernTheme::normalizedCustomAccentStrength(Global::get().s.iModernShellCustomAccentStrength))
			/ 100.0;
		tokens.accent       = accent;
		tokens.accentHover  = mixUiThemeColors(accent, tokens.text.isValid() ? tokens.text : QColor(Qt::white),
											   0.14 + (strength * 0.12));
		tokens.accentSubtle = colorWithAlpha(accent, 0.06 + (strength * 0.22));
		tokens.focusAccent  = accent;
		tokens.lavender     = accent;
		tokens.teal         = accent;
	}

	void finalizeUiThemeTokens(UiThemeTokens &tokens) {
		applyCustomAccentOverlay(tokens);
		applyRuntimeAliases(tokens);
	}

	QColor tokenColor(const QVariantMap &tokens, const QString &name, const QColor &fallback) {
		const QColor color(tokens.value(name).toString());
		return color.isValid() ? color : fallback;
	}

	std::optional< UiThemeTokens > activeModernCustomThemeTokens() {
		if (!Global::g_global_struct) {
			return std::nullopt;
		}

		const QVariantMap themeTokens = Mumble::ModernTheme::customThemeTokens(Global::get().s.qsModernShellTheme);
		if (themeTokens.isEmpty()) {
			return std::nullopt;
		}

		const QPalette palette = qApp ? qApp->palette() : QPalette();
		UiThemeTokens tokens;
		tokens.preset   = UiThemePreset::MumbleDark;
		tokens.crust    = tokenColor(themeTokens, QStringLiteral("--shell-strip"), palette.color(QPalette::Window));
		tokens.mantle   = tokenColor(themeTokens, QStringLiteral("--shell-rail"), tokens.crust);
		tokens.base     = tokenColor(themeTokens, QStringLiteral("--shell-panel"), palette.color(QPalette::Base));
		tokens.surface0 = tokenColor(themeTokens, QStringLiteral("--shell-panel-soft"), palette.color(QPalette::Button));
		tokens.surface1 = tokenColor(themeTokens, QStringLiteral("--shell-highlight"), palette.color(QPalette::Mid));
		tokens.surface2 = tokenColor(themeTokens, QStringLiteral("--surface-border"), palette.color(QPalette::Light));
		tokens.text     = tokenColor(themeTokens, QStringLiteral("--text-strong"), palette.color(QPalette::WindowText));
		tokens.subtext0 = tokenColor(themeTokens, QStringLiteral("--text-main"), palette.color(QPalette::Text));
		tokens.overlay0 = tokenColor(themeTokens, QStringLiteral("--text-muted"), palette.color(QPalette::Disabled, QPalette::Text));
		tokens.accent   = tokenColor(themeTokens, QStringLiteral("--accent"), palette.color(QPalette::Highlight));
		tokens.accentHover =
			tokenColor(themeTokens, QStringLiteral("--accent-strong"), mixUiThemeColors(tokens.accent, tokens.text, 0.18));
		tokens.accentSubtle =
			tokenColor(themeTokens, QStringLiteral("--accent-soft"), colorWithAlpha(tokens.accent, 0.15));
		tokens.red       = tokenColor(themeTokens, QStringLiteral("--danger"), QColor(QStringLiteral("#e6736f")));
		tokens.green     = tokenColor(themeTokens, QStringLiteral("--success"), QColor(QStringLiteral("#5fd0a3")));
		tokens.yellow    = tokenColor(themeTokens, QStringLiteral("--warning"), QColor(QStringLiteral("#e0c574")));
		tokens.peach     = tokenColor(themeTokens, QStringLiteral("--latency-orange"), tokens.yellow);
		tokens.mauve     = tokens.red;
		tokens.lavender  = tokens.accent;
		tokens.teal      = tokens.green;
		tokens.pink      = tokens.red;
		tokens.rosewater = tokens.text;
		tokens.focusAccent = tokens.accent;
		finalizeUiThemeTokens(tokens);
		return tokens;
	}

	UiThemeTokens modernBuiltInUiThemeTokens(const QString &themeID) {
		const QString theme = Mumble::ModernTheme::normalizedThemeId(themeID);
		UiThemeTokens tokens;

		if (theme == QStringLiteral("light") || theme == QStringLiteral("latte")) {
			tokens.preset       = UiThemePreset::MumbleLight;
			tokens.crust        = QColor(QStringLiteral("#d8e0eb"));
			tokens.mantle       = QColor(QStringLiteral("#e6ebf3"));
			tokens.base         = QColor(QStringLiteral("#f7f9fc"));
			tokens.surface0     = QColor(QStringLiteral("#e9eef6"));
			tokens.surface1     = QColor(QStringLiteral("#ffffff"));
			tokens.surface2     = QColor(QStringLiteral("#cfd7e0"));
			tokens.text         = QColor(QStringLiteral("#1f2937"));
			tokens.subtext0     = QColor(QStringLiteral("#3a4656"));
			tokens.overlay0     = QColor(QStringLiteral("#647184"));
			tokens.accent       = QColor(QStringLiteral("#268f7f"));
			tokens.accentHover  = QColor(QStringLiteral("#1d776a"));
			tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.13);
			tokens.red          = QColor(QStringLiteral("#c75f5f"));
			tokens.green        = QColor(QStringLiteral("#2f9d79"));
			tokens.yellow       = QColor(QStringLiteral("#b96b2d"));
			tokens.peach        = tokens.yellow;
			tokens.mauve        = QColor(QStringLiteral("#8d6ce6"));
			tokens.lavender     = tokens.mauve;
			tokens.teal         = tokens.accent;
			tokens.pink         = tokens.red;
			tokens.rosewater    = tokens.text;
			tokens.focusAccent  = tokens.accent;
			finalizeUiThemeTokens(tokens);
			return tokens;
		}

		if (theme == QStringLiteral("mocha")) {
			tokens.preset       = UiThemePreset::CatppuccinMocha;
			tokens.crust        = QColor(QStringLiteral("#11111b"));
			tokens.mantle       = QColor(QStringLiteral("#181825"));
			tokens.base         = QColor(QStringLiteral("#1e1e2e"));
			tokens.surface0     = QColor(QStringLiteral("#313244"));
			tokens.surface1     = QColor(QStringLiteral("#45475a"));
			tokens.surface2     = QColor(QStringLiteral("#585b70"));
			tokens.text         = QColor(QStringLiteral("#cdd6f4"));
			tokens.subtext0     = QColor(QStringLiteral("#bac2de"));
			tokens.overlay0     = QColor(QStringLiteral("#6c7086"));
			tokens.accent       = QColor(QStringLiteral("#94e2d5"));
			tokens.accentHover  = QColor(QStringLiteral("#b4f1e7"));
			tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.14);
			tokens.red          = QColor(QStringLiteral("#f38ba8"));
			tokens.green        = QColor(QStringLiteral("#a6e3a1"));
			tokens.yellow       = QColor(QStringLiteral("#f9e2af"));
			tokens.peach        = QColor(QStringLiteral("#fab387"));
			tokens.mauve        = QColor(QStringLiteral("#cba6f7"));
			tokens.lavender     = QColor(QStringLiteral("#b4befe"));
			tokens.teal         = tokens.accent;
			tokens.pink         = QColor(QStringLiteral("#f5c2e7"));
			tokens.rosewater    = QColor(QStringLiteral("#f5e0dc"));
			tokens.focusAccent  = tokens.lavender;
			finalizeUiThemeTokens(tokens);
			return tokens;
		}

		if (theme == QStringLiteral("macchiato") || theme == QStringLiteral("frappe")) {
			const bool frappe   = theme == QStringLiteral("frappe");
			tokens.preset       = UiThemePreset::CatppuccinMocha;
			tokens.crust        = QColor(frappe ? QStringLiteral("#232634") : QStringLiteral("#181926"));
			tokens.mantle       = QColor(frappe ? QStringLiteral("#292c3c") : QStringLiteral("#1e2030"));
			tokens.base         = QColor(frappe ? QStringLiteral("#303446") : QStringLiteral("#24273a"));
			tokens.surface0     = QColor(frappe ? QStringLiteral("#414559") : QStringLiteral("#363a4f"));
			tokens.surface1     = QColor(frappe ? QStringLiteral("#51576d") : QStringLiteral("#494d64"));
			tokens.surface2     = QColor(frappe ? QStringLiteral("#626880") : QStringLiteral("#5b6078"));
			tokens.text         = QColor(frappe ? QStringLiteral("#c6d0f5") : QStringLiteral("#cad3f5"));
			tokens.subtext0     = QColor(frappe ? QStringLiteral("#a5adce") : QStringLiteral("#a5adcb"));
			tokens.overlay0     = QColor(frappe ? QStringLiteral("#737994") : QStringLiteral("#6e738d"));
			tokens.accent       = QColor(frappe ? QStringLiteral("#81c8be") : QStringLiteral("#8bd5ca"));
			tokens.accentHover  = mixUiThemeColors(tokens.accent, tokens.text, 0.22);
			tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.14);
			tokens.red          = QColor(frappe ? QStringLiteral("#e78284") : QStringLiteral("#ed8796"));
			tokens.green        = QColor(frappe ? QStringLiteral("#a6d189") : QStringLiteral("#a6da95"));
			tokens.yellow       = QColor(frappe ? QStringLiteral("#e5c890") : QStringLiteral("#eed49f"));
			tokens.peach        = QColor(frappe ? QStringLiteral("#ef9f76") : QStringLiteral("#f5a97f"));
			tokens.mauve        = QColor(frappe ? QStringLiteral("#ca9ee6") : QStringLiteral("#c6a0f6"));
			tokens.lavender     = QColor(frappe ? QStringLiteral("#babbf1") : QStringLiteral("#b7bdf8"));
			tokens.teal         = tokens.accent;
			tokens.pink         = QColor(frappe ? QStringLiteral("#f4b8e4") : QStringLiteral("#f5bde6"));
			tokens.rosewater    = QColor(frappe ? QStringLiteral("#f2d5cf") : QStringLiteral("#f4dbd6"));
			tokens.focusAccent  = tokens.lavender;
			finalizeUiThemeTokens(tokens);
			return tokens;
		}

		if (theme == QStringLiteral("nord") || theme == QStringLiteral("gruvbox")) {
			const bool gruvbox  = theme == QStringLiteral("gruvbox");
			tokens.preset       = UiThemePreset::MumbleDark;
			tokens.crust        = QColor(gruvbox ? QStringLiteral("#171a1b") : QStringLiteral("#202630"));
			tokens.mantle       = QColor(gruvbox ? QStringLiteral("#1d2021") : QStringLiteral("#252b35"));
			tokens.base         = QColor(gruvbox ? QStringLiteral("#282828") : QStringLiteral("#2e3440"));
			tokens.surface0     = QColor(gruvbox ? QStringLiteral("#3c3836") : QStringLiteral("#3b4252"));
			tokens.surface1     = QColor(gruvbox ? QStringLiteral("#504945") : QStringLiteral("#434c5e"));
			tokens.surface2     = QColor(gruvbox ? QStringLiteral("#665c54") : QStringLiteral("#4c566a"));
			tokens.text         = QColor(gruvbox ? QStringLiteral("#ebdbb2") : QStringLiteral("#e5e9f0"));
			tokens.subtext0     = QColor(gruvbox ? QStringLiteral("#d5c4a1") : QStringLiteral("#d8dee9"));
			tokens.overlay0     = QColor(gruvbox ? QStringLiteral("#a89984") : QStringLiteral("#aeb6c4"));
			tokens.accent       = QColor(gruvbox ? QStringLiteral("#8ec07c") : QStringLiteral("#88c0d0"));
			tokens.accentHover  = mixUiThemeColors(tokens.accent, tokens.text, 0.20);
			tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.14);
			tokens.red          = QColor(gruvbox ? QStringLiteral("#fb4934") : QStringLiteral("#bf616a"));
			tokens.green        = QColor(gruvbox ? QStringLiteral("#b8bb26") : QStringLiteral("#a3be8c"));
			tokens.yellow       = QColor(gruvbox ? QStringLiteral("#fabd2f") : QStringLiteral("#ebcb8b"));
			tokens.peach        = QColor(gruvbox ? QStringLiteral("#fe8019") : QStringLiteral("#d08770"));
			tokens.mauve        = QColor(gruvbox ? QStringLiteral("#d3869b") : QStringLiteral("#b48ead"));
			tokens.lavender     = tokens.accent;
			tokens.teal         = tokens.accent;
			tokens.pink         = tokens.mauve;
			tokens.rosewater    = tokens.text;
			tokens.focusAccent  = tokens.accent;
			finalizeUiThemeTokens(tokens);
			return tokens;
		}

		tokens.preset       = UiThemePreset::MumbleDark;
		tokens.crust        = QColor(QStringLiteral("#14181f"));
		tokens.mantle       = QColor(QStringLiteral("#1b2027"));
		tokens.base         = QColor(QStringLiteral("#20262f"));
		tokens.surface0     = QColor(QStringLiteral("#262d38"));
		tokens.surface1     = QColor(QStringLiteral("#2e3742"));
		tokens.surface2     = QColor(QStringLiteral("#384453"));
		tokens.text         = QColor(QStringLiteral("#e7ecf3"));
		tokens.subtext0     = QColor(QStringLiteral("#c3cbd6"));
		tokens.overlay0     = QColor(QStringLiteral("#8b94a3"));
		tokens.accent       = QColor(QStringLiteral("#5ec8b0"));
		tokens.accentHover  = QColor(QStringLiteral("#82ddca"));
		tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.16);
		tokens.red          = QColor(QStringLiteral("#e6736f"));
		tokens.green        = QColor(QStringLiteral("#5fd0a3"));
		tokens.yellow       = QColor(QStringLiteral("#e0c574"));
		tokens.peach        = QColor(QStringLiteral("#f09a4a"));
		tokens.mauve        = QColor(QStringLiteral("#b59cff"));
		tokens.lavender     = tokens.mauve;
		tokens.teal         = tokens.accent;
		tokens.pink         = QColor(QStringLiteral("#ff8aa0"));
		tokens.rosewater    = tokens.text;
		tokens.focusAccent  = tokens.accent;
		finalizeUiThemeTokens(tokens);
		return tokens;
	}

#ifdef Q_OS_WIN
	using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

	constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	constexpr DWORD DwmUseImmersiveDarkModeAttribute       = 20;
	constexpr DWORD DwmBorderColorAttribute                = 34;
	constexpr DWORD DwmCaptionColorAttribute               = 35;
	constexpr DWORD DwmTextColorAttribute                  = 36;

	COLORREF colorRefFromQColor(const QColor &color) {
		return RGB(color.red(), color.green(), color.blue());
	}

	bool shouldThemeNativeTitleBar(const QWidget *widget) {
		if (!widget || !widget->isWindow()) {
			return false;
		}

		const Qt::WindowFlags flags = widget->windowFlags();
		return !(flags.testFlag(Qt::Popup) || flags.testFlag(Qt::ToolTip) || flags.testFlag(Qt::SplashScreen));
	}
#endif
}

QColor uiThemeColorWithAlpha(const QColor &color, qreal alpha) {
	QColor adjusted = color;
	adjusted.setAlphaF(qBound< qreal >(0.0, alpha, 1.0));
	return adjusted;
}

QString uiThemeQssColor(const QColor &color) {
	if (color.alpha() < 255) {
		return QString::fromLatin1("rgba(%1, %2, %3, %4)")
			.arg(color.red())
			.arg(color.green())
			.arg(color.blue())
			.arg(QString::number(color.alphaF(), 'f', 3));
	}

	return color.name();
}

bool uiThemePaletteIsDark(const QPalette &palette) {
	return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
}

UiThemeWindowChrome uiThemeWindowChromeForPalette(const QPalette &palette) {
	const bool darkTheme         = uiThemePaletteIsDark(palette);
	const QColor windowColor     = palette.color(QPalette::Window);
	const QColor baseColor       = palette.color(QPalette::Base);
	const QColor accentColor     = palette.color(QPalette::Highlight);
	UiThemeWindowChrome chrome;
	chrome.dark    = darkTheme;
	chrome.text    = palette.color(QPalette::WindowText);
	chrome.caption = darkTheme ? mixUiThemeColors(windowColor, baseColor, 0.22)
							   : mixUiThemeColors(windowColor, accentColor, 0.08);
	chrome.border  = darkTheme ? mixUiThemeColors(chrome.caption, accentColor, 0.18)
							   : mixUiThemeColors(chrome.caption, accentColor, 0.26);
	return chrome;
}

UiThemeWindowChrome uiThemeWindowChromeForActiveTheme(const QPalette &fallbackPalette) {
	UiThemeWindowChrome chrome = uiThemeWindowChromeForPalette(fallbackPalette);

	if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
		chrome.caption = tokens->crust;
		chrome.text    = tokens->text;
		chrome.border  = tokens->surface1;
		chrome.dark    = tokens->text.lightness() > tokens->crust.lightness();
	}

	return chrome;
}

void applyUiThemeNativeTitleBar(QWidget *widget) {
	if (!widget) {
		return;
	}
	applyUiThemeNativeTitleBar(widget, uiThemeWindowChromeForActiveTheme(widget->palette()));
}

void applyUiThemeNativeTitleBar(QWidget *widget, const UiThemeWindowChrome &chrome) {
#ifdef Q_OS_WIN
	if (!shouldThemeNativeTitleBar(widget)) {
		return;
	}

	const HWND hwnd = reinterpret_cast< HWND >(widget->winId());
	if (!hwnd) {
		return;
	}

	static const HMODULE dwmapiModule = GetModuleHandleW(L"dwmapi.dll");
	if (!dwmapiModule) {
		return;
	}

	static const DwmSetWindowAttributeFn setWindowAttribute =
		reinterpret_cast< DwmSetWindowAttributeFn >(GetProcAddress(dwmapiModule, "DwmSetWindowAttribute"));
	if (!setWindowAttribute) {
		return;
	}

	const UiThemeWindowChrome fallbackChrome =
		chrome.caption.isValid() && chrome.text.isValid() && chrome.border.isValid()
			? chrome
			: uiThemeWindowChromeForPalette(widget->palette());
	const BOOL immersiveDarkMode = fallbackChrome.dark ? TRUE : FALSE;
	HRESULT result =
		setWindowAttribute(hwnd, DwmUseImmersiveDarkModeAttribute, &immersiveDarkMode, sizeof(immersiveDarkMode));
	if (FAILED(result)) {
		setWindowAttribute(hwnd, DwmUseImmersiveDarkModeLegacyAttribute, &immersiveDarkMode,
						   sizeof(immersiveDarkMode));
	}

	const COLORREF captionColorRef = colorRefFromQColor(fallbackChrome.caption);
	const COLORREF textColorRef    = colorRefFromQColor(fallbackChrome.text);
	const COLORREF borderColorRef  = colorRefFromQColor(fallbackChrome.border);
	setWindowAttribute(hwnd, DwmCaptionColorAttribute, &captionColorRef, sizeof(captionColorRef));
	setWindowAttribute(hwnd, DwmTextColorAttribute, &textColorRef, sizeof(textColorRef));
	setWindowAttribute(hwnd, DwmBorderColorAttribute, &borderColorRef, sizeof(borderColorRef));
#else
	Q_UNUSED(widget);
	Q_UNUSED(chrome);
#endif
}

void applyUiThemeNativeTitleBars() {
#ifdef Q_OS_WIN
	if (!qApp) {
		return;
	}

	for (QWidget *widget : QApplication::topLevelWidgets()) {
		applyUiThemeNativeTitleBar(widget);
	}
#endif
}

std::optional< UiThemeTokens > activeUiThemeTokens() {
	if (!Global::g_global_struct) {
		return std::nullopt;
	}

	if (const std::optional< UiThemeTokens > modernCustomTokens = activeModernCustomThemeTokens(); modernCustomTokens) {
		return modernCustomTokens;
	}

	return modernBuiltInUiThemeTokens(Global::get().s.qsModernShellTheme);
}

UiThemeTokens uiThemeTokensForThemeId(const QString &themeId) {
	return modernBuiltInUiThemeTokens(themeId);
}
