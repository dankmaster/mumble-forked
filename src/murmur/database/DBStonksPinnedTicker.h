// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBSTONKSPINNEDTICKER_H_
#define MUMBLE_SERVER_DATABASE_DBSTONKSPINNEDTICKER_H_

#include <chrono>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBStonksPinnedTicker {
			unsigned int serverID                            = {};
			unsigned int userID                              = {};
			std::string symbol                               = {};
			std::string displayName                          = {};
			std::string providerID                           = {};
			std::string providerSymbol                       = {};
			std::string exchange                             = {};
			std::string quoteSourceURL                       = {};
			unsigned int displayOrder                        = {};
			std::chrono::system_clock::time_point createdAt = {};
			std::chrono::system_clock::time_point updatedAt = {};

			DBStonksPinnedTicker() = default;
			DBStonksPinnedTicker(unsigned int serverID, unsigned int userID, const std::string &symbol);

			friend bool operator==(const DBStonksPinnedTicker &lhs, const DBStonksPinnedTicker &rhs);
			friend bool operator!=(const DBStonksPinnedTicker &lhs, const DBStonksPinnedTicker &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBSTONKSPINNEDTICKER_H_
