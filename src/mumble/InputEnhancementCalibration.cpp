// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibration.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace Mumble::InputEnhancement {
namespace {
	constexpr float minimumLevelRms = 0.005f;
	constexpr float clippingPeak    = 0.995f;

	void secureZero(float *storage, std::size_t samples) noexcept {
		if (!storage) {
			return;
		}

		volatile float *volatileStorage = storage;
		for (std::size_t index = 0; index < samples; ++index) {
			volatileStorage[index] = 0.0f;
		}
		std::atomic_signal_fence(std::memory_order_seq_cst);
	}

	bool validPcm(std::span< const float > samples) noexcept {
		for (const float sample : samples) {
			if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f) {
				return false;
			}
		}
		return true;
	}
} // namespace

CalibrationSession::CalibrationSession() : m_ownedStorage(std::make_unique< float[] >(requiredStorageSamples)) {
	initializeStorage(m_ownedStorage.get(), requiredStorageSamples);
}

CalibrationSession::CalibrationSession(std::span< float > preallocatedStorage) noexcept {
	initializeStorage(preallocatedStorage.data(), preallocatedStorage.size());
}

CalibrationSession::~CalibrationSession() {
	clearRawAudio();
}

void CalibrationSession::initializeStorage(float *storage, std::size_t storageSamples) noexcept {
	m_storage        = storage;
	m_storageSamples = storageSamples;
	secureZero(m_storage, std::min(m_storageSamples, requiredStorageSamples));
	m_storageValid = m_storage && m_storageSamples >= requiredStorageSamples;
	if (!m_storageValid) {
		m_state         = State::Error;
		m_failureReason = FailureReason::InvalidStorage;
	}
}

bool CalibrationSession::start(const Selection &previousSelection, bool captureOptionalLocalNoise,
							   std::uint64_t blindSeed) noexcept {
	if (!m_storageValid || active() || !selectionValid(previousSelection)) {
		return false;
	}

	clearRawAudio();
	m_previousSelection         = previousSelection;
	m_draftSelection            = {};
	m_resultSelection           = {};
	m_hasDraft                  = false;
	m_hasResult                 = false;
	m_captureOptionalLocalNoise = captureOptionalLocalNoise;
	m_blindSeed                 = blindSeed;
	m_failureReason             = FailureReason::None;
	m_candidateCount            = 0;
	m_leftCandidate             = 0;
	m_rightCandidate            = 0;
	m_blindPair                 = {};
	resetLevelCheck();
	m_state = State::LevelCheck;
	return true;
}

std::size_t CalibrationSession::appendPcm(std::span< const float > samples) noexcept {
	if (samples.empty()) {
		return 0;
	}

	switch (m_state) {
		case State::LevelCheck:
			return appendLevelCheck(samples);
		case State::RoomNoise:
			return appendRaw(samples, 0, roomNoiseSamples, m_roomCaptured, State::RoomNoiseReady);
		case State::GuidedVoice:
			return appendRaw(samples, roomNoiseSamples, guidedVoiceSamples, m_voiceCaptured, State::GuidedVoiceReady);
		case State::LocalNoise:
			return appendRaw(samples, roomNoiseSamples + guidedVoiceSamples, localNoiseSamples, m_localNoiseCaptured,
							 State::LocalNoiseReady);
		default:
			return 0;
	}
}

std::size_t CalibrationSession::appendLevelCheck(std::span< const float > samples) noexcept {
	const std::size_t remaining          = levelCheckSamples - m_levelMetrics.samples;
	const std::size_t accepted           = std::min(remaining, samples.size());
	const std::span< const float > input = samples.first(accepted);
	if (!validPcm(input)) {
		fail(FailureReason::InvalidPcm);
		return 0;
	}

	for (const float sample : input) {
		const float magnitude = std::abs(sample);
		m_levelMetrics.peak   = std::max(m_levelMetrics.peak, magnitude);
		m_levelSquareSum += static_cast< double >(sample) * static_cast< double >(sample);
	}
	m_levelMetrics.samples += accepted;

	if (m_levelMetrics.samples == levelCheckSamples) {
		m_levelMetrics.rms =
			static_cast< float >(std::sqrt(m_levelSquareSum / static_cast< double >(levelCheckSamples)));
		if (m_levelMetrics.peak >= clippingPeak) {
			m_levelMetrics.status = LevelStatus::Clipping;
		} else if (m_levelMetrics.rms < minimumLevelRms) {
			m_levelMetrics.status = LevelStatus::TooQuiet;
		} else {
			m_levelMetrics.status = LevelStatus::Good;
		}
		m_state = State::LevelCheckReady;
	}

	return accepted;
}

std::size_t CalibrationSession::appendRaw(std::span< const float > samples, std::size_t offset,
										  std::size_t targetSamples, std::size_t &capturedSamples,
										  State readyState) noexcept {
	const std::size_t remaining          = targetSamples - capturedSamples;
	const std::size_t accepted           = std::min(remaining, samples.size());
	const std::span< const float > input = samples.first(accepted);
	if (!validPcm(input)) {
		fail(FailureReason::InvalidPcm);
		return 0;
	}

	std::memcpy(m_storage + offset + capturedSamples, input.data(), accepted * sizeof(float));
	capturedSamples += accepted;
	if (capturedSamples == targetSamples) {
		m_state = readyState;
	}
	return accepted;
}

bool CalibrationSession::advance() noexcept {
	switch (m_state) {
		case State::LevelCheckReady:
			if (m_levelMetrics.status != LevelStatus::Good) {
				resetLevelCheck();
				m_state = State::LevelCheck;
				return false;
			}
			m_state = State::RoomNoise;
			return true;
		case State::RoomNoiseReady:
			m_state = State::GuidedVoice;
			return true;
		case State::GuidedVoiceReady:
			if (m_captureOptionalLocalNoise) {
				m_state = State::LocalNoise;
			} else {
				enterEvaluation();
			}
			return true;
		case State::LocalNoiseReady:
			enterEvaluation();
			return true;
		default:
			return false;
	}
}

bool CalibrationSession::skipOptionalLocalNoise() noexcept {
	if (m_state != State::LocalNoise && m_state != State::LocalNoiseReady) {
		return false;
	}

	secureZero(m_storage + roomNoiseSamples + guidedVoiceSamples, localNoiseSamples);
	m_localNoiseCaptured = 0;
	enterEvaluation();
	return true;
}

CalibrationSession::CaptureView CalibrationSession::captureView() const noexcept {
	if (m_state != State::Evaluating && m_state != State::BlindComparison && m_state != State::DraftReady) {
		return {};
	}

	return {
		std::span< const float >(m_storage, m_roomCaptured),
		std::span< const float >(m_storage + roomNoiseSamples, m_voiceCaptured),
		std::span< const float >(m_storage + roomNoiseSamples + guidedVoiceSamples, m_localNoiseCaptured),
	};
}

bool CalibrationSession::recordCandidate(const CandidateResult &candidate) noexcept {
	if (m_state != State::Evaluating) {
		return false;
	}
	if (m_candidateCount >= maximumCandidates) {
		fail(FailureReason::CandidateCapacityExceeded);
		return false;
	}
	if (!selectionValid(candidate.selection) || candidate.selection.recipeToken == 0
		|| !std::isfinite(candidate.objectiveScore)
		|| (candidate.eligible && (!candidate.localPipelineAndOpusEvaluated || !candidate.loudnessMatched))) {
		fail(FailureReason::InvalidCandidate);
		return false;
	}
	for (std::size_t index = 0; index < m_candidateCount; ++index) {
		if (m_candidates[index].result.selection.recipeToken == candidate.selection.recipeToken) {
			fail(FailureReason::InvalidCandidate);
			return false;
		}
	}

	m_candidates[m_candidateCount++].result = candidate;
	return true;
}

bool CalibrationSession::finishEvaluation() noexcept {
	if (m_state != State::Evaluating) {
		return false;
	}

	std::size_t best   = maximumCandidates;
	std::size_t second = maximumCandidates;
	auto better        = [this](std::size_t lhs, std::size_t rhs) noexcept {
        if (rhs == maximumCandidates) {
            return true;
        }
        const CandidateResult &left  = m_candidates[lhs].result;
        const CandidateResult &right = m_candidates[rhs].result;
        if (left.objectiveScore != right.objectiveScore) {
            return left.objectiveScore > right.objectiveScore;
        }
        return left.selection.recipeToken < right.selection.recipeToken;
	};

	for (std::size_t index = 0; index < m_candidateCount; ++index) {
		if (!m_candidates[index].result.eligible) {
			continue;
		}
		if (better(index, best)) {
			second = best;
			best   = index;
		} else if (better(index, second)) {
			second = index;
		}
	}

	if (best == maximumCandidates || second == maximumCandidates) {
		fail(FailureReason::EvaluationFailed);
		return false;
	}

	const bool swapSides           = (mixToken(m_blindSeed ^ 0x6a09e667f3bcc909ULL) & 1U) != 0;
	m_leftCandidate                = swapSides ? second : best;
	m_rightCandidate               = swapSides ? best : second;
	m_blindPair.leftPlaybackToken  = mixToken(m_blindSeed ^ 0xbb67ae8584caa73bULL);
	m_blindPair.rightPlaybackToken = mixToken(m_blindSeed ^ 0x3c6ef372fe94f82bULL);
	if (m_blindPair.leftPlaybackToken == 0) {
		m_blindPair.leftPlaybackToken = 1;
	}
	if (m_blindPair.rightPlaybackToken == 0 || m_blindPair.rightPlaybackToken == m_blindPair.leftPlaybackToken) {
		m_blindPair.rightPlaybackToken = m_blindPair.leftPlaybackToken ^ 0xa54ff53a5f1d36f1ULL;
	}
	m_state = State::BlindComparison;
	return true;
}

bool CalibrationSession::failEvaluation() noexcept {
	if (m_state != State::Evaluating) {
		return false;
	}
	fail(FailureReason::EvaluationFailed);
	return true;
}

CalibrationSession::BlindPair CalibrationSession::blindPair() const noexcept {
	if (m_state != State::BlindComparison && m_state != State::DraftReady) {
		return {};
	}
	return m_blindPair;
}

const CalibrationSession::Selection *
	CalibrationSession::selectionForPlaybackToken(std::uint64_t playbackToken) const noexcept {
	if (m_state != State::BlindComparison && m_state != State::DraftReady) {
		return nullptr;
	}
	if (playbackToken == m_blindPair.leftPlaybackToken && m_leftCandidate < m_candidateCount) {
		return &m_candidates[m_leftCandidate].result.selection;
	}
	if (playbackToken == m_blindPair.rightPlaybackToken && m_rightCandidate < m_candidateCount) {
		return &m_candidates[m_rightCandidate].result.selection;
	}
	return nullptr;
}

bool CalibrationSession::selectBlindWinner(std::uint64_t playbackToken) noexcept {
	if (m_state != State::BlindComparison) {
		return false;
	}
	const Selection *winner = selectionForPlaybackToken(playbackToken);
	if (!winner) {
		return false;
	}
	m_draftSelection = *winner;
	m_hasDraft       = true;
	m_state          = State::DraftReady;
	return true;
}

bool CalibrationSession::apply() noexcept {
	if (m_state != State::DraftReady || !m_hasDraft) {
		return false;
	}
	m_resultSelection = m_draftSelection;
	m_hasResult       = true;
	clearRawAudio();
	m_state = State::Applied;
	return true;
}

bool CalibrationSession::cancel() noexcept {
	if (!active()) {
		return false;
	}
	m_resultSelection = m_previousSelection;
	m_hasResult       = true;
	clearRawAudio();
	m_state = State::Cancelled;
	return true;
}

bool CalibrationSession::abort() noexcept {
	if (!active()) {
		return false;
	}
	m_resultSelection = m_previousSelection;
	m_hasResult       = true;
	m_failureReason   = FailureReason::AbortedByCaller;
	clearRawAudio();
	m_state = State::Aborted;
	return true;
}

CalibrationSession::State CalibrationSession::state() const noexcept {
	return m_state;
}

CalibrationSession::FailureReason CalibrationSession::failureReason() const noexcept {
	return m_failureReason;
}

CalibrationSession::LevelMetrics CalibrationSession::levelMetrics() const noexcept {
	return m_levelMetrics;
}

const CalibrationSession::Selection *CalibrationSession::draftSelection() const noexcept {
	return m_hasDraft ? &m_draftSelection : nullptr;
}

const CalibrationSession::Selection *CalibrationSession::resultSelection() const noexcept {
	return m_hasResult ? &m_resultSelection : nullptr;
}

bool CalibrationSession::transmissionAllowed() const noexcept {
	return !active();
}

bool CalibrationSession::rawAudioCleared() const noexcept {
	const std::size_t samples = std::min(m_storageSamples, requiredStorageSamples);
	for (std::size_t index = 0; index < samples; ++index) {
		if (m_storage[index] != 0.0f) {
			return false;
		}
	}
	return true;
}

bool CalibrationSession::active() const noexcept {
	switch (m_state) {
		case State::LevelCheck:
		case State::LevelCheckReady:
		case State::RoomNoise:
		case State::RoomNoiseReady:
		case State::GuidedVoice:
		case State::GuidedVoiceReady:
		case State::LocalNoise:
		case State::LocalNoiseReady:
		case State::Evaluating:
		case State::BlindComparison:
		case State::DraftReady:
			return true;
		case State::Idle:
		case State::Applied:
		case State::Cancelled:
		case State::Aborted:
		case State::Error:
			return false;
	}
	return false;
}

bool CalibrationSession::selectionValid(const Selection &selection) const noexcept {
	if (selection.noiseReduction < 0 || selection.noiseReduction > 100 || selection.naturalCrisp < 0
		|| selection.naturalCrisp > 100) {
		return false;
	}
	switch (selection.profile) {
		case Profile::Original:
		case Profile::Light:
		case Profile::Balanced:
		case Profile::Quality:
		case Profile::VoiceFocus:
		case Profile::Auto:
			return true;
	}
	return false;
}

void CalibrationSession::resetLevelCheck() noexcept {
	m_levelMetrics   = {};
	m_levelSquareSum = 0.0;
}

void CalibrationSession::enterEvaluation() noexcept {
	m_candidateCount = 0;
	m_blindPair      = {};
	m_hasDraft       = false;
	m_state          = State::Evaluating;
}

void CalibrationSession::fail(FailureReason reason) noexcept {
	m_failureReason   = reason;
	m_resultSelection = m_previousSelection;
	m_hasResult       = true;
	clearRawAudio();
	m_state = State::Error;
}

void CalibrationSession::clearRawAudio() noexcept {
	secureZero(m_storage, std::min(m_storageSamples, requiredStorageSamples));
	m_roomCaptured       = 0;
	m_voiceCaptured      = 0;
	m_localNoiseCaptured = 0;
}

std::uint64_t CalibrationSession::mixToken(std::uint64_t value) noexcept {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

} // namespace Mumble::InputEnhancement
