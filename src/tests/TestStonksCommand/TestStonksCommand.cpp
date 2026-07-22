// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksCommand.h"
#include "StonksLedger.h"

#include <QtTest>

#include <limits>
#include <vector>

class TestStonksCommand : public QObject {
	Q_OBJECT

private slots:
	void parsesQuoteCommands();
	void parsesScoreCommands();
	void parsesLeaderboardAndFollowCommands();
	void computesLedgerPeriodsAndReturns();
	void reconstructsValuationTimeline();
	void computesTimeWeightedReturnsAcrossPortfolioChanges();
	void formatsLedgerPositionSummaries();
	void ranksPopularTickers();
	void ignoresUnknownText();
};

void TestStonksCommand::parsesQuoteCommands() {
	const std::optional< Mumble::Stonks::Command > bare = Mumble::Stonks::parseCommand(QStringLiteral("rklb"));
	QVERIFY(bare.has_value());
	QCOMPARE(static_cast< int >(bare->type), static_cast< int >(Mumble::Stonks::CommandType::Quote));
	QCOMPARE(bare->symbol, QStringLiteral("RKLB"));

	const std::optional< Mumble::Stonks::Command > explicitQuote =
		Mumble::Stonks::parseCommand(QStringLiteral("quote eric-b.st"));
	QVERIFY(explicitQuote.has_value());
	QCOMPARE(static_cast< int >(explicitQuote->type), static_cast< int >(Mumble::Stonks::CommandType::Quote));
	QCOMPARE(explicitQuote->symbol, QStringLiteral("ERIC-B.ST"));

	const std::optional< Mumble::Stonks::Command > swedishClassQuote =
		Mumble::Stonks::parseCommand(QStringLiteral("quote saab b"));
	QVERIFY(swedishClassQuote.has_value());
	QCOMPARE(static_cast< int >(swedishClassQuote->type), static_cast< int >(Mumble::Stonks::CommandType::Quote));
	QCOMPARE(swedishClassQuote->symbol, QStringLiteral("SAAB-B"));

	const std::optional< Mumble::Stonks::Command > explicitCashtagQuote =
		Mumble::Stonks::parseCommand(QStringLiteral("quote $rklb"));
	QVERIFY(explicitCashtagQuote.has_value());
	QCOMPARE(static_cast< int >(explicitCashtagQuote->type), static_cast< int >(Mumble::Stonks::CommandType::Quote));
	QCOMPARE(explicitCashtagQuote->symbol, QStringLiteral("RKLB"));

	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("$RKLB")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("this is just chat")).has_value());
}

void TestStonksCommand::parsesScoreCommands() {
	const std::optional< Mumble::Stonks::Command > defaultPeriod =
		Mumble::Stonks::parseCommand(QStringLiteral("score +12.5%"));
	QVERIFY(defaultPeriod.has_value());
	QCOMPARE(static_cast< int >(defaultPeriod->type), static_cast< int >(Mumble::Stonks::CommandType::SetScore));
	QCOMPARE(defaultPeriod->period, QStringLiteral("30d"));
	QCOMPARE(defaultPeriod->scorePercent, 12.5);

	const std::optional< Mumble::Stonks::Command > week =
		Mumble::Stonks::parseCommand(QStringLiteral("score 7d -2,25"));
	QVERIFY(week.has_value());
	QCOMPARE(week->period, QStringLiteral("7d"));
	QCOMPARE(week->scorePercent, -2.25);
}

void TestStonksCommand::parsesLeaderboardAndFollowCommands() {
	const std::optional< Mumble::Stonks::Command > leaderboard =
		Mumble::Stonks::parseCommand(QStringLiteral("leaderboard ytd"));
	QVERIFY(leaderboard.has_value());
	QCOMPARE(static_cast< int >(leaderboard->type), static_cast< int >(Mumble::Stonks::CommandType::Leaderboard));
	QCOMPARE(leaderboard->period, QStringLiteral("ytd"));

	const std::optional< Mumble::Stonks::Command > follow =
		Mumble::Stonks::parseCommand(QStringLiteral("follow @dank master"));
	QVERIFY(follow.has_value());
	QCOMPARE(static_cast< int >(follow->type), static_cast< int >(Mumble::Stonks::CommandType::Follow));
	QCOMPARE(follow->targetName, QStringLiteral("dank master"));
}

void TestStonksCommand::computesLedgerPeriodsAndReturns() {
	QCOMPARE(Mumble::Stonks::ledgerPeriods(), QStringList({ QStringLiteral("1d"), QStringLiteral("7d"),
															QStringLiteral("30d"), QStringLiteral("ytd") }));
	QCOMPARE(Mumble::Stonks::periodSeconds(QStringLiteral("1d")).value(), static_cast< qint64 >(24 * 60 * 60));
	QCOMPARE(Mumble::Stonks::periodSeconds(QStringLiteral("7d")).value(), static_cast< qint64 >(7 * 24 * 60 * 60));
	QCOMPARE(Mumble::Stonks::periodSeconds(QStringLiteral("30d")).value(), static_cast< qint64 >(30 * 24 * 60 * 60));
	QVERIFY(!Mumble::Stonks::periodSeconds(QStringLiteral("90d")).has_value());

	const std::optional< double > positive = Mumble::Stonks::returnPercent(100.0, 115.0);
	QVERIFY(positive.has_value());
	QCOMPARE(*positive, 15.0);

	const std::optional< double > negative = Mumble::Stonks::returnPercent(200.0, 150.0);
	QVERIFY(negative.has_value());
	QCOMPARE(*negative, -25.0);

	QVERIFY(!Mumble::Stonks::returnPercent(0.0, 150.0).has_value());
	QVERIFY(!Mumble::Stonks::returnPercent(std::numeric_limits< double >::infinity(), 150.0).has_value());
}

void TestStonksCommand::reconstructsValuationTimeline() {
	using namespace Mumble::Stonks;
	PortfolioRevision revision;
	revision.revisionID  = 7;
	revision.userID      = 42;
	revision.effectiveAt = 1;
	revision.currency    = QStringLiteral("USD");
	revision.positions   = {
		{ QStringLiteral("RKLB"), QStringLiteral("USD"), 2.0 },
		{ QStringLiteral("AMD"), QStringLiteral("USD"), 1.0 },
	};

	QuoteSeries rklb { QStringLiteral("rklb"), QStringLiteral("usd"), { { 10, 10.0 }, { 110, 11.0 } } };
	QuoteSeries amd { QStringLiteral("AMD"), QStringLiteral("USD"), { { 10, 20.0 }, { 110, 25.0 } } };
	const std::vector< ValuationSample > timeline =
		buildValuationTimeline({ revision }, { rklb, amd }, 1, 199, 100, 200, QStringLiteral("historical"));

	QCOMPARE(timeline.size(), static_cast< std::size_t >(2));
	QCOMPARE(timeline.at(0).valuedAt, static_cast< qint64 >(99));
	QCOMPARE(timeline.at(0).revisionID, 7u);
	QCOMPARE(timeline.at(0).totalValue, 40.0);
	QCOMPARE(timeline.at(0).pricedPositions, 2u);
	QVERIFY(timeline.at(0).complete());
	QCOMPARE(timeline.at(1).valuedAt, static_cast< qint64 >(199));
	QCOMPARE(timeline.at(1).totalValue, 47.0);

	revision.effectiveAt = 99;
	const std::vector< ValuationSample > exactRevisionBoundary =
		buildValuationTimeline({ revision }, { rklb, amd }, 1, 199, 100, 200, QStringLiteral("historical"));
	QCOMPARE(exactRevisionBoundary.size(), static_cast< std::size_t >(1));
	QCOMPARE(exactRevisionBoundary.at(0).valuedAt, static_cast< qint64 >(199));

	revision.effectiveAt = 1;
	amd.currency = QStringLiteral("SEK");
	const std::vector< ValuationSample > currencyMismatch =
		buildValuationTimeline({ revision }, { rklb, amd }, 1, 199, 100, 200, QStringLiteral("historical"));
	QCOMPARE(currencyMismatch.size(), static_cast< std::size_t >(2));
	QCOMPARE(currencyMismatch.at(0).pricedPositions, 1u);
	QVERIFY(!currencyMismatch.at(0).complete());
}

void TestStonksCommand::computesTimeWeightedReturnsAcrossPortfolioChanges() {
	using namespace Mumble::Stonks;
	const std::vector< ValuationSample > samples {
		{ 1, 1000, 100.0, QStringLiteral("USD"), QStringLiteral("historical"), 1, 1 },
		{ 1, 2000, 110.0, QStringLiteral("USD"), QStringLiteral("historical"), 1, 1 },
		// The contribution at the portfolio revision boundary must not count as investment return.
		{ 2, 3000, 200.0, QStringLiteral("USD"), QStringLiteral("submitted"), 2, 2 },
		{ 2, 5000, 220.0, QStringLiteral("USD"), QStringLiteral("automatic"), 2, 2 },
	};
	const std::optional< ReturnWindow > result = timeWeightedReturn(samples, 1000, 5000, 0);
	QVERIFY(result.has_value());
	QVERIFY(qAbs(result->returnPercent - 21.0) < 0.000001);
	QCOMPARE(result->startValue, 100.0);
	QCOMPARE(result->endValue, 220.0);
	QCOMPARE(result->segmentCount, 2u);
	QCOMPARE(result->sampleCount, 4u);
	QVERIFY(!result->partialPeriod);
	QVERIFY(result->estimated);

	const std::vector< ValuationSample > partialSamples {
		{ 3, 2500, 50.0, QStringLiteral("USD"), QStringLiteral("submitted"), 1, 1 },
		{ 3, 5000, 55.0, QStringLiteral("USD"), QStringLiteral("automatic"), 1, 1 },
	};
	const std::optional< ReturnWindow > partial = timeWeightedReturn(partialSamples, 1000, 5000, 100);
	QVERIFY(partial.has_value());
	QVERIFY(partial->partialPeriod);
	QVERIFY(qAbs(partial->returnPercent - 10.0) < 0.000001);

	std::vector< ValuationSample > incomplete = partialSamples;
	incomplete.at(1).pricedPositions          = 0;
	QVERIFY(!timeWeightedReturn(incomplete, 1000, 5000, 100).has_value());
	QVERIFY(!timeWeightedReturn(partialSamples, 1000, 10000, 100).has_value());
}

void TestStonksCommand::formatsLedgerPositionSummaries() {
	const std::vector< Mumble::Stonks::LedgerPositionSummary > positions {
		{ QStringLiteral("TSLA"), 1.0, 342.09, QStringLiteral("USD") },
		{ QStringLiteral("rklb"), 1.0, 229.39, QStringLiteral("usd") },
	};
	QCOMPARE(Mumble::Stonks::formatPositionSummary(positions),
			 QStringLiteral("1 TSLA (USD 342.09) and 1 RKLB (USD 229.39)"));

	const std::vector< Mumble::Stonks::LedgerPositionSummary > manyPositions {
		{ QStringLiteral("TSLA"), 1.0, 342.09, QStringLiteral("USD") },
		{ QStringLiteral("RKLB"), 2.5, 573.48, QStringLiteral("USD") },
		{ QStringLiteral("NVDA"), 3.0, 411.0, QStringLiteral("USD") },
	};
	QCOMPARE(Mumble::Stonks::formatPositionSummary(manyPositions, 2),
			 QStringLiteral("1 TSLA (USD 342.09), 2.5 RKLB (USD 573.48), and 1 more position"));
}

void TestStonksCommand::ranksPopularTickers() {
	const std::vector< Mumble::Stonks::PopularTickerPosition > positions {
		{ 1, QStringLiteral("rklb"), 2.0, 200.0, QStringLiteral("usd"), QStringLiteral("Rocket Lab"), QString(), QString(), QString(), QString(), 100 },
		{ 2, QStringLiteral("RKLB"), 1.0, 120.0, QStringLiteral("USD"), QString(), QString(), QString(), QString(), QString(), 120 },
		{ 3, QStringLiteral("AAPL"), 1.0, 400.0, QStringLiteral("USD"), QStringLiteral("Apple"), QString(), QString(), QString(), QString(), 90 },
		{ 4, QStringLiteral("TSLA"), 1.0, 300.0, QStringLiteral("USD"), QStringLiteral("Tesla"), QString(), QString(), QString(), QString(), 80 },
		{ 1, QStringLiteral("not a ticker"), 1.0, 999.0, QStringLiteral("USD"), QString(), QString(), QString(), QString(), QString(), 70 },
	};

	const std::vector< Mumble::Stonks::PopularTickerSummary > popular =
		Mumble::Stonks::popularTickers(positions, 2);
	QCOMPARE(popular.size(), static_cast< std::size_t >(2));
	QCOMPARE(popular.at(0).symbol, QStringLiteral("RKLB"));
	QCOMPARE(popular.at(0).displayName, QStringLiteral("Rocket Lab"));
	QCOMPARE(popular.at(0).holderCount, 2u);
	QCOMPARE(popular.at(0).totalQuantity, 3.0);
	QCOMPARE(popular.at(0).totalMarketValue, 320.0);
	QCOMPARE(popular.at(0).latestUpdatedAt, static_cast< qint64 >(120));
	QCOMPARE(popular.at(1).symbol, QStringLiteral("AAPL"));
}

void TestStonksCommand::ignoresUnknownText() {
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("leaderboard forever")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 30d nope")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 90d +4")).has_value());
}

QTEST_MAIN(TestStonksCommand)
#include "TestStonksCommand.moc"
