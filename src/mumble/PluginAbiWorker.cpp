// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "PluginAbiWorker.h"

#include <QtConcurrent>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>

#include <algorithm>
#include <exception>
#include <iterator>
#include <utility>

namespace {
thread_local const PluginAbiWorker *CurrentPluginAbiWorker = nullptr;
}

PluginAbiWorker::PluginAbiWorker() {
	m_pool.setMaxThreadCount(1);
	m_pool.setExpiryTimeout(-1);
	m_pool.setObjectName(QStringLiteral("MumblePluginAbiWorker"));
}

PluginAbiWorker::~PluginAbiWorker() {
	shutdown();
	std::ignore = waitForDone();
}

void PluginAbiWorker::appendAcceptedWork(WorkItem work) {
	Q_ASSERT(work.work);
	if (work.runtimeNotification) {
		++m_pendingRuntimeCalls[static_cast< std::size_t >(work.runtimeQueueClass)];
		m_pendingRuntimePayloadBytes += work.runtimePayloadBytes;
	}
	m_queue.push_back(std::move(work));
	++m_outstanding;
	startRunnerLocked();
}

void PluginAbiWorker::startRunnerLocked() {
	if (m_runnerActive) return;
	m_runnerActive = true;
	std::ignore = QtConcurrent::run(&m_pool, [this]() { runQueue(); });
}

void PluginAbiWorker::runQueue() {
	struct CurrentWorkerGuard {
		const PluginAbiWorker *previous = nullptr;
		~CurrentWorkerGuard() { CurrentPluginAbiWorker = previous; }
	} guard { CurrentPluginAbiWorker };
	CurrentPluginAbiWorker = this;

	for (;;) {
		WorkItem current;
		{
			QMutexLocker lock(&m_mutex);
			if (m_queue.empty()) {
				m_runnerActive = false;
				m_idleCondition.wakeAll();
				return;
			}
			current = std::move(m_queue.front());
			m_queue.pop_front();
			if (current.runtimeNotification) {
				auto &pending = m_pendingRuntimeCalls[static_cast< std::size_t >(current.runtimeQueueClass)];
				Q_ASSERT(pending > 0);
				--pending;
				m_pendingRuntimePayloadBytes -= current.runtimePayloadBytes;
			}
		}

		try {
			current.work();
		} catch (const std::exception &exception) {
			qCritical().noquote() << "Unhandled plugin worker exception:" << exception.what();
		} catch (...) {
			qCritical() << "Unhandled non-standard plugin worker exception";
		}

		{
			QMutexLocker lock(&m_mutex);
			Q_ASSERT(m_outstanding > 0);
			--m_outstanding;
			m_idleCondition.wakeAll();
		}
	}
}

bool PluginAbiWorker::enqueue(std::function< void() > work) {
	if (!work) return false;
	QMutexLocker lock(&m_mutex);
	if (!m_accepting) return false;
	appendAcceptedWork({ std::move(work), {}, 0, RuntimeQueueClass::TalkState, false });
	return true;
}

bool PluginAbiWorker::enqueueRuntime(std::function< void() > work, const RuntimeQueueClass queueClass,
									 QByteArray coalescingKey, const qsizetype payloadBytes) {
	if (!work || queueClass == RuntimeQueueClass::Count || payloadBytes < 0
		|| (queueClass != RuntimeQueueClass::Data && payloadBytes != 0)
		|| (queueClass == RuntimeQueueClass::Data && payloadBytes > MaximumPendingDataBytes)) return false;
	QMutexLocker lock(&m_mutex);
	if (!m_accepting) return false;
	const std::size_t queueIndex = static_cast< std::size_t >(queueClass);

	if (!coalescingKey.isEmpty()) {
		// Never fold state across lifecycle/settings/structural/shutdown barriers.
		auto segmentStart = m_queue.begin();
		for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
			if (!it->runtimeNotification) segmentStart = std::next(it);
		}
		for (auto it = segmentStart; it != m_queue.end(); ++it) {
			if (!it->runtimeNotification || it->runtimeQueueClass != queueClass
				|| it->runtimeCoalescingKey != coalescingKey) continue;
			Q_ASSERT(m_pendingRuntimeCalls[queueIndex] > 0);
			--m_pendingRuntimeCalls[queueIndex];
			m_pendingRuntimePayloadBytes -= it->runtimePayloadBytes;
			Q_ASSERT(m_outstanding > 0);
			--m_outstanding;
			m_queue.erase(it);
			break;
		}
	}

	const auto maximumCalls = [queueClass]() -> qsizetype {
		switch (queueClass) {
			case RuntimeQueueClass::TalkState: return MaximumPendingTalkStateCalls;
			case RuntimeQueueClass::ChannelRename: return MaximumPendingRenameCalls;
			case RuntimeQueueClass::Key: return MaximumPendingKeyCalls;
			case RuntimeQueueClass::Data: return MaximumPendingDataCalls;
			case RuntimeQueueClass::Count: break;
		}
		return 0;
	}();
	const auto exceedsLimit = [this, queueClass, queueIndex, maximumCalls, payloadBytes]() {
		return m_pendingRuntimeCalls[queueIndex] >= maximumCalls
			|| (queueClass == RuntimeQueueClass::Data
				&& m_pendingRuntimePayloadBytes + payloadBytes > MaximumPendingDataBytes);
	};
	while (exceedsLimit()) {
		// State is latest-value and may evict stale state. Key/data are exact FIFO and reject their newest event.
		auto stale = std::find_if(m_queue.begin(), m_queue.end(), [queueClass](const WorkItem &item) {
			return item.runtimeNotification && item.runtimeQueueClass == queueClass
				&& !item.runtimeCoalescingKey.isEmpty();
		});
		if (stale == m_queue.end()) return false;
		Q_ASSERT(m_pendingRuntimeCalls[queueIndex] > 0);
		--m_pendingRuntimeCalls[queueIndex];
		m_pendingRuntimePayloadBytes -= stale->runtimePayloadBytes;
		Q_ASSERT(m_outstanding > 0);
		--m_outstanding;
		m_queue.erase(stale);
	}

	appendAcceptedWork({ std::move(work), std::move(coalescingKey), payloadBytes, queueClass, true });
	return true;
}

bool PluginAbiWorker::shutdown(std::function< void() > finalWork) {
	QMutexLocker lock(&m_mutex);
	if (!m_accepting) return false;
	m_accepting = false;
	if (finalWork) appendAcceptedWork({ std::move(finalWork), {}, 0, RuntimeQueueClass::TalkState, false });
	if (m_outstanding == 0) m_idleCondition.wakeAll();
	return true;
}

bool PluginAbiWorker::waitForDone(const int timeoutMilliseconds) {
	if (isCurrentThread()) return true;
	const bool isApplicationThread = QCoreApplication::instance()
		&& QThread::currentThread() == QCoreApplication::instance()->thread();
	QElapsedTimer deadline;
	if (timeoutMilliseconds >= 0) deadline.start();
	for (;;) {
		{
			QMutexLocker lock(&m_mutex);
			if (m_outstanding == 0) break;
			int waitMilliseconds = isApplicationThread ? 1 : 20;
			if (timeoutMilliseconds >= 0) {
				const qint64 remaining = static_cast< qint64 >(timeoutMilliseconds) - deadline.elapsed();
				if (remaining <= 0) return false;
				waitMilliseconds = std::min(waitMilliseconds, static_cast< int >(remaining));
			}
			m_idleCondition.wait(&m_mutex, static_cast< unsigned long >(waitMilliseconds));
		}
		if (isApplicationThread) {
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
		}
	}
	if (timeoutMilliseconds < 0) {
		m_pool.waitForDone();
		return true;
	}
	const qint64 remaining = static_cast< qint64 >(timeoutMilliseconds) - deadline.elapsed();
	return remaining >= 0 && m_pool.waitForDone(static_cast< int >(remaining));
}

bool PluginAbiWorker::isCurrentThread() const {
	return CurrentPluginAbiWorker == this;
}

PluginAbiWorker &sharedPluginAbiWorker() {
	static PluginAbiWorker worker;
	return worker;
}
