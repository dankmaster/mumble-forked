// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShare.h"
#include "ScreenShareProcessJob.h"
#include "ScreenShareProcessOwner.h"
#include "ScreenShareHelperServer.h"
#include "ScreenShareIPC.h"
#include "ScreenShareSessionPlanner.h"
#include "ScreenShareRelayClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#include <cstdio>

namespace {
QList< QPointer< QProcess > > helperChildren;
int codecValue(const MumbleProto::ScreenShareCodec codec) {
	return static_cast< int >(codec);
}

ScreenShareExternalProcess::LaunchResult prepareTestChild(QObject *parent, const QString &mode,
														 const bool nativeFrames = false) {
	ScreenShareExternalProcess::LaunchResult launch;
	launch.prepared = true;
	launch.process = new QProcess(parent);
	launch.process->setProgram(QCoreApplication::applicationFilePath());
	launch.process->setArguments({ QStringLiteral("--screen-share-test-child"), mode });
	launch.nativeFrameFeed = nativeFrames;
	launch.nativeFrameWidth = 1;
	launch.nativeFrameHeight = 1;
	return launch;
}

ScreenShareProcessJob::Deadlines testDeadlines() {
	ScreenShareProcessJob::Deadlines deadlines;
	deadlines.start = 3000;
	deadlines.frame = 350;
	deadlines.settle = 80;
	deadlines.stop = 100;
	deadlines.total = 5000;
	return deadlines;
}

QString fixtureSocketName() {
	return QStringLiteral("mumble-screen-protocol-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QJsonObject fixturePayload(const QString &mode = QStringLiteral("fragmented")) {
	return { { QStringLiteral("stream_id"), QStringLiteral("fixture-stream") },
		{ QStringLiteral("native_frame_requested"), true }, { QStringLiteral("fixture_mode"), mode } };
}

QJsonObject ownedPayload(qint64 processID, quint64 sequence, const QString &mode = QStringLiteral("fragmented")) {
	auto payload = fixturePayload(mode);
	payload.insert(QStringLiteral("owner_process_id"), QString::number(processID));
	payload.insert(QStringLiteral("owner_process_identity"), ScreenShareProcessOwner::identityForProcess(processID));
	payload.insert(QStringLiteral("owner_operation_sequence"), QString::number(sequence));
	return payload;
}

void sendFixtureRequest(QLocalSocket &socket, const QString &name, Mumble::ScreenShare::IPC::Command command,
	const QJsonObject &payload, int version = Mumble::ScreenShare::IPC::PROTOCOL_VERSION) {
	const QByteArray data = QJsonDocument(Mumble::ScreenShare::IPC::makeRequest(command, payload, version))
		.toJson(QJsonDocument::Compact) + '\n';
	QObject::connect(&socket, &QLocalSocket::connected, &socket, [&socket, data]() { socket.write(data); });
	socket.connectToServer(Mumble::ScreenShare::IPC::socketPath(name));
}

QJsonObject readFixtureReply(QLocalSocket &socket) {
	return QJsonDocument::fromJson(socket.readLine()).object();
}

bool stopped(const QPointer< QProcess > &process) {
	return !process || process->state() == QProcess::NotRunning;
}
} // namespace

// Replace media planning/launching only in this test executable. The production
// server, IPC sockets, owner handles and process-job state machine are unchanged.
ScreenShareMediaSupport::CapabilitySummary ScreenShareMediaSupport::probe() { return {}; }
ScreenShareExternalProcess::RuntimeSupport ScreenShareExternalProcess::probeRuntimeSupport(bool) { return {}; }
QJsonObject ScreenShareExternalProcess::runtimeSupportToJson(const RuntimeSupport &) { return {}; }
ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::prepareView(const QJsonObject &plan, QObject *parent) {
	auto launch = prepareTestChild(parent, plan.value(QStringLiteral("fixture_mode")).toString(),
		plan.value(QStringLiteral("native_frame_requested")).toBool());
	helperChildren.append(launch.process);
	return launch;
}
ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::preparePublish(const QJsonObject &plan, QObject *parent) {
	return prepareView(plan, parent);
}
ScreenShareSessionPlanner::Plan ScreenShareSessionPlanner::planView(const QJsonObject &payload,
	const ScreenShareMediaSupport::CapabilitySummary &) {
	return { !payload.value(QStringLiteral("stream_id")).toString().isEmpty(), {}, payload };
}
ScreenShareSessionPlanner::Plan ScreenShareSessionPlanner::planPublish(const QJsonObject &payload,
	const ScreenShareMediaSupport::CapabilitySummary &capabilities) { return planView(payload, capabilities); }
QJsonArray ScreenShareSessionPlanner::advertisedEncoderBackends() { return {}; }
QJsonArray ScreenShareRelayClient::advertisedContracts() { return {}; }
QJsonArray ScreenShareRelayClient::runtimeRelayTransports() { return {}; }
QJsonArray ScreenShareRelayClient::signalingRelayTransports() { return {}; }

class TestScreenShareProtocol : public QObject {
	Q_OBJECT

private slots:
	void init() { helperChildren.clear(); }
	void sanitizesRelayEndpointsUsedByTheServer();
	void negotiatesBoundedServerParameters();
	void cancelsBeforeQueuedLaunch();
	void cancelledReplacementKeepsPredecessorBarrier();
	void firstFrameRequiresAllBytes();
	void incompleteFrameTimesOutWithoutBlockingEventLoop();
	void failedProcessRetriesAsynchronously();
	void stopDuringProcessStartCompletesOnce();
	void overallDeadlineBoundsStartup();
	void normalReplySocketCloseRetainsSession_data();
	void normalReplySocketCloseRetainsSession();
	void pendingSocketDisconnectStopsSession();
	void ownerExitStopsRunningSession_data();
	void ownerExitStopsRunningSession();
	void invalidOwnerCannotStartOrReplaceSession();
	void reorderedOwnerOperationsDoNotResurrectOrStopNewerSession();
};

void TestScreenShareProtocol::sanitizesRelayEndpointsUsedByTheServer() {
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral(" wss://relay.example.com/mumble/../room ")),
			 QStringLiteral("wss://relay.example.com/room"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("https://relay.example.com/mumble-screen")),
			 QStringLiteral("https://relay.example.com/mumble-screen"));
	QCOMPARE(Mumble::ScreenShare::normalizeRelayUrl(QStringLiteral("rtmps://relay.example.com/live/stream")),
			 QStringLiteral("rtmps://relay.example.com/live/stream"));

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

void TestScreenShareProtocol::negotiatesBoundedServerParameters() {
	const QList< int > webRtcPreferences = Mumble::ScreenShare::webRtcRelayCodecPreferenceList();
	QCOMPARE(Mumble::ScreenShare::selectPreferredCodec(
			 webRtcPreferences,
			 { codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264) }),
		 MumbleProto::ScreenShareCodecH264);
	QCOMPARE(Mumble::ScreenShare::selectPreferredCodec(
			 webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8) }),
		 MumbleProto::ScreenShareCodecVP8);

	QCOMPARE(Mumble::ScreenShare::negotiateLimit(3840, 1920, 1280, 1280, 3840), 1280U);
	QCOMPARE(Mumble::ScreenShare::negotiateLimit(0, 0, 0, 720, 2160), 720U);
	QCOMPARE(Mumble::ScreenShare::sanitizeBitrateKbps(
			 Mumble::ScreenShare::HARD_MAX_BITRATE_KBPS + 1,
			 MumbleProto::ScreenShareCodecH264,
			 Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
			 Mumble::ScreenShare::DEFAULT_MAX_HEIGHT,
			 Mumble::ScreenShare::DEFAULT_MAX_FPS),
		 Mumble::ScreenShare::HARD_MAX_BITRATE_KBPS);
}

void TestScreenShareProtocol::cancelsBeforeQueuedLaunch() {
	int launches = 0;
	int completions = 0;
	int terminations = 0;
	ScreenShareProcessJob job([&](int, QObject *) { ++launches; return ScreenShareExternalProcess::LaunchResult{}; });
	job.terminated = [&]() { ++terminations; };
	job.start([&](bool success, const QString &) { QVERIFY(!success); ++completions; });
	job.cancel();
	QCoreApplication::processEvents();
	QCOMPARE(launches, 0);
	QCOMPARE(completions, 1);
	QCOMPARE(terminations, 1);
	QCOMPARE(job.state(), ScreenShareProcessJob::State::Terminal);
	job.cancel();
	QCOMPARE(terminations, 1);
}

void TestScreenShareProtocol::cancelledReplacementKeepsPredecessorBarrier() {
	int launches = 0;
	int terminations = 0;
	ScreenShareProcessJob replacement([&](int, QObject *) { ++launches; return ScreenShareExternalProcess::LaunchResult{}; });
	replacement.terminated = [&]() { ++terminations; };
	replacement.start([](bool success, const QString &) { QVERIFY(!success); }, true);
	replacement.cancel();
	QCoreApplication::processEvents();
	QCOMPARE(launches, 0);
	QCOMPARE(terminations, 0);
	QCOMPARE(replacement.state(), ScreenShareProcessJob::State::Stopping);
	// A newer replacement may only start after the original process is gone.
	replacement.resumeStart();
	QCOMPARE(terminations, 1);
	QCOMPARE(replacement.state(), ScreenShareProcessJob::State::Terminal);
}

void TestScreenShareProtocol::firstFrameRequiresAllBytes() {
	int completions = 0;
	bool succeeded = false;
	ScreenShareProcessJob job([](int, QObject *parent) { return prepareTestChild(parent, QStringLiteral("fragmented"), true); });
	auto deadlines = testDeadlines();
	deadlines.frame = 2500;
	job.setDeadlines(deadlines);
	job.start([&](bool success, const QString &) { ++completions; succeeded = success; });
	QTRY_COMPARE_WITH_TIMEOUT(job.state(), ScreenShareProcessJob::State::AwaitingFrame, 3000);
	QTest::qWait(30);
	QCOMPARE(completions, 0);
	QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 3000);
	QVERIFY(succeeded);
	QCOMPARE(job.state(), ScreenShareProcessJob::State::Running);
	Mumble::ScreenShare::FrameTransport reader;
	QVERIFY(reader.attach(job.frameKey()));
	Mumble::ScreenShare::NativeFrame frame;
	QVERIFY(reader.readLatest(&frame));
	QCOMPARE(frame.bgra, QByteArray("BGRA", 4));
	QCOMPARE(frame.generation, job.frameGeneration());
	job.cancel();
	QTRY_COMPARE_WITH_TIMEOUT(job.state(), ScreenShareProcessJob::State::Terminal, 3000);
	QCOMPARE(completions, 1);
}

void TestScreenShareProtocol::incompleteFrameTimesOutWithoutBlockingEventLoop() {
	int completions = 0;
	int ticks = 0;
	QString error;
	QTimer heartbeat;
	connect(&heartbeat, &QTimer::timeout, this, [&]() { ++ticks; });
	heartbeat.start(10);
	ScreenShareProcessJob job([](int, QObject *parent) { return prepareTestChild(parent, QStringLiteral("partial"), true); });
	job.setDeadlines(testDeadlines());
	job.start([&](bool success, const QString &message) { QVERIFY(!success); ++completions; error = message; });
	QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 5000);
	QVERIFY(error.contains(QStringLiteral("complete frame")));
	QVERIFY(ticks >= 5);
	QCOMPARE(job.state(), ScreenShareProcessJob::State::Terminal);
}

void TestScreenShareProtocol::failedProcessRetriesAsynchronously() {
	int attempts = 0;
	int completions = 0;
	bool succeeded = false;
	ScreenShareProcessJob job([&](int attempt, QObject *parent) {
		++attempts;
		auto launch = prepareTestChild(parent, QStringLiteral("hold"));
		if (attempt == 0) {
			launch.process->setProgram(QCoreApplication::applicationFilePath() + QStringLiteral(".missing"));
			launch.hasNextAttempt = true;
		}
		return launch;
	});
	job.setDeadlines(testDeadlines());
	job.start([&](bool success, const QString &) { ++completions; succeeded = success; });
	QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 5000);
	QCOMPARE(attempts, 2);
	QVERIFY(succeeded);
	job.cancel();
	QTRY_COMPARE_WITH_TIMEOUT(job.state(), ScreenShareProcessJob::State::Terminal, 3000);
	QCOMPARE(completions, 1);
}

void TestScreenShareProtocol::stopDuringProcessStartCompletesOnce() {
	int completions = 0;
	int terminations = 0;
	QPointer< QProcess > child;
	ScreenShareProcessJob job([&](int, QObject *parent) {
		auto launch = prepareTestChild(parent, QStringLiteral("hold"));
		child = launch.process;
		return launch;
	});
	job.setDeadlines(testDeadlines());
	job.terminated = [&]() { ++terminations; };
	job.start([&](bool success, const QString &) { QVERIFY(!success); ++completions; });
	QTimer::singleShot(0, &job, [&]() { job.cancel(); job.cancel(); });
	QTRY_COMPARE_WITH_TIMEOUT(terminations, 1, 3000);
	QVERIFY(!child || child->state() == QProcess::NotRunning);
	QCOMPARE(completions, 1);
}

void TestScreenShareProtocol::overallDeadlineBoundsStartup() {
	int completions = 0;
	QString error;
	ScreenShareProcessJob job([](int, QObject *parent) { return prepareTestChild(parent, QStringLiteral("hold")); });
	auto deadlines = testDeadlines();
	deadlines.settle = 10000;
	deadlines.total = 200;
	job.setDeadlines(deadlines);
	job.start([&](bool success, const QString &message) { QVERIFY(!success); ++completions; error = message; });
	QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 3000);
	QVERIFY(error.contains(QStringLiteral("overall deadline")));
	QCOMPARE(job.state(), ScreenShareProcessJob::State::Terminal);
}

void TestScreenShareProtocol::normalReplySocketCloseRetainsSession_data() {
	QTest::addColumn< int >("version");
	QTest::addColumn< bool >("publish");
	for (const int version : { 1, 2 }) {
		for (const bool publish : { false, true }) {
			QTest::newRow(qPrintable(QStringLiteral("v%1-%2").arg(version).arg(publish ? "publish" : "view")))
				<< version << publish;
		}
	}
}

void TestScreenShareProtocol::normalReplySocketCloseRetainsSession() {
	QFETCH(int, version);
	QFETCH(bool, publish);
	using Command = Mumble::ScreenShare::IPC::Command;
	ScreenShareHelperServer server;
	const QString name = fixtureSocketName();
	QString error;
	QVERIFY2(server.start(name, &error), qPrintable(error));
	QLocalSocket start;
	sendFixtureRequest(start, name, publish ? Command::StartPublish : Command::StartView, fixturePayload(), version);
	QTRY_VERIFY_WITH_TIMEOUT(start.canReadLine(), 4000);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(start)));
	QTRY_COMPARE(start.state(), QLocalSocket::UnconnectedState);
	QCOMPARE(helperChildren.size(), 1);
	const auto child = helperChildren.first();
	QTest::qWait(100);
	QVERIFY(child && child->state() == QProcess::Running);
	QLocalSocket stop;
	sendFixtureRequest(stop, name, publish ? Command::StopPublish : Command::StopView, fixturePayload(), version);
	QTRY_VERIFY_WITH_TIMEOUT(stop.canReadLine(), 3500);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(stop)));
	QVERIFY(stopped(child));
}

void TestScreenShareProtocol::pendingSocketDisconnectStopsSession() {
	ScreenShareHelperServer server;
	const QString name = fixtureSocketName();
	QString error;
	QVERIFY2(server.start(name, &error), qPrintable(error));
	QLocalSocket start;
	sendFixtureRequest(start, name, Mumble::ScreenShare::IPC::Command::StartView, fixturePayload(QStringLiteral("partial")));
	QTRY_COMPARE(helperChildren.size(), 1);
	const auto child = helperChildren.first();
	QTRY_VERIFY(child && child->state() == QProcess::Running);
	QVERIFY(!start.canReadLine());
	start.abort();
	QTRY_VERIFY_WITH_TIMEOUT(stopped(child), 3500);
}

void TestScreenShareProtocol::ownerExitStopsRunningSession_data() {
	QTest::addColumn< bool >("ready");
	QTest::newRow("ready-after-socket-close") << true;
	QTest::newRow("pending-first-frame") << false;
}

void TestScreenShareProtocol::ownerExitStopsRunningSession() {
#ifndef Q_OS_WIN
	QSKIP("Native process ownership is currently advertised by Windows clients only.");
#else
	QFETCH(bool, ready);
	QProcess owner;
	owner.setProgram(QCoreApplication::applicationFilePath());
	owner.setArguments({ QStringLiteral("--screen-share-test-child"), QStringLiteral("hold") });
	owner.start();
	QTRY_COMPARE(owner.state(), QProcess::Running);
	ScreenShareHelperServer server;
	const QString name = fixtureSocketName();
	QString error;
	QVERIFY2(server.start(name, &error), qPrintable(error));
	QLocalSocket start;
	sendFixtureRequest(start, name, Mumble::ScreenShare::IPC::Command::StartView,
		ownedPayload(owner.processId(), 1, ready ? QStringLiteral("fragmented") : QStringLiteral("partial")));
	if (ready) {
		QTRY_VERIFY_WITH_TIMEOUT(start.canReadLine(), 4000);
		QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(start)));
		QTRY_COMPARE(start.state(), QLocalSocket::UnconnectedState);
	}
	QTRY_COMPARE(helperChildren.size(), 1);
	const auto media = helperChildren.first();
	QTRY_VERIFY(media && media->state() == QProcess::Running);
	// Simulate abrupt client exit after the successful request socket is gone.
	// Only this test-owned process is terminated.
	owner.kill();
	QTRY_COMPARE(owner.state(), QProcess::NotRunning);
	QTRY_VERIFY_WITH_TIMEOUT(stopped(media), 3500);
#endif
}

void TestScreenShareProtocol::invalidOwnerCannotStartOrReplaceSession() {
#ifndef Q_OS_WIN
	QSKIP("Native process ownership is currently advertised by Windows clients only.");
#else
	using Command = Mumble::ScreenShare::IPC::Command;
	ScreenShareHelperServer server;
	const QString name = fixtureSocketName();
	QString error;
	QVERIFY2(server.start(name, &error), qPrintable(error));
	const qint64 owner = QCoreApplication::applicationPid();
	auto invalid = ownedPayload(owner, 1);
	invalid.insert(QStringLiteral("owner_process_identity"), QStringLiteral("1"));
	QLocalSocket rejectedStart;
	sendFixtureRequest(rejectedStart, name, Command::StartView, invalid);
	QTRY_VERIFY(rejectedStart.canReadLine());
	QVERIFY(!Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(rejectedStart)));
	QVERIFY(helperChildren.isEmpty());
	QLocalSocket start;
	sendFixtureRequest(start, name, Command::StartView, ownedPayload(owner, 2));
	QTRY_VERIFY_WITH_TIMEOUT(start.canReadLine(), 4000);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(start)));
	QCOMPARE(helperChildren.size(), 1);
	const auto media = helperChildren.first();
	for (const auto command : { Command::StartView, Command::StopView }) {
		QLocalSocket legacy;
		sendFixtureRequest(legacy, name, command, fixturePayload());
		QTRY_VERIFY(legacy.canReadLine());
		QVERIFY(!Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(legacy)));
		QVERIFY(media && media->state() == QProcess::Running);
		QCOMPARE(helperChildren.size(), 1);
	}
	QLocalSocket stop;
	sendFixtureRequest(stop, name, Command::StopView, ownedPayload(owner, 3));
	QTRY_VERIFY_WITH_TIMEOUT(stop.canReadLine(), 3500);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(stop)));
	QVERIFY(stopped(media));
#endif
}

void TestScreenShareProtocol::reorderedOwnerOperationsDoNotResurrectOrStopNewerSession() {
#ifndef Q_OS_WIN
	QSKIP("Native process ownership is currently advertised by Windows clients only.");
#else
	using Command = Mumble::ScreenShare::IPC::Command;
	ScreenShareHelperServer server;
	const QString name = fixtureSocketName();
	QString error;
	QVERIFY2(server.start(name, &error), qPrintable(error));
	const qint64 owner = QCoreApplication::applicationPid();
	// Stop arrives while the earlier start's socket is delayed. Retain a
	// tombstone even though the helper has not created that session yet.
	QLocalSocket stopFirst;
	sendFixtureRequest(stopFirst, name, Command::StopView, ownedPayload(owner, 2));
	QTRY_VERIFY(stopFirst.canReadLine());
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(stopFirst)));
	QLocalSocket lateStart;
	sendFixtureRequest(lateStart, name, Command::StartView, ownedPayload(owner, 1));
	QTRY_VERIFY(lateStart.canReadLine());
	QVERIFY(!Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(lateStart)));
	QVERIFY(helperChildren.isEmpty());
	QLocalSocket newStart;
	sendFixtureRequest(newStart, name, Command::StartView, ownedPayload(owner, 3));
	QTRY_VERIFY_WITH_TIMEOUT(newStart.canReadLine(), 4000);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(newStart)));
	QCOMPARE(helperChildren.size(), 1);
	const auto media = helperChildren.first();
	QLocalSocket lateStop;
	sendFixtureRequest(lateStop, name, Command::StopView, ownedPayload(owner, 2));
	QTRY_VERIFY(lateStop.canReadLine());
	QVERIFY(!Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(lateStop)));
	QVERIFY(media && media->state() == QProcess::Running);
	QLocalSocket finalStop;
	sendFixtureRequest(finalStop, name, Command::StopView, ownedPayload(owner, 4));
	QTRY_VERIFY_WITH_TIMEOUT(finalStop.canReadLine(), 3500);
	QVERIFY(Mumble::ScreenShare::IPC::replySucceeded(readFixtureReply(finalStop)));
	QVERIFY(stopped(media));
#endif
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const auto arguments = app.arguments();
	if (arguments.size() == 3 && arguments.at(1) == QLatin1String("--screen-share-test-child")) {
		// A local deterministic media substitute: no screen, microphone, relay or SDK.
		QFile output;
		if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
		const auto mode = arguments.at(2);
		if (mode == QLatin1String("partial") || mode == QLatin1String("fragmented")) {
			output.write("BGR", 3);
			output.flush();
			if (mode == QLatin1String("fragmented")) {
				QTimer::singleShot(150, &app, [&]() { output.write("A", 1); output.flush(); });
			}
		}
		QTimer::singleShot(10000, &app, &QCoreApplication::quit);
		return app.exec();
	}
	TestScreenShareProtocol test;
	return QTest::qExec(&test, argc, argv);
}
#include "TestScreenShareProtocol.moc"
