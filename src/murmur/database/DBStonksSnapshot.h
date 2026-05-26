// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOT_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOT_H_

#include <chrono>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksSnapshot {
			unsigned int serverID                           = {};
			unsigned int snapshotID                         = {};
			unsigned int userID                             = {};
			std::chrono::system_clock::time_point createdAt = {};
			std::string currency                            = {};
			double totalValue                               = {};
			std::string note                                = {};

			DBStonksSnapshot() = default;
			DBStonksSnapshot(unsigned int serverID, unsigned int snapshotID, unsigned int userID);

			friend bool operator==(const DBStonksSnapshot &lhs, const DBStonksSnapshot &rhs);
			friend bool operator!=(const DBStonksSnapshot &lhs, const DBStonksSnapshot &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSSNAPSHOT_H_
