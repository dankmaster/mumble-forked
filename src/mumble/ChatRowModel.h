// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATROWMODEL_H_
#define MUMBLE_MUMBLE_CHATROWMODEL_H_

#include "ChatTimelineRow.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QtGlobal>
#include <QtCore/QVariantMap>

// The v2 timeline is the default; MUMBLE_CHAT_TIMELINE_V2=0 brings back the
// previous timeline until it is removed (docs/chat-timeline-v2-plan.md, phase 6).
inline bool chatTimelineV2Enabled() {
	return !qEnvironmentVariableIsSet("MUMBLE_CHAT_TIMELINE_V2")
		   || qEnvironmentVariableIntValue("MUMBLE_CHAT_TIMELINE_V2") != 0;
}

// Typed rows of the v2 chat timeline, newest first: row 0 is the bottom of a
// ListView laid out bottom to top, live messages insert at row 0 and older
// history appends at the end. All updates arrive prepared from the worker;
// applying them only inserts, replaces or removes rows.
class ChatRowModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(QString scopeToken READ scopeToken NOTIFY scopeTokenChanged)
	// Conversation search, the same contract ConversationSearchBar uses with the
	// current timeline model. Matches are newest first; "next" goes older.
	Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchCountChanged)
	Q_PROPERTY(int currentMatchIndex READ currentMatchIndex NOTIFY currentMatchChanged)
	Q_PROPERTY(int currentMatchRow READ currentMatchRow NOTIFY currentMatchChanged)
	Q_PROPERTY(QString currentMatchStableId READ currentMatchStableId NOTIFY currentMatchChanged)

public:
	enum Role {
		MessageIdRole = Qt::UserRole + 1,
		ThreadIdRole,
		KindRole,
		AuthorNameRole,
		AuthorUserIdRole,
		OwnRole,
		CreatedAtRole,
		TimeTextRole,
		DateLabelRole,
		GroupStartRole,
		EditedRole,
		BodyBlocksRole,
		PlainTextRole,
		MediaRole,
		CardRole,
		ReactionsRole,
		ReplyToMessageIdRole,
		ReplyActorRole,
		ReplySnippetRole,
		DeliveryStateRole,
		DeliveryLabelRole,
		DeliveryCanRetryRole,
		StableIdRole,
		AvatarUrlRole,
		AttachmentsRole,
		PreviewRole,
		CanReplyRole,
		CanReactRole,
		CanDeleteRole,
	};

	explicit ChatRowModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;

	int count() const { return static_cast< int >(m_rows.size()); }
	QString scopeToken() const { return m_scopeToken; }

	void resetScope(const QString &scopeToken);
	// Inserts rows that are new and replaces rows that exist, in any order. A
	// replaced row reports only the roles that changed.
	void upsertRows(QVector< ChatTimelineRow > rows);
	void removeMessages(const QVector< quint64 > &identities);
	// Presentation state that still comes from the current message pipeline
	// (avatar, attachment and preview image sources, permissions) until the
	// v2 asset path replaces it. Takes that pipeline's message state; it may
	// arrive before or after the row itself. A "stableId" in the state replaces
	// the row's stableId role (direct messages are addressed by their own ids).
	void setExtras(const QVariantMap &messageState);

	const ChatTimelineRow &rowAt(int row) const { return m_rows.at(row); }
	Q_INVOKABLE int rowForMessage(quint32 threadId, quint32 messageId) const;
	// Row of the newest message with this message id (the stableId role), or -1.
	Q_INVOKABLE int rowForStableId(const QString &stableId) const;
	// Rows whose author or text contains the query, newest first.
	Q_INVOKABLE QVariantList rowsMatching(const QString &query) const;

	QString query() const { return m_query; }
	void setQuery(const QString &query);
	int matchCount() const { return static_cast< int >(m_matches.size()); }
	int currentMatchIndex() const { return m_currentMatch; }
	int currentMatchRow() const;
	QString currentMatchStableId() const;
	Q_INVOKABLE bool nextMatch();
	Q_INVOKABLE bool previousMatch();
	Q_INVOKABLE void clearSearch();

signals:
	void countChanged();
	void scopeTokenChanged();
	void queryChanged();
	void matchCountChanged();
	void currentMatchChanged();

private:
	int lowerBound(const ChatTimelineRowKey &key, int from = 0) const;
	void insertRun(int position, QVector< ChatTimelineRow > &run);
	static QList< int > changedRoles(const ChatTimelineRow &before, const ChatTimelineRow &after);
	void pruneExtras();
	static bool rowMatches(const ChatTimelineRow &row, const QString &needle);
	void refreshSearch();
	bool selectMatch(int index);

	struct Extras {
		QString stableId;
		QString avatarUrl;
		QVariantList attachments;
		QVariantMap preview;
		bool canReply   = false;
		bool canReact   = false;
		bool canDelete  = false;
		quint64 touched = 0;
	};

	QVector< ChatTimelineRow > m_rows;
	QHash< quint64, ChatTimelineRowKey > m_keyByIdentity;
	QString m_scopeToken;
	QHash< quint64, Extras > m_extras;
	quint64 m_extrasSequence = 0;
	QString m_query;
	QVector< quint64 > m_matches;
	int m_currentMatch = -1;
};

#endif // MUMBLE_MUMBLE_CHATROWMODEL_H_
