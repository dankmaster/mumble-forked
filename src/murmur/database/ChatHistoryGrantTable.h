// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATHISTORYGRANTTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATHISTORYGRANTTABLE_H_

#include "DBChatHistoryGrant.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <optional>
#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class UserTable;

		class ChatHistoryGrantTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_history_grants";

			struct column {
				column()                                      = delete;
				static constexpr const char *server_id        = "server_id";
				static constexpr const char *user_id          = "user_id";
				static constexpr const char *scope            = "scope";
				static constexpr const char *scope_id         = "scope_id";
				static constexpr const char *visible_after    = "visible_after";
				static constexpr const char *granted_at       = "granted_at";
				static constexpr const char *granted_by_user_id = "granted_by_user_id";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 18;

			ChatHistoryGrantTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~ChatHistoryGrantTable() = default;

			void setGrant(const DBChatHistoryGrant &grant);
			void removeGrant(unsigned int serverID, unsigned int userID, ChatThreadScope scope, unsigned int scopeID);
			std::optional< DBChatHistoryGrant > getGrant(unsigned int serverID, unsigned int userID,
														 ChatThreadScope scope, unsigned int scopeID);
			std::vector< DBChatHistoryGrant > getGrants(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATHISTORYGRANTTABLE_H_
