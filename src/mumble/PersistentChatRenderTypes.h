// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATRENDERTYPES_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATRENDERTYPES_H_

#include "Mumble.pb.h"

#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include <optional>

enum class PersistentChatDisplayMode { Bubble, CompactTranscript };

struct PersistentChatGroupHeaderSpec {
	bool selfAuthored = false;
	bool aggregateScope = false;
	bool systemMessage = false;
	PersistentChatDisplayMode displayMode = PersistentChatDisplayMode::Bubble;
	QString actorLabel;
	QColor actorColor;
	QColor avatarForegroundColor;
	QColor avatarBackgroundColor;
	QString timeLabel;
	QString timeToolTip;
	QString scopeLabel;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID = 0;
};

enum class PersistentChatPreviewKind { None, LinkCard, Image };

struct PersistentChatPreviewSpec {
	PersistentChatPreviewKind kind = PersistentChatPreviewKind::None;
	QUrl actionUrl;
	QString title;
	QString description;
	QString subtitle;
	QString statusText;
	QImage thumbnailImage;
	bool showThumbnailPlaceholder = false;
};

struct PersistentChatBubbleSpec {
	unsigned int messageID = 0;
	unsigned int threadID = 0;
	PersistentChatDisplayMode displayMode = PersistentChatDisplayMode::Bubble;
	QString bodyHtml;
	QString previewKey;
	QVector< QPair< QUrl, QImage > > imageResources;
	bool selfAuthored = false;
	PersistentChatPreviewSpec previewSpec;
	QString copyText;
	bool systemMessage = false;
	bool deleteEnabled = false;
	bool hasReply = false;
	unsigned int replyMessageID = 0;
	QString replyActor;
	QString replySnippet;
	QString timeToolTip;
	bool replyEnabled = true;
	bool readOnlyAction = false;
	QString actionText;
	MumbleProto::ChatScope actionScope = MumbleProto::Channel;
	unsigned int actionScopeID = 0;
	QString transcriptActorLabel;
	QColor transcriptActorColor;
	QString transcriptTimeLabel;
};

enum class PersistentChatHistoryRowKind { State, LoadOlder, DateDivider, UnreadDivider, MessageGroup };

struct PersistentChatStateRowSpec {
	QString eyebrow;
	QString title;
	QString body;
	QStringList hints;
	int minimumHeight = 220;
};

struct PersistentChatLoadOlderRowSpec {
	QString text;
	bool loading = false;
	bool enabled = true;
};

struct PersistentChatTextRowSpec {
	QString text;
	PersistentChatDisplayMode displayMode = PersistentChatDisplayMode::Bubble;
};

struct PersistentChatMessageGroupRowSpec {
	PersistentChatGroupHeaderSpec header;
	QString avatarFallbackText;
	QVector< PersistentChatBubbleSpec > bubbles;
	unsigned int firstMessageID = 0;
	unsigned int firstThreadID  = 0;
};

struct PersistentChatHistoryRow {
	PersistentChatHistoryRowKind kind = PersistentChatHistoryRowKind::State;
	QString rowId;
	QString signature;
	std::optional< PersistentChatStateRowSpec > state;
	std::optional< PersistentChatLoadOlderRowSpec > loadOlder;
	std::optional< PersistentChatTextRowSpec > text;
	std::optional< PersistentChatMessageGroupRowSpec > messageGroup;
};

#endif
