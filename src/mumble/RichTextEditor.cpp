// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RichTextEditor.h"

#include <QtCore/QBuffer>
#include <QtCore/QIODevice>
#include <QtGui/QImageReader>


bool RichTextImage::isValidImage(const QByteArray &ba, QByteArray &fmt) {
	QBuffer qb;
	qb.setData(ba);
	if (!qb.open(QIODevice::ReadOnly)) {
		return false;
	}

	QByteArray detectedFormat = QImageReader::imageFormat(&qb).toLower();
	if (detectedFormat == QByteArray("png") || detectedFormat == QByteArray("jpg")
		|| detectedFormat == QByteArray("jpeg") || detectedFormat == QByteArray("gif")) {
		fmt = detectedFormat;
		return true;
	}

	return false;
}
