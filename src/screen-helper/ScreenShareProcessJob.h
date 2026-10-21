// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#ifndef MUMBLE_SCREENHELPER_SCREENSHAREPROCESSJOB_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREPROCESSJOB_H_

#include "ScreenShareExternalProcess.h"
#include "ScreenShareFrameTransport.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <functional>
#include <memory>

class ScreenShareProcessOwner;

// One helper-owned operation. Construction does not launch anything, allowing the server
// to register ownership and cancellation before start() yields to the event loop.
class ScreenShareProcessJob : public QObject {
public:
	enum class State { Pending, Starting, AwaitingFrame, Settling, Running, Stopping, Terminal };
	struct Deadlines { int start = 3000; int frame = 10000; int settle = 1000; int stop = 2000; int total = 15000; };
	using LaunchFactory = std::function< ScreenShareExternalProcess::LaunchResult(int, QObject *) >;
	using Completion = std::function< void(bool, const QString &) >;

	ScreenShareProcessJob(LaunchFactory factory, QObject *parent = nullptr);
	~ScreenShareProcessJob() override;
	void start(Completion completion, bool waitForPrevious = false);
	bool bindOwnerProcess(qint64 processID, const QString &identity, QString *error);
	void resumeStart();
	void cancel();
	State state() const { return m_state; }
	const ScreenShareExternalProcess::LaunchResult &launch() const { return m_launch; }
	QString frameKey() const { return m_frameKey; }
	quint64 frameGeneration() const { return m_frameGeneration; }
	void setDeadlines(Deadlines deadlines) { m_deadlines = deadlines; }
	std::function< void() > terminated;

private:
	void beginAttempt();
	void consumeFrames();
	void ready();
	void fail(const QString &error);
	void stopProcess();
	void finishStop();
	void complete(bool success, const QString &error);
	bool owns(QProcess *process, int attempt) const;

	LaunchFactory m_factory;
	Completion m_completion;
	State m_state = State::Pending;
	Deadlines m_deadlines;
	ScreenShareExternalProcess::LaunchResult m_launch;
	QPointer< QProcess > m_process;
	QTimer m_stageTimer;
	QTimer m_totalTimer;
	QTimer m_stopTimer;
	int m_attempt = 0;
	bool m_retry = false;
	bool m_waitingForPrevious = false;
	QString m_lastError;
	QString m_frameKey;
	quint64 m_frameGeneration = 0;
	quint64 m_sequence = 0;
	std::unique_ptr< Mumble::ScreenShare::FrameTransport > m_transport;
	std::unique_ptr< Mumble::ScreenShare::RawBgraFrameAssembler > m_assembler;
	std::unique_ptr< ScreenShareProcessOwner > m_ownerProcess;
};

#endif
