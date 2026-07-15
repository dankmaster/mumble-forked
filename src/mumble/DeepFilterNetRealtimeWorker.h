// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DEEPFILTERNETREALTIMEWORKER_H_
#define MUMBLE_MUMBLE_DEEPFILTERNETREALTIMEWORKER_H_

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

/// Moves allocation-heavy DeepFilterNet/tract inference away from the audio
/// callback. The producer is the single capture callback and the consumer is
/// the owned worker thread. All slots are fixed-size and allocated as part of
/// this object; processFrame() only copies samples and uses lock-free atomics.
///
/// start(), stop(), and destruction are lifecycle operations and must run only
/// after the audio callback has quiesced. In particular, stop() may join.
class DeepFilterNetRealtimeWorker final {
public:
	static constexpr unsigned int frameSamples           = 480;
	static constexpr unsigned int schedulingDelayFrames  = 2;
	static constexpr unsigned int schedulingDelaySamples = frameSamples * schedulingDelayFrames;
	static constexpr unsigned int slotCount              = 4;
	static constexpr std::size_t cacheLineBytes          = 64;
	static constexpr std::chrono::milliseconds offlineFrameWaitTimeout{ 2000 };

	using ProcessFunction = int (*)(void *context, const float *input, float *output) noexcept;

	static constexpr int statusOk                 = 0;
	static constexpr int statusNotStarted         = -1;
	static constexpr int statusInvalidFrame       = -2;
	static constexpr int statusOutputNotReady     = -3;
	static constexpr int statusQueueFull          = -4;
	static constexpr int statusSequenceMismatch   = -5;
	static constexpr int statusWorkerStartFailed  = -6;
	static constexpr int statusOfflineWaitTimeout = -7;

	DeepFilterNetRealtimeWorker() = default;
	~DeepFilterNetRealtimeWorker();

	DeepFilterNetRealtimeWorker(const DeepFilterNetRealtimeWorker &)            = delete;
	DeepFilterNetRealtimeWorker(DeepFilterNetRealtimeWorker &&)                 = delete;
	DeepFilterNetRealtimeWorker &operator=(const DeepFilterNetRealtimeWorker &) = delete;
	DeepFilterNetRealtimeWorker &operator=(DeepFilterNetRealtimeWorker &&)      = delete;

	/// Starts the worker and resets its fixed SPSC slots. Off-callback only.
	bool start(void *context, ProcessFunction processFunction);
	/// Requests shutdown and joins the worker. Off-callback only.
	void stop() noexcept;

	/// Enqueues one complete 10 ms frame and emits the result from two calls
	/// earlier. It never waits for the worker: inference has two callback
	/// intervals to complete. A late/missing result permanently latches a failure
	/// so the owning product pipeline can fail closed.
	bool processFrame(const float *input, float *output, unsigned int sampleCount = frameSamples) noexcept;
	/// Waits until the result required by the next synthetic callback is ready.
	/// This is bounded and fail-closed, but intentionally blocking: offline only.
	/// It must be called by the same single producer that calls processFrame(),
	/// with no concurrent start(), stop(), or processFrame() operation.
	bool prepareOfflineFrame(std::chrono::milliseconds timeout = offlineFrameWaitTimeout) noexcept;
	/// Waits for every frame submitted so far without consuming or emitting a
	/// slot, so offline diagnostics include end-of-stream work.
	bool finishOfflineProcessing(std::chrono::milliseconds timeout = offlineFrameWaitTimeout) noexcept;

	bool healthy() const noexcept;
	int failureStatus() const noexcept;
	std::uint64_t completedFrames() const noexcept;
	std::uint64_t processingFrames() const noexcept;
	std::uint64_t lastProcessingNanoseconds() const noexcept;
	std::uint64_t totalProcessingNanoseconds() const noexcept;
	std::uint64_t maximumProcessingNanoseconds() const noexcept;
	/// Conservative nearest-rank p99 from fixed 10 us buckets. The final
	/// bucket is represented by the observed maximum instead of being clipped.
	std::uint64_t processingP99Nanoseconds() const noexcept;
	unsigned int pendingFrames() const noexcept;
	unsigned int schedulingSlackFrames() const noexcept;

private:
	enum class SlotState : unsigned char { Empty, InputReady, OutputReady };

	static constexpr std::size_t slotDataBytes =
		(2 * frameSamples * sizeof(float)) + sizeof(std::uint64_t) + sizeof(std::atomic< unsigned char >);
	static constexpr std::size_t slotPaddingBytes = cacheLineBytes - (slotDataBytes % cacheLineBytes);
	static constexpr std::uint64_t processingHistogramBucketNanoseconds = 10'000;
	static constexpr std::size_t processingHistogramBucketCount         = 2'001;

	struct alignas(cacheLineBytes) Slot final {
		std::array< float, frameSamples > input  = {};
		std::array< float, frameSamples > output = {};
		std::uint64_t sequence                   = 0;
		std::atomic< unsigned char > state{ static_cast< unsigned char >(SlotState::Empty) };
		// Keep every slot on its own cache-line range without relying on the
		// compiler's implicit tail padding (which is a /W4 warning in MSVC).
		std::array< unsigned char, slotPaddingBytes > cacheLinePadding = {};
	};
	static_assert(sizeof(Slot) % cacheLineBytes == 0);

	void workerLoop() noexcept;
	bool waitForCompletedFrames(std::uint64_t requiredCompletedFrames, std::chrono::milliseconds timeout) noexcept;
	void latchFailure(int status) noexcept;
	void notifyWorker() noexcept;
	void notifyOfflineWaitersFromWorker() noexcept;
	void recordProcessingDuration(std::uint64_t durationNanoseconds) noexcept;
	void resetSlots() noexcept;
	void resetProcessingMetrics() noexcept;

	std::array< Slot, slotCount > m_slots = {};
	std::thread m_worker;
	void *m_context                 = nullptr;
	ProcessFunction m_process       = nullptr;
	std::uint64_t m_submittedFrames = 0;
	std::atomic_uint m_pendingFrames{ 0 };
	std::atomic_bool m_stopRequested{ false };
	std::atomic_int m_failureStatus{ statusNotStarted };
	std::atomic< std::uint64_t > m_completedFrames{ 0 };
	/// Monotonic generation used with C++20 atomic wait/notify. Publishing a
	/// frame increments it after the slot state becomes InputReady. Unlike
	/// sleep-based polling, this does not inherit the 15.6 ms Windows timer tick.
	std::atomic_uint m_workGeneration{ 0 };
	std::atomic_bool m_offlineWaitEnabled{ false };
	std::mutex m_offlineWaitMutex;
	std::condition_variable m_offlineWaitCondition;
	std::atomic< std::uint64_t > m_processingFrames{ 0 };
	std::atomic_uint m_lastProcessingNanoseconds{ 0 };
	std::atomic< std::uint64_t > m_totalProcessingNanoseconds{ 0 };
	std::atomic< std::uint64_t > m_maximumProcessingNanoseconds{ 0 };
	std::array< std::atomic< std::uint64_t >, processingHistogramBucketCount > m_processingHistogram = {};
	std::atomic_bool m_started{ false };
};

static_assert(std::atomic< unsigned char >::is_always_lock_free, "DeepFilterNet callback slot state must be lock-free");
static_assert(std::atomic_int::is_always_lock_free, "DeepFilterNet callback failure state must be lock-free");
static_assert(std::atomic_bool::is_always_lock_free, "DeepFilterNet callback lifecycle state must be lock-free");
static_assert(std::atomic_uint::is_always_lock_free, "DeepFilterNet callback wake generation must be lock-free");
static_assert(DeepFilterNetRealtimeWorker::slotCount > DeepFilterNetRealtimeWorker::schedulingDelayFrames,
			  "DeepFilterNet worker needs distinct input and deadline output slots");

#endif // MUMBLE_MUMBLE_DEEPFILTERNETREALTIMEWORKER_H_
