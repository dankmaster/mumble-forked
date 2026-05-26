// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksSnapshotTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/Index.h"
#include "database/TransactionHolder.h"
#include "database/Utils.h"

#include <soci/soci.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <set>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksSnapshotTable::NAME;
		constexpr const char *StonksSnapshotTable::column::server_id;
		constexpr const char *StonksSnapshotTable::column::snapshot_id;
		constexpr const char *StonksSnapshotTable::column::user_id;
		constexpr const char *StonksSnapshotTable::column::created_at;
		constexpr const char *StonksSnapshotTable::column::currency;
		constexpr const char *StonksSnapshotTable::column::total_value;
		constexpr const char *StonksSnapshotTable::column::note;
		constexpr unsigned int StonksSnapshotTable::INTRODUCED_IN_SCHEMA_VERSION;

		namespace {
			DBStonksSnapshot snapshotFromRow(unsigned int serverID, const soci::row &row) {
				DBStonksSnapshot snapshot(serverID, static_cast< unsigned int >(row.get< int >(0)),
										  static_cast< unsigned int >(row.get< int >(1)));
				snapshot.createdAt =
					std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(2)));
				snapshot.currency   = row.get< std::string >(3);
				snapshot.totalValue = row.get< double >(4);
				snapshot.note       = row.get< std::string >(5);
				return snapshot;
			}
		} // namespace

		StonksSnapshotTable::StonksSnapshotTable(soci::session &sql, ::mdb::Backend backend,
												 const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column snapshotCol(column::snapshot_id, ::mdb::DataType(::mdb::DataType::Integer));
			snapshotCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column currencyCol(column::currency, ::mdb::DataType(::mdb::DataType::VarChar, 16));
			currencyCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column totalValueCol(column::total_value, ::mdb::DataType(::mdb::DataType::Double));
			totalValueCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column noteCol(column::note, ::mdb::DataType(::mdb::DataType::VarChar, 512));
			noteCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, snapshotCol, userCol, createdAtCol, currencyCol, totalValueCol, noteCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), snapshotCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey userFK(userTable, { serverCol, userCol });
			addForeignKey(userFK);

			::mdb::Index userTimeIndex(std::string(NAME) + "_user_time",
									   { column::server_id, column::user_id, column::created_at, column::snapshot_id });
			addIndex(userTimeIndex, false);
		}

		unsigned int StonksSnapshotTable::getFreeSnapshotID(unsigned int serverID) {
			try {
				int snapshotID = 0;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT COALESCE(MAX(\"" << column::snapshot_id << "\"), 0) + 1 FROM \"" << NAME
					  << "\" WHERE \"" << column::server_id << "\" = :serverID",
					soci::use(serverID), soci::into(snapshotID);
				transaction.commit();
				return static_cast< unsigned int >(std::max(1, snapshotID));
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting free stonks snapshot ID on server "
															  + std::to_string(serverID)));
			}
		}

		void StonksSnapshotTable::addSnapshot(const DBStonksSnapshot &snapshot) {
			try {
				auto createdAt = snapshot.createdAt;
				if (createdAt == std::chrono::system_clock::time_point()) {
					createdAt = std::chrono::system_clock::now();
				}
				const std::size_t createdAtEpoch = toEpochSeconds(createdAt);

				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
					  << column::snapshot_id << "\", \"" << column::user_id << "\", \"" << column::created_at
					  << "\", \"" << column::currency << "\", \"" << column::total_value << "\", \"" << column::note
					  << "\") VALUES (:serverID, :snapshotID, :userID, :createdAt, :currency, :totalValue, :note)",
					soci::use(snapshot.serverID), soci::use(snapshot.snapshotID), soci::use(snapshot.userID),
					soci::use(createdAtEpoch), soci::use(snapshot.currency), soci::use(snapshot.totalValue),
					soci::use(snapshot.note);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding stonks snapshot "
															  + std::to_string(snapshot.snapshotID)
															  + " on server " + std::to_string(snapshot.serverID)));
			}
		}

		std::optional< DBStonksSnapshot > StonksSnapshotTable::getSnapshot(unsigned int serverID,
																		   unsigned int snapshotID) {
			try {
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT \"" << column::snapshot_id << "\", \"" << column::user_id << "\", \""
					  << column::created_at << "\", \"" << column::currency << "\", \"" << column::total_value
					  << "\", \"" << column::note << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::snapshot_id << "\" = :snapshotID",
					soci::use(serverID), soci::use(snapshotID), soci::into(row);
				transaction.commit();
				if (!m_sql.got_data()) {
					return std::nullopt;
				}
				return snapshotFromRow(serverID, row);
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks snapshot "
															  + std::to_string(snapshotID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksSnapshot > StonksSnapshotTable::getSnapshotsForUser(unsigned int serverID,
																				 unsigned int userID,
																				 unsigned int maxEntries) {
			assert(maxEntries <= static_cast< unsigned int >(std::numeric_limits< int >::max()));
			try {
				std::vector< DBStonksSnapshot > snapshots;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::snapshot_id << "\", \"" << column::user_id << "\", \""
								   << column::created_at << "\", \"" << column::currency << "\", \""
								   << column::total_value << "\", \"" << column::note << "\" FROM \"" << NAME
								   << "\" WHERE \"" << column::server_id << "\" = :serverID AND \"" << column::user_id
								   << "\" = :userID ORDER BY \"" << column::created_at << "\" DESC, \""
								   << column::snapshot_id << "\" DESC " << ::mdb::utils::limitOffset(m_backend, ":limit", "0"),
					 soci::use(serverID), soci::use(userID), soci::use(maxEntries), soci::into(row));
				stmt.execute(false);
				while (stmt.fetch()) {
					snapshots.push_back(snapshotFromRow(serverID, row));
				}
				transaction.commit();
				return snapshots;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks snapshots for user "
															  + std::to_string(userID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		std::optional< DBStonksSnapshot > StonksSnapshotTable::getLatestSnapshotForUser(unsigned int serverID,
																					   unsigned int userID) {
			std::vector< DBStonksSnapshot > snapshots = getSnapshotsForUser(serverID, userID, 1);
			return snapshots.empty() ? std::nullopt : std::optional< DBStonksSnapshot >(snapshots.front());
		}

		std::optional< DBStonksSnapshot > StonksSnapshotTable::getSnapshotAtOrBefore(
			unsigned int serverID, unsigned int userID, std::chrono::system_clock::time_point latestAt) {
			try {
				const std::size_t latestEpoch = toEpochSeconds(latestAt);
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT \"" << column::snapshot_id << "\", \"" << column::user_id << "\", \""
					  << column::created_at << "\", \"" << column::currency << "\", \"" << column::total_value
					  << "\", \"" << column::note << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::user_id << "\" = :userID AND \"" << column::created_at
					  << "\" <= :latestAt ORDER BY \"" << column::created_at << "\" DESC, \"" << column::snapshot_id
					  << "\" DESC " << ::mdb::utils::limitOffset(m_backend, "1", "0"),
					soci::use(serverID), soci::use(userID), soci::use(latestEpoch), soci::into(row);
				transaction.commit();
				if (!m_sql.got_data()) {
					return std::nullopt;
				}
				return snapshotFromRow(serverID, row);
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting historical stonks snapshot for user "
															  + std::to_string(userID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksSnapshot > StonksSnapshotTable::getLatestSnapshotsByUser(unsigned int serverID) {
			try {
				std::vector< DBStonksSnapshot > snapshots;
				std::set< unsigned int > seenUsers;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::snapshot_id << "\", \"" << column::user_id << "\", \""
								   << column::created_at << "\", \"" << column::currency << "\", \""
								   << column::total_value << "\", \"" << column::note << "\" FROM \"" << NAME
								   << "\" WHERE \"" << column::server_id << "\" = :serverID ORDER BY \""
								   << column::user_id << "\" ASC, \"" << column::created_at << "\" DESC, \""
								   << column::snapshot_id << "\" DESC",
					 soci::use(serverID), soci::into(row));
				stmt.execute(false);
				while (stmt.fetch()) {
					DBStonksSnapshot snapshot = snapshotFromRow(serverID, row);
					if (seenUsers.insert(snapshot.userID).second) {
						snapshots.push_back(std::move(snapshot));
					}
				}
				transaction.commit();
				return snapshots;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting latest stonks snapshots on server "
															  + std::to_string(serverID)));
			}
		}

		void StonksSnapshotTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
