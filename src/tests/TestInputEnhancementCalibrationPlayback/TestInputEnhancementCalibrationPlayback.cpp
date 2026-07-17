// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationPlayback.h"

#include <QtCore/QElapsedTimer>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <stop_token>
#include <vector>

namespace {

using Playback = Mumble::InputEnhancement::CalibrationPlayback;

struct BackendControl final {
	std::mutex mutex;
	std::condition_variable condition;
	std::vector< float > observed;
	Playback::Target lastTarget;
	int started       = 0;
	int stopped       = 0;
	int active        = 0;
	int maximumActive = 0;
	int readyReturned = 0;
	std::vector< const Playback::Clip * > liveClips;
	bool inspect                 = false;
	bool inspected               = false;
	bool failStartup             = false;
	bool failAfterStart          = false;
	bool finishNaturally         = false;
	bool ignoreStopUntilReleased = false;
	bool releaseAll              = false;
	bool holdAfterStopped        = false;
	bool releaseAfterStopped     = false;
};

class FakeBackend final : public Playback::Backend {
public:
	explicit FakeBackend(std::shared_ptr< BackendControl > control) : m_control(std::move(control)) {}

	Playback::StartResult run(const Playback::Clip &mono48k, const Playback::Target &target,
							  const std::stop_token stopToken, Playback::ReadyCallback ready) override {
		bool failStartup             = false;
		bool failAfterStart          = false;
		bool finishNaturally         = false;
		bool ignoreStopUntilReleased = false;
		{
			const std::lock_guard lock(m_control->mutex);
			++m_control->started;
			++m_control->active;
			m_control->maximumActive = std::max(m_control->maximumActive, m_control->active);
			m_control->lastTarget    = target;
			m_control->liveClips.push_back(&mono48k);
			failStartup             = m_control->failStartup;
			failAfterStart          = m_control->failAfterStart;
			finishNaturally         = m_control->finishNaturally;
			ignoreStopUntilReleased = m_control->ignoreStopUntilReleased;
		}
		m_control->condition.notify_all();
		auto deliverReady = [&](const Playback::StartResult result) {
			ready(result);
			{
				const std::lock_guard lock(m_control->mutex);
				++m_control->readyReturned;
			}
			m_control->condition.notify_all();
		};
		if (stopToken.stop_requested()) {
			finish(&mono48k);
			return { Playback::Error::None, false };
		}

		if (failStartup) {
			deliverReady({ Playback::Error::DeviceUnavailable, false });
			finish(&mono48k);
			return { Playback::Error::DeviceUnavailable, false };
		}

		deliverReady({ Playback::Error::None, false });
		if (failAfterStart) {
			finish(&mono48k);
			return { Playback::Error::BackendFailure, false };
		}
		if (finishNaturally) {
			finish(&mono48k);
			return { Playback::Error::None, false };
		}
		std::stop_callback wake(stopToken, [control = m_control] { control->condition.notify_all(); });
		std::unique_lock lock(m_control->mutex);
		for (;;) {
			m_control->condition.wait(lock, [&] {
				return m_control->releaseAll || m_control->inspect
					   || (!ignoreStopUntilReleased && stopToken.stop_requested());
			});
			if (m_control->inspect) {
				m_control->observed.resize(mono48k.sampleCount() * 2U);
				if (!mono48k.writeInterleavedStereo(0, m_control->observed, 1.0f)) {
					m_control->observed.clear();
				}
				m_control->inspect   = false;
				m_control->inspected = true;
				m_control->condition.notify_all();
			}
			if (m_control->releaseAll || (!ignoreStopUntilReleased && stopToken.stop_requested())) {
				break;
			}
		}
		lock.unlock();
		finish(&mono48k);
		return { Playback::Error::None, false };
	}

private:
	void finish(const Playback::Clip *clip) {
		{
			std::unique_lock lock(m_control->mutex);
			--m_control->active;
			++m_control->stopped;
			const auto position = std::find(m_control->liveClips.begin(), m_control->liveClips.end(), clip);
			if (position != m_control->liveClips.end()) {
				m_control->liveClips.erase(position);
			}
			m_control->condition.notify_all();
			m_control->condition.wait(lock,
										  [this] { return !m_control->holdAfterStopped || m_control->releaseAfterStopped; });
		}
		m_control->condition.notify_all();
	}

	std::shared_ptr< BackendControl > m_control;
};

std::unique_ptr< Playback > makePlayback(const std::shared_ptr< BackendControl > &control) {
	return std::make_unique< Playback >(std::make_unique< FakeBackend >(control));
}

Playback::Target testTarget() {
	Playback::Target target;
	target.outputBackend = QStringLiteral("WASAPI");
	target.endpointId    = QStringLiteral("test-endpoint");
	target.gain          = 0.75f;
	target.role          = Playback::EndpointRole::Multimedia;
	return target;
}

} // namespace

class TestInputEnhancementCalibrationPlayback : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void ownsInputUntilSynchronousStop();
	void rejectsInvalidPcmBeforeBackendStart();
	void clipWritesOnlyBoundedStereoChunks();
	void stopIsBoundedAndWipesClipWhileBackendIsWedged();
	void normalReplacementIsProcessWideSerial();
	void wedgedReplacementFailsBusyWithoutSecondWorker();
	void wedgedSecondInstanceRespectsProcessWideBudget();
	void staleQueuedEventsAreIgnoredAfterGenerationAdvance();
	void startupFailureIsReportedAsynchronously();
	void postStartFailureIsReported();
	void naturalCompletionIsReported();
	void processWideIdleBarrierTracksFullWorkerCleanup();
	void destructorDoesNotJoinWedgedBackend();
	void windowsDefaultEndpointSmoke();
};

void TestInputEnhancementCalibrationPlayback::cleanup() {
	QVERIFY2(Playback::waitForProcessIdle(std::chrono::seconds(2)),
			 "detached calibration playback worker did not release its process-wide lease");
}

void TestInputEnhancementCalibrationPlayback::ownsInputUntilSynchronousStop() {
	auto control  = std::make_shared< BackendControl >();
	auto playback = makePlayback(control);
	std::vector< float > source(960);
	for (std::size_t index = 0; index < source.size(); ++index) {
		source[index] = static_cast< float >(index + 1) / 2000.0f;
	}
	std::vector< float > expected(source.size() * 2U);
	for (std::size_t index = 0; index < source.size(); ++index) {
		expected[index * 2U]     = source[index];
		expected[index * 2U + 1] = source[index];
	}

	QVERIFY(playback->start(source, testTarget()));
	QVERIFY(playback->active());
	std::fill(source.begin(), source.end(), 0.0f);
	{
		std::unique_lock lock(control->mutex);
		control->inspect = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->inspected; }));
		QCOMPARE(control->observed, expected);
		QCOMPARE(control->lastTarget.endpointId, QStringLiteral("test-endpoint"));
		QCOMPARE(control->lastTarget.gain, 0.75f);
	}

	playback->stop();
	QVERIFY(!playback->active());
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
}

void TestInputEnhancementCalibrationPlayback::rejectsInvalidPcmBeforeBackendStart() {
	auto control  = std::make_shared< BackendControl >();
	auto playback = makePlayback(control);
	const std::vector< float > empty;
	QCOMPARE(playback->start(empty, testTarget()).error, Playback::Error::InvalidClip);

	std::vector< float > nonFinite(480, 0.1f);
	nonFinite[17] = std::numeric_limits< float >::quiet_NaN();
	QCOMPARE(playback->start(nonFinite, testTarget()).error, Playback::Error::InvalidClip);

	std::vector< float > tooLong(Playback::maximumSampleCount + 1U, 0.0f);
	QCOMPARE(playback->start(tooLong, testTarget()).error, Playback::Error::InvalidClip);
	QCOMPARE(control->started, 0);
}

void TestInputEnhancementCalibrationPlayback::clipWritesOnlyBoundedStereoChunks() {
	auto control                     = std::make_shared< BackendControl >();
	control->ignoreStopUntilReleased = true;
	auto playback                    = makePlayback(control);
	const std::vector< float > source(Playback::maximumQueueFrames + 1U, 0.75f);
	QVERIFY(playback->start(source, testTarget()));
	const Playback::Clip *liveClip = nullptr;
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 1; }));
		liveClip = control->liveClips.front();
	}

	std::vector< float > bounded(Playback::maximumQueueFrames * 2U);
	QVERIFY(liveClip->writeInterleavedStereo(0, bounded, 2.0f));
	QVERIFY(std::all_of(bounded.cbegin(), bounded.cend(), [](const float sample) { return sample == 1.0f; }));
	std::vector< float > oversized((Playback::maximumQueueFrames + 1U) * 2U);
	QVERIFY(!liveClip->writeInterleavedStereo(0, oversized, 1.0f));
	std::vector< float > finalFrame(2U);
	QVERIFY(liveClip->writeInterleavedStereo(Playback::maximumQueueFrames, finalFrame, 1.0f));
	QCOMPARE(finalFrame[0], 0.75f);
	QCOMPARE(finalFrame[1], 0.75f);

	playback->stop();
	{
		std::unique_lock lock(control->mutex);
		control->releaseAll = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
}

void TestInputEnhancementCalibrationPlayback::stopIsBoundedAndWipesClipWhileBackendIsWedged() {
	auto control                     = std::make_shared< BackendControl >();
	control->ignoreStopUntilReleased = true;
	auto playback                    = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	const Playback::Clip *liveClip = nullptr;
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 1; }));
		QCOMPARE(control->liveClips.size(), 1U);
		liveClip = control->liveClips.front();
	}

	QElapsedTimer timer;
	timer.start();
	playback->stop();
	QVERIFY2(timer.elapsed() < 100, qPrintable(QStringLiteral("stop blocked for %1 ms").arg(timer.elapsed())));
	QVERIFY(!playback->active());
	std::vector< float > destination(source.size() * 2U);
	QVERIFY(!liveClip->writeInterleavedStereo(0, destination, 1.0f));

	{
		std::unique_lock lock(control->mutex);
		control->releaseAll = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
}

void TestInputEnhancementCalibrationPlayback::normalReplacementIsProcessWideSerial() {
	auto control  = std::make_shared< BackendControl >();
	auto playback = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] {
			return control->started == 1 && control->readyReturned == 1;
		}));
	}
	QVERIFY(playback->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] {
			return control->started == 2 && control->readyReturned == 2;
		}));
		QCOMPARE(control->maximumActive, 1);
	}
	playback->stop();
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 2; }));
	}
}

void TestInputEnhancementCalibrationPlayback::wedgedReplacementFailsBusyWithoutSecondWorker() {
	auto control                     = std::make_shared< BackendControl >();
	control->ignoreStopUntilReleased = true;
	auto playback                    = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 1; }));
	}
	QElapsedTimer timer;
	timer.start();
	const Playback::StartResult replacement = playback->start(source, testTarget());
	QCOMPARE(replacement.error, Playback::Error::PlaybackBusy);
	QVERIFY2(timer.elapsed() < 200, qPrintable(QStringLiteral("replacement blocked for %1 ms").arg(timer.elapsed())));
	{
		std::unique_lock lock(control->mutex);
		QCOMPARE(control->started, 1);
		QCOMPARE(control->maximumActive, 1);
		control->releaseAll = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
}

void TestInputEnhancementCalibrationPlayback::wedgedSecondInstanceRespectsProcessWideBudget() {
	auto control                     = std::make_shared< BackendControl >();
	control->ignoreStopUntilReleased = true;
	auto first                       = makePlayback(control);
	auto second                      = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(first->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 1; }));
	}
	const Playback::StartResult rejected = second->start(source, testTarget());
	QCOMPARE(rejected.error, Playback::Error::PlaybackBusy);
	{
		std::unique_lock lock(control->mutex);
		QCOMPARE(control->started, 1);
		QCOMPARE(control->maximumActive, 1);
		control->releaseAll = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
	first->stop();
}

void TestInputEnhancementCalibrationPlayback::staleQueuedEventsAreIgnoredAfterGenerationAdvance() {
	auto control            = std::make_shared< BackendControl >();
	control->failAfterStart = true;
	auto playback           = makePlayback(control);
	QSignalSpy started(playback.get(), &Playback::playbackStarted);
	QSignalSpy failure(playback.get(), &Playback::playbackFailed);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] {
			return control->readyReturned == 1 && control->stopped == 1;
		}));
	}
	// readyReturned is set only after ReadyCallback has returned, which means the
	// Started event is already queued. Advance the generation before dispatch.
	playback->stop();
	QCoreApplication::processEvents();
	QCOMPARE(started.count(), 0);
	QCOMPARE(failure.count(), 0);
}

void TestInputEnhancementCalibrationPlayback::startupFailureIsReportedAsynchronously() {
	auto control         = std::make_shared< BackendControl >();
	control->failStartup = true;
	auto playback        = makePlayback(control);
	QSignalSpy failure(playback.get(), &Playback::playbackFailed);
	const std::vector< float > source(480, 0.1f);
	const Playback::StartResult result = playback->start(source, testTarget());
	QVERIFY(result);
	QTRY_COMPARE(failure.count(), 1);
	QCOMPARE(failure.at(0).at(0).toULongLong(), playback->generation());
	QCOMPARE(failure.at(0).at(1).toInt(), static_cast< int >(Playback::Error::DeviceUnavailable));
	QCOMPARE(failure.at(0).at(2).toBool(), false);
	QTRY_VERIFY(!playback->active());
	QCOMPARE(control->started, 1);
	QCOMPARE(control->stopped, 1);
}

void TestInputEnhancementCalibrationPlayback::postStartFailureIsReported() {
	auto control            = std::make_shared< BackendControl >();
	control->failAfterStart = true;
	auto playback           = makePlayback(control);
	QSignalSpy started(playback.get(), &Playback::playbackStarted);
	QSignalSpy failure(playback.get(), &Playback::playbackFailed);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	QTRY_COMPARE(started.count(), 1);
	QTRY_COMPARE(failure.count(), 1);
	QCOMPARE(failure.at(0).at(0).toULongLong(), playback->generation());
	QCOMPARE(failure.at(0).at(1).toInt(), static_cast< int >(Playback::Error::BackendFailure));
	QCOMPARE(failure.at(0).at(2).toBool(), true);
	QTRY_VERIFY(!playback->active());
}

void TestInputEnhancementCalibrationPlayback::naturalCompletionIsReported() {
	auto control             = std::make_shared< BackendControl >();
	control->finishNaturally = true;
	auto playback            = makePlayback(control);
	QSignalSpy finished(playback.get(), &Playback::playbackFinished);
	QSignalSpy failure(playback.get(), &Playback::playbackFailed);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	QTRY_COMPARE(finished.count(), 1);
	QCOMPARE(failure.count(), 0);
	QTRY_VERIFY(!playback->active());
}

void TestInputEnhancementCalibrationPlayback::processWideIdleBarrierTracksFullWorkerCleanup() {
	auto control              = std::make_shared< BackendControl >();
	control->holdAfterStopped = true;
	auto playback             = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	playback->stop();
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
	}
	QVERIFY(!Playback::waitForProcessIdle(std::chrono::milliseconds(10)));
	{
		const std::lock_guard lock(control->mutex);
		control->releaseAfterStopped = true;
	}
	control->condition.notify_all();
	QVERIFY(Playback::waitForProcessIdle(std::chrono::seconds(1)));
}

void TestInputEnhancementCalibrationPlayback::destructorDoesNotJoinWedgedBackend() {
	auto control                     = std::make_shared< BackendControl >();
	control->ignoreStopUntilReleased = true;
	auto playback                    = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	const Playback::Clip *liveClip = nullptr;
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 1; }));
		liveClip = control->liveClips.front();
	}
	QElapsedTimer timer;
	timer.start();
	playback.reset();
	QVERIFY2(timer.elapsed() < 100, qPrintable(QStringLiteral("destructor blocked for %1 ms").arg(timer.elapsed())));
	std::vector< float > destination(source.size() * 2U);
	QVERIFY(!liveClip->writeInterleavedStereo(0, destination, 1.0f));
	{
		std::unique_lock lock(control->mutex);
		control->releaseAll = true;
		control->condition.notify_all();
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->stopped == 1; }));
		QCOMPARE(control->active, 0);
	}
}

void TestInputEnhancementCalibrationPlayback::windowsDefaultEndpointSmoke() {
#ifndef Q_OS_WIN
	QSKIP("WASAPI calibration playback is Windows-only");
#else
	if (qEnvironmentVariableIntValue("MUMBLE_TEST_WASAPI_CALIBRATION_PLAYBACK") != 1) {
		QSKIP("Set MUMBLE_TEST_WASAPI_CALIBRATION_PLAYBACK=1 for the audible local endpoint smoke");
	}

	Playback playback;
	QSignalSpy started(&playback, &Playback::playbackStarted);
	QSignalSpy failure(&playback, &Playback::playbackFailed);
	QSignalSpy finished(&playback, &Playback::playbackFinished);
	std::vector< float > tone(Playback::sampleRateHz / 4U);
	for (std::size_t index = 0; index < tone.size(); ++index) {
		const float envelope = static_cast< float >(std::min(index, tone.size() - 1U - index)) / 480.0f;
		tone[index] = std::sin(static_cast< float >(index) * 0.057595865f) * 0.015f * std::clamp(envelope, 0.0f, 1.0f);
	}
	Playback::Target target;
	target.outputBackend = QStringLiteral("WASAPI");
	target.gain          = 1.0f;
	QVERIFY(playback.start(tone, target));
	QTRY_VERIFY_WITH_TIMEOUT(started.count() == 1 || failure.count() == 1, 3000);
	if (!failure.isEmpty()) {
		QFAIL(qPrintable(QStringLiteral("WASAPI startup error %1").arg(failure.at(0).at(1).toInt())));
	}
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
	QCOMPARE(failure.count(), 0);
#endif
}

QTEST_GUILESS_MAIN(TestInputEnhancementCalibrationPlayback)

#include "TestInputEnhancementCalibrationPlayback.moc"
