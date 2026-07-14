#include "QmlThemeController.h"

#include "Global.h"
#include "QmlClientModels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>

#include <cmath>

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

	QString colorStateValue(const QColor &color) {
		return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
	}

	qreal linearColorChannel(const qreal channel) {
		return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
	}

	QColor contrastTextColor(const QColor &background) {
		const qreal luminance = 0.2126 * linearColorChannel(background.redF())
			+ 0.7152 * linearColorChannel(background.greenF())
			+ 0.0722 * linearColorChannel(background.blueF());
		constexpr qreal darkLuminance = 0.007;
		const qreal darkContrast = (luminance + 0.05) / (darkLuminance + 0.05);
		const qreal lightContrast = 1.05 / (luminance + 0.05);
		return darkContrast >= lightContrast ? QColor(QStringLiteral("#10151c")) : QColor(Qt::white);
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
	if (customTheme) {
		tokens = uiThemeTokensForThemeDefinition(*customTheme);
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
	applyUiThemeAccentOverride(tokens, accentId, customAccent, customAccentStrength);

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
	const QColor onAccent = contrastTextColor(m_accent);
	QVariantMap effectiveTokens {
		{ QStringLiteral("shellBackground"), colorStateValue(m_shellBackground) },
		{ QStringLiteral("panel"), colorStateValue(m_panel) },
		{ QStringLiteral("surfaceRaised"), colorStateValue(m_surfaceRaised) },
		{ QStringLiteral("surfaceHover"), colorStateValue(m_surfaceHover) },
		{ QStringLiteral("surfaceBorder"), colorStateValue(m_surfaceBorder) },
		{ QStringLiteral("mediaCanvas"), colorStateValue(m_mediaCanvas) },
		{ QStringLiteral("rail"), colorStateValue(m_rail) },
		{ QStringLiteral("strip"), colorStateValue(m_strip) },
		{ QStringLiteral("divider"), colorStateValue(m_divider) },
		{ QStringLiteral("textStrong"), colorStateValue(m_textStrong) },
		{ QStringLiteral("textMain"), colorStateValue(m_textMain) },
		{ QStringLiteral("textMuted"), colorStateValue(m_textMuted) },
		{ QStringLiteral("accent"), colorStateValue(m_accent) },
		{ QStringLiteral("accentHover"), colorStateValue(m_accentHover) },
		{ QStringLiteral("accentSubtle"), colorStateValue(m_accentSubtle) },
		{ QStringLiteral("selected"), colorStateValue(m_selected) },
		{ QStringLiteral("danger"), colorStateValue(m_danger) },
		{ QStringLiteral("success"), colorStateValue(m_success) },
		{ QStringLiteral("warning"), colorStateValue(m_warning) },
		{ QStringLiteral("focus"), colorStateValue(m_focus) },
		{ QStringLiteral("chatCanvas"), colorStateValue(m_shellBackground) },
		{ QStringLiteral("chatSurface"), colorStateValue(m_panel) },
		{ QStringLiteral("chatIncomingSurface"), colorStateValue(m_panel) },
		{ QStringLiteral("chatIncomingBorder"), colorStateValue(m_divider) },
		{ QStringLiteral("chatHover"), colorStateValue(m_surfaceHover) },
		{ QStringLiteral("chatOwnSurface"), colorStateValue(m_accentSubtle) },
		{ QStringLiteral("chatOwnBorder"), colorStateValue(uiThemeColorWithAlpha(m_accent, 0.30)) },
		{ QStringLiteral("chatMetadata"), colorStateValue(m_textMuted) },
		{ QStringLiteral("chatReplySurface"), colorStateValue(m_strip) },
		{ QStringLiteral("composerBackground"), colorStateValue(m_surfaceRaised) },
		{ QStringLiteral("composerBorder"), colorStateValue(m_surfaceBorder) },
		{ QStringLiteral("composerFocusBorder"), colorStateValue(m_focus) },
		{ QStringLiteral("composerShadow"), colorStateValue(uiThemeColorWithAlpha(m_mediaCanvas, 0.28)) },
		{ QStringLiteral("popupBackground"), colorStateValue(m_surfaceRaised) },
		{ QStringLiteral("popupBorder"), colorStateValue(m_surfaceBorder) },
		{ QStringLiteral("popupHover"), colorStateValue(m_surfaceHover) },
		{ QStringLiteral("popupSelected"), colorStateValue(m_selected) },
		{ QStringLiteral("selfCardBackground"), colorStateValue(m_rail) },
		{ QStringLiteral("selfCardHover"), colorStateValue(m_surfaceRaised) },
		{ QStringLiteral("selfCardBorder"), colorStateValue(m_divider) },
		{ QStringLiteral("previewCardBackground"), colorStateValue(m_surfaceRaised) },
		{ QStringLiteral("previewCardHover"), colorStateValue(m_surfaceHover) },
		{ QStringLiteral("previewCardBorder"), colorStateValue(m_surfaceBorder) },
		{ QStringLiteral("embedCanvas"), colorStateValue(m_mediaCanvas) },
		{ QStringLiteral("embedSurface"), colorStateValue(m_panel) },
		{ QStringLiteral("embedBorder"), colorStateValue(m_surfaceBorder) },
		{ QStringLiteral("embedHover"), colorStateValue(m_surfaceHover) },
		{ QStringLiteral("embedRevealSurface"), colorStateValue(m_strip) },
		{ QStringLiteral("embedSelection"), colorStateValue(m_selected) },
		{ QStringLiteral("embedOverlayBase"), colorStateValue(m_strip) },
		{ QStringLiteral("elevationShadow"), colorStateValue(uiThemeColorWithAlpha(m_mediaCanvas, 0.46)) },
		{ QStringLiteral("elevationHighlight"), colorStateValue(uiThemeColorWithAlpha(m_textStrong, 0.08)) },
		{ QStringLiteral("onAccent"), colorStateValue(onAccent) },
		{ QStringLiteral("textFaint"), colorStateValue(uiThemeColorWithAlpha(m_textMuted, 0.72)) },
		{ QStringLiteral("shellRadius"), m_shellRadius },
		{ QStringLiteral("innerRadius"), m_innerRadius },
		{ QStringLiteral("spacing"), m_spacing }
	};
	// Keep the established token aliases in the automation DTO while QML consumes
	// the typed properties above. This lets one visual gate compare both frontends.
	effectiveTokens.insert(QStringLiteral("--shell-bg"), colorStateValue(m_shellBackground));
	effectiveTokens.insert(QStringLiteral("--shell-panel"), colorStateValue(m_panel));
	effectiveTokens.insert(QStringLiteral("--text-strong"), colorStateValue(m_textStrong));
	effectiveTokens.insert(QStringLiteral("--accent"), colorStateValue(m_accent));
	effectiveTokens.insert(QStringLiteral("--accent-rgb"),
		QStringLiteral("%1, %2, %3").arg(m_accent.red()).arg(m_accent.green()).arg(m_accent.blue()));
	effectiveTokens.insert(QStringLiteral("--surface-border"), colorStateValue(m_surfaceBorder));
	effectiveTokens.insert(QStringLiteral("--on-accent"), colorStateValue(onAccent));
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
	const QColor nextSurfaceRaised = tokens.surface0.isValid() ? tokens.surface0
		: mixColors(tokens.base, tokens.text, 0.06);
	const QColor nextSurfaceHover = tokens.surface1.isValid() ? tokens.surface1
		: mixColors(tokens.base, tokens.text, 0.10);
	const QColor nextSurfaceBorder = tokens.surface2.isValid() ? tokens.surface2 : tokens.border;
	const QColor nextMediaCanvas = tokens.mediaCanvas.isValid()
		? tokens.mediaCanvas : QColor(QStringLiteral("#05070a"));
	const QColor nextAccentHover = tokens.accentHover.isValid() ? tokens.accentHover
		: mixColors(tokens.accent, tokens.text, 0.18);
	const QColor nextSelected = tokens.accentSubtle.isValid() ? tokens.accentSubtle
													 : uiThemeColorWithAlpha(tokens.accent, 0.16);
	const bool changed = m_shellBackground != nextShell || m_panel != tokens.base
		|| m_surfaceRaised != nextSurfaceRaised || m_surfaceHover != nextSurfaceHover
		|| m_surfaceBorder != nextSurfaceBorder || m_mediaCanvas != nextMediaCanvas || m_rail != tokens.mantle
		|| m_strip != tokens.crust || m_divider != tokens.border || m_textStrong != tokens.text
		|| m_textMain != tokens.subtext0 || m_textMuted != tokens.overlay0 || m_accent != tokens.accent
		|| m_accentHover != nextAccentHover || m_accentSubtle != nextSelected
		|| m_selected != nextSelected || m_danger != tokens.danger || m_success != tokens.success
		|| m_warning != tokens.warning || m_focus != tokens.focusAccent || m_shellRadius != metrics.shellRadius
		|| m_innerRadius != metrics.innerRadius || m_spacing != metrics.spacing;
	if (!changed) {
		return;
	}

	m_shellBackground = nextShell;
	m_panel = tokens.base;
	m_surfaceRaised = nextSurfaceRaised;
	m_surfaceHover = nextSurfaceHover;
	m_surfaceBorder = nextSurfaceBorder;
	m_mediaCanvas = nextMediaCanvas;
	m_rail = tokens.mantle;
	m_strip = tokens.crust;
	m_divider = tokens.border;
	m_textStrong = tokens.text;
	m_textMain = tokens.subtext0;
	m_textMuted = tokens.overlay0;
	m_accent = tokens.accent;
	m_accentHover = nextAccentHover;
	m_accentSubtle = nextSelected;
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
	if ((normalizedTheme != QLatin1String("light") && normalizedTheme != QLatin1String("dark")
		 && normalizedTheme != QLatin1String("custom"))
		|| (normalizedLayout != QLatin1String("regular") && normalizedLayout != QLatin1String("compact"))) {
		return false;
	}
	const QString densityId = normalizedLayout == QLatin1String("compact")
		? QStringLiteral("compact") : QStringLiteral("comfortable");
	UiThemeTokens tokens = uiThemeTokensForThemeId(
		normalizedTheme == QLatin1String("custom") ? QStringLiteral("frappe") : normalizedTheme);
	QString themeId = normalizedTheme;
	if (normalizedTheme == QLatin1String("custom")) {
		// Deterministic synthetic custom appearance for screenshot gates. It uses
		// the same typed runtime path as loaded manifests while avoiding machine-
		// local theme files in CI.
		tokens.crust = QColor(QStringLiteral("#111827"));
		tokens.mantle = QColor(QStringLiteral("#172033"));
		tokens.base = QColor(QStringLiteral("#1f2937"));
		tokens.surface0 = QColor(QStringLiteral("#293548"));
		tokens.surface1 = QColor(QStringLiteral("#35445a"));
		tokens.surface2 = QColor(QStringLiteral("#47576e"));
		tokens.text = QColor(QStringLiteral("#f3f4f6"));
		tokens.subtext0 = QColor(QStringLiteral("#d1d5db"));
		tokens.overlay0 = QColor(QStringLiteral("#9ca3af"));
		tokens.border = tokens.surface2;
		tokens.mediaCanvas = QColor(QStringLiteral("#050b12"));
		applyUiThemeAccentOverride(tokens, QStringLiteral("rose"), {}, 60);
		themeId = QStringLiteral("visual-custom");
	}
	applyProductTokens(tokens, {}, {}, themeId,
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
