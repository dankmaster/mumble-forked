#ifndef MUMBLE_MUMBLE_QMLTHEMECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLTHEMECONTROLLER_H_

#include "ModernTheme.h"
#include "UiTheme.h"

#include <QtCore/QObject>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>

class QmlThemeController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QColor shellBackground READ shellBackground NOTIFY themeChanged)
	Q_PROPERTY(QColor panel READ panel NOTIFY themeChanged)
	Q_PROPERTY(QColor surfaceRaised READ surfaceRaised NOTIFY themeChanged)
	Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY themeChanged)
	Q_PROPERTY(QColor surfaceBorder READ surfaceBorder NOTIFY themeChanged)
	Q_PROPERTY(QColor mediaCanvas READ mediaCanvas NOTIFY themeChanged)
	Q_PROPERTY(QColor rail READ rail NOTIFY themeChanged)
	Q_PROPERTY(QColor strip READ strip NOTIFY themeChanged)
	Q_PROPERTY(QColor divider READ divider NOTIFY themeChanged)
	Q_PROPERTY(QColor textStrong READ textStrong NOTIFY themeChanged)
	Q_PROPERTY(QColor textMain READ textMain NOTIFY themeChanged)
	Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
	Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
	Q_PROPERTY(QColor accentHover READ accentHover NOTIFY themeChanged)
	Q_PROPERTY(QColor accentSubtle READ accentSubtle NOTIFY themeChanged)
	Q_PROPERTY(QColor selected READ selected NOTIFY themeChanged)
	Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
	Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
	Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
	Q_PROPERTY(QColor focus READ focus NOTIFY themeChanged)
	Q_PROPERTY(int shellRadius READ shellRadius NOTIFY themeChanged)
	Q_PROPERTY(int innerRadius READ innerRadius NOTIFY themeChanged)
	Q_PROPERTY(int spacing READ spacing NOTIFY themeChanged)
	Q_PROPERTY(bool compact READ compact NOTIFY densityChanged)
	Q_PROPERTY(QString themeId READ themeId NOTIFY themeStateChanged)
	Q_PROPERTY(QString themeSource READ themeSource NOTIFY themeStateChanged)
	Q_PROPERTY(QString densityId READ densityId NOTIFY densityChanged)
	Q_PROPERTY(QString accentId READ accentId NOTIFY themeStateChanged)
	Q_PROPERTY(QString railSide READ railSide NOTIFY themeStateChanged)
	Q_PROPERTY(QVariantMap state READ state NOTIFY themeStateChanged)

public:
	explicit QmlThemeController(QObject *parent = nullptr);

	QColor shellBackground() const { return m_shellBackground; }
	QColor panel() const { return m_panel; }
	QColor surfaceRaised() const { return m_surfaceRaised; }
	QColor surfaceHover() const { return m_surfaceHover; }
	QColor surfaceBorder() const { return m_surfaceBorder; }
	QColor mediaCanvas() const { return m_mediaCanvas; }
	QColor rail() const { return m_rail; }
	QColor strip() const { return m_strip; }
	QColor divider() const { return m_divider; }
	QColor textStrong() const { return m_textStrong; }
	QColor textMain() const { return m_textMain; }
	QColor textMuted() const { return m_textMuted; }
	QColor accent() const { return m_accent; }
	QColor accentHover() const { return m_accentHover; }
	QColor accentSubtle() const { return m_accentSubtle; }
	QColor selected() const { return m_selected; }
	QColor danger() const { return m_danger; }
	QColor success() const { return m_success; }
	QColor warning() const { return m_warning; }
	QColor focus() const { return m_focus; }
	int shellRadius() const { return m_shellRadius; }
	int innerRadius() const { return m_innerRadius; }
	int spacing() const { return m_spacing; }
	bool compact() const { return m_compact; }
	QString themeId() const { return m_themeId; }
	QString themeSource() const { return m_themeSource; }
	QString densityId() const { return m_densityId; }
	QString accentId() const { return m_accentId; }
	QString railSide() const { return m_railSide; }
	QVariantMap state() const;

	Q_INVOKABLE void refresh();
	bool applyProductAppearance(const QString &theme, const QString &density, const QString &accent,
							const QString &customAccent = QStringLiteral("#5ec8b0"), int customAccentStrength = 50);
	void applyTokens(const UiThemeTokens &tokens, const Mumble::ModernTheme::ThemeMetrics &metrics = {},
					 const QColor &shellBackground = {});
	bool applyVisualGateAppearance(const QString &theme, const QString &layout,
							   const QString &density = {});

signals:
	void themeChanged();
	void densityChanged();
	void themeStateChanged();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void applyProductTokens(UiThemeTokens tokens, Mumble::ModernTheme::ThemeMetrics metrics,
						const QColor &shellBackground, const QString &themeId, const QString &themeSource,
						const QString &densityId, const QString &accentId, const QString &customAccent,
						int customAccentStrength);
	QColor m_shellBackground = QColor(QStringLiteral("#20262f"));
	QColor m_panel = QColor(QStringLiteral("#262d38"));
	QColor m_surfaceRaised = QColor(QStringLiteral("#2e3742"));
	QColor m_surfaceHover = QColor(QStringLiteral("#384453"));
	QColor m_surfaceBorder = QColor(QStringLiteral("#384453"));
	QColor m_mediaCanvas = QColor(QStringLiteral("#05070a"));
	QColor m_rail = QColor(QStringLiteral("#1b2027"));
	QColor m_strip = QColor(QStringLiteral("#14181f"));
	QColor m_divider = QColor(QStringLiteral("#1fffffff"));
	QColor m_textStrong = QColor(QStringLiteral("#e7ecf3"));
	QColor m_textMain = QColor(QStringLiteral("#c3cbd6"));
	QColor m_textMuted = QColor(QStringLiteral("#8b94a3"));
	QColor m_accent = QColor(QStringLiteral("#5ec8b0"));
	QColor m_accentHover = QColor(QStringLiteral("#82ddca"));
	QColor m_accentSubtle = QColor(QStringLiteral("#295ec8b0"));
	QColor m_selected = QColor(QStringLiteral("#295ec8b0"));
	QColor m_danger = QColor(QStringLiteral("#ef4444"));
	QColor m_success = QColor(QStringLiteral("#5fd0a3"));
	QColor m_warning = QColor(QStringLiteral("#e0c574"));
	QColor m_focus = QColor(QStringLiteral("#5ec8b0"));
	int m_shellRadius = 16;
	int m_innerRadius = 11;
	int m_spacing = 12;
	bool m_compact = false;
	QString m_themeId = QStringLiteral("dark");
	QString m_themeSource = QStringLiteral("modernShell");
	QString m_densityId = QStringLiteral("comfortable");
	QString m_accentId = QStringLiteral("auto");
	QString m_railSide = QStringLiteral("right");
};

#endif
