// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATRENDERTYPES_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATRENDERTYPES_H_

#include "Mumble.pb.h"

#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtGui/QColor>
#include <QtGui/QImage>

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

#endif
