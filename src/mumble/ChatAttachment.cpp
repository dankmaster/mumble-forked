// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatAttachment.h"

#include <QtCore/QMimeDatabase>
#include <QtCore/QMimeType>
#include <QtCore/QSet>

namespace Mumble {
namespace ChatAttachments {

	namespace {
		const QSet< QString > &imageMimes() {
			static const QSet< QString > values {
				QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/webp"),
				QStringLiteral("image/gif"), QStringLiteral("image/bmp"),
			};
			return values;
		}

		const QSet< QString > &videoMimes() {
			static const QSet< QString > values {
				QStringLiteral("video/mp4"), QStringLiteral("video/webm"), QStringLiteral("video/quicktime"),
			};
			return values;
		}

		const QSet< QString > &audioMimes() {
			static const QSet< QString > values {
				QStringLiteral("audio/aac"), QStringLiteral("audio/flac"), QStringLiteral("audio/mp4"),
				QStringLiteral("audio/mpeg"), QStringLiteral("audio/ogg"), QStringLiteral("audio/wav"),
				QStringLiteral("audio/webm"), QStringLiteral("audio/x-wav"),
			};
			return values;
		}

		const QSet< QString > &documentMimes() {
			static const QSet< QString > values {
				QStringLiteral("application/pdf"), QStringLiteral("text/plain"), QStringLiteral("text/markdown"),
			};
			return values;
		}
	} // namespace

	Classification classifyFile(const QString &path) {
		QMimeDatabase database;
		const QMimeType contentType   = database.mimeTypeForFile(path, QMimeDatabase::MatchContent);
		const QMimeType extensionType = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
		QString mime                  = contentType.name().section(QLatin1Char(';'), 0, 0).trimmed().toLower();
		if ((mime.isEmpty() || mime == QLatin1String("application/octet-stream")) && extensionType.isValid()) {
			mime = extensionType.name().section(QLatin1Char(';'), 0, 0).trimmed().toLower();
		}

		if (imageMimes().contains(mime)) {
			return { mime, Kind::Image };
		}
		if (videoMimes().contains(mime)) {
			return { mime, Kind::Video };
		}
		if (audioMimes().contains(mime)) {
			return { mime, Kind::Audio };
		}
		if (documentMimes().contains(mime)) {
			return { mime, Kind::Document };
		}
		if (mime == QLatin1String("application/zip")) {
			return { mime, Kind::Binary };
		}

		// Unknown formats are deliberately transported as opaque downloads. The
		// original filename is retained for the recipient, but the declared MIME
		// never grants inline rendering or automatic execution privileges.
		return { QStringLiteral("application/octet-stream"), Kind::Binary };
	}

	QString kindName(const Kind kind) {
		switch (kind) {
			case Kind::Image: return QStringLiteral("image");
			case Kind::Video: return QStringLiteral("video");
			case Kind::Audio: return QStringLiteral("audio");
			case Kind::Document: return QStringLiteral("document");
			case Kind::Binary: return QStringLiteral("binary");
			case Kind::Unknown:
			default: return QStringLiteral("unknown");
		}
	}

	bool supportsInlinePreview(const Kind kind, const QString &mime) {
		return kind == Kind::Image && imageMimes().contains(mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower());
	}

} // namespace ChatAttachments
} // namespace Mumble
