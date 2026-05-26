// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_STONKSLEDGER_H_
#define MUMBLE_STONKSLEDGER_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstddef>
#include <optional>
#include <vector>

namespace Mumble {
namespace Stonks {
	struct LedgerPositionSummary {
		QString symbol;
		double quantity    = 0.0;
		double marketValue = 0.0;
		QString currency;
	};

	QStringList ledgerPeriods();
	std::optional< qint64 > periodSeconds(const QString &periodText);
	std::optional< double > returnPercent(double startValue, double endValue);
	QString formatPositionSummary(const std::vector< LedgerPositionSummary > &positions, std::size_t maxPositions = 4);
} // namespace Stonks
} // namespace Mumble

#endif
