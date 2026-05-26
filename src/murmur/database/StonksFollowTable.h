// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSFOLLOWTABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSFOLLOWTABLE_H_

#include "DBStonksFollow.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class UserTable;

		class StonksFollowTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_follows";

			struct column {
				column()                                       = delete;
				static constexpr const char *server_id         = "server_id";
				static constexpr const char *follower_user_id  = "follower_user_id";
				static constexpr const char *target_user_id    = "target_user_id";
				static constexpr const char *created_at        = "created_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 19;

			StonksFollowTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~StonksFollowTable() = default;

			void setFollow(const DBStonksFollow &follow);
			void removeFollow(unsigned int serverID, unsigned int followerUserID, unsigned int targetUserID);
			std::vector< unsigned int > getFollowedUsers(unsigned int serverID, unsigned int followerUserID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSFOLLOWTABLE_H_
