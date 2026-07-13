// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ExposeEventFilter.h"

#include <QtCore/QEvent>
#include <QtCore/QTimer>

#include <utility>

ExposeEventFilter::ExposeEventFilter(QObject *parent, std::function< void() > callback)
	: QObject(parent), m_callback(std::move(callback)) {
}

bool ExposeEventFilter::eventFilter(QObject *object, QEvent *event) {
	if (event->type() == QEvent::Expose) {
		object->removeEventFilter(this);
		QTimer::singleShot(0, [callback = std::move(m_callback)]() { callback(); });
		deleteLater();
	}
	return false;
}
