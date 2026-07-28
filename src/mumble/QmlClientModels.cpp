// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlClientModels.h"

#include "ClientActionRegistry.h"
#include "EmbedDocument.h"

#include <QtCore/QCache>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui/QAction>
#include <QtGui/QTextBlock>
#include <QtGui/QTextBlockFormat>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextDocument>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFragment>
#include <QtGui/QTextImageFormat>
#include <QtGui/QTextList>
#include <QtGui/QTextListFormat>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <utility>

namespace {
	constexpr qsizetype MaxAttachmentCount = 16;
	constexpr qsizetype MaxPreviewMediaItemCount = 16;
	constexpr qsizetype MaxInlineMediaBytes = 24 * 1024 * 1024;
	constexpr qsizetype MaxReactionCount = 32;
	constexpr qsizetype MaxReactionActorCount = 32;
	constexpr qsizetype MaxReactionEmojiCharacters = 64;
	constexpr qsizetype MaxReactionActorNameCharacters = 256;

	bool isPluginOperation(const QVariantMap &operation) {
		return operation.value(QStringLiteral("kind")).toString().startsWith(QLatin1String("plugin-"))
			|| operation.value(QStringLiteral("id")).toString().startsWith(QLatin1String("plugin-"));
	}

	struct NativePlaybackPreparationResult {
		qulonglong generation = 0;
		QUrl mediaUrl;
		QUrl audioUrl;
		QStringList materializedPaths;
		QString error;
		QString audioWarning;
		bool cancelled = false;
	};

	struct MaterializedNativeMediaSource {
		QUrl url;
		QString path;
		QString error;
		bool cancelled = false;
	};

	std::once_flag g_nativeMediaCrashCleanupOnce;

	QString nativeMediaCacheRoot() {
		QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		if (root.isEmpty()) root = QDir::tempPath() + QStringLiteral("/Mumble");
		return QDir(root).filePath(QStringLiteral("native-direct-media"));
	}

	QString nativeMediaSuffix(const QString &mime) {
		static const QHash< QString, QString > suffixes {
			{ QStringLiteral("audio/aac"), QStringLiteral(".aac") },
			{ QStringLiteral("audio/flac"), QStringLiteral(".flac") },
			{ QStringLiteral("audio/mp4"), QStringLiteral(".m4a") },
			{ QStringLiteral("audio/mpeg"), QStringLiteral(".mp3") },
			{ QStringLiteral("audio/ogg"), QStringLiteral(".ogg") },
			{ QStringLiteral("audio/wav"), QStringLiteral(".wav") },
			{ QStringLiteral("audio/webm"), QStringLiteral(".webm") },
			{ QStringLiteral("audio/x-wav"), QStringLiteral(".wav") },
			{ QStringLiteral("video/mp4"), QStringLiteral(".mp4") },
			{ QStringLiteral("video/ogg"), QStringLiteral(".ogv") },
			{ QStringLiteral("video/quicktime"), QStringLiteral(".mov") },
			{ QStringLiteral("video/webm"), QStringLiteral(".webm") }
		};
		return suffixes.value(mime);
	}

	void removeNativePlaybackFiles(const QStringList &paths, const QString &sessionDirectory) {
		for (const QString &path : paths) {
			for (int attempt = 0; attempt < 40; ++attempt) {
				if (!QFileInfo::exists(path) || QFile::remove(path)) break;
				QThread::msleep(50);
			}
		}
		if (!sessionDirectory.isEmpty()) QDir().rmdir(sessionDirectory);
	}

	void scheduleNativePlaybackCleanup(const QStringList &paths, const QString &sessionDirectory) {
		if (paths.isEmpty() && sessionDirectory.isEmpty()) return;
		(void) QtConcurrent::run([paths, sessionDirectory]() {
			removeNativePlaybackFiles(paths, sessionDirectory);
		});
	}

	void cleanupNativeMediaFilesFromPreviousRuns(const QString &cacheRoot) {
		const QDir root(cacheRoot);
		if (!root.exists()) return;
		for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
			if (entry.isDir()) QDir(entry.absoluteFilePath()).removeRecursively();
			else QFile::remove(entry.absoluteFilePath());
		}
	}

	MaterializedNativeMediaSource materializeNativeMediaSource(
		const QUrl &source, const QString &mime, const QString &sessionDirectory,
		const std::shared_ptr< std::atomic_bool > &token) {
		MaterializedNativeMediaSource result;
		if (!token->load(std::memory_order_acquire)) {
			result.cancelled = true;
			return result;
		}
		if (source.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0) {
			result.url = source;
			return result;
		}

		const QString encodedSource = source.toString();
		const qsizetype comma = encodedSource.indexOf(QLatin1Char(','));
		const QString suffix = nativeMediaSuffix(mime);
		if (source.scheme().compare(QLatin1String("data"), Qt::CaseInsensitive) != 0
			|| comma <= 5 || suffix.isEmpty()) {
			result.error = QObject::tr("The native media source is not supported.");
			return result;
		}

		const QByteArray payload = encodedSource.mid(comma + 1).toLatin1();
		const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
			payload, QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > MaxInlineMediaBytes) {
			result.error = QObject::tr("The media data is invalid or exceeds the playback limit.");
			return result;
		}
		if (!token->load(std::memory_order_acquire)) {
			result.cancelled = true;
			return result;
		}

		if (!QDir().mkpath(sessionDirectory)) {
			result.error = QObject::tr("The private media cache is unavailable.");
			return result;
		}
		const QString path = QDir(sessionDirectory).filePath(
			QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
		QSaveFile output(path);
		if (!output.open(QIODevice::WriteOnly)
			|| output.write(decoded.decoded) != decoded.decoded.size()
			|| !output.commit()) {
			output.cancelWriting();
			QFile::remove(path);
			result.error = QObject::tr("The native media file could not be prepared.");
			return result;
		}
		if (!token->load(std::memory_order_acquire)) {
			QFile::remove(path);
			result.cancelled = true;
			return result;
		}
		result.path = path;
		result.url = QUrl::fromLocalFile(path);
		return result;
	}

	NativePlaybackPreparationResult materializeNativePlaybackSources(
		const qulonglong generation, const QUrl &mediaUrl, const QString &mediaMime,
		const QUrl &audioUrl, const QString &audioMime, const QString &cacheRoot,
		const QString &sessionDirectory, const std::shared_ptr< std::atomic_bool > &token) {
		NativePlaybackPreparationResult result;
		result.generation = generation;
		std::call_once(g_nativeMediaCrashCleanupOnce, [cacheRoot]() {
			cleanupNativeMediaFilesFromPreviousRuns(cacheRoot);
		});

		const MaterializedNativeMediaSource media = materializeNativeMediaSource(
			mediaUrl, mediaMime, sessionDirectory, token);
		if (media.cancelled) {
			result.cancelled = true;
			return result;
		}
		if (!media.error.isEmpty()) {
			result.error = media.error;
			return result;
		}
		result.mediaUrl = media.url;
		if (!media.path.isEmpty()) result.materializedPaths.push_back(media.path);

		if (!audioUrl.isEmpty()) {
			const MaterializedNativeMediaSource audio = materializeNativeMediaSource(
				audioUrl, audioMime, sessionDirectory, token);
			if (audio.cancelled) {
				removeNativePlaybackFiles(result.materializedPaths, sessionDirectory);
				result.materializedPaths.clear();
				result.cancelled = true;
				return result;
			}
			if (!audio.error.isEmpty()) {
				// A separate track is optional. Preserve the playable primary source
				// and let QML surface a degraded-audio warning instead of replacing
				// the entire video with a fatal error state.
				result.audioWarning = audio.error;
				return result;
			}
			result.audioUrl = audio.url;
			if (!audio.path.isEmpty()) result.materializedPaths.push_back(audio.path);
		}
		return result;
	}

	bool participantHasStatus(const QVariantList &statuses, const QStringList &kinds) {
		for (const QVariant &entry : statuses) {
			const QString kind = entry.toMap().value(QStringLiteral("kind")).toString().trimmed();
			for (const QString &candidate : kinds) {
				if (kind.compare(candidate, Qt::CaseInsensitive) == 0) return true;
			}
		}
		return false;
	}

	QVariantList coalesceVoiceScopeParticipants(const QVariantList &participants,
										 const QString &fallbackScopeToken = {}) {
		QSet< QString > presentUserScopes;
		for (const QVariant &entry : participants) {
			const QVariantMap participant = entry.toMap();
			const QString session = participant.value(QStringLiteral("session")).toString().trimmed();
			const QString entryKind = participant.value(QStringLiteral("entryKind"), QStringLiteral("user"))
								  .toString().trimmed().toLower();
			const QString scopeToken = participant.value(QStringLiteral("scopeToken"), fallbackScopeToken)
								   .toString().trimmed();
			if (!session.isEmpty() && !scopeToken.isEmpty() && entryKind != QLatin1String("listener")) {
				presentUserScopes.insert(scopeToken + QLatin1Char('\x1f') + session);
			}
		}

		QVariantList coalesced;
		QHash< QString, qsizetype > outputIndexByIdentity;
		coalesced.reserve(participants.size());
		outputIndexByIdentity.reserve(participants.size());
		for (const QVariant &entry : participants) {
			const QVariantMap participant = entry.toMap();
			const QString session = participant.value(QStringLiteral("session")).toString().trimmed();
			const QString entryKind = participant.value(QStringLiteral("entryKind"), QStringLiteral("user"))
								  .toString().trimmed().toLower();
			const QString scopeToken = participant.value(QStringLiteral("scopeToken"), fallbackScopeToken)
								   .toString().trimmed();
			const QString scopeSession = scopeToken + QLatin1Char('\x1f') + session;
			if (!session.isEmpty() && !scopeToken.isEmpty() && entryKind == QLatin1String("listener")
				&& presentUserScopes.contains(scopeSession)) {
				continue;
			}

			QString identity = participant.value(QStringLiteral("participantKey")).toString().trimmed();
			if (!session.isEmpty() && !scopeToken.isEmpty()) {
				identity = entryKind + QLatin1Char('\x1f') + scopeSession;
			}
			if (identity.isEmpty()) {
				coalesced.push_back(entry);
				continue;
			}
			const auto existing = outputIndexByIdentity.constFind(identity);
			if (existing != outputIndexByIdentity.cend()) {
				coalesced[*existing] = entry;
			} else {
				outputIndexByIdentity.insert(identity, coalesced.size());
				coalesced.push_back(entry);
			}
		}
		return coalesced;
	}

	void preserveListenerPresentation(const QVariantMap &row, QVariantList &badges, QVariantList &statuses) {
		if (!row.value(QStringLiteral("listener")).toBool()
			&& row.value(QStringLiteral("entryKind")).toString() != QLatin1String("listener"))
			return;

		QVariantMap listenerStatus;
		for (const QVariant &entry : row.value(QStringLiteral("statuses")).toList()) {
			const QVariantMap status = entry.toMap();
			if (status.value(QStringLiteral("kind")).toString() == QLatin1String("listener")) {
				listenerStatus = status;
				break;
			}
		}
		if (!listenerStatus.isEmpty()) {
			QVariantList mergedStatuses { listenerStatus };
			for (const QVariant &entry : std::as_const(statuses)) {
				if (entry.toMap().value(QStringLiteral("kind")).toString() != QLatin1String("listener"))
					mergedStatuses.push_back(entry);
			}
			statuses = std::move(mergedStatuses);

			const QString listenerLabel = listenerStatus.value(QStringLiteral("label")).toString();
			if (!listenerLabel.isEmpty() && !badges.contains(listenerLabel)) badges.prepend(listenerLabel);
		}
	}

	bool acceptsFrontendStateMutation(const QObject *object) {
		return !object->property(QmlVisualFixtureMutation::OverrideProperty).toBool()
			|| object->property(QmlVisualFixtureMutation::WriteProperty).toBool();
	}

	QString motdContentSignature(const QString &value) {
		const QString text = value.trimmed();
		if (text.isEmpty()) return {};

		quint32 hash = 2166136261u;
		for (const QChar character : text) {
			hash ^= static_cast< quint32 >(character.unicode());
			hash *= 16777619u;
		}
		return QStringLiteral("v1:%1:%2").arg(text.length()).arg(QString::number(hash, 16));
	}

	QVariantMap motdAction(const QString &id, const QString &label, const QString &signature,
						 const QString &tone = QString()) {
		QVariantMap action { { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
						 { QStringLiteral("enabled"), true } };
		if (!tone.isEmpty()) action.insert(QStringLiteral("tone"), tone);
		if (!signature.isEmpty()) {
			action.insert(QStringLiteral("payload"),
						  QVariantMap { { QStringLiteral("signature"), signature } });
		}
		return action;
	}

	QString safeExternalUrl(const QVariant &value, const bool allowMumble = false) {
		QUrl url(value.toString().trimmed());
		if (!url.isValid() || url.isRelative()) return {};
		const QString scheme = url.scheme().toLower();
		if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
			if (url.host().isEmpty() || !url.userInfo().isEmpty()) return {};
		} else if (scheme == QLatin1String("mailto")) {
			if (url.path().trimmed().isEmpty()) return {};
		} else if (allowMumble && scheme == QLatin1String("mumble")) {
			if (url.host().isEmpty() || !url.userInfo().isEmpty()) return {};
		} else {
			return {};
		}
		url.setScheme(scheme);
		return url.toString(QUrl::FullyEncoded);
	}

	QString safeImageSource(const QVariant &value) {
		const QString source = value.toString().trimmed();
		if (source.isEmpty() || source.size() > 16384) return {};
		const QUrl url(source);
		if (!url.isValid() || url.isRelative()) return {};
		const QString scheme = url.scheme().toLower();
		if (scheme == QLatin1String("image")) {
			return url.host() == QLatin1String("mumble") && !url.path().isEmpty() ? source : QString();
		}
		if (scheme == QLatin1String("qrc")) {
			return !url.path().isEmpty() && url.userInfo().isEmpty() ? source : QString();
		}
		return {};
	}

	QString safeManagedAnimatedImageSource(const QVariant &value) {
		const QString source = value.toString().trimmed();
		const QUrl url(source);
		if (source.size() > 16384 || !url.isValid() || !url.isLocalFile() || !url.query().isEmpty()
			|| !url.fragment().isEmpty()) {
			return {};
		}
		QString path = QDir::fromNativeSeparators(url.toLocalFile());
		static const QRegularExpression managedPath(
			QStringLiteral("/mumble-qml-images-[A-Za-z0-9]+/[0-9a-f]{64}-[0-9a-f-]{36}\\.gif$"),
			QRegularExpression::CaseInsensitiveOption);
		return managedPath.match(path).hasMatch() ? source : QString();
	}

	QString normalizedMediaMime(const QVariant &value) {
		return value.toString().section(QLatin1Char(';'), 0, 0).trimmed().toLower().left(128);
	}

	bool isAllowedDirectMediaMime(const QString &mime, const bool audio) {
		static const QSet< QString > videoMimes {
			QStringLiteral("video/mp4"), QStringLiteral("video/webm"), QStringLiteral("video/ogg"),
			QStringLiteral("video/quicktime"), QStringLiteral("application/vnd.apple.mpegurl"),
			QStringLiteral("application/dash+xml")
		};
		static const QSet< QString > audioMimes {
			QStringLiteral("audio/aac"), QStringLiteral("audio/flac"), QStringLiteral("audio/mp4"),
			QStringLiteral("audio/mpeg"), QStringLiteral("audio/ogg"), QStringLiteral("audio/wav"),
			QStringLiteral("audio/webm"), QStringLiteral("audio/x-wav")
		};
		return (audio ? audioMimes : videoMimes).contains(mime);
	}

	QString safeDirectMediaSource(const QVariant &value, const QString &mime, const bool audio) {
		const QString source = value.toString().trimmed();
		if (source.isEmpty() || !isAllowedDirectMediaMime(mime, audio)) return {};
		const QUrl url(source);
		if (!url.isValid() || url.isRelative()) return {};
		const QString scheme = url.scheme().toLower();
		if (scheme == QLatin1String("https")) {
			return source.size() <= 16384 ? safeExternalUrl(url) : QString();
		}
		if (mime == QLatin1String("application/vnd.apple.mpegurl")
			|| mime == QLatin1String("application/dash+xml")) {
			// Manifest playback is delegated to the isolated WebEngineQuick surface.
			// Unlike bounded audio/video payloads it never accepts an inline data URL.
			return {};
		}
		if (scheme != QLatin1String("data")) return {};

		const qsizetype comma = source.indexOf(QLatin1Char(','));
		if (comma <= 5 || comma > 256) return {};
		const QString header = source.mid(5, comma - 5).toLower();
		if (header.section(QLatin1Char(';'), 0, 0).trimmed() != mime
			|| !header.split(QLatin1Char(';'), Qt::SkipEmptyParts).contains(QStringLiteral("base64"))) {
			return {};
		}
		const qsizetype payloadCharacters = source.size() - comma - 1;
		const qsizetype maximumCharacters = ((MaxInlineMediaBytes + 2) / 3) * 4;
		return payloadCharacters <= maximumCharacters ? source : QString();
	}

	QString safeHttpsMediaUrl(const QVariant &value) {
		const QUrl url(value.toString().trimmed());
		return url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
			? safeExternalUrl(url) : QString();
	}

	QVariantMap richTextSegment(const QString &text, const QTextCharFormat &format = {}) {
		QString safeText = text;
		safeText.remove(QChar::ObjectReplacementCharacter);
		if (safeText.isEmpty()) return {};

		QVariantMap segment { { QStringLiteral("text"), safeText } };
		if (format.fontWeight() >= QFont::DemiBold) segment.insert(QStringLiteral("bold"), true);
		if (format.fontItalic()) segment.insert(QStringLiteral("italic"), true);
		if (format.fontStrikeOut()) segment.insert(QStringLiteral("strike"), true);
		if (format.fontFixedPitch()) segment.insert(QStringLiteral("code"), true);
		if (format.isAnchor()) {
			const QString href = safeExternalUrl(format.anchorHref(), true);
			if (!href.isEmpty()) segment.insert(QStringLiteral("href"), href);
		}
		return segment;
	}

	QVariantMap richImageSegment(const QTextCharFormat &format) {
		if (!format.isImageFormat()) return {};
		const QTextImageFormat imageFormat = format.toImageFormat();
		const QString source = safeImageSource(imageFormat.name());
		if (source.isEmpty()) return {};

		QVariantMap segment { { QStringLiteral("kind"), QStringLiteral("image") },
						 { QStringLiteral("source"), source } };
		const QString altText = imageFormat.property(QTextFormat::ImageAltText).toString().trimmed().left(512);
		if (!altText.isEmpty()) {
			segment.insert(QStringLiteral("alt"), altText);
		}
		const qreal width = imageFormat.width();
		if (qIsFinite(width) && width > 0) {
			segment.insert(QStringLiteral("width"), qBound(1, qRound(qMin(width, qreal(8192))), 8192));
		}
		const qreal height = imageFormat.height();
		if (qIsFinite(height) && height > 0) {
			segment.insert(QStringLiteral("height"), qBound(1, qRound(qMin(height, qreal(8192))), 8192));
		}
		if (format.isAnchor()) {
			const QString href = safeExternalUrl(format.anchorHref(), true);
			if (!href.isEmpty()) segment.insert(QStringLiteral("href"), href);
		}
		return segment;
	}

	constexpr qsizetype MaxRichBodyCharacters = 100000;
	constexpr qsizetype MaxRichBodySegments = 512;
	constexpr int RichBodyCacheBytes = 4 * 1024 * 1024;

	QMutex &richBodyCacheMutex() {
		// Rich-body parsing runs on a deliberately process-lifetime pool so shell
		// teardown never waits for sender-controlled HTML parsing. Keep the shared
		// cache alive for the same lifetime to avoid static-destruction races.
		static auto *mutex = new QMutex;
		return *mutex;
	}

	QCache< QByteArray, QVariantList > &richBodyCache() {
		static auto *cache = new QCache< QByteArray, QVariantList >(RichBodyCacheBytes);
		return *cache;
	}

	QThreadPool &richBodyThreadPool() {
		static auto *pool = [] {
			auto *instance = new QThreadPool;
			instance->setMaxThreadCount(1);
			instance->setExpiryTimeout(30000);
			return instance;
		}();
		return *pool;
	}

	QVariantList fallbackStructuredMessageBody(const QString &bodyText) {
		const QString fallback = bodyText.left(MaxRichBodyCharacters);
		return fallback.isEmpty() ? QVariantList() : QVariantList { richTextSegment(fallback) };
	}

	bool messageBodyNeedsRichParsing(const QString &bodyHtml) {
		// The overwhelmingly common chat path is plain text that has merely been
		// HTML-escaped or wrapped in structural line-break tags.
		static const QRegularExpression richTagExpression(
			QStringLiteral(R"(<\s*/?\s*(?:a|b|strong|i|em|s|strike|del|code|img)\b)"),
			QRegularExpression::CaseInsensitiveOption);
		return richTagExpression.match(bodyHtml.left(MaxRichBodyCharacters)).hasMatch();
	}

	QByteArray structuredMessageBodyCacheKey(const QString &bodyHtml, const QString &bodyText) {
		QByteArray cacheMaterial = bodyHtml.left(MaxRichBodyCharacters).toUtf8();
		cacheMaterial.append('\0');
		cacheMaterial.append(bodyText.left(MaxRichBodyCharacters).toUtf8());
		return QCryptographicHash::hash(cacheMaterial, QCryptographicHash::Sha256);
	}

	std::optional< QVariantList > cachedStructuredMessageBody(const QByteArray &cacheKey) {
		QMutexLocker locker(&richBodyCacheMutex());
		if (const QVariantList *cached = richBodyCache().object(cacheKey)) return *cached;
		return std::nullopt;
	}

	QVariantList structuredMessageBody(const QString &bodyHtml, const QString &bodyText,
										 const QByteArray &knownCacheKey = {}) {
		const QString boundedHtml = bodyHtml.left(MaxRichBodyCharacters);
		const QString fallback = bodyText.left(MaxRichBodyCharacters);
		if (boundedHtml.trimmed().isEmpty() || !messageBodyNeedsRichParsing(boundedHtml)) {
			return fallbackStructuredMessageBody(fallback);
		}

		const QByteArray cacheKey = knownCacheKey.isEmpty()
			? structuredMessageBodyCacheKey(boundedHtml, fallback) : knownCacheKey;
		if (const auto cached = cachedStructuredMessageBody(cacheKey)) return *cached;

		QTextDocument document;
		document.setDocumentMargin(0);
		document.setHtml(boundedHtml);
		QVariantList segments;
		qsizetype emittedCharacters = 0;
		bool firstBlock = true;
		for (QTextBlock block = document.begin(); block.isValid() && segments.size() < MaxRichBodySegments;
			 block = block.next()) {
			if (!firstBlock && emittedCharacters < MaxRichBodyCharacters) {
				segments.push_back(richTextSegment(QStringLiteral("\n")));
				++emittedCharacters;
			}
			firstBlock = false;
			for (QTextBlock::iterator it = block.begin(); !it.atEnd() && segments.size() < MaxRichBodySegments; ++it) {
				const QTextFragment fragment = it.fragment();
				if (!fragment.isValid()) continue;
				const QVariantMap imageSegment = richImageSegment(fragment.charFormat());
				if (!imageSegment.isEmpty()) {
					segments.push_back(imageSegment);
					continue;
				}
				QString text = fragment.text();
				if (emittedCharacters + text.size() > MaxRichBodyCharacters) {
					text.truncate(MaxRichBodyCharacters - emittedCharacters);
				}
				const QVariantMap segment = richTextSegment(text, fragment.charFormat());
				if (!segment.isEmpty()) {
					segments.push_back(segment);
					emittedCharacters += segment.value(QStringLiteral("text")).toString().size();
				}
				if (emittedCharacters >= MaxRichBodyCharacters) break;
			}
		}
		if (segments.isEmpty() && !fallback.isEmpty()) segments.push_back(richTextSegment(fallback));
		const qsizetype estimatedBytes = boundedHtml.size() * 2 + fallback.size() * 2
			+ std::accumulate(segments.cbegin(), segments.cend(), qsizetype(0),
				[](const qsizetype total, const QVariant &segment) {
					return total + segment.toMap().value(QStringLiteral("text")).toString().size() * 2 + 64;
				});
		{
			QMutexLocker locker(&richBodyCacheMutex());
			richBodyCache().insert(cacheKey, new QVariantList(segments),
				static_cast< int >(std::clamp< qsizetype >(estimatedBytes, 1, RichBodyCacheBytes)));
		}
		return segments;
	}

	struct StructuredMotdDocument {
		QVariantList segments;
		QVariantList blocks;
	};

	QString motdBlockAlignment(const Qt::Alignment alignment) {
		if (alignment.testFlag(Qt::AlignHCenter)) return QStringLiteral("center");
		if (alignment.testFlag(Qt::AlignRight)) return QStringLiteral("right");
		return QStringLiteral("left");
	}

	QString motdListMarker(const QTextBlock &block) {
		const QTextList *list = block.textList();
		if (!list) return {};
		const QTextListFormat::Style style = list->format().style();
		if (style == QTextListFormat::ListDecimal) {
			return QString::number(list->itemNumber(block) + 1) + QLatin1Char('.');
		}
		return style == QTextListFormat::ListCircle ? QStringLiteral("\u25e6")
			: style == QTextListFormat::ListSquare ? QStringLiteral("\u25aa")
			: QStringLiteral("\u2022");
	}

	QVariantMap motdContentBlock(const QString &kind, const QVariantList &segments,
							 const QTextBlock &sourceBlock, const int headingLevel = 0) {
		if (segments.isEmpty()) return {};
		QString plainText;
		for (const QVariant &value : segments) {
			const QVariantMap segment = value.toMap();
			if (segment.contains(QStringLiteral("text"))) {
				plainText += segment.value(QStringLiteral("text")).toString();
			} else if (segment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
				plainText += segment.value(QStringLiteral("alt")).toString();
			}
		}

		const QTextBlockFormat format = sourceBlock.blockFormat();
		QVariantMap block {
			{ QStringLiteral("kind"), kind },
			{ QStringLiteral("segments"), segments },
			{ QStringLiteral("plainText"), plainText.trimmed().left(4096) },
			{ QStringLiteral("alignment"), motdBlockAlignment(format.alignment()) },
			{ QStringLiteral("indent"), qBound(0, format.indent(), 8) }
		};
		if (headingLevel > 0) block.insert(QStringLiteral("headingLevel"), headingLevel);
		if (kind == QLatin1String("list-item")) {
			block.insert(QStringLiteral("marker"), motdListMarker(sourceBlock));
		}
		return block;
	}

	StructuredMotdDocument structuredMotdDocument(const QString &bodyHtml, const QString &bodyText) {
		StructuredMotdDocument result;
		const QString boundedHtml = bodyHtml.left(MaxRichBodyCharacters);
		const QString fallback = bodyText.left(MaxRichBodyCharacters);
		if (boundedHtml.trimmed().isEmpty()) return result;

		QTextDocument document;
		document.setDocumentMargin(0);
		document.setHtml(boundedHtml);
		qsizetype emittedCharacters = 0;
		bool firstFlatBlock = true;
		for (QTextBlock block = document.begin(); block.isValid()
				&& result.blocks.size() < MaxRichBodySegments; block = block.next()) {
			const QTextBlockFormat blockFormat = block.blockFormat();
			const int headingLevel = qBound(0, blockFormat.headingLevel(), 6);
			const bool listItem = block.textList() != nullptr;
			const QString baseKind = headingLevel > 0 ? QStringLiteral("heading")
				: listItem ? QStringLiteral("list-item")
				: blockFormat.indent() > 0 ? QStringLiteral("quote")
				: QStringLiteral("paragraph");
			QVariantList textSegments;

			const auto appendFlatBoundary = [&] {
				if (!firstFlatBlock && emittedCharacters < MaxRichBodyCharacters) {
					result.segments.push_back(richTextSegment(QStringLiteral("\n")));
					++emittedCharacters;
				}
				firstFlatBlock = false;
			};
			const auto flushTextBlock = [&] {
				const QVariantMap content = motdContentBlock(baseKind, textSegments, block, headingLevel);
				if (!content.isEmpty()) {
					appendFlatBoundary();
					result.blocks.push_back(content);
					for (const QVariant &segment : std::as_const(textSegments)) result.segments.push_back(segment);
				}
				textSegments.clear();
			};

			for (QTextBlock::iterator it = block.begin(); !it.atEnd()
					&& result.blocks.size() < MaxRichBodySegments; ++it) {
				const QTextFragment fragment = it.fragment();
				if (!fragment.isValid()) continue;
				const QVariantMap image = richImageSegment(fragment.charFormat());
				if (!image.isEmpty()) {
					flushTextBlock();
					appendFlatBoundary();
					result.blocks.push_back(motdContentBlock(
						QStringLiteral("image"), QVariantList { image }, block));
					result.segments.push_back(image);
					continue;
				}

				QString text = fragment.text();
				if (emittedCharacters + text.size() > MaxRichBodyCharacters) {
					text.truncate(MaxRichBodyCharacters - emittedCharacters);
				}
				const QVariantMap segment = richTextSegment(text, fragment.charFormat());
				if (!segment.isEmpty()) {
					textSegments.push_back(segment);
					emittedCharacters += segment.value(QStringLiteral("text")).toString().size();
				}
				if (emittedCharacters >= MaxRichBodyCharacters) break;
			}
			flushTextBlock();
		}

		if (result.blocks.isEmpty() && !fallback.isEmpty()) {
			const QVariantList segments { richTextSegment(fallback) };
			result.segments = segments;
			result.blocks.push_back(QVariantMap {
				{ QStringLiteral("kind"), QStringLiteral("paragraph") },
				{ QStringLiteral("segments"), segments },
				{ QStringLiteral("plainText"), fallback.trimmed().left(4096) },
				{ QStringLiteral("alignment"), QStringLiteral("left") },
				{ QStringLiteral("indent"), 0 }
			});
		}
		return result;
	}

	QVariantMap normalizedPreviewMetadata(const QVariant &value) {
		const QVariantMap source = value.toMap();
		QVariantMap normalized;
		QSet< QString > consumed;
		static const QRegularExpression safeKey(
			QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,63}$"));
		constexpr qsizetype MaxMetadataFields = 32;
		constexpr qsizetype MaxMetadataStringCharacters = 4096;
		constexpr qsizetype MaxMetadataListEntries = 8;
		constexpr qsizetype MaxSparklinePoints = 64;
		constexpr qsizetype MaxSocialContextEntries = 3;
		constexpr qsizetype MaxStructuredTextCharacters = 1024;
		constexpr qsizetype MaxMetadataUrlCharacters = 16384;
		constexpr qulonglong MaxMetadataSafeInteger = 9007199254740991ULL;
		const auto boundedText = [](const QVariant &field, const qsizetype maximum) {
			return field.toString().trimmed().left(maximum);
		};
		const auto isNumeric = [](const QVariant &field) {
			switch (field.metaType().id()) {
				case QMetaType::Int:
				case QMetaType::UInt:
				case QMetaType::LongLong:
				case QMetaType::ULongLong:
				case QMetaType::Double: return true;
				default: return false;
			}
		};
		const auto appendScalar = [&](const QString &key, const QVariant &field) {
			if (normalized.size() >= MaxMetadataFields || consumed.contains(key)
				|| !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			switch (field.metaType().id()) {
				case QMetaType::Bool:
				case QMetaType::Int:
				case QMetaType::UInt:
				case QMetaType::LongLong:
				case QMetaType::ULongLong: normalized.insert(key, field); break;
				case QMetaType::Double:
					if (std::isfinite(field.toDouble())) normalized.insert(key, field);
					break;
				case QMetaType::QString:
					normalized.insert(key, field.toString().left(MaxMetadataStringCharacters));
					break;
				default: break;
			}
		};
		const auto appendBoundedText = [&](const QString &key, const qsizetype maximum) {
			if (consumed.contains(key) || !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			const QVariant field = source.value(key);
			if (field.metaType().id() != QMetaType::QString) return;
			const QString text = boundedText(field, maximum);
			if (!text.isEmpty()) normalized.insert(key, text);
		};
		const auto appendBoolean = [&](const QString &key) {
			if (consumed.contains(key) || !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			const QVariant field = source.value(key);
			if (field.metaType().id() == QMetaType::Bool) normalized.insert(key, field.toBool());
		};
		const auto boundedUnsignedInteger = [&](const QVariant &field,
												 const qulonglong maximum) -> std::optional< qulonglong > {
			qulonglong value = 0;
			switch (field.metaType().id()) {
				case QMetaType::Int:
				case QMetaType::LongLong: {
					const qlonglong signedValue = field.toLongLong();
					if (signedValue < 0) return std::nullopt;
					value = static_cast< qulonglong >(signedValue);
					break;
				}
				case QMetaType::UInt:
				case QMetaType::ULongLong: value = field.toULongLong(); break;
				case QMetaType::Double: {
					const double numericValue = field.toDouble();
					if (!std::isfinite(numericValue) || numericValue < 0.0
						|| numericValue > static_cast< double >(maximum)
						|| std::floor(numericValue) != numericValue) {
						return std::nullopt;
					}
					value = static_cast< qulonglong >(numericValue);
					break;
				}
				default: return std::nullopt;
			}
			return value <= maximum ? std::optional< qulonglong >(value) : std::nullopt;
		};
		const auto appendUnsignedInteger = [&](const QString &key, const qulonglong maximum) {
			if (consumed.contains(key) || !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			const std::optional< qulonglong > value = boundedUnsignedInteger(source.value(key), maximum);
			if (value) normalized.insert(key, QVariant::fromValue(*value));
		};
		const auto appendWebUrl = [&](const QString &key) {
			if (consumed.contains(key) || !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			const QVariant field = source.value(key);
			if (field.metaType().id() != QMetaType::QString
				|| field.toString().trimmed().size() > MaxMetadataUrlCharacters) return;
			const QString safe = safeExternalUrl(field);
			const QString scheme = QUrl(safe).scheme().toLower();
			if (!safe.isEmpty() && (scheme == QLatin1String("http") || scheme == QLatin1String("https"))) {
				normalized.insert(key, safe);
			}
		};
		const auto appendManagedImage = [&](const QString &key) {
			if (consumed.contains(key) || !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			const QString image = safeImageSource(source.value(key));
			if (!image.isEmpty()) normalized.insert(key, image);
		};
		const auto consumeField = [&](const QString &key) { consumed.insert(key); };
		const auto appendStringList = [&](const QString &key) {
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			QVariantList result;
			for (const QVariant &entry : source.value(key).toList()) {
				const QString text = boundedText(entry, 256);
				if (!text.isEmpty() && !result.contains(text)) result.push_back(text);
				if (result.size() >= MaxMetadataListEntries) break;
			}
			consumed.insert(key);
			if (!result.isEmpty()) normalized.insert(key, result);
		};
		const auto appendSpecList = [&](const QString &key) {
			if (normalized.size() >= MaxMetadataFields || !source.contains(key)) return;
			QVariantList result;
			for (const QVariant &entry : source.value(key).toList()) {
				const QVariantMap item = entry.toMap();
				const QString label = boundedText(item.value(QStringLiteral("label")), 128);
				const QString valueText = boundedText(item.value(QStringLiteral("value")), 256);
				if (!label.isEmpty() && !valueText.isEmpty()) {
					result.push_back(QVariantMap { { QStringLiteral("label"), label },
											 { QStringLiteral("value"), valueText } });
				}
				if (result.size() >= MaxMetadataListEntries) break;
			}
			consumed.insert(key);
			if (!result.isEmpty()) normalized.insert(key, result);
		};
		const auto normalizedSocialPost = [&](const QVariant &entry) {
			const QVariantMap item = entry.toMap();
			QVariantMap result;
			for (const QString &key : { QStringLiteral("id"), QStringLiteral("displayName"),
					 QStringLiteral("handle"), QStringLiteral("text"), QStringLiteral("verified"),
					 QStringLiteral("createdAt"), QStringLiteral("replyCount"),
					 QStringLiteral("likeCount"), QStringLiteral("repostCount"),
					 QStringLiteral("quoteCount"), QStringLiteral("bookmarkCount"),
					 QStringLiteral("viewCount") }) {
				const QVariant field = item.value(key);
				if (field.metaType().id() == QMetaType::Bool || isNumeric(field)) {
					result.insert(key, field);
				} else if (field.metaType().id() == QMetaType::QString) {
					const QString text = boundedText(field, key == QLatin1String("text")
						? MaxStructuredTextCharacters : 256);
					if (!text.isEmpty()) result.insert(key, text);
				}
			}
			const QString url = safeExternalUrl(item.value(QStringLiteral("url")), true);
			if (!url.isEmpty()) result.insert(QStringLiteral("url"), url);
			return result;
		};
		const auto appendTwitchMetadata = [&] {
			for (const QString &key : { QStringLiteral("twitchKind"), QStringLiteral("twitchBadge"),
					 QStringLiteral("twitchChannel"), QStringLiteral("twitchVideoId"),
					 QStringLiteral("twitchClipSlug"), QStringLiteral("twitchCollectionId"),
					 QStringLiteral("twitchDisplayName"), QStringLiteral("twitchLiveState"),
					 QStringLiteral("twitchEmbedMode"), QStringLiteral("twitchStreamType"),
					 QStringLiteral("twitchGame"), QStringLiteral("twitchSuggestedVideoId"),
					 QStringLiteral("twitchSuggestedClipSlug") }) {
				appendBoundedText(key, 256);
			}
			for (const QString &key : { QStringLiteral("twitchPlaybackNote"),
					 QStringLiteral("twitchDisclaimer"), QStringLiteral("twitchMetadataFailure"),
					 QStringLiteral("twitchStateFailure") }) {
				appendBoundedText(key, MaxStructuredTextCharacters);
			}
			appendUnsignedInteger(QStringLiteral("twitchMetadataVersion"), 1000);
			appendUnsignedInteger(QStringLiteral("twitchViewerCount"), MaxMetadataSafeInteger);
			appendWebUrl(QStringLiteral("twitchSuggestedEmbedUrl"));
			// Remote provider images must be registered with QmlImagePipeline before
			// they can cross this boundary.
			appendManagedImage(QStringLiteral("twitchThumbnailUrl"));
		};
		const auto appendGitHubMetadata = [&] {
			appendStringList(QStringLiteral("githubTopics"));
			for (const QString &key : { QStringLiteral("githubOwner"), QStringLiteral("githubRepo"),
					 QStringLiteral("githubFullName"), QStringLiteral("githubLanguage"),
					 QStringLiteral("githubDefaultBranch"), QStringLiteral("githubPushedAt"),
					 QStringLiteral("githubOwnerLogin"), QStringLiteral("githubLicense"),
					 QStringLiteral("githubLatestReleaseTag"), QStringLiteral("githubLatestReleaseName"),
					 QStringLiteral("githubLatestReleasePublishedAt"),
					 QStringLiteral("githubLatestReleaseAssetName") }) {
				appendBoundedText(key, 256);
			}
			appendBoundedText(QStringLiteral("githubDescription"), MaxStructuredTextCharacters);
			appendBoundedText(QStringLiteral("githubLatestReleaseNotes"), MaxStructuredTextCharacters);
			for (const QString &key : { QStringLiteral("githubStars"), QStringLiteral("githubForks"),
					 QStringLiteral("githubOpenIssues"), QStringLiteral("githubLatestReleaseAssetCount"),
					 QStringLiteral("githubLatestReleaseDownloadCount") }) {
				appendUnsignedInteger(key, MaxMetadataSafeInteger);
			}
			for (const QString &key : { QStringLiteral("githubPrivate"), QStringLiteral("githubArchived"),
					 QStringLiteral("githubFork"), QStringLiteral("githubLatestReleaseLoading"),
					 QStringLiteral("githubLatestReleaseMissing"),
					 QStringLiteral("githubLatestReleasePrerelease") }) {
				appendBoolean(key);
			}
			for (const QString &key : { QStringLiteral("githubHtmlUrl"),
					 QStringLiteral("githubLatestReleaseUrl"),
					 QStringLiteral("githubLatestReleaseAssetUrl") }) {
				appendWebUrl(key);
			}
			appendManagedImage(QStringLiteral("githubOwnerAvatarUrl"));
		};
		const auto appendForumMetadata = [&] {
			// The producer mirrors first-post fields for compatibility. Keep one copy
			// so the 32-field budget is spent on distinct, rendered information.
			const auto appendPreferredPostText = [&](const QString &suffix, const qsizetype maximum) {
				const QString primary = QStringLiteral("forumPost%1").arg(suffix);
				const QString fallback = QStringLiteral("forumFirstPost%1").arg(suffix);
				if (source.contains(primary)) {
					appendBoundedText(primary, maximum);
					consumeField(fallback);
				} else if (source.contains(fallback) && !consumed.contains(primary)
						   && !consumed.contains(fallback)) {
					consumed.insert(primary);
					consumed.insert(fallback);
					const QVariant field = source.value(fallback);
					if (normalized.size() < MaxMetadataFields && field.metaType().id() == QMetaType::QString) {
						const QString text = boundedText(field, maximum);
						if (!text.isEmpty()) normalized.insert(primary, text);
					}
				}
			};
			const auto appendPreferredPostImage = [&](const QString &suffix) {
				const QString primary = QStringLiteral("forumPost%1").arg(suffix);
				const QString fallback = QStringLiteral("forumFirstPost%1").arg(suffix);
				if (source.contains(primary)) {
					appendManagedImage(primary);
					consumeField(fallback);
				} else if (source.contains(fallback) && !consumed.contains(primary)
						   && !consumed.contains(fallback)) {
					consumed.insert(primary);
					consumed.insert(fallback);
					const QString image = safeImageSource(source.value(fallback));
					if (normalized.size() < MaxMetadataFields && !image.isEmpty()) {
						normalized.insert(primary, image);
					}
				}
			};

			for (const QString &key : { QStringLiteral("forumProvider"),
					 QStringLiteral("forumThreadTitle"), QStringLiteral("forumLinkKind"),
					 QStringLiteral("forumCategory"), QStringLiteral("forumName"),
					 QStringLiteral("forumPage"), QStringLiteral("forumPageCount"),
					 QStringLiteral("forumPostCount") }) {
				appendBoundedText(key, key == QLatin1String("forumThreadTitle") ? 512 : 256);
			}
			if (source.contains(QStringLiteral("forumThreadId"))) {
				appendBoundedText(QStringLiteral("forumThreadId"), 128);
				consumeField(QStringLiteral("threadId"));
			} else {
				appendBoundedText(QStringLiteral("threadId"), 128);
			}
			if (source.contains(QStringLiteral("postId"))) {
				appendBoundedText(QStringLiteral("postId"), 128);
				consumeField(QStringLiteral("forumLinkedPostId"));
			} else {
				appendBoundedText(QStringLiteral("forumLinkedPostId"), 128);
			}
			appendWebUrl(QStringLiteral("forumThreadPostUrl"));
			for (const QString &suffix : { QStringLiteral("Id"), QStringLiteral("Time"),
					 QStringLiteral("Number"), QStringLiteral("Author"), QStringLiteral("AuthorTitle"),
					 QStringLiteral("AuthorRegistered"), QStringLiteral("AuthorPosts") }) {
				appendPreferredPostText(suffix, 256);
			}
			appendPreferredPostText(QStringLiteral("Excerpt"), MaxStructuredTextCharacters);
			appendPreferredPostImage(QStringLiteral("AuthorAvatarUrl"));
			appendBoundedText(QStringLiteral("forumQuoteAuthor"), 256);
			appendBoundedText(QStringLiteral("forumQuoteExcerpt"), MaxStructuredTextCharacters);
			appendBoundedText(QStringLiteral("forumQuotePostId"), 128);
			appendBoundedText(QStringLiteral("forumQuotePostNumber"), 64);
			appendWebUrl(QStringLiteral("forumQuotePostUrl"));
		};
		const auto appendInstagramMetadata = [&] {
			appendUnsignedInteger(QStringLiteral("instagramMetadataVersion"), 1000);
			appendUnsignedInteger(QStringLiteral("instagramMediaCount"), 16);
			appendBoundedText(QStringLiteral("instagramMediaKind"), 64);
			appendBoundedText(QStringLiteral("instagramDisplayName"), 256);
			appendBoundedText(QStringLiteral("instagramHandle"), 128);
			appendBoundedText(QStringLiteral("instagramCaption"), MaxStructuredTextCharacters);
			appendBoundedText(QStringLiteral("instagramCreatedAt"), 128);
			appendBoundedText(QStringLiteral("instagramOwnerUserId"), 128);
			appendUnsignedInteger(QStringLiteral("instagramLikeCount"), MaxMetadataSafeInteger);
			appendUnsignedInteger(QStringLiteral("instagramCommentCount"), MaxMetadataSafeInteger);
			appendManagedImage(QStringLiteral("instagramAvatarUrl"));
		};
		const auto appendFacebookMetadata = [&] {
			appendBoundedText(QStringLiteral("facebookMediaKind"), 64);
			appendBoundedText(QStringLiteral("facebookAuthor"), 256);
			appendBoundedText(QStringLiteral("facebookCaption"), MaxStructuredTextCharacters);
			appendBoundedText(QStringLiteral("facebookViews"), 64);
			appendBoundedText(QStringLiteral("facebookReactions"), 64);
		};
		const auto appendYouTubeMetadata = [&] {
			appendBoundedText(QStringLiteral("youtubeContentKind"), 64);
			appendBoundedText(QStringLiteral("youtubeAuthor"), 256);
			appendBoundedText(QStringLiteral("youtubeResolvedVideoId"), 32);
			appendUnsignedInteger(QStringLiteral("youtubeClipStartSeconds"), 24 * 60 * 60);
			appendUnsignedInteger(QStringLiteral("youtubeClipEndSeconds"), 24 * 60 * 60);
			appendUnsignedInteger(QStringLiteral("youtubeClipDurationSeconds"), 24 * 60 * 60);
		};
		const auto appendSteamMetadata = [&] {
			for (const QString &key : { QStringLiteral("steamAppId"), QStringLiteral("steamAppName"),
					 QStringLiteral("steamDeveloper"), QStringLiteral("steamGenres"),
					 QStringLiteral("steamReleaseDate"), QStringLiteral("steamPlatforms"),
					 QStringLiteral("steamPrice"), QStringLiteral("steamOriginalPrice"),
					 QStringLiteral("steamReviewSummary") }) {
				appendBoundedText(key, 256);
			}
			appendUnsignedInteger(QStringLiteral("steamDiscountPercent"), 100);
			appendUnsignedInteger(QStringLiteral("steamRecommendationsTotal"), MaxMetadataSafeInteger);
			appendUnsignedInteger(QStringLiteral("steamMetacriticScore"), 100);
			appendUnsignedInteger(QStringLiteral("steamReviewScore"), 100);
			appendUnsignedInteger(QStringLiteral("steamReviewTotal"), MaxMetadataSafeInteger);
			appendUnsignedInteger(QStringLiteral("steamReviewPositive"), MaxMetadataSafeInteger);
			appendUnsignedInteger(QStringLiteral("steamReviewNegative"), MaxMetadataSafeInteger);
			appendUnsignedInteger(QStringLiteral("steamReviewPercent"), 100);
			appendWebUrl(QStringLiteral("steamStoreUrl"));
			appendWebUrl(QStringLiteral("steamMetacriticUrl"));
			appendManagedImage(QStringLiteral("steamHeaderImage"));
			appendManagedImage(QStringLiteral("steamCapsuleImage"));
			consumeField(QStringLiteral("steamMediaItems"));
		};
		const auto appendVehicleMetadata = [&] {
			appendBoundedText(QStringLiteral("vehicleWarning"), 256);
			appendBoundedText(QStringLiteral("vehicleListingId"), 128);
			appendManagedImage(QStringLiteral("vehicleImage"));
		};
		const auto appendGoogleSearchMetadata = [&] {
			appendBoundedText(QStringLiteral("googleSearchQuery"), MaxStructuredTextCharacters);
			appendBoundedText(QStringLiteral("googleSearchMode"), 128);
			appendBoundedText(QStringLiteral("googleSearchModeLabel"), 128);
		};
		const auto appendLinkDigestMetadata = [&] {
			appendBoundedText(QStringLiteral("linkDigestTitle"), 512);
			appendBoundedText(QStringLiteral("linkDigestSource"), 256);
			appendBoundedText(QStringLiteral("linkDigestCaption"), MaxStructuredTextCharacters);
		};

		for (const QString &key : { QStringLiteral("provider"), QStringLiteral("previewProvider"),
				 QStringLiteral("providerName"), QStringLiteral("previewKind") }) {
			appendBoundedText(key, 128);
		}
		appendBoundedText(QStringLiteral("contentWarning"), 256);
		appendBoolean(QStringLiteral("thumbnailBlur"));
		// X metadata is normalized by the common scalar pass, but its avatar is a
		// managed image capability and must not lose priority to a noisy metadata
		// tail once backend hydration has replaced the remote source.
		appendManagedImage(QStringLiteral("xAvatarUrl"));

		const QString provider = source.value(QStringLiteral("provider"),
										 source.value(QStringLiteral("previewProvider")))
								 .toString()
								 .trimmed()
								 .toLower();
		const QString previewKind = source.value(QStringLiteral("previewKind")).toString().trimmed();
		if (provider == QLatin1String("twitch")) appendTwitchMetadata();
		if (provider == QLatin1String("github")) appendGitHubMetadata();
		if (provider == QLatin1String("flashback") || previewKind == QLatin1String("forum"))
			appendForumMetadata();
		if (provider == QLatin1String("instagram")) appendInstagramMetadata();
		if (provider == QLatin1String("facebook")) appendFacebookMetadata();
		if (provider == QLatin1String("youtube")) appendYouTubeMetadata();
		if (provider == QLatin1String("steam")) appendSteamMetadata();
		if (provider == QLatin1String("bytbil") || previewKind == QLatin1String("vehicleListing"))
			appendVehicleMetadata();
		if (provider == QLatin1String("google-search")) appendGoogleSearchMetadata();
		if (provider == QLatin1String("existenz") || previewKind == QLatin1String("linkDigest"))
			appendLinkDigestMetadata();

		// Only the structured fields below cross the frontend boundary. Each one is
		// shape-normalized and hard-bounded so a provider cannot grow the delegate tree.
		if (source.contains(QStringLiteral("financeSparkline"))) {
			QVariantList points;
			for (const QVariant &entry : source.value(QStringLiteral("financeSparkline")).toList()) {
				QVariantMap point;
				if (isNumeric(entry)) {
					const double close = entry.toDouble();
					if (std::isfinite(close)) point.insert(QStringLiteral("close"), close);
				} else {
					const QVariantMap item = entry.toMap();
					const QVariant closeValue = item.value(QStringLiteral("close"));
					if (isNumeric(closeValue) && std::isfinite(closeValue.toDouble())) {
						point.insert(QStringLiteral("close"), closeValue.toDouble());
						const QVariant timestamp = item.value(QStringLiteral("timestamp"));
						if (isNumeric(timestamp)) point.insert(QStringLiteral("timestamp"), timestamp);
					}
				}
				if (!point.isEmpty()) points.push_back(point);
				if (points.size() >= MaxSparklinePoints) break;
			}
			consumed.insert(QStringLiteral("financeSparkline"));
			if (!points.isEmpty()) normalized.insert(QStringLiteral("financeSparkline"), points);
		}
		for (const QString &key : { QStringLiteral("productSpecs"), QStringLiteral("listingSpecs"),
				 QStringLiteral("vehicleSpecs") }) appendSpecList(key);
		for (const QString &key : { QStringLiteral("gameStoreTags"), QStringLiteral("vehicleHighlights"),
				 QStringLiteral("githubTopics") }) appendStringList(key);
		if (source.contains(QStringLiteral("xQuotedPost")) && normalized.size() < MaxMetadataFields) {
			const QVariantMap post = normalizedSocialPost(source.value(QStringLiteral("xQuotedPost")));
			consumed.insert(QStringLiteral("xQuotedPost"));
			if (!post.isEmpty()) normalized.insert(QStringLiteral("xQuotedPost"), post);
		}
		if (source.contains(QStringLiteral("xReplyContext")) && normalized.size() < MaxMetadataFields) {
			const QVariantList sourceContext = source.value(QStringLiteral("xReplyContext")).toList();
			QVariantList context;
			const qsizetype first = std::max< qsizetype >(0, sourceContext.size() - MaxSocialContextEntries);
			for (qsizetype index = first; index < sourceContext.size(); ++index) {
				const QVariantMap post = normalizedSocialPost(sourceContext.at(index));
				if (!post.isEmpty()) context.push_back(post);
			}
			consumed.insert(QStringLiteral("xReplyContext"));
			if (!context.isEmpty()) normalized.insert(QStringLiteral("xReplyContext"), context);
		}
		// Sparse cached or test payloads may predate provider identity fields. The
		// second pass still applies the same typed normalization without allowing a
		// noisy unrelated provider to take precedence over its own rendered fields.
		appendTwitchMetadata();
		appendGitHubMetadata();
		appendForumMetadata();
		appendInstagramMetadata();
		appendFacebookMetadata();
		appendYouTubeMetadata();
		appendSteamMetadata();
		appendVehicleMetadata();
		appendGoogleSearchMetadata();
		appendLinkDigestMetadata();
		// Preserve the scalar fields rendered by the current card before filling
		// the bounded diagnostics tail in deterministic key order.
		static const QStringList priorityScalarKeys {
			QStringLiteral("contentWarning"), QStringLiteral("thumbnailBlur"), QStringLiteral("provider"),
			QStringLiteral("previewProvider"), QStringLiteral("providerName"), QStringLiteral("previewKind"),
			QStringLiteral("statusLabel"), QStringLiteral("locationLabel"),
			QStringLiteral("tickerSymbol"), QStringLiteral("financeName"), QStringLiteral("financePrice"),
			QStringLiteral("financeCurrency"), QStringLiteral("financeDayChange"),
			QStringLiteral("financeDayChangePercent"), QStringLiteral("financeDayTrend"),
			QStringLiteral("financeRangeLabel"), QStringLiteral("financeRangeChange"),
			QStringLiteral("financeRangeChangePercent"), QStringLiteral("financeRangeTrend"),
			QStringLiteral("financeExchange"), QStringLiteral("financeInstrument"),
			QStringLiteral("financeUpdatedAt"),
			QStringLiteral("productPrice"), QStringLiteral("productOriginalPrice"),
			QStringLiteral("productDiscount"), QStringLiteral("productAvailability"),
			QStringLiteral("productDelivery"), QStringLiteral("productRating"),
			QStringLiteral("productReviewCount"), QStringLiteral("productBrand"),
			QStringLiteral("productSku"), QStringLiteral("productId"), QStringLiteral("productVolume"),
			QStringLiteral("productAlcohol"),
			QStringLiteral("gameStorePrice"), QStringLiteral("gameStoreOriginalPrice"),
			QStringLiteral("gameStoreDiscount"), QStringLiteral("gameStoreAvailability"),
			QStringLiteral("gameStoreRating"), QStringLiteral("gameStoreReviewCount"),
			QStringLiteral("gameStorePlatform"), QStringLiteral("gameStoreBrand"),
			QStringLiteral("steamPrice"), QStringLiteral("steamOriginalPrice"),
			QStringLiteral("steamDiscountPercent"), QStringLiteral("steamDeveloper"),
			QStringLiteral("steamReleaseDate"), QStringLiteral("steamPlatforms"),
			QStringLiteral("steamGenres"), QStringLiteral("steamReviewSummary"),
			QStringLiteral("steamReviewPercent"), QStringLiteral("steamRecommendationsTotal"),
			QStringLiteral("steamMetacriticScore"),
			QStringLiteral("listingPrice"), QStringLiteral("listingOriginalPrice"),
			QStringLiteral("listingCondition"), QStringLiteral("listingLocation"),
			QStringLiteral("listingSaleType"), QStringLiteral("listingEndsAt"),
			QStringLiteral("listingId"),
			QStringLiteral("vehiclePrice"), QStringLiteral("vehiclePriceExVat"),
			QStringLiteral("vehicleKind"), QStringLiteral("vehicleYear"), QStringLiteral("vehicleMileage"),
			QStringLiteral("vehicleFuel"), QStringLiteral("vehicleTransmission"),
			QStringLiteral("vehicleDealer"), QStringLiteral("vehicleLocation"),
			QStringLiteral("realEstatePrice"), QStringLiteral("realEstateArea"),
			QStringLiteral("realEstateRooms"), QStringLiteral("realEstateFee"),
			QStringLiteral("articleSection"), QStringLiteral("articleAuthor"),
			QStringLiteral("articlePublishedAt"), QStringLiteral("articleModifiedAt"),
			QStringLiteral("articleAccess"), QStringLiteral("articlePremium"),
			QStringLiteral("articlePublisher"),
			QStringLiteral("forumProvider"), QStringLiteral("forumThreadId"), QStringLiteral("threadId"),
			QStringLiteral("forumPostAuthor"), QStringLiteral("forumFirstPostAuthor"),
			QStringLiteral("forumPostTime"), QStringLiteral("forumFirstPostTime"),
			QStringLiteral("forumPostCount"), QStringLiteral("forumQuoteAuthor"),
			QStringLiteral("audioProvider"), QStringLiteral("audioProgram"),
			QStringLiteral("xDisplayName"), QStringLiteral("xHandle"), QStringLiteral("xVerified"),
			QStringLiteral("xCreatedAt"), QStringLiteral("xReplyCount"), QStringLiteral("xRepostCount"),
			QStringLiteral("xQuoteCount"), QStringLiteral("xLikeCount"), QStringLiteral("xViewCount"),
			QStringLiteral("xBookmarkCount"),
			QStringLiteral("instagramHandle"), QStringLiteral("instagramLikeCount"),
			QStringLiteral("instagramCommentCount"), QStringLiteral("instagramMediaKind"),
			QStringLiteral("instagramCreatedAt"),
			QStringLiteral("githubRepo"), QStringLiteral("githubStars"), QStringLiteral("githubForks"),
			QStringLiteral("githubOpenIssues"), QStringLiteral("githubLanguage"),
			QStringLiteral("githubLicense"), QStringLiteral("githubDefaultBranch"),
			QStringLiteral("githubPushedAt"), QStringLiteral("githubPrivate"),
			QStringLiteral("githubArchived"), QStringLiteral("githubFork")
		};
		for (const QString &key : priorityScalarKeys) {
			const auto it = source.constFind(key);
			if (it != source.cend()) appendScalar(it.key(), it.value());
		}
		for (auto it = source.cbegin(); it != source.cend() && normalized.size() < MaxMetadataFields; ++it) {
			if (consumed.contains(it.key())) continue;
			if (it.key().endsWith(QLatin1String("Image"), Qt::CaseInsensitive)
				|| (it.key().endsWith(QLatin1String("Url"), Qt::CaseInsensitive)
					&& (it.key().contains(QLatin1String("avatar"), Qt::CaseInsensitive)
						|| it.key().contains(QLatin1String("thumbnail"), Qt::CaseInsensitive)))) {
				appendManagedImage(it.key());
				continue;
			}
			if (it.key().endsWith(QLatin1String("Url"), Qt::CaseInsensitive)) {
				appendWebUrl(it.key());
				continue;
			}
			if (it.key().contains(QLatin1String("html"), Qt::CaseInsensitive)) {
				consumeField(it.key());
				continue;
			}
			appendScalar(it.key(), it.value());
		}
		return normalized;
	}

	QVariantMap normalizedPreview(const QVariant &value) {
		const QVariantMap preview = value.toMap();
		if (preview.isEmpty()) return {};

		QVariantMap normalized;
		static const QStringList textFields {
			QStringLiteral("kind"), QStringLiteral("title"), QStringLiteral("subtitle"),
			QStringLiteral("description"), QStringLiteral("host"), QStringLiteral("openLabel"),
			QStringLiteral("errorDescription"), QStringLiteral("errorMessage"), QStringLiteral("error"),
			QStringLiteral("loadingLabel"), QStringLiteral("embedKind"), QStringLiteral("embedAspect"),
			QStringLiteral("previewSize"), QStringLiteral("mediaKind"),
			QStringLiteral("presentationFamily"), QStringLiteral("caseVariant")
		};
		for (const QString &field : textFields) {
			if (preview.contains(field)) normalized.insert(field, preview.value(field).toString().left(4096));
		}
		const QString url = safeExternalUrl(preview.value(QStringLiteral("url")), true);
		if (!url.isEmpty()) normalized.insert(QStringLiteral("url"), url);
		const QString thumbnailUrl = safeImageSource(preview.value(QStringLiteral("thumbnailUrl")));
		if (!thumbnailUrl.isEmpty()) normalized.insert(QStringLiteral("thumbnailUrl"), thumbnailUrl);
		const QString mediaMime = normalizedMediaMime(preview.value(QStringLiteral("mediaMime")));
		const QString mediaKind = preview.value(QStringLiteral("mediaKind")).toString().trimmed().toLower();
		QString mediaUrl;
		const bool mediaAnimated = preview.value(QStringLiteral("mediaAnimated")).toBool();
		if (mediaMime.startsWith(QLatin1String("image/")) || mediaKind == QLatin1String("image")
			|| mediaKind == QLatin1String("gif")) {
			mediaUrl = safeImageSource(preview.value(QStringLiteral("mediaUrl")));
			if (mediaUrl.isEmpty() && mediaAnimated) {
				mediaUrl = safeManagedAnimatedImageSource(preview.value(QStringLiteral("mediaUrl")));
			}
		} else if (mediaKind == QLatin1String("audio") || mediaMime.startsWith(QLatin1String("audio/"))) {
			mediaUrl = safeDirectMediaSource(preview.value(QStringLiteral("mediaUrl")), mediaMime, true);
		} else {
			mediaUrl = safeDirectMediaSource(preview.value(QStringLiteral("mediaUrl")), mediaMime, false);
		}
		if (!mediaMime.isEmpty()) normalized.insert(QStringLiteral("mediaMime"), mediaMime);
		if (!mediaUrl.isEmpty()) normalized.insert(QStringLiteral("mediaUrl"), mediaUrl);
		if (mediaAnimated && !mediaUrl.isEmpty() && QUrl(mediaUrl).isLocalFile()) {
			normalized.insert(QStringLiteral("mediaAnimated"), true);
		}
		const QString mediaExternalUrl = safeHttpsMediaUrl(
			preview.contains(QStringLiteral("mediaExternalUrl"))
				? preview.value(QStringLiteral("mediaExternalUrl"))
				: preview.value(QStringLiteral("mediaUrl")));
		if (!mediaExternalUrl.isEmpty() && (mediaKind == QLatin1String("image")
			|| mediaKind == QLatin1String("gif") || mediaMime.startsWith(QLatin1String("image/")))) {
			normalized.insert(QStringLiteral("mediaExternalUrl"), mediaExternalUrl);
		}
		const QString mediaAudioMime = normalizedMediaMime(preview.value(QStringLiteral("mediaAudioMime")));
		const QString mediaAudioUrl =
			safeDirectMediaSource(preview.value(QStringLiteral("mediaAudioUrl")), mediaAudioMime, true);
		if (!mediaAudioMime.isEmpty()) normalized.insert(QStringLiteral("mediaAudioMime"), mediaAudioMime);
		if (!mediaAudioUrl.isEmpty()) normalized.insert(QStringLiteral("mediaAudioUrl"), mediaAudioUrl);
		const QString embedUrl = safeExternalUrl(preview.value(QStringLiteral("embedUrl")));
		if (!embedUrl.isEmpty()) normalized.insert(QStringLiteral("embedUrl"), embedUrl);

		QVariantList mediaItems;
		QSet< QString > seenMediaUrls;
		for (const QVariant &entry : preview.value(QStringLiteral("mediaItems")).toList()) {
			if (mediaItems.size() >= MaxPreviewMediaItemCount) break;
			const QVariantMap item = entry.toMap();
			if (item.isEmpty()) continue;
			QString kind = item.value(QStringLiteral("kind")).toString().trimmed().toLower();
			const QString itemMime = normalizedMediaMime(item.value(QStringLiteral("mime")));
			if (kind.isEmpty()) {
				kind = itemMime.startsWith(QLatin1String("image/")) ? QStringLiteral("image")
					 : itemMime.startsWith(QLatin1String("audio/")) ? QStringLiteral("audio")
															 : QStringLiteral("video");
			}
			QString itemUrl;
			QString itemExternalUrl;
			bool directPlayable = false;
			const bool imageItem = kind == QLatin1String("image") || kind == QLatin1String("gif")
				|| itemMime.startsWith(QLatin1String("image/"));
			if (imageItem) {
				kind = QStringLiteral("image");
				itemUrl = safeImageSource(item.value(QStringLiteral("url")));
				const bool managedAnimated = item.value(QStringLiteral("managedAnimated")).toBool();
				if (itemUrl.isEmpty() && managedAnimated) {
					itemUrl = safeManagedAnimatedImageSource(item.value(QStringLiteral("url")));
				}
				itemExternalUrl = safeHttpsMediaUrl(
					item.contains(QStringLiteral("externalUrl")) ? item.value(QStringLiteral("externalUrl"))
																 : item.value(QStringLiteral("url")));
			}
			else {
				itemUrl = safeDirectMediaSource(item.value(QStringLiteral("url")), itemMime,
												kind == QLatin1String("audio"));
				directPlayable = !itemUrl.isEmpty();
				if (itemUrl.isEmpty()) itemUrl = safeHttpsMediaUrl(item.value(QStringLiteral("url")));
			}
			const QString thumbnail = safeImageSource(item.value(QStringLiteral("thumbnail")));
			const QString poster = safeImageSource(item.value(QStringLiteral("poster")));
			const QString itemIdentity = !itemUrl.isEmpty() ? itemUrl : itemExternalUrl;
			if (itemIdentity.isEmpty() || seenMediaUrls.contains(itemIdentity)) continue;
			seenMediaUrls.insert(itemIdentity);
			QVariantMap normalizedItem {
				{ QStringLiteral("kind"), kind.left(32) },
				{ QStringLiteral("mime"), itemMime },
				{ QStringLiteral("title"), item.value(QStringLiteral("title")).toString().left(1024) },
				{ QStringLiteral("directPlayable"), imageItem ? !itemUrl.isEmpty()
																							 : directPlayable }
			};
			if (!itemUrl.isEmpty()) normalizedItem.insert(QStringLiteral("url"), itemUrl);
			if (item.value(QStringLiteral("managedAnimated")).toBool() && QUrl(itemUrl).isLocalFile()) {
				normalizedItem.insert(QStringLiteral("managedAnimated"), true);
			}
			if (!itemExternalUrl.isEmpty()) normalizedItem.insert(QStringLiteral("externalUrl"), itemExternalUrl);
			if (!thumbnail.isEmpty()) normalizedItem.insert(QStringLiteral("thumbnail"), thumbnail);
			if (!poster.isEmpty()) normalizedItem.insert(QStringLiteral("poster"), poster);
			const QString contentBranch =
				item.value(QStringLiteral("contentBranch")).toString().trimmed().toLower().left(64);
			const QString mediaPresentation =
				item.value(QStringLiteral("mediaPresentation")).toString().trimmed().toLower().left(64);
			if (!contentBranch.isEmpty()) {
				normalizedItem.insert(QStringLiteral("contentBranch"), contentBranch);
			}
			if (!mediaPresentation.isEmpty()) {
				normalizedItem.insert(QStringLiteral("mediaPresentation"), mediaPresentation);
			}
			QString streamKind = item.value(QStringLiteral("streamKind")).toString().trimmed().toLower();
			const bool hlsManifest = itemMime == QLatin1String("application/vnd.apple.mpegurl");
			const bool dashManifest = itemMime == QLatin1String("application/dash+xml");
			if (hlsManifest) streamKind = QStringLiteral("hls");
			else if (dashManifest) streamKind = QStringLiteral("dash");
			else if (streamKind != QLatin1String("direct")) streamKind.clear();
			if (!streamKind.isEmpty()) normalizedItem.insert(QStringLiteral("streamKind"), streamKind);
			mediaItems.push_back(normalizedItem);
		}
		if (!mediaItems.isEmpty()) normalized.insert(QStringLiteral("mediaItems"), mediaItems);

		const bool unresolvedStub = preview.contains(QStringLiteral("loadingLabel"))
			&& !preview.contains(QStringLiteral("loading")) && !preview.contains(QStringLiteral("failed"));
		const bool loading = preview.value(QStringLiteral("loading"), unresolvedStub).toBool();
		const bool failed = preview.value(QStringLiteral("failed")).toBool();
		normalized.insert(QStringLiteral("loading"), loading && !failed);
		normalized.insert(QStringLiteral("failed"), failed);
		normalized.insert(QStringLiteral("state"),
					  failed ? QStringLiteral("error") : loading ? QStringLiteral("loading")
														 : QStringLiteral("ready"));
		normalized.insert(QStringLiteral("singleUrl"), preview.value(QStringLiteral("singleUrl")).toBool());
		normalized.insert(QStringLiteral("autoplay"), preview.value(QStringLiteral("autoplay")).toBool());
		normalized.insert(QStringLiteral("reserveEmbedGeometry"),
						  preview.value(QStringLiteral("reserveEmbedGeometry")).toBool());
		const QVariantMap metadata = normalizedPreviewMetadata(preview.value(QStringLiteral("metadata")));
		if (!metadata.isEmpty()) normalized.insert(QStringLiteral("metadata"), metadata);
		const QVariantMap document = EmbedDocument::fromNormalizedPreview(normalized);
		if (!document.isEmpty()) normalized.insert(QStringLiteral("document"), document);
		return normalized;
	}

	QVariantList normalizedAttachments(const QVariant &value) {
		QVariantList normalized;
		static const QRegularExpression safeInlineToken(QStringLiteral("^[0-9a-f]{24}$"));
		for (const QVariant &entry : value.toList()) {
			if (normalized.size() >= MaxAttachmentCount) break;
			const QVariantMap attachment = entry.toMap();
			if (attachment.isEmpty()) continue;
			const QString source = safeImageSource(attachment.value(QStringLiteral("url")));
			const QString thumbnail = safeImageSource(attachment.value(QStringLiteral("thumbnailUrl")));
			const QVariant rawAssetID = attachment.contains(QStringLiteral("assetId"))
				? attachment.value(QStringLiteral("assetId")) : attachment.value(QStringLiteral("assetID"));
			bool validAssetID = false;
			const qulonglong assetID = rawAssetID.toString().trimmed().toULongLong(&validAssetID);
			validAssetID = validAssetID && assetID > 0
				&& assetID <= std::numeric_limits< unsigned int >::max();
			const QString inlineToken = attachment.value(QStringLiteral("inlineToken")).toString().trimmed().toLower();
			const bool validInlineToken = safeInlineToken.match(inlineToken).hasMatch();
			if (source.isEmpty() && thumbnail.isEmpty() && !validAssetID && !validInlineToken) continue;
			QString kind = attachment.value(QStringLiteral("kind")).toString().trimmed().toLower().left(64);
			const QString mime = attachment.value(QStringLiteral("mime")).toString().trimmed().toLower().left(128);
			if (kind.isEmpty()) {
				kind = mime.startsWith(QLatin1String("image/")) ? QStringLiteral("image")
					 : mime.startsWith(QLatin1String("video/")) ? QStringLiteral("video")
					 : mime.startsWith(QLatin1String("audio/")) ? QStringLiteral("audio")
					 : QStringLiteral("file");
			}
			const QString fileName = attachment.value(
				QStringLiteral("fileName"), attachment.value(QStringLiteral("name"))).toString().left(1024);
			QString state = attachment.value(QStringLiteral("state"), QStringLiteral("ready"))
				.toString().trimmed().toLower();
			if (state != QLatin1String("ready") && state != QLatin1String("loading")
				&& state != QLatin1String("error")) {
				state = QStringLiteral("ready");
			}
			const bool previewCanRetry = validAssetID && kind == QLatin1String("image")
				&& state == QLatin1String("error")
				&& attachment.value(QStringLiteral("previewCanRetry")).toBool();
			QString previewErrorCode = attachment.value(QStringLiteral("previewErrorCode"))
				.toString().trimmed().toLower().left(64);
			static const QRegularExpression safePreviewErrorCode(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
			if (!safePreviewErrorCode.match(previewErrorCode).hasMatch()) previewErrorCode.clear();
			QVariantMap item {
				{ QStringLiteral("id"), attachment.value(QStringLiteral("id"), rawAssetID).toString().left(512) },
				{ QStringLiteral("kind"), kind },
				{ QStringLiteral("name"), fileName },
				{ QStringLiteral("fileName"), fileName },
				{ QStringLiteral("mime"), mime },
				{ QStringLiteral("alt"), attachment.value(QStringLiteral("alt")).toString().left(4096) },
				{ QStringLiteral("url"), source },
				{ QStringLiteral("thumbnailUrl"), thumbnail.isEmpty() ? source : thumbnail },
				{ QStringLiteral("state"), state },
				{ QStringLiteral("previewCanRetry"), previewCanRetry },
				{ QStringLiteral("previewError"), attachment.value(QStringLiteral("previewError"))
					.toString().trimmed().left(512) }
			};
			if (!previewErrorCode.isEmpty()) {
				item.insert(QStringLiteral("previewErrorCode"), previewErrorCode);
			}
			if (validAssetID) item.insert(QStringLiteral("assetId"), QVariant::fromValue(assetID));
			if (validInlineToken) item.insert(QStringLiteral("inlineToken"), inlineToken);
			if (attachment.contains(QStringLiteral("byteSize"))) {
				item.insert(QStringLiteral("byteSize"), attachment.value(QStringLiteral("byteSize")).toULongLong());
			} else if (attachment.contains(QStringLiteral("size"))) {
				item.insert(QStringLiteral("byteSize"), attachment.value(QStringLiteral("size")).toULongLong());
			}
			if (attachment.contains(QStringLiteral("width"))) item.insert(QStringLiteral("width"), attachment.value(QStringLiteral("width")).toInt());
			if (attachment.contains(QStringLiteral("height"))) item.insert(QStringLiteral("height"), attachment.value(QStringLiteral("height")).toInt());
			if (attachment.contains(QStringLiteral("durationMs"))) {
				item.insert(QStringLiteral("durationMs"), attachment.value(QStringLiteral("durationMs")).toULongLong());
			}
			const QVariantMap document = EmbedDocument::fromNormalizedAttachment(item);
			if (!document.isEmpty()) item.insert(QStringLiteral("document"), document);
			normalized.push_back(item);
		}
		return normalized;
	}

	QVariantList normalizedReactions(const QVariant &value) {
		QVariantList normalized;
		QSet< QString > seenEmoji;
		for (const QVariant &entry : value.toList()) {
			if (normalized.size() >= MaxReactionCount) break;
			const QVariantMap reaction = entry.toMap();
			const QString emoji = reaction.value(QStringLiteral("emoji")).toString().trimmed().left(
				MaxReactionEmojiCharacters);
			if (emoji.isEmpty() || seenEmoji.contains(emoji)) continue;
			seenEmoji.insert(emoji);

			QVariantList actorNames;
			QSet< QString > seenActors;
			for (const QVariant &actorEntry : reaction.value(QStringLiteral("actorNames")).toList()) {
				if (actorNames.size() >= MaxReactionActorCount) break;
				const QString actorName = actorEntry.toString().trimmed().left(MaxReactionActorNameCharacters);
				if (actorName.isEmpty() || seenActors.contains(actorName)) continue;
				seenActors.insert(actorName);
				actorNames.push_back(actorName);
			}

			const qulonglong count = std::min< qulonglong >(
				reaction.value(QStringLiteral("count")).toULongLong(),
				std::numeric_limits< unsigned int >::max());
			normalized.push_back(QVariantMap {
				{ QStringLiteral("emoji"), emoji },
				{ QStringLiteral("count"), count },
				{ QStringLiteral("selfReacted"), reaction.value(QStringLiteral("selfReacted")).toBool() },
				{ QStringLiteral("actorNames"), actorNames }
			});
		}
		return normalized;
	}
}

namespace ModernMotd {
	namespace {
		constexpr int MaxStoredServerStates = 64;

		QJsonObject decodedServerStates(const QString &serializedStates) {
			QJsonParseError error;
			const QJsonDocument document = QJsonDocument::fromJson(serializedStates.toUtf8(), &error);
			if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
			return document.object().value(QStringLiteral("servers")).toObject();
		}

		QString boundedSignature(const QJsonValue &value) {
			return value.toString().trimmed().left(256);
		}
	}

	QString serverStateKey(const QByteArray &serverDigest, const QString &host, const quint16 port) {
		QByteArray identity;
		if (!serverDigest.isEmpty()) {
			identity = QByteArrayLiteral("digest\0") + serverDigest;
		} else {
			const QString normalizedHost = host.trimmed().toLower();
			if (normalizedHost.isEmpty() || port == 0) return {};
			identity = QByteArrayLiteral("endpoint\0") + normalizedHost.toUtf8()
				+ QByteArrayLiteral("\0") + QByteArray::number(port);
		}
		return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
	}

	QVariantMap serverViewState(const QString &serializedStates, const QString &serverKey) {
		QVariantMap state {
			{ QStringLiteral("exists"), false },
			{ QStringLiteral("expanded"), false },
			{ QStringLiteral("dismissedSignature"), QString() },
			{ QStringLiteral("lastSeenSignature"), QString() }
		};
		const QString normalizedKey = serverKey.trimmed();
		if (normalizedKey.isEmpty()) return state;

		const QJsonObject servers = decodedServerStates(serializedStates);
		const QJsonValue entryValue = servers.value(normalizedKey);
		if (!entryValue.isObject()) return state;
		const QJsonObject entry = entryValue.toObject();
		state.insert(QStringLiteral("exists"), true);
		state.insert(QStringLiteral("expanded"), entry.value(QStringLiteral("expanded")).toBool());
		state.insert(QStringLiteral("dismissedSignature"),
					 boundedSignature(entry.value(QStringLiteral("dismissedSignature"))));
		state.insert(QStringLiteral("lastSeenSignature"),
					 boundedSignature(entry.value(QStringLiteral("lastSeenSignature"))));
		return state;
	}

	QString withServerViewState(const QString &serializedStates, const QString &serverKey,
							const QVariantMap &state) {
		const QString normalizedKey = serverKey.trimmed();
		if (normalizedKey.isEmpty()) return serializedStates;

		QJsonObject servers = decodedServerStates(serializedStates);
		QJsonObject entry;
		entry.insert(QStringLiteral("expanded"), state.value(QStringLiteral("expanded")).toBool());
		entry.insert(QStringLiteral("dismissedSignature"),
					 state.value(QStringLiteral("dismissedSignature")).toString().trimmed().left(256));
		entry.insert(QStringLiteral("lastSeenSignature"),
					 state.value(QStringLiteral("lastSeenSignature")).toString().trimmed().left(256));
		entry.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
		servers.insert(normalizedKey, entry);

		if (servers.size() > MaxStoredServerStates) {
			QList< QPair< QString, qint64 > > byAge;
			byAge.reserve(servers.size());
			const QJsonObject &serverEntries = servers;
			for (auto it = serverEntries.begin(); it != serverEntries.end(); ++it) {
				byAge.push_back(qMakePair(it.key(), static_cast< qint64 >(
					it.value().toObject().value(QStringLiteral("updatedAt")).toVariant().toLongLong())));
			}
			std::sort(byAge.begin(), byAge.end(), [](const auto &lhs, const auto &rhs) {
				return lhs.second == rhs.second ? lhs.first < rhs.first : lhs.second < rhs.second;
			});
			for (int index = 0; index < byAge.size() - MaxStoredServerStates; ++index) {
				servers.remove(byAge.at(index).first);
			}
		}

		QJsonObject root;
		root.insert(QStringLiteral("version"), 1);
		root.insert(QStringLiteral("servers"), servers);
		return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
	}

	QVariantList documentBlocks(const QString &html) {
		const QString bounded = html.left(MaxRichBodyCharacters);
		if (bounded.trimmed().isEmpty()) return {};
		const QString plainText =
			QTextDocumentFragment::fromHtml(bounded).toPlainText().left(MaxRichBodyCharacters);
		return structuredMotdDocument(bounded, plainText).blocks;
	}
}

ClientSessionController::ClientSessionController(QObject *parent) : QObject(parent) {
}

QString ClientSessionController::serverName() const { return m_serverName; }
QString ClientSessionController::serverMonogram() const { return m_serverMonogram; }
QString ClientSessionController::serverImageUrl() const { return m_serverImageUrl; }
QString ClientSessionController::connectionLabel() const { return m_connectionLabel; }
QString ClientSessionController::selfStatusLabel() const { return m_selfStatusLabel; }
QString ClientSessionController::connectionState() const { return m_connectionState; }
QString ClientSessionController::connectionTone() const { return m_connectionTone; }
QString ClientSessionController::connectionDetail() const { return m_connectionDetail; }
int ClientSessionController::connectionRetryRemainingMs() const { return m_connectionRetryRemainingMs; }
bool ClientSessionController::canConnect() const { return m_canConnect; }
bool ClientSessionController::canCancel() const { return m_canCancel; }
QString ClientSessionController::selfName() const { return m_selfName; }
bool ClientSessionController::connected() const { return m_connected; }
bool ClientSessionController::selfMuted() const { return m_selfMuted; }
bool ClientSessionController::selfDeafened() const { return m_selfDeafened; }
QVariantList ClientSessionController::appMenus() const { return m_appMenus; }
QVariantMap ClientSessionController::selfMenu() const { return m_selfMenu; }
QVariantMap ClientSessionController::updateBanner() const { return m_updateBanner; }
QVariantMap ClientSessionController::stonks() const { return m_stonks; }
QStringList ClientSessionController::collapsedNavigationSections() const {
	return m_collapsedNavigationSections;
}
QString ClientSessionController::motdHtml() const { return m_motdHtml; }
QVariantList ClientSessionController::motdSegments() const { return m_motdSegments; }
QVariantList ClientSessionController::motdBlocks() const { return m_motdBlocks; }
QString ClientSessionController::motdSummary() const { return m_motdSummary; }
bool ClientSessionController::hasMotd() const { return m_hasMotd; }
bool ClientSessionController::motdExpanded() const { return m_motdExpanded; }
bool ClientSessionController::motdDismissed() const { return m_motdDismissed; }
QString ClientSessionController::motdSignature() const { return m_motdSignature; }
QString ClientSessionController::motdDismissedSignature() const { return m_motdDismissedSignature; }
QString ClientSessionController::motdLastSeenSignature() const { return m_motdLastSeenSignature; }
bool ClientSessionController::motdChanged() const { return m_motdChanged; }
QVariantList ClientSessionController::motdActions() const { return m_motdActions; }

#define SET_VALUE(member, signalName) \
	if (!acceptsFrontendStateMutation(this)) { \
		return; \
	} \
	if (member == value) { \
		return; \
	} \
	member = value; \
	emit signalName()

void ClientSessionController::setServerName(const QString &value) { SET_VALUE(m_serverName, serverNameChanged); }
void ClientSessionController::setServerMonogram(const QString &value) {
	const QString accepted = value.trimmed().left(12);
	if (!acceptsFrontendStateMutation(this) || m_serverMonogram == accepted) return;
	m_serverMonogram = accepted;
	emit serverMonogramChanged();
}
void ClientSessionController::setServerImageUrl(const QString &value) {
	const QString candidate = value.trimmed();
	const QString accepted = candidate.startsWith(QLatin1String("image://mumble/"), Qt::CaseInsensitive)
		? candidate : QString();
	if (!acceptsFrontendStateMutation(this) || m_serverImageUrl == accepted) return;
	m_serverImageUrl = accepted;
	emit serverImageUrlChanged();
}
void ClientSessionController::setConnectionLabel(const QString &value) {
	SET_VALUE(m_connectionLabel, connectionLabelChanged);
}
void ClientSessionController::setSelfStatusLabel(const QString &value) {
	SET_VALUE(m_selfStatusLabel, selfStatusLabelChanged);
}
void ClientSessionController::setConnectionState(const QString &value) {
	const QString normalized = value.trimmed().toLower();
	const QString accepted = normalized.isEmpty() ? QStringLiteral("disconnected") : normalized;
	if (!acceptsFrontendStateMutation(this) || m_connectionState == accepted) return;
	m_connectionState = accepted;
	emit connectionStateChanged();
}
void ClientSessionController::setConnectionTone(const QString &value) {
	const QString normalized = value.trimmed().toLower();
	if (!acceptsFrontendStateMutation(this) || m_connectionTone == normalized) return;
	m_connectionTone = normalized;
	emit connectionToneChanged();
}
void ClientSessionController::setConnectionDetail(const QString &value) {
	SET_VALUE(m_connectionDetail, connectionDetailChanged);
}
void ClientSessionController::setConnectionRetryRemainingMs(const int value) {
	const int accepted = qMax(0, value);
	if (!acceptsFrontendStateMutation(this) || m_connectionRetryRemainingMs == accepted) return;
	m_connectionRetryRemainingMs = accepted;
	emit connectionRetryRemainingMsChanged();
}
void ClientSessionController::setCanConnect(const bool value) { SET_VALUE(m_canConnect, canConnectChanged); }
void ClientSessionController::setCanCancel(const bool value) { SET_VALUE(m_canCancel, canCancelChanged); }
void ClientSessionController::setSelfName(const QString &value) { SET_VALUE(m_selfName, selfNameChanged); }
void ClientSessionController::setConnected(bool value) { SET_VALUE(m_connected, connectedChanged); }
void ClientSessionController::setSelfMuted(bool value) { SET_VALUE(m_selfMuted, selfMutedChanged); }
void ClientSessionController::setSelfDeafened(bool value) { SET_VALUE(m_selfDeafened, selfDeafenedChanged); }
void ClientSessionController::setAppMenus(const QVariantList &value) { SET_VALUE(m_appMenus, appMenusChanged); }
void ClientSessionController::setSelfMenu(const QVariantMap &value) { SET_VALUE(m_selfMenu, selfMenuChanged); }
void ClientSessionController::setUpdateBanner(const QVariantMap &value) { SET_VALUE(m_updateBanner, updateBannerChanged); }
void ClientSessionController::setStonks(const QVariantMap &value) { SET_VALUE(m_stonks, stonksChanged); }
void ClientSessionController::setCollapsedNavigationSections(const QStringList &value) {
	static const QStringList supportedSections {
		QStringLiteral("voice"), QStringLiteral("text"), QStringLiteral("direct"), QStringLiteral("tool")
	};
	QSet< QString > requestedSections;
	for (const QString &section : value) {
		const QString normalized = section.trimmed().toLower();
		if (supportedSections.contains(normalized)) requestedSections.insert(normalized);
	}
	QStringList accepted;
	for (const QString &section : supportedSections) {
		if (requestedSections.contains(section)) accepted.push_back(section);
	}
	if (!acceptsFrontendStateMutation(this) || m_collapsedNavigationSections == accepted) return;
	m_collapsedNavigationSections = accepted;
	emit collapsedNavigationSectionsChanged();
}
void ClientSessionController::setNavigationSectionExpanded(const QString &sectionKind, const bool expanded) {
	const QString normalized = sectionKind.trimmed().toLower();
	static const QSet< QString > supportedSections {
		QStringLiteral("voice"), QStringLiteral("text"), QStringLiteral("direct"), QStringLiteral("tool")
	};
	if (!supportedSections.contains(normalized)) return;
	QStringList collapsed = m_collapsedNavigationSections;
	if (expanded)
		collapsed.removeAll(normalized);
	else if (!collapsed.contains(normalized))
		collapsed.push_back(normalized);
	setCollapsedNavigationSections(collapsed);
}
void ClientSessionController::setMotdHtml(const QString &value) {
	const QString bounded = value.left(MaxRichBodyCharacters);
	setMotdContent(bounded, bounded);
}
void ClientSessionController::setMotdContent(const QString &html, const QString &signatureIdentity) {
	const QString bounded = html.left(MaxRichBodyCharacters);
	const QString signature = motdContentSignature(
		signatureIdentity.trimmed().isEmpty() ? bounded : signatureIdentity);
	if (!acceptsFrontendStateMutation(this)
		|| (m_motdHtml == bounded && m_motdContentSignature == signature)) {
		return;
	}
	const bool htmlChanged = m_motdHtml != bounded;
	m_motdHtml = bounded;
	m_motdContentSignature = signature;
	if (!htmlChanged) {
		recomputeMotdDerivedState();
		return;
	}
	++m_motdParseGeneration;
	emit motdHtmlChanged();
	if (!m_motdSegments.isEmpty()) {
		m_motdSegments.clear();
		emit motdSegmentsChanged();
	}
	if (!m_motdBlocks.isEmpty()) {
		m_motdBlocks.clear();
		emit motdBlocksChanged();
	}
	recomputeMotdDerivedState();
	if (bounded.trimmed().isEmpty()) return;

	const quint64 generation = m_motdParseGeneration;
	auto *watcher = new QFutureWatcher< StructuredMotdDocument >(this);
	connect(watcher, &QFutureWatcher< StructuredMotdDocument >::finished, this,
		[this, watcher, generation, bounded] {
			const StructuredMotdDocument document = watcher->result();
			watcher->deleteLater();
			if (generation != m_motdParseGeneration || m_motdHtml != bounded) return;
			if (m_motdSegments != document.segments) {
				m_motdSegments = document.segments;
				emit motdSegmentsChanged();
			}
			if (m_motdBlocks != document.blocks) {
				m_motdBlocks = document.blocks;
				emit motdBlocksChanged();
			}
		});
	watcher->setFuture(QtConcurrent::run(&richBodyThreadPool(), [bounded] {
		const QString plainText =
			QTextDocumentFragment::fromHtml(bounded).toPlainText().left(MaxRichBodyCharacters);
		return structuredMotdDocument(bounded, plainText);
	}));
}
void ClientSessionController::setMotdSummary(const QString &value) { SET_VALUE(m_motdSummary, motdSummaryChanged); }
void ClientSessionController::setMotdExpanded(const bool value) {
	if (!acceptsFrontendStateMutation(this) || m_motdExpanded == value) return;
	m_motdExpanded = value;
	emit motdExpandedChanged();
	recomputeMotdDerivedState();
}
void ClientSessionController::setMotdDismissedSignature(const QString &value) {
	const QString normalized = value.trimmed().left(256);
	if (!acceptsFrontendStateMutation(this) || m_motdDismissedSignature == normalized) return;
	m_motdDismissedSignature = normalized;
	emit motdDismissedSignatureChanged();
	recomputeMotdDerivedState();
}
void ClientSessionController::setMotdLastSeenSignature(const QString &value) {
	const QString normalized = value.trimmed().left(256);
	if (!acceptsFrontendStateMutation(this) || m_motdLastSeenSignature == normalized) return;
	m_motdLastSeenSignature = normalized;
	emit motdLastSeenSignatureChanged();
	recomputeMotdDerivedState();
}

void ClientSessionController::applyState(const QVariantMap &state) {
	if (state.contains(QStringLiteral("serverName"))) setServerName(state.value(QStringLiteral("serverName")).toString());
	if (state.contains(QStringLiteral("serverMonogram")))
		setServerMonogram(state.value(QStringLiteral("serverMonogram")).toString());
	if (state.contains(QStringLiteral("serverImageUrl")))
		setServerImageUrl(state.value(QStringLiteral("serverImageUrl")).toString());
	if (state.contains(QStringLiteral("connectionLabel")))
		setConnectionLabel(state.value(QStringLiteral("connectionLabel")).toString());
	if (state.contains(QStringLiteral("selfStatusLabel")))
		setSelfStatusLabel(state.value(QStringLiteral("selfStatusLabel")).toString());
	if (state.contains(QStringLiteral("connectionState")))
		setConnectionState(state.value(QStringLiteral("connectionState")).toString());
	if (state.contains(QStringLiteral("connectionTone")))
		setConnectionTone(state.value(QStringLiteral("connectionTone")).toString());
	if (state.contains(QStringLiteral("connectionTooltip")))
		setConnectionDetail(state.value(QStringLiteral("connectionTooltip")).toString());
	if (state.contains(QStringLiteral("connectionRetryRemainingMs")))
		setConnectionRetryRemainingMs(state.value(QStringLiteral("connectionRetryRemainingMs")).toInt());
	else if (connectionState() != QLatin1String("retrying"))
		setConnectionRetryRemainingMs(0);
	if (state.contains(QStringLiteral("canConnect"))) setCanConnect(state.value(QStringLiteral("canConnect")).toBool());
	if (state.contains(QStringLiteral("canCancelConnection")))
		setCanCancel(state.value(QStringLiteral("canCancelConnection")).toBool());
	else if (state.contains(QStringLiteral("canDisconnect")))
		setCanCancel(state.value(QStringLiteral("canDisconnect")).toBool());
	if (state.contains(QStringLiteral("selfName"))) setSelfName(state.value(QStringLiteral("selfName")).toString());
	if (state.contains(QStringLiteral("selfMuted"))) setSelfMuted(state.value(QStringLiteral("selfMuted")).toBool());
	if (state.contains(QStringLiteral("selfDeafened")))
		setSelfDeafened(state.value(QStringLiteral("selfDeafened")).toBool());
	if (state.contains(QStringLiteral("menus"))) setAppMenus(state.value(QStringLiteral("menus")).toList());
	if (state.contains(QStringLiteral("selfMenu"))) setSelfMenu(state.value(QStringLiteral("selfMenu")).toMap());
	if (state.contains(QStringLiteral("updateBanner")))
		setUpdateBanner(state.value(QStringLiteral("updateBanner")).toMap());
	if (state.contains(QStringLiteral("stonks"))) setStonks(state.value(QStringLiteral("stonks")).toMap());
	if (state.contains(QStringLiteral("collapsedNavigationSections")))
		setCollapsedNavigationSections(state.value(QStringLiteral("collapsedNavigationSections")).toStringList());
	if (state.contains(QStringLiteral("motdHtml"))) setMotdHtml(state.value(QStringLiteral("motdHtml")).toString());
	if (state.contains(QStringLiteral("motdSummary")))
		setMotdSummary(state.value(QStringLiteral("motdSummary")).toString());
	if (state.contains(QStringLiteral("motdExpanded")))
		setMotdExpanded(state.value(QStringLiteral("motdExpanded")).toBool());
	if (state.contains(QStringLiteral("motdDismissedSignature")))
		setMotdDismissedSignature(state.value(QStringLiteral("motdDismissedSignature")).toString());
	if (state.contains(QStringLiteral("motdLastSeenSignature")))
		setMotdLastSeenSignature(state.value(QStringLiteral("motdLastSeenSignature")).toString());
}

void ClientSessionController::recomputeMotdDerivedState() {
	const bool hasContent = !m_motdHtml.trimmed().isEmpty();
	const QString signature = hasContent ? m_motdContentSignature : QString();
	const bool dismissed = hasContent && !m_motdDismissedSignature.isEmpty()
		&& (m_motdDismissedSignature == signature || m_motdDismissedSignature == m_motdHtml.trimmed());
	const QString comparisonSignature = !m_motdLastSeenSignature.isEmpty()
		? m_motdLastSeenSignature : m_motdDismissedSignature;
	const bool changed = hasContent && !comparisonSignature.isEmpty()
		&& comparisonSignature != signature && comparisonSignature != m_motdHtml.trimmed();

	QVariantList actions;
	if (hasContent && !dismissed) {
		actions.push_back(motdAction(m_motdExpanded ? QStringLiteral("motd.hide") : QStringLiteral("motd.show"),
			m_motdExpanded ? tr("Collapse") : tr("Expand"), signature));
		actions.push_back(motdAction(QStringLiteral("motd.dismiss"), tr("Dismiss"), signature,
			QStringLiteral("muted")));
	}

	if (m_hasMotd != hasContent) {
		m_hasMotd = hasContent;
		emit hasMotdChanged();
	}
	if (m_motdSignature != signature) {
		m_motdSignature = signature;
		emit motdSignatureChanged();
	}
	if (m_motdDismissed != dismissed) {
		m_motdDismissed = dismissed;
		emit motdDismissedChanged();
	}
	if (m_motdChanged != changed) {
		m_motdChanged = changed;
		emit motdChangedChanged();
	}
	if (m_motdActions != actions) {
		m_motdActions = actions;
		emit motdActionsChanged();
	}
}

#undef SET_VALUE

ActiveScopeController::ActiveScopeController(QObject *parent) : QObject(parent) {
}

QString ActiveScopeController::scopeToken() const { return m_scopeToken; }
QString ActiveScopeController::label() const { return m_label; }
QString ActiveScopeController::description() const { return m_description; }
QString ActiveScopeController::kindLabel() const { return m_kindLabel; }
QString ActiveScopeController::composerPlaceholder() const { return m_composerPlaceholder; }
QString ActiveScopeController::composerHint() const { return m_composerHint; }
bool ActiveScopeController::activity() const {
	return m_activity;
}
bool ActiveScopeController::canSend() const { return m_canSend; }
bool ActiveScopeController::hasPendingReply() const { return m_hasPendingReply; }
QString ActiveScopeController::replyActor() const { return m_replyActor; }
QString ActiveScopeController::replySnippet() const { return m_replySnippet; }
bool ActiveScopeController::canAttachImages() const { return m_canAttachImages; }
bool ActiveScopeController::canAttachFiles() const { return m_canAttachFiles; }
bool ActiveScopeController::canLoadOlder() const { return m_canLoadOlder; }
qulonglong ActiveScopeController::unreadCount() const { return m_unreadCount; }
bool ActiveScopeController::canMarkRead() const { return m_canMarkRead; }
bool ActiveScopeController::loading() const { return m_loading; }
QString ActiveScopeController::loadingState() const { return m_loadingState; }
QVariantMap ActiveScopeController::screenShare() const { return m_screenShare; }

#define SET_SCOPE_VALUE(member, signalName) \
	if (!acceptsFrontendStateMutation(this)) return; \
	if (member == value) return; \
	member = value; \
	emit signalName()

void ActiveScopeController::setScopeToken(const QString &value) { SET_SCOPE_VALUE(m_scopeToken, scopeTokenChanged); }
void ActiveScopeController::setLabel(const QString &value) { SET_SCOPE_VALUE(m_label, labelChanged); }
void ActiveScopeController::setDescription(const QString &value) { SET_SCOPE_VALUE(m_description, descriptionChanged); }
void ActiveScopeController::setKindLabel(const QString &value) { SET_SCOPE_VALUE(m_kindLabel, kindLabelChanged); }
void ActiveScopeController::setComposerPlaceholder(const QString &value) {
	SET_SCOPE_VALUE(m_composerPlaceholder, composerPlaceholderChanged);
}
void ActiveScopeController::setComposerHint(const QString &value) {
	SET_SCOPE_VALUE(m_composerHint, composerHintChanged);
}
void ActiveScopeController::setActivity(bool value) {
	SET_SCOPE_VALUE(m_activity, activityChanged);
}
void ActiveScopeController::setCanSend(bool value) { SET_SCOPE_VALUE(m_canSend, canSendChanged); }
void ActiveScopeController::setHasPendingReply(bool value) {
	SET_SCOPE_VALUE(m_hasPendingReply, hasPendingReplyChanged);
}
void ActiveScopeController::setReplyActor(const QString &value) { SET_SCOPE_VALUE(m_replyActor, replyActorChanged); }
void ActiveScopeController::setReplySnippet(const QString &value) { SET_SCOPE_VALUE(m_replySnippet, replySnippetChanged); }
void ActiveScopeController::setCanAttachImages(bool value) {
	SET_SCOPE_VALUE(m_canAttachImages, canAttachImagesChanged);
}
void ActiveScopeController::setCanAttachFiles(bool value) {
	SET_SCOPE_VALUE(m_canAttachFiles, canAttachFilesChanged);
}
void ActiveScopeController::setCanLoadOlder(bool value) { SET_SCOPE_VALUE(m_canLoadOlder, canLoadOlderChanged); }
void ActiveScopeController::setUnreadCount(qulonglong value) { SET_SCOPE_VALUE(m_unreadCount, unreadCountChanged); }
void ActiveScopeController::setCanMarkRead(bool value) { SET_SCOPE_VALUE(m_canMarkRead, canMarkReadChanged); }
void ActiveScopeController::setLoading(bool value) { SET_SCOPE_VALUE(m_loading, loadingChanged); }
void ActiveScopeController::setLoadingState(const QString &value) {
	SET_SCOPE_VALUE(m_loadingState, loadingStateChanged);
}
void ActiveScopeController::setScreenShare(const QVariantMap &value) {
	SET_SCOPE_VALUE(m_screenShare, screenShareChanged);
}

#undef SET_SCOPE_VALUE

void ActiveScopeController::applyState(const QVariantMap &state) {
	setScopeToken(state.value(QStringLiteral("scopeToken")).toString());
	setLabel(state.value(QStringLiteral("label")).toString());
	setDescription(state.value(QStringLiteral("description")).toString());
	setKindLabel(state.value(QStringLiteral("kindLabel")).toString());
	setComposerPlaceholder(state.value(QStringLiteral("composerPlaceholder")).toString());
	setComposerHint(state.value(QStringLiteral("composerHint")).toString());
	setActivity(state.value(QStringLiteral("activity")).toBool());
	setCanSend(state.value(QStringLiteral("canSend")).toBool());
	setHasPendingReply(state.value(QStringLiteral("hasPendingReply")).toBool());
	setReplyActor(state.value(QStringLiteral("replyActor")).toString());
	setReplySnippet(state.value(QStringLiteral("replySnippet")).toString());
	setCanAttachImages(state.value(QStringLiteral("canAttachImages")).toBool());
	setCanAttachFiles(state.value(QStringLiteral("canAttachFiles")).toBool());
	setCanLoadOlder(state.value(QStringLiteral("canLoadOlder")).toBool());
	setUnreadCount(state.value(QStringLiteral("unreadCount")).toULongLong());
	setCanMarkRead(state.value(QStringLiteral("canMarkRead")).toBool());
	setLoading(state.value(QStringLiteral("loading")).toBool());
	setLoadingState(state.value(QStringLiteral("loadingState")).toString());
	setScreenShare(state.value(QStringLiteral("screenShare")).toMap());
}

StableListModel::StableListModel(QObject *parent) : QAbstractListModel(parent) {
}

int StableListModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant StableListModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
		return {};
	}
	return valueForRole(m_rows.at(index.row()).toMap(), role);
}

QVariant StableListModel::valueForRole(const QVariantMap &row, const int role) {
	switch (role) {
		case StableIdRole: return row.value(QStringLiteral("id"));
		case TitleRole: return row.value(QStringLiteral("title"));
		case SubtitleRole: return row.value(QStringLiteral("subtitle"));
		case KindRole: return row.value(QStringLiteral("kind"));
		case SelectedRole: return row.value(QStringLiteral("selected"));
		case StatusRole: return row.value(QStringLiteral("status"));
		case PayloadRole: return row;
		case DepthRole: return row.value(QStringLiteral("depth"));
		case UnreadCountRole: return row.value(QStringLiteral("unreadCount"));
		case AvatarUrlRole: return row.value(QStringLiteral("avatarUrl"));
		case EnabledRole: return row.value(QStringLiteral("enabled"), true);
		case CheckedRole: return row.value(QStringLiteral("checked"));
		case TimestampRole: return row.value(QStringLiteral("timestamp"));
		case ReplyActorRole: return row.value(QStringLiteral("replyActor"));
		case ReplySnippetRole: return row.value(QStringLiteral("replySnippet"));
		case ReactionsRole: return row.value(QStringLiteral("reactions"));
		case BodySegmentsRole: return row.value(QStringLiteral("bodySegments"));
		case PreviewRole: return row.value(QStringLiteral("preview"));
		case OwnRole: return row.value(QStringLiteral("own"));
		case DeletedRole: return row.value(QStringLiteral("deleted"));
		case CanReplyRole: return row.value(QStringLiteral("canReply"));
		case CanReactRole: return row.value(QStringLiteral("canReact"));
		case CanDeleteRole: return row.value(QStringLiteral("canDelete"));
		case ScopeTokenRole: return row.value(QStringLiteral("scopeToken"));
		case ShortcutRole: return row.value(QStringLiteral("shortcut"));
		case CheckableRole: return row.value(QStringLiteral("checkable"));
		case MenuRoleRole: return row.value(QStringLiteral("menuRole"));
		case ToolTipRole: return row.value(QStringLiteral("toolTip"));
		case VisibleRole: return row.value(QStringLiteral("visible"), true);
		case AttachmentsRole: return row.value(QStringLiteral("attachments"));
		case SourceRole: return row.value(QStringLiteral("source"));
		case SectionKindRole: return row.value(QStringLiteral("sectionKind"));
		default: return {};
	}
}

QHash< int, QByteArray > StableListModel::roleNames() const {
	return { { StableIdRole, "stableId" }, { TitleRole, "title" }, { SubtitleRole, "subtitle" },
			 { KindRole, "kind" }, { SelectedRole, "selected" }, { StatusRole, "status" },
			 { PayloadRole, "payload" }, { DepthRole, "depth" }, { UnreadCountRole, "unreadCount" },
			 { AvatarUrlRole, "avatarUrl" }, { EnabledRole, "enabled" }, { CheckedRole, "checked" },
			 { TimestampRole, "timestamp" }, { ReplyActorRole, "replyActor" },
			 { ReplySnippetRole, "replySnippet" }, { ReactionsRole, "reactions" },
			 { BodySegmentsRole, "bodySegments" }, { PreviewRole, "preview" },
			 { OwnRole, "own" }, { DeletedRole, "deleted" }, { CanReplyRole, "canReply" },
			 { CanReactRole, "canReact" }, { CanDeleteRole, "canDelete" }, { ScopeTokenRole, "scopeToken" },
			 { ShortcutRole, "shortcut" }, { CheckableRole, "checkable" }, { MenuRoleRole, "menuRole" },
			 { ToolTipRole, "toolTip" }, { VisibleRole, "visible" }, { AttachmentsRole, "attachments" },
			 { SourceRole, "source" }, { SectionKindRole, "sectionKind" } };
}

QVariantMap StableListModel::get(int row) const {
	return row >= 0 && row < m_rows.size() ? m_rows.at(row).toMap() : QVariantMap {};
}

int StableListModel::rowForStableId(const QString &stableId) const {
	return indexOf(stableId.trimmed());
}

int StableListModel::indexOf(const QString &stableId) const {
	return m_rowIndexById.value(stableId, -1);
}

QList< int > StableListModel::changedRoles(const QVariantMap &before, const QVariantMap &after) {
	if (before == after) return {};
	QList< int > roles { PayloadRole };
	for (int role = StableIdRole; role <= SectionKindRole; ++role) {
		if (role != PayloadRole && valueForRole(before, role) != valueForRole(after, role)) roles.push_back(role);
	}
	return roles;
}

void StableListModel::rebuildRowIndex() {
	m_rowIndexById.clear();
	m_rowIndexById.reserve(m_rowIds.size());
	for (int row = 0; row < m_rowIds.size(); ++row) m_rowIndexById.insert(m_rowIds.at(row), row);
}

void StableListModel::synchronizeRows(const QVariantList &rows) {
	if (!acceptsFrontendStateMutation(this)) return;
	QVariantList validRows;
	QStringList validIds;
	QHash< QString, int > validIndexById;
	validRows.reserve(rows.size());
	validIds.reserve(rows.size());
	validIndexById.reserve(rows.size());
	for (const QVariant &entry : rows) {
		QVariantMap row = entry.toMap();
		row.detach();
		const QString stableId = row.value(QStringLiteral("id")).toString();
		if (!stableId.isEmpty()) {
			const int duplicateIndex = validIndexById.value(stableId, -1);
			if (duplicateIndex >= 0) {
				validRows[duplicateIndex] = row;
			} else {
				validIndexById.insert(stableId, validRows.size());
				validRows.push_back(row);
				validIds.push_back(stableId);
			}
		}
	}

	const int oldCount = m_rows.size();
	const int sharedCount = std::min(m_rowIds.size(), validIds.size());
	int commonPrefix = 0;
	while (commonPrefix < sharedCount && m_rowIds.at(commonPrefix) == validIds.at(commonPrefix)) ++commonPrefix;

	const auto updateRow = [this, &validRows](const int row) {
		const QVariantMap before = m_rows.at(row).toMap();
		const QVariantMap after = validRows.at(row).toMap();
		const QList< int > roles = changedRoles(before, after);
		if (roles.isEmpty()) return;
		emit rowsAboutToChange(row, row);
		m_rows[row] = after;
		emit dataChanged(index(row), index(row), roles);
	};

	// Same-order synchronization is the steady-state path. Handle append and tail removal in batches
	// so 10k-message timelines stay linear instead of repeatedly scanning and shifting the model.
	if (commonPrefix == sharedCount && (commonPrefix == m_rowIds.size() || commonPrefix == validIds.size())) {
		for (int row = 0; row < commonPrefix; ++row) updateRow(row);
		if (validIds.size() > m_rowIds.size()) {
			const int first = m_rowIds.size();
			const int last = validIds.size() - 1;
			beginInsertRows(QModelIndex(), first, last);
			for (int row = first; row <= last; ++row) {
				m_rows.push_back(validRows.at(row));
				m_rowIds.push_back(validIds.at(row));
			}
			endInsertRows();
			rebuildRowIndex();
		} else if (validIds.size() < m_rowIds.size()) {
			const int first = validIds.size();
			const int last = m_rowIds.size() - 1;
			beginRemoveRows(QModelIndex(), first, last);
			m_rows.remove(first, last - first + 1);
			m_rowIds.remove(first, last - first + 1);
			endRemoveRows();
			rebuildRowIndex();
		}
		if (oldCount != m_rows.size()) emit countChanged();
		return;
	}

	// History pagination prepends a contiguous block while retaining every
	// existing stable ID in the same order. Handle that shape as one insertion
	// and one index rebuild; the generic reconciliation below would otherwise
	// perform an insertion and O(N) hash rebuild for every older message.
	if (validIds.size() > m_rowIds.size()) {
		const int prependCount = validIds.size() - m_rowIds.size();
		bool purePrepend = prependCount > 0;
		for (int oldRow = 0; purePrepend && oldRow < m_rowIds.size(); ++oldRow) {
			purePrepend = m_rowIds.at(oldRow) == validIds.at(prependCount + oldRow);
		}
		if (purePrepend) {
			emit rowsAboutToBePrepended(prependCount);
			beginInsertRows(QModelIndex(), 0, prependCount - 1);
			QVariantList mergedRows;
			QStringList mergedIds;
			mergedRows.reserve(validRows.size());
			mergedIds.reserve(validIds.size());
			for (int row = 0; row < prependCount; ++row) {
				mergedRows.push_back(validRows.at(row));
				mergedIds.push_back(validIds.at(row));
			}
			mergedRows.append(m_rows);
			mergedIds.append(m_rowIds);
			m_rows.swap(mergedRows);
			m_rowIds.swap(mergedIds);
			rebuildRowIndex();
			endInsertRows();

			for (int row = prependCount; row < validRows.size(); ++row) updateRow(row);
			emit countChanged();
			return;
		}
	}

	// Switching between unrelated scopes is a replacement, not an interleaved
	// sequence of inserts and removals. The latter temporarily exposes rows from
	// both conversations to a reused QML ListView and can leave pooled delegates
	// painted with stale content. Keep the operation reset-free, but remove the
	// old range before inserting the new one. For large lists, use the same
	// bounded path when only a small minority of IDs overlap.
	if (!m_rowIds.isEmpty() && !validIds.isEmpty()) {
		int overlap = 0;
		for (const QString &id : validIds) {
			if (m_rowIndexById.contains(id)) ++overlap;
		}
		const int smallerCount = std::min(m_rowIds.size(), validIds.size());
		const bool disjointScopes = overlap == 0;
		const bool largeMostlyUnrelated = smallerCount >= 128 && overlap * 4 <= smallerCount;
		if (disjointScopes || largeMostlyUnrelated) {
			if (!m_rows.isEmpty()) {
				beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
				m_rows.clear();
				m_rowIds.clear();
				m_rowIndexById.clear();
				endRemoveRows();
			}
			if (!validRows.isEmpty()) {
				beginInsertRows(QModelIndex(), 0, validRows.size() - 1);
				m_rows = validRows;
				m_rowIds = validIds;
				rebuildRowIndex();
				endInsertRows();
			}
			if (oldCount != m_rows.size()) emit countChanged();
			return;
		}
	}

	for (int targetIndex = 0; targetIndex < validRows.size(); ++targetIndex) {
		const QVariantMap targetRow = validRows.at(targetIndex).toMap();
		const QString &targetId = validIds.at(targetIndex);

		int existingIndex = indexOf(targetId);
		if (existingIndex < 0) {
			beginInsertRows(QModelIndex(), targetIndex, targetIndex);
			m_rows.insert(targetIndex, targetRow);
			m_rowIds.insert(targetIndex, targetId);
			endInsertRows();
			rebuildRowIndex();
			existingIndex = targetIndex;
		} else if (existingIndex != targetIndex) {
			const int destination = existingIndex < targetIndex ? targetIndex + 1 : targetIndex;
			beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), destination);
			m_rows.move(existingIndex, targetIndex);
			m_rowIds.move(existingIndex, targetIndex);
			endMoveRows();
			rebuildRowIndex();
			existingIndex = targetIndex;
		}

		const QList< int > roles = changedRoles(m_rows.at(existingIndex).toMap(), targetRow);
		if (!roles.isEmpty()) {
			emit rowsAboutToChange(existingIndex, existingIndex);
			m_rows[existingIndex] = targetRow;
			emit dataChanged(index(existingIndex), index(existingIndex), roles);
		}
	}

	while (m_rows.size() > validRows.size()) {
		const int last = m_rows.size() - 1;
		beginRemoveRows(QModelIndex(), last, last);
		m_rows.removeAt(last);
		m_rowIds.removeAt(last);
		endRemoveRows();
		rebuildRowIndex();
	}
	if (oldCount != m_rows.size()) {
		emit countChanged();
	}
}

void StableListModel::upsertRow(const QVariantMap &row) {
	if (!acceptsFrontendStateMutation(this)) return;
	upsertRowFromInternalResult(row);
}

void StableListModel::upsertRowFromInternalResult(const QVariantMap &row) {
	const QString stableId = row.value(QStringLiteral("id")).toString();
	if (stableId.isEmpty()) {
		return;
	}
	const int existing = indexOf(stableId);
	if (existing >= 0) {
		QVariantMap ownedRow = row;
		ownedRow.detach();
		const QList< int > roles = changedRoles(m_rows.at(existing).toMap(), ownedRow);
		if (roles.isEmpty()) return;
		emit rowsAboutToChange(existing, existing);
		m_rows[existing] = ownedRow;
		emit dataChanged(index(existing), index(existing), roles);
		return;
	}
	QVariantMap ownedRow = row;
	ownedRow.detach();
	const int newRow = m_rows.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	m_rows.push_back(ownedRow);
	m_rowIds.push_back(stableId);
	m_rowIndexById.insert(stableId, newRow);
	endInsertRows();
	emit countChanged();
}

void StableListModel::removeRow(const QString &stableId) {
	if (!acceptsFrontendStateMutation(this)) return;
	const int existing = indexOf(stableId);
	if (existing < 0) {
		return;
	}
	beginRemoveRows(QModelIndex(), existing, existing);
	m_rows.removeAt(existing);
	m_rowIds.removeAt(existing);
	endRemoveRows();
	rebuildRowIndex();
	emit countChanged();
}

void StableListModel::clear() {
	if (!acceptsFrontendStateMutation(this)) return;
	if (m_rows.isEmpty()) {
		return;
	}
	beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
	m_rows.clear();
	m_rowIds.clear();
	m_rowIndexById.clear();
	endRemoveRows();
	emit countChanged();
}

QVariantMap RoomModel::roomRow(const QVariantMap &room, const QString &kind) {
	const QString scopeToken = room.value(QStringLiteral("token")).toString().trimmed();
	if (scopeToken.isEmpty()) return {};
	const bool joined = room.value(QStringLiteral("joined")).toBool();
	const bool canJoin = room.contains(QStringLiteral("canJoin"))
		? room.value(QStringLiteral("canJoin")).toBool()
		: kind == QLatin1String("voice") && !joined;
	const QVariantList actions = room.value(QStringLiteral("actions")).toList();
	const QVariantList badges = room.value(QStringLiteral("badges")).toList();
	const QVariantMap screenShare = room.value(QStringLiteral("screenShare")).toMap();
	QVariantMap source;
	if (!actions.isEmpty() || room.contains(QStringLiteral("actions")))
		source.insert(QStringLiteral("actions"), actions);
	return { { QStringLiteral("id"), QStringLiteral("%1:%2").arg(kind, scopeToken) },
			 { QStringLiteral("scopeToken"), scopeToken },
			 { QStringLiteral("title"), room.value(QStringLiteral("label")) },
			 { QStringLiteral("subtitle"),
			   room.value(QStringLiteral("topic"),
						  room.value(QStringLiteral("description"), room.value(QStringLiteral("subtitle")))) },
			 { QStringLiteral("pathLabel"), room.value(QStringLiteral("pathLabel")) },
			 { QStringLiteral("kindLabel"), room.value(QStringLiteral("kindLabel")) },
			 { QStringLiteral("kind"), kind },
			 { QStringLiteral("sectionKind"), room.value(QStringLiteral("sectionKind"), kind) },
			 { QStringLiteral("selected"), room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))) },
			 { QStringLiteral("status"), joined ? QStringLiteral("joined") : QString() },
			 { QStringLiteral("joined"), joined },
			 { QStringLiteral("canJoin"), canJoin },
			 { QStringLiteral("screenShare"), screenShare },
			 { QStringLiteral("badges"), badges },
			 { QStringLiteral("actions"), actions },
			 { QStringLiteral("participantsCurrent"), room.value(QStringLiteral("participantsCurrent")) },
			 { QStringLiteral("participantCount"), room.value(QStringLiteral("participantCount")) },
			 { QStringLiteral("depth"), room.value(QStringLiteral("depth")) },
			 { QStringLiteral("unreadCount"), room.value(QStringLiteral("unreadCount")) },
			 { QStringLiteral("source"), source } };
}

void RoomModel::clearConnectionState() {
	m_voiceRoomStates.clear();
	m_textRoomStates.clear();
	m_directMessageStates.clear();
	synchronizeAllRows();
}

void RoomModel::replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms) {
	if (!acceptsFrontendStateMutation(this)) return;
	m_voiceRoomStates = voiceRooms;
	m_textRoomStates = textRooms;
	synchronizeAllRows();
}

void RoomModel::replaceDirectMessageStates(const QVariantList &conversations) {
	if (!acceptsFrontendStateMutation(this)) return;
	m_directMessageStates = conversations;
	synchronizeAllRows();
}

void RoomModel::selectScope(const QString &scopeToken) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString selectedToken = scopeToken.trimmed();
	bool changed = false;
	const auto updateStates = [&selectedToken, &changed](QVariantList &states) {
		for (QVariant &entry : states) {
			QVariantMap room = entry.toMap();
			const bool selected = !selectedToken.isEmpty()
				&& room.value(QStringLiteral("token")).toString() == selectedToken;
			const bool wasSelected = room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))).toBool();
			if (selected == wasSelected && room.contains(QStringLiteral("selected"))) continue;
			room.insert(QStringLiteral("selected"), selected);
			entry = room;
			changed = true;
		}
	};
	updateStates(m_voiceRoomStates);
	updateStates(m_textRoomStates);
	updateStates(m_directMessageStates);
	if (changed) synchronizeAllRows();
}

void RoomModel::selectScopeFromRail(const QString &scopeToken, const QString &railKind) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString selectedToken = scopeToken.trimmed();
	const QString selectedKind  = railKind.trimmed().toLower();
	bool changed                = false;
	const auto updateStates     = [&selectedToken, &selectedKind, &changed](QVariantList &states,
																	 const QString &kind) {
		for (QVariant &entry : states) {
			QVariantMap room = entry.toMap();
			const bool selected = !selectedToken.isEmpty() && selectedKind == kind
				&& room.value(QStringLiteral("token")).toString() == selectedToken;
			const bool wasSelected =
				room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))).toBool();
			if (selected == wasSelected && room.contains(QStringLiteral("selected"))) continue;
			room.insert(QStringLiteral("selected"), selected);
			entry = room;
			changed = true;
		}
	};
	updateStates(m_voiceRoomStates, QStringLiteral("voice"));
	updateStates(m_textRoomStates, QStringLiteral("text"));
	updateStates(m_directMessageStates, QStringLiteral("direct"));
	if (changed) synchronizeAllRows();
}

void RoomModel::synchronizeAllRows() {
	QVariantList rows;
	rows.reserve(m_voiceRoomStates.size() + m_textRoomStates.size() + m_directMessageStates.size());
	const auto append = [&rows](const QVariantList &rooms, const QString &kind) {
		for (const QVariant &entry : rooms) {
			const QVariantMap row = roomRow(entry.toMap(), kind);
			if (!row.isEmpty()) rows.push_back(row);
		}
	};
	append(m_voiceRoomStates, QStringLiteral("voice"));
	append(m_textRoomStates, QStringLiteral("text"));
	append(m_directMessageStates, QStringLiteral("direct"));
	synchronizeRows(rows);
}

QVariantMap ParticipantModel::participantRow(const QVariantMap &participant) {
	const QString sessionId = participant.value(QStringLiteral("session")).toString().trimmed();
	if (sessionId.isEmpty()) return {};
	const QString participantKey = participant.value(QStringLiteral("participantKey")).toString().trimmed();
	const QString stableId = participantKey.isEmpty() ? sessionId : participantKey;
	const QVariant title = participant.contains(QStringLiteral("label"))
		? participant.value(QStringLiteral("label"))
		: participant.value(QStringLiteral("name"));
	const QVariant subtitle = participant.contains(QStringLiteral("subtitle"))
		? participant.value(QStringLiteral("subtitle"))
		: participant.value(QStringLiteral("statusLabel"));
	const QString entryKind = participant.value(QStringLiteral("entryKind"), QStringLiteral("user"))
							  .toString()
							  .trimmed()
							  .toLower();
	const QVariantList statuses = participant.value(QStringLiteral("statuses")).toList();
	const bool deafened = participant.value(
		QStringLiteral("deafened"),
		participantHasStatus(statuses, { QStringLiteral("serverDeafened"), QStringLiteral("selfDeafened") }))
						 .toBool();
	const bool muted = participant.value(
		QStringLiteral("muted"),
		participantHasStatus(statuses, { QStringLiteral("serverMuted"), QStringLiteral("selfMuted"),
										 QStringLiteral("localMuted"), QStringLiteral("suppressed") }))
					  .toBool();
	return { { QStringLiteral("id"), stableId },
			 { QStringLiteral("participantSession"), sessionId },
			 { QStringLiteral("title"), title },
			 { QStringLiteral("subtitle"), subtitle },
			 { QStringLiteral("kind"), QStringLiteral("participant") },
			 { QStringLiteral("status"), participant.value(QStringLiteral("talkState")) },
			 { QStringLiteral("avatarUrl"), participant.value(QStringLiteral("avatarUrl")) },
			 { QStringLiteral("entryKind"), entryKind.isEmpty() ? QStringLiteral("user") : entryKind },
			 { QStringLiteral("scopeToken"), participant.value(QStringLiteral("scopeToken")) },
			 { QStringLiteral("isSelf"), participant.value(QStringLiteral("isSelf")) },
			 { QStringLiteral("talkLabel"), participant.value(QStringLiteral("talkLabel")) },
			 { QStringLiteral("talkTone"), participant.value(QStringLiteral("talkTone")) },
			 { QStringLiteral("talking"), participant.value(QStringLiteral("talking")) },
			 { QStringLiteral("badges"), participant.value(QStringLiteral("badges")).toList() },
			 { QStringLiteral("statuses"), statuses },
			 { QStringLiteral("localVolume"), participant.value(QStringLiteral("localVolume")).toMap() },
			 { QStringLiteral("canMessage"), participant.value(QStringLiteral("canMessage")) },
			 { QStringLiteral("canJoin"), participant.value(QStringLiteral("canJoin")) },
			 { QStringLiteral("actions"), participant.value(QStringLiteral("actions")).toList() },
			 { QStringLiteral("muted"), muted },
			 { QStringLiteral("deafened"), deafened },
			 { QStringLiteral("listener"), entryKind == QLatin1String("listener") },
			 { QStringLiteral("source"), participant } };
}

void ParticipantModel::replaceParticipantStates(const QVariantList &participants) {
	const QVariantList coalescedParticipants = coalesceVoiceScopeParticipants(participants);
	QVariantList rows;
	rows.reserve(coalescedParticipants.size());
	for (const QVariant &entry : coalescedParticipants) {
		const QVariantMap row = participantRow(entry.toMap());
		if (!row.isEmpty()) rows.push_back(row);
	}
	synchronizeRows(rows);
}

QVariantList ParticipantModel::participantStates() const {
	QVariantList states;
	states.reserve(rowCount());
	for (int row = 0; row < rowCount(); ++row) states.push_back(get(row).value(QStringLiteral("source")));
	return states;
}

void ParticipantModel::upsertParticipantState(const QVariantMap &participant) {
	const QVariantMap row = participantRow(participant);
	if (row.isEmpty()) return;

	const QString session = participant.value(QStringLiteral("session")).toString().trimmed();
	const QString scopeToken = participant.value(QStringLiteral("scopeToken")).toString().trimmed();
	const bool incomingListener = participant.value(QStringLiteral("entryKind")).toString().trimmed().toLower()
		== QLatin1String("listener");
	if (!session.isEmpty() && !scopeToken.isEmpty()) {
		for (int rowIndex = rowCount() - 1; rowIndex >= 0; --rowIndex) {
			const QVariantMap existingRow = get(rowIndex);
			const QVariantMap existing = existingRow.value(QStringLiteral("source")).toMap();
			if (existing.value(QStringLiteral("session")).toString().trimmed() != session
				|| existing.value(QStringLiteral("scopeToken")).toString().trimmed() != scopeToken) {
				continue;
			}
			const bool existingListener = existing.value(QStringLiteral("entryKind")).toString()
										 .trimmed().toLower() == QLatin1String("listener");
			if (incomingListener && !existingListener) return;
			if (!incomingListener && existingListener) {
				removeRow(existingRow.value(QStringLiteral("id")).toString());
			}
		}
	}
	upsertRow(row);
}

void ParticipantModel::removeParticipant(const QString &sessionId) {
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;
	for (int rowIndex = rowCount() - 1; rowIndex >= 0; --rowIndex) {
		const QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("participantSession")).toString() == id
			|| row.value(QStringLiteral("id")).toString() == id) {
			removeRow(row.value(QStringLiteral("id")).toString());
		}
	}
}

void ParticipantModel::updatePresence(const QString &sessionId, const QString &talkState, const QString &talkLabel,
							  const QString &talkTone, const bool talking, const bool isSelf,
							  const QVariantList &badges, const QVariantList &statuses) {
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;

	for (int rowIndex = 0; rowIndex < rowCount(); ++rowIndex) {
		QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("participantSession")).toString() != id
			&& row.value(QStringLiteral("id")).toString() != id)
			continue;
		const QVariantMap previousRow = row;
		QVariantList rowBadges = badges;
		QVariantList rowStatuses = statuses;
		preserveListenerPresentation(row, rowBadges, rowStatuses);

		row.insert(QStringLiteral("status"), talkState);
		row.insert(QStringLiteral("talkLabel"), talkLabel);
		row.insert(QStringLiteral("talkTone"), talkTone);
		row.insert(QStringLiteral("talking"), talking);
		row.insert(QStringLiteral("isSelf"), isSelf);
		row.insert(QStringLiteral("badges"), rowBadges);
		row.insert(QStringLiteral("statuses"), rowStatuses);
		row.insert(QStringLiteral("deafened"), participantHasStatus(
			rowStatuses, { QStringLiteral("serverDeafened"), QStringLiteral("selfDeafened") }));
		row.insert(QStringLiteral("muted"), participantHasStatus(
			rowStatuses, { QStringLiteral("serverMuted"), QStringLiteral("selfMuted"),
						QStringLiteral("localMuted"), QStringLiteral("suppressed") }));
		QVariantMap source = row.value(QStringLiteral("source")).toMap();
		source.insert(QStringLiteral("talkState"), talkState);
		source.insert(QStringLiteral("talkLabel"), talkLabel);
		source.insert(QStringLiteral("talkTone"), talkTone);
		source.insert(QStringLiteral("talking"), talking);
		source.insert(QStringLiteral("isSelf"), isSelf);
		source.insert(QStringLiteral("badges"), rowBadges);
		source.insert(QStringLiteral("statuses"), rowStatuses);
		row.insert(QStringLiteral("source"), source);
		if (row != previousRow) upsertRow(row);
	}
}

namespace {
	QString normalizedNavigationFilter(const QString &value) {
		return value.simplified().toCaseFolded();
	}

	bool navigationTextMatches(const QVariant &value, const QString &filter) {
		return !filter.isEmpty() && value.toString().toCaseFolded().contains(filter);
	}

	bool navigationListMatches(const QVariant &value, const QString &filter) {
		for (const QVariant &entry : value.toList()) {
			const QVariantMap map = entry.toMap();
			if (map.isEmpty()) {
				if (navigationTextMatches(entry, filter)) return true;
				continue;
			}
			if (navigationTextMatches(map.value(QStringLiteral("label")), filter)
				|| navigationTextMatches(map.value(QStringLiteral("kind")), filter)) return true;
		}
		return false;
	}

	bool navigationParticipantMatches(const QVariantMap &participant, const QString &filter) {
		if (filter.isEmpty()) return true;
		return navigationTextMatches(participant.value(QStringLiteral("label")), filter)
			|| navigationTextMatches(participant.value(QStringLiteral("name")), filter)
			|| navigationTextMatches(participant.value(QStringLiteral("talkLabel")), filter)
			|| navigationTextMatches(participant.value(QStringLiteral("talkState")), filter)
			|| navigationListMatches(participant.value(QStringLiteral("badges")), filter)
			|| navigationListMatches(participant.value(QStringLiteral("statuses")), filter);
	}

	bool navigationRoomMatches(const QVariantMap &room, const QString &filter) {
		if (filter.isEmpty()) return true;
		return navigationTextMatches(room.value(QStringLiteral("label")), filter)
			|| navigationTextMatches(room.value(QStringLiteral("topic")), filter)
			|| navigationTextMatches(room.value(QStringLiteral("description")), filter)
			|| navigationTextMatches(room.value(QStringLiteral("subtitle")), filter)
			|| navigationTextMatches(room.value(QStringLiteral("pathLabel")), filter)
			|| navigationListMatches(room.value(QStringLiteral("badges")), filter);
	}

	bool navigationParticipantIsTalking(const QVariantMap &participant) {
		if (participant.value(QStringLiteral("talking")).toBool()) return true;
		const QString state = participant.value(QStringLiteral("talkState")).toString().trimmed().toLower();
		return state == QLatin1String("talking") || state == QLatin1String("whispering")
			|| state == QLatin1String("shouting") || state == QLatin1String("mutedtalking");
	}
}

QString NavigationRailModel::filterText() const { return m_filterText; }

void NavigationRailModel::setFilterText(const QString &filterText) {
	const QString accepted = filterText.left(128);
	if (m_filterText == accepted) return;
	m_filterText = accepted;
	synchronizeAllRows();
	emit filterTextChanged();
}

bool NavigationRailModel::isRoomExpanded(const QString &scopeToken) const {
	const QString token = scopeToken.trimmed();
	return !token.isEmpty() && !m_collapsedRoomScopes.contains(token);
}

void NavigationRailModel::setRoomExpanded(const QString &scopeToken, const bool expanded) {
	const QString token = scopeToken.trimmed();
	if (token.isEmpty() || isRoomExpanded(token) == expanded) return;
	if (expanded) {
		m_collapsedRoomScopes.remove(token);
	} else {
		m_collapsedRoomScopes.insert(token);
	}
	synchronizeAllRows();
	emit roomExpansionChanged(token, expanded);
}

void NavigationRailModel::toggleRoomExpanded(const QString &scopeToken) {
	const QString token = scopeToken.trimmed();
	if (!token.isEmpty()) setRoomExpanded(token, !isRoomExpanded(token));
}

void NavigationRailModel::clearConnectionState() {
	m_voiceRoomStates.clear();
	m_textRoomStates.clear();
	m_directMessageStates.clear();
	m_collapsedRoomScopes.clear();
	synchronizeAllRows();
}

QVariantMap NavigationRailModel::navigationRoomRow(const QVariantMap &room, const QString &kind) const {
	QVariantMap row = RoomModel::roomRow(room, kind);
	if (row.isEmpty()) return {};

	const QVariantList participants = room.value(QStringLiteral("participants")).toList();
	const int participantCount = room.contains(QStringLiteral("participants"))
		? participants.size() : room.value(QStringLiteral("participantCount")).toInt();
	int talkingCount = 0;
	QStringList talkingLabels;
	for (const QVariant &entry : participants) {
		const QVariantMap participant = entry.toMap();
		if (!navigationParticipantIsTalking(participant)) continue;
		++talkingCount;
		const QString label = participant.value(QStringLiteral("label")).toString().trimmed();
		if (!label.isEmpty() && !talkingLabels.contains(label) && talkingLabels.size() < 3)
			talkingLabels.push_back(label);
	}

	const QString filter = normalizedNavigationFilter(m_filterText);
	const bool ownMatch = navigationRoomMatches(room, filter);
	bool participantMatch = false;
	for (const QVariant &entry : participants) {
		if (navigationParticipantMatches(entry.toMap(), filter)) {
			participantMatch = true;
			break;
		}
	}
	const bool selected = row.value(QStringLiteral("selected")).toBool();
	const bool expanded = kind != QLatin1String("voice")
		|| isRoomExpanded(row.value(QStringLiteral("scopeToken")).toString());
	row.insert(QStringLiteral("rowKind"), QStringLiteral("room"));
	const QString requestedSection = row.value(QStringLiteral("sectionKind")).toString().trimmed().toLower();
	row.insert(QStringLiteral("sectionKind"), kind == QLatin1String("text") && requestedSection == QLatin1String("tool")
												  ? QStringLiteral("tool")
												  : kind);
	row.insert(QStringLiteral("participantCount"), participantCount);
	row.insert(QStringLiteral("talkingParticipantCount"), talkingCount);
	row.insert(QStringLiteral("talkingParticipantLabels"), talkingLabels);
	row.insert(QStringLiteral("expanded"), expanded);
	row.insert(QStringLiteral("filterMatch"), ownMatch);
	row.insert(QStringLiteral("railVisible"), filter.isEmpty() || ownMatch || participantMatch || selected);
	return row;
}

void NavigationRailModel::replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms) {
	if (!acceptsFrontendStateMutation(this)) return;
	m_voiceRoomStates.clear();
	m_voiceRoomStates.reserve(voiceRooms.size());
	for (const QVariant &entry : voiceRooms) {
		QVariantMap room = entry.toMap();
		if (room.contains(QStringLiteral("participants"))) {
			const QVariantList participants = coalesceVoiceScopeParticipants(
				room.value(QStringLiteral("participants")).toList(),
				room.value(QStringLiteral("token")).toString().trimmed());
			room.insert(QStringLiteral("participants"), participants);
			room.insert(QStringLiteral("participantCount"), participants.size());
		}
		m_voiceRoomStates.push_back(room);
	}
	m_textRoomStates = textRooms;
	synchronizeAllRows();
}

void NavigationRailModel::replaceDirectMessageStates(const QVariantList &conversations) {
	if (!acceptsFrontendStateMutation(this)) return;
	m_directMessageStates = conversations;
	synchronizeAllRows();
}

void NavigationRailModel::selectScope(const QString &scopeToken) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString selectedToken = scopeToken.trimmed();
	bool changed = false;
	const auto updateStates = [&selectedToken, &changed](QVariantList &states) {
		for (QVariant &entry : states) {
			QVariantMap room = entry.toMap();
			const bool selected = !selectedToken.isEmpty()
				&& room.value(QStringLiteral("token")).toString() == selectedToken;
			const bool wasSelected = room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))).toBool();
			if (selected == wasSelected && room.contains(QStringLiteral("selected"))) continue;
			room.insert(QStringLiteral("selected"), selected);
			entry = room;
			changed = true;
		}
	};
	updateStates(m_voiceRoomStates);
	updateStates(m_textRoomStates);
	updateStates(m_directMessageStates);
	if (changed) synchronizeAllRows();
}

void NavigationRailModel::selectScopeFromRail(const QString &scopeToken, const QString &railKind) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString selectedToken = scopeToken.trimmed();
	const QString selectedKind = railKind.trimmed().toLower();
	bool changed = false;
	const auto updateStates = [&selectedToken, &selectedKind, &changed](QVariantList &states,
																	 const QString &kind) {
		for (QVariant &entry : states) {
			QVariantMap room = entry.toMap();
			const bool selected = !selectedToken.isEmpty() && selectedKind == kind
				&& room.value(QStringLiteral("token")).toString() == selectedToken;
			const bool wasSelected = room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))).toBool();
			if (selected == wasSelected && room.contains(QStringLiteral("selected"))) continue;
			room.insert(QStringLiteral("selected"), selected);
			entry = room;
			changed = true;
		}
	};
	updateStates(m_voiceRoomStates, QStringLiteral("voice"));
	updateStates(m_textRoomStates, QStringLiteral("text"));
	updateStates(m_directMessageStates, QStringLiteral("direct"));
	if (changed) synchronizeAllRows();
}

void NavigationRailModel::updatePresence(const QString &sessionId, const QString &talkState,
										 const QString &talkLabel, const QString &talkTone, const bool talking,
										 const bool isSelf, const QVariantList &badges,
										 const QVariantList &statuses) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;

	// Keep the retained room payload in sync with the flattened rows. Scope
	// selection rebuilds the rail from this payload and must not roll a fresh
	// talk-state update back to its bootstrap value.
	QVariantList changedRooms;
	for (QVariant &roomEntry : m_voiceRoomStates) {
		QVariantMap room = roomEntry.toMap();
		QVariantList participants = room.value(QStringLiteral("participants")).toList();
		bool roomChanged = false;
		for (QVariant &participantEntry : participants) {
			QVariantMap participant = participantEntry.toMap();
			if (participant.value(QStringLiteral("session")).toString().trimmed() != id) continue;

			QVariantList participantBadges = badges;
			QVariantList participantStatuses = statuses;
			preserveListenerPresentation(participant, participantBadges, participantStatuses);
			participant.insert(QStringLiteral("talkState"), talkState);
			participant.insert(QStringLiteral("talkLabel"), talkLabel);
			participant.insert(QStringLiteral("talkTone"), talkTone);
			participant.insert(QStringLiteral("talking"), talking);
			participant.insert(QStringLiteral("isSelf"), isSelf);
			participant.insert(QStringLiteral("badges"), participantBadges);
			participant.insert(QStringLiteral("statuses"), participantStatuses);
			participantEntry = participant;
			roomChanged = true;
		}
		if (roomChanged) {
			room.insert(QStringLiteral("participants"), participants);
			roomEntry = room;
			changedRooms.push_back(room);
		}
	}
	if (!normalizedNavigationFilter(m_filterText).isEmpty()) {
		// Presence labels are searchable. Re-evaluate room ancestry and matching
		// participant visibility, while StableListModel keeps this reset-free.
		synchronizeAllRows();
		return;
	}
	for (const QVariant &entry : std::as_const(changedRooms)) {
		const QVariantMap roomRow = navigationRoomRow(entry.toMap(), QStringLiteral("voice"));
		if (!roomRow.isEmpty()) upsertRow(roomRow);
	}

	for (int rowIndex = 0; rowIndex < rowCount(); ++rowIndex) {
		QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("rowKind")).toString() != QLatin1String("participant")
			|| row.value(QStringLiteral("participantSession")).toString() != id)
			continue;
		const QVariantMap previousRow = row;
		QVariantList rowBadges = badges;
		QVariantList rowStatuses = statuses;
		preserveListenerPresentation(row, rowBadges, rowStatuses);

		row.insert(QStringLiteral("status"), talkState);
		row.insert(QStringLiteral("talkLabel"), talkLabel);
		row.insert(QStringLiteral("talkTone"), talkTone);
		row.insert(QStringLiteral("talking"), talking);
		row.insert(QStringLiteral("isSelf"), isSelf);
		row.insert(QStringLiteral("badges"), rowBadges);
		row.insert(QStringLiteral("statuses"), rowStatuses);
		row.insert(QStringLiteral("deafened"), participantHasStatus(
			rowStatuses, { QStringLiteral("serverDeafened"), QStringLiteral("selfDeafened") }));
		row.insert(QStringLiteral("muted"), participantHasStatus(
			rowStatuses, { QStringLiteral("serverMuted"), QStringLiteral("selfMuted"),
							   QStringLiteral("localMuted"), QStringLiteral("suppressed") }));
		QVariantMap source = row.value(QStringLiteral("source")).toMap();
		source.insert(QStringLiteral("talkState"), talkState);
		source.insert(QStringLiteral("talkLabel"), talkLabel);
		source.insert(QStringLiteral("talkTone"), talkTone);
		source.insert(QStringLiteral("talking"), talking);
		source.insert(QStringLiteral("isSelf"), isSelf);
		source.insert(QStringLiteral("badges"), rowBadges);
		source.insert(QStringLiteral("statuses"), rowStatuses);
		row.insert(QStringLiteral("source"), source);
		if (row != previousRow) upsertRow(row);
	}
}

void NavigationRailModel::removeParticipant(const QString &sessionId) {
	if (!acceptsFrontendStateMutation(this)) return;
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;
	QVariantList changedRooms;
	for (QVariant &roomEntry : m_voiceRoomStates) {
		QVariantMap room = roomEntry.toMap();
		QVariantList participants = room.value(QStringLiteral("participants")).toList();
		const qsizetype previousSize = participants.size();
		for (qsizetype index = participants.size(); index > 0; --index) {
			if (participants.at(index - 1).toMap().value(QStringLiteral("session")).toString().trimmed() == id)
				participants.removeAt(index - 1);
		}
		if (participants.size() != previousSize) {
			room.insert(QStringLiteral("participants"), participants);
			room.insert(QStringLiteral("participantCount"), participants.size());
			roomEntry = room;
			changedRooms.push_back(room);
		}
	}
	if (!normalizedNavigationFilter(m_filterText).isEmpty()) {
		synchronizeAllRows();
		return;
	}
	for (const QVariant &entry : std::as_const(changedRooms)) {
		const QVariantMap roomRow = navigationRoomRow(entry.toMap(), QStringLiteral("voice"));
		if (!roomRow.isEmpty()) upsertRow(roomRow);
	}
	for (int rowIndex = rowCount() - 1; rowIndex >= 0; --rowIndex) {
		const QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("rowKind")).toString() == QLatin1String("participant")
			&& row.value(QStringLiteral("participantSession")).toString() == id)
			removeRow(row.value(QStringLiteral("id")).toString());
	}
}

void NavigationRailModel::synchronizeAllRows() {
	QVariantList rows;
	const QString filter = normalizedNavigationFilter(m_filterText);
	const auto appendRoom = [this, &rows, &filter](const QVariantMap &room, const QString &kind) {
		QVariantMap roomRow = navigationRoomRow(room, kind);
		if (roomRow.isEmpty()) return;
		roomRow.insert(QStringLiteral("rowKind"), QStringLiteral("room"));
		rows.push_back(roomRow);

		if (kind != QLatin1String("voice")) return;
		const QString parentScopeToken = roomRow.value(QStringLiteral("scopeToken")).toString();
		const int participantDepth = roomRow.value(QStringLiteral("depth")).toInt() + 1;
		const bool expanded = roomRow.value(QStringLiteral("expanded"), true).toBool();
		const bool roomVisible = roomRow.value(QStringLiteral("railVisible"), true).toBool();
		const bool roomMatches = roomRow.value(QStringLiteral("filterMatch"), filter.isEmpty()).toBool();
		for (const QVariant &entry : room.value(QStringLiteral("participants")).toList()) {
			const QVariantMap participant = entry.toMap();
			QVariantMap participantRow = ParticipantModel::participantRow(participant);
			if (participantRow.isEmpty()) continue;
			const bool participantMatches = navigationParticipantMatches(participant, filter);
			participantRow.insert(QStringLiteral("rowKind"), QStringLiteral("participant"));
			participantRow.insert(QStringLiteral("sectionKind"), kind);
			participantRow.insert(QStringLiteral("parentScopeToken"), parentScopeToken);
			participantRow.insert(QStringLiteral("depth"), participantDepth);
			if (participantRow.value(QStringLiteral("scopeToken")).toString().isEmpty())
				participantRow.insert(QStringLiteral("scopeToken"), parentScopeToken);
			participantRow.insert(QStringLiteral("parentExpanded"), expanded);
			participantRow.insert(QStringLiteral("filterMatch"), participantMatches);
			participantRow.insert(QStringLiteral("railVisible"), roomVisible && expanded
				&& (filter.isEmpty() || roomMatches || participantMatches));
			rows.push_back(participantRow);
		}
	};

	rows.reserve(m_voiceRoomStates.size() + m_textRoomStates.size() + m_directMessageStates.size());
	for (const QVariant &entry : std::as_const(m_voiceRoomStates))
		appendRoom(entry.toMap(), QStringLiteral("voice"));
	for (const QVariant &entry : std::as_const(m_textRoomStates)) {
		if (entry.toMap().value(QStringLiteral("sectionKind")).toString().trimmed().toLower()
			== QLatin1String("tool")) {
			continue;
		}
		appendRoom(entry.toMap(), QStringLiteral("text"));
	}
	for (const QVariant &entry : std::as_const(m_textRoomStates)) {
		if (entry.toMap().value(QStringLiteral("sectionKind")).toString().trimmed().toLower()
			!= QLatin1String("tool")) {
			continue;
		}
		appendRoom(entry.toMap(), QStringLiteral("text"));
	}
	for (const QVariant &entry : std::as_const(m_directMessageStates))
		appendRoom(entry.toMap(), QStringLiteral("direct"));
	synchronizeRows(rows);
}

ChatTimelineModel::ChatTimelineModel(QObject *parent) : StableListModel(parent) {}

QString ChatTimelineModel::query() const { return m_query; }

void ChatTimelineModel::setQuery(const QString &queryValue) {
	const QString accepted = queryValue.left(512);
	if (m_query == accepted) return;
	m_query = accepted;
	emit queryChanged();
	refreshSearchState(false);
}

int ChatTimelineModel::matchCount() const { return static_cast< int >(m_searchMatchIds.size()); }
int ChatTimelineModel::currentMatchIndex() const { return m_currentMatchIndex; }
int ChatTimelineModel::currentMatchRow() const { return m_currentMatchRow; }
QString ChatTimelineModel::currentMatchStableId() const { return m_currentMatchStableId; }

bool ChatTimelineModel::searchActive() const { return !m_query.trimmed().isEmpty(); }

bool ChatTimelineModel::rowMatchesQuery(const QVariantMap &row, const QString &normalizedQuery) const {
	if (normalizedQuery.isEmpty()) return false;
	const auto matchesText = [&normalizedQuery](const QVariant &value) {
		return value.toString().toCaseFolded().contains(normalizedQuery);
	};
	const auto matchesFields = [&matchesText](const QVariantMap &source, const QStringList &fields) {
		for (const QString &field : fields) {
			if (matchesText(source.value(field))) return true;
		}
		return false;
	};
	const auto matchesAttachmentNames = [&matchesText](const QVariant &attachmentsValue) {
		for (const QVariant &entry : attachmentsValue.toList()) {
			const QVariantMap attachment = entry.toMap();
			if (matchesText(attachment.value(QStringLiteral("fileName")))
				|| matchesText(attachment.value(QStringLiteral("filename")))
				|| matchesText(attachment.value(QStringLiteral("name")))
				|| matchesText(attachment.value(QStringLiteral("label")))) {
				return true;
			}
		}
		return false;
	};

	static const QStringList senderFields {
		QStringLiteral("actor"), QStringLiteral("actorLabel"), QStringLiteral("actorName"),
		QStringLiteral("author"), QStringLiteral("authorName"), QStringLiteral("sender"),
		QStringLiteral("senderName")
	};
	static const QStringList bodyFields {
		QStringLiteral("bodyText"), QStringLiteral("plainText")
	};
	static const QStringList replyFields {
		QStringLiteral("replyActor"), QStringLiteral("replySnippet"), QStringLiteral("replyText")
	};

	const QVariantMap source = row.value(QStringLiteral("source")).toMap();
	if (matchesText(row.value(QStringLiteral("title")))
		|| matchesText(row.value(QStringLiteral("subtitle")))
		|| matchesText(row.value(QStringLiteral("replyActor")))
		|| matchesText(row.value(QStringLiteral("replySnippet")))
		|| matchesFields(source, senderFields)
		|| matchesFields(source, bodyFields)
		|| matchesFields(source, replyFields)
		|| matchesAttachmentNames(row.value(QStringLiteral("attachments")))
		|| matchesAttachmentNames(source.value(QStringLiteral("attachments")))) {
		return true;
	}

	const QVariantMap reply = source.value(QStringLiteral("reply")).toMap();
	return matchesFields(reply, senderFields) || matchesFields(reply, bodyFields)
		|| matchesFields(reply, replyFields);
}

void ChatTimelineModel::refreshSearchState(const bool preserveCurrentMatch) {
	const int previousMatchCount = static_cast< int >(m_searchMatchIds.size());
	const int previousIndex = m_currentMatchIndex;
	const int previousRow = m_currentMatchRow;
	const QString previousStableId = m_currentMatchStableId;
	const QString anchorStableId = preserveCurrentMatch ? previousStableId : QString();
	const int anchorIndex = preserveCurrentMatch ? previousIndex : -1;

	QStringList matches;
	const QString normalizedQuery = m_query.trimmed().toCaseFolded();
	if (!normalizedQuery.isEmpty()) {
		matches.reserve(rowCount());
		for (int row = 0; row < rowCount(); ++row) {
			const QVariantMap candidate = get(row);
			if (rowMatchesQuery(candidate, normalizedQuery)) {
				matches.push_back(candidate.value(QStringLiteral("id")).toString());
			}
		}
	}

	int selectedIndex = -1;
	if (!matches.isEmpty()) {
		selectedIndex = anchorStableId.isEmpty() ? -1
			: static_cast< int >(matches.indexOf(anchorStableId));
		if (selectedIndex < 0) {
			selectedIndex = anchorIndex >= 0
				? qMin(anchorIndex, static_cast< int >(matches.size()) - 1) : 0;
		}
	}
	m_searchMatchIds = std::move(matches);
	m_currentMatchIndex = selectedIndex;
	m_currentMatchStableId = selectedIndex >= 0 ? m_searchMatchIds.at(selectedIndex) : QString();
	m_currentMatchRow = m_currentMatchStableId.isEmpty() ? -1 : rowForStableId(m_currentMatchStableId);

	if (previousMatchCount != static_cast< int >(m_searchMatchIds.size())) emit matchCountChanged();
	if (previousIndex != m_currentMatchIndex || previousRow != m_currentMatchRow
		|| previousStableId != m_currentMatchStableId) {
		emit currentMatchChanged();
	}
}

bool ChatTimelineModel::selectMatch(const int matchIndex) {
	if (matchIndex < 0 || matchIndex >= static_cast< int >(m_searchMatchIds.size())) return false;
	const QString stableId = m_searchMatchIds.at(matchIndex);
	const int row = rowForStableId(stableId);
	if (row < 0) {
		refreshSearchState(true);
		return false;
	}
	if (m_currentMatchIndex == matchIndex && m_currentMatchRow == row
		&& m_currentMatchStableId == stableId) {
		return false;
	}
	m_currentMatchIndex = matchIndex;
	m_currentMatchRow = row;
	m_currentMatchStableId = stableId;
	emit currentMatchChanged();
	return true;
}

bool ChatTimelineModel::nextMatch() {
	if (m_searchMatchIds.isEmpty()) return false;
	const int count = static_cast< int >(m_searchMatchIds.size());
	const int nextIndex = m_currentMatchIndex < 0 ? 0 : (m_currentMatchIndex + 1) % count;
	return selectMatch(nextIndex);
}

bool ChatTimelineModel::previousMatch() {
	if (m_searchMatchIds.isEmpty()) return false;
	const int count = static_cast< int >(m_searchMatchIds.size());
	const int previousIndex = m_currentMatchIndex < 0 ? count - 1
		: (m_currentMatchIndex + count - 1) % count;
	return selectMatch(previousIndex);
}

void ChatTimelineModel::clearSearch() { setQuery(QString()); }

bool ChatTimelineModel::isUserHistoryRow(const QVariantMap &row) {
	const QVariantMap source = row.value(QStringLiteral("source")).toMap();
	return !source.value(QStringLiteral("system")).toBool()
		&& !row.value(QStringLiteral("deleted")).toBool();
}

void ChatTimelineModel::updateUserHistoryRow(const QString &messageId, const QVariantMap &row) {
	const bool hadUserHistory = !m_userHistoryMessageIds.isEmpty();
	if (isUserHistoryRow(row)) {
		m_userHistoryMessageIds.insert(messageId);
	} else {
		m_userHistoryMessageIds.remove(messageId);
	}
	if (hadUserHistory != !m_userHistoryMessageIds.isEmpty()) emit hasUserHistoryChanged();
}

QVariantMap ChatTimelineModel::messageRow(const QVariantMap &message,
										  QList< RichBodyParseRequest > *requests) {
	const QVariant messageIdValue = message.value(QStringLiteral("messageId"));
	QString messageId = messageIdValue.toULongLong() > 0 ? messageIdValue.toString().trimmed() : QString();
	if (messageId.isEmpty()) messageId = message.value(QStringLiteral("messageKey")).toString().trimmed();
	if (messageId.isEmpty()) messageId = message.value(QStringLiteral("id")).toString().trimmed();
	if (messageId.isEmpty()) return {};

	const QString bodyText = message.value(QStringLiteral("bodyText"), message.value(QStringLiteral("plainText"))).toString();
	const QString bodyHtml = message.value(QStringLiteral("bodyHtml")).toString();
	QVariantList bodySegments = fallbackStructuredMessageBody(bodyText);
	bool bodyHydrationPending = false;
	if (messageBodyNeedsRichParsing(bodyHtml)) {
		const QByteArray cacheKey = structuredMessageBodyCacheKey(bodyHtml, bodyText);
		const QByteArray previousKey = m_expectedRichBodyKeyByMessage.value(messageId);
		if (!previousKey.isEmpty() && previousKey != cacheKey) {
			auto previousConsumers = m_richBodyConsumers.find(previousKey);
			if (previousConsumers != m_richBodyConsumers.end()) {
				previousConsumers->remove(messageId);
				if (previousConsumers->isEmpty()) m_richBodyConsumers.erase(previousConsumers);
			}
		}
		m_expectedRichBodyKeyByMessage.insert(messageId, cacheKey);
		if (const auto cached = cachedStructuredMessageBody(cacheKey)) {
			bodySegments = *cached;
		} else if (requests) {
			bodyHydrationPending = true;
			requests->push_back({ messageId, cacheKey, bodyHtml.left(MaxRichBodyCharacters),
								  bodyText.left(MaxRichBodyCharacters) });
		}
	} else {
		forgetRichBodyMessage(messageId);
	}
	const QVariant previewValue = message.contains(QStringLiteral("preview"))
		? message.value(QStringLiteral("preview")) : message.value(QStringLiteral("previewStub"));
	QVariantMap source = message;
	// Preview payloads can contain base64 audio/video data. The normalized preview
	// role below is the single QML-facing copy; retaining the original payload in
	// SourceRole doubled dormant-card memory before playback was ever activated.
	source.remove(QStringLiteral("preview"));
	source.remove(QStringLiteral("previewStub"));
	if (bodyHydrationPending) source.insert(QStringLiteral("bodyHydrationPending"), true);
	else source.remove(QStringLiteral("bodyHydrationPending"));

	return { { QStringLiteral("id"), messageId },
			 { QStringLiteral("title"), message.value(QStringLiteral("actor"),
												 message.value(QStringLiteral("actorLabel"),
																   message.value(QStringLiteral("actorName")))) },
			 { QStringLiteral("subtitle"), bodyText },
			 { QStringLiteral("bodySegments"), bodySegments },
			 { QStringLiteral("kind"), QStringLiteral("message") },
			 { QStringLiteral("status"), message.value(QStringLiteral("deliveryState")) },
			 { QStringLiteral("avatarUrl"), message.value(QStringLiteral("avatarUrl")) },
			 { QStringLiteral("timestamp"), message.value(QStringLiteral("timeLabel")) },
			 { QStringLiteral("replyActor"), message.value(QStringLiteral("replyActor")) },
			 { QStringLiteral("replySnippet"), message.value(QStringLiteral("replySnippet")) },
			 { QStringLiteral("reactions"), normalizedReactions(message.value(QStringLiteral("reactions"))) },
			 { QStringLiteral("preview"), normalizedPreview(previewValue) },
			 { QStringLiteral("attachments"), normalizedAttachments(message.value(QStringLiteral("attachments"))) },
			 // Required QML bool roles must never be invalid. ListView can otherwise
			 // retain the previous delegate value when a sparse row is rebound.
			 { QStringLiteral("own"), message.value(QStringLiteral("own")).toBool() },
			 { QStringLiteral("deleted"), message.value(QStringLiteral("deleted")).toBool() },
			 { QStringLiteral("canReply"), message.value(QStringLiteral("canReply")).toBool() },
			 { QStringLiteral("canReact"), message.value(QStringLiteral("canReact")).toBool() },
			 { QStringLiteral("canDelete"), message.value(QStringLiteral("canDelete")).toBool() },
			 { QStringLiteral("source"), source } };
}

ChatTimelineModel::MessageMutation ChatTimelineModel::applyMessage(const QVariantMap &message) {
	if (!acceptsFrontendStateMutation(this)) return MessageMutation::Ignored;
	QList< RichBodyParseRequest > requests;
	const QVariantMap row = messageRow(message, &requests);
	if (row.isEmpty()) return MessageMutation::Ignored;
	const QString id = row.value(QStringLiteral("id")).toString();
	const int rowIndex = indexOf(id);
	if (rowIndex >= 0) {
		const QVariantMap current = get(rowIndex);
		if (current == row) {
			scheduleRichBodyParses(requests);
			return MessageMutation::Unchanged;
		}
		upsertRow(row);
		updateUserHistoryRow(id, row);
		scheduleRichBodyParses(requests);
		if (searchActive()) refreshSearchState(true);
		return MessageMutation::Updated;
	}
	upsertRow(row);
	updateUserHistoryRow(id, row);
	scheduleRichBodyParses(requests);
	if (searchActive()) refreshSearchState(true);
	return MessageMutation::Inserted;
}

bool ChatTimelineModel::upsertMessage(const QVariantMap &message) {
	return applyMessage(message) != MessageMutation::Ignored;
}

bool ChatTimelineModel::removeMessage(const QString &messageId) {
	if (!acceptsFrontendStateMutation(this)) return false;
	const QString id = messageId.trimmed();
	if (id.isEmpty()) return false;
	if (indexOf(id) < 0) return false;
	forgetRichBodyMessage(id);
	const bool hadUserHistory = !m_userHistoryMessageIds.isEmpty();
	m_userHistoryMessageIds.remove(id);
	removeRow(id);
	if (hadUserHistory != !m_userHistoryMessageIds.isEmpty()) emit hasUserHistoryChanged();
	if (searchActive()) refreshSearchState(true);
	return true;
}

int ChatTimelineModel::appendMessages(const QVariantList &messages) {
	int applied = 0;
	bool searchStateMayHaveChanged = false;
	QList< RichBodyParseRequest > requests;
	for (const QVariant &entry : messages) {
		if (!acceptsFrontendStateMutation(this)) break;
		const QVariantMap row = messageRow(entry.toMap(), &requests);
		if (row.isEmpty()) continue;
		++applied;
		const QString id = row.value(QStringLiteral("id")).toString();
		const int rowIndex = indexOf(id);
		if (rowIndex >= 0 && get(rowIndex) == row) continue;
		upsertRow(row);
		updateUserHistoryRow(id, row);
		searchStateMayHaveChanged = true;
	}
	scheduleRichBodyParses(requests);
	if (searchActive() && searchStateMayHaveChanged) refreshSearchState(true);
	return applied;
}

void ChatTimelineModel::replaceMessages(const QVariantList &messages) {
	if (!acceptsFrontendStateMutation(this)) return;
	QVariantList rows;
	QList< RichBodyParseRequest > requests;
	QSet< QString > retainedMessageIds;
	QSet< QString > userHistoryMessageIds;
	rows.reserve(messages.size());
	for (const QVariant &entry : messages) {
		const QVariantMap row = messageRow(entry.toMap(), &requests);
		if (!row.isEmpty()) {
			rows.push_back(row);
			const QString id = row.value(QStringLiteral("id")).toString();
			retainedMessageIds.insert(id);
			if (isUserHistoryRow(row))
				userHistoryMessageIds.insert(id);
			else
				userHistoryMessageIds.remove(id);
		}
	}
	const QStringList expectedIds = m_expectedRichBodyKeyByMessage.keys();
	for (const QString &messageId : expectedIds) {
		if (!retainedMessageIds.contains(messageId)) forgetRichBodyMessage(messageId);
	}
	for (auto it = m_pendingRichBodyOrder.begin(); it != m_pendingRichBodyOrder.end();) {
		if (m_richBodyConsumers.contains(*it)) {
			++it;
			continue;
		}
		m_pendingRichBodyParses.remove(*it);
		m_inFlightRichBodyKeys.remove(*it);
		it = m_pendingRichBodyOrder.erase(it);
	}
	for (auto it = m_deferredRichBodyOrder.begin(); it != m_deferredRichBodyOrder.end();) {
		if (m_richBodyConsumers.contains(*it)) {
			++it;
			continue;
		}
		m_deferredRichBodyKeys.remove(*it);
		m_inFlightRichBodyKeys.remove(*it);
		it = m_deferredRichBodyOrder.erase(it);
	}
	const bool hadUserHistory = !m_userHistoryMessageIds.isEmpty();
	synchronizeRows(rows);
	m_userHistoryMessageIds = std::move(userHistoryMessageIds);
	if (hadUserHistory != !m_userHistoryMessageIds.isEmpty()) emit hasUserHistoryChanged();
	scheduleRichBodyParses(requests);
	if (searchActive()) refreshSearchState(true);
}

QVariantList ChatTimelineModel::messages() const {
	QVariantList states;
	states.reserve(rowCount());
	for (int row = 0; row < rowCount(); ++row) states.push_back(get(row).value(QStringLiteral("source")));
	return states;
}

bool ChatTimelineModel::hasUserHistory() const {
	return !m_userHistoryMessageIds.isEmpty();
}

void ChatTimelineModel::clear() {
	if (!acceptsFrontendStateMutation(this)) return;
	for (auto it = m_richBodyConsumers.begin(); it != m_richBodyConsumers.end(); ++it) it->clear();
	m_richBodyConsumers.clear();
	m_expectedRichBodyKeyByMessage.clear();
	m_pendingRichBodyParses.clear();
	m_pendingRichBodyOrder.clear();
	m_deferredRichBodyOrder.clear();
	m_deferredRichBodyKeys.clear();
	m_inFlightRichBodyKeys = m_activeRichBodyKeys;
	m_readyRichBodies.clear();
	m_richBodyDrainScheduled = false;
	const bool hadUserHistory = !m_userHistoryMessageIds.isEmpty();
	m_userHistoryMessageIds.clear();
	StableListModel::clear();
	if (hadUserHistory) emit hasUserHistoryChanged();
	if (searchActive()) refreshSearchState(true);
}

void ChatTimelineModel::forgetRichBodyMessage(const QString &messageId) {
	const QByteArray cacheKey = m_expectedRichBodyKeyByMessage.take(messageId);
	if (cacheKey.isEmpty()) return;
	auto consumers = m_richBodyConsumers.find(cacheKey);
	if (consumers == m_richBodyConsumers.end()) return;
	consumers->remove(messageId);
	if (consumers->isEmpty()) m_richBodyConsumers.erase(consumers);
}

void ChatTimelineModel::scheduleRichBodyParses(const QList< RichBodyParseRequest > &requests) {
	if (requests.isEmpty()) return;
	constexpr int MaxPendingRichBodies = 512;
	for (const RichBodyParseRequest &request : requests) {
		if (request.messageId.isEmpty() || request.cacheKey.isEmpty()
			|| m_expectedRichBodyKeyByMessage.value(request.messageId) != request.cacheKey) {
			continue;
		}
		if (const auto cached = cachedStructuredMessageBody(request.cacheKey)) {
			m_readyRichBodies.push_back({ request.messageId, request.cacheKey, *cached });
			continue;
		}
		m_richBodyConsumers[request.cacheKey].insert(request.messageId);
		if (!m_inFlightRichBodyKeys.contains(request.cacheKey)) {
			m_inFlightRichBodyKeys.insert(request.cacheKey);
			m_pendingRichBodyParses.insert(request.cacheKey, request);
			m_pendingRichBodyOrder.push_back(request.cacheKey);
			while (m_pendingRichBodyOrder.size() > MaxPendingRichBodies) {
				const QByteArray evictedKey = m_pendingRichBodyOrder.takeFirst();
				m_pendingRichBodyParses.remove(evictedKey);
				if (!m_deferredRichBodyKeys.contains(evictedKey)) {
					m_deferredRichBodyKeys.insert(evictedKey);
					m_deferredRichBodyOrder.push_back(evictedKey);
				}
			}
		}
	}
	if (!m_readyRichBodies.empty()) scheduleRichBodyDrain();
	launchRichBodyParseBatch();
}

void ChatTimelineModel::refillDeferredRichBodyParses() {
	constexpr int MaxPendingRichBodies = 512;
	while (m_pendingRichBodyOrder.size() < MaxPendingRichBodies && !m_deferredRichBodyOrder.isEmpty()) {
		const QByteArray key = m_deferredRichBodyOrder.takeLast();
		m_deferredRichBodyKeys.remove(key);
		const QSet< QString > consumers = m_richBodyConsumers.value(key);
		RichBodyParseRequest request;
		for (const QString &messageId : consumers) {
			if (m_expectedRichBodyKeyByMessage.value(messageId) != key) continue;
			const int row = indexOf(messageId);
			if (row < 0) continue;
			const QVariantMap source = get(row).value(QStringLiteral("source")).toMap();
			request = { messageId, key,
				source.value(QStringLiteral("bodyHtml")).toString().left(MaxRichBodyCharacters),
				source.value(QStringLiteral("bodyText"), source.value(QStringLiteral("plainText")))
					.toString().left(MaxRichBodyCharacters) };
			break;
		}
		if (request.messageId.isEmpty()) {
			m_inFlightRichBodyKeys.remove(key);
			continue;
		}
		m_pendingRichBodyParses.insert(key, request);
		m_pendingRichBodyOrder.push_back(key);
	}
}

void ChatTimelineModel::launchRichBodyParseBatch() {
	refillDeferredRichBodyParses();
	if (m_richBodyWorkerActive || m_pendingRichBodyOrder.isEmpty()) return;
	constexpr int MaxRichBodiesPerBatch = 32;
	QList< RichBodyParseRequest > pending;
	pending.reserve(std::min< qsizetype >(MaxRichBodiesPerBatch, m_pendingRichBodyOrder.size()));
	while (pending.size() < MaxRichBodiesPerBatch && !m_pendingRichBodyOrder.isEmpty()) {
		const QByteArray key = m_pendingRichBodyOrder.takeLast();
		const auto request = m_pendingRichBodyParses.take(key);
		if (request.cacheKey.isEmpty()) continue;
		m_activeRichBodyKeys.insert(key);
		pending.push_back(request);
	}
	if (pending.isEmpty()) return;
	m_richBodyWorkerActive = true;

	auto *watcher = new QFutureWatcher< QList< ParsedRichBody > >(this);
	connect(watcher, &QFutureWatcher< QList< ParsedRichBody > >::finished, this, [this, watcher] {
		const QList< ParsedRichBody > parsedBodies = watcher->result();
		watcher->deleteLater();
		for (const ParsedRichBody &parsed : parsedBodies) {
			m_activeRichBodyKeys.remove(parsed.cacheKey);
			m_inFlightRichBodyKeys.remove(parsed.cacheKey);
			const QSet< QString > consumers = m_richBodyConsumers.take(parsed.cacheKey);
			for (const QString &messageId : consumers) {
				if (m_expectedRichBodyKeyByMessage.value(messageId) == parsed.cacheKey) {
					m_readyRichBodies.push_back({ messageId, parsed.cacheKey, parsed.segments });
				}
			}
		}
		m_richBodyWorkerActive = false;
		if (!m_readyRichBodies.empty()) scheduleRichBodyDrain();
		launchRichBodyParseBatch();
	});
	watcher->setFuture(QtConcurrent::run(&richBodyThreadPool(), [pending] {
		QList< ParsedRichBody > parsedBodies;
		parsedBodies.reserve(pending.size());
		for (const RichBodyParseRequest &request : pending) {
			QVariantList segments;
			try {
				segments = structuredMessageBody(request.bodyHtml, request.bodyText, request.cacheKey);
			} catch (...) {
				segments = fallbackStructuredMessageBody(request.bodyText);
			}
			parsedBodies.push_back({ request.cacheKey, segments });
		}
		return parsedBodies;
	}));
}

void ChatTimelineModel::scheduleRichBodyDrain() {
	if (m_richBodyDrainScheduled || m_readyRichBodies.empty()) return;
	m_richBodyDrainScheduled = true;
	QMetaObject::invokeMethod(this, &ChatTimelineModel::drainRichBodyResults, Qt::QueuedConnection);
}

void ChatTimelineModel::drainRichBodyResults() {
	m_richBodyDrainScheduled = false;
	constexpr int MaxResultsPerTurn = 32;
	for (int applied = 0; applied < MaxResultsPerTurn && !m_readyRichBodies.empty(); ++applied) {
		ReadyRichBody ready = std::move(m_readyRichBodies.front());
		m_readyRichBodies.pop_front();
		if (m_expectedRichBodyKeyByMessage.value(ready.messageId) != ready.cacheKey) continue;
		const int rowIndex = indexOf(ready.messageId);
		if (rowIndex < 0) continue;
		QVariantMap row = get(rowIndex);
		QVariantMap source = row.value(QStringLiteral("source")).toMap();
		const bool segmentsChanged = row.value(QStringLiteral("bodySegments")).toList() != ready.segments;
		const bool hydrationPending = source.value(QStringLiteral("bodyHydrationPending")).toBool();
		if (!segmentsChanged && !hydrationPending) continue;
		if (segmentsChanged) row.insert(QStringLiteral("bodySegments"), ready.segments);
		source.remove(QStringLiteral("bodyHydrationPending"));
		row.insert(QStringLiteral("source"), source);
		// The parser result is owned by this model and is accepted only after both
		// the stable message id and expected cache key still match. It must be able
		// to land while the visual-fixture override rejects unrelated live writes.
		upsertRowFromInternalResult(row);
	}
	if (!m_readyRichBodies.empty()) scheduleRichBodyDrain();
}

namespace {
	constexpr int MaxOperationItemResultPageSize = 64;
	constexpr qsizetype MaxDialogPresentationFieldValueCount = 32;
	constexpr qulonglong MaxProtocolId = std::numeric_limits< unsigned int >::max();

	void setOperationField(QVariantMap &row, const QString &key, const QVariant &value) {
		row.insert(key, value);
		QVariantMap details = row.value(QStringLiteral("payload")).toMap();
		details.insert(key, value);
		row.insert(QStringLiteral("payload"), details);
	}

	QString normalizedOperationStatus(const QString &value) {
		const QString status = value.trimmed().toLower();
		static const QSet< QString > allowed { QStringLiteral("succeeded"), QStringLiteral("partial"),
			QStringLiteral("failed"), QStringLiteral("cancelled") };
		return allowed.contains(status) ? status : QStringLiteral("failed");
	}

	bool parseProtocolId(const QString &value, const bool allowZero, qulonglong *result) {
		const QString text = value.trimmed();
		if (text.isEmpty()) return false;
		for (const QChar character : text) {
			if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
		}
		bool valid = false;
		const qulonglong id = text.toULongLong(&valid);
		if (!valid || id > MaxProtocolId || (!allowZero && id == 0)) return false;
		if (result) *result = id;
		return true;
	}

	QString normalizedChannelScopeToken(const QString &value) {
		const QString token = value.trimmed();
		// The production scope wire format is "<ChatScope enum>:<channel id>". Keep accepting the
		// early QML prototype's "channel:<id>" spelling for local callers, but never rewrite a real
		// protocol token into that non-protocol form before it reaches MainWindow.
		qulonglong channel = 0;
		if (token.startsWith(QLatin1String("channel:"))) {
			return parseProtocolId(token.mid(QStringLiteral("channel:").size()), true, &channel)
				? QStringLiteral("channel:%1").arg(channel) : QString();
		}
		const qsizetype separator = token.indexOf(QLatin1Char(':'));
		if (separator <= 0 || separator != token.lastIndexOf(QLatin1Char(':'))) return {};
		// Moving users/channels is only meaningful for the protocol's Channel scope (0).
		// TextChannel, Private/DM and every other chat scope must never reach MainWindow's
		// channel move handlers merely because they share the "<scope>:<id>" shape.
		if (token.left(separator) != QLatin1String("0")) return {};
		return parseProtocolId(token.mid(separator + 1), true, &channel)
			? QStringLiteral("0:%1").arg(channel) : QString();
	}

	QSet< QString > dialogPresentationFieldIds(const QVariantMap &state) {
		QSet< QString > ids;
		if (!state.value(QStringLiteral("open")).toBool()) return ids;
		for (const QVariant &sectionValue : state.value(QStringLiteral("sections")).toList()) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("type")).toString() != QLatin1String("voiceMeter")
					|| field.value(QStringLiteral("staticMeter")).toBool()) {
					continue;
				}
				const QString id = field.value(QStringLiteral("id")).toString().trimmed();
				if (id.isEmpty()) continue;
				ids.insert(id);
				if (ids.size() >= MaxDialogPresentationFieldValueCount) return ids;
			}
		}
		return ids;
	}

	bool scopeTokenHasRootId(const QString &token) {
		return token.section(QLatin1Char(':'), -1) == QLatin1String("0");
	}
}

QVariantMap DirectMessageSummaryModel::conversationRow(const QVariantMap &conversation) {
	qulonglong session = 0;
	const QVariant sessionValue = conversation.value(QStringLiteral("peerSession"),
		conversation.value(QStringLiteral("session")));
	if (!parseProtocolId(sessionValue.toString(), false, &session)) return {};

	const QString label = conversation.value(QStringLiteral("label")).toString().trimmed();
	const QString preview = conversation.value(QStringLiteral("lastMessagePreview")).toString().trimmed();
	const QString subtitle = preview.isEmpty()
		? conversation.value(QStringLiteral("subtitle")).toString().trimmed() : preview;
	const QString scopeToken = conversation.value(QStringLiteral("token")).toString().trimmed();

	return { { QStringLiteral("id"), QString::number(session) },
			 { QStringLiteral("title"), label.isEmpty() ? QObject::tr("User %1").arg(session) : label },
			 { QStringLiteral("subtitle"), subtitle },
			 { QStringLiteral("kind"), QStringLiteral("direct") },
			 { QStringLiteral("selected"), conversation.value(QStringLiteral("open")).toBool() },
			 { QStringLiteral("status"), conversation.value(QStringLiteral("status")).toString() },
			 { QStringLiteral("unreadCount"), conversation.value(QStringLiteral("unreadCount")).toULongLong() },
			 { QStringLiteral("avatarUrl"), conversation.value(QStringLiteral("avatarUrl")).toString() },
			 { QStringLiteral("enabled"), true },
			 { QStringLiteral("timestamp"), conversation.value(QStringLiteral("lastActivityAtMs")) },
			 { QStringLiteral("scopeToken"), scopeToken },
			 { QStringLiteral("source"), conversation } };
}

void DirectMessageSummaryModel::replaceConversationStates(const QVariantList &conversations) {
	QVariantList rows;
	rows.reserve(conversations.size());
	for (const QVariant &value : conversations) {
		const QVariantMap row = conversationRow(value.toMap());
		if (!row.isEmpty()) rows.push_back(row);
	}
	synchronizeRows(rows);
}

DirectMessageController::DirectMessageController(QObject *parent)
	: QObject(parent), m_summaryModel(this), m_timelineModel(this) {
}

DirectMessageSummaryModel *DirectMessageController::summaryModel() { return &m_summaryModel; }
ChatTimelineModel *DirectMessageController::timelineModel() { return &m_timelineModel; }
bool DirectMessageController::available() const { return m_state.value(QStringLiteral("available")).toBool(); }
QString DirectMessageController::title() const {
	return m_state.value(QStringLiteral("title"), tr("Direct messages")).toString();
}
QString DirectMessageController::description() const { return m_state.value(QStringLiteral("description")).toString(); }
qulonglong DirectMessageController::unreadTotal() const {
	return m_state.value(QStringLiteral("unreadTotal")).toULongLong();
}
bool DirectMessageController::hasUnread() const { return unreadTotal() > 0; }
bool DirectMessageController::trayOpen() const { return m_trayOpen; }
bool DirectMessageController::conversationOpen() const {
	return !m_activeSessionId.isEmpty() && m_activeConversation.value(QStringLiteral("open")).toBool();
}
QString DirectMessageController::activeSessionId() const { return m_activeSessionId; }
QString DirectMessageController::activeScopeToken() const {
	return m_activeConversation.value(QStringLiteral("token")).toString();
}
QString DirectMessageController::activeLabel() const { return m_activeConversation.value(QStringLiteral("label")).toString(); }
QString DirectMessageController::activeSubtitle() const {
	return m_activeConversation.value(QStringLiteral("subtitle")).toString();
}
QString DirectMessageController::activeAvatarUrl() const {
	return m_activeConversation.value(QStringLiteral("avatarUrl")).toString();
}
qulonglong DirectMessageController::activeUnreadCount() const {
	return m_activeConversation.value(QStringLiteral("unreadCount")).toULongLong();
}
bool DirectMessageController::canSend() const { return m_activeConversation.value(QStringLiteral("canSend")).toBool(); }
QString DirectMessageController::mode() const {
	return m_activeConversation.value(QStringLiteral("persistentHistory")).toBool()
		? QStringLiteral("history") : QStringLiteral("private");
}
bool DirectMessageController::persistentHistoryAvailable() const {
	return m_activeConversation.value(QStringLiteral("persistentHistoryAvailable")).toBool();
}
bool DirectMessageController::historyLoading() const {
	return m_activeConversation.value(QStringLiteral("historyLoading")).toBool();
}
QString DirectMessageController::historyError() const {
	return m_activeConversation.value(QStringLiteral("historyError")).toString();
}
QString DirectMessageController::emptyCopy() const {
	return m_activeConversation.value(QStringLiteral("emptyCopy"), tr("Direct messages will appear here.")).toString();
}
bool DirectMessageController::canAttachImages() const {
	return canSend() && m_activeConversation.value(QStringLiteral("canAttachImages")).toBool();
}
bool DirectMessageController::canAttachFiles() const {
	return canSend() && m_activeConversation.value(QStringLiteral("canAttachFiles")).toBool();
}
QVariantList DirectMessageController::draftAttachments() const {
	QVariantList attachments;
	for (const QVariant &value : m_activeConversation.value(QStringLiteral("draftAttachments")).toList()) {
		if (attachments.size() >= 16) break;
		const QVariantMap source = value.toMap();
		const QString id = source.value(QStringLiteral("id"), source.value(QStringLiteral("stableId")))
			.toString().trimmed().left(256);
		if (id.isEmpty()) continue;
		QVariantMap attachment {
			{ QStringLiteral("id"), id },
			{ QStringLiteral("fileName"), QFileInfo(source.value(QStringLiteral("fileName"),
				 source.value(QStringLiteral("name"))).toString()).fileName().left(255) },
			{ QStringLiteral("kind"), source.value(QStringLiteral("kind")).toString().trimmed().toLower().left(64) },
			{ QStringLiteral("status"), source.value(QStringLiteral("status")).toString().trimmed().toLower().left(64) },
			{ QStringLiteral("error"), source.value(QStringLiteral("error")).toString().left(1024) },
			{ QStringLiteral("progress"), qBound(0.0, source.value(QStringLiteral("progress")).toDouble(), 1.0) }
		};
		attachments.push_back(attachment);
	}
	return attachments;
}
bool DirectMessageController::hasPendingReply() const { return !m_pendingReplyMessageId.isEmpty(); }
QString DirectMessageController::pendingReplyMessageId() const { return m_pendingReplyMessageId; }
QString DirectMessageController::pendingReplyActor() const { return m_pendingReplyActor; }
QString DirectMessageController::pendingReplySnippet() const { return m_pendingReplySnippet; }
QString DirectMessageController::draft() const { return m_drafts.value(m_activeSessionId); }
bool DirectMessageController::windowDocked() const { return m_windowDocked; }
bool DirectMessageController::windowMinimized() const { return m_windowMinimized; }

QString DirectMessageController::normalizedSessionId(const QVariant &value) {
	qulonglong session = 0;
	return parseProtocolId(value.toString(), false, &session) ? QString::number(session) : QString();
}

QVariantMap DirectMessageController::timelineRow(const QString &stableId) const {
	const QString id = stableId.trimmed();
	if (id.isEmpty()) return {};
	const int row = m_timelineModel.rowForStableId(id);
	if (row >= 0) return m_timelineModel.get(row);
	for (int index = 0; index < m_timelineModel.rowCount(); ++index) {
		const QVariantMap candidate = m_timelineModel.get(index);
		if (candidate.value(QStringLiteral("source")).toMap().value(QStringLiteral("messageId")).toString() == id)
			return candidate;
	}
	return {};
}

QString DirectMessageController::protocolMessageId(const QVariantMap &row) const {
	const QVariantMap source = row.value(QStringLiteral("source")).toMap();
	const QVariant candidate = source.value(QStringLiteral("messageId"), row.value(QStringLiteral("id")));
	qulonglong messageId = 0;
	return parseProtocolId(candidate.toString(), false, &messageId)
		? QString::number(messageId) : QString();
}

void DirectMessageController::clearPendingReplyState() {
	if (!hasPendingReply() && m_pendingReplyActor.isEmpty() && m_pendingReplySnippet.isEmpty()) return;
	m_pendingReplyMessageId.clear();
	m_pendingReplyActor.clear();
	m_pendingReplySnippet.clear();
	emit pendingReplyChanged();
}

void DirectMessageController::switchActiveConversation(const QVariantMap &conversation) {
	const QString previousSession = m_activeSessionId;
	const QString previousDraft = draft();
	const QString session = normalizedSessionId(conversation.value(QStringLiteral("peerSession"),
		conversation.value(QStringLiteral("session"))));
	m_activeSessionId = session;
	m_activeConversation = session.isEmpty() ? QVariantMap {} : conversation;

	QVariantList normalizedMessages;
	for (const QVariant &value : m_activeConversation.value(QStringLiteral("messages")).toList()) {
		QVariantMap message = value.toMap();
		if (!message.contains(QStringLiteral("bodyHtml"))) {
			message.insert(QStringLiteral("bodyHtml"), message.value(QStringLiteral("messageHtml")));
		}
		if (!message.contains(QStringLiteral("bodyText"))) {
			message.insert(QStringLiteral("bodyText"), message.value(QStringLiteral("plainText")));
		}
		if (!message.contains(QStringLiteral("timeLabel"))) {
			const qint64 createdAtMs = message.value(QStringLiteral("createdAtMs")).toLongLong();
			message.insert(QStringLiteral("timeLabel"), createdAtMs > 0
				? QDateTime::fromMSecsSinceEpoch(createdAtMs).toString(QStringLiteral("HH:mm")) : QString());
		}
		normalizedMessages.push_back(message);
	}
	m_timelineModel.replaceMessages(normalizedMessages);
	if (previousSession != m_activeSessionId) {
		clearPendingReplyState();
	} else if (hasPendingReply()) {
		const QVariantMap replyRow = timelineRow(m_pendingReplyMessageId);
		if (replyRow.isEmpty() || !replyRow.value(QStringLiteral("canReply")).toBool()) {
			clearPendingReplyState();
		}
	}

	if (previousSession != m_activeSessionId || previousDraft != draft()) emit draftChanged();
	if (!conversationOpen() && m_windowMinimized) {
		m_windowMinimized = false;
		emit windowMinimizedChanged();
	}
}

void DirectMessageController::applyState(const QVariantMap &state) {
	if (!acceptsFrontendStateMutation(this)) return;

	const QVariantMap previousState = m_state;
	const QVariantMap previousConversation = m_activeConversation;
	const bool previousTrayOpen = m_trayOpen;
	const QVariantList conversations = state.value(QStringLiteral("conversations")).toList();
	m_summaryModel.replaceConversationStates(conversations);

	QVariantMap activeConversation = state.value(QStringLiteral("activeConversation")).toMap();
	if (activeConversation.isEmpty()) {
		for (const QVariant &value : conversations) {
			const QVariantMap candidate = value.toMap();
			if (candidate.value(QStringLiteral("open")).toBool()) {
				activeConversation = candidate;
				break;
			}
		}
	}

	m_state = state;
	m_trayOpen = state.value(QStringLiteral("trayOpen"), previousTrayOpen).toBool();
	switchActiveConversation(activeConversation);
	if (previousTrayOpen != m_trayOpen) emit trayOpenChanged();
	if (previousState != m_state || previousConversation != m_activeConversation) emit stateChanged();
}

bool DirectMessageController::applyMessageState(const QString &sessionId, const QVariantMap &sourceMessage) {
	if (!acceptsFrontendStateMutation(this) || normalizedSessionId(sessionId) != m_activeSessionId) return false;
	QVariantMap message = sourceMessage;
	if (!message.contains(QStringLiteral("bodyHtml")))
		message.insert(QStringLiteral("bodyHtml"), message.value(QStringLiteral("messageHtml")));
	if (!message.contains(QStringLiteral("bodyText")))
		message.insert(QStringLiteral("bodyText"), message.value(QStringLiteral("plainText")));
	if (!message.contains(QStringLiteral("timeLabel"))) {
		const qint64 createdAtMs = message.value(QStringLiteral("createdAtMs")).toLongLong();
		message.insert(QStringLiteral("timeLabel"), createdAtMs > 0
			? QDateTime::fromMSecsSinceEpoch(createdAtMs).toString(QStringLiteral("HH:mm")) : QString());
	}
	const QString stableId = message.value(QStringLiteral("id")).toString().trimmed();
	if (stableId.isEmpty()) return false;
	const ChatTimelineModel::MessageMutation mutation = m_timelineModel.applyMessage(message);
	if (mutation == ChatTimelineModel::MessageMutation::Ignored) return false;

	QVariantList messages = m_activeConversation.value(QStringLiteral("messages")).toList();
	bool replaced = false;
	for (QVariant &value : messages) {
		if (value.toMap().value(QStringLiteral("id")).toString() != stableId) continue;
		value = message;
		replaced = true;
		break;
	}
	if (!replaced) messages.push_back(message);
	m_activeConversation.insert(QStringLiteral("messages"), messages);
	emit stateChanged();
	return true;
}

void DirectMessageController::setTrayOpen(const bool open) {
	if (m_trayOpen == open) return;
	m_trayOpen = open;
	emit trayOpenChanged();
	emit trayOpenChangeRequested(open);
}

void DirectMessageController::openConversation(const QString &sessionId) {
	const QString session = normalizedSessionId(sessionId);
	if (session.isEmpty()) return;
	if (m_windowMinimized) {
		m_windowMinimized = false;
		emit windowMinimizedChanged();
	}
	emit openRequested(session);
}

void DirectMessageController::closeConversation() {
	if (m_activeSessionId.isEmpty()) return;
	emit closeRequested(m_activeSessionId);
}

void DirectMessageController::markRead(const QString &sessionId) {
	const QString session = normalizedSessionId(sessionId.isEmpty() ? m_activeSessionId : sessionId);
	if (!session.isEmpty()) emit markReadRequested(session);
}

void DirectMessageController::setMode(const QString &modeValue) {
	if (m_activeSessionId.isEmpty()) return;
	const QString normalized = modeValue.trimmed().toLower();
	if (normalized != QLatin1String("history") && normalized != QLatin1String("private")) return;
	if (normalized == mode()) return;
	emit modeChangeRequested(m_activeSessionId, normalized);
}

void DirectMessageController::setDraft(const QString &draftValue) {
	if (m_activeSessionId.isEmpty()) return;
	const QString bounded = draftValue.left(16384);
	if (m_drafts.value(m_activeSessionId) == bounded) return;
	if (bounded.isEmpty()) m_drafts.remove(m_activeSessionId);
	else m_drafts.insert(m_activeSessionId, bounded);
	pruneDrafts();
	emit draftChanged();
}

void DirectMessageController::clearDraft(const QString &sessionId) {
	const QString session = normalizedSessionId(sessionId.isEmpty() ? m_activeSessionId : sessionId);
	if (session.isEmpty() || !m_drafts.remove(session)) return;
	if (session == m_activeSessionId) emit draftChanged();
}

void DirectMessageController::sendDraft() {
	if (m_activeSessionId.isEmpty() || !canSend()) return;
	const QString message = draft().trimmed();
	const QVariantList attachments = draftAttachments();
	if (message.isEmpty() && attachments.isEmpty()) return;
	if (hasPendingReply() || !attachments.isEmpty()) {
		emit richSendRequested(m_activeSessionId, message, m_pendingReplyMessageId, attachments);
		return;
	}
	emit sendRequested(m_activeSessionId, message);
}

void DirectMessageController::replyToMessage(const QString &messageId) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	if (row.isEmpty() || !row.value(QStringLiteral("canReply")).toBool()) return;
	const QString protocolId = protocolMessageId(row);
	if (protocolId.isEmpty()) return;
	const QString actor = row.value(QStringLiteral("title")).toString().trimmed().left(256);
	const QString snippet = row.value(QStringLiteral("subtitle")).toString().trimmed().left(512);
	const bool changed = m_pendingReplyMessageId != protocolId || m_pendingReplyActor != actor
		|| m_pendingReplySnippet != snippet;
	m_pendingReplyMessageId = protocolId;
	m_pendingReplyActor = actor;
	m_pendingReplySnippet = snippet;
	if (changed) emit pendingReplyChanged();
	emit messageReplyRequested(m_activeSessionId, protocolId);
}

void DirectMessageController::cancelPendingReply() { clearPendingReplyState(); }

void DirectMessageController::retryMessage(const QString &messageId) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	if (row.isEmpty() || !row.value(QStringLiteral("source")).toMap()
		.value(QStringLiteral("deliveryCanRetry")).toBool()) return;
	emit messageRetryRequested(m_activeSessionId, row.value(QStringLiteral("id")).toString());
}

void DirectMessageController::deleteMessage(const QString &messageId) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	if (row.isEmpty() || !row.value(QStringLiteral("canDelete")).toBool()) return;
	const QString protocolId = protocolMessageId(row);
	if (!protocolId.isEmpty()) emit messageDeleteRequested(m_activeSessionId, protocolId);
}

void DirectMessageController::toggleMessageReaction(const QString &messageId, const QString &emoji) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	const QString reaction = emoji.trimmed().left(64);
	if (row.isEmpty() || !row.value(QStringLiteral("canReact")).toBool() || reaction.isEmpty()) return;
	const QString protocolId = protocolMessageId(row);
	if (!protocolId.isEmpty()) emit messageReactionToggleRequested(m_activeSessionId, protocolId, reaction);
}

void DirectMessageController::chooseAttachment() {
	if (!m_activeSessionId.isEmpty() && (canAttachImages() || canAttachFiles())) {
		emit attachmentChooseRequested(m_activeSessionId);
	}
}

void DirectMessageController::removeDraftAttachment(const QString &attachmentId) {
	const QString id = attachmentId.trimmed();
	if (!m_activeSessionId.isEmpty() && !id.isEmpty()) emit draftAttachmentRemoveRequested(m_activeSessionId, id);
}

void DirectMessageController::retryDraftAttachment(const QString &attachmentId) {
	const QString id = attachmentId.trimmed();
	if (!m_activeSessionId.isEmpty() && !id.isEmpty()) emit draftAttachmentRetryRequested(m_activeSessionId, id);
}

void DirectMessageController::openAttachment(const QString &assetId, const QString &fileName) {
	qulonglong parsed = 0;
	if (!m_activeSessionId.isEmpty() && parseProtocolId(assetId, false, &parsed)
		&& parsed <= std::numeric_limits< unsigned int >::max()) {
		emit attachmentOpenRequested(m_activeSessionId, static_cast< unsigned int >(parsed),
			QFileInfo(fileName).fileName().left(255));
	}
}

void DirectMessageController::downloadAttachment(const QString &assetId, const QString &fileName) {
	qulonglong parsed = 0;
	if (!m_activeSessionId.isEmpty() && parseProtocolId(assetId, false, &parsed)
		&& parsed <= std::numeric_limits< unsigned int >::max()) {
		emit attachmentDownloadRequested(m_activeSessionId, static_cast< unsigned int >(parsed),
			QFileInfo(fileName).fileName().left(255));
	}
}

void DirectMessageController::retryAttachmentPreview(const QString &messageId, const QString &assetId) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	const QString protocolId = protocolMessageId(row);
	qulonglong parsedAsset = 0;
	if (protocolId.isEmpty() || !parseProtocolId(assetId, false, &parsedAsset)
		|| parsedAsset > std::numeric_limits< unsigned int >::max()) return;
	bool retryable = false;
	for (const QVariant &value : row.value(QStringLiteral("attachments")).toList()) {
		const QVariantMap attachment = value.toMap();
		if (attachment.value(QStringLiteral("assetId")).toULongLong() == parsedAsset
			&& attachment.value(QStringLiteral("previewCanRetry")).toBool()) {
			retryable = true;
			break;
		}
	}
	if (retryable) {
		emit attachmentPreviewRetryRequested(m_activeSessionId, protocolId,
			static_cast< unsigned int >(parsedAsset));
	}
}

void DirectMessageController::requestContentHydration(const QString &messageId, const bool highPriority) {
	if (m_activeSessionId.isEmpty()) return;
	const QVariantMap row = timelineRow(messageId);
	const QString protocolId = protocolMessageId(row);
	if (protocolId.isEmpty()) return;
	emit contentHydrationRequested(m_activeSessionId, { protocolId }, highPriority);
}

void DirectMessageController::setWindowDocked(const bool docked) {
	if (m_windowDocked == docked) return;
	m_windowDocked = docked;
	emit windowDockedChanged();
}

void DirectMessageController::setWindowMinimized(const bool minimized) {
	if (m_windowMinimized == minimized) return;
	m_windowMinimized = minimized;
	emit windowMinimizedChanged();
}

void DirectMessageController::pruneDrafts() {
	constexpr qsizetype MaxDraftConversations = 32;
	while (m_drafts.size() > MaxDraftConversations) {
		auto it = m_drafts.begin();
		if (it.key() == m_activeSessionId && m_drafts.size() > 1) ++it;
		m_drafts.erase(it);
	}
}

ToastController::ToastController(QObject *parent) : QObject(parent), m_dismissTimer(new QTimer(this)) {
	m_dismissTimer->setSingleShot(true);
	connect(m_dismissTimer, &QTimer::timeout, this, &ToastController::dismiss);
}

bool ToastController::visible() const { return m_visible; }
QString ToastController::tone() const { return m_tone; }
QString ToastController::title() const { return m_title; }
QString ToastController::message() const { return m_message; }
QString ToastController::actionId() const { return m_actionId; }
QString ToastController::actionLabel() const { return m_actionLabel; }
int ToastController::repeatCount() const { return m_repeatCount; }
qulonglong ToastController::revision() const { return m_revision; }

void ToastController::publish(const QString &tone, const QString &title, const QString &message,
							  const QString &actionId, const QString &actionLabel, const int timeoutMs) {
	QString normalizedTone = tone.trimmed().toLower();
	if (normalizedTone == QLatin1String("error")) normalizedTone = QStringLiteral("danger");
	if (normalizedTone != QLatin1String("info") && normalizedTone != QLatin1String("accent")
		&& normalizedTone != QLatin1String("success") && normalizedTone != QLatin1String("warning")
		&& normalizedTone != QLatin1String("danger")) {
		normalizedTone = QStringLiteral("info");
	}
	const QString normalizedTitle = title.trimmed();
	const QString normalizedMessage = message.trimmed();
	const QString normalizedActionId = actionId.trimmed();
	const QString normalizedActionLabel = actionLabel.trimmed();
	const bool duplicate = m_visible && normalizedTone == m_tone && normalizedTitle == m_title
		&& normalizedMessage == m_message && normalizedActionId == m_actionId
		&& normalizedActionLabel == m_actionLabel;

	m_visible = true;
	m_tone = normalizedTone;
	m_title = normalizedTitle;
	m_message = normalizedMessage;
	m_actionId = normalizedActionId;
	m_actionLabel = normalizedActionLabel;
	m_repeatCount = duplicate ? qMin(m_repeatCount + 1, 999) : 1;
	m_remainingMs = qBound(250, timeoutMs > 0 ? timeoutMs : 4500, 60000);
	++m_revision;
	scheduleDismiss();
	emit stateChanged();
}

void ToastController::dismiss() {
	if (!m_visible) return;
	m_dismissTimer->stop();
	m_visible = false;
	m_interactionActive = false;
	m_repeatCount = 0;
	m_remainingMs = 4500;
	m_timerStartedMs = 0;
	emit stateChanged();
}

void ToastController::setInteractionActive(const bool active) {
	if (m_interactionActive == active) return;
	m_interactionActive = active;
	if (!m_visible) return;
	if (active) {
		if (m_dismissTimer->isActive()) {
			const qint64 elapsed = qMax< qint64 >(0, QDateTime::currentMSecsSinceEpoch() - m_timerStartedMs);
			m_remainingMs = qMax(1, m_remainingMs - static_cast< int >(qMin< qint64 >(elapsed, m_remainingMs)));
			m_dismissTimer->stop();
		}
	} else {
		scheduleDismiss();
	}
}

void ToastController::scheduleDismiss() {
	if (!m_visible || m_interactionActive) {
		m_dismissTimer->stop();
		return;
	}
	m_timerStartedMs = QDateTime::currentMSecsSinceEpoch();
	m_dismissTimer->start(qMax(1, m_remainingMs));
}

void AsyncOperationModel::startOperation(const QString &operationId, const QString &title, const QString &subtitle,
										 const bool cancellable) {
	startStructuredOperation(operationId, QString(), title, subtitle, -1, cancellable);
}

void AsyncOperationModel::startStructuredOperation(const QString &operationId, const QString &kind,
											 const QString &title, const QString &subtitle, const int totalItems,
											 const bool cancellable) {
	const QString id = operationId.trimmed();
	if (id.isEmpty() || !acceptsFrontendStateMutation(this)) return;
	m_itemResultsByOperation.remove(id);
	QVariantMap row { { QStringLiteral("id"), id }, { QStringLiteral("title"), title },
		{ QStringLiteral("subtitle"), subtitle }, { QStringLiteral("kind"), kind.trimmed() },
		{ QStringLiteral("payload"), QVariantMap() } };
	setOperationField(row, QStringLiteral("status"), QStringLiteral("running"));
	setOperationField(row, QStringLiteral("progress"), -1);
	setOperationField(row, QStringLiteral("indeterminate"), true);
	setOperationField(row, QStringLiteral("cancellable"), cancellable);
	setOperationField(row, QStringLiteral("cancellationRequested"), false);
	setOperationField(row, QStringLiteral("phase"), QStringLiteral("starting"));
	setOperationField(row, QStringLiteral("completedItems"), 0);
	setOperationField(row, QStringLiteral("totalItems"), totalItems < 0 ? -1 : totalItems);
	setOperationField(row, QStringLiteral("successfulItems"), 0);
	setOperationField(row, QStringLiteral("failedItems"), 0);
	setOperationField(row, QStringLiteral("cancelledItems"), 0);
	setOperationField(row, QStringLiteral("bytesReceived"), -1);
	setOperationField(row, QStringLiteral("bytesTotal"), -1);
	setOperationField(row, QStringLiteral("currentPluginId"), QVariant());
	setOperationField(row, QStringLiteral("itemResultCount"), 0);
	setOperationField(row, QStringLiteral("unsuccessfulItemResultCount"), 0);
	setOperationField(row, QStringLiteral("itemResultRevision"), 0);
	setOperationField(row, QStringLiteral("errorCode"), QString());
	upsertRow(row);
}

void AsyncOperationModel::updateProgress(const QString &operationId, const qint64 bytesReceived,
										 const qint64 bytesTotal) {
	const int rowIndex = indexOf(operationId.trimmed());
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	if (row.isEmpty()) return;
	const QString status = row.value(QStringLiteral("status")).toString();
	if (status != QLatin1String("running") && status != QLatin1String("cancelling")) return;
	setOperationField(row, QStringLiteral("bytesReceived"), bytesReceived);
	setOperationField(row, QStringLiteral("bytesTotal"), bytesTotal);
	setOperationField(row, QStringLiteral("indeterminate"), bytesTotal <= 0);
	setOperationField(row, QStringLiteral("progress"),
		bytesTotal > 0
			? qBound(0, qRound(qBound(0.0, static_cast< double >(qMax< qint64 >(0, bytesReceived))
										 / static_cast< double >(bytesTotal), 1.0) * 100.0), 100)
			: -1);
	upsertRow(row);
}

bool AsyncOperationModel::updateStructuredProgress(const QString &operationId, const QString &phase,
												 const int completedItems, const int totalItems,
												 const qulonglong currentPluginId, const qint64 bytesReceived,
												 const qint64 bytesTotal) {
	const int rowIndex = indexOf(operationId.trimmed());
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	if (row.isEmpty()) return false;
	const QString status = row.value(QStringLiteral("status")).toString();
	if (status != QLatin1String("running") && status != QLatin1String("cancelling")) return false;
	const QString normalizedPhase = phase.trimmed().toLower();
	const int normalizedTotal = totalItems < 0
		? row.value(QStringLiteral("totalItems"), -1).toInt()
		: totalItems;
	const int completed = normalizedTotal >= 0 ? qBound(0, completedItems, normalizedTotal)
											 : qMax(0, completedItems);
	setOperationField(row, QStringLiteral("phase"), normalizedPhase);
	setOperationField(row, QStringLiteral("completedItems"), completed);
	setOperationField(row, QStringLiteral("totalItems"), normalizedTotal);
	setOperationField(row, QStringLiteral("currentPluginId"),
		currentPluginId == 0 ? QVariant() : QVariant::fromValue(currentPluginId));
	setOperationField(row, QStringLiteral("bytesReceived"), bytesReceived);
	setOperationField(row, QStringLiteral("bytesTotal"), bytesTotal);
	if (normalizedPhase.contains(QLatin1String("noncancellable"))) {
		setOperationField(row, QStringLiteral("cancellable"), false);
	}

	double completedFraction = static_cast< double >(completed);
	if (normalizedTotal > 0 && bytesTotal > 0 && completed < normalizedTotal) {
		completedFraction += qBound(0.0, static_cast< double >(qMax< qint64 >(0, bytesReceived))
											 / static_cast< double >(bytesTotal), 1.0);
	}
	int progress = -1;
	if (normalizedTotal > 0) {
		progress = qBound(0, qRound((completedFraction * 100.0) / static_cast< double >(normalizedTotal)), 100);
	} else if (normalizedTotal == 0) {
		progress = 100;
	} else if (bytesTotal > 0) {
		progress = qBound(0, qRound(qBound(0.0, static_cast< double >(qMax< qint64 >(0, bytesReceived))
											 / static_cast< double >(bytesTotal), 1.0) * 100.0), 100);
	}
	setOperationField(row, QStringLiteral("progress"), progress);
	setOperationField(row, QStringLiteral("indeterminate"), progress < 0);
	upsertRow(row);
	return true;
}

bool AsyncOperationModel::appendItemResult(const QString &operationId, const QString &itemId,
											 const qulonglong pluginId, const bool success,
											 const bool cancelled, const QString &errorCode,
											 const QString &message) {
	const int rowIndex = indexOf(operationId.trimmed());
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	const QString id = itemId.trimmed();
	if (row.isEmpty() || id.isEmpty()) return false;
	const QString status = row.value(QStringLiteral("status")).toString();
	if (status != QLatin1String("running") && status != QLatin1String("cancelling")) return false;
	QVariantMap result { { QStringLiteral("itemId"), id },
		{ QStringLiteral("pluginId"), pluginId == 0 ? QVariant() : QVariant::fromValue(pluginId) },
		{ QStringLiteral("success"), success }, { QStringLiteral("cancelled"), cancelled },
		{ QStringLiteral("errorCode"), errorCode.trimmed() }, { QStringLiteral("message"), message } };
	ItemResultStore &store = m_itemResultsByOperation[operationId.trimmed()];
	const int existingIndex = store.indexByItemId.value(id, -1);
	if (existingIndex >= 0) {
		const bool wasUnsuccessful = !store.results.at(existingIndex).toMap().value(QStringLiteral("success")).toBool();
		const bool isUnsuccessful = !success;
		store.results[existingIndex] = result;
		if (wasUnsuccessful != isUnsuccessful) store.unsuccessfulCount += isUnsuccessful ? 1 : -1;
	} else {
		store.indexByItemId.insert(id, static_cast< int >(store.results.size()));
		store.results.push_back(result);
		if (!success) ++store.unsuccessfulCount;
	}
	++store.revision;
	setOperationField(row, QStringLiteral("itemResultCount"), static_cast< int >(store.results.size()));
	setOperationField(row, QStringLiteral("unsuccessfulItemResultCount"), store.unsuccessfulCount);
	setOperationField(row, QStringLiteral("itemResultRevision"), QVariant::fromValue(store.revision));
	setOperationField(row, QStringLiteral("currentPluginId"),
		pluginId == 0 ? QVariant() : QVariant::fromValue(pluginId));
	upsertRow(row);
	return true;
}

QVariantList AsyncOperationModel::itemResultPage(const QString &operationId, const int offset, const int limit,
												  const bool unsuccessfulOnly) const {
	const auto storeIt = m_itemResultsByOperation.constFind(operationId.trimmed());
	if (storeIt == m_itemResultsByOperation.cend() || offset < 0 || limit <= 0) return {};

	const int boundedLimit = qMin(limit, MaxOperationItemResultPageSize);
	QVariantList page;
	page.reserve(boundedLimit);
	int matchingIndex = 0;
	for (const QVariant &entry : storeIt->results) {
		if (unsuccessfulOnly && entry.toMap().value(QStringLiteral("success")).toBool()) continue;
		if (matchingIndex++ < offset) continue;
		page.push_back(entry);
		if (page.size() >= boundedLimit) break;
	}
	return page;
}

int AsyncOperationModel::itemResultCount(const QString &operationId, const bool unsuccessfulOnly) const {
	const auto storeIt = m_itemResultsByOperation.constFind(operationId.trimmed());
	if (storeIt == m_itemResultsByOperation.cend()) return 0;
	return unsuccessfulOnly ? storeIt->unsuccessfulCount : static_cast< int >(storeIt->results.size());
}

void AsyncOperationModel::finishOperation(const QString &operationId, const bool success, const QString &errorCode,
									   const QString &message) {
	const int rowIndex = indexOf(operationId.trimmed());
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	if (row.isEmpty()) return;
	const QString status = row.value(QStringLiteral("status")).toString();
	if (status != QLatin1String("running") && status != QLatin1String("cancelling")) return;
	setOperationField(row, QStringLiteral("cancellable"), false);
	setOperationField(row, QStringLiteral("indeterminate"), false);
	setOperationField(row, QStringLiteral("errorCode"), errorCode.trimmed());
	if (success) setOperationField(row, QStringLiteral("progress"), 100);
	setOperationField(row, QStringLiteral("status"), success ? QStringLiteral("succeeded")
		: errorCode.trimmed() == QLatin1String("cancelled") ? QStringLiteral("cancelled")
															 : QStringLiteral("failed"));
	row.insert(QStringLiteral("subtitle"), message);
	upsertRow(row);
	if (isPluginOperation(row) && (success || errorCode.trimmed() == QLatin1String("cancelled"))) {
		dismiss(operationId);
	}
}

bool AsyncOperationModel::finishStructuredOperation(const QString &operationId, const QString &status,
												 const int successfulItems, const int failedItems,
												 const int cancelledItems) {
	const int rowIndex = indexOf(operationId.trimmed());
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	if (row.isEmpty()) return false;
	const QString currentStatus = row.value(QStringLiteral("status")).toString();
	if (currentStatus != QLatin1String("running") && currentStatus != QLatin1String("cancelling")) return false;
	const QString terminalStatus = normalizedOperationStatus(status);
	const int succeeded = qMax(0, successfulItems);
	const int failed = qMax(0, failedItems);
	const int cancelled = qMax(0, cancelledItems);
	setOperationField(row, QStringLiteral("successfulItems"), succeeded);
	setOperationField(row, QStringLiteral("failedItems"), failed);
	setOperationField(row, QStringLiteral("cancelledItems"), cancelled);
	const int terminalItems = static_cast< int >(std::min< qint64 >(
		qint64(succeeded) + failed + cancelled, std::numeric_limits< int >::max()));
	setOperationField(row, QStringLiteral("completedItems"), terminalItems);
	setOperationField(row, QStringLiteral("totalItems"),
		qMax(row.value(QStringLiteral("totalItems")).toInt(), terminalItems));
	setOperationField(row, QStringLiteral("cancellable"), false);
	setOperationField(row, QStringLiteral("indeterminate"), false);
	setOperationField(row, QStringLiteral("progress"), 100);
	setOperationField(row, QStringLiteral("phase"), QStringLiteral("finished"));
	setOperationField(row, QStringLiteral("currentPluginId"), QVariant());
	setOperationField(row, QStringLiteral("status"), terminalStatus);
	QString summary;
	if (terminalStatus == QLatin1String("succeeded")) {
		summary = tr("%n item(s) completed", nullptr, succeeded);
	} else if (terminalStatus == QLatin1String("partial")) {
		summary = cancelled > 0 ? tr("%1 succeeded, %2 failed, %3 cancelled").arg(succeeded).arg(failed).arg(cancelled)
								: tr("%1 succeeded, %2 failed").arg(succeeded).arg(failed);
	} else if (terminalStatus == QLatin1String("cancelled")) {
		summary = cancelled > 0 ? tr("%n item(s) cancelled", nullptr, cancelled) : tr("Operation cancelled");
	} else {
		summary = failed > 0 ? tr("%n item(s) failed", nullptr, failed) : tr("Operation failed");
	}
	row.insert(QStringLiteral("subtitle"), summary);
	upsertRow(row);
	if (isPluginOperation(row)
		&& (terminalStatus == QLatin1String("succeeded") || terminalStatus == QLatin1String("cancelled"))) {
		dismiss(operationId);
	}
	return true;
}

void AsyncOperationModel::interruptOperations(const QString &prefix) {
	QStringList matchingOperationIds;
	for (int index = 0; index < rowCount(); ++index) {
		const QVariantMap row = get(index);
		const QString id = row.value(QStringLiteral("id")).toString();
		const QString status = row.value(QStringLiteral("status")).toString();
		if (id.startsWith(prefix)
			&& (status == QLatin1String("running") || status == QLatin1String("cancelling"))) {
			matchingOperationIds.push_back(id);
		}
	}
	for (const QString &id : std::as_const(matchingOperationIds)) {
		finishOperation(id, false, QStringLiteral("cancelled"), tr("Operation cancelled"));
	}
}

bool AsyncOperationModel::hasOperation(const QString &operationId) const {
	return indexOf(operationId.trimmed()) >= 0;
}

void AsyncOperationModel::cancel(const QString &operationId) {
	const QString id = operationId.trimmed();
	const int rowIndex = indexOf(id);
	QVariantMap row = rowIndex >= 0 ? get(rowIndex) : QVariantMap();
	if (row.isEmpty() || row.value(QStringLiteral("status")).toString() != QLatin1String("running")) return;
	const bool cancellable = row.value(QStringLiteral("cancellable"),
		row.value(QStringLiteral("payload")).toMap().value(QStringLiteral("cancellable"))).toBool();
	if (!cancellable) return;
	setOperationField(row, QStringLiteral("status"), QStringLiteral("cancelling"));
	row.insert(QStringLiteral("subtitle"), tr("Cancelling…"));
	setOperationField(row, QStringLiteral("cancellable"), false);
	setOperationField(row, QStringLiteral("cancellationRequested"), true);
	upsertRow(row);
	emit cancellationRequested(id);
}

void AsyncOperationModel::dismiss(const QString &operationId) {
	const QString id = operationId.trimmed();
	if (id.isEmpty()) return;

	for (int index = 0; index < rowCount(); ++index) {
		const QVariantMap row = get(index);
		if (row.value(QStringLiteral("id")).toString() != id) continue;
		const QString status = row.value(QStringLiteral("status")).toString();
		if (status == QLatin1String("running") || status == QLatin1String("cancelling")) return;
		removeRow(id);
		m_itemResultsByOperation.remove(id);
		return;
	}
}

void AsyncOperationModel::clear() {
	if (!acceptsFrontendStateMutation(this)) return;
	m_itemResultsByOperation.clear();
	StableListModel::clear();
}

AsyncOperationOverlayProxyModel::AsyncOperationOverlayProxyModel(QObject *parent)
	: QSortFilterProxyModel(parent) {
	setDynamicSortFilter(true);
}

bool AsyncOperationOverlayProxyModel::filterAcceptsRow(const int sourceRow,
													   const QModelIndex &sourceParent) const {
	if (!sourceModel()) return false;
	const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
	if (!sourceIndex.isValid()) return false;
	return !isPluginOperation(sourceModel()->data(sourceIndex, StableListModel::PayloadRole).toMap());
}

UiCommandController::UiCommandController(QObject *parent) : QObject(parent) {
}

void UiCommandController::selectScope(const QString &scopeToken) {
	if (!scopeToken.trimmed().isEmpty()) emit scopeSelectionRequested(scopeToken.trimmed());
}
void UiCommandController::selectScopeFromRail(const QString &scopeToken, const QString &railKind) {
	const QString normalizedScope = scopeToken.trimmed();
	if (!normalizedScope.isEmpty())
		emit scopeRailSelectionRequested(normalizedScope, railKind.trimmed().toLower());
}
void UiCommandController::joinVoiceChannel(const QString &scopeToken) {
	if (!scopeToken.trimmed().isEmpty()) emit voiceJoinRequested(scopeToken.trimmed());
}
void UiCommandController::selectParticipant(const QString &sessionId) {
	qulonglong session = 0;
	if (parseProtocolId(sessionId, false, &session))
		emit participantSelectionRequested(QString::number(session));
}
void UiCommandController::openDirectMessage(const QString &sessionId) {
	qulonglong session = 0;
	if (parseProtocolId(sessionId, false, &session))
		emit directMessageOpenRequested(QString::number(session));
}
void UiCommandController::moveParticipant(const QString &sessionId, const QString &targetScopeToken) {
	qulonglong session = 0;
	const QString target = normalizedChannelScopeToken(targetScopeToken);
	if (!parseProtocolId(sessionId, false, &session) || target.isEmpty()) return;
	emit participantMoveRequested(session, target);
}
void UiCommandController::moveScope(const QString &sourceScopeToken, const QString &targetScopeToken,
									const QString &placement) {
	const QString source = normalizedChannelScopeToken(sourceScopeToken);
	const QString target = normalizedChannelScopeToken(targetScopeToken);
	const QString normalizedPlacement = placement.trimmed().toLower();
	if (source.isEmpty() || scopeTokenHasRootId(source) || target.isEmpty() || source == target
		|| (normalizedPlacement != QLatin1String("before") && normalizedPlacement != QLatin1String("after")
			&& normalizedPlacement != QLatin1String("inside"))) return;
	emit scopeMoveRequested(source, target, normalizedPlacement);
}
void UiCommandController::sendMessage(const QString &message) {
	if (!message.trimmed().isEmpty()) emit messageSendRequested(message);
}
void UiCommandController::requestOlderMessages() { emit olderMessagesRequested(); }
void UiCommandController::markActiveScopeRead() { emit activeScopeMarkReadRequested(); }
void UiCommandController::requestPreviewHydration(const QString &scopeToken, const QVariantList &messageIds,
												  const bool highPriority) {
	constexpr qsizetype MaxHydrationBatchSize = 32;
	const QString scope = scopeToken.trimmed();
	if (scope.isEmpty()) return;

	QVariantList normalizedIds;
	QSet< qulonglong > seen;
	for (const QVariant &value : messageIds) {
		if (normalizedIds.size() >= MaxHydrationBatchSize) break;
		bool valid = false;
		const qulonglong messageId = value.toString().trimmed().toULongLong(&valid);
		if (!valid || messageId == 0 || messageId > MaxProtocolId || seen.contains(messageId)) continue;
		seen.insert(messageId);
		normalizedIds.push_back(QVariant::fromValue(messageId));
	}
	if (!normalizedIds.isEmpty()) emit previewHydrationRequested(scope, normalizedIds, highPriority);
}
void UiCommandController::cancelPendingReply() { emit pendingReplyCancelRequested(); }
void UiCommandController::chooseAttachment() { emit attachmentChooseRequested(); }
void UiCommandController::openChatAttachment(const QString &assetId, const QString &fileName) {
	bool valid = false;
	const qulonglong parsed = assetId.trimmed().toULongLong(&valid);
	if (!valid || parsed == 0 || parsed > std::numeric_limits< unsigned int >::max()) return;
	emit chatAttachmentOpenRequested(static_cast< unsigned int >(parsed), QFileInfo(fileName).fileName().left(255));
}
void UiCommandController::downloadChatAttachment(const QString &assetId, const QString &fileName) {
	bool valid = false;
	const qulonglong parsed = assetId.trimmed().toULongLong(&valid);
	if (!valid || parsed == 0 || parsed > std::numeric_limits< unsigned int >::max()) return;
	emit chatAttachmentDownloadRequested(static_cast< unsigned int >(parsed), QFileInfo(fileName).fileName().left(255));
}
void UiCommandController::requestChatAttachmentImage(const QString &assetId, const QString &messageId) {
	bool validAsset = false;
	const qulonglong parsedAsset = assetId.trimmed().toULongLong(&validAsset);
	bool validMessage = false;
	const qulonglong parsedMessage = messageId.trimmed().toULongLong(&validMessage);
	if (!validAsset || parsedAsset == 0 || parsedAsset > std::numeric_limits< unsigned int >::max()
		|| !validMessage || parsedMessage == 0 || parsedMessage > MaxProtocolId) {
		return;
	}
	emit chatAttachmentImageRequested(
		static_cast< unsigned int >(parsedAsset), QString::number(parsedMessage));
}
void UiCommandController::requestChatInlineImage(const QString &token, const QString &messageId) {
	const QString normalizedToken = token.trimmed().toLower();
	static const QRegularExpression safeToken(QStringLiteral("^[0-9a-f]{24}$"));
	bool validMessage = false;
	const qulonglong parsedMessage = messageId.trimmed().toULongLong(&validMessage);
	if (!safeToken.match(normalizedToken).hasMatch() || !validMessage || parsedMessage == 0
		|| parsedMessage > MaxProtocolId) {
		return;
	}
	emit chatInlineImageRequested(normalizedToken, QString::number(parsedMessage));
}
void UiCommandController::retryChatAttachmentPreview(const QString &scopeToken, const QString &messageId,
											  const QString &assetId) {
	const QString scope = scopeToken.trimmed();
	bool validMessage = false;
	const qulonglong parsedMessage = messageId.trimmed().toULongLong(&validMessage);
	bool validAsset = false;
	const qulonglong parsedAsset = assetId.trimmed().toULongLong(&validAsset);
	if (scope.isEmpty() || !validMessage || parsedMessage == 0 || parsedMessage > MaxProtocolId
		|| !validAsset || parsedAsset == 0 || parsedAsset > MaxProtocolId) return;
	emit chatAttachmentPreviewRetryRequested(scope, QString::number(parsedMessage),
		static_cast< unsigned int >(parsedAsset));
}
void UiCommandController::saveChatInlineImage(const QString &token, const QString &fileName) {
	const QString normalizedToken = token.trimmed().toLower();
	static const QRegularExpression safeToken(QStringLiteral("^[0-9a-f]{24}$"));
	if (!safeToken.match(normalizedToken).hasMatch()) return;
	emit chatInlineImageSaveRequested(normalizedToken, QFileInfo(fileName).fileName().left(255));
}
void UiCommandController::replyToMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageReplyRequested(id);
}
void UiCommandController::retryMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageRetryRequested(id);
}
void UiCommandController::deleteMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (!id.isEmpty()) emit messageDeleteRequested(id);
}
void UiCommandController::toggleMessageReaction(const QString &messageId, const QString &emoji) {
	const QString id = messageId.trimmed();
	const QString reaction = emoji.trimmed();
	if (!id.isEmpty() && !reaction.isEmpty()) emit messageReactionToggleRequested(id, reaction);
}
void UiCommandController::invokeAction(const QString &actionId) {
	if (!actionId.trimmed().isEmpty()) emit actionRequested(actionId.trimmed());
}
void UiCommandController::invokeAppAction(const QString &actionId, const QVariantMap &payload) {
	const QString action = actionId.trimmed();
	if (!action.isEmpty()) emit appActionRequested(action, payload);
}
void UiCommandController::invokeScopeAction(const QString &scopeToken, const QString &actionId) {
	const QString scope = scopeToken.trimmed();
	const QString action = actionId.trimmed();
	if (!scope.isEmpty() && !action.isEmpty()) emit scopeActionRequested(scope, action);
}
void UiCommandController::invokeScopeActionValue(const QString &scopeToken, const QString &actionId,
												 const int value, const bool finalValue) {
	const QString scope = scopeToken.trimmed();
	const QString action = actionId.trimmed();
	if (!scope.isEmpty() && !action.isEmpty()) emit scopeActionValueRequested(scope, action, value, finalValue);
}
QVariantList UiCommandController::requestScopeActions(const QString &scopeToken, const QString &kind) const {
	const QString scope = scopeToken.trimmed();
	if (scope.isEmpty() || !m_scopeActionsProvider) return {};
	return m_scopeActionsProvider(scope, kind.trimmed().toLower());
}
void UiCommandController::setScopeActionsProvider(ScopeActionsProvider provider) {
	m_scopeActionsProvider = std::move(provider);
}
QVariantList UiCommandController::requestParticipantActions(const QString &sessionId, const QString &entryKind,
																		 const QString &scopeToken) const {
	const QString session = sessionId.trimmed();
	if (session.isEmpty() || !m_participantActionsProvider) return {};
	return m_participantActionsProvider(session, entryKind.trimmed().toLower(), scopeToken.trimmed());
}
void UiCommandController::setParticipantActionsProvider(ParticipantActionsProvider provider) {
	m_participantActionsProvider = std::move(provider);
}
void UiCommandController::invokeParticipantAction(const QString &sessionId, const QString &actionId) {
	qulonglong session = 0;
	const QString action = actionId.trimmed();
	if (parseProtocolId(sessionId, false, &session) && !action.isEmpty())
		emit participantActionRequested(QString::number(session), action);
}
void UiCommandController::invokeParticipantActionValue(const QString &sessionId, const QString &actionId,
													   const int value, const bool finalValue) {
	qulonglong session = 0;
	const QString action = actionId.trimmed();
	if (parseProtocolId(sessionId, false, &session) && !action.isEmpty()) {
		emit participantActionValueRequested(QString::number(session), action, value, finalValue);
	}
}
void UiCommandController::toggleSelfMute() { emit selfMuteToggleRequested(); }
void UiCommandController::toggleSelfDeaf() { emit selfDeafToggleRequested(); }
bool UiCommandController::pttPressed() const { return m_pttPressed; }
void UiCommandController::setPttPressed(const bool pressed) {
	if (m_pttPressed == pressed) return;
	m_pttPressed = pressed;
	emit pttPressedChanged();
	emit pttStateRequested(pressed);
}
void UiCommandController::releasePtt() { setPttPressed(false); }

PttSafetyController::PttSafetyController(UiCommandController *commands) : m_commands(commands) {
}

void PttSafetyController::release(PttSafetyReason reason) {
	Q_UNUSED(reason)
	if (m_commands) m_commands->releasePtt();
}

ActionModel::ActionModel(ClientActionRegistry *registry, QObject *parent)
	: StableListModel(parent), m_registry(registry) {
	if (m_registry) {
		connect(m_registry, &ClientActionRegistry::actionStateChanged, this, [this](const QString &) { refresh(); });
	}
	refresh();
}

void ActionModel::refresh() {
	QVariantList rows;
	if (m_registry) {
		for (const QVariant &entry : m_registry->stateSnapshot()) {
			const QVariantMap state = entry.toMap();
			QVariantMap row;
			row.insert(QStringLiteral("id"), state.value(QStringLiteral("id")));
			row.insert(QStringLiteral("title"), state.value(QStringLiteral("text")));
			row.insert(QStringLiteral("kind"), QStringLiteral("action"));
			row.insert(QStringLiteral("status"), state.value(QStringLiteral("checked")).toBool()
												? QStringLiteral("checked") : QString());
			row.insert(QStringLiteral("enabled"), state.value(QStringLiteral("enabled")));
			row.insert(QStringLiteral("checkable"), state.value(QStringLiteral("checkable")));
			row.insert(QStringLiteral("checked"), state.value(QStringLiteral("checked")));
			row.insert(QStringLiteral("shortcut"), state.value(QStringLiteral("shortcut")));
			row.insert(QStringLiteral("shortcutPortableText"), state.value(QStringLiteral("shortcutPortableText")));
			row.insert(QStringLiteral("menuRole"), state.value(QStringLiteral("menuRole")));
			row.insert(QStringLiteral("toolTip"), state.value(QStringLiteral("toolTip")));
			row.insert(QStringLiteral("visible"), state.value(QStringLiteral("visible"), true));
			row.insert(QStringLiteral("source"), state);
			rows.push_back(row);
		}
	}
	synchronizeRows(rows);
}

bool ActionModel::trigger(const QString &actionId) {
	QAction *action = m_registry ? m_registry->action(actionId) : nullptr;
	if (!action || !action->isEnabled()) {
		return false;
	}
	action->trigger();
	return true;
}

DialogStateController::DialogStateController(QObject *parent) : QObject(parent) {
}

bool DialogStateController::open() const { return m_state.value(QStringLiteral("open")).toBool(); }
QString DialogStateController::dialogId() const { return m_state.value(QStringLiteral("id")).toString(); }
QString DialogStateController::kind() const { return m_state.value(QStringLiteral("kind")).toString(); }
QString DialogStateController::title() const { return m_state.value(QStringLiteral("title")).toString(); }
QString DialogStateController::subtitle() const { return m_state.value(QStringLiteral("subtitle")).toString(); }
QString DialogStateController::activePage() const { return m_state.value(QStringLiteral("activePage")).toString(); }
QVariantList DialogStateController::pages() const { return m_state.value(QStringLiteral("pages")).toList(); }
QVariantList DialogStateController::sections() const { return m_state.value(QStringLiteral("sections")).toList(); }
QVariantList DialogStateController::actions() const { return m_state.value(QStringLiteral("actions")).toList(); }
QVariantList DialogStateController::favorites() const { return m_state.value(QStringLiteral("favorites")).toList(); }
int DialogStateController::selectedFavoriteIndex() const {
	return m_state.value(QStringLiteral("selectedFavoriteIndex"), -1).toInt();
}
bool DialogStateController::editorOpen() const { return m_state.value(QStringLiteral("editorOpen")).toBool(); }
QString DialogStateController::editorTitle() const { return m_state.value(QStringLiteral("editorTitle")).toString(); }
QString DialogStateController::primaryActionId() const {
	return m_state.value(QStringLiteral("primaryActionId")).toString();
}
bool DialogStateController::loading() const { return m_state.value(QStringLiteral("loading")).toBool(); }
QString DialogStateController::loadingScaffold() const {
	return m_state.value(QStringLiteral("loadingScaffold")).toString();
}
QString DialogStateController::statusMessage() const {
	const QString explicitMessage = m_state.value(QStringLiteral("statusMessage")).toString().trimmed();
	if (!explicitMessage.isEmpty()) return explicitMessage;
	const QVariant status = m_state.value(QStringLiteral("status"));
	if (status.metaType().id() == QMetaType::QVariantMap) {
		const QVariantMap statusMap = status.toMap();
		return statusMap.value(QStringLiteral("message"), statusMap.value(QStringLiteral("label"))).toString();
	}
	return status.toString();
}
QString DialogStateController::tone() const { return m_state.value(QStringLiteral("tone")).toString(); }
int DialogStateController::preferredWidth() const { return m_state.value(QStringLiteral("width"), 920).toInt(); }
int DialogStateController::preferredHeight() const { return m_state.value(QStringLiteral("height"), 700).toInt(); }
QString DialogStateController::initialFocusId() const {
	const QString explicitFocus = m_state.value(QStringLiteral("initialFocusId")).toString().trimmed();
	if (!explicitFocus.isEmpty()) return explicitFocus;
	const QString defaultFocus = m_state.value(QStringLiteral("defaultFocusId")).toString().trimmed();
	return defaultFocus.isEmpty() ? primaryActionId() : defaultFocus;
}
QVariantMap DialogStateController::state() const { return m_state; }
qulonglong DialogStateController::revision() const { return m_revision; }
QVariantMap DialogStateController::presentationFieldValues() const { return m_presentationFieldValues; }
QVariant DialogStateController::fieldValue(const QString &fieldId) const {
	for (const QVariant &sectionValue : sections()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == fieldId) return field.value(QStringLiteral("value"));
		}
	}
	return {};
}
QVariant DialogStateController::presentationFieldValue(const QString &fieldId) const {
	const QString id = fieldId.trimmed();
	const auto value = m_presentationFieldValues.constFind(id);
	return value == m_presentationFieldValues.cend() ? fieldValue(id) : value.value();
}
QString DialogStateController::fieldError(const QString &fieldId) const {
	return m_state.value(QStringLiteral("errors")).toMap().value(fieldId).toString();
}

void DialogStateController::applyState(const QVariantMap &state) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (m_state == state) return;
	const QString previousDialogId = dialogId();
	const bool previousOpen = open();
	const QString nextDialogId = state.value(QStringLiteral("id")).toString();
	const bool nextOpen = state.value(QStringLiteral("open")).toBool();
	const QSet< QString > nextPresentationFieldIds = dialogPresentationFieldIds(state);
	QVariantMap nextPresentationFieldValues;
	if (previousOpen && nextOpen && previousDialogId == nextDialogId) {
		for (auto value = m_presentationFieldValues.cbegin(); value != m_presentationFieldValues.cend(); ++value) {
			if (nextPresentationFieldIds.contains(value.key())) {
				nextPresentationFieldValues.insert(value.key(), value.value());
			}
		}
	}
	const bool presentationChanged = nextPresentationFieldValues != m_presentationFieldValues;
	m_state = state;
	m_state.detach();
	m_presentationFieldValues = nextPresentationFieldValues;
	m_presentationFieldIds = nextPresentationFieldIds;
	++m_revision;
	emit stateChanged();
	if (presentationChanged) emit presentationFieldValuesChanged();
}

bool DialogStateController::updatePresentationFieldValue(const QString &fieldId, const QVariant &value) {
	const QString id = fieldId.trimmed();
	if (id.isEmpty() || !open() || !acceptsFrontendStateMutation(this)) return false;
	if (!m_presentationFieldIds.contains(id)) return false;
	const auto existing = m_presentationFieldValues.constFind(id);
	if (existing != m_presentationFieldValues.cend() && existing.value() == value) return false;
	if (existing == m_presentationFieldValues.cend()
		&& m_presentationFieldValues.size() >= MaxDialogPresentationFieldValueCount) {
		return false;
	}
	m_presentationFieldValues.insert(id, value);
	emit presentationFieldValuesChanged();
	return true;
}

void DialogStateController::updateField(const QString &fieldId, const QVariant &value) {
	if (!open() || dialogId().isEmpty() || fieldId.trimmed().isEmpty()) return;
	emit fieldUpdateRequested(dialogId(), fieldId.trimmed(), value);
}

void DialogStateController::invokeAction(const QString &actionId, const QVariantMap &payload) {
	if (!open() || dialogId().isEmpty() || actionId.trimmed().isEmpty()) return;
	emit actionRequested(dialogId(), actionId.trimmed(), payload);
}

void DialogStateController::requestClose() {
	if (!open() || dialogId().isEmpty()) return;
	emit closeRequested(dialogId());
}

MediaSessionBackend::MediaSessionBackend(QObject *parent, const int sharedOperationAcknowledgementTimeoutMs)
	: QObject(parent), m_sharedOperationAcknowledgementTimer(new QTimer(this)),
	  m_sharedOperationAcknowledgementTimeoutMs(qMax(1, sharedOperationAcknowledgementTimeoutMs)) {
	m_sharedOperationAcknowledgementTimer->setSingleShot(true);
}

MediaSessionBackend::~MediaSessionBackend() {
	cancelSharedOperationAcknowledgementTimeout();
	if (m_nativePreparationToken) m_nativePreparationToken->store(false, std::memory_order_release);
	scheduleNativePlaybackCleanup(m_materializedPlaybackPaths, m_nativePlaybackSessionDirectory);
}

namespace {
const QHash< QString, QSet< QString > > &mediaProviderHosts() {
	static const QHash< QString, QSet< QString > > hosts {
		{ QStringLiteral("youtube"), { QStringLiteral("www.youtube.com"), QStringLiteral("youtube.com"),
									 QStringLiteral("www.youtube-nocookie.com"), QStringLiteral("youtube-nocookie.com") } },
		{ QStringLiteral("twitch"),
		  { QStringLiteral("player.twitch.tv"), QStringLiteral("clips.twitch.tv") } },
		{ QStringLiteral("streamable"), { QStringLiteral("streamable.com") } },
		{ QStringLiteral("vimeo"), { QStringLiteral("player.vimeo.com") } },
		{ QStringLiteral("dailymotion"), { QStringLiteral("geo.dailymotion.com") } },
		{ QStringLiteral("spotify"), { QStringLiteral("open.spotify.com") } },
		{ QStringLiteral("facebook"), { QStringLiteral("www.facebook.com") } },
		{ QStringLiteral("tiktok"), { QStringLiteral("www.tiktok.com") } },
		{ QStringLiteral("instagram"), { QStringLiteral("www.instagram.com") } },
		{ QStringLiteral("soundcloud"), { QStringLiteral("w.soundcloud.com") } }
	};
	return hosts;
}

QString canonicalMediaProvider(const QUrl &url, const QString &provider) {
	const QString requested = provider.trimmed().toLower();
	const QString host = url.host().toLower();
	if (requested == QLatin1String("direct")) {
		// Older protocol payloads only distinguish YouTube from a generic
		// provider. Infer that legacy value solely from the exact embed host;
		// never turn it into an arbitrary-origin WebEngine escape hatch.
		for (auto it = mediaProviderHosts().cbegin(); it != mediaProviderHosts().cend(); ++it) {
			if (it->contains(host)) return it.key();
		}
		return {};
	}
	const auto providerHosts = mediaProviderHosts().constFind(requested);
	return providerHosts != mediaProviderHosts().cend() && providerHosts->contains(host) ? requested : QString();
}

bool mediaProviderSupportsSynchronizedPlayback(const QString &provider) {
	static const QSet< QString > supported {
		QStringLiteral("direct"), QStringLiteral("youtube")
	};
	return supported.contains(provider.trimmed().toLower());
}

QString normalizedMediaErrorCode(const QString &code) {
	const QString normalized = code.trimmed().toLower().left(64);
	static const QRegularExpression validCode(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$"));
	return validCode.match(normalized).hasMatch() ? normalized : QStringLiteral("playback-failed");
}

QString normalizedSharedPresentationAspect(const QString &aspect) {
	const QString normalized = aspect.trimmed().toLower();
	static const QSet< QString > supported {
		QStringLiteral("wide"), QStringLiteral("short"), QStringLiteral("square")
	};
	return supported.contains(normalized) ? normalized : QStringLiteral("wide");
}
}

bool MediaSessionBackend::active() const { return m_active; }
bool MediaSessionBackend::sharedAvailable() const { return m_sharedAvailable; }
bool MediaSessionBackend::sharedJoined() const { return m_sharedJoined; }
bool MediaSessionBackend::sharedHost() const { return m_sharedHost; }
QString MediaSessionBackend::sharedTitle() const { return m_sharedTitle; }
QString MediaSessionBackend::sharedAspect() const { return m_sharedAspect; }
QString MediaSessionBackend::sharedSessionId() const { return m_sharedSessionId; }
qulonglong MediaSessionBackend::sharedScopeId() const { return m_sharedScopeId; }
qulonglong MediaSessionBackend::sharedHostSession() const { return m_sharedHostSession; }
int MediaSessionBackend::sharedParticipantCount() const { return m_sharedParticipantSessions.size(); }
QVariantList MediaSessionBackend::sharedParticipantSessions() const { return m_sharedParticipantSessions; }
QString MediaSessionBackend::sharedOperationStatus() const { return m_sharedOperationStatus; }
QString MediaSessionBackend::sharedOperationError() const { return m_sharedOperationError; }
QUrl MediaSessionBackend::url() const { return m_url; }
QUrl MediaSessionBackend::audioUrl() const { return m_audioUrl; }
QUrl MediaSessionBackend::playbackUrl() const { return m_playbackUrl; }
QUrl MediaSessionBackend::playbackAudioUrl() const { return m_playbackAudioUrl; }
QString MediaSessionBackend::playbackAudioWarning() const { return m_playbackAudioWarning; }
bool MediaSessionBackend::playbackSourceReady() const { return m_playbackSourceReady; }
bool MediaSessionBackend::playbackSourcePreparing() const { return m_playbackSourcePreparing; }
qulonglong MediaSessionBackend::playbackSourceGeneration() const { return m_playbackSourceGeneration; }
QString MediaSessionBackend::provider() const { return m_provider; }
bool MediaSessionBackend::detached() const { return m_detached; }
bool MediaSessionBackend::detachedPlaybackSupported() const {
#ifdef Q_OS_WIN
	// A second QQuickWindow can deadlock the threaded D3D11 render loop while
	// exposing its swapchain on Windows/NVIDIA systems. Inline WebEngine media
	// uses the already-established main-window scenegraph and remains safe.
	return false;
#else
	return true;
#endif
}
bool MediaSessionBackend::playbackControllable() const {
	return mediaProviderSupportsSynchronizedPlayback(m_provider);
}
bool MediaSessionBackend::playbackControlAllowed() const {
	return playbackControllable()
		&& (!m_sharedAvailable || (m_sharedHost && sharedScopeMatchesCurrentVoiceRoom()));
}
QString MediaSessionBackend::mediaMime() const { return m_mediaMime; }
QString MediaSessionBackend::audioMime() const { return m_audioMime; }
QString MediaSessionBackend::sessionId() const { return m_sessionId; }
QString MediaSessionBackend::state() const { return m_state; }
double MediaSessionBackend::position() const { return m_position; }
double MediaSessionBackend::duration() const { return m_duration; }
QString MediaSessionBackend::error() const { return m_error; }
QString MediaSessionBackend::errorCode() const { return m_errorCode; }
qulonglong MediaSessionBackend::syncGeneration() const { return m_syncGeneration; }
int MediaSessionBackend::loadProgress() const { return m_loadProgress; }
int MediaSessionBackend::volume() const { return m_volume; }
bool MediaSessionBackend::muted() const { return m_muted; }

void MediaSessionBackend::setCurrentVoiceScopeId(const qulonglong scopeId) {
	if (m_currentVoiceScopeId == scopeId) return;
	m_currentVoiceScopeId = scopeId;
	if (m_sharedAvailable && m_sharedScopeId != 0 && !sharedScopeMatchesCurrentVoiceRoom()) clearSharedState();
}

bool MediaSessionBackend::sharedScopeMatchesCurrentVoiceRoom() const {
	return m_currentVoiceScopeId != 0 && m_sharedScopeId != 0 && m_sharedScopeId == m_currentVoiceScopeId;
}

void MediaSessionBackend::armSharedOperationAcknowledgementTimeout(const SharedOperationKind kind,
														 const QString &sessionId) {
	cancelSharedOperationAcknowledgementTimeout();
	if (kind == SharedOperationKind::None || sessionId.isEmpty()) return;

	++m_sharedOperationGeneration;
	if (m_sharedOperationGeneration == 0) ++m_sharedOperationGeneration;
	const qulonglong generation = m_sharedOperationGeneration;
	m_pendingSharedOperationGeneration = generation;
	m_pendingSharedOperationKind = kind;
	m_sharedOperationAcknowledgementConnection = connect(
		m_sharedOperationAcknowledgementTimer, &QTimer::timeout, this,
		[this, generation, kind, sessionId]() { recoverTimedOutSharedOperation(generation, kind, sessionId); });
	m_sharedOperationAcknowledgementTimer->start(m_sharedOperationAcknowledgementTimeoutMs);
}

void MediaSessionBackend::cancelSharedOperationAcknowledgementTimeout() {
	if (m_sharedOperationAcknowledgementTimer) m_sharedOperationAcknowledgementTimer->stop();
	QObject::disconnect(m_sharedOperationAcknowledgementConnection);
	m_sharedOperationAcknowledgementConnection = {};
	m_pendingSharedOperationGeneration = 0;
	m_pendingSharedOperationKind = SharedOperationKind::None;
}

void MediaSessionBackend::recoverTimedOutSharedOperation(const qulonglong generation,
														 const SharedOperationKind kind,
														 const QString &sessionId) {
	if (generation == 0 || generation != m_pendingSharedOperationGeneration
		|| kind != m_pendingSharedOperationKind || sessionId != m_pendingExplicitSessionId
		|| sessionId != m_sharedSessionId) {
		return;
	}

	cancelSharedOperationAcknowledgementTimeout();
	if (!m_sharedAvailable || m_sharedJoined) return;
	m_pendingExplicitSessionId.clear();

	const QString message = kind == SharedOperationKind::Start
		? tr("The server did not confirm the watch-together start. Try again.")
		: tr("The server did not confirm joining the watch-together session. Try again.");
	if (kind == SharedOperationKind::Start) {
		clearSharedState();
		m_sharedOperationStatus = QStringLiteral("idle");
	} else {
		m_state = QStringLiteral("available");
		m_sharedOperationStatus = QStringLiteral("available");
	}
	m_sharedOperationError = message;
	emit stateChanged();
	emit playbackRejected(message);
}

bool MediaSessionBackend::validateSource(const QUrl &url, const QString &provider, QUrl *normalized,
										 QString *error) const {
	const QUrl candidate = url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
	if (!candidate.isValid() || candidate.scheme() != QLatin1String("https") || candidate.host().isEmpty()
		|| !candidate.userInfo().isEmpty() || (candidate.port(-1) != -1 && candidate.port(-1) != 443)) {
		if (error) *error = tr("Media playback requires a valid HTTPS URL.");
		return false;
	}

	if (canonicalMediaProvider(candidate, provider).isEmpty()) {
		if (error) *error = tr("The media embed URL is not allowed for this provider.");
		return false;
	}

	if (normalized) *normalized = candidate;
	return true;
}

bool MediaSessionBackend::validateDirectSource(const QUrl &url, const QString &mime, const bool audio,
											QUrl *normalized, QString *error) const {
	const QString normalizedMime = normalizedMediaMime(mime);
	const QString safeSource = safeDirectMediaSource(url.toString(), normalizedMime, audio);
	if (safeSource.isEmpty()) {
		if (error) {
			*error = audio ? tr("Direct audio requires a bounded HTTPS or base64 media URL with a supported MIME type.")
						   : tr("Direct video requires a bounded HTTPS or base64 media URL with a supported MIME type.");
		}
		return false;
	}
	if (normalized) *normalized = QUrl(safeSource);
	return true;
}

bool MediaSessionBackend::open(const QUrl &url, const QString &provider, const QString &sessionId) {
	return openWithPresentation(url, provider, sessionId, true);
}

bool MediaSessionBackend::openInline(const QUrl &url, const QString &provider, const QString &sessionId) {
	return openWithPresentation(url, provider, sessionId, false);
}

bool MediaSessionBackend::openWithPresentation(const QUrl &url, const QString &provider,
												 const QString &sessionId, const bool detached) {
	const bool effectiveDetached = detached && detachedPlaybackSupported();
	const QString requestedSessionId = sessionId.trimmed();
	if (m_sharedAvailable && (!m_sharedJoined || requestedSessionId != m_sharedSessionId)) {
		emit playbackRejected(tr("Leave or end the current watch-together session before opening other media."));
		return false;
	}

	QUrl normalized;
	QString validationError;
	if (!validateSource(url, provider, &normalized, &validationError)) {
		rejectPlayback(validationError);
		return false;
	}
	const QString normalizedProvider = canonicalMediaProvider(normalized, provider);
	invalidateNativePlaybackSources();
	m_active = true;
	m_url = normalized;
	m_audioUrl = {};
	m_provider = normalizedProvider;
	m_detached = effectiveDetached;
	m_mediaMime.clear();
	m_audioMime.clear();
	m_sessionId = requestedSessionId;
	m_state = QStringLiteral("loading");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	m_errorCode.clear();
	m_remoteStateSessionId.clear();
	m_remoteStateGeneration = 0;
	updateLoadProgress(0);
	++m_syncGeneration;
	emit sourceChanged();
	emit stateChanged();
	return true;
}

bool MediaSessionBackend::openDirect(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
									 const QString &audioMime, const QString &sessionId) {
	return openDirectWithPresentation(url, mediaMime, audioUrl, audioMime, sessionId, true);
}

bool MediaSessionBackend::openDirectInline(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
										   const QString &audioMime, const QString &sessionId) {
	return openDirectWithPresentation(url, mediaMime, audioUrl, audioMime, sessionId, false);
}

bool MediaSessionBackend::openDirectWithPresentation(const QUrl &url, const QString &mediaMime,
											   const QUrl &audioUrl, const QString &audioMime,
											   const QString &sessionId, const bool detached) {
	const bool effectiveDetached = detached && detachedPlaybackSupported();
	if (m_sharedAvailable) {
		emit playbackRejected(tr("Leave or end the current watch-together session before opening other media."));
		return false;
	}

	const QString normalizedMediaMime = ::normalizedMediaMime(mediaMime);
	const bool primaryIsAudio = normalizedMediaMime.startsWith(QLatin1String("audio/"));
	QUrl normalizedMediaUrl;
	QString validationError;
	if (!validateDirectSource(url, normalizedMediaMime, primaryIsAudio, &normalizedMediaUrl, &validationError)) {
		rejectPlayback(validationError);
		return false;
	}

	QUrl normalizedAudioUrl;
	const QString normalizedAudioMime = ::normalizedMediaMime(audioMime);
	if (!audioUrl.isEmpty()) {
		if (primaryIsAudio
			|| !validateDirectSource(audioUrl, normalizedAudioMime, true, &normalizedAudioUrl, &validationError)) {
			rejectPlayback(primaryIsAudio ? tr("A direct audio source cannot have a secondary audio track.")
										: validationError);
			return false;
		}
	}

	invalidateNativePlaybackSources();
	m_active = true;
	m_url = normalizedMediaUrl;
	m_audioUrl = normalizedAudioUrl;
	m_provider = QStringLiteral("direct");
	m_mediaMime = normalizedMediaMime;
	m_audioMime = normalizedAudioMime;
	m_detached = effectiveDetached;
	m_sessionId = sessionId.trimmed();
	m_state = QStringLiteral("loading");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	m_errorCode.clear();
	m_remoteStateSessionId.clear();
	m_remoteStateGeneration = 0;
	updateLoadProgress(0);
	++m_syncGeneration;
	emit sourceChanged();
	emit stateChanged();
	prepareNativePlaybackSources();
	return true;
}

void MediaSessionBackend::invalidateNativePlaybackSources() {
	if (m_nativePreparationToken) m_nativePreparationToken->store(false, std::memory_order_release);
	m_nativePreparationToken.reset();
	const QStringList stalePaths = std::exchange(m_materializedPlaybackPaths, {});
	m_playbackUrl = {};
	m_playbackAudioUrl = {};
	m_playbackAudioWarning.clear();
	m_playbackSourceReady = false;
	m_playbackSourcePreparing = false;
	++m_playbackSourceGeneration;
	emit playbackSourceChanged();
	scheduleNativePlaybackCleanup(stalePaths, m_nativePlaybackSessionDirectory);
}

void MediaSessionBackend::prepareNativePlaybackSources() {
	if (!m_active || m_provider != QLatin1String("direct") || m_url.isEmpty()) return;
	const qulonglong generation = m_playbackSourceGeneration;
	if (m_url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
		&& (m_audioUrl.isEmpty()
			|| m_audioUrl.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0)) {
		m_playbackUrl = m_url;
		m_playbackAudioUrl = m_audioUrl;
		m_playbackAudioWarning.clear();
		m_playbackSourceReady = true;
		m_playbackSourcePreparing = false;
		emit playbackSourceChanged();
		return;
	}

	const QString cacheRoot = nativeMediaCacheRoot();
	if (m_nativePlaybackSessionDirectory.isEmpty()) {
		m_nativePlaybackSessionDirectory = QDir(cacheRoot).filePath(
			QUuid::createUuid().toString(QUuid::WithoutBraces));
	}
	const auto token = std::make_shared< std::atomic_bool >(true);
	m_nativePreparationToken = token;
	m_playbackSourcePreparing = true;
	m_playbackSourceReady = false;
	emit playbackSourceChanged();

	auto *watcher = new QFutureWatcher< NativePlaybackPreparationResult >(this);
	connect(watcher, &QFutureWatcher< NativePlaybackPreparationResult >::finished, this,
		[this, watcher, token, generation]() {
			const NativePlaybackPreparationResult result = watcher->result();
			watcher->deleteLater();
			if (!token->load(std::memory_order_acquire)
				|| result.cancelled || result.generation != m_playbackSourceGeneration
				|| generation != m_playbackSourceGeneration || !m_active
				|| m_provider != QLatin1String("direct")) {
				scheduleNativePlaybackCleanup(result.materializedPaths, m_nativePlaybackSessionDirectory);
				return;
			}

			m_nativePreparationToken.reset();
			m_playbackSourcePreparing = false;
			if (!result.error.isEmpty()) {
				m_playbackSourceReady = false;
				emit playbackSourceChanged();
				reportTypedError(QStringLiteral("native-source-prepare-failed"), result.error);
				return;
			}
			m_playbackUrl = result.mediaUrl;
			m_playbackAudioUrl = result.audioUrl;
			m_playbackAudioWarning = result.audioWarning.isEmpty()
				? QString()
				: tr("The separate audio track could not be prepared: %1").arg(result.audioWarning);
			m_materializedPlaybackPaths = result.materializedPaths;
			m_playbackSourceReady = true;
			emit playbackSourceChanged();
		});
	watcher->setFuture(QtConcurrent::run([generation, mediaUrl = m_url, mediaMime = m_mediaMime,
			audioUrl = m_audioUrl, audioMime = m_audioMime, cacheRoot,
			sessionDirectory = m_nativePlaybackSessionDirectory, token]() {
		return materializeNativePlaybackSources(generation, mediaUrl, mediaMime, audioUrl, audioMime,
			cacheRoot, sessionDirectory, token);
	}));
}

bool MediaSessionBackend::startShared(const QUrl &url, const QString &provider, const QString &title,
									  const QString &presentationAspect) {
	QUrl normalized;
	QString validationError;
	if (!validateSource(url, provider, &normalized, &validationError)) {
		rejectPlayback(validationError);
		return false;
	}
	if (m_sharedAvailable) {
		reportError(tr("Leave or end the current watch-together session first."));
		return false;
	}
	if (m_currentVoiceScopeId == 0) {
		rejectPlayback(tr("Join a voice room before using Watch Together."));
		return false;
	}
	const QString normalizedProvider = canonicalMediaProvider(normalized, provider);
	if (!mediaProviderSupportsSynchronizedPlayback(normalizedProvider)) {
		rejectPlayback(tr("This provider does not expose synchronized playback controls. Open it in your browser instead."));
		return false;
	}

	const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_pendingExplicitSessionId = sessionId;
	m_sharedAvailable = true;
	m_sharedJoined = false;
	m_sharedHost = false;
	m_sharedTitle = title.trimmed();
	m_sharedAspect = normalizedSharedPresentationAspect(presentationAspect);
	m_sharedSessionId = sessionId;
	m_sharedUrl = normalized;
	m_sharedProvider = normalizedProvider;
	m_sharedScopeId = m_currentVoiceScopeId;
	m_sharedParticipantSessions.clear();
	m_sharedOperationStatus = QStringLiteral("starting");
	m_sharedOperationError.clear();
	m_sharedPlayerSuppressed = false;
	m_sharedPosition = 0.0;
	m_sharedPaused = true;
	m_sharedGeneration = 0;
	m_state = QStringLiteral("starting");
	m_error.clear();
	m_errorCode.clear();
	armSharedOperationAcknowledgementTimeout(SharedOperationKind::Start, sessionId);
	emit stateChanged();
	emit sharedStartRequested(sessionId, normalized, m_sharedProvider, m_sharedTitle, m_sharedAspect);
	return true;
}

void MediaSessionBackend::joinShared() {
	if (!m_sharedAvailable || m_sharedSessionId.isEmpty() || m_sharedJoined) return;
	if (!sharedScopeMatchesCurrentVoiceRoom()) {
		clearSharedState();
		return;
	}
	m_pendingExplicitSessionId = m_sharedSessionId;
	m_sharedOperationStatus = QStringLiteral("starting");
	m_sharedOperationError.clear();
	armSharedOperationAcknowledgementTimeout(SharedOperationKind::Join, m_sharedSessionId);
	emit stateChanged();
	emit sharedEventRequested(m_sharedSessionId, QStringLiteral("join"), 0);
}

void MediaSessionBackend::leaveShared() {
	cancelSharedOperationAcknowledgementTimeout();
	if (!m_sharedAvailable || m_sharedSessionId.isEmpty()) return;
	if (m_sharedHost) {
		endShared();
		return;
	}
	if (m_sharedJoined) emit sharedEventRequested(m_sharedSessionId, QStringLiteral("leave"), 0);
	m_pendingExplicitSessionId.clear();
	m_sharedJoined = false;
	m_sharedOperationStatus = QStringLiteral("available");
	m_sharedOperationError.clear();
	m_sharedPlayerSuppressed = false;
	closePlayer();
	emit stateChanged();
}

void MediaSessionBackend::endShared() {
	cancelSharedOperationAcknowledgementTimeout();
	if (!m_sharedAvailable || m_sharedSessionId.isEmpty() || !m_sharedHost) return;
	emit sharedEventRequested(m_sharedSessionId, QStringLiteral("end"), 0);
	clearSharedState();
}

void MediaSessionBackend::transferSharedHost(const QString &sessionId) {
	bool valid = false;
	const qulonglong target = sessionId.trimmed().toULongLong(&valid);
	if (!m_sharedAvailable || !m_sharedHost || !valid || target == 0 || target > MaxProtocolId
		|| target == m_sharedHostSession) return;
	emit sharedEventRequested(m_sharedSessionId, QStringLiteral("host-transfer"), target);
}

bool MediaSessionBackend::reopenSharedPlayer() {
	if (!m_sharedAvailable || !m_sharedJoined || m_sharedUrl.isEmpty()
		|| !sharedScopeMatchesCurrentVoiceRoom()) return false;
	m_sharedPlayerSuppressed = false;
	m_sharedOperationStatus = QStringLiteral("reconnecting");
	m_sharedOperationError.clear();
	emit stateChanged();
	if (!openWithPresentation(m_sharedUrl, m_sharedProvider, m_sharedSessionId,
			detachedPlaybackSupported())) {
		return false;
	}
	m_remoteStateSessionId = m_sharedSessionId;
	m_remoteStateGeneration = m_sharedGeneration;
	m_position = m_sharedPosition;
	m_state = m_sharedPaused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_syncGeneration = qMax(m_syncGeneration, m_sharedGeneration);
	emit stateChanged();
	return true;
}

void MediaSessionBackend::retry() {
	if (!m_active || m_url.isEmpty()) return;
	m_state = QStringLiteral("loading");
	m_error.clear();
	m_errorCode.clear();
	if (m_sharedAvailable) {
		m_sharedOperationStatus = QStringLiteral("reconnecting");
		m_sharedOperationError.clear();
	}
	updateLoadProgress(0);
	if (m_provider == QLatin1String("direct") && !m_playbackSourceReady) {
		invalidateNativePlaybackSources();
		prepareNativePlaybackSources();
	}
	emit stateChanged();
	emit retryRequested();
}

bool MediaSessionBackend::isNavigationAllowed(const QUrl &url) const {
	if (url == QUrl(QStringLiteral("about:blank"))) return true;
	if (!m_active) return false;
	if (m_provider == QLatin1String("direct")) {
		const QUrl candidate = url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
		return candidate == m_url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment)
			|| (!m_audioUrl.isEmpty()
				&& candidate == m_audioUrl.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment));
	}
	if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty()
		|| !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port(-1) != 443)) return false;
	const auto providerHosts = mediaProviderHosts().constFind(m_provider);
	return providerHosts != mediaProviderHosts().cend() && providerHosts->contains(url.host().toLower());
}

bool MediaSessionBackend::supportsSynchronizedPlayback(const QString &provider) const {
	return mediaProviderSupportsSynchronizedPlayback(provider);
}

void MediaSessionBackend::detach() {
	if (!m_active || m_detached || !detachedPlaybackSupported()) return;
	m_detached = true;
	emit sourceChanged();
	emit stateChanged();
}

void MediaSessionBackend::attach() {
	if (!m_active || !m_detached) return;
	m_detached = false;
	emit sourceChanged();
	emit stateChanged();
}

void MediaSessionBackend::closePlayer() {
	invalidateNativePlaybackSources();
	if (!m_active) {
		if (m_sharedAvailable && m_state != QLatin1String("available")) {
			m_state = QStringLiteral("available");
			m_sharedOperationStatus = m_sharedJoined ? QStringLiteral("ready") : QStringLiteral("available");
			m_sharedOperationError.clear();
			emit stateChanged();
		}
		return;
	}
	if (m_sharedAvailable && m_sharedJoined) {
		m_sharedPlayerSuppressed = true;
		m_sharedPosition = m_position;
		m_sharedPaused = m_state != QLatin1String("playing");
		m_sharedGeneration = qMax(m_sharedGeneration, m_syncGeneration);
	}
	m_active = false;
	m_url = {};
	m_audioUrl = {};
	m_provider.clear();
	m_detached = true;
	m_mediaMime.clear();
	m_audioMime.clear();
	m_sessionId.clear();
	m_state = m_sharedAvailable ? QStringLiteral("available") : QStringLiteral("idle");
	if (m_sharedAvailable) {
		m_sharedOperationStatus = m_sharedJoined ? QStringLiteral("ready") : QStringLiteral("available");
		m_sharedOperationError.clear();
	}
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	m_errorCode.clear();
	m_remoteStateSessionId.clear();
	m_remoteStateGeneration = 0;
	updateLoadProgress(0);
	++m_syncGeneration;
	emit sourceChanged();
	emit stateChanged();
}

void MediaSessionBackend::close() {
	if (m_pendingSharedOperationKind == SharedOperationKind::Start) {
		clearSharedState();
		return;
	}
	if (m_pendingSharedOperationKind == SharedOperationKind::Join) {
		leaveShared();
		return;
	}
	cancelSharedOperationAcknowledgementTimeout();
	if (m_sharedJoined) {
		leaveShared();
		return;
	}
	closePlayer();
}

void MediaSessionBackend::play() {
	if (!m_active || !playbackControlAllowed()) return;
	m_state = QStringLiteral("playing");
	m_error.clear();
	m_errorCode.clear();
	if (m_sharedAvailable && m_sharedJoined) {
		m_sharedPosition = m_position;
		m_sharedPaused = false;
	}
	emit stateChanged();
	emit playRequested();
	publishSharedPlaybackState(m_position, false, true);
}

void MediaSessionBackend::pause() {
	if (!m_active || !playbackControlAllowed()) return;
	m_state = QStringLiteral("paused");
	m_error.clear();
	m_errorCode.clear();
	if (m_sharedAvailable && m_sharedJoined) {
		m_sharedPosition = m_position;
		m_sharedPaused = true;
	}
	emit stateChanged();
	emit pauseRequested();
	publishSharedPlaybackState(m_position, true, true);
}

void MediaSessionBackend::seek(const double seconds) {
	if (!m_active || !playbackControlAllowed() || !qIsFinite(seconds) || seconds < 0.0) return;
	m_position = m_duration > 0.0 ? qMin(seconds, m_duration) : seconds;
	if (m_sharedAvailable && m_sharedJoined) {
		m_sharedPosition = m_position;
		m_sharedPaused = m_state != QLatin1String("playing");
	}
	emit stateChanged();
	emit seekRequested(m_position);
	publishSharedPlaybackState(m_position, m_state != QLatin1String("playing"), true);
}

void MediaSessionBackend::setVolume(const int volume) {
	const int normalized = qBound(0, volume, 100);
	if (m_volume == normalized) return;
	m_volume = normalized;
	emit volumeChanged();
	if (m_active) emit volumeRequested(m_volume);
}

void MediaSessionBackend::setMuted(const bool muted) {
	if (m_muted == muted) return;
	m_muted = muted;
	emit mutedChanged();
	if (m_active) emit mutedRequested(m_muted);
}

void MediaSessionBackend::toggleMuted() { setMuted(!m_muted); }

void MediaSessionBackend::updateLoadProgress(const int progress) {
	const int normalized = qBound(0, progress, 100);
	if (m_loadProgress == normalized) return;
	m_loadProgress = normalized;
	emit loadProgressChanged();
}

void MediaSessionBackend::reportLoadProgress(const int progress) {
	if (!m_active) return;
	const int normalized = qBound(0, progress, 100);
	updateLoadProgress(normalized);
	bool sharedOperationChanged = false;
	if (normalized == 100 && m_sharedAvailable && m_sharedJoined
		&& m_sharedOperationStatus != QLatin1String("ready")) {
		m_sharedOperationStatus = QStringLiteral("ready");
		m_sharedOperationError.clear();
		sharedOperationChanged = true;
	}
	// Some allowlisted providers expose their own controls inside the isolated
	// embed instead of a scriptable transport API. A successful document load is
	// therefore their ready signal; leaving them in "loading" would permanently
	// cover the usable provider UI with our loading scrim.
	if (normalized == 100 && !playbackControllable() && m_state == QLatin1String("loading")) {
		m_state = QStringLiteral("ready");
		m_error.clear();
		m_errorCode.clear();
		emit stateChanged();
	} else if (sharedOperationChanged) {
		emit stateChanged();
	}
}

void MediaSessionBackend::reportPlaybackState(const double position, const double duration, const bool paused) {
	if (!m_active) return;
	m_position = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	m_duration = qIsFinite(duration) ? qMax(0.0, duration) : 0.0;
	m_state = paused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_error.clear();
	m_errorCode.clear();
	if (m_sharedAvailable && m_sharedJoined) {
		m_sharedOperationStatus = QStringLiteral("ready");
		m_sharedOperationError.clear();
		m_sharedPosition = m_position;
		m_sharedPaused = paused;
		m_sharedGeneration = qMax(m_sharedGeneration, m_syncGeneration);
	}
	updateLoadProgress(100);
	emit stateChanged();
	publishSharedPlaybackState(m_position, paused, false);
}

void MediaSessionBackend::reportError(const QString &message) {
	reportTypedError(QStringLiteral("playback-failed"), message);
}

void MediaSessionBackend::reportTypedError(const QString &code, const QString &message) {
	m_state = QStringLiteral("error");
	m_error = message.trimmed().isEmpty() ? tr("Media playback failed.") : message.trimmed();
	m_errorCode = normalizedMediaErrorCode(code);
	if (m_sharedAvailable) {
		m_sharedOperationStatus = QStringLiteral("error");
		m_sharedOperationError = m_error;
	}
	emit stateChanged();
}

void MediaSessionBackend::rejectPlayback(const QString &message) {
	// Preserve typed, inspectable state for the owning card or automation. The
	// shell deliberately does not turn this into a global toast over unrelated
	// chat or media content.
	reportTypedError(QStringLiteral("source-rejected"), message);
	emit playbackRejected(m_error);
}

void MediaSessionBackend::applyRemoteState(const QUrl &url, const QString &provider, const QString &sessionId,
										   const double position, const bool paused, const qulonglong generation) {
	const QString remoteSessionId = sessionId.trimmed();
	const bool sameRemoteSession = !remoteSessionId.isEmpty()
		&& remoteSessionId == m_remoteStateSessionId;
	if (sameRemoteSession && generation != 0 && m_remoteStateGeneration != 0
		&& generation < m_remoteStateGeneration) {
		return;
	}
	const QString previousState = m_state;
	const double previousPosition = m_position;
	const bool sourceChanged = !m_active || m_sessionId != remoteSessionId || m_url != url;
	if (sourceChanged && !openWithPresentation(url, provider, remoteSessionId,
			detachedPlaybackSupported())) {
		return;
	}
	m_remoteStateSessionId = remoteSessionId;
	if (generation != 0 || !sameRemoteSession) m_remoteStateGeneration = generation;
	m_syncGeneration = generation == 0 ? m_syncGeneration + 1 : qMax(m_syncGeneration, generation);
	const double targetPosition = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	const double drift = qAbs(targetPosition - previousPosition);
	const double correctionThreshold = paused ? 0.35 : 1.5;
	const bool playbackTransition = sourceChanged
		|| (paused ? previousState != QLatin1String("paused") : previousState != QLatin1String("playing"));
	const bool needsSeek = sourceChanged || drift > correctionThreshold;
	m_position = targetPosition;
	m_state = paused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_error.clear();
	m_errorCode.clear();
	if (m_sharedAvailable && m_sharedJoined && sessionId == m_sharedSessionId) {
		m_sharedOperationStatus = QStringLiteral("ready");
		m_sharedOperationError.clear();
		m_sharedPosition = targetPosition;
		m_sharedPaused = paused;
		m_sharedGeneration = qMax(m_sharedGeneration, generation);
	}
	if (needsSeek) emit seekRequested(m_position);
	if (playbackTransition) {
		if (paused) emit pauseRequested();
		else emit playRequested();
	}
	emit stateChanged();
}

void MediaSessionBackend::publishSharedPlaybackState(const double position, const bool paused, const bool force) {
	if (!m_sharedAvailable || !m_sharedJoined || !m_sharedHost || m_sharedSessionId.isEmpty()
		|| !sharedScopeMatchesCurrentVoiceRoom()) return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool stateChanged = paused != m_lastSharedPublishPaused;
	const bool positionChanged = m_lastSharedPublishPosition < 0.0
		|| qAbs(position - m_lastSharedPublishPosition) >= 0.75;
	if (!force && !stateChanged && (!positionChanged || now - m_lastSharedPublishMs < 1000)) return;
	m_lastSharedPublishMs = now;
	m_lastSharedPublishPosition = position;
	m_lastSharedPublishPaused = paused;
	emit sharedPlaybackStateRequested(m_sharedSessionId, position, paused);
}

void MediaSessionBackend::applySharedState(const QString &sessionId, const QUrl &url, const QString &provider,
										 const QString &title, const qulonglong scopeId,
										 const qulonglong actorSession, const qulonglong hostSession,
										 const QVariantList &participantSessions, const QString &event,
										 double position, const bool paused, const qulonglong generation,
										 const qulonglong selfSession,
										 const QString &presentationAspect) {
	const QString id = sessionId.trimmed();
	const QString normalizedEvent = event.trimmed().toLower();
	if (id.isEmpty()) return;
	if (scopeId == 0 || scopeId != m_currentVoiceScopeId) {
		if (m_sharedSessionId == id) clearSharedState();
		return;
	}
	if (normalizedEvent == QLatin1String("end")) {
		if (m_sharedSessionId == id) clearSharedState();
		return;
	}

	QUrl normalizedUrl;
	QString validationError;
	if (!validateSource(url, provider, &normalizedUrl, &validationError)) {
		if (m_sharedSessionId == id) reportError(validationError);
		return;
	}

	const bool sameSession = m_sharedSessionId == id;
	const bool wasJoined = sameSession && m_sharedJoined;
	const bool explicitlyRequested = m_pendingExplicitSessionId == id;
	if (!sameSession && m_pendingSharedOperationKind != SharedOperationKind::None) {
		cancelSharedOperationAcknowledgementTimeout();
		m_pendingExplicitSessionId.clear();
	}
	if (!sameSession && m_sharedJoined) closePlayer();

	QVariantList normalizedParticipants;
	QSet< qulonglong > seenParticipants;
	for (const QVariant &participant : participantSessions) {
		bool valid = false;
		const qulonglong value = participant.toULongLong(&valid);
		if (!valid || value == 0 || seenParticipants.contains(value)) continue;
		seenParticipants.insert(value);
		normalizedParticipants.push_back(QVariant::fromValue(value));
	}
	if (hostSession != 0 && !seenParticipants.contains(hostSession)) {
		seenParticipants.insert(hostSession);
		normalizedParticipants.push_back(QVariant::fromValue(hostSession));
	}

	bool joined = selfSession != 0 && seenParticipants.contains(selfSession);
	if (normalizedEvent == QLatin1String("leave") && actorSession == selfSession) joined = false;
	if ((normalizedEvent == QLatin1String("start") || normalizedEvent == QLatin1String("join"))
		&& actorSession == selfSession && explicitlyRequested) {
		joined = true;
	}

	m_sharedAvailable = true;
	m_sharedJoined = joined;
	m_sharedHost = selfSession != 0 && hostSession == selfSession;
	m_sharedTitle = title.trimmed();
	// Preserve the host's locally selected aspect when talking to an older
	// server that cannot echo the optional presentation field.
	if (!presentationAspect.trimmed().isEmpty() || !sameSession)
		m_sharedAspect = normalizedSharedPresentationAspect(presentationAspect);
	m_sharedSessionId = id;
	m_sharedUrl = normalizedUrl;
	m_sharedProvider = canonicalMediaProvider(normalizedUrl, provider);
	m_sharedScopeId = scopeId;
	m_sharedHostSession = hostSession;
	m_sharedParticipantSessions = normalizedParticipants;
	if (!sameSession) m_sharedPlayerSuppressed = false;
	if (explicitlyRequested && joined) {
		cancelSharedOperationAcknowledgementTimeout();
		m_pendingExplicitSessionId.clear();
		m_sharedPlayerSuppressed = false;
	}

	if (!joined) {
		if (wasJoined) closePlayer();
		m_state = QStringLiteral("available");
		m_error.clear();
		m_errorCode.clear();
		m_sharedOperationStatus = QStringLiteral("available");
		m_sharedOperationError.clear();
		emit stateChanged();
		return;
	}

	// The protocol currently transports the server's wall-clock timestamp in
	// `generation`. Comparing it with the client's wall clock turns ordinary
	// clock skew into a huge seek and treating it as an ordering token rejects
	// valid updates after a server clock correction. ServerHandler delivers these
	// events in connection order, so derive a client-local monotonic generation
	// and use the host-reported position as-is. The next regular host heartbeat
	// naturally absorbs network transit without coupling playback to wall clocks.
	Q_UNUSED(generation);
	const qulonglong currentLocalGeneration = qMax(m_sharedGeneration, m_syncGeneration);
	const qulonglong localGeneration = currentLocalGeneration == std::numeric_limits< qulonglong >::max()
		? currentLocalGeneration : currentLocalGeneration + 1;
	m_sharedPosition = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	m_sharedPaused = paused;
	m_sharedGeneration = localGeneration;
	if (m_sharedPlayerSuppressed) {
		m_state = QStringLiteral("available");
		m_error.clear();
		m_errorCode.clear();
		m_sharedOperationStatus = QStringLiteral("ready");
		m_sharedOperationError.clear();
		emit stateChanged();
		return;
	}
	if (wasJoined || explicitlyRequested) {
		applyRemoteState(normalizedUrl, m_sharedProvider, id, m_sharedPosition, paused, localGeneration);
	} else {
		m_state = QStringLiteral("available");
		m_sharedOperationStatus = QStringLiteral("available");
		m_sharedOperationError.clear();
		emit stateChanged();
	}
}

void MediaSessionBackend::clearSharedState() {
	cancelSharedOperationAcknowledgementTimeout();
	closePlayer();
	m_sharedAvailable = false;
	m_sharedJoined = false;
	m_sharedHost = false;
	m_sharedTitle.clear();
	m_sharedAspect = QStringLiteral("wide");
	m_sharedSessionId.clear();
	m_sharedUrl = {};
	m_sharedProvider.clear();
	m_sharedScopeId = 0;
	m_sharedHostSession = 0;
	m_sharedParticipantSessions.clear();
	m_sharedOperationStatus = QStringLiteral("idle");
	m_sharedOperationError.clear();
	m_sharedPlayerSuppressed = false;
	m_sharedPosition = 0.0;
	m_sharedPaused = true;
	m_sharedGeneration = 0;
	m_pendingExplicitSessionId.clear();
	m_lastSharedPublishMs = 0;
	m_lastSharedPublishPosition = -1.0;
	m_lastSharedPublishPaused = true;
	m_state = QStringLiteral("idle");
	m_error.clear();
	m_errorCode.clear();
	emit stateChanged();
}

QmlSelectionState::QmlSelectionState(QObject *parent) : QObject(parent) {
}

void QmlSelectionState::bindModels(RoomModel *rooms, ParticipantModel *participants) {
	if (m_rooms == rooms && m_participants == participants) return;
	if (m_rooms) disconnect(m_rooms, nullptr, this, nullptr);
	if (m_participants) disconnect(m_participants, nullptr, this, nullptr);
	m_rooms = rooms;
	m_participants = participants;
	const auto connectValidation = [this](QAbstractItemModel *model) {
		if (!model) return;
		connect(model, &QAbstractItemModel::rowsRemoved, this, &QmlSelectionState::validate);
		connect(model, &QAbstractItemModel::modelReset, this, &QmlSelectionState::validate);
	};
	connectValidation(m_rooms);
	connectValidation(m_participants);
	validate();
}

QString QmlSelectionState::scopeToken() const { return m_scopeToken; }
int QmlSelectionState::scopeValue() const { return m_scopeValue; }
QVariant QmlSelectionState::scopeId() const { return m_scopeId; }
QVariant QmlSelectionState::selectedUserSession() const { return m_selectedUserSession; }
QVariant QmlSelectionState::selectedVoiceChannelId() const { return m_selectedVoiceChannelId; }
void QmlSelectionState::setScopeToken(const QString &value) {
	const QString normalized = value.trimmed();
	const QString accepted = !m_rooms || normalized.isEmpty() || hasScopeToken(normalized) ? normalized : QString();
	if (m_scopeToken == accepted) {
		if (accepted.isEmpty()) {
			setScopeValue(-1);
			setScopeId({});
		}
		return;
	}
	m_scopeToken = accepted;
	emit scopeTokenChanged();
	if (m_scopeToken.isEmpty()) {
		setScopeValue(-1);
		setScopeId({});
	}
}
void QmlSelectionState::setScopeValue(const int value) {
	if (m_scopeValue == value) return;
	m_scopeValue = value;
	emit scopeValueChanged();
}
void QmlSelectionState::setScopeId(const QVariant &value) {
	bool valid = false;
	const qulonglong id = value.toULongLong(&valid);
	const QVariant accepted = valid ? QVariant::fromValue(id) : QVariant();
	if (m_scopeId == accepted) return;
	m_scopeId = accepted;
	emit scopeIdChanged();
}
void QmlSelectionState::applySelection(const QString &scopeToken, const int scopeValue, const QVariant &scopeId,
									   const QVariant &selectedUserSession,
									   const QVariant &selectedVoiceChannelId) {
	// Backend commands validate protocol IDs before reaching this transactional path. Accept the authoritative
	// selection even when the corresponding model row is queued for the next incremental synchronization (for
	// example, the server's default text room arrives immediately before RoomModel is refreshed). Direct QML
	// property writes still use the validating setters below and cannot select unknown IDs.
	const QString normalizedScope = scopeToken.trimmed();
	if (m_scopeToken != normalizedScope) {
		m_scopeToken = normalizedScope;
		emit scopeTokenChanged();
	}
	setScopeValue(m_scopeToken.isEmpty() ? -1 : scopeValue);
	setScopeId(m_scopeToken.isEmpty() ? QVariant() : scopeId);

	const auto authoritativeId = [](const QVariant &value, const bool requirePositive) {
		bool valid = false;
		const qulonglong id = value.toULongLong(&valid);
		return valid && (!requirePositive || id > 0) ? QVariant::fromValue(id) : QVariant();
	};
	const QVariant userSession = authoritativeId(selectedUserSession, true);
	if (m_selectedUserSession != userSession) {
		m_selectedUserSession = userSession;
		emit selectedUserSessionChanged();
	}
	const QVariant voiceChannelId = authoritativeId(selectedVoiceChannelId, false);
	if (m_selectedVoiceChannelId != voiceChannelId) {
		m_selectedVoiceChannelId = voiceChannelId;
		emit selectedVoiceChannelIdChanged();
	}
}
void QmlSelectionState::setSelectedUserSession(const QVariant &value) {
	bool valid = false;
	const qulonglong session = value.toULongLong(&valid);
	const QString stableId = valid && session > 0 ? QString::number(session) : QString();
	const QVariant accepted = !stableId.isEmpty() && (!m_participants || hasParticipantSession(stableId))
							  ? QVariant::fromValue(session)
							  : QVariant();
	if (m_selectedUserSession == accepted) return;
	m_selectedUserSession = accepted;
	emit selectedUserSessionChanged();
}
void QmlSelectionState::setSelectedVoiceChannelId(const QVariant &value) {
	bool valid = false;
	const qulonglong channelId = value.toULongLong(&valid);
	const QString stableId = valid ? QString::number(channelId) : QString();
	const QVariant accepted = valid && (!m_rooms || hasVoiceChannelId(stableId)) ? QVariant::fromValue(channelId)
																						 : QVariant();
	if (m_selectedVoiceChannelId == accepted) return;
	m_selectedVoiceChannelId = accepted;
	emit selectedVoiceChannelIdChanged();
}

void QmlSelectionState::validate() {
	if (!m_scopeToken.isEmpty() && !hasScopeToken(m_scopeToken)) {
		setScopeToken({});
	}
	if (m_selectedUserSession.isValid()
		&& !hasParticipantSession(QString::number(m_selectedUserSession.toULongLong())))
		setSelectedUserSession({});
	if (m_selectedVoiceChannelId.isValid()
		&& !hasVoiceChannelId(QString::number(m_selectedVoiceChannelId.toULongLong())))
		setSelectedVoiceChannelId({});
}

bool QmlSelectionState::hasScopeToken(const QString &scopeToken) const {
	if (!m_rooms) return true;
	for (int row = 0; row < m_rooms->rowCount(); ++row)
		if (m_rooms->get(row).value(QStringLiteral("scopeToken")).toString() == scopeToken) return true;
	return false;
}

bool QmlSelectionState::hasVoiceChannelId(const QString &channelId) const {
	if (!m_rooms) return true;
	for (int row = 0; row < m_rooms->rowCount(); ++row) {
		const QVariantMap room = m_rooms->get(row);
		const QString scopeToken = room.value(QStringLiteral("scopeToken")).toString();
		if (room.value(QStringLiteral("kind")).toString() == QLatin1String("voice")
			&& scopeToken.section(QLatin1Char(':'), -1) == channelId)
			return true;
	}
	return false;
}

bool QmlSelectionState::hasParticipantSession(const QString &sessionId) const {
	if (!m_participants) return true;
	for (int row = 0; row < m_participants->rowCount(); ++row) {
		const QVariantMap participant = m_participants->get(row);
		if (participant.value(QStringLiteral("participantSession")).toString() == sessionId) return true;
	}
	return false;
}
