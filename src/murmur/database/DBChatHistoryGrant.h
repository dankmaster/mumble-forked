// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATHISTORYGRANT_H_
#define MUMBLE_SERVER_DATABASE_DBCHATHISTORYGRANT_H_

#include "DBChatThread.h"

#include <chrono>
#include <optional>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatHistoryGrant {
			unsigned int serverID = {};
			unsigned int userID   = {};
			ChatThreadScope scope = ChatThreadScope::Channel;
			unsigned int scopeID  = {};
			std::chrono::system_clock::time_point visibleAfter = {};
			std::chrono::system_clock::time_point grantedAt    = {};
			std::optional< unsigned int > grantedByUserID      = {};

			DBChatHistoryGrant() = default;
			DBChatHistoryGrant(unsigned int serverID, unsigned int userID, ChatThreadScope scope, unsigned int scopeID);

			friend bool operator==(const DBChatHistoryGrant &lhs, const DBChatHistoryGrant &rhs);
			friend bool operator!=(const DBChatHistoryGrant &lhs, const DBChatHistoryGrant &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATHISTORYGRANT_H_
