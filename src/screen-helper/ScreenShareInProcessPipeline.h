// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREINPROCESSPIPELINE_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREINPROCESSPIPELINE_H_

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace ScreenShareInProcessPipeline {

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

// Options for the in-process publish pipeline (the --internal-gst-pipeline helper sub-mode).
struct Options {
	QStringList pipelineTokens; // gst_parse_launchv argument vector (no leading "-e")
	QString gstBinDir;          // bundled GStreamer bin directory (DLLs + plugins)

	// Window following: when windowHandle is set, the monitor crop of the d3d11screencapturesrc
	// named sourceName keeps tracking that top-level window.
	QString sourceName;
	quint64 windowHandle = 0;

	// Bitrate adaptation: when both names are set, the encoder's "bitrate" follows the GCC
	// estimate of the webrtcsink's consumer, clamped to bitrateKbps. When maxBitrateKbps is set,
	// "max-bitrate" is scaled by the same factor. The encoder properties are in kbit/s times
	// bitratePropertyScale (1000 for encoders that take bit/s).
	QString sinkName;
	QString encoderName;
	int bitrateKbps                = 0;
	int maxBitrateKbps             = 0;
	int bitratePropertyScale       = 1;
	bool costlyBitrateReconfigure = false; // the encoder re-opens on every bitrate change
};

// Runs the publish pipeline in-process, dynamically loading the bundled GStreamer runtime, until
// the pipeline ends, the tracked window closes or the process is asked to stop. Returns a process
// exit code.
int run(const Options &options);

} // namespace ScreenShareInProcessPipeline

#endif // MUMBLE_SCREENHELPER_SCREENSHAREINPROCESSPIPELINE_H_
