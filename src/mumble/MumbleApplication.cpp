// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "MumbleApplication.h"

#include "EnvUtils.h"
#include "MainWindow.h"
#include "Global.h"
#include "GlobalShortcut.h"
#include "UiTheme.h"

#if defined(Q_OS_WIN)
#	include "GlobalShortcut_win.h"
#endif

#include <QtGui/QFileOpenEvent>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

MumbleApplication *MumbleApplication::instance() {
	return static_cast< MumbleApplication * >(QCoreApplication::instance());
}

MumbleApplication::MumbleApplication(int &pargc, char **pargv) : QApplication(pargc, pargv) {
	connect(this, SIGNAL(commitDataRequest(QSessionManager &)), SLOT(onCommitDataRequest(QSessionManager &)),
			Qt::DirectConnection);
}

QString MumbleApplication::applicationVersionRootPath() {
	QString versionRoot = EnvUtils::getenv(QLatin1String("MUMBLE_VERSION_ROOT"));
	if (!versionRoot.isEmpty()) {
		return versionRoot;
	}
	return this->applicationDirPath();
}

bool MumbleApplication::notify(QObject *receiver, QEvent *event) {
	const QEvent::Type type = event ? event->type() : QEvent::None;
#ifdef Q_OS_WIN
	const bool applyAllTitleBars = type == QEvent::ApplicationPaletteChange || type == QEvent::ThemeChange;
	const bool applyReceiverTitleBar =
		type == QEvent::Show || type == QEvent::WinIdChange || type == QEvent::PaletteChange
		|| type == QEvent::StyleChange;
	QPointer< QWidget > receiverWidget =
		applyReceiverTitleBar ? qobject_cast< QWidget * >(receiver) : nullptr;
#endif
	const bool handled = QApplication::notify(receiver, event);
#ifdef Q_OS_WIN
	if (applyAllTitleBars) {
		applyUiThemeNativeTitleBars();
		return handled;
	}

	if (receiverWidget) {
		applyUiThemeNativeTitleBar(receiverWidget);
	}
#endif
	return handled;
}

void MumbleApplication::onCommitDataRequest(QSessionManager &) {
	// Make sure the config is saved and suppress the ask on quit message
	if (Global::get().mw) {
		Global::get().s.mumbleQuitNormally = true;
		Global::get().s.save();
		Global::get().mw->forceQuit = true;
		qWarning() << "Session likely ending. Suppressing ask on quit";
	}
}

bool MumbleApplication::event(QEvent *e) {
	if (e->type() == QEvent::FileOpen) {
		QFileOpenEvent *foe = static_cast< QFileOpenEvent * >(e);
		if (!Global::get().mw) {
			this->quLaunchURL = foe->url();
		} else {
			Global::get().mw->openUrl(foe->url());
		}
		return true;
	}
	return QApplication::event(e);
}

#ifdef Q_OS_WIN
bool MumbleApplication::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
	auto gsw = static_cast< GlobalShortcutWin * >(GlobalShortcutEngine::engine);
	if (!gsw) {
		return false;
	}

	auto msg = reinterpret_cast< const MSG * >(message);
	switch (msg->message) {
		case WM_INPUT:
			gsw->injectRawInputMessage(reinterpret_cast< HRAWINPUT >(msg->lParam));
			break;
		case WM_INPUT_DEVICE_CHANGE:
			// We don't care about GIDC_ARRIVAL because we add a device only when we receive input from it.
			if (msg->wParam == GIDC_REMOVAL) {
				// The device is not available anymore, free resources allocated for it.
				gsw->deviceRemoved(reinterpret_cast< const HANDLE >(msg->lParam));
			}
	}

	return false;
}
#endif
