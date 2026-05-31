// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksFeedPreferences.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksFeedPreferences::DBStonksFeedPreferences(unsigned int serverID, unsigned int userID)
			: serverID(serverID), userID(userID) {}

		bool operator==(const DBStonksFeedPreferences &lhs, const DBStonksFeedPreferences &rhs) {
			return lhs.serverID == rhs.serverID && lhs.userID == rhs.userID && lhs.showMine == rhs.showMine
				   && lhs.showPopular == rhs.showPopular && lhs.showPins == rhs.showPins
				   && toEpochSeconds(lhs.updatedAt) == toEpochSeconds(rhs.updatedAt);
		}

		bool operator!=(const DBStonksFeedPreferences &lhs, const DBStonksFeedPreferences &rhs) {
			return !(lhs == rhs);
		}

	} // namespace db
} // namespace server
} // namespace mumble
