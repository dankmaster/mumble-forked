// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RNNoiseSpeechCleanup.h"
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
#include <QtTest>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

class TestSpeechCleanup : public QObject {
	Q_OBJECT

private slots:
	void transmitDrainUsesExactLatencyAndTerminatesOnlyItsLastFrame();
	void transmitDrainCanBeCancelledForFreshSpeech();
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

void TestSpeechCleanup::transmitDrainUsesExactLatencyAndTerminatesOnlyItsLastFrame() {
	Mumble::SpeechCleanup::TransmitDrain drain;
	drain.begin(1776);

	std::vector< unsigned int > drainedSamples;
	while (drain.active()) {
		const auto frame = drain.takeFrame(480);
		QVERIFY(frame.draining);
		drainedSamples.push_back(frame.zeroInputSamples);
		QCOMPARE(frame.terminator, !drain.active());
	}

	QCOMPARE(drainedSamples, (std::vector< unsigned int >{ 480, 480, 480, 336 }));
	QCOMPARE(std::accumulate(drainedSamples.cbegin(), drainedSamples.cend(), 0u), 1776u);
	QCOMPARE(drain.remainingSamples(), 0u);

	const auto exhaustedFrame = drain.takeFrame(480);
	QVERIFY(!exhaustedFrame.draining);
	QVERIFY(!exhaustedFrame.terminator);
	QCOMPARE(exhaustedFrame.zeroInputSamples, 0u);

	drain.begin(0);
	QVERIFY(!drain.active());
}

void TestSpeechCleanup::transmitDrainCanBeCancelledForFreshSpeech() {
	Mumble::SpeechCleanup::TransmitDrain drain;
	drain.begin(1440);

	const auto firstFrame = drain.takeFrame(480);
	QVERIFY(firstFrame.draining);
	QVERIFY(!firstFrame.terminator);
	QCOMPARE(drain.remainingSamples(), 960u);

	// AudioInput invokes this on either a fresh VAD activation or PTT press. No
	// cleanup reset is coupled to cancellation, so the live processor timeline
	// can continue immediately with the new microphone frame.
	drain.cancel();
	QVERIFY(!drain.active());
	QCOMPARE(drain.remainingSamples(), 0u);
	QVERIFY(!drain.takeFrame(480).draining);
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
	QVERIFY(cleanup.activeModelPath().isEmpty());

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
