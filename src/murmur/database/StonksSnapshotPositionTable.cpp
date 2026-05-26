// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksSnapshotPositionTable.h"
#include "StonksSnapshotTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/Database.h"
#include "database/ForeignKey.h"
#include "database/Index.h"
#include "database/MigrationException.h"
#include "database/TransactionHolder.h"

#include <soci/soci.h>

#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksSnapshotPositionTable::NAME;
		constexpr const char *StonksSnapshotPositionTable::column::server_id;
		constexpr const char *StonksSnapshotPositionTable::column::snapshot_id;
		constexpr const char *StonksSnapshotPositionTable::column::display_order;
		constexpr const char *StonksSnapshotPositionTable::column::symbol;
		constexpr const char *StonksSnapshotPositionTable::column::quantity;
		constexpr const char *StonksSnapshotPositionTable::column::price;
		constexpr const char *StonksSnapshotPositionTable::column::market_value;
		constexpr const char *StonksSnapshotPositionTable::column::currency;
		constexpr const char *StonksSnapshotPositionTable::column::display_name;
		constexpr const char *StonksSnapshotPositionTable::column::provider_id;
		constexpr const char *StonksSnapshotPositionTable::column::provider_symbol;
		constexpr const char *StonksSnapshotPositionTable::column::exchange;
		constexpr const char *StonksSnapshotPositionTable::column::quote_time;
		constexpr const char *StonksSnapshotPositionTable::column::quote_source_url;
		constexpr const char *StonksSnapshotPositionTable::column::quote_confidence;
		constexpr unsigned int StonksSnapshotPositionTable::INTRODUCED_IN_SCHEMA_VERSION;

		StonksSnapshotPositionTable::StonksSnapshotPositionTable(soci::session &sql, ::mdb::Backend backend,
																 const StonksSnapshotTable &snapshotTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column snapshotCol(column::snapshot_id, ::mdb::DataType(::mdb::DataType::Integer));
			snapshotCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column orderCol(column::display_order, ::mdb::DataType(::mdb::DataType::Integer));
			orderCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column symbolCol(column::symbol, ::mdb::DataType(::mdb::DataType::VarChar, 32));
			symbolCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column quantityCol(column::quantity, ::mdb::DataType(::mdb::DataType::Double));
			quantityCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column priceCol(column::price, ::mdb::DataType(::mdb::DataType::Double));
			priceCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column marketValueCol(column::market_value, ::mdb::DataType(::mdb::DataType::Double));
			marketValueCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column currencyCol(column::currency, ::mdb::DataType(::mdb::DataType::VarChar, 16));
			currencyCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column displayNameCol(column::display_name, ::mdb::DataType(::mdb::DataType::VarChar, 256));
			displayNameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column providerIDCol(column::provider_id, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			providerIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column providerSymbolCol(column::provider_symbol, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			providerSymbolCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column exchangeCol(column::exchange, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			exchangeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column quoteTimeCol(column::quote_time, ::mdb::DataType(::mdb::DataType::Integer));
			quoteTimeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column quoteSourceURLCol(column::quote_source_url, ::mdb::DataType(::mdb::DataType::VarChar, 2048));
			quoteSourceURLCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column quoteConfidenceCol(column::quote_confidence, ::mdb::DataType(::mdb::DataType::Double));
			quoteConfidenceCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, snapshotCol, orderCol, symbolCol, quantityCol, priceCol, marketValueCol,
						 currencyCol, displayNameCol, providerIDCol, providerSymbolCol, exchangeCol, quoteTimeCol,
						 quoteSourceURLCol, quoteConfidenceCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), snapshotCol.getName(), orderCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey snapshotFK(snapshotTable, { serverCol, snapshotCol });
			addForeignKey(snapshotFK);

			::mdb::Index symbolIndex(std::string(NAME) + "_symbol",
									 { column::server_id, column::symbol, column::snapshot_id });
			addIndex(symbolIndex, false);
		}

		void StonksSnapshotPositionTable::setPositions(
			unsigned int serverID, unsigned int snapshotID,
			const std::vector< DBStonksSnapshotPosition > &positions) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::snapshot_id << "\" = :snapshotID",
					soci::use(serverID), soci::use(snapshotID);

				for (const DBStonksSnapshotPosition &position : positions) {
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
						  << column::snapshot_id << "\", \"" << column::display_order << "\", \"" << column::symbol
						  << "\", \"" << column::quantity << "\", \"" << column::price << "\", \""
						  << column::market_value << "\", \"" << column::currency << "\", \"" << column::display_name
						  << "\", \"" << column::provider_id << "\", \"" << column::provider_symbol << "\", \""
						  << column::exchange << "\", \"" << column::quote_time << "\", \"" << column::quote_source_url
						  << "\", \"" << column::quote_confidence
						  << "\") VALUES (:serverID, :snapshotID, :displayOrder, :symbol, :quantity, :price, "
							 ":marketValue, :currency, :displayName, :providerID, :providerSymbol, :exchange, "
							 ":quoteTime, :quoteSourceURL, :quoteConfidence)",
						soci::use(serverID), soci::use(snapshotID), soci::use(position.displayOrder),
						soci::use(position.symbol), soci::use(position.quantity), soci::use(position.price),
						soci::use(position.marketValue), soci::use(position.currency),
						soci::use(position.displayName), soci::use(position.providerID),
						soci::use(position.providerSymbol), soci::use(position.exchange),
						soci::use(position.quoteTime), soci::use(position.quoteSourceURL),
						soci::use(position.quoteConfidence);
				}
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting stonks positions for snapshot "
															  + std::to_string(snapshotID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		void StonksSnapshotPositionTable::removePositions(unsigned int serverID, unsigned int snapshotID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::snapshot_id << "\" = :snapshotID",
					soci::use(serverID), soci::use(snapshotID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing stonks positions for snapshot "
															  + std::to_string(snapshotID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksSnapshotPosition > StonksSnapshotPositionTable::getPositions(unsigned int serverID,
																						 unsigned int snapshotID) {
			try {
				std::vector< DBStonksSnapshotPosition > positions;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::display_order << "\", \"" << column::symbol << "\", \""
								   << column::quantity << "\", \"" << column::price << "\", \""
								   << column::market_value << "\", \"" << column::currency << "\", \""
								   << column::display_name << "\", \"" << column::provider_id << "\", \""
								   << column::provider_symbol << "\", \"" << column::exchange << "\", \""
								   << column::quote_time << "\", \"" << column::quote_source_url << "\", \""
								   << column::quote_confidence << "\" FROM \"" << NAME << "\" WHERE \""
								   << column::server_id << "\" = :serverID AND \"" << column::snapshot_id
								   << "\" = :snapshotID ORDER BY \"" << column::display_order << "\" ASC",
					 soci::use(serverID), soci::use(snapshotID), soci::into(row));
				stmt.execute(false);
				while (stmt.fetch()) {
					DBStonksSnapshotPosition position(serverID, snapshotID,
													  static_cast< unsigned int >(row.get< int >(0)),
													  row.get< std::string >(1));
					position.quantity    = row.get< double >(2);
					position.price       = row.get< double >(3);
					position.marketValue = row.get< double >(4);
					position.currency    = row.get< std::string >(5);
					position.displayName = row.get< std::string >(6);
					position.providerID     = row.get< std::string >(7);
					position.providerSymbol = row.get< std::string >(8);
					position.exchange       = row.get< std::string >(9);
					position.quoteTime      = row.get< long long >(10);
					position.quoteSourceURL = row.get< std::string >(11);
					position.quoteConfidence = row.get< double >(12);
					positions.push_back(std::move(position));
				}
				transaction.commit();
				return positions;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks positions for snapshot "
															  + std::to_string(snapshotID) + " on server "
															  + std::to_string(serverID)));
			}
		}

		void StonksSnapshotPositionTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			try {
				if (fromSchemaVersion < 21) {
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
						  << column::snapshot_id << "\", \"" << column::display_order << "\", \"" << column::symbol
						  << "\", \"" << column::quantity << "\", \"" << column::price << "\", \""
						  << column::market_value << "\", \"" << column::currency << "\", \"" << column::display_name
						  << "\", \"" << column::provider_id << "\", \"" << column::provider_symbol << "\", \""
						  << column::exchange << "\", \"" << column::quote_time << "\", \"" << column::quote_source_url
						  << "\", \"" << column::quote_confidence << "\") SELECT old.\"" << column::server_id
						  << "\", old.\"" << column::snapshot_id << "\", old.\"" << column::display_order
						  << "\", old.\"" << column::symbol << "\", old.\"" << column::quantity << "\", old.\""
						  << column::price << "\", old.\"" << column::market_value << "\", old.\"" << column::currency
						  << "\", old.\"" << column::display_name
						  << "\", 'manual', old.\"" << column::symbol << "\", '', 0, '', 0.35 FROM \"" << NAME
						  << ::mdb::Database::OLD_TABLE_SUFFIX << "\" AS old";
				} else {
					::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
				}
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::MigrationException(
					std::string("Failed at migrating table \"") + NAME + "\" from schema version "
					+ std::to_string(fromSchemaVersion) + " to " + std::to_string(toSchemaVersion)));
			}
		}

	} // namespace db
} // namespace server
} // namespace mumble
