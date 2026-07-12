// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_CLIENTACTIONREGISTRY_H_
#define MUMBLE_MUMBLE_CLIENTACTIONREGISTRY_H_

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QVariantList>

class QAction;

class ClientActionList final : public QObject {
	Q_OBJECT
public:
	explicit ClientActionList(QObject *parent = nullptr);
	void addAction(QAction *action);
	QAction *addSeparator();
	void removeAction(QAction *action);
	void clear();
	QList< QAction * > actions() const;

private:
	QList< QAction * > m_actions;
};

class ClientActionRegistry final : public QObject {
	Q_OBJECT

public:
	explicit ClientActionRegistry(QObject *parent = nullptr);
	void adopt(QAction *action);
	QAction *action(const QString &id) const;
	QVariantList stateSnapshot() const;

signals:
	void actionStateChanged(const QString &id);

private:
	QHash< QString, QAction * > m_actions;
};

#endif
