#include "QmlThemeController.h"

#include "Global.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>

QmlThemeController::QmlThemeController(QObject *parent) : QObject(parent) {
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->installEventFilter(this);
	}
	refresh();
}

void QmlThemeController::refresh() {
	const std::optional< UiThemeTokens > activeTokens = activeUiThemeTokens();
	if (!activeTokens) {
		return;
	}

	Mumble::ModernTheme::ThemeMetrics metrics;
	QColor shellBackground;
	if (Global::g_global_struct) {
		if (const auto customTheme = Mumble::ModernTheme::customTheme(Global::get().s.qsModernShellTheme); customTheme) {
			metrics = customTheme->metrics;
			shellBackground = customTheme->palette.shellBackground;
		}
	}
	applyTokens(*activeTokens, metrics, shellBackground);
}

void QmlThemeController::applyTokens(const UiThemeTokens &tokens,
								  const Mumble::ModernTheme::ThemeMetrics &metrics,
								  const QColor &shellBackground) {
	const QColor nextShell = shellBackground.isValid() ? shellBackground : tokens.base;
	const QColor nextSelected = tokens.accentSubtle.isValid() ? tokens.accentSubtle
														 : uiThemeColorWithAlpha(tokens.accent, 0.16);
	const bool changed = m_shellBackground != nextShell || m_panel != tokens.base || m_rail != tokens.mantle
		|| m_strip != tokens.crust || m_divider != tokens.border || m_textStrong != tokens.text
		|| m_textMain != tokens.subtext0 || m_textMuted != tokens.overlay0 || m_accent != tokens.accent
		|| m_selected != nextSelected || m_danger != tokens.danger || m_success != tokens.success
		|| m_warning != tokens.warning || m_focus != tokens.focusAccent || m_shellRadius != metrics.shellRadius
		|| m_innerRadius != metrics.innerRadius || m_spacing != metrics.spacing;
	if (!changed) {
		return;
	}

	m_shellBackground = nextShell;
	m_panel = tokens.base;
	m_rail = tokens.mantle;
	m_strip = tokens.crust;
	m_divider = tokens.border;
	m_textStrong = tokens.text;
	m_textMain = tokens.subtext0;
	m_textMuted = tokens.overlay0;
	m_accent = tokens.accent;
	m_selected = nextSelected;
	m_danger = tokens.danger;
	m_success = tokens.success;
	m_warning = tokens.warning;
	m_focus = tokens.focusAccent;
	m_shellRadius = metrics.shellRadius;
	m_innerRadius = metrics.innerRadius;
	m_spacing = metrics.spacing;
	emit themeChanged();
}

bool QmlThemeController::applyVisualGateAppearance(const QString &theme, const QString &layout) {
	const QString normalizedTheme = theme.trimmed().toLower();
	const QString normalizedLayout = layout.trimmed().toLower();
	if ((normalizedTheme != QLatin1String("light") && normalizedTheme != QLatin1String("dark"))
		|| (normalizedLayout != QLatin1String("regular") && normalizedLayout != QLatin1String("compact"))) {
		return false;
	}
	const bool nextCompact = normalizedLayout == QLatin1String("compact");
	Mumble::ModernTheme::ThemeMetrics metrics;
	if (nextCompact) {
		metrics.shellRadius = 12;
		metrics.innerRadius = 8;
		metrics.spacing = 8;
	}
	applyTokens(uiThemeTokensForThemeId(normalizedTheme), metrics);
	if (m_compact != nextCompact) {
		m_compact = nextCompact;
		emit densityChanged();
	}
	return true;
}

bool QmlThemeController::eventFilter(QObject *watched, QEvent *event) {
	if (watched == QCoreApplication::instance() && event
		&& (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange)) {
		refresh();
	}
	return QObject::eventFilter(watched, event);
}
