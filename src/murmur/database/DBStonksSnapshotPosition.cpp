// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksSnapshotPosition.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksSnapshotPosition::DBStonksSnapshotPosition(unsigned int serverID, unsigned int snapshotID,
														   unsigned int displayOrder, const std::string &symbol)
			: serverID(serverID), snapshotID(snapshotID), displayOrder(displayOrder), symbol(symbol) {}

		bool operator==(const DBStonksSnapshotPosition &lhs, const DBStonksSnapshotPosition &rhs) {
			return lhs.serverID == rhs.serverID && lhs.snapshotID == rhs.snapshotID
				   && lhs.displayOrder == rhs.displayOrder && lhs.symbol == rhs.symbol
				   && lhs.quantity == rhs.quantity && lhs.price == rhs.price
				   && lhs.marketValue == rhs.marketValue && lhs.currency == rhs.currency
				   && lhs.displayName == rhs.displayName && lhs.providerID == rhs.providerID
				   && lhs.providerSymbol == rhs.providerSymbol && lhs.exchange == rhs.exchange
				   && lhs.quoteTime == rhs.quoteTime && lhs.quoteSourceURL == rhs.quoteSourceURL
				   && lhs.quoteConfidence == rhs.quoteConfidence;
		}

		bool operator!=(const DBStonksSnapshotPosition &lhs, const DBStonksSnapshotPosition &rhs) {
			return !(lhs == rhs);
		}

	} // namespace db
} // namespace server
} // namespace mumble
