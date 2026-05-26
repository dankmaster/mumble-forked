// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBStonksSnapshot.h"

namespace mumble {
namespace server {
	namespace db {

		DBStonksSnapshot::DBStonksSnapshot(unsigned int serverID, unsigned int snapshotID, unsigned int userID)
			: serverID(serverID), snapshotID(snapshotID), userID(userID) {}

		bool operator==(const DBStonksSnapshot &lhs, const DBStonksSnapshot &rhs) {
			return lhs.serverID == rhs.serverID && lhs.snapshotID == rhs.snapshotID && lhs.userID == rhs.userID
				   && lhs.createdAt == rhs.createdAt && lhs.currency == rhs.currency
				   && lhs.totalValue == rhs.totalValue && lhs.note == rhs.note;
		}

		bool operator!=(const DBStonksSnapshot &lhs, const DBStonksSnapshot &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
