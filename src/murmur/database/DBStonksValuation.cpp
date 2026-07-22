// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksValuation.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksValuation::DBStonksValuation(unsigned int serverID, unsigned int userID,
											 unsigned int portfolioSnapshotID)
			: serverID(serverID), userID(userID), portfolioSnapshotID(portfolioSnapshotID) {}

		bool operator==(const DBStonksValuation &lhs, const DBStonksValuation &rhs) {
			return lhs.serverID == rhs.serverID && lhs.userID == rhs.userID
				   && lhs.portfolioSnapshotID == rhs.portfolioSnapshotID
				   && toEpochSeconds(lhs.valuedAt) == toEpochSeconds(rhs.valuedAt)
				   && lhs.totalValue == rhs.totalValue && lhs.currency == rhs.currency && lhs.source == rhs.source
				   && lhs.pricedPositions == rhs.pricedPositions && lhs.totalPositions == rhs.totalPositions
				   && lhs.estimated == rhs.estimated;
		}

		bool operator!=(const DBStonksValuation &lhs, const DBStonksValuation &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
