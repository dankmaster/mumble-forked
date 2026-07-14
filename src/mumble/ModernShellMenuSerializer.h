// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_
#define MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QVariant>

#include <functional>

class QAction;

class ModernShellMenuSerializer {
public:
	struct RegistryEntry {
		QPointer< QAction > action;
		QString contextActionData;
	};

	struct ActionDefinition {
		QString id;
		QString label;
		QString tone;
		QString hint;
		QString contextActionData;
		/// Whether this action is a valid alternative for the current context.
		/// This is separate from QAction::isEnabled() so callers can withhold an
		/// ACL-gated action while its effective permissions are still unknown.
		bool available = true;
		/// Omit the action entirely when its QAction is disabled. Context menus use
		/// this for ACL- and role-gated alternatives that are not available to the
		/// current user, while explanatory disabled actions may remain visible.
		bool omitWhenDisabled = false;
	};

	using ActionRegistry = QHash< QString, RegistryEntry >;
	using Resolver       = std::function< ActionDefinition(const QAction *) >;

	static QString normalizedActionLabel(const QString &text);
	static QString contextActionId(const QString &scope, const QVariant &actionData);

	static QVariantMap separatorItem();
	static QVariantMap actionItem(const QString &id, const QString &label, bool enabled, bool checked,
								  const QString &tone = QString(), const QString &hint = QString());
	static QVariantMap labelItem(const QString &label, const QString &hint = QString());
	static QVariantMap sliderItem(const QString &id, const QString &label, int value, int minimum, int maximum, int step,
								  const QString &suffix, bool finalOnRelease, bool enabled,
								  const QString &tone = QString(), const QString &hint = QString());
	static QVariantList normalize(const QVariantList &items);
	static QVariantList serializeActions(const QList< QAction * > &actions, const Resolver &resolver,
										 ActionRegistry *registry = nullptr);
};

#endif // MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_
