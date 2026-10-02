// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_

#include "SpeechCleanup.h"

#include <QString>
#include <QtCore/QFuture>

#include <chrono>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QThreadPool;

class SpeechCleanupProcessor {
public:
	virtual ~SpeechCleanupProcessor() = default;

	virtual bool isReady() const = 0;
	virtual void reset()         = 0;
	/// Completes any real-time handoff after warmup/reset. Implementations may
	/// allocate, create worker threads, or otherwise prepare here; Pipeline calls
	/// this off the audio callback immediately before publishing the processor.
	virtual bool prepareRealtime() {
		return true;
	}
	/// Receive callbacks can consume several 10 ms frames at once. Capture
	/// retains prepareRealtime() and its existing two-frame scheduling contract.
	virtual bool prepareRealtimeForReceive(unsigned int maximumBurstFrames) {
		(void) maximumBurstFrames;
		return prepareRealtime();
	}
	/// Lets an offline driver pace an asynchronous implementation before it
	/// submits the next synthetic callback frame. This may wait, so it must never
	/// be called from the real-time audio callback. Synchronous implementations
	/// have nothing to drain.
	virtual bool prepareOfflineFrame() noexcept {
		return true;
	}
	/// Completes work submitted by an offline driver before it records final
	/// diagnostics. This may wait and follows the same off-callback restriction.
	virtual bool finishOfflineProcessing() noexcept {
		return true;
	}
	/// Returns the fixed causal delay, in 48 kHz mono samples, introduced by
	/// this processor. Both the cleaned signal and the dry signal used by
	/// processInPlace() are placed on this timeline.
	virtual unsigned int latencySamples() const {
		return 0;
	}
	virtual void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) = 0;
	virtual QString activeModelId() const {
		return {};
	}
	virtual QString activeModelPath() const {
		return {};
	}
	virtual bool usedFallback() const {
		return false;
	}
	/// Optional asynchronous-inference diagnostics. Synchronous processors keep
	/// the default zero values; worker-backed processors expose model execution
	/// separately from the real-time callback handoff cost.
	virtual std::uint64_t workerProcessingFrames() const noexcept {
		return 0;
	}
	virtual std::uint64_t lastWorkerProcessingNanoseconds() const noexcept {
		return 0;
	}
	virtual std::uint64_t workerTotalProcessingNanoseconds() const noexcept {
		return 0;
	}
	virtual std::uint64_t workerMaximumProcessingNanoseconds() const noexcept {
		return 0;
	}
	virtual std::uint64_t workerProcessingP99Nanoseconds() const noexcept {
		return 0;
	}
	virtual unsigned int workerPendingFrames() const noexcept {
		return 0;
	}
	virtual unsigned int workerSchedulingDelayFrames() const noexcept {
		return 0;
	}
	virtual unsigned int workerSchedulingSlackFrames() const noexcept {
		return 0;
	}
};

std::unique_ptr< SpeechCleanupProcessor > createSpeechCleanupProcessor(
	const Mumble::SpeechCleanup::Selection &selection);

/// Receive-only framing/fallback adapter. Construction and prepareRealtime()
/// run on the preparation pool; processStereoFrame never waits or allocates.
class ReceiveSpeechCleanupProcessor final : public SpeechCleanupProcessor {
public:
	static constexpr unsigned int frameSamples = 480;
	ReceiveSpeechCleanupProcessor(std::unique_ptr< SpeechCleanupProcessor > processor, unsigned int maximumBurstFrames);
	bool isReady() const override;
	bool prepareRealtime() override;
	void reset() override;
	unsigned int latencySamples() const override;
	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) override;
	bool processStereoFrame(float *samples, unsigned int sampleCount, float mixFactor);
	bool usingDryFallback() const noexcept { return m_failed; }
	const SpeechCleanupProcessor *processor() const { return m_processor.get(); }
	bool prepareOfflineFrame() noexcept override;
	bool finishOfflineProcessing() noexcept override;

private:
	std::unique_ptr< SpeechCleanupProcessor > m_processor;
	unsigned int m_maximumBurstFrames;
	unsigned int m_latencySamples = 0;
	unsigned int m_delayPosition = 0;
	bool m_prepared = false;
	bool m_failed = false;
	std::vector< float > m_dryDelay;
	std::vector< unsigned char > m_monoDelay;
	std::array< float, frameSamples > m_monoFrame = {};
	std::array< float, frameSamples * 2 > m_dryFrame = {};
	std::array< unsigned char, frameSamples > m_wetEligible = {};
};

/// Cancellable off-callback model construction. Cancellation skips queued
/// factories and wakes a startup delay; an already executing backend constructor is
/// allowed to return before its result is discarded on the worker thread.
class SpeechCleanupPreparation final {
public:
	using Factory = std::function< std::unique_ptr< SpeechCleanupProcessor >() >;
	SpeechCleanupPreparation(QThreadPool &pool, Factory factory,
		std::chrono::steady_clock::time_point notBefore = {});
	~SpeechCleanupPreparation();
	SpeechCleanupPreparation(const SpeechCleanupPreparation &) = delete;
	SpeechCleanupPreparation &operator=(const SpeechCleanupPreparation &) = delete;

	void cancel();
	bool isFinished() const;
	std::unique_ptr< SpeechCleanupProcessor > takeResult();

private:
	struct Cancellation;
	std::shared_ptr< Cancellation > m_cancellation;
	QFuture< std::unique_ptr< SpeechCleanupProcessor > > m_future;
};

/// Model/worker destruction can join. Transfer it out of AudioOutput's map
/// lock so retiring an utterance cannot stall the next mixer callback.
void retireSpeechCleanupProcessor(std::unique_ptr< SpeechCleanupProcessor > processor);

/// Returns whether the selected noise-cancellation mode consumes a neural
/// speech-cleanup processor. Off and Speex must remain allocation-free even if
/// a neural backend/model remains selected in settings.
bool noiseCancelUsesSpeechCleanup(Settings::NoiseCancel mode) noexcept;

/// Keeps the normalized selection available for diagnostics while lazily
/// owning the heavyweight processor only for RNN/Both modes. Switching to Off
/// or Speex releases an existing processor immediately.
void reconcileSpeechCleanupProcessor(
	Settings::NoiseCancel mode, const Mumble::SpeechCleanup::Selection &requestedSelection,
	Mumble::SpeechCleanup::Selection &activeSelection,
	std::unique_ptr< SpeechCleanupProcessor > &processor);

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
