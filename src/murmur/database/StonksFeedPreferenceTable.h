// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSFEEDPREFERENCETABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSFEEDPREFERENCETABLE_H_

#include "DBStonksFeedPreferences.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <optional>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class UserTable;

		class StonksFeedPreferenceTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_feed_preferences";

			struct column {
				column()                                  = delete;
				static constexpr const char *server_id    = "server_id";
				static constexpr const char *user_id      = "user_id";
				static constexpr const char *show_mine    = "show_mine";
				static constexpr const char *show_popular = "show_popular";
				static constexpr const char *show_pins    = "show_pins";
				static constexpr const char *updated_at   = "updated_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 22;

			StonksFeedPreferenceTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~StonksFeedPreferenceTable() = default;

			void setPreferences(const DBStonksFeedPreferences &preferences);
			std::optional< DBStonksFeedPreferences > getPreferences(unsigned int serverID, unsigned int userID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSFEEDPREFERENCETABLE_H_
