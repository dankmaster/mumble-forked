// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_CONNECTIONFAILTYPES_H_
#define MUMBLE_MUMBLE_CONNECTIONFAILTYPES_H_

#include <QtCore/QString>

enum class ConnectionFailType {
	InvalidUsername,
	UsernameAlreadyInUse,
	AuthenticationFailure,
	InvalidServerPassword,
};

struct ConnectDetails {
	QString host;
	unsigned short port = 0;
	QString username;
	QString password;
};

#endif
