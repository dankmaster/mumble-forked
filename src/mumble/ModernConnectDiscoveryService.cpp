// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernConnectDiscoveryService.h"

#include "MumbleConstants.h"
#include "NetworkConfig.h"
#include "Version.h"

#ifdef USE_ZEROCONF
#	include "Zeroconf.h"
#	include <BonjourRecord.h>
#endif

#include <QtCore/QHash>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QXmlStreamReader>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>

namespace {
constexpr qint64 PublicListMaximumBytes = 4 * 1024 * 1024;
constexpr int PublicListTimeoutMs       = 15000;
constexpr qsizetype PublicListMaximumServers = 10000;

QString normalizedServicePrefix(QString prefix) {
	prefix = prefix.trimmed().toLower();
	static const QRegularExpression validPrefix(QStringLiteral("^[a-z]+$"));
	return validPrefix.match(prefix).hasMatch() ? prefix : QString();
}

QString publicListHost(const QString &servicePrefix) {
	const QString prefix = normalizedServicePrefix(servicePrefix);
	return prefix.isEmpty() ? QStringLiteral("publist.mumble.info")
							: QStringLiteral("%1-publist.mumble.info").arg(prefix);
}

QString continentName(QString code) {
	code = code.trimmed().toLower();
	if (code == QLatin1String("af")) return ModernConnectDiscoveryService::tr("Africa");
	if (code == QLatin1String("as")) return ModernConnectDiscoveryService::tr("Asia");
	if (code == QLatin1String("eu")) return ModernConnectDiscoveryService::tr("Europe");
	if (code == QLatin1String("na")) return ModernConnectDiscoveryService::tr("North America");
	if (code == QLatin1String("oc")) return ModernConnectDiscoveryService::tr("Oceania");
	if (code == QLatin1String("sa")) return ModernConnectDiscoveryService::tr("South America");
	return {};
}

#ifdef USE_ZEROCONF
QString lanRecordKey(const BonjourRecord &record) {
	return record.serviceName + QChar(0x1f) + record.registeredType + QChar(0x1f) + record.replyDomain;
}
#endif
} // namespace

struct ModernConnectDiscoveryService::LanState {
#ifdef USE_ZEROCONF
	Zeroconf *backend = nullptr;
	QHash< QString, BonjourRecord > records;
	QHash< QString, ModernConnectController::ServerEntry > resolvedServers;
#endif
	quint64 generation = 0;
	bool active        = false;
};

ModernConnectDiscoveryService::ModernConnectDiscoveryService(QNetworkAccessManager *networkManager,
												   Zeroconf *zeroconf, QObject *parent)
	: QObject(parent), m_networkManager(networkManager), m_publicTimeout(new QTimer(this)),
	  m_lan(std::make_unique< LanState >()) {
	m_publicTimeout->setSingleShot(true);
	connect(m_publicTimeout, &QTimer::timeout, this, [this]() {
		if (!m_publicReply) return;
		m_publicAbortReason = PublicAbortReason::Timeout;
		m_publicReply->abort();
	});

#ifdef USE_ZEROCONF
	m_lan->backend = zeroconf;
	if (m_lan->backend) {
		connect(m_lan->backend, &Zeroconf::recordsChanged, this,
				[this](const QList< BonjourRecord > &records) {
					if (!m_lan->active || !m_lan->backend) return;

					QHash< QString, BonjourRecord > nextRecords;
					for (const BonjourRecord &record : records) {
						const QString key = lanRecordKey(record);
						nextRecords.insert(key, record);
						if (!m_lan->records.contains(key)) {
							m_lan->backend->startResolver(record);
						}
					}

					for (auto it = m_lan->resolvedServers.begin(); it != m_lan->resolvedServers.end();) {
						if (!nextRecords.contains(it.key())) {
							it = m_lan->resolvedServers.erase(it);
						} else {
							++it;
						}
					}
					m_lan->records = std::move(nextRecords);
					publishLanServers();
				});
		connect(m_lan->backend, &Zeroconf::recordResolved, this,
				[this](const BonjourRecord record, const QString hostname, const uint16_t port) {
					if (!m_lan->active) return;
					const QString key = lanRecordKey(record);
					if (!m_lan->records.contains(key) || hostname.trimmed().isEmpty() || port == 0) return;

					ModernConnectController::ServerEntry server;
					server.id     = QStringLiteral("bonjour:%1").arg(key);
					server.label  = record.serviceName.simplified();
					server.host   = hostname.simplified();
					server.port   = port;
					server.region = tr("Local network");
					if (server.label.isEmpty()) server.label = server.host;
					m_lan->resolvedServers.insert(key, server);
					publishLanServers();
				});
		connect(m_lan->backend, &Zeroconf::resolveError, this, [this](const BonjourRecord record) {
			if (!m_lan->active) return;
			m_lan->resolvedServers.remove(lanRecordKey(record));
			publishLanServers();
		});
	}
#else
	Q_UNUSED(zeroconf);
#endif
}

ModernConnectDiscoveryService::~ModernConnectDiscoveryService() {
	cancelAll();
}

void ModernConnectDiscoveryService::setServicePrefix(const QString &servicePrefix) {
	m_servicePrefix = normalizedServicePrefix(servicePrefix);
}

void ModernConnectDiscoveryService::start(const QString &sourceID, const quint64 generation) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (generation == 0) return;
	if (normalizedSource == QLatin1String("public")) {
		startPublic(generation);
	} else if (normalizedSource == QLatin1String("lan")) {
		startLan(generation);
	}
}

void ModernConnectDiscoveryService::cancel(const QString &sourceID) {
	const QString normalizedSource = sourceID.trimmed().toLower();
	if (normalizedSource == QLatin1String("public")) {
		clearPublicRequest(PublicAbortReason::Cancelled);
	} else if (normalizedSource == QLatin1String("lan")) {
		cancelLan();
	}
}

void ModernConnectDiscoveryService::cancelAll() {
	clearPublicRequest(PublicAbortReason::Cancelled);
	cancelLan();
}

ModernConnectDiscoveryService::PublicListParseResult
	ModernConnectDiscoveryService::parsePublicServerList(const QByteArray &data) {
	PublicListParseResult result;
	if (data.size() > PublicListMaximumBytes) {
		result.error = tr("The public server list is larger than the supported limit.");
		return result;
	}

	QXmlStreamReader reader(data);
	QSet< QString > endpoints;
	while (!reader.atEnd()) {
		reader.readNext();
		if (!reader.isStartElement() || reader.name() != QLatin1String("server")) continue;
		if (result.servers.size() >= PublicListMaximumServers) {
			result.error = tr("The public server list contains too many entries.");
			return result;
		}

		const QXmlStreamAttributes attributes = reader.attributes();
		const QUrl advertisedUrl(attributes.value(QLatin1String("url")).toString());
		QString host = attributes.value(QLatin1String("ip")).toString().simplified();
		if (host.isEmpty()) host = advertisedUrl.host().simplified();

		bool portOk = false;
		int port = attributes.value(QLatin1String("port")).toInt(&portOk);
		if ((!portOk || port <= 0) && advertisedUrl.port(-1) > 0) {
			port   = advertisedUrl.port();
			portOk = true;
		}
		if (host.isEmpty() || host.size() > 255 || !portOk || port <= 0 || port > 65535) continue;

		const QString endpoint = QStringLiteral("%1:%2").arg(host.toCaseFolded()).arg(port);
		if (endpoints.contains(endpoint)) continue;
		endpoints.insert(endpoint);

		ModernConnectController::ServerEntry server;
		server.id = QStringLiteral("registry:%1").arg(endpoint);
		server.label = attributes.value(QLatin1String("name")).toString().simplified().left(256);
		server.host = host;
		server.port = static_cast< unsigned short >(port);
		server.country = attributes.value(QLatin1String("country")).toString().simplified().left(128);
		server.region = continentName(attributes.value(QLatin1String("continent_code")).toString());
		if (server.label.isEmpty()) server.label = host;
		result.servers.push_back(server);
	}

	if (reader.hasError()) {
		result.servers.clear();
		result.error = tr("The public server registry returned malformed data.");
		return result;
	}

	std::sort(result.servers.begin(), result.servers.end(), [](const auto &left, const auto &right) {
		const int labelOrder = QString::localeAwareCompare(left.label, right.label);
		if (labelOrder != 0) return labelOrder < 0;
		if (left.host != right.host) return left.host < right.host;
		return left.port < right.port;
	});
	result.ok = true;
	return result;
}

void ModernConnectDiscoveryService::startPublic(const quint64 generation) {
	clearPublicRequest(PublicAbortReason::Cancelled);
	m_publicGeneration  = generation;
	m_publicAbortReason = PublicAbortReason::None;
	if (!m_networkManager) {
		emit sourceUnavailable(QStringLiteral("public"), generation,
						   tr("Public server discovery is not available in this build."));
		return;
	}
	issuePublicRequest(!m_servicePrefix.isEmpty());
}

void ModernConnectDiscoveryService::issuePublicRequest(const bool useServicePrefix) {
	if (!m_networkManager || m_publicGeneration == 0) return;

	QUrl url;
	url.setScheme(QStringLiteral("https"));
	url.setHost(publicListHost(useServicePrefix ? m_servicePrefix : QString()));
	url.setPath(QStringLiteral("/v1/list"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("version"), Version::getRelease());
	url.setQuery(query);

	QNetworkRequest request(url);
	Network::prepareRequest(request);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	m_publicAbortReason = PublicAbortReason::None;
	QNetworkReply *reply = m_networkManager->get(request);
	m_publicReply = reply;
	const quint64 generation = m_publicGeneration;
	connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](const qint64 received, qint64) {
		if (m_publicReply != reply || received <= PublicListMaximumBytes) return;
		m_publicAbortReason = PublicAbortReason::PayloadTooLarge;
		reply->abort();
	});
	connect(reply, &QNetworkReply::finished, this,
			[this, reply, generation, useServicePrefix]() {
				finishPublicRequest(reply, generation, useServicePrefix);
			});
	m_publicTimeout->start(PublicListTimeoutMs);
}

void ModernConnectDiscoveryService::finishPublicRequest(QNetworkReply *reply, const quint64 generation,
													 const bool usedServicePrefix) {
	if (m_publicReply != reply || generation == 0 || generation != m_publicGeneration) {
		reply->deleteLater();
		return;
	}
	m_publicTimeout->stop();
	m_publicReply.clear();

	const PublicAbortReason abortReason = m_publicAbortReason;
	m_publicAbortReason = PublicAbortReason::None;
	const bool success = abortReason == PublicAbortReason::None && reply->error() == QNetworkReply::NoError;
	if (!success && usedServicePrefix && abortReason == PublicAbortReason::None) {
		reply->deleteLater();
		issuePublicRequest(false);
		return;
	}

	if (abortReason == PublicAbortReason::Cancelled) {
		reply->deleteLater();
		return;
	}
	if (abortReason == PublicAbortReason::Timeout) {
		emit sourceError(QStringLiteral("public"), generation,
						 tr("The public server list request timed out."), true);
		reply->deleteLater();
		return;
	}
	if (abortReason == PublicAbortReason::PayloadTooLarge) {
		emit sourceError(QStringLiteral("public"), generation,
						 tr("The public server list is larger than the supported limit."), true);
		reply->deleteLater();
		return;
	}
	if (!success) {
		QString detail = reply->errorString().simplified();
		if (detail.isEmpty()) detail = tr("Unknown network error");
		emit sourceError(QStringLiteral("public"), generation,
						 tr("Could not load the public server list: %1").arg(detail), true);
		reply->deleteLater();
		return;
	}

	const QString suggestedPrefix = normalizedServicePrefix(
		QString::fromUtf8(reply->rawHeader(QByteArrayLiteral("Use-Service-Prefix"))));
	if (!suggestedPrefix.isEmpty() && suggestedPrefix != m_servicePrefix) {
		m_servicePrefix = suggestedPrefix;
		emit servicePrefixSuggested(suggestedPrefix);
	}

	const PublicListParseResult parsed = parsePublicServerList(reply->readAll());
	reply->deleteLater();
	if (!parsed.ok) {
		emit sourceError(QStringLiteral("public"), generation, parsed.error, true);
		return;
	}
	emit serversReady(QStringLiteral("public"), generation, parsed.servers);
}

void ModernConnectDiscoveryService::clearPublicRequest(const PublicAbortReason reason) {
	m_publicTimeout->stop();
	m_publicGeneration  = 0;
	m_publicAbortReason = reason;
	if (!m_publicReply) return;
	QNetworkReply *reply = m_publicReply.data();
	m_publicReply.clear();
	disconnect(reply, nullptr, this, nullptr);
	reply->abort();
	reply->deleteLater();
}

void ModernConnectDiscoveryService::startLan(const quint64 generation) {
	cancelLan();
	m_lan->generation = generation;
#ifdef USE_ZEROCONF
	if (!m_lan->backend || !m_lan->backend->isOk()) {
		emit sourceUnavailable(QStringLiteral("lan"), generation,
						   tr("Local network discovery is not available on this system."));
		return;
	}

	m_lan->active = true;
	if (!m_lan->backend->startBrowser(QStringLiteral("_mumble._tcp"))) {
		m_lan->active = false;
		emit sourceUnavailable(QStringLiteral("lan"), generation,
						   tr("Local network discovery could not be started."));
		return;
	}

	const QList< BonjourRecord > currentRecords = m_lan->backend->currentRecords();
	QHash< QString, BonjourRecord > initialRecords;
	for (const BonjourRecord &record : currentRecords) {
		const QString key = lanRecordKey(record);
		initialRecords.insert(key, record);
		if (!m_lan->records.contains(key)) m_lan->backend->startResolver(record);
	}
	m_lan->records = std::move(initialRecords);
	publishLanServers();
#else
	emit sourceUnavailable(QStringLiteral("lan"), generation,
						   tr("This build does not include local network discovery."));
#endif
}

void ModernConnectDiscoveryService::cancelLan() {
#ifdef USE_ZEROCONF
	const bool wasActive = m_lan->active;
	m_lan->active        = false;
	if (wasActive && m_lan->backend) {
		m_lan->backend->stopBrowser();
		m_lan->backend->cleanupResolvers();
	}
	m_lan->records.clear();
	m_lan->resolvedServers.clear();
#else
	m_lan->active = false;
#endif
	m_lan->generation = 0;
}

void ModernConnectDiscoveryService::publishLanServers() {
#ifdef USE_ZEROCONF
	if (!m_lan->active || m_lan->generation == 0) return;
	QList< ModernConnectController::ServerEntry > servers = m_lan->resolvedServers.values();
	std::sort(servers.begin(), servers.end(), [](const auto &left, const auto &right) {
		const int labelOrder = QString::localeAwareCompare(left.label, right.label);
		if (labelOrder != 0) return labelOrder < 0;
		if (left.host != right.host) return left.host < right.host;
		return left.port < right.port;
	});
	emit serversReady(QStringLiteral("lan"), m_lan->generation, servers);
#endif
}
