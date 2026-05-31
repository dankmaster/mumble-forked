// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksPinnedTicker.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksPinnedTicker::DBStonksPinnedTicker(unsigned int serverID, unsigned int userID,
												   const std::string &symbol)
			: serverID(serverID), userID(userID), symbol(symbol) {}

		bool operator==(const DBStonksPinnedTicker &lhs, const DBStonksPinnedTicker &rhs) {
			return lhs.serverID == rhs.serverID && lhs.userID == rhs.userID && lhs.symbol == rhs.symbol
				   && lhs.displayName == rhs.displayName && lhs.providerID == rhs.providerID
				   && lhs.providerSymbol == rhs.providerSymbol && lhs.exchange == rhs.exchange
				   && lhs.quoteSourceURL == rhs.quoteSourceURL && lhs.displayOrder == rhs.displayOrder
				   && toEpochSeconds(lhs.createdAt) == toEpochSeconds(rhs.createdAt)
				   && toEpochSeconds(lhs.updatedAt) == toEpochSeconds(rhs.updatedAt);
		}

		bool operator!=(const DBStonksPinnedTicker &lhs, const DBStonksPinnedTicker &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
