// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHARELINUXPORTALCAPTURE_H_
#define MUMBLE_SCREENHELPER_SCREENSHARELINUXPORTALCAPTURE_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

class ScreenShareLinuxPortalCapture {
public:
	struct Capability {
		bool portalAvailable      = false;
		bool pipeWireSrcAvailable = false;
		QString backendToken;
		QString detail;
		QStringList warnings;
	};

	struct NegotiationResult {
		bool valid       = false;
		QString errorMessage;
		quint32 nodeId   = 0;
		quint32 width    = 0;
		quint32 height   = 0;
		QString sourceType;
		// The org.freedesktop.portal.ScreenCast session handle (e.g.
		// "/org/freedesktop/portal/desktop/session/..."). Retained so the caller can release the
		// captured stream by closing the session via ScreenShareLinuxPortalCapture::close() once
		// the share is no longer active. Empty when unavailable.
		QString sessionHandle;
	};

	static QString backendToken();
	static Capability probe();
	static NegotiationResult negotiate(const QString &captureSourceId, int timeoutMsec = 60000);

	// Closes an active ScreenCast session, releasing the captured stream/pipewire node so the
	// compositor stops treating the screen as being shared. Session handles come from
	// NegotiationResult::sessionHandle. Safe to call with an empty handle or after the session was
	// already released.
	static void close(const QString &sessionHandle);
};

#endif // MUMBLE_SCREENHELPER_SCREENSHARELINUXPORTALCAPTURE_H_
