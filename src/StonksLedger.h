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
#include <cmath>
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

	struct ValuationPosition {
		QString symbol;
		QString currency;
		double quantity = 0.0;
	};

	struct PortfolioRevision {
		unsigned int revisionID = 0;
		unsigned int userID     = 0;
		qint64 effectiveAt      = 0;
		QString currency;
		std::vector< ValuationPosition > positions;
	};

	struct QuotePoint {
		qint64 timestamp = 0;
		double close     = 0.0;
	};

	struct QuoteSeries {
		QString symbol;
		QString currency;
		std::vector< QuotePoint > points;
	};

	struct ValuationSample {
		unsigned int revisionID = 0;
		qint64 valuedAt          = 0;
		double totalValue       = 0.0;
		QString currency;
		QString source;
		unsigned int pricedPositions = 0;
		unsigned int totalPositions  = 0;

		bool complete() const {
			return totalPositions > 0 && pricedPositions == totalPositions && std::isfinite(totalValue)
				   && totalValue > 0.0;
		}
	};

	struct ReturnWindow {
		double returnPercent        = 0.0;
		double startValue           = 0.0;
		double endValue             = 0.0;
		qint64 startAt              = 0;
		qint64 endAt                = 0;
		qint64 coverageSeconds      = 0;
		qint64 requestedSeconds     = 0;
		unsigned int sampleCount    = 0;
		unsigned int segmentCount   = 0;
		bool partialPeriod          = false;
		bool estimated              = false;
	};

	QStringList ledgerPeriods();
	std::optional< qint64 > periodSeconds(const QString &periodText);
	std::optional< double > returnPercent(double startValue, double endValue);
	QString formatPositionSummary(const std::vector< LedgerPositionSummary > &positions, std::size_t maxPositions = 4);
	std::vector< PopularTickerSummary > popularTickers(const std::vector< PopularTickerPosition > &positions,
													   std::size_t maxTickers = 5);
	std::vector< ValuationSample > buildValuationTimeline(
		const std::vector< PortfolioRevision > &revisions, const std::vector< QuoteSeries > &quotes,
		qint64 earliestAt, qint64 latestAt, qint64 bucketSeconds, qint64 maximumQuoteAgeSeconds,
		const QString &source);
	std::optional< ReturnWindow > timeWeightedReturn(const std::vector< ValuationSample > &samples,
												 qint64 cutoffAt, qint64 latestAt,
												 qint64 baselineToleranceSeconds = 4 * 24 * 60 * 60);
} // namespace Stonks
} // namespace Mumble

#endif
