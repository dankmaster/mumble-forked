// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioOutputSample.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <vector>

class TestAudioOutputMemorySample : public QObject {
	Q_OBJECT

private slots:
	void ownsInputAndZeroPadsFinalFrame();
	void rejectsUnboundedOrInvalidPcm();
	void preparesRateConversionBeforePlayback();
};

void TestAudioOutputMemorySample::ownsInputAndZeroPadsFinalFrame() {
	std::vector< float > source(500);
	for (std::size_t index = 0; index < source.size(); ++index) {
		source[index] = static_cast< float >(index + 1) / 1000.0f;
	}
	const std::vector< float > expected = source;

	AudioOutputSample sample(source, AudioOutputSample::memorySampleRate, 0.75f,
							  AudioOutputSample::memorySampleRate, 480);
	QVERIFY(sample.isValid());
	QCOMPARE(sample.getVolume(), 0.75f);

	// The playback object must own its clip. CalibrationRuntime may wipe its
	// source arena as soon as the terminal transition is accepted.
	std::fill(source.begin(), source.end(), 0.0f);
	QVERIFY(sample.prepareSampleBuffer(480));
	for (std::size_t index = 0; index < 480; ++index) {
		QCOMPARE(sample.pfBuffer[index], expected[index]);
	}

	QVERIFY(sample.prepareSampleBuffer(480));
	for (std::size_t index = 0; index < 20; ++index) {
		QCOMPARE(sample.pfBuffer[index], expected[480 + index]);
	}
	for (std::size_t index = 20; index < 480; ++index) {
		QCOMPARE(sample.pfBuffer[index], 0.0f);
	}
	QVERIFY(!sample.prepareSampleBuffer(480));
}

void TestAudioOutputMemorySample::rejectsUnboundedOrInvalidPcm() {
	const std::vector< float > valid(480, 0.1f);
	const std::vector< float > empty;
	AudioOutputSample emptySample(empty, AudioOutputSample::memorySampleRate, 1.0f,
								 AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!emptySample.isValid());

	std::vector< float > nonFinite = valid;
	nonFinite[17] = std::numeric_limits< float >::quiet_NaN();
	AudioOutputSample nonFiniteSample(nonFinite, AudioOutputSample::memorySampleRate, 1.0f,
									 AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!nonFiniteSample.isValid());

	std::vector< float > tooLong(AudioOutputSample::maximumMemorySampleCount + 1U, 0.0f);
	AudioOutputSample oversized(tooLong, AudioOutputSample::memorySampleRate, 1.0f,
								  AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!oversized.isValid());

	AudioOutputSample wrongRate(valid, 44100, 1.0f, AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!wrongRate.isValid());
	AudioOutputSample negativeVolume(valid, AudioOutputSample::memorySampleRate, -1.0f,
									AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!negativeVolume.isValid());
	AudioOutputSample infiniteVolume(valid, AudioOutputSample::memorySampleRate,
									std::numeric_limits< float >::infinity(),
									AudioOutputSample::memorySampleRate, 480);
	QVERIFY(!infiniteVolume.isValid());

	AudioOutputSample boundedBuffer(valid, AudioOutputSample::memorySampleRate, 1.0f,
									 AudioOutputSample::memorySampleRate, 480);
	QVERIFY(boundedBuffer.isValid());
	QVERIFY(!boundedBuffer.prepareSampleBuffer(481));
}

void TestAudioOutputMemorySample::preparesRateConversionBeforePlayback() {
	std::vector< float > source(480);
	for (std::size_t index = 0; index < source.size(); ++index) {
		source[index] = std::sin(static_cast< float >(index) * 0.07f) * 0.25f;
	}

	AudioOutputSample downsampled(source, AudioOutputSample::memorySampleRate, 1.0f, 24000, 240);
	QVERIFY(downsampled.isValid());
	QVERIFY(downsampled.prepareSampleBuffer(240));
	bool hasSignal = false;
	for (std::size_t index = 0; index < 240; ++index) {
		QVERIFY(std::isfinite(downsampled.pfBuffer[index]));
		hasSignal = hasSignal || std::abs(downsampled.pfBuffer[index]) > 0.001f;
	}
	QVERIFY(hasSignal);
	QVERIFY(!downsampled.prepareSampleBuffer(240));
}

QTEST_GUILESS_MAIN(TestAudioOutputMemorySample)

#include "TestAudioOutputMemorySample.moc"
