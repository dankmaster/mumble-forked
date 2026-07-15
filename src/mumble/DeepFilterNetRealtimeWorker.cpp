// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetRealtimeWorker.h"

#include <algorithm>
#include <chrono>
#include <limits>

DeepFilterNetRealtimeWorker::~DeepFilterNetRealtimeWorker() {
	stop();
}

bool DeepFilterNetRealtimeWorker::start(void *context, ProcessFunction processFunction) {
	stop();
	resetSlots();
	m_context         = context;
	m_process         = processFunction;
	m_submittedFrames = 0;
	m_pendingFrames.store(0, std::memory_order_relaxed);
	m_completedFrames.store(0, std::memory_order_relaxed);
	m_workGeneration.store(0, std::memory_order_relaxed);
	m_offlineWaitEnabled.store(false, std::memory_order_relaxed);
	resetProcessingMetrics();
	m_stopRequested.store(false, std::memory_order_release);
	m_failureStatus.store(statusOk, std::memory_order_release);

	if (!m_context || !m_process) {
		m_failureStatus.store(statusNotStarted, std::memory_order_release);
		return false;
	}

	try {
		m_worker = std::thread([this] { workerLoop(); });
		m_started.store(true, std::memory_order_release);
	} catch (...) {
		m_started.store(false, std::memory_order_release);
		m_failureStatus.store(statusWorkerStartFailed, std::memory_order_release);
		return false;
	}
	return true;
}

void DeepFilterNetRealtimeWorker::stop() noexcept {
	m_started.store(false, std::memory_order_release);
	m_stopRequested.store(true, std::memory_order_release);
	notifyWorker();
	m_offlineWaitCondition.notify_all();
	if (m_worker.joinable()) {
		m_worker.join();
	}
}

bool DeepFilterNetRealtimeWorker::processFrame(const float *input, float *output, unsigned int sampleCount) noexcept {
	if (output && sampleCount == frameSamples) {
		std::fill_n(output, frameSamples, 0.0f);
	}
	if (!m_started.load(std::memory_order_acquire)) {
		latchFailure(statusNotStarted);
		return false;
	}
	if (!input || !output || sampleCount != frameSamples) {
		latchFailure(statusInvalidFrame);
		return false;
	}
	if (!healthy()) {
		return false;
	}

	const std::uint64_t sequence = m_submittedFrames;
	if (sequence >= schedulingDelayFrames) {
		const std::uint64_t outputSequence = sequence - schedulingDelayFrames;
		Slot &outputSlot                   = m_slots[outputSequence % slotCount];
		const auto state                   = static_cast< SlotState >(outputSlot.state.load(std::memory_order_acquire));
		if (state != SlotState::OutputReady || outputSlot.sequence != outputSequence) {
			latchFailure(statusOutputNotReady);
			return false;
		}
		std::copy(outputSlot.output.cbegin(), outputSlot.output.cend(), output);
		outputSlot.state.store(static_cast< unsigned char >(SlotState::Empty), std::memory_order_release);
	}

	Slot &inputSlot = m_slots[sequence % slotCount];
	if (static_cast< SlotState >(inputSlot.state.load(std::memory_order_acquire)) != SlotState::Empty) {
		latchFailure(statusQueueFull);
		std::fill_n(output, frameSamples, 0.0f);
		return false;
	}
	std::copy_n(input, frameSamples, inputSlot.input.begin());
	inputSlot.sequence = sequence;
	m_pendingFrames.fetch_add(1, std::memory_order_relaxed);
	inputSlot.state.store(static_cast< unsigned char >(SlotState::InputReady), std::memory_order_release);
	++m_submittedFrames;
	notifyWorker();
	return true;
}

bool DeepFilterNetRealtimeWorker::prepareOfflineFrame(std::chrono::milliseconds timeout) noexcept {
	if (!healthy()) {
		return false;
	}
	if (m_submittedFrames < schedulingDelayFrames) {
		return true;
	}

	const std::uint64_t requiredCompletedFrames = m_submittedFrames - schedulingDelayFrames + 1;
	return waitForCompletedFrames(requiredCompletedFrames, timeout);
}

bool DeepFilterNetRealtimeWorker::finishOfflineProcessing(std::chrono::milliseconds timeout) noexcept {
	if (!healthy()) {
		return false;
	}
	return waitForCompletedFrames(m_submittedFrames, timeout);
}

bool DeepFilterNetRealtimeWorker::waitForCompletedFrames(std::uint64_t requiredCompletedFrames,
														 std::chrono::milliseconds timeout) noexcept {
	timeout = std::max(timeout, std::chrono::milliseconds::zero());
	m_offlineWaitEnabled.store(true, std::memory_order_release);
	if (m_completedFrames.load(std::memory_order_acquire) >= requiredCompletedFrames) {
		return healthy();
	}

	const auto deadline = std::chrono::steady_clock::now() + timeout;
	std::unique_lock< std::mutex > lock(m_offlineWaitMutex);
	const bool completed = m_offlineWaitCondition.wait_until(lock, deadline, [this, requiredCompletedFrames] {
		return m_completedFrames.load(std::memory_order_acquire) >= requiredCompletedFrames || !healthy();
	});
	lock.unlock();
	if (!completed || m_completedFrames.load(std::memory_order_acquire) < requiredCompletedFrames) {
		// Give a completion racing the deadline one final acquire before the
		// timeout permanently disables the worker.
		if (healthy() && m_completedFrames.load(std::memory_order_acquire) < requiredCompletedFrames) {
			latchFailure(statusOfflineWaitTimeout);
		}
		return false;
	}
	return healthy();
}

bool DeepFilterNetRealtimeWorker::healthy() const noexcept {
	return m_started.load(std::memory_order_acquire) && m_failureStatus.load(std::memory_order_acquire) == statusOk;
}

int DeepFilterNetRealtimeWorker::failureStatus() const noexcept {
	return m_failureStatus.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::completedFrames() const noexcept {
	return m_completedFrames.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::processingFrames() const noexcept {
	return m_processingFrames.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::lastProcessingNanoseconds() const noexcept {
	return m_lastProcessingNanoseconds.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::totalProcessingNanoseconds() const noexcept {
	return m_totalProcessingNanoseconds.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::maximumProcessingNanoseconds() const noexcept {
	return m_maximumProcessingNanoseconds.load(std::memory_order_acquire);
}

std::uint64_t DeepFilterNetRealtimeWorker::processingP99Nanoseconds() const noexcept {
	const std::uint64_t sampleCount = processingFrames();
	if (sampleCount == 0) {
		return 0;
	}
	const std::uint64_t targetRank = (sampleCount * 99 + 99) / 100;
	std::uint64_t accumulated      = 0;
	for (std::size_t bucket = 0; bucket < m_processingHistogram.size(); ++bucket) {
		accumulated += m_processingHistogram[bucket].load(std::memory_order_acquire);
		if (accumulated >= targetRank) {
			if (bucket + 1 == m_processingHistogram.size()) {
				return maximumProcessingNanoseconds();
			}
			return static_cast< std::uint64_t >(bucket) * processingHistogramBucketNanoseconds;
		}
	}
	return maximumProcessingNanoseconds();
}

unsigned int DeepFilterNetRealtimeWorker::pendingFrames() const noexcept {
	return m_pendingFrames.load(std::memory_order_acquire);
}

unsigned int DeepFilterNetRealtimeWorker::schedulingSlackFrames() const noexcept {
	const unsigned int pending = pendingFrames();
	return pending < schedulingDelayFrames ? schedulingDelayFrames - pending : 0;
}

void DeepFilterNetRealtimeWorker::workerLoop() noexcept {
	std::uint64_t nextSequence = 0;
	while (!m_stopRequested.load(std::memory_order_acquire)) {
		if (m_failureStatus.load(std::memory_order_acquire) != statusOk) {
			const unsigned int observedGeneration = m_workGeneration.load(std::memory_order_acquire);
			if (!m_stopRequested.load(std::memory_order_acquire)) {
				m_workGeneration.wait(observedGeneration, std::memory_order_acquire);
			}
			continue;
		}

		Slot &slot       = m_slots[nextSequence % slotCount];
		const auto state = static_cast< SlotState >(slot.state.load(std::memory_order_acquire));
		if (state != SlotState::InputReady) {
			const unsigned int observedGeneration = m_workGeneration.load(std::memory_order_acquire);
			// Recheck after observing the generation so a publication racing this
			// path cannot be lost between the predicate and atomic::wait().
			if (!m_stopRequested.load(std::memory_order_acquire)
				&& m_failureStatus.load(std::memory_order_acquire) == statusOk
				&& static_cast< SlotState >(slot.state.load(std::memory_order_acquire)) != SlotState::InputReady) {
				m_workGeneration.wait(observedGeneration, std::memory_order_acquire);
			}
			continue;
		}
		if (slot.sequence != nextSequence) {
			latchFailure(statusSequenceMismatch);
			continue;
		}

		const auto processingStartedAt  = std::chrono::steady_clock::now();
		const int status                = m_process(m_context, slot.input.data(), slot.output.data());
		const auto processingFinishedAt = std::chrono::steady_clock::now();
		recordProcessingDuration(static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::nanoseconds >(processingFinishedAt - processingStartedAt)
				.count()));
		m_pendingFrames.fetch_sub(1, std::memory_order_release);
		if (status != statusOk) {
			std::fill(slot.output.begin(), slot.output.end(), 0.0f);
			latchFailure(status);
			notifyOfflineWaitersFromWorker();
			continue;
		}
		slot.state.store(static_cast< unsigned char >(SlotState::OutputReady), std::memory_order_release);
		++nextSequence;
		m_completedFrames.fetch_add(1, std::memory_order_release);
		notifyOfflineWaitersFromWorker();
	}
}

void DeepFilterNetRealtimeWorker::latchFailure(int status) noexcept {
	if (status == statusOk) {
		return;
	}
	int expected = statusOk;
	m_failureStatus.compare_exchange_strong(expected, status, std::memory_order_acq_rel, std::memory_order_acquire);
}

void DeepFilterNetRealtimeWorker::notifyWorker() noexcept {
	m_workGeneration.fetch_add(1, std::memory_order_release);
	m_workGeneration.notify_all();
}

void DeepFilterNetRealtimeWorker::notifyOfflineWaitersFromWorker() noexcept {
	if (!m_offlineWaitEnabled.load(std::memory_order_acquire)) {
		return;
	}
	// The worker owns this lock; the audio callback never touches it. Taking it
	// closes the check-to-wait race for the bounded offline condition variable.
	{
		std::lock_guard< std::mutex > lock(m_offlineWaitMutex);
	}
	m_offlineWaitCondition.notify_all();
}

void DeepFilterNetRealtimeWorker::recordProcessingDuration(std::uint64_t durationNanoseconds) noexcept {
	m_lastProcessingNanoseconds.store(static_cast< unsigned int >(std::min< std::uint64_t >(
										  durationNanoseconds, std::numeric_limits< unsigned int >::max())),
									  std::memory_order_release);
	m_totalProcessingNanoseconds.fetch_add(durationNanoseconds, std::memory_order_relaxed);
	std::uint64_t maximum = m_maximumProcessingNanoseconds.load(std::memory_order_relaxed);
	while (durationNanoseconds > maximum
		   && !m_maximumProcessingNanoseconds.compare_exchange_weak(
			   maximum, durationNanoseconds, std::memory_order_relaxed, std::memory_order_relaxed)) {
	}
	const std::uint64_t quotient    = durationNanoseconds / processingHistogramBucketNanoseconds;
	const std::uint64_t remainder   = durationNanoseconds % processingHistogramBucketNanoseconds;
	const std::uint64_t bucketIndex = std::min< std::uint64_t >(
		quotient + (remainder == 0 ? 0 : 1), static_cast< std::uint64_t >(m_processingHistogram.size() - 1));
	const std::size_t bucket = static_cast< std::size_t >(bucketIndex);
	m_processingHistogram[bucket].fetch_add(1, std::memory_order_relaxed);
	// Publish the sample count last. Readers acquire it before scanning the
	// histogram, so a live snapshot never targets a sample whose bucket has not
	// yet been published.
	m_processingFrames.fetch_add(1, std::memory_order_release);
}

void DeepFilterNetRealtimeWorker::resetSlots() noexcept {
	for (Slot &slot : m_slots) {
		slot.input.fill(0.0f);
		slot.output.fill(0.0f);
		slot.sequence = 0;
		slot.state.store(static_cast< unsigned char >(SlotState::Empty), std::memory_order_relaxed);
	}
}

void DeepFilterNetRealtimeWorker::resetProcessingMetrics() noexcept {
	m_processingFrames.store(0, std::memory_order_relaxed);
	m_lastProcessingNanoseconds.store(0, std::memory_order_relaxed);
	m_totalProcessingNanoseconds.store(0, std::memory_order_relaxed);
	m_maximumProcessingNanoseconds.store(0, std::memory_order_relaxed);
	for (auto &bucket : m_processingHistogram) {
		bucket.store(0, std::memory_order_relaxed);
	}
}
