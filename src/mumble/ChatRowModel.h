// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATROWMODEL_H_
#define MUMBLE_MUMBLE_CHATROWMODEL_H_

#include "ChatTimelineRow.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>

// Typed rows of the v2 chat timeline, newest first: row 0 is the bottom of a
// ListView laid out bottom to top, live messages insert at row 0 and older
// history appends at the end. All updates arrive prepared from the worker;
// applying them only inserts, replaces or removes rows.
class ChatRowModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(QString scopeToken READ scopeToken NOTIFY scopeTokenChanged)

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

	const ChatTimelineRow &rowAt(int row) const { return m_rows.at(row); }
	Q_INVOKABLE int rowForMessage(quint32 threadId, quint32 messageId) const;
	// Rows whose author or text contains the query, newest first.
	Q_INVOKABLE QVariantList rowsMatching(const QString &query) const;

signals:
	void countChanged();
	void scopeTokenChanged();

private:
	int lowerBound(const ChatTimelineRowKey &key, int from = 0) const;
	void insertRun(int position, QVector< ChatTimelineRow > &run);
	static QList< int > changedRoles(const ChatTimelineRow &before, const ChatTimelineRow &after);

	QVector< ChatTimelineRow > m_rows;
	QHash< quint64, ChatTimelineRowKey > m_keyByIdentity;
	QString m_scopeToken;
};

#endif // MUMBLE_MUMBLE_CHATROWMODEL_H_
