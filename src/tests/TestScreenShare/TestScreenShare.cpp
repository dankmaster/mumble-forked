// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>
#include <QtCore/QUuid>
#include <QtCore/QSharedMemory>
#include <QtCore/QThread>
#include <QtGui/QColor>

#include <limits>

#include "ScreenShare.h"
#include "ScreenShareFrameTransport.h"
#include "ScreenShareViewBackend.h"
#include "ScreenShareOperationTracker.h"

#ifdef MUMBLE_TEST_SCREENCAPTUREKIT
#	include "ScreenShareMacOSNativeCapture.h"
#endif

namespace {
int codecValue(const MumbleProto::ScreenShareCodec codec) {
	return static_cast< int >(codec);
}
} // namespace

class TestScreenShare : public QObject {
	Q_OBJECT

private slots:
	void parsesAndFormatsVp8CodecPreferences();
	void keepsDirectDefaultH264First();
	void keepsWebRtcRelayH264First();
	void negotiatesWebRtcRelayWithLegacyFallback();
	void recommendsVp8Bitrate();
	void exposesPublisherQualityCeiling();
	void normalizesSecureRelayUrls();
	void rejectsUnsafeRelayUrls();
	void qmlViewBackendPublishesLifecycleState();
	void viewerAudioPreferencesUseStableSharerIdentity();
	void viewerAudioControlsCoalesceLatestStateOffGuiThread();
	void qmlViewBackendReportsExternalWindowTransportHonestly();
	void visualFixtureFrameInjectionActivatesNativeRendering();
	void qmlViewBackendKeepsReceiverAndFrameIdentityWhenPaused();
	void qmlViewBackendClearsNativeFrameForExternalFallback();
	void nativeFrameTransportIsBoundedAndTracksDrops();
	void qmlViewBackendConsumesNativeFramesOffThread();
	void qmlViewBackendCoalescesFramesWhileGuiThreadIsBusy();
	void rawBgraAssemblerKeepsFrameBoundariesAndDropsBacklog();
	void nativeFrameDecoderArgumentsKeepStdoutBinaryAndBounded();
	void helperOperationGenerationsRejectStaleAndCancelledResults();
	void macosScreenCaptureCapabilityProbeIsNonInteractive();
};

void TestScreenShare::macosScreenCaptureCapabilityProbeIsNonInteractive() {
#ifdef MUMBLE_TEST_SCREENCAPTUREKIT
	// probe() uses CGPreflightScreenCaptureAccess only. It must remain safe for
	// unsigned CI bundles and must never trigger the system permission dialog.
	const ScreenShareMacOSNativeCapture::Capability capability = ScreenShareMacOSNativeCapture::probe();
	QCOMPARE(capability.backend, QStringLiteral("screencapturekit-bgra"));
	QVERIFY(!capability.statusMessage.isEmpty());
	QCOMPARE(capability.permissionRequestRequired,
			 capability.runtimeSupported && !capability.permissionGranted);
#else
	QSKIP("ScreenCaptureKit capture is only built for the macOS screen helper.");
#endif
}

void TestScreenShare::qmlViewBackendPublishesLifecycleState() {
	ScreenShareSession session;
	session.streamID = QStringLiteral("stream:7");
	session.ownerSession = 42;
	session.scopeID = 9;
	session.captureAudio = true;
	ScreenShareViewBackend backend(session);
	QSignalSpy sessionSpy(&backend, &ScreenShareViewBackend::sessionChanged);
	QSignalSpy pauseSpy(&backend, &ScreenShareViewBackend::pauseToggled);
	QSignalSpy muteSpy(&backend, &ScreenShareViewBackend::audioMuteToggled);
	QSignalSpy volumeSpy(&backend, &ScreenShareViewBackend::audioVolumeAdjusted);
	QSignalSpy retrySpy(&backend, &ScreenShareViewBackend::retryRequested);
	QSignalSpy stopSpy(&backend, &ScreenShareViewBackend::stopRequested);
	QSignalSpy operationSpy(&backend, &ScreenShareViewBackend::operationStateChanged);

	QCOMPARE(backend.streamId(), QStringLiteral("stream:7"));
	QCOMPARE(backend.ownerLabel(), QStringLiteral("Session 42"));
	QCOMPARE(backend.roomLabel(), QStringLiteral("Voice room 9"));
	backend.setIdentity(QStringLiteral(" Alice "), QStringLiteral(" Lobby "));
	QCOMPARE(sessionSpy.count(), 1);
	QCOMPARE(backend.ownerLabel(), QStringLiteral("Alice"));
	QCOMPARE(backend.roomLabel(), QStringLiteral("Lobby"));
	QCOMPARE(backend.title(), QStringLiteral("Alice's screen share"));
	QCOMPARE(backend.detail(), QStringLiteral("Alice · Lobby"));
	QVERIFY(backend.audioAvailable());
	backend.setPaused(true);
	backend.setAudioMuted(true);
	backend.setAudioVolume(125);
	backend.requestRetry();
	backend.requestStop();
	backend.setOperationState(QStringLiteral("loading"), {}, true);
	QVERIFY(backend.paused());
	QVERIFY(backend.audioMuted());
	QCOMPARE(backend.audioVolume(), 100);
	QCOMPARE(pauseSpy.count(), 1);
	QCOMPARE(muteSpy.count(), 1);
	QCOMPARE(volumeSpy.count(), 0);
	QCOMPARE(retrySpy.count(), 1);
	QCOMPARE(retrySpy.constFirst().constFirst().toString(), QStringLiteral("stream:7"));
	QCOMPARE(stopSpy.count(), 1);
	QCOMPARE(operationSpy.count(), 1);
	QCOMPARE(backend.operationStatus(), QStringLiteral("loading"));
	QVERIFY(backend.operationCancellable());
	backend.setProcessId(77);
	backend.setProcessId(0);
	backend.setOperationState(QStringLiteral("error"), QStringLiteral("cleanup failed"), false);
	QCOMPARE(backend.processId(), 0);
	QCOMPARE(backend.operationStatus(), QStringLiteral("error"));
	QCOMPARE(backend.operationError(), QStringLiteral("cleanup failed"));
	QVERIFY(!backend.operationCancellable());
}

void TestScreenShare::viewerAudioPreferencesUseStableSharerIdentity() {
	const QString certificateKey = Mumble::ScreenShare::viewerAudioPreferenceKey(
		QStringLiteral(" A1B2C3 "), QByteArray::fromHex("0102"), QStringLiteral("First name"));
	QCOMPARE(certificateKey, QStringLiteral("certificate:a1b2c3"));
	QCOMPARE(certificateKey,
			 Mumble::ScreenShare::viewerAudioPreferenceKey(QStringLiteral("a1b2c3"), QByteArray::fromHex("ffff"),
													 QStringLiteral("Renamed user")));

	const QString fallbackKey = Mumble::ScreenShare::viewerAudioPreferenceKey(
		{}, QByteArray::fromHex("0102"), QStringLiteral(" Guest "));
	QVERIFY(fallbackKey.startsWith(QStringLiteral("server-user:")));
	QCOMPARE(fallbackKey,
			 Mumble::ScreenShare::viewerAudioPreferenceKey({}, QByteArray::fromHex("0102"), QStringLiteral("guest")));
	QVERIFY(fallbackKey != Mumble::ScreenShare::viewerAudioPreferenceKey(
		{}, QByteArray::fromHex("0304"), QStringLiteral("guest")));
	QVERIFY(Mumble::ScreenShare::viewerAudioPreferenceKey({}, QByteArray::fromHex("0102"), {}).isEmpty());

	ScreenShareSession session;
	session.streamID = QStringLiteral("stream:volume");
	ScreenShareViewBackend backend(session);
	QSignalSpy volumeSpy(&backend, &ScreenShareViewBackend::audioVolumeAdjusted);
	backend.setAudioVolume(37);
	QCOMPARE(volumeSpy.count(), 1);
	QCOMPARE(volumeSpy.constFirst().at(0).toString(), QStringLiteral("stream:volume"));
	QCOMPARE(volumeSpy.constFirst().at(1).toInt(), 37);
}

void TestScreenShare::viewerAudioControlsCoalesceLatestStateOffGuiThread() {
#ifdef Q_OS_WIN
	ScreenShareSession session;
	session.streamID = QStringLiteral("stream:audio-coalescing");
	session.captureAudio = true;
	ScreenShareViewBackend backend(session);
	QSignalSpy dispatchSpy(&backend, &ScreenShareViewBackend::audioControlsDispatchedForTest);
	QSignalSpy stateSpy(&backend, &ScreenShareViewBackend::audioControlStateChanged);

	backend.setProcessId(7331);
	backend.setAudioVolume(12);
	backend.setAudioVolume(38);
	backend.setAudioVolume(64);
	// No Core Audio enumeration or worker dispatch occurs inline with the GUI
	// setters. The single-shot debounce owns the first dispatch.
	QCOMPARE(dispatchSpy.count(), 0);
	QTRY_COMPARE_WITH_TIMEOUT(dispatchSpy.count(), 1, 300);
	QCOMPARE(dispatchSpy.at(0).at(0).toLongLong(), 7331);
	QVERIFY(!dispatchSpy.at(0).at(1).toBool());
	QCOMPARE(dispatchSpy.at(0).at(2).toInt(), 64);
	QVERIFY(stateSpy.count() > 0);
	QVERIFY(backend.audioControlStatus() != QStringLiteral("idle"));

	backend.setPaused(true);
	backend.setAudioVolume(71);
	backend.setAudioVolume(83);
	QCOMPARE(backend.processId(), 7331);
	QCOMPARE(dispatchSpy.count(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(dispatchSpy.count(), 2, 300);
	QCOMPARE(dispatchSpy.at(1).at(0).toLongLong(), 7331);
	QVERIFY(dispatchSpy.at(1).at(1).toBool());
	QCOMPARE(dispatchSpy.at(1).at(2).toInt(), 83);

	backend.setPaused(false);
	QCOMPARE(backend.processId(), 7331);
	QTRY_COMPARE_WITH_TIMEOUT(dispatchSpy.count(), 3, 300);
	QCOMPARE(dispatchSpy.at(2).at(0).toLongLong(), 7331);
	QVERIFY(!dispatchSpy.at(2).at(1).toBool());
	QCOMPARE(dispatchSpy.at(2).at(2).toInt(), 83);
#else
	QSKIP("Windows Core Audio coalescing is only active in the Windows client.");
#endif
}

void TestScreenShare::qmlViewBackendReportsExternalWindowTransportHonestly() {
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	QVERIFY(!backend.hasCurrentFrame());

	QCOMPARE(backend.renderTransport(), QStringLiteral("external-process-window"));
	QVERIFY(backend.nativeFrameTransportAvailable());
	QVERIFY(backend.nativeFrameTransportBlocker().isEmpty());
}

void TestScreenShare::visualFixtureFrameInjectionActivatesNativeRendering() {
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	QSignalSpy activeSpy(&backend, &ScreenShareViewBackend::nativeFrameActiveChanged);
	QSignalSpy frameSpy(&backend, &ScreenShareViewBackend::frameChanged);
	QImage fixtureFrame(3, 2, QImage::Format_RGBA8888);
	fixtureFrame.fill(QColor(32, 96, 192));

	backend.setVisualFixtureFrame(fixtureFrame);

	QVERIFY(backend.nativeFrameActive());
	QVERIFY(backend.hasCurrentFrame());
	QCOMPARE(backend.currentFrame().size(), QSize(3, 2));
	QCOMPARE(backend.currentFrame().pixelColor(1, 1), QColor(32, 96, 192));
	QCOMPARE(backend.renderTransport(), QStringLiteral("native-shared-memory-bgra"));
	QCOMPARE(activeSpy.count(), 1);
	QCOMPARE(frameSpy.count(), 1);
}

void TestScreenShare::qmlViewBackendKeepsReceiverAndFrameIdentityWhenPaused() {
	ScreenShareSession session;
	session.streamID = QStringLiteral("stream:pause");
	ScreenShareViewBackend backend(session);
	QImage fixtureFrame(3, 2, QImage::Format_RGBA8888);
	fixtureFrame.fill(QColor(32, 96, 192));
	backend.setVisualFixtureFrame(fixtureFrame);
	backend.setProcessId(7331);
	QVERIFY(backend.nativeFrameActive());
	QVERIFY(backend.hasCurrentFrame());
	QSignalSpy frameSpy(&backend, &ScreenShareViewBackend::frameChanged);
	QSignalSpy transportSpy(&backend, &ScreenShareViewBackend::nativeFrameActiveChanged);

	backend.setPaused(true);

	QVERIFY(backend.paused());
	QCOMPARE(backend.processId(), 7331);
	QVERIFY(backend.hasCurrentFrame());
	QVERIFY(backend.nativeFrameActive());
	QCOMPARE(backend.currentFrame().size(), QSize(3, 2));
	QCOMPARE(backend.renderTransport(), QStringLiteral("native-shared-memory-bgra"));
	QCOMPARE(frameSpy.count(), 0);
	QCOMPARE(transportSpy.count(), 0);
	backend.setPaused(false);
	QCOMPARE(backend.processId(), 7331);
	QVERIFY(backend.hasCurrentFrame());
	QVERIFY(backend.nativeFrameActive());
}

void TestScreenShare::qmlViewBackendClearsNativeFrameForExternalFallback() {
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	QImage fixtureFrame(3, 2, QImage::Format_RGBA8888);
	fixtureFrame.fill(QColor(32, 96, 192));
	backend.setVisualFixtureFrame(fixtureFrame);
	QVERIFY(backend.nativeFrameActive());
	QVERIFY(backend.hasCurrentFrame());

	backend.setNativeFrameTransport({}, 0);

	QVERIFY(!backend.hasCurrentFrame());
	QTRY_VERIFY(!backend.nativeFrameActive());
	QCOMPARE(backend.renderTransport(), QStringLiteral("external-process-window"));
}

void TestScreenShare::nativeFrameTransportIsBoundedAndTracksDrops() {
	const QString key = QStringLiteral("mumble-frame-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	Mumble::ScreenShare::FrameTransport consumer;
	QVERIFY(producer.create(key, 64));
	QVERIFY(consumer.attach(key));
	Mumble::ScreenShare::NativeFrame frame;
	frame.generation = 4;
	frame.width = 2;
	frame.height = 2;
	frame.stride = 8;
	frame.bgra = QByteArray(16, '\x7f');
	frame.sequence = 1;
	QVERIFY(producer.publish(frame));
	Mumble::ScreenShare::NativeFrame received;
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(received.sequence, 1ULL);
	frame.sequence = 4;
	QVERIFY(producer.publish(frame));
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(consumer.droppedFrames(), 2ULL);
	frame.generation = 5;
	frame.sequence = 1;
	frame.stride = 8;
	QVERIFY(producer.publish(frame));
	QVERIFY(consumer.readLatest(&received));
	QCOMPARE(received.generation, 5ULL);
	frame.stride = std::numeric_limits< quint32 >::max();
	QVERIFY(!producer.publish(frame));
	QCOMPARE(producer.droppedFrames(), 1ULL);
	producer.detach();
	QVERIFY(!producer.publish(frame));

	const QString malformedKey = key + QStringLiteral("-malformed");
	QSharedMemory malformed(malformedKey);
	QVERIFY(malformed.create(64));
	Mumble::ScreenShare::FrameTransport rejected;
	QVERIFY(!rejected.attach(malformedKey));
}

void TestScreenShare::qmlViewBackendConsumesNativeFramesOffThread() {
	const QString key = QStringLiteral("mumble-frame-backend-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	QVERIFY(producer.create(key, 64));
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	backend.setNativeFrameTransport(key, 9);
	QTRY_VERIFY(backend.nativeFrameActive());
	Mumble::ScreenShare::NativeFrame frame;
	frame.generation = 9;
	frame.width = 2;
	frame.height = 2;
	frame.stride = 8;
	frame.sequence = 1;
	frame.bgra = QByteArray(16, '\xff');
	QVERIFY(producer.publish(frame));
	QTRY_VERIFY(!backend.currentFrame().isNull());
	QVERIFY(backend.hasCurrentFrame());
	QCOMPARE(backend.currentFrame().size(), QSize(2, 2));
	QCOMPARE(backend.renderTransport(), QStringLiteral("native-shared-memory-bgra"));
	backend.setNativeFrameTransport({}, 0);
	QTRY_VERIFY(!backend.nativeFrameActive());
	QVERIFY(!backend.hasCurrentFrame());
}

void TestScreenShare::qmlViewBackendCoalescesFramesWhileGuiThreadIsBusy() {
	const QString key = QStringLiteral("mumble-frame-coalesce-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	QVERIFY(producer.create(key, 64));
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	backend.setNativeFrameTransport(key, 12);
	QTRY_VERIFY(backend.nativeFrameActive());
	QSignalSpy frameSpy(&backend, &ScreenShareViewBackend::frameChanged);

	auto publishFrame = [&producer](const quint64 sequence, const quint8 red) {
		Mumble::ScreenShare::NativeFrame frame;
		frame.generation = 12;
		frame.sequence = sequence;
		frame.width = 2;
		frame.height = 2;
		frame.stride = 8;
		frame.bgra.resize(16);
		for (int offset = 0; offset < frame.bgra.size(); offset += 4) {
			frame.bgra[offset] = 0;
			frame.bgra[offset + 1] = 0;
			frame.bgra[offset + 2] = static_cast< char >(red);
			frame.bgra[offset + 3] = static_cast< char >(0xff);
		}
		return producer.publish(frame);
	};

	QVERIFY(publishFrame(1, 16));
	QThread::msleep(40);
	for (quint64 sequence = 2; sequence <= 20; ++sequence) {
		QVERIFY(publishFrame(sequence, sequence == 20 ? 224 : 32));
		QThread::msleep(2);
	}
	QThread::msleep(40);

	QTRY_VERIFY(backend.hasCurrentFrame());
	QTRY_COMPARE(backend.currentFrame().pixelColor(0, 0).red(), 224);
	QVERIFY2(frameSpy.count() <= 2,
			 qPrintable(QStringLiteral("Expected at most one in-flight and one latest frame, got %1")
						.arg(frameSpy.count())));
}

void TestScreenShare::rawBgraAssemblerKeepsFrameBoundariesAndDropsBacklog() {
	Mumble::ScreenShare::RawBgraFrameAssembler assembler(8, 2);
	QCOMPARE(assembler.push(QByteArray("abcd", 4)).size(), 0);
	const QList< QByteArray > completed = assembler.push(QByteArray("efghijkl", 8));
	QCOMPARE(completed.size(), 1);
	QCOMPARE(completed.first(), QByteArray("abcdefgh", 8));
	QCOMPARE(assembler.bufferedBytes(), 4);

	const QByteArray burst("mnopqrstuvwx0123456789AB", 24);
	const QList< QByteArray > bounded = assembler.push(burst);
	QCOMPARE(bounded.size(), 2);
	QCOMPARE(assembler.droppedFrames(), 1ULL);
	QVERIFY(assembler.bufferedBytes() < 8);
	const QByteArray veryLargeBurst(1024 * 1024 + 3, '\x55');
	assembler.push(veryLargeBurst);
	QVERIFY(assembler.bufferedBytes() <= (2 * 8 + 7));
	QVERIFY(assembler.droppedFrames() > 1000);
	QCOMPARE(Mumble::ScreenShare::IPC::MINIMUM_PROTOCOL_VERSION, 1);
	QCOMPARE(Mumble::ScreenShare::IPC::PROTOCOL_VERSION, 2);
	QCOMPARE(Mumble::ScreenShare::IPC::makeRequest(Mumble::ScreenShare::IPC::Command::StartView, {}, 1)
				 .value(QStringLiteral("version")).toInt(), 1);
}

void TestScreenShare::nativeFrameDecoderArgumentsKeepStdoutBinaryAndBounded() {
	const QStringList ffmpeg = Mumble::ScreenShare::ffmpegRawBgraOutputArguments(1280, 720);
	QVERIFY(ffmpeg.contains(QStringLiteral("rawvideo")));
	QVERIFY(ffmpeg.contains(QStringLiteral("bgra")));
	QCOMPARE(ffmpeg.last(), QStringLiteral("pipe:1"));
	QVERIFY(ffmpeg.join(QLatin1Char(' ')).contains(QStringLiteral("scale=1280:720")));

	const QStringList gstreamer = Mumble::ScreenShare::gstreamerRawBgraSinkArguments(640, 360);
	QVERIFY(gstreamer.contains(QStringLiteral("fdsink")));
	QVERIFY(gstreamer.contains(QStringLiteral("fd=1")));
	QVERIFY(gstreamer.join(QLatin1Char(' ')).contains(QStringLiteral("format=BGRA,width=640,height=360")));
	QVERIFY(Mumble::ScreenShare::ffmpegRawBgraOutputArguments(0, 720).isEmpty());
}

void TestScreenShare::helperOperationGenerationsRejectStaleAndCancelledResults() {
	ScreenShareOperationTracker tracker;
	const quint64 first = tracker.begin(QStringLiteral("stream:1"));
	QVERIFY(tracker.isCurrent(QStringLiteral("stream:1"), first));
	const quint64 replacement = tracker.begin(QStringLiteral("stream:1"));
	QVERIFY(!tracker.isCurrent(QStringLiteral("stream:1"), first));
	QVERIFY(tracker.isCurrent(QStringLiteral("stream:1"), replacement));
	const quint64 cancelled = tracker.invalidate(QStringLiteral("stream:1"));
	QVERIFY(!tracker.isCurrent(QStringLiteral("stream:1"), replacement));
	QVERIFY(tracker.isCurrent(QStringLiteral("stream:1"), cancelled));
	QVERIFY(tracker.finishIfCurrent(QStringLiteral("stream:1"), cancelled));
	QVERIFY(!tracker.isCurrent(QStringLiteral("stream:1"), cancelled));
	QVERIFY(tracker.streamIDs().isEmpty());

	ScreenShareOperationTracker publishTracker;
	ScreenShareOperationTracker viewTracker;
	const quint64 publishStart = publishTracker.begin(QStringLiteral("stream:shared"));
	const quint64 viewStop = viewTracker.begin(QStringLiteral("stream:shared"));
	QVERIFY(publishTracker.isCurrent(QStringLiteral("stream:shared"), publishStart));
	QVERIFY(viewTracker.isCurrent(QStringLiteral("stream:shared"), viewStop));
	viewTracker.invalidate(QStringLiteral("stream:shared"));
	QVERIFY(publishTracker.isCurrent(QStringLiteral("stream:shared"), publishStart));
}

void TestScreenShare::parsesAndFormatsVp8CodecPreferences() {
	const QList< int > codecs =
		Mumble::ScreenShare::parseCodecPreferenceString(QStringLiteral("vp8 h264,av1 vp9 vp8 nope"));

	QCOMPARE(codecs, (QList< int >{
						 codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264),
						 codecValue(MumbleProto::ScreenShareCodecAV1), codecValue(MumbleProto::ScreenShareCodecVP9) }));
	QCOMPARE(Mumble::ScreenShare::codecPreferenceString(codecs), QStringLiteral("vp8 h264 av1 vp9"));
	QCOMPARE(Mumble::ScreenShare::codecToConfigToken(MumbleProto::ScreenShareCodecVP8), QStringLiteral("vp8"));
	QVERIFY(Mumble::ScreenShare::isValidCodec(MumbleProto::ScreenShareCodecVP8));
}

void TestScreenShare::keepsDirectDefaultH264First() {
	const QList< int > codecs = Mumble::ScreenShare::defaultCodecPreferenceList();

	QVERIFY(!codecs.isEmpty());
	QCOMPARE(codecs.first(), codecValue(MumbleProto::ScreenShareCodecH264));
	QVERIFY(codecs.contains(codecValue(MumbleProto::ScreenShareCodecVP8)));
}

void TestScreenShare::keepsWebRtcRelayH264First() {
	QCOMPARE(
		Mumble::ScreenShare::webRtcRelayCodecPreferenceList(),
		(QList< int >{ codecValue(MumbleProto::ScreenShareCodecH264), codecValue(MumbleProto::ScreenShareCodecAV1),
					   codecValue(MumbleProto::ScreenShareCodecVP9), codecValue(MumbleProto::ScreenShareCodecVP8) }));
}

void TestScreenShare::negotiatesWebRtcRelayWithLegacyFallback() {
	const QList< int > webRtcPreferences = Mumble::ScreenShare::webRtcRelayCodecPreferenceList();

	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8),
																	   codecValue(MumbleProto::ScreenShareCodecH264) }),
		MumbleProto::ScreenShareCodecH264);
	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8) }),
		MumbleProto::ScreenShareCodecVP8);
	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecH264),
																	   codecValue(MumbleProto::ScreenShareCodecAV1) }),
		MumbleProto::ScreenShareCodecH264);
}

void TestScreenShare::recommendsVp8Bitrate() {
	const unsigned int h264Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecH264, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);
	const unsigned int vp8Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecVP8, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);

	QCOMPARE(h264Bitrate, 4000U);
	QCOMPARE(vp8Bitrate, 4200U);
}

void TestScreenShare::exposesPublisherQualityCeiling() {
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH, 2560U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT, 1440U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS, 144U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH, 3840U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT, 2160U);
	QCOMPARE(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS, 144U);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH >= Mumble::ScreenShare::DEFAULT_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT >= Mumble::ScreenShare::DEFAULT_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS >= Mumble::ScreenShare::DEFAULT_MAX_FPS);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS >= Mumble::ScreenShare::PUBLISHER_QUALITY_MAX_FPS);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_WIDTH <= Mumble::ScreenShare::HARD_MAX_WIDTH);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_HEIGHT <= Mumble::ScreenShare::HARD_MAX_HEIGHT);
	QVERIFY(Mumble::ScreenShare::PUBLISHER_CAPTURE_MAX_FPS <= Mumble::ScreenShare::HARD_MAX_FPS);
}

void TestScreenShare::normalizesSecureRelayUrls() {
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral(" wss://relay.example.com/mumble/../room ")),
			 QStringLiteral("wss://relay.example.com/room"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("https://relay.example.com/mumble-screen")),
			 QStringLiteral("https://relay.example.com/mumble-screen"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("rtmps://relay.example.com/live/stream")),
			 QStringLiteral("rtmps://relay.example.com/live/stream"));
}

void TestScreenShare::rejectsUnsafeRelayUrls() {
	const QStringList rejected{
		QStringLiteral("file:///tmp/mumble-screen.mp4"),
		QStringLiteral("ws://relay.example.com/mumble-screen"),
		QStringLiteral("http://relay.example.com/mumble-screen"),
		QStringLiteral("rtmp://relay.example.com/live/stream"),
		QStringLiteral("wss:///missing-host"),
		QStringLiteral("wss://relay.example.com/mumble screen"),
		QStringLiteral("wss://relay.example.com/mumble!fakesink"),
		QStringLiteral("wss://relay.example.com/mumble\\fakesink"),
	};
	for (const QString &url : rejected) {
		QVERIFY2(Mumble::ScreenShare::normalizeRelayUrl(url).isEmpty(), qPrintable(url));
	}
}

QTEST_GUILESS_MAIN(TestScreenShare)
#include "TestScreenShare.moc"
