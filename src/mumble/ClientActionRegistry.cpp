// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ClientActionRegistry.h"

#include "ModernShellMenuSerializer.h"

#include <QtGui/QAction>

ClientActionList::ClientActionList(QObject *parent) : QObject(parent) {}
void ClientActionList::addAction(QAction *action) {
	if (action && !m_actions.contains(action)) m_actions.push_back(action);
}
QAction *ClientActionList::addSeparator() {
	QAction *separator = new QAction(this);
	separator->setSeparator(true);
	m_actions.push_back(separator);
	return separator;
}
void ClientActionList::removeAction(QAction *action) { m_actions.removeAll(action); }
void ClientActionList::clear() { m_actions.clear(); }
QList< QAction * > ClientActionList::actions() const { return m_actions; }

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
		state.insert(QStringLiteral("icon"), ModernShellMenuSerializer::actionIconId(id));
		state.insert(QStringLiteral("enabled"), entry->isEnabled());
		state.insert(QStringLiteral("checked"), entry->isChecked());
		state.insert(QStringLiteral("checkable"), entry->isCheckable());
		state.insert(QStringLiteral("shortcut"), entry->shortcut().toString(QKeySequence::NativeText));
		state.insert(QStringLiteral("shortcutPortableText"),
					 entry->shortcut().toString(QKeySequence::PortableText));
		state.insert(QStringLiteral("menuRole"), static_cast< int >(entry->menuRole()));
		state.insert(QStringLiteral("toolTip"), entry->toolTip());
		const QString statusTip = entry->statusTip().trimmed();
		const QString secondary = statusTip.isEmpty() ? entry->toolTip().trimmed() : statusTip;
		state.insert(QStringLiteral("secondary"), secondary == entry->text().trimmed() ? QString() : secondary);
		state.insert(QStringLiteral("visible"), entry->isVisible());
		state.insert(QStringLiteral("iconAvailable"), !entry->icon().isNull());
		result.push_back(state);
	}
	return result;
}
