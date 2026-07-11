// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#ifndef MUMBLE_MUMBLE_SEARCHTYPES_H_
#define MUMBLE_MUMBLE_SEARCHTYPES_H_
#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <cstdint>
#include <map>

namespace Search {
enum class SearchType { User, Channel };

struct SearchResult {
	int64_t begin  = -1;
	int64_t length = -1;
	SearchType type;
	QString fullText;
	QString channelHierarchy;

	explicit operator bool() const {
		return begin >= 0 && length > 0;
	}
};

struct SearchResultSortComparator {
	bool operator()(const SearchResult &lhs, const SearchResult &rhs) const {
		if (lhs.length != rhs.length) {
			return lhs.length > rhs.length;
		}
		if (lhs.begin != rhs.begin) {
			return lhs.begin < rhs.begin;
		}
		if (lhs.fullText != rhs.fullText) {
			return lhs.fullText.compare(rhs.fullText) < 0;
		}
		if (lhs.channelHierarchy != rhs.channelHierarchy) {
			return lhs.channelHierarchy.compare(rhs.channelHierarchy) < 0;
		}
		return lhs.type == SearchType::User;
	}
};

using SearchResultMap = std::map< SearchResult, unsigned int, SearchResultSortComparator >;

class SearchDialog final {
public:
	enum class UserAction { NONE, JOIN };
	enum class ChannelAction { NONE, JOIN };
	static QString toString(UserAction action);
	static QString toString(ChannelAction action);
};
}
Q_DECLARE_METATYPE(Search::SearchDialog::UserAction)
Q_DECLARE_METATYPE(Search::SearchDialog::ChannelAction)
#endif
