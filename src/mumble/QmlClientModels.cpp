// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlClientModels.h"

#include "ClientActionRegistry.h"

#include <QtCore/QCache>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFutureWatcher>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>
#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtCore/QThreadPool>
#include <QtCore/QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui/QAction>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextDocument>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFragment>

#include <algorithm>
#include <limits>
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
			QStringLiteral("video/quicktime")
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
			QStringLiteral(R"(<\s*/?\s*(?:a|b|strong|i|em|s|strike|del|code)\b)"),
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

	QVariantMap normalizedPreviewMetadata(const QVariant &value) {
		const QVariantMap source = value.toMap();
		QVariantMap normalized;
		QSet< QString > consumed;
		static const QRegularExpression safeKey(
			QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,63}$"));
		constexpr qsizetype MaxMetadataFields = 32;
		constexpr qsizetype MaxMetadataStringCharacters = 4096;
		const auto appendScalar = [&](const QString &key, const QVariant &field) {
			if (normalized.size() >= MaxMetadataFields || consumed.contains(key)
				|| !safeKey.match(key).hasMatch()) return;
			consumed.insert(key);
			switch (field.metaType().id()) {
				case QMetaType::Bool:
				case QMetaType::Int:
				case QMetaType::UInt:
				case QMetaType::LongLong:
				case QMetaType::ULongLong:
				case QMetaType::Double: normalized.insert(key, field); break;
				case QMetaType::QString:
					normalized.insert(key, field.toString().left(MaxMetadataStringCharacters));
					break;
				default: break;
			}
		};
		// Preserve the scalar fields rendered by the current card before filling
		// the bounded diagnostics tail in deterministic key order.
		for (const QString &key : { QStringLiteral("xDisplayName"), QStringLiteral("xHandle") }) {
			const auto it = source.constFind(key);
			if (it != source.cend()) appendScalar(it.key(), it.value());
		}
		for (auto it = source.cbegin(); it != source.cend() && normalized.size() < MaxMetadataFields; ++it) {
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
			QStringLiteral("loadingLabel"), QStringLiteral("embedKind"), QStringLiteral("embedAspect"),
			QStringLiteral("previewSize"), QStringLiteral("mediaKind")
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
			const QString streamKind = item.value(QStringLiteral("streamKind")).toString().trimmed().toLower();
			if (!streamKind.isEmpty()) normalizedItem.insert(QStringLiteral("streamKind"), streamKind.left(32));
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
		const QVariantMap metadata = normalizedPreviewMetadata(preview.value(QStringLiteral("metadata")));
		if (!metadata.isEmpty()) normalized.insert(QStringLiteral("metadata"), metadata);
		return normalized;
	}

	QVariantList normalizedAttachments(const QVariant &value) {
		QVariantList normalized;
		for (const QVariant &entry : value.toList()) {
			if (normalized.size() >= MaxAttachmentCount) break;
			const QVariantMap attachment = entry.toMap();
			if (attachment.isEmpty()) continue;
			const QString source = safeImageSource(attachment.value(QStringLiteral("url")));
			const QString thumbnail = safeImageSource(attachment.value(QStringLiteral("thumbnailUrl")));
			if (source.isEmpty() && thumbnail.isEmpty()) continue;
			QVariantMap item {
				{ QStringLiteral("id"), attachment.value(QStringLiteral("id")).toString().left(512) },
				{ QStringLiteral("kind"), attachment.value(QStringLiteral("kind"), QStringLiteral("image")).toString().left(64) },
				{ QStringLiteral("name"), attachment.value(QStringLiteral("name")).toString().left(1024) },
				{ QStringLiteral("mime"), attachment.value(QStringLiteral("mime")).toString().left(128) },
				{ QStringLiteral("alt"), attachment.value(QStringLiteral("alt")).toString().left(4096) },
				{ QStringLiteral("url"), source },
				{ QStringLiteral("thumbnailUrl"), thumbnail.isEmpty() ? source : thumbnail },
				{ QStringLiteral("state"), attachment.value(QStringLiteral("state"), QStringLiteral("ready")).toString().left(64) }
			};
			if (attachment.contains(QStringLiteral("width"))) item.insert(QStringLiteral("width"), attachment.value(QStringLiteral("width")).toInt());
			if (attachment.contains(QStringLiteral("height"))) item.insert(QStringLiteral("height"), attachment.value(QStringLiteral("height")).toInt());
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

ClientSessionController::ClientSessionController(QObject *parent) : QObject(parent) {
}

QString ClientSessionController::serverName() const { return m_serverName; }
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
QVariantMap ClientSessionController::updateBanner() const { return m_updateBanner; }
QString ClientSessionController::motdHtml() const { return m_motdHtml; }
QVariantList ClientSessionController::motdSegments() const { return m_motdSegments; }
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
void ClientSessionController::setUpdateBanner(const QVariantMap &value) { SET_VALUE(m_updateBanner, updateBannerChanged); }
void ClientSessionController::setMotdHtml(const QString &value) {
	const QString bounded = value.left(MaxRichBodyCharacters);
	if (!acceptsFrontendStateMutation(this) || m_motdHtml == bounded) return;
	m_motdHtml = bounded;
	++m_motdParseGeneration;
	emit motdHtmlChanged();
	if (!m_motdSegments.isEmpty()) {
		m_motdSegments.clear();
		emit motdSegmentsChanged();
	}
	recomputeMotdDerivedState();
	if (bounded.trimmed().isEmpty()) return;

	const quint64 generation = m_motdParseGeneration;
	auto *watcher = new QFutureWatcher< QVariantList >(this);
	connect(watcher, &QFutureWatcher< QVariantList >::finished, this,
		[this, watcher, generation, bounded] {
			const QVariantList segments = watcher->result();
			watcher->deleteLater();
			if (generation != m_motdParseGeneration || m_motdHtml != bounded
				|| m_motdSegments == segments) {
				return;
			}
			m_motdSegments = segments;
			emit motdSegmentsChanged();
		});
	watcher->setFuture(QtConcurrent::run(&richBodyThreadPool(), [bounded] {
		const QString plainText =
			QTextDocumentFragment::fromHtml(bounded).toPlainText().left(MaxRichBodyCharacters);
		return structuredMessageBody(bounded, plainText);
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
	if (state.contains(QStringLiteral("updateBanner")))
		setUpdateBanner(state.value(QStringLiteral("updateBanner")).toMap());
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
	const QString signature = motdContentSignature(m_motdHtml);
	const bool dismissed = hasContent && !m_motdDismissedSignature.isEmpty()
		&& (m_motdDismissedSignature == signature || m_motdDismissedSignature == m_motdHtml.trimmed());
	const QString comparisonSignature = !m_motdLastSeenSignature.isEmpty()
		? m_motdLastSeenSignature : m_motdDismissedSignature;
	const bool changed = hasContent && !comparisonSignature.isEmpty()
		&& comparisonSignature != signature && comparisonSignature != m_motdHtml.trimmed();

	QVariantList actions;
	if (hasContent) {
		if (dismissed) {
			actions.push_back(motdAction(QStringLiteral("motd.restore"), tr("Show welcome message"), {}));
		} else {
			actions.push_back(motdAction(m_motdExpanded ? QStringLiteral("motd.hide") : QStringLiteral("motd.show"),
				m_motdExpanded ? tr("Collapse") : tr("Expand"), signature));
			actions.push_back(motdAction(QStringLiteral("motd.dismiss"), tr("Dismiss"), signature,
				QStringLiteral("muted")));
		}
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
bool ActiveScopeController::canSend() const { return m_canSend; }
bool ActiveScopeController::hasPendingReply() const { return m_hasPendingReply; }
QString ActiveScopeController::replyActor() const { return m_replyActor; }
QString ActiveScopeController::replySnippet() const { return m_replySnippet; }
bool ActiveScopeController::canAttachImages() const { return m_canAttachImages; }
bool ActiveScopeController::canLoadOlder() const { return m_canLoadOlder; }
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
void ActiveScopeController::setCanSend(bool value) { SET_SCOPE_VALUE(m_canSend, canSendChanged); }
void ActiveScopeController::setHasPendingReply(bool value) {
	SET_SCOPE_VALUE(m_hasPendingReply, hasPendingReplyChanged);
}
void ActiveScopeController::setReplyActor(const QString &value) { SET_SCOPE_VALUE(m_replyActor, replyActorChanged); }
void ActiveScopeController::setReplySnippet(const QString &value) { SET_SCOPE_VALUE(m_replySnippet, replySnippetChanged); }
void ActiveScopeController::setCanAttachImages(bool value) {
	SET_SCOPE_VALUE(m_canAttachImages, canAttachImagesChanged);
}
void ActiveScopeController::setCanLoadOlder(bool value) { SET_SCOPE_VALUE(m_canLoadOlder, canLoadOlderChanged); }
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
	setCanSend(state.value(QStringLiteral("canSend")).toBool());
	setHasPendingReply(state.value(QStringLiteral("hasPendingReply")).toBool());
	setReplyActor(state.value(QStringLiteral("replyActor")).toString());
	setReplySnippet(state.value(QStringLiteral("replySnippet")).toString());
	setCanAttachImages(state.value(QStringLiteral("canAttachImages")).toBool());
	setCanLoadOlder(state.value(QStringLiteral("canLoadOlder")).toBool());
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
			 { SourceRole, "source" } };
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
	for (int role = StableIdRole; role <= SourceRole; ++role) {
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

	// Switching between large, mostly unrelated scopes is a replacement, not a
	// sequence of thousands of individual moves. Keep the operation reset-free
	// for QML delegates, but perform one remove and one insert so index upkeep is
	// linear rather than rebuilding the full ID hash after every row.
	if (std::min(m_rowIds.size(), validIds.size()) >= 128) {
		int overlap = 0;
		for (const QString &id : validIds) {
			if (m_rowIndexById.contains(id)) ++overlap;
		}
		const int smallerCount = std::min(m_rowIds.size(), validIds.size());
		if (overlap * 4 <= smallerCount) {
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
	QVariantMap source;
	if (room.contains(QStringLiteral("actions"))) {
		source.insert(QStringLiteral("actions"), room.value(QStringLiteral("actions")).toList());
	}
	return { { QStringLiteral("id"), QStringLiteral("%1:%2").arg(kind, scopeToken) },
			 { QStringLiteral("scopeToken"), scopeToken },
			 { QStringLiteral("title"), room.value(QStringLiteral("label")) },
			 { QStringLiteral("subtitle"),
			   room.value(QStringLiteral("topic"),
						  room.value(QStringLiteral("description"), room.value(QStringLiteral("subtitle")))) },
			 { QStringLiteral("kind"), kind },
			 { QStringLiteral("selected"),
			   room.value(QStringLiteral("selected"), room.value(QStringLiteral("open"))) },
			 { QStringLiteral("status"),
			   room.value(QStringLiteral("joined")).toBool() ? QStringLiteral("joined") : QString() },
			 { QStringLiteral("depth"), room.value(QStringLiteral("depth")) },
			 { QStringLiteral("unreadCount"), room.value(QStringLiteral("unreadCount")) },
			 { QStringLiteral("source"), source } };
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
	const QVariant title = participant.contains(QStringLiteral("label"))
		? participant.value(QStringLiteral("label"))
		: participant.value(QStringLiteral("name"));
	const QVariant subtitle = participant.contains(QStringLiteral("subtitle"))
		? participant.value(QStringLiteral("subtitle"))
		: participant.value(QStringLiteral("statusLabel"));
	return { { QStringLiteral("id"), sessionId },
			 { QStringLiteral("title"), title },
			 { QStringLiteral("subtitle"), subtitle },
			 { QStringLiteral("kind"), QStringLiteral("participant") },
			 { QStringLiteral("status"), participant.value(QStringLiteral("talkState")) },
			 { QStringLiteral("avatarUrl"), participant.value(QStringLiteral("avatarUrl")) },
			 { QStringLiteral("source"), participant } };
}

void ParticipantModel::replaceParticipantStates(const QVariantList &participants) {
	QVariantList rows;
	rows.reserve(participants.size());
	for (const QVariant &entry : participants) {
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
	if (!row.isEmpty()) upsertRow(row);
}

void ParticipantModel::removeParticipant(const QString &sessionId) {
	removeRow(sessionId.trimmed());
}

void ParticipantModel::updatePresence(const QString &sessionId, const QString &talkState, const QString &talkLabel,
							  const QString &talkTone, const bool talking, const bool isSelf,
							  const QVariantList &badges, const QVariantList &statuses) {
	const QString id = sessionId.trimmed();
	if (id.isEmpty()) return;

	for (int rowIndex = 0; rowIndex < rowCount(); ++rowIndex) {
		QVariantMap row = get(rowIndex);
		if (row.value(QStringLiteral("id")).toString() != id) continue;
		const QVariantMap previousRow = row;

		row.insert(QStringLiteral("status"), talkState);
		QVariantMap source = row.value(QStringLiteral("source")).toMap();
		source.insert(QStringLiteral("talkState"), talkState);
		source.insert(QStringLiteral("talkLabel"), talkLabel);
		source.insert(QStringLiteral("talkTone"), talkTone);
		source.insert(QStringLiteral("talking"), talking);
		source.insert(QStringLiteral("isSelf"), isSelf);
		source.insert(QStringLiteral("badges"), badges);
		source.insert(QStringLiteral("statuses"), statuses);
		row.insert(QStringLiteral("source"), source);
		if (row == previousRow) return;
		upsertRow(row);
		return;
	}
}

ChatTimelineModel::ChatTimelineModel(QObject *parent) : StableListModel(parent) {}

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
			requests->push_back({ messageId, cacheKey, bodyHtml.left(MaxRichBodyCharacters),
								  bodyText.left(MaxRichBodyCharacters) });
		}
	} else {
		forgetRichBodyMessage(messageId);
	}
	const QVariant previewValue = message.contains(QStringLiteral("preview"))
		? message.value(QStringLiteral("preview")) : message.value(QStringLiteral("previewStub"));

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
			 { QStringLiteral("own"), message.value(QStringLiteral("own")) },
			 { QStringLiteral("deleted"), message.value(QStringLiteral("deleted")) },
			 { QStringLiteral("canReply"), message.value(QStringLiteral("canReply")) },
			 { QStringLiteral("canReact"), message.value(QStringLiteral("canReact")) },
			 { QStringLiteral("canDelete"), message.value(QStringLiteral("canDelete")) },
			 { QStringLiteral("source"), message } };
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
		scheduleRichBodyParses(requests);
		return MessageMutation::Updated;
	}
	upsertRow(row);
	scheduleRichBodyParses(requests);
	return MessageMutation::Inserted;
}

bool ChatTimelineModel::upsertMessage(const QVariantMap &message) {
	return applyMessage(message) != MessageMutation::Ignored;
}

bool ChatTimelineModel::removeMessage(const QString &messageId) {
	const QString id = messageId.trimmed();
	if (id.isEmpty()) return false;
	if (indexOf(id) < 0) return false;
	forgetRichBodyMessage(id);
	removeRow(id);
	return true;
}

int ChatTimelineModel::appendMessages(const QVariantList &messages) {
	int applied = 0;
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
	}
	scheduleRichBodyParses(requests);
	return applied;
}

void ChatTimelineModel::replaceMessages(const QVariantList &messages) {
	if (!acceptsFrontendStateMutation(this)) return;
	QVariantList rows;
	QList< RichBodyParseRequest > requests;
	QSet< QString > retainedMessageIds;
	rows.reserve(messages.size());
	for (const QVariant &entry : messages) {
		const QVariantMap row = messageRow(entry.toMap(), &requests);
		if (!row.isEmpty()) {
			rows.push_back(row);
			retainedMessageIds.insert(row.value(QStringLiteral("id")).toString());
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
	synchronizeRows(rows);
	scheduleRichBodyParses(requests);
}

QVariantList ChatTimelineModel::messages() const {
	QVariantList states;
	states.reserve(rowCount());
	for (int row = 0; row < rowCount(); ++row) states.push_back(get(row).value(QStringLiteral("source")));
	return states;
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
	StableListModel::clear();
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
		if (row.value(QStringLiteral("bodySegments")).toList() == ready.segments) continue;
		row.insert(QStringLiteral("bodySegments"), ready.segments);
		upsertRow(row);
	}
	if (!m_readyRichBodies.empty()) scheduleRichBodyDrain();
}

namespace {
	constexpr int MaxOperationItemResultPageSize = 64;
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
		if (!token.startsWith(QLatin1String("channel:"))) return {};
		qulonglong channel = 0;
		return parseProtocolId(token.mid(QStringLiteral("channel:").size()), true, &channel)
			? QStringLiteral("channel:%1").arg(channel) : QString();
	}
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
	return true;
}

void AsyncOperationModel::interruptOperations(const QString &prefix) {
	for (int index = 0; index < rowCount(); ++index) {
		const QVariantMap row = get(index);
		const QString id = row.value(QStringLiteral("id")).toString();
		const QString status = row.value(QStringLiteral("status")).toString();
		if (id.startsWith(prefix)
			&& (status == QLatin1String("running") || status == QLatin1String("cancelling"))) {
			finishOperation(id, false, QStringLiteral("cancelled"), tr("Operation cancelled"));
		}
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

UiCommandController::UiCommandController(QObject *parent) : QObject(parent) {
}

void UiCommandController::selectScope(const QString &scopeToken) {
	if (!scopeToken.trimmed().isEmpty()) emit scopeSelectionRequested(scopeToken.trimmed());
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
	if (source.isEmpty() || source == QLatin1String("channel:0") || target.isEmpty() || source == target
		|| (normalizedPlacement != QLatin1String("before") && normalizedPlacement != QLatin1String("after")
			&& normalizedPlacement != QLatin1String("inside"))) return;
	emit scopeMoveRequested(source, target, normalizedPlacement);
}
void UiCommandController::sendMessage(const QString &message) {
	if (!message.trimmed().isEmpty()) emit messageSendRequested(message);
}
void UiCommandController::requestOlderMessages() { emit olderMessagesRequested(); }
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
QVariantMap DialogStateController::state() const { return m_state; }
qulonglong DialogStateController::revision() const { return m_revision; }
QVariant DialogStateController::fieldValue(const QString &fieldId) const {
	for (const QVariant &sectionValue : sections()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap field = fieldValue.toMap();
			if (field.value(QStringLiteral("id")).toString() == fieldId) return field.value(QStringLiteral("value"));
		}
	}
	return {};
}
QString DialogStateController::fieldError(const QString &fieldId) const {
	return m_state.value(QStringLiteral("errors")).toMap().value(fieldId).toString();
}

void DialogStateController::applyState(const QVariantMap &state) {
	if (!acceptsFrontendStateMutation(this)) return;
	if (m_state == state) return;
	m_state = state;
	m_state.detach();
	++m_revision;
	emit stateChanged();
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

MediaSessionBackend::MediaSessionBackend(QObject *parent) : QObject(parent) {
}

namespace {
const QHash< QString, QSet< QString > > &mediaProviderHosts() {
	static const QHash< QString, QSet< QString > > hosts {
		{ QStringLiteral("youtube"), { QStringLiteral("www.youtube.com"), QStringLiteral("youtube.com"),
									 QStringLiteral("www.youtube-nocookie.com"), QStringLiteral("youtube-nocookie.com") } },
		{ QStringLiteral("twitch"), { QStringLiteral("player.twitch.tv") } },
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
		QStringLiteral("direct"), QStringLiteral("youtube"), QStringLiteral("twitch"),
		QStringLiteral("streamable"), QStringLiteral("vimeo"), QStringLiteral("dailymotion")
	};
	return supported.contains(provider.trimmed().toLower());
}
}

bool MediaSessionBackend::active() const { return m_active; }
bool MediaSessionBackend::sharedAvailable() const { return m_sharedAvailable; }
bool MediaSessionBackend::sharedJoined() const { return m_sharedJoined; }
bool MediaSessionBackend::sharedHost() const { return m_sharedHost; }
QString MediaSessionBackend::sharedTitle() const { return m_sharedTitle; }
QString MediaSessionBackend::sharedSessionId() const { return m_sharedSessionId; }
qulonglong MediaSessionBackend::sharedScopeId() const { return m_sharedScopeId; }
qulonglong MediaSessionBackend::sharedHostSession() const { return m_sharedHostSession; }
int MediaSessionBackend::sharedParticipantCount() const { return m_sharedParticipantSessions.size(); }
QVariantList MediaSessionBackend::sharedParticipantSessions() const { return m_sharedParticipantSessions; }
QUrl MediaSessionBackend::url() const { return m_url; }
QUrl MediaSessionBackend::audioUrl() const { return m_audioUrl; }
QString MediaSessionBackend::provider() const { return m_provider; }
bool MediaSessionBackend::playbackControllable() const {
	return mediaProviderSupportsSynchronizedPlayback(m_provider);
}
QString MediaSessionBackend::mediaMime() const { return m_mediaMime; }
QString MediaSessionBackend::audioMime() const { return m_audioMime; }
QString MediaSessionBackend::sessionId() const { return m_sessionId; }
QString MediaSessionBackend::state() const { return m_state; }
double MediaSessionBackend::position() const { return m_position; }
double MediaSessionBackend::duration() const { return m_duration; }
QString MediaSessionBackend::error() const { return m_error; }
qulonglong MediaSessionBackend::syncGeneration() const { return m_syncGeneration; }

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
	const QString requestedSessionId = sessionId.trimmed();
	if (m_sharedAvailable && (!m_sharedJoined || requestedSessionId != m_sharedSessionId)) {
		emit playbackRejected(tr("Leave or end the current watch-together session before opening other media."));
		return false;
	}

	QUrl normalized;
	QString validationError;
	if (!validateSource(url, provider, &normalized, &validationError)) {
		reportError(validationError);
		return false;
	}
	const QString normalizedProvider = canonicalMediaProvider(normalized, provider);
	m_active = true;
	m_url = normalized;
	m_audioUrl = {};
	m_provider = normalizedProvider;
	m_mediaMime.clear();
	m_audioMime.clear();
	m_sessionId = requestedSessionId;
	m_navigationHost = normalized.host().toLower();
	m_navigationPort = normalized.port(443);
	m_state = QStringLiteral("loading");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	++m_syncGeneration;
	emit stateChanged();
	return true;
}

bool MediaSessionBackend::openDirect(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
									 const QString &audioMime, const QString &sessionId) {
	if (m_sharedAvailable) {
		emit playbackRejected(tr("Leave or end the current watch-together session before opening other media."));
		return false;
	}

	const QString normalizedMediaMime = ::normalizedMediaMime(mediaMime);
	const bool primaryIsAudio = normalizedMediaMime.startsWith(QLatin1String("audio/"));
	QUrl normalizedMediaUrl;
	QString validationError;
	if (!validateDirectSource(url, normalizedMediaMime, primaryIsAudio, &normalizedMediaUrl, &validationError)) {
		reportError(validationError);
		return false;
	}

	QUrl normalizedAudioUrl;
	const QString normalizedAudioMime = ::normalizedMediaMime(audioMime);
	if (!audioUrl.isEmpty()) {
		if (primaryIsAudio
			|| !validateDirectSource(audioUrl, normalizedAudioMime, true, &normalizedAudioUrl, &validationError)) {
			reportError(primaryIsAudio ? tr("A direct audio source cannot have a secondary audio track.")
									   : validationError);
			return false;
		}
	}

	m_active = true;
	m_url = normalizedMediaUrl;
	m_audioUrl = normalizedAudioUrl;
	m_provider = QStringLiteral("direct");
	m_mediaMime = normalizedMediaMime;
	m_audioMime = normalizedAudioMime;
	m_sessionId = sessionId.trimmed();
	m_navigationHost.clear();
	m_navigationPort = -1;
	m_state = QStringLiteral("loading");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	++m_syncGeneration;
	emit stateChanged();
	return true;
}

bool MediaSessionBackend::startShared(const QUrl &url, const QString &provider, const QString &title) {
	QUrl normalized;
	QString validationError;
	if (!validateSource(url, provider, &normalized, &validationError)) {
		reportError(validationError);
		return false;
	}
	if (m_sharedAvailable) {
		reportError(tr("Leave or end the current watch-together session first."));
		return false;
	}
	const QString normalizedProvider = canonicalMediaProvider(normalized, provider);
	if (!mediaProviderSupportsSynchronizedPlayback(normalizedProvider)) {
		reportError(tr("This provider does not expose synchronized playback controls. Open it in your browser instead."));
		return false;
	}

	const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_pendingExplicitSessionId = sessionId;
	m_sharedAvailable = true;
	m_sharedJoined = false;
	m_sharedHost = false;
	m_sharedTitle = title.trimmed();
	m_sharedSessionId = sessionId;
	m_sharedUrl = normalized;
	m_sharedProvider = normalizedProvider;
	m_sharedParticipantSessions.clear();
	m_state = QStringLiteral("starting");
	m_error.clear();
	emit stateChanged();
	emit sharedStartRequested(sessionId, normalized, m_sharedProvider, m_sharedTitle);
	return true;
}

void MediaSessionBackend::joinShared() {
	if (!m_sharedAvailable || m_sharedSessionId.isEmpty() || m_sharedJoined) return;
	m_pendingExplicitSessionId = m_sharedSessionId;
	emit sharedEventRequested(m_sharedSessionId, QStringLiteral("join"), 0);
}

void MediaSessionBackend::leaveShared() {
	if (!m_sharedAvailable || m_sharedSessionId.isEmpty()) return;
	if (m_sharedHost) {
		endShared();
		return;
	}
	if (m_sharedJoined) emit sharedEventRequested(m_sharedSessionId, QStringLiteral("leave"), 0);
	m_pendingExplicitSessionId.clear();
	m_sharedJoined = false;
	closePlayer();
	emit stateChanged();
}

void MediaSessionBackend::endShared() {
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
	if (!m_sharedAvailable || !m_sharedJoined || m_sharedUrl.isEmpty()) return false;
	return open(m_sharedUrl, m_sharedProvider, m_sharedSessionId);
}

void MediaSessionBackend::retry() {
	if (!m_active || m_url.isEmpty()) return;
	m_state = QStringLiteral("loading");
	m_error.clear();
	emit stateChanged();
	emit retryRequested();
}

bool MediaSessionBackend::isNavigationAllowed(const QUrl &url) const {
	if (url == QUrl(QStringLiteral("about:blank"))) return true;
	if (!m_active) return false;
	if (m_provider == QLatin1String("direct")) {
		const QUrl candidate = url.adjusted(QUrl::RemoveFragment);
		return candidate == m_url.adjusted(QUrl::RemoveFragment)
			|| (!m_audioUrl.isEmpty() && candidate == m_audioUrl.adjusted(QUrl::RemoveFragment));
	}
	const auto providerHosts = mediaProviderHosts().constFind(m_provider);
	return providerHosts != mediaProviderHosts().cend() && url.isValid() && url.scheme() == QLatin1String("https")
		   && providerHosts->contains(url.host().toLower());
}

bool MediaSessionBackend::supportsSynchronizedPlayback(const QString &provider) const {
	return mediaProviderSupportsSynchronizedPlayback(provider);
}

void MediaSessionBackend::closePlayer() {
	if (!m_active && m_state == QLatin1String("idle")) return;
	m_active = false;
	m_url = {};
	m_audioUrl = {};
	m_provider.clear();
	m_mediaMime.clear();
	m_audioMime.clear();
	m_sessionId.clear();
	m_navigationHost.clear();
	m_navigationPort = -1;
	m_state = QStringLiteral("idle");
	m_position = 0.0;
	m_duration = 0.0;
	m_error.clear();
	++m_syncGeneration;
	emit stateChanged();
}

void MediaSessionBackend::close() {
	if (m_sharedJoined) {
		leaveShared();
		return;
	}
	closePlayer();
}

void MediaSessionBackend::play() {
	if (!m_active || !playbackControllable()) return;
	m_state = QStringLiteral("playing");
	m_error.clear();
	emit stateChanged();
	emit playRequested();
	publishSharedPlaybackState(m_position, false, true);
}

void MediaSessionBackend::pause() {
	if (!m_active || !playbackControllable()) return;
	m_state = QStringLiteral("paused");
	m_error.clear();
	emit stateChanged();
	emit pauseRequested();
	publishSharedPlaybackState(m_position, true, true);
}

void MediaSessionBackend::seek(const double seconds) {
	if (!m_active || !qIsFinite(seconds) || seconds < 0.0) return;
	m_position = seconds;
	emit stateChanged();
	emit seekRequested(seconds);
	publishSharedPlaybackState(seconds, m_state != QLatin1String("playing"), true);
}

void MediaSessionBackend::reportPlaybackState(const double position, const double duration, const bool paused) {
	if (!m_active) return;
	m_position = qIsFinite(position) ? qMax(0.0, position) : 0.0;
	m_duration = qIsFinite(duration) ? qMax(0.0, duration) : 0.0;
	m_state = paused ? QStringLiteral("paused") : QStringLiteral("playing");
	m_error.clear();
	emit stateChanged();
	publishSharedPlaybackState(m_position, paused, false);
}

void MediaSessionBackend::reportError(const QString &message) {
	m_state = QStringLiteral("error");
	m_error = message.trimmed().isEmpty() ? tr("Media playback failed.") : message.trimmed();
	emit stateChanged();
}

void MediaSessionBackend::applyRemoteState(const QUrl &url, const QString &provider, const QString &sessionId,
										   const double position, const bool paused, const qulonglong generation) {
	if (generation != 0 && generation < m_syncGeneration) return;
	const QString previousState = m_state;
	const double previousPosition = m_position;
	const bool sourceChanged = !m_active || m_sessionId != sessionId || m_url != url;
	if (sourceChanged && !open(url, provider, sessionId)) return;
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
	if (needsSeek) emit seekRequested(m_position);
	if (playbackTransition) {
		if (paused) emit pauseRequested();
		else emit playRequested();
	}
	emit stateChanged();
}

void MediaSessionBackend::publishSharedPlaybackState(const double position, const bool paused, const bool force) {
	if (!m_sharedAvailable || !m_sharedJoined || !m_sharedHost || m_sharedSessionId.isEmpty()) return;
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
										 const qulonglong selfSession) {
	const QString id = sessionId.trimmed();
	const QString normalizedEvent = event.trimmed().toLower();
	if (id.isEmpty()) return;
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
	m_sharedSessionId = id;
	m_sharedUrl = normalizedUrl;
	m_sharedProvider = canonicalMediaProvider(normalizedUrl, provider);
	m_sharedScopeId = scopeId;
	m_sharedHostSession = hostSession;
	m_sharedParticipantSessions = normalizedParticipants;
	if (explicitlyRequested && joined) m_pendingExplicitSessionId.clear();

	if (!joined) {
		if (wasJoined) closePlayer();
		m_state = QStringLiteral("available");
		m_error.clear();
		emit stateChanged();
		return;
	}

	if (!paused && generation > 0) {
		const qint64 ageMs = qMax< qint64 >(0, QDateTime::currentMSecsSinceEpoch() - static_cast< qint64 >(generation));
		position += static_cast< double >(ageMs) / 1000.0;
	}
	if (wasJoined || explicitlyRequested) {
		applyRemoteState(normalizedUrl, m_sharedProvider, id, position, paused, generation);
	} else {
		m_state = QStringLiteral("available");
		emit stateChanged();
	}
}

void MediaSessionBackend::clearSharedState() {
	closePlayer();
	m_sharedAvailable = false;
	m_sharedJoined = false;
	m_sharedHost = false;
	m_sharedTitle.clear();
	m_sharedSessionId.clear();
	m_sharedUrl = {};
	m_sharedProvider.clear();
	m_sharedScopeId = 0;
	m_sharedHostSession = 0;
	m_sharedParticipantSessions.clear();
	m_pendingExplicitSessionId.clear();
	m_lastSharedPublishMs = 0;
	m_lastSharedPublishPosition = -1.0;
	m_lastSharedPublishPaused = true;
	m_state = QStringLiteral("idle");
	m_error.clear();
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
	setScopeToken(scopeToken);
	setScopeValue(m_scopeToken.isEmpty() ? -1 : scopeValue);
	setScopeId(m_scopeToken.isEmpty() ? QVariant() : scopeId);
	setSelectedUserSession(selectedUserSession);
	setSelectedVoiceChannelId(selectedVoiceChannelId);
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
	const QString scopeToken = QStringLiteral("channel:%1").arg(channelId);
	for (int row = 0; row < m_rooms->rowCount(); ++row) {
		const QVariantMap room = m_rooms->get(row);
		if (room.value(QStringLiteral("kind")).toString() == QLatin1String("voice")
			&& room.value(QStringLiteral("scopeToken")).toString() == scopeToken)
			return true;
	}
	return false;
}

bool QmlSelectionState::hasParticipantSession(const QString &sessionId) const {
	if (!m_participants) return true;
	for (int row = 0; row < m_participants->rowCount(); ++row)
		if (m_participants->get(row).value(QStringLiteral("id")).toString() == sessionId) return true;
	return false;
}
