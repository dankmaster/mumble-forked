// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatHistoryGrantTable.h"
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

		namespace {
			ChatThreadScope decodeScope(unsigned int scopeValue) {
				switch (scopeValue) {
					case static_cast< unsigned int >(ChatThreadScope::Channel):
						return ChatThreadScope::Channel;
					case static_cast< unsigned int >(ChatThreadScope::ServerGlobal):
						return ChatThreadScope::ServerGlobal;
					case static_cast< unsigned int >(ChatThreadScope::TextChannel):
						return ChatThreadScope::TextChannel;
					default:
						return ChatThreadScope::Private;
				}
			}
		} // namespace

		constexpr const char *ChatHistoryGrantTable::NAME;
		constexpr const char *ChatHistoryGrantTable::column::server_id;
		constexpr const char *ChatHistoryGrantTable::column::user_id;
		constexpr const char *ChatHistoryGrantTable::column::scope;
		constexpr const char *ChatHistoryGrantTable::column::scope_id;
		constexpr const char *ChatHistoryGrantTable::column::visible_after;
		constexpr const char *ChatHistoryGrantTable::column::granted_at;
		constexpr const char *ChatHistoryGrantTable::column::granted_by_user_id;
		constexpr unsigned int ChatHistoryGrantTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatHistoryGrantTable::ChatHistoryGrantTable(soci::session &sql, ::mdb::Backend backend,
													 const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userIDCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column scopeCol(column::scope, ::mdb::DataType(::mdb::DataType::SmallInteger));
			scopeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column scopeIDCol(column::scope_id, ::mdb::DataType(::mdb::DataType::Integer));
			scopeIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column visibleAfterCol(column::visible_after, ::mdb::DataType(::mdb::DataType::EpochTime));
			visibleAfterCol.setDefaultValue("0");
			visibleAfterCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column grantedAtCol(column::granted_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			grantedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column grantedByUserIDCol(column::granted_by_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			grantedByUserIDCol.setDefaultValue("NULL");

			setColumns({ serverCol, userIDCol, scopeCol, scopeIDCol, visibleAfterCol, grantedAtCol,
						 grantedByUserIDCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), userIDCol.getName(), scopeCol.getName(),
								   scopeIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey userFK(userTable, { serverCol, userIDCol });
			addForeignKey(userFK);

			::mdb::ForeignKey grantedByFK(userTable, { serverCol, grantedByUserIDCol });
			addForeignKey(grantedByFK);

			::mdb::Index scopeIndex(std::string(NAME) + "_scope",
									{ column::server_id, column::scope, column::scope_id });
			addIndex(scopeIndex, false);
		}

		void ChatHistoryGrantTable::setGrant(const DBChatHistoryGrant &grant) {
			try {
				const unsigned int scopeValue = static_cast< unsigned int >(grant.scope);
				const std::size_t visibleAfter = toEpochSeconds(grant.visibleAfter);
				const std::size_t grantedAt =
					toEpochSeconds(grant.grantedAt == std::chrono::system_clock::time_point()
									   ? std::chrono::system_clock::now()
									   : grant.grantedAt);
				unsigned int grantedByUserID = 0;
				soci::indicator grantedByInd = soci::i_null;
				if (grant.grantedByUserID) {
					grantedByUserID = grant.grantedByUserID.value();
					grantedByInd    = soci::i_ok;
				}

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID AND \"" << column::scope << "\" = :scope AND \""
					  << column::scope_id << "\" = :scopeID",
					soci::use(grant.serverID), soci::use(grant.userID), soci::use(scopeValue),
					soci::use(grant.scopeID);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::user_id
					  << "\", \"" << column::scope << "\", \"" << column::scope_id << "\", \""
					  << column::visible_after << "\", \"" << column::granted_at << "\", \""
					  << column::granted_by_user_id
					  << "\") VALUES (:serverID, :userID, :scope, :scopeID, :visibleAfter, :grantedAt, :grantedByUserID)",
					soci::use(grant.serverID), soci::use(grant.userID), soci::use(scopeValue),
					soci::use(grant.scopeID), soci::use(visibleAfter), soci::use(grantedAt),
					soci::use(grantedByUserID, grantedByInd);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting chat history grant for user with ID "
															  + std::to_string(grant.userID)
															  + " on server with ID "
															  + std::to_string(grant.serverID)));
			}
		}

		void ChatHistoryGrantTable::removeGrant(unsigned int serverID, unsigned int userID, ChatThreadScope scope,
												unsigned int scopeID) {
			try {
				const unsigned int scopeValue = static_cast< unsigned int >(scope);
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::user_id << "\" = :userID AND \"" << column::scope << "\" = :scope AND \""
					  << column::scope_id << "\" = :scopeID",
					soci::use(serverID), soci::use(userID), soci::use(scopeValue), soci::use(scopeID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing chat history grant for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::optional< DBChatHistoryGrant > ChatHistoryGrantTable::getGrant(unsigned int serverID, unsigned int userID,
																			 ChatThreadScope scope, unsigned int scopeID) {
			try {
				const unsigned int scopeValue = static_cast< unsigned int >(scope);
				std::size_t visibleAfter      = 0;
				std::size_t grantedAt         = 0;
				unsigned int grantedByUserID  = 0;
				soci::indicator grantedByInd;

				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT \"" << column::visible_after << "\", \"" << column::granted_at << "\", \""
					  << column::granted_by_user_id << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::user_id << "\" = :userID AND \"" << column::scope
					  << "\" = :scope AND \"" << column::scope_id << "\" = :scopeID",
					soci::use(serverID), soci::use(userID), soci::use(scopeValue), soci::use(scopeID),
					soci::into(visibleAfter), soci::into(grantedAt), soci::into(grantedByUserID, grantedByInd);
				transaction.commit();

				if (!m_sql.got_data()) {
					return std::nullopt;
				}

				DBChatHistoryGrant grant(serverID, userID, scope, scopeID);
				grant.visibleAfter = std::chrono::system_clock::time_point(std::chrono::seconds(visibleAfter));
				grant.grantedAt    = std::chrono::system_clock::time_point(std::chrono::seconds(grantedAt));
				if (grantedByInd == soci::i_ok) {
					grant.grantedByUserID = grantedByUserID;
				}

				return grant;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat history grant for user with ID "
															  + std::to_string(userID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatHistoryGrant > ChatHistoryGrantTable::getGrants(unsigned int serverID) {
			try {
				std::vector< DBChatHistoryGrant > grants;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::user_id << "\", \"" << column::scope << "\", \""
								   << column::scope_id << "\", \"" << column::visible_after << "\", \""
								   << column::granted_at << "\", \"" << column::granted_by_user_id << "\" FROM \""
								   << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID",
					 soci::use(serverID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBChatHistoryGrant grant(serverID, static_cast< unsigned int >(row.get< int >(0)),
											 decodeScope(static_cast< unsigned int >(row.get< int >(1))),
											 static_cast< unsigned int >(row.get< int >(2)));
					grant.visibleAfter =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(3)));
					grant.grantedAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(4)));
					if (row.get_indicator(5) == soci::i_ok) {
						grant.grantedByUserID = static_cast< unsigned int >(row.get< int >(5));
					}
					grants.push_back(std::move(grant));
				}
				transaction.commit();
				return grants;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat history grants on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatHistoryGrantTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
