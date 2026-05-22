// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_

#include "Database.h"
#include "Settings.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QMap>
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

	void open(const QList< FavoriteServer > &favorites, const Settings &settings,
			  const QMap< UnresolvedServerAddress, unsigned int > &pingCache = {});
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);
	bool setFavoritePing(const QString &host, unsigned short port, quint32 ping,
						 std::optional< quint32 > users = std::nullopt,
						 std::optional< quint32 > maxUsers = std::nullopt);

	const QList< FavoriteServer > &favorites() const;

private:
	struct FavoriteTelemetry {
		bool hasPing       = false;
		bool hasUsers      = false;
		quint32 ping       = 0;
		quint32 users      = 0;
		quint32 maxUsers   = 0;
	};

	QList< FavoriteServer > m_favorites;
	QHash< QString, FavoriteTelemetry > m_favoriteTelemetry;
	int m_selectedFavoriteIndex = -1;
	QString m_defaultUsername;
	QString m_name;
	QString m_host;
	QString m_username;
	QString m_password;
	unsigned short m_port = 0;
	bool m_editorOpen     = false;

	QVariantMap favoriteItem(const FavoriteServer &server, int index, bool selected) const;
	void selectFavorite(int index);
	FavoriteServer currentFavorite() const;
	QVariantMap validationErrors() const;
	bool canSubmit() const;
	static QString favoriteTelemetryKey(const QString &host, unsigned short port);
};

#endif // MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_
