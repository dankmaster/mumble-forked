// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSERVERADMINCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNSERVERADMINCONTROLLER_H_

#include "Mumble.pb.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

/// A bounded, searchable projection of the registered-user snapshot. The
/// model deliberately exposes stable server user IDs rather than QModelIndex
/// values so QML can preserve selection across filtering and refreshes.
class ModernRegisteredUserListModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int totalCount READ totalCount NOTIFY projectionChanged)
	Q_PROPERTY(int filteredCount READ filteredCount NOTIFY projectionChanged)
	Q_PROPERTY(int page READ page WRITE setPage NOTIFY projectionChanged)
	Q_PROPERTY(int pageCount READ pageCount NOTIFY projectionChanged)
	Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY projectionChanged)
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY projectionChanged)
	Q_PROPERTY(QString selectedStableId READ selectedStableId WRITE setSelectedStableId NOTIFY selectionChanged)

public:
	enum Role {
		StableIdRole = Qt::UserRole + 1,
		UserIdRole,
		NameRole,
		LastSeenRole,
		LastSeenDisplayRole,
		LastChannelIdRole,
		LastChannelLabelRole,
		PendingRole
	};
	Q_ENUM(Role)

	explicit ModernRegisteredUserListModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QHash< int, QByteArray > roleNames() const override;

	int totalCount() const;
	int filteredCount() const;
	int page() const;
	int pageCount() const;
	int pageSize() const;
	QString filter() const;
	QString selectedStableId() const;

	Q_INVOKABLE void setPage(int page);
	Q_INVOKABLE void setPageSize(int pageSize);
	Q_INVOKABLE void setFilter(const QString &filter);
	Q_INVOKABLE void setSelectedStableId(const QString &stableId);
	Q_INVOKABLE QVariantMap item(const QString &stableId) const;

signals:
	void projectionChanged();
	void selectionChanged();

private:
	struct Entry {
		quint32 userId = 0;
		QString name;
		QString lastSeen;
		quint32 lastChannelId = 0;
		QString lastChannelLabel;
		bool hasLastChannel = false;
		bool pending = false;

		QString stableId() const;
	};

	friend class ModernRegisteredUsersController;
	void replaceEntries(QVector< Entry > entries, const QString &preferredSelection = QString());
	const Entry *findEntry(const QString &stableId) const;
	Entry *findEntry(const QString &stableId);
	void rebuildProjection(bool preservePage = true);
	QVariantMap itemMap(const Entry &entry) const;

	QVector< Entry > m_entries;
	QVector< int > m_filteredIndices;
	QVector< int > m_pageIndices;
	QString m_filter;
	QString m_selectedStableId;
	int m_page = 0;
	int m_pageSize = 50;
};

class ModernRegisteredUsersController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
	Q_PROPERTY(QString operationError READ operationError NOTIFY operationChanged)
	Q_PROPERTY(bool canManage READ canManage WRITE setCanManage NOTIFY permissionsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
	Q_PROPERTY(QVariantMap pendingConfirmation READ pendingConfirmation NOTIFY confirmationChanged)
	Q_PROPERTY(qulonglong loadGeneration READ loadGeneration NOTIFY stateChanged)

public:
	explicit ModernRegisteredUsersController(QObject *parent = nullptr);

	QAbstractItemModel *model();
	ModernRegisteredUserListModel *typedModel();
	QString state() const;
	QString errorMessage() const;
	QString operationError() const;
	bool canManage() const;
	bool busy() const;
	QVariantMap pendingConfirmation() const;
	qulonglong loadGeneration() const;

	void setCanManage(bool canManage);
	Q_INVOKABLE bool refresh();
	void applySnapshot(const MumbleProto::UserList &message,
					 const QHash< quint32, QString > &channelLabels = {}, qulonglong generation = 0);
	void applyLoadError(const QString &message, qulonglong generation = 0);

	Q_INVOKABLE bool beginRename(const QString &stableId, const QString &newName);
	Q_INVOKABLE bool beginUnregister(const QString &stableId);
	Q_INVOKABLE bool confirmPending();
	Q_INVOKABLE void cancelPending();
	void completeOperation(qulonglong operationId, bool accepted, const QString &error = QString());
	void reset();

signals:
	void stateChanged();
	void permissionsChanged();
	void operationChanged();
	void confirmationChanged();
	void refreshRequested(qulonglong generation);
	void updateRequested(qulonglong operationId, const MumbleProto::UserList &update);

private:
	enum class MutationKind { None, Rename, Unregister };
	struct PendingMutation {
		MutationKind kind = MutationKind::None;
		QString stableId;
		QString newName;
	};

	void setState(const QString &state, const QString &error = QString());
	void setOperationError(const QString &error);
	void clearPendingConfirmation();
	QString validationErrorForRename(const QString &stableId, const QString &newName) const;

	ModernRegisteredUserListModel m_model;
	QString m_state = QStringLiteral("idle");
	QString m_errorMessage;
	QString m_operationError;
	bool m_canManage = false;
	qulonglong m_loadGeneration = 0;
	qulonglong m_nextOperationId = 1;
	qulonglong m_activeOperationId = 0;
	PendingMutation m_pendingMutation;
	QVariantMap m_pendingConfirmation;
	QVector< ModernRegisteredUserListModel::Entry > m_rollbackEntries;
	QString m_rollbackSelection;
};

/// A bounded, searchable projection of the server ban snapshot. Ban entries
/// have no protocol ID, so stable IDs are derived from the same identity tuple
/// used by the classic editor: hash, normalized address and mask.
class ModernBanListModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int totalCount READ totalCount NOTIFY projectionChanged)
	Q_PROPERTY(int filteredCount READ filteredCount NOTIFY projectionChanged)
	Q_PROPERTY(int page READ page WRITE setPage NOTIFY projectionChanged)
	Q_PROPERTY(int pageCount READ pageCount NOTIFY projectionChanged)
	Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY projectionChanged)
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY projectionChanged)
	Q_PROPERTY(QString selectedStableId READ selectedStableId WRITE setSelectedStableId NOTIFY selectionChanged)

public:
	enum Role {
		StableIdRole = Qt::UserRole + 1,
		AddressRole,
		MaskRole,
		UserNameRole,
		HashRole,
		ReasonRole,
		StartUtcRole,
		DurationSecondsRole,
		ExpiresAtRole,
		PermanentRole,
		PendingRole,
		TitleRole,
		SubtitleRole
	};
	Q_ENUM(Role)

	explicit ModernBanListModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QHash< int, QByteArray > roleNames() const override;

	int totalCount() const;
	int filteredCount() const;
	int page() const;
	int pageCount() const;
	int pageSize() const;
	QString filter() const;
	QString selectedStableId() const;

	Q_INVOKABLE void setPage(int page);
	Q_INVOKABLE void setPageSize(int pageSize);
	Q_INVOKABLE void setFilter(const QString &filter);
	Q_INVOKABLE void setSelectedStableId(const QString &stableId);
	Q_INVOKABLE QVariantMap item(const QString &stableId) const;

signals:
	void projectionChanged();
	void selectionChanged();

private:
	struct Entry {
		QString stableId;
		QByteArray addressBytes;
		QString address;
		int mask = 0;
		QString userName;
		QString hash;
		QString reason;
		QDateTime startUtc;
		quint32 durationSeconds = 0;
		bool pending = false;

		QString identityKey() const;
	};

	friend class ModernBanListController;
	void replaceEntries(QVector< Entry > entries, const QString &preferredSelection = QString(),
					  const QString &preferredIdentityKey = QString());
	const Entry *findEntry(const QString &stableId) const;
	Entry *findEntry(const QString &stableId);
	void rebuildProjection(bool preservePage = true);
	QVariantMap itemMap(const Entry &entry) const;

	QVector< Entry > m_entries;
	QVector< int > m_filteredIndices;
	QVector< int > m_pageIndices;
	QString m_filter;
	QString m_selectedStableId;
	int m_page = 0;
	int m_pageSize = 50;
};

class ModernBanListController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
	Q_PROPERTY(QString operationError READ operationError NOTIFY operationChanged)
	Q_PROPERTY(bool canManage READ canManage WRITE setCanManage NOTIFY permissionsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
	Q_PROPERTY(QVariantMap pendingConfirmation READ pendingConfirmation NOTIFY confirmationChanged)
	Q_PROPERTY(qulonglong loadGeneration READ loadGeneration NOTIFY stateChanged)

public:
	explicit ModernBanListController(QObject *parent = nullptr);

	QAbstractItemModel *model();
	ModernBanListModel *typedModel();
	QString state() const;
	QString errorMessage() const;
	QString operationError() const;
	bool canManage() const;
	bool busy() const;
	QVariantMap pendingConfirmation() const;
	qulonglong loadGeneration() const;

	void setCanManage(bool canManage);
	Q_INVOKABLE bool refresh();
	void applySnapshot(const MumbleProto::BanList &message, qulonglong generation = 0);
	void applyLoadError(const QString &message, qulonglong generation = 0);

	Q_INVOKABLE QVariantMap validateDraft(const QVariantMap &draft, const QString &editingStableId = QString()) const;
	Q_INVOKABLE bool beginAdd(const QVariantMap &draft);
	Q_INVOKABLE bool beginEdit(const QString &stableId, const QVariantMap &draft);
	Q_INVOKABLE bool beginRemove(const QString &stableId);
	Q_INVOKABLE bool confirmPending();
	Q_INVOKABLE void cancelPending();
	void completeOperation(qulonglong operationId, bool accepted, const QString &error = QString());
	void reset();

signals:
	void stateChanged();
	void permissionsChanged();
	void operationChanged();
	void confirmationChanged();
	void refreshRequested(qulonglong generation);
	void updateRequested(qulonglong operationId, const MumbleProto::BanList &update);

private:
	enum class MutationKind { None, Add, Edit, Remove };
	struct PendingMutation {
		MutationKind kind = MutationKind::None;
		QString stableId;
		ModernBanListModel::Entry entry;
	};

	void setState(const QString &state, const QString &error = QString());
	void setOperationError(const QString &error);
	void clearPendingConfirmation();
	bool entryFromDraft(const QVariantMap &draft, ModernBanListModel::Entry *entry,
					QString *field, QString *error) const;
	MumbleProto::BanList protocolSnapshot() const;

	ModernBanListModel m_model;
	QString m_state = QStringLiteral("idle");
	QString m_errorMessage;
	QString m_operationError;
	bool m_canManage = false;
	qulonglong m_loadGeneration = 0;
	qulonglong m_nextOperationId = 1;
	qulonglong m_activeOperationId = 0;
	PendingMutation m_pendingMutation;
	QVariantMap m_pendingConfirmation;
	QVector< ModernBanListModel::Entry > m_rollbackEntries;
	QString m_rollbackSelection;
};

class ModernServerAdminController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QObject *users READ users CONSTANT)
	Q_PROPERTY(QObject *bans READ bans CONSTANT)

public:
	explicit ModernServerAdminController(QObject *parent = nullptr);
	ModernRegisteredUsersController *users();
	ModernBanListController *bans();

private:
	ModernRegisteredUsersController m_users;
	ModernBanListController m_bans;
};

#endif // MUMBLE_MUMBLE_MODERNSERVERADMINCONTROLLER_H_
