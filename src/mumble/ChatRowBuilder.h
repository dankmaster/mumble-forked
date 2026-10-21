// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATROWBUILDER_H_
#define MUMBLE_MUMBLE_CHATROWBUILDER_H_

#include "ChatTimelineRow.h"

#include <QtCore/QLocale>

namespace MumbleProto {
class ChatMessage;
}

// Turns MumbleProto chat messages into ChatTimelineRows. Stateless and
// re-entrant: it runs on the chat timeline worker thread and must not touch
// UI-thread objects.
class ChatRowBuilder {
public:
	struct Options {
		quint32 ownUserId  = 0;
		quint32 ownSession = 0;
		QLocale locale;
	};

	explicit ChatRowBuilder(Options options = {});

	ChatTimelineRow build(const MumbleProto::ChatMessage &message) const;
	// Undecorated row for a log line or direct message.
	ChatTimelineRow buildLocal(const ChatLocalEntry &entry) const;

	// Rows for messages[first..last], oldest first, each decorated against its
	// older neighbour. messages[first - 1], when it exists, is only read to
	// decorate the first row, the way a history page carries one context message.
	QVector< ChatTimelineRow > buildRange(const QVector< MumbleProto::ChatMessage > &messages, int first,
										  int last) const;

	// Group and date decoration against the older neighbour only.
	void decorate(ChatTimelineRow &row, const ChatTimelineRow *older) const;

	struct Body {
		QVariantList blocks;
		QString plainText;
		int inlineImages = 0;
	};
	// MarkdownLite bodies become paragraph, heading, quote and code blocks; plain
	// text is linkified; legacy HTML is parsed and re-emitted from a safe subset.
	static Body buildBody(const QString &text, bool markdown);

private:
	Options m_options;
};

#endif // MUMBLE_MUMBLE_CHATROWBUILDER_H_
