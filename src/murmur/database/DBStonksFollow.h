// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSFOLLOW_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSFOLLOW_H_

#include <chrono>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksFollow {
			unsigned int serverID                            = {};
			unsigned int followerUserID                      = {};
			unsigned int targetUserID                        = {};
			std::chrono::system_clock::time_point createdAt = {};

			DBStonksFollow() = default;
			DBStonksFollow(unsigned int serverID, unsigned int followerUserID, unsigned int targetUserID);

			friend bool operator==(const DBStonksFollow &lhs, const DBStonksFollow &rhs);
			friend bool operator!=(const DBStonksFollow &lhs, const DBStonksFollow &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSFOLLOW_H_
