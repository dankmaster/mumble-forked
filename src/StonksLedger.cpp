// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksLedger.h"

#include <cmath>

namespace Mumble {
namespace Stonks {
	QStringList ledgerPeriods() {
		return { QStringLiteral("1d"), QStringLiteral("7d"), QStringLiteral("30d"), QStringLiteral("ytd") };
	}

	std::optional< qint64 > periodSeconds(const QString &periodText) {
		const QString period = periodText.trimmed().toLower();
		if (period == QLatin1String("1d")) {
			return 24 * 60 * 60;
		}
		if (period == QLatin1String("7d")) {
			return 7 * 24 * 60 * 60;
		}
		if (period == QLatin1String("30d")) {
			return 30 * 24 * 60 * 60;
		}
		if (period == QLatin1String("ytd")) {
			return 366 * 24 * 60 * 60;
		}
		return std::nullopt;
	}

	std::optional< double > returnPercent(double startValue, double endValue) {
		if (!std::isfinite(startValue) || !std::isfinite(endValue) || std::abs(startValue) < 0.0000001) {
			return std::nullopt;
		}

		return ((endValue - startValue) / startValue) * 100.0;
	}
} // namespace Stonks
} // namespace Mumble
