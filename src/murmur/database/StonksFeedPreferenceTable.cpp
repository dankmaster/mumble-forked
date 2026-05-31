// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksFeedPreferenceTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/TransactionHolder.h"

#include <soci/soci.h>

#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksFeedPreferenceTable::NAME;
		constexpr const char *StonksFeedPreferenceTable::column::server_id;
		constexpr const char *StonksFeedPreferenceTable::column::user_id;
		constexpr const char *StonksFeedPreferenceTable::column::show_mine;
		constexpr const char *StonksFeedPreferenceTable::column::show_popular;
		constexpr const char *StonksFeedPreferenceTable::column::show_pins;
		constexpr const char *StonksFeedPreferenceTable::column::updated_at;
		constexpr unsigned int StonksFeedPreferenceTable::INTRODUCED_IN_SCHEMA_VERSION;

		StonksFeedPreferenceTable::StonksFeedPreferenceTable(soci::session &sql, ::mdb::Backend backend,
															 const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column showMineCol(column::show_mine, ::mdb::DataType(::mdb::DataType::Integer));
			showMineCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column showPopularCol(column::show_popular, ::mdb::DataType(::mdb::DataType::Integer));
			showPopularCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column showPinsCol(column::show_pins, ::mdb::DataType(::mdb::DataType::Integer));
			showPinsCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column updatedAtCol(column::updated_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			updatedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, userCol, showMineCol, showPopularCol, showPinsCol, updatedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), userCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey userFK(userTable, { serverCol, userCol });
			addForeignKey(userFK);
		}

		void StonksFeedPreferenceTable::setPreferences(const DBStonksFeedPreferences &preferences) {
			try {
				auto updatedAt = preferences.updatedAt;
				if (updatedAt == std::chrono::system_clock::time_point()) {
					updatedAt = std::chrono::system_clock::now();
				}

				const int showMine        = preferences.showMine ? 1 : 0;
				const int showPopular     = preferences.showPopular ? 1 : 0;
				const int showPins        = preferences.showPins ? 1 : 0;
				const std::size_t updated = toEpochSeconds(updatedAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID",
					soci::use(preferences.serverID), soci::use(preferences.userID);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::user_id
					  << "\", \"" << column::show_mine << "\", \"" << column::show_popular << "\", \""
					  << column::show_pins << "\", \"" << column::updated_at
					  << "\") VALUES (:serverID, :userID, :showMine, :showPopular, :showPins, :updatedAt)",
					soci::use(preferences.serverID), soci::use(preferences.userID), soci::use(showMine),
					soci::use(showPopular), soci::use(showPins), soci::use(updated);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting stonks feed preferences for user with ID "
															  + std::to_string(preferences.userID)
															  + " on server with ID "
															  + std::to_string(preferences.serverID)));
			}
		}

		std::optional< DBStonksFeedPreferences > StonksFeedPreferenceTable::getPreferences(unsigned int serverID,
																						 unsigned int userID) {
			try {
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::show_mine << "\", \"" << column::show_popular << "\", \""
								   << column::show_pins << "\", \"" << column::updated_at << "\" FROM \"" << NAME
								   << "\" WHERE \"" << column::server_id << "\" = :serverID AND \"" << column::user_id
								   << "\" = :userID",
					 soci::use(serverID), soci::use(userID), soci::into(row));

				stmt.execute(false);
				if (!stmt.fetch()) {
					transaction.commit();
					return std::nullopt;
				}

				DBStonksFeedPreferences preferences(serverID, userID);
				preferences.showMine    = row.get< int >(0) != 0;
				preferences.showPopular = row.get< int >(1) != 0;
				preferences.showPins    = row.get< int >(2) != 0;
				preferences.updatedAt =
					std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(3)));
				transaction.commit();
				return preferences;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks feed preferences for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void StonksFeedPreferenceTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
