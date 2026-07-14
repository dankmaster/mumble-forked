// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_UITHEME_H_
#define MUMBLE_MUMBLE_UITHEME_H_

#include <QtCore/QString>
#include <QtGui/QColor>

#include <optional>

class QPalette;
class QWindow;
class QWidget;

namespace Mumble {
namespace ModernTheme {
struct ThemeDefinition;
}
}

enum class UiThemePreset {
	MumbleDark,
	MumbleLight,
	CatppuccinMocha
};

struct UiThemeTokens {
	UiThemePreset preset = UiThemePreset::MumbleDark;
	QColor crust;
	QColor mantle;
	QColor base;
	QColor surface0;
	QColor surface1;
	QColor surface2;
	QColor text;
	QColor subtext0;
	QColor overlay0;
	QColor red;
	QColor green;
	QColor yellow;
	QColor peach;
	QColor mauve;
	QColor lavender;
	QColor teal;
	QColor pink;
	QColor rosewater;

	// Aliases used by existing runtime styling code.
	QColor surface;
	QColor overlay;
	QColor highlight;
	QColor border;
	QColor mediaCanvas;
	QColor textPrimary;
	QColor textSecondary;
	QColor textMuted;
	QColor accent;
	QColor accentHover;
	QColor accentSubtle;
	QColor success;
	QColor warning;
	QColor danger;
	QColor orange;
	QColor purple;
	QColor focusAccent;
};

struct UiThemeWindowChrome {
	QColor caption;
	QColor text;
	QColor border;
	bool dark = true;
};

std::optional< UiThemeTokens > activeUiThemeTokens();
UiThemeTokens uiThemeTokensForThemeId(const QString &themeId);
UiThemeTokens uiThemeTokensForThemeDefinition(const Mumble::ModernTheme::ThemeDefinition &theme);
bool applyUiThemeAccentOverride(UiThemeTokens &tokens, const QString &accentId, const QString &customAccent,
								int customAccentStrength);
QColor uiThemeColorWithAlpha(const QColor &color, qreal alpha);
QString uiThemeQssColor(const QColor &color);
bool uiThemePaletteIsDark(const QPalette &palette);
UiThemeWindowChrome uiThemeWindowChromeForPalette(const QPalette &palette);
UiThemeWindowChrome uiThemeWindowChromeForActiveTheme(const QPalette &fallbackPalette);
void applyUiThemeNativeTitleBar(QWidget *widget);
void applyUiThemeNativeTitleBar(QWidget *widget, const UiThemeWindowChrome &chrome);
void applyUiThemeNativeTitleBar(QWindow *window);
void applyUiThemeNativeTitleBar(QWindow *window, const UiThemeWindowChrome &chrome);
void applyUiThemeNativeTitleBars();

#endif // MUMBLE_MUMBLE_UITHEME_H_
