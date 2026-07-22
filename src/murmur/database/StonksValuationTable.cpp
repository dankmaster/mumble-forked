// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksValuationTable.h"
#include "ChronoUtils.h"
#include "StonksSnapshotTable.h"
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

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksValuationTable::NAME;
		constexpr const char *StonksValuationTable::column::server_id;
		constexpr const char *StonksValuationTable::column::user_id;
		constexpr const char *StonksValuationTable::column::portfolio_snapshot_id;
		constexpr const char *StonksValuationTable::column::valued_at;
		constexpr const char *StonksValuationTable::column::total_value;
		constexpr const char *StonksValuationTable::column::currency;
		constexpr const char *StonksValuationTable::column::source;
		constexpr const char *StonksValuationTable::column::priced_positions;
		constexpr const char *StonksValuationTable::column::total_positions;
		constexpr const char *StonksValuationTable::column::estimated;
		constexpr unsigned int StonksValuationTable::INTRODUCED_IN_SCHEMA_VERSION;

		namespace {
			DBStonksValuation valuationFromRow(unsigned int serverID, unsigned int userID, const soci::row &row) {
				DBStonksValuation valuation(serverID, userID, static_cast< unsigned int >(row.get< int >(0)));
				valuation.valuedAt =
					std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(1)));
				valuation.totalValue       = row.get< double >(2);
				valuation.currency         = row.get< std::string >(3);
				valuation.source           = row.get< std::string >(4);
				valuation.pricedPositions  = static_cast< unsigned int >(row.get< int >(5));
				valuation.totalPositions   = static_cast< unsigned int >(row.get< int >(6));
				valuation.estimated        = row.get< int >(7) != 0;
				return valuation;
			}
		} // namespace

		StonksValuationTable::StonksValuationTable(soci::session &sql, ::mdb::Backend backend,
											   const UserTable &userTable,
											   const StonksSnapshotTable &snapshotTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column userCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column snapshotCol(column::portfolio_snapshot_id, ::mdb::DataType(::mdb::DataType::Integer));
			snapshotCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column valuedAtCol(column::valued_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			valuedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column totalValueCol(column::total_value, ::mdb::DataType(::mdb::DataType::Double));
			totalValueCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column currencyCol(column::currency, ::mdb::DataType(::mdb::DataType::VarChar, 16));
			currencyCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column sourceCol(column::source, ::mdb::DataType(::mdb::DataType::VarChar, 32));
			sourceCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column pricedCol(column::priced_positions, ::mdb::DataType(::mdb::DataType::Integer));
			pricedCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column totalCol(column::total_positions, ::mdb::DataType(::mdb::DataType::Integer));
			totalCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column estimatedCol(column::estimated, ::mdb::DataType(::mdb::DataType::Integer));
			estimatedCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, userCol, snapshotCol, valuedAtCol, totalValueCol, currencyCol, sourceCol,
						 pricedCol, totalCol, estimatedCol });
			setPrimaryKey(::mdb::PrimaryKey({ serverCol.getName(), userCol.getName(), valuedAtCol.getName() }));
			addForeignKey(::mdb::ForeignKey(userTable, { serverCol, userCol }));
			addForeignKey(::mdb::ForeignKey(snapshotTable, { serverCol, snapshotCol }));
			addIndex(::mdb::Index(std::string(NAME) + "_user_time",
									 { column::server_id, column::user_id, column::valued_at }), false);
		}

		void StonksValuationTable::setValuation(const DBStonksValuation &valuation) {
			try {
				const std::size_t valuedAt = toEpochSeconds(valuation.valuedAt);
				const int estimated        = valuation.estimated ? 1 : 0;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::user_id << "\" = :userID AND \""
					  << column::valued_at << "\" = :valuedAt",
					soci::use(valuation.serverID), soci::use(valuation.userID), soci::use(valuedAt);
				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
					  << column::user_id << "\", \"" << column::portfolio_snapshot_id << "\", \""
					  << column::valued_at << "\", \"" << column::total_value << "\", \"" << column::currency
					  << "\", \"" << column::source << "\", \"" << column::priced_positions << "\", \""
					  << column::total_positions << "\", \"" << column::estimated
					  << "\") VALUES (:serverID, :userID, :snapshotID, :valuedAt, :totalValue, :currency, :source, "
						 ":pricedPositions, :totalPositions, :estimated)",
					soci::use(valuation.serverID), soci::use(valuation.userID),
					soci::use(valuation.portfolioSnapshotID), soci::use(valuedAt), soci::use(valuation.totalValue),
					soci::use(valuation.currency), soci::use(valuation.source), soci::use(valuation.pricedPositions),
					soci::use(valuation.totalPositions), soci::use(estimated);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting a Stonks valuation for user "
														  + std::to_string(valuation.userID) + " on server "
														  + std::to_string(valuation.serverID)));
			}
		}

		std::optional< DBStonksValuation > StonksValuationTable::getLatestValuationForUser(
			unsigned int serverID, unsigned int userID) {
			const std::vector< DBStonksValuation > values = getValuationsForUser(
				serverID, userID, std::chrono::system_clock::time_point(std::chrono::seconds(0)), 1);
			return values.empty() ? std::nullopt : std::optional< DBStonksValuation >(values.back());
		}

		std::vector< DBStonksValuation > StonksValuationTable::getValuationsForUser(
			unsigned int serverID, unsigned int userID, std::chrono::system_clock::time_point earliestAt,
			unsigned int maxEntries) {
			assert(maxEntries <= static_cast< unsigned int >(std::numeric_limits< int >::max()));
			try {
				const std::size_t earliestEpoch = toEpochSeconds(earliestAt);
				std::vector< DBStonksValuation > values;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::portfolio_snapshot_id << "\", \""
								   << column::valued_at << "\", \"" << column::total_value << "\", \""
								   << column::currency << "\", \"" << column::source << "\", \""
								   << column::priced_positions << "\", \"" << column::total_positions << "\", \""
								   << column::estimated << "\" FROM \"" << NAME << "\" WHERE \""
								   << column::server_id << "\" = :serverID AND \"" << column::user_id
								   << "\" = :userID AND \"" << column::valued_at
								   << "\" >= :earliestAt ORDER BY \"" << column::valued_at << "\" DESC "
								   << ::mdb::utils::limitOffset(m_backend, ":limit", "0"),
					 soci::use(serverID), soci::use(userID), soci::use(earliestEpoch), soci::use(maxEntries),
					 soci::into(row));
				stmt.execute(false);
				while (stmt.fetch()) {
					values.push_back(valuationFromRow(serverID, userID, row));
				}
				transaction.commit();
				std::reverse(values.begin(), values.end());
				return values;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting Stonks valuations for user "
														  + std::to_string(userID) + " on server "
														  + std::to_string(serverID)));
			}
		}

		void StonksValuationTable::removeValuationsForSnapshot(unsigned int serverID,
															 unsigned int portfolioSnapshotID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::portfolio_snapshot_id << "\" = :snapshotID",
					soci::use(serverID), soci::use(portfolioSnapshotID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing Stonks valuations for snapshot "
														  + std::to_string(portfolioSnapshotID)));
			}
		}

		void StonksValuationTable::removeValuationsBefore(
			unsigned int serverID, std::chrono::system_clock::time_point earliestAt) {
			try {
				const std::size_t earliestEpoch = toEpochSeconds(earliestAt);
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::valued_at << "\" < :earliestAt",
					soci::use(serverID), soci::use(earliestEpoch);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at pruning Stonks valuations on server "
														  + std::to_string(serverID)));
			}
		}

		void StonksValuationTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);
			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}
			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
