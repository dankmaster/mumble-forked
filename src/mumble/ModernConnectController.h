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
	struct ServerEntry {
		QString id;
		QString label;
		QString host;
		unsigned short port = 0;
		QString username;
		QString country;
		QString region;
		std::optional< quint32 > ping;
		std::optional< quint32 > users;
		std::optional< quint32 > maxUsers;
	};

	struct SourceOperationRequest {
		QString sourceID;
		QString operation;
		quint64 generation = 0;
	};

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
		std::optional< SourceOperationRequest > sourceOperation;
		std::optional< bool > publicListDisabledSetting;
	};

	void open(const QList< FavoriteServer > &favorites, const Settings &settings,
			  const QMap< UnresolvedServerAddress, unsigned int > &pingCache = {});
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);
	bool setFavoritePing(const QString &host, unsigned short port, quint32 ping,
						 std::optional< quint32 > users = std::nullopt,
						 std::optional< quint32 > maxUsers = std::nullopt);
	quint64 beginSourceRefresh(const QString &sourceID);
	bool applySourceServers(const QString &sourceID, quint64 generation, const QList< ServerEntry > &servers);
	bool applySourceError(const QString &sourceID, quint64 generation, const QString &message, bool retryable = true);
	bool applySourceUnavailable(const QString &sourceID, quint64 generation, const QString &message);
	bool setSourceUnavailable(const QString &sourceID, const QString &message);
	bool requestPublicListConsent(quint64 generation);

	const QList< FavoriteServer > &favorites() const;

private:
	struct FavoriteTelemetry {
		bool hasPing       = false;
		bool hasUsers      = false;
		quint32 ping       = 0;
		quint32 users      = 0;
		quint32 maxUsers   = 0;
	};
	struct SourceState {
		QList< ServerEntry > servers;
		QString status = QStringLiteral("idle");
		QString error;
		quint64 generation = 0;
		bool retryable = true;
	};
	struct PendingConfirmation {
		QString kind;
		QString targetID;
		QString title;
		QString message;
		quint64 generation = 0;
	};

	QList< FavoriteServer > m_favorites;
	QHash< QString, FavoriteTelemetry > m_favoriteTelemetry;
	QHash< QString, SourceState > m_sources;
	QHash< QString, QString > m_selectedServerIDs;
	QString m_activeSource = QStringLiteral("favorites");
	QString m_filter;
	int m_selectedFavoriteIndex = -1;
	int m_editorReturnFavoriteIndex = -1;
	QString m_defaultUsername;
	QString m_name;
	QString m_host;
	QString m_username;
	QString m_password;
	unsigned short m_port = 0;
	bool m_editorOpen     = false;
	FavoriteServer m_editorBaseline;
	PendingConfirmation m_pendingConfirmation;

	QVariantMap favoriteItem(const FavoriteServer &server, int index, bool selected) const;
	QVariantMap serverItem(const ServerEntry &server, const QString &sourceID, int index, bool selected) const;
	QVariantList sourceRows(const QString &sourceID) const;
	QVariantList sourceItems() const;
	void selectFavorite(int index);
	bool selectServer(const QString &sourceID, const QString &serverID, int fallbackIndex = -1);
	void loadServerIntoConnectionFields(const ServerEntry &server);
	void leaveEditor(bool restoreSelection);
	bool editorDirty() const;
	void requestDiscardConfirmation();
	void requestRemoveConfirmation(const QString &favoriteID);
	void clearConfirmation();
	FavoriteServer currentFavorite() const;
	QVariantMap validationErrors() const;
	bool canSubmit() const;
	static QString favoriteTelemetryKey(const QString &host, unsigned short port);
	static QString stableServerID(const QString &sourceID, const QString &providedID, const QString &host,
								 unsigned short port, const QString &label = QString());
	static bool validSourceID(const QString &sourceID);
};

#endif // MUMBLE_MUMBLE_MODERNCONNECTCONTROLLER_H_
