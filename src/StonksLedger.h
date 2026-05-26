// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_STONKSLEDGER_H_
#define MUMBLE_STONKSLEDGER_H_

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

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

	struct PopularTickerPosition {
		unsigned int holderID = 0;
		QString symbol;
		double quantity    = 0.0;
		double marketValue = 0.0;
		QString currency;
		QString displayName;
		QString providerID;
		QString providerSymbol;
		QString exchange;
		QString quoteSourceURL;
		qint64 updatedAt = 0;
	};

	struct PopularTickerSummary {
		QString symbol;
		QString displayName;
		unsigned int holderCount = 0;
		double totalQuantity     = 0.0;
		double totalMarketValue  = 0.0;
		QString currency;
		QString providerID;
		QString providerSymbol;
		QString exchange;
		QString quoteSourceURL;
		qint64 latestUpdatedAt = 0;
	};

	QStringList ledgerPeriods();
	std::optional< qint64 > periodSeconds(const QString &periodText);
	std::optional< double > returnPercent(double startValue, double endValue);
	QString formatPositionSummary(const std::vector< LedgerPositionSummary > &positions, std::size_t maxPositions = 4);
	std::vector< PopularTickerSummary > popularTickers(const std::vector< PopularTickerPosition > &positions,
													   std::size_t maxTickers = 5);
} // namespace Stonks
} // namespace Mumble

#endif
