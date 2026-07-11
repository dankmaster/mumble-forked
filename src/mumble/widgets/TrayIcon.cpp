// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "TrayIcon.h"

#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "Global.h"
#include "ServerHandler.h"

#	include "ModernContextMenuHost.h"

#include <QApplication>
#include <QCursor>
#include <QVariantMap>

#include <utility>

namespace {
	QString trayPlainActionText(QString text) {
		const QChar ampersandPlaceholder(0xE000);
		const QString placeholder(1, ampersandPlaceholder);
		text.replace(QStringLiteral("&&"), placeholder);
		text.remove(QLatin1Char('&'));
		text.replace(placeholder, QStringLiteral("&"));
		return text.trimmed();
	}

	bool trayMainWindowVisible() {
		return Global::get().mw && Global::get().mw->isVisible() && !Global::get().mw->isMinimized();
	}
}

TrayIcon::TrayIcon() : QSystemTrayIcon(Global::get().mw), m_statusIcon(Global::get().mw->qiIcon) {
	setIcon(m_statusIcon);

	setToolTip("Mumble");

	assert(Global::get().mw);
	assert(Global::get().l);

	QObject::connect(Global::get().mw, &MainWindow::talkingStatusChanged, this, &TrayIcon::on_icon_update);
	QObject::connect(Global::get().mw, &MainWindow::disconnectedFromServer, this, &TrayIcon::on_icon_update);
	QObject::connect(Global::get().mw, &MainWindow::windowMinimized, this, &TrayIcon::on_windowMinimized);
	QObject::connect(Global::get().mw, &MainWindow::windowVisibilityToggled, this, &TrayIcon::on_toggleShowHide);
	QObject::connect(
		Global::get().l, &Log::notificationSpawned, this,
		[this](QString title, QString body, QSystemTrayIcon::MessageIcon icon) { showMessage(title, body, icon); });

	QObject::connect(Global::get().l, &Log::highlightSpawned, this, &TrayIcon::on_timer_triggered);
	QObject::connect(Global::get().mw, &MainWindow::windowActivated, this, &TrayIcon::on_tray_unhighlight);

	m_highlightTimer = new QTimer(this);
	m_highlightTimer->setSingleShot(true);
	QObject::connect(m_highlightTimer, &QTimer::timeout, this, &TrayIcon::on_timer_triggered);

	QObject::connect(this, &QSystemTrayIcon::activated, this, &TrayIcon::on_icon_clicked);

	// messageClicked is buggy in Qt on some platforms and we can not do anything about this (QTBUG-87329)
	QObject::connect(this, &QSystemTrayIcon::messageClicked, this, &TrayIcon::on_showAction_triggered);

	m_showAction = new QAction(tr("Show"), Global::get().mw);
	QObject::connect(m_showAction, &QAction::triggered, this, &TrayIcon::on_showAction_triggered);

	m_hideAction = new QAction(tr("Hide"), Global::get().mw);
	QObject::connect(m_hideAction, &QAction::triggered, this, &TrayIcon::on_hideAction_triggered);

	m_contextMenu = new QMenu(Global::get().mw);
	QObject::connect(m_contextMenu, &QMenu::aboutToShow, this, &TrayIcon::updateNativeContextMenu);

	// Some window managers hate it when a tray icon sets an empty context menu...
	updateNativeContextMenu();

#if !defined(Q_OS_MAC)
	if (!shouldUseModernContextMenu()) {
		setContextMenu(m_contextMenu);
	}
#else
	setContextMenu(m_contextMenu);
#endif

	show();
}

TrayIcon::~TrayIcon() {
	if (m_modernContextMenu) {
		delete m_modernContextMenu.data();
		m_modernContextMenu.clear();
	}
}

void TrayIcon::on_icon_update() {
	std::reference_wrapper< QIcon > newIcon = Global::get().mw->qiIcon;

	const ClientUser *p = ClientUser::get(Global::get().uiSession);

	if (Global::get().s.bDeaf) {
		newIcon = Global::get().mw->qiIconDeafSelf;
	} else if (p && p->bDeaf) {
		newIcon = Global::get().mw->qiIconDeafServer;
	} else if (Global::get().s.bMute) {
		newIcon = Global::get().mw->qiIconMuteSelf;
	} else if (p && p->bMute) {
		newIcon = Global::get().mw->qiIconMuteServer;
	} else if (p && p->bSuppress) {
		newIcon = Global::get().mw->qiIconMuteSuppressed;
	} else if (Global::get().s.bStateInTray && Global::get().bPushToMute) {
		newIcon = Global::get().mw->qiIconMutePushToMute;
	} else if (p && Global::get().s.bStateInTray) {
		switch (p->tsState) {
			case Settings::Talking:
			case Settings::MutedTalking:
				newIcon = Global::get().mw->qiTalkingOn;
				break;
			case Settings::Whispering:
				newIcon = Global::get().mw->qiTalkingWhisper;
				break;
			case Settings::Shouting:
				newIcon = Global::get().mw->qiTalkingShout;
				break;
			case Settings::Passive:
				newIcon = Global::get().mw->qiTalkingOff;
				break;
		}
	}

	if (&newIcon.get() != &m_statusIcon.get()) {
		m_statusIcon = newIcon;
		setIcon(m_statusIcon);
	}
}

void TrayIcon::on_icon_clicked(QSystemTrayIcon::ActivationReason reason) {
	switch (reason) {
		case QSystemTrayIcon::Trigger:
#ifndef Q_OS_MAC
			// macOS is special as it both shows the context menu AND triggers the action.
			// We only want at most one of those and since we can not prevent showing
			// the menu, we skip the action.
			on_toggleShowHide();
#endif
			break;
		case QSystemTrayIcon::Unknown:
		case QSystemTrayIcon::Context:
#if !defined(Q_OS_MAC)
			if (shouldUseModernContextMenu()) {
				showModernContextMenu();
			}
#endif
			break;
		case QSystemTrayIcon::DoubleClick:
		case QSystemTrayIcon::MiddleClick:
			break;
	}
}

void TrayIcon::updateNativeContextMenu() {
	m_contextMenu->clear();

	if (trayMainWindowVisible()) {
		m_hideAction->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());
		m_contextMenu->addAction(m_hideAction);
	} else {
		m_contextMenu->addAction(m_showAction);
	}

	m_contextMenu->addSeparator();

	m_contextMenu->addAction(Global::get().mw->qaAudioMute);
	m_contextMenu->addAction(Global::get().mw->qaAudioDeaf);
	m_contextMenu->addSeparator();
	m_contextMenu->addAction(Global::get().mw->qaQuit);
}

void TrayIcon::showNativeFallbackMenu() {
	if (!m_contextMenu) {
		return;
	}

	updateNativeContextMenu();
	m_contextMenu->popup(QCursor::pos());
}

bool TrayIcon::shouldUseModernContextMenu() const {
#	if defined(Q_OS_MAC)
	return false;
#	else
	return Global::get().mw && true;
#	endif
}

ModernContextMenuHost *TrayIcon::ensureModernContextMenuHost() {
	if (m_modernContextMenu) {
		return m_modernContextMenu.data();
	}

	ModernContextMenuHost *host = new ModernContextMenuHost();
	host->setObjectName(QStringLiteral("modernTrayContextMenu"));
	m_modernContextMenu = host;

	QObject::connect(host, &ModernContextMenuHost::actionRequested, this,
					 [this](const QString &token, const int actionIndex) {
						 if (token != m_modernContextMenuToken || actionIndex < 0
							 || actionIndex >= m_modernContextMenuHandlers.size()) {
							 return;
						 }

						 const std::function< void() > handler = m_modernContextMenuHandlers.at(actionIndex);
						 if (handler) {
							 handler();
						 }
					 });

	QObject::connect(host, &ModernContextMenuHost::popupClosed, this, [this](const QString &token) {
		if (token == m_modernContextMenuToken) {
			clearModernContextMenuState();
		}
	});

	QObject::connect(host, &ModernContextMenuHost::hostFailed, this, [this](const QString &reason) {
		Q_UNUSED(reason);
		const bool shouldFallback = !m_modernContextMenuToken.isEmpty();
		clearModernContextMenuState();
		if (m_modernContextMenu) {
			ModernContextMenuHost *host = m_modernContextMenu.data();
			m_modernContextMenu.clear();
			host->deleteLater();
		}
		if (shouldFallback) {
			showNativeFallbackMenu();
		}
	});

	return host;
}

bool TrayIcon::showModernContextMenu() {
	ModernContextMenuHost *host = ensureModernContextMenuHost();
	if (!host) {
		showNativeFallbackMenu();
		return false;
	}

	const QVariantList items = buildModernContextMenuItems();
	if (items.isEmpty()) {
		showNativeFallbackMenu();
		return false;
	}

	m_modernContextMenuToken = QStringLiteral("tray:%1").arg(++m_modernContextMenuSerial);
	const QString token = m_modernContextMenuToken;
	const bool shown =
		host->showMenuAtGlobalPosition(token, items, QCursor::pos(), QString(), Global::get().mw->modernTrayMenuUiTweaks());
	if (!shown) {
		if (m_modernContextMenuToken == token) {
			clearModernContextMenuState();
			showNativeFallbackMenu();
		}
		return false;
	}

	return true;
}

QVariantList TrayIcon::buildModernContextMenuItems() {
	QVariantList items;
	m_modernContextMenuHandlers.clear();

	MainWindow *mw = Global::get().mw;
	if (!mw) {
		return items;
	}

	mw->on_qmServer_aboutToShow();
	mw->on_qmSelf_aboutToShow();
	mw->on_qmConfig_aboutToShow();

	const QVariantMap profileHeader = mw->modernTrayProfileHeaderState();
	if (!profileHeader.isEmpty()) {
		items.push_back(profileHeader);
	}

	if (trayMainWindowVisible()) {
		appendModernTrayAction(items, QStringLiteral("window.hide"), tr("Hide"),
							   QSystemTrayIcon::isSystemTrayAvailable(), false, QStringLiteral("eye-off"),
							   QString(), [this]() { on_hideAction_triggered(); });
	} else {
		appendModernTrayAction(items, QStringLiteral("window.show"), tr("Show"), true, false,
							   QStringLiteral("log-in"), QString(), [this]() { on_showAction_triggered(); });
	}

	appendModernTraySeparator(items);

	appendModernTrayAction(items, QStringLiteral("self.toggleMute"), tr("Mute Self"),
						   mw->qaAudioMute ? mw->qaAudioMute->isEnabled() : true, Global::get().s.bMute,
						   QStringLiteral("mic"), QString(), [mw]() {
							   if (mw) {
								   mw->handleModernShellAppAction(QStringLiteral("self.toggleMute"));
							   }
						   });
	appendModernTrayAction(items, QStringLiteral("self.toggleDeaf"), tr("Deafen Self"),
						   mw->qaAudioDeaf ? mw->qaAudioDeaf->isEnabled() : true, Global::get().s.bDeaf,
						   QStringLiteral("headphones"), QString(), [mw]() {
							   if (mw) {
								   mw->handleModernShellAppAction(QStringLiteral("self.toggleDeaf"));
							   }
						   });

	appendModernTraySeparator(items);

	const bool connected = Global::get().uiSession != 0 && Global::get().sh && Global::get().sh->isRunning();
	if (connected) {
		appendModernTrayAction(items, QStringLiteral("server.disconnect"), tr("Disconnect"),
							   mw->qaServerDisconnect ? mw->qaServerDisconnect->isEnabled() : true, false,
							   QStringLiteral("log-out"), QStringLiteral("danger"), [mw]() {
								   if (mw) {
									   mw->handleModernShellAppAction(QStringLiteral("server.disconnect"));
								   }
							   });
	} else {
		appendModernTrayAction(items, QStringLiteral("server.connect"), tr("Connect"),
							   mw->qaServerConnect ? mw->qaServerConnect->isEnabled() : true, false,
							   QStringLiteral("log-in"), QString(), [mw]() {
								   if (mw) {
									   mw->handleModernShellAppAction(QStringLiteral("server.connect"));
								   }
							   });
	}

	appendModernTrayAction(items, QStringLiteral("configure.settings"), tr("Settings"), true, false,
						   QStringLiteral("settings"), QString(), [mw]() {
							   if (mw) {
								   mw->handleModernShellAppAction(QStringLiteral("configure.settings"));
							   }
						   });

	appendModernTraySeparator(items);

	QString quitLabel = mw->qaQuit ? trayPlainActionText(mw->qaQuit->text()) : QString();
	if (quitLabel.isEmpty()) {
		quitLabel = tr("Quit Mumble");
	}
	appendModernTrayAction(items, QStringLiteral("server.quit"), quitLabel,
						   mw->qaQuit ? mw->qaQuit->isEnabled() : true, false, QStringLiteral("log-out"),
						   QStringLiteral("danger"), [mw]() {
							   if (mw) {
								   mw->handleModernShellAppAction(QStringLiteral("server.quit"));
							   }
						   });

	return items;
}

void TrayIcon::clearModernContextMenuState() {
	m_modernContextMenuToken.clear();
	m_modernContextMenuHandlers.clear();
}

void TrayIcon::appendModernTraySeparator(QVariantList &items) const {
	QVariantMap separator;
	separator.insert(QStringLiteral("kind"), QStringLiteral("separator"));
	items.push_back(separator);
}

void TrayIcon::appendModernTrayAction(QVariantList &items, const QString &id, const QString &label, const bool enabled,
									  const bool checked, const QString &icon, const QString &tone,
									  std::function< void() > handler) {
	QVariantMap item;
	item.insert(QStringLiteral("kind"), QStringLiteral("action"));
	item.insert(QStringLiteral("id"), id);
	item.insert(QStringLiteral("label"), label);
	item.insert(QStringLiteral("enabled"), enabled);
	item.insert(QStringLiteral("checked"), checked);
	item.insert(QStringLiteral("icon"), icon);
	if (!tone.isEmpty()) {
		item.insert(QStringLiteral("tone"), tone);
	}
	item.insert(QStringLiteral("actionIndex"), m_modernContextMenuHandlers.size());
	m_modernContextMenuHandlers.push_back(std::move(handler));
	items.push_back(item);
}

void TrayIcon::on_toggleShowHide() {
	if (trayMainWindowVisible()) {
		on_hideAction_triggered();
	} else {
		on_showAction_triggered();
	}
}

void TrayIcon::on_showAction_triggered() {
	Global::get().mw->showRaiseWindow();
	updateNativeContextMenu();
}

void TrayIcon::on_hideAction_triggered() {
	if (!QSystemTrayIcon::isSystemTrayAvailable()) {
		// The system reports that no system tray is available.
		// If we would hide Mumble now, there would be no way to
		// get it back...
		return;
	}

	if (qApp->activeModalWidget() || qApp->activePopupWidget()) {
		// There is one or multiple modal or popup window(s) active, which
		// would not be hidden by this call. So we also do not hide
		// the MainWindow...
		return;
	}

#ifndef Q_OS_MAC
	Global::get().mw->hide();
#else
	// Qt can not hide the window via the native macOS hide function. This should be re-evaluated with new Qt versions.
	// Instead we just minimize.
	Global::get().mw->setWindowState(Global::get().mw->windowState() | Qt::WindowMinimized);
#endif

	updateNativeContextMenu();
}

void TrayIcon::on_windowMinimized() {
	if (!Global::get().s.bHideInTray) {
		return;
	}

	on_hideAction_triggered();
}

void TrayIcon::on_tray_unhighlight() {
	if (m_highlightTimer == nullptr || !m_highlightTimer->isActive()) {
		return;
	}

	m_highlightTimer->stop();
	setIcon(m_statusIcon);
}

void TrayIcon::on_timer_triggered() {
	// We implement tray icon "highlighting" by blinking the
	// current status icon every few seconds until the MainWindow
	// receives focus.
	// This will only be happening, if the user selects "highlight"
	// for a specific message in the messages settings table.
	// Normal window highlighting - which desktops usually implement
	// by blinking the application in the task bar - is invisible
	// if the application is hidden to tray.

	switch (m_blinkState) {
		case BlinkState::RegularIcon:
			setIcon(Global::get().mw->m_iconInformation);
			m_highlightTimer->start(500);
			m_blinkState = BlinkState::BlinkIcon;
			break;
		case BlinkState::BlinkIcon:
			setIcon(m_statusIcon);
			m_highlightTimer->start(2000);
			m_blinkState = BlinkState::RegularIcon;
			break;
	}
}
