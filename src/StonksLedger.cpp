// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksLedger.h"
#include "FinanceQuote.h"

#include <QtCore/QHash>
#include <QtCore/QLocale>
#include <QtCore/QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace {
	struct PopularTickerAccumulator {
		Mumble::Stonks::PopularTickerSummary summary;
		QSet< unsigned int > holders;
		unsigned int anonymousHolderCount = 0;
		bool mixedCurrencies              = false;
	};

	QString trimmedFixedNumber(double value, int precision) {
		if (!std::isfinite(value)) {
			return QString();
		}

		QLocale locale = QLocale::c();
		locale.setNumberOptions(QLocale::OmitGroupSeparator);
		QString text = locale.toString(value, 'f', precision);
		while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
			text.chop(1);
		}
		if (text.endsWith(QLatin1Char('.'))) {
			text.chop(1);
		}
		return text == QLatin1String("-0") ? QStringLiteral("0") : text;
	}

	QString formatSummaryMoney(double value, const QString &currencyText) {
		if (!std::isfinite(value) || value <= 0.0) {
			return QString();
		}

		QLocale locale = QLocale::c();
		locale.setNumberOptions(QLocale::OmitGroupSeparator);
		const QString currency =
			currencyText.trimmed().isEmpty() ? QStringLiteral("USD") : currencyText.trimmed().toUpper();
		return QStringLiteral("%1 %2").arg(currency, locale.toString(value, 'f', 2));
	}

	QString naturalJoin(QStringList parts) {
		parts.removeAll(QString());
		if (parts.isEmpty()) {
			return QString();
		}
		if (parts.size() == 1) {
			return parts.front();
		}
		if (parts.size() == 2) {
			return QStringLiteral("%1 and %2").arg(parts.at(0), parts.at(1));
		}

		const QString last = parts.takeLast();
		return QStringLiteral("%1, and %2").arg(parts.join(QStringLiteral(", ")), last);
	}

	void adoptTickerMetadata(PopularTickerAccumulator &entry,
							 const Mumble::Stonks::PopularTickerPosition &position) {
		if (entry.summary.displayName.trimmed().isEmpty() && !position.displayName.trimmed().isEmpty()) {
			entry.summary.displayName = position.displayName.trimmed();
		}
		if (entry.summary.providerID.trimmed().isEmpty() && !position.providerID.trimmed().isEmpty()) {
			entry.summary.providerID = position.providerID.trimmed();
		}
		if (entry.summary.providerSymbol.trimmed().isEmpty() && !position.providerSymbol.trimmed().isEmpty()) {
			entry.summary.providerSymbol = Mumble::Finance::normalizeTickerSymbol(position.providerSymbol);
		}
		if (entry.summary.exchange.trimmed().isEmpty() && !position.exchange.trimmed().isEmpty()) {
			entry.summary.exchange = position.exchange.trimmed();
		}
		if (entry.summary.quoteSourceURL.trimmed().isEmpty() && !position.quoteSourceURL.trimmed().isEmpty()) {
			entry.summary.quoteSourceURL = position.quoteSourceURL.trimmed();
		}
	}

	bool higherPopularTicker(const Mumble::Stonks::PopularTickerSummary &lhs,
							 const Mumble::Stonks::PopularTickerSummary &rhs) {
		if (lhs.holderCount != rhs.holderCount) {
			return lhs.holderCount > rhs.holderCount;
		}
		if (lhs.totalMarketValue != rhs.totalMarketValue) {
			return lhs.totalMarketValue > rhs.totalMarketValue;
		}
		if (lhs.totalQuantity != rhs.totalQuantity) {
			return lhs.totalQuantity > rhs.totalQuantity;
		}
		return lhs.symbol.localeAwareCompare(rhs.symbol) < 0;
	}

	qint64 valuationBucketEnd(const qint64 timestamp, const qint64 bucketSeconds) {
		if (timestamp <= 0 || bucketSeconds <= 0) {
			return 0;
		}
		return ((timestamp / bucketSeconds) + 1) * bucketSeconds - 1;
	}
}

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

	QString formatPositionSummary(const std::vector< LedgerPositionSummary > &positions, std::size_t maxPositions) {
		QStringList formattedPositions;
		for (const LedgerPositionSummary &position : positions) {
			const QString symbol = position.symbol.trimmed().toUpper();
			const QString quantity = trimmedFixedNumber(position.quantity, 4);
			if (symbol.isEmpty() || quantity.isEmpty()) {
				continue;
			}

			QString summary = QStringLiteral("%1 %2").arg(quantity, symbol);
			const QString value = formatSummaryMoney(position.marketValue, position.currency);
			if (!value.isEmpty()) {
				summary += QStringLiteral(" (%1)").arg(value);
			}
			formattedPositions << summary;
		}

		if (formattedPositions.isEmpty()) {
			return QString();
		}

		const qsizetype positionCount = formattedPositions.size();
		qsizetype limit               = positionCount;
		if (maxPositions > 0 && maxPositions < static_cast< std::size_t >(positionCount)) {
			limit = static_cast< qsizetype >(maxPositions);
		}
		QStringList visible = formattedPositions.mid(0, limit);
		const qsizetype hiddenCount = positionCount - limit;
		if (hiddenCount == 1) {
			visible << QStringLiteral("1 more position");
		} else if (hiddenCount > 1) {
			visible << QStringLiteral("%1 more positions").arg(hiddenCount);
		}

		return naturalJoin(visible);
	}

	std::vector< PopularTickerSummary > popularTickers(const std::vector< PopularTickerPosition > &positions,
													   std::size_t maxTickers) {
		QHash< QString, PopularTickerAccumulator > bySymbol;
		for (const PopularTickerPosition &position : positions) {
			const QString symbol = Mumble::Finance::normalizeTickerSymbol(position.symbol);
			if (symbol.isEmpty()) {
				continue;
			}

			const double quantity    = std::isfinite(position.quantity) && position.quantity > 0.0 ? position.quantity : 0.0;
			const double marketValue = std::isfinite(position.marketValue) && position.marketValue > 0.0
										   ? position.marketValue
										   : 0.0;
			if (quantity <= 0.0 && marketValue <= 0.0) {
				continue;
			}

			PopularTickerAccumulator &entry = bySymbol[symbol];
			entry.summary.symbol            = symbol;
			if (position.holderID > 0) {
				entry.holders.insert(position.holderID);
			} else {
				++entry.anonymousHolderCount;
			}
			entry.summary.totalQuantity += quantity;
			entry.summary.totalMarketValue += marketValue;
			entry.summary.latestUpdatedAt = std::max(entry.summary.latestUpdatedAt, position.updatedAt);
			if (!position.currency.trimmed().isEmpty()) {
				const QString currency = position.currency.trimmed().toUpper();
				if (entry.summary.currency.trimmed().isEmpty()) {
					entry.summary.currency = currency;
				} else if (entry.summary.currency != currency) {
					entry.mixedCurrencies = true;
				}
			}
			adoptTickerMetadata(entry, position);
		}

		std::vector< PopularTickerSummary > summaries;
		summaries.reserve(static_cast< std::size_t >(bySymbol.size()));
		for (auto it = bySymbol.cbegin(); it != bySymbol.cend(); ++it) {
			PopularTickerSummary summary = it.value().summary;
			summary.holderCount          = static_cast< unsigned int >(it.value().holders.size())
								  + it.value().anonymousHolderCount;
			if (it.value().mixedCurrencies) {
				summary.currency.clear();
			}
			if (summary.providerSymbol.trimmed().isEmpty()) {
				summary.providerSymbol = summary.symbol;
			}
			if (summary.holderCount > 0) {
				summaries.push_back(std::move(summary));
			}
		}

		std::sort(summaries.begin(), summaries.end(), higherPopularTicker);
		if (maxTickers > 0 && summaries.size() > maxTickers) {
			summaries.resize(maxTickers);
		}
		return summaries;
	}

	std::vector< ValuationSample > buildValuationTimeline(
		const std::vector< PortfolioRevision > &inputRevisions, const std::vector< QuoteSeries > &inputQuotes,
		const qint64 earliestAt, const qint64 latestAt, const qint64 bucketSeconds,
		const qint64 maximumQuoteAgeSeconds, const QString &source) {
		if (inputRevisions.empty() || inputQuotes.empty() || earliestAt <= 0 || latestAt < earliestAt
			|| bucketSeconds <= 0 || maximumQuoteAgeSeconds < 0) {
			return {};
		}

		std::vector< PortfolioRevision > revisions = inputRevisions;
		std::sort(revisions.begin(), revisions.end(), [](const PortfolioRevision &lhs, const PortfolioRevision &rhs) {
			if (lhs.effectiveAt != rhs.effectiveAt) {
				return lhs.effectiveAt < rhs.effectiveAt;
			}
			return lhs.revisionID < rhs.revisionID;
		});

		std::map< QString, QuoteSeries > quotes;
		std::set< qint64 > buckets;
		for (QuoteSeries series : inputQuotes) {
			series.symbol   = Mumble::Finance::normalizeTickerSymbol(series.symbol);
			series.currency = series.currency.trimmed().toUpper();
			if (series.symbol.isEmpty()) {
				continue;
			}
			std::stable_sort(series.points.begin(), series.points.end(), [](const QuotePoint &lhs, const QuotePoint &rhs) {
				return lhs.timestamp < rhs.timestamp;
			});
			series.points.erase(std::remove_if(series.points.begin(), series.points.end(), [](const QuotePoint &point) {
				return point.timestamp <= 0 || !std::isfinite(point.close) || point.close <= 0.0;
			}), series.points.end());
			std::vector< QuotePoint > uniquePoints;
			uniquePoints.reserve(series.points.size());
			for (const QuotePoint &point : series.points) {
				if (!uniquePoints.empty() && uniquePoints.back().timestamp == point.timestamp) {
					uniquePoints.back() = point;
				} else {
					uniquePoints.push_back(point);
				}
			}
			series.points = std::move(uniquePoints);
			for (const QuotePoint &point : series.points) {
				const qint64 bucket = valuationBucketEnd(point.timestamp, bucketSeconds);
				if (bucket >= earliestAt && bucket <= latestAt) {
					buckets.insert(bucket);
				}
			}
			quotes[series.symbol] = std::move(series);
		}

		for (const PortfolioRevision &revision : revisions) {
			const qint64 bucket = valuationBucketEnd(revision.effectiveAt, bucketSeconds);
			if (bucket >= earliestAt && bucket <= latestAt) {
				buckets.insert(bucket);
			}
		}
		const qint64 latestBucket = valuationBucketEnd(latestAt, bucketSeconds);
		if (latestBucket >= earliestAt) {
			buckets.insert(std::min(latestBucket, latestAt));
		}

		std::vector< ValuationSample > samples;
		for (const qint64 valuedAt : buckets) {
			auto revisionIt = std::upper_bound(
				revisions.begin(), revisions.end(), valuedAt,
				[](const qint64 timestamp, const PortfolioRevision &revision) { return timestamp < revision.effectiveAt; });
			if (revisionIt == revisions.begin()) {
				continue;
			}
			--revisionIt;
			// The exact revision timestamp is persisted from the submitted portfolio value. Never replace that
			// trustworthy boundary point with an estimated quote merely because it happens to land on a bucket end.
			if (valuedAt == revisionIt->effectiveAt) {
				continue;
			}

			ValuationSample sample;
			sample.revisionID    = revisionIt->revisionID;
			sample.valuedAt      = valuedAt;
			sample.currency      = revisionIt->currency.trimmed().toUpper();
			sample.source        = source.trimmed().isEmpty() ? QStringLiteral("automatic") : source.trimmed();
			sample.totalPositions = static_cast< unsigned int >(revisionIt->positions.size());

			for (const ValuationPosition &position : revisionIt->positions) {
				const QString symbol = Mumble::Finance::normalizeTickerSymbol(position.symbol);
				const auto quoteIt    = quotes.find(symbol);
				if (quoteIt == quotes.end() || !std::isfinite(position.quantity) || position.quantity <= 0.0) {
					continue;
				}
				const QString positionCurrency = position.currency.trimmed().toUpper();
				if (!positionCurrency.isEmpty() && !quoteIt->second.currency.isEmpty()
					&& positionCurrency != quoteIt->second.currency) {
					continue;
				}

				const std::vector< QuotePoint > &points = quoteIt->second.points;
				auto pointIt = std::upper_bound(points.begin(), points.end(), valuedAt,
					[](const qint64 timestamp, const QuotePoint &point) { return timestamp < point.timestamp; });
				if (pointIt == points.begin()) {
					continue;
				}
				--pointIt;
				if (valuedAt - pointIt->timestamp > maximumQuoteAgeSeconds) {
					continue;
				}
				const double marketValue = position.quantity * pointIt->close;
				if (!std::isfinite(marketValue) || marketValue <= 0.0) {
					continue;
				}
				sample.totalValue += marketValue;
				++sample.pricedPositions;
			}

			if (sample.pricedPositions > 0 && std::isfinite(sample.totalValue) && sample.totalValue > 0.0) {
				samples.push_back(std::move(sample));
			}
		}

		return samples;
	}

	std::optional< ReturnWindow > timeWeightedReturn(const std::vector< ValuationSample > &inputSamples,
												 const qint64 cutoffAt, const qint64 latestAt,
												 const qint64 baselineToleranceSeconds) {
		if (cutoffAt <= 0 || latestAt <= cutoffAt || baselineToleranceSeconds < 0) {
			return std::nullopt;
		}

		std::vector< ValuationSample > samples;
		for (const ValuationSample &sample : inputSamples) {
			if (sample.complete() && sample.valuedAt > 0 && sample.valuedAt <= latestAt) {
				samples.push_back(sample);
			}
		}
		std::sort(samples.begin(), samples.end(), [](const ValuationSample &lhs, const ValuationSample &rhs) {
			if (lhs.valuedAt != rhs.valuedAt) {
				return lhs.valuedAt < rhs.valuedAt;
			}
			return lhs.revisionID < rhs.revisionID;
		});
		if (samples.size() < 2) {
			return std::nullopt;
		}

		std::size_t startIndex = samples.size();
		for (std::size_t i = 0; i < samples.size(); ++i) {
			if (samples[i].valuedAt <= cutoffAt) {
				if (cutoffAt - samples[i].valuedAt <= baselineToleranceSeconds) {
					startIndex = i;
				}
				continue;
			}
			if (startIndex == samples.size()) {
				startIndex = i;
			}
			break;
		}
		if (startIndex >= samples.size() - 1) {
			return std::nullopt;
		}

		const ValuationSample &start = samples[startIndex];
		const ValuationSample &end   = samples.back();
		if (latestAt - end.valuedAt > baselineToleranceSeconds) {
			return std::nullopt;
		}
		double chainedGrowth         = 1.0;
		unsigned int segmentCount    = 0;
		std::size_t segmentStart     = startIndex;
		for (std::size_t i = startIndex + 1; i <= samples.size(); ++i) {
			const bool segmentEnded = i == samples.size() || samples[i].revisionID != samples[segmentStart].revisionID;
			if (!segmentEnded) {
				continue;
			}
			const ValuationSample &segmentFirst = samples[segmentStart];
			const ValuationSample &segmentLast  = samples[i - 1];
			if (segmentLast.valuedAt > segmentFirst.valuedAt && segmentFirst.totalValue > 0.0
				&& segmentLast.totalValue > 0.0) {
				const double growth = segmentLast.totalValue / segmentFirst.totalValue;
				if (!std::isfinite(growth) || growth <= 0.0) {
					return std::nullopt;
				}
				chainedGrowth *= growth;
				++segmentCount;
			}
			segmentStart = i;
		}

		if (segmentCount == 0 || !std::isfinite(chainedGrowth)) {
			return std::nullopt;
		}

		ReturnWindow result;
		result.returnPercent    = (chainedGrowth - 1.0) * 100.0;
		result.startValue       = start.totalValue;
		result.endValue         = end.totalValue;
		result.startAt          = start.valuedAt;
		result.endAt            = end.valuedAt;
		result.coverageSeconds  = std::max< qint64 >(0, end.valuedAt - start.valuedAt);
		result.requestedSeconds = std::max< qint64 >(0, latestAt - cutoffAt);
		result.sampleCount      = static_cast< unsigned int >(samples.size() - startIndex);
		result.segmentCount     = segmentCount;
		result.partialPeriod    = start.valuedAt > cutoffAt + baselineToleranceSeconds;
		for (std::size_t i = startIndex; i < samples.size(); ++i) {
			if (samples[i].source.compare(QStringLiteral("submitted"), Qt::CaseInsensitive) != 0) {
				result.estimated = true;
				break;
			}
		}
		return result;
	}
} // namespace Stonks
} // namespace Mumble
