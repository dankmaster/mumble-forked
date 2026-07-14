// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_

#include "SpeechCleanupProcessor.h"

#include <array>
#include <cstdio>

struct DenoiseState;

class RNNoiseSpeechCleanup final : public SpeechCleanupProcessor {
public:
	explicit RNNoiseSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection);
	~RNNoiseSpeechCleanup() override;

	bool isReady() const override;
	void reset() override;
	unsigned int latencySamples() const override;
	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) override;
	QString activeModelId() const override;
	QString activeModelPath() const override;
	bool usedFallback() const override;

private:
	static constexpr unsigned int FRAME_SIZE = 480;
	// The raw RNNoise API reconstructs a frame two frames behind the input. The
	// sample-streaming adapter must also collect one complete frame before it can
	// expose that API output. The resulting signal timeline is therefore delayed
	// by three 10 ms frames at 48 kHz.
	static constexpr unsigned int LATENCY_SAMPLES = 3 * FRAME_SIZE;

	Mumble::SpeechCleanup::Selection m_selection;
	QString m_activeModelId;
	QString m_activeModelPath;
	bool m_usedFallback = false;
	struct RNNModel *m_model = nullptr;
	std::FILE *m_modelFile = nullptr;
	DenoiseState *m_state = nullptr;
	std::array< float, FRAME_SIZE > m_inputBuffer  = {};
	unsigned int m_inputBufferSize = 0;
	std::array< float, FRAME_SIZE > m_outputBuffer = {};
	unsigned int m_outputBufferPosition = FRAME_SIZE;
	std::array< float, LATENCY_SAMPLES > m_dryDelayBuffer = {};
	unsigned int m_dryDelayPosition = 0;
};

#endif // MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_
