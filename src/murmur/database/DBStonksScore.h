// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSSCORE_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSSCORE_H_

#include <chrono>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksScore {
			unsigned int serverID                          = {};
			unsigned int userID                            = {};
			std::string period                             = {};
			double scorePercent                            = {};
			std::chrono::system_clock::time_point updatedAt = {};

			DBStonksScore() = default;
			DBStonksScore(unsigned int serverID, unsigned int userID, const std::string &period);

			friend bool operator==(const DBStonksScore &lhs, const DBStonksScore &rhs);
			friend bool operator!=(const DBStonksScore &lhs, const DBStonksScore &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSSCORE_H_
