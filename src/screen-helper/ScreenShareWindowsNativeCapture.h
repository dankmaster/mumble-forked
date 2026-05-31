// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREWINDOWSNATIVECAPTURE_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREWINDOWSNATIVECAPTURE_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

class ScreenShareWindowsNativeCapture {
public:
	struct Capability {
		bool graphicsCaptureSupported        = false;
		bool d3d11HardwareDeviceAvailable    = false;
		bool freeThreadedFramePoolSupported  = false;
		bool dirtyRegionMetadataSupported    = false;
		bool inProcessCapturePipelinePlanned = false;
		QString backendToken;
		QString detail;
		QStringList warnings;
	};

	static QString backendToken();
	static Capability probe();
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREWINDOWSNATIVECAPTURE_H_
