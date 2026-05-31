// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSFEEDPREFERENCES_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSFEEDPREFERENCES_H_

#include <chrono>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksFeedPreferences {
			unsigned int serverID                            = {};
			unsigned int userID                              = {};
			bool showMine                                    = true;
			bool showPopular                                 = true;
			bool showPins                                    = true;
			std::chrono::system_clock::time_point updatedAt = {};

			DBStonksFeedPreferences() = default;
			DBStonksFeedPreferences(unsigned int serverID, unsigned int userID);

			friend bool operator==(const DBStonksFeedPreferences &lhs, const DBStonksFeedPreferences &rhs);
			friend bool operator!=(const DBStonksFeedPreferences &lhs, const DBStonksFeedPreferences &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSFEEDPREFERENCES_H_
