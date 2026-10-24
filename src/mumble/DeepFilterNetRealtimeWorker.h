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
#include <memory>
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
	/// Frames the worker may fall behind its deadline (for example during a
	/// game's frame hitch) before the queue is treated as failed. Late frames
	/// are emitted as aligned dry audio while the model keeps its state.
	static constexpr unsigned int lateFrameToleranceFrames = 16;
	static constexpr unsigned int slotCount = schedulingDelayFrames + 2 + lateFrameToleranceFrames;
	/// Sustained lateness, rather than a single stall, fails closed. Each late
	/// frame adds lateFrameDebt and each timely frame repays one unit.
	static constexpr unsigned int lateFrameDebt      = 10;
	static constexpr unsigned int lateFrameDebtLimit = 250;
	static constexpr unsigned int maximumSchedulingDelayFrames = 64;
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
	bool start(void *context, ProcessFunction processFunction,
		unsigned int delayFrames = schedulingDelayFrames);
	/// Requests shutdown and joins the worker. Off-callback only.
	void stop() noexcept;

	/// Enqueues one complete 10 ms frame and emits the result from two calls
	/// earlier. It never waits for the worker: inference has two callback
	/// intervals to complete. When that result is not ready yet, the frame is
	/// still accepted, *outputLate is set and output stays zero so the caller can
	/// emit its aligned dry signal; the late result is discarded once it
	/// completes. Sustained lateness or a full queue permanently latches a
	/// failure so the owning product pipeline can fail closed.
	bool processFrame(const float *input, float *output, unsigned int sampleCount = frameSamples,
					  bool *outputLate = nullptr) noexcept;
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
	unsigned int configuredDelayFrames() const noexcept { return m_delayFrames; }
	/// Frames emitted as dry because their result missed the deadline.
	std::uint64_t lateFrames() const noexcept { return m_lateFrames.load(std::memory_order_acquire); }

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
	Slot &slotForSequence(std::uint64_t sequence) noexcept;

	std::array< Slot, slotCount > m_slots = {};
	// Capture uses the in-object slots. Only receive callbacks with a larger
	// burst budget allocate extra slots, before the worker starts.
	std::unique_ptr< Slot[] > m_extraSlots;
	unsigned int m_activeSlotCount = slotCount;
	unsigned int m_delayFrames = schedulingDelayFrames;
	std::thread m_worker;
	void *m_context                 = nullptr;
	ProcessFunction m_process       = nullptr;
	std::uint64_t m_submittedFrames = 0;
	/// Producer-only: the oldest output sequence that has not been consumed or
	/// discarded yet. It trails the deadline sequence only after late frames.
	std::uint64_t m_releasedFrames = 0;
	unsigned int m_lateFrameDebt   = 0;
	std::atomic< std::uint64_t > m_lateFrames{ 0 };
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
static_assert(std::atomic< std::uint64_t >::is_always_lock_free,
			  "DeepFilterNet late-frame diagnostics must be lock-free");

#endif // MUMBLE_MUMBLE_DEEPFILTERNETREALTIMEWORKER_H_
