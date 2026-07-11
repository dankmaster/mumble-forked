// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_RICHTEXTEDITOR_H_
#define MUMBLE_MUMBLE_RICHTEXTEDITOR_H_

#include <QtCore/QByteArray>

class RichTextImage final {
public:
	static bool isValidImage(const QByteArray &buffer, QByteArray &format);
};

#endif
