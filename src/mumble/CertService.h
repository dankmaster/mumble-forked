// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_CERTSERVICE_H_
#define MUMBLE_MUMBLE_CERTSERVICE_H_

#include "Settings.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

class CertService final {
public:
	static bool validate(const Settings::KeyPair &keyPair);
	static Settings::KeyPair generate(QString name = QString(), const QString &email = QString());
	static Settings::KeyPair importPkcs12(QByteArray data, const QString &password = QString());
	static QByteArray exportPkcs12(const Settings::KeyPair &keyPair);
};

#endif
