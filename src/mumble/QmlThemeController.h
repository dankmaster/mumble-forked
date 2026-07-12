#ifndef MUMBLE_MUMBLE_QMLTHEMECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLTHEMECONTROLLER_H_

#include "ModernTheme.h"
#include "UiTheme.h"

#include <QtCore/QObject>
#include <QtGui/QColor>

class QmlThemeController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QColor shellBackground READ shellBackground NOTIFY themeChanged)
	Q_PROPERTY(QColor panel READ panel NOTIFY themeChanged)
	Q_PROPERTY(QColor rail READ rail NOTIFY themeChanged)
	Q_PROPERTY(QColor strip READ strip NOTIFY themeChanged)
	Q_PROPERTY(QColor divider READ divider NOTIFY themeChanged)
	Q_PROPERTY(QColor textStrong READ textStrong NOTIFY themeChanged)
	Q_PROPERTY(QColor textMain READ textMain NOTIFY themeChanged)
	Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
	Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
	Q_PROPERTY(QColor selected READ selected NOTIFY themeChanged)
	Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
	Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
	Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
	Q_PROPERTY(QColor focus READ focus NOTIFY themeChanged)
	Q_PROPERTY(int shellRadius READ shellRadius NOTIFY themeChanged)
	Q_PROPERTY(int innerRadius READ innerRadius NOTIFY themeChanged)
	Q_PROPERTY(int spacing READ spacing NOTIFY themeChanged)

public:
	explicit QmlThemeController(QObject *parent = nullptr);

	QColor shellBackground() const { return m_shellBackground; }
	QColor panel() const { return m_panel; }
	QColor rail() const { return m_rail; }
	QColor strip() const { return m_strip; }
	QColor divider() const { return m_divider; }
	QColor textStrong() const { return m_textStrong; }
	QColor textMain() const { return m_textMain; }
	QColor textMuted() const { return m_textMuted; }
	QColor accent() const { return m_accent; }
	QColor selected() const { return m_selected; }
	QColor danger() const { return m_danger; }
	QColor success() const { return m_success; }
	QColor warning() const { return m_warning; }
	QColor focus() const { return m_focus; }
	int shellRadius() const { return m_shellRadius; }
	int innerRadius() const { return m_innerRadius; }
	int spacing() const { return m_spacing; }

	Q_INVOKABLE void refresh();
	void applyTokens(const UiThemeTokens &tokens, const Mumble::ModernTheme::ThemeMetrics &metrics = {},
					 const QColor &shellBackground = {});

signals:
	void themeChanged();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QColor m_shellBackground = QColor(QStringLiteral("#20262f"));
	QColor m_panel = QColor(QStringLiteral("#262d38"));
	QColor m_rail = QColor(QStringLiteral("#1b2027"));
	QColor m_strip = QColor(QStringLiteral("#14181f"));
	QColor m_divider = QColor(QStringLiteral("#1fffffff"));
	QColor m_textStrong = QColor(QStringLiteral("#e7ecf3"));
	QColor m_textMain = QColor(QStringLiteral("#c3cbd6"));
	QColor m_textMuted = QColor(QStringLiteral("#8b94a3"));
	QColor m_accent = QColor(QStringLiteral("#5ec8b0"));
	QColor m_selected = QColor(QStringLiteral("#295ec8b0"));
	QColor m_danger = QColor(QStringLiteral("#ef4444"));
	QColor m_success = QColor(QStringLiteral("#5fd0a3"));
	QColor m_warning = QColor(QStringLiteral("#e0c574"));
	QColor m_focus = QColor(QStringLiteral("#5ec8b0"));
	int m_shellRadius = 16;
	int m_innerRadius = 11;
	int m_spacing = 12;
};

#endif
