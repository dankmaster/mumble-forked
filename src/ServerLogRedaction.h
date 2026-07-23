// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVERLOGREDACTION_H_
#define MUMBLE_SERVERLOGREDACTION_H_

#include <QString>

namespace Mumble {
namespace ServerLog {
	QString superUserBootstrapNotice(unsigned int serverID);
	QString redactSensitiveText(const QString &text);
} // namespace ServerLog
} // namespace Mumble

#endif // MUMBLE_SERVERLOGREDACTION_H_
