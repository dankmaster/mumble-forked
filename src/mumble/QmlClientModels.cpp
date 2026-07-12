// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlClientModels.h"

#include "ClientActionRegistry.h"

#include <QtCore/QString>
#include <QtGui/QAction>

ClientSessionController::ClientSessionController(QObject *parent) : QObject(parent) {
}

QString ClientSessionController::serverName() const { return m_serverName; }
QString ClientSessionController::connectionLabel() const { return m_connectionLabel; }
QString ClientSessionController::selfName() const { return m_selfName; }
bool ClientSessionController::connected() const { return m_connected; }
bool ClientSessionController::selfMuted() const { return m_selfMuted; }
bool ClientSessionController::selfDeafened() const { return m_selfDeafened; }

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

#undef SET_SCOPE_VALUE

void ActiveScopeController::applyState(const QVariantMap &state) {
	setScopeToken(state.value(QStringLiteral("scopeToken")).toString());
	setLabel(state.value(QStringLiteral("label")).toString());
	setDescription(state.value(QStringLiteral("description")).toString());
	setKindLabel(state.value(QStringLiteral("kindLabel")).toString());
	setComposerPlaceholder(state.value(QStringLiteral("composerPlaceholder")).toString());
	setComposerHint(state.value(QStringLiteral("composerHint")).toString());
	setCanSend(state.value(QStringLiteral("canSend")).toBool());
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
	const QVariantMap row = m_rows.at(index.row()).toMap();
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
		default: return {};
	}
}

QHash< int, QByteArray > StableListModel::roleNames() const {
	return { { StableIdRole, "stableId" }, { TitleRole, "title" }, { SubtitleRole, "subtitle" },
			 { KindRole, "kind" }, { SelectedRole, "selected" }, { StatusRole, "status" },
			 { PayloadRole, "payload" }, { DepthRole, "depth" }, { UnreadCountRole, "unreadCount" },
			 { AvatarUrlRole, "avatarUrl" }, { EnabledRole, "enabled" }, { CheckedRole, "checked" } };
}

QVariantMap StableListModel::get(int row) const {
	return row >= 0 && row < m_rows.size() ? m_rows.at(row).toMap() : QVariantMap {};
}

int StableListModel::indexOf(const QString &stableId) const {
	return m_rowIds.indexOf(stableId);
}

void StableListModel::replaceRows(const QVariantList &rows) {
	beginResetModel();
	m_rows.clear();
	m_rowIds.clear();
	m_rows.reserve(rows.size());
	m_rowIds.reserve(rows.size());
	for (const QVariant &entry : rows) {
		QVariantMap row = entry.toMap();
		row.detach();
		m_rows.push_back(row);
		m_rowIds.push_back(row.value(QStringLiteral("id")).toString());
	}
	endResetModel();
	emit countChanged();
}

void StableListModel::synchronizeRows(const QVariantList &rows) {
	QVariantList validRows;
	QStringList validIds;
	validRows.reserve(rows.size());
	validIds.reserve(rows.size());
	for (const QVariant &entry : rows) {
		QVariantMap row = entry.toMap();
		row.detach();
		const QString stableId = row.value(QStringLiteral("id")).toString();
		if (!stableId.isEmpty()) {
			validRows.push_back(row);
			validIds.push_back(stableId);
		}
	}

	const int oldCount = m_rows.size();
	for (int targetIndex = 0; targetIndex < validRows.size(); ++targetIndex) {
		const QVariantMap targetRow = validRows.at(targetIndex).toMap();
		const QString &targetId = validIds.at(targetIndex);

		int existingIndex = indexOf(targetId);
		if (existingIndex < 0) {
			beginInsertRows(QModelIndex(), targetIndex, targetIndex);
			m_rows.insert(targetIndex, targetRow);
			m_rowIds.insert(targetIndex, targetId);
			endInsertRows();
			existingIndex = targetIndex;
		} else if (existingIndex != targetIndex) {
			const int destination = existingIndex < targetIndex ? targetIndex + 1 : targetIndex;
			beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), destination);
			m_rows.move(existingIndex, targetIndex);
			m_rowIds.move(existingIndex, targetIndex);
			endMoveRows();
			existingIndex = targetIndex;
		}

		if (m_rows.at(existingIndex).toMap() != targetRow) {
			m_rows[existingIndex] = targetRow;
			emit dataChanged(index(existingIndex), index(existingIndex));
		}
	}

	while (m_rows.size() > validRows.size()) {
		const int last = m_rows.size() - 1;
		beginRemoveRows(QModelIndex(), last, last);
		m_rows.removeAt(last);
		m_rowIds.removeAt(last);
		endRemoveRows();
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
		m_rows[existing] = ownedRow;
		emit dataChanged(index(existing), index(existing));
		return;
	}
	QVariantMap ownedRow = row;
	ownedRow.detach();
	const int newRow = m_rows.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	m_rows.push_back(ownedRow);
	m_rowIds.push_back(stableId);
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
	emit countChanged();
}

void StableListModel::clear() {
	if (m_rows.isEmpty()) {
		return;
	}
	beginResetModel();
	m_rows.clear();
	m_rowIds.clear();
	endResetModel();
	emit countChanged();
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
void UiCommandController::invokeAction(const QString &actionId) {
	if (!actionId.trimmed().isEmpty()) emit actionRequested(actionId.trimmed());
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

QmlSelectionState::QmlSelectionState(QObject *parent) : QObject(parent) {
}

QString QmlSelectionState::scopeToken() const { return m_scopeToken; }
QVariant QmlSelectionState::selectedUserSession() const { return m_selectedUserSession; }
QVariant QmlSelectionState::selectedVoiceChannelId() const { return m_selectedVoiceChannelId; }
void QmlSelectionState::setScopeToken(const QString &value) {
	if (m_scopeToken == value) return;
	m_scopeToken = value;
	emit scopeTokenChanged();
}
void QmlSelectionState::setSelectedUserSession(const QVariant &value) {
	if (m_selectedUserSession == value) return;
	m_selectedUserSession = value;
	emit selectedUserSessionChanged();
}
void QmlSelectionState::setSelectedVoiceChannelId(const QVariant &value) {
	if (m_selectedVoiceChannelId == value) return;
	m_selectedVoiceChannelId = value;
	emit selectedVoiceChannelIdChanged();
}
