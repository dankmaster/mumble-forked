// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBChatHistoryGrant.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBChatHistoryGrant::DBChatHistoryGrant(unsigned int serverID, unsigned int userID, ChatThreadScope scope,
											   unsigned int scopeID)
			: serverID(serverID), userID(userID), scope(scope), scopeID(scopeID) {}

		bool operator==(const DBChatHistoryGrant &lhs, const DBChatHistoryGrant &rhs) {
			return lhs.serverID == rhs.serverID && lhs.userID == rhs.userID && lhs.scope == rhs.scope
				   && lhs.scopeID == rhs.scopeID && toEpochSeconds(lhs.visibleAfter) == toEpochSeconds(rhs.visibleAfter)
				   && toEpochSeconds(lhs.grantedAt) == toEpochSeconds(rhs.grantedAt)
				   && lhs.grantedByUserID == rhs.grantedByUserID;
		}

		bool operator!=(const DBChatHistoryGrant &lhs, const DBChatHistoryGrant &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
