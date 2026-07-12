// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RichTextEditor.h"

#include "Log.h"
#include "MainWindow.h"
#include "XMLTools.h"
#include "Global.h"

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolTip>

#ifdef Q_OS_WIN
#	include <shlobj.h>
#endif


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
