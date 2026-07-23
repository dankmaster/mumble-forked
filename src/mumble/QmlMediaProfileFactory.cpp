// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlMediaProfileFactory.h"

#include "ChatPerfTrace.h"
#include "QmlClientModels.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFutureWatcher>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QSet>
#include <QtNetwork/QHostAddress>
#include <QtWebEngineCore/QWebEngineUrlRequestInfo>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>

#ifdef Q_OS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

namespace {
const QByteArray MediaClientReferer("https://www.mumble.info/");
const QUrl AdaptiveMediaDocument(QStringLiteral("qrc:/media-player/AdaptiveMediaPlayer.html"));
const QUrl AdaptiveMediaBootstrap(QStringLiteral("qrc:/media-player/AdaptiveMediaPlayer.js"));
const QUrl AdaptiveMediaRuntime(QStringLiteral("qrc:/media-player/shaka-player.compiled.js"));
constexpr int VideoMemoryCacheBytes = 32 * 1024 * 1024;
constexpr int AudioMemoryCacheBytes = 8 * 1024 * 1024;

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
		  { QStringLiteral("facebook.com"), QStringLiteral("facebook.net"), QStringLiteral("fbcdn.net"),
			QStringLiteral("fbsbx.com") } },
		{ QStringLiteral("tiktok"),
		  { QStringLiteral("tiktok.com"), QStringLiteral("tiktokcdn.com"),
			QStringLiteral("tiktokcdn-us.com"), QStringLiteral("tiktokcdn-eu.com"),
			QStringLiteral("tiktokw.eu"), QStringLiteral("ttwstatic.com"),
			QStringLiteral("tiktokv.eu"), QStringLiteral("byteoversea.com") } },
		{ QStringLiteral("instagram"),
		  { QStringLiteral("instagram.com"), QStringLiteral("cdninstagram.com"),
			QStringLiteral("facebook.net"), QStringLiteral("fbcdn.net"), QStringLiteral("fbsbx.com") } },
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

bool isAdaptivePlayerDocumentUrl(const QUrl &url) {
	return requestIdentity(url) == AdaptiveMediaDocument;
}

bool isAdaptivePlayerResourceUrl(const QUrl &url) {
	const QUrl candidate = requestIdentity(url);
	return candidate == AdaptiveMediaDocument || candidate == AdaptiveMediaBootstrap
		|| candidate == AdaptiveMediaRuntime;
}

QUrl adaptivePlayerDocumentUrl(const QUrl &manifestUrl) {
	QUrl documentUrl = AdaptiveMediaDocument;
	documentUrl.setFragment(QString::fromLatin1(
		manifestUrl.toEncoded(QUrl::FullyEncoded).toBase64(QByteArray::Base64UrlEncoding
			| QByteArray::OmitTrailingEquals)));
	return documentUrl;
}

bool isAdaptiveManifestMime(const QString &mime) {
	const QString normalized = mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
	return normalized == QLatin1String("application/vnd.apple.mpegurl")
		|| normalized == QLatin1String("application/dash+xml");
}

bool isSameHttpsOrigin(const QUrl &left, const QUrl &right) {
	return left.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
		&& right.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
		&& left.host().compare(right.host(), Qt::CaseInsensitive) == 0
		&& left.port(443) == right.port(443);
}

bool isManifestFirstPartyAllowed(const QUrl &manifestUrl, const QUrl &firstPartyUrl) {
	// Media-pipeline requests can be reported without a first-party URL, or with
	// about:blank while Chromium constructs its internal media document. The
	// session-bound interceptor still restricts those requests to the manifest's
	// exact public HTTPS origin.
	return firstPartyUrl.isEmpty() || firstPartyUrl == QUrl(QStringLiteral("about:blank"))
		|| isAdaptivePlayerDocumentUrl(firstPartyUrl) || isSameHttpsOrigin(manifestUrl, firstPartyUrl);
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

bool isSafePublicHttpsUrl(const QUrl &url) {
	return url.isValid() && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
		&& !url.host().isEmpty() && url.userInfo().isEmpty()
		&& (url.port(-1) == -1 || url.port(-1) == 443) && !isLocalNetworkHost(url.host());
}
}

class MediaRequestInterceptor final : public QWebEngineUrlRequestInterceptor {
public:
	explicit MediaRequestInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}

	void setPolicy(const QString &provider, const QUrl &primaryUrl, const QUrl &audioUrl,
				   const QString &mediaMime) {
		QMutexLocker locker(&m_mutex);
		m_provider = provider.trimmed().toLower();
		m_primaryUrl = primaryUrl;
		m_audioUrl = audioUrl;
		m_mediaMime = mediaMime;
	}

	void interceptRequest(QWebEngineUrlRequestInfo &info) override {
		QString provider;
		QString mediaMime;
		QUrl primaryUrl;
		QUrl audioUrl;
		{
			QMutexLocker locker(&m_mutex);
			provider = m_provider;
			mediaMime = m_mediaMime;
			primaryUrl = m_primaryUrl;
			audioUrl = m_audioUrl;
		}
		const QByteArray method = info.requestMethod().toUpper();
		const bool methodAllowed = provider == QLatin1String("direct")
			? method == "GET" || method == "HEAD"
			: method == "GET" || method == "HEAD" || method == "POST" || method == "OPTIONS";
		const bool resourceAllowed = QmlMediaProfileFactory::isResourceRequestAllowed(
			provider, primaryUrl, audioUrl, info.requestUrl(), info.firstPartyUrl(), mediaMime);
		const bool blocked = info.isDownload() || info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeWebSocket
			|| !methodAllowed || !resourceAllowed;
		// Desktop WebViews do not synthesize a Referer for a top-level embed
		// navigation. YouTube uses it as the API-client identity (error 153 when
		// absent), while Twitch checks it against its parent parameter. Attach the
		// public application origin only to allowlisted main-frame navigations;
		// subresources retain the provider page's normal Referer.
		if (!blocked && (provider == QLatin1String("youtube") || provider == QLatin1String("twitch"))
			&& info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame) {
			info.setHttpHeader(QByteArrayLiteral("Referer"), MediaClientReferer);
		}
		info.block(blocked);
	}

private:
	QMutex m_mutex;
	QString m_provider;
	QString m_mediaMime;
	QUrl m_primaryUrl;
	QUrl m_audioUrl;
};

QmlMediaProfileFactory::QmlMediaProfileFactory(MediaSessionBackend *session, QObject *parent)
	: QObject(parent), m_session(session) {
#if !defined(Q_OS_WIN) || (!defined(MUMBLE_DELAYLOAD_WEBENGINE_QUICK) && !defined(MUMBLE_TEST_DELAYED_WEBENGINE))
	m_runtimeReady = true;
#endif
	if (m_session) {
		connect(m_session, &MediaSessionBackend::sourceChanged, this, [this] {
			updatePolicies();
			emit documentUrlChanged();
		});
		connect(m_session, &MediaSessionBackend::stateChanged, this, [this] {
			if (m_session && m_session->active()) {
				prepareRuntime();
				updatePolicies();
			} else {
				releaseProfiles();
			}
		});
	}
}

QmlMediaProfileFactory::~QmlMediaProfileFactory() {
	if (m_videoProfile) delete m_videoProfile.data();
	if (m_audioProfile) delete m_audioProfile.data();
}

bool QmlMediaProfileFactory::isResourceRequestAllowed(const QString &provider, const QUrl &primaryUrl,
												const QUrl &audioUrl, const QUrl &requestUrl,
												const QUrl &firstPartyUrl, const QString &mediaMime) {
	if (!requestUrl.isValid()) return false;
	const QString normalizedProvider = provider.trimmed().toLower();
	const QString scheme = requestUrl.scheme().toLower();
	if (scheme == QLatin1String("about")) return requestUrl == QUrl(QStringLiteral("about:blank"));
	if (scheme == QLatin1String("qrc")) {
		return normalizedProvider == QLatin1String("direct") && isAdaptiveManifestMime(mediaMime)
			&& isSafePublicHttpsUrl(primaryUrl) && isAdaptivePlayerResourceUrl(requestUrl)
			&& (firstPartyUrl.isEmpty() || firstPartyUrl == QUrl(QStringLiteral("about:blank"))
				|| isAdaptivePlayerDocumentUrl(firstPartyUrl));
	}
	if (scheme == QLatin1String("data")) {
		if (normalizedProvider == QLatin1String("direct")) {
			return requestUrl == primaryUrl || (!audioUrl.isEmpty() && requestUrl == audioUrl);
		}
		return providerAllowsHost(normalizedProvider, firstPartyUrl.host());
	}
	if (scheme == QLatin1String("blob")) {
		return normalizedProvider == QLatin1String("direct")
			? isAdaptiveManifestMime(mediaMime) && isAdaptivePlayerDocumentUrl(firstPartyUrl)
			: providerAllowsHost(normalizedProvider, firstPartyUrl.host());
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
		if (candidate == requestIdentity(primaryUrl)
			|| (!audioUrl.isEmpty() && candidate == requestIdentity(audioUrl))) {
			return true;
		}

		// Adaptive manifests are the only direct-media sources that legitimately
		// fan out after the initial navigation. Permit their child playlists,
		// initialization objects, keys and media segments, but only on the exact
		// public HTTPS origin selected by the active session. This keeps the
		// isolated renderer useful for Steam HLS/DASH without turning it into an
		// arbitrary-origin fetch surface.
		return isSafePublicHttpsUrl(primaryUrl) && isAdaptiveManifestMime(mediaMime)
			&& isSameHttpsOrigin(primaryUrl, requestUrl)
			&& isManifestFirstPartyAllowed(primaryUrl, firstPartyUrl);
	}
	return providerAllowsHost(normalizedProvider, requestUrl.host());
}

QQuickWebEngineProfile *QmlMediaProfileFactory::createProfile(const bool audio) {
	if (!m_session || !m_session->active()) return nullptr;
	QElapsedTimer profileTimer;
	if (mumble::chatperf::enabled()) profileTimer.start();
	auto *profile = new QQuickWebEngineProfile(this);
	profile->setOffTheRecord(true);
	// Keep warm navigation and short provider reloads fast without retaining
	// media across sessions. The active session owns this bounded memory-only
	// cache; releaseProfiles() destroys it as soon as playback closes.
	profile->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
	profile->setHttpCacheMaximumSize(audio ? AudioMemoryCacheBytes : VideoMemoryCacheBytes);
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
	if (mumble::chatperf::enabled()) {
		mumble::chatperf::recordNote("qml.media.profile.create",
			QStringLiteral("audio=%1 duration_ms=%2").arg(audio).arg(profileTimer.elapsed()));
	}
	return profile;
}

QObject *QmlMediaProfileFactory::videoProfile() {
	if (!m_runtimeReady) {
		prepareRuntime();
		return nullptr;
	}
	return m_videoProfile ? m_videoProfile.data() : createProfile(false);
}

QObject *QmlMediaProfileFactory::audioProfile() {
	if (!m_runtimeReady) {
		prepareRuntime();
		return nullptr;
	}
	return m_audioProfile ? m_audioProfile.data() : createProfile(true);
}

bool QmlMediaProfileFactory::runtimeReady() const { return m_runtimeReady; }

bool QmlMediaProfileFactory::runtimePreparing() const { return m_runtimePreparing; }

QString QmlMediaProfileFactory::runtimeError() const { return m_runtimeError; }

QUrl QmlMediaProfileFactory::videoDocumentUrl() const {
	if (!m_session || !m_session->active()) return {};
	if (m_session->provider() == QLatin1String("direct")
		&& isAdaptiveManifestMime(m_session->mediaMime())) {
		return adaptivePlayerDocumentUrl(m_session->url());
	}
	return m_session->url();
}

bool QmlMediaProfileFactory::isNavigationRequestAllowed(const QUrl &requestUrl,
												 const QUrl &firstPartyUrl) const {
	if (!m_session || !m_session->active()) return false;
	if (m_session->provider() == QLatin1String("direct")
		&& isAdaptiveManifestMime(m_session->mediaMime())) {
		const QUrl documentUrl = videoDocumentUrl();
		if (requestUrl.adjusted(QUrl::NormalizePathSegments)
			!= documentUrl.adjusted(QUrl::NormalizePathSegments)) return false;
		return isResourceRequestAllowed(m_session->provider(), m_session->url(),
			m_session->audioUrl(), requestUrl, firstPartyUrl, m_session->mediaMime());
	}
	if (!m_session->isNavigationAllowed(requestUrl)) return false;
	return isResourceRequestAllowed(m_session->provider(), m_session->url(), m_session->audioUrl(),
		requestUrl, firstPartyUrl, m_session->mediaMime());
}

void QmlMediaProfileFactory::retryRuntime() {
	if (m_runtimeReady || m_runtimePreparing || !m_session || !m_session->active()) return;
	if (!m_runtimeError.isEmpty()) {
		m_runtimeError.clear();
		emit runtimeStateChanged();
	}
	prepareRuntime();
}

void QmlMediaProfileFactory::prepareRuntime() {
	if (m_runtimeReady || m_runtimePreparing || !m_runtimeError.isEmpty()
		|| !m_session || !m_session->active()) {
		return;
	}

#if defined(Q_OS_WIN) && (defined(MUMBLE_DELAYLOAD_WEBENGINE_QUICK) || defined(MUMBLE_TEST_DELAYED_WEBENGINE))
	m_runtimePreparing = true;
	mumble::chatperf::recordNote("qml.media.runtime", QStringLiteral("worker-load-requested"));
	emit runtimeStateChanged();
	const QString applicationDirectory = QCoreApplication::applicationDirPath();
	auto *watcher = new QFutureWatcher< QString >(this);
	connect(watcher, &QFutureWatcher< QString >::finished, this, [this, watcher] {
		const QString error = watcher->result();
		watcher->deleteLater();
		m_runtimePreparing = false;
		m_runtimeReady = error.isEmpty();
		m_runtimeError = error;
		mumble::chatperf::recordNote("qml.media.runtime",
			error.isEmpty() ? QStringLiteral("worker-load-ready")
							: QStringLiteral("worker-load-error %1").arg(error));
		emit runtimeStateChanged();
		// A profile getter can have been evaluated while the delayed Windows
		// runtime was still loading. Wake those bindings once creating a concrete
		// QQuickWebEngineProfile is safe; otherwise their first nullptr result is
		// retained for the lifetime of the detached player.
		if (m_runtimeReady) emit profilesChanged();
		if (!error.isEmpty() && m_session && m_session->active()) {
			m_session->reportTypedError(QStringLiteral("renderer-runtime-unavailable"), error);
		}
	});
	watcher->setFuture(QtConcurrent::run([applicationDirectory]() -> QString {
		const auto loadRuntime = [&applicationDirectory](const QString &pattern) -> QString {
			const QFileInfoList matches = QDir(applicationDirectory).entryInfoList(
				{ pattern }, QDir::Files | QDir::Readable, QDir::Name);
			if (matches.isEmpty()) {
				return QStringLiteral("The packaged media runtime is missing %1.").arg(pattern);
			}
			const std::wstring nativePath = QDir::toNativeSeparators(matches.constFirst().absoluteFilePath()).toStdWString();
			const HMODULE module = LoadLibraryExW(nativePath.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (!module) {
				return QStringLiteral("The packaged media runtime could not load %1 (Windows error %2).")
					.arg(matches.constFirst().fileName()).arg(GetLastError());
			}
			return {};
		};
		QString error = loadRuntime(QStringLiteral("Qt6WebEngineCore*.dll"));
		if (error.isEmpty()) error = loadRuntime(QStringLiteral("Qt6WebEngineQuick*.dll"));
		return error;
	}));
#else
	m_runtimeReady = true;
	emit runtimeStateChanged();
	emit profilesChanged();
#endif
}

void QmlMediaProfileFactory::updatePolicies() {
	if (!m_session || !m_session->active()) return;
	if (m_videoInterceptor) {
		m_videoInterceptor->setPolicy(m_session->provider(), m_session->url(), m_session->audioUrl(),
			m_session->mediaMime());
	}
	if (m_audioInterceptor) {
		m_audioInterceptor->setPolicy(QStringLiteral("direct"), m_session->audioUrl(), {},
			m_session->audioMime());
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
