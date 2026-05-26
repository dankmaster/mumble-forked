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
	void formatsLedgerPositionSummaries();
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

void TestStonksCommand::ignoresUnknownText() {
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("leaderboard forever")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 30d nope")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 90d +4")).has_value());
}

QTEST_MAIN(TestStonksCommand)
#include "TestStonksCommand.moc"
