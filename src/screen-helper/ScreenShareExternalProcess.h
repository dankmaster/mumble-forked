// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

class QObject;
class QProcess;

class ScreenShareExternalProcess {
public:
	struct RuntimeSupport {
		QString ffmpegPath;
		QString ffplayPath;
		QString gstLaunchPath;
		QString gstInspectPath;
		QString gstreamerVersion;
		QString edgePath;
		QString chromePath;
		QString firefoxPath;
		bool ffmpegAvailable           = false;
		bool ffplayAvailable           = false;
		bool gstLaunchAvailable        = false;
		bool gstInspectAvailable       = false;
		bool gstreamerAvailable        = false;
		bool gstreamerLiveKitPublishAvailable = false;
		bool gstreamerLiveKitViewAvailable    = false;
		bool gstLiveKitWebRtcSinkAvailable    = false;
		bool gstLiveKitWebRtcSrcAvailable     = false;
		bool gstD3D11ScreenCaptureAvailable   = false;
		bool gstD3D11ConvertAvailable         = false;
		bool gstD3D11ScaleAvailable           = false;
		bool gstD3D11DownloadAvailable        = false;
		bool gstNvD3D11H264EncoderAvailable  = false;
		bool gstMfH264EncoderAvailable        = false;
		bool gstX264EncoderAvailable          = false;
		bool gstOpenH264EncoderAvailable      = false;
		bool gstH264ParseAvailable            = false;
		bool gstVideoTestSrcAvailable         = false;
		bool gstVideoConvertAvailable         = false;
		bool gstVideoScaleAvailable           = false;
		bool gstWasapi2SrcAvailable           = false;
		bool gstAudioConvertAvailable         = false;
		bool gstAudioResampleAvailable        = false;
		bool gstDecodeBinAvailable            = false;
		bool gstAutoVideoSinkAvailable        = false;
		bool gstAutoAudioSinkAvailable        = false;
		bool gstD3D11VideoSinkAvailable       = false;
		bool gstFakeSinkAvailable             = false;
		QStringList missingGStreamerElements;
		bool graphicalSessionAvailable = false;
		bool x11GrabAvailable          = false;
		bool gdigrabAvailable          = false;
		bool ddagrabAvailable          = false;
		bool lavfiAvailable            = false;
		bool x11DisplayAvailable       = false;
		bool windowedViewerAvailable   = false;
		bool d3d11HardwareDeviceAvailable           = false;
		bool windowsGraphicsCaptureAvailable        = false;
		bool windowsGraphicsCaptureFreeThreaded     = false;
		bool windowsGraphicsCaptureDirtyRegions     = false;
		bool windowsNativeCapturePipelineAvailable  = false;
		bool h264NvencAvailable        = false;
		bool h264VaapiAvailable        = false;
		bool h264MfAvailable           = false;
		bool h264QsvAvailable          = false;
		bool libx264Available          = false;
		bool av1NvencAvailable         = false;
		bool av1VaapiAvailable         = false;
		bool av1MfAvailable            = false;
		bool av1QsvAvailable           = false;
		bool libSvtAv1Available        = false;
		bool libVpxVp8Available        = false;
		bool libVpxVp9Available        = false;
		bool fileProtocolAvailable     = false;
		bool rtmpProtocolAvailable     = false;
		bool rtmpsProtocolAvailable    = false;
		bool edgeAvailable             = false;
		bool chromeAvailable           = false;
		bool firefoxAvailable          = false;
		bool browserWebRtcAvailable    = false;
	};

	struct LaunchResult {
		bool started  = false;
		bool usedStub = false;
		QString errorMessage;
		QString executionMode;
		QString endpointUrl;
		QString selectedEncoder;
		QString selectedCaptureSource;
		QString selectedRenderer;
		QString program;
		QStringList warnings;
		QProcess *process = nullptr;
	};

	static RuntimeSupport probeRuntimeSupport(bool refresh = false);
	static QJsonObject runtimeSupportToJson(const RuntimeSupport &support);
	static LaunchResult startPublish(const QJsonObject &plan, QObject *parent = nullptr);
	static LaunchResult startView(const QJsonObject &plan, QObject *parent = nullptr);
	static void stop(QProcess *process, int timeoutMsec = 2000);
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_
