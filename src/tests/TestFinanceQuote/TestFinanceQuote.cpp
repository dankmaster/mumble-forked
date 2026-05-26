// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FinanceQuote.h"

#include <QtCore/QUrlQuery>

#include <QtTest>

class TestFinanceQuote : public QObject {
	Q_OBJECT

private slots:
	void normalizesTickerSymbols();
	void extractsTickerMentions();
	void extractsYahooFinanceQuoteSymbols();
	void buildsFinanceProviderLinks();
	void parsesYahooChartQuote();
	void usesSparklinePreviousPointWhenDailyCloseIsMissing();
	void rejectsYahooChartErrors();
};

void TestFinanceQuote::normalizesTickerSymbols() {
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("$aapl")), QStringLiteral("AAPL"));
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("brk.b")), QStringLiteral("BRK.B"));
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("btc-usd")), QStringLiteral("BTC-USD"));
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("sek=x")), QStringLiteral("SEK=X"));
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("^gspc")), QStringLiteral("^GSPC"));
	QCOMPARE(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("0700.hk")), QStringLiteral("0700.HK"));

	QVERIFY(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("$5")).isEmpty());
	QVERIFY(Mumble::Finance::normalizeTickerSymbol(QStringLiteral("not a ticker")).isEmpty());
}

void TestFinanceQuote::extractsTickerMentions() {
	const QList< Mumble::Finance::TickerMention > mentions = Mumble::Finance::extractTickerMentions(
		QStringLiteral("Buy $aapl, ignore $5 and $$CASH, then $BRK.B $BTC-USD $AAPL."), 3);

	QCOMPARE(mentions.size(), 3);
	QCOMPARE(mentions.at(0).symbol, QStringLiteral("AAPL"));
	QCOMPARE(mentions.at(1).symbol, QStringLiteral("BRK.B"));
	QCOMPARE(mentions.at(2).symbol, QStringLiteral("BTC-USD"));
	QCOMPARE(mentions.at(0).start, 4);
	QCOMPARE(mentions.at(0).length, 5);
	QCOMPARE(mentions.at(0).yahooFinanceUrl.host(), QStringLiteral("finance.yahoo.com"));

	const QList< Mumble::Finance::TickerMention > embedded =
		Mumble::Finance::extractTickerMentions(QStringLiteral("foo$AAPL $MSFT"));
	QCOMPARE(embedded.size(), 1);
	QCOMPARE(embedded.at(0).symbol, QStringLiteral("MSFT"));

	const QList< Mumble::Finance::TickerMention > lowercaseOnly =
		Mumble::Finance::extractTickerMentions(QStringLiteral("$rklb"));
	QCOMPARE(lowercaseOnly.size(), 1);
	QCOMPARE(lowercaseOnly.at(0).symbol, QStringLiteral("RKLB"));
}

void TestFinanceQuote::extractsYahooFinanceQuoteSymbols() {
	const QUrl quoteUrl = Mumble::Finance::yahooFinanceQuoteUrl(QStringLiteral("^GSPC"));
	QString symbol;
	QVERIFY(Mumble::Finance::symbolFromYahooFinanceQuoteUrl(quoteUrl, &symbol));
	QCOMPARE(symbol, QStringLiteral("^GSPC"));
	QCOMPARE(Mumble::Finance::yahooFinanceChartUrl(symbol).host(), QStringLiteral("query1.finance.yahoo.com"));

	QVERIFY(!Mumble::Finance::symbolFromYahooFinanceQuoteUrl(QUrl(QStringLiteral("https://example.com/quote/AAPL"))));
}

void TestFinanceQuote::buildsFinanceProviderLinks() {
	Mumble::Finance::YahooChartQuote quote;
	quote.symbol       = QStringLiteral("AAPL");
	quote.exchangeName = QStringLiteral("NMS");

	const QList< Mumble::Finance::FinanceProviderLink > links =
		Mumble::Finance::financeProviderLinks(QStringLiteral("AAPL"), &quote);
	QCOMPARE(links.size(), 6);
	QCOMPARE(static_cast< int >(links.at(0).provider),
			 static_cast< int >(Mumble::Finance::FinanceProvider::YahooFinance));
	QCOMPARE(links.at(0).url.host(), QStringLiteral("finance.yahoo.com"));

	const QUrl googleUrl =
		Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::GoogleFinance,
											QStringLiteral("AAPL"), &quote);
	QCOMPARE(googleUrl.host(), QStringLiteral("www.google.com"));
	QVERIFY(googleUrl.path().contains(QStringLiteral("AAPL:NASDAQ")));

	const QUrl xUrl =
		Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::XCashtag, QStringLiteral("AAPL"));
	QCOMPARE(xUrl.host(), QStringLiteral("x.com"));
	QCOMPARE(QUrlQuery(xUrl).queryItemValue(QStringLiteral("q")), QStringLiteral("$AAPL"));

	QCOMPARE(Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::Avanza, QStringLiteral("AAPL"))
				 .host(),
			 QStringLiteral("www.avanza.se"));
	QCOMPARE(Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::Nordnet, QStringLiteral("AAPL"))
				 .path(),
			 QStringLiteral("/aktier/kurser"));
	QCOMPARE(Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::InteractiveBrokers,
												 QStringLiteral("AAPL"))
				 .host(),
			 QStringLiteral("www.interactivebrokers.com"));

	quote.symbol       = QStringLiteral("ERIC-B.ST");
	quote.exchangeName = QStringLiteral("STO");
	const QUrl stockholmGoogleUrl =
		Mumble::Finance::financeProviderUrl(Mumble::Finance::FinanceProvider::GoogleFinance,
											QStringLiteral("ERIC-B.ST"), &quote);
	QVERIFY(stockholmGoogleUrl.path().contains(QStringLiteral("ERIC-B:STO")));

	const QString summary = Mumble::Finance::financeProviderSummary(QStringLiteral("AAPL"), &quote);
	QVERIFY(summary.contains(QStringLiteral("Google Finance")));
	QVERIFY(summary.contains(QStringLiteral("Avanza")));
	QVERIFY(summary.contains(QStringLiteral("Nordnet")));
	QVERIFY(summary.contains(QStringLiteral("IBKR")));
	QVERIFY(!summary.contains(QStringLiteral("Yahoo Finance")));
}

void TestFinanceQuote::parsesYahooChartQuote() {
	const QByteArray payload = R"json(
		{
			"chart": {
				"result": [
					{
						"meta": {
							"currency": "USD",
							"symbol": "AAPL",
							"exchangeName": "NMS",
							"fullExchangeName": "NasdaqGS",
							"instrumentType": "EQUITY",
							"regularMarketTime": 1779480001,
							"regularMarketPrice": 308.82,
							"regularMarketPreviousClose": 304.99,
							"chartPreviousClose": 304.99,
							"shortName": "Apple Inc.",
							"priceHint": 2
						},
						"timestamp": [1779307200, 1779393600, 1779480000],
						"indicators": {
							"quote": [
								{
									"close": [302.10, null, 308.82]
								}
							]
						}
					}
				],
				"error": null
			}
		}
	)json";

	QString errorMessage;
	const std::optional< Mumble::Finance::YahooChartQuote > quote =
		Mumble::Finance::parseYahooChartQuote(payload, &errorMessage);
	QVERIFY2(quote.has_value(), qPrintable(errorMessage));
	QCOMPARE(quote->symbol, QStringLiteral("AAPL"));
	QCOMPARE(quote->shortName, QStringLiteral("Apple Inc."));
	QVERIFY(quote->hasRegularMarketPrice);
	QVERIFY(quote->hasPreviousClose);
	QCOMPARE(quote->points.size(), 2);
	QCOMPARE(quote->points.at(0).timestamp, static_cast< qint64 >(1779307200));
	QCOMPARE(quote->points.at(0).close, 302.10);
	QCOMPARE(quote->points.at(1).close, 308.82);
	QCOMPARE(Mumble::Finance::yahooFinanceQuoteTitle(*quote), QStringLiteral("AAPL 308.82 USD +3.83 (+1.26%)"));
	QVERIFY(Mumble::Finance::yahooFinanceQuoteDescription(*quote).contains(QStringLiteral("Apple Inc.")));
}

void TestFinanceQuote::usesSparklinePreviousPointWhenDailyCloseIsMissing() {
	const QByteArray payload = R"json(
		{
			"chart": {
				"result": [
					{
						"meta": {
							"currency": "USD",
							"symbol": "RKLB",
							"exchangeName": "NMS",
							"regularMarketPrice": 120.00,
							"chartPreviousClose": 80.00,
							"shortName": "Rocket Lab Corporation",
							"priceHint": 2
						},
						"timestamp": [1779307200, 1779393600, 1779480000],
						"indicators": {
							"quote": [
								{
									"close": [84.00, 118.00, 120.00]
								}
							]
						}
					}
				],
				"error": null
			}
		}
	)json";

	QString errorMessage;
	const std::optional< Mumble::Finance::YahooChartQuote > quote =
		Mumble::Finance::parseYahooChartQuote(payload, &errorMessage);
	QVERIFY2(quote.has_value(), qPrintable(errorMessage));
	QVERIFY(quote->hasPreviousClose);
	QCOMPARE(quote->previousClose, 118.00);
	QCOMPARE(Mumble::Finance::yahooFinanceQuoteTitle(*quote), QStringLiteral("RKLB 120.00 USD +2.00 (+1.69%)"));
}

void TestFinanceQuote::rejectsYahooChartErrors() {
	const QByteArray payload = R"json(
		{
			"chart": {
				"result": null,
				"error": {
					"code": "Not Found",
					"description": "No data found, symbol may be delisted"
				}
			}
		}
	)json";

	QString errorMessage;
	QVERIFY(!Mumble::Finance::parseYahooChartQuote(payload, &errorMessage).has_value());
	QCOMPARE(errorMessage, QStringLiteral("No data found, symbol may be delisted"));
}

QTEST_MAIN(TestFinanceQuote)
#include "TestFinanceQuote.moc"
