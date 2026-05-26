// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FinanceQuote.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QTimeZone>
#include <QtCore/QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {
bool jsonNumber(const QJsonObject &object, const QString &key, double *value) {
	if (!value) {
		return false;
	}

	const QJsonValue jsonValue = object.value(key);
	if (!jsonValue.isDouble()) {
		return false;
	}

	*value = jsonValue.toDouble();
	return true;
}

int boundedPriceHint(int priceHint) {
	return qBound(0, priceHint, 8);
}

QString formatQuoteNumber(double value, int priceHint) {
	QLocale locale = QLocale::c();
	return locale.toString(value, 'f', boundedPriceHint(priceHint));
}

QString signedQuoteNumber(double value, int priceHint) {
	const QString formatted = formatQuoteNumber(value, priceHint);
	return value > 0.0 ? QStringLiteral("+%1").arg(formatted) : formatted;
}

QString displayName(const Mumble::Finance::YahooChartQuote &quote) {
	if (!quote.shortName.trimmed().isEmpty()) {
		return quote.shortName.trimmed();
	}
	if (!quote.longName.trimmed().isEmpty()) {
		return quote.longName.trimmed();
	}
	return quote.symbol;
}

QString googleExchangeForYahooQuote(const QString &symbol, const Mumble::Finance::YahooChartQuote *quote) {
	if (quote) {
		const QString exchangeName = quote->exchangeName.trimmed().toUpper();
		if (exchangeName == QLatin1String("NMS") || exchangeName == QLatin1String("NCM")
			|| exchangeName == QLatin1String("NGM") || exchangeName == QLatin1String("NQ")
			|| exchangeName == QLatin1String("NASDAQ")) {
			return QStringLiteral("NASDAQ");
		}
		if (exchangeName == QLatin1String("NYQ") || exchangeName == QLatin1String("NYSE")) {
			return QStringLiteral("NYSE");
		}
		if (exchangeName == QLatin1String("ASE") || exchangeName == QLatin1String("AMEX")) {
			return QStringLiteral("NYSEAMERICAN");
		}
		if (exchangeName == QLatin1String("PCX") || exchangeName == QLatin1String("ARCX")) {
			return QStringLiteral("NYSEARCA");
		}
		if (exchangeName == QLatin1String("STO")) {
			return QStringLiteral("STO");
		}
		if (exchangeName == QLatin1String("LSE")) {
			return QStringLiteral("LON");
		}
		if (exchangeName == QLatin1String("TOR")) {
			return QStringLiteral("TSE");
		}
	}

	const QString upperSymbol = symbol.toUpper();
	if (upperSymbol.endsWith(QLatin1String(".ST"))) {
		return QStringLiteral("STO");
	}
	if (upperSymbol.endsWith(QLatin1String(".L"))) {
		return QStringLiteral("LON");
	}
	if (upperSymbol.endsWith(QLatin1String(".TO"))) {
		return QStringLiteral("TSE");
	}
	if (upperSymbol.endsWith(QLatin1String(".AX"))) {
		return QStringLiteral("ASX");
	}
	if (upperSymbol.endsWith(QLatin1String(".HK"))) {
		return QStringLiteral("HKG");
	}

	return QString();
}

QString googleTickerForYahooSymbol(const QString &symbol, const QString &googleExchange) {
	QString googleTicker = symbol;
	if (googleExchange == QLatin1String("STO") && googleTicker.endsWith(QLatin1String(".ST"))) {
		googleTicker.chop(3);
	} else if (googleExchange == QLatin1String("LON") && googleTicker.endsWith(QLatin1String(".L"))) {
		googleTicker.chop(2);
	} else if (googleExchange == QLatin1String("TSE") && googleTicker.endsWith(QLatin1String(".TO"))) {
		googleTicker.chop(3);
	} else if (googleExchange == QLatin1String("ASX") && googleTicker.endsWith(QLatin1String(".AX"))) {
		googleTicker.chop(3);
	} else if (googleExchange == QLatin1String("HKG") && googleTicker.endsWith(QLatin1String(".HK"))) {
		googleTicker.chop(3);
	}

	if ((googleExchange == QLatin1String("NYSE") || googleExchange == QLatin1String("NASDAQ"))
		&& googleTicker.contains(QLatin1Char('-'))) {
		googleTicker.replace(QLatin1Char('-'), QLatin1Char('.'));
	}

	return googleExchange.isEmpty() ? googleTicker : QStringLiteral("%1:%2").arg(googleTicker, googleExchange);
}

QUrl urlWithQueryItem(const QString &host, const QString &path, const QString &key, const QString &value) {
	QUrl url;
	url.setScheme(QStringLiteral("https"));
	url.setHost(host);
	url.setPath(path);

	QUrlQuery query;
	query.addQueryItem(key, value);
	url.setQuery(query);
	return url;
}

constexpr int MAX_YAHOO_SYMBOL_CANDIDATES = 12;

struct YahooMarketFallback {
	QString market;
	QString countryCode;
	QString suffix;
	QStringList classStyles;
	int priority = 0;
};

const QList< YahooMarketFallback > &yahooMarketFallbacks() {
	static const QList< YahooMarketFallback > fallbacks = {
		{ QStringLiteral("SE"), QStringLiteral("SE"), QStringLiteral(".ST"),
		  { QStringLiteral("-B"), QStringLiteral("-A") }, 10 },
		{ QStringLiteral("NO"), QStringLiteral("NO"), QStringLiteral(".OL"),
		  { QStringLiteral("-B"), QStringLiteral("-A") }, 20 },
		{ QStringLiteral("DK"), QStringLiteral("DK"), QStringLiteral(".CO"),
		  { QStringLiteral("-B"), QStringLiteral("-A") }, 30 },
		{ QStringLiteral("FI"), QStringLiteral("FI"), QStringLiteral(".HE"),
		  { QStringLiteral("-B"), QStringLiteral("-A") }, 40 },
		{ QStringLiteral("DE"), QStringLiteral("DE"), QStringLiteral(".DE"), {}, 50 },
		{ QStringLiteral("UK"), QStringLiteral("GB"), QStringLiteral(".L"), {}, 60 },
		{ QStringLiteral("CA-TSX"), QStringLiteral("CA"), QStringLiteral(".TO"), {}, 70 },
		{ QStringLiteral("CA-TSXV"), QStringLiteral("CA"), QStringLiteral(".V"), {}, 80 },
		{ QStringLiteral("AU"), QStringLiteral("AU"), QStringLiteral(".AX"), {}, 90 },
		{ QStringLiteral("HK"), QStringLiteral("HK"), QStringLiteral(".HK"), {}, 100 },
	};
	return fallbacks;
}

QString systemLocaleCountryCode() {
	const QString localeName = QLocale::system().name().toUpper();
	const int separatorIndex = localeName.indexOf(QLatin1Char('_'));
	if (separatorIndex < 0 || separatorIndex + 3 > localeName.size()) {
		return {};
	}
	return localeName.mid(separatorIndex + 1, 2);
}

QList< YahooMarketFallback > orderedYahooMarketFallbacks() {
	QList< YahooMarketFallback > fallbacks = yahooMarketFallbacks();
	const QString countryCode             = systemLocaleCountryCode();
	std::stable_sort(fallbacks.begin(), fallbacks.end(), [&countryCode](const YahooMarketFallback &left,
																		const YahooMarketFallback &right) {
		const bool leftLocaleMatch  = !countryCode.isEmpty() && left.countryCode == countryCode;
		const bool rightLocaleMatch = !countryCode.isEmpty() && right.countryCode == countryCode;
		if (leftLocaleMatch != rightLocaleMatch) {
			return leftLocaleMatch;
		}
		return left.priority < right.priority;
	});
	return fallbacks;
}

void appendYahooCandidate(QList< Mumble::Finance::YahooFinanceSymbolCandidate > &candidates, QSet< QString > &seenSymbols,
						  const QString &symbol, const QString &market, const QString &reason, int priority) {
	if (candidates.size() >= MAX_YAHOO_SYMBOL_CANDIDATES) {
		return;
	}

	const QString normalizedSymbol = Mumble::Finance::normalizeTickerSymbol(symbol);
	if (normalizedSymbol.isEmpty() || seenSymbols.contains(normalizedSymbol)) {
		return;
	}

	seenSymbols.insert(normalizedSymbol);
	Mumble::Finance::YahooFinanceSymbolCandidate candidate;
	candidate.symbol   = normalizedSymbol;
	candidate.market   = market;
	candidate.reason   = reason;
	candidate.priority = priority;
	candidates.push_back(candidate);
}

const YahooMarketFallback *marketFallbackForExplicitSuffix(const QString &symbol) {
	for (const YahooMarketFallback &fallback : yahooMarketFallbacks()) {
		if (symbol.endsWith(fallback.suffix)) {
			return &fallback;
		}
	}

	return nullptr;
}

bool hasSingleLetterClassSuffix(const QString &symbol) {
	const int dashIndex = symbol.lastIndexOf(QLatin1Char('-'));
	if (dashIndex <= 0 || symbol.indexOf(QLatin1Char('-')) != dashIndex) {
		return false;
	}

	const QString base        = symbol.left(dashIndex);
	const QString classSuffix = symbol.mid(dashIndex + 1);
	return base.size() <= 6 && !base.contains(QLatin1Char('.')) && classSuffix.size() == 1
		   && classSuffix.at(0).isLetter();
}

bool isShortClasslessEquitySymbol(const QString &symbol) {
	return !symbol.isEmpty() && symbol.size() <= 6 && symbol.at(0).isLetter() && !symbol.startsWith(QLatin1Char('^'))
		   && !symbol.contains(QLatin1Char('=')) && !symbol.contains(QLatin1Char('.'))
		   && !symbol.contains(QLatin1Char('-'));
}

bool isMarketFallbackEligible(const QString &symbol) {
	if (symbol.isEmpty() || symbol.startsWith(QLatin1Char('^')) || symbol.contains(QLatin1Char('='))
		|| symbol.contains(QLatin1Char('.'))) {
		return false;
	}

	if (symbol.contains(QLatin1Char('-'))) {
		return hasSingleLetterClassSuffix(symbol);
	}

	return isShortClasslessEquitySymbol(symbol);
}
} // namespace

namespace Mumble {
namespace Finance {
	QString normalizeTickerSymbol(const QString &symbolText) {
		QString symbol = symbolText.trimmed();
		if (symbol.startsWith(QLatin1Char('$'))) {
			symbol.remove(0, 1);
		}
		symbol = symbol.toUpper();

		static const QRegularExpression validTickerPattern(QStringLiteral(
			R"(^(\^?[A-Z][A-Z0-9]*(?:[.=-][A-Z0-9]+){0,4}|\d{3,}[A-Z0-9]*(?:[.=-][A-Z0-9]+){1,4})$)"));
		return validTickerPattern.match(symbol).hasMatch() ? symbol : QString();
	}

	QList< YahooFinanceSymbolCandidate > yahooFinanceSymbolCandidateInfos(const QString &symbolText) {
		const QString normalizedSymbol = normalizeTickerSymbol(symbolText);
		if (normalizedSymbol.isEmpty()) {
			return {};
		}

		QList< YahooFinanceSymbolCandidate > candidates;
		QSet< QString > seenSymbols;
		appendYahooCandidate(candidates, seenSymbols, normalizedSymbol, QString(), QStringLiteral("exact"), 0);

		if (const YahooMarketFallback *explicitMarket = marketFallbackForExplicitSuffix(normalizedSymbol)) {
			QString marketBase = normalizedSymbol;
			marketBase.chop(explicitMarket->suffix.size());
			if (isShortClasslessEquitySymbol(marketBase)) {
				for (const QString &classStyle : explicitMarket->classStyles) {
					appendYahooCandidate(candidates, seenSymbols,
										 QStringLiteral("%1%2%3").arg(marketBase, classStyle, explicitMarket->suffix),
										 explicitMarket->market, QStringLiteral("explicit-market-class-fallback"),
										 explicitMarket->priority + 1);
				}
			}
			return candidates;
		}

		if (!isMarketFallbackEligible(normalizedSymbol)) {
			return candidates;
		}

		const QList< YahooMarketFallback > orderedFallbacks = orderedYahooMarketFallbacks();
		const bool hasClassSuffix                           = hasSingleLetterClassSuffix(normalizedSymbol);

		if (hasClassSuffix) {
			for (const YahooMarketFallback &fallback : orderedFallbacks) {
				if (!fallback.classStyles.isEmpty()) {
					appendYahooCandidate(candidates, seenSymbols, normalizedSymbol + fallback.suffix, fallback.market,
										 QStringLiteral("market-class-fallback"), fallback.priority);
				}
			}
			return candidates;
		}

		const QString primaryClassStyle = QStringLiteral("-B");

		for (const YahooMarketFallback &fallback : orderedFallbacks) {
			appendYahooCandidate(candidates, seenSymbols, normalizedSymbol + fallback.suffix, fallback.market,
								 QStringLiteral("market-fallback"), fallback.priority);

			if (fallback.market == QLatin1String("SE") && fallback.classStyles.contains(primaryClassStyle)) {
				appendYahooCandidate(candidates, seenSymbols,
									 QStringLiteral("%1%2%3").arg(normalizedSymbol, primaryClassStyle, fallback.suffix),
									 fallback.market, QStringLiteral("market-class-fallback"), fallback.priority + 1);
			}
		}

		return candidates;
	}

	QList< QString > yahooFinanceSymbolCandidates(const QString &symbolText) {
		const QList< YahooFinanceSymbolCandidate > candidateInfos = yahooFinanceSymbolCandidateInfos(symbolText);
		QList< QString > symbols;
		for (const YahooFinanceSymbolCandidate &candidate : candidateInfos) {
			symbols.push_back(candidate.symbol);
		}
		return symbols;
	}

	QList< TickerMention > extractTickerMentions(const QString &text, int maxMentions) {
		if (text.isEmpty() || maxMentions <= 0) {
			return {};
		}

		static const QRegularExpression tickerPattern(QStringLiteral(
			R"((^|[^A-Za-z0-9_$])\$((?:\^?[A-Za-z][A-Za-z0-9]*(?:[.=-][A-Za-z0-9]+){0,4})|(?:\d{3,}[A-Za-z0-9]*(?:[.=-][A-Za-z0-9]+){1,4})))"));

		QSet< QString > seenSymbols;
		QList< TickerMention > mentions;
		QRegularExpressionMatchIterator it = tickerPattern.globalMatch(text);
		while (it.hasNext() && mentions.size() < maxMentions) {
			const QRegularExpressionMatch match = it.next();
			const QString symbol                = normalizeTickerSymbol(match.captured(2));
			if (symbol.isEmpty() || seenSymbols.contains(symbol)) {
				continue;
			}

			seenSymbols.insert(symbol);
			TickerMention mention;
			mention.symbol          = symbol;
			mention.start           = static_cast< int >(match.capturedStart(2) - 1);
			mention.length          = static_cast< int >(match.capturedLength(2) + 1);
			mention.yahooFinanceUrl = yahooFinanceQuoteUrl(symbol);
			mentions.push_back(mention);
		}

		return mentions;
	}

	QUrl yahooFinanceQuoteUrl(const QString &symbol) {
		const QString normalizedSymbol = normalizeTickerSymbol(symbol);
		if (normalizedSymbol.isEmpty()) {
			return {};
		}

		QUrl url;
		url.setScheme(QStringLiteral("https"));
		url.setHost(QStringLiteral("finance.yahoo.com"));
		url.setPath(QStringLiteral("/quote/%1").arg(normalizedSymbol));
		return url;
	}

	QUrl yahooFinanceChartUrl(const QString &symbol, const QString &range, const QString &interval) {
		const QString normalizedSymbol = normalizeTickerSymbol(symbol);
		if (normalizedSymbol.isEmpty()) {
			return {};
		}

		QUrl url;
		url.setScheme(QStringLiteral("https"));
		url.setHost(QStringLiteral("query1.finance.yahoo.com"));
		url.setPath(QStringLiteral("/v8/finance/chart/%1").arg(normalizedSymbol));

		QUrlQuery query;
		query.addQueryItem(QStringLiteral("interval"), interval.trimmed().isEmpty() ? QStringLiteral("1d") : interval.trimmed());
		query.addQueryItem(QStringLiteral("range"), range.trimmed().isEmpty() ? QStringLiteral("1mo") : range.trimmed());
		url.setQuery(query);
		return url;
	}

	bool symbolFromYahooFinanceQuoteUrl(const QUrl &url, QString *symbol) {
		if (!url.isValid() || url.scheme() != QLatin1String("https")) {
			return false;
		}

		const QString host = url.host().trimmed().toLower();
		if (host != QLatin1String("finance.yahoo.com") && host != QLatin1String("www.finance.yahoo.com")) {
			return false;
		}

		const QStringList pathSegments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
		if (pathSegments.size() < 2 || pathSegments.at(0) != QLatin1String("quote")) {
			return false;
		}

		const QString normalizedSymbol = normalizeTickerSymbol(pathSegments.at(1));
		if (normalizedSymbol.isEmpty()) {
			return false;
		}

		if (symbol) {
			*symbol = normalizedSymbol;
		}
		return true;
	}

	QString financeProviderLabel(FinanceProvider provider) {
		switch (provider) {
			case FinanceProvider::YahooFinance:
				return QStringLiteral("Yahoo Finance");
			case FinanceProvider::GoogleFinance:
				return QStringLiteral("Google Finance");
			case FinanceProvider::XCashtag:
				return QStringLiteral("X");
			case FinanceProvider::Avanza:
				return QStringLiteral("Avanza");
			case FinanceProvider::Nordnet:
				return QStringLiteral("Nordnet");
			case FinanceProvider::InteractiveBrokers:
				return QStringLiteral("IBKR");
		}

		return QString();
	}

	QUrl financeProviderUrl(FinanceProvider provider, const QString &symbol, const YahooChartQuote *quote) {
		const QString normalizedSymbol = normalizeTickerSymbol(symbol);
		if (normalizedSymbol.isEmpty()) {
			return {};
		}

		switch (provider) {
			case FinanceProvider::YahooFinance:
				return yahooFinanceQuoteUrl(normalizedSymbol);
			case FinanceProvider::GoogleFinance: {
				const QString googleExchange = googleExchangeForYahooQuote(normalizedSymbol, quote);
				const QString googleTicker   = googleTickerForYahooSymbol(normalizedSymbol, googleExchange);
				QUrl url;
				url.setScheme(QStringLiteral("https"));
				url.setHost(QStringLiteral("www.google.com"));
				url.setPath(QStringLiteral("/finance/quote/%1").arg(googleTicker));
				return url;
			}
			case FinanceProvider::XCashtag: {
				QUrl url;
				url.setScheme(QStringLiteral("https"));
				url.setHost(QStringLiteral("x.com"));
				url.setPath(QStringLiteral("/search"));

				QUrlQuery query;
				query.addQueryItem(QStringLiteral("q"), QStringLiteral("$%1").arg(normalizedSymbol));
				query.addQueryItem(QStringLiteral("src"), QStringLiteral("cashtag_click"));
				url.setQuery(query);
				return url;
			}
			case FinanceProvider::Avanza:
				return urlWithQueryItem(QStringLiteral("www.avanza.se"), QStringLiteral("/sok.html"),
										QStringLiteral("query"), normalizedSymbol);
			case FinanceProvider::Nordnet:
				return urlWithQueryItem(QStringLiteral("www.nordnet.se"), QStringLiteral("/aktier/kurser"),
										QStringLiteral("search"), normalizedSymbol);
			case FinanceProvider::InteractiveBrokers:
				return urlWithQueryItem(QStringLiteral("www.interactivebrokers.com"),
										QStringLiteral("/en/trading/symbol.php"), QStringLiteral("symbol"),
										normalizedSymbol);
		}

		return {};
	}

	QList< FinanceProviderLink > financeProviderLinks(const QString &symbol, const YahooChartQuote *quote) {
		static const QList< FinanceProvider > providers = { FinanceProvider::YahooFinance, FinanceProvider::GoogleFinance,
															FinanceProvider::XCashtag, FinanceProvider::Avanza,
															FinanceProvider::Nordnet,
															FinanceProvider::InteractiveBrokers };

		QList< FinanceProviderLink > links;
		for (const FinanceProvider provider : providers) {
			const QUrl url = financeProviderUrl(provider, symbol, quote);
			if (!url.isValid()) {
				continue;
			}

			links.push_back(FinanceProviderLink { provider, financeProviderLabel(provider), url });
		}
		return links;
	}

	QString financeProviderSummary(const QString &symbol, const YahooChartQuote *quote) {
		QStringList labels;
		for (const FinanceProviderLink &link : financeProviderLinks(symbol, quote)) {
			if (link.provider == FinanceProvider::YahooFinance) {
				continue;
			}
			labels << link.label;
		}

		return labels.isEmpty() ? QString() : QStringLiteral("Also on %1").arg(labels.join(QStringLiteral(", ")));
	}

	std::optional< YahooChartQuote > parseYahooChartQuote(const QByteArray &payload, QString *errorMessage) {
		const auto setError = [errorMessage](const QString &message) {
			if (errorMessage) {
				*errorMessage = message;
			}
		};

		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			setError(parseError.errorString());
			return std::nullopt;
		}

		const QJsonObject chart = document.object().value(QStringLiteral("chart")).toObject();
		const QJsonValue errorValue = chart.value(QStringLiteral("error"));
		if (errorValue.isObject()) {
			const QJsonObject errorObject = errorValue.toObject();
			const QString description = errorObject.value(QStringLiteral("description")).toString();
			setError(description.isEmpty() ? QStringLiteral("Yahoo Finance returned an error") : description);
			return std::nullopt;
		}

		const QJsonArray results = chart.value(QStringLiteral("result")).toArray();
		if (results.isEmpty() || !results.first().isObject()) {
			setError(QStringLiteral("Yahoo Finance response did not include a chart result"));
			return std::nullopt;
		}

		const QJsonObject meta = results.first().toObject().value(QStringLiteral("meta")).toObject();
		YahooChartQuote quote;
		quote.symbol           = normalizeTickerSymbol(meta.value(QStringLiteral("symbol")).toString());
		quote.shortName        = meta.value(QStringLiteral("shortName")).toString().trimmed();
		quote.longName         = meta.value(QStringLiteral("longName")).toString().trimmed();
		quote.exchangeName     = meta.value(QStringLiteral("exchangeName")).toString().trimmed();
		quote.fullExchangeName = meta.value(QStringLiteral("fullExchangeName")).toString().trimmed();
		quote.instrumentType   = meta.value(QStringLiteral("instrumentType")).toString().trimmed();
		quote.currency         = meta.value(QStringLiteral("currency")).toString().trimmed();
		quote.priceHint        = boundedPriceHint(meta.value(QStringLiteral("priceHint")).toInt(2));
		quote.regularMarketTime = static_cast< qint64 >(meta.value(QStringLiteral("regularMarketTime")).toDouble(0.0));
		quote.hasRegularMarketPrice =
			jsonNumber(meta, QStringLiteral("regularMarketPrice"), &quote.regularMarketPrice);
		quote.hasPreviousClose = jsonNumber(meta, QStringLiteral("regularMarketPreviousClose"), &quote.previousClose)
								 || jsonNumber(meta, QStringLiteral("previousClose"), &quote.previousClose);

		if (quote.symbol.isEmpty()) {
			setError(QStringLiteral("Yahoo Finance response did not include a valid symbol"));
			return std::nullopt;
		}

		const QJsonArray timestamps = results.first().toObject().value(QStringLiteral("timestamp")).toArray();
		const QJsonObject indicators = results.first().toObject().value(QStringLiteral("indicators")).toObject();
		const QJsonArray quoteObjects = indicators.value(QStringLiteral("quote")).toArray();
		const QJsonArray closeValues =
			quoteObjects.isEmpty() ? QJsonArray() : quoteObjects.first().toObject().value(QStringLiteral("close")).toArray();
		const qsizetype pointCount = std::min(timestamps.size(), closeValues.size());
		for (qsizetype i = 0; i < pointCount; ++i) {
			const QJsonValue timestampValue = timestamps.at(i);
			const QJsonValue closeValue     = closeValues.at(i);
			if (!timestampValue.isDouble() || !closeValue.isDouble()) {
				continue;
			}

			const double close = closeValue.toDouble();
			if (!std::isfinite(close)) {
				continue;
			}

			YahooChartQuote::Point point;
			point.timestamp = static_cast< qint64 >(timestampValue.toDouble(0.0));
			point.close     = close;
			quote.points.push_back(point);
		}

		if (!quote.hasPreviousClose && quote.points.size() >= 2) {
			const double previousPointClose = quote.points.at(quote.points.size() - 2).close;
			if (std::isfinite(previousPointClose) && std::abs(previousPointClose) > 0.0000001) {
				quote.previousClose    = previousPointClose;
				quote.hasPreviousClose = true;
			}
		}

		return quote;
	}

	QString yahooFinanceQuoteTitle(const YahooChartQuote &quote) {
		if (!quote.hasRegularMarketPrice) {
			return QStringLiteral("%1 on Yahoo Finance").arg(quote.symbol);
		}

		QString title = QStringLiteral("%1 %2")
							.arg(quote.symbol, formatQuoteNumber(quote.regularMarketPrice, quote.priceHint));
		if (!quote.currency.isEmpty()) {
			title += QStringLiteral(" %1").arg(quote.currency);
		}

		if (quote.hasPreviousClose && std::abs(quote.previousClose) > 0.0000001) {
			const double change = quote.regularMarketPrice - quote.previousClose;
			const double changePercent = (change / quote.previousClose) * 100.0;
			title += QStringLiteral(" %1 (%2%)")
						 .arg(signedQuoteNumber(change, quote.priceHint),
							  signedQuoteNumber(changePercent, 2));
		}

		return title;
	}

	QString yahooFinanceQuoteDescription(const YahooChartQuote &quote) {
		QStringList parts;
		const QString name = displayName(quote);
		if (!name.isEmpty() && name != quote.symbol) {
			parts << name;
		}
		if (!quote.fullExchangeName.isEmpty()) {
			parts << quote.fullExchangeName;
		} else if (!quote.exchangeName.isEmpty()) {
			parts << quote.exchangeName;
		}
		if (!quote.instrumentType.isEmpty()) {
			parts << quote.instrumentType;
		}
		if (quote.regularMarketTime > 0) {
			const QDateTime updatedAt = QDateTime::fromSecsSinceEpoch(quote.regularMarketTime, QTimeZone::UTC);
			parts << QStringLiteral("Updated %1 UTC").arg(updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
		}
		const QString providerSummary = financeProviderSummary(quote.symbol, &quote);
		if (!providerSummary.isEmpty()) {
			parts << providerSummary;
		}

		return parts.isEmpty() ? QStringLiteral("Yahoo Finance quote") : parts.join(QStringLiteral(" · "));
	}
} // namespace Finance
} // namespace Mumble
