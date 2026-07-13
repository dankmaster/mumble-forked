// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Screen.h"

#include "MumbleApplication.h"

#include <QScreen>

namespace Mumble {
namespace Screen {

	QScreen *screenAt(const QPoint &point) { return qApp->screenAt(point); }

} // namespace Screen
} // namespace Mumble
