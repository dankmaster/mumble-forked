// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksScore.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksScore::DBStonksScore(unsigned int serverID, unsigned int userID, const std::string &period)
			: serverID(serverID), userID(userID), period(period) {}

		bool operator==(const DBStonksScore &lhs, const DBStonksScore &rhs) {
			return lhs.serverID == rhs.serverID && lhs.userID == rhs.userID && lhs.period == rhs.period
				   && lhs.scorePercent == rhs.scorePercent && toEpochSeconds(lhs.updatedAt) == toEpochSeconds(rhs.updatedAt);
		}

		bool operator!=(const DBStonksScore &lhs, const DBStonksScore &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
