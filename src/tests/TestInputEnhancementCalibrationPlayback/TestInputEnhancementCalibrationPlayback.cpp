// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationPlayback.h"

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
	int started          = 0;
	int stopped          = 0;
	int active           = 0;
	int maximumActive    = 0;
	bool inspect         = false;
	bool inspected       = false;
	bool failStartup     = false;
	bool failAfterStart  = false;
	bool finishNaturally = false;
};

class FakeBackend final : public Playback::Backend {
public:
	explicit FakeBackend(std::shared_ptr< BackendControl > control) : m_control(std::move(control)) {}

	Playback::StartResult run(const std::span< const float > mono48k, const Playback::Target &target,
							  const std::stop_token stopToken, Playback::ReadyCallback ready) override {
		{
			const std::lock_guard lock(m_control->mutex);
			++m_control->started;
			++m_control->active;
			m_control->maximumActive = std::max(m_control->maximumActive, m_control->active);
			m_control->lastTarget    = target;
		}
		m_control->condition.notify_all();
		if (stopToken.stop_requested()) {
			finish();
			return { Playback::Error::None, false };
		}

		if (m_control->failStartup) {
			ready({ Playback::Error::DeviceUnavailable, false });
			finish();
			return { Playback::Error::DeviceUnavailable, false };
		}

		ready({ Playback::Error::None, false });
		if (m_control->failAfterStart) {
			finish();
			return { Playback::Error::BackendFailure, false };
		}
		if (m_control->finishNaturally) {
			finish();
			return { Playback::Error::None, false };
		}
		std::stop_callback wake(stopToken, [control = m_control] { control->condition.notify_all(); });
		std::unique_lock lock(m_control->mutex);
		m_control->condition.wait(lock, [&] { return stopToken.stop_requested() || m_control->inspect; });
		if (m_control->inspect) {
			m_control->observed.assign(mono48k.begin(), mono48k.end());
			m_control->inspected = true;
			m_control->condition.notify_all();
			m_control->condition.wait(lock, [&] { return stopToken.stop_requested(); });
		}
		lock.unlock();
		finish();
		return { Playback::Error::None, false };
	}

private:
	void finish() {
		{
			const std::lock_guard lock(m_control->mutex);
			--m_control->active;
			++m_control->stopped;
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
	void ownsInputUntilSynchronousStop();
	void rejectsInvalidPcmBeforeBackendStart();
	void replacementJoinsPreviousPlayback();
	void startupFailureIsReportedAndJoined();
	void postStartFailureIsReported();
	void naturalCompletionIsReported();
	void destructorStopsWorker();
	void windowsDefaultEndpointSmoke();
};

void TestInputEnhancementCalibrationPlayback::ownsInputUntilSynchronousStop() {
	auto control  = std::make_shared< BackendControl >();
	auto playback = makePlayback(control);
	std::vector< float > source(960);
	for (std::size_t index = 0; index < source.size(); ++index) {
		source[index] = static_cast< float >(index + 1) / 2000.0f;
	}
	const std::vector< float > expected = source;

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
	QCOMPARE(control->stopped, 1);
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

void TestInputEnhancementCalibrationPlayback::replacementJoinsPreviousPlayback() {
	auto control  = std::make_shared< BackendControl >();
	auto playback = makePlayback(control);
	const std::vector< float > source(480, 0.1f);
	QVERIFY(playback->start(source, testTarget()));
	QVERIFY(playback->start(source, testTarget()));
	{
		std::unique_lock lock(control->mutex);
		QVERIFY(control->condition.wait_for(lock, std::chrono::seconds(1), [&] { return control->started == 2; }));
		QCOMPARE(control->started, 2);
		QCOMPARE(control->stopped, 1);
		QCOMPARE(control->maximumActive, 1);
	}
	playback->stop();
	QCOMPARE(control->stopped, 2);
}

void TestInputEnhancementCalibrationPlayback::startupFailureIsReportedAndJoined() {
	auto control         = std::make_shared< BackendControl >();
	control->failStartup = true;
	auto playback        = makePlayback(control);
	QSignalSpy failure(playback.get(), &Playback::playbackFailed);
	const std::vector< float > source(480, 0.1f);
	const Playback::StartResult result = playback->start(source, testTarget());
	QVERIFY(result);
	QTRY_COMPARE(failure.count(), 1);
	QCOMPARE(failure.at(0).at(0).toInt(), static_cast< int >(Playback::Error::DeviceUnavailable));
	QCOMPARE(failure.at(0).at(1).toBool(), false);
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
	QCOMPARE(failure.at(0).at(0).toInt(), static_cast< int >(Playback::Error::BackendFailure));
	QCOMPARE(failure.at(0).at(1).toBool(), true);
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

void TestInputEnhancementCalibrationPlayback::destructorStopsWorker() {
	auto control = std::make_shared< BackendControl >();
	{
		auto playback = makePlayback(control);
		const std::vector< float > source(480, 0.1f);
		QVERIFY(playback->start(source, testTarget()));
	}
	QCOMPARE(control->stopped, 1);
	QCOMPARE(control->active, 0);
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
		QFAIL(qPrintable(QStringLiteral("WASAPI startup error %1").arg(failure.at(0).at(0).toInt())));
	}
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
	QCOMPARE(failure.count(), 0);
#endif
}

QTEST_GUILESS_MAIN(TestInputEnhancementCalibrationPlayback)

#include "TestInputEnhancementCalibrationPlayback.moc"
