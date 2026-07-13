// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREMACOSNATIVECAPTURE_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREMACOSNATIVECAPTURE_H_

#include <QtCore/QObject>
#include <QtCore/QString>

#include <memory>

class QProcess;
class ScreenShareMacOSNativeCapturePrivate;

/// ScreenCaptureKit capture source for the external screen-share helper.
///
/// Frames are delivered as tightly packed BGRA to the stdin of the existing
/// encoder/relay process. Keeping the capture source in-process lets macOS use
/// ScreenCaptureKit while preserving the platform-neutral relay contract.
class ScreenShareMacOSNativeCapture final : public QObject {
public:
	struct Capability {
		bool runtimeSupported = false;
		bool permissionGranted = false;
		bool permissionRequestRequired = false;
		QString backend = QStringLiteral("screencapturekit-bgra");
		QString statusMessage;
	};

	/// Performs a non-interactive runtime and TCC preflight. This never displays
	/// a permission prompt and is therefore safe for capability probes and CI.
	static Capability probe();

	/// Starts capture and parents the capture lifetime to encoderProcess.
	/// captureSourceID accepts display:<CGDirectDisplayID>, window:<CGWindowID>,
	/// or an empty value for the main display.
	static ScreenShareMacOSNativeCapture *start(QProcess *encoderProcess, unsigned int width,
											 unsigned int height, unsigned int fps,
											 const QString &captureSourceID, QString *errorMessage = nullptr);

	~ScreenShareMacOSNativeCapture() override;

private:
	friend class ScreenShareMacOSNativeCapturePrivate;

	explicit ScreenShareMacOSNativeCapture(QProcess *encoderProcess, unsigned int width,
										 unsigned int height, unsigned int fps,
										 const QString &captureSourceID);

	bool begin(QString *errorMessage);

	std::unique_ptr< ScreenShareMacOSNativeCapturePrivate > m_private;
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREMACOSNATIVECAPTURE_H_
