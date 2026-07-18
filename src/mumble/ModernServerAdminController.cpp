// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernServerAdminController.h"

#include "HostAddress.h"
#include "QmlClientModels.h"
#include "QtUtils.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QLocale>
#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>

#include <algorithm>
#include <limits>

namespace {
	constexpr int MinimumPageSize = 10;
	constexpr int MaximumPageSize = 100;
	constexpr int IPv4MaskOffset  = 96;

	int boundedPageSize(const int pageSize) {
		return std::clamp(pageSize, MinimumPageSize, MaximumPageSize);
	}

	int pageCountFor(const int count, const int pageSize) {
		return count <= 0 ? 1 : ((count + pageSize - 1) / pageSize);
	}

	QString normalizedFilter(const QString &filter) {
		return filter.simplified();
	}

	bool containsInsensitive(const QString &value, const QString &needle) {
		return needle.isEmpty() || value.contains(needle, Qt::CaseInsensitive);
	}

	bool acceptsFrontendStateMutation(const QObject *object) {
		return !object->property(QmlVisualFixtureMutation::OverrideProperty).toBool()
			|| object->property(QmlVisualFixtureMutation::WriteProperty).toBool();
	}

	QString banStableId(const QString &identityKey) {
		return QStringLiteral("ban:%1")
			.arg(QString::fromLatin1(QCryptographicHash::hash(identityKey.toUtf8(), QCryptographicHash::Sha256).toHex()));
	}

	QDateTime parseUtcDateTime(const QVariant &value) {
		QDateTime dateTime;
		if (value.metaType().id() == QMetaType::QDateTime) {
			dateTime = value.toDateTime();
		} else {
			dateTime = QDateTime::fromString(value.toString().trimmed(), Qt::ISODate);
		}
		return dateTime.isValid() ? dateTime.toUTC() : QDateTime();
	}

	QString displayDateTime(const QDateTime &dateTime) {
		return dateTime.isValid() ? QLocale().toString(dateTime.toLocalTime(), QLocale::ShortFormat) : QString();
	}

	QString fallbackOperationError(const QString &error) {
		return error.trimmed().isEmpty() ? QObject::tr("The server did not accept the change.") : error.trimmed();
	}
} // namespace

QString ModernRegisteredUserListModel::Entry::stableId() const {
	return QStringLiteral("user:%1").arg(userId);
}

ModernRegisteredUserListModel::ModernRegisteredUserListModel(QObject *parent) : QAbstractListModel(parent) {
}

int ModernRegisteredUserListModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : static_cast< int >(m_pageIndices.size());
}

QVariant ModernRegisteredUserListModel::data(const QModelIndex &index, const int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_pageIndices.size()) return {};
	const Entry &entry = m_entries.at(m_pageIndices.at(index.row()));
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return entry.name;
		case StableIdRole:
			return entry.stableId();
		case UserIdRole:
			return entry.userId;
		case LastSeenRole:
			return entry.lastSeen;
		case LastSeenDisplayRole: {
			const QDateTime dateTime = QDateTime::fromString(entry.lastSeen, Qt::ISODate);
			return dateTime.isValid() ? displayDateTime(dateTime) : tr("Never");
		}
		case LastChannelIdRole:
			return entry.hasLastChannel ? QVariant::fromValue(entry.lastChannelId) : QVariant();
		case LastChannelLabelRole:
			return entry.lastChannelLabel;
		case PendingRole:
			return entry.pending;
		default:
			return {};
	}
}

QHash< int, QByteArray > ModernRegisteredUserListModel::roleNames() const {
	return { { StableIdRole, "stableId" },
			 { UserIdRole, "userId" },
			 { NameRole, "name" },
			 { LastSeenRole, "lastSeen" },
			 { LastSeenDisplayRole, "lastSeenDisplay" },
			 { LastChannelIdRole, "lastChannelId" },
			 { LastChannelLabelRole, "lastChannelLabel" },
			 { PendingRole, "pending" } };
}

int ModernRegisteredUserListModel::totalCount() const { return static_cast< int >(m_entries.size()); }
int ModernRegisteredUserListModel::filteredCount() const { return static_cast< int >(m_filteredIndices.size()); }
int ModernRegisteredUserListModel::page() const { return m_page; }
int ModernRegisteredUserListModel::pageCount() const { return pageCountFor(filteredCount(), m_pageSize); }
int ModernRegisteredUserListModel::pageSize() const { return m_pageSize; }
QString ModernRegisteredUserListModel::filter() const { return m_filter; }
QString ModernRegisteredUserListModel::selectedStableId() const { return m_selectedStableId; }

void ModernRegisteredUserListModel::setPage(const int page) {
	const int bounded = std::clamp(page, 0, pageCount() - 1);
	if (m_page == bounded) return;
	m_page = bounded;
	rebuildProjection(true);
}

void ModernRegisteredUserListModel::setPageSize(const int pageSize) {
	const int bounded = boundedPageSize(pageSize);
	if (m_pageSize == bounded) return;
	m_pageSize = bounded;
	m_page     = 0;
	rebuildProjection(false);
}

void ModernRegisteredUserListModel::setFilter(const QString &filter) {
	const QString normalized = normalizedFilter(filter);
	if (m_filter == normalized) return;
	m_filter = normalized;
	m_page   = 0;
	rebuildProjection(false);
}

void ModernRegisteredUserListModel::setSelectedStableId(const QString &stableId) {
	const QString normalized = stableId.trimmed();
	if (!normalized.isEmpty() && !findEntry(normalized)) return;
	if (m_selectedStableId == normalized) return;
	m_selectedStableId = normalized;

	if (!normalized.isEmpty()) {
		for (int i = 0; i < m_filteredIndices.size(); ++i) {
			if (m_entries.at(m_filteredIndices.at(i)).stableId() == normalized) {
				const int targetPage = i / m_pageSize;
				if (targetPage != m_page) {
					m_page = targetPage;
					rebuildProjection(true);
				}
				break;
			}
		}
	}
	emit selectionChanged();
}

QVariantMap ModernRegisteredUserListModel::item(const QString &stableId) const {
	const Entry *entry = findEntry(stableId.trimmed());
	return entry ? itemMap(*entry) : QVariantMap();
}

void ModernRegisteredUserListModel::replaceEntries(QVector< Entry > entries, const QString &preferredSelection) {
	std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
		const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
		return byName == 0 ? left.userId < right.userId : byName < 0;
	});

	QString nextSelection = preferredSelection.isEmpty() ? m_selectedStableId : preferredSelection;
	const auto containsSelection = [&entries, &nextSelection]() {
		return std::any_of(entries.cbegin(), entries.cend(), [&nextSelection](const Entry &entry) {
			return entry.stableId() == nextSelection;
		});
	};
	if (!nextSelection.isEmpty() && !containsSelection()) nextSelection.clear();
	const bool selectionWasChanged = nextSelection != m_selectedStableId;
	m_selectedStableId             = nextSelection;
	m_entries                      = std::move(entries);
	rebuildProjection(true);
	if (selectionWasChanged) emit selectionChanged();
}

const ModernRegisteredUserListModel::Entry *ModernRegisteredUserListModel::findEntry(const QString &stableId) const {
	for (const Entry &entry : m_entries) {
		if (entry.stableId() == stableId) return &entry;
	}
	return nullptr;
}

ModernRegisteredUserListModel::Entry *ModernRegisteredUserListModel::findEntry(const QString &stableId) {
	for (Entry &entry : m_entries) {
		if (entry.stableId() == stableId) return &entry;
	}
	return nullptr;
}

void ModernRegisteredUserListModel::rebuildProjection(const bool preservePage) {
	beginResetModel();
	m_filteredIndices.clear();
	for (int index = 0; index < m_entries.size(); ++index) {
		const Entry &entry = m_entries.at(index);
		const bool matches = m_filter.isEmpty() || containsInsensitive(entry.name, m_filter)
						 || containsInsensitive(QString::number(entry.userId), m_filter)
						 || containsInsensitive(entry.lastChannelLabel, m_filter)
						 || containsInsensitive(entry.lastSeen, m_filter);
		if (matches) m_filteredIndices.push_back(index);
	}
	if (!preservePage) m_page = 0;
	m_page = std::clamp(m_page, 0, pageCountFor(m_filteredIndices.size(), m_pageSize) - 1);
	const int first = m_page * m_pageSize;
	const int last  = std::min(first + m_pageSize, static_cast< int >(m_filteredIndices.size()));
	m_pageIndices   = m_filteredIndices.mid(first, last - first);
	endResetModel();
	emit projectionChanged();
}

QVariantMap ModernRegisteredUserListModel::itemMap(const Entry &entry) const {
	QVariantMap item { { QStringLiteral("stableId"), entry.stableId() },
					   { QStringLiteral("userId"), entry.userId },
					   { QStringLiteral("name"), entry.name },
					   { QStringLiteral("lastSeen"), entry.lastSeen },
					   { QStringLiteral("lastChannelLabel"), entry.lastChannelLabel },
					   { QStringLiteral("pending"), entry.pending } };
	if (entry.hasLastChannel) item.insert(QStringLiteral("lastChannelId"), entry.lastChannelId);
	return item;
}

ModernRegisteredUsersController::ModernRegisteredUsersController(QObject *parent) : QObject(parent), m_model(this) {
}

QAbstractItemModel *ModernRegisteredUsersController::model() { return &m_model; }
ModernRegisteredUserListModel *ModernRegisteredUsersController::typedModel() { return &m_model; }
QString ModernRegisteredUsersController::state() const { return m_state; }
QString ModernRegisteredUsersController::errorMessage() const { return m_errorMessage; }
QString ModernRegisteredUsersController::operationError() const { return m_operationError; }
bool ModernRegisteredUsersController::canManage() const { return m_canManage; }
bool ModernRegisteredUsersController::busy() const { return m_activeOperationId != 0; }
QVariantMap ModernRegisteredUsersController::pendingConfirmation() const { return m_pendingConfirmation; }
qulonglong ModernRegisteredUsersController::loadGeneration() const { return m_loadGeneration; }

void ModernRegisteredUsersController::setCanManage(const bool canManage) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (m_canManage == canManage) return;
	m_canManage = canManage;
	if (!m_canManage) clearPendingConfirmation();
	emit permissionsChanged();
}

bool ModernRegisteredUsersController::refresh() {
	if (!acceptsFrontendStateMutation(this)) return false;
	if (busy() || m_state == QLatin1String("loading") || m_state == QLatin1String("refreshing")) return false;
	setOperationError(QString());
	++m_loadGeneration;
	setState(m_model.totalCount() > 0 ? QStringLiteral("refreshing") : QStringLiteral("loading"));
	emit refreshRequested(m_loadGeneration);
	return true;
}

void ModernRegisteredUsersController::applySnapshot(const MumbleProto::UserList &message,
												 const QHash< quint32, QString > &channelLabels,
												 const qulonglong generation) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (generation != 0 && generation != m_loadGeneration) return;
	QVector< ModernRegisteredUserListModel::Entry > entries;
	entries.reserve(message.users_size());
	for (int index = 0; index < message.users_size(); ++index) {
		const auto &user = message.users(index);
		ModernRegisteredUserListModel::Entry entry;
		entry.userId   = user.user_id();
		entry.name     = user.has_name() ? u8(user.name()) : tr("Unnamed user");
		entry.lastSeen = user.has_last_seen() ? u8(user.last_seen()) : QString();
		if (user.has_last_channel()) {
			entry.hasLastChannel   = true;
			entry.lastChannelId    = user.last_channel();
			entry.lastChannelLabel = channelLabels.value(entry.lastChannelId,
				tr("Room %1").arg(entry.lastChannelId));
		}
		entries.push_back(std::move(entry));
	}
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	clearPendingConfirmation();
	m_model.replaceEntries(std::move(entries));
	// refresh() and confirmPending() clear stale errors before issuing work. Do
	// not erase a rejection reported by completeOperation(false) when the
	// mandatory post-mutation snapshot arrives immediately afterwards.
	setState(QStringLiteral("ready"));
	emit operationChanged();
}

void ModernRegisteredUsersController::applyLoadError(const QString &message, const qulonglong generation) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (generation != 0 && generation != m_loadGeneration) return;
	setState(QStringLiteral("error"), message.trimmed().isEmpty() ? tr("Unable to load registered users.") : message);
}

bool ModernRegisteredUsersController::beginRename(const QString &stableId, const QString &newName) {
	setOperationError(QString());
	const QString error = validationErrorForRename(stableId.trimmed(), newName);
	if (!error.isEmpty()) {
		setOperationError(error);
		return false;
	}
	const auto *entry = m_model.findEntry(stableId.trimmed());
	m_pendingMutation = { MutationKind::Rename, stableId.trimmed(), newName.trimmed() };
	m_pendingConfirmation = {
		{ QStringLiteral("kind"), QStringLiteral("renameUser") },
		{ QStringLiteral("title"), tr("Rename registered user?") },
		{ QStringLiteral("message"), tr("Rename %1 to %2 on this server?").arg(entry->name, newName.trimmed()) },
		{ QStringLiteral("confirmLabel"), tr("Rename") },
		{ QStringLiteral("tone"), QStringLiteral("accent") },
		{ QStringLiteral("stableId"), stableId.trimmed() }
	};
	emit confirmationChanged();
	return true;
}

bool ModernRegisteredUsersController::beginUnregister(const QString &stableId) {
	setOperationError(QString());
	const auto *entry = m_model.findEntry(stableId.trimmed());
	if (!m_canManage || busy() || !entry) {
		setOperationError(!m_canManage ? tr("You do not have permission to manage registered users.")
										 : tr("The selected user is no longer available."));
		return false;
	}
	m_pendingMutation = { MutationKind::Unregister, stableId.trimmed(), QString() };
	m_pendingConfirmation = {
		{ QStringLiteral("kind"), QStringLiteral("unregisterUser") },
		{ QStringLiteral("title"), tr("Unregister user?") },
		{ QStringLiteral("message"), tr("Remove %1 from the server's registered users?").arg(entry->name) },
		{ QStringLiteral("confirmLabel"), tr("Unregister") },
		{ QStringLiteral("tone"), QStringLiteral("danger") },
		{ QStringLiteral("stableId"), stableId.trimmed() }
	};
	emit confirmationChanged();
	return true;
}

bool ModernRegisteredUsersController::confirmPending() {
	if (!m_canManage || busy() || m_pendingMutation.kind == MutationKind::None) return false;
	auto *entry = m_model.findEntry(m_pendingMutation.stableId);
	if (!entry) {
		setOperationError(tr("The selected user is no longer available."));
		clearPendingConfirmation();
		return false;
	}

	m_rollbackEntries   = m_model.m_entries;
	m_rollbackSelection = m_model.selectedStableId();
	// QVector storage is implicitly shared. Detach the optimistic copy before
	// mutating through an Entry pointer so the rollback snapshot remains exact.
	m_model.m_entries.detach();
	entry = m_model.findEntry(m_pendingMutation.stableId);
	if (!entry) return false;
	MumbleProto::UserList update;
	auto *changed = update.add_users();
	changed->set_user_id(entry->userId);
	const qulonglong operationId = m_nextOperationId++;
	if (m_pendingMutation.kind == MutationKind::Rename) {
		changed->set_name(u8(m_pendingMutation.newName));
		entry->name    = m_pendingMutation.newName;
		entry->pending = true;
		m_model.replaceEntries(m_model.m_entries, entry->stableId());
	} else {
		changed->clear_name();
		const QString removedStableId = entry->stableId();
		auto &entries = m_model.m_entries;
		entries.erase(std::remove_if(entries.begin(), entries.end(), [&removedStableId](const auto &candidate) {
			return candidate.stableId() == removedStableId;
		}), entries.end());
		m_model.replaceEntries(entries);
	}
	m_activeOperationId = operationId;
	clearPendingConfirmation();
	setOperationError(QString());
	emit operationChanged();
	emit updateRequested(operationId, update);
	return true;
}

void ModernRegisteredUsersController::cancelPending() {
	clearPendingConfirmation();
}

void ModernRegisteredUsersController::completeOperation(const qulonglong operationId, const bool accepted,
														 const QString &error) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (operationId == 0 || operationId != m_activeOperationId) return;
	const QString selection = m_model.selectedStableId();
	if (!accepted) {
		m_model.replaceEntries(m_rollbackEntries, m_rollbackSelection);
		setOperationError(fallbackOperationError(error));
	} else {
		auto entries = m_model.m_entries;
		for (auto &entry : entries) entry.pending = false;
		m_model.replaceEntries(std::move(entries), selection);
		setOperationError(QString());
	}
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	emit operationChanged();
}

void ModernRegisteredUsersController::reset() {
	if (!acceptsFrontendStateMutation(this)) return;
	++m_loadGeneration;
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	clearPendingConfirmation();
	m_model.replaceEntries({});
	setOperationError(QString());
	setState(QStringLiteral("idle"));
	emit operationChanged();
}

void ModernRegisteredUsersController::setState(const QString &state, const QString &error) {
	if (m_state == state && m_errorMessage == error) return;
	m_state        = state;
	m_errorMessage = error;
	emit stateChanged();
}

void ModernRegisteredUsersController::setOperationError(const QString &error) {
	if (m_operationError == error) return;
	m_operationError = error;
	emit operationChanged();
}

void ModernRegisteredUsersController::clearPendingConfirmation() {
	if (m_pendingMutation.kind == MutationKind::None && m_pendingConfirmation.isEmpty()) return;
	m_pendingMutation = {};
	m_pendingConfirmation.clear();
	emit confirmationChanged();
}

QString ModernRegisteredUsersController::validationErrorForRename(const QString &stableId,
															const QString &newName) const {
	if (!m_canManage) return tr("You do not have permission to manage registered users.");
	if (busy()) return tr("Wait for the current user change to finish.");
	const auto *entry = m_model.findEntry(stableId);
	if (!entry) return tr("The selected user is no longer available.");
	const QString normalized = newName.trimmed();
	if (normalized.isEmpty()) return tr("Enter a user name.");
	if (normalized.size() > 256) return tr("The user name is too long.");
	if (normalized == entry->name) return tr("The user already has that name.");
	for (const auto &candidate : m_model.m_entries) {
		if (candidate.userId != entry->userId && candidate.name.compare(normalized, Qt::CaseInsensitive) == 0) {
			return tr("Another registered user already has that name.");
		}
	}
	return {};
}

QString ModernBanListModel::Entry::identityKey() const {
	return hash + QLatin1Char('\n') + address + QLatin1Char('\n') + QString::number(mask);
}

ModernBanListModel::ModernBanListModel(QObject *parent) : QAbstractListModel(parent) {
}

int ModernBanListModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : static_cast< int >(m_pageIndices.size());
}

QVariant ModernBanListModel::data(const QModelIndex &index, const int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_pageIndices.size()) return {};
	const Entry &entry = m_entries.at(m_pageIndices.at(index.row()));
	const QString title = !entry.userName.isEmpty() ? entry.userName
		: !entry.address.isEmpty() ? QStringLiteral("%1/%2").arg(entry.address).arg(entry.mask)
									 : entry.hash;
	QStringList subtitleParts;
	if (!entry.reason.isEmpty()) subtitleParts << entry.reason;
	if (entry.durationSeconds == 0) {
		subtitleParts << tr("Permanent");
	} else {
		subtitleParts << tr("Until %1").arg(displayDateTime(entry.startUtc.addSecs(entry.durationSeconds)));
	}
	switch (role) {
		case Qt::DisplayRole:
		case TitleRole:
			return title;
		case StableIdRole:
			return entry.stableId;
		case AddressRole:
			return entry.address;
		case MaskRole:
			return entry.mask;
		case UserNameRole:
			return entry.userName;
		case HashRole:
			return entry.hash;
		case ReasonRole:
			return entry.reason;
		case StartUtcRole:
			return entry.startUtc;
		case DurationSecondsRole:
			return entry.durationSeconds;
		case ExpiresAtRole:
			return entry.durationSeconds == 0 ? QVariant() : QVariant::fromValue(entry.startUtc.addSecs(entry.durationSeconds));
		case PermanentRole:
			return entry.durationSeconds == 0;
		case PendingRole:
			return entry.pending;
		case SubtitleRole:
			return subtitleParts.join(QStringLiteral(" · "));
		default:
			return {};
	}
}

QHash< int, QByteArray > ModernBanListModel::roleNames() const {
	return { { StableIdRole, "stableId" },
			 { AddressRole, "address" },
			 { MaskRole, "mask" },
			 { UserNameRole, "userName" },
			 { HashRole, "hash" },
			 { ReasonRole, "reason" },
			 { StartUtcRole, "startUtc" },
			 { DurationSecondsRole, "durationSeconds" },
			 { ExpiresAtRole, "expiresAt" },
			 { PermanentRole, "permanent" },
			 { PendingRole, "pending" },
			 { TitleRole, "title" },
			 { SubtitleRole, "subtitle" } };
}

int ModernBanListModel::totalCount() const { return static_cast< int >(m_entries.size()); }
int ModernBanListModel::filteredCount() const { return static_cast< int >(m_filteredIndices.size()); }
int ModernBanListModel::page() const { return m_page; }
int ModernBanListModel::pageCount() const { return pageCountFor(filteredCount(), m_pageSize); }
int ModernBanListModel::pageSize() const { return m_pageSize; }
QString ModernBanListModel::filter() const { return m_filter; }
QString ModernBanListModel::selectedStableId() const { return m_selectedStableId; }

void ModernBanListModel::setPage(const int page) {
	const int bounded = std::clamp(page, 0, pageCount() - 1);
	if (m_page == bounded) return;
	m_page = bounded;
	rebuildProjection(true);
}

void ModernBanListModel::setPageSize(const int pageSize) {
	const int bounded = boundedPageSize(pageSize);
	if (m_pageSize == bounded) return;
	m_pageSize = bounded;
	m_page     = 0;
	rebuildProjection(false);
}

void ModernBanListModel::setFilter(const QString &filter) {
	const QString normalized = normalizedFilter(filter);
	if (m_filter == normalized) return;
	m_filter = normalized;
	m_page   = 0;
	rebuildProjection(false);
}

void ModernBanListModel::setSelectedStableId(const QString &stableId) {
	const QString normalized = stableId.trimmed();
	if (!normalized.isEmpty() && !findEntry(normalized)) return;
	if (m_selectedStableId == normalized) return;
	m_selectedStableId = normalized;
	if (!normalized.isEmpty()) {
		for (int i = 0; i < m_filteredIndices.size(); ++i) {
			if (m_entries.at(m_filteredIndices.at(i)).stableId == normalized) {
				const int targetPage = i / m_pageSize;
				if (targetPage != m_page) {
					m_page = targetPage;
					rebuildProjection(true);
				}
				break;
			}
		}
	}
	emit selectionChanged();
}

QVariantMap ModernBanListModel::item(const QString &stableId) const {
	const Entry *entry = findEntry(stableId.trimmed());
	return entry ? itemMap(*entry) : QVariantMap();
}

void ModernBanListModel::replaceEntries(QVector< Entry > entries, const QString &preferredSelection,
												const QString &preferredIdentityKey) {
	std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
		const QString leftTitle  = left.userName.isEmpty() ? (left.address.isEmpty() ? left.hash : left.address)
															 : left.userName;
		const QString rightTitle = right.userName.isEmpty() ? (right.address.isEmpty() ? right.hash : right.address)
															  : right.userName;
		const int byTitle = QString::compare(leftTitle, rightTitle, Qt::CaseInsensitive);
		return byTitle == 0 ? left.identityKey() < right.identityKey() : byTitle < 0;
	});
	QString nextSelection = preferredSelection.isEmpty() ? m_selectedStableId : preferredSelection;
	auto selectionExists = [&entries, &nextSelection]() {
		return std::any_of(entries.cbegin(), entries.cend(), [&nextSelection](const Entry &entry) {
			return entry.stableId == nextSelection;
		});
	};
	if (!nextSelection.isEmpty() && !selectionExists()) {
		nextSelection.clear();
		if (!preferredIdentityKey.isEmpty()) {
			for (const Entry &entry : entries) {
				if (entry.identityKey() == preferredIdentityKey) {
					nextSelection = entry.stableId;
					break;
				}
			}
		}
	}
	const bool selectionWasChanged = nextSelection != m_selectedStableId;
	m_selectedStableId             = nextSelection;
	m_entries                      = std::move(entries);
	rebuildProjection(true);
	if (selectionWasChanged) emit selectionChanged();
}

const ModernBanListModel::Entry *ModernBanListModel::findEntry(const QString &stableId) const {
	for (const Entry &entry : m_entries) {
		if (entry.stableId == stableId) return &entry;
	}
	return nullptr;
}

ModernBanListModel::Entry *ModernBanListModel::findEntry(const QString &stableId) {
	for (Entry &entry : m_entries) {
		if (entry.stableId == stableId) return &entry;
	}
	return nullptr;
}

void ModernBanListModel::rebuildProjection(const bool preservePage) {
	beginResetModel();
	m_filteredIndices.clear();
	for (int index = 0; index < m_entries.size(); ++index) {
		const Entry &entry = m_entries.at(index);
		const bool matches = m_filter.isEmpty() || containsInsensitive(entry.userName, m_filter)
						 || containsInsensitive(entry.address, m_filter) || containsInsensitive(entry.hash, m_filter)
						 || containsInsensitive(entry.reason, m_filter);
		if (matches) m_filteredIndices.push_back(index);
	}
	if (!preservePage) m_page = 0;
	m_page = std::clamp(m_page, 0, pageCountFor(m_filteredIndices.size(), m_pageSize) - 1);
	const int first = m_page * m_pageSize;
	const int last  = std::min(first + m_pageSize, static_cast< int >(m_filteredIndices.size()));
	m_pageIndices   = m_filteredIndices.mid(first, last - first);
	endResetModel();
	emit projectionChanged();
}

QVariantMap ModernBanListModel::itemMap(const Entry &entry) const {
	QVariantMap item { { QStringLiteral("stableId"), entry.stableId },
					   { QStringLiteral("address"), entry.address },
					   { QStringLiteral("mask"), entry.mask },
					   { QStringLiteral("userName"), entry.userName },
					   { QStringLiteral("hash"), entry.hash },
					   { QStringLiteral("reason"), entry.reason },
					   { QStringLiteral("startUtc"), entry.startUtc },
					   { QStringLiteral("durationSeconds"), entry.durationSeconds },
					   { QStringLiteral("permanent"), entry.durationSeconds == 0 },
					   { QStringLiteral("pending"), entry.pending } };
	if (entry.durationSeconds > 0) item.insert(QStringLiteral("expiresAt"), entry.startUtc.addSecs(entry.durationSeconds));
	return item;
}

ModernBanListController::ModernBanListController(QObject *parent) : QObject(parent), m_model(this) {
}

QAbstractItemModel *ModernBanListController::model() { return &m_model; }
ModernBanListModel *ModernBanListController::typedModel() { return &m_model; }
QString ModernBanListController::state() const { return m_state; }
QString ModernBanListController::errorMessage() const { return m_errorMessage; }
QString ModernBanListController::operationError() const { return m_operationError; }
bool ModernBanListController::canManage() const { return m_canManage; }
bool ModernBanListController::busy() const { return m_activeOperationId != 0; }
QVariantMap ModernBanListController::pendingConfirmation() const { return m_pendingConfirmation; }
qulonglong ModernBanListController::loadGeneration() const { return m_loadGeneration; }

void ModernBanListController::setCanManage(const bool canManage) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (m_canManage == canManage) return;
	m_canManage = canManage;
	if (!m_canManage) clearPendingConfirmation();
	emit permissionsChanged();
}

bool ModernBanListController::refresh() {
	if (!acceptsFrontendStateMutation(this)) return false;
	if (busy() || m_state == QLatin1String("loading") || m_state == QLatin1String("refreshing")) return false;
	setOperationError(QString());
	++m_loadGeneration;
	setState(m_model.totalCount() > 0 ? QStringLiteral("refreshing") : QStringLiteral("loading"));
	emit refreshRequested(m_loadGeneration);
	return true;
}

void ModernBanListController::applySnapshot(const MumbleProto::BanList &message, const qulonglong generation) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (generation != 0 && generation != m_loadGeneration) return;
	QString selectedIdentity;
	if (const auto *selected = m_model.findEntry(m_model.selectedStableId())) selectedIdentity = selected->identityKey();
	QVector< ModernBanListModel::Entry > entries;
	entries.reserve(message.bans_size());
	for (int index = 0; index < message.bans_size(); ++index) {
		const auto &ban = message.bans(index);
		ModernBanListModel::Entry entry;
		entry.addressBytes = QByteArray(ban.address().data(), static_cast< int >(ban.address().size()));
		HostAddress host(entry.addressBytes);
		if (host.isValid()) {
			entry.address = host.toString(false);
			entry.mask    = static_cast< int >(ban.mask());
			if (!host.isV6()) entry.mask = std::max(0, entry.mask - IPv4MaskOffset);
		}
		entry.userName       = ban.has_name() ? u8(ban.name()) : QString();
		entry.hash           = ban.has_hash() ? u8(ban.hash()) : QString();
		entry.reason         = ban.has_reason() ? u8(ban.reason()) : QString();
		entry.startUtc       = ban.has_start() ? QDateTime::fromString(u8(ban.start()), Qt::ISODate).toUTC() : QDateTime();
		entry.durationSeconds = ban.has_duration() ? ban.duration() : 0;
		if (!entry.startUtc.isValid()) entry.startUtc = QDateTime::currentDateTimeUtc();
		entry.stableId = banStableId(entry.identityKey());
		entries.push_back(std::move(entry));
	}
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	clearPendingConfirmation();
	m_model.replaceEntries(std::move(entries), m_model.selectedStableId(), selectedIdentity);
	// Preserve a server rejection across the authoritative re-query that
	// follows a denied mutation. The next explicit refresh/mutation clears it.
	setState(QStringLiteral("ready"));
	emit operationChanged();
}

void ModernBanListController::applyLoadError(const QString &message, const qulonglong generation) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (generation != 0 && generation != m_loadGeneration) return;
	setState(QStringLiteral("error"), message.trimmed().isEmpty() ? tr("Unable to load the ban list.") : message);
}

QVariantMap ModernBanListController::validateDraft(const QVariantMap &draft, const QString &editingStableId) const {
	ModernBanListModel::Entry entry;
	QString field;
	QString error;
	if (!entryFromDraft(draft, &entry, &field, &error)) {
		return { { QStringLiteral("valid"), false }, { QStringLiteral("field"), field },
				 { QStringLiteral("error"), error } };
	}
	for (const auto &candidate : m_model.m_entries) {
		if (candidate.stableId != editingStableId.trimmed() && candidate.identityKey() == entry.identityKey()) {
			return { { QStringLiteral("valid"), false }, { QStringLiteral("field"), QStringLiteral("address") },
					 { QStringLiteral("error"), tr("That address or certificate is already banned.") } };
		}
	}
	return { { QStringLiteral("valid"), true }, { QStringLiteral("normalized"), m_model.itemMap(entry) } };
}

bool ModernBanListController::beginAdd(const QVariantMap &draft) {
	setOperationError(QString());
	if (!m_canManage || busy()) {
		setOperationError(!m_canManage ? tr("You do not have permission to manage bans.")
										 : tr("Wait for the current ban change to finish."));
		return false;
	}
	ModernBanListModel::Entry entry;
	QString field;
	QString error;
	if (!entryFromDraft(draft, &entry, &field, &error)) {
		setOperationError(error);
		return false;
	}
	for (const auto &candidate : m_model.m_entries) {
		if (candidate.identityKey() == entry.identityKey()) {
			setOperationError(tr("That address or certificate is already banned."));
			return false;
		}
	}
	entry.stableId    = banStableId(entry.identityKey());
	m_pendingMutation = { MutationKind::Add, QString(), entry };
	m_pendingConfirmation = { { QStringLiteral("kind"), QStringLiteral("addBan") },
		{ QStringLiteral("title"), tr("Add ban?") },
		{ QStringLiteral("message"), tr("Add this ban to the server?") },
		{ QStringLiteral("confirmLabel"), tr("Add ban") },
		{ QStringLiteral("tone"), QStringLiteral("danger") } };
	emit confirmationChanged();
	return true;
}

bool ModernBanListController::beginEdit(const QString &stableId, const QVariantMap &draft) {
	setOperationError(QString());
	if (!m_canManage || busy() || !m_model.findEntry(stableId.trimmed())) {
		setOperationError(!m_canManage ? tr("You do not have permission to manage bans.")
										 : tr("The selected ban is no longer available."));
		return false;
	}
	ModernBanListModel::Entry entry;
	QString field;
	QString error;
	if (!entryFromDraft(draft, &entry, &field, &error)) {
		setOperationError(error);
		return false;
	}
	for (const auto &candidate : m_model.m_entries) {
		if (candidate.stableId != stableId.trimmed() && candidate.identityKey() == entry.identityKey()) {
			setOperationError(tr("That address or certificate is already banned."));
			return false;
		}
	}
	entry.stableId    = stableId.trimmed();
	m_pendingMutation = { MutationKind::Edit, stableId.trimmed(), entry };
	m_pendingConfirmation = { { QStringLiteral("kind"), QStringLiteral("editBan") },
		{ QStringLiteral("title"), tr("Update ban?") },
		{ QStringLiteral("message"), tr("Apply these changes to the selected ban?") },
		{ QStringLiteral("confirmLabel"), tr("Update ban") },
		{ QStringLiteral("tone"), QStringLiteral("danger") },
		{ QStringLiteral("stableId"), stableId.trimmed() } };
	emit confirmationChanged();
	return true;
}

bool ModernBanListController::beginRemove(const QString &stableId) {
	setOperationError(QString());
	const auto *entry = m_model.findEntry(stableId.trimmed());
	if (!m_canManage || busy() || !entry) {
		setOperationError(!m_canManage ? tr("You do not have permission to manage bans.")
										 : tr("The selected ban is no longer available."));
		return false;
	}
	m_pendingMutation = { MutationKind::Remove, stableId.trimmed(), *entry };
	const QString label = !entry->userName.isEmpty() ? entry->userName
		: (!entry->address.isEmpty() ? entry->address : entry->hash);
	m_pendingConfirmation = { { QStringLiteral("kind"), QStringLiteral("removeBan") },
		{ QStringLiteral("title"), tr("Remove ban?") },
		{ QStringLiteral("message"), tr("Remove the ban for %1?").arg(label) },
		{ QStringLiteral("confirmLabel"), tr("Remove ban") },
		{ QStringLiteral("tone"), QStringLiteral("danger") },
		{ QStringLiteral("stableId"), stableId.trimmed() } };
	emit confirmationChanged();
	return true;
}

bool ModernBanListController::confirmPending() {
	if (!m_canManage || busy() || m_pendingMutation.kind == MutationKind::None) return false;
	m_rollbackEntries   = m_model.m_entries;
	m_rollbackSelection = m_model.selectedStableId();
	// Keep a truly immutable pre-operation snapshot for edit rollback. Add and
	// remove detach through container APIs, while edit mutates an Entry in place.
	m_model.m_entries.detach();
	QString preferredSelection;
	if (m_pendingMutation.kind == MutationKind::Add) {
		auto entry    = m_pendingMutation.entry;
		entry.pending = true;
		preferredSelection = entry.stableId;
		m_model.m_entries.push_back(std::move(entry));
	} else if (m_pendingMutation.kind == MutationKind::Edit) {
		auto *entry = m_model.findEntry(m_pendingMutation.stableId);
		if (!entry) return false;
		*entry         = m_pendingMutation.entry;
		entry->pending = true;
		preferredSelection = entry->stableId;
	} else {
		const QString removedStableId = m_pendingMutation.stableId;
		auto &entries = m_model.m_entries;
		entries.erase(std::remove_if(entries.begin(), entries.end(), [&removedStableId](const auto &entry) {
			return entry.stableId == removedStableId;
		}), entries.end());
	}
	m_model.replaceEntries(m_model.m_entries, preferredSelection);
	const qulonglong operationId = m_nextOperationId++;
	m_activeOperationId          = operationId;
	const MumbleProto::BanList update = protocolSnapshot();
	clearPendingConfirmation();
	setOperationError(QString());
	emit operationChanged();
	emit updateRequested(operationId, update);
	return true;
}

void ModernBanListController::cancelPending() {
	clearPendingConfirmation();
}

void ModernBanListController::completeOperation(const qulonglong operationId, const bool accepted,
													 const QString &error) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (operationId == 0 || operationId != m_activeOperationId) return;
	const QString selection = m_model.selectedStableId();
	if (!accepted) {
		m_model.replaceEntries(m_rollbackEntries, m_rollbackSelection);
		setOperationError(fallbackOperationError(error));
	} else {
		auto entries = m_model.m_entries;
		for (auto &entry : entries) entry.pending = false;
		m_model.replaceEntries(std::move(entries), selection);
		setOperationError(QString());
	}
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	emit operationChanged();
}

void ModernBanListController::reset() {
	if (!acceptsFrontendStateMutation(this)) return;
	++m_loadGeneration;
	m_activeOperationId = 0;
	m_rollbackEntries.clear();
	m_rollbackSelection.clear();
	clearPendingConfirmation();
	m_model.replaceEntries({});
	setOperationError(QString());
	setState(QStringLiteral("idle"));
	emit operationChanged();
}

void ModernBanListController::setState(const QString &state, const QString &error) {
	if (m_state == state && m_errorMessage == error) return;
	m_state        = state;
	m_errorMessage = error;
	emit stateChanged();
}

void ModernBanListController::setOperationError(const QString &error) {
	if (m_operationError == error) return;
	m_operationError = error;
	emit operationChanged();
}

void ModernBanListController::clearPendingConfirmation() {
	if (m_pendingMutation.kind == MutationKind::None && m_pendingConfirmation.isEmpty()) return;
	m_pendingMutation = {};
	m_pendingConfirmation.clear();
	emit confirmationChanged();
}

bool ModernBanListController::entryFromDraft(const QVariantMap &draft, ModernBanListModel::Entry *entry,
													  QString *field, QString *error) const {
	if (!entry || !field || !error) return false;
	*entry = {};
	field->clear();
	error->clear();
	entry->address  = draft.value(QStringLiteral("address")).toString().trimmed();
	entry->userName = draft.value(QStringLiteral("userName")).toString().trimmed();
	entry->hash     = draft.value(QStringLiteral("hash")).toString().trimmed();
	entry->reason   = draft.value(QStringLiteral("reason")).toString().trimmed();

	HostAddress host;
	host.reset();
	if (!entry->address.isEmpty()) {
		const QHostAddress parsed(entry->address);
		if (parsed.protocol() != QAbstractSocket::IPv4Protocol
			&& parsed.protocol() != QAbstractSocket::IPv6Protocol) {
			*field = QStringLiteral("address");
			*error = tr("Enter a valid IPv4 or IPv6 address.");
			return false;
		}
		host              = HostAddress(parsed);
		entry->address     = host.toString(false);
		const int maximum = host.isV6() ? 128 : 32;
		const int defaultMask = maximum;
		entry->mask = draft.contains(QStringLiteral("mask")) ? draft.value(QStringLiteral("mask")).toInt() : defaultMask;
		if (entry->mask < 8 || entry->mask > maximum) {
			*field = QStringLiteral("mask");
			*error = tr("The subnet mask must be between 8 and %1.").arg(maximum);
			return false;
		}
	} else {
		entry->mask = 0;
		if (entry->hash.isEmpty()) {
			*field = QStringLiteral("address");
			*error = tr("Enter an IP address or certificate hash.");
			return false;
		}
	}
	entry->addressBytes = host.toByteArray();
	entry->startUtc = parseUtcDateTime(draft.value(QStringLiteral("startUtc")));
	if (!entry->startUtc.isValid()) entry->startUtc = QDateTime::currentDateTimeUtc();
	if (draft.value(QStringLiteral("permanent"), false).toBool()) {
		entry->durationSeconds = 0;
	} else if (draft.contains(QStringLiteral("expiresAt"))) {
		const QDateTime expiresAt = parseUtcDateTime(draft.value(QStringLiteral("expiresAt")));
		if (!expiresAt.isValid() || expiresAt <= entry->startUtc) {
			*field = QStringLiteral("expiresAt");
			*error = tr("Choose an end time after the ban starts, or make it permanent.");
			return false;
		}
		entry->durationSeconds = static_cast< quint32 >(std::min< qint64 >(
			entry->startUtc.secsTo(expiresAt), std::numeric_limits< quint32 >::max()));
	} else {
		const qulonglong duration = draft.value(QStringLiteral("durationSeconds"), 0).toULongLong();
		entry->durationSeconds = static_cast< quint32 >(std::min< qulonglong >(
			duration, std::numeric_limits< quint32 >::max()));
	}
	entry->stableId = banStableId(entry->identityKey());
	return true;
}

MumbleProto::BanList ModernBanListController::protocolSnapshot() const {
	MumbleProto::BanList message;
	message.set_query(false);
	for (const auto &entry : m_model.m_entries) {
		auto *ban = message.add_bans();
		HostAddress host(entry.addressBytes);
		ban->set_address(host.toStdString());
		const int protocolMask = host.isValid() && !host.isV6() ? entry.mask + IPv4MaskOffset : entry.mask;
		ban->set_mask(static_cast< quint32 >(std::max(0, protocolMask)));
		ban->set_name(u8(entry.userName));
		ban->set_hash(u8(entry.hash));
		ban->set_reason(u8(entry.reason));
		ban->set_start(u8(entry.startUtc.toUTC().toString(Qt::ISODate)));
		ban->set_duration(entry.durationSeconds);
	}
	return message;
}

ModernServerAdminController::ModernServerAdminController(QObject *parent)
	: QObject(parent), m_users(this), m_bans(this) {
}

ModernRegisteredUsersController *ModernServerAdminController::users() { return &m_users; }
ModernBanListController *ModernServerAdminController::bans() { return &m_bans; }
