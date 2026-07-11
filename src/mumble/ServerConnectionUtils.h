// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_SERVERCONNECTIONUTILS_H_
#define MUMBLE_MUMBLE_SERVERCONNECTIONUTILS_H_

#include <QtCore/QString>

class QMimeData;

namespace ServerConnectionUtils {
QMimeData *createServerUrlMimeData(const QString &name, const QString &host, unsigned short port,
								   const QString &channel = QString());
}

#endif
