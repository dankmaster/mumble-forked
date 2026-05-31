// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksPinnedTickerTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/Index.h"
#include "database/TransactionHolder.h"

#include <soci/soci.h>

#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksPinnedTickerTable::NAME;
		constexpr const char *StonksPinnedTickerTable::column::server_id;
		constexpr const char *StonksPinnedTickerTable::column::user_id;
		constexpr const char *StonksPinnedTickerTable::column::symbol;
		constexpr const char *StonksPinnedTickerTable::column::display_name;
		constexpr const char *StonksPinnedTickerTable::column::provider_id;
		constexpr const char *StonksPinnedTickerTable::column::provider_symbol;
		constexpr const char *StonksPinnedTickerTable::column::exchange;
		constexpr const char *StonksPinnedTickerTable::column::quote_source_url;
		constexpr const char *StonksPinnedTickerTable::column::display_order;
		constexpr const char *StonksPinnedTickerTable::column::created_at;
		constexpr const char *StonksPinnedTickerTable::column::updated_at;
		constexpr unsigned int StonksPinnedTickerTable::INTRODUCED_IN_SCHEMA_VERSION;

		StonksPinnedTickerTable::StonksPinnedTickerTable(soci::session &sql, ::mdb::Backend backend,
														 const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column symbolCol(column::symbol, ::mdb::DataType(::mdb::DataType::VarChar, 32));
			symbolCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column displayNameCol(column::display_name, ::mdb::DataType(::mdb::DataType::VarChar, 128));
			displayNameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column providerIDCol(column::provider_id, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			providerIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column providerSymbolCol(column::provider_symbol, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			providerSymbolCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column exchangeCol(column::exchange, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			exchangeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column quoteSourceURLCol(column::quote_source_url, ::mdb::DataType(::mdb::DataType::Text));
			quoteSourceURLCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column displayOrderCol(column::display_order, ::mdb::DataType(::mdb::DataType::Integer));
			displayOrderCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column updatedAtCol(column::updated_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			updatedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, userCol, symbolCol, displayNameCol, providerIDCol, providerSymbolCol,
						 exchangeCol, quoteSourceURLCol, displayOrderCol, createdAtCol, updatedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), userCol.getName(), symbolCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey userFK(userTable, { serverCol, userCol });
			addForeignKey(userFK);

			::mdb::Index orderIndex(std::string(NAME) + "_user_order",
									{ column::server_id, column::user_id, column::display_order, column::symbol });
			addIndex(orderIndex, false);
		}

		void StonksPinnedTickerTable::setPinnedTicker(const DBStonksPinnedTicker &ticker) {
			try {
				auto createdAt = ticker.createdAt;
				auto updatedAt = ticker.updatedAt;
				if (createdAt == std::chrono::system_clock::time_point()) {
					createdAt = std::chrono::system_clock::now();
				}
				if (updatedAt == std::chrono::system_clock::time_point()) {
					updatedAt = std::chrono::system_clock::now();
				}

				const std::size_t createdAtEpoch = toEpochSeconds(createdAt);
				const std::size_t updatedAtEpoch = toEpochSeconds(updatedAt);
				const int displayOrder           = static_cast< int >(ticker.displayOrder);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID AND \"" << column::symbol << "\" = :symbol",
					soci::use(ticker.serverID), soci::use(ticker.userID), soci::use(ticker.symbol);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::user_id
					  << "\", \"" << column::symbol << "\", \"" << column::display_name << "\", \""
					  << column::provider_id << "\", \"" << column::provider_symbol << "\", \"" << column::exchange
					  << "\", \"" << column::quote_source_url << "\", \"" << column::display_order << "\", \""
					  << column::created_at << "\", \"" << column::updated_at
					  << "\") VALUES (:serverID, :userID, :symbol, :displayName, :providerID, :providerSymbol, "
						 ":exchange, :quoteSourceURL, :displayOrder, :createdAt, :updatedAt)",
					soci::use(ticker.serverID), soci::use(ticker.userID), soci::use(ticker.symbol),
					soci::use(ticker.displayName), soci::use(ticker.providerID), soci::use(ticker.providerSymbol),
					soci::use(ticker.exchange), soci::use(ticker.quoteSourceURL), soci::use(displayOrder),
					soci::use(createdAtEpoch), soci::use(updatedAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting pinned stonks ticker \""
															  + ticker.symbol + "\" for user with ID "
															  + std::to_string(ticker.userID)
															  + " on server with ID "
															  + std::to_string(ticker.serverID)));
			}
		}

		void StonksPinnedTickerTable::removePinnedTicker(unsigned int serverID, unsigned int userID,
														 const std::string &symbol) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID AND \"" << column::symbol << "\" = :symbol",
					soci::use(serverID), soci::use(userID), soci::use(symbol);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing pinned stonks ticker \"" + symbol
															  + "\" for user with ID " + std::to_string(userID)
															  + " on server with ID " + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksPinnedTicker > StonksPinnedTickerTable::getPinnedTickers(unsigned int serverID,
																					 unsigned int userID) {
			try {
				std::vector< DBStonksPinnedTicker > tickers;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::symbol << "\", \"" << column::display_name << "\", \""
								   << column::provider_id << "\", \"" << column::provider_symbol << "\", \""
								   << column::exchange << "\", \"" << column::quote_source_url << "\", \""
								   << column::display_order << "\", \"" << column::created_at << "\", \""
								   << column::updated_at << "\" FROM \"" << NAME << "\" WHERE \""
								   << column::server_id << "\" = :serverID AND \"" << column::user_id
								   << "\" = :userID ORDER BY \"" << column::display_order << "\" ASC, \""
								   << column::symbol << "\" ASC",
					 soci::use(serverID), soci::use(userID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBStonksPinnedTicker ticker(serverID, userID, row.get< std::string >(0));
					ticker.displayName    = row.get< std::string >(1);
					ticker.providerID     = row.get< std::string >(2);
					ticker.providerSymbol = row.get< std::string >(3);
					ticker.exchange       = row.get< std::string >(4);
					ticker.quoteSourceURL = row.get< std::string >(5);
					ticker.displayOrder   = static_cast< unsigned int >(row.get< int >(6));
					ticker.createdAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(7)));
					ticker.updatedAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(8)));
					tickers.push_back(std::move(ticker));
				}

				transaction.commit();
				return tickers;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting pinned stonks tickers for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void StonksPinnedTickerTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
