// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksCommand.h"

#include <QtTest>

class TestStonksCommand : public QObject {
	Q_OBJECT

private slots:
	void parsesQuoteCommands();
	void parsesScoreCommands();
	void parsesLeaderboardAndFollowCommands();
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

void TestStonksCommand::ignoresUnknownText() {
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("leaderboard forever")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 30d nope")).has_value());
	QVERIFY(!Mumble::Stonks::parseCommand(QStringLiteral("score 90d +4")).has_value());
}

QTEST_MAIN(TestStonksCommand)
#include "TestStonksCommand.moc"
