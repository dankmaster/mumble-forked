// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLWINDOWSTATESTORE_H_
#define MUMBLE_MUMBLE_QMLWINDOWSTATESTORE_H_

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QString>

class QmlWindowStateController;
class QWindow;

/// Persists independent geometry for the detached Qt Quick product surfaces.
///
/// A single QQuickWindow can host several logical product dialogs over its
/// lifetime. The logical key therefore belongs to the state entry rather than
/// the native window. Every entry uses QmlWindowStateController for safe
/// monitor, work-area and DPI restoration.
class QmlWindowStateStore final : public QObject {
	Q_OBJECT

public:
	explicit QmlWindowStateStore(const QByteArray &encodedStates = {}, QObject *parent = nullptr);
	~QmlWindowStateStore() override;

	Q_INVOKABLE bool restoreWindow(QObject *windowObject, const QString &logicalKey,
							   int minimumWidth = 320, int minimumHeight = 240);
	Q_INVOKABLE void flushWindow(QObject *windowObject);
	void flush();

	QByteArray encodedStates() const;
	static QByteArray encode(const QHash< QString, QByteArray > &states);
	static QHash< QString, QByteArray > decode(const QByteArray &encodedStates);

signals:
	void encodedStatesChanged(const QByteArray &encodedStates);

private:
	struct Attachment {
		QString key;
		QmlWindowStateController *controller = nullptr;
		QMetaObject::Connection destroyedConnection;
	};

	static QString normalizedKey(const QString &logicalKey);
	void detachWindow(QWindow *window, bool flushFirst);
	void recordState(const QString &key, const QByteArray &state);

	QHash< QString, QByteArray > m_states;
	QHash< QWindow *, Attachment > m_attachments;
};

#endif
