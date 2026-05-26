// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOTPOSITION_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOTPOSITION_H_

#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksSnapshotPosition {
			unsigned int serverID    = {};
			unsigned int snapshotID  = {};
			unsigned int displayOrder = {};
			std::string symbol       = {};
			double quantity          = {};
			double price             = {};
			double marketValue       = {};
			std::string currency     = {};
			std::string displayName  = {};
			std::string providerID   = {};
			std::string providerSymbol = {};
			std::string exchange     = {};
			long long quoteTime = {};
			std::string quoteSourceURL = {};
			double quoteConfidence   = {};

			DBStonksSnapshotPosition() = default;
			DBStonksSnapshotPosition(unsigned int serverID, unsigned int snapshotID, unsigned int displayOrder,
									 const std::string &symbol);

			friend bool operator==(const DBStonksSnapshotPosition &lhs, const DBStonksSnapshotPosition &rhs);
			friend bool operator!=(const DBStonksSnapshotPosition &lhs, const DBStonksSnapshotPosition &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOTPOSITION_H_
