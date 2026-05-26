// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "StonksFollowTable.h"
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

		constexpr const char *StonksFollowTable::NAME;
		constexpr const char *StonksFollowTable::column::server_id;
		constexpr const char *StonksFollowTable::column::follower_user_id;
		constexpr const char *StonksFollowTable::column::target_user_id;
		constexpr const char *StonksFollowTable::column::created_at;
		constexpr unsigned int StonksFollowTable::INTRODUCED_IN_SCHEMA_VERSION;

		StonksFollowTable::StonksFollowTable(soci::session &sql, ::mdb::Backend backend, const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column followerUserIDCol(column::follower_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			followerUserIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column targetUserIDCol(column::target_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			targetUserIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, followerUserIDCol, targetUserIDCol, createdAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), followerUserIDCol.getName(), targetUserIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey followerFK(userTable, { serverCol, followerUserIDCol });
			addForeignKey(followerFK);

			::mdb::ForeignKey targetFK(userTable, { serverCol, targetUserIDCol });
			addForeignKey(targetFK);

			::mdb::Index followingIndex(std::string(NAME) + "_following",
										{ column::server_id, column::follower_user_id, column::created_at });
			addIndex(followingIndex, false);
		}

		void StonksFollowTable::setFollow(const DBStonksFollow &follow) {
			try {
				auto createdAt = follow.createdAt;
				if (createdAt == std::chrono::system_clock::time_point()) {
					createdAt = std::chrono::system_clock::now();
				}

				const std::size_t createdAtEpoch = toEpochSeconds(createdAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::follower_user_id << "\" = :followerUserID AND \"" << column::target_user_id
					  << "\" = :targetUserID",
					soci::use(follow.serverID), soci::use(follow.followerUserID), soci::use(follow.targetUserID);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
					  << column::follower_user_id << "\", \"" << column::target_user_id << "\", \""
					  << column::created_at
					  << "\") VALUES (:serverID, :followerUserID, :targetUserID, :createdAt)",
					soci::use(follow.serverID), soci::use(follow.followerUserID), soci::use(follow.targetUserID),
					soci::use(createdAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting stonks follow from user with ID "
															  + std::to_string(follow.followerUserID)
															  + " to user with ID "
															  + std::to_string(follow.targetUserID)
															  + " on server with ID "
															  + std::to_string(follow.serverID)));
			}
		}

		void StonksFollowTable::removeFollow(unsigned int serverID, unsigned int followerUserID,
											 unsigned int targetUserID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::follower_user_id << "\" = :followerUserID AND \"" << column::target_user_id
					  << "\" = :targetUserID",
					soci::use(serverID), soci::use(followerUserID), soci::use(targetUserID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing stonks follow from user with ID "
															  + std::to_string(followerUserID)
															  + " to user with ID "
															  + std::to_string(targetUserID)
															  + " on server with ID " + std::to_string(serverID)));
			}
		}

		std::vector< unsigned int > StonksFollowTable::getFollowedUsers(unsigned int serverID,
																		unsigned int followerUserID) {
			try {
				std::vector< unsigned int > followedUsers;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::target_user_id << "\" FROM \"" << NAME << "\" WHERE \""
								   << column::server_id << "\" = :serverID AND \"" << column::follower_user_id
								   << "\" = :followerUserID ORDER BY \"" << column::created_at << "\" ASC, \""
								   << column::target_user_id << "\" ASC",
					 soci::use(serverID), soci::use(followerUserID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					followedUsers.push_back(static_cast< unsigned int >(row.get< int >(0)));
				}

				transaction.commit();
				return followedUsers;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting stonks followed users for user with ID "
															  + std::to_string(followerUserID)
															  + " on server with ID " + std::to_string(serverID)));
			}
		}

		void StonksFollowTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
