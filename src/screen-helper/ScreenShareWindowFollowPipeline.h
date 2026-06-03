// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREWINDOWFOLLOWPIPELINE_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREWINDOWFOLLOWPIPELINE_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace ScreenShareWindowFollow {

// Monitor-relative crop rectangle for a tracked top-level window. The coordinates match the
// d3d11screencapturesrc "monitor-handle" + "crop-x/crop-y/crop-width/crop-height" properties.
struct WindowCrop {
	bool valid              = false;
	quint64 monitorHandle   = 0;
	int cropX               = 0;
	int cropY               = 0;
	int cropWidth           = 0;
	int cropHeight          = 0;
};

// Computes the current monitor-relative crop for the given native window handle. Returns an
// invalid result on non-Windows builds or when the window/monitor geometry cannot be resolved.
WindowCrop computeWindowCrop(quint64 windowHandle);

// Options for the in-process window-following capture pipeline (the
// --internal-gst-window-follow helper sub-mode).
struct Options {
	QStringList pipelineTokens; // gst_parse_launchv argument vector (no leading "-e")
	QString sourceName;         // name= of the d3d11screencapturesrc to retarget
	quint64 windowHandle = 0;   // tracked top-level window
	QString gstBinDir;          // bundled GStreamer bin directory (DLLs + plugins)
};

// Runs the capture pipeline in-process, dynamically loading the bundled GStreamer runtime, and
// keeps the crop following the tracked window until the pipeline ends or the window closes.
// Returns a process exit code.
int run(const Options &options);

} // namespace ScreenShareWindowFollow

#endif // MUMBLE_SCREENHELPER_SCREENSHAREWINDOWFOLLOWPIPELINE_H_
