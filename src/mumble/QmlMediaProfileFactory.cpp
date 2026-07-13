// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlMediaProfileFactory.h"

#include "QmlClientModels.h"

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QSet>
#include <QtNetwork/QHostAddress>
#include <QtWebEngineCore/QWebEngineUrlRequestInfo>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>

namespace {
const QHash< QString, QSet< QString > > &providerResourceDomains() {
	static const QHash< QString, QSet< QString > > domains {
		{ QStringLiteral("youtube"),
		  { QStringLiteral("youtube.com"), QStringLiteral("youtube-nocookie.com"),
			QStringLiteral("ytimg.com"), QStringLiteral("googlevideo.com"), QStringLiteral("ggpht.com"),
			QStringLiteral("google.com"), QStringLiteral("googleapis.com"), QStringLiteral("gstatic.com") } },
		{ QStringLiteral("twitch"),
		  { QStringLiteral("twitch.tv"), QStringLiteral("twitchcdn.net"), QStringLiteral("jtvnw.net"),
			QStringLiteral("ttvnw.net") } },
		{ QStringLiteral("streamable"), { QStringLiteral("streamable.com") } },
		{ QStringLiteral("vimeo"),
		  { QStringLiteral("vimeo.com"), QStringLiteral("vimeocdn.com"),
			QStringLiteral("vod-progressive.akamaized.net") } },
		{ QStringLiteral("dailymotion"),
		  { QStringLiteral("dailymotion.com"), QStringLiteral("dmcdn.net") } },
		{ QStringLiteral("spotify"),
		  { QStringLiteral("spotify.com"), QStringLiteral("scdn.co"), QStringLiteral("spotifycdn.com") } },
		{ QStringLiteral("facebook"),
		  { QStringLiteral("facebook.com"), QStringLiteral("fbcdn.net") } },
		{ QStringLiteral("tiktok"),
		  { QStringLiteral("tiktok.com"), QStringLiteral("tiktokcdn.com"),
			QStringLiteral("tiktokcdn-us.com"), QStringLiteral("byteoversea.com") } },
		{ QStringLiteral("instagram"),
		  { QStringLiteral("instagram.com"), QStringLiteral("cdninstagram.com"),
			QStringLiteral("fbcdn.net") } },
		{ QStringLiteral("soundcloud"),
		  { QStringLiteral("soundcloud.com"), QStringLiteral("sndcdn.com") } }
	};
	return domains;
}

bool hostMatchesDomain(const QString &host, const QString &domain) {
	return host == domain || host.endsWith(QStringLiteral(".") + domain);
}

bool providerAllowsHost(const QString &provider, const QString &host) {
	const auto domains = providerResourceDomains().constFind(provider.trimmed().toLower());
	if (domains == providerResourceDomains().cend()) return false;
	const QString normalizedHost = host.trimmed().toLower();
	for (const QString &domain : *domains) {
		if (hostMatchesDomain(normalizedHost, domain)) return true;
	}
	return false;
}

QUrl requestIdentity(const QUrl &url) {
	return url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
}

bool isLocalNetworkHost(QString host) {
	host = host.trimmed().toLower();
	while (host.endsWith(QLatin1Char('.'))) host.chop(1);
	if (host.isEmpty()) return true;

	// Chromium accepts several legacy IPv4 spellings (for example 127.1, octal
	// or hexadecimal labels). Reject those ambiguous forms before Qt/Chromium
	// can disagree about which address they denote.
	bool legacyAddressLike = true;
	bool canonicalDottedDecimal = true;
	const QStringList labels = host.split(QLatin1Char('.'));
	canonicalDottedDecimal = labels.size() == 4;
	for (const QString &label : labels) {
		QString digits = label;
		int base = 10;
		if (digits.startsWith(QLatin1String("0x"))) {
			digits.remove(0, 2);
			base = 16;
			canonicalDottedDecimal = false;
		}
		bool valid = !digits.isEmpty();
		for (const QChar character : digits) {
			valid = valid && (base == 10 ? character.isDigit()
										  : ((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
											 || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))));
		}
		if (!valid) {
			legacyAddressLike = false;
			canonicalDottedDecimal = false;
			break;
		}
		bool decimalOk = false;
		const uint decimalValue = digits.toUInt(&decimalOk, 10);
		canonicalDottedDecimal = canonicalDottedDecimal && base == 10 && decimalOk && decimalValue <= 255
			&& (digits == QLatin1String("0") || !digits.startsWith(QLatin1Char('0')));
	}
	if (legacyAddressLike && !canonicalDottedDecimal) return true;

	QHostAddress address;
	if (address.setAddress(host)) {
		return !address.isGlobal() || address.isLoopback() || address.isLinkLocal() || address.isSiteLocal()
			|| address.isUniqueLocalUnicast() || address.isPrivateUse() || address.isMulticast()
			|| address.isBroadcast();
	}
	if (legacyAddressLike) return true;

	if (!host.contains(QLatin1Char('.')) || host == QLatin1String("localhost")
		|| host == QLatin1String("localdomain") || host.endsWith(QLatin1String(".localhost"))
		|| host.endsWith(QLatin1String(".localdomain")) || host.endsWith(QLatin1String(".local"))) {
		return true;
	}
	return false;
}
}

class MediaRequestInterceptor final : public QWebEngineUrlRequestInterceptor {
public:
	explicit MediaRequestInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}

	void setPolicy(const QString &provider, const QUrl &primaryUrl, const QUrl &audioUrl) {
		QMutexLocker locker(&m_mutex);
		m_provider = provider.trimmed().toLower();
		m_primaryUrl = primaryUrl;
		m_audioUrl = audioUrl;
	}

	void interceptRequest(QWebEngineUrlRequestInfo &info) override {
		QString provider;
		QUrl primaryUrl;
		QUrl audioUrl;
		{
			QMutexLocker locker(&m_mutex);
			provider = m_provider;
			primaryUrl = m_primaryUrl;
			audioUrl = m_audioUrl;
		}
		const QByteArray method = info.requestMethod().toUpper();
		const bool methodAllowed = method == "GET" || method == "HEAD" || method == "POST"
			|| method == "OPTIONS";
		const bool blocked = info.isDownload() || info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeWebSocket
			|| !methodAllowed || !QmlMediaProfileFactory::isResourceRequestAllowed(
				provider, primaryUrl, audioUrl, info.requestUrl(), info.firstPartyUrl());
		info.block(blocked);
	}

private:
	QMutex m_mutex;
	QString m_provider;
	QUrl m_primaryUrl;
	QUrl m_audioUrl;
};

QmlMediaProfileFactory::QmlMediaProfileFactory(MediaSessionBackend *session, QObject *parent)
	: QObject(parent), m_session(session) {
	if (m_session) {
		connect(m_session, &MediaSessionBackend::stateChanged, this, [this] {
			if (m_session && m_session->active())
				updatePolicies();
			else
				releaseProfiles();
		});
	}
}

QmlMediaProfileFactory::~QmlMediaProfileFactory() {
	if (m_videoProfile) delete m_videoProfile.data();
	if (m_audioProfile) delete m_audioProfile.data();
}

bool QmlMediaProfileFactory::isResourceRequestAllowed(const QString &provider, const QUrl &primaryUrl,
												const QUrl &audioUrl, const QUrl &requestUrl,
												const QUrl &firstPartyUrl) {
	if (!requestUrl.isValid()) return false;
	const QString normalizedProvider = provider.trimmed().toLower();
	const QString scheme = requestUrl.scheme().toLower();
	if (scheme == QLatin1String("about")) return requestUrl == QUrl(QStringLiteral("about:blank"));
	if (scheme == QLatin1String("data")) {
		if (normalizedProvider == QLatin1String("direct")) {
			return requestUrl == primaryUrl || (!audioUrl.isEmpty() && requestUrl == audioUrl);
		}
		return providerAllowsHost(normalizedProvider, firstPartyUrl.host());
	}
	if (scheme == QLatin1String("blob")) {
		return normalizedProvider != QLatin1String("direct")
			&& providerAllowsHost(normalizedProvider, firstPartyUrl.host());
	}
	if (scheme != QLatin1String("https") || requestUrl.host().isEmpty() || !requestUrl.userInfo().isEmpty()
		|| (requestUrl.port(-1) != -1 && requestUrl.port(-1) != 443)) {
		return false;
	}
	// Sender-controlled direct-media URLs must not turn the isolated player into
	// a browser-assisted request to loopback, link-local, private or single-label
	// intranet hosts. Provider resources are public allowlisted domains and pass
	// the same fail-closed check.
	if (isLocalNetworkHost(requestUrl.host())) return false;
	if (normalizedProvider == QLatin1String("direct")) {
		const QUrl candidate = requestIdentity(requestUrl);
		return candidate == requestIdentity(primaryUrl)
			|| (!audioUrl.isEmpty() && candidate == requestIdentity(audioUrl));
	}
	return providerAllowsHost(normalizedProvider, requestUrl.host());
}

QQuickWebEngineProfile *QmlMediaProfileFactory::createProfile(const bool audio) {
	if (!m_session || !m_session->active()) return nullptr;
	auto *profile = new QQuickWebEngineProfile(this);
	profile->setOffTheRecord(true);
	profile->setHttpCacheType(QQuickWebEngineProfile::NoCache);
	profile->setHttpCacheMaximumSize(0);
	profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
	profile->setPersistentPermissionsPolicy(QQuickWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);
	profile->setSpellCheckEnabled(false);
	profile->setPushServiceEnabled(false);
	auto *interceptor = new MediaRequestInterceptor(profile);
	profile->setUrlRequestInterceptor(interceptor);
	if (audio) {
		m_audioProfile = profile;
		m_audioInterceptor = interceptor;
	} else {
		m_videoProfile = profile;
		m_videoInterceptor = interceptor;
	}
	updatePolicies();
	return profile;
}

QQuickWebEngineProfile *QmlMediaProfileFactory::videoProfile() {
	return m_videoProfile ? m_videoProfile.data() : createProfile(false);
}

QQuickWebEngineProfile *QmlMediaProfileFactory::audioProfile() {
	return m_audioProfile ? m_audioProfile.data() : createProfile(true);
}

void QmlMediaProfileFactory::updatePolicies() {
	if (!m_session || !m_session->active()) return;
	if (m_videoInterceptor) {
		m_videoInterceptor->setPolicy(m_session->provider(), m_session->url(), m_session->audioUrl());
	}
	if (m_audioInterceptor) {
		m_audioInterceptor->setPolicy(QStringLiteral("direct"), m_session->audioUrl(), {});
	}
}

void QmlMediaProfileFactory::releaseProfiles() {
	QQuickWebEngineProfile *video = m_videoProfile.data();
	QQuickWebEngineProfile *audio = m_audioProfile.data();
	if (!video && !audio) return;
	m_videoProfile = nullptr;
	m_audioProfile = nullptr;
	m_videoInterceptor = nullptr;
	m_audioInterceptor = nullptr;
	if (video) video->deleteLater();
	if (audio) audio->deleteLater();
	emit profilesChanged();
}
