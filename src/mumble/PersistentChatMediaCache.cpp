// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatMediaCache.h"

#include "Global.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QVector>

#include <algorithm>

namespace {
constexpr int CACHE_VERSION                  = 12;
constexpr qint64 CACHE_MAX_AGE_SECONDS       = 30LL * 24LL * 60LL * 60LL;
constexpr quint64 CACHE_MAX_TOTAL_BYTES      = 512ULL * 1024ULL * 1024ULL;
constexpr int CACHE_MAX_DATA_URL_CHARACTERS  = 40 * 1024 * 1024;
constexpr int CACHE_MAX_THUMBNAIL_BYTES      = 6 * 1024 * 1024;
constexpr char CACHE_DIR_NAME[]              = "cache/persistent-chat-media";
constexpr char CACHE_FILE_SUFFIX[]           = ".json";

QString cacheFileNameForKey(const QString &previewKey) {
	const QByteArray hash =
		QCryptographicHash::hash(previewKey.toUtf8(), QCryptographicHash::Sha256).toHex();
	return QString::fromLatin1(hash) + QLatin1String(CACHE_FILE_SUFFIX);
}

QString cacheFilePathForKey(const QString &previewKey) {
	return QDir(PersistentChatMediaCache::cacheRootPath()).filePath(cacheFileNameForKey(previewKey));
}

bool ensureCacheRoot() {
	QDir base = Global::get().qdBasePath;
	if (!base.exists() && !QDir::root().mkpath(base.absolutePath())) {
		return false;
	}
	return base.mkpath(QLatin1String(CACHE_DIR_NAME));
}

QByteArray thumbnailBytes(const QImage &image) {
	if (image.isNull()) {
		return {};
	}

	QByteArray bytes;
	QBuffer buffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly)) {
		return {};
	}
	if (!image.save(&buffer, "PNG")) {
		return {};
	}
	if (bytes.size() > CACHE_MAX_THUMBNAIL_BYTES) {
		return {};
	}
	return bytes;
}

QImage thumbnailImageFromJson(const QJsonObject &object) {
	const QString thumbnail = object.value(QStringLiteral("thumbnailPng")).toString();
	if (thumbnail.isEmpty()) {
		return {};
	}

	QByteArray bytes = QByteArray::fromBase64(thumbnail.toLatin1());
	if (bytes.isEmpty() || bytes.size() > CACHE_MAX_THUMBNAIL_BYTES) {
		return {};
	}

	QImage image;
	image.loadFromData(bytes, "PNG");
	return image;
}

bool shouldStoreDataUrl(const QString &value) {
	if (!value.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
		return true;
	}
	return value.size() <= CACHE_MAX_DATA_URL_CHARACTERS;
}

void pruneCache() {
	const QString rootPath = PersistentChatMediaCache::cacheRootPath();
	QDir root(rootPath);
	if (!root.exists()) {
		return;
	}

	struct CacheFile {
		QString path;
		QDateTime lastModified;
		quint64 size = 0;
	};

	QVector< CacheFile > files;
	quint64 totalBytes = 0;
	const QDateTime now = QDateTime::currentDateTimeUtc();
	QDirIterator it(rootPath, QStringList { QStringLiteral("*") + QLatin1String(CACHE_FILE_SUFFIX) }, QDir::Files);
	while (it.hasNext()) {
		const QString path = it.next();
		const QFileInfo info(path);
		if (info.lastModified().toUTC().secsTo(now) > CACHE_MAX_AGE_SECONDS) {
			QFile::remove(path);
			continue;
		}
		const quint64 size = static_cast< quint64 >(std::max< qint64 >(0, info.size()));
		files.push_back(CacheFile { path, info.lastModified().toUTC(), size });
		totalBytes += size;
	}

	if (totalBytes <= CACHE_MAX_TOTAL_BYTES) {
		return;
	}

	std::sort(files.begin(), files.end(), [](const CacheFile &left, const CacheFile &right) {
		return left.lastModified < right.lastModified;
	});

	const quint64 targetBytes = (CACHE_MAX_TOTAL_BYTES * 9) / 10;
	for (const CacheFile &file : files) {
		if (totalBytes <= targetBytes) {
			break;
		}
		if (QFile::remove(file.path)) {
			totalBytes = file.size > totalBytes ? 0 : totalBytes - file.size;
		}
	}
}
} // namespace

namespace PersistentChatMediaCache {
QString cacheRootPath() {
	if (!Global::g_global_struct) {
		return QDir::temp().filePath(QLatin1String("mumble/cache/persistent-chat-media"));
	}

	return Global::get().qdBasePath.filePath(QLatin1String(CACHE_DIR_NAME));
}

std::optional< PreviewEntry > loadPreview(const QString &previewKey) {
	if (previewKey.isEmpty()) {
		return std::nullopt;
	}

	QFile file(cacheFilePathForKey(previewKey));
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	file.close();
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}

	const QJsonObject object = document.object();
	if (object.value(QStringLiteral("version")).toInt() != CACHE_VERSION
		|| object.value(QStringLiteral("previewKey")).toString() != previewKey) {
		return std::nullopt;
	}

	const qint64 updatedAt = object.value(QStringLiteral("updatedAt")).toVariant().toLongLong();
	if (updatedAt <= 0 || updatedAt + CACHE_MAX_AGE_SECONDS < QDateTime::currentSecsSinceEpoch()) {
		QFile::remove(file.fileName());
		return std::nullopt;
	}

	PreviewEntry entry;
	entry.canonicalUrl         = object.value(QStringLiteral("canonicalUrl")).toString();
	entry.title                = object.value(QStringLiteral("title")).toString();
	entry.subtitle             = object.value(QStringLiteral("subtitle")).toString();
	entry.description          = object.value(QStringLiteral("description")).toString();
	entry.thumbnailImage       = thumbnailImageFromJson(object);
	entry.mediaDataUrl         = object.value(QStringLiteral("mediaDataUrl")).toString();
	entry.mediaAudioDataUrl    = object.value(QStringLiteral("mediaAudioDataUrl")).toString();
	entry.mediaAudioMime       = object.value(QStringLiteral("mediaAudioMime")).toString();
	entry.mediaMime            = object.value(QStringLiteral("mediaMime")).toString();
	entry.mediaKind            = object.value(QStringLiteral("mediaKind")).toString();
	entry.metadata             = object.value(QStringLiteral("metadata")).toObject().toVariantMap();
	const QJsonArray mediaItems = object.value(QStringLiteral("mediaItems")).toArray();
	for (const QJsonValue &value : mediaItems) {
		const QJsonObject itemObject = value.toObject();
		MediaItem item;
		item.url  = itemObject.value(QStringLiteral("url")).toString();
		item.mime = itemObject.value(QStringLiteral("mime")).toString();
		item.kind = itemObject.value(QStringLiteral("kind")).toString();
		if (!item.url.trimmed().isEmpty()) {
			entry.mediaItems.push_back(item);
		}
	}
	entry.openLabel            = object.value(QStringLiteral("openLabel")).toString();
	entry.previewAssetID       = static_cast< unsigned int >(std::max(0, object.value(QStringLiteral("previewAssetID")).toInt()));
	entry.autoplay             = object.value(QStringLiteral("autoplay")).toBool(false);
	entry.metadataFinished     = object.value(QStringLiteral("metadataFinished")).toBool(false);
	entry.thumbnailFinished    = object.value(QStringLiteral("thumbnailFinished")).toBool(false);
	entry.failed               = object.value(QStringLiteral("failed")).toBool(false);
	entry.siteSnapshotFinished = object.value(QStringLiteral("siteSnapshotFinished")).toBool(false);
	entry.remoteMediaFinished  = object.value(QStringLiteral("remoteMediaFinished")).toBool(false);

	if (entry.canonicalUrl.trimmed().isEmpty() || !entry.metadataFinished || !entry.thumbnailFinished) {
		return std::nullopt;
	}
	if (!shouldStoreDataUrl(entry.mediaDataUrl) || !shouldStoreDataUrl(entry.mediaAudioDataUrl)) {
		return std::nullopt;
	}

	return entry;
}

void storePreview(const QString &previewKey, const PreviewEntry &entry) {
	if (previewKey.isEmpty() || entry.canonicalUrl.trimmed().isEmpty() || !entry.metadataFinished
		|| !entry.thumbnailFinished) {
		return;
	}
	if (entry.failed && entry.thumbnailImage.isNull() && entry.mediaDataUrl.isEmpty()) {
		return;
	}
	if (!shouldStoreDataUrl(entry.mediaDataUrl) || !shouldStoreDataUrl(entry.mediaAudioDataUrl)) {
		return;
	}
	if (!ensureCacheRoot()) {
		return;
	}

	QJsonObject object;
	object.insert(QStringLiteral("version"), CACHE_VERSION);
	object.insert(QStringLiteral("previewKey"), previewKey);
	object.insert(QStringLiteral("updatedAt"), QDateTime::currentSecsSinceEpoch());
	object.insert(QStringLiteral("canonicalUrl"), entry.canonicalUrl);
	object.insert(QStringLiteral("title"), entry.title);
	object.insert(QStringLiteral("subtitle"), entry.subtitle);
	object.insert(QStringLiteral("description"), entry.description);
	object.insert(QStringLiteral("mediaDataUrl"), entry.mediaDataUrl);
	object.insert(QStringLiteral("mediaAudioDataUrl"), entry.mediaAudioDataUrl);
	object.insert(QStringLiteral("mediaAudioMime"), entry.mediaAudioMime);
	object.insert(QStringLiteral("mediaMime"), entry.mediaMime);
	object.insert(QStringLiteral("mediaKind"), entry.mediaKind);
	if (!entry.metadata.isEmpty()) {
		object.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(entry.metadata));
	}
	QJsonArray mediaItems;
	for (const MediaItem &item : entry.mediaItems) {
		if (item.url.trimmed().isEmpty()) {
			continue;
		}
		QJsonObject itemObject;
		itemObject.insert(QStringLiteral("url"), item.url);
		itemObject.insert(QStringLiteral("mime"), item.mime);
		itemObject.insert(QStringLiteral("kind"), item.kind);
		mediaItems.push_back(itemObject);
	}
	if (!mediaItems.isEmpty()) {
		object.insert(QStringLiteral("mediaItems"), mediaItems);
	}
	object.insert(QStringLiteral("openLabel"), entry.openLabel);
	object.insert(QStringLiteral("previewAssetID"), static_cast< int >(entry.previewAssetID));
	object.insert(QStringLiteral("autoplay"), entry.autoplay);
	object.insert(QStringLiteral("metadataFinished"), entry.metadataFinished);
	object.insert(QStringLiteral("thumbnailFinished"), entry.thumbnailFinished);
	object.insert(QStringLiteral("failed"), entry.failed);
	object.insert(QStringLiteral("siteSnapshotFinished"), entry.siteSnapshotFinished);
	object.insert(QStringLiteral("remoteMediaFinished"), entry.remoteMediaFinished);

	const QByteArray thumbnail = thumbnailBytes(entry.thumbnailImage);
	if (!thumbnail.isEmpty()) {
		object.insert(QStringLiteral("thumbnailPng"), QString::fromLatin1(thumbnail.toBase64()));
	}

	QSaveFile file(cacheFilePathForKey(previewKey));
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	if (file.commit()) {
		pruneCache();
	}
}

bool clear() {
	QDir root(cacheRootPath());
	if (!root.exists()) {
		return ensureCacheRoot();
	}
	const bool removed = root.removeRecursively();
	return ensureCacheRoot() && removed;
}

quint64 sizeBytes() {
	const QString rootPath = cacheRootPath();
	QDir root(rootPath);
	if (!root.exists()) {
		return 0;
	}

	quint64 totalBytes = 0;
	QDirIterator it(rootPath, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QFileInfo info(it.next());
		totalBytes += static_cast< quint64 >(std::max< qint64 >(0, info.size()));
	}
	return totalBytes;
}

QString formattedSize(quint64 bytes) {
	static const char *units[] = { "B", "KB", "MB", "GB" };
	double value              = static_cast< double >(bytes);
	int unitIndex             = 0;
	while (value >= 1024.0 && unitIndex < 3) {
		value /= 1024.0;
		++unitIndex;
	}

	const int precision = unitIndex == 0 ? 0 : (value < 10.0 ? 1 : 0);
	return QString::fromLatin1("%1 %2").arg(value, 0, 'f', precision).arg(QLatin1String(units[unitIndex]));
}
} // namespace PersistentChatMediaCache
