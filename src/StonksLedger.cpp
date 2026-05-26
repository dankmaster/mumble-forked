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

		const int limit = maxPositions == 0
							  ? formattedPositions.size()
							  : std::min< int >(formattedPositions.size(), static_cast< int >(maxPositions));
		QStringList visible = formattedPositions.mid(0, limit);
		const int hiddenCount = formattedPositions.size() - limit;
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
} // namespace Stonks
} // namespace Mumble
