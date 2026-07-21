// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibration.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using Mumble::InputEnhancement::CalibrationSession;
using Mumble::InputEnhancement::Profile;

namespace {
using State         = CalibrationSession::State;
using FailureReason = CalibrationSession::FailureReason;
using LevelStatus   = CalibrationSession::LevelStatus;

int enumValue(State value) {
	return static_cast< int >(value);
}

int enumValue(FailureReason value) {
	return static_cast< int >(value);
}

int enumValue(LevelStatus value) {
	return static_cast< int >(value);
}

bool appendConstant(CalibrationSession &session, std::size_t samples, float value) {
	std::array< float, 480 > frame;
	frame.fill(value);
	while (samples > 0) {
		const std::size_t count = std::min(samples, frame.size());
		if (session.appendPcm(std::span< const float >(frame.data(), count)) != count) {
			return false;
		}
		samples -= count;
	}
	return true;
}

bool reachEvaluation(CalibrationSession &session, bool withLocalNoise) {
	if (!appendConstant(session, CalibrationSession::levelCheckSamples, 0.10f) || !session.advance()
		|| !appendConstant(session, CalibrationSession::roomNoiseSamples, 0.01f) || !session.advance()
		|| !appendConstant(session, CalibrationSession::guidedVoiceSamples, 0.15f) || !session.advance()) {
		return false;
	}
	if (withLocalNoise) {
		if (!appendConstant(session, CalibrationSession::localNoiseSamples, 0.04f) || !session.advance()) {
			return false;
		}
	}
	return session.state() == State::Evaluating;
}

CalibrationSession::Selection previousSelection() {
	return { Profile::Light, 31, 42, 1001 };
}

CalibrationSession::CandidateResult candidate(Profile profile, std::uint32_t token, double score,
											  bool eligible = true) {
	return { { profile, 55, 65, token }, score, eligible, true, true };
}

bool allZero(const std::vector< float > &storage) {
	return std::all_of(storage.cbegin(), storage.cend(), [](float value) { return value == 0.0f; });
}
} // namespace

static_assert(!std::is_copy_constructible_v< CalibrationSession >);
static_assert(!std::is_move_constructible_v< CalibrationSession >);
static_assert(noexcept(std::declval< CalibrationSession & >().appendPcm(std::span< const float >())));
static_assert(noexcept(std::declval< CalibrationSession & >().cancel()));
static_assert(noexcept(std::declval< CalibrationSession & >().abort()));

class TestInputEnhancementCalibration : public QObject {
	Q_OBJECT

private slots:
	void exactCaptureDurationsAndPromptBoundaries();
	void failedLevelCheckCanRetry();
	void optionalLocalNoiseCanBeSkipped();
	void localEvaluationCreatesBlindDraftAndApplyWipes();
	void cancelRestoresPreviousSelectionAndWipes();
	void invalidPcmFailsClosedAndRestores();
	void abortAndDestructionWipeExternalStorage();
	void insufficientStorageFailsClosed();
};

void TestInputEnhancementCalibration::exactCaptureDurationsAndPromptBoundaries() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples, 0.75f);
	CalibrationSession session(storage);
	QVERIFY(session.rawAudioCleared());
	QVERIFY(session.start(previousSelection(), true, 0x12345678ULL));
	QVERIFY(!session.transmissionAllowed());

	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples - 1, 0.1f));
	QCOMPARE(enumValue(session.state()), enumValue(State::LevelCheck));
	QVERIFY(appendConstant(session, 1, 0.1f));
	QCOMPARE(enumValue(session.state()), enumValue(State::LevelCheckReady));
	QCOMPARE(session.appendPcm(std::span< const float >()), std::size_t{ 0 });
	QCOMPARE(enumValue(session.levelMetrics().status), enumValue(LevelStatus::Good));
	QCOMPARE(session.levelMetrics().samples, CalibrationSession::levelCheckSamples);
	QVERIFY(std::abs(session.levelMetrics().rms - 0.1f) < 0.0001f);

	QVERIFY(session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::RoomNoise));
	QVERIFY(appendConstant(session, CalibrationSession::roomNoiseSamples - 1, 0.01f));
	QCOMPARE(enumValue(session.state()), enumValue(State::RoomNoise));
	QVERIFY(appendConstant(session, 1, 0.01f));
	QCOMPARE(enumValue(session.state()), enumValue(State::RoomNoiseReady));
	std::array< float, 1 > spill = { 0.9f };
	QCOMPARE(session.appendPcm(spill), std::size_t{ 0 });

	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, CalibrationSession::guidedVoiceSamples - 1, 0.15f));
	QCOMPARE(enumValue(session.state()), enumValue(State::GuidedVoice));
	QVERIFY(appendConstant(session, 1, 0.15f));
	QCOMPARE(enumValue(session.state()), enumValue(State::GuidedVoiceReady));

	QVERIFY(session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::LocalNoise));
	QVERIFY(appendConstant(session, CalibrationSession::localNoiseSamples - 1, 0.04f));
	QCOMPARE(enumValue(session.state()), enumValue(State::LocalNoise));
	QVERIFY(appendConstant(session, 1, 0.04f));
	QCOMPARE(enumValue(session.state()), enumValue(State::LocalNoiseReady));
	QVERIFY(session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::Evaluating));

	const CalibrationSession::CaptureView capture = session.captureView();
	QCOMPARE(capture.roomNoise.size(), CalibrationSession::roomNoiseSamples);
	QCOMPARE(capture.guidedVoice.size(), CalibrationSession::guidedVoiceSamples);
	QCOMPARE(capture.localNoise.size(), CalibrationSession::localNoiseSamples);
	QVERIFY(!session.transmissionAllowed());
}

void TestInputEnhancementCalibration::failedLevelCheckCanRetry() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationSession session(storage);
	QVERIFY(session.start(previousSelection(), false, 7));
	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.0001f));
	QCOMPARE(enumValue(session.levelMetrics().status), enumValue(LevelStatus::TooQuiet));
	QVERIFY(!session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::LevelCheck));
	QCOMPARE(enumValue(session.levelMetrics().status), enumValue(LevelStatus::Collecting));
	QCOMPARE(session.levelMetrics().samples, std::size_t{ 0 });
	QVERIFY(!session.transmissionAllowed());

	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 1.0f));
	QCOMPARE(enumValue(session.levelMetrics().status), enumValue(LevelStatus::Clipping));
	QVERIFY(!session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::LevelCheck));

	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.1f));
	QVERIFY(session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::RoomNoise));
}

void TestInputEnhancementCalibration::optionalLocalNoiseCanBeSkipped() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationSession session(storage);
	QVERIFY(session.start(previousSelection(), true, 8));
	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.1f));
	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, CalibrationSession::roomNoiseSamples, 0.01f));
	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, CalibrationSession::guidedVoiceSamples, 0.15f));
	QVERIFY(session.advance());
	QCOMPARE(enumValue(session.state()), enumValue(State::LocalNoise));
	QVERIFY(appendConstant(session, 12'000, 0.08f));
	QVERIFY(session.skipOptionalLocalNoise());
	QCOMPARE(enumValue(session.state()), enumValue(State::Evaluating));
	QCOMPARE(session.captureView().localNoise.size(), std::size_t{ 0 });
	QVERIFY(std::all_of(storage.cbegin()
							+ static_cast< std::ptrdiff_t >(CalibrationSession::roomNoiseSamples
															+ CalibrationSession::guidedVoiceSamples),
						storage.cend(), [](float value) { return value == 0.0f; }));
}

void TestInputEnhancementCalibration::localEvaluationCreatesBlindDraftAndApplyWipes() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationSession session(storage);
	QVERIFY(session.start(previousSelection(), false, 0xfeedbeefULL));
	QVERIFY(reachEvaluation(session, false));
	QVERIFY(!session.transmissionAllowed());

	QVERIFY(session.recordCandidate(candidate(Profile::Original, 11, 0.1)));
	QVERIFY(session.recordCandidate(candidate(Profile::Balanced, 22, 0.9)));
	QVERIFY(session.recordCandidate(candidate(Profile::Crisp, 33, 0.8)));
	QVERIFY(session.recordCandidate(candidate(Profile::Light, 44, 2.0, false)));
	QVERIFY(session.finishEvaluation());
	QCOMPARE(enumValue(session.state()), enumValue(State::BlindComparison));
	QVERIFY(!session.transmissionAllowed());
	const CalibrationSession::BlindComparison comparison = session.blindComparison();
	QCOMPARE(comparison.count, std::size_t{ 3 });
	std::array< std::uint32_t, 3 > comparisonRecipeTokens = {};
	for (std::size_t index = 0; index < comparison.count; ++index) {
		QVERIFY(comparison.playbackTokens[index] != 0);
		const CalibrationSession::Selection *selection =
			session.selectionForPlaybackToken(comparison.playbackTokens[index]);
		QVERIFY(selection);
		comparisonRecipeTokens[index] = selection->recipeToken;
		for (std::size_t prior = 0; prior < index; ++prior) {
			QVERIFY(comparison.playbackTokens[index] != comparison.playbackTokens[prior]);
		}
	}
	for (const std::uint32_t expectedToken : { 11U, 22U, 33U }) {
		QVERIFY(std::find(comparisonRecipeTokens.cbegin(), comparisonRecipeTokens.cend(), expectedToken)
				!= comparisonRecipeTokens.cend());
	}

	const CalibrationSession::BlindPair pair = session.blindPair();
	QVERIFY(pair.leftPlaybackToken != 0);
	QVERIFY(pair.rightPlaybackToken != 0);
	QVERIFY(pair.leftPlaybackToken != pair.rightPlaybackToken);
	const CalibrationSession::Selection *left  = session.selectionForPlaybackToken(pair.leftPlaybackToken);
	const CalibrationSession::Selection *right = session.selectionForPlaybackToken(pair.rightPlaybackToken);
	QVERIFY(left);
	QVERIFY(right);

	const CalibrationSession::Selection expected = *right;
	QVERIFY(session.selectBlindWinner(pair.rightPlaybackToken));
	QCOMPARE(enumValue(session.state()), enumValue(State::DraftReady));
	QVERIFY(session.draftSelection());
	QVERIFY(*session.draftSelection() == expected);
	QVERIFY(!session.transmissionAllowed());
	QVERIFY(session.apply());
	QCOMPARE(enumValue(session.state()), enumValue(State::Applied));
	QVERIFY(session.resultSelection());
	QVERIFY(*session.resultSelection() == expected);
	QVERIFY(session.transmissionAllowed());
	QVERIFY(session.rawAudioCleared());
	QVERIFY(allZero(storage));
}

void TestInputEnhancementCalibration::cancelRestoresPreviousSelectionAndWipes() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationSession session(storage);
	const CalibrationSession::Selection previous = previousSelection();
	QVERIFY(session.start(previous, false, 91));
	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.1f));
	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, CalibrationSession::roomNoiseSamples, 0.03f));
	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, 48'000, 0.2f));
	QVERIFY(!session.rawAudioCleared());
	QVERIFY(session.cancel());
	QCOMPARE(enumValue(session.state()), enumValue(State::Cancelled));
	QVERIFY(session.resultSelection());
	QVERIFY(*session.resultSelection() == previous);
	QVERIFY(session.transmissionAllowed());
	QVERIFY(session.rawAudioCleared());
	QVERIFY(allZero(storage));
}

void TestInputEnhancementCalibration::invalidPcmFailsClosedAndRestores() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples);
	CalibrationSession session(storage);
	const CalibrationSession::Selection previous = previousSelection();
	QVERIFY(session.start(previous, false, 92));
	QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.1f));
	QVERIFY(session.advance());
	QVERIFY(appendConstant(session, 10'000, 0.03f));
	std::array< float, 2 > invalid = { 0.1f, std::numeric_limits< float >::quiet_NaN() };
	QCOMPARE(session.appendPcm(invalid), std::size_t{ 0 });
	QCOMPARE(enumValue(session.state()), enumValue(State::Error));
	QCOMPARE(enumValue(session.failureReason()), enumValue(FailureReason::InvalidPcm));
	QVERIFY(session.resultSelection());
	QVERIFY(*session.resultSelection() == previous);
	QVERIFY(session.transmissionAllowed());
	QVERIFY(session.rawAudioCleared());
	QVERIFY(allZero(storage));
}

void TestInputEnhancementCalibration::abortAndDestructionWipeExternalStorage() {
	std::vector< float > abortedStorage(CalibrationSession::requiredStorageSamples);
	CalibrationSession aborted(abortedStorage);
	QVERIFY(aborted.start(previousSelection(), false, 93));
	QVERIFY(appendConstant(aborted, CalibrationSession::levelCheckSamples, 0.1f));
	QVERIFY(aborted.advance());
	QVERIFY(appendConstant(aborted, 24'000, 0.04f));
	QVERIFY(aborted.abort());
	QCOMPARE(enumValue(aborted.state()), enumValue(State::Aborted));
	QCOMPARE(enumValue(aborted.failureReason()), enumValue(FailureReason::AbortedByCaller));
	QVERIFY(aborted.transmissionAllowed());
	QVERIFY(allZero(abortedStorage));

	std::vector< float > destructorStorage(CalibrationSession::requiredStorageSamples);
	{
		CalibrationSession session(destructorStorage);
		QVERIFY(session.start(previousSelection(), false, 94));
		QVERIFY(appendConstant(session, CalibrationSession::levelCheckSamples, 0.1f));
		QVERIFY(session.advance());
		QVERIFY(appendConstant(session, 32'000, 0.05f));
		QVERIFY(!allZero(destructorStorage));
	}
	QVERIFY(allZero(destructorStorage));
}

void TestInputEnhancementCalibration::insufficientStorageFailsClosed() {
	std::vector< float > storage(CalibrationSession::requiredStorageSamples - 1, 0.5f);
	CalibrationSession session(storage);
	QCOMPARE(enumValue(session.state()), enumValue(State::Error));
	QCOMPARE(enumValue(session.failureReason()), enumValue(FailureReason::InvalidStorage));
	QVERIFY(session.transmissionAllowed());
	QVERIFY(session.rawAudioCleared());
	QVERIFY(allZero(storage));
	QVERIFY(!session.start(previousSelection(), false, 95));
}

QTEST_GUILESS_MAIN(TestInputEnhancementCalibration)

#include "TestInputEnhancementCalibration.moc"
