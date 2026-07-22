// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSVALUATIONTABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSVALUATIONTABLE_H_

#include "DBStonksValuation.h"

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

		class StonksSnapshotTable;
		class UserTable;

		class StonksValuationTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_valuations";

			struct column {
				column()                                       = delete;
				static constexpr const char *server_id         = "server_id";
				static constexpr const char *user_id           = "user_id";
				static constexpr const char *portfolio_snapshot_id = "portfolio_snapshot_id";
				static constexpr const char *valued_at         = "valued_at";
				static constexpr const char *total_value       = "total_value";
				static constexpr const char *currency          = "currency";
				static constexpr const char *source            = "source";
				static constexpr const char *priced_positions  = "priced_positions";
				static constexpr const char *total_positions   = "total_positions";
				static constexpr const char *estimated         = "estimated";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 23;

			StonksValuationTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable,
								 const StonksSnapshotTable &snapshotTable);
			~StonksValuationTable() = default;

			void setValuation(const DBStonksValuation &valuation);
			std::optional< DBStonksValuation > getLatestValuationForUser(unsigned int serverID,
																 unsigned int userID);
			std::vector< DBStonksValuation > getValuationsForUser(
				unsigned int serverID, unsigned int userID, std::chrono::system_clock::time_point earliestAt,
				unsigned int maxEntries);
			void removeValuationsForSnapshot(unsigned int serverID, unsigned int portfolioSnapshotID);
			void removeValuationsBefore(unsigned int serverID, std::chrono::system_clock::time_point earliestAt);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSVALUATIONTABLE_H_
