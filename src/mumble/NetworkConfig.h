// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_NETWORKCONFIG_H_
#define MUMBLE_MUMBLE_NETWORKCONFIG_H_

class QNetworkReply;
class QNetworkRequest;
class QUrl;

class NetworkConfig final {
public:
	static void SetupProxy();
	static bool TcpModeEnabled();
	static bool ApplyStartWithPCRegistration(bool enabled);
};

namespace Network {
void prepareRequest(QNetworkRequest &);
QNetworkReply *get(const QUrl &);
} // namespace Network

#endif
