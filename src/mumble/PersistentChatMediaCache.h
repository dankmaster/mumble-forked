// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATMEDIACACHE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATMEDIACACHE_H_

#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QtGlobal>
#include <QtGui/QImage>

#include <optional>
#include <vector>

namespace PersistentChatMediaCache {
struct MediaItem {
	QString url;
	QString mime;
	QString kind;
	QString title;
	QString streamKind;
	QString thumbnail;
	QString poster;
};

struct PreviewEntry {
	QString canonicalUrl;
	QString title;
	QString subtitle;
	QString description;
	QImage thumbnailImage;
	QString mediaDataUrl;
	QString mediaAudioDataUrl;
	QString mediaAudioMime;
	QString mediaMime;
	QString mediaKind;
	std::vector< MediaItem > mediaItems;
	QVariantMap metadata;
	QString openLabel;
	unsigned int previewAssetID = 0;
	bool autoplay               = false;
	bool metadataFinished       = false;
	bool thumbnailFinished      = false;
	bool failed                 = false;
	bool siteSnapshotFinished   = false;
	bool remoteMediaFinished    = false;
};

std::optional< PreviewEntry > loadPreview(const QString &previewKey);
void storePreview(const QString &previewKey, const PreviewEntry &entry);
bool clear();
quint64 sizeBytes();
QString formattedSize(quint64 bytes);
QString cacheRootPath();
} // namespace PersistentChatMediaCache

#endif // MUMBLE_MUMBLE_PERSISTENTCHATMEDIACACHE_H_
