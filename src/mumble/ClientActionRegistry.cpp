// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ClientActionRegistry.h"

#include <QtGui/QAction>

ClientActionRegistry::ClientActionRegistry(QObject *parent) : QObject(parent) {
}

void ClientActionRegistry::adopt(QAction *action) {
	if (!action || action->objectName().isEmpty()) {
		return;
	}
	const QString id = action->objectName();
	m_actions.insert(id, action);
	connect(action, &QAction::changed, this, [this, id]() { emit actionStateChanged(id); });
	emit actionStateChanged(id);
}

QAction *ClientActionRegistry::action(const QString &id) const {
	return m_actions.value(id.trimmed(), nullptr);
}

QVariantList ClientActionRegistry::stateSnapshot() const {
	QVariantList result;
	QStringList ids = m_actions.keys();
	ids.sort(Qt::CaseSensitive);
	for (const QString &id : ids) {
		const QAction *entry = m_actions.value(id);
		if (!entry) {
			continue;
		}
		QVariantMap state;
		state.insert(QStringLiteral("id"), id);
		state.insert(QStringLiteral("text"), entry->text());
		state.insert(QStringLiteral("enabled"), entry->isEnabled());
		state.insert(QStringLiteral("checked"), entry->isChecked());
		state.insert(QStringLiteral("checkable"), entry->isCheckable());
		result.push_back(state);
	}
	return result;
}
