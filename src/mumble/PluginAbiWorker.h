// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_PLUGINABIWORKER_H_
#define MUMBLE_MUMBLE_PLUGINABIWORKER_H_

#include <QtCore/QByteArray>
#include <QtCore/QMutex>
#include <QtCore/QThreadPool>
#include <QtCore/QWaitCondition>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

/// Serial executor for third-party plugin ABI calls. Normal work is an exact FIFO used by lifecycle operations and
/// low-frequency structural notifications. Noisy runtime families are independently bounded while retaining their
/// position relative to normal-work barriers.
class PluginAbiWorker final {
public:
	enum class RuntimeQueueClass : std::uint8_t { TalkState, ChannelRename, Key, Data, Count };
	static constexpr qsizetype MaximumPendingTalkStateCalls = 2048;
	static constexpr qsizetype MaximumPendingRenameCalls    = 512;
	static constexpr qsizetype MaximumPendingKeyCalls       = 512;
	static constexpr qsizetype MaximumPendingDataCalls      = 256;
	static constexpr qsizetype MaximumPendingDataBytes      = 8 * 1024 * 1024;

	PluginAbiWorker();
	~PluginAbiWorker();

	PluginAbiWorker(const PluginAbiWorker &)            = delete;
	PluginAbiWorker &operator=(const PluginAbiWorker &) = delete;

	bool enqueue(std::function< void() > work);
	/// A non-empty coalescing key gives the newest pending notification latest-value semantics within the current
	/// barrier segment. Empty-key notifications remain exact FIFO until their family's safety limit is reached.
	bool enqueueRuntime(std::function< void() > work, RuntimeQueueClass queueClass,
						QByteArray coalescingKey = {}, qsizetype payloadBytes = 0);
	bool shutdown(std::function< void() > finalWork = {});
	bool waitForDone(int timeoutMilliseconds = -1);
	bool isCurrentThread() const;

private:
	struct WorkItem {
		std::function< void() > work;
		QByteArray runtimeCoalescingKey;
		qsizetype runtimePayloadBytes      = 0;
		RuntimeQueueClass runtimeQueueClass = RuntimeQueueClass::TalkState;
		bool runtimeNotification            = false;
	};

	QMutex m_mutex;
	QWaitCondition m_idleCondition;
	QThreadPool m_pool;
	std::deque< WorkItem > m_queue;
	qsizetype m_outstanding = 0;
	std::array< qsizetype, static_cast< std::size_t >(RuntimeQueueClass::Count) > m_pendingRuntimeCalls {};
	qsizetype m_pendingRuntimePayloadBytes = 0;
	bool m_accepting                       = true;
	bool m_runnerActive                    = false;
	void appendAcceptedWork(WorkItem work);
	void startRunnerLocked();
	void runQueue();
};

PluginAbiWorker &sharedPluginAbiWorker();

#endif // MUMBLE_MUMBLE_PLUGINABIWORKER_H_
