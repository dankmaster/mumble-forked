// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_FINANCEQUOTE_H_
#define MUMBLE_FINANCEQUOTE_H_

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QUrl>

#include <optional>

namespace Mumble {
namespace Finance {
	struct TickerMention {
		QString symbol;
		int start  = -1;
		int length = 0;
		QUrl yahooFinanceUrl;
	};

	enum class FinanceProvider {
		YahooFinance,
		GoogleFinance,
		XCashtag,
		Avanza,
		Nordnet,
		InteractiveBrokers,
	};

	struct FinanceProviderLink {
		FinanceProvider provider = FinanceProvider::YahooFinance;
		QString label;
		QUrl url;
	};

	struct YahooChartQuote {
		struct Point {
			qint64 timestamp = 0;
			double close     = 0.0;
		};

		QString symbol;
		QString shortName;
		QString longName;
		QString exchangeName;
		QString fullExchangeName;
		QString instrumentType;
		QString currency;
		int priceHint = 2;
		double regularMarketPrice = 0.0;
		double previousClose       = 0.0;
		qint64 regularMarketTime   = 0;
		bool hasRegularMarketPrice = false;
		bool hasPreviousClose      = false;
		QList< Point > points;
	};

	QString normalizeTickerSymbol(const QString &symbolText);
	QList< QString > yahooFinanceSymbolCandidates(const QString &symbolText);
	QList< TickerMention > extractTickerMentions(const QString &text, int maxMentions = 3);

	QUrl yahooFinanceQuoteUrl(const QString &symbol);
	QUrl yahooFinanceChartUrl(const QString &symbol, const QString &range = QStringLiteral("1mo"),
							  const QString &interval = QStringLiteral("1d"));
	bool symbolFromYahooFinanceQuoteUrl(const QUrl &url, QString *symbol = nullptr);

	QString financeProviderLabel(FinanceProvider provider);
	QUrl financeProviderUrl(FinanceProvider provider, const QString &symbol, const YahooChartQuote *quote = nullptr);
	QList< FinanceProviderLink > financeProviderLinks(const QString &symbol, const YahooChartQuote *quote = nullptr);
	QString financeProviderSummary(const QString &symbol, const YahooChartQuote *quote = nullptr);

	std::optional< YahooChartQuote > parseYahooChartQuote(const QByteArray &payload, QString *errorMessage = nullptr);
	QString yahooFinanceQuoteTitle(const YahooChartQuote &quote);
	QString yahooFinanceQuoteDescription(const YahooChartQuote &quote);
} // namespace Finance
} // namespace Mumble

#endif
