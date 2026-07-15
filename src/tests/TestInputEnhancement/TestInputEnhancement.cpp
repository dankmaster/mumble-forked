// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetRealtimeWorker.h"
#include "InputEnhancement.h"
#include "SpeechCleanupProcessor.h"

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace Mumble::InputEnhancement;

namespace {
struct FakeState {
	int factoryCalls                 = 0;
	int processCalls                 = 0;
	int resetCalls                   = 0;
	int processCallsAtLastReset      = 0;
	int prepareCalls                 = 0;
	int processCallsAtPrepare        = 0;
	int offlinePrepareCalls          = 0;
	int offlineFinishCalls           = 0;
	int coldProcessCalls             = 0;
	bool ready                       = true;
	bool prepareResult               = true;
	bool offlinePrepareResult        = true;
	bool offlineFinishResult         = true;
	bool realtimePrepared            = false;
	bool failAfterPrepare            = false;
	bool usedFallback                = false;
	bool returnNull                  = false;
	bool emitNonFinite               = false;
	bool emitOutOfRange              = false;
	bool throwOnProcess              = false;
	bool applyReportedLatency        = false;
	bool inputWasFiniteAndClamped    = true;
	bool coldProcessPending          = true;
	unsigned int latency             = 960;
	unsigned int latencyAfterPrepare = 0;
	float lastMix                    = -1.0f;
	QString activeModelId;
	QString activeModelPath;
};

class FakeProcessor final : public SpeechCleanupProcessor {
public:
	explicit FakeProcessor(FakeState &state) : m_state(state) {}

	bool isReady() const override { return m_state.ready; }
	void reset() override {
		++m_state.resetCalls;
		m_state.processCallsAtLastReset = m_state.processCalls;
		m_state.coldProcessPending      = true;
		m_state.realtimePrepared        = false;
		m_delay.fill(0.0f);
		m_delayPosition = 0;
	}
	bool prepareRealtime() override {
		++m_state.prepareCalls;
		m_state.processCallsAtPrepare = m_state.processCalls;
		m_state.realtimePrepared      = m_state.prepareResult;
		if (m_state.prepareResult && m_state.latencyAfterPrepare > 0) {
			m_state.latency = m_state.latencyAfterPrepare;
		}
		return m_state.prepareResult;
	}
	bool prepareOfflineFrame() noexcept override {
		++m_state.offlinePrepareCalls;
		return m_state.offlinePrepareResult;
	}
	bool finishOfflineProcessing() noexcept override {
		++m_state.offlineFinishCalls;
		return m_state.offlineFinishResult;
	}
	unsigned int latencySamples() const override { return m_state.latency; }
	QString activeModelId() const override { return m_state.activeModelId; }
	QString activeModelPath() const override { return m_state.activeModelPath; }
	bool usedFallback() const override { return m_state.usedFallback; }

	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) override {
		++m_state.processCalls;
		if (m_state.coldProcessPending) {
			++m_state.coldProcessCalls;
			m_state.coldProcessPending = false;
		}
		m_state.lastMix = mixFactor;
		for (unsigned int i = 0; i < sampleCount; ++i) {
			m_state.inputWasFiniteAndClamped = m_state.inputWasFiniteAndClamped && std::isfinite(samples[i])
											   && samples[i] >= -1.0f && samples[i] <= 1.0f;
		}
		if (m_state.throwOnProcess) {
			throw std::runtime_error("fake processor failure");
		}
		if (m_state.applyReportedLatency && m_state.latency > 0) {
			for (unsigned int i = 0; i < sampleCount; ++i) {
				const float delayed      = m_delay[m_delayPosition];
				m_delay[m_delayPosition] = samples[i];
				m_delayPosition          = (m_delayPosition + 1) % m_state.latency;
				samples[i]               = delayed;
			}
		}
		if (m_state.emitNonFinite) {
			samples[7] = std::numeric_limits< float >::quiet_NaN();
		} else if (m_state.emitOutOfRange) {
			samples[0] = 2.0f;
			samples[1] = -3.0f;
		}
		if (m_state.failAfterPrepare && m_state.realtimePrepared) {
			m_state.ready = false;
		}
	}

private:
	FakeState &m_state;
	std::array< float, crispLatencyBudgetSamples > m_delay = {};
	unsigned int m_delayPosition                           = 0;
};

Pipeline::ProcessorFactory factoryFor(FakeState &state) {
	return [&state](const Recipe &recipe) -> std::unique_ptr< SpeechCleanupProcessor > {
		++state.factoryCalls;
		if (state.returnNull) {
			return {};
		}
		if (state.activeModelId.isEmpty()) {
			state.activeModelId = recipe.modelId();
		}
		return std::make_unique< FakeProcessor >(state);
	};
}

ResolveRequest requestFor(Profile profile) {
	ResolveRequest request;
	request.profile             = profile;
	request.backendAvailability = { true, true, true };
	return request;
}

int enumValue(Profile value) {
	return static_cast< int >(value);
}
int enumValue(Engine value) {
	return static_cast< int >(value);
}
int enumValue(FallbackReason value) {
	return static_cast< int >(value);
}

struct RealtimeWorkerState {
	std::atomic_bool releaseProcess{ true };
	std::atomic_uint enteredCalls{ 0 };
	std::atomic_uint processCalls{ 0 };
	std::atomic_int returnStatus{ DeepFilterNetRealtimeWorker::statusOk };
	float offset = 0.125f;
};

constexpr int backendProcessError = 2;

int processRealtimeWorkerFrame(void *context, const float *input, float *output) noexcept {
	auto &state = *static_cast< RealtimeWorkerState * >(context);
	state.enteredCalls.fetch_add(1, std::memory_order_release);
	while (!state.releaseProcess.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	for (unsigned int i = 0; i < DeepFilterNetRealtimeWorker::frameSamples; ++i) {
		output[i] = input[i] + state.offset;
	}
	state.processCalls.fetch_add(1, std::memory_order_release);
	return state.returnStatus.load(std::memory_order_acquire);
}
} // namespace

static_assert(!std::is_copy_assignable_v< Recipe >);
static_assert(!std::is_move_assignable_v< Recipe >);
static_assert(!std::is_copy_assignable_v< Diagnostics >);

class TestInputEnhancement : public QObject {
	Q_OBJECT

private slots:
	void catalogDefinesStableProductRecipes();
	void catalogClampsControlsAndInterpolatesMix();
	void autoPolicyUsesCpuAndBackendAvailability();
	void explicitProfilesFallBackDeterministically();
	void defaultPipelineStartsAsZeroLatencyOriginal();
	void originalPreservesPcmBitsWithoutInitializingProcessor();
	void originalAndLightNeverConstructNeuralProcessor();
	void neuralRecipeIsPreparedAndReported();
	void neuralRecipeBindsAuthorizationToLoadedAsset();
	void offlinePreparationDelegatesAndFailsClosed();
	void neuralRecipeWarmupRunsDuringConfigureAndResetsState();
	void neuralRealtimePreparationRunsAfterFinalReset();
	void realtimePreparationFailureFailsClosed();
	void neuralRecipeWarmupFailureFailsClosed();
	void liveMixOverrideUsesConfiguredProcessorWithoutReconfigure();
	void preparationFailuresFailClosed();
	void runtimeUnhealthyProcessorFailsClosed();
	void neuralInputIsSanitizedAndOutputIsClamped();
	void invalidOutputPermanentlyFallsBackToOriginal();
	void processorExceptionPermanentlyFallsBackToOriginal();
	void deadlineMissIsMeasuredAndFailsClosed();
	void callbackPercentilesIgnoreSingleSubDeadlineOutlier();
	void invalidFrameSizeFailsClosed();
	void deepFilterWorkerEmitsAfterTwoFrames();
	void deepFilterWorkerOfflineDriveWaitsForRequiredFrame();
	void deepFilterWorkerWakeDoesNotPollAtWindowsTimerGranularity();
	void deepFilterWorkerOfflineWaitTimeoutLatches();
	void deepFilterWorkerLateResultFailsClosed();
	void deepFilterWorkerBackendFailureLatches();
};

void TestInputEnhancement::catalogDefinesStableProductRecipes() {
	struct Expected {
		Profile profile;
		Engine engine;
		const char *id;
		const char *modelId;
		unsigned int latencyBudget;
	};
	const std::array< Expected, 4 > expected = { {
		{ Profile::Original, Engine::None, "input.original", "", originalLatencyBudgetSamples },
		{ Profile::Light, Engine::Speex, "input.light.speex", "", lightLatencyBudgetSamples },
		{ Profile::Balanced, Engine::RNNoise, "input.balanced.rnnoise-embedded", "rnnoise:embedded",
		  balancedLatencyBudgetSamples },
		{ Profile::Crisp, Engine::DeepFilterNet, "input.crisp.deepfilternet-balanced", "deepfilternet:balanced",
		  crispLatencyBudgetSamples },
	} };

	for (const Expected &item : expected) {
		const Recipe recipe = RecipeCatalog::resolve(requestFor(item.profile));
		QCOMPARE(enumValue(recipe.requestedProfile()), enumValue(item.profile));
		QCOMPARE(enumValue(recipe.effectiveProfile()), enumValue(item.profile));
		QCOMPARE(enumValue(recipe.engine()), enumValue(item.engine));
		QCOMPARE(recipe.id(), QString::fromLatin1(item.id));
		QCOMPARE(recipe.modelId(), QString::fromLatin1(item.modelId));
		QCOMPARE(recipe.revision(), RecipeCatalog::currentRevision);
		QCOMPARE(recipe.latencyBudgetSamples(), item.latencyBudget);
	}
}

void TestInputEnhancement::catalogClampsControlsAndInterpolatesMix() {
	ResolveRequest request = requestFor(Profile::Balanced);
	request.noiseReduction = -12;
	request.naturalCrisp   = 151;
	const Recipe minimum   = RecipeCatalog::resolve(request);
	QCOMPARE(minimum.noiseReduction(), 20);
	QCOMPARE(minimum.naturalCrisp(), 90);
	QVERIFY(std::abs(minimum.mixFactor() - 0.195f) < 0.00001f);

	request.noiseReduction = 100;
	const Recipe maximum   = RecipeCatalog::resolve(request);
	QCOMPARE(maximum.noiseReduction(), 90);
	QCOMPARE(maximum.naturalCrisp(), 90);
	QVERIFY(std::abs(maximum.mixFactor() - 0.8775f) < 0.00001f);

	request.noiseReduction = 50;
	request.naturalCrisp   = 0;
	const Recipe natural   = RecipeCatalog::resolve(request);
	QCOMPARE(natural.noiseReduction(), 55);
	QCOMPARE(natural.naturalCrisp(), 10);
	QVERIFY(std::abs(natural.mixFactor() - 0.42625f) < 0.00001f);

	const ValidatedControls crispMinimum = validatedControlsForProfile(Profile::Crisp, 0, 0);
	QCOMPARE(crispMinimum.noiseReduction, 25);
	QCOMPARE(crispMinimum.naturalCrisp, 25);
	const ValidatedControls crispMaximum = validatedControlsForProfile(Profile::Crisp, 100, 100);
	QCOMPARE(crispMaximum.noiseReduction, 90);
	QCOMPARE(crispMaximum.naturalCrisp, 100);
	QCOMPARE(mixFactorForControls(Profile::Original, 100, 100), 0.0f);
}

void TestInputEnhancement::autoPolicyUsesCpuAndBackendAvailability() {
	ResolveRequest request = requestFor(Profile::Auto);
	request.cpuClass       = CpuClass::Low;
	QCOMPARE(enumValue(RecipeCatalog::resolve(request).effectiveProfile()), enumValue(Profile::Light));

	request.cpuClass = CpuClass::Standard;
	QCOMPARE(enumValue(RecipeCatalog::resolve(request).effectiveProfile()), enumValue(Profile::Balanced));

	request.cpuClass = CpuClass::High;
	QCOMPARE(enumValue(RecipeCatalog::resolve(request).effectiveProfile()), enumValue(Profile::Crisp));

	request.backendAvailability.deepFilterNet = false;
	QCOMPARE(enumValue(RecipeCatalog::resolve(request).effectiveProfile()), enumValue(Profile::Balanced));

	request.backendAvailability.rnnoise = false;
	QCOMPARE(enumValue(RecipeCatalog::resolve(request).effectiveProfile()), enumValue(Profile::Light));
}

void TestInputEnhancement::explicitProfilesFallBackDeterministically() {
	ResolveRequest balanced              = requestFor(Profile::Balanced);
	balanced.backendAvailability.rnnoise = false;
	const Recipe lightFallback           = RecipeCatalog::resolve(balanced);
	QCOMPARE(enumValue(lightFallback.effectiveProfile()), enumValue(Profile::Light));
	QCOMPARE(lightFallback.id(), QStringLiteral("input.balanced.fallback-light.speex"));

	ResolveRequest crisp                    = requestFor(Profile::Crisp);
	crisp.backendAvailability.deepFilterNet = false;
	const Recipe balancedFallback           = RecipeCatalog::resolve(crisp);
	QCOMPARE(enumValue(balancedFallback.effectiveProfile()), enumValue(Profile::Balanced));
	QCOMPARE(balancedFallback.id(), QStringLiteral("input.crisp.fallback-balanced.rnnoise-embedded"));

	crisp.backendAvailability.rnnoise = false;
	QCOMPARE(enumValue(RecipeCatalog::resolve(crisp).effectiveProfile()), enumValue(Profile::Light));
}

void TestInputEnhancement::defaultPipelineStartsAsZeroLatencyOriginal() {
	FakeState state;
	Pipeline pipeline(factoryFor(state));

	QCOMPARE(state.factoryCalls, 0);
	QCOMPARE(pipeline.latencySamples(), 0u);
	QVERIFY(!pipeline.fallbackActive());
	QCOMPARE(pipeline.lastWorkerProcessingNanoseconds(), std::uint64_t{ 0 });
	QCOMPARE(pipeline.workerPendingFrames(), 0u);
	QCOMPARE(pipeline.workerSchedulingDelayFrames(), 0u);
	QCOMPARE(pipeline.workerSchedulingSlackFrames(), 0u);

	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.requestedRecipeId(), QStringLiteral("input.original"));
	QCOMPARE(enumValue(diagnostics.requestedProfile()), enumValue(Profile::Original));
	QCOMPARE(enumValue(diagnostics.activeProfile()), enumValue(Profile::Original));
	QCOMPARE(enumValue(diagnostics.activeEngine()), enumValue(Engine::None));
	QCOMPARE(diagnostics.actualLatencySamples(), 0u);
	QCOMPARE(diagnostics.processedFrames(), std::uint64_t{ 0 });
	QCOMPARE(diagnostics.neuralFrames(), std::uint64_t{ 0 });
}

void TestInputEnhancement::originalPreservesPcmBitsWithoutInitializingProcessor() {
	FakeState state;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Original))));

	constexpr std::array< std::int16_t, 8 > pcmValues = {
		std::numeric_limits< std::int16_t >::min(), -32767, -12345, -1, 0, 1, 12345,
		std::numeric_limits< std::int16_t >::max(),
	};
	std::array< float, frameSamples > frame = {};
	for (std::size_t index = 0; index < frame.size(); ++index) {
		frame[index] = static_cast< float >(pcmValues[index % pcmValues.size()]) / 32768.0f;
	}
	const auto original = frame;

	// Multiple 10 ms calls also cover the steady-state pass-through contract;
	// packet grouping remains protected separately by the source/evidence check.
	for (int call = 0; call < 8; ++call) {
		QVERIFY(!pipeline.processFrame(frame));
		QCOMPARE(std::memcmp(frame.data(), original.data(), sizeof(frame)), 0);
	}

	QCOMPARE(state.factoryCalls, 0);
	QCOMPARE(state.processCalls, 0);
	QCOMPARE(pipeline.latencySamples(), 0u);
	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.processedFrames(), std::uint64_t{ 8 });
	QCOMPARE(diagnostics.neuralFrames(), std::uint64_t{ 0 });
}

void TestInputEnhancement::originalAndLightNeverConstructNeuralProcessor() {
	FakeState state;
	Pipeline pipeline(factoryFor(state));
	std::array< float, frameSamples > frame;
	std::fill(frame.begin(), frame.end(), 0.25f);
	const auto original = frame;

	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Original))));
	QVERIFY(!pipeline.processFrame(frame));
	QVERIFY(frame == original);
	QCOMPARE(state.factoryCalls, 0);

	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Light))));
	QVERIFY(!pipeline.processFrame(frame));
	QVERIFY(frame == original);
	QCOMPARE(state.factoryCalls, 0);
	QCOMPARE(enumValue(pipeline.diagnostics().activeEngine()), enumValue(Engine::Speex));
}

void TestInputEnhancement::neuralRecipeIsPreparedAndReported() {
	FakeState state;
	Pipeline pipeline(factoryFor(state), [] { return std::uint64_t{ 100 }; });
	const Recipe recipe = RecipeCatalog::resolve(requestFor(Profile::Balanced));
	QTemporaryDir root;
	QVERIFY(root.isValid());
	state.activeModelPath = QDir(root.path()).filePath(QStringLiteral("rnnoise.dll"));
	QFile model(state.activeModelPath);
	QVERIFY(model.open(QIODevice::WriteOnly));
	QCOMPARE(model.write("embedded-rnnoise-runtime"), qint64(24));
	model.close();
	const QString modelSha256 = QString::fromLatin1(
		QCryptographicHash::hash(QByteArrayLiteral("embedded-rnnoise-runtime"), QCryptographicHash::Sha256).toHex());
	QVERIFY(pipeline.configure(recipe, modelSha256, state.activeModelPath));
	QCOMPARE(state.factoryCalls, 1);
	QCOMPARE(state.processCalls,
			 static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames));
	QCOMPARE(state.resetCalls, 2);
	QCOMPARE(state.prepareCalls, 1);
	QCOMPARE(state.offlinePrepareCalls, 0);

	std::array< float, frameSamples > frame = {};
	QVERIFY(pipeline.processFrame(frame));
	// The real-time process path must never invoke the blocking offline hook.
	QCOMPARE(state.offlinePrepareCalls, 0);
	QCOMPARE(state.processCalls
				 - static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames),
			 1);
	QVERIFY(std::abs(state.lastMix - recipe.mixFactor()) < 0.00001f);

	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.requestedRecipeId(), recipe.id());
	QCOMPARE(enumValue(diagnostics.activeProfile()), enumValue(Profile::Balanced));
	QCOMPARE(enumValue(diagnostics.activeEngine()), enumValue(Engine::RNNoise));
	QCOMPARE(diagnostics.activeModelSha256(), modelSha256);
	QCOMPARE(diagnostics.actualLatencySamples(), 960u);
	QCOMPARE(diagnostics.processedFrames(), std::uint64_t{ 1 });
	QCOMPARE(diagnostics.neuralFrames(), std::uint64_t{ 1 });
}

void TestInputEnhancement::neuralRecipeBindsAuthorizationToLoadedAsset() {
	const Recipe recipe = RecipeCatalog::resolve(requestFor(Profile::Balanced));
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QByteArray originalBytes = QByteArrayLiteral("signed-embedded-rnnoise-runtime");
	const QString loadedPath       = QDir(root.path()).filePath(QStringLiteral("rnnoise.dll"));
	const QString otherPath        = QDir(root.path()).filePath(QStringLiteral("other-rnnoise.dll"));
	for (const QString &path : { loadedPath, otherPath }) {
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(originalBytes), qint64(originalBytes.size()));
	}
	const QString signedHash =
		QString::fromLatin1(QCryptographicHash::hash(originalBytes, QCryptographicHash::Sha256).toHex());

	// rnnoise:embedded identifies the actual loaded RNNoise module, not an
	// empty pseudo-path. Matching bytes at a different path are insufficient.
	FakeState pathMismatch;
	pathMismatch.activeModelPath = loadedPath;
	Pipeline wrongPath(factoryFor(pathMismatch));
	QVERIFY(!wrongPath.configure(recipe, signedHash, otherPath));
	QCOMPARE(enumValue(wrongPath.diagnostics().fallbackReason()), enumValue(FallbackReason::UnexpectedModel));

	FakeState missingPath;
	missingPath.activeModelPath = loadedPath;
	Pipeline incompleteAuthorization(factoryFor(missingPath));
	QVERIFY(!incompleteAuthorization.configure(recipe, signedHash));
	QCOMPARE(enumValue(incompleteAuthorization.diagnostics().fallbackReason()),
			 enumValue(FallbackReason::UnexpectedModel));

	QFile tampered(loadedPath);
	QVERIFY(tampered.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(tampered.write(QByteArray(originalBytes.size(), 'X')), qint64(originalBytes.size()));
	tampered.close();
	FakeState hashMismatch;
	hashMismatch.activeModelPath = loadedPath;
	Pipeline wrongHash(factoryFor(hashMismatch));
	QVERIFY(!wrongHash.configure(recipe, signedHash, loadedPath));
	QCOMPARE(enumValue(wrongHash.diagnostics().fallbackReason()), enumValue(FallbackReason::UnexpectedModel));
}

void TestInputEnhancement::offlinePreparationDelegatesAndFailsClosed() {
	FakeState originalState;
	Pipeline original(factoryFor(originalState));
	QVERIFY(original.configure(RecipeCatalog::resolve(requestFor(Profile::Original))));
	QVERIFY(original.prepareOfflineFrame());
	QVERIFY(original.finishOfflineProcessing());
	QCOMPARE(originalState.factoryCalls, 0);
	QCOMPARE(originalState.offlinePrepareCalls, 0);
	QCOMPARE(originalState.offlineFinishCalls, 0);

	FakeState neuralState;
	Pipeline neural(factoryFor(neuralState));
	QVERIFY(neural.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	QVERIFY(neural.prepareOfflineFrame());
	QCOMPARE(neuralState.offlinePrepareCalls, 1);
	QVERIFY(neural.finishOfflineProcessing());
	QCOMPARE(neuralState.offlineFinishCalls, 1);
	QVERIFY(!neural.fallbackActive());

	neuralState.offlinePrepareResult = false;
	QVERIFY(!neural.prepareOfflineFrame());
	QCOMPARE(neuralState.offlinePrepareCalls, 2);
	QVERIFY(neural.fallbackActive());
	QCOMPARE(enumValue(neural.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorNotReady));
}

void TestInputEnhancement::neuralRecipeWarmupRunsDuringConfigureAndResetsState() {
	FakeState state;
	state.applyReportedLatency = true;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	QCOMPARE(state.processCalls,
			 static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames));
	QCOMPARE(state.resetCalls, 2);
	QCOMPARE(state.processCallsAtLastReset,
			 static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames));
	QCOMPARE(state.coldProcessCalls, 2);
	QCOMPARE(pipeline.diagnostics().processedFrames(), std::uint64_t{ 0 });
	QCOMPARE(pipeline.diagnostics().neuralFrames(), std::uint64_t{ 0 });

	std::array< float, frameSamples > frame;
	frame.fill(0.25f);
	QVERIFY(pipeline.processFrame(frame));
	QVERIFY(std::all_of(frame.cbegin(), frame.cend(), [](float sample) { return sample == 0.0f; }));
	QCOMPARE(state.coldProcessCalls, 3);
}

void TestInputEnhancement::neuralRealtimePreparationRunsAfterFinalReset() {
	FakeState state;
	state.latency             = frameSamples * 4;
	state.latencyAfterPrepare = crispLatencyBudgetSamples;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Crisp))));

	const int expectedPreparationPoint =
		static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames);
	QCOMPARE(state.resetCalls, 2);
	QCOMPARE(state.prepareCalls, 1);
	QCOMPARE(state.processCallsAtLastReset, expectedPreparationPoint);
	QCOMPARE(state.processCallsAtPrepare, expectedPreparationPoint);
	QVERIFY(state.realtimePrepared);
	QCOMPARE(pipeline.latencySamples(), crispLatencyBudgetSamples);
	QCOMPARE(pipeline.diagnostics().actualLatencySamples(), crispLatencyBudgetSamples);
}

void TestInputEnhancement::realtimePreparationFailureFailsClosed() {
	FakeState state;
	state.prepareResult = false;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(!pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Crisp))));

	QCOMPARE(state.prepareCalls, 1);
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorNotReady));
	QVERIFY(pipeline.fallbackActive());

	state                     = FakeState{};
	state.latency             = frameSamples * 4;
	state.latencyAfterPrepare = crispLatencyBudgetSamples + 1;
	Pipeline excessiveLatency(factoryFor(state));
	QVERIFY(!excessiveLatency.configure(RecipeCatalog::resolve(requestFor(Profile::Crisp))));
	QCOMPARE(enumValue(excessiveLatency.diagnostics().fallbackReason()),
			 enumValue(FallbackReason::LatencyBudgetExceeded));
}

void TestInputEnhancement::neuralRecipeWarmupFailureFailsClosed() {
	const Recipe recipe = RecipeCatalog::resolve(requestFor(Profile::Balanced));
	FakeState state;
	state.throwOnProcess = true;
	Pipeline throwing(factoryFor(state));
	QVERIFY(!throwing.configure(recipe));
	QCOMPARE(enumValue(throwing.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorException));
	QCOMPARE(throwing.diagnostics().processedFrames(), std::uint64_t{ 0 });

	state               = FakeState{};
	state.emitNonFinite = true;
	Pipeline invalid(factoryFor(state));
	QVERIFY(!invalid.configure(recipe));
	QCOMPARE(enumValue(invalid.diagnostics().fallbackReason()), enumValue(FallbackReason::InvalidOutput));
}

void TestInputEnhancement::liveMixOverrideUsesConfiguredProcessorWithoutReconfigure() {
	FakeState state;
	Pipeline pipeline(factoryFor(state), [] { return std::uint64_t{ 100 }; });
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	const int factoryCallsAfterWarmup       = state.factoryCalls;
	std::array< float, frameSamples > frame = {};
	QVERIFY(pipeline.processFrame(frame, 0.37f));
	QCOMPARE(state.factoryCalls, factoryCallsAfterWarmup);
	QVERIFY(std::abs(state.lastMix - 0.37f) < 0.00001f);
	QCOMPARE(state.processCalls
				 - static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames),
			 1);
}

void TestInputEnhancement::preparationFailuresFailClosed() {
	const Recipe recipe = RecipeCatalog::resolve(requestFor(Profile::Balanced));
	FakeState state;
	Pipeline pipeline(factoryFor(state));

	state.returnNull = true;
	QVERIFY(!pipeline.configure(recipe));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorUnavailable));

	state.returnNull = false;
	state.ready      = false;
	QVERIFY(!pipeline.configure(recipe));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorNotReady));

	state.ready        = true;
	state.usedFallback = true;
	QVERIFY(!pipeline.configure(recipe));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorFallback));

	state.usedFallback  = false;
	state.activeModelId = QStringLiteral("rnnoise:wrong");
	QVERIFY(!pipeline.configure(recipe));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::UnexpectedModel));

	state.activeModelId = recipe.modelId();
	state.latency       = balancedLatencyBudgetSamples + 1;
	QVERIFY(!pipeline.configure(recipe));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::LatencyBudgetExceeded));
}

void TestInputEnhancement::runtimeUnhealthyProcessorFailsClosed() {
	FakeState state;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	state.failAfterPrepare = true;

	std::array< float, frameSamples > frame;
	frame.fill(0.25f);
	QVERIFY(!pipeline.processFrame(frame));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorNotReady));
	QVERIFY(pipeline.fallbackActive());
	QVERIFY(pipeline.alignedFallbackActive());
	QVERIFY(std::all_of(frame.cbegin(), frame.cend(), [](float sample) { return sample == 0.0f; }));
}

void TestInputEnhancement::neuralInputIsSanitizedAndOutputIsClamped() {
	FakeState state;
	Pipeline pipeline(factoryFor(state), [] { return std::uint64_t{ 100 }; });
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	state.emitOutOfRange = true;

	std::array< float, frameSamples > frame = {};
	frame[0]                                = std::numeric_limits< float >::quiet_NaN();
	frame[1]                                = std::numeric_limits< float >::infinity();
	frame[2]                                = 2.0f;
	frame[3]                                = -2.0f;
	QVERIFY(pipeline.processFrame(frame));
	QVERIFY(state.inputWasFiniteAndClamped);
	QCOMPARE(frame[0], 1.0f);
	QCOMPARE(frame[1], -1.0f);

	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.sanitizedInputSamples(), std::uint64_t{ 2 });
	QCOMPARE(diagnostics.clampedInputSamples(), std::uint64_t{ 2 });
	QCOMPARE(diagnostics.clampedOutputSamples(), std::uint64_t{ 2 });
}

void TestInputEnhancement::invalidOutputPermanentlyFallsBackToOriginal() {
	FakeState state;
	state.latency              = frameSamples * 2;
	state.applyReportedLatency = true;
	Pipeline pipeline(factoryFor(state), [] { return std::uint64_t{ 100 }; });
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));

	std::array< float, frameSamples > first;
	std::fill(first.begin(), first.end(), 0.25f);
	QVERIFY(pipeline.processFrame(first));
	QVERIFY(std::all_of(first.cbegin(), first.cend(), [](float sample) { return sample == 0.0f; }));

	state.emitNonFinite = true;
	std::array< float, frameSamples > second;
	std::fill(second.begin(), second.end(), 0.5f);
	QVERIFY(!pipeline.processFrame(second));
	QVERIFY(std::all_of(second.cbegin(), second.cend(), [](float sample) { return sample == 0.0f; }));
	QVERIFY(pipeline.fallbackActive());
	QVERIFY(pipeline.alignedFallbackActive());
	QCOMPARE(pipeline.latencySamples(), frameSamples * 2);
	QCOMPARE(enumValue(pipeline.diagnostics().activeProfile()), enumValue(Profile::Original));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::InvalidOutput));

	// The failure latches an aligned dry path. Draining exactly the reported
	// latency recovers both queued input frames in order; the current failure
	// frame is never substituted on the zero-latency timeline.
	state.emitNonFinite                    = false;
	std::array< float, frameSamples > tail = {};
	QVERIFY(!pipeline.processFrame(tail));
	QVERIFY(std::all_of(tail.cbegin(), tail.cend(), [](float sample) { return sample == 0.25f; }));
	tail.fill(0.0f);
	QVERIFY(!pipeline.processFrame(tail));
	QVERIFY(std::all_of(tail.cbegin(), tail.cend(), [](float sample) { return sample == 0.5f; }));
	QCOMPARE(state.processCalls
				 - static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames),
			 2);
}

void TestInputEnhancement::processorExceptionPermanentlyFallsBackToOriginal() {
	FakeState state;
	Pipeline pipeline(factoryFor(state), [] { return std::uint64_t{ 100 }; });
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	state.throwOnProcess                    = true;
	std::array< float, frameSamples > frame = {};
	QVERIFY(!pipeline.processFrame(frame));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::ProcessorException));
}

void TestInputEnhancement::deadlineMissIsMeasuredAndFailsClosed() {
	FakeState state;
	std::uint64_t now = 100;
	Pipeline pipeline(
		factoryFor(state),
		[&now] {
			const std::uint64_t current = now;
			now += 101;
			return current;
		},
		100);
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	std::array< float, frameSamples > frame = {};
	QVERIFY(!pipeline.processFrame(frame));
	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.deadlineMisses(), std::uint64_t{ 1 });
	QCOMPARE(diagnostics.totalProcessingNanoseconds(), std::uint64_t{ 101 });
	QCOMPARE(diagnostics.maximumProcessingNanoseconds(), std::uint64_t{ 101 });
	QCOMPARE(diagnostics.processingP50Nanoseconds(), std::uint64_t{ 10'000 });
	QCOMPARE(diagnostics.processingP95Nanoseconds(), std::uint64_t{ 10'000 });
	QCOMPARE(diagnostics.processingP99Nanoseconds(), std::uint64_t{ 10'000 });
	QCOMPARE(enumValue(diagnostics.fallbackReason()), enumValue(FallbackReason::DeadlineExceeded));
}

void TestInputEnhancement::callbackPercentilesIgnoreSingleSubDeadlineOutlier() {
	FakeState state;
	std::uint64_t now         = 1'000'000;
	unsigned int clockCalls   = 0;
	unsigned int outlierCount = 1;
	Pipeline pipeline(
		factoryFor(state),
		[&now, &clockCalls, &outlierCount] {
			const bool isStart            = (clockCalls % 2) == 0;
			const unsigned int frameIndex = clockCalls / 2;
			++clockCalls;
			if (isStart) {
				return now;
			}
			const std::uint64_t duration = frameIndex >= 100 - outlierCount ? 7'000'000 : 200'000;
			now += duration;
			return now;
		},
		10'000'000);
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	std::array< float, frameSamples > frame = {};
	for (unsigned int index = 0; index < 100; ++index) {
		QVERIFY(pipeline.processFrame(frame));
	}

	const Diagnostics diagnostics = pipeline.diagnostics();
	QCOMPARE(diagnostics.neuralFrames(), std::uint64_t{ 100 });
	QCOMPARE(diagnostics.deadlineMisses(), std::uint64_t{ 0 });
	QCOMPARE(diagnostics.totalProcessingNanoseconds(), std::uint64_t{ 26'800'000 });
	QCOMPARE(diagnostics.maximumProcessingNanoseconds(), std::uint64_t{ 7'000'000 });
	QCOMPARE(diagnostics.processingP50Nanoseconds(), std::uint64_t{ 200'000 });
	QCOMPARE(diagnostics.processingP95Nanoseconds(), std::uint64_t{ 200'000 });
	QCOMPARE(diagnostics.processingP99Nanoseconds(), std::uint64_t{ 200'000 });

	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	clockCalls   = 0;
	outlierCount = 2;
	for (unsigned int index = 0; index < 100; ++index) {
		QVERIFY(pipeline.processFrame(frame));
	}
	const Diagnostics inclusiveDiagnostics = pipeline.diagnostics();
	QCOMPARE(inclusiveDiagnostics.processingP95Nanoseconds(), std::uint64_t{ 200'000 });
	QCOMPARE(inclusiveDiagnostics.processingP99Nanoseconds(), std::uint64_t{ 7'000'000 });
	QCOMPARE(inclusiveDiagnostics.maximumProcessingNanoseconds(), std::uint64_t{ 7'000'000 });

	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	const Diagnostics resetDiagnostics = pipeline.diagnostics();
	QCOMPARE(resetDiagnostics.neuralFrames(), std::uint64_t{ 0 });
	QCOMPARE(resetDiagnostics.processingP50Nanoseconds(), std::uint64_t{ 0 });
	QCOMPARE(resetDiagnostics.processingP95Nanoseconds(), std::uint64_t{ 0 });
	QCOMPARE(resetDiagnostics.processingP99Nanoseconds(), std::uint64_t{ 0 });
}

void TestInputEnhancement::invalidFrameSizeFailsClosed() {
	FakeState state;
	Pipeline pipeline(factoryFor(state));
	QVERIFY(pipeline.configure(RecipeCatalog::resolve(requestFor(Profile::Balanced))));
	std::array< float, 32 > frame = {};
	QVERIFY(!pipeline.processFrame(frame.data(), static_cast< unsigned int >(frame.size())));
	QCOMPARE(enumValue(pipeline.diagnostics().fallbackReason()), enumValue(FallbackReason::InvalidFrame));
	QCOMPARE(state.processCalls
				 - static_cast< int >(Pipeline::processorWarmupFrames + Pipeline::processorPostResetProbeFrames),
			 0);
}

void TestInputEnhancement::deepFilterWorkerEmitsAfterTwoFrames() {
	RealtimeWorkerState state;
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output;
	input.fill(0.25f);
	output.fill(1.0f);
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QVERIFY(std::all_of(output.cbegin(), output.cend(), [](float sample) { return sample == 0.0f; }));
	QTRY_VERIFY_WITH_TIMEOUT(state.processCalls.load(std::memory_order_acquire) >= 1, 1000);

	input.fill(0.5f);
	output.fill(1.0f);
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QVERIFY(std::all_of(output.cbegin(), output.cend(), [](float sample) { return sample == 0.0f; }));
	QTRY_VERIFY_WITH_TIMEOUT(state.processCalls.load(std::memory_order_acquire) >= 2, 1000);

	input.fill(0.75f);
	output.fill(0.0f);
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QVERIFY(
		std::all_of(output.cbegin(), output.cend(), [](float sample) { return std::abs(sample - 0.375f) < 0.00001f; }));
	QCOMPARE(worker.failureStatus(), DeepFilterNetRealtimeWorker::statusOk);
	worker.stop();
}

void TestInputEnhancement::deepFilterWorkerOfflineDriveWaitsForRequiredFrame() {
	RealtimeWorkerState state;
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input  = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output = {};
	for (unsigned int frameIndex = 0; frameIndex < 16; ++frameIndex) {
		QVERIFY(worker.prepareOfflineFrame(std::chrono::milliseconds{ 1000 }));
		const float inputValue = static_cast< float >(frameIndex + 1) / 100.0f;
		input.fill(inputValue);
		QVERIFY(worker.processFrame(input.data(), output.data()));
		const float expected =
			frameIndex < DeepFilterNetRealtimeWorker::schedulingDelayFrames
				? 0.0f
				: static_cast< float >(frameIndex - DeepFilterNetRealtimeWorker::schedulingDelayFrames + 1) / 100.0f
					  + state.offset;
		QVERIFY(std::all_of(output.cbegin(), output.cend(),
							[expected](float sample) { return std::abs(sample - expected) < 0.00001f; }));
	}

	QCOMPARE(worker.failureStatus(), DeepFilterNetRealtimeWorker::statusOk);
	QVERIFY(worker.finishOfflineProcessing(std::chrono::milliseconds{ 1000 }));
	QCOMPARE(worker.completedFrames(), std::uint64_t{ 16 });
	worker.stop();
}

void TestInputEnhancement::deepFilterWorkerWakeDoesNotPollAtWindowsTimerGranularity() {
	RealtimeWorkerState state;
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input  = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output = {};
	constexpr unsigned int frameCount                                     = 64;
	const auto startedAt                                                  = std::chrono::steady_clock::now();
	for (unsigned int frame = 0; frame < frameCount; ++frame) {
		QVERIFY(worker.prepareOfflineFrame(std::chrono::milliseconds{ 1000 }));
		QVERIFY(worker.processFrame(input.data(), output.data()));
	}
	QVERIFY(worker.finishOfflineProcessing(std::chrono::milliseconds{ 1000 }));
	const auto elapsed =
		std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::steady_clock::now() - startedAt);

	// On default-resolution Windows, sleep_for(100 us) rounds to roughly
	// 15.6 ms. A poll per frame therefore takes close to one second here even
	// though the fake inference is only a bounded copy. Atomic notification is
	// expected to retain a wide margin under ordinary CI contention.
	QVERIFY2(
		elapsed < std::chrono::milliseconds{ 750 },
		qPrintable(QStringLiteral("64 notified frames took %1 ms; the worker may be polling").arg(elapsed.count())));
	QCOMPARE(worker.processingFrames(), std::uint64_t{ frameCount });
	QVERIFY(worker.lastProcessingNanoseconds() > 0);
	QVERIFY(worker.totalProcessingNanoseconds() > 0);
	QVERIFY(worker.maximumProcessingNanoseconds() > 0);
	QVERIFY(worker.totalProcessingNanoseconds() >= worker.maximumProcessingNanoseconds());
	QVERIFY(worker.processingP99Nanoseconds() > 0);
	QCOMPARE(worker.pendingFrames(), 0u);
	QCOMPARE(worker.schedulingSlackFrames(), DeepFilterNetRealtimeWorker::schedulingDelayFrames);
	worker.stop();
}

void TestInputEnhancement::deepFilterWorkerOfflineWaitTimeoutLatches() {
	RealtimeWorkerState state;
	state.releaseProcess.store(false, std::memory_order_release);
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input  = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output = {};
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QTRY_VERIFY_WITH_TIMEOUT(state.enteredCalls.load(std::memory_order_acquire) >= 1, 1000);

	const bool prepared     = worker.prepareOfflineFrame(std::chrono::milliseconds::zero());
	const int failureStatus = worker.failureStatus();
	state.releaseProcess.store(true, std::memory_order_release);
	worker.stop();

	QVERIFY(!prepared);
	QCOMPARE(failureStatus, DeepFilterNetRealtimeWorker::statusOfflineWaitTimeout);
	QVERIFY(!worker.healthy());
}

void TestInputEnhancement::deepFilterWorkerLateResultFailsClosed() {
	RealtimeWorkerState state;
	state.releaseProcess.store(false, std::memory_order_release);
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input  = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output = {};
	const bool firstResult    = worker.processFrame(input.data(), output.data());
	const bool secondResult   = worker.processFrame(input.data(), output.data());
	const bool deadlineResult = worker.processFrame(input.data(), output.data());
	state.releaseProcess.store(true, std::memory_order_release);
	worker.stop();

	QVERIFY(firstResult);
	QVERIFY(secondResult);
	QVERIFY(!deadlineResult);
	QCOMPARE(worker.failureStatus(), DeepFilterNetRealtimeWorker::statusOutputNotReady);
	QVERIFY(std::all_of(output.cbegin(), output.cend(), [](float sample) { return sample == 0.0f; }));
}

void TestInputEnhancement::deepFilterWorkerBackendFailureLatches() {
	RealtimeWorkerState state;
	state.returnStatus.store(backendProcessError, std::memory_order_release);
	DeepFilterNetRealtimeWorker worker;
	QVERIFY(worker.start(&state, &processRealtimeWorkerFrame));

	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > input  = {};
	std::array< float, DeepFilterNetRealtimeWorker::frameSamples > output = {};
	QVERIFY(worker.processFrame(input.data(), output.data()));
	QTRY_COMPARE_WITH_TIMEOUT(worker.failureStatus(), backendProcessError, 1000);
	QVERIFY(!worker.prepareOfflineFrame(std::chrono::milliseconds{ 100 }));
	QVERIFY(!worker.processFrame(input.data(), output.data()));
	QVERIFY(!worker.healthy());
	worker.stop();
}

QTEST_GUILESS_MAIN(TestInputEnhancement)

#include "TestInputEnhancement.moc"
