// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "Global.h"
#include "Log.h"
#include "ModernContextMenuHost.h"
#include "ModernShellBridge.h"
#include "ModernShellPage.h"

#include <QtCore/QEvent>
#include <QtCore/QDateTime>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>
#include <QtCore/QPoint>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtCore/QVariantList>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QPixmap>
#include <QtGui/QImageReader>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineCore/QWebEngineUrlRequestInfo>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>
#include <QtWebEngineWidgets/QWebEngineView>

namespace {
	const QByteArray PREVIEW_MEDIA_USER_AGENT =
		QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
						  "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36");
	const QByteArray PREVIEW_MEDIA_ACCEPT_LANGUAGE = QByteArrayLiteral("en-US,en;q=0.9");
	const QByteArray PREVIEW_MEDIA_ACCEPT =
		QByteArrayLiteral("video/webm,video/mp4,video/*;q=0.9,audio/*;q=0.8,image/avif,image/webp,image/*,*/*;q=0.5");
	const QByteArray YOUTUBE_EMBED_REFERER = QByteArrayLiteral("https://www.mumble.info/");

	void appendModernShellConnectTrace(const QString &message);

	void addModernUiTweaksQuery(QUrl &url, const QVariantMap &uiTweaks) {
		if (uiTweaks.isEmpty()) {
			return;
		}

		QUrlQuery query(url);
		const QStringList keys { QStringLiteral("theme"), QStringLiteral("accent"), QStringLiteral("density"),
								 QStringLiteral("userIcons"), QStringLiteral("classicUserIcons"),
								 QStringLiteral("railSide") };
		for (const QString &key : keys) {
			const QVariant value = uiTweaks.value(key);
			if (!value.isValid() || value.isNull()) {
				continue;
			}
			const QString text = value.toString().trimmed();
			if (!text.isEmpty()) {
				query.addQueryItem(key, text);
			}
		}
		url.setQuery(query);
	}

	QUrl modernShellUrl(const quint16 localShellPort) {
		QUrl url;
		if (localShellPort > 0) {
			url.setScheme(QStringLiteral("http"));
			url.setHost(QStringLiteral("127.0.0.1"));
			url.setPort(localShellPort);
			url.setPath(QStringLiteral("/modern-shell/index.html"));
		} else {
			url = QUrl(QStringLiteral("qrc:/modern-shell/index.html"));
		}
		QVariantMap uiTweaks;
		uiTweaks.insert(QStringLiteral("theme"), Global::get().s.qsModernShellTheme);
		uiTweaks.insert(QStringLiteral("accent"), Global::get().s.qsModernShellAccent);
		uiTweaks.insert(QStringLiteral("density"), Global::get().s.qsModernShellDensity);
		uiTweaks.insert(QStringLiteral("userIcons"),
						Global::get().s.bModernShellClassicUserIcons ? QStringLiteral("classic")
																	 : QStringLiteral("avatars"));
		uiTweaks.insert(QStringLiteral("classicUserIcons"), Global::get().s.bModernShellClassicUserIcons);
		uiTweaks.insert(QStringLiteral("railSide"), Global::get().s.qsModernShellRailSide);
		addModernUiTweaksQuery(url, uiTweaks);
		return url;
	}

	QByteArray modernShellMimeTypeForResource(const QString &resourcePath) {
		const QString suffix = QFileInfo(resourcePath).suffix().toLower();
		if (suffix == QLatin1String("html")) {
			return QByteArrayLiteral("text/html; charset=utf-8");
		}
		if (suffix == QLatin1String("js")) {
			return QByteArrayLiteral("application/javascript; charset=utf-8");
		}
		if (suffix == QLatin1String("css")) {
			return QByteArrayLiteral("text/css; charset=utf-8");
		}
		if (suffix == QLatin1String("svg")) {
			return QByteArrayLiteral("image/svg+xml");
		}
		if (suffix == QLatin1String("png")) {
			return QByteArrayLiteral("image/png");
		}
		if (suffix == QLatin1String("json")) {
			return QByteArrayLiteral("application/json; charset=utf-8");
		}
		if (suffix == QLatin1String("ico")) {
			return QByteArrayLiteral("image/x-icon");
		}
		if (suffix == QLatin1String("woff2")) {
			return QByteArrayLiteral("font/woff2");
		}
		return QByteArrayLiteral("application/octet-stream");
	}

	QString modernShellResourcePathForHttpPath(QString path) {
		if (path.isEmpty() || path == QLatin1String("/")) {
			path = QStringLiteral("/modern-shell/index.html");
		}
		if (path == QLatin1String("/qtwebchannel/qwebchannel.js")) {
			return QStringLiteral(":/qtwebchannel/qwebchannel.js");
		}
		if (!path.startsWith(QLatin1String("/modern-shell/"))) {
			return QString();
		}

		QString assetPath = path.mid(QStringLiteral("/modern-shell/").size());
		if (assetPath.isEmpty()) {
			assetPath = QStringLiteral("index.html");
		}
		if (assetPath.contains(QLatin1String("..")) || assetPath.contains(QLatin1Char('\\'))) {
			return QString();
		}
		return QStringLiteral(":/modern-shell/%1").arg(assetPath);
	}

	void writeModernShellHttpResponse(QTcpSocket *socket, const int statusCode, const QByteArray &reason,
									  const QByteArray &body = QByteArray(),
									  const QByteArray &contentType = QByteArrayLiteral("text/plain; charset=utf-8"),
									  const bool headOnly = false) {
		if (!socket) {
			return;
		}

		QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + QByteArrayLiteral(" ")
							  + reason + QByteArrayLiteral("\r\n");
		response += QByteArrayLiteral("Connection: close\r\n");
		response += QByteArrayLiteral("Cache-Control: no-store\r\n");
		response += QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n");
		if (!contentType.isEmpty()) {
			response += QByteArrayLiteral("Content-Type: ") + contentType + QByteArrayLiteral("\r\n");
		}
		response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n");
		response += QByteArrayLiteral("\r\n");
		if (!headOnly) {
			response += body;
		}
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::localHttp response status=%1 bytes=%2 head=%3")
										  .arg(statusCode)
										  .arg(body.size())
										  .arg(headOnly ? 1 : 0));
		socket->write(response);
		socket->disconnectFromHost();
	}

	void serveModernShellHttpRequest(QTcpSocket *socket, const QByteArray &request) {
		const int headerEnd             = request.indexOf(QByteArrayLiteral("\r\n\r\n"));
		const QByteArray headerBytes    = headerEnd >= 0 ? request.left(headerEnd) : request;
		const QList< QByteArray > lines = headerBytes.split('\n');
		const QByteArray requestLine    = lines.value(0).trimmed();
		const QList< QByteArray > parts = requestLine.split(' ');
		if (parts.size() < 2) {
			writeModernShellHttpResponse(socket, 400, QByteArrayLiteral("Bad Request"), QByteArrayLiteral("Bad Request"));
			return;
		}

		const QByteArray method = parts.at(0).trimmed().toUpper();
		if (method != QByteArrayLiteral("GET") && method != QByteArrayLiteral("HEAD")) {
			writeModernShellHttpResponse(socket, 405, QByteArrayLiteral("Method Not Allowed"),
										 QByteArrayLiteral("Method Not Allowed"));
			return;
		}

		const QByteArray requestTarget = parts.at(1);
		const QUrl requestUrl          = QUrl::fromEncoded(requestTarget);
		QString requestPath            = requestUrl.path();
		if (requestPath.isEmpty() && requestTarget.startsWith('/')) {
			const int queryStart     = requestTarget.indexOf('?');
			const QByteArray pathBytes = queryStart >= 0 ? requestTarget.left(queryStart) : requestTarget;
			requestPath              = QUrl::fromPercentEncoding(pathBytes);
		}

		const QString resourcePath = modernShellResourcePathForHttpPath(requestPath);
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::localHttp request target=%1 path=%2 resource=%3")
										  .arg(QString::fromUtf8(requestTarget.left(180)))
										  .arg(requestPath)
										  .arg(resourcePath));
		if (resourcePath.isEmpty()) {
			writeModernShellHttpResponse(socket, 404, QByteArrayLiteral("Not Found"), QByteArrayLiteral("Not Found"));
			return;
		}

		QFile file(resourcePath);
		if (!file.open(QIODevice::ReadOnly)) {
			writeModernShellHttpResponse(socket, 404, QByteArrayLiteral("Not Found"), QByteArrayLiteral("Not Found"));
			return;
		}

		const QByteArray body = file.readAll();
		writeModernShellHttpResponse(socket, 200, QByteArrayLiteral("OK"), body,
									 modernShellMimeTypeForResource(resourcePath), method == QByteArrayLiteral("HEAD"));
	}

	bool hostEqualsOrEndsWith(const QString &host, const QString &domain) {
		const QString normalizedHost = host.trimmed().toLower();
		const QString normalizedDomain = domain.trimmed().toLower();
		return normalizedHost == normalizedDomain || normalizedHost.endsWith(QStringLiteral(".") + normalizedDomain);
	}

	bool isPreviewMediaCdnHost(const QString &host) {
		return hostEqualsOrEndsWith(host, QStringLiteral("cdninstagram.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("fbcdn.net"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("fbsbx.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("twimg.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("tenor.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("giphy.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("redd.it"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("redditmedia.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("imgur.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("4cdn.org"));
	}

	bool isYouTubeEmbedDocumentHost(const QString &host) {
		return hostEqualsOrEndsWith(host, QStringLiteral("youtube.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("youtube-nocookie.com"));
	}

	bool isTwitchEmbedDocumentHost(const QString &host) {
		return hostEqualsOrEndsWith(host, QStringLiteral("twitch.tv"));
	}

	bool isPreviewEmbedPlayerHost(const QString &host) {
		return hostEqualsOrEndsWith(host, QStringLiteral("tiktok.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("instagram.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("instagr.am"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("vimeo.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("dailymotion.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("spotify.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("spotifycdn.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("soundcloud.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("streamable.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("facebook.com"))
			   || isTwitchEmbedDocumentHost(host);
	}

	bool isTrustedProviderSessionUrl(const QUrl &url) {
		if (url.scheme().toLower() != QLatin1String("https")) {
			return false;
		}

		const QString host = url.host().trimmed().toLower();
		return isYouTubeEmbedDocumentHost(host) || isPreviewEmbedPlayerHost(host)
			   || hostEqualsOrEndsWith(host, QStringLiteral("google.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("googleusercontent.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("gstatic.com"))
			   || hostEqualsOrEndsWith(host, QStringLiteral("fbcdn.net"));
	}

	bool isModernShellInitiatedRequest(const QWebEngineUrlRequestInfo &info) {
		const QUrl initiator = info.initiator();
		if (!initiator.isValid() || initiator.isEmpty()) {
			return false;
		}

		const QString scheme = initiator.scheme().toLower();
		if (scheme == QLatin1String("qrc")) {
			return true;
		}
		if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
			return false;
		}

		const QString host = initiator.host().trimmed().toLower();
		return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
			   || host == QLatin1String("::1");
	}

	QByteArray previewMediaRefererForHost(const QString &host) {
		if (hostEqualsOrEndsWith(host, QStringLiteral("cdninstagram.com"))) {
			return QByteArrayLiteral("https://www.instagram.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("fbcdn.net"))
			|| hostEqualsOrEndsWith(host, QStringLiteral("fbsbx.com"))) {
			return QByteArrayLiteral("https://www.facebook.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("twimg.com"))) {
			return QByteArrayLiteral("https://x.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("tenor.com"))) {
			return QByteArrayLiteral("https://tenor.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("giphy.com"))) {
			return QByteArrayLiteral("https://giphy.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("redd.it"))
			|| hostEqualsOrEndsWith(host, QStringLiteral("redditmedia.com"))) {
			return QByteArrayLiteral("https://www.reddit.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("imgur.com"))) {
			return QByteArrayLiteral("https://imgur.com/");
		}
		if (hostEqualsOrEndsWith(host, QStringLiteral("4cdn.org"))) {
			return QByteArrayLiteral("https://boards.4chan.org/");
		}
		return QByteArray();
	}

	class PreviewMediaUrlInterceptor final : public QWebEngineUrlRequestInterceptor {
	public:
		explicit PreviewMediaUrlInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}

		void interceptRequest(QWebEngineUrlRequestInfo &info) override {
			const QUrl url = info.requestUrl();
			const QWebEngineUrlRequestInfo::ResourceType resourceType = info.resourceType();
			const bool shellInitiatedRequest                          = isModernShellInitiatedRequest(info);

			if (url.scheme().toLower() != QLatin1String("https")) {
				return;
			}

			if (isYouTubeEmbedDocumentHost(url.host())) {
				if (resourceType == QWebEngineUrlRequestInfo::ResourceTypeSubFrame
					|| resourceType == QWebEngineUrlRequestInfo::ResourceTypeMainFrame) {
					info.setHttpHeader(QByteArrayLiteral("Referer"), YOUTUBE_EMBED_REFERER);
					return;
				}
				return;
			}

			if (isTwitchEmbedDocumentHost(url.host())) {
				// Keep WebEngine's localhost Referer intact so Twitch can match the iframe
				// against its required parent= query value.
			}

			if (isPreviewEmbedPlayerHost(url.host())) {
				info.setHttpHeader(QByteArrayLiteral("User-Agent"), PREVIEW_MEDIA_USER_AGENT);
				info.setHttpHeader(QByteArrayLiteral("Accept-Language"), PREVIEW_MEDIA_ACCEPT_LANGUAGE);
				return;
			}

			if (!isPreviewMediaCdnHost(url.host())) {
				// SECURITY: this host is on none of the media/embed allowlists. Block the request
				// types that are usable for data exfiltration (XHR/fetch, beacons, CSP reports)
				// and WebSockets when they originate from the qrc shell document. Requests initiated
				// by approved iframe players are deliberately left untouched so embeds can load their
				// own API/CDN/WebSocket dependencies without being mistaken for shell egress.
				if (shellInitiatedRequest) {
					switch (resourceType) {
						case QWebEngineUrlRequestInfo::ResourceTypeXhr:
						case QWebEngineUrlRequestInfo::ResourceTypePing:
						case QWebEngineUrlRequestInfo::ResourceTypeCspReport:
						case QWebEngineUrlRequestInfo::ResourceTypeWebSocket:
							info.block(true);
							break;
						default:
							break;
					}
				}
				return;
			}

			const bool mediaLike = resourceType == QWebEngineUrlRequestInfo::ResourceTypeMedia
								   || resourceType == QWebEngineUrlRequestInfo::ResourceTypeImage
								   || resourceType == QWebEngineUrlRequestInfo::ResourceTypeUnknown;
			if (!mediaLike) {
				return;
			}

			info.setHttpHeader(QByteArrayLiteral("User-Agent"), PREVIEW_MEDIA_USER_AGENT);
			info.setHttpHeader(QByteArrayLiteral("Accept"), PREVIEW_MEDIA_ACCEPT);
			info.setHttpHeader(QByteArrayLiteral("Accept-Language"), PREVIEW_MEDIA_ACCEPT_LANGUAGE);
			const QByteArray referer = previewMediaRefererForHost(url.host());
			if (!referer.isEmpty()) {
				info.setHttpHeader(QByteArrayLiteral("Referer"), referer);
				info.setHttpHeader(QByteArrayLiteral("Origin"), referer.left(referer.size() - 1));
			}
			info.setHttpHeader(QByteArrayLiteral("Sec-Fetch-Dest"),
							   resourceType == QWebEngineUrlRequestInfo::ResourceTypeImage ? QByteArrayLiteral("image")
																							: QByteArrayLiteral("video"));
			info.setHttpHeader(QByteArrayLiteral("Sec-Fetch-Mode"), QByteArrayLiteral("no-cors"));
			info.setHttpHeader(QByteArrayLiteral("Sec-Fetch-Site"), QByteArrayLiteral("cross-site"));
		}
	};

	void appendModernShellConnectTrace(const QString &message) {
		if (qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") == 0) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::Append | QIODevice::Text)) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
								+ " UI " + message.toUtf8() + '\n';
		traceFile.write(line);
		traceFile.flush();
	}
} // namespace

ModernShellHost::ModernShellHost(QWidget *parent) : QWidget(parent) {
	setAttribute(Qt::WA_StyledBackground, true);
	setObjectName(QLatin1String("qwModernShellHost"));

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);

	m_view = new QWebEngineView(this);
	m_layout->addWidget(m_view);
	m_view->setAcceptDrops(true);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_view->installEventFilter(this);

	m_shellServer = new QTcpServer(this);
	connect(m_shellServer, &QTcpServer::newConnection, this, &ModernShellHost::handleLocalShellConnection);

	m_profile = new QWebEngineProfile(QStringLiteral("MumbleModernShell"), this);
	m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
	m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
	m_profile->setPersistentStoragePath(Global::get().qdBasePath.filePath(QStringLiteral("modern-shell-web-profile")));
	m_profile->setCachePath(Global::get().qdBasePath.filePath(QStringLiteral("modern-shell-web-cache")));
	m_profile->setHttpUserAgent(QString::fromLatin1(PREVIEW_MEDIA_USER_AGENT));
	m_profile->setHttpAcceptLanguage(QString::fromLatin1(PREVIEW_MEDIA_ACCEPT_LANGUAGE));

	m_page = new ModernShellPage(m_profile, m_view);
	m_view->setPage(m_page);
	m_requestInterceptor = new PreviewMediaUrlInterceptor(m_profile);
	m_profile->setUrlRequestInterceptor(m_requestInterceptor);

	m_channel = new QWebChannel(this);
	m_bridge  = new ModernShellBridge(this);
	m_channel->registerObject(QStringLiteral("modernBridge"), m_bridge);
	m_page->setWebChannel(m_channel);
	m_bootTimeoutTimer = new QTimer(this);
	m_bootTimeoutTimer->setSingleShot(true);
	m_bootTimeoutTimer->setInterval(15000);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

	connect(m_view, &QWebEngineView::loadFinished, this, &ModernShellHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this, &ModernShellHost::handleRenderProcessTerminated);
	connect(m_bridge, &ModernShellBridge::bootReady, this, &ModernShellHost::handleBridgeBootReady);
	connect(m_bridge, &ModernShellBridge::providerSessionRequested, this, &ModernShellHost::openProviderSession);
	connect(m_bridge, &ModernShellBridge::nativeContextMenuRequested, this, &ModernShellHost::showNativeContextMenu);
	connect(m_bridge, &ModernShellBridge::nativeContextMenuCloseRequested, this, &ModernShellHost::closeNativeContextMenu);
	connect(m_bootTimeoutTimer, &QTimer::timeout, this, &ModernShellHost::handleBootTimeout);
	connect(m_page, &ModernShellPage::externalNavigationRequested, this, [](const QUrl &url) {
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 QObject::tr("Modern layout requested external navigation to %1.")
									 .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped()));
		}
	});
}

bool ModernShellHost::start(QString *errorMessage) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start enter started=%1 view=%2")
									  .arg(m_started ? 1 : 0)
									  .arg(m_view ? 1 : 0));
	if (m_started) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start already-started"));
		return true;
	}

	if (!m_view) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Modern shell view could not be initialized.");
		}
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start missing-view"));
		return false;
	}

	if (!ensureLocalShellServer()) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start local-shell-server-unavailable"));
	}

	const QUrl url = modernShellUrl(m_shellServer && m_shellServer->isListening() ? m_shellServerPort : 0);
	if (!url.isValid() || url.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Modern shell URL is invalid.");
		}
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start invalid-url"));
		return false;
	}

	m_shellUrl = url;
	m_view->load(url);
	m_started = true;
	m_bootReady = false;
	m_bootTimeoutTimer->start();
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start load=%1 timeout=%2")
									  .arg(url.toString())
									  .arg(m_bootTimeoutTimer->interval()));
	return true;
}

bool ModernShellHost::ensureLocalShellServer() {
	if (!m_shellServer) {
		return false;
	}
	if (m_shellServer->isListening()) {
		return true;
	}

	if (!m_shellServer->listen(QHostAddress(QStringLiteral("127.0.0.1")), 0)) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::ensureLocalShellServer failed=%1")
										  .arg(m_shellServer->errorString()));
		return false;
	}

	m_shellServerPort = m_shellServer->serverPort();
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::ensureLocalShellServer port=%1")
									  .arg(m_shellServerPort));
	return true;
}

void ModernShellHost::handleLocalShellConnection() {
	if (!m_shellServer) {
		return;
	}

	while (QTcpSocket *socket = m_shellServer->nextPendingConnection()) {
		socket->setParent(this);
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::localHttp connection peer=%1:%2 available=%3")
										  .arg(socket->peerAddress().toString())
										  .arg(socket->peerPort())
										  .arg(socket->bytesAvailable()));
		const auto processSocketRead = [socket]() {
			QByteArray request = socket->property("modernShellRequestBuffer").toByteArray();
			request.append(socket->readAll());
			if (!request.contains(QByteArrayLiteral("\r\n\r\n"))) {
				socket->setProperty("modernShellRequestBuffer", request);
				return;
			}

			socket->setProperty("modernShellRequestBuffer", QByteArray());
			serveModernShellHttpRequest(socket, request);
		};
		connect(socket, &QTcpSocket::readyRead, this, processSocketRead);
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		if (socket->bytesAvailable() > 0) {
			processSocketRead();
		}
	}
}

QWebEngineProfile *ModernShellHost::webProfile() const {
	return m_profile;
}

ModernShellBridge *ModernShellHost::bridge() const {
	return m_bridge;
}

void ModernShellHost::runAutomationScript(const QString &script) {
	if (!m_page || script.trimmed().isEmpty()) {
		return;
	}

	m_page->runJavaScript(script, QWebEngineScript::MainWorld);
}

QVariant ModernShellHost::runAutomationScriptResult(const QString &script, const int timeoutMilliseconds) {
	if (!m_page || script.trimmed().isEmpty()) {
		return QVariant();
	}

	QVariant result;
	bool finished = false;
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	m_page->runJavaScript(script, QWebEngineScript::MainWorld, [&result, &finished, &loop](const QVariant &value) {
		result   = value;
		finished = true;
		loop.quit();
	});
	timeout.start(qBound(50, timeoutMilliseconds, 10000));
	loop.exec();
	return finished ? result : QVariant();
}

QVariantMap ModernShellHost::lastNativeContextMenuRequest() const {
	return m_lastNativeContextMenuRequest;
}

void ModernShellHost::clearLastNativeContextMenuRequest() {
	m_lastNativeContextMenuRequest.clear();
}

void ModernShellHost::closeNativeContextMenuForAutomation() {
	closeNativeContextMenu();
}

void ModernShellHost::notifyNativeContextMenuClosed(const QString &token) {
	if (!m_page || token.trimmed().isEmpty()) {
		return;
	}

	const QVariantList args { token };
	const QString script =
		QStringLiteral("if(window.__mumbleModernNativeContextMenuClosed){"
					   "window.__mumbleModernNativeContextMenuClosed.apply(null,%1);"
					   "}")
			.arg(QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact)));
	m_page->runJavaScript(script, QWebEngineScript::MainWorld);
}

void ModernShellHost::invokeNativeContextMenuAction(const QString &token, const int actionIndex) {
	if (!m_page || token.trimmed().isEmpty() || actionIndex < 0) {
		return;
	}

	const QVariantList args { token, actionIndex };
	const QString script =
		QStringLiteral("if(window.__mumbleModernInvokeNativeContextMenuAction){"
					   "window.__mumbleModernInvokeNativeContextMenuAction.apply(null,%1);"
					   "}")
			.arg(QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact)));
	m_page->runJavaScript(script, QWebEngineScript::MainWorld);
}

void ModernShellHost::closeNativeContextMenu() {
	m_lastNativeContextMenuRequest.clear();
	if (!m_nativeContextMenu) {
		return;
	}

	m_nativeContextMenu->close();
}

void ModernShellHost::openProviderSession(const QString &href) {
	QUrl url(href.trimmed());
	if (!url.isValid()) {
		url = QUrl::fromUserInput(href.trimmed());
	}
	if (url.isEmpty() || !isTrustedProviderSessionUrl(url) || !m_profile) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Warning,
				tr("Modern layout ignored an untrusted provider-session URL: %1.").arg(href.toHtmlEscaped()));
		}
		return;
	}

	if (!m_providerSessionView) {
		m_providerSessionView = new QWebEngineView(this);
		m_providerSessionView->setAttribute(Qt::WA_DeleteOnClose, true);
		m_providerSessionView->setWindowFlag(Qt::Window, true);
		m_providerSessionView->setWindowTitle(tr("Preview provider session"));
		m_providerSessionView->resize(1120, 780);

		QWebEnginePage *sessionPage = new QWebEnginePage(m_profile, m_providerSessionView);
		sessionPage->setAudioMuted(false);
		m_providerSessionView->setPage(sessionPage);
		m_providerSessionView->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

		connect(m_providerSessionView, &QObject::destroyed, this, [this]() { m_providerSessionView = nullptr; });
	}

	m_providerSessionView->load(url);
	m_providerSessionView->show();
	m_providerSessionView->raise();
	m_providerSessionView->activateWindow();
}

ModernContextMenuHost *ModernShellHost::ensureNativeContextMenuHost() {
	if (m_nativeContextMenu) {
		return m_nativeContextMenu;
	}

	auto *menu = new ModernContextMenuHost(window());
	menu->setObjectName(QStringLiteral("modernShellNativeContextMenu"));
	m_nativeContextMenu = menu;
	connect(menu, &ModernContextMenuHost::actionRequested, this,
			[this](const QString &actionToken, const int actionIndex) {
				invokeNativeContextMenuAction(actionToken, actionIndex);
			});
	connect(menu, &ModernContextMenuHost::popupClosed, this, [this](const QString &closedToken) {
		notifyNativeContextMenuClosed(closedToken);
	});
	connect(menu, &ModernContextMenuHost::hostFailed, this, [this, menu](const QString &reason) {
		if (m_nativeContextMenu == menu) {
			m_nativeContextMenu = nullptr;
		}
		menu->deleteLater();
		emit bootFailed(reason);
	});
	return menu;
}

void ModernShellHost::showNativeContextMenu(const QVariantMap &request) {
	if (!m_page || !m_view) {
		return;
	}

	const QString token      = request.value(QStringLiteral("token")).toString().trimmed();
	const QVariantList items = request.value(QStringLiteral("items")).toList();
	if (token.isEmpty() || items.isEmpty()) {
		return;
	}
	m_lastNativeContextMenuRequest = request;

	if (m_nativeContextMenu) {
		m_nativeContextMenu->close();
	}

	const int clientX = request.value(QStringLiteral("x")).toInt();
	const int clientY = request.value(QStringLiteral("y")).toInt();
	const QPoint globalAnchor = m_view->mapToGlobal(QPoint(clientX, clientY));
	const QString openSubmenuLabel = request.value(QStringLiteral("openSubmenuLabel")).toString();
	const QVariantMap uiTweaks = request.value(QStringLiteral("uiTweaks")).toMap();

	ModernContextMenuHost *menu = ensureNativeContextMenuHost();
	if (!menu) {
		m_lastNativeContextMenuRequest.clear();
		return;
	}
	if (!menu->showMenu(token, items, globalAnchor, openSubmenuLabel, uiTweaks)) {
		if (m_nativeContextMenu == menu) {
			m_nativeContextMenu = nullptr;
		}
		menu->deleteLater();
		m_lastNativeContextMenuRequest.clear();
	}
}

void ModernShellHost::publishHostViewportMetrics(const QSize &viewportSize, bool openRail) {
	if (!m_page || !m_view) {
		return;
	}

	const QSize size = viewportSize.isValid() ? viewportSize : m_view->size();
	const QString script =
		QStringLiteral("window.mumbleHostViewportWidth=%1;"
					   "window.mumbleHostViewportHeight=%2;"
					   "(function(){"
					   "var shell=document.querySelector('.app-shell');"
					   "if(shell){"
					   "var compact=%1<=940;"
					   "shell.classList.toggle('is-compact-layout',compact);"
					   "if(compact){"
					   "shell.classList.add('rail-is-collapsed');"
					   "shell.classList.remove('rail-user-opened');"
					   "}"
					   "}"
					   "window.dispatchEvent(new CustomEvent('mumble-host-viewport',"
					   "{detail:{width:%1,height:%2}}));"
					   "if(compact&&%3){"
					   "window.setTimeout(function(){"
					   "var button=document.getElementById('rail-toggle-button');"
					   "if(button&&button.getAttribute('aria-expanded')!=='true'){button.click();}"
					   "},0);"
					   "}"
					   "})();")
			.arg(size.width())
			.arg(size.height())
			.arg(openRail ? QStringLiteral("true") : QStringLiteral("false"));
	m_page->runJavaScript(script, QWebEngineScript::MainWorld);
}

bool ModernShellHost::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_view) {
		const auto extractImageUrls = [](const QMimeData *mimeData) {
			QList< QUrl > imageUrls;
			if (!mimeData) {
				return imageUrls;
			}

			const QList< QUrl > urls = mimeData->urls();
			for (const QUrl &url : urls) {
				if (!url.isLocalFile()) {
					continue;
				}

				const QString localPath = url.toLocalFile();
				if (QImageReader::imageFormat(localPath).isEmpty()) {
					continue;
				}

				imageUrls.push_back(url);
			}

			return imageUrls;
		};

		const auto extractMimeImage = [](const QMimeData *mimeData) {
			if (!mimeData || !mimeData->hasImage()) {
				return QImage();
			}

			const QVariant imageData = mimeData->imageData();
			QImage image             = qvariant_cast< QImage >(imageData);
			if (!image.isNull()) {
				return image;
			}

			const QPixmap pixmap = qvariant_cast< QPixmap >(imageData);
			return pixmap.isNull() ? QImage() : pixmap.toImage();
		};

		if (event->type() == QEvent::Resize) {
			publishHostViewportMetrics();
		} else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
			QDropEvent *dropEvent = static_cast< QDropEvent * >(event);
			const QList< QUrl > imageUrls = extractImageUrls(dropEvent->mimeData());
			const QImage image            = extractMimeImage(dropEvent->mimeData());
			if (!imageUrls.isEmpty() || !image.isNull()) {
				dropEvent->acceptProposedAction();
				return true;
			}
		} else if (event->type() == QEvent::Drop) {
			QDropEvent *dropEvent = static_cast< QDropEvent * >(event);
			const QList< QUrl > imageUrls = extractImageUrls(dropEvent->mimeData());
			if (!imageUrls.isEmpty()) {
				dropEvent->acceptProposedAction();
				emit imageUrlsDropped(imageUrls);
				return true;
			}

			const QImage image = extractMimeImage(dropEvent->mimeData());
			if (!image.isNull()) {
				dropEvent->acceptProposedAction();
				emit imageDropped(image);
				return true;
			}
		}
	}

	return QWidget::eventFilter(watched, event);
}

void ModernShellHost::handleLoadFinished(const bool ok) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleLoadFinished ok=%1").arg(ok ? 1 : 0));
	if (ok) {
		return;
	}

	const QUrl currentUrl = m_shellUrl.isValid() ? m_shellUrl : (m_view ? m_view->url() : QUrl());
	if (currentUrl.scheme().toLower() == QLatin1String("http")
		&& currentUrl.host().trimmed().toLower() == QLatin1String("127.0.0.1")) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleLoadFinished tolerated-local-http url=%1")
										  .arg(currentUrl.toString()));
		return;
	}

	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	emit bootFailed(tr("The modern layout failed to load its local web assets."));
}

void ModernShellHost::handleRenderProcessTerminated(const QWebEnginePage::RenderProcessTerminationStatus status,
													const int exitCode) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleRenderProcessTerminated status=%1 exit=%2")
									  .arg(static_cast< int >(status))
									  .arg(exitCode));
	Q_UNUSED(status);
	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	emit bootFailed(tr("The modern layout renderer stopped unexpectedly with exit code %1.").arg(exitCode));
}

void ModernShellHost::handleBridgeBootReady() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleBridgeBootReady"));
	m_bootReady = true;
	m_bootTimeoutTimer->stop();
	publishHostViewportMetrics();
	QTimer::singleShot(0, this, [this]() {
		if (ModernContextMenuHost *menu = ensureNativeContextMenuHost()) {
			menu->prewarm();
		}
	});
}

void ModernShellHost::handleBootTimeout() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleBootTimeout started=%1 bootReady=%2")
									  .arg(m_started ? 1 : 0)
									  .arg(m_bootReady ? 1 : 0));
	if (!m_started || m_bootReady) {
		return;
	}

	m_started = false;
	emit bootFailed(tr("The modern layout did not finish initializing its local bridge."));
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
