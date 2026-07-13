// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ApplicationThemeObserver.h"

#include "UiTheme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>

ApplicationThemeObserver::ApplicationThemeObserver(QObject *parent) : QObject(parent) {
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->installEventFilter(this);
	}
}

ApplicationThemeObserver::~ApplicationThemeObserver() {
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->removeEventFilter(this);
	}
}

bool ApplicationThemeObserver::eventFilter(QObject *watched, QEvent *event) {
	if (watched == QCoreApplication::instance() && event
		&& (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange
			|| event->type() == QEvent::StyleChange)) {
		scheduleNativeThemeRefresh();
	}

	return QObject::eventFilter(watched, event);
}

void ApplicationThemeObserver::scheduleNativeThemeRefresh() {
	if (m_refreshPending) {
		return;
	}

	m_refreshPending = true;
	QTimer::singleShot(0, this, [this]() {
		m_refreshPending = false;
		applyUiThemeNativeTitleBars();
	});
}
