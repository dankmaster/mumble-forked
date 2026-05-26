// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTPOSITIONTABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTPOSITIONTABLE_H_

#include "DBStonksSnapshotPosition.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class StonksSnapshotTable;

		class StonksSnapshotPositionTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_snapshot_positions";

			struct column {
				column()                                     = delete;
				static constexpr const char *server_id       = "server_id";
				static constexpr const char *snapshot_id     = "snapshot_id";
				static constexpr const char *display_order   = "display_order";
				static constexpr const char *symbol          = "symbol";
				static constexpr const char *quantity        = "quantity";
				static constexpr const char *price           = "price";
				static constexpr const char *market_value    = "market_value";
				static constexpr const char *currency        = "currency";
				static constexpr const char *display_name    = "display_name";
				static constexpr const char *provider_id     = "provider_id";
				static constexpr const char *provider_symbol = "provider_symbol";
				static constexpr const char *exchange        = "exchange";
				static constexpr const char *quote_time      = "quote_time";
				static constexpr const char *quote_source_url = "quote_source_url";
				static constexpr const char *quote_confidence = "quote_confidence";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 20;

			StonksSnapshotPositionTable(soci::session &sql, ::mumble::db::Backend backend,
										const StonksSnapshotTable &snapshotTable);
			~StonksSnapshotPositionTable() = default;

			void setPositions(unsigned int serverID, unsigned int snapshotID,
							  const std::vector< DBStonksSnapshotPosition > &positions);
			std::vector< DBStonksSnapshotPosition > getPositions(unsigned int serverID, unsigned int snapshotID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSSNAPSHOTPOSITIONTABLE_H_
