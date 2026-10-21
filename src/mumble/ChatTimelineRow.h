// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATTIMELINEROW_H_
#define MUMBLE_MUMBLE_CHATTIMELINEROW_H_

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QVector>
#include <QtCore/QtGlobal>

#include <tuple>

// One decorated, immutable timeline row as the v2 chat timeline presents it.
// Rows are built off the UI thread from MumbleProto messages. Everything a row
// needs to lay itself out, including media geometry, is known when it is built,
// so a row never changes height because something finished loading.
struct ChatTimelineRowKey {
	quint64 createdAt  = 0;
	quint32 threadId   = 0;
	quint32 messageId  = 0;

	bool operator==(const ChatTimelineRowKey &other) const {
		return createdAt == other.createdAt && threadId == other.threadId && messageId == other.messageId;
	}
	bool operator!=(const ChatTimelineRowKey &other) const { return !(*this == other); }
	// Same order as PersistentChatStore: oldest first.
	bool operator<(const ChatTimelineRowKey &other) const {
		return std::tie(createdAt, threadId, messageId)
			   < std::tie(other.createdAt, other.threadId, other.messageId);
	}
	quint64 identity() const { return (static_cast< quint64 >(threadId) << 32U) | messageId; }
};

enum class ChatTimelineRowKind { Text, Media, Gallery, Video, LinkCard, File, System };

struct ChatTimelineMedia {
	enum class Kind { Image, Video, Audio, Document, Binary, InlineImage };
	Kind kind              = Kind::Image;
	quint32 assetId        = 0;
	quint32 previewAssetId = 0;
	QString fileName;
	QString mime;
	quint64 byteSize   = 0;
	quint64 durationMs = 0;
	int width          = 0;
	int height         = 0;
	// False when the source has no dimensions; the view then uses a fixed default
	// box for the kind and fits the media inside it.
	bool geometryKnown = false;
	bool inlineSafe    = false;

	bool operator==(const ChatTimelineMedia &other) const {
		return kind == other.kind && assetId == other.assetId && previewAssetId == other.previewAssetId
			   && fileName == other.fileName && mime == other.mime && byteSize == other.byteSize
			   && durationMs == other.durationMs && width == other.width && height == other.height
			   && geometryKnown == other.geometryKnown && inlineSafe == other.inlineSafe;
	}
};

struct ChatTimelineCard {
	enum class Status { None, Pending, Ready, Failed };
	// Layout class chosen from metadata so the card's size is fixed before any
	// thumbnail arrives.
	enum class Layout { Compact, SmallThumbnail, LargeThumbnail };
	Status status = Status::None;
	Layout layout = Layout::Compact;
	QString url;
	QString siteName;
	QString title;
	QString description;
	quint32 previewAssetId = 0;
	int previewWidth       = 0;
	int previewHeight      = 0;

	bool operator==(const ChatTimelineCard &other) const {
		return status == other.status && layout == other.layout && url == other.url && siteName == other.siteName
			   && title == other.title && description == other.description
			   && previewAssetId == other.previewAssetId && previewWidth == other.previewWidth
			   && previewHeight == other.previewHeight;
	}
};

struct ChatTimelineReaction {
	QString emoji;
	quint32 count = 0;
	bool self     = false;
	QStringList actorNames;

	bool operator==(const ChatTimelineReaction &other) const {
		return emoji == other.emoji && count == other.count && self == other.self && actorNames == other.actorNames;
	}
};

struct ChatTimelineRow {
	ChatTimelineRowKey key;
	ChatTimelineRowKind kind = ChatTimelineRowKind::Text;

	quint32 authorUserId  = 0;
	quint32 authorSession = 0;
	QString authorName;
	QByteArray authorTextureHash;
	bool own = false;

	quint64 editedAt = 0;

	// Body as blocks: { kind: "p" | "h" | "quote" | "code", level, format: "plain" |
	// "styled" | "rich", text }. "styled" is Qt StyledText, "rich" is sanitized
	// RichText kept for legacy HTML bodies.
	QVariantList bodyBlocks;
	QString plainText;

	QVector< ChatTimelineMedia > media;
	ChatTimelineCard card;
	QVector< ChatTimelineReaction > reactions;

	quint32 replyToMessageId = 0;
	QString replyActor;
	QString replySnippet;

	// Decoration against the older neighbour only, so newer rows never change it.
	bool groupStart = true;
	QString dateLabel;
	QString timeText;
};

#endif // MUMBLE_MUMBLE_CHATTIMELINEROW_H_
