// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_

#include "SpeechCleanupProcessor.h"

#include <cstddef>
#include <memory>

class DeepFilterNetSpeechCleanup final : public SpeechCleanupProcessor {
public:
	explicit DeepFilterNetSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection);
	~DeepFilterNetSpeechCleanup() override;

	bool isReady() const override;
	void reset() override;
	bool prepareRealtime() override;
	bool prepareOfflineFrame() noexcept override;
	bool finishOfflineProcessing() noexcept override;
	unsigned int latencySamples() const override;
	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) override;
	QString activeModelId() const override;
	QString activeModelPath() const override;
	bool usedFallback() const override;
	std::uint64_t workerProcessingFrames() const noexcept override;
	std::uint64_t lastWorkerProcessingNanoseconds() const noexcept override;
	std::uint64_t workerTotalProcessingNanoseconds() const noexcept override;
	std::uint64_t workerMaximumProcessingNanoseconds() const noexcept override;
	std::uint64_t workerProcessingP99Nanoseconds() const noexcept override;
	unsigned int workerPendingFrames() const noexcept override;
	unsigned int workerSchedulingDelayFrames() const noexcept override;
	unsigned int workerSchedulingSlackFrames() const noexcept override;

private:
	class Implementation;

	std::unique_ptr< Implementation > m_impl;
};

#endif // MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_
