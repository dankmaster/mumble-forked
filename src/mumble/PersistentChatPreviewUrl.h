// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATPREVIEWURL_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATPREVIEWURL_H_

#include <QtCore/QString>
#include <QtCore/QUrl>

namespace mumble::chat {

// This resolves optional metadata references, not network authorization.
// Callers must still apply the preview target and request safety checks.
inline QUrl resolveOptionalPreviewUrl(const QUrl &base, const QString &reference) {
	const QString trimmed = reference.trimmed();
	if (trimmed.isEmpty()) return {};
	return base.isEmpty() ? QUrl(trimmed) : base.resolved(QUrl(trimmed));
}

}

#endif
