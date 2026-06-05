// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
#define MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_

#include <functional>

#include <QAction>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
class ModernContextMenuHost;
#endif

class TrayIcon : public QSystemTrayIcon {
	Q_OBJECT

public:
	TrayIcon();
	~TrayIcon() override;

public slots:
	void on_hideAction_triggered();
	void on_showAction_triggered();
	void on_toggleShowHide();

	void on_icon_update();
	void on_tray_unhighlight();

private:
	enum class BlinkState {
		RegularIcon,
		BlinkIcon,
	};

	std::reference_wrapper< QIcon > m_statusIcon;
	BlinkState m_blinkState  = BlinkState::RegularIcon;
	bool m_blinkingIcon      = false;
	QMenu *m_contextMenu     = nullptr;
	QAction *m_showAction    = nullptr;
	QAction *m_hideAction    = nullptr;
	QTimer *m_highlightTimer = nullptr;

	void updateNativeContextMenu();
	void showNativeFallbackMenu();

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	bool shouldUseModernContextMenu() const;
	ModernContextMenuHost *ensureModernContextMenuHost();
	bool showModernContextMenu();
	QVariantList buildModernContextMenuItems();
	void clearModernContextMenuState();
	void appendModernTraySeparator(QVariantList &items) const;
	void appendModernTrayAction(QVariantList &items, const QString &id, const QString &label, bool enabled,
								bool checked, const QString &icon, const QString &tone,
								std::function< void() > handler);

	QPointer< ModernContextMenuHost > m_modernContextMenu;
	QString m_modernContextMenuToken;
	QVector< std::function< void() > > m_modernContextMenuHandlers;
	quint64 m_modernContextMenuSerial = 0;
#endif

private slots:
	void on_icon_clicked(QSystemTrayIcon::ActivationReason reason);
	void on_windowMinimized();
	void on_timer_triggered();
};

#endif // MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
