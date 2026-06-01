// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareMediaSupport.h"

#include "ScreenShare.h"
#include "ScreenShareExternalProcess.h"

#include <QtCore/QLibrary>
#include <QtCore/QStringList>

namespace {
bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

void appendIf(QStringList *values, const bool condition, const QString &value) {
	if (condition && values && !values->contains(value)) {
		values->append(value);
	}
}

void appendCodecIf(QList< int > *values, const bool condition, const MumbleProto::ScreenShareCodec codec) {
	const int value = static_cast< int >(codec);
	if (condition && values && !values->contains(value)) {
		values->append(value);
	}
}

bool hasHardwareEncoder(const ScreenShareExternalProcess::RuntimeSupport &support) {
	return support.h264NvencAvailable || support.h264VaapiAvailable || support.h264MfAvailable
		   || support.h264QsvAvailable || support.av1NvencAvailable || support.av1VaapiAvailable
		   || support.av1MfAvailable || support.av1QsvAvailable || support.gstNvD3D11H264EncoderAvailable
		   || support.gstMfH264EncoderAvailable;
}

QList< int > executableCodecListForRuntime(const ScreenShareExternalProcess::RuntimeSupport &support,
										   const bool browserRelayFallbackEnabled) {
	QList< int > codecs;
	appendCodecIf(&codecs,
				  support.gstreamerLiveKitPublishAvailable || support.gstreamerLiveKitViewAvailable
					  || support.h264NvencAvailable || support.h264VaapiAvailable || support.h264MfAvailable
					  || support.h264QsvAvailable || support.libx264Available,
				  MumbleProto::ScreenShareCodecH264);
	appendCodecIf(&codecs,
				  support.av1NvencAvailable || support.av1VaapiAvailable || support.av1MfAvailable
					  || support.av1QsvAvailable || support.libSvtAv1Available,
				  MumbleProto::ScreenShareCodecAV1);
	appendCodecIf(&codecs, support.libVpxVp9Available, MumbleProto::ScreenShareCodecVP9);
	appendCodecIf(&codecs,
				  support.libVpxVp8Available || (browserRelayFallbackEnabled && support.browserWebRtcAvailable),
				  MumbleProto::ScreenShareCodecVP8);
	return Mumble::ScreenShare::sanitizeCodecList(codecs);
}

QStringList ingestProtocolsForRuntime(const ScreenShareExternalProcess::RuntimeSupport &support) {
	QStringList protocols;
	appendIf(&protocols, support.fileProtocolAvailable, QStringLiteral("file"));
	appendIf(&protocols, support.rtmpProtocolAvailable, QStringLiteral("rtmp"));
	appendIf(&protocols, support.rtmpsProtocolAvailable, QStringLiteral("rtmps"));
	appendIf(&protocols, support.gstreamerLiveKitPublishAvailable || support.gstreamerLiveKitViewAvailable,
			 QStringLiteral("webrtc"));
	return protocols;
}

#ifdef Q_OS_LINUX
bool pipeWireRuntimeAvailable(QString *libraryName) {
	const QStringList names{ QStringLiteral("libpipewire.so"), QStringLiteral("libpipewire-0.3.so"),
							 QStringLiteral("libpipewire-0.3.so.0") };

	for (const QString &name : names) {
		QLibrary lib(name);
		if (!lib.load()) {
			continue;
		}

		if (libraryName) {
			*libraryName = lib.fileName();
		}

		lib.unload();
		return true;
	}

	return false;
}
#endif
} // namespace

ScreenShareMediaSupport::CapabilitySummary ScreenShareMediaSupport::probe() {
	CapabilitySummary summary;
	summary.maxWidth        = Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH;
	summary.maxHeight       = Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT;
	summary.maxFps          = Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS;

	const ScreenShareExternalProcess::RuntimeSupport runtimeSupport = ScreenShareExternalProcess::probeRuntimeSupport();
	const bool testPatternEnabled =
		envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower()
			   == QLatin1String("test-pattern")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower() == QLatin1String("lavfi");
	const bool browserRelayFallbackEnabled = envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_RELAY_WEBAPP");
	summary.hardwareEncodeSupported   = hasHardwareEncoder(runtimeSupport);
	summary.hardwareEncodingPreferred = summary.hardwareEncodeSupported;
	summary.hardwareDecodeSupported   = runtimeSupport.gstD3D11VideoSinkAvailable
									  || (browserRelayFallbackEnabled && runtimeSupport.browserWebRtcAvailable);
	summary.ingestProtocols           = ingestProtocolsForRuntime(runtimeSupport);
	summary.gstreamerAvailable        = runtimeSupport.gstreamerAvailable;
	summary.gstreamerLiveKitPublishAvailable = runtimeSupport.gstreamerLiveKitPublishAvailable;
	summary.gstreamerLiveKitViewAvailable    = runtimeSupport.gstreamerLiveKitViewAvailable;
	summary.gstreamerVersion                 = runtimeSupport.gstreamerVersion;
	summary.missingGStreamerElements         = runtimeSupport.missingGStreamerElements;
	summary.supportedCodecs = executableCodecListForRuntime(runtimeSupport, browserRelayFallbackEnabled);

#ifdef Q_OS_LINUX
	QString libraryName;
	const bool pipeWireAvailable = pipeWireRuntimeAvailable(&libraryName);
	const bool x11CaptureAvailable =
		runtimeSupport.ffmpegAvailable && runtimeSupport.x11GrabAvailable && runtimeSupport.x11DisplayAvailable;
	const bool browserCaptureAvailable = browserRelayFallbackEnabled && runtimeSupport.browserWebRtcAvailable;

	summary.captureSupported = x11CaptureAvailable || browserCaptureAvailable || testPatternEnabled;
	summary.viewSupported =
		runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable || browserCaptureAvailable;
	appendIf(&summary.captureBackends, x11CaptureAvailable, QStringLiteral("x11grab"));
	appendIf(&summary.captureBackends, browserCaptureAvailable, QStringLiteral("browser-webrtc"));
	appendIf(&summary.captureBackends, testPatternEnabled && runtimeSupport.lavfiAvailable,
			 QStringLiteral("lavfi-test-pattern"));
	summary.captureBackend = testPatternEnabled
								 ? QStringLiteral("lavfi-test-pattern")
								 : (x11CaptureAvailable ? QStringLiteral("x11grab")
														: (browserCaptureAvailable ? QStringLiteral("browser-webrtc")
																				   : QStringLiteral("unavailable")));
	if (summary.captureSupported) {
		if (testPatternEnabled) {
			summary.statusMessage =
				QStringLiteral("ffmpeg test-pattern mode is enabled for headless screen-share verification.");
		} else if (browserCaptureAvailable && !x11CaptureAvailable) {
			summary.statusMessage =
				QStringLiteral("A graphical browser runtime is available for WebRTC relay screen sharing.");
		} else if (pipeWireAvailable) {
			summary.statusMessage = QStringLiteral("PipeWire runtime %1 detected, but the executable helper path "
												   "currently uses ffmpeg x11grab capture.")
										.arg(libraryName);
		} else {
			summary.statusMessage =
				QStringLiteral("ffmpeg x11grab desktop capture is available for the helper runtime.");
		}
	} else {
		summary.statusMessage = QStringLiteral("No executable Linux capture path is available. A graphical X11 session "
											   "or MUMBLE_SCREENSHARE_TEST_PATTERN=1 is required.");
	}
#elif defined(Q_OS_WIN)
	const bool gstreamerLiveKitCaptureAvailable =
		runtimeSupport.gstreamerLiveKitPublishAvailable && runtimeSupport.gstD3D11ScreenCaptureAvailable;
	const bool gdiCaptureAvailable     = runtimeSupport.ffmpegAvailable && runtimeSupport.gdigrabAvailable;
	const bool d3d11DesktopCaptureAvailable =
		runtimeSupport.ffmpegAvailable && runtimeSupport.ddagrabAvailable
		&& runtimeSupport.d3d11HardwareDeviceAvailable;
	const bool zeroCopyD3D11EncodeAvailable =
		d3d11DesktopCaptureAvailable && (runtimeSupport.h264NvencAvailable || runtimeSupport.av1NvencAvailable);
	const bool windowsGraphicsCaptureAvailable = runtimeSupport.windowsNativeCapturePipelineAvailable;
	const bool browserCaptureAvailable = browserRelayFallbackEnabled && runtimeSupport.browserWebRtcAvailable;

	summary.captureSupported =
		gstreamerLiveKitCaptureAvailable || windowsGraphicsCaptureAvailable || d3d11DesktopCaptureAvailable || gdiCaptureAvailable
		|| browserCaptureAvailable || testPatternEnabled;
	summary.viewSupported =
		runtimeSupport.gstreamerLiveKitViewAvailable || runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable
		|| browserCaptureAvailable;
	appendIf(&summary.captureBackends, gstreamerLiveKitCaptureAvailable,
			 QStringLiteral("gstreamer-d3d11-livekit"));
	appendIf(&summary.captureBackends, windowsGraphicsCaptureAvailable,
			 QStringLiteral("windows-graphics-capture-d3d11"));
	appendIf(&summary.captureBackends, d3d11DesktopCaptureAvailable, QStringLiteral("d3d11-desktop-duplication"));
	appendIf(&summary.captureBackends, gdiCaptureAvailable, QStringLiteral("gdigrab"));
	appendIf(&summary.captureBackends, browserCaptureAvailable, QStringLiteral("browser-webrtc"));
	appendIf(&summary.captureBackends, testPatternEnabled && runtimeSupport.lavfiAvailable,
			 QStringLiteral("lavfi-test-pattern"));
	summary.captureBackend =
		testPatternEnabled
			? QStringLiteral("lavfi-test-pattern")
			: (gstreamerLiveKitCaptureAvailable
				   ? QStringLiteral("gstreamer-d3d11-livekit")
				   : (zeroCopyD3D11EncodeAvailable
				   ? QStringLiteral("d3d11-desktop-duplication")
				   : (gdiCaptureAvailable
						  ? QStringLiteral("gdigrab")
						  : (windowsGraphicsCaptureAvailable ? QStringLiteral("windows-graphics-capture-d3d11")
															 : (browserCaptureAvailable ? QStringLiteral("browser-webrtc")
																						: QStringLiteral("unavailable"))))));
	summary.zeroCopySupported       = gstreamerLiveKitCaptureAvailable || zeroCopyD3D11EncodeAvailable || windowsGraphicsCaptureAvailable;
	summary.damageMetadataSupported = runtimeSupport.windowsGraphicsCaptureDirtyRegions;
	summary.queueBudgetFrames       = summary.zeroCopySupported ? 2 : summary.queueBudgetFrames;
	if (summary.captureSupported) {
		if (testPatternEnabled) {
			summary.statusMessage =
				QStringLiteral("ffmpeg test-pattern mode is enabled for headless screen-share verification.");
		} else if (gstreamerLiveKitCaptureAvailable) {
			summary.statusMessage =
				QStringLiteral("GStreamer LiveKit runtime is available and will prefer D3D11 screen capture with "
							   "H.264 hardware encoding for WebRTC screen sharing.");
		} else if (zeroCopyD3D11EncodeAvailable) {
			summary.statusMessage =
				QStringLiteral("Windows helper runtime will prefer D3D11 Desktop Duplication capture with NVENC "
							   "zero-copy encoding for direct relay publishing.");
		} else if (windowsGraphicsCaptureAvailable) {
			summary.statusMessage =
				QStringLiteral("Windows Graphics Capture and a hardware D3D11 device are available for the native "
							   "in-process publisher pipeline.");
		} else if (gdiCaptureAvailable) {
			summary.statusMessage =
				QStringLiteral("Windows helper runtime can capture the desktop via ffmpeg gdigrab and prefers "
							   "H.264-first encoding.");
		} else {
			summary.statusMessage =
				QStringLiteral("Windows helper runtime can execute WebRTC screen sharing through a dedicated browser "
							   "runtime.");
		}
	} else {
		summary.statusMessage =
			QStringLiteral("No executable Windows capture path is available. Install an ffmpeg build with ddagrab or "
						   "gdigrab support, or enable MUMBLE_SCREENSHARE_TEST_PATTERN=1 for verification.");
	}
#else
	summary.captureBackend = QStringLiteral("unsupported");
	summary.statusMessage =
		QStringLiteral("Screen-share media helper has no executable capture backend on this platform yet.");
	summary.viewSupported = runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable
							|| (browserRelayFallbackEnabled && runtimeSupport.browserWebRtcAvailable);
#endif
	if (summary.captureBackends.isEmpty() && !summary.captureBackend.isEmpty()
		&& summary.captureBackend != QLatin1String("unavailable")
		&& summary.captureBackend != QLatin1String("unsupported")) {
		summary.captureBackends.append(summary.captureBackend);
	}

	return summary;
}
