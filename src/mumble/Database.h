// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DATABASE_H_
#define MUMBLE_MUMBLE_DATABASE_H_

#include "Channel.h"
#include "Settings.h"
#include "UnresolvedServerAddress.h"
#include <QSqlDatabase>

struct FavoriteServer {
	QString qsName;
	QString qsUsername;
	QString qsPassword;
	QString qsHostname;
	QString qsUrl;
	unsigned short usPort;
};

class Database : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(Database)

	QSqlDatabase db;
	bool m_valid = false;
	bool m_compactOnDestruction = true;
	bool m_removeConnectionOnDestruction = false;
	QString m_initializationError;
	/// This function is called when no database location is configured
	/// in the config file. It tries to find an existing database file and
	/// creates a new one if none was found.
	bool findOrCreateDatabase();
	void initialize(const QString &dbname, const QString &databaseLocation, bool updateGlobalSettings,
					bool fatalOnFailure);

public:
	Database(const QString &dbname);
	/// Opens a thread-local connection without mutating the global settings.
	/// This is used by ServerHandler, whose database must be created and
	/// destroyed on the handler thread. Initialization failures are reported via
	/// isValid()/initializationError() instead of terminating the process.
	Database(const QString &dbname, const QString &databaseLocation, bool compactOnDestruction);
	~Database() Q_DECL_OVERRIDE;
	bool isValid() const;
	QString initializationError() const;

	QList< FavoriteServer > getFavorites();
	void setFavorites(const QList< FavoriteServer > &servers);
	void addFavorite(const FavoriteServer &server);
	bool isFavorite(const QString &hostname, unsigned short port);
	void setPassword(const QString &host, unsigned short port, const QString &user, const QString &pw);
	bool fuzzyMatch(QString &name, QString &user, QString &pw, QString &host, unsigned short port);

	bool isLocalIgnored(const QString &hash);
	void setLocalIgnored(const QString &hash, bool ignored);

	bool isLocalIgnoredTTS(const QString &hash);
	void setLocalIgnoredTTS(const QString &hash, bool ignoredTTS);

	bool isLocalMuted(const QString &hash);
	void setLocalMuted(const QString &hash, bool muted);
	void clearLocalMuted();

	float getUserLocalVolume(const QString &hash);
	void setUserLocalVolume(const QString &hash, float volume);
	std::optional< bool > getUserRemoteSpeechCleanup(const QString &hash);
	void setUserRemoteSpeechCleanup(const QString &hash, std::optional< bool > enabled);

	QString getUserLocalNickname(const QString &hash);
	void setUserLocalNickname(const QString &hash, const QString &nickname);

	ChannelFilterMode getChannelFilterMode(const QByteArray &server_cert_digest, unsigned int channel_id);
	void setChannelFilterMode(const QByteArray &server_cert_digest, unsigned int channel_id,
							  ChannelFilterMode filterMode);

	QMap< UnresolvedServerAddress, unsigned int > getPingCache();
	void setPingCache(const QMap< UnresolvedServerAddress, unsigned int > &cache);

	bool seenComment(const QString &hash, const QByteArray &commenthash);
	void setSeenComment(const QString &hash, const QByteArray &commenthash);

	QByteArray blob(const QByteArray &hash);
	void setBlob(const QByteArray &hash, const QByteArray &blob);

	QStringList getTokens(const QByteArray &digest);
	void setTokens(const QByteArray &digest, QStringList &tokens);

	QList< Shortcut > getShortcuts(const QByteArray &digest);
	void setShortcuts(const QByteArray &digest, const QList< Shortcut > &shortcuts);

	void addFriend(const QString &name, const QString &hash);
	void removeFriend(const QString &hash);
	const QString getFriend(const QString &hash);
	const QMap< QString, QString > getFriends();

	const QString getDigest(const QString &hostname, unsigned short port);
	void setDigest(const QString &hostname, unsigned short port, const QString &digest);

	bool getUdp(const QByteArray &digest);
	void setUdp(const QByteArray &digest, bool udp);
};

#endif
