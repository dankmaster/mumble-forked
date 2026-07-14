// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellMenuSerializer.h"

#include <QAction>

QString ModernShellMenuSerializer::normalizedActionLabel(const QString &text) {
	QString normalized = text;
	normalized.remove(QLatin1Char('&'));
	return normalized.trimmed();
}

QString ModernShellMenuSerializer::contextActionId(const QString &scope, const QVariant &actionData) {
	const QString normalizedScope = scope.trimmed();
	const QString normalizedData  = actionData.toString().trimmed();
	if (normalizedScope.isEmpty() || normalizedData.isEmpty()) {
		return QString();
	}

	return QStringLiteral("context:%1:%2").arg(normalizedScope, normalizedData);
}

QVariantMap ModernShellMenuSerializer::separatorItem() {
	QVariantMap item;
	item.insert(QStringLiteral("kind"), QStringLiteral("separator"));
	return item;
}

QVariantMap ModernShellMenuSerializer::actionItem(const QString &id, const QString &label, const bool enabled,
												  const bool checked, const QString &tone, const QString &hint,
												  const QString &icon, const QString &secondary, const bool checkable) {
	QVariantMap item;
	item.insert(QStringLiteral("kind"), QStringLiteral("action"));
	item.insert(QStringLiteral("id"), id);
	item.insert(QStringLiteral("label"), label);
	item.insert(QStringLiteral("enabled"), enabled);
	item.insert(QStringLiteral("checked"), checked);
	item.insert(QStringLiteral("checkable"), checkable || checked);
	item.insert(QStringLiteral("icon"), icon.trimmed().isEmpty() ? actionIconId(id) : icon.trimmed());
	item.insert(QStringLiteral("secondary"), secondary.trimmed());
	if (!tone.isEmpty()) {
		item.insert(QStringLiteral("tone"), tone);
	}
	if (!hint.isEmpty()) {
		item.insert(QStringLiteral("hint"), hint);
	}
	return item;
}

QVariantMap ModernShellMenuSerializer::labelItem(const QString &label, const QString &hint) {
	QVariantMap item;
	item.insert(QStringLiteral("kind"), QStringLiteral("label"));
	item.insert(QStringLiteral("id"), QString());
	item.insert(QStringLiteral("label"), label);
	item.insert(QStringLiteral("enabled"), false);
	item.insert(QStringLiteral("checked"), false);
	item.insert(QStringLiteral("checkable"), false);
	item.insert(QStringLiteral("tone"), QString());
	if (!hint.isEmpty()) {
		item.insert(QStringLiteral("hint"), hint);
	}
	return item;
}

QVariantMap ModernShellMenuSerializer::sliderItem(const QString &id, const QString &label, const int value,
												  const int minimum, const int maximum, const int step,
												  const QString &suffix, const bool finalOnRelease,
												  const bool enabled, const QString &tone, const QString &hint) {
	QVariantMap item;
	item.insert(QStringLiteral("kind"), QStringLiteral("slider"));
	item.insert(QStringLiteral("id"), id);
	item.insert(QStringLiteral("label"), label);
	item.insert(QStringLiteral("enabled"), enabled);
	item.insert(QStringLiteral("checked"), false);
	item.insert(QStringLiteral("checkable"), false);
	item.insert(QStringLiteral("tone"), tone);
	item.insert(QStringLiteral("value"), value);
	item.insert(QStringLiteral("min"), minimum);
	item.insert(QStringLiteral("max"), maximum);
	item.insert(QStringLiteral("step"), step);
	item.insert(QStringLiteral("suffix"), suffix);
	item.insert(QStringLiteral("finalOnRelease"), finalOnRelease);
	if (!hint.isEmpty()) {
		item.insert(QStringLiteral("hint"), hint);
	}
	return item;
}

QVariantList ModernShellMenuSerializer::normalize(const QVariantList &items) {
	QVariantList normalized;
	bool previousWasSeparator = true;

	for (const QVariant &entry : items) {
		const QVariantMap item = entry.toMap();
		const QString kind     = item.value(QStringLiteral("kind")).toString();
		if (kind == QLatin1String("separator")) {
			if (previousWasSeparator) {
				continue;
			}

			normalized.push_back(item);
			previousWasSeparator = true;
			continue;
		}

		if (item.isEmpty()) {
			continue;
		}

		// A heading is already a visual group boundary. Removing an adjacent
		// separator keeps compact menus from accumulating double gutters when
		// data from different controllers is combined.
		if (kind == QLatin1String("section") || kind == QLatin1String("header")) {
			if (!normalized.isEmpty()
				&& normalized.back().toMap().value(QStringLiteral("kind")).toString()
					   == QLatin1String("separator")) {
				normalized.removeLast();
			}
			normalized.push_back(item);
			previousWasSeparator = true;
			continue;
		}

		normalized.push_back(item);
		previousWasSeparator = false;
	}

	while (!normalized.isEmpty()
		   && normalized.back().toMap().value(QStringLiteral("kind")).toString() == QLatin1String("separator")) {
		normalized.removeLast();
	}

	return normalized;
}

QVariantList ModernShellMenuSerializer::serializeActions(const QList< QAction * > &actions, const Resolver &resolver,
													 ActionRegistry *registry) {
	QVariantList items;
	for (QAction *action : actions) {
		if (!action) {
			continue;
		}

		if (action->isSeparator()) {
			items.push_back(separatorItem());
			continue;
		}

		const ActionDefinition definition = resolver ? resolver(action) : ActionDefinition();
		if (definition.id.trimmed().isEmpty()) {
			continue;
		}
		if (!definition.available) {
			continue;
		}
		if (definition.omitWhenDisabled && !action->isEnabled()) {
			continue;
		}

		if (registry) {
			RegistryEntry entry;
			entry.action            = action;
			entry.contextActionData = definition.contextActionData;
			registry->insert(definition.id, entry);
		}

		const QString label =
			definition.label.isEmpty() ? normalizedActionLabel(action->text()) : definition.label;
		const QString hint = !definition.hint.isEmpty()
								 ? definition.hint
								 : normalizedActionLabel(
									   action->toolTip().trimmed().isEmpty() ? action->statusTip() : action->toolTip());
		QVariantMap item = actionItem(definition.id, label, action->isEnabled(),
								  action->isCheckable() && action->isChecked(), definition.tone, hint,
								  definition.icon, definition.secondary, action->isCheckable());
		item.insert(QStringLiteral("shortcut"), action->shortcut().toString(QKeySequence::NativeText));
		item.insert(QStringLiteral("shortcutPortableText"),
					action->shortcut().toString(QKeySequence::PortableText));
		items.push_back(item);
	}

	return normalize(items);
}
