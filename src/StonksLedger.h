// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_STONKSLEDGER_H_
#define MUMBLE_STONKSLEDGER_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>

namespace Mumble {
namespace Stonks {
	QStringList ledgerPeriods();
	std::optional< qint64 > periodSeconds(const QString &periodText);
	std::optional< double > returnPercent(double startValue, double endValue);
} // namespace Stonks
} // namespace Mumble

#endif
