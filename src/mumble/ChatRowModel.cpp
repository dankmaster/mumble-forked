// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatRowModel.h"

#include <algorithm>

namespace {
	// Rows are newest first, so a row "comes before" another when its key is greater.
	bool newerThan(const ChatTimelineRowKey &lhs, const ChatTimelineRowKey &rhs) {
		return rhs < lhs;
	}

	QString kindName(ChatTimelineRowKind kind) {
		switch (kind) {
			case ChatTimelineRowKind::Text:
				return QStringLiteral("text");
			case ChatTimelineRowKind::Media:
				return QStringLiteral("media");
			case ChatTimelineRowKind::Gallery:
				return QStringLiteral("gallery");
			case ChatTimelineRowKind::Video:
				return QStringLiteral("video");
			case ChatTimelineRowKind::LinkCard:
				return QStringLiteral("card");
			case ChatTimelineRowKind::File:
				return QStringLiteral("file");
			case ChatTimelineRowKind::System:
				return QStringLiteral("system");
			case ChatTimelineRowKind::Log:
				return QStringLiteral("log");
		}
		return QStringLiteral("text");
	}

	QString mediaKindName(ChatTimelineMedia::Kind kind) {
		switch (kind) {
			case ChatTimelineMedia::Kind::Image:
				return QStringLiteral("image");
			case ChatTimelineMedia::Kind::Video:
				return QStringLiteral("video");
			case ChatTimelineMedia::Kind::Audio:
				return QStringLiteral("audio");
			case ChatTimelineMedia::Kind::Document:
				return QStringLiteral("document");
			case ChatTimelineMedia::Kind::Binary:
				return QStringLiteral("binary");
			case ChatTimelineMedia::Kind::InlineImage:
				return QStringLiteral("inlineImage");
		}
		return QStringLiteral("binary");
	}

	QVariantList mediaList(const QVector< ChatTimelineMedia > &media) {
		QVariantList list;
		list.reserve(media.size());
		for (const ChatTimelineMedia &item : media) {
			list << QVariantMap{ { QStringLiteral("kind"), mediaKindName(item.kind) },
								 { QStringLiteral("assetId"), item.assetId },
								 { QStringLiteral("previewAssetId"), item.previewAssetId },
								 { QStringLiteral("fileName"), item.fileName },
								 { QStringLiteral("mime"), item.mime },
								 { QStringLiteral("byteSize"), item.byteSize },
								 { QStringLiteral("durationMs"), item.durationMs },
								 { QStringLiteral("width"), item.width },
								 { QStringLiteral("height"), item.height },
								 { QStringLiteral("geometryKnown"), item.geometryKnown },
								 { QStringLiteral("inlineSafe"), item.inlineSafe } };
		}
		return list;
	}

	QVariantMap cardMap(const ChatTimelineCard &card) {
		if (card.status == ChatTimelineCard::Status::None) {
			return {};
		}
		const QString layout = card.layout == ChatTimelineCard::Layout::LargeThumbnail ? QStringLiteral("large")
							   : card.layout == ChatTimelineCard::Layout::SmallThumbnail ? QStringLiteral("small")
																						 : QStringLiteral("compact");
		return { { QStringLiteral("status"),
				   card.status == ChatTimelineCard::Status::Ready ? QStringLiteral("ready") : QStringLiteral("pending") },
				 { QStringLiteral("layout"), layout },
				 { QStringLiteral("url"), card.url },
				 { QStringLiteral("siteName"), card.siteName },
				 { QStringLiteral("title"), card.title },
				 { QStringLiteral("description"), card.description },
				 { QStringLiteral("previewAssetId"), card.previewAssetId },
				 { QStringLiteral("previewWidth"), card.previewWidth },
				 { QStringLiteral("previewHeight"), card.previewHeight } };
	}

	QVariantList reactionList(const QVector< ChatTimelineReaction > &reactions) {
		QVariantList list;
		list.reserve(reactions.size());
		for (const ChatTimelineReaction &reaction : reactions) {
			list << QVariantMap{ { QStringLiteral("emoji"), reaction.emoji },
								 { QStringLiteral("count"), reaction.count },
								 { QStringLiteral("self"), reaction.self },
								 { QStringLiteral("actorNames"), reaction.actorNames } };
		}
		return list;
	}
} // namespace

ChatRowModel::ChatRowModel(QObject *parent) : QAbstractListModel(parent) {
}

int ChatRowModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : count();
}

QVariant ChatRowModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
		return {};
	}
	const ChatTimelineRow &row = m_rows.at(index.row());
	if (role >= AvatarUrlRole && role <= CanDeleteRole) {
		const auto extras = m_extras.constFind(row.key.identity());
		if (extras == m_extras.cend()) {
			switch (role) {
				case AvatarUrlRole:
					return QString();
				case AttachmentsRole:
					return QVariantList();
				case PreviewRole:
					return QVariantMap();
				default:
					return false;
			}
		}
		switch (role) {
			case AvatarUrlRole:
				return extras->avatarUrl;
			case AttachmentsRole:
				return extras->attachments;
			case PreviewRole:
				return extras->preview;
			case CanReplyRole:
				return extras->canReply;
			case CanReactRole:
				return extras->canReact;
			default:
				return extras->canDelete;
		}
	}
	switch (role) {
		case MessageIdRole:
			return row.key.messageId;
		case ThreadIdRole:
			return row.key.threadId;
		case KindRole:
			return kindName(row.kind);
		case AuthorNameRole:
			return row.authorName;
		case AuthorUserIdRole:
			return row.authorUserId;
		case OwnRole:
			return row.own;
		case CreatedAtRole:
			return static_cast< qulonglong >(row.key.createdAt);
		case TimeTextRole:
			return row.timeText;
		case DateLabelRole:
			return row.dateLabel;
		case GroupStartRole:
			return row.groupStart;
		case EditedRole:
			return row.editedAt > 0;
		case BodyBlocksRole:
			return row.bodyBlocks;
		case PlainTextRole:
			return row.plainText;
		case MediaRole:
			return mediaList(row.media);
		case CardRole:
			return cardMap(row.card);
		case ReactionsRole:
			return reactionList(row.reactions);
		case ReplyToMessageIdRole:
			return row.replyToMessageId;
		case ReplyActorRole:
			return row.replyActor;
		case ReplySnippetRole:
			return row.replySnippet;
		case DeliveryStateRole:
			return row.deliveryState;
		case DeliveryLabelRole:
			return row.deliveryLabel;
		case DeliveryCanRetryRole:
			return row.deliveryCanRetry;
		case StableIdRole:
			return QString::number(row.key.messageId);
		default:
			return {};
	}
}

QHash< int, QByteArray > ChatRowModel::roleNames() const {
	return {
		{ MessageIdRole, "messageId" },
		{ ThreadIdRole, "threadId" },
		{ KindRole, "kind" },
		{ AuthorNameRole, "authorName" },
		{ AuthorUserIdRole, "authorUserId" },
		{ OwnRole, "own" },
		{ CreatedAtRole, "createdAt" },
		{ TimeTextRole, "timeText" },
		{ DateLabelRole, "dateLabel" },
		{ GroupStartRole, "groupStart" },
		{ EditedRole, "edited" },
		{ BodyBlocksRole, "bodyBlocks" },
		{ PlainTextRole, "plainText" },
		{ MediaRole, "media" },
		{ CardRole, "card" },
		{ ReactionsRole, "reactions" },
		{ ReplyToMessageIdRole, "replyToMessageId" },
		{ ReplyActorRole, "replyActor" },
		{ ReplySnippetRole, "replySnippet" },
		{ DeliveryStateRole, "deliveryState" },
		{ DeliveryLabelRole, "deliveryLabel" },
		{ DeliveryCanRetryRole, "deliveryCanRetry" },
		{ StableIdRole, "stableId" },
		{ AvatarUrlRole, "avatarUrl" },
		{ AttachmentsRole, "attachments" },
		{ PreviewRole, "preview" },
		{ CanReplyRole, "canReply" },
		{ CanReactRole, "canReact" },
		{ CanDeleteRole, "canDelete" },
	};
}

void ChatRowModel::resetScope(const QString &scopeToken) {
	const bool hadRows = !m_rows.isEmpty();
	beginResetModel();
	m_rows.clear();
	m_keyByIdentity.clear();
	m_scopeToken = scopeToken;
	endResetModel();
	if (hadRows) {
		emit countChanged();
	}
	emit scopeTokenChanged();
}

int ChatRowModel::lowerBound(const ChatTimelineRowKey &key, int from) const {
	const auto begin = m_rows.cbegin() + from;
	const auto it    = std::lower_bound(begin, m_rows.cend(), key, [](const ChatTimelineRow &row,
																		  const ChatTimelineRowKey &value) {
		   return newerThan(row.key, value);
	   });
	return static_cast< int >(it - m_rows.cbegin());
}

void ChatRowModel::insertRun(int position, QVector< ChatTimelineRow > &run) {
	if (run.isEmpty()) {
		return;
	}
	beginInsertRows(QModelIndex(), position, position + static_cast< int >(run.size()) - 1);
	for (const ChatTimelineRow &row : run) {
		m_keyByIdentity.insert(row.key.identity(), row.key);
	}
	m_rows.insert(position, run.size(), ChatTimelineRow());
	std::move(run.begin(), run.end(), m_rows.begin() + position);
	endInsertRows();
	run.clear();
}

void ChatRowModel::upsertRows(QVector< ChatTimelineRow > rows) {
	if (rows.isEmpty()) {
		return;
	}
	std::stable_sort(rows.begin(), rows.end(), [](const ChatTimelineRow &lhs, const ChatTimelineRow &rhs) {
		return newerThan(lhs.key, rhs.key);
	});
	rows.erase(std::unique(rows.begin(), rows.end(),
						   [](const ChatTimelineRow &lhs, const ChatTimelineRow &rhs) { return lhs.key == rhs.key; }),
			   rows.end());

	// Incoming rows are walked newest first. New rows that land on the same
	// position are collected into one run and inserted with a single
	// beginInsertRows. Positions are looked up in the array without the pending
	// run; every row in that run is newer, so flushing it shifts later positions
	// by exactly its length.
	const int countBefore = count();
	QVector< ChatTimelineRow > run;
	int runPosition = -1;
	int cursor      = 0;
	for (ChatTimelineRow &row : rows) {
		const auto known = m_keyByIdentity.constFind(row.key.identity());
		if (known != m_keyByIdentity.cend() && known.value() != row.key) {
			// The message moved in the order (its timestamp changed): reinsert it.
			insertRun(runPosition, run);
			removeMessages({ row.key.identity() });
			cursor = 0;
		}

		const int found   = lowerBound(row.key, cursor);
		const bool exists = found < count() && m_rows.at(found).key == row.key;
		int position      = found;
		if (!run.isEmpty() && (exists || found != runPosition)) {
			position += static_cast< int >(run.size());
			insertRun(runPosition, run);
		}

		if (exists) {
			const QList< int > roles = changedRoles(m_rows.at(position), row);
			if (!roles.isEmpty()) {
				m_rows[position] = std::move(row);
				emit dataChanged(index(position), index(position), roles);
			}
			cursor = position + 1;
			continue;
		}
		if (run.isEmpty()) {
			runPosition = position;
		}
		run.push_back(std::move(row));
		cursor = position;
	}
	insertRun(runPosition, run);
	if (count() != countBefore) {
		emit countChanged();
	}
}

void ChatRowModel::removeMessages(const QVector< quint64 > &identities) {
	const int countBefore = count();
	for (const quint64 identity : identities) {
		const auto known = m_keyByIdentity.constFind(identity);
		if (known == m_keyByIdentity.cend()) {
			continue;
		}
		const int position = lowerBound(known.value());
		m_keyByIdentity.erase(known);
		if (position >= count() || m_rows.at(position).key.identity() != identity) {
			continue;
		}
		beginRemoveRows(QModelIndex(), position, position);
		m_rows.removeAt(position);
		endRemoveRows();
	}
	if (count() != countBefore) {
		emit countChanged();
	}
}

void ChatRowModel::setExtras(const QVariantMap &messageState) {
	ChatTimelineRowKey identityOnly;
	identityOnly.threadId  = messageState.value(QStringLiteral("threadId")).toUInt();
	identityOnly.messageId = messageState.value(QStringLiteral("messageId")).toUInt();
	if (identityOnly.messageId == 0) {
		return;
	}
	Extras next;
	next.avatarUrl   = messageState.value(QStringLiteral("avatarUrl")).toString();
	next.attachments = messageState.value(QStringLiteral("attachments")).toList();
	next.preview     = messageState.value(QStringLiteral("preview")).toMap();
	next.canReply    = messageState.value(QStringLiteral("canReply")).toBool();
	next.canReact    = messageState.value(QStringLiteral("canReact")).toBool();
	next.canDelete   = messageState.value(QStringLiteral("canDelete")).toBool();
	next.touched     = ++m_extrasSequence;

	const quint64 identity = identityOnly.identity();
	Extras &current        = m_extras[identity];
	QList< int > roles;
	if (current.avatarUrl != next.avatarUrl) {
		roles << AvatarUrlRole;
	}
	if (current.attachments != next.attachments) {
		roles << AttachmentsRole;
	}
	if (current.preview != next.preview) {
		roles << PreviewRole;
	}
	if (current.canReply != next.canReply) {
		roles << CanReplyRole;
	}
	if (current.canReact != next.canReact) {
		roles << CanReactRole;
	}
	if (current.canDelete != next.canDelete) {
		roles << CanDeleteRole;
	}
	current = std::move(next);
	if (!roles.isEmpty()) {
		const int row = rowForMessage(identityOnly.threadId, identityOnly.messageId);
		if (row >= 0) {
			emit dataChanged(index(row), index(row), roles);
		}
	}
	pruneExtras();
}

void ChatRowModel::pruneExtras() {
	// Keep extras for shown rows and for the most recent updates, which may be
	// for rows the worker has not delivered yet.
	constexpr qsizetype Limit = 4096;
	constexpr quint64 Recent  = 1024;
	if (m_extras.size() <= Limit) {
		return;
	}
	const quint64 cutoff = m_extrasSequence > Recent ? m_extrasSequence - Recent : 0;
	for (auto it = m_extras.begin(); it != m_extras.end();) {
		if (it->touched < cutoff && !m_keyByIdentity.contains(it.key())) {
			it = m_extras.erase(it);
		} else {
			++it;
		}
	}
}

int ChatRowModel::rowForMessage(quint32 threadId, quint32 messageId) const {
	ChatTimelineRowKey identityOnly;
	identityOnly.threadId  = threadId;
	identityOnly.messageId = messageId;
	const auto known       = m_keyByIdentity.constFind(identityOnly.identity());
	if (known == m_keyByIdentity.cend()) {
		return -1;
	}
	const int position = lowerBound(known.value());
	return position < count() && m_rows.at(position).key == known.value() ? position : -1;
}

QVariantList ChatRowModel::rowsMatching(const QString &query) const {
	QVariantList matches;
	const QString needle = query.trimmed();
	if (needle.isEmpty()) {
		return matches;
	}
	for (int row = 0; row < m_rows.size(); ++row) {
		const ChatTimelineRow &candidate = m_rows.at(row);
		if (candidate.plainText.contains(needle, Qt::CaseInsensitive)
			|| candidate.authorName.contains(needle, Qt::CaseInsensitive)) {
			matches << row;
		}
	}
	return matches;
}

QList< int > ChatRowModel::changedRoles(const ChatTimelineRow &before, const ChatTimelineRow &after) {
	QList< int > roles;
	if (before.kind != after.kind) {
		roles << KindRole;
	}
	if (before.authorName != after.authorName) {
		roles << AuthorNameRole;
	}
	if (before.authorUserId != after.authorUserId) {
		roles << AuthorUserIdRole;
	}
	if (before.own != after.own) {
		roles << OwnRole;
	}
	if (before.timeText != after.timeText) {
		roles << TimeTextRole;
	}
	if (before.dateLabel != after.dateLabel) {
		roles << DateLabelRole;
	}
	if (before.groupStart != after.groupStart) {
		roles << GroupStartRole;
	}
	if ((before.editedAt > 0) != (after.editedAt > 0)) {
		roles << EditedRole;
	}
	if (before.bodyBlocks != after.bodyBlocks) {
		roles << BodyBlocksRole;
	}
	if (before.plainText != after.plainText) {
		roles << PlainTextRole;
	}
	if (before.media != after.media) {
		roles << MediaRole;
	}
	if (!(before.card == after.card)) {
		roles << CardRole;
	}
	if (before.reactions != after.reactions) {
		roles << ReactionsRole;
	}
	if (before.replyToMessageId != after.replyToMessageId) {
		roles << ReplyToMessageIdRole;
	}
	if (before.replyActor != after.replyActor) {
		roles << ReplyActorRole;
	}
	if (before.replySnippet != after.replySnippet) {
		roles << ReplySnippetRole;
	}
	if (before.deliveryState != after.deliveryState) {
		roles << DeliveryStateRole;
	}
	if (before.deliveryLabel != after.deliveryLabel) {
		roles << DeliveryLabelRole;
	}
	if (before.deliveryCanRetry != after.deliveryCanRetry) {
		roles << DeliveryCanRetryRole;
	}
	return roles;
}
