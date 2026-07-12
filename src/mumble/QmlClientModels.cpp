// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlClientModels.h"

#include <QtCore/QString>

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
		default: return {};
	}
}

QHash< int, QByteArray > StableListModel::roleNames() const {
	return { { StableIdRole, "stableId" }, { TitleRole, "title" }, { SubtitleRole, "subtitle" },
			 { KindRole, "kind" }, { SelectedRole, "selected" }, { StatusRole, "status" },
			 { PayloadRole, "payload" } };
}

QVariantMap StableListModel::get(int row) const {
	return row >= 0 && row < m_rows.size() ? m_rows.at(row).toMap() : QVariantMap {};
}

int StableListModel::indexOf(const QString &stableId) const {
	for (int i = 0; i < m_rows.size(); ++i) {
		if (m_rows.at(i).toMap().value(QStringLiteral("id")).toString() == stableId) {
			return i;
		}
	}
	return -1;
}

void StableListModel::replaceRows(const QVariantList &rows) {
	beginResetModel();
	m_rows = rows;
	endResetModel();
	emit countChanged();
}

void StableListModel::upsertRow(const QVariantMap &row) {
	const QString stableId = row.value(QStringLiteral("id")).toString();
	if (stableId.isEmpty()) {
		return;
	}
	const int existing = indexOf(stableId);
	if (existing >= 0) {
		m_rows[existing] = row;
		emit dataChanged(index(existing), index(existing));
		return;
	}
	const int newRow = m_rows.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	m_rows.push_back(row);
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
	endRemoveRows();
	emit countChanged();
}

void StableListModel::clear() {
	if (m_rows.isEmpty()) {
		return;
	}
	beginResetModel();
	m_rows.clear();
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
void UiCommandController::sendMessage(const QString &message) {
	if (!message.trimmed().isEmpty()) emit messageSendRequested(message);
}
void UiCommandController::invokeAction(const QString &actionId) {
	if (!actionId.trimmed().isEmpty()) emit actionRequested(actionId.trimmed());
}
void UiCommandController::toggleSelfMute() { emit selfMuteToggleRequested(); }
void UiCommandController::toggleSelfDeaf() { emit selfDeafToggleRequested(); }
