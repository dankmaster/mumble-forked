// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSSCORETABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSSCORETABLE_H_

#include "DBStonksScore.h"

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

		class StonksScoreTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_scores";

			struct column {
				column()                                    = delete;
				static constexpr const char *server_id      = "server_id";
				static constexpr const char *user_id        = "user_id";
				static constexpr const char *period         = "period";
				static constexpr const char *score_percent  = "score_percent";
				static constexpr const char *updated_at     = "updated_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 19;

			StonksScoreTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~StonksScoreTable() = default;

			void setScore(const DBStonksScore &score);
			std::optional< DBStonksScore > getScore(unsigned int serverID, unsigned int userID,
													const std::string &period);
			std::vector< DBStonksScore > getScores(unsigned int serverID, unsigned int userID);
			std::vector< DBStonksScore > getLeaderboard(unsigned int serverID, const std::string &period,
														 unsigned int maxEntries);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSSCORETABLE_H_
