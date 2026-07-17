// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementLightProcessor.h"

#include <algorithm>
#include <chrono>

namespace Mumble::InputEnhancement {

bool LightProcessor::configure(const Recipe &recipe, Pipeline &pipeline) {
	reset();
	if (recipe.effectiveProfile() != Profile::Light || recipe.engine() != Engine::Speex
		|| recipe.usesNeuralProcessor() || !pipeline.configure(recipe)) {
		return false;
	}

	m_pipeline      = &pipeline;
	m_suppressionDb = lightSpeexSuppressionDb(recipe.noiseReduction());
	if (!m_preprocessor.init(sampleRateHz, frameSamples) || !m_preprocessor.setEchoState(nullptr)
		|| !m_preprocessor.setAGC(false) || !m_preprocessor.setVAD(false)
		|| !m_preprocessor.setDereverb(false) || !m_preprocessor.setDenoise(true)
		|| !m_preprocessor.setNoiseSuppress(m_suppressionDb)) {
		pipeline.markClassicProcessingFailure(FallbackReason::ProcessorUnavailable);
		m_preprocessor.deinit();
		m_pipeline = nullptr;
		return false;
	}

	m_ready = true;
	return true;
}

void LightProcessor::reset() noexcept {
	m_preprocessor.deinit();
	m_pipeline = nullptr;
	m_dryFrame.fill(0);
	m_noisePsd.fill(0);
	m_suppressionDb         = 0;
	m_lastSpeechProbability = 100;
	m_lastNoisePsdSum       = 0;
	m_ready                 = false;
}

bool LightProcessor::processFrame(std::int16_t *samples, unsigned int sampleCount) noexcept {
	if (!m_pipeline) {
		return false;
	}
	if (!samples || sampleCount != frameSamples) {
		m_pipeline->markClassicProcessingFailure(FallbackReason::InvalidFrame);
		return false;
	}

	std::copy_n(samples, frameSamples, m_dryFrame.begin());
	if (m_pipeline->fallbackActive()) {
		return m_pipeline->mixClassicFrame(samples, m_dryFrame.data(), frameSamples, 0, 0);
	}
	if (!m_ready) {
		m_pipeline->markClassicProcessingFailure(FallbackReason::ProcessorUnavailable);
		m_pipeline->mixClassicFrame(samples, m_dryFrame.data(), frameSamples, 0, 0);
		return false;
	}

	const auto startedAt = std::chrono::steady_clock::now();
	m_preprocessor.run(*samples);
	m_lastSpeechProbability = std::clamp(m_preprocessor.getSpeechProb(), 0, 100);
	const bool noisePsdValid = m_preprocessor.getNoisePSD(m_noisePsd.data(), m_noisePsd.size());
	m_lastNoisePsdSum        = 0;
	if (noisePsdValid) {
		for (const std::int32_t value : m_noisePsd) {
			m_lastNoisePsdSum += static_cast< std::uint64_t >(std::max(value, std::int32_t { 0 }));
		}
	}

	if (!noisePsdValid) {
		const auto finishedAt = std::chrono::steady_clock::now();
		m_pipeline->recordClassicProcessingFrame(static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::nanoseconds >(finishedAt - startedAt).count()));
		m_pipeline->markClassicProcessingFailure(FallbackReason::ProcessorNotReady);
		m_pipeline->mixClassicFrame(samples, m_dryFrame.data(), frameSamples, m_lastSpeechProbability, 0);
		return false;
	}

	const bool mixed = m_pipeline->mixClassicFrame(samples, m_dryFrame.data(), frameSamples,
												 m_lastSpeechProbability, m_lastNoisePsdSum);
	const auto finishedAt = std::chrono::steady_clock::now();
	m_pipeline->recordClassicProcessingFrame(static_cast< std::uint64_t >(
		std::chrono::duration_cast< std::chrono::nanoseconds >(finishedAt - startedAt).count()));
	if (m_pipeline->fallbackActive()) {
		m_pipeline->restoreClassicAlignedDryFrame(samples, sampleCount);
		return false;
	}
	return mixed;
}

bool LightProcessor::agcEnabled() const noexcept {
	return ready() && m_preprocessor.usesAGC();
}

bool LightProcessor::vadEnabled() const noexcept {
	return ready() && m_preprocessor.usesVAD();
}

bool LightProcessor::dereverbEnabled() const noexcept {
	return ready() && m_preprocessor.usesDereverb();
}

bool LightProcessor::denoiseEnabled() const noexcept {
	return ready() && m_preprocessor.usesDenoise();
}

bool LightProcessor::copySignalPsd(std::int32_t *values, std::size_t count) const noexcept {
	return ready() && m_preprocessor.getPSD(values, count);
}

} // namespace Mumble::InputEnhancement
