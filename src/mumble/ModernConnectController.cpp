// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernConnectController.h"

#include "ModernShellMenuSerializer.h"
#include "Net.h"

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

#include <algorithm>

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

} // namespace

void ModernConnectController::open(const QList< FavoriteServer > &favorites, const Settings &settings,
								   const QMap< UnresolvedServerAddress, unsigned int > &pingCache) {
	m_favorites             = favorites;
	m_defaultUsername       = settings.qsUsername;
	m_selectedFavoriteIndex = -1;
	m_editorReturnFavoriteIndex = -1;
	m_favoriteTelemetry.clear();
	m_sources.clear();
	m_sources.insert(QStringLiteral("public"), SourceState {});
	m_sources.insert(QStringLiteral("lan"), SourceState {});
	m_selectedServerIDs.clear();
	m_activeSource = QStringLiteral("favorites");
	m_filter.clear();
	m_name.clear();
	m_host.clear();
	m_username = settings.qsUsername;
	m_password.clear();
	m_port       = DEFAULT_MUMBLE_PORT;
	m_editorOpen = false;
	m_editorBaseline = FavoriteServer {};
	clearConfirmation();

	for (auto it = pingCache.cbegin(); it != pingCache.cend(); ++it) {
		FavoriteTelemetry telemetry;
		telemetry.hasPing = true;
		telemetry.ping    = it.value();
		m_favoriteTelemetry.insert(favoriteTelemetryKey(it.key().hostname, it.key().port), telemetry);
	}

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
				  m_editorOpen ? QObject::tr("Review the server details, then save or connect.")
							   : QObject::tr("Choose a saved server or add one."));
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("connect"));
	dialog.insert(QStringLiteral("width"), 860);
	dialog.insert(QStringLiteral("height"), 640);

	QVariantList favorites;
	for (int i = 0; i < m_favorites.size(); ++i) {
		const QString favoriteID = stableServerID(QStringLiteral("favorites"), QString(),
			m_favorites.at(i).qsHostname, m_favorites.at(i).usPort, m_favorites.at(i).qsName);
		favorites.push_back(favoriteItem(m_favorites.at(i), i,
			favoriteID == m_selectedServerIDs.value(QStringLiteral("favorites"))));
	}
	dialog.insert(QStringLiteral("favorites"), favorites);
	dialog.insert(QStringLiteral("sources"), sourceItems());
	dialog.insert(QStringLiteral("activeSource"), m_activeSource);
	dialog.insert(QStringLiteral("filter"), m_filter);
	const QVariantList activeRows = sourceRows(m_activeSource);
	dialog.insert(QStringLiteral("sourceRows"), activeRows);
	dialog.insert(QStringLiteral("selectedServerId"), m_selectedServerIDs.value(m_activeSource));
	int selectedServerIndex = -1;
	for (int index = 0; index < activeRows.size(); ++index) {
		if (activeRows.at(index).toMap().value(QStringLiteral("selected")).toBool()) {
			selectedServerIndex = index;
			break;
		}
	}
	dialog.insert(QStringLiteral("selectedServerIndex"), selectedServerIndex);
	dialog.insert(QStringLiteral("selectedFavoriteIndex"), m_selectedFavoriteIndex);
	dialog.insert(QStringLiteral("editorOpen"), m_editorOpen);
	dialog.insert(QStringLiteral("editorDirty"), editorDirty());
	dialog.insert(QStringLiteral("editorTitle"),
				  m_selectedFavoriteIndex >= 0 ? QObject::tr("Edit server") : QObject::tr("Add server"));
	dialog.insert(QStringLiteral("initialFocusId"), m_editorOpen ? QStringLiteral("dialogField_host")
		: (!activeRows.isEmpty() ? QStringLiteral("connectFavoriteList") : QStringLiteral("connectNewFavoriteButton")));
	QVariantMap confirmation;
	if (!m_pendingConfirmation.kind.isEmpty()) {
		confirmation.insert(QStringLiteral("kind"), m_pendingConfirmation.kind);
		confirmation.insert(QStringLiteral("targetId"), m_pendingConfirmation.targetID);
		confirmation.insert(QStringLiteral("title"), m_pendingConfirmation.title);
		confirmation.insert(QStringLiteral("message"), m_pendingConfirmation.message);
		const bool publicConsent = m_pendingConfirmation.kind == QLatin1String("publicConsent");
		confirmation.insert(QStringLiteral("confirmActionId"), publicConsent
			? QStringLiteral("confirmEnablePublicSource")
			: m_pendingConfirmation.kind == QLatin1String("remove") ? QStringLiteral("confirmRemoveFavorite")
																		 : QStringLiteral("confirmDiscardEditor"));
		confirmation.insert(QStringLiteral("confirmLabel"), publicConsent ? QObject::tr("Enable public servers")
			: m_pendingConfirmation.kind == QLatin1String("remove") ? QObject::tr("Remove server")
																		 : QObject::tr("Discard changes"));
		confirmation.insert(QStringLiteral("cancelLabel"), publicConsent ? QObject::tr("Keep disabled")
																	  : QObject::tr("Keep editing"));
		confirmation.insert(QStringLiteral("confirmTone"),
			publicConsent ? QStringLiteral("accent") : QStringLiteral("danger"));
	}
	dialog.insert(QStringLiteral("pendingConfirmation"), confirmation);

	QVariantList fields;
	fields.push_back(fieldItem(QStringLiteral("name"), QObject::tr("Server title"), QStringLiteral("text"), m_name));
	fields.push_back(fieldItem(QStringLiteral("host"), QObject::tr("Server address"), QStringLiteral("text"), m_host,
							   true));
	QVariantMap portField =
		fieldItem(QStringLiteral("port"), QObject::tr("Port"), QStringLiteral("number"), static_cast< int >(m_port),
				  true);
	portField.insert(QStringLiteral("min"), 1);
	portField.insert(QStringLiteral("max"), 65535);
	// Ports are identifiers, not quantities. Locale grouping (for example
	// "64 738") makes the endpoint harder to scan and copy correctly.
	portField.insert(QStringLiteral("useGrouping"), false);
	fields.push_back(portField);
	fields.push_back(
		fieldItem(QStringLiteral("username"), QObject::tr("Username"), QStringLiteral("text"), m_username, true));
	fields.push_back(
		fieldItem(QStringLiteral("password"), QObject::tr("Server password"), QStringLiteral("password"), m_password));

	QVariantMap section;
	section.insert(QStringLiteral("title"), QObject::tr("Details"));
	section.insert(QStringLiteral("fields"), fields);
	dialog.insert(QStringLiteral("sections"), m_editorOpen ? QVariantList { section } : QVariantList {});

	const bool hasFavoriteSelection = m_selectedFavoriteIndex >= 0 && m_selectedFavoriteIndex < m_favorites.size();
	const bool canConnect = canSubmit() && (m_editorOpen || selectedServerIndex >= 0);
	QVariantList actions;
	if (!m_editorOpen) {
		actions.push_back(actionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), true));
		actions.push_back(actionItem(QStringLiteral("editFavorite"), QObject::tr("Edit"),
			m_activeSource == QLatin1String("favorites") && hasFavoriteSelection));
	} else {
		actions.push_back(actionItem(QStringLiteral("backToFavorites"), QObject::tr("Back"), true));
		actions.push_back(actionItem(QStringLiteral("removeFavorite"), QObject::tr("Remove"), hasFavoriteSelection,
									 QStringLiteral("danger")));
		actions.push_back(actionItem(QStringLiteral("saveFavorite"), QObject::tr("Save"), canSubmit()));
	}
	actions.push_back(actionItem(QStringLiteral("connect"), QObject::tr("Connect"), canConnect,
								 QStringLiteral("accent")));
	dialog.insert(QStringLiteral("actions"), actions);
	dialog.insert(QStringLiteral("errors"), validationErrors());
	dialog.insert(QStringLiteral("canSubmit"), canSubmit());
	return dialog;
}

void ModernConnectController::updateField(const QString &fieldID, const QVariant &value) {
	const QString normalizedField = fieldID.trimmed();
	if (normalizedField == QLatin1String("filter") || normalizedField == QLatin1String("connect.filter")) {
		m_filter = value.toString().simplified();
	} else if (normalizedField == QLatin1String("name")) {
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
}

ModernConnectController::ActionResult ModernConnectController::invokeAction(const QString &actionID,
																			const QVariantMap &payload) {
	ActionResult result;
	const QString normalizedAction = actionID.trimmed();
	if (normalizedAction == QLatin1String("cancel")) {
		if (m_editorOpen && editorDirty()) {
			requestDiscardConfirmation();
		} else {
			result.closeDialog = true;
		}
		return result;
	}

	if (normalizedAction == QLatin1String("dismissConfirmation")) {
		if (m_pendingConfirmation.kind == QLatin1String("publicConsent")) {
			const quint64 consentGeneration = m_pendingConfirmation.generation;
			SourceState &publicSource = m_sources[QStringLiteral("public")];
			if (consentGeneration == publicSource.generation && publicSource.status == QLatin1String("loading")) {
				publicSource.status    = QStringLiteral("unavailable");
				publicSource.error     = QObject::tr("Public server discovery remains disabled.");
				publicSource.retryable = true;
			}
			result.publicListDisabledSetting = true;
		}
		clearConfirmation();
		return result;
	}

	if (normalizedAction == QLatin1String("confirmEnablePublicSource")) {
		if (m_pendingConfirmation.kind != QLatin1String("publicConsent")) {
			result.stateChanged = false;
			return result;
		}
		const quint64 consentGeneration = m_pendingConfirmation.generation;
		clearConfirmation();
		const SourceState &publicSource = m_sources.value(QStringLiteral("public"));
		if (consentGeneration == 0 || consentGeneration != publicSource.generation
			|| publicSource.status != QLatin1String("loading")) {
			// The consent is stale, but clearing the nested confirmation is still
			// a visible state change that must be published to the frontend.
			return result;
		}
		result.publicListDisabledSetting = false;
		result.sourceOperation = SourceOperationRequest {
			QStringLiteral("public"), QStringLiteral("retry"), consentGeneration
		};
		return result;
	}

	if (normalizedAction == QLatin1String("confirmDiscardEditor")) {
		if (m_pendingConfirmation.kind == QLatin1String("discard")) {
			clearConfirmation();
			leaveEditor(true);
		}
		return result;
	}

	if (normalizedAction == QLatin1String("selectSource")) {
		// Source navigation is a browser concern. Once the favorite editor is
		// open, Back/Escape must return to the same Favorites context without a
		// hidden source change taking place behind the form.
		if (m_editorOpen) {
			result.stateChanged = false;
			return result;
		}
		const QString sourceID = payload.value(QStringLiteral("sourceId")).toString().trimmed().toLower();
		if (!validSourceID(sourceID)) {
			result.stateChanged = false;
			return result;
		}
		m_activeSource = sourceID;
		const QVariantList rows = sourceRows(sourceID);
		if (!rows.isEmpty()) {
			QString selectedID = m_selectedServerIDs.value(sourceID);
			const bool selectedVisible = std::any_of(rows.cbegin(), rows.cend(), [&selectedID](const QVariant &row) {
				return row.toMap().value(QStringLiteral("id")).toString() == selectedID;
			});
			if (!selectedVisible) {
				selectedID = rows.constFirst().toMap().value(QStringLiteral("id")).toString();
			}
			selectServer(sourceID, selectedID);
		} else if (!m_editorOpen) {
			m_selectedFavoriteIndex = -1;
			m_name.clear();
			m_host.clear();
			m_username = m_defaultUsername;
			m_password.clear();
			m_port = DEFAULT_MUMBLE_PORT;
		}
		if (sourceID != QLatin1String("favorites") && m_sources.value(sourceID).status == QLatin1String("idle")) {
			const quint64 generation = beginSourceRefresh(sourceID);
			result.sourceOperation = SourceOperationRequest { sourceID, QStringLiteral("retry"), generation };
		}
		return result;
	}

	if (normalizedAction == QLatin1String("retrySource")) {
		const QString sourceID = payload.value(QStringLiteral("sourceId"), m_activeSource).toString().toLower();
		if (!validSourceID(sourceID) || sourceID == QLatin1String("favorites")) {
			result.stateChanged = false;
			return result;
		}
		const quint64 generation = beginSourceRefresh(sourceID);
		result.sourceOperation = SourceOperationRequest { sourceID, QStringLiteral("retry"), generation };
		return result;
	}

	if (normalizedAction == QLatin1String("cancelSource")) {
		const QString sourceID = payload.value(QStringLiteral("sourceId"), m_activeSource).toString().toLower();
		if (!validSourceID(sourceID) || sourceID == QLatin1String("favorites")) {
			result.stateChanged = false;
			return result;
		}
		SourceState &source = m_sources[sourceID];
		if (source.status != QLatin1String("loading")) {
			result.stateChanged = false;
			return result;
		}
		++source.generation;
		source.status = QStringLiteral("cancelled");
		source.error.clear();
		result.sourceOperation = SourceOperationRequest { sourceID, QStringLiteral("cancel"), source.generation };
		return result;
	}

	if (normalizedAction == QLatin1String("selectFavorite") || normalizedAction == QLatin1String("selectServer")) {
		const QString sourceID = normalizedAction == QLatin1String("selectFavorite")
			? QStringLiteral("favorites")
			: payload.value(QStringLiteral("sourceId"), m_activeSource).toString().toLower();
		const QString serverID = payload.value(QStringLiteral("id")).toString();
		const int requestedIndex = payload.value(QStringLiteral("index"), -1).toInt();
		if (!selectServer(sourceID, serverID, requestedIndex)) {
			result.stateChanged = false;
			return result;
		}
		const bool edit = payload.value(QStringLiteral("edit")).toBool();
		if (edit && sourceID == QLatin1String("favorites") && m_selectedFavoriteIndex >= 0) {
			m_editorReturnFavoriteIndex = m_selectedFavoriteIndex;
			m_editorBaseline = currentFavorite();
			m_editorOpen = true;
		}
		return result;
	}

	if (normalizedAction == QLatin1String("editFavorite")) {
		const int requestedIndex = payload.contains(QStringLiteral("index"))
									   ? payload.value(QStringLiteral("index")).toInt()
									   : m_selectedFavoriteIndex;
		if (requestedIndex >= 0 && requestedIndex < m_favorites.size()) {
			selectFavorite(requestedIndex);
			m_editorReturnFavoriteIndex = requestedIndex;
			m_editorBaseline = currentFavorite();
			m_editorOpen = true;
		}
		return result;
	}

	if (normalizedAction == QLatin1String("newFavorite")) {
		m_editorReturnFavoriteIndex = m_selectedFavoriteIndex;
		m_selectedFavoriteIndex = -1;
		m_name.clear();
		m_host.clear();
		m_username = m_defaultUsername;
		m_password.clear();
		m_port = DEFAULT_MUMBLE_PORT;
		m_editorBaseline = currentFavorite();
		m_editorOpen = true;
		clearConfirmation();
		return result;
	}

	if (normalizedAction == QLatin1String("backToFavorites") || normalizedAction == QLatin1String("backToServers")) {
		if (editorDirty()) {
			requestDiscardConfirmation();
		} else {
			leaveEditor(true);
		}
		return result;
	}

	if (normalizedAction == QLatin1String("removeFavorite")) {
		const int requestedIndex = payload.contains(QStringLiteral("index"))
									   ? payload.value(QStringLiteral("index")).toInt()
									   : m_selectedFavoriteIndex;
		if (requestedIndex >= 0 && requestedIndex < m_favorites.size()) {
			const FavoriteServer &favorite = m_favorites.at(requestedIndex);
			requestRemoveConfirmation(stableServerID(QStringLiteral("favorites"), QString(), favorite.qsHostname,
				favorite.usPort, favorite.qsName));
		}
		return result;
	}

	if (normalizedAction == QLatin1String("confirmRemoveFavorite")) {
		if (m_pendingConfirmation.kind != QLatin1String("remove")) {
			result.stateChanged = false;
			return result;
		}
		int removeIndex = -1;
		for (int index = 0; index < m_favorites.size(); ++index) {
			const FavoriteServer &favorite = m_favorites.at(index);
			if (stableServerID(QStringLiteral("favorites"), QString(), favorite.qsHostname, favorite.usPort,
					favorite.qsName) == m_pendingConfirmation.targetID) {
				removeIndex = index;
				break;
			}
		}
		clearConfirmation();
		if (removeIndex < 0) {
			result.stateChanged = false;
			return result;
		}
		m_favorites.removeAt(removeIndex);
		result.favoritesToSave = m_favorites;
		m_editorOpen = false;
		m_editorReturnFavoriteIndex = -1;
		if (m_favorites.isEmpty()) {
			m_selectedFavoriteIndex = -1;
			m_selectedServerIDs.remove(QStringLiteral("favorites"));
			m_name.clear();
			m_host.clear();
			m_username = m_defaultUsername;
			m_password.clear();
			m_port = DEFAULT_MUMBLE_PORT;
		} else {
			selectFavorite(qMin(removeIndex, m_favorites.size() - 1));
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
		selectFavorite(m_selectedFavoriteIndex);
		m_editorOpen = false;
		m_editorReturnFavoriteIndex = -1;
		m_editorBaseline = currentFavorite();
		m_activeSource = QStringLiteral("favorites");
		clearConfirmation();
		return result;
	}

	if (normalizedAction == QLatin1String("connectFavorite") || normalizedAction == QLatin1String("connectServer")) {
		const QString sourceID = normalizedAction == QLatin1String("connectFavorite")
			? QStringLiteral("favorites") : payload.value(QStringLiteral("sourceId"), m_activeSource).toString();
		if (!selectServer(sourceID, payload.value(QStringLiteral("id")).toString(),
				payload.value(QStringLiteral("index"), -1).toInt())) {
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

	if (normalizedAction == QLatin1String("connectFavorite") || normalizedAction == QLatin1String("connectServer")) {
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

quint64 ModernConnectController::beginSourceRefresh(const QString &sourceID) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource) || normalizedSource == QLatin1String("favorites")) {
		return 0;
	}
	SourceState &source = m_sources[normalizedSource];
	++source.generation;
	source.status = QStringLiteral("loading");
	source.error.clear();
	source.retryable = true;
	return source.generation;
}

bool ModernConnectController::applySourceServers(const QString &sourceID, const quint64 generation,
													 const QList< ServerEntry > &servers) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource) || normalizedSource == QLatin1String("favorites")) {
		return false;
	}
	SourceState &source = m_sources[normalizedSource];
	const bool acceptsLiveLanUpdate = normalizedSource == QLatin1String("lan")
		&& source.status == QLatin1String("ready");
	if (generation == 0 || generation != source.generation
		|| (source.status != QLatin1String("loading") && !acceptsLiveLanUpdate)) {
		return false;
	}

	source.servers.clear();
	QSet< QString > seenIDs;
	for (ServerEntry server : servers) {
		server.host = server.host.simplified();
		if (server.host.isEmpty()) {
			continue;
		}
		server.port = server.port == 0 ? DEFAULT_MUMBLE_PORT : server.port;
		server.label = server.label.simplified();
		server.username = server.username.simplified();
		server.id = stableServerID(normalizedSource, server.id, server.host, server.port, server.label);
		if (seenIDs.contains(server.id)) {
			continue;
		}
		seenIDs.insert(server.id);
		source.servers.push_back(server);
	}
	source.status = QStringLiteral("ready");
	source.error.clear();
	source.retryable = true;

	const QString selectedID = m_selectedServerIDs.value(normalizedSource);
	const bool selectionStillExists = std::any_of(source.servers.cbegin(), source.servers.cend(),
		[&selectedID](const ServerEntry &server) { return server.id == selectedID; });
	if (!selectionStillExists) {
		if (source.servers.isEmpty()) {
			m_selectedServerIDs.remove(normalizedSource);
		} else {
			m_selectedServerIDs.insert(normalizedSource, source.servers.constFirst().id);
			if (m_activeSource == normalizedSource && !m_editorOpen) {
				loadServerIntoConnectionFields(source.servers.constFirst());
			}
		}
	}
	return true;
}

bool ModernConnectController::applySourceError(const QString &sourceID, const quint64 generation,
											  const QString &message, const bool retryable) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource) || normalizedSource == QLatin1String("favorites")) {
		return false;
	}
	SourceState &source = m_sources[normalizedSource];
	if (generation == 0 || generation != source.generation || source.status != QLatin1String("loading")) {
		return false;
	}
	source.status = QStringLiteral("error");
	source.error = message.simplified().isEmpty() ? QObject::tr("The server list could not be loaded.")
															 : message.simplified();
	source.retryable = retryable;
	return true;
}

bool ModernConnectController::applySourceUnavailable(const QString &sourceID, const quint64 generation,
												  const QString &message) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource) || normalizedSource == QLatin1String("favorites")) {
		return false;
	}
	SourceState &source = m_sources[normalizedSource];
	if (generation == 0 || generation != source.generation || source.status != QLatin1String("loading")) {
		return false;
	}
	source.status    = QStringLiteral("unavailable");
	source.error     = message.simplified();
	source.retryable = false;
	return true;
}

bool ModernConnectController::setSourceUnavailable(const QString &sourceID, const QString &message) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource) || normalizedSource == QLatin1String("favorites")) {
		return false;
	}
	SourceState &source = m_sources[normalizedSource];
	++source.generation;
	source.status = QStringLiteral("unavailable");
	source.error = message.simplified();
	source.retryable = false;
	return true;
}

bool ModernConnectController::requestPublicListConsent(const quint64 generation) {
	const SourceState &publicSource = m_sources.value(QStringLiteral("public"));
	if (generation == 0 || generation != publicSource.generation
		|| publicSource.status != QLatin1String("loading")) {
		return false;
	}
	m_pendingConfirmation.kind       = QStringLiteral("publicConsent");
	m_pendingConfirmation.targetID   = QStringLiteral("public");
	m_pendingConfirmation.title      = QObject::tr("Enable public server discovery?");
	m_pendingConfirmation.message    = QObject::tr(
		"Loading the public directory sends a network request to the Mumble registry, which can see your IP "
		"address. If you connect to a listed server, that server can also see your IP address. Enable public "
		"server discovery on this device?");
	m_pendingConfirmation.generation = generation;
	return true;
}

bool ModernConnectController::setFavoritePing(const QString &host, const unsigned short port, const quint32 ping,
											  const std::optional< quint32 > users,
											  const std::optional< quint32 > maxUsers) {
	const QString key = favoriteTelemetryKey(host, port);
	FavoriteTelemetry telemetry = m_favoriteTelemetry.value(key);
	FavoriteTelemetry next      = telemetry;
	next.hasPing                = true;
	next.ping                   = ping;
	if (users) {
		next.hasUsers = true;
		next.users    = *users;
		next.maxUsers = maxUsers.value_or(0);
	}

	if (telemetry.hasPing == next.hasPing && telemetry.hasUsers == next.hasUsers && telemetry.ping == next.ping
		&& telemetry.users == next.users && telemetry.maxUsers == next.maxUsers) {
		return false;
	}

	m_favoriteTelemetry.insert(key, next);
	return true;
}

QVariantMap ModernConnectController::favoriteItem(const FavoriteServer &server, const int index,
												  const bool selected) const {
	QVariantMap item;
	const QString label = server.qsName.trimmed().isEmpty() ? server.qsHostname : server.qsName;
	const FavoriteTelemetry telemetry =
		m_favoriteTelemetry.value(favoriteTelemetryKey(server.qsHostname, server.usPort));
	item.insert(QStringLiteral("id"), stableServerID(QStringLiteral("favorites"), QString(), server.qsHostname,
		server.usPort, server.qsName));
	item.insert(QStringLiteral("sourceId"), QStringLiteral("favorites"));
	item.insert(QStringLiteral("index"), index);
	item.insert(QStringLiteral("label"), label.trimmed().isEmpty() ? QObject::tr("Saved server") : label);
	item.insert(QStringLiteral("host"), server.qsHostname);
	item.insert(QStringLiteral("port"), server.usPort);
	item.insert(QStringLiteral("username"), server.qsUsername);
	item.insert(QStringLiteral("selected"), selected);
	item.insert(QStringLiteral("usersLabel"),
				telemetry.hasUsers
					? (telemetry.maxUsers > 0 ? QObject::tr("Users: %1/%2").arg(telemetry.users).arg(telemetry.maxUsers)
											  : QObject::tr("Users: %1").arg(telemetry.users))
					: QObject::tr("Users: -"));
	item.insert(QStringLiteral("usersValue"),
				telemetry.hasUsers
					? (telemetry.maxUsers > 0 ? QObject::tr("%1/%2").arg(telemetry.users).arg(telemetry.maxUsers)
											  : QString::number(telemetry.users))
					: QStringLiteral("-"));
	item.insert(QStringLiteral("pingLabel"),
				telemetry.hasPing ? QObject::tr("Ping: %1 ms").arg(telemetry.ping) : QObject::tr("Ping: -"));
	item.insert(QStringLiteral("pingValue"),
				telemetry.hasPing ? QObject::tr("%1 ms").arg(telemetry.ping) : QStringLiteral("-"));
	item.insert(QStringLiteral("tooltip"), QObject::tr("%1:%2").arg(server.qsHostname).arg(server.usPort));
	item.insert(QStringLiteral("subtitle"),
				QObject::tr("%1:%2 / %3")
					.arg(server.qsHostname)
					.arg(server.usPort)
					.arg(server.qsUsername.trimmed().isEmpty() ? QObject::tr("username") : server.qsUsername));
	return item;
}

QVariantMap ModernConnectController::serverItem(const ServerEntry &server, const QString &sourceID, const int index,
												const bool selected) const {
	QVariantMap item;
	item.insert(QStringLiteral("id"), server.id);
	item.insert(QStringLiteral("sourceId"), sourceID);
	item.insert(QStringLiteral("index"), index);
	item.insert(QStringLiteral("label"), server.label.isEmpty() ? server.host : server.label);
	item.insert(QStringLiteral("host"), server.host);
	item.insert(QStringLiteral("port"), server.port);
	item.insert(QStringLiteral("username"), server.username);
	item.insert(QStringLiteral("country"), server.country);
	item.insert(QStringLiteral("region"), server.region);
	item.insert(QStringLiteral("selected"), selected);
	item.insert(QStringLiteral("usersLabel"), server.users
		? (server.maxUsers && *server.maxUsers > 0
			? QObject::tr("Users: %1/%2").arg(*server.users).arg(*server.maxUsers)
			: QObject::tr("Users: %1").arg(*server.users))
		: QObject::tr("Users: -"));
	item.insert(QStringLiteral("usersValue"), server.users
		? (server.maxUsers && *server.maxUsers > 0
			? QObject::tr("%1/%2").arg(*server.users).arg(*server.maxUsers)
			: QString::number(*server.users))
		: QStringLiteral("-"));
	item.insert(QStringLiteral("pingLabel"), server.ping ? QObject::tr("Ping: %1 ms").arg(*server.ping)
														 : QObject::tr("Ping: -"));
	item.insert(QStringLiteral("pingValue"), server.ping ? QObject::tr("%1 ms").arg(*server.ping)
														 : QStringLiteral("-"));
	item.insert(QStringLiteral("tooltip"), QObject::tr("%1:%2").arg(server.host).arg(server.port));
	QStringList location;
	if (!server.region.trimmed().isEmpty()) {
		location.push_back(server.region.trimmed());
	}
	if (!server.country.trimmed().isEmpty()) {
		location.push_back(server.country.trimmed());
	}
	const QString endpoint = QObject::tr("%1:%2").arg(server.host).arg(server.port);
	item.insert(QStringLiteral("subtitle"), location.isEmpty() ? endpoint
																			 : QObject::tr("%1 / %2").arg(endpoint, location.join(QStringLiteral(", "))));
	return item;
}

QVariantList ModernConnectController::sourceRows(const QString &sourceID) const {
	const QString normalizedSource = sourceID.trimmed().toLower();
	QVariantList rows;
	const QString filter = m_filter.simplified();
	auto matchesFilter = [&filter](const QStringList &values) {
		if (filter.isEmpty()) {
			return true;
		}
		for (const QString &value : values) {
			if (value.contains(filter, Qt::CaseInsensitive)) {
				return true;
			}
		}
		return false;
	};

	if (normalizedSource == QLatin1String("favorites")) {
		for (int index = 0; index < m_favorites.size(); ++index) {
			const FavoriteServer &favorite = m_favorites.at(index);
			if (!matchesFilter({ favorite.qsName, favorite.qsHostname, favorite.qsUsername })) {
				continue;
			}
			const QString id = stableServerID(normalizedSource, QString(), favorite.qsHostname, favorite.usPort,
				favorite.qsName);
			QVariantMap item = favoriteItem(favorite, index, id == m_selectedServerIDs.value(normalizedSource));
			item.insert(QStringLiteral("filteredIndex"), rows.size());
			rows.push_back(item);
		}
		return rows;
	}

	const SourceState source = m_sources.value(normalizedSource);
	for (int index = 0; index < source.servers.size(); ++index) {
		const ServerEntry &server = source.servers.at(index);
		if (!matchesFilter({ server.label, server.host, server.country, server.region })) {
			continue;
		}
		QVariantMap item = serverItem(server, normalizedSource, index,
			server.id == m_selectedServerIDs.value(normalizedSource));
		item.insert(QStringLiteral("filteredIndex"), rows.size());
		rows.push_back(item);
	}
	return rows;
}

QVariantList ModernConnectController::sourceItems() const {
	QVariantList sources;
	const QStringList ids { QStringLiteral("favorites"), QStringLiteral("public"), QStringLiteral("lan") };
	for (const QString &sourceID : ids) {
		QVariantMap item;
		const bool favoritesSource = sourceID == QLatin1String("favorites");
		const SourceState source = m_sources.value(sourceID);
		const int totalCount = favoritesSource ? m_favorites.size() : source.servers.size();
		item.insert(QStringLiteral("id"), sourceID);
		item.insert(QStringLiteral("label"), favoritesSource ? QObject::tr("Favorites")
			: sourceID == QLatin1String("public") ? QObject::tr("Public") : QObject::tr("LAN"));
		item.insert(QStringLiteral("selected"), sourceID == m_activeSource);
		item.insert(QStringLiteral("status"), favoritesSource ? QStringLiteral("ready") : source.status);
		item.insert(QStringLiteral("count"), totalCount);
		item.insert(QStringLiteral("filteredCount"), sourceRows(sourceID).size());
		item.insert(QStringLiteral("error"), source.error);
		item.insert(QStringLiteral("canRetry"), !favoritesSource && source.retryable
			&& source.status != QLatin1String("loading"));
		item.insert(QStringLiteral("canCancel"), !favoritesSource && source.status == QLatin1String("loading"));
		sources.push_back(item);
	}
	return sources;
}

void ModernConnectController::selectFavorite(const int index) {
	if (index < 0 || index >= m_favorites.size()) {
		return;
	}

	const FavoriteServer &favorite = m_favorites.at(index);
	m_selectedFavoriteIndex        = index;
	m_selectedServerIDs.insert(QStringLiteral("favorites"),
		stableServerID(QStringLiteral("favorites"), QString(), favorite.qsHostname, favorite.usPort, favorite.qsName));
	m_name                         = favorite.qsName;
	m_host                         = favorite.qsHostname;
	m_username                     = favorite.qsUsername.trimmed().isEmpty() ? m_defaultUsername : favorite.qsUsername;
	m_password                     = favorite.qsPassword;
	m_port                         = favorite.usPort == 0 ? DEFAULT_MUMBLE_PORT : favorite.usPort;
}

bool ModernConnectController::selectServer(const QString &sourceID, const QString &serverID, const int fallbackIndex) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (!validSourceID(normalizedSource)) {
		return false;
	}
	if (normalizedSource == QLatin1String("favorites")) {
		int index = -1;
		if (!serverID.isEmpty()) {
			for (int candidate = 0; candidate < m_favorites.size(); ++candidate) {
				const FavoriteServer &favorite = m_favorites.at(candidate);
				if (stableServerID(normalizedSource, QString(), favorite.qsHostname, favorite.usPort, favorite.qsName)
					== serverID) {
					index = candidate;
					break;
				}
			}
		}
		if (index < 0) {
			index = fallbackIndex;
		}
		if (index < 0 || index >= m_favorites.size()) {
			return false;
		}
		m_activeSource = normalizedSource;
		if (!m_editorOpen) {
			selectFavorite(index);
		} else {
			const FavoriteServer &favorite = m_favorites.at(index);
			m_selectedFavoriteIndex = index;
			m_selectedServerIDs.insert(normalizedSource, stableServerID(normalizedSource, QString(),
				favorite.qsHostname, favorite.usPort, favorite.qsName));
		}
		return true;
	}

	const SourceState &source = m_sources[normalizedSource];
	int index = -1;
	if (!serverID.isEmpty()) {
		for (int candidate = 0; candidate < source.servers.size(); ++candidate) {
			if (source.servers.at(candidate).id == serverID) {
				index = candidate;
				break;
			}
		}
	}
	if (index < 0) {
		index = fallbackIndex;
	}
	if (index < 0 || index >= source.servers.size()) {
		return false;
	}
	m_activeSource = normalizedSource;
	m_selectedServerIDs.insert(normalizedSource, source.servers.at(index).id);
	if (!m_editorOpen) {
		loadServerIntoConnectionFields(source.servers.at(index));
	}
	return true;
}

void ModernConnectController::loadServerIntoConnectionFields(const ServerEntry &server) {
	m_selectedFavoriteIndex = -1;
	m_name = server.label;
	m_host = server.host;
	m_port = server.port == 0 ? DEFAULT_MUMBLE_PORT : server.port;
	m_username = server.username.isEmpty() ? m_defaultUsername : server.username;
	m_password.clear();
}

void ModernConnectController::leaveEditor(const bool restoreSelection) {
	m_editorOpen = false;
	clearConfirmation();
	if (restoreSelection) {
		if (m_activeSource == QLatin1String("favorites") && m_editorReturnFavoriteIndex >= 0
			&& m_editorReturnFavoriteIndex < m_favorites.size()) {
			selectFavorite(m_editorReturnFavoriteIndex);
		} else {
			selectServer(m_activeSource, m_selectedServerIDs.value(m_activeSource));
		}
	}
	m_editorReturnFavoriteIndex = -1;
	m_editorBaseline = currentFavorite();
}

bool ModernConnectController::editorDirty() const {
	if (!m_editorOpen) {
		return false;
	}
	const FavoriteServer current = currentFavorite();
	return current.qsName != m_editorBaseline.qsName || current.qsHostname != m_editorBaseline.qsHostname
		|| current.usPort != m_editorBaseline.usPort || current.qsUsername != m_editorBaseline.qsUsername
		|| current.qsPassword != m_editorBaseline.qsPassword;
}

void ModernConnectController::requestDiscardConfirmation() {
	m_pendingConfirmation.kind = QStringLiteral("discard");
	m_pendingConfirmation.targetID.clear();
	m_pendingConfirmation.title = QObject::tr("Discard server changes?");
	m_pendingConfirmation.message = QObject::tr("Your unsaved server details will be lost.");
}

void ModernConnectController::requestRemoveConfirmation(const QString &favoriteID) {
	m_pendingConfirmation.kind = QStringLiteral("remove");
	m_pendingConfirmation.targetID = favoriteID;
	m_pendingConfirmation.title = QObject::tr("Remove saved server?");
	m_pendingConfirmation.message = QObject::tr("This server will be removed from Favorites.");
}

void ModernConnectController::clearConfirmation() {
	m_pendingConfirmation = PendingConfirmation {};
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

QString ModernConnectController::favoriteTelemetryKey(const QString &host, const unsigned short port) {
	return QStringLiteral("%1:%2").arg(host.trimmed().toLower()).arg(port);
}

QString ModernConnectController::stableServerID(const QString &sourceID, const QString &providedID,
												const QString &host, const unsigned short port, const QString &label) {
	Q_UNUSED(label);
	const QString normalizedSource = sourceID.trimmed().toLower();
	const QString identity = providedID.trimmed().isEmpty()
		? QStringLiteral("%1:%2").arg(host.trimmed().toLower()).arg(port)
		: providedID.trimmed();
	return QStringLiteral("%1:%2").arg(normalizedSource,
		QString::fromLatin1(QUrl::toPercentEncoding(identity, QByteArray(), QByteArray(":@"))));
}

bool ModernConnectController::validSourceID(const QString &sourceID) {
	return sourceID == QLatin1String("favorites") || sourceID == QLatin1String("public")
		|| sourceID == QLatin1String("lan");
}
