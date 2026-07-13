#include "QmlThemeController.h"

#include "Global.h"
#include "QmlClientModels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>

namespace {
	QString normalizedDensityId(const QString &density) {
		const QString normalized = density.trimmed().toLower();
		return QStringList { QStringLiteral("compact"), QStringLiteral("comfortable"), QStringLiteral("spacious") }
					 .contains(normalized)
			   ? normalized
			   : QStringLiteral("comfortable");
	}

	QColor mixColors(const QColor &base, const QColor &overlay, const qreal overlayRatio) {
		const qreal ratio = qBound< qreal >(0.0, overlayRatio, 1.0);
		return QColor::fromRgbF(base.redF() * (1.0 - ratio) + overlay.redF() * ratio,
								base.greenF() * (1.0 - ratio) + overlay.greenF() * ratio,
								base.blueF() * (1.0 - ratio) + overlay.blueF() * ratio, 1.0);
	}

	QColor namedAccentColor(const QString &accentId, const QString &customAccent) {
		if (accentId == QLatin1String("teal")) return QColor(QStringLiteral("#5ec8b0"));
		if (accentId == QLatin1String("blue")) return QColor(QStringLiteral("#73b7ff"));
		if (accentId == QLatin1String("violet")) return QColor(QStringLiteral("#b59cff"));
		if (accentId == QLatin1String("amber")) return QColor(QStringLiteral("#f2c76f"));
		if (accentId == QLatin1String("rose")) return QColor(QStringLiteral("#ff8aa0"));
		if (accentId == Mumble::ModernTheme::customAccentId()) {
			return QColor(Mumble::ModernTheme::normalizedCustomAccentColor(customAccent));
		}
		return {};
	}

	QString colorStateValue(const QColor &color) {
		return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
	}
}

QmlThemeController::QmlThemeController(QObject *parent) : QObject(parent) {
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->installEventFilter(this);
	}
	refresh();
}

void QmlThemeController::refresh() {
	if (property(QmlVisualFixtureMutation::OverrideProperty).toBool()
		&& !property(QmlVisualFixtureMutation::WriteProperty).toBool()) return;
	const std::optional< UiThemeTokens > activeTokens = activeUiThemeTokens();
	if (!activeTokens) {
		return;
	}

	const QString themeId = Mumble::ModernTheme::normalizedThemeId(Global::get().s.qsModernShellTheme);
	const QString densityId = normalizedDensityId(Global::get().s.qsModernShellDensity);
	const QString accentId = Mumble::ModernTheme::normalizedAccentId(Global::get().s.qsModernShellAccent);
	const QString requestedRailSide = Global::get().s.qsModernShellRailSide.trimmed().toLower();
	const QString railSide = requestedRailSide == QLatin1String("left")
		? QStringLiteral("left") : QStringLiteral("right");
	const bool railSideChanged = m_railSide != railSide;
	m_railSide = railSide;
	Mumble::ModernTheme::ThemeMetrics metrics;
	QColor shellBackground;
	QString themeSource = QStringLiteral("modernShell");
	if (const auto customTheme = Mumble::ModernTheme::customTheme(themeId); customTheme) {
		metrics = customTheme->metrics;
		shellBackground = customTheme->palette.shellBackground;
		themeSource = QStringLiteral("customTheme");
	}
	applyProductTokens(*activeTokens, metrics, shellBackground, themeId, themeSource, densityId, accentId,
		Global::get().s.qsModernShellCustomAccent, Global::get().s.iModernShellCustomAccentStrength);
	if (railSideChanged) emit themeStateChanged();
}

bool QmlThemeController::applyProductAppearance(const QString &theme, const QString &density, const QString &accent,
												 const QString &customAccent, const int customAccentStrength) {
	const QString themeId = Mumble::ModernTheme::normalizedThemeId(theme);
	const QString densityId = normalizedDensityId(density);
	const QString accentId = Mumble::ModernTheme::normalizedAccentId(accent);
	const auto customTheme = Mumble::ModernTheme::customTheme(themeId);
	UiThemeTokens tokens = uiThemeTokensForThemeId(themeId);
	Mumble::ModernTheme::ThemeMetrics metrics;
	QColor shellBackground;
	QString themeSource = QStringLiteral("modernShell");
	if (customTheme && Global::g_global_struct
		&& Mumble::ModernTheme::normalizedThemeId(Global::get().s.qsModernShellTheme) == themeId) {
		if (const auto activeTokens = activeUiThemeTokens()) tokens = *activeTokens;
		metrics = customTheme->metrics;
		shellBackground = customTheme->palette.shellBackground;
		themeSource = QStringLiteral("customTheme");
	}
	applyProductTokens(tokens, metrics, shellBackground, themeId, themeSource, densityId, accentId,
		customAccent, customAccentStrength);
	return true;
}

void QmlThemeController::applyProductTokens(UiThemeTokens tokens, Mumble::ModernTheme::ThemeMetrics metrics,
											 const QColor &shellBackground, const QString &themeId,
											 const QString &themeSource, const QString &densityId,
											 const QString &accentId, const QString &customAccent,
											 const int customAccentStrength) {
	const QColor accentColor = namedAccentColor(accentId, customAccent);
	if (accentColor.isValid()) {
		const qreal strength = accentId == Mumble::ModernTheme::customAccentId()
			? static_cast< qreal >(
				  Mumble::ModernTheme::normalizedCustomAccentStrength(customAccentStrength)) / 100.0
			: 0.5;
		tokens.accent = accentColor;
		tokens.accentHover = mixColors(accentColor, tokens.text.isValid() ? tokens.text : QColor(Qt::white),
			0.14 + (strength * 0.12));
		tokens.accentSubtle = uiThemeColorWithAlpha(accentColor, 0.06 + (strength * 0.22));
		tokens.focusAccent = accentColor;
	}

	if (densityId == QLatin1String("compact")) {
		metrics.shellRadius = qMax(4, metrics.shellRadius - 4);
		metrics.innerRadius = qMax(4, metrics.innerRadius - 3);
		metrics.spacing = 8;
	} else if (densityId == QLatin1String("spacious")) {
		metrics.shellRadius += 2;
		metrics.innerRadius += 2;
		metrics.spacing = 16;
	}

	const bool densityChangedValue = m_densityId != densityId || m_compact != (densityId == QLatin1String("compact"));
	const bool metadataChanged = m_themeId != themeId || m_themeSource != themeSource || m_accentId != accentId;
	m_themeId = themeId;
	m_themeSource = themeSource;
	m_densityId = densityId;
	m_accentId = accentId;
	m_compact = densityId == QLatin1String("compact");
	applyTokens(tokens, metrics, shellBackground);
	if (densityChangedValue) emit densityChanged();
	if (metadataChanged || densityChangedValue) emit themeStateChanged();
}

QVariantMap QmlThemeController::state() const {
	const QVariantMap effectiveTokens {
		{ QStringLiteral("shellBackground"), colorStateValue(m_shellBackground) },
		{ QStringLiteral("panel"), colorStateValue(m_panel) },
		{ QStringLiteral("rail"), colorStateValue(m_rail) },
		{ QStringLiteral("strip"), colorStateValue(m_strip) },
		{ QStringLiteral("divider"), colorStateValue(m_divider) },
		{ QStringLiteral("textStrong"), colorStateValue(m_textStrong) },
		{ QStringLiteral("textMain"), colorStateValue(m_textMain) },
		{ QStringLiteral("textMuted"), colorStateValue(m_textMuted) },
		{ QStringLiteral("accent"), colorStateValue(m_accent) },
		{ QStringLiteral("selected"), colorStateValue(m_selected) },
		{ QStringLiteral("danger"), colorStateValue(m_danger) },
		{ QStringLiteral("success"), colorStateValue(m_success) },
		{ QStringLiteral("warning"), colorStateValue(m_warning) },
		{ QStringLiteral("focus"), colorStateValue(m_focus) },
		{ QStringLiteral("shellRadius"), m_shellRadius },
		{ QStringLiteral("innerRadius"), m_innerRadius },
		{ QStringLiteral("spacing"), m_spacing }
	};
	return {
		{ QStringLiteral("theme"), m_themeId },
		{ QStringLiteral("themeId"), m_themeId },
		{ QStringLiteral("themeSource"), m_themeSource },
		{ QStringLiteral("density"), m_densityId },
		{ QStringLiteral("accent"), m_accentId },
		{ QStringLiteral("railSide"), m_railSide },
		{ QStringLiteral("compact"), m_compact },
		{ QStringLiteral("effectiveTokens"), effectiveTokens },
		{ QStringLiteral("themeTokens"), effectiveTokens }
	};
}

void QmlThemeController::applyTokens(const UiThemeTokens &tokens,
								  const Mumble::ModernTheme::ThemeMetrics &metrics,
								  const QColor &shellBackground) {
	if (property(QmlVisualFixtureMutation::OverrideProperty).toBool()
		&& !property(QmlVisualFixtureMutation::WriteProperty).toBool()) return;
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
	emit themeStateChanged();
}

bool QmlThemeController::applyVisualGateAppearance(const QString &theme, const QString &layout) {
	const QString normalizedTheme = theme.trimmed().toLower();
	const QString normalizedLayout = layout.trimmed().toLower();
	if ((normalizedTheme != QLatin1String("light") && normalizedTheme != QLatin1String("dark"))
		|| (normalizedLayout != QLatin1String("regular") && normalizedLayout != QLatin1String("compact"))) {
		return false;
	}
	const QString densityId = normalizedLayout == QLatin1String("compact")
		? QStringLiteral("compact") : QStringLiteral("comfortable");
	applyProductTokens(uiThemeTokensForThemeId(normalizedTheme), {}, {}, normalizedTheme,
		QStringLiteral("visualFixture"), densityId, QStringLiteral("auto"), QStringLiteral("#5ec8b0"), 50);
	return true;
}

bool QmlThemeController::eventFilter(QObject *watched, QEvent *event) {
	if (watched == QCoreApplication::instance() && event
		&& (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange)) {
		refresh();
	}
	return QObject::eventFilter(watched, event);
}
