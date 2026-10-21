// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#include "ScreenShareProcessJob.h"
#include "ScreenShareProcessOwner.h"

#include <QtCore/QDateTime>
#include <QtCore/QProcess>
#include <QtCore/QUuid>

#include <limits>
#include <utility>

ScreenShareProcessJob::ScreenShareProcessJob(LaunchFactory factory, QObject *parent)
	: QObject(parent), m_factory(std::move(factory)) {
	m_stageTimer.setSingleShot(true);
	m_totalTimer.setSingleShot(true);
	m_stopTimer.setSingleShot(true);
	connect(&m_stageTimer, &QTimer::timeout, this, [this]() {
		if (m_state == State::Settling) ready();
		else fail(m_state == State::AwaitingFrame
			? QStringLiteral("The decoder did not produce a complete frame before the startup deadline.")
			: QStringLiteral("The media process did not start before the deadline."));
	});
	connect(&m_totalTimer, &QTimer::timeout, this, [this]() {
		m_launch.hasNextAttempt = false;
		m_retry = false;
		fail(QStringLiteral("Screen sharing startup exceeded its overall deadline."));
	});
	connect(&m_stopTimer, &QTimer::timeout, this, [this]() {
		if (m_process && m_process->state() != QProcess::NotRunning) m_process->kill();
		else finishStop();
	});
}

ScreenShareProcessJob::~ScreenShareProcessJob() {
	// Server shutdown is the only destruction path before Terminal. Do not leave a capture child running.
	if (m_process && m_process->state() != QProcess::NotRunning) m_process->kill();
}

bool ScreenShareProcessJob::owns(QProcess *process, const int attempt) const {
	return m_process == process && m_attempt == attempt && m_state != State::Terminal;
}

bool ScreenShareProcessJob::bindOwnerProcess(qint64 processID, const QString &identity, QString *error) {
	if (m_ownerProcess || m_state != State::Pending) return false;
	auto owner = std::make_unique< ScreenShareProcessOwner >(this);
	if (!owner->watch(processID, identity, [this]() { cancel(); }, error)) return false;
	m_ownerProcess = std::move(owner);
	return true;
}

void ScreenShareProcessJob::start(Completion completion, bool waitForPrevious) {
	if (m_state != State::Pending || m_completion) return;
	m_completion = std::move(completion);
	m_waitingForPrevious = waitForPrevious;
	if (!waitForPrevious) resumeStart();
}

void ScreenShareProcessJob::resumeStart() {
	const bool wasWaiting = m_waitingForPrevious;
	m_waitingForPrevious = false;
	if (m_state == State::Stopping) { if (wasWaiting) finishStop(); return; }
	if (m_state != State::Pending || !m_completion || m_totalTimer.isActive()) return;
	m_totalTimer.start(m_deadlines.total);
	QTimer::singleShot(0, this, [this]() { if (m_state == State::Pending) beginAttempt(); });
}

void ScreenShareProcessJob::beginAttempt() {
	m_state = State::Starting;
	m_launch = m_factory(m_attempt, this);
	m_process = m_launch.process;
	if (!m_launch.prepared) { fail(m_launch.errorMessage); return; }
	if (!m_process) { ready(); return; }
	QProcess *process = m_process;
	const int attempt = m_attempt;
	if (m_launch.nativeFrameFeed) {
		const quint64 bytes = quint64(m_launch.nativeFrameWidth) * m_launch.nativeFrameHeight * 4U;
		if (!bytes || bytes > std::numeric_limits< quint32 >::max()) {
			fail(QStringLiteral("Invalid native frame dimensions.")); return;
		}
		m_frameKey = QStringLiteral("mumble-screen-frame-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		m_frameGeneration = static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch());
		m_transport = std::make_unique< Mumble::ScreenShare::FrameTransport >();
		m_assembler = std::make_unique< Mumble::ScreenShare::RawBgraFrameAssembler >(static_cast< quint32 >(bytes), 2);
		if (!m_transport->create(m_frameKey, static_cast< quint32 >(bytes))) {
			fail(QStringLiteral("Unable to allocate the bounded native frame transport.")); return;
		}
		connect(process, &QProcess::readyReadStandardOutput, this, [this, process, attempt]() {
			if (owns(process, attempt)) consumeFrames();
		});
	}
	connect(process, &QProcess::started, this, [this, process, attempt]() {
		if (!owns(process, attempt)) return;
		if (m_state == State::Stopping) { process->terminate(); return; }
		m_launch.processID = process->processId();
		QString error;
		if (m_launch.onStarted && !m_launch.onStarted(&error)) { fail(error); return; }
		m_state = m_launch.nativeFrameFeed ? State::AwaitingFrame : State::Settling;
		m_stageTimer.start(m_launch.nativeFrameFeed ? m_deadlines.frame : m_deadlines.settle);
		if (m_launch.nativeFrameFeed) consumeFrames();
	});
	connect(process, &QProcess::errorOccurred, this, [this, process, attempt](QProcess::ProcessError error) {
		if (!owns(process, attempt)) return;
		if (m_state == State::Stopping) {
			if (error == QProcess::FailedToStart) finishStop();
			return;
		}
		if (error == QProcess::FailedToStart) fail(process->errorString());
	});
	connect(process, qOverload< int, QProcess::ExitStatus >(&QProcess::finished), this,
		[this, process, attempt](int, QProcess::ExitStatus) {
			if (!owns(process, attempt)) return;
			if (m_state == State::Stopping) { finishStop(); return; }
			const QString output = QString::fromUtf8(m_launch.nativeFrameFeed
				? process->readAllStandardError() : process->readAll()).trimmed();
			fail(output.isEmpty() ? QStringLiteral("The media process exited.") : output.left(4096));
		});
	// Connect every observer and allocate the frame ring before starting the child.
	m_stageTimer.start(m_deadlines.start);
	process->start();
}

void ScreenShareProcessJob::consumeFrames() {
	if (!m_assembler || !m_process || m_state == State::Stopping || m_state == State::Terminal) return;
	const auto frames = m_assembler->push(m_process->readAllStandardOutput());
	for (const auto &bytes : frames) {
		// The readiness callback may stop the job synchronously.
		if (!m_transport || m_state == State::Stopping || m_state == State::Terminal) break;
		Mumble::ScreenShare::NativeFrame frame;
		frame.generation = m_frameGeneration;
		frame.sequence = ++m_sequence;
		frame.timestampUsec = QDateTime::currentMSecsSinceEpoch() * 1000;
		frame.width = m_launch.nativeFrameWidth;
		frame.height = m_launch.nativeFrameHeight;
		frame.stride = frame.width * 4U;
		frame.bgra = bytes;
		if (m_transport->publish(frame) && m_state == State::AwaitingFrame) ready();
	}
}

void ScreenShareProcessJob::complete(bool success, const QString &error) {
	auto completion = std::move(m_completion);
	m_completion = {};
	if (completion) completion(success, error);
}

void ScreenShareProcessJob::ready() {
	if (m_state == State::Stopping || m_state == State::Terminal) return;
	m_state = State::Running;
	m_stageTimer.stop();
	m_totalTimer.stop();
	complete(true, {});
}

void ScreenShareProcessJob::fail(const QString &error) {
	if (m_state == State::Stopping || m_state == State::Terminal) return;
	m_lastError = error;
	m_retry = m_state != State::Running && m_launch.hasNextAttempt && m_totalTimer.isActive();
	stopProcess();
}

void ScreenShareProcessJob::cancel() {
	if (m_state == State::Terminal) return;
	m_retry = false;
	m_lastError = QStringLiteral("Screen sharing startup was cancelled.");
	m_totalTimer.stop();
	// Repeated stop/disconnect notifications must not extend the kill deadline.
	if (m_state == State::Stopping) return;
	stopProcess();
}

void ScreenShareProcessJob::stopProcess() {
	m_state = State::Stopping;
	m_stageTimer.stop();
	// Keep the predecessor barrier even when a waiting replacement is cancelled.
	// Otherwise A -> B -> C could start C while A is still stopping.
	if (m_waitingForPrevious) return;
	if (!m_process || m_process->state() == QProcess::NotRunning) { finishStop(); return; }
	m_process->terminate();
	m_stopTimer.start(m_deadlines.stop);
}

void ScreenShareProcessJob::finishStop() {
	if (m_state == State::Terminal) return;
	m_stopTimer.stop();
	if (m_process) { m_process->deleteLater(); m_process = nullptr; }
	m_launch.process = nullptr;
	m_transport.reset();
	m_assembler.reset();
	m_frameKey.clear();
	if (m_retry) {
		m_retry = false;
		++m_attempt;
		m_state = State::Pending;
		QTimer::singleShot(0, this, [this]() { if (m_state == State::Pending) beginAttempt(); });
		return;
	}
	m_state = State::Terminal;
	m_totalTimer.stop();
	complete(false, m_lastError);
	if (terminated) terminated();
}
