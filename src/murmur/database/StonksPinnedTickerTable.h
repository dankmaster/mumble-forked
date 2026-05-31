// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_STONKSPINNEDTICKERTABLE_H_
#define MUMBLE_SERVER_DATABASE_STONKSPINNEDTICKERTABLE_H_

#include "DBStonksPinnedTicker.h"

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

		class StonksPinnedTickerTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "stonks_pinned_tickers";

			struct column {
				column()                                      = delete;
				static constexpr const char *server_id        = "server_id";
				static constexpr const char *user_id          = "user_id";
				static constexpr const char *symbol           = "symbol";
				static constexpr const char *display_name     = "display_name";
				static constexpr const char *provider_id      = "provider_id";
				static constexpr const char *provider_symbol  = "provider_symbol";
				static constexpr const char *exchange         = "exchange";
				static constexpr const char *quote_source_url = "quote_source_url";
				static constexpr const char *display_order    = "display_order";
				static constexpr const char *created_at       = "created_at";
				static constexpr const char *updated_at       = "updated_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 22;

			StonksPinnedTickerTable(soci::session &sql, ::mumble::db::Backend backend, const UserTable &userTable);
			~StonksPinnedTickerTable() = default;

			void setPinnedTicker(const DBStonksPinnedTicker &ticker);
			void removePinnedTicker(unsigned int serverID, unsigned int userID, const std::string &symbol);
			std::vector< DBStonksPinnedTicker > getPinnedTickers(unsigned int serverID, unsigned int userID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_STONKSPINNEDTICKERTABLE_H_
