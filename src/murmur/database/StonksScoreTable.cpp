// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksScoreTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/FormatException.h"
#include "database/Index.h"
#include "database/TransactionHolder.h"
#include "database/Utils.h"

#include <soci/soci.h>

#include <cassert>
#include <exception>
#include <limits>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *StonksScoreTable::NAME;
		constexpr const char *StonksScoreTable::column::server_id;
		constexpr const char *StonksScoreTable::column::user_id;
		constexpr const char *StonksScoreTable::column::period;
		constexpr const char *StonksScoreTable::column::score_percent;
		constexpr const char *StonksScoreTable::column::updated_at;
		constexpr unsigned int StonksScoreTable::INTRODUCED_IN_SCHEMA_VERSION;

		StonksScoreTable::StonksScoreTable(soci::session &sql, ::mdb::Backend backend, const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userIDCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column periodCol(column::period, ::mdb::DataType(::mdb::DataType::VarChar, 16));
			periodCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column scorePercentCol(column::score_percent, ::mdb::DataType(::mdb::DataType::Double));
			scorePercentCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column updatedAtCol(column::updated_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			updatedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, userIDCol, periodCol, scorePercentCol, updatedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), userIDCol.getName(), periodCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey userFK(userTable, { serverCol, userIDCol });
			addForeignKey(userFK);

			::mdb::Index leaderboardIndex(std::string(NAME) + "_leaderboard",
										  { column::server_id, column::period, column::score_percent });
			addIndex(leaderboardIndex, false);
		}

		void StonksScoreTable::setScore(const DBStonksScore &score) {
			if (score.period.empty()) {
				throw ::mdb::FormatException("A stonks score requires a non-empty period");
			}

			try {
				auto updatedAt = score.updatedAt;
				if (updatedAt == std::chrono::system_clock::time_point()) {
					updatedAt = std::chrono::system_clock::now();
				}

				const std::size_t updatedAtEpoch = toEpochSeconds(updatedAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID AND \"" << column::period << "\" = :period",
					soci::use(score.serverID), soci::use(score.userID), soci::use(score.period);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::user_id
					  << "\", \"" << column::period << "\", \"" << column::score_percent << "\", \""
					  << column::updated_at
					  << "\") VALUES (:serverID, :userID, :period, :scorePercent, :updatedAt)",
					soci::use(score.serverID), soci::use(score.userID), soci::use(score.period),
					soci::use(score.scorePercent), soci::use(updatedAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting stonks score for user with ID "
															  + std::to_string(score.userID)
															  + " on server with ID " + std::to_string(score.serverID)));
			}
		}

		std::optional< DBStonksScore > StonksScoreTable::getScore(unsigned int serverID, unsigned int userID,
																  const std::string &period) {
			try {
				double scorePercent = 0.0;
				std::size_t updatedAt = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::score_percent << "\", \"" << column::updated_at << "\" FROM \""
					  << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \"" << column::user_id
					  << "\" = :userID AND \"" << column::period << "\" = :period",
					soci::use(serverID), soci::use(userID), soci::use(period), soci::into(scorePercent),
					soci::into(updatedAt);

				transaction.commit();

				if (!m_sql.got_data()) {
					return std::nullopt;
				}

				DBStonksScore score(serverID, userID, period);
				score.scorePercent = scorePercent;
				score.updatedAt    = std::chrono::system_clock::time_point(std::chrono::seconds(updatedAt));
				return score;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks score for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksScore > StonksScoreTable::getScores(unsigned int serverID, unsigned int userID) {
			try {
				std::vector< DBStonksScore > scores;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::period << "\", \"" << column::score_percent << "\", \""
								   << column::updated_at << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
								   << "\" = :serverID AND \"" << column::user_id << "\" = :userID ORDER BY \""
								   << column::period << "\" ASC",
					 soci::use(serverID), soci::use(userID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBStonksScore score(serverID, userID, row.get< std::string >(0));
					score.scorePercent = row.get< double >(1);
					score.updatedAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(2)));
					scores.push_back(std::move(score));
				}

				transaction.commit();
				return scores;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks scores for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBStonksScore > StonksScoreTable::getLeaderboard(unsigned int serverID, const std::string &period,
																	  unsigned int maxEntries) {
			assert(maxEntries <= std::numeric_limits< int >::max());

			try {
				std::vector< DBStonksScore > scores;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::user_id << "\", \"" << column::score_percent << "\", \""
								   << column::updated_at << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
								   << "\" = :serverID AND \"" << column::period << "\" = :period ORDER BY \""
								   << column::score_percent << "\" DESC, \"" << column::updated_at << "\" DESC "
								   << ::mdb::utils::limitOffset(m_backend, ":limit", "0"),
					 soci::use(serverID), soci::use(period), soci::use(maxEntries), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBStonksScore score(serverID, static_cast< unsigned int >(row.get< int >(0)), period);
					score.scorePercent = row.get< double >(1);
					score.updatedAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(2)));
					scores.push_back(std::move(score));
				}

				transaction.commit();
				return scores;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks leaderboard for period \""
															  + period + "\" on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void StonksScoreTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
