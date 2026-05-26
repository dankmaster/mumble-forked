// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTTABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTTABLE_H_

#include "DBStonksSnapshot.h"

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

		class StonksSnapshotTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_snapshots";

			struct column {
				column()                                    = delete;
				static constexpr const char *server_id      = "server_id";
				static constexpr const char *snapshot_id    = "snapshot_id";
				static constexpr const char *user_id        = "user_id";
				static constexpr const char *created_at     = "created_at";
				static constexpr const char *currency       = "currency";
				static constexpr const char *total_value    = "total_value";
				static constexpr const char *note           = "note";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 20;

			StonksSnapshotTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~StonksSnapshotTable() = default;

			unsigned int getFreeSnapshotID(unsigned int serverID);
			void addSnapshot(const DBStonksSnapshot &snapshot);
			std::optional< DBStonksSnapshot > getSnapshot(unsigned int serverID, unsigned int snapshotID);
			std::vector< DBStonksSnapshot > getSnapshotsForUser(unsigned int serverID, unsigned int userID,
																unsigned int maxEntries);
			std::optional< DBStonksSnapshot > getLatestSnapshotForUser(unsigned int serverID, unsigned int userID);
			std::optional< DBStonksSnapshot > getSnapshotAtOrBefore(unsigned int serverID, unsigned int userID,
																	std::chrono::system_clock::time_point latestAt);
			std::vector< DBStonksSnapshot > getLatestSnapshotsByUser(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTTABLE_H_
