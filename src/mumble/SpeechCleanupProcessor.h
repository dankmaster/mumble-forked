// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_

#include "SpeechCleanup.h"

#include <QString>

#include <cstdint>
#include <memory>

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

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
