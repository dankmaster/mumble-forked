// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksCommand.h"

#include "FinanceQuote.h"

#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStringList>

namespace {
QString cleanedToken(QString token) {
	token = token.trimmed();
	if (token.startsWith(QLatin1Char('@'))) {
		token.remove(0, 1);
	}
	return token;
}

bool parsePercent(QString text, double *value) {
	if (!value) {
		return false;
	}

	text = text.trimmed();
	if (text.endsWith(QLatin1Char('%'))) {
		text.chop(1);
	}
	text.replace(QLatin1Char(','), QLatin1Char('.'));

	bool ok         = false;
	const double v  = QLocale::c().toDouble(text, &ok);
	const bool sane = ok && v >= -10000.0 && v <= 10000.0;
	if (sane) {
		*value = v;
	}
	return sane;
}

bool looksLikeBareTicker(const QString &text) {
	const QString trimmed = text.trimmed();
	if (trimmed.contains(QRegularExpression(QStringLiteral(R"(\s)")))) {
		return false;
	}

	if (trimmed.size() > 5
		&& !trimmed.contains(QLatin1Char('.')) && !trimmed.contains(QLatin1Char('-'))
		&& !trimmed.contains(QLatin1Char('=')) && !trimmed.startsWith(QLatin1Char('^'))) {
		return false;
	}

	return !Mumble::Finance::normalizeTickerSymbol(trimmed).isEmpty();
}
} // namespace

namespace Mumble {
namespace Stonks {
	QString normalizePeriod(const QString &periodText) {
		QString period = periodText.trimmed().toLower();
		if (period.isEmpty()) {
			return QStringLiteral("30d");
		}
		if (period == QLatin1String("day") || period == QLatin1String("today")) {
			return QStringLiteral("1d");
		}
		if (period == QLatin1String("week")) {
			return QStringLiteral("7d");
		}
		if (period == QLatin1String("month")) {
			return QStringLiteral("30d");
		}
		if (period == QLatin1String("year")) {
			return QStringLiteral("ytd");
		}

		static const QSet< QString > validPeriods = { QStringLiteral("1d"), QStringLiteral("7d"),
													  QStringLiteral("30d"), QStringLiteral("ytd") };
		return validPeriods.contains(period) ? period : QString();
	}

	std::optional< Command > parseCommand(const QString &bodyText) {
		const QString text = bodyText.trimmed();
		if (text.isEmpty()) {
			return std::nullopt;
		}

		const QStringList rawTokens = text.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
		if (rawTokens.isEmpty()) {
			return std::nullopt;
		}

		QString verb = rawTokens.front().trimmed().toLower();
		if (verb.startsWith(QLatin1Char('/')) || verb.startsWith(QLatin1Char('!'))) {
			verb.remove(0, 1);
		}

		if (verb == QLatin1String("help") || verb == QLatin1String("stonks")) {
			return Command { CommandType::Help };
		}

		if (verb == QLatin1String("me")) {
			return Command { CommandType::Me };
		}

		if (verb == QLatin1String("following")) {
			return Command { CommandType::Following };
		}

		if (verb == QLatin1String("leaderboard") || verb == QLatin1String("lb") || verb == QLatin1String("top")) {
			Command command;
			command.type   = CommandType::Leaderboard;
			command.period = rawTokens.size() >= 2 ? normalizePeriod(rawTokens.at(1)) : QStringLiteral("30d");
			return command.period.isEmpty() ? std::nullopt : std::optional< Command >(command);
		}

		if (verb == QLatin1String("quote") || verb == QLatin1String("q")) {
			if (rawTokens.size() < 2) {
				return std::nullopt;
			}

			Command command;
			command.type   = CommandType::Quote;
			command.symbol = Mumble::Finance::normalizeTickerSymbol(rawTokens.at(1));
			return command.symbol.isEmpty() ? std::nullopt : std::optional< Command >(command);
		}

		if (verb == QLatin1String("score") || verb == QLatin1String("setscore")) {
			if (rawTokens.size() < 2) {
				return std::nullopt;
			}

			Command command;
			command.type = CommandType::SetScore;

			int scoreTokenIndex = 1;
			command.period      = QStringLiteral("30d");
			if (rawTokens.size() >= 3) {
				const QString period = normalizePeriod(rawTokens.at(1));
				if (!period.isEmpty()) {
					command.period = period;
					scoreTokenIndex = 2;
				}
			}

			if (!parsePercent(rawTokens.value(scoreTokenIndex), &command.scorePercent)) {
				return std::nullopt;
			}
			return command;
		}

		if (verb == QLatin1String("follow") || verb == QLatin1String("unfollow")) {
			if (rawTokens.size() < 2) {
				return std::nullopt;
			}

			Command command;
			command.type       = verb == QLatin1String("follow") ? CommandType::Follow : CommandType::Unfollow;
			command.targetName = cleanedToken(rawTokens.mid(1).join(QLatin1Char(' ')));
			return command.targetName.isEmpty() ? std::nullopt : std::optional< Command >(command);
		}

		if (looksLikeBareTicker(text)) {
			Command command;
			command.type   = CommandType::Quote;
			command.symbol = Mumble::Finance::normalizeTickerSymbol(text);
			return command;
		}

		return std::nullopt;
	}
} // namespace Stonks
} // namespace Mumble
