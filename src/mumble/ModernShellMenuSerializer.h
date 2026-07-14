// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_
#define MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QString>
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
		QString icon;
		QString secondary;
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
	/// Maps both public Modern action IDs and QAction object names to the stable
	/// icon vocabulary consumed by QML. Unknown actions intentionally receive a
	/// neutral action glyph so menus never fall back to platform icon themes.
	static QString actionIconId(const QString &actionId) {
		const QString id = actionId.trimmed().toLower();
		if (id.isEmpty()) {
			return QString();
		}

		const auto contains = [&id](const char *value) { return id.contains(QLatin1String(value)); };
		if (id.startsWith(QLatin1String("context:"))) return QStringLiteral("plugin");
		if (contains("disconnect")) return QStringLiteral("disconnect");
		if (contains("quit")) return QStringLiteral("quit");
		if (contains("connect") || contains("reconnect")) return QStringLiteral("connect");
		if (contains("screenshare")) return QStringLiteral("screen-share");
		if (contains("friendadd")) return QStringLiteral("user-add");
		if (contains("friendremove") || contains("kick")) return QStringLiteral("user-remove");
		if (contains("mute")) return QStringLiteral("mute");
		if (contains("deaf")) return QStringLiteral("deafen");
		if (contains("record")) return QStringLiteral("record");
		if (contains("certificate") || contains("configcert")) return QStringLiteral("certificate");
		if (contains("settings") || contains("configdialog") || contains("preference")) {
			return QStringLiteral("settings");
		}
		if (contains("audiowizard") || contains("pushtotalk") || contains("transmit")
			|| contains("adaptivepush")) {
			return QStringLiteral("microphone");
		}
		if (contains("developer") || contains("console")) return QStringLiteral("terminal");
		if (contains("versioncheck") || contains("update") || contains("retry") || contains("resetaudio")
			|| contains("audioreset")) {
			return QStringLiteral("refresh");
		}
		if (contains("search") || contains("filter")) return QStringLiteral("search");
		if (contains("priority") || contains("audiostats") || contains("stonks") || contains("positional")
			|| contains("speechcleanup")) {
			return QStringLiteral("activity");
		}
		if (contains("volume") || contains("listen")) return QStringLiteral("volume");
		if (contains("whisper") || contains("direct")) return QStringLiteral("direct");
		if (contains("reply")) return QStringLiteral("reply");
		if (contains("ignore") || contains("hide")) return QStringLiteral("eye-off");
		if (contains("watch") || contains("view") || contains("visibility")) return QStringLiteral("eye");
		if (contains("commentreset")) return QStringLiteral("delete");
		if (contains("message") || contains("comment") || contains("feedback") || contains("tts")) {
			return QStringLiteral("message");
		}
		if (contains("ban") || contains("acl") || contains("grant")) return QStringLiteral("shield");
		if (contains("token")) return QStringLiteral("key");
		if (contains("userinfo") || contains("userlist") || contains("register")) {
			return QStringLiteral("user");
		}
		if (contains("avatar") || contains("texture")) {
			if (contains("remove") || contains("reset")) return QStringLiteral("delete");
			return QStringLiteral("user");
		}
		if (contains("favorite") || contains("pin")) return QStringLiteral("pin");
		if (contains("copy")) return QStringLiteral("copy");
		if (contains("unlink")) return QStringLiteral("unlink");
		if (contains("link")) return QStringLiteral("link");
		if (contains("edit") || contains("nickname")) return QStringLiteral("edit");
		if (contains("remove") || contains("delete")) return QStringLiteral("delete");
		if (contains("create") || contains("add")) return QStringLiteral("add");
		if (contains("move")) return QStringLiteral("move");
		if (contains("join")) return QStringLiteral("join");
		if (contains("metachannel")) return QStringLiteral("voice-room");
		if (contains("information") || contains("about") || contains("help") || contains("whatsthis")) {
			return QStringLiteral("info");
		}
		if (contains("minimal")) return QStringLiteral("menu");
		return QStringLiteral("action");
	}

	static QVariantMap separatorItem();
	static QVariantMap actionItem(const QString &id, const QString &label, bool enabled, bool checked,
								  const QString &tone = QString(), const QString &hint = QString(),
								  const QString &icon = QString(), const QString &secondary = QString(),
								  bool checkable = false);
	static QVariantMap labelItem(const QString &label, const QString &hint = QString());
	static QVariantMap sliderItem(const QString &id, const QString &label, int value, int minimum, int maximum, int step,
								  const QString &suffix, bool finalOnRelease, bool enabled,
								  const QString &tone = QString(), const QString &hint = QString());
	static QVariantList normalize(const QVariantList &items);
	static QVariantList serializeActions(const QList< QAction * > &actions, const Resolver &resolver,
										 ActionRegistry *registry = nullptr);
};

#endif // MUMBLE_MUMBLE_MODERNSHELLMENUSERIALIZER_H_
