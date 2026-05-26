// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_STONKSCOMMAND_H_
#define MUMBLE_STONKSCOMMAND_H_

#include <QtCore/QString>

#include <optional>

namespace Mumble {
namespace Stonks {
	enum class CommandType {
		None,
		Help,
		Quote,
		SetScore,
		Leaderboard,
		Me,
		Follow,
		Unfollow,
		Following,
	};

	struct Command {
		CommandType type = CommandType::None;
		QString symbol;
		QString period;
		double scorePercent = 0.0;
		QString targetName;
	};

	QString normalizePeriod(const QString &periodText);
	std::optional< Command > parseCommand(const QString &bodyText);
} // namespace Stonks
} // namespace Mumble

#endif
