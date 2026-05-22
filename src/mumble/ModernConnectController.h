// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_

#include "Database.h"
#include "Settings.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include <optional>

class ModernConnectController {
public:
	struct ConnectionRequest {
		QString host;
		unsigned short port = 0;
		QString username;
		QString password;
	};

	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< ConnectionRequest > connectionRequest;
		std::optional< QList< FavoriteServer > > favoritesToSave;
	};

	void open(const QList< FavoriteServer > &favorites, const Settings &settings);
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);

	const QList< FavoriteServer > &favorites() const;

private:
	QList< FavoriteServer > m_favorites;
	int m_selectedFavoriteIndex = -1;
	QString m_defaultUsername;
	QString m_name;
	QString m_host;
	QString m_username;
	QString m_password;
	unsigned short m_port = 0;

	void selectFavorite(int index);
	FavoriteServer currentFavorite() const;
	QVariantMap validationErrors() const;
	bool canSubmit() const;
};

#endif // MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_
