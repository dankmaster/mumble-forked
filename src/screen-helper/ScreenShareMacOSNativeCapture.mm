// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareMacOSNativeCapture.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <Availability.h>
#include <dispatch/dispatch.h>

#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

class ScreenShareMacOSNativeCapturePrivate;
struct ScreenShareMacOSCaptureCallbackState {
	std::mutex mutex;
	ScreenShareMacOSNativeCapturePrivate *owner = nullptr;
};

@interface MumbleScreenCaptureKitDelegate : NSObject <SCStreamDelegate, SCStreamOutput> {
@public
	std::shared_ptr< ScreenShareMacOSCaptureCallbackState > callbackState;
}
@end

namespace {
constexpr unsigned int MAX_CAPTURE_WIDTH  = 7680;
constexpr unsigned int MAX_CAPTURE_HEIGHT = 4320;
constexpr unsigned int MAX_CAPTURE_FPS    = 120;
constexpr quint64 MAX_CAPTURE_FRAME_BYTES = 64ULL * 1024ULL * 1024ULL;

bool captureShapeIsSupported(const unsigned int width, const unsigned int height, const unsigned int fps) {
	if (width == 0 || height == 0 || fps == 0 || width > MAX_CAPTURE_WIDTH || height > MAX_CAPTURE_HEIGHT
		|| fps > MAX_CAPTURE_FPS) {
		return false;
	}
	return static_cast< quint64 >(width) * static_cast< quint64 >(height) * 4ULL
		   <= MAX_CAPTURE_FRAME_BYTES;
}

QString errorDescription(NSError *error) {
	return error ? QString::fromUtf8(error.localizedDescription.UTF8String) : QString();
}

bool parseSourceID(const QString &sourceID, QString *kind, quint32 *nativeID) {
	const QRegularExpression matchExpression(
		QStringLiteral("^(display|window)\\s*:\\s*([0-9]+)$"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = matchExpression.match(sourceID.trimmed());
	if (!match.hasMatch()) {
		return sourceID.trimmed().isEmpty() || sourceID.compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0
			   || sourceID.compare(QLatin1String("screen"), Qt::CaseInsensitive) == 0;
	}

	bool ok = false;
	const quint64 parsedID = match.captured(2).toULongLong(&ok);
	if (!ok || parsedID > std::numeric_limits< quint32 >::max()) {
		return false;
	}

	if (kind) {
		*kind = match.captured(1).toLower();
	}
	if (nativeID) {
		*nativeID = static_cast< quint32 >(parsedID);
	}
	return true;
}
} // namespace

class ScreenShareMacOSNativeCapturePrivate {
public:
	ScreenShareMacOSNativeCapturePrivate(ScreenShareMacOSNativeCapture *owner, QProcess *encoderProcess,
										 unsigned int width, unsigned int height, unsigned int fps,
										 QString captureSourceID)
		: q(owner), encoder(encoderProcess), captureWidth(width), captureHeight(height), captureFps(fps),
		  sourceID(std::move(captureSourceID)) {
		captureQueue = dispatch_queue_create("info.mumble.screen-capture-kit.frames", DISPATCH_QUEUE_SERIAL);
		callbackState = std::make_shared< ScreenShareMacOSCaptureCallbackState >();
		callbackState->owner = this;
		delegate = [MumbleScreenCaptureKitDelegate new];
		delegate->callbackState = callbackState;
	}

	~ScreenShareMacOSNativeCapturePrivate() {
		{
			const std::lock_guard lock(callbackState->mutex);
			callbackState->owner = nullptr;
		}
		if (@available(macOS 12.3, *)) {
			if (stream) {
				[stream stopCaptureWithCompletionHandler:nil];
				NSError *removeError = nil;
				[stream removeStreamOutput:delegate type:SCStreamOutputTypeScreen error:&removeError];
			}
		}
		if (captureQueue) {
			dispatch_sync(captureQueue, ^{});
		}
		stream = nil;
		delegate = nil;
	}

	void startAsynchronously() API_AVAILABLE(macos(12.3)) {
		QPointer< ScreenShareMacOSNativeCapture > guard(q);
		[SCShareableContent
			getShareableContentExcludingDesktopWindows:NO
							 onScreenWindowsOnly:NO
							 completionHandler:^(SCShareableContent *content, NSError *error) {
				dispatch_async(dispatch_get_main_queue(), ^{
					if (!guard) {
						return;
					}
					if (error || !content) {
						guard->m_private->fail(
							QStringLiteral("ScreenCaptureKit could not enumerate shareable content: %1")
								.arg(errorDescription(error)));
						return;
					}
					guard->m_private->configureAndStart(content);
				});
		}];
	}

	void configureAndStart(SCShareableContent *content) API_AVAILABLE(macos(12.3)) {
		QString sourceKind;
		quint32 requestedNativeID = 0;
		if (!parseSourceID(sourceID, &sourceKind, &requestedNativeID)) {
			fail(QStringLiteral("Unsupported macOS capture source '%1'. Use display:<id> or window:<id>.")
					 .arg(sourceID));
			return;
		}

		SCContentFilter *filter = nil;
		if (sourceKind == QLatin1String("window")) {
			for (SCWindow *window in content.windows) {
				if (window.windowID == requestedNativeID) {
					filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
					break;
				}
			}
			if (!filter) {
				fail(QStringLiteral("The requested macOS window capture source is no longer available."));
				return;
			}
		} else {
			SCDisplay *selectedDisplay = nil;
			const CGDirectDisplayID desiredDisplayID = requestedNativeID > 0
													 ? static_cast< CGDirectDisplayID >(requestedNativeID)
													 : CGMainDisplayID();
			for (SCDisplay *display in content.displays) {
				if (display.displayID == desiredDisplayID) {
					selectedDisplay = display;
					break;
				}
			}
			if (!selectedDisplay && content.displays.count > 0 && requestedNativeID == 0) {
				selectedDisplay = content.displays.firstObject;
			}
			if (!selectedDisplay) {
				fail(QStringLiteral("The requested macOS display capture source is no longer available."));
				return;
			}
			filter = [[SCContentFilter alloc] initWithDisplay:selectedDisplay excludingWindows:@[]];
		}

		SCStreamConfiguration *configuration = [SCStreamConfiguration new];
		configuration.width = static_cast< NSInteger >(captureWidth);
		configuration.height = static_cast< NSInteger >(captureHeight);
		configuration.minimumFrameInterval = CMTimeMake(1, static_cast< int32_t >(captureFps));
		configuration.queueDepth = 2;
		configuration.pixelFormat = kCVPixelFormatType_32BGRA;
		configuration.showsCursor = YES;

		stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:delegate];
		NSError *outputError = nil;
		if (![stream addStreamOutput:delegate type:SCStreamOutputTypeScreen sampleHandlerQueue:captureQueue
							 error:&outputError]) {
			fail(QStringLiteral("ScreenCaptureKit could not add its video output: %1")
					 .arg(errorDescription(outputError)));
			return;
		}

		QPointer< ScreenShareMacOSNativeCapture > guard(q);
		[stream startCaptureWithCompletionHandler:^(NSError *error) {
			if (!error) {
				return;
			}
			const QString message = errorDescription(error);
			QMetaObject::invokeMethod(
				guard.data(),
				[guard, message]() {
					if (guard) {
						guard->m_private->fail(
							QStringLiteral("ScreenCaptureKit failed to start capture: %1").arg(message));
					}
				},
				Qt::QueuedConnection);
		}];
	}

	void consumeSampleBuffer(CMSampleBufferRef sampleBuffer) {
		if (!sampleBuffer || !CMSampleBufferDataIsReady(sampleBuffer) || framePending.exchange(true)) {
			return;
		}

		CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
		if (!pixelBuffer || CVPixelBufferGetPixelFormatType(pixelBuffer) != kCVPixelFormatType_32BGRA
			|| CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
			framePending.store(false);
			return;
		}

		const size_t width = CVPixelBufferGetWidth(pixelBuffer);
		const size_t height = CVPixelBufferGetHeight(pixelBuffer);
		const size_t sourceStride = CVPixelBufferGetBytesPerRow(pixelBuffer);
		const size_t destinationStride = width * 4U;
		const size_t frameBytes = destinationStride * height;
		const void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
		if (!baseAddress || width != captureWidth || height != captureHeight
			|| frameBytes > static_cast< size_t >(std::numeric_limits< qsizetype >::max())) {
			CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
			framePending.store(false);
			return;
		}

		QByteArray frame(static_cast< qsizetype >(frameBytes), Qt::Uninitialized);
		const auto *source = static_cast< const char * >(baseAddress);
		for (size_t row = 0; row < height; ++row) {
			std::memcpy(frame.data() + static_cast< qsizetype >(row * destinationStride),
						source + row * sourceStride, destinationStride);
		}
		CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

		QPointer< QProcess > processGuard(encoder.data());
		QPointer< ScreenShareMacOSNativeCapture > ownerGuard(q);
		QMetaObject::invokeMethod(
			q,
			[ownerGuard, processGuard, frame = std::move(frame)]() mutable {
				if (!ownerGuard) {
					return;
				}
				auto &capture = *ownerGuard->m_private;
				QString writeError;
				if (processGuard && processGuard->state() != QProcess::NotRunning
					&& processGuard->bytesToWrite() == 0) {
					const qint64 accepted = processGuard->write(frame);
					if (accepted != frame.size()) {
						writeError = QStringLiteral(
							"The macOS capture encoder accepted only %1 of %2 frame bytes; stopping to preserve raw-video framing.")
								 .arg(accepted)
								 .arg(frame.size());
					}
				}
				capture.framePending.store(false);
				if (!writeError.isEmpty()) {
					capture.fail(writeError);
				}
			},
			Qt::QueuedConnection);
	}

	void fail(const QString &message) {
		qWarning().noquote() << message;
		if (encoder && encoder->state() != QProcess::NotRunning) {
			encoder->terminate();
		}
	}

	ScreenShareMacOSNativeCapture *q = nullptr;
	QPointer< QProcess > encoder;
	unsigned int captureWidth = 0;
	unsigned int captureHeight = 0;
	unsigned int captureFps = 0;
	QString sourceID;
	std::atomic_bool framePending{ false };
	std::shared_ptr< ScreenShareMacOSCaptureCallbackState > callbackState;
	dispatch_queue_t captureQueue = nullptr;
	__strong MumbleScreenCaptureKitDelegate *delegate = nil;
	__strong SCStream *stream = nil;
};

@implementation MumbleScreenCaptureKitDelegate
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
	Q_UNUSED(stream);
	const auto state = callbackState;
	const std::lock_guard lock(state->mutex);
	ScreenShareMacOSNativeCapturePrivate *capture = state->owner;
	if (capture) {
		const QString message = errorDescription(error);
		QMetaObject::invokeMethod(
			capture->q,
			[capture, message]() {
				if (capture) {
					capture->fail(QStringLiteral("ScreenCaptureKit stopped capture: %1").arg(message));
				}
			},
			Qt::QueuedConnection);
	}
}

- (void)stream:(SCStream *)stream
	didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
	ofType:(SCStreamOutputType)type {
	Q_UNUSED(stream);
	const auto state = callbackState;
	const std::lock_guard lock(state->mutex);
	if (type == SCStreamOutputTypeScreen && state->owner) {
		state->owner->consumeSampleBuffer(sampleBuffer);
	}
}
@end

ScreenShareMacOSNativeCapture::Capability ScreenShareMacOSNativeCapture::probe() {
	Capability capability;
	if (@available(macOS 12.3, *)) {
		capability.runtimeSupported = NSClassFromString(@"SCStream") != nil;
		capability.permissionGranted = CGPreflightScreenCaptureAccess();
		capability.permissionRequestRequired = capability.runtimeSupported && !capability.permissionGranted;
		capability.statusMessage = !capability.runtimeSupported
								   ? QStringLiteral("ScreenCaptureKit is unavailable in this macOS runtime.")
								   : (capability.permissionGranted
										  ? QStringLiteral("ScreenCaptureKit is available and Screen Recording permission is granted.")
										  : QStringLiteral("ScreenCaptureKit is available; Screen Recording permission will be requested when sharing starts."));
	} else {
		capability.statusMessage = QStringLiteral("ScreenCaptureKit requires macOS 12.3 or newer.");
	}
	return capability;
}

ScreenShareMacOSNativeCapture *ScreenShareMacOSNativeCapture::start(
	QProcess *encoderProcess, const unsigned int width, const unsigned int height, const unsigned int fps,
	const QString &captureSourceID, QString *errorMessage) {
	if (!encoderProcess || encoderProcess->state() == QProcess::NotRunning) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("The macOS capture encoder process is not running.");
		}
		return nullptr;
	}
	if (!captureShapeIsSupported(width, height, fps)) {
		if (errorMessage) {
			*errorMessage = QStringLiteral(
				"The requested macOS capture size or frame rate exceeds the bounded raw-frame budget.");
		}
		return nullptr;
	}

	const Capability capability = probe();
	if (!capability.runtimeSupported) {
		if (errorMessage) {
			*errorMessage = capability.statusMessage;
		}
		return nullptr;
	}

	if (!capability.permissionGranted) {
		const bool permissionWasGranted = CGRequestScreenCaptureAccess();
		if (errorMessage) {
			*errorMessage = permissionWasGranted
				? QStringLiteral(
					  "Screen Recording permission was granted. Restart Mumble so the capture helper can use the new permission.")
				: QStringLiteral(
					  "Screen Recording permission is required. Enable Mumble in System Settings > Privacy & Security > Screen Recording, then restart Mumble.");
		}
		return nullptr;
	}

	auto *capture = new ScreenShareMacOSNativeCapture(encoderProcess, width, height, fps, captureSourceID);
	if (!capture->begin(errorMessage)) {
		delete capture;
		return nullptr;
	}
	return capture;
}

ScreenShareMacOSNativeCapture::ScreenShareMacOSNativeCapture(
	QProcess *encoderProcess, const unsigned int width, const unsigned int height, const unsigned int fps,
	const QString &captureSourceID)
	: QObject(encoderProcess),
	  m_private(std::make_unique< ScreenShareMacOSNativeCapturePrivate >(this, encoderProcess, width, height, fps,
																 captureSourceID)) {
}

ScreenShareMacOSNativeCapture::~ScreenShareMacOSNativeCapture() = default;

bool ScreenShareMacOSNativeCapture::begin(QString *errorMessage) {
	if (@available(macOS 12.3, *)) {
		m_private->startAsynchronously();
		return true;
	}
	if (errorMessage) {
		*errorMessage = QStringLiteral("ScreenCaptureKit requires macOS 12.3 or newer.");
	}
	return false;
}
