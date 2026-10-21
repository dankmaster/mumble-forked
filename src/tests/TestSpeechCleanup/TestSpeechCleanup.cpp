// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RNNoiseSpeechCleanup.h"
#include "AudioOutputSpeech.h"
#include "ClientUser.h"
#include "DeepFilterNetRealtimeWorker.h"
#include "Global.h"
#include "SpeechCleanupProcessor.h"
#include "SpeechCleanupTransmitDrain.h"

#ifdef TEST_BUNDLED_RNNOISE
extern "C" {
#	include "rnnoise.h"
}
#endif

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtTest>
#include <opus.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

#ifdef Q_OS_WIN
#	include <windows.h>
#endif

class TestSpeechCleanup : public QObject {
	Q_OBJECT

private slots:
	void windowsDelayLoadsNeuralRuntimesOnFirstUse();
	void modelDomainNormalizationIsExplicitAndPartOfSelectionIdentity();
	void transmitDrainUsesExactLatencyAndTerminatesOnlyItsLastFrame();
	void transmitDrainCanAppendUncountedTerminalFlushFrame();
	void transmitDrainCanBeCancelledForFreshSpeech();
	void processorOwnershipFollowsNoiseCancelModeWithoutModelInitialization();
	void cancelledPreparationSkipsQueuedFactoryAndWakesDelay();
	void cancelledPreparationDoesNotStartRealtimeWorker();
	void preparedProcessorAndAbandonedResultRetireOffCaller();
	void receiveAdapterPreservesStereoAndFailureTimeline();
	void receiveWorkerSupportsBoundedCallbackBursts_data();
	void receiveWorkerSupportsBoundedCallbackBursts();
	void receiveOpusPacketsAreStagedAndDrained_data();
	void receiveOpusPacketsAreStagedAndDrained();
	void receiveMuteTransitionAdvancesCleanupTimeline_data();
	void receiveMuteTransitionAdvancesCleanupTimeline();
#ifdef TEST_EXPECT_RNNOISE
	void customModelDirectoryFallsBackSafely();
	void missingCustomModelFallsBackSafely();
	void emptyCustomModelFallsBackSafely();
	void truncatedCustomModelFallsBackSafely();
#endif
#ifdef TEST_BUNDLED_RNNOISE
	void validCustomModelIsReportedAsActive();
#endif
#ifdef TEST_EXPECT_RNNOISE
	void rnnoiseOutputIsIndependentOfInputChunking();
	void rnnoiseAdapterReportsAndPreservesTheRawApiLatency();
	void rnnoiseDryWetMixUsesTheProcessedTimeline();
	void rnnoiseResetRestoresTheStreamingTimeline();
	void rnnoiseSanitizesNonFiniteInputWithoutPoisoningItsState();
#endif
#ifdef TEST_EXPECT_DTLN
	void dtlnOutputIsIndependentOfInputChunking();
	void dtlnReportsAndAppliesItsCausalLatency();
	void dtlnDryWetMixUsesTheProcessedTimeline();
	void dtlnResetRestoresTheStreamingTimeline();
	void dtlnSanitizesNonFiniteInputWithoutPoisoningItsState();
#endif
	void deepFilterNetOutputIsIndependentOfInputChunking();
	void deepFilterNetModelDomainNormalizationRemainsContinuousAcrossFrameGainChanges();
	void deepFilterNetRealtimeTimelineMatchesReportedLatency();
	void deepFilterNetDryWetMixUsesTheProcessedTimeline();
	void deepFilterNetResetRestoresTheStreamingTimeline();
	void deepFilterNetSanitizesNonFiniteInputWithoutPoisoningItsState();

private:
	static Mumble::SpeechCleanup::Selection customSelection(const QString &path);
	static void verifyEmbeddedFallback(const QString &path);
	static void writeFile(const QString &path, const QByteArray &contents);
	static Mumble::SpeechCleanup::Selection embeddedSelection(Settings::SpeechCleanupBackend backend);
	static std::vector< float > testSignal(std::size_t sampleCount);
	static std::vector< float > processSignal(SpeechCleanupProcessor &processor, const std::vector< float > &input,
											const std::vector< unsigned int > &chunkPattern, float mixFactor);
	static void compareSignals(const std::vector< float > &actual, const std::vector< float > &expected,
							   float tolerance, const char *context);
	static void verifyChunkInvariant(const Mumble::SpeechCleanup::Selection &selection);
	static void verifyDryWetTimeline(const Mumble::SpeechCleanup::Selection &selection);
	static void verifyResetTimeline(const Mumble::SpeechCleanup::Selection &selection);
	static void verifyNonFiniteInputSanitized(const Mumble::SpeechCleanup::Selection &selection);
};

namespace {
class TestSpeechCleanupProcessor final : public SpeechCleanupProcessor {
public:
	bool isReady() const override { return true; }
	void reset() override {}
	void processInPlace(float *, unsigned int, float) override {}
};

struct PreparationState {
	std::atomic_int constructed { 0 };
	std::atomic_int prepared { 0 };
	std::atomic_int destroyed { 0 };
	std::atomic_bool preparedOnCaller { false };
	std::atomic_bool destroyedOnCaller { false };
	Qt::HANDLE caller = QThread::currentThreadId();
	QSemaphore entered;
	QSemaphore release;
};

class PreparationProbe final : public SpeechCleanupProcessor {
public:
	explicit PreparationProbe(std::shared_ptr< PreparationState > state) : m_state(std::move(state)) {}
	~PreparationProbe() override {
		m_state->destroyedOnCaller.store(QThread::currentThreadId() == m_state->caller);
		m_state->destroyed.fetch_add(1);
	}
	bool isReady() const override { return true; }
	void reset() override {}
	bool prepareRealtime() override {
		m_state->preparedOnCaller.store(QThread::currentThreadId() == m_state->caller);
		m_state->prepared.fetch_add(1);
		return true;
	}
	void processInPlace(float *, unsigned int, float) override {}

private:
	std::shared_ptr< PreparationState > m_state;
};

class DelayedReceiveProbe final : public SpeechCleanupProcessor {
public:
	explicit DelayedReceiveProbe(unsigned int latency) : m_delay(latency, 0.0f) {}
	bool isReady() const override { return m_ready; }
	void reset() override {}
	bool prepareRealtimeForReceive(unsigned int maximumBurstFrames) override {
		burstBudget = maximumBurstFrames;
		return true;
	}
	unsigned int latencySamples() const override { return static_cast< unsigned int >(m_delay.size()); }
	void processInPlace(float *samples, unsigned int count, float mix) override {
		++calls;
		validFrames = validFrames && count == ReceiveSpeechCleanupProcessor::frameSamples;
		if (calls == failAtCall) {
			m_ready = false;
			std::fill_n(samples, count, std::numeric_limits< float >::quiet_NaN());
			return;
		}
		for (unsigned int index = 0; index < count; ++index) {
			const float delayed = m_delay[m_position];
			m_delay[m_position] = samples[index];
			m_position = (m_position + 1) % m_delay.size();
			samples[index] = delayed * (1.0f - mix * 0.5f);
		}
	}
	unsigned int calls = 0;
	unsigned int burstBudget = 0;
	unsigned int failAtCall = 0;
	bool validFrames = true;

private:
	std::vector< float > m_delay;
	std::size_t m_position = 0;
	bool m_ready = true;
};

struct ReceiveWorkerProbe {
	QSemaphore release;
	std::atomic_bool blocked { true };
	static int process(void *context, const float *input, float *output) noexcept {
		auto &self = *static_cast< ReceiveWorkerProbe * >(context);
		if (self.blocked.exchange(false) && !self.release.tryAcquire(1, 2000)) return 1;
		std::copy_n(input, DeepFilterNetRealtimeWorker::frameSamples, output);
		return DeepFilterNetRealtimeWorker::statusOk;
	}
};

class ReceiveOutputProbe final : public AudioOutputSpeech {
public:
	ReceiveOutputProbe(unsigned int callbackFrames, std::unique_ptr< SpeechCleanupProcessor > processor)
		: AudioOutputSpeech(nullptr, 48000, Mumble::Protocol::AudioCodec::Opus, callbackFrames) {
		m_remoteSpeechCleanup = std::move(processor);
		m_remoteSpeechCleanupRequested = true;
		m_remoteSpeechCleanupFrameStaging = true;
		m_remoteSpeechCleanupActive = m_remoteSpeechCleanup->isReady();
		m_remoteSpeechCleanupPreparationConsumed = true;
		m_remoteSpeechCleanupMixFactor = 1.0f;
	}
	unsigned int drainedSamples() const { return m_remoteSpeechCleanupDrainedSamples; }
	bool drainCompleted() const { return m_remoteSpeechCleanupDrainCompleted; }
	void queueDecodedMarkerPacket() {
		for (unsigned int frame = 0; frame < 12; ++frame) {
			std::fill_n(m_pendingDecodedPacket.data() + frame * 960, 960,
				static_cast< float >(frame + 1) * 0.05f);
		}
		m_pendingDecodedSamples = static_cast< unsigned int >(m_pendingDecodedPacket.size());
		m_pendingDecodedOffset = 0;
		m_pendingDecodedTerminator = true;
	}
};

class ScopedGlobalOverride final {
public:
	explicit ScopedGlobalOverride(Global &global) : m_previous(Global::g_global_struct) {
		Global::g_global_struct = &global;
	}
	~ScopedGlobalOverride() { Global::g_global_struct = m_previous; }

private:
	Global *m_previous;
};
}

void TestSpeechCleanup::cancelledPreparationSkipsQueuedFactoryAndWakesDelay() {
	QThreadPool pool;
	pool.setMaxThreadCount(1);
	auto state = std::make_shared< PreparationState >();
	pool.start([state] {
		state->entered.release();
		(void) state->release.tryAcquire(1, 2000);
	});
	QVERIFY(state->entered.tryAcquire(1, 1000));
	SpeechCleanupPreparation queued(pool, [state] {
		state->constructed.fetch_add(1);
		return std::make_unique< PreparationProbe >(state);
	});
	queued.cancel();
	state->release.release();
	QVERIFY(pool.waitForDone(1000));
	QCOMPARE(state->constructed.load(), 0);
	QVERIFY(queued.isFinished());
	QVERIFY(!queued.takeResult());

	SpeechCleanupPreparation delayed(pool, [state] {
		state->constructed.fetch_add(1);
		return std::make_unique< PreparationProbe >(state);
	}, std::chrono::steady_clock::now() + std::chrono::seconds(30));
	QTRY_COMPARE_WITH_TIMEOUT(pool.activeThreadCount(), 1, 1000);
	delayed.cancel();
	QVERIFY2(pool.waitForDone(1000), "Cancellation must wake the grace-period wait");
	QCOMPARE(state->constructed.load(), 0);
}

void TestSpeechCleanup::cancelledPreparationDoesNotStartRealtimeWorker() {
	QThreadPool pool;
	auto state = std::make_shared< PreparationState >();
	SpeechCleanupPreparation preparation(pool, [state] {
		state->entered.release();
		(void) state->release.tryAcquire(1, 2000);
		state->constructed.fetch_add(1);
		return std::make_unique< PreparationProbe >(state);
	});
	QVERIFY(state->entered.tryAcquire(1, 1000));
	preparation.cancel();
	state->release.release();
	QVERIFY(pool.waitForDone(1000));
	QCOMPARE(state->constructed.load(), 1);
	QCOMPARE(state->prepared.load(), 0);
	QCOMPARE(state->destroyed.load(), 1);
	QVERIFY(!state->destroyedOnCaller.load());
	QVERIFY(!preparation.takeResult());
}

void TestSpeechCleanup::preparedProcessorAndAbandonedResultRetireOffCaller() {
	QThreadPool pool;
	for (const bool adopt : { false, true }) {
		auto state = std::make_shared< PreparationState >();
		{
			SpeechCleanupPreparation preparation(pool, [state] {
				return std::make_unique< PreparationProbe >(state);
			});
			QVERIFY(pool.waitForDone(1000));
			QVERIFY(preparation.isFinished());
			QCOMPARE(state->prepared.load(), 1);
			QVERIFY(!state->preparedOnCaller.load());
			if (adopt) {
				auto processor = preparation.takeResult();
				QVERIFY(processor && processor->isReady());
				retireSpeechCleanupProcessor(std::move(processor));
			}
		}
		QTRY_COMPARE_WITH_TIMEOUT(state->destroyed.load(), 1, 1000);
		QVERIFY(!state->destroyedOnCaller.load());
	}
}

void TestSpeechCleanup::receiveAdapterPreservesStereoAndFailureTimeline() {
	constexpr unsigned int latency = 600;
	constexpr unsigned int chunk = ReceiveSpeechCleanupProcessor::frameSamples;
	auto owned = std::make_unique< DelayedReceiveProbe >(latency);
	auto *probe = owned.get();
	probe->failAtCall = 7;
	ReceiveSpeechCleanupProcessor processor(std::move(owned), 4);
	QVERIFY(processor.prepareRealtime());
	QCOMPARE(probe->burstBudget, 4u);
	QCOMPARE(processor.latencySamples(), latency);
	const unsigned int totalFrames = chunk * 9 + 336;
	std::vector< float > original(totalFrames * 2);
	for (unsigned int frame = 0; frame < totalFrames; ++frame) {
		original[frame * 2] = 0.2f + static_cast< float >(frame % 13) * 0.01f;
		original[frame * 2 + 1] = frame / chunk == 2 ? -original[frame * 2] : original[frame * 2];
	}
	std::vector< float > actual = original;
	for (unsigned int offset = 0; offset < totalFrames; offset += chunk) {
		const unsigned int count = std::min(chunk, totalFrames - offset);
		QVERIFY(processor.processStereoFrame(actual.data() + offset * 2, count * 2, 1.0f));
		for (unsigned int frame = offset; frame < offset + count; ++frame) {
			for (unsigned int channel = 0; channel < 2; ++channel) {
				float expected = 0.0f;
				if (frame >= latency) {
					const unsigned int source = frame - latency;
					const bool wet = offset / chunk < 6 && source / chunk != 2;
					expected = original[source * 2 + channel] * (wet ? 0.5f : 1.0f);
				}
				QCOMPARE(actual[frame * 2 + channel], expected);
			}
		}
	}
	QVERIFY(processor.isReady());
	QVERIFY(processor.usingDryFallback());
	QVERIFY(probe->validFrames);
	QCOMPARE(probe->calls, 7u);
}

void TestSpeechCleanup::receiveWorkerSupportsBoundedCallbackBursts_data() {
	QTest::addColumn< unsigned int >("burstFrames");
	for (unsigned int frames : { 1u, 2u, 6u, 12u, 63u }) {
		QTest::newRow(qPrintable(QString::number(frames))) << frames;
	}
}

void TestSpeechCleanup::receiveWorkerSupportsBoundedCallbackBursts() {
	QFETCH(unsigned int, burstFrames);
	ReceiveWorkerProbe state;
	DeepFilterNetRealtimeWorker worker;
	const unsigned int delay = std::max(2u, burstFrames + 1);
	QVERIFY(worker.start(&state, &ReceiveWorkerProbe::process, delay));
	QCOMPARE(worker.configuredDelayFrames(), delay);
	std::array< float, 480 > input;
	std::array< float, 480 > output;
	// Deliberately hold inference while the callback admits its whole burst.
	for (unsigned int frame = 0; frame < burstFrames; ++frame) {
		input.fill(static_cast< float >(frame + 1) / 100.0f);
		QVERIFY(worker.processFrame(input.data(), output.data()));
		QVERIFY(std::all_of(output.cbegin(), output.cend(), [](float value) { return value == 0.0f; }));
	}
	state.release.release();
	QVERIFY(worker.finishOfflineProcessing());
	for (unsigned int frame = burstFrames; frame < delay + burstFrames; ++frame) {
		QVERIFY(worker.prepareOfflineFrame());
		input.fill(0.0f);
		QVERIFY(worker.processFrame(input.data(), output.data()));
		const float expected = frame < delay ? 0.0f : static_cast< float >(frame - delay + 1) / 100.0f;
		QVERIFY(std::all_of(output.cbegin(), output.cend(), [expected](float value) { return value == expected; }));
	}
	QVERIFY(worker.finishOfflineProcessing());
	worker.stop();
	QVERIFY(worker.start(&state, &ReceiveWorkerProbe::process));
	QCOMPARE(worker.configuredDelayFrames(), 2u);
	worker.stop();
	QVERIFY(!worker.start(&state, &ReceiveWorkerProbe::process, 65));
}

void TestSpeechCleanup::receiveOpusPacketsAreStagedAndDrained_data() {
	QTest::addColumn< unsigned int >("packetFrames");
	QTest::addColumn< unsigned int >("callbackFrames");
	QTest::addColumn< bool >("muted");
	for (unsigned int packet : { 1u, 2u, 4u, 6u, 8u, 12u }) {
		for (unsigned int callback : { 240u, 480u, 1440u }) {
			QTest::newRow(qPrintable(QStringLiteral("%1ms-%2samples").arg(packet * 10).arg(callback)))
				<< packet << callback << false;
		}
	}
	for (unsigned int callback : { 240u, 480u, 1440u }) {
		QTest::newRow(qPrintable(QStringLiteral("120ms-%1samples-muted").arg(callback)))
			<< 12u << callback << true;
	}
}

void TestSpeechCleanup::receiveOpusPacketsAreStagedAndDrained() {
	QFETCH(unsigned int, packetFrames);
	QFETCH(unsigned int, callbackFrames);
	QFETCH(bool, muted);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	Global global(directory.filePath(QStringLiteral("receive-test.ini")));
	ScopedGlobalOverride overrideGlobal(global);
	global.s.iJitterBufferSize = 0;
	using Encoder = std::unique_ptr< OpusEncoder, decltype(&opus_encoder_destroy) >;
	using Decoder = std::unique_ptr< OpusDecoder, decltype(&opus_decoder_destroy) >;
	using Repacketizer = std::unique_ptr< OpusRepacketizer, decltype(&opus_repacketizer_destroy) >;
	Encoder encoder(opus_encoder_create(48000, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY, nullptr), opus_encoder_destroy);
	Decoder decoder(opus_decoder_create(48000, 2, nullptr), opus_decoder_destroy);
	Repacketizer repacketizer(opus_repacketizer_create(), opus_repacketizer_destroy);
	QVERIFY(encoder && decoder && repacketizer);
	QCOMPARE(opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(64000)), OPUS_OK);
	QCOMPARE(opus_encoder_ctl(encoder.get(), OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)), OPUS_OK);
	QCOMPARE(opus_decoder_ctl(decoder.get(), OPUS_SET_PHASE_INVERSION_DISABLED(1)), OPUS_OK);
	std::array< std::array< unsigned char, 1275 >, 12 > encoded {};
	std::array< float, 480 > frame;
	for (unsigned int index = 0; index < packetFrames; ++index) {
		for (unsigned int sample = 0; sample < frame.size(); ++sample)
			frame[sample] = 0.25f * std::sin(static_cast< float >(sample + index * 480) * 0.071f);
		const int bytes = opus_encode_float(encoder.get(), frame.data(), 480, encoded[index].data(), 1275);
		QVERIFY(bytes > 0);
		QCOMPARE(opus_repacketizer_cat(repacketizer.get(), encoded[index].data(), bytes), OPUS_OK);
	}
	std::array< unsigned char, 12 * 1275 > packet {};
	const int bytes = opus_repacketizer_out(repacketizer.get(), packet.data(), static_cast< opus_int32 >(packet.size()));
	QVERIFY(bytes > 0);
	std::vector< float > decoded(packetFrames * 480 * 2);
	QCOMPARE(opus_decode_float(decoder.get(), packet.data(), bytes, decoded.data(), packetFrames * 480, 0),
		static_cast< int >(packetFrames * 480));

	constexpr unsigned int latency = 960;
	auto owned = std::make_unique< DelayedReceiveProbe >(latency);
	auto *probe = owned.get();
	auto adapter = std::make_unique< ReceiveSpeechCleanupProcessor >(std::move(owned), (callbackFrames + 499) / 480);
	QVERIFY(adapter->prepareRealtime());
	ClientUser localUser;
	ReceiveOutputProbe receiver(callbackFrames, std::move(adapter));
	if (muted) {
		localUser.bLocalMute = true;
		receiver.p = &localUser;
	}
	const float *const initialBuffer = receiver.pfBuffer;
	Mumble::Protocol::AudioData audio;
	audio.payload = { packet.data(), static_cast< std::size_t >(bytes) };
	audio.isLastFrame = true;
	receiver.addFrameToBuffer(audio);
	std::vector< float > output;
	bool ended = false;
	for (unsigned int callback = 0; callback < 40; ++callback) {
		const unsigned int before = probe->calls;
		if (!receiver.prepareSampleBuffer(callbackFrames)) {
			ended = true;
			break;
		}
		QCOMPARE(receiver.pfBuffer, initialBuffer);
		QVERIFY(probe->calls - before <= (callbackFrames + 499) / 480);
		output.insert(output.end(), receiver.pfBuffer, receiver.pfBuffer + callbackFrames * 2);
	}
	QVERIFY(ended);
	QVERIFY(receiver.drainCompleted());
	QCOMPARE(receiver.drainedSamples(), latency);
	QVERIFY(probe->validFrames);
	QCOMPARE(probe->calls, packetFrames + latency / 480);
	const std::size_t expectedEnd = decoded.size() + latency * 2;
	QVERIFY(output.size() >= expectedEnd);
	for (std::size_t sample = 0; sample < output.size(); ++sample) {
		float expected = 0.0f;
		if (!muted && sample >= latency * 2 && sample < expectedEnd) {
			expected = decoded[sample - latency * 2] * 0.5f;
			if (sample >= expectedEnd - 960) {
				const unsigned int fadeIndex = static_cast< unsigned int >((sample - (expectedEnd - 960)) / 2);
				expected *= std::sin(static_cast< float >(479 - fadeIndex) * static_cast< float >(M_PI / 960.0));
			}
		}
		QVERIFY2(std::fabs(output[sample] - expected) < 1.0e-6f,
			qPrintable(QStringLiteral("Receive timeline mismatch at stereo sample %1").arg(sample)));
	}
}

void TestSpeechCleanup::receiveMuteTransitionAdvancesCleanupTimeline_data() {
	QTest::addColumn< bool >("failWhileMuted");
	QTest::newRow("wet") << false;
	QTest::newRow("dry-fallback") << true;
}

void TestSpeechCleanup::receiveMuteTransitionAdvancesCleanupTimeline() {
	QFETCH(bool, failWhileMuted);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	Global global(directory.filePath(QStringLiteral("receive-mute-test.ini")));
	ScopedGlobalOverride overrideGlobal(global);
	auto owned = std::make_unique< DelayedReceiveProbe >(960);
	auto *probe = owned.get();
	if (failWhileMuted) probe->failAtCall = 4;
	auto adapter = std::make_unique< ReceiveSpeechCleanupProcessor >(std::move(owned), 2);
	QVERIFY(adapter->prepareRealtime());
	ClientUser localUser;
	ReceiveOutputProbe receiver(480, std::move(adapter));
	receiver.p = &localUser;
	receiver.queueDecodedMarkerPacket();
	unsigned int playedFrames = 0;
	for (; playedFrames < 20; ++playedFrames) {
		// Five muted callbacks exceed the two-frame cleanup latency. The first
		// callback has one lookahead frame, so admitted source frames 3..7 are muted.
		localUser.bLocalMute = playedFrames >= 2 && playedFrames < 7;
		if (!receiver.prepareSampleBuffer(480)) break;
		for (unsigned int sample = 0; sample < 480; ++sample) {
			float expected = 0.0f;
			if (!localUser.bLocalMute && playedFrames >= 2) {
				const unsigned int sourceFrame = playedFrames - 2;
				if (sourceFrame < 12 && !(sourceFrame >= 3 && sourceFrame <= 7)) {
					expected = static_cast< float >(sourceFrame + 1) * 0.05f * (failWhileMuted ? 1.0f : 0.5f);
					if (playedFrames == 13)
						expected *= std::sin(static_cast< float >(479 - sample) * static_cast< float >(M_PI / 960.0));
				}
			}
			// AudioOutput::prepareOutputBuffers excludes locally muted users from
			// mixing, including already-prefetched samples from the preceding callback.
			for (unsigned int channel = 0; channel < 2; ++channel) {
				const float rendered = localUser.bLocalMute ? 0.0f : receiver.pfBuffer[2 * sample + channel];
				QVERIFY2(std::fabs(rendered - expected) < 1.0e-6f,
					qPrintable(QStringLiteral("Mute transition replayed stale audio at frame %1 sample %2")
						.arg(playedFrames).arg(sample)));
			}
		}
	}
	QCOMPARE(playedFrames, 14u);
	QVERIFY(receiver.drainCompleted());
	QCOMPARE(receiver.drainedSamples(), 960u);
	QCOMPARE(probe->calls, failWhileMuted ? 4u : 14u);
}

void TestSpeechCleanup::windowsDelayLoadsNeuralRuntimesOnFirstUse() {
#ifdef Q_OS_WIN
	QVERIFY2(GetModuleHandleW(L"rnnoise.dll") == nullptr,
			 "RNNoise must not be mapped before an RNNoise processor is requested");
	QVERIFY2(GetModuleHandleW(L"onnxruntime.dll") == nullptr,
			 "ONNX Runtime must not be mapped before a DTLN processor is requested");

#	ifdef TEST_EXPECT_RNNOISE
	auto rnnoiseProcessor = createSpeechCleanupProcessor(embeddedSelection(Settings::RNNoiseBackend));
	QVERIFY(rnnoiseProcessor && rnnoiseProcessor->isReady());
	QVERIFY(GetModuleHandleW(L"rnnoise.dll") != nullptr);
	QVERIFY2(GetModuleHandleW(L"onnxruntime.dll") == nullptr,
			 "Creating RNNoise must not load the unrelated ONNX runtime");
#	endif

#	ifdef TEST_EXPECT_DTLN
	auto dtlnProcessor = createSpeechCleanupProcessor(embeddedSelection(Settings::DTLNBackend));
	QVERIFY(dtlnProcessor && dtlnProcessor->isReady());
	QVERIFY(GetModuleHandleW(L"onnxruntime.dll") != nullptr);
#	endif
#else
	QSKIP("Windows delay-load semantics are only available on Windows.");
#endif
}

void TestSpeechCleanup::modelDomainNormalizationIsExplicitAndPartOfSelectionIdentity() {
	Mumble::SpeechCleanup::Selection legacy;
	legacy.backend = Settings::DeepFilterNetBackend;
	legacy.modelId = QStringLiteral("deepfilternet:balanced");
	QVERIFY(!legacy.modelDomainNormalization);
	QVERIFY(!Mumble::SpeechCleanup::normalizeSelection(legacy).modelDomainNormalization);

	Mumble::SpeechCleanup::Selection product = legacy;
	product.modelDomainNormalization         = true;
	QVERIFY(product != legacy);
	QVERIFY(Mumble::SpeechCleanup::normalizeSelection(product).modelDomainNormalization);
}

void TestSpeechCleanup::transmitDrainUsesExactLatencyAndTerminatesOnlyItsLastFrame() {
	Mumble::SpeechCleanup::TransmitDrain drain;
	drain.begin(1776);

	std::vector< unsigned int > drainedSamples;
	while (drain.active()) {
		const auto frame = drain.takeFrame(480);
		QVERIFY(frame.draining);
		drainedSamples.push_back(frame.zeroInputSamples);
		QCOMPARE(frame.causalDrainSamples, frame.zeroInputSamples);
		QCOMPARE(frame.terminator, !drain.active());
	}

	QCOMPARE(drainedSamples, (std::vector< unsigned int >{ 480, 480, 480, 336 }));
	QCOMPARE(std::accumulate(drainedSamples.cbegin(), drainedSamples.cend(), 0u), 1776u);
	QCOMPARE(drain.requestedSamples(), 1776u);
	QCOMPARE(drain.remainingSamples(), 0u);
	QCOMPARE(drain.drainedSamples(), 1776u);

	const auto exhaustedFrame = drain.takeFrame(480);
	QVERIFY(!exhaustedFrame.draining);
	QVERIFY(!exhaustedFrame.terminator);
	QCOMPARE(exhaustedFrame.zeroInputSamples, 0u);
	QCOMPARE(exhaustedFrame.causalDrainSamples, 0u);

	drain.begin(0);
	QVERIFY(!drain.active());
	QCOMPARE(drain.requestedSamples(), 0u);
	QCOMPARE(drain.drainedSamples(), 0u);
}

void TestSpeechCleanup::transmitDrainCanBeCancelledForFreshSpeech() {
	Mumble::SpeechCleanup::TransmitDrain drain;
	drain.begin(1440);

	const auto firstFrame = drain.takeFrame(480);
	QVERIFY(firstFrame.draining);
	QVERIFY(!firstFrame.terminator);
	QCOMPARE(drain.remainingSamples(), 960u);
	QCOMPARE(drain.drainedSamples(), 480u);

	// AudioInput invokes this on either a fresh VAD activation or PTT press. No
	// cleanup reset is coupled to cancellation, so the live processor timeline
	// can continue immediately with the new microphone frame.
	drain.cancel();
	QVERIFY(!drain.active());
	QCOMPARE(drain.requestedSamples(), 0u);
	QCOMPARE(drain.remainingSamples(), 0u);
	QCOMPARE(drain.drainedSamples(), 0u);
	QVERIFY(!drain.takeFrame(480).draining);
}

void TestSpeechCleanup::processorOwnershipFollowsNoiseCancelModeWithoutModelInitialization() {
	Mumble::SpeechCleanup::Selection dtlnSelection;
	dtlnSelection.backend = Settings::DTLNBackend;
	dtlnSelection.modelId = QStringLiteral("dtln:baseline");

	Mumble::SpeechCleanup::Selection activeSelection;
	std::unique_ptr< SpeechCleanupProcessor > processor;
	reconcileSpeechCleanupProcessor(Settings::NoiseCancelOff, dtlnSelection, activeSelection, processor);
	QVERIFY(!processor);
	QVERIFY(activeSelection == Mumble::SpeechCleanup::normalizeSelection(dtlnSelection));
	QVERIFY(!noiseCancelUsesSpeechCleanup(Settings::NoiseCancelOff));
	QVERIFY(!noiseCancelUsesSpeechCleanup(Settings::NoiseCancelSpeex));
	QVERIFY(noiseCancelUsesSpeechCleanup(Settings::NoiseCancelRNN));
	QVERIFY(noiseCancelUsesSpeechCleanup(Settings::NoiseCancelBoth));

	processor = std::make_unique< TestSpeechCleanupProcessor >();
	SpeechCleanupProcessor *const retainedProcessor = processor.get();
	reconcileSpeechCleanupProcessor(Settings::NoiseCancelRNN, dtlnSelection, activeSelection, processor);
	QCOMPARE(processor.get(), retainedProcessor);

	reconcileSpeechCleanupProcessor(Settings::NoiseCancelSpeex, dtlnSelection, activeSelection, processor);
	QVERIFY(!processor);
	QVERIFY(activeSelection == Mumble::SpeechCleanup::normalizeSelection(dtlnSelection));

	processor = std::make_unique< TestSpeechCleanupProcessor >();
	reconcileSpeechCleanupProcessor(Settings::NoiseCancelBoth, dtlnSelection, activeSelection, processor);
	QVERIFY(processor);
	reconcileSpeechCleanupProcessor(Settings::NoiseCancelOff, dtlnSelection, activeSelection, processor);
	QVERIFY(!processor);
}

void TestSpeechCleanup::transmitDrainCanAppendUncountedTerminalFlushFrame() {
	Mumble::SpeechCleanup::TransmitDrain drain;
	drain.begin(960, 1);

	const auto first = drain.takeFrame(480);
	QVERIFY(first.draining);
	QVERIFY(!first.terminalFlush);
	QVERIFY(!first.terminator);
	QCOMPARE(first.zeroInputSamples, 480u);
	QCOMPARE(first.causalDrainSamples, 480u);

	const auto second = drain.takeFrame(480);
	QVERIFY(second.draining);
	QVERIFY(!second.terminalFlush);
	QVERIFY(!second.terminator);
	QCOMPARE(second.zeroInputSamples, 480u);
	QCOMPARE(second.causalDrainSamples, 480u);
	QCOMPARE(drain.drainedSamples(), 960u);
	QCOMPARE(drain.remainingTerminalFlushFrames(), 1u);

	const auto flush = drain.takeFrame(480);
	QVERIFY(flush.draining);
	QVERIFY(flush.terminalFlush);
	QVERIFY(flush.terminator);
	QCOMPARE(flush.zeroInputSamples, 480u);
	QCOMPARE(flush.causalDrainSamples, 0u);
	QCOMPARE(drain.requestedSamples(), 960u);
	QCOMPARE(drain.drainedSamples(), 960u);
	QCOMPARE(drain.remainingTerminalFlushFrames(), 0u);
	QVERIFY(!drain.active());

	// A zero-latency Original E2E release still needs one callback to flush the
	// unchanged common preprocessor and carry a real terminator.
	drain.begin(0, 1);
	QVERIFY(drain.active());
	const auto originalFlush = drain.takeFrame(480);
	QVERIFY(originalFlush.draining);
	QVERIFY(originalFlush.terminalFlush);
	QVERIFY(originalFlush.terminator);
	QCOMPARE(originalFlush.zeroInputSamples, 480u);
	QCOMPARE(originalFlush.causalDrainSamples, 0u);
	QCOMPARE(drain.requestedSamples(), 0u);
	QCOMPARE(drain.drainedSamples(), 0u);
}

Mumble::SpeechCleanup::Selection TestSpeechCleanup::customSelection(const QString &path) {
	Mumble::SpeechCleanup::Selection selection;
	selection.backend         = Settings::RNNoiseBackend;
	selection.modelId         = QStringLiteral("rnnoise:custom");
	selection.customModelPath = path;
	return selection;
}

void TestSpeechCleanup::verifyEmbeddedFallback(const QString &path) {
	RNNoiseSpeechCleanup cleanup(customSelection(path));
	QVERIFY(cleanup.isReady());
	QVERIFY(cleanup.usedFallback());
	QCOMPARE(cleanup.activeModelId(), QStringLiteral("rnnoise:embedded"));
#ifdef Q_OS_WIN
	QVERIFY(QFileInfo(cleanup.activeModelPath()).isFile());
	QCOMPARE(QFileInfo(cleanup.activeModelPath()).fileName().toLower(), QStringLiteral("rnnoise.dll"));
#else
	QVERIFY(cleanup.activeModelPath().isEmpty());
#endif

	std::array< float, 480 > silence = {};
	cleanup.processInPlace(silence.data(), static_cast< unsigned int >(silence.size()));
}

void TestSpeechCleanup::writeFile(const QString &path, const QByteArray &contents) {
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(file.write(contents), contents.size());
	QVERIFY(file.flush());
}

#ifdef TEST_EXPECT_RNNOISE
void TestSpeechCleanup::customModelDirectoryFallsBackSafely() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	verifyEmbeddedFallback(directory.path());
}

void TestSpeechCleanup::missingCustomModelFallsBackSafely() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	verifyEmbeddedFallback(directory.filePath(QStringLiteral("missing.weights_blob.bin")));
}

void TestSpeechCleanup::emptyCustomModelFallsBackSafely() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("empty.weights_blob.bin"));
	writeFile(path, {});
	verifyEmbeddedFallback(path);
}

void TestSpeechCleanup::truncatedCustomModelFallsBackSafely() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("truncated.weights_blob.bin"));
	writeFile(path, QByteArray::fromHex("10000000"));
	verifyEmbeddedFallback(path);
}
#endif

#ifdef TEST_BUNDLED_RNNOISE
void TestSpeechCleanup::validCustomModelIsReportedAsActive() {
	const QString path = QDir(QCoreApplication::applicationDirPath())
		.filePath(QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin"));
	QVERIFY2(QFileInfo::exists(path), qPrintable(path));

	RNNoiseSpeechCleanup cleanup(customSelection(path));
	QVERIFY(cleanup.isReady());
	QVERIFY(!cleanup.usedFallback());
	QCOMPARE(cleanup.activeModelId(), QStringLiteral("rnnoise:custom"));
	QCOMPARE(cleanup.activeModelPath(), QFileInfo(path).absoluteFilePath());
}
#endif

Mumble::SpeechCleanup::Selection TestSpeechCleanup::embeddedSelection(Settings::SpeechCleanupBackend backend) {
	Mumble::SpeechCleanup::Selection selection;
	selection.backend = backend;
	switch (backend) {
		case Settings::RNNoiseBackend:
			selection.modelId = QStringLiteral("rnnoise:embedded");
			break;
		case Settings::DTLNBackend:
			selection.modelId = QStringLiteral("dtln:baseline");
			break;
		case Settings::DeepFilterNetBackend:
			selection.modelId = QStringLiteral("deepfilternet:default");
			break;
	}
	return selection;
}

std::vector< float > TestSpeechCleanup::testSignal(std::size_t sampleCount) {
	std::vector< float > samples(sampleCount);
	std::uint32_t noiseState = 0x13579bdu;
	for (std::size_t i = 0; i < sampleCount; ++i) {
		noiseState = noiseState * 1664525u + 1013904223u;
		const float noise = static_cast< float >((noiseState >> 8) & 0xffffu) / 32767.5f - 1.0f;
		const float voice = 0.28f * std::sin(static_cast< float >(i) * 0.071f)
						  + 0.13f * std::sin(static_cast< float >(i) * 0.019f);
		samples[i] = std::clamp(voice + 0.08f * noise, -0.8f, 0.8f);
	}
	return samples;
}

std::vector< float > TestSpeechCleanup::processSignal(SpeechCleanupProcessor &processor,
												 const std::vector< float > &input,
												 const std::vector< unsigned int > &chunkPattern,
												 float mixFactor) {
	Q_ASSERT(!chunkPattern.empty());
	std::vector< float > output = input;
	std::size_t offset = 0;
	std::size_t patternIndex = 0;
	while (offset < output.size()) {
		const unsigned int requested = chunkPattern[patternIndex++ % chunkPattern.size()];
		Q_ASSERT(requested > 0);
		const unsigned int chunkSize = static_cast< unsigned int >(
			std::min< std::size_t >(requested, output.size() - offset));
		processor.processInPlace(output.data() + offset, chunkSize, mixFactor);
		offset += chunkSize;
	}
	return output;
}

void TestSpeechCleanup::compareSignals(const std::vector< float > &actual, const std::vector< float > &expected,
										   float tolerance, const char *context) {
	QCOMPARE(actual.size(), expected.size());
	for (std::size_t i = 0; i < actual.size(); ++i) {
		const float difference = std::fabs(actual[i] - expected[i]);
		QVERIFY2(difference <= tolerance,
				 qPrintable(QStringLiteral("%1 differs at sample %2: actual=%3 expected=%4 delta=%5")
							.arg(QString::fromLatin1(context))
							.arg(i)
							.arg(actual[i], 0, 'g', 9)
							.arg(expected[i], 0, 'g', 9)
							.arg(difference, 0, 'g', 9)));
	}
}

void TestSpeechCleanup::verifyChunkInvariant(const Mumble::SpeechCleanup::Selection &selection) {
	constexpr std::size_t SAMPLE_COUNT = 480 * 16;
	const std::vector< float > input = testSignal(SAMPLE_COUNT);

	auto referenceProcessor = createSpeechCleanupProcessor(selection);
	QVERIFY(referenceProcessor);
	QVERIFY(referenceProcessor->isReady());
	const std::vector< float > reference = processSignal(*referenceProcessor, input, { 960 }, 1.0f);

	for (const std::vector< unsigned int > &pattern : {
			 std::vector< unsigned int >{ 120 }, std::vector< unsigned int >{ 240 },
			 std::vector< unsigned int >{ 480 }, std::vector< unsigned int >{ 120, 240, 480, 960 } }) {
		auto processor = createSpeechCleanupProcessor(selection);
		QVERIFY(processor);
		QVERIFY(processor->isReady());
		const std::vector< float > chunked = processSignal(*processor, input, pattern, 1.0f);
		compareSignals(chunked, reference, 1.0e-6f, "chunked output");
	}

	const auto firstNonSilent = std::find_if(reference.begin() + std::min< std::size_t >(
															 referenceProcessor->latencySamples(), reference.size()),
										  reference.end(), [](float sample) { return std::fabs(sample) > 1.0e-4f; });
	QVERIFY2(firstNonSilent != reference.end(), "The streaming comparison must exercise non-silent processed output");
}

void TestSpeechCleanup::verifyDryWetTimeline(const Mumble::SpeechCleanup::Selection &selection) {
	constexpr float MIX_FACTOR = 0.35f;
	const std::vector< float > input = testSignal(480 * 16);

	auto wetProcessor = createSpeechCleanupProcessor(selection);
	auto mixedProcessor = createSpeechCleanupProcessor(selection);
	QVERIFY(wetProcessor && mixedProcessor);
	QVERIFY(wetProcessor->isReady() && mixedProcessor->isReady());
	const unsigned int latency = wetProcessor->latencySamples();
	QVERIFY2(latency > 0, "A block cleanup backend must report its causal output latency");
	QCOMPARE(mixedProcessor->latencySamples(), latency);

	const std::vector< float > wet = processSignal(*wetProcessor, input, { 120, 240, 480, 960 }, 1.0f);
	const std::vector< float > mixed = processSignal(*mixedProcessor, input, { 120, 240, 480, 960 }, MIX_FACTOR);
	std::vector< float > expected(mixed.size(), 0.0f);
	for (std::size_t i = 0; i < expected.size(); ++i) {
		const float delayedDry = i >= latency ? input[i - latency] : 0.0f;
		expected[i] = std::clamp(wet[i] * MIX_FACTOR + delayedDry * (1.0f - MIX_FACTOR), -1.0f, 1.0f);
	}
	compareSignals(mixed, expected, 1.0e-6f, "dry/wet timeline");
}

void TestSpeechCleanup::verifyResetTimeline(const Mumble::SpeechCleanup::Selection &selection) {
	const std::vector< float > input = testSignal(480 * 12);
	auto processor = createSpeechCleanupProcessor(selection);
	QVERIFY(processor);
	QVERIFY(processor->isReady());
	const std::vector< float > first = processSignal(*processor, input, { 120, 240, 480 }, 0.65f);
	processor->reset();
	QVERIFY(processor->isReady());
	const std::vector< float > afterReset = processSignal(*processor, input, { 120, 240, 480 }, 0.65f);
	compareSignals(afterReset, first, 1.0e-6f, "reset output");
}

void TestSpeechCleanup::verifyNonFiniteInputSanitized(const Mumble::SpeechCleanup::Selection &selection) {
	std::vector< float > input = testSignal(480 * 16);
	std::vector< float > sanitizedInput = input;
	input[17]                  = std::numeric_limits< float >::quiet_NaN();
	input[613]                 = std::numeric_limits< float >::infinity();
	input[1441]                = -std::numeric_limits< float >::infinity();
	input[2167]                = 2.0f;
	input[3301]                = -2.0f;
	sanitizedInput[17]         = 0.0f;
	sanitizedInput[613]        = 0.0f;
	sanitizedInput[1441]       = 0.0f;
	sanitizedInput[2167]       = 1.0f;
	sanitizedInput[3301]       = -1.0f;

	auto processor = createSpeechCleanupProcessor(selection);
	auto referenceProcessor = createSpeechCleanupProcessor(selection);
	QVERIFY(processor && referenceProcessor);
	QVERIFY(processor->isReady() && referenceProcessor->isReady());

	const std::vector< float > actual = processSignal(*processor, input, { 17, 120, 511, 37 }, 0.65f);
	const std::vector< float > expected =
		processSignal(*referenceProcessor, sanitizedInput, { 17, 120, 511, 37 }, 0.65f);
	compareSignals(actual, expected, 1.0e-6f, "sanitized cleanup output");
	QVERIFY(std::all_of(actual.cbegin(), actual.cend(), [](float sample) { return std::isfinite(sample); }));
}

#ifdef TEST_EXPECT_RNNOISE
void TestSpeechCleanup::rnnoiseOutputIsIndependentOfInputChunking() {
	verifyChunkInvariant(embeddedSelection(Settings::RNNoiseBackend));
}

void TestSpeechCleanup::rnnoiseAdapterReportsAndPreservesTheRawApiLatency() {
	auto processor = createSpeechCleanupProcessor(embeddedSelection(Settings::RNNoiseBackend));
	QVERIFY(processor);
	QVERIFY(processor->isReady());
#ifdef Q_OS_WIN
	// Embedded weights live in the loader-resolved RNNoise module. Reporting
	// this exact path lets the product pipeline bind its signed hash to the DLL
	// that actually supplied those weights.
	QVERIFY(QFileInfo(processor->activeModelPath()).isFile());
	QCOMPARE(QFileInfo(processor->activeModelPath()).fileName().toLower(), QStringLiteral("rnnoise.dll"));
#endif
	// This value is intentionally independent of the implementation constant: it
	// protects the two-frame RNNoise transform delay plus the adapter's one-frame
	// collection delay from being collapsed into the same mistaken expectation.
	QCOMPARE(processor->latencySamples(), 1440u);

#ifdef TEST_BUNDLED_RNNOISE
	constexpr unsigned int FRAME_SIZE = 480;
	constexpr float PCM_SCALE = 32768.0f;
	constexpr std::size_t FRAME_COUNT = 8;
	constexpr std::size_t IMPULSE_INDEX = 2 * FRAME_SIZE + 137;

	std::vector< float > input(FRAME_COUNT * FRAME_SIZE, 0.0f);
	// Keep the analysis energy below RNNoise's silence threshold so the raw API
	// acts as a deterministic transform-delay reference rather than invoking the
	// neural suppression path.
	input[IMPULSE_INDEX] = 1.0e-5f;

	std::vector< float > rawOutput(input.size(), 0.0f);
	std::array< float, FRAME_SIZE > rawInput = {};
	std::array< float, FRAME_SIZE > rawFrameOutput = {};
	DenoiseState *rawState = rnnoise_create(nullptr);
	QVERIFY(rawState);
	for (std::size_t frame = 0; frame < FRAME_COUNT; ++frame) {
		for (std::size_t sample = 0; sample < FRAME_SIZE; ++sample) {
			rawInput[sample] = input[frame * FRAME_SIZE + sample] * PCM_SCALE;
		}
		rnnoise_process_frame(rawState, rawFrameOutput.data(), rawInput.data());
		for (std::size_t sample = 0; sample < FRAME_SIZE; ++sample) {
			rawOutput[frame * FRAME_SIZE + sample] = rawFrameOutput[sample] / PCM_SCALE;
		}
	}
	rnnoise_destroy(rawState);

	const std::vector< float > adapterOutput = processSignal(*processor, input, { 120, 240, 480 }, 1.0f);
	for (std::size_t sample = 0; sample < FRAME_SIZE; ++sample) {
		QCOMPARE(adapterOutput[sample], 0.0f);
	}
	for (std::size_t sample = FRAME_SIZE; sample < adapterOutput.size(); ++sample) {
		QVERIFY2(std::fabs(adapterOutput[sample] - rawOutput[sample - FRAME_SIZE]) <= 1.0e-8f,
				 qPrintable(QStringLiteral("adapter/raw mismatch at sample %1").arg(sample)));
	}

	const auto rawPeak = std::max_element(rawOutput.cbegin(), rawOutput.cend(), [](float left, float right) {
		return std::fabs(left) < std::fabs(right);
	});
	const auto adapterPeak = std::max_element(adapterOutput.cbegin(), adapterOutput.cend(), [](float left, float right) {
		return std::fabs(left) < std::fabs(right);
	});
	QVERIFY(rawPeak != rawOutput.cend());
	QVERIFY(adapterPeak != adapterOutput.cend());
	QCOMPARE(static_cast< std::size_t >(std::distance(rawOutput.cbegin(), rawPeak)), IMPULSE_INDEX + 2 * FRAME_SIZE);
	QCOMPARE(static_cast< std::size_t >(std::distance(adapterOutput.cbegin(), adapterPeak)),
			 IMPULSE_INDEX + 3 * FRAME_SIZE);
#endif
}

void TestSpeechCleanup::rnnoiseDryWetMixUsesTheProcessedTimeline() {
	verifyDryWetTimeline(embeddedSelection(Settings::RNNoiseBackend));
}

void TestSpeechCleanup::rnnoiseResetRestoresTheStreamingTimeline() {
	verifyResetTimeline(embeddedSelection(Settings::RNNoiseBackend));
}

void TestSpeechCleanup::rnnoiseSanitizesNonFiniteInputWithoutPoisoningItsState() {
	verifyNonFiniteInputSanitized(embeddedSelection(Settings::RNNoiseBackend));
}
#endif

#ifdef TEST_EXPECT_DTLN
void TestSpeechCleanup::dtlnOutputIsIndependentOfInputChunking() {
	verifyChunkInvariant(embeddedSelection(Settings::DTLNBackend));
}

void TestSpeechCleanup::dtlnReportsAndAppliesItsCausalLatency() {
	auto processor = createSpeechCleanupProcessor(embeddedSelection(Settings::DTLNBackend));
	QVERIFY(processor);
	QVERIFY(processor->isReady());
	// 512 samples at 16 kHz map to 1536 samples at 48 kHz. The quality-5
	// downsampler input latency and upsampler output latency are 120 samples each.
	constexpr unsigned int EXPECTED_LATENCY = 1536 + 120 + 120;
	QCOMPARE(processor->latencySamples(), EXPECTED_LATENCY);

	constexpr std::size_t IMPULSE_INDEX = 4800;
	std::vector< float > input(EXPECTED_LATENCY + IMPULSE_INDEX + 480, 0.0f);
	input[IMPULSE_INDEX] = 0.5f;
	const std::vector< float > dry = processSignal(*processor, input, { 17, 120, 511, 37 }, 0.0f);
	for (std::size_t i = 0; i < dry.size(); ++i) {
		const float expected = i == IMPULSE_INDEX + EXPECTED_LATENCY ? 0.5f : 0.0f;
		QVERIFY2(std::fabs(dry[i] - expected) <= 1.0e-8f,
				 qPrintable(QStringLiteral("DTLN dry latency mismatch at sample %1").arg(i)));
	}

	auto wetProcessor = createSpeechCleanupProcessor(embeddedSelection(Settings::DTLNBackend));
	QVERIFY(wetProcessor && wetProcessor->isReady());
	const std::vector< float > wet = processSignal(*wetProcessor, input, { 17, 120, 511, 37 }, 1.0f);
	const auto peak = std::max_element(wet.cbegin(), wet.cend(), [](float left, float right) {
		return std::fabs(left) < std::fabs(right);
	});
	QVERIFY(peak != wet.cend());
	QVERIFY2(std::fabs(*peak) > 1.0e-4f, "The DTLN impulse probe was unexpectedly silent");
	QCOMPARE(static_cast< std::size_t >(std::distance(wet.cbegin(), peak)),
			 IMPULSE_INDEX + EXPECTED_LATENCY);
}

void TestSpeechCleanup::dtlnDryWetMixUsesTheProcessedTimeline() {
	verifyDryWetTimeline(embeddedSelection(Settings::DTLNBackend));
}

void TestSpeechCleanup::dtlnResetRestoresTheStreamingTimeline() {
	verifyResetTimeline(embeddedSelection(Settings::DTLNBackend));
}

void TestSpeechCleanup::dtlnSanitizesNonFiniteInputWithoutPoisoningItsState() {
	verifyNonFiniteInputSanitized(embeddedSelection(Settings::DTLNBackend));
}
#endif

void TestSpeechCleanup::deepFilterNetOutputIsIndependentOfInputChunking() {
	auto probe = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(probe && probe->isReady(), "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!probe || !probe->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif
	verifyChunkInvariant(embeddedSelection(Settings::DeepFilterNetBackend));
}

void TestSpeechCleanup::deepFilterNetModelDomainNormalizationRemainsContinuousAcrossFrameGainChanges() {
	constexpr std::size_t FRAME_SAMPLES = 480;
	constexpr std::size_t FRAME_COUNT   = 80;
	constexpr float PI                   = 3.14159265358979323846f;
	constexpr float FREQUENCY_HZ         = 1000.0f;
	constexpr float SAMPLE_RATE          = 48000.0f;

	auto selection                     = embeddedSelection(Settings::DeepFilterNetBackend);
	selection.modelId                  = QStringLiteral("deepfilternet:low-latency");
	selection.modelDomainNormalization = true;
	auto processor                      = createSpeechCleanupProcessor(selection);
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(processor && processor->isReady(),
			 "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!processor || !processor->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif

	// One kilohertz completes exactly ten cycles per 10 ms processor frame, so
	// the source crosses zero at every tested boundary. Alternating 0.006 and
	// 0.0108 peaks moves the model-domain gain between the 8x cap and roughly
	// 5.84x without introducing a source-value step. The isolated 0.05 transient
	// forces one frame down to roughly 1.26x and exercises recovery on its end.
	std::vector< float > input(FRAME_COUNT * FRAME_SAMPLES, 0.0f);
	for (std::size_t frame = 0; frame < FRAME_COUNT; ++frame) {
		const float amplitude = frame < 20 || frame >= 68 ? 0.002f
							 : ((frame % 2) == 0 ? 0.006f : 0.0108f);
		for (std::size_t offset = 0; offset < FRAME_SAMPLES; ++offset) {
			const std::size_t index = frame * FRAME_SAMPLES + offset;
			input[index] = amplitude * std::sin(
				2.0f * PI * FREQUENCY_HZ * static_cast< float >(index) / SAMPLE_RATE);
		}
	}
	input[50 * FRAME_SAMPLES + FRAME_SAMPLES / 2] = 0.05f;

	const unsigned int latency = processor->latencySamples();
	QVERIFY2(latency > 0, "DeepFilterNet normalization regression requires a causal processor latency");
	const std::vector< float > output = processSignal(*processor, input, { 480 }, 1.0f);
	QVERIFY(std::all_of(output.cbegin(), output.cend(), [](float sample) {
		return std::isfinite(sample) && std::fabs(sample) < 1.0f;
	}));

	for (std::size_t frame = 30; frame <= 68; ++frame) {
		const std::size_t boundary = frame * FRAME_SAMPLES + latency;
		QVERIFY(boundary > 0 && boundary < output.size());
		const float delta = std::fabs(output[boundary] - output[boundary - 1]);
		QVERIFY2(delta < 1.0e-3f,
				 qPrintable(QStringLiteral("DeepFilterNet normalized gain boundary discontinuity at frame %1: %2")
							.arg(frame)
							.arg(delta, 0, 'g', 9)));
	}
}

void TestSpeechCleanup::deepFilterNetRealtimeTimelineMatchesReportedLatency() {
	auto processor = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(processor && processor->isReady(),
			 "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!processor || !processor->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif
	QVERIFY(processor->prepareRealtime());
	QVERIFY(processor->isReady());
	QCOMPARE(processor->workerSchedulingDelayFrames(), 2u);

	constexpr unsigned int FRAME_SAMPLES = 480;
	constexpr std::size_t IMPULSE_INDEX  = FRAME_SAMPLES * 4;
	const unsigned int reportedLatency   = processor->latencySamples();
	QVERIFY2(reportedLatency > 0, "The real-time DeepFilterNet adapter must report its causal latency");

	std::vector< float > signal(IMPULSE_INDEX + reportedLatency + FRAME_SAMPLES * 4, 0.0f);
	signal[IMPULSE_INDEX] = 0.5f;
	for (std::size_t offset = 0; offset < signal.size(); offset += FRAME_SAMPLES) {
		QVERIFY2(processor->prepareOfflineFrame(), "The real-time worker did not complete before its deadline");
		processor->processInPlace(signal.data() + offset, FRAME_SAMPLES, 0.0f);
		QVERIFY(processor->isReady());
	}
	QVERIFY(processor->finishOfflineProcessing());

	for (std::size_t index = 0; index < signal.size(); ++index) {
		const float expected = index == IMPULSE_INDEX + reportedLatency ? 0.5f : 0.0f;
		QVERIFY2(std::fabs(signal[index] - expected) <= 1.0e-8f,
				 qPrintable(QStringLiteral("DeepFilterNet real-time dry latency mismatch at sample %1; reported=%2")
							.arg(index)
							.arg(reportedLatency)));
	}

	auto wetProcessor = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
	QVERIFY(wetProcessor && wetProcessor->isReady());
	QVERIFY(wetProcessor->prepareRealtime());
	QCOMPARE(wetProcessor->latencySamples(), reportedLatency);
	std::vector< float > wetSignal(signal.size(), 0.0f);
	wetSignal[IMPULSE_INDEX] = 0.5f;
	for (std::size_t offset = 0; offset < wetSignal.size(); offset += FRAME_SAMPLES) {
		QVERIFY(wetProcessor->prepareOfflineFrame());
		wetProcessor->processInPlace(wetSignal.data() + offset, FRAME_SAMPLES, 1.0f);
		QVERIFY(wetProcessor->isReady());
	}
	QVERIFY(wetProcessor->finishOfflineProcessing());
	const auto wetPeak = std::max_element(wetSignal.cbegin(), wetSignal.cend(), [](float left, float right) {
		return std::fabs(left) < std::fabs(right);
	});
	QVERIFY(wetPeak != wetSignal.cend());
	QVERIFY2(std::fabs(*wetPeak) > 1.0e-4f, "The real-time DeepFilterNet impulse probe was unexpectedly silent");
	const std::size_t wetPeakIndex = static_cast< std::size_t >(std::distance(wetSignal.cbegin(), wetPeak));
	const std::size_t expectedWetPeak = IMPULSE_INDEX + reportedLatency;
	QVERIFY2(wetPeakIndex >= expectedWetPeak - 1 && wetPeakIndex <= expectedWetPeak + 1,
			 qPrintable(QStringLiteral("DeepFilterNet real-time wet peak mismatch: actual=%1 expected=%2")
						.arg(wetPeakIndex)
						.arg(expectedWetPeak)));
}

void TestSpeechCleanup::deepFilterNetDryWetMixUsesTheProcessedTimeline() {
	auto probe = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(probe && probe->isReady(), "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!probe || !probe->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif
	verifyDryWetTimeline(embeddedSelection(Settings::DeepFilterNetBackend));
}

void TestSpeechCleanup::deepFilterNetResetRestoresTheStreamingTimeline() {
	auto probe = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(probe && probe->isReady(), "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!probe || !probe->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif
	verifyResetTimeline(embeddedSelection(Settings::DeepFilterNetBackend));
}

void TestSpeechCleanup::deepFilterNetSanitizesNonFiniteInputWithoutPoisoningItsState() {
	auto probe = createSpeechCleanupProcessor(embeddedSelection(Settings::DeepFilterNetBackend));
#ifdef TEST_EXPECT_DEEPFILTERNET
	QVERIFY2(probe && probe->isReady(), "DeepFilterNet was enabled but its runtime/model test payload is unavailable");
#else
	if (!probe || !probe->isReady()) {
		QSKIP("DeepFilterNet runtime/model is not available in this build");
	}
#endif
	verifyNonFiniteInputSanitized(embeddedSelection(Settings::DeepFilterNetBackend));
}

QTEST_GUILESS_MAIN(TestSpeechCleanup)
#include "TestSpeechCleanup.moc"
