// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#ifndef MUMBLE_SCREENHELPER_SCREENSHAREPROCESSOWNER_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREPROCESSOWNER_H_

#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>
#include <memory>

// Keeps the actual Windows process handle, not just its recyclable numeric PID.
class ScreenShareProcessOwner final : public QObject {
public:
	explicit ScreenShareProcessOwner(QObject *parent = nullptr);
	~ScreenShareProcessOwner() override;
	static QString identityForProcess(qint64 processID);
	bool watch(qint64 processID, const QString &identity, std::function< void() > exited, QString *error);

private:
	struct Implementation;
	std::unique_ptr< Implementation > m_impl;
};

#endif
