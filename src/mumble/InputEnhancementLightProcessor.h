// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTLIGHTPROCESSOR_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTLIGHTPROCESSOR_H_

#include "AudioPreprocessor.h"
#include "InputEnhancement.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Mumble::InputEnhancement {

/// The complete executable Light recipe shared by live capture, calibration,
/// and the offline benchmark. Configuration may allocate inside Speex and must
/// therefore happen off the audio callback. processFrame() uses only fixed
/// storage and calls no locks or allocating APIs.
class LightProcessor final {
public:
	LightProcessor() = default;
	~LightProcessor() = default;

	LightProcessor(const LightProcessor &)            = delete;
	LightProcessor(LightProcessor &&)                 = delete;
	LightProcessor &operator=(const LightProcessor &) = delete;
	LightProcessor &operator=(LightProcessor &&)      = delete;

	/// Configures both the product Pipeline and a dedicated denoise-only Speex
	/// state. AGC, VAD, dereverb, and echo cancellation remain disabled here;
	/// the established Mumble preprocessor runs separately after this stage.
	bool configure(const Recipe &recipe, Pipeline &pipeline);
	void reset() noexcept;

	bool ready() const noexcept { return m_ready && m_pipeline; }
	bool processFrame(std::int16_t *samples, unsigned int sampleCount = frameSamples) noexcept;

	int suppressionDb() const noexcept { return m_suppressionDb; }
	int lastSpeechProbability() const noexcept { return m_lastSpeechProbability; }
	std::uint64_t lastNoisePsdSum() const noexcept { return m_lastNoisePsdSum; }
	std::uint64_t lastSignalPsdSum() const noexcept { return m_lastSignalPsdSum; }
	bool agcEnabled() const noexcept;
	bool vadEnabled() const noexcept;
	bool dereverbEnabled() const noexcept;
	bool denoiseEnabled() const noexcept;
	/// Benchmark-only fixed-storage diagnostic. It does not participate in the
	/// product mix and is never called by AudioInput.
	bool copySignalPsd(std::int32_t *values, std::size_t count) const noexcept;

private:
	AudioPreprocessor m_preprocessor;
	Pipeline *m_pipeline = nullptr;
	std::array< std::int16_t, frameSamples > m_dryFrame = {};
	std::array< std::int32_t, frameSamples > m_noisePsd = {};
	std::array< std::int32_t, frameSamples > m_signalPsd = {};
	int m_suppressionDb                           = 0;
	int m_lastSpeechProbability                   = 100;
	std::uint64_t m_lastNoisePsdSum               = 0;
	std::uint64_t m_lastSignalPsdSum              = 0;
	bool m_signalPsdValid                         = false;
	bool m_ready                                  = false;
};

} // namespace Mumble::InputEnhancement

#endif
