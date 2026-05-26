// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksFollow.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksFollow::DBStonksFollow(unsigned int serverID, unsigned int followerUserID, unsigned int targetUserID)
			: serverID(serverID), followerUserID(followerUserID), targetUserID(targetUserID) {}

		bool operator==(const DBStonksFollow &lhs, const DBStonksFollow &rhs) {
			return lhs.serverID == rhs.serverID && lhs.followerUserID == rhs.followerUserID
				   && lhs.targetUserID == rhs.targetUserID && toEpochSeconds(lhs.createdAt) == toEpochSeconds(rhs.createdAt);
		}

		bool operator!=(const DBStonksFollow &lhs, const DBStonksFollow &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
