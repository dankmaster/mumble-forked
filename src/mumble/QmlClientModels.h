// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
#define MUMBLE_MUMBLE_QMLCLIENTMODELS_H_

#include <QtCore/QAbstractListModel>
#include <QtCore/QObject>
#include <QtCore/QVariantList>

class ClientActionRegistry;

class ClientSessionController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString serverName READ serverName WRITE setServerName NOTIFY serverNameChanged)
	Q_PROPERTY(QString connectionLabel READ connectionLabel WRITE setConnectionLabel NOTIFY connectionLabelChanged)
	Q_PROPERTY(QString selfName READ selfName WRITE setSelfName NOTIFY selfNameChanged)
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged)
	Q_PROPERTY(bool selfMuted READ selfMuted WRITE setSelfMuted NOTIFY selfMutedChanged)
	Q_PROPERTY(bool selfDeafened READ selfDeafened WRITE setSelfDeafened NOTIFY selfDeafenedChanged)

public:
	explicit ClientSessionController(QObject *parent = nullptr);

	QString serverName() const;
	QString connectionLabel() const;
	QString selfName() const;
	bool connected() const;
	bool selfMuted() const;
	bool selfDeafened() const;

	void setServerName(const QString &value);
	void setConnectionLabel(const QString &value);
	void setSelfName(const QString &value);
	void setConnected(bool value);
	void setSelfMuted(bool value);
	void setSelfDeafened(bool value);

signals:
	void serverNameChanged();
	void connectionLabelChanged();
	void selfNameChanged();
	void connectedChanged();
	void selfMutedChanged();
	void selfDeafenedChanged();

private:
	QString m_serverName = QStringLiteral("Mumble");
	QString m_connectionLabel = QStringLiteral("Disconnected");
	QString m_selfName = QStringLiteral("You");
	bool m_connected = false;
	bool m_selfMuted = false;
	bool m_selfDeafened = false;
};

class StableListModel : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
	enum Role {
		StableIdRole = Qt::UserRole + 1,
		TitleRole,
		SubtitleRole,
		KindRole,
		SelectedRole,
		StatusRole,
		PayloadRole
	};

	explicit StableListModel(QObject *parent = nullptr);
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;
	Q_INVOKABLE QVariantMap get(int row) const;

	void replaceRows(const QVariantList &rows);
	void synchronizeRows(const QVariantList &rows);
	void upsertRow(const QVariantMap &row);
	void removeRow(const QString &stableId);
	void clear();

signals:
	void countChanged();

private:
	int indexOf(const QString &stableId) const;
	QVariantList m_rows;
};

class RoomModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
};

class ParticipantModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
};

class ChatTimelineModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
};

class AsyncOperationModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
};

class ActionModel final : public StableListModel {
	Q_OBJECT

public:
	explicit ActionModel(ClientActionRegistry *registry, QObject *parent = nullptr);
	Q_INVOKABLE bool trigger(const QString &actionId);

private:
	void refresh();
	ClientActionRegistry *m_registry = nullptr;
};

class QmlSelectionState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString scopeToken READ scopeToken WRITE setScopeToken NOTIFY scopeTokenChanged)
	Q_PROPERTY(QVariant selectedUserSession READ selectedUserSession WRITE setSelectedUserSession NOTIFY selectedUserSessionChanged)
	Q_PROPERTY(QVariant selectedVoiceChannelId READ selectedVoiceChannelId WRITE setSelectedVoiceChannelId NOTIFY selectedVoiceChannelIdChanged)

public:
	explicit QmlSelectionState(QObject *parent = nullptr);
	QString scopeToken() const;
	QVariant selectedUserSession() const;
	QVariant selectedVoiceChannelId() const;
	void setScopeToken(const QString &value);
	void setSelectedUserSession(const QVariant &value);
	void setSelectedVoiceChannelId(const QVariant &value);

signals:
	void scopeTokenChanged();
	void selectedUserSessionChanged();
	void selectedVoiceChannelIdChanged();

private:
	QString m_scopeToken;
	QVariant m_selectedUserSession;
	QVariant m_selectedVoiceChannelId;
};

class UiCommandController final : public QObject {
	Q_OBJECT

public:
	explicit UiCommandController(QObject *parent = nullptr);

	Q_INVOKABLE void selectScope(const QString &scopeToken);
	Q_INVOKABLE void joinVoiceChannel(const QString &scopeToken);
	Q_INVOKABLE void sendMessage(const QString &message);
	Q_INVOKABLE void invokeAction(const QString &actionId);
	Q_INVOKABLE void toggleSelfMute();
	Q_INVOKABLE void toggleSelfDeaf();

signals:
	void scopeSelectionRequested(const QString &scopeToken);
	void voiceJoinRequested(const QString &scopeToken);
	void messageSendRequested(const QString &message);
	void actionRequested(const QString &actionId);
	void selfMuteToggleRequested();
	void selfDeafToggleRequested();
};

#endif // MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
