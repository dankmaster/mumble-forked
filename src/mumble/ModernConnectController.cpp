// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernConnectController.h"

#include "ModernShellMenuSerializer.h"
#include "Net.h"

#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace {
	struct ParsedConnectInput {
		QString host;
		QString username;
		QString password;
		QString name;
		unsigned short port = 0;
		bool hasHost        = false;
		bool hasUsername    = false;
		bool hasPassword    = false;
		bool hasName        = false;
		bool hasPort        = false;
	};

	ParsedConnectInput parsedFromUrl(const QUrl &url) {
		ParsedConnectInput parsed;
		if (!url.isValid() || url.host().trimmed().isEmpty()) {
			return parsed;
		}

		parsed.host    = url.host().trimmed();
		parsed.hasHost = true;

		const int port = url.port(-1);
		if (port > 0 && port <= 65535) {
			parsed.port    = static_cast< unsigned short >(port);
			parsed.hasPort = true;
		}

		if (!url.userName().trimmed().isEmpty()) {
			parsed.username    = url.userName().simplified();
			parsed.hasUsername = true;
		}
		if (!url.password().isEmpty()) {
			parsed.password    = url.password();
			parsed.hasPassword = true;
		}

		const QUrlQuery query(url);
		if (query.hasQueryItem(QLatin1String("title"))) {
			parsed.name    = query.queryItemValue(QLatin1String("title")).simplified();
			parsed.hasName = !parsed.name.isEmpty();
		}
		return parsed;
	}

	ParsedConnectInput parseConnectInput(QString input) {
		input = input.simplified();
		if (input.isEmpty()) {
			return {};
		}

		if (input.contains(QLatin1String("://"))) {
			const ParsedConnectInput parsed = parsedFromUrl(QUrl::fromEncoded(input.toUtf8(), QUrl::TolerantMode));
			if (parsed.hasHost) {
				return parsed;
			}
		}

		const bool looksLikeAuthority =
			input.contains(QLatin1Char('@')) || input.contains(QLatin1Char(':')) || input.startsWith(QLatin1Char('['));
		if (looksLikeAuthority) {
			const ParsedConnectInput parsed =
				parsedFromUrl(QUrl::fromEncoded(QStringLiteral("mumble://%1").arg(input).toUtf8(), QUrl::TolerantMode));
			if (parsed.hasHost) {
				return parsed;
			}
		}

		return {};
	}

	QString normalizeConnectHost(QString host) {
		const ParsedConnectInput parsed = parseConnectInput(host);
		if (parsed.hasHost) {
			return parsed.host;
		}

		host = host.simplified();

		const int schemaPosition = host.indexOf(QLatin1String("://"));
		if (schemaPosition != -1) {
			host.remove(0, schemaPosition + 3);
		}

		const int pathPosition = host.indexOf(QLatin1Char('/'));
		if (pathPosition != -1) {
			host.resize(pathPosition);
		}

		const int userInfoPosition = host.lastIndexOf(QLatin1Char('@'));
		if (userInfoPosition != -1) {
			host.remove(0, userInfoPosition + 1);
		}

		return host.trimmed();
	}

	QVariantMap actionItem(const QString &id, const QString &label, const bool enabled,
						   const QString &tone = QString()) {
		return ModernShellMenuSerializer::actionItem(id, label, enabled, false, tone);
	}

	QVariantMap fieldItem(const QString &id, const QString &label, const QString &type, const QVariant &value,
						  const bool required = false) {
		QVariantMap field;
		field.insert(QStringLiteral("id"), id);
		field.insert(QStringLiteral("label"), label);
		field.insert(QStringLiteral("type"), type);
		field.insert(QStringLiteral("value"), value);
		field.insert(QStringLiteral("required"), required);
		return field;
	}

	QVariantMap favoriteItem(const FavoriteServer &server, const int index, const bool selected) {
		QVariantMap item;
		const QString label = server.qsName.trimmed().isEmpty() ? server.qsHostname : server.qsName;
		item.insert(QStringLiteral("index"), index);
		item.insert(QStringLiteral("label"), label.trimmed().isEmpty() ? QObject::tr("Saved server") : label);
		item.insert(QStringLiteral("host"), server.qsHostname);
		item.insert(QStringLiteral("port"), server.usPort);
		item.insert(QStringLiteral("username"), server.qsUsername);
		item.insert(QStringLiteral("selected"), selected);
		item.insert(QStringLiteral("subtitle"),
					QObject::tr("%1:%2 as %3")
						.arg(server.qsHostname)
						.arg(server.usPort)
						.arg(server.qsUsername.trimmed().isEmpty() ? QObject::tr("username") : server.qsUsername));
		return item;
	}
} // namespace

void ModernConnectController::open(const QList< FavoriteServer > &favorites, const Settings &settings) {
	m_favorites             = favorites;
	m_defaultUsername       = settings.qsUsername;
	m_selectedFavoriteIndex = -1;
	m_name.clear();
	m_host.clear();
	m_username = settings.qsUsername;
	m_password.clear();
	m_port = DEFAULT_MUMBLE_PORT;

	if (!m_favorites.isEmpty()) {
		selectFavorite(0);
	}
}

QVariantMap ModernConnectController::state() const {
	QVariantMap dialog;
	dialog.insert(QStringLiteral("open"), true);
	dialog.insert(QStringLiteral("id"), QStringLiteral("connect"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("connect"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Connect to a server"));
	dialog.insert(QStringLiteral("subtitle"),
				  QObject::tr("Use a saved server or enter connection details directly."));
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("connect"));

	QVariantList favorites;
	for (int i = 0; i < m_favorites.size(); ++i) {
		favorites.push_back(favoriteItem(m_favorites.at(i), i, i == m_selectedFavoriteIndex));
	}
	dialog.insert(QStringLiteral("favorites"), favorites);
	dialog.insert(QStringLiteral("selectedFavoriteIndex"), m_selectedFavoriteIndex);

	QVariantList fields;
	fields.push_back(fieldItem(QStringLiteral("name"), QObject::tr("Favorite name"), QStringLiteral("text"), m_name));
	fields.push_back(fieldItem(QStringLiteral("host"), QObject::tr("Server"), QStringLiteral("text"), m_host, true));
	QVariantMap portField =
		fieldItem(QStringLiteral("port"), QObject::tr("Port"), QStringLiteral("number"), static_cast< int >(m_port),
				  true);
	portField.insert(QStringLiteral("min"), 1);
	portField.insert(QStringLiteral("max"), 65535);
	fields.push_back(portField);
	fields.push_back(
		fieldItem(QStringLiteral("username"), QObject::tr("Username"), QStringLiteral("text"), m_username, true));
	fields.push_back(
		fieldItem(QStringLiteral("password"), QObject::tr("Server password"), QStringLiteral("password"), m_password));

	QVariantMap section;
	section.insert(QStringLiteral("title"), QObject::tr("Connection details"));
	section.insert(QStringLiteral("fields"), fields);
	dialog.insert(QStringLiteral("sections"), QVariantList { section });

	const bool hasSelection = m_selectedFavoriteIndex >= 0 && m_selectedFavoriteIndex < m_favorites.size();
	QVariantList actions;
	actions.push_back(actionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), true));
	actions.push_back(actionItem(QStringLiteral("removeFavorite"), QObject::tr("Remove saved"), hasSelection,
								 QStringLiteral("danger")));
	actions.push_back(actionItem(QStringLiteral("saveFavorite"), QObject::tr("Save server"), canSubmit()));
	actions.push_back(actionItem(QStringLiteral("connect"), QObject::tr("Connect"), canSubmit(),
								 QStringLiteral("accent")));
	dialog.insert(QStringLiteral("actions"), actions);
	dialog.insert(QStringLiteral("errors"), validationErrors());
	dialog.insert(QStringLiteral("canSubmit"), canSubmit());
	return dialog;
}

void ModernConnectController::updateField(const QString &fieldID, const QVariant &value) {
	const QString normalizedField = fieldID.trimmed();
	if (normalizedField == QLatin1String("name")) {
		m_name = value.toString().simplified();
	} else if (normalizedField == QLatin1String("host")) {
		const ParsedConnectInput parsed = parseConnectInput(value.toString());
		if (parsed.hasHost) {
			m_host = parsed.host;
			if (parsed.hasPort) {
				m_port = parsed.port;
			}
			if (parsed.hasUsername) {
				m_username = parsed.username;
			}
			if (parsed.hasPassword) {
				m_password = parsed.password;
			}
			if (parsed.hasName && m_name.trimmed().isEmpty()) {
				m_name = parsed.name;
			}
		} else {
			m_host = value.toString().simplified();
		}
	} else if (normalizedField == QLatin1String("port")) {
		const int port = value.toInt();
		m_port         = static_cast< unsigned short >(qBound(1, port, 65535));
	} else if (normalizedField == QLatin1String("username")) {
		m_username = value.toString().simplified();
	} else if (normalizedField == QLatin1String("password")) {
		m_password = value.toString();
	}

	if (normalizedField != QLatin1String("password")) {
		m_selectedFavoriteIndex = -1;
	}
}

ModernConnectController::ActionResult ModernConnectController::invokeAction(const QString &actionID,
																			const QVariantMap &payload) {
	ActionResult result;
	const QString normalizedAction = actionID.trimmed();
	if (normalizedAction == QLatin1String("cancel")) {
		result.closeDialog = true;
		return result;
	}

	if (normalizedAction == QLatin1String("selectFavorite")) {
		selectFavorite(payload.value(QStringLiteral("index")).toInt());
		return result;
	}

	if (normalizedAction == QLatin1String("newFavorite")) {
		m_selectedFavoriteIndex = -1;
		m_name.clear();
		m_host.clear();
		m_username = m_defaultUsername;
		m_password.clear();
		m_port = DEFAULT_MUMBLE_PORT;
		return result;
	}

	if (normalizedAction == QLatin1String("removeFavorite")) {
		const int requestedIndex =
			payload.contains(QStringLiteral("index")) ? payload.value(QStringLiteral("index")).toInt() : m_selectedFavoriteIndex;
		if (requestedIndex >= 0 && requestedIndex < m_favorites.size()) {
			m_selectedFavoriteIndex = requestedIndex;
			m_favorites.removeAt(m_selectedFavoriteIndex);
			result.favoritesToSave = m_favorites;
			m_selectedFavoriteIndex = -1;
			m_name.clear();
			m_host.clear();
			m_username = m_defaultUsername;
			m_password.clear();
			m_port = DEFAULT_MUMBLE_PORT;
		}
		return result;
	}

	if (normalizedAction == QLatin1String("saveFavorite")) {
		if (!canSubmit()) {
			return result;
		}

		FavoriteServer favorite = currentFavorite();
		if (m_selectedFavoriteIndex >= 0 && m_selectedFavoriteIndex < m_favorites.size()) {
			m_favorites[m_selectedFavoriteIndex] = favorite;
		} else {
			m_favorites.push_back(favorite);
			m_selectedFavoriteIndex = m_favorites.size() - 1;
		}
		result.favoritesToSave = m_favorites;
		return result;
	}

	if (normalizedAction == QLatin1String("connectFavorite")) {
		const int requestedIndex = payload.value(QStringLiteral("index"), m_selectedFavoriteIndex).toInt();
		if (requestedIndex >= 0 && requestedIndex < m_favorites.size()) {
			selectFavorite(requestedIndex);
		} else {
			result.stateChanged = false;
			return result;
		}
	}

	if (normalizedAction == QLatin1String("connect")) {
		if (!canSubmit()) {
			return result;
		}

		ConnectionRequest request;
		const ParsedConnectInput parsed = parseConnectInput(m_host);
		request.host     = parsed.hasHost ? parsed.host : normalizeConnectHost(m_host);
		request.port     = parsed.hasPort ? parsed.port : m_port;
		request.username = m_username.simplified();
		request.password = m_password;
		result.connectionRequest = request;
		result.closeDialog       = true;
		return result;
	}

	if (normalizedAction == QLatin1String("connectFavorite")) {
		if (!canSubmit()) {
			return result;
		}

		ConnectionRequest request;
		const ParsedConnectInput parsed = parseConnectInput(m_host);
		request.host     = parsed.hasHost ? parsed.host : normalizeConnectHost(m_host);
		request.port     = parsed.hasPort ? parsed.port : m_port;
		request.username = m_username.simplified();
		request.password = m_password;
		result.connectionRequest = request;
		result.closeDialog       = true;
		return result;
	}

	result.stateChanged = false;
	return result;
}

const QList< FavoriteServer > &ModernConnectController::favorites() const {
	return m_favorites;
}

void ModernConnectController::selectFavorite(const int index) {
	if (index < 0 || index >= m_favorites.size()) {
		return;
	}

	const FavoriteServer &favorite = m_favorites.at(index);
	m_selectedFavoriteIndex        = index;
	m_name                         = favorite.qsName;
	m_host                         = favorite.qsHostname;
	m_username                     = favorite.qsUsername.trimmed().isEmpty() ? m_defaultUsername : favorite.qsUsername;
	m_password                     = favorite.qsPassword;
	m_port                         = favorite.usPort == 0 ? DEFAULT_MUMBLE_PORT : favorite.usPort;
}

FavoriteServer ModernConnectController::currentFavorite() const {
	FavoriteServer favorite;
	const ParsedConnectInput parsed = parseConnectInput(m_host);
	const QString host       = parsed.hasHost ? parsed.host : normalizeConnectHost(m_host);
	favorite.qsName         = m_name.trimmed().isEmpty() ? host : m_name.simplified();
	favorite.qsHostname     = host;
	favorite.usPort         = parsed.hasPort ? parsed.port : m_port;
	favorite.qsUsername = m_username.simplified();
	favorite.qsPassword = m_password;
	return favorite;
}

QVariantMap ModernConnectController::validationErrors() const {
	QVariantMap errors;
	if (normalizeConnectHost(m_host).isEmpty()) {
		errors.insert(QStringLiteral("host"), QObject::tr("Enter a server host."));
	}
	if (m_port == 0) {
		errors.insert(QStringLiteral("port"), QObject::tr("Enter a port between 1 and 65535."));
	}
	if (m_username.simplified().isEmpty()) {
		errors.insert(QStringLiteral("username"), QObject::tr("Enter a username."));
	}
	return errors;
}

bool ModernConnectController::canSubmit() const {
	return validationErrors().isEmpty();
}
