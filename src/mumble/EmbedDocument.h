// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_EMBEDDOCUMENT_H_
#define MUMBLE_MUMBLE_EMBEDDOCUMENT_H_

#include <QtCore/QVariantMap>

namespace EmbedDocument {
	inline constexpr int SchemaVersion = 1;

	/**
	 * Converts an already security-normalized chat preview into the stable,
	 * provider-independent document consumed by the Qt Quick presentation.
	 *
	 * The input must have passed the URL, media and metadata capability checks in
	 * QmlClientModels. This function deliberately does not accept raw network
	 * responses or HTML.
	 */
	QVariantMap fromNormalizedPreview(const QVariantMap &preview);

	/**
	 * Builds the matching typed document for a normalized message attachment.
	 * Missing previews remain explicit capabilities instead of invented poster
	 * data, so video/audio thumbnail generation can be added independently.
	 */
	QVariantMap fromNormalizedAttachment(const QVariantMap &attachment);
}

#endif
