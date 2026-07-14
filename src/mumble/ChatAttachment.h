// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATATTACHMENT_H_
#define MUMBLE_MUMBLE_CHATATTACHMENT_H_

#include <QtCore/QList>
#include <QtCore/QMetaType>
#include <QtCore/QString>

namespace Mumble {
namespace ChatAttachments {

	enum class Kind : unsigned int {
		Unknown  = 0,
		Image    = 1,
		Video    = 2,
		Document = 3,
		Binary   = 4,
		Audio    = 5,
	};

	struct Source {
		QString draftID;
		QString path;
		QString fileName;
		QString mime;
		QString sha256;
		quint64 byteSize = 0;
		Kind kind        = Kind::Unknown;
	};

	struct Classification {
		QString mime;
		Kind kind = Kind::Unknown;
	};

	Classification classifyFile(const QString &path);
	QString kindName(Kind kind);
	bool supportsInlinePreview(Kind kind, const QString &mime);

} // namespace ChatAttachments
} // namespace Mumble

Q_DECLARE_METATYPE(Mumble::ChatAttachments::Source)
Q_DECLARE_METATYPE(QList< Mumble::ChatAttachments::Source >)

#endif // MUMBLE_MUMBLE_CHATATTACHMENT_H_
