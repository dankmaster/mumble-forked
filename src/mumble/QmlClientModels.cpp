// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlClientModels.h"

#include "ClientActionRegistry.h"

#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtGui/QAction>

#include <algorithm>

ClientSessionController::ClientSessionController(QObject *parent) : QObject(parent) {
}

QString ClientSessionController::serverName() const { return m_serverName; }
QString ClientSessionController::connectionLabel() const { return m_connectionLabel; }
QString ClientSessionController::selfName() const { return m_selfName; }
bool ClientSessionController::connected() const { return m_connected; }
bool ClientSessionController::selfMuted() const { return m_selfMuted; }
bool ClientSessionController::selfDeafened() const { return m_selfDeafened; }
QVariantMap ClientSessionController::updateBanner() const { return m_updateBanner; }
QString ClientSessionController::motdHtml() const { return m_motdHtml; }
QString ClientSessionController::motdSummary() const { return m_motdSummary; }

#define SET_VALUE(member, signalName) \
	if (member == value) { \
		return; \
	} \
	member = value; \
	emit signalName()

void ClientSessionController::setServerName(const QString &value) { SET_VALUE(m_serverName, serverNameChanged); }
void ClientSessionController::setConnectionLabel(const QString &value) {
	SET_VALUE(m_connectionLabel, connectionLabelChanged);
}
void ClientSessionController::setSelfName(const QString &value) { SET_VALUE(m_selfName, selfNameChanged); }
void ClientSessionController::setConnected(bool value) { SET_VALUE(m_connected, connectedChanged); }
void ClientSessionController::setSelfMuted(bool value) { SET_VALUE(m_selfMuted, selfMutedChanged); }
void ClientSessionController::setSelfDeafened(bool value) { SET_VALUE(m_selfDeafened, selfDeafenedChanged); }
void ClientSessionController::setUpdateBanner(const QVariantMap &value) { SET_VALUE(m_updateBanner, updateBannerChanged); }
void ClientSessionController::setMotdHtml(const QString &value) { SET_VALUE(m_motdHtml, motdHtmlChanged); }
void ClientSessionController::setMotdSummary(const QString &value) { SET_VALUE(m_motdSummary, motdSummaryChanged); }

#undef SET_VALUE

ActiveScopeController::ActiveScopeController(QObject *parent) : QObject(parent) {
}

QString ActiveScopeController::scopeToken() const { return m_scopeToken; }
QString ActiveScopeController::label() const { return m_label; }
QString ActiveScopeController::description() const { return m_description; }
QString ActiveScopeController::kindLabel() const { return m_kindLabel; }
QString ActiveScopeController::composerPlaceholder() const { return m_composerPlaceholder; }
QString ActiveScopeController::composerHint() const { return m_composerHint; }
bool ActiveScopeController::canSend() const { return m_canSend; }
bool ActiveScopeController::hasPendingReply() const { return m_hasPendingReply; }
QString ActiveScopeController::replyActor() const { return m_replyActor; }
QString ActiveScopeController::replySnippet() const { return m_replySnippet; }
bool ActiveScopeController::canAttachImages() const { return m_canAttachImages; }

#define SET_SCOPE_VALUE(member, signalName) \
	if (member == value) return; \
	member = value; \
	emit signalName()

void ActiveScopeController::setScopeToken(const QString &value) { SET_SCOPE_VALUE(m_scopeToken, scopeTokenChanged); }
void ActiveScopeController::setLabel(const QString &value) { SET_SCOPE_VALUE(m_label, labelChanged); }
void ActiveScopeController::setDescription(const QString &value) { SET_SCOPE_VALUE(m_description, descriptionChanged); }
void ActiveScopeController::setKindLabel(const QString &value) { SET_SCOPE_VALUE(m_kindLabel, kindLabelChanged); }
void ActiveScopeController::setComposerPlaceholder(const QString &value) {
	SET_SCOPE_VALUE(m_composerPlaceholder, composerPlaceholderChanged);
}
void ActiveScopeController::setComposerHint(const QString &value) {
	SET_SCOPE_VALUE(m_composerHint, composerHintChanged);
}
void ActiveScopeController::setCanSend(bool value) { SET_SCOPE_VALUE(m_canSend, canSendChanged); }
void ActiveScopeController::setHasPendingReply(bool value) {
	SET_SCOPE_VALUE(m_hasPendingReply, hasPendingReplyChanged);
}
void ActiveScopeController::setReplyActor(const QString &value) { SET_SCOPE_VALUE(m_replyActor, replyActorChanged); }
void ActiveScopeController::setReplySnippet(const QString &value) { SET_SCOPE_VALUE(m_replySnippet, replySnippetChanged); }
void ActiveScopeController::setCanAttachImages(bool value) {
	SET_SCOPE_VALUE(m_canAttachImages, canAttachImagesChanged);
}

#undef SET_SCOPE_VALUE

void ActiveScopeController::applyState(const QVariantMap &state) {
	setScopeToken(state.value(QStringLiteral("scopeToken")).toString());
	setLabel(state.value(QStringLiteral("label")).toString());
	setDescription(state.value(QStringLiteral("description")).toString());
	setKindLabel(state.value(QStringLiteral("kindLabel")).toString());
	setComposerPlaceholder(state.value(QStringLiteral("composerPlaceholder")).toString());
	setComposerHint(state.value(QStringLiteral("composerHint")).toString());
	setCanSend(state.value(QStringLiteral("canSend")).toBool());
	setHasPendingReply(state.value(QStringLiteral("hasPendingReply")).toBool());
	setReplyActor(state.value(QStringLiteral("replyActor")).toString());
	setReplySnippet(state.value(QStringLiteral("replySnippet")).toString());
	setCanAttachImages(state.value(QStringLiteral("canAttachImages")).toBool());
}

StableListModel::StableListModel(QObject *parent) : QAbstractListModel(parent) {
}

int StableListModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant StableListModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
		return {};
	}
	return valueForRole(m_rows.at(index.row()).toMap(), role);
}

QVariant StableListModel::valueForRole(const QVariantMap &row, const int role) {
	switch (role) {
		case StableIdRole: return row.value(QStringLiteral("id"));
		case TitleRole: return row.value(QStringLiteral("title"));
		case SubtitleRole: return row.value(QStringLiteral("subtitle"));
		case KindRole: return row.value(QStringLiteral("kind"));
		case SelectedRole: return row.value(QStringLiteral("selected"));
		case StatusRole: return row.value(QStringLiteral("status"));
		case PayloadRole: return row;
		case DepthRole: return row.value(QStringLiteral("depth"));
		case UnreadCountRole: return row.value(QStringLiteral("unreadCount"));
		case AvatarUrlRole: return row.value(QStringLiteral("avatarUrl"));
		case EnabledRole: return row.value(QStringLiteral("enabled"), true);
		case CheckedRole: return row.value(QStringLiteral("checked"));
		case TimestampRole: return row.value(QStringLiteral("timestamp"));
		case ReplyActorRole: return row.value(QStringLiteral("replyActor"));
		case ReplySnippetRole: return row.value(QStringLiteral("replySnippet"));
		case ReactionsRole: return row.value(QStringLiteral("reactions"));
		case PreviewRole: return row.value(QStringLiteral("preview"));
		case OwnRole: return row.value(QStringLiteral("own"));
		case DeletedRole: return row.value(QStringLiteral("deleted"));
		case CanReplyRole: return row.value(QStringLiteral("canReply"));
		case CanReactRole: return row.value(QStringLiteral("canReact"));
		case CanDeleteRole: return row.value(QStringLiteral("canDelete"));
		case ScopeTokenRole: return row.value(QStringLiteral("scopeToken"));
		case ShortcutRole: return row.value(QStringLiteral("shortcut"));
		case CheckableRole: return row.value(QStringLiteral("checkable"));
		case MenuRoleRole: return row.value(QStringLiteral("menuRole"));
		case ToolTipRole: return row.value(QStringLiteral("toolTip"));
		case VisibleRole: return row.value(QStringLiteral("visible"), true);
		case AttachmentsRole: return row.value(QStringLiteral("attachments"));
		default: return {};
	}
}

QHash< int, QByteArray > StableListModel::roleNames() const {
	return { { StableIdRole, "stableId" }, { TitleRole, "title" }, { SubtitleRole, "subtitle" },
			 { KindRole, "kind" }, { SelectedRole, "selected" }, { StatusRole, "status" },
			 { PayloadRole, "payload" }, { DepthRole, "depth" }, { UnreadCountRole, "unreadCount" },
			 { AvatarUrlRole, "avatarUrl" }, { EnabledRole, "enabled" }, { CheckedRole, "checked" },
			 { TimestampRole, "timestamp" }, { ReplyActorRole, "replyActor" },
			 { ReplySnippetRole, "replySnippet" }, { ReactionsRole, "reactions" }, { PreviewRole, "preview" },
			 { OwnRole, "own" }, { DeletedRole, "deleted" }, { CanReplyRole, "canReply" },
			 { CanReactRole, "canReact" }, { CanDeleteRole, "canDelete" }, { ScopeTokenRole, "scopeToken" },
			 { ShortcutRole, "shortcut" }, { CheckableRole, "checkable" }, { MenuRoleRole, "menuRole" },
			 { ToolTipRole, "toolTip" }, { VisibleRole, "visible" }, { AttachmentsRole, "attachments" } };
}

QVariantMap StableListModel::get(int row) const {
	return row >= 0 && row < m_rows.size() ? m_rows.at(row).toMap() : QVariantMap {};
}

int StableListModel::indexOf(const QString &stableId) const {
	return m_rowIndexById.value(stableId, -1);
}

QList< int > StableListModel::changedRoles(const QVariantMap &before, const QVariantMap &after) {
	if (before == after) return {};
	QList< int > roles { PayloadRole };
	for (int role = StableIdRole; role <= AttachmentsRole; ++role) {
		if (role != PayloadRole && valueForRole(before, role) != valueForRole(after, role)) roles.push_back(role);
	}
	return roles;
}

void StableListModel::rebuildRowIndex() {
	m_rowIndexById.clear();
	m_rowIndexById.reserve(m_rowIds.size());
	for (int row = 0; row < m_rowIds.size(); ++row) m_rowIndexById.insert(m_rowIds.at(row), row);
}

void StableListModel::synchronizeRows(const QVariantList &rows) {
	QVariantList validRows;
	QStringList validIds;
	QHash< QString, int > validIndexById;
	validRows.reserve(rows.size());
	validIds.reserve(rows.size());
	validIndexById.reserve(rows.size());
	for (const QVariant &entry : rows) {
		QVariantMap row = entry.toMap();
		row.detach();
		const QString stableId = row.value(QStringLiteral("id")).toString();
		if (!stableId.isEmpty()) {
			const int duplicateIndex = validIndexById.value(stableId, -1);
			if (duplicateIndex >= 0) {
				validRows[duplicateIndex] = row;
			} else {
				validIndexById.insert(stableId, validRows.size());
				validRows.push_back(row);
				validIds.push_back(stableId);
			}
		}
	}

	const int oldCount = m_rows.size();
	const int sharedCount = std::min(m_rowIds.size(), validIds.size());
	int commonPrefix = 0;
	while (commonPrefix < sharedCount && m_rowIds.at(commonPrefix) == validIds.at(commonPrefix)) ++commonPrefix;

	const auto updateRow = [this, &validRows](const int row) {
		const QVariantMap before = m_rows.at(row).toMap();
		const QVariantMap after = validRows.at(row).toMap();
		const QList< int > roles = changedRoles(before, after);
		if (roles.isEmpty()) return;
		m_rows[row] = after;
		emit dataChanged(index(row), index(row), roles);
	};

	// Same-order synchronization is the steady-state path. Handle append and tail removal in batches
	// so 10k-message timelines stay linear instead of repeatedly scanning and shifting the model.
	if (commonPrefix == sharedCount && (commonPrefix == m_rowIds.size() || commonPrefix == validIds.size())) {
		for (int row = 0; row < commonPrefix; ++row) updateRow(row);
		if (validIds.size() > m_rowIds.size()) {
			const int first = m_rowIds.size();
			const int last = validIds.size() - 1;
			beginInsertRows(QModelIndex(), first, last);
			for (int row = first; row <= last; ++row) {
				m_rows.push_back(validRows.at(row));
				m_rowIds.push_back(validIds.at(row));
			}
			endInsertRows();
			rebuildRowIndex();
		} else if (validIds.size() < m_rowIds.size()) {
			const int first = validIds.size();
			const int last = m_rowIds.size() - 1;
			beginRemoveRows(QModelIndex(), first, last);
			m_rows.remove(first, last - first + 1);
			m_rowIds.remove(first, last - first + 1);
			endRemoveRows();
			rebuildRowIndex();
		}
		if (oldCount != m_rows.size()) emit countChanged();
		return;
	}

	for (int targetIndex = 0; targetIndex < validRows.size(); ++targetIndex) {
		const QVariantMap targetRow = validRows.at(targetIndex).toMap();
		const QString &targetId = validIds.at(targetIndex);

		int existingIndex = indexOf(targetId);
		if (existingIndex < 0) {
			beginInsertRows(QModelIndex(), targetIndex, targetIndex);
			m_rows.insert(targetIndex, targetRow);
			m_rowIds.insert(targetIndex, targetId);
			endInsertRows();
			rebuildRowIndex();
			existingIndex = targetIndex;
		} else if (existingIndex != targetIndex) {
			const int destination = existingIndex < targetIndex ? targetIndex + 1 : targetIndex;
			beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), destination);
			m_rows.move(existingIndex, targetIndex);
			m_rowIds.move(existingIndex, targetIndex);
			endMoveRows();
			rebuildRowIndex();
			existingIndex = targetIndex;
		}

		const QList< int > roles = changedRoles(m_rows.at(existingIndex).toMap(), targetRow);
		if (!roles.isEmpty()) {
			m_rows[existingIndex] = targetRow;
			emit dataChanged(index(existingIndex), index(existingIndex), roles);
		}
	}

	while (m_rows.size() > validRows.size()) {
		const int last = m_rows.size() - 1;
		beginRemoveRows(QModelIndex(), last, last);
		m_rows.removeAt(last);
		m_rowIds.removeAt(last);
		endRemoveRows();
		rebuildRowIndex();
	}
	if (oldCount != m_rows.size()) {
		emit countChanged();
	}
}

void StableListModel::upsertRow(const QVariantMap &row) {
	const QString stableId = row.value(QStringLiteral("id")).toString();
	if (stableId.isEmpty()) {
		return;
	}
	const int existing = indexOf(stableId);
	if (existing >= 0) {
		QVariantMap ownedRow = row;
		ownedRow.detach();
		const QList< int > roles = changedRoles(m_rows.at(existing).toMap(), ownedRow);
		if (roles.isEmpty()) return;
		m_rows[existing] = ownedRow;
		emit dataChanged(index(existing), index(existing), roles);
		return;
	}
	QVariantMap ownedRow = row;
	ownedRow.detach();
	const int newRow = m_rows.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	m_rows.push_back(ownedRow);
	m_rowIds.push_back(stableId);
	m_rowIndexById.insert(stableId, newRow);
	endInsertRows();
	emit countChanged();
}

void StableListModel::removeRow(const QString &stableId) {
	const int existing = indexOf(stableId);
	if (existing < 0) {
		return;
	}
	beginRemoveRows(QModelIndex(), existing, existing);
	m_rows.removeAt(existing);
	m_rowIds.removeAt(existing);
	endRemoveRows();
	rebuildRowIndex();
	emit countChanged();
}

void StableListModel::clear() {
	if (m_rows.isEmpty()) {
		return;
	}
	beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
	m_rows.clear();
	m_rowIds.clear();
	m_rowIndexById.clear();
	endRemoveRows();
	emit countChanged();
}

QVariantMap RoomModel::roomRow(const QVariantMap &room, const QString &kind) {
	const QString scopeToken = room.value(QStringLiteral("token")).toString().trimmed();
	if (scopeToken.isEmpty()) return {};
	return { { QStringLiteral("id"), QStringLiteral("%1:%2").arg(kind, scopeToken) },
			 { QStringLiteral("scopeToken"), scopeToken },
			 { QStringLiteral("title"), room.value(QStringLiteral("label")) },
			 { QStringLiteral("subtitle"),
			   room.value(QStringLiteral("topic"),
						  room.value(QStringLiteral("description"), room.value(QStringLiteral("subtitle")))) },
			 { QStringLiteral("kind"), kind },
			 { QStringLiteral("selected"),
			   room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))) },
			 { QStringLiteral("status"),
			   room.value(QStringLiteral("joined")).toBool() ? QStringLiteral("joined") : QString() },
			 { QStringLiteral("depth"), room.value(QStringLiteral("depth")) },
			 { QStringLiteral("unreadCount"), room.value(QStringLiteral("unreadCount")) },
			 { QStringLiteral("source"), room } };
}

void RoomModel::replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms) {
	m_voiceRoomStates = voiceRooms;
	m_textRoomStates = textRooms;
	synchronizeAllRows();
}

void RoomModel::replaceDirectMessageStates(const QVariantList &conversations) {
	m_directMessageStates = conversations;
	synchronizeAllRows();
}

void RoomModel::synchronizeAllRows() {
	QVariantList rows;
	rows.reserve(m_voiceRoomStates.size() + m_textRoomStates.size() + m_directMessageStates.size());
	const auto append = [&rows](const QVariantList &rooms, const QString &kind) {
		for (const QVariant &entry : rooms) {
			const QVariantMap row = roomRow(entry.toMap(), kind);
			if (!row.isEmpty()) rows.push_back(row);
		}
	};
	append(m_voiceRoomStates, QStringLiteral("voice"));
	append(m_textRoomStates, QStringLiteral("text"));
	append(m_directMessageStates, QStringLiteral("direct"));
	synchronizeRows(rows);
}

QVariantMap ParticipantModel::participantRow(const QVariantMap &participant) {
	const QString sessionId = participant.value(QStringLiteral("session")).toString().trimmed();
	if (sessionId.isEmpty()) return {};
	return { { QStringLiteral("id"), sessionId },
			 { QStringLiteral("title"), participant.value(QStringLiteral("name")) },
			 { QStringLiteral("subtitle"), participant.value(QStringLiteral("statusLabel")) },
			 { QStringLiteral("kind"), QStringLiteral("participant") },
			 { QStringLiteral("status"), participant.value(QStringLiteral("talkState")) },
			 { QStringLiteral("avatarUrl"), participant.value(QStringLiteral("avatarUrl")) },
			 { QStringLiteral("source"), participant } };
}

void ParticipantModel::replaceParticipantStates(const QVariantList &participants) {
	QVariantList rows;
	rows.reserve(participants.size());
	for (const QVariant &entry : participants) {
		const QVariantMap row = participantRow(entry.toMap());
		if (!row.isEmpty()) rows.push_back(row);
	}
	synchronizeRows(rows);
}

void ParticipantModel::updatePresence(const QString &sessionId, const QString &talkState, const QString &talkLabel,
							  const QString &talkTone, const bool talking, const bool isSelf,
							  const QVariantList &badges, const QVariantList &statuses) {
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;

	for (int rowIndex = 0; rowIndex < rowCount(); ++rowIndex) {
		QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("id")).toString() != id) continue;
		const QVariantMap previousRow = row;

		row.insert(QStringLiteral("status"), talkState);
		QVariantMap source = row.value(QStringLiteral("source")).toMap();
		source.insert(QStringLiteral("talkState"), talkState);
		source.insert(QStringLiteral("talkLabel"), talkLabel);
		source.insert(QStringLiteral("talkTone"), talkTone);
		source.insert(QStringLiteral("talking"), talking);
		source.insert(QStringLiteral("isSelf"), isSelf);
		source.insert(QStringLiteral("badges"), badges);
		source.insert(QStringLiteral("statuses"), statuses);
		row.insert(QStringLiteral("source"), source);
		if (row == previousRow) return;
		upsertRow(row);
		return;
	}
}

QVariantMap ChatTimelineModel::messageRow(const QVariantMap &message) {
	const QVariant messageIdValue = message.value(QStringLiteral("messageId"));
	QString messageId = messageIdValue.toULongLong() > 0 ? messageIdValue.toString().trimmed() : QString();
	if (messageId.isEmpty()) messageId = message.value(QStringLiteral("messageKey")).toString().trimmed();
	if (messageId.isEmpty()) messageId = message.value(QStringLiteral("id")).toString().trimmed();
	if (messageId.isEmpty()) return {};

	return { { QStringLiteral("id"), messageId },
			 { QStringLiteral("title"), message.value(QStringLiteral("actor"),
												 message.value(QStringLiteral("actorLabel"),
															   message.value(QStringLiteral("actorName")))) },
			 { QStringLiteral("subtitle"),
			   message.value(QStringLiteral("bodyText"), message.value(QStringLiteral("plainText"))) },
			 { QStringLiteral("kind"), QStringLiteral("message") },
			 { QStringLiteral("status"), message.value(QStringLiteral("deliveryState")) },
			 { QStringLiteral("avatarUrl"), message.value(QStringLiteral("avatarUrl")) },
			 { QStringLiteral("timestamp"), message.value(QStringLiteral("timeLabel")) },
			 { QStringLiteral("replyActor"), message.value(QStringLiteral("replyActor")) },
			 { QStringLiteral("replySnippet"), message.value(QStringLiteral("replySnippet")) },
			 { QStringLiteral("reactions"), message.value(QStringLiteral("reactions")) },
			 { QStringLiteral("preview"),
			   message.value(QStringLiteral("preview"), message.value(QStringLiteral("previewStub"))) },
			 { QStringLiteral("attachments"), message.value(QStringLiteral("attachments")) },
			 { QStringLiteral("own"), message.value(QStringLiteral("own")) },
			 { QStringLiteral("deleted"), message.value(QStringLiteral("deleted")) },
			 { QStringLiteral("canReply"), message.value(QStringLiteral("canReply")) },
			 { QStringLiteral("canReact"), message.value(QStringLiteral("canReact")) },
			 { QStringLiteral("canDelete"), message.value(QStringLiteral("canDelete")) },
			 { QStringLiteral("source"), message } };
}

ChatTimelineModel::MessageMutation ChatTimelineModel::applyMessage(const QVariantMap &message) {
	const QVariantMap row = messageRow(message);
	if (row.isEmpty()) return MessageMutation::Ignored;
	const QString id = row.value(QStringLiteral("id")).toString();
	const int rowIndex = indexOf(id);
	if (rowIndex >= 0) {
		const QVariantMap current = get(rowIndex);
		if (current == row) return MessageMutation::Unchanged;
		upsertRow(row);
		return MessageMutation::Updated;
	}
	upsertRow(row);
	return MessageMutation::Inserted;
}

bool ChatTimelineModel::upsertMessage(const QVariantMap &message) {
	return applyMessage(message) != MessageMutation::Ignored;
}

bool ChatTimelineModel::removeMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (id.isEmpty()) return false;
	if (indexOf(id) < 0) return false;
	removeRow(id);
	return true;
}

int ChatTimelineModel::appendMessages(const QVariantList &messages) {
	int applied = 0;
	for (const QVariant &entry : messages) {
		if (upsertMessage(entry.toMap())) ++applied;
	}
	return applied;
}

void ChatTimelineModel::replaceMessages(const QVariantList &messages) {
	QVariantList rows;
	rows.reserve(messages.size());
	for (const QVariant &entry : messages) {
		const QVariantMap row = messageRow(entry.toMap());
		if (!row.isEmpty()) rows.push_back(row);
	}
	synchronizeRows(rows);
}

void AsyncOperationModel::startOperation(const QString &operationId, const QString &title, const QString &subtitle,
										 const bool cancellable) {
	QVariantMap payload { { QStringLiteral("progress"), -1 }, { QStringLiteral("indeterminate"), true },
						 { QStringLiteral("cancellable"), cancellable }, { QStringLiteral("errorCode"), QString() } };
	upsertRow({ { QStringLiteral("id"), operationId }, { QStringLiteral("title"), title },
				{ QStringLiteral("subtitle"), subtitle }, { QStringLiteral("status"), QStringLiteral("running") },
				{ QStringLiteral("payload"), payload } });
}

void AsyncOperationModel::updateProgress(const QString &operationId, const qint64 bytesReceived,
										 const qint64 bytesTotal) {
	QVariantMap row;
	for (int index = 0; index < rowCount(); ++index) {
		if (get(index).value(QStringLiteral("id")).toString() == operationId) {
			row = get(index);
			break;
		}
	}
	if (row.isEmpty()) return;
	QVariantMap payload = row.value(QStringLiteral("payload")).toMap();
	payload.insert(QStringLiteral("bytesReceived"), bytesReceived);
	payload.insert(QStringLiteral("bytesTotal"), bytesTotal);
	payload.insert(QStringLiteral("indeterminate"), bytesTotal <= 0);
	payload.insert(QStringLiteral("progress"),
				   bytesTotal > 0 ? qBound(0, static_cast< int >((bytesReceived * 100) / bytesTotal), 100) : -1);
	row.insert(QStringLiteral("payload"), payload);
	upsertRow(row);
}

void AsyncOperationModel::finishOperation(const QString &operationId, const bool success, const QString &errorCode,
									   const QString &message) {
	QVariantMap row;
	for (int index = 0; index < rowCount(); ++index) {
		if (get(index).value(QStringLiteral("id")).toString() == operationId) {
			row = get(index);
			break;
		}
	}
	if (row.isEmpty()) return;
	QVariantMap payload = row.value(QStringLiteral("payload")).toMap();
	payload.insert(QStringLiteral("cancellable"), false);
	payload.insert(QStringLiteral("indeterminate"), false);
	payload.insert(QStringLiteral("errorCode"), errorCode);
	if (success) payload.insert(QStringLiteral("progress"), 100);
	row.insert(QStringLiteral("payload"), payload);
	row.insert(QStringLiteral("status"), success ? QStringLiteral("succeeded") : QStringLiteral("failed"));
	row.insert(QStringLiteral("subtitle"), message);
	upsertRow(row);
}

void AsyncOperationModel::interruptOperations(const QString &prefix) {
	for (int index = 0; index < rowCount(); ++index) {
		const QVariantMap row = get(index);
		const QString id = row.value(QStringLiteral("id")).toString();
		if (id.startsWith(prefix) && row.value(QStringLiteral("status")).toString() == QLatin1String("running")) {
			finishOperation(id, false, QStringLiteral("cancelled"), tr("Operation cancelled"));
		}
	}
}

void AsyncOperationModel::cancel(const QString &operationId) {
	const QString id = operationId.trimmed();
	if (!id.isEmpty()) emit cancellationRequested(id);
}

void AsyncOperationModel::dismiss(const QString &operationId) {
	const QString id = operationId.trimmed();
	if (id.isEmpty()) return;

	for (int index = 0; index < rowCount(); ++index) {
		const QVariantMap row = get(index);
		if (row.value(QStringLiteral("id")).toString() != id) continue;
		if (row.value(QStringLiteral("status")).toString() == QLatin1String("running")) return;
		removeRow(id);
		return;
	}
}

UiCommandController::UiCommandController(QObject *parent) : QObject(parent) {
}

void UiCommandController::selectScope(const QString &scopeToken) {
	if (!scopeToken.trimmed().isEmpty()) emit scopeSelectionRequested(scopeToken.trimmed());
}
void UiCommandController::joinVoiceChannel(const QString &scopeToken) {
	if (!scopeToken.trimmed().isEmpty()) emit voiceJoinRequested(scopeToken.trimmed());
}
void UiCommandController::selectParticipant(const QString &sessionId) {
	if (!sessionId.trimmed().isEmpty()) emit participantSelectionRequested(sessionId.trimmed());
}
void UiCommandController::openDirectMessage(const QString &sessionId) {
	if (!sessionId.trimmed().isEmpty()) emit directMessageOpenRequested(sessionId.trimmed());
}
void UiCommandController::sendMessage(const QString &message) {
	if (!message.trimmed().isEmpty()) emit messageSendRequested(message);
}
void UiCommandController::cancelPendingReply() { emit pendingReplyCancelRequested(); }
void UiCommandController::chooseAttachment() { emit attachmentChooseRequested(); }
void UiCommandController::replyToMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageReplyRequested(id);
}
void UiCommandController::retryMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageRetryRequested(id);
}
void UiCommandController::deleteMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageDeleteRequested(id);
}
void UiCommandController::toggleMessageReaction(const QString &messageId, const QString &emoji) {
	const QString id = messageId.trimmed();
	const QString reaction = emoji.trimmed();
	if (!id.isEmpty() && !reaction.isEmpty()) emit messageReactionToggleRequested(id, reaction);
}
void UiCommandController::invokeAction(const QString &actionId) {
	if (!actionId.trimmed().isEmpty()) emit actionRequested(actionId.trimmed());
}
void UiCommandController::invokeScopeAction(const QString &scopeToken, const QString &actionId) {
	const QString scope = scopeToken.trimmed();
	const QString action = actionId.trimmed();
	if (!scope.isEmpty() && !action.isEmpty()) emit scopeActionRequested(scope, action);
}
void UiCommandController::invokeParticipantAction(const QString &sessionId, const QString &actionId) {
	const QString session = sessionId.trimmed();
	const QString action = actionId.trimmed();
	if (!session.isEmpty() && !action.isEmpty()) emit participantActionRequested(session, action);
}
void UiCommandController::toggleSelfMute() { emit selfMuteToggleRequested(); }
void UiCommandController::toggleSelfDeaf() { emit selfDeafToggleRequested(); }
bool UiCommandController::pttPressed() const { return m_pttPressed; }
void UiCommandController::setPttPressed(const bool pressed) {
	if (m_pttPressed == pressed) return;
	m_pttPressed = pressed;
	emit pttPressedChanged();
	emit pttStateRequested(pressed);
}
void UiCommandController::releasePtt() { setPttPressed(false); }

PttSafetyController::PttSafetyController(UiCommandController *commands) : m_commands(commands) {
}

void PttSafetyController::release(PttSafetyReason reason) {
	Q_UNUSED(reason)
	if (m_commands) m_commands->releasePtt();
}

ActionModel::ActionModel(ClientActionRegistry *registry, QObject *parent)
	: StableListModel(parent), m_registry(registry) {
	if (m_registry) {
		connect(m_registry, &ClientActionRegistry::actionStateChanged, this, [this](const QString &) { refresh(); });
	}
	refresh();
}

void ActionModel::refresh() {
	QVariantList rows;
	if (m_registry) {
		for (const QVariant &entry : m_registry->stateSnapshot()) {
			const QVariantMap state = entry.toMap();
			QVariantMap row;
			row.insert(QStringLiteral("id"), state.value(QStringLiteral("id")));
			row.insert(QStringLiteral("title"), state.value(QStringLiteral("text")));
			row.insert(QStringLiteral("kind"), QStringLiteral("action"));
			row.insert(QStringLiteral("status"), state.value(QStringLiteral("checked")).toBool()
												? QStringLiteral("checked") : QString());
			row.insert(QStringLiteral("enabled"), state.value(QStringLiteral("enabled")));
			row.insert(QStringLiteral("checkable"), state.value(QStringLiteral("checkable")));
			row.insert(QStringLiteral("checked"), state.value(QStringLiteral("checked")));
			row.insert(QStringLiteral("shortcut"), state.value(QStringLiteral("shortcut")));
			row.insert(QStringLiteral("menuRole"), state.value(QStringLiteral("menuRole")));
			row.insert(QStringLiteral("toolTip"), state.value(QStringLiteral("toolTip")));
			row.insert(QStringLiteral("visible"), state.value(QStringLiteral("visible"), true));
			row.insert(QStringLiteral("source"), state);
			rows.push_back(row);
		}
	}
	synchronizeRows(rows);
}

bool ActionModel::trigger(const QString &actionId) {
	QAction *action = m_registry ? m_registry->action(actionId) : nullptr;
	if (!action || !action->isEnabled()) {
		return false;
	}
	action->trigger();
	return true;
}

DialogStateController::DialogStateController(QObject *parent) : QObject(parent) {
}

bool DialogStateController::open() const { return m_state.value(QStringLiteral("open")).toBool(); }
QString DialogStateController::dialogId() const { return m_state.value(QStringLiteral("id")).toString(); }
QString DialogStateController::kind() const { return m_state.value(QStringLiteral("kind")).toString(); }
QString DialogStateController::title() const { return m_state.value(QStringLiteral("title")).toString(); }
QString DialogStateController::subtitle() const { return m_state.value(QStringLiteral("subtitle")).toString(); }
QString DialogStateController::activePage() const { return m_state.value(QStringLiteral("activePage")).toString(); }
QVariantList DialogStateController::pages() const { return m_state.value(QStringLiteral("pages")).toList(); }
QVariantList DialogStateController::sections() const { return m_state.value(QStringLiteral("sections")).toList(); }
QVariantList DialogStateController::actions() const { return m_state.value(QStringLiteral("actions")).toList(); }
QVariantMap DialogStateController::state() const { return m_state; }
qulonglong DialogStateController::revision() const { return m_revision; }
QVariant DialogStateController::fieldValue(const QString &fieldId) const {
	for (const QVariant &sectionValue : sections()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == fieldId) return field.value(QStringLiteral("value"));
		}
	}
	return {};
}
QString DialogStateController::fieldError(const QString &fieldId) const {
	return m_state.value(QStringLiteral("errors")).toMap().value(fieldId).toString();
}

void DialogStateController::applyState(const QVariantMap &state) {
	if (m_state == state) return;
	m_state = state;
	m_state.detach();
	++m_revision;
	emit stateChanged();
}

void DialogStateController::updateField(const QString &fieldId, const QVariant &value) {
	if (!open() || dialogId().isEmpty() || fieldId.trimmed().isEmpty()) return;
	emit fieldUpdateRequested(dialogId(), fieldId.trimmed(), value);
}

void DialogStateController::invokeAction(const QString &actionId, const QVariantMap &payload) {
	if (!open() || dialogId().isEmpty() || actionId.trimmed().isEmpty()) return;
	emit actionRequested(dialogId(), actionId.trimmed(), payload);
}

void DialogStateController::requestClose() {
	if (!open() || dialogId().isEmpty()) return;
	emit closeRequested(dialogId());
}

MediaSessionBackend::MediaSessionBackend(QObject *parent) : QObject(parent) {
}

namespace {
const QHash< QString, QSet< QString > > &mediaProviderHosts() {
	static const QHash< QString, QSet< QString > > hosts {
		{ QStringLiteral("youtube"), { QStringLiteral("www.youtube.com"), QStringLiteral("youtube.com"),
									 QStringLiteral("www.youtube-nocookie.com"), QStringLiteral("youtube-nocookie.com") } },
		{ QStringLiteral("twitch"), { QStringLiteral("player.twitch.tv") } },
		{ QStringLiteral("streamable"), { QStringLiteral("streamable.com") } },
		{ QStringLiteral("vimeo"), { QStringLiteral("player.vimeo.com") } },
		{ QStringLiteral("dailymotion"), { QStringLiteral("geo.dailymotion.com") } },
		{ QStringLiteral("spotify"), { QStringLiteral("open.spotify.com") } },
		{ QStringLiteral("facebook"), { QStringLiteral("www.facebook.com") } },
		{ QStringLiteral("tiktok"), { QStringLiteral("www.tiktok.com") } },
		{ QStringLiteral("instagram"), { QStringLiteral("www.instagram.com") } },
		{ QStringLiteral("soundcloud"), { QStringLiteral("w.soundcloud.com") } }
	};
	return hosts;
}
}

bool MediaSessionBackend::active() const { return m_active; }
QUrl MediaSessionBackend::url() const { return m_url; }
QString MediaSessionBackend::provider() const { return m_provider; }
QString MediaSessionBackend::sessionId() const { return m_sessionId; }
QString MediaSessionBackend::state() const { return m_state; }
double MediaSessionBackend::position() const { return m_position; }
double MediaSessionBackend::duration() const { return m_duration; }
QString MediaSessionBackend::error() const { return m_error; }
qulonglong MediaSessionBackend::syncGeneration() const { return m_syncGeneration; }

bool MediaSessionBackend::open(const QUrl &url, const QString &provider, const QString &sessionId) {
	const QUrl normalized = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
	const QString normalizedProvider = provider.trimmed().toLower();
	const auto providerHosts = mediaProviderHosts().constFind(normalizedProvider);
	if (providerHosts == mediaProviderHosts().cend()) {
		reportError(tr("This media provider is not supported."));
		return false;
	}
	if (!normalized.isValid() || normalized.scheme() != QLatin1String("https")
		|| !providerHosts->contains(normalized.host().toLower())) {
		reportError(tr("The media embed URL is not allowed for this provider."));
		return false;
	}
	m_active = true;
	m_url = normalized;
	m_provider = normalizedProvider;
	m_sessionId = sessionId.trimmed();
	m_state = QStringLiteral("loading");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	++m_syncGeneration;
	emit stateChanged();
	return true;
}

bool MediaSessionBackend::isNavigationAllowed(const QUrl &url) const {
	if (url == QUrl(QStringLiteral("about:blank"))) return true;
	if (!m_active) return false;
	const auto providerHosts = mediaProviderHosts().constFind(m_provider);
	return providerHosts != mediaProviderHosts().cend() && url.isValid() && url.scheme() == QLatin1String("https")
		   && providerHosts->contains(url.host().toLower());
}

void MediaSessionBackend::close() {
	if (!m_active && m_state == QLatin1String("idle")) return;
	m_active = false;
	m_url = {};
	m_provider.clear();
	m_sessionId.clear();
	m_state = QStringLiteral("idle");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	++m_syncGeneration;
	emit stateChanged();
}

void MediaSessionBackend::play() {
	if (m_active) emit playRequested();
}

void MediaSessionBackend::pause() {
	if (m_active) emit pauseRequested();
}

void MediaSessionBackend::seek(const double seconds) {
	if (m_active && qIsFinite(seconds) && seconds >= 0.0) emit seekRequested(seconds);
}

void MediaSessionBackend::reportPlaybackState(const double position, const double duration, const bool paused) {
	if (!m_active) return;
	m_position = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	m_duration = qIsFinite(duration) ? qMax(0.0, duration) : 0.0;
	m_state = paused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_error.clear();
	emit stateChanged();
}

void MediaSessionBackend::reportError(const QString &message) {
	m_state = QStringLiteral("error");
	m_error = message.trimmed().isEmpty() ? tr("Media playback failed.") : message.trimmed();
	emit stateChanged();
}

void MediaSessionBackend::applyRemoteState(const QUrl &url, const QString &provider, const QString &sessionId,
										   const double position, const bool paused, const qulonglong generation) {
	if (generation != 0 && generation < m_syncGeneration) return;
	const bool sourceChanged = !m_active || m_sessionId != sessionId || m_url != url;
	if (sourceChanged && !open(url, provider, sessionId)) return;
	m_syncGeneration = generation == 0 ? m_syncGeneration + 1 : qMax(m_syncGeneration, generation);
	m_position = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	m_state = paused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_error.clear();
	emit seekRequested(m_position);
	if (paused) emit pauseRequested();
	else emit playRequested();
	emit stateChanged();
}

QmlSelectionState::QmlSelectionState(QObject *parent) : QObject(parent) {
}

void QmlSelectionState::bindModels(RoomModel *rooms, ParticipantModel *participants) {
	if (m_rooms == rooms && m_participants == participants) return;
	if (m_rooms) disconnect(m_rooms, nullptr, this, nullptr);
	if (m_participants) disconnect(m_participants, nullptr, this, nullptr);
	m_rooms = rooms;
	m_participants = participants;
	const auto connectValidation = [this](QAbstractItemModel *model) {
		if (!model) return;
		connect(model, &QAbstractItemModel::rowsRemoved, this, &QmlSelectionState::validate);
		connect(model, &QAbstractItemModel::modelReset, this, &QmlSelectionState::validate);
	};
	connectValidation(m_rooms);
	connectValidation(m_participants);
	validate();
}

QString QmlSelectionState::scopeToken() const { return m_scopeToken; }
QVariant QmlSelectionState::selectedUserSession() const { return m_selectedUserSession; }
QVariant QmlSelectionState::selectedVoiceChannelId() const { return m_selectedVoiceChannelId; }
void QmlSelectionState::setScopeToken(const QString &value) {
	const QString normalized = value.trimmed();
	const QString accepted = !m_rooms || normalized.isEmpty() || hasScopeToken(normalized) ? normalized : QString();
	if (m_scopeToken == accepted) return;
	m_scopeToken = accepted;
	emit scopeTokenChanged();
}
void QmlSelectionState::setSelectedUserSession(const QVariant &value) {
	bool valid = false;
	const qulonglong session = value.toULongLong(&valid);
	const QString stableId = valid && session > 0 ? QString::number(session) : QString();
	const QVariant accepted = !stableId.isEmpty() && (!m_participants || hasParticipantSession(stableId))
							  ? QVariant::fromValue(session)
							  : QVariant();
	if (m_selectedUserSession == accepted) return;
	m_selectedUserSession = accepted;
	emit selectedUserSessionChanged();
}
void QmlSelectionState::setSelectedVoiceChannelId(const QVariant &value) {
	bool valid = false;
	const qulonglong channelId = value.toULongLong(&valid);
	const QString stableId = valid ? QString::number(channelId) : QString();
	const QVariant accepted = valid && (!m_rooms || hasVoiceChannelId(stableId)) ? QVariant::fromValue(channelId)
																						 : QVariant();
	if (m_selectedVoiceChannelId == accepted) return;
	m_selectedVoiceChannelId = accepted;
	emit selectedVoiceChannelIdChanged();
}

void QmlSelectionState::validate() {
	if (!m_scopeToken.isEmpty() && !hasScopeToken(m_scopeToken)) setScopeToken({});
	if (m_selectedUserSession.isValid()
		&& !hasParticipantSession(QString::number(m_selectedUserSession.toULongLong())))
		setSelectedUserSession({});
	if (m_selectedVoiceChannelId.isValid()
		&& !hasVoiceChannelId(QString::number(m_selectedVoiceChannelId.toULongLong())))
		setSelectedVoiceChannelId({});
}

bool QmlSelectionState::hasScopeToken(const QString &scopeToken) const {
	if (!m_rooms) return true;
	for (int row = 0; row < m_rooms->rowCount(); ++row)
		if (m_rooms->get(row).value(QStringLiteral("scopeToken")).toString() == scopeToken) return true;
	return false;
}

bool QmlSelectionState::hasVoiceChannelId(const QString &channelId) const {
	if (!m_rooms) return true;
	const QString scopeToken = QStringLiteral("channel:%1").arg(channelId);
	for (int row = 0; row < m_rooms->rowCount(); ++row) {
		const QVariantMap room = m_rooms->get(row);
		if (room.value(QStringLiteral("kind")).toString() == QLatin1String("voice")
			&& room.value(QStringLiteral("scopeToken")).toString() == scopeToken)
			return true;
	}
	return false;
}

bool QmlSelectionState::hasParticipantSession(const QString &sessionId) const {
	if (!m_participants) return true;
	for (int row = 0; row < m_participants->rowCount(); ++row)
		if (m_participants->get(row).value(QStringLiteral("id")).toString() == sessionId) return true;
	return false;
}
