// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_EXPOSEEVENTFILTER_H_
#define MUMBLE_MUMBLE_EXPOSEEVENTFILTER_H_

#include <QtCore/QEvent>
#include <QtCore/QObject>

#include <functional>

class ExposeEventFilter final : public QObject {
public:
	ExposeEventFilter(QObject *parent, std::function< void() > callback);

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	std::function< void() > m_callback;
};

#endif
